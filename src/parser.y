%{
/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric Parser
 *
 * (C) 1998-2002 GNOME Foundation
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jody Goldberg (jody@gnome.org)
 *    Morten Welinder (terra@diku.dk)
 *    Almer S. Tigelaar (almer@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "number-match.h"
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "workbook.h"
#include "sheet.h"
#include "format.h"
#include "application.h"
#include "parse-util.h"
#include "gutils.h"
#include "style.h"
#include "value.h"
#include "str.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#define YYDEBUG 1

/* ------------------------------------------------------------------------- */
/* Allocation with disposal-on-error */

/*
 * Defined: the stack itself will be kept in use.  This isn't much, btw.
 *   This setting is good for speed.
 *
 * Not defined: memory will be freed.  The is good for finding leaks in the
 * program.  (Here and elsewhere.)
 */
#define KEEP_DEALLOCATION_STACK_BETWEEN_CALLS

/*
 * If some dork enters "=1+2+2*(1+" we have already allocated space for
 * "1+2", "2", and "1" before the parser sees the syntax error and warps
 * us to the error production in the "line" non-terminal.
 *
 * To make sure we can clean up, we register every allocation.  On success,
 * nothing should be left (except the final expression which is unregistered),
 * but on failure we must free everything allocated.
 *
 * Note: there is some room left for optimisation here.  Talk to terra@diku.dk
 * before you set out to do it.
 */

static void
free_expr_list_list (GSList *list)
{
	GSList *l;
	for (l = list; l; l = l->next)
		gnm_expr_list_unref (l->data);
	g_slist_free (list);
}

typedef void (*ParseDeallocator) (void *);
static GPtrArray *deallocate_stack;

static void
deallocate_init (void)
{
	deallocate_stack = g_ptr_array_new ();
}

static void
deallocate_uninit (void)
{
#ifndef KEEP_DEALLOCATION_STACK_BETWEEN_CALLS
	g_ptr_array_free (deallocate_stack, TRUE);
	deallocate_stack = NULL;
#endif
}

static void
deallocate_all (void)
{
	int i;

	for (i = 0; i < (int)deallocate_stack->len; i += 2) {
		ParseDeallocator freer = g_ptr_array_index (deallocate_stack, i + 1);
		freer (g_ptr_array_index (deallocate_stack, i));
	}

	g_ptr_array_set_size (deallocate_stack, 0);
}

static void
deallocate_assert_empty (void)
{
	if (deallocate_stack->len == 0)
		return;

	g_warning ("deallocate_stack not empty as expected.");
	deallocate_all ();
}

static void *
register_allocation (gpointer data, ParseDeallocator freer)
{
	/* It's handy to be able to register and unregister NULLs.  */
	if (data) {
		int len;
		/*
		 * There are really only a few different freers, so we
		 * could encode the freer in the lower bits of the data
		 * pointer.  Unfortunately, no-one can predict how high
		 * Miguel would jump when he found out.
		 */
		len = deallocate_stack->len;
		g_ptr_array_set_size (deallocate_stack, len + 2);
		g_ptr_array_index (deallocate_stack, len) = data;
		g_ptr_array_index (deallocate_stack, len + 1) = freer;
	}

	/* Returning the pointer here improved readability of the caller.  */
	return data;
}

#define register_expr_allocation(expr) \
  register_allocation ((gpointer)(expr), (ParseDeallocator)&gnm_expr_unref)

#define register_expr_list_allocation(list) \
  register_allocation ((list), (ParseDeallocator)&gnm_expr_list_unref)

#define register_expr_list_list_allocation(list) \
  register_allocation ((list), (ParseDeallocator)&free_expr_list_list)

static void
unregister_allocation (void const *data)
{
	int pos;

	/* It's handy to be able to register and unregister NULLs.  */
	if (!data)
		return;

	pos = deallocate_stack->len - 2;
	if (pos >= 0 && data == g_ptr_array_index (deallocate_stack, pos)) {
		g_ptr_array_set_size (deallocate_stack, pos);
		return;
	}

	/*
	 * Bummer.  In certain error cases, it is possible that the parser
	 * will reduce after it has discovered a token that will lead to an
	 * error.  "2/16/1800 00:00" (without the quotes) is an example.
	 * The first "00" is registered before the second division is
	 * reduced.
	 *
	 * This isn't a big deal -- we will just look at the entry just below
	 * the top.
	 */
	pos -= 2;
	if (pos >= 0 && data == g_ptr_array_index (deallocate_stack, pos)) {
		g_ptr_array_index (deallocate_stack, pos) =
			g_ptr_array_index (deallocate_stack, pos + 2);
		g_ptr_array_index (deallocate_stack, pos + 1) =
			g_ptr_array_index (deallocate_stack, pos + 3);

		g_ptr_array_set_size (deallocate_stack, pos + 2);
		return;
	}

	g_warning ("Unbalanced allocation registration");
}

/* ------------------------------------------------------------------------- */

/* Bison/Yacc internals */
static int yylex (void);
static int yyerror (const char *s);

typedef struct {
	char const *ptr;	/* current position of the lexer */
	char const *start;	/* start of the expression */

	/* Location where the parsing is taking place */
	ParsePos const *pos;

	/* Locale info. */
	gunichar decimal_point;
	gunichar separator;
	gunichar array_col_separator;

	/* flags */
	gboolean force_absolute_col_references;
	gboolean force_absolute_row_references;
	gboolean force_explicit_sheet_references;
	gboolean unknown_names_are_strings;

	GnmExprConventions *convs;

	GnmExprList *result;

	ParseError *error;
} ParserState;

/* The error returned from the */
static ParserState *state;

static void
report_err (ParserState *state, GError *err,
	    char const *last, int guesstimate_of_length)
{
	if (state->error != NULL) {
		state->error->err    	 = err;
		state->error->end_char   = last - state->start;
		state->error->begin_char = state->error->end_char - guesstimate_of_length;
		if (state->error->begin_char < 0)
			state->error->begin_char = 0;
	} else
		g_error_free (err);
}

static GnmExpr *
fold_negative (GnmExpr *expr)
{
	if (expr->any.oper == GNM_EXPR_OP_CONSTANT) {
		Value const *v = expr->constant.value;

		if (v->type == VALUE_INTEGER)
			((Value *)v)->v_int.val   = -v->v_int.val;
		else if (v->type == VALUE_FLOAT)
			((Value *)v)->v_float.val = -v->v_float.val;
		else
			return NULL;
		return expr;
	} else
		return NULL;
}

static GnmExpr *
build_unary_op (GnmExprOp op, GnmExpr *expr)
{
	unregister_allocation (expr);
	return register_expr_allocation (gnm_expr_new_unary (op, expr));
}

static GnmExpr *
build_binop (GnmExpr *l, GnmExprOp op, GnmExpr *r)
{
	unregister_allocation (r);
	unregister_allocation (l);
	return register_expr_allocation (gnm_expr_new_binary (l, op, r));
}

static GnmExpr *
build_logical (GnmExpr *l, gboolean is_and, GnmExpr *r)
{
	static GnmFunc *and_func = NULL, *or_func = NULL;

	if (and_func == NULL)
		and_func = gnm_func_lookup ("AND", NULL);
	if (or_func == NULL)
		or_func = gnm_func_lookup ("OR", NULL);

	unregister_allocation (r);
	unregister_allocation (l);
	return register_expr_allocation (gnm_expr_new_funcall (is_and ? and_func : or_func,
		    g_slist_prepend (g_slist_prepend (NULL, l), r)));
}

static GnmExpr *
build_not (GnmExpr *expr)
{
	static GnmFunc *not_func = NULL;
	if (not_func == NULL)
		not_func = gnm_func_lookup ("NOT", NULL);
	unregister_allocation (expr);
	return register_expr_allocation (gnm_expr_new_funcall (not_func,
		    g_slist_prepend (NULL, expr)));
}

static GnmExpr *
build_array (GSList *cols)
{
	Value *array;
	GSList *row;
	int x, mx, y;

	if (!cols) {
		/* parser_error = PARSE_ERR_SYNTAX; */
		return NULL;
	}

	mx  = 0;
	row = cols->data;
	while (row) {
		mx++;
		row = row->next;
	}

	array = value_new_array_empty (mx, g_slist_length (cols));

	y = 0;
	while (cols) {
		row = cols->data;
		x = 0;
		while (row && x < mx) {
			GnmExpr    *expr = row->data;
			Value const *v = expr->constant.value;

			g_assert (expr->any.oper == GNM_EXPR_OP_CONSTANT);

			value_array_set (array, x, y, value_duplicate (v));

			x++;
			row = row->next;
		}
		if (x < mx || row) {
			/* parser_error = PARSE_ERR_SYNTAX; */
			value_release (array);
			return NULL;
		}
		y++;
		cols = cols->next;
	}

	return register_expr_allocation (gnm_expr_new_constant (array));
}

static GnmExpr *
build_range_ctor (GnmExpr *l, GnmExpr *r, GnmExpr *validate)
{
	if (validate != NULL) {
		if (validate->any.oper != GNM_EXPR_OP_CELLREF ||
		    validate->cellref.ref.sheet != NULL) {
			report_err (state, g_error_new (1, PERR_UNEXPECTED_TOKEN,
				_("Constructed ranges use simple references")),
				state->ptr, 0);
			return NULL;
		    }
	}
	return build_binop (l, GNM_EXPR_OP_RANGE_CTOR, r);
}

static GnmExpr *
build_intersect (GnmExpr *l, GnmExpr *r)
{
	if (gnm_expr_is_rangeref (l) && gnm_expr_is_rangeref (r))
		return build_binop (l, GNM_EXPR_OP_INTERSECT, r);
	report_err (state, g_error_new (1, PERR_SET_CONTENT_MUST_BE_RANGE,
		_("All entries in the set must be references")),
		state->ptr, 0);
	return NULL;
}

static GnmExpr *
build_set (GnmExprList *list)
{
	/* verify that every thing is a ref */
	GnmExprList *ptr;
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		if (!gnm_expr_is_rangeref (ptr->data)) {
			report_err (state, g_error_new (1, PERR_SET_CONTENT_MUST_BE_RANGE,
				_("All entries in the set must be references")),
				state->ptr, 0);
			return NULL;
		}

	return register_expr_allocation (gnm_expr_new_set (list));
}

/**
 * parse_string_as_value :
 *
 * Try to parse the entered text as a basic value (empty, bool, int,
 * float, err) if this succeeds, we store this as a Value otherwise, we
 * return a string.
 */
static GnmExpr *
parse_string_as_value (GnmExpr *str)
{
	Value *v = format_match_simple (str->constant.value->v_str.val->str);

	if (v != NULL) {
		unregister_allocation (str);
		gnm_expr_unref (str);
		return register_expr_allocation (gnm_expr_new_constant (v));
	}
	return str;
}

/**
 * parser_simple_val_or_name :
 * @str : An expression with oper constant, whose value is a string.
 *
 * Check to see if a string is a simple value or failing that a named
 * expression, if it is not create a placeholder name for it.
 */
static GnmExpr *
parser_simple_val_or_name (GnmExpr *str_expr)
{
	GnmExpr const	*res;
	char const	*str = str_expr->constant.value->v_str.val->str;
	Value		*v   = format_match_simple (str);

	/* if it is not a simple value see if it is a name */
	if (v == NULL) {
		GnmNamedExpr *nexpr = expr_name_lookup (state->pos, str);
		if (nexpr == NULL) {
			if (state->unknown_names_are_strings) {
				res = gnm_expr_new_constant (value_new_string (str));
			} else {
				ParsePos pp = *state->pos;
				pp.sheet = NULL;
				/* Create a place holder */
				nexpr = expr_name_add (&pp, str, NULL, NULL, TRUE);
				res = gnm_expr_new_name (nexpr, NULL, NULL);
			}
		} else
			res = gnm_expr_new_name (nexpr, NULL, NULL);
	} else
		res = gnm_expr_new_constant (v);

	unregister_allocation (str_expr);
	gnm_expr_unref (str_expr);
	return register_expr_allocation (res);
}

static Sheet *
parser_sheet_by_name (Workbook *wb, GnmExpr *name_expr)
{
	char const *name = name_expr->constant.value->v_str.val->str;
	Sheet *sheet = NULL;

	if (wb == NULL)
		return NULL;

	sheet = workbook_sheet_by_name (wb, name);

	/* Applix has absolute and relative sheet references */
	if (sheet == NULL && *name == '$' &&
	    state->convs->allow_absolute_sheet_references)
		sheet = workbook_sheet_by_name (wb, name + 1);

	if (sheet == NULL)
		/* TODO : length is broken in the context of quoted names or
		 * names with escaped character */
		/* -1 is a kludge.  We know that this routine is only called
		 * when the last token was SHEET_SEP
		 */
		report_err (state, g_error_new (1, PERR_UNKNOWN_SHEET,
			_("Unknown sheet '%s'"), name),
			state->ptr-1, strlen (name));

	return sheet;
}

/* Make byacc happier */
int yyparse (void);

%}

%union {
	GnmExpr		*expr;
	Value		*value;
	CellRef		*cell;
	GnmExprList	*list;
	Sheet		*sheet;
	Workbook	*wb;
}
%type  <list>	opt_exp arg_list array_row array_cols
%type  <expr>	exp array_exp function string_opt_quote cellref
%token <expr>	STRING QUOTED_STRING CONSTANT RANGEREF GTE LTE NE AND OR NOT INTERSECT
%token		SEPARATOR SHEET_SEP INVALID_TOKEN
%type  <sheet>	sheetref
%type  <wb>	workbookref

%left '<' '>' '=' GTE LTE NE
%left '&'
%left '-' '+'
%left '*' '/'
%right '^'
%nonassoc '%'
%nonassoc NEG PLUS NOT
%left AND OR
%left ','
%left ' '
%left RANGE_SEP

%%
line:	opt_exp exp {
		unregister_allocation ($2);
		unregister_allocation ($1);
		state->result = gnm_expr_list_prepend ($1, $2);
	}

	| error 	{
		if (state->result != NULL) {
			gnm_expr_list_unref (state->result);
			state->result = NULL;
		}
	}
	;

opt_exp : opt_exp exp  SEPARATOR {
	       unregister_allocation ($2);
	       unregister_allocation ($1);
	       $$ = gnm_expr_list_prepend ($1, $2);
	       register_expr_list_allocation ($$);
	}
	| { $$ = NULL; register_expr_list_allocation ($$); }
	;

exp:	  CONSTANT 	{ $$ = $1; }
	| QUOTED_STRING { $$ = $1; }
	| STRING        { $$ = parser_simple_val_or_name ($1); }
        | cellref       { $$ = $1; }
	| exp '+' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_ADD,	$3); }
	| exp '-' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_SUB,	$3); }
	| exp '*' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_MULT,	$3); }
	| exp '/' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_DIV,	$3); }
	| exp '^' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_EXP,	$3); }
	| exp '&' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_CAT,	$3); }
	| exp '=' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_EQUAL,	$3); }
	| exp '<' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_LT,		$3); }
	| exp '>' exp	{ $$ = build_binop ($1, GNM_EXPR_OP_GT,		$3); }
	| exp GTE exp	{ $$ = build_binop ($1, GNM_EXPR_OP_GTE,	$3); }
	| exp NE  exp	{ $$ = build_binop ($1, GNM_EXPR_OP_NOT_EQUAL,	$3); }
	| exp LTE exp	{ $$ = build_binop ($1, GNM_EXPR_OP_LTE,	$3); }
	| exp AND exp	{ $$ = build_logical ($1, TRUE,	$3); }
	| exp OR  exp	{ $$ = build_logical ($1, FALSE, $3); }
	| exp ' ' exp	{
		$$ = build_intersect ($1, $3);
		if ($$ == NULL) { YYERROR; }
	}

        | '-' exp %prec NEG {
		GnmExpr *tmp = fold_negative ($2);
		$$ = (tmp != NULL) ? tmp : build_unary_op (GNM_EXPR_OP_UNARY_NEG, $2);
	}
        | '+' exp %prec PLUS { $$ = build_unary_op (GNM_EXPR_OP_UNARY_PLUS, $2); }
        | NOT exp { $$ = build_not ($2); }
        | exp '%' { $$ = build_unary_op (GNM_EXPR_OP_PERCENTAGE, $1); }

	| '(' arg_list ')' {
		if ($2 == NULL) {
			report_err (state, g_error_new (1, PERR_INVALID_EMPTY,
				_("() is an invalid expression")),
				state->ptr-2, 2);
			YYERROR;
		} else {
			unregister_allocation ($2);
			if ($2->next == NULL) {
				$$ = register_expr_allocation ($2->data);
				/* NOTE : free list not content */
				gnm_expr_list_free ($2);
			} else {
				$$ = build_set ($2);
				if ($$ == NULL) {
					YYERROR;
				}
			}
		}
	}
        | '{' array_cols '}' {
		unregister_allocation ($2);
		$$ = build_array ($2);
		free_expr_list_list ($2);
	}

	| function
	| sheetref STRING {
		GnmNamedExpr *nexpr = NULL;
		char const *name = $2->constant.value->v_str.val->str;
		ParsePos pos = *state->pos;

		pos.sheet = $1;
		nexpr = expr_name_lookup (&pos, name);
		if (nexpr == NULL) {
			report_err (state, g_error_new (1, PERR_UNKNOWN_NAME,
				_("Name '%s' does not exist in sheet '%s'"),
						name, pos.sheet->name_quoted),
				state->ptr, strlen (name));
			YYERROR;
		} else {
			unregister_allocation ($2); gnm_expr_unref ($2);
			$$ = register_expr_allocation (gnm_expr_new_name (nexpr, $1, NULL));
		}
	}
	| workbookref STRING {
		GnmNamedExpr *nexpr = NULL;
		char const *name = $2->constant.value->v_str.val->str;
		ParsePos pos = *state->pos;

		pos.sheet = NULL;
		pos.wb = $1;
		nexpr = expr_name_lookup (&pos, name);
		if (nexpr != NULL) {
			unregister_allocation ($2); gnm_expr_unref ($2);
			$$ = register_expr_allocation (gnm_expr_new_name (nexpr, NULL, $1));
		} else {
			report_err (state, g_error_new (1, PERR_UNKNOWN_NAME,
				_("Name '%s' does not exist in sheet '%s'"),
						name, pos.sheet->name_quoted),
				state->ptr, strlen (name));
			YYERROR;
		}
	}
	;

