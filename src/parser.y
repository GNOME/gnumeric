%{
/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric Parser
 *
 * (C) 1998-2002 GNOME Foundation
 * (C) 2002-2006 Morten Welinder
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jody Goldberg (jody@gnome.org)
 *    Morten Welinder (terra@diku.dk)
 *    Almer S. Tigelaar (almer@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "number-match.h"
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "workbook.h"
#include "sheet.h"
#include "gnm-format.h"
#include "application.h"
#include "parse-util.h"
#include "gutils.h"
#include "style.h"
#include "value.h"
#include "str.h"
#include <goffice/utils/go-glib-extras.h>
#include <goffice/app/go-doc.h>
#include <goffice/utils/go-locale.h>

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
  register_allocation ((gpointer)(expr), (ParseDeallocator)&gnm_expr_free)

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
static int yyerror (char const *s);

typedef struct {
	char const *ptr;	/* current position of the lexer */
	char const *start;	/* start of the expression */

	/* Location where the parsing is taking place */
	GnmParsePos const *pos;

	/* loaded from convs with locale specific mappings */
	gunichar decimal_point;
	gunichar arg_sep;
	gunichar array_col_sep;
	gunichar array_row_sep;
	/* if arg_sep conflicts with array_col_sep or array_row_sep */
	int in_array_sep_is;	/* token id */

	GnmExprParseFlags     flags;
	GnmConventions const *convs;

	/* dynamic state */
	int in_array; /* toggled in the lexer for '{' and '}' */
	GnmExprList *result;

	GnmParseError *error;
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

/* Handle -cst for use in arrays.  Don't handle other types here.  */
static GnmExpr *
fold_negative_constant (GnmExpr *expr)
{
	if (expr && GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_CONSTANT) {
		GnmValue *v = (GnmValue *)expr->constant.value;
		
		if (VALUE_IS_FLOAT (v)) {
			gnm_float f = value_get_as_float (v);
			expr->constant.value = value_new_float (0 - f);
			value_release (v);
			return expr;
		}
	}

	return NULL;
}

/* Handle +cst for use in arrays.  Don't handle other types here.  */
static GnmExpr *
fold_positive_constant (GnmExpr *expr)
{
	if (expr && GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_CONSTANT) {
		const GnmValue *v = expr->constant.value;
		if (VALUE_IS_FLOAT (v))
			return expr;
	}

	return NULL;
}

static GnmExpr *
build_unary_op (GnmExprOp op, GnmExpr *expr)
{
	if (!expr) return NULL;

	unregister_allocation (expr);
	return register_expr_allocation (gnm_expr_new_unary (op, expr));
}

static GnmExpr *
build_binop (GnmExpr *l, GnmExprOp op, GnmExpr *r)
{
	if (!l || !r) return NULL;

	unregister_allocation (r);
	unregister_allocation (l);
	return register_expr_allocation (gnm_expr_new_binary (l, op, r));
}

static GnmExpr *
build_logical (GnmExpr *l, gboolean is_and, GnmExpr *r)
{
	static GnmFunc *and_func = NULL, *or_func = NULL;

	if (!l || !r) return NULL;

	if (and_func == NULL)
		and_func = gnm_func_lookup ("AND", NULL);
	if (or_func == NULL)
		or_func = gnm_func_lookup ("OR", NULL);

	unregister_allocation (r);
	unregister_allocation (l);
	return register_expr_allocation
		(gnm_expr_new_funcall2 (is_and ? and_func : or_func, l, r));
}

static GnmExpr *
build_not (GnmExpr *expr)
{
	static GnmFunc *not_func = NULL;

	if (!expr) return NULL;

	if (not_func == NULL)
		not_func = gnm_func_lookup ("NOT", NULL);
	unregister_allocation (expr);
	return register_expr_allocation
		(gnm_expr_new_funcall1 (not_func, expr));
}

/*
 * Build an array expression.
 *
 * Returns NULL on failure.  Caller must YYERROR in that case.
 */
static GnmExpr *
build_array (GSList *cols)
{
	GnmValue *array;
	int mx, y;

	if (!cols) {
		report_err (state, g_error_new (1, PERR_INVALID_EMPTY,
			_("An array must have at least 1 element")),
			state->ptr, 0);
		return NULL;
	}

	mx = g_list_length (cols->data);
	array = value_new_array_empty (mx, g_slist_length (cols));

	y = 0;
	while (cols) {
		GSList *row = cols->data;
		int x = 0;
		while (row && x < mx) {
			GnmExpr const *expr = row->data;
			GnmValue const *v = expr->constant.value;

			g_assert (expr && GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_CONSTANT);

			value_array_set (array, x, y, value_dup (v));

			x++;
			row = row->next;
		}
		if (x < mx || row) {
			/* parser_error = PARSE_ERR_SYNTAX; */
			report_err (state, g_error_new (1, PERR_ASYMETRIC_ARRAY,
				_("Arrays must be rectangular")),
				state->ptr, 0);
			value_release (array);
			return NULL;
		}
		y++;
		cols = cols->next;
	}

	return register_expr_allocation (gnm_expr_new_constant (array));
}

/*
 * Build a range constructor.
 *
 * Returns NULL on failure.  Caller must YYERROR in that case.
 */
static GnmExpr *
build_range_ctor (GnmExpr *l, GnmExpr *r, GnmExpr *validate)
{
	if (validate != NULL) {
		if (GNM_EXPR_GET_OPER (validate) != GNM_EXPR_OP_CELLREF ||
		    validate->cellref.ref.sheet != NULL) {
			report_err (state, g_error_new (1, PERR_UNEXPECTED_TOKEN,
				_("Constructed ranges use simple references")),
				state->ptr, 0);
			return NULL;
		    }
	}
	return build_binop (l, GNM_EXPR_OP_RANGE_CTOR, r);
}

/*
 * Build an intersection expression.
 *
 * Returns NULL on failure.  Caller must YYERROR in that case.
 */
static GnmExpr *
build_intersect (GnmExpr *l, GnmExpr *r)
{
	if (!l || !r) return NULL;

	if (gnm_expr_is_rangeref (l) && gnm_expr_is_rangeref (r))
		return build_binop (l, GNM_EXPR_OP_INTERSECT, r);
	report_err (state, g_error_new (1, PERR_SET_CONTENT_MUST_BE_RANGE,
		_("All entries in the set must be references")),
		state->ptr, 0);
	return NULL;
}

/*
 * Build a set expression.
 *
 * Returns NULL on failure.  Caller must YYERROR in that case.
 */
static GnmExpr *
build_set (GnmExprList *list)
{
	/* verify that every thing is a ref */
	GnmExprList *ptr;
	for (ptr = list; ptr != NULL ; ptr = ptr->next) {
		GnmExpr const *expr = ptr->data;
		if (!expr || !gnm_expr_is_rangeref (expr)) {
			report_err (state, g_error_new (1, PERR_SET_CONTENT_MUST_BE_RANGE,
				_("All entries in the set must be references")),
				state->ptr, 0);
			return NULL;
		}
	}

	unregister_allocation (list);
	return register_expr_allocation (gnm_expr_new_set (list));
}

/**
 * parse_string_as_value :
 *
 * Try to parse the entered text as a basic value (empty, bool, int,
 * float, err) if this succeeds, we store this as a GnmValue otherwise, we
 * return a string.
 */
static GnmExpr *
parse_string_as_value (GnmExpr *str)
{
	GnmValue *v = format_match_simple (str->constant.value->v_str.val->str);

	if (v != NULL) {
		unregister_allocation (str);
		gnm_expr_free (str);
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
	GnmExpr const *res;
	char const *str = str_expr->constant.value->v_str.val->str;
	GnmValue *v = format_match_simple (str);

	/* if it is not a simple value see if it is a name */
	if (v == NULL) {
		GnmNamedExpr *nexpr = expr_name_lookup (state->pos, str);
		if (nexpr == NULL) {
			if (state->flags & GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_INVALID) {
				report_err (state, g_error_new (1, PERR_UNKNOWN_NAME,
								_("Name '%s' does not exist"),
								str),
					    state->ptr, 0);
				res = NULL;
			} else if (state->flags & GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS) {
				res = gnm_expr_new_constant (value_new_string (str));
			} else {
				GnmParsePos pp = *state->pos;
				pp.sheet = NULL;
				/* Create a place holder */
				nexpr = expr_name_add (&pp, str, NULL, NULL, TRUE, NULL);
				res = gnm_expr_new_name (nexpr, NULL, NULL);
			}
		} else
			res = gnm_expr_new_name (nexpr, NULL, NULL);
	} else
		res = gnm_expr_new_constant (v);

	unregister_allocation (str_expr);
	gnm_expr_free (str_expr);
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
		 * when the last token was SHEET_SEP */
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
	GnmValue	*value;
	GnmCellRef	*cell;
	GnmExprList	*list;
	Sheet		*sheet;
	Workbook	*wb;
}
%type  <list>	opt_exp arg_list array_row array_rows
%type  <expr>	exp array_exp function string_opt_quote cellref
%token <expr>	STRING QUOTED_STRING CONSTANT RANGEREF GTE LTE NE AND OR NOT INTERSECT
%token		ARG_SEP ARRAY_COL_SEP ARRAY_ROW_SEP SHEET_SEP INVALID_TOKEN
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
%left RANGE_INTERSECT
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

opt_exp : opt_exp exp  ARG_SEP {
	       unregister_allocation ($2);
	       unregister_allocation ($1);
	       $$ = gnm_expr_list_prepend ($1, $2);
	       register_expr_list_allocation ($$);
	}
	| { $$ = NULL; register_expr_list_allocation ($$); }
	;

exp:	  CONSTANT 	{ $$ = $1; }
	| QUOTED_STRING { $$ = $1; }
	| STRING        {
		$$ = parser_simple_val_or_name ($1);
		if ($$ == NULL) { YYERROR; }
	}
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
	| exp RANGE_INTERSECT exp {
		$$ = build_intersect ($1, $3);
		if ($$ == NULL) { YYERROR; }
	}

        | '-' exp %prec NEG {
		GnmExpr *tmp = fold_negative_constant ($2);
		$$ = tmp ? tmp : build_unary_op (GNM_EXPR_OP_UNARY_NEG, $2);
	}
        | '+' exp %prec PLUS {
		/* Don't fold here.  */
		$$ = build_unary_op (GNM_EXPR_OP_UNARY_PLUS, $2);
	}
        | NOT exp { $$ = build_not ($2); }
        | exp '%' { $$ = build_unary_op (GNM_EXPR_OP_PERCENTAGE, $1); }

	| '(' arg_list ')' {
		if ($2 == NULL) {
			report_err (state, g_error_new (1, PERR_INVALID_EMPTY,
				_("() is an invalid expression")),
				state->ptr-2, 2);
			YYERROR;
		} else {
			if ($2->next == NULL) {
				unregister_allocation ($2);
				$$ = register_expr_allocation ($2->data);
				/* NOTE : free list not content */
				gnm_expr_list_free ($2);
			} else {
				$$ = build_set ($2);
				if ($$ == NULL) { YYERROR; }
			}
		}
	}
        | '{' array_rows '}' {
		unregister_allocation ($2);
		$$ = build_array ($2);
		free_expr_list_list ($2);
		if ($$ == NULL) { YYERROR; }
	}

	| function
	| sheetref STRING {
		GnmNamedExpr *nexpr = NULL;
		char const *name = $2->constant.value->v_str.val->str;
		GnmParsePos pos = *state->pos;

		pos.sheet = $1;
		nexpr = expr_name_lookup (&pos, name);
		if (nexpr == NULL) {
			report_err (state, g_error_new (1, PERR_UNKNOWN_NAME,
				_("Name '%s' does not exist in sheet '%s'"),
						name, pos.sheet->name_quoted),
				state->ptr, strlen (name));
			YYERROR;
		} else {
			unregister_allocation ($2); gnm_expr_free ($2);
			$$ = register_expr_allocation (gnm_expr_new_name (nexpr, $1, NULL));
		}
	}
	| workbookref STRING {
		GnmNamedExpr *nexpr = NULL;
		char const *name = $2->constant.value->v_str.val->str;
		GnmParsePos pos = *state->pos;

		pos.sheet = NULL;
		pos.wb = $1;
		nexpr = expr_name_lookup (&pos, name);
		if (nexpr != NULL) {
			unregister_allocation ($2); gnm_expr_free ($2);
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
		GnmExpr const *f_call = (*state->convs->input.func) (
			state->convs, state->pos->wb, name, $3);

		$$ = NULL;
		if (f_call) {
			/* We're done with the function name.  */
			unregister_allocation ($1); gnm_expr_free ($1);
			unregister_allocation ($3);
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
		Workbook *ref_wb = state->pos
			? (state->pos->wb
			   ? state->pos->wb
			   : (state->pos->sheet
			      ? state->pos->sheet->workbook
			      : NULL))
			: NULL;
		Workbook *wb = gnm_app_workbook_get_by_name
			(wb_name,
			 ref_wb ? go_doc_get_uri ((GODoc *)ref_wb) : NULL);

		if (wb != NULL) {
			unregister_allocation ($2); gnm_expr_free ($2);
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
			unregister_allocation ($1); gnm_expr_free ($1);
			$$ = sheet;
		} else {
			YYERROR;
		}
	}
	| workbookref string_opt_quote SHEET_SEP {
		Workbook *wb = $1;
		Sheet *sheet = parser_sheet_by_name (wb, $2);
		if (sheet != NULL) {
			unregister_allocation ($2); gnm_expr_free ($2);
			$$ = sheet;
		} else {
			YYERROR;
		}
        }
	;

cellref:  RANGEREF { $$ = $1; }
	| function RANGE_SEP function {
		$$ = build_range_ctor ($1, $3, NULL);
		if ($$ == NULL) { YYERROR; }
	}
	| RANGEREF RANGE_SEP function {
		$$ = build_range_ctor ($1, $3, $1);
		if ($$ == NULL) { YYERROR; }
	}
	| function RANGE_SEP RANGEREF {
		$$ = build_range_ctor ($1, $3, $3);
		if ($$ == NULL) { YYERROR; }
	}
	| RANGEREF RANGE_SEP RANGEREF {
		$$ = build_binop ($1, GNM_EXPR_OP_RANGE_CTOR, $3);
	}
	;

arg_list: exp {
		unregister_allocation ($1);
		$$ = gnm_expr_list_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }
	| exp ARG_SEP arg_list {
		GSList *tmp = $3;
		unregister_allocation ($3);
		unregister_allocation ($1);

		if (tmp == NULL)
			tmp = gnm_expr_list_prepend (NULL, gnm_expr_new_constant (value_new_empty ()));

		$$ = gnm_expr_list_prepend (tmp, $1);
		register_expr_list_allocation ($$);
	}
	| ARG_SEP arg_list {
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
	 | '-' CONSTANT		{
		GnmExpr *tmp = fold_negative_constant ($2);
		if (!tmp) { YYERROR; }
		$$ = tmp;
	 }
	 | '+' CONSTANT		{
		GnmExpr *tmp = fold_positive_constant ($2);
		if (!tmp) { YYERROR; }
		$$ = tmp;
	 }
	 | string_opt_quote	{ $$ = parse_string_as_value ($1); }
	 ;


array_row : { $$ = NULL; }
	| array_exp {
		unregister_allocation ($1);
		$$ = g_slist_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }
	| array_exp ARRAY_COL_SEP array_row {
		unregister_allocation ($3);
		unregister_allocation ($1);
		$$ = g_slist_prepend ($3, $1);
		register_expr_list_allocation ($$);
	}
	;

array_rows: array_row {
		unregister_allocation ($1);
		$$ = g_slist_prepend (NULL, $1);
		register_expr_list_list_allocation ($$);
        }
        | array_row ARRAY_ROW_SEP array_rows {
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
	while (*str) {
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
			GString *dummy = g_string_new (NULL);
			char const *end = go_strunescape (dummy, str);
			g_string_free (dummy, TRUE);
			if (end == NULL)
				return str + strlen (str);
			str = end;
			continue; /* skip incrementing str */
		}
		str = g_utf8_next_char (str);
	}

	return str;
}

static inline int
eat_space (ParserState *state, int res)
{
	/* help the user by ignoring pointless spaces after an
	 * arg_sep.  We know they are going to be errors and
	 * the spaces can not be operators in this context */
	while (*state->ptr == ' ')
		state->ptr++;
	return res;
}

/*
 * Do we want to ignore space before a given character?
 */
static gboolean
ignore_space_before (gunichar c)
{
	switch (c) {
	case '*': case '/': case '+': case '-': case '%': case '^': case '&':
	case '>': case '<': case '=':
	case ')':
	case '#':
	case '"': case '\'':  /* Refers to opening quote only.  */
	case UNICODE_LOGICAL_NOT_C:
	case UNICODE_LOGICAL_AND_C:
	case UNICODE_LOGICAL_OR_C:
	case UNICODE_MINUS_SIGN_C:
	case UNICODE_DIVISION_SLASH_C:
	case UNICODE_NOT_EQUAL_TO_C:
	case UNICODE_LESS_THAN_OR_EQUAL_TO_C:
	case UNICODE_GREATER_THAN_OR_EQUAL_TO_C:
	case 0:
		return TRUE;
	default:
		return FALSE;
	}
}

/*
 * Do we want to ignore space after a given character?
 */
static gboolean
ignore_space_after (gunichar c)
{
	switch (c) {
	case '*': case '/': case '+': case '-': case '%': case '^': case '&':
	case '>': case '<': case '=':
	case '(':
	case '"': case '\'':  /* Refers to closing quote only [not actually hit].  */
	case UNICODE_LOGICAL_NOT_C:
	case UNICODE_LOGICAL_AND_C:
	case UNICODE_LOGICAL_OR_C:
	case UNICODE_MINUS_SIGN_C:
	case UNICODE_DIVISION_SLASH_C:
	case UNICODE_NOT_EQUAL_TO_C:
	case UNICODE_LESS_THAN_OR_EQUAL_TO_C:
	case UNICODE_GREATER_THAN_OR_EQUAL_TO_C:
	case 0:
		return TRUE;
	default:
		return FALSE;
	}
}

static int
yylex (void)
{
	gunichar c, tmp;
	char const *start, *end;
	GnmRangeRef ref;
	gboolean is_number = FALSE;
	gboolean is_space = FALSE;
	gboolean error_token = FALSE;

	/*
	 * Some special logic to handle space as intersection char.
	 * Any number of white space characters are treated as one
	 * intersecton.
	 *
	 * Also, if we are not using space for that, drop spaces.
	 */
        while (g_unichar_isspace (g_utf8_get_char (state->ptr))) {
                state->ptr = g_utf8_next_char (state->ptr);
		is_space = TRUE;
	}
	if (is_space && state->convs->intersection_char == ' ' &&
	    !ignore_space_before (g_utf8_get_char (state->ptr)))
		return RANGE_INTERSECT;

	start = state->ptr;
	c = g_utf8_get_char (start);
	if (c == 0)
		return 0;
	state->ptr = g_utf8_next_char (state->ptr);

	if (c == state->convs->intersection_char)
		return RANGE_INTERSECT;

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
			GnmValue *v;

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
		return eat_space (state, RANGE_SEP);

	if (c == state->convs->sheet_name_sep)
		return eat_space (state, SHEET_SEP);

	if (c == '.' && *state->ptr == '.' && state->convs->range_sep_dotdot) {
		state->ptr++;
		return RANGE_SEP;
	}

	if (c == '#' && state->convs->accept_hash_logicals) {
		if (!strncmp (state->ptr, "NOT#", 4)) {
			state->ptr += 4;
			return eat_space (state, NOT);
		}
		if (!strncmp (state->ptr, "AND#", 4)) {
			state->ptr += 4;
			return eat_space (state, AND);
		}
		if (!strncmp (state->ptr, "OR#", 3)) {
			state->ptr += 3;
			return eat_space (state, OR);
		}
	}

	if (c == state->arg_sep)
		return eat_space (state, state->in_array ? state->in_array_sep_is : ARG_SEP);
	if (c == state->array_col_sep)
		return eat_space (state, ARRAY_COL_SEP);
	if (c == state->array_row_sep)
		return eat_space (state, ARRAY_ROW_SEP);

	if (start != (end = state->convs->input.range_ref (&ref, start, state->pos, state->convs))) {
		state->ptr = end;
		if (state->flags & GNM_EXPR_PARSE_FORCE_ABSOLUTE_REFERENCES) {
			if (ref.a.col_relative) {
				ref.a.col += state->pos->eval.col;
				ref.a.col_relative = FALSE;
			}
			if (ref.b.col_relative) {
				ref.b.col += state->pos->eval.col;
				ref.b.col_relative = FALSE;
			}
			if (ref.a.row_relative) {
				ref.a.row += state->pos->eval.row;
				ref.a.row_relative = FALSE;
			}
			if (ref.b.row_relative) {
				ref.b.row += state->pos->eval.row;
				ref.b.row_relative = FALSE;
			}
		} else if (state->flags & GNM_EXPR_PARSE_FORCE_RELATIVE_REFERENCES) {
			if (!ref.a.col_relative) {
				ref.a.col -= state->pos->eval.col;
				ref.a.col_relative = TRUE;
			}
			if (!ref.b.col_relative) {
				ref.b.col -= state->pos->eval.col;
				ref.b.col_relative = TRUE;
			}
			if (!ref.a.row_relative) {
				ref.a.row -= state->pos->eval.row;
				ref.a.row_relative = TRUE;
			}
			if (!ref.b.row_relative) {
				ref.b.row -= state->pos->eval.row;
				ref.b.row_relative = TRUE;
			}
		}

		if (ref.a.sheet == NULL && (state->flags & GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES)) {
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
		GnmValue *v = NULL;

		if (c == state->decimal_point || c == 'e' || c == 'E') {
			/* This is float */
			char *end;
			gnm_float d;

			errno = 0;
			d = gnm_strto (start, &end);
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
			} else if (errno != ERANGE && l >= INT_MIN && l <= INT_MAX) {
				v = value_new_int (l);
				state->ptr = end;
			} else {
				gnm_float d;

				errno = 0;
				d = gnm_strto (start, &end);
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
			while ((tmp = g_utf8_get_char (state->ptr)) != 0 &&
			       !g_unichar_isspace (tmp)) {
				state->ptr = g_utf8_next_char (state->ptr);
				if (tmp == '!' || tmp == '?' ||
				((state->ptr - start) == 4 && 0 == strncmp (start, "#N/A", 4))) {
					GnmString *name = gnm_string_get_nocopy (g_strndup (start, state->ptr - start));
					yylval.expr = register_expr_allocation
						(gnm_expr_new_constant (
							value_new_error_str (NULL, name)));
					gnm_string_unref (name);
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
		GString *s = g_string_new (NULL);
		char const *end = go_strunescape (s, start);

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
			GnmValue *v = value_new_error (NULL, s->str);
			yylval.expr = register_expr_allocation (gnm_expr_new_constant (v));
			g_string_free (s, TRUE);
			return eat_space (state, CONSTANT);
		} else {
			GnmValue *v = value_new_string_nocopy (g_string_free (s, FALSE));
			yylval.expr = register_expr_allocation (gnm_expr_new_constant (v));
			return eat_space (state, QUOTED_STRING);
		}
	}
	}

	if ((end = state->convs->input.name (start, state->convs))) {
		state->ptr = end;
		yylval.expr = register_expr_allocation (gnm_expr_new_constant (
			value_new_string_nocopy (g_strndup (start, state->ptr - start))));
		return STRING;
	}

	switch (c) {
	case '<':
		if (*state->ptr == '='){
			state->ptr++;
			return eat_space (state, LTE);
		}
		if (*state->ptr == '>'){
			state->ptr++;
			return eat_space (state, NE);
		}
		return eat_space (state, c);

	case '>':
		if (*state->ptr == '='){
			state->ptr++;
			return eat_space (state, GTE);
		}
		return eat_space (state, c);

	case '\n': return 0;

	case '{' :
		state->in_array++;
		return c;
	case '}' :
		state->in_array--;
		return c;

	case UNICODE_LOGICAL_NOT_C: return NOT;
	case UNICODE_MINUS_SIGN_C: return '-';
	case UNICODE_DIVISION_SLASH_C: return '/';
	case UNICODE_LOGICAL_AND_C: return AND;
	case UNICODE_LOGICAL_OR_C: return OR;
	case UNICODE_NOT_EQUAL_TO_C: return eat_space (state, NE);
	case UNICODE_LESS_THAN_OR_EQUAL_TO_C: return eat_space (state, LTE);
	case UNICODE_GREATER_THAN_OR_EQUAL_TO_C: return eat_space (state, GTE);
	}

	if (ignore_space_after (c))
		return eat_space (state, c);
	else
		return c;
}

int
yyerror (char const *s)
{
#if 0
	printf ("Error: %s\n", s);
#endif
	return 0;
}

/**
 * gnm_expr_parse_str:
 *
 * @str   : The string to parse.
 * @pp	  : #GnmParsePos
 * @flags : See parse-utils for descriptions
 * @convs : optionally NULL #GnmConventions
 * @error : optionally NULL ptr to store details of error.
 *
 * Parse a string. if @error is non-null it will be assumed that the
 * caller has passed a pointer to a GnmParseError struct AND that it will
 * take responsibility for freeing that struct and its contents.
 * with parse_error_free.
 * If @convs is NULL use the conventions from @pp.
 **/
GnmExprTop const *
gnm_expr_parse_str (char const *str, GnmParsePos const *pp,
		    GnmExprParseFlags flags,
		    GnmConventions const *convs,
		    GnmParseError *error)
{
	GnmExpr const *expr;
	ParserState pstate;

	g_return_val_if_fail (str != NULL, NULL);
	g_return_val_if_fail (pp != NULL, NULL);

	pstate.start = pstate.ptr = str;
	pstate.pos   = pp;

	pstate.flags		= flags;
	pstate.convs                                    =
		(NULL != convs) ? convs : ((NULL != pp->sheet) ? pp->sheet->convs : gnm_conventions_default);


	pstate.decimal_point = pstate.convs->decimal_sep_dot
		? '.'
		: g_utf8_get_char (go_locale_get_decimal ()->str); /* FIXME: one char handled.  */

	if (pstate.convs->arg_sep != 0)
		pstate.arg_sep = pstate.convs->arg_sep;
	else
		pstate.arg_sep = go_locale_get_arg_sep ();
	if (pstate.convs->array_col_sep != 0)
		pstate.array_col_sep = pstate.convs->array_col_sep;
	else
		pstate.array_col_sep = go_locale_get_col_sep ();
	if (pstate.convs->array_row_sep != 0)
		pstate.array_row_sep = pstate.convs->array_row_sep;
	else
		pstate.array_row_sep = go_locale_get_row_sep ();

	/* Some locales/conventions have ARG_SEP == ARRAY_ROW_SEP
	 * 	eg {1\2\3;4\5\6} for XL style with ',' as a decimal
	 * some have ARG_SEP == ARRAY_COL_SEPARATOR
	 * 	eg {1,2,3;4,5,6} for XL style with '.' as a decimal
	 * 	or {1;2;3|4;5;6} for OOo/
	 * keep track of whether we are in an array to allow the lexer to
	 * dis-ambiguate. */
	if (pstate.arg_sep == pstate.array_col_sep)
		pstate.in_array_sep_is = ARRAY_COL_SEP;
	else if (pstate.arg_sep == pstate.array_row_sep)
		pstate.in_array_sep_is = ARRAY_ROW_SEP;
	else
		pstate.in_array_sep_is = ARG_SEP;
	pstate.in_array = 0;

	pstate.result = NULL;
	pstate.error = error;

	if (deallocate_stack == NULL)
		deallocate_init ();

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
			char const *last_token = pstate.ptr;

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

	return gnm_expr_top_new (expr);
}