function : STRING '(' arg_list ')' {
		char const *name = $1->constant.value->v_str.val->str;
		GnmFunc *f;
		GnmExpr const *f_call = NULL;
		GnmParseFunctionHandler h = NULL;

		$$ = NULL;

		if (state->convs->function_rewriter_hash)
			h = g_hash_table_lookup (state->convs->function_rewriter_hash, name);

		if (h) {
			f_call = h (name, $3, state->convs);
		} else if ((f = gnm_func_lookup (name, state->pos->wb))) {
			f_call = gnm_expr_new_funcall (f, $3);
		} else {
			h = state->convs->unknown_function_handler;
			if (h)
				f_call = h (name, $3, state->convs);
		}

		/* We're done with the function name.  */
		unregister_allocation ($1); gnm_expr_unref ($1);
		unregister_allocation ($3);

		if (f_call) {
			$$ = register_expr_allocation (f_call);
		} else {
			YYERROR;
		}
	}
	;

string_opt_quote : STRING
		 | QUOTED_STRING
		 ;

workbookref : '[' string_opt_quote ']'  {
		char const *wb_name = $2->constant.value->v_str.val->str;
		Workbook *wb = application_workbook_get_by_name (wb_name);

		if (wb != NULL) {
			unregister_allocation ($2); gnm_expr_unref ($2);
			$$ = wb;
		} else {
			/* kludge to produce better error messages
			 * we know that the last token read will be the ']'
			 * so subtract 1.
			 */
			report_err (state, g_error_new (1, PERR_UNKNOWN_WORKBOOK,
				_("Unknown workbook '%s'"), wb_name),
				state->ptr - 1, strlen (wb_name));
			YYERROR;
		}
	}
	;

/* does not need to handle 3d case.  this is only used for names.
 * 3d cell references are handled in the lexer
 */
sheetref: string_opt_quote SHEET_SEP {
		Sheet *sheet = parser_sheet_by_name (state->pos->wb, $1);
		if (sheet != NULL) {
			unregister_allocation ($1); gnm_expr_unref ($1);
			$$ = sheet;
		} else {
			YYERROR;
		}
	}
	| workbookref string_opt_quote SHEET_SEP {
		Sheet *sheet = parser_sheet_by_name ($1, $2);

		if (sheet != NULL) {
			unregister_allocation ($2); gnm_expr_unref ($2);
			$$ = sheet;
		} else {
			YYERROR;
		}
        }
	;

cellref:  RANGEREF { $$ = $1; }
	| function RANGE_SEP function { $$ = build_range_ctor ($1, $3, NULL); }
	| RANGEREF RANGE_SEP function {
		$$ = build_range_ctor ($1, $3, $1);
		if ($$ == NULL) { YYERROR; }
	}
	| function RANGE_SEP RANGEREF {
		$$ = build_range_ctor ($1, $3, $3);
		if ($$ == NULL) { YYERROR; }
	}
	;

arg_list: exp {
		unregister_allocation ($1);
		$$ = g_slist_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }
	| exp SEPARATOR arg_list {
		unregister_allocation ($3);
		unregister_allocation ($1);
		$$ = g_slist_prepend ($3, $1);
		register_expr_list_allocation ($$);
	}
	| SEPARATOR arg_list {
		GSList *tmp = $2;
		unregister_allocation ($2);

		if (tmp == NULL)
			tmp = gnm_expr_list_prepend (NULL, gnm_expr_new_constant (value_new_empty ()));

		$$ = gnm_expr_list_prepend (tmp, gnm_expr_new_constant (value_new_empty ()));
		register_expr_list_allocation ($$);
	}
        | { $$ = NULL; }
	;

array_exp:     CONSTANT		{ $$ = $1; }
	 | '-' CONSTANT		{ $$ = fold_negative ($2); }
	 | string_opt_quote	{ $$ = parse_string_as_value ($1); }
	 ;

array_row: array_exp {
		unregister_allocation ($1);
		$$ = g_slist_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }

	/* Some locales use {1\2\3;4\5\6} rather than {1,2,3;4,5,6}
	 * but the lexer will call ',' or ';' SEPARATOR depending on locale
	 * which does not work here.  So we fake it and have _two_ productions
	 * then test the separator at parse time.
	 */
	| array_exp SEPARATOR array_row {
		if (state->array_col_separator == ',') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_slist_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else {
			report_err (state, g_error_new (1, PERR_INVALID_ARRAY_SEPARATOR,
				_("This locale uses '\\' rather than ',' to separate array columns.")),
				state->ptr, 1);
			YYERROR;
		}
	}
	| array_exp '\\' array_row {
		if (state->array_col_separator == '\\') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_slist_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else {
			report_err (state, g_error_new (1, PERR_INVALID_ARRAY_SEPARATOR,
				_("This locale uses ',' rather than '\\' to separate array columns.")),
				state->ptr, 1);
			YYERROR;
		}
	}
        | { $$ = NULL; }
	;

array_cols: array_row {
		unregister_allocation ($1);
		$$ = g_slist_prepend (NULL, $1);
		register_expr_list_list_allocation ($$);
        }
        | array_row ';' array_cols {
		unregister_allocation ($3);
		unregister_allocation ($1);
		$$ = g_slist_prepend ($3, $1);
		register_expr_list_list_allocation ($$);
	}
	;
%%

static char const *
find_matching_close (char const *str, char const **res)
{
	for (; *str; str = g_utf8_next_char (str)) {
		if (*str == '(') {
			char const *tmp = str;
			str = find_matching_close (str + 1, res);
			if (*str != ')' && *res == NULL) {
				*res = tmp;
				return str;
			}
		} else if (*str == ')')
			return str;
		else if (*str == '\'' || *str == '\"') {
			GString *dummy = g_string_new ("");
			const char *end = gnm_strunescape (dummy, str);
			g_string_free (dummy, TRUE);
			if (end)
				str = end;
			else
				return str + strlen (str);
		}
	}

	return str;
}

static int
yylex (void)
{
	gunichar c;
	char const *start, *end;
	RangeRef ref;
	gboolean is_number = FALSE;
	gboolean is_space = FALSE;
	gboolean error_token = FALSE;

        while (g_unichar_isspace (g_utf8_get_char (state->ptr))) {
                state->ptr = g_utf8_next_char (state->ptr);
		is_space = TRUE;
	}
	if (is_space && !state->convs->ignore_whitespace)
		return ' ';

	start = state->ptr;
	c = g_utf8_get_char (start);
	state->ptr = g_utf8_next_char (state->ptr);

	if (c == '(' || c == ')')
		return c;

	if (c == '&' && state->convs->decode_ampersands) {
		if (!strncmp (state->ptr, "amp;", 4)) {
			state->ptr += 4;
			return '&';
		}

		if (!strncmp (state->ptr, "lt;", 3)) {
			state->ptr += 3;
			if (*state->ptr == '='){
				state->ptr++;
				return LTE;
			}
			if (!strncmp (state->ptr, "&gt;", 4)) {
				state->ptr += 4;
				return NE;
			}
			return '<';
		}
		if (!strncmp (state->ptr, "gt;", 3)) {
			state->ptr += 3;
			if (*state->ptr == '='){
				state->ptr++;
				return GTE;
			}
			return '>';
		}
		if (!strncmp (state->ptr, "apos;", 5) ||
		    !strncmp (state->ptr, "quot;", 5)) {
			char const *quotes_end;
			char const *p;
			char *string, *s;
			Value *v;

			if (*state->ptr == 'q') {
				quotes_end = "&quot;";
				c = '\"';
			} else {
				quotes_end = "&apos;";
				c = '\'';
			}

			state->ptr += 5;
			p = state->ptr;
			double_quote_loop :
				state->ptr = strstr (state->ptr, quotes_end);
			if (!*state->ptr) {
				report_err (state, g_error_new (1, PERR_MISSING_CLOSING_QUOTE,
								_("Could not find matching closing quote")),
					    p, 1);
				return INVALID_TOKEN;
			}
			if (!strncmp (state->ptr + 6, quotes_end, 6)) {
				state->ptr += 2 * 6;
				goto double_quote_loop;
			}

			s = string = (char *) g_alloca (1 + state->ptr - p);
			while (p != state->ptr) {
				if (*p == '&') {
					if (!strncmp (p, "&amp;", 5)) {
						p += 5;
						*s++ = '&';
						continue;
					} else if (!strncmp (p, "&lt;", 4)) {
						p += 4;
						*s++ = '<';
						continue;
					} else if (!strncmp (p, "&gt;", 4)) {
						p += 4;
						*s++ = '>';
						continue;
					} else if (!strncmp (p, quotes_end, 6)) {
						p += 12; /* two in a row is the escape mechanism */
						*s++ = c;
						continue;
					} else if (!strncmp (p, "&quot;", 6)) {
						p += 6;
						*s++ = '\"';
						continue;
					} else if (!strncmp (p, "&apos;", 6)) {
						p += 6;
						*s++ = '\'';
						continue;
					}
				}
				*s++ = *p++;
			}

			*s = 0;
			state->ptr += 6;

			v = value_new_string (string);
			yylval.expr = register_expr_allocation (gnm_expr_new_constant (v));
			return QUOTED_STRING;
		}
	}

	if (c == ':' && state->convs->range_sep_colon)
		return RANGE_SEP;

	if (c == '!' && state->convs->sheet_sep_exclamation)
		return SHEET_SEP;

	if (c == '.' && *state->ptr == '.' && state->convs->range_sep_dotdot) {
		state->ptr++;
		return RANGE_SEP;
	}

	if (c == ':' && state->convs->sheet_sep_colon)
		return SHEET_SEP;

	if (c == '#' && state->convs->accept_hash_logicals) {
		if (!strncmp (state->ptr, "NOT#", 4)) {
			state->ptr += 4;
			return NOT;
		}
		if (!strncmp (state->ptr, "AND#", 4)) {
			state->ptr += 4;
			return AND;
		}
		if (!strncmp (state->ptr, "OR#", 3)) {
			state->ptr += 3;
			return OR;
		}
	}

	if (c == state->separator)
		return SEPARATOR;

	if (start != (end = state->convs->ref_parser (&ref, start, state->pos))) {
		state->ptr = end;
		if (state->force_absolute_col_references) {
			if (ref.a.col_relative) {
				ref.a.col += state->pos->eval.col;
				ref.a.col_relative = FALSE;
			}
			if (ref.b.col_relative) {
				ref.b.col += state->pos->eval.col;
				ref.b.col_relative = FALSE;
			}
		}
		if (state->force_absolute_row_references) {
			if (ref.a.row_relative) {
				ref.a.row += state->pos->eval.row;
				ref.a.row_relative = FALSE;
			}
			if (ref.b.row_relative) {
				ref.b.row += state->pos->eval.row;
				ref.b.row_relative = FALSE;
			}
		}

		if (ref.a.sheet == NULL && state->force_explicit_sheet_references) {
			ref.a.sheet = state->pos->sheet;
			if (ref.a.sheet == NULL) {
				report_err (state, g_error_new (1, PERR_SHEET_IS_REQUIRED,
					_("Sheet name is required")),
					state->ptr, 0);
				return INVALID_TOKEN;
			}
		}

		if ((ref.b.sheet == NULL || ref.b.sheet == ref.a.sheet) &&
		    ref.a.col		== ref.b.col &&
		    ref.a.col_relative	== ref.b.col_relative &&
		    ref.a.row		== ref.b.row &&
		    ref.a.row_relative	== ref.b.row_relative) {
			yylval.expr = register_expr_allocation (gnm_expr_new_cellref (&ref.a));
			return RANGEREF;
		}
		yylval.expr = register_expr_allocation (gnm_expr_new_constant (
			 value_new_cellrange_unsafe (&ref.a, &ref.b)));
		return RANGEREF;
	}

	/* Do NOT handle negative numbers here.  That has to be done in the
	 * parser otherwise we mishandle A1-1 when it looks like
	 * rangeref CONSTANT  */
	if (c == state->decimal_point) {
		/* Could be a number or a stand alone  */
		if (!g_unichar_isdigit (g_utf8_get_char (state->ptr)))
			return c;
		is_number = TRUE;
	}  else if (g_unichar_isdigit (c)) {
		/* find the end of the first portion of the number */
		do {
			c = g_utf8_get_char (state->ptr);
			state->ptr = g_utf8_next_char (state->ptr);
		} while (g_unichar_isdigit (c));
		is_number = TRUE;
	}

	if (is_number) {
		Value *v = NULL;

		if (c == state->decimal_point || c == 'e' || c == 'E') {
			/* This is float */
			char *end;
			gnm_float d;

			errno = 0;
			d = strtognum (start, &end);
			if (start == end) {
				g_warning ("%s is not a double, but was expected to be one", start);
			}  else if (errno != ERANGE) {
				v = value_new_float (d);
				state->ptr = end;
			} else if (c != 'e' && c != 'E') {
				report_err (state, g_error_new (1, PERR_OUT_OF_RANGE,
					_("The number is out of range")),
					state->ptr, end - start);
				return INVALID_TOKEN;
			} else {
				/* For an exponent it's hard to highlight the
				 * right region w/o it turning into an ugly
				 * hack, for now the cursor is put at the end.
				 */
				report_err (state, g_error_new (1, PERR_OUT_OF_RANGE,
					_("The number is out of range")),
					state->ptr, 0);
				return INVALID_TOKEN;
			}
		} else {
			char *end;
			long l;

			errno = 0;
			l = strtol (start, &end, 10);
			if (start == end) {
				g_warning ("%s is not an integer, but was expected to be one", start);
			} else if (errno != ERANGE) {
				v = value_new_int (l);
				state->ptr = end;
			} else if (l == LONG_MIN || l == LONG_MAX) {
				gnm_float d;

				errno = 0;
				d = strtognum (start, &end);
				if (errno != ERANGE) {
					v = value_new_float (d);
					state->ptr = end;
				} else {
					report_err (state, g_error_new (1, PERR_OUT_OF_RANGE,
						_("The number is out of range")),
						state->ptr, end - start);
					return INVALID_TOKEN;
				}
			}
		}

		/* Very odd string,  Could be a bound problem.  Trigger an error */
		if (v == NULL)
			return c;

		yylval.expr = register_expr_allocation (gnm_expr_new_constant (v));
		return CONSTANT;
	}

	switch (c) {
	case '#':
		if (state->ptr[0] != '"') {
			gunichar tmp;

			while ((tmp = g_utf8_get_char (state->ptr)) != 0 &&
			       !g_unichar_isspace (tmp)) {
				state->ptr = g_utf8_next_char (state->ptr);
				if (tmp == '!' || tmp == '?') {
					String *name = string_get_nocopy (g_strndup (start, state->ptr - start));
					yylval.expr = register_expr_allocation
						(gnm_expr_new_constant (
							value_new_error_str (NULL, name)));
					string_unref (name);
					return CONSTANT;
				}
			}

			report_err (state, g_error_new
				    (1, PERR_UNEXPECTED_TOKEN,
				     _("Improperly formatted error token")),
				    state->ptr, state->ptr - start);

			return INVALID_TOKEN;
		}
		error_token = TRUE;
		start++;
		/* Fall through */
	case '\'':
	case '"': {
		GString *s = g_string_new ("");
		const char *end = gnm_strunescape (s, start);

		if (end == NULL) {
			size_t len = strlen (start);
			g_string_free (s, TRUE);
  			report_err (state,
				    g_error_new (1, PERR_MISSING_CLOSING_QUOTE,
						 _("Could not find matching closing quote")),
				    start + len, len);
			return INVALID_TOKEN;
		}

		state->ptr = (char *)end;

		if (error_token) {
			Value *v = value_new_error (NULL, s->str);
			yylval.expr = register_expr_allocation (gnm_expr_new_constant (v));
			g_string_free (s, TRUE);
			return CONSTANT;
		} else {
			Value *v = value_new_string_nocopy (g_string_free (s, FALSE));
			yylval.expr = register_expr_allocation (gnm_expr_new_constant (v));
			return QUOTED_STRING;
		}
	}
	}

	if (g_unichar_isalpha (c) || c == '_' || c == '$'){
		gunichar tmp;

		while ((tmp = g_utf8_get_char (state->ptr)) != 0 &&
		       (g_unichar_isalnum (tmp) || tmp == '_' || tmp == '$' ||
		       (tmp == '.' && state->convs->dots_in_names)))
			state->ptr = g_utf8_next_char (state->ptr);

		yylval.expr = register_expr_allocation (gnm_expr_new_constant (
			value_new_string_nocopy (g_strndup (start, state->ptr - start))));
		return STRING;
	}

	if (c == '\n' || c == 0)
		return 0;

	if (c == '<'){
		if (*state->ptr == '='){
			state->ptr++;
			return LTE;
		}
		if (*state->ptr == '>'){
			state->ptr++;
			return NE;
		}
		return c;
	}

	if (c == '>'){
		if (*state->ptr == '='){
			state->ptr++;
			return GTE;
		}
		return c;
	}

	return c;
}

int
yyerror (const char *s)
{
#if 0
	printf ("Error: %s\n", s);
#endif
	return 0;
}

/**
 * gnm_expr_parse_str:
 *
 * @expr_text   : The string to parse.
 * @flags       : See parse-utils for descriptions
 * @error       : optionally NULL ptr to store details of error.
 *
 * Parse a string. if @error is non-null it will be assumed that the
 * caller has passed a pointer to a ParseError struct AND that it will
 * take responsibility for freeing that struct and its contents.
 * with parse_error_free.
 **/
GnmExpr const *
gnm_expr_parse_str (char const *expr_text, ParsePos const *pos,
		    GnmExprParseFlags flags,
		    GnmExprConventions *convs,
		    ParseError *error)
{
	GnmExpr const *expr;
	ParserState pstate;

	g_return_val_if_fail (expr_text != NULL, NULL);
	g_return_val_if_fail (convs != NULL, NULL);

	pstate.start = pstate.ptr = expr_text;
	pstate.pos   = pos;

	pstate.force_absolute_col_references		= flags & GNM_EXPR_PARSE_FORCE_ABSOLUTE_COL_REFERENCES;
	pstate.force_absolute_row_references		= flags & GNM_EXPR_PARSE_FORCE_ABSOLUTE_ROW_REFERENCES;
	pstate.force_explicit_sheet_references		= flags & GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;
	pstate.unknown_names_are_strings		= flags & GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS;
	pstate.convs                                    = convs;

	pstate.decimal_point = convs->decimal_sep_dot
		? '.'
		: format_get_decimal ();
	pstate.separator = convs->argument_sep_semicolon
		? ';'
		: format_get_arg_sep ();
	pstate.array_col_separator = convs->array_col_sep_comma
		? ','
		: format_get_col_sep ();

	pstate.result = NULL;
	pstate.error = error;

	if (deallocate_stack == NULL)
		deallocate_init ();

	g_return_val_if_fail (pstate.pos != NULL, NULL);
	g_return_val_if_fail (pstate.ptr != NULL, NULL);
	g_return_val_if_fail (state == NULL, NULL);

	state = &pstate;
	yyparse ();
	state = NULL;

	if (pstate.result != NULL) {
		deallocate_assert_empty ();

#if 0
		/* If this happens, something is very wrong */
		if (pstate.error != NULL && pstate.error->message != NULL) {
			g_warning ("An error occurred and the GnmExpr is non-null! This should not happen");
			g_warning ("Error message is %s (%d, %d)", pstate.error->message, pstate.error->begin_char,
					pstate.error->end_char);
		}
#endif

		/* Do we have multiple expressions */
		if (pstate.result->next != NULL) {
			if (flags & GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS)
				expr = gnm_expr_new_set (g_slist_reverse (pstate.result));
			else {
				gnm_expr_list_unref (pstate.result);
				report_err (&pstate, g_error_new (1, PERR_MULTIPLE_EXPRESSIONS,
					_("Multiple expressions are not supported in this context")),
					pstate.start,
					(pstate.ptr - pstate.start));
				expr = NULL;
			}
		} else {
			/* Free the list, do not unref the content */
			expr = pstate.result->data;
			gnm_expr_list_free (pstate.result);
		}
	} else {
		/* If there is no error message, attempt to be more detailed */
		if (pstate.error != NULL &&
		    (pstate.error->err == NULL || pstate.error->err->message == NULL)) {
			char const *last_token = pstate.ptr - 1;

			if (*last_token == '\0') {
				char const *str = pstate.start;
				char const *res = NULL;
				char const *last = find_matching_close (str, &res);

				if (*last)
					report_err (&pstate, g_error_new (1, PERR_MISSING_PAREN_OPEN,
						_("Could not find matching opening parenthesis")),
						last, 1);
				else if (res != NULL)
					report_err (&pstate, g_error_new (1, PERR_MISSING_PAREN_CLOSE,
						_("Could not find matching closing parenthesis")),
						res, 1);
				else
					report_err (&pstate, g_error_new (1, PERR_INVALID_EXPRESSION,
						_("Invalid expression")),
						pstate.ptr, pstate.ptr - pstate.start);
			} else
				report_err (&pstate, g_error_new (1, PERR_UNEXPECTED_TOKEN,
					_("Unexpected token %c"), *last_token),
					last_token, 1);
		}

		deallocate_all ();

		expr = NULL;
	}

	deallocate_uninit ();

	return expr;
}
