%{
/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric Parser
 *
 * (C) 1998-2001 the Free Software Foundation
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
#include "auto-format.h"
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

#define ERROR -1

/* Bison/Yacc internals */
static int yylex (void);
static int yyerror (const char *s);

typedef struct {
	/* The expression being parsed */
	char const *expr_text;

	/* A backup of the above, this will always point to the real
	 * expression beginning to calculate the offset in the expression
	 */
	char const *expr_backup;

	/* Location where the parsing is taking place */
	ParsePos const *pos;

	/* Locale info. */
	gunichar decimal_point;
	gunichar separator;
	gunichar array_col_separator;

	/* flags */
	gboolean use_excel_reference_conventions;
	gboolean create_placeholder_for_unknown_func;
	gboolean force_absolute_col_references;
	gboolean force_absolute_row_references;
	gboolean force_explicit_sheet_references;

	/* The suggested format to use for this expression */
	GnmExprList *result;

	ParseError *error;
} ParserState;

/* The error returned from the */
static ParserState *state;

static int
gnumeric_parse_error (ParserState *state, ParseErrorID id, char *message, int end, int relative_begin)
{
	if (state->error != NULL) {
		state->error->id         = id;
		state->error->message    = message;
		state->error->begin_char = (end - relative_begin);
		state->error->end_char   = end;
	} else
		g_free (message);

	return ERROR;
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
	static FunctionDefinition *and_func = NULL, *or_func = NULL;

	if (and_func == NULL)
		and_func = func_lookup_by_name ("AND", NULL);
	if (or_func == NULL)
		or_func = func_lookup_by_name ("OR", NULL);

	unregister_allocation (r);
	unregister_allocation (l);
	return register_expr_allocation (gnm_expr_new_funcall (is_and ? and_func : or_func,
		    g_slist_prepend (g_slist_prepend (NULL, l), r)));
}

static GnmExpr *
build_not (GnmExpr *expr)
{
	static FunctionDefinition *not_func = NULL;
	if (not_func == NULL)
		not_func = func_lookup_by_name ("NOT", NULL);
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
				return gnumeric_parse_error (state,
							     ParseErrorID id, char *message, int end, int relative_begin)
		    }
	}
	return build_binop (l, GNM_EXPR_OP_RANGE_CTOR, r);
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
	Value *v = format_match_simple (value_peek_string (str->constant.value));

	if (v != NULL) {
		unregister_allocation (str);
		gnm_expr_unref (str);
		return register_expr_allocation (gnm_expr_new_constant (v));
	}
	return str;
}

/**
 * parse_string_as_value_or_name :
 * @str : An expression with oper constant, whose value is a string.
 *
 * Check to see if a string is a name
 * if it is not check to see if it can be parsed as a value
 */
static GnmExpr *
parse_string_as_value_or_name (GnmExpr *str)
{
	GnmNamedExpr *expr_name;

	expr_name = expr_name_lookup (state->pos, str->constant.value->v_str.val->str);
	if (expr_name != NULL) {
		unregister_allocation (str);
		gnm_expr_unref (str);
		return register_expr_allocation (gnm_expr_new_name (expr_name, NULL, NULL));
	}

	return parse_string_as_value (str);
}

static gboolean
force_explicit_sheet_references (ParserState *state, CellRef *ref)
{
	ref->sheet = state->pos->sheet;
	if (ref->sheet != NULL)
		return FALSE;

	gnumeric_parse_error (
		state, PERR_SHEET_IS_REQUIRED,
		g_strdup (_("Sheet name is required")),
		state->expr_text - state->expr_backup, 0);
	return TRUE;
}

static Sheet *
parser_sheet_by_name (Workbook *wb, GnmExpr *name_expr)
{
	char  *name = name_expr->constant.value->v_str.val->str;
	Sheet *sheet = NULL;
	
	if (wb == NULL)
		return NULL;
	
	sheet = workbook_sheet_by_name (wb, name);

	/* Applix has absolute and relative sheet references */
	if (sheet == NULL &&
	    !state->use_excel_reference_conventions && *name == '$')
		sheet = workbook_sheet_by_name (wb, name+1);

	if (sheet == NULL) {
		gnumeric_parse_error (
				state, PERR_UNKNOWN_SHEET,
				g_strdup_printf (_("Unknown sheet '%s'"), name),
				state->expr_text - state->expr_backup, strlen (name));
	}
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
	struct {
		Sheet	*first;
		Sheet	*last;
	} sheet;
}
%type  <list>	opt_exp arg_list array_row, array_cols
%type  <expr>	exp array_exp function string_opt_quote cellref
%token <expr>	STRING QUOTED_STRING CONSTANT RANGEREF GTE LTE NE AND OR NOT
%token		SEPARATOR INVALID_TOKEN
%type  <sheet>	sheetref opt_sheetref

%left '&'
%left '<' '>' '=' GTE LTE NE
%left '-' '+'
%left '*' '/'
%left NEG PLUS NOT
%left RANGE_SEP SHEET_SEP
%right '^'
%left AND OR
%right '%'

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
	| STRING        { $$ = parse_string_as_value_or_name ($1); }
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
	| '(' exp ')'   { $$ = $2; }

        | '-' exp %prec NEG { $$ = build_unary_op (GNM_EXPR_OP_UNARY_NEG, $2); }
        | '+' exp %prec PLUS { $$ = build_unary_op (GNM_EXPR_OP_UNARY_PLUS, $2); }
        | NOT exp { $$ = build_not ($2); }
        | exp '%' { $$ = build_unary_op (GNM_EXPR_OP_PERCENTAGE, $1); }

        | '{' array_cols '}' {
		unregister_allocation ($2);
		$$ = build_array ($2);
		free_expr_list_list ($2);
	}

	| function
	| sheetref string_opt_quote {
		GnmNamedExpr *expr_name = NULL;
		char const *name = $2->constant.value->v_str.val->str;
		ParsePos pos = *state->pos;

		pos.sheet = $1.first;
		if ($1.last != NULL)
			gnumeric_parse_error (
				state, PERR_3D_NAME,
				g_strdup_printf (_("What is a 3D name %s:%s!%s ?"),
						$1.first->name_quoted,
						$1.last->name_quoted,
						name),
				state->expr_text - state->expr_backup + 1, strlen (name));
		else {
			expr_name = expr_name_lookup (&pos, name);
			if (expr_name == NULL)
				gnumeric_parse_error (
					state, PERR_UNKNOWN_NAME,
					g_strdup_printf (_("Name '%s' does not exist in sheet '%s'"),
							name, pos.sheet->name_quoted),
					state->expr_text - state->expr_backup + 1, strlen (name));
		}

		unregister_allocation ($2); gnm_expr_unref ($2);
		if (expr_name == NULL)
			return ERROR;
	        $$ = register_expr_allocation (gnm_expr_new_name (expr_name, $1.first, NULL));
	}
	| '[' string_opt_quote ']' string_opt_quote {
		GnmNamedExpr *expr_name;
		char *name = $4->constant.value->v_str.val->str;
		char *wb_name = $2->constant.value->v_str.val->str;
		ParsePos pos = *state->pos;

		pos.sheet = NULL;
		pos.wb = application_workbook_get_by_name (wb_name);

		if (pos.wb == NULL) {
			int retval = gnumeric_parse_error (
				state, PERR_UNKNOWN_WORKBOOK,
				g_strdup_printf (_("Unknown workbook '%s'"), wb_name), 
				state->expr_text - state->expr_backup + 1, strlen (name));

			unregister_allocation ($4); gnm_expr_unref ($4);
			unregister_allocation ($2); gnm_expr_unref ($2);
			return retval;
		}

		expr_name = expr_name_lookup (&pos, name);
		if (expr_name == NULL) {
			int retval = gnumeric_parse_error (
				state, PERR_UNKNOWN_NAME,
				g_strdup_printf (_("Name '%s' does not exist in workbook '%s'"),
						name, wb_name),
				state->expr_text - state->expr_backup + 1, strlen (name));

			unregister_allocation ($4); gnm_expr_unref ($4);
			unregister_allocation ($2); gnm_expr_unref ($2);
			return retval;
		} else {
			unregister_allocation ($4); gnm_expr_unref ($4);
			unregister_allocation ($2); gnm_expr_unref ($2);
		}
	        $$ = register_expr_allocation (gnm_expr_new_name (expr_name, NULL, pos.wb));
	}
	;

function : STRING '(' arg_list ')' {
		char const *name = $1->constant.value->v_str.val->str;
		FunctionDefinition *f = func_lookup_by_name (name,
			state->pos->wb);

		/* THINK TODO: Do we want to make this workbook-local??  */
		if (f == NULL && state->create_placeholder_for_unknown_func)
			f = function_add_placeholder (name, "");

		unregister_allocation ($3);
		unregister_allocation ($1); gnm_expr_unref ($1);

		if (f == NULL) {
			gnm_expr_list_unref ($3);
			YYERROR;
		} else {
			$$ = register_expr_allocation (gnm_expr_new_funcall (f, $3));
		}
	}
	;

string_opt_quote : STRING
		 | QUOTED_STRING
		 ;

sheetref: string_opt_quote SHEET_SEP {
		Sheet *sheet = parser_sheet_by_name (state->pos->wb, $1);
		unregister_allocation ($1); gnm_expr_unref ($1);
		if (sheet == NULL)
			return ERROR;
	        $$.first = sheet;
	        $$.last = NULL;
	}
	| string_opt_quote RANGE_SEP string_opt_quote SHEET_SEP {
		Sheet *a_sheet = parser_sheet_by_name (state->pos->wb, $1);
		Sheet *b_sheet = parser_sheet_by_name (state->pos->wb, $3);

		unregister_allocation ($1); gnm_expr_unref ($1);
		unregister_allocation ($3); gnm_expr_unref ($3);

		if (a_sheet == NULL || b_sheet == NULL)
			return ERROR;
	        $$.first = a_sheet;
	        $$.last = b_sheet;
	}

	| '[' string_opt_quote ']' string_opt_quote SHEET_SEP {
		Workbook *wb = application_workbook_get_by_name (
			$2->constant.value->v_str.val->str);
		Sheet *sheet = parser_sheet_by_name (wb, $4);

		unregister_allocation ($2); gnm_expr_unref ($2);
		unregister_allocation ($4); gnm_expr_unref ($4);

		if (sheet == NULL)
			return ERROR;
	        $$.first = sheet;
	        $$.last = NULL;
        }
	| '[' string_opt_quote ']' string_opt_quote RANGE_SEP string_opt_quote SHEET_SEP {
		Workbook *wb = application_workbook_get_by_name (
			$2->constant.value->v_str.val->str);
		Sheet *a_sheet = parser_sheet_by_name (wb, $4);
		Sheet *b_sheet = parser_sheet_by_name (wb, $6);

		unregister_allocation ($2); gnm_expr_unref ($2);
		unregister_allocation ($4); gnm_expr_unref ($4);
		unregister_allocation ($6); gnm_expr_unref ($6);

		if (a_sheet == NULL || b_sheet == NULL)
			return ERROR;
	        $$.first = a_sheet;
	        $$.last = b_sheet;
        }
	;

opt_sheetref: sheetref
	    | { $$.first = $$.last = NULL; }
	    ;

cellref:  RANGEREF { $$ = $1; }
	| RANGEREF RANGE_SEP function  { $$ = build_range_ctor ($1, $3, $1); }
	| function RANGE_SEP function  { $$ = build_range_ctor ($1, $3, NULL); }
	| function RANGE_SEP RANGEREF  { $$ = build_range_ctor ($1, $3, $3); }
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

array_exp: CONSTANT		{ $$ = $1; }
	 | string_opt_quote	{ $$ = parse_string_as_value ($1); }
	 ;

array_row: array_exp {
		unregister_allocation ($1);
		$$ = g_slist_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }
	| array_exp SEPARATOR array_row {
		if (state->array_col_separator == ',') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_slist_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else {
			return gnumeric_parse_error (
				state, PERR_INVALID_ARRAY_SEPARATOR,
				g_strdup_printf (_("The character %c cannot be used to separate array elements"),
				state->array_col_separator), state->expr_text - state->expr_backup + 1, 1);
		}
	}
	| array_exp '\\' array_row {
		if (state->array_col_separator == '\\') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_slist_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else {
			/* FIXME: Is this the right error to display? */
			return gnumeric_parse_error (
				state, PERR_INVALID_ARRAY_SEPARATOR,
				g_strdup_printf (_("The character %c cannot be used to separate array elements"),
				state->array_col_separator), state->expr_text - state->expr_backup + 1, 1);
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

/**
 * find_char:
 * @str:
 * @c:
 *
 * Returns a pointer to character c in str.
 * Callers should check whether p is '\0'!
 **/
static char const *
find_char (char const *str, char c)
{
	for (; *str && *str != c; str = g_utf8_next_char (str))
		if (*str == '\\' && str[1])
			str = g_utf8_next_char (str+1);
	return str;
}

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
		else if (*str == '\'' || *str == '\"')
			str = find_char (str + 1, *str);
	}

	return str;
}

int
yylex (void)
{
	gunichar c;
	char const *start, *end;
	RangeRef ref;
	gboolean is_number = FALSE;

        while (g_unichar_isspace (g_utf8_get_char (state->expr_text)))
                state->expr_text = g_utf8_next_char (state->expr_text);

	start = state->expr_text;
	c = g_utf8_get_char (start);
	state->expr_text = g_utf8_next_char (state->expr_text);

	if (c == '(' || c == ')')
		return c;

	if (state->use_excel_reference_conventions) {
		if (c == ':')
			return RANGE_SEP;
		if (c == '!')
			return SHEET_SEP;
	} else {
		/* Treat '..' as range sep (A1..C3) */
		if (c == '.' && *state->expr_text == '.') {
			state->expr_text++;
			return RANGE_SEP;
		}
		if (c == ':')
			return SHEET_SEP;
		if (c == '#') {
			if (!strncmp (state->expr_text, "NOT#", 4)) {
				state->expr_text += 4;
				return NOT;
			}
			if (!strncmp (state->expr_text, "AND#", 4)) {
				state->expr_text += 4;
				return AND;
			}
			if (!strncmp (state->expr_text, "OR#", 3)) {
				state->expr_text += 3;
				return OR;
			}
		}
	}

	if (c == state->separator)
		return SEPARATOR;

	if (start != (end = rangeref_parse (&ref, start, state->pos))) {
		state->expr_text = end;
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

	if (c == state->decimal_point) {
		/* Could be a number or a stand alone  */
		if (!g_unichar_isdigit (g_utf8_get_char (state->expr_text)))
			return c;
		is_number = TRUE;
	} else if (g_unichar_isdigit (c)) {
		do {
			c = g_utf8_get_char (state->expr_text);
			state->expr_text = g_utf8_next_char (state->expr_text);
		} while (g_unichar_isdigit (c));
		is_number = TRUE;
	}

	if (is_number) {
		Value *v = NULL;

		if (c == state->decimal_point || c == 'e' || c == 'E') {
			/* This is float */
			char *end;
			gnum_float d;

			errno = 0;
			d = strtognum (start, &end);
			if (start == end) {
				g_warning ("%s is not a double, but was expected to be one", start);
			}  else if (errno != ERANGE) {
				v = value_new_float (d);
				state->expr_text = end;
			} else if (c != 'e' && c != 'E') {
				gnumeric_parse_error (
					state, PERR_OUT_OF_RANGE,
					g_strdup (_("The number is out of range")),
					state->expr_text - state->expr_backup, end - start);
				return INVALID_TOKEN;
			} else {
				/* For an exponent it's hard to highlight the
				 * right region w/o it turning into an ugly
				 * hack, for now the cursor is put at the end.
				 */
				gnumeric_parse_error (
					state, PERR_OUT_OF_RANGE,
					g_strdup (_("The number is out of range")),
					0, 0);
				return INVALID_TOKEN;
			}
		} else {
			/* This could be a row range ref or an integer */
			char *end;
			long l;

			errno = 0;
			l = strtol (start, &end, 10);
			if (start == end) {
				g_warning ("%s is not an integer, but was expected to be one", start);
			} else if (errno != ERANGE) {
				v = value_new_int (l);
				state->expr_text = end;
			} else if (l == LONG_MIN || l == LONG_MAX) {
				gnum_float d;

				errno = 0;
				d = strtognum (start, &end);
				if (errno != ERANGE) {
					v = value_new_float (d);
					state->expr_text = end;
				} else {
					gnumeric_parse_error (
						state, PERR_OUT_OF_RANGE,
						g_strdup (_("The number is out of range")),
						state->expr_text - state->expr_backup, end - start);
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

	switch (c){
	case '\'':
	case '"': {
		char const *p;
		char *string, *s;
		char quotes_end = c;
		Value *v;

 		p = state->expr_text;
 		state->expr_text = find_char (state->expr_text, quotes_end);
		if (!*state->expr_text) {
  			gnumeric_parse_error (
  				state, PERR_MISSING_CLOSING_QUOTE,
				g_strdup (_("Could not find matching closing quote")),
  				(p - state->expr_backup) + 1, 1);
			return INVALID_TOKEN;
		}

		s = string = (char *) g_alloca (1 + state->expr_text - p);
		while (p != state->expr_text)
			if (*p == '\\') {
				int n = g_utf8_skip [*(guchar *)(++p)];
				strncpy (s, p, n);
				s += n;
				p += n;
			} else
				*s++ = *p++;

		*s = 0;
		state->expr_text++;

		v = value_new_string (string);
		yylval.expr = register_expr_allocation (gnm_expr_new_constant (v));
		return QUOTED_STRING;
	}
	}

	if (g_unichar_isalpha (c) || c == '_' || c == '$'){
		char const *start = state->expr_text - 1;
		gunichar tmp;

		while ((tmp = g_utf8_get_char (state->expr_text)) != 0 &&
		       (g_unichar_isalnum (tmp) || tmp == '_' || tmp == '$' ||
		       (state->use_excel_reference_conventions && tmp == '.')))
			state->expr_text = g_utf8_next_char (state->expr_text);

		yylval.expr = register_expr_allocation (gnm_expr_new_constant (
			value_new_string_nocopy (g_strndup (start, state->expr_text - start))));
		return STRING;
	}

	if (c == '\n' || c == 0)
		return 0;

	if (c == '<'){
		if (*state->expr_text == '='){
			state->expr_text++;
			return LTE;
		}
		if (*state->expr_text == '>'){
			state->expr_text++;
			return NE;
		}
		return c;
	}

	if (c == '>'){
		if (*state->expr_text == '='){
			state->expr_text++;
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
 * take responsibility for freeing that struct and it's contents.
 * with parse_error_free.
 **/
GnmExpr const *
gnm_expr_parse_str (char const *expr_text, ParsePos const *pos,
		    GnmExprParseFlags flags,
		    ParseError *error)
{
	GnmExpr const *expr;
	ParserState pstate;

	g_return_val_if_fail (expr_text != NULL, NULL);

	pstate.expr_text   = expr_text;
	pstate.expr_backup = expr_text;
	pstate.pos	   = pos;

	pstate.decimal_point	   = format_get_decimal ();
	pstate.separator 	   = format_get_arg_sep ();
	pstate.array_col_separator = format_get_col_sep ();

	pstate.use_excel_reference_conventions	   	= !(flags & GNM_EXPR_PARSE_USE_APPLIX_REFERENCE_CONVENTIONS);
	pstate.create_placeholder_for_unknown_func	= flags & GNM_EXPR_PARSE_CREATE_PLACEHOLDER_FOR_UNKNOWN_FUNC;
	pstate.force_absolute_col_references		= flags & GNM_EXPR_PARSE_FORCE_ABSOLUTE_COL_REFERENCES;
	pstate.force_absolute_row_references		= flags & GNM_EXPR_PARSE_FORCE_ABSOLUTE_ROW_REFERENCES;
	pstate.force_explicit_sheet_references		= flags & GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;

	pstate.result = NULL;
	pstate.error = error;
	
	if (deallocate_stack == NULL)
		deallocate_init ();

	g_return_val_if_fail (pstate.pos != NULL, NULL);
	g_return_val_if_fail (pstate.expr_text != NULL, NULL);
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
				gnumeric_parse_error (&pstate, PERR_MULTIPLE_EXPRESSIONS,
					g_strdup (_("Multiple expressions are not supported in this context")),
					(pstate.expr_text - pstate.expr_backup) + 1,
					(pstate.expr_text - pstate.expr_backup));
				expr = NULL;
			}
		} else {
			/* Free the list, do not unref the content */
			expr = pstate.result->data;
			gnm_expr_list_free (pstate.result);
		}
	} else {
		/* If there is no error message, attempt to be more detailed */
		if (pstate.error != NULL && pstate.error->message == NULL) {
			char const *last_token = pstate.expr_text - 1;

			if (*last_token == '\0') {
				char const *str = pstate.expr_backup;
				char const *res = NULL;
				char const *last = find_matching_close (str, &res);

				if (*last)
					gnumeric_parse_error (&pstate, PERR_MISSING_PAREN_OPEN,
						g_strdup (_("Could not find matching opening parenthesis")),
						(last - str) + 2, 1);
				else if (res != NULL)
					gnumeric_parse_error (&pstate, PERR_MISSING_PAREN_CLOSE,
						g_strdup (_("Could not find matching closing parenthesis")),
						(res - str) + 2, 1);
				else
					gnumeric_parse_error (&pstate, PERR_INVALID_EXPRESSION,
						g_strdup (_("Invalid expression")),
						(pstate.expr_text - pstate.expr_backup) + 1,
						(pstate.expr_text - pstate.expr_backup));
			} else
				gnumeric_parse_error (&pstate, PERR_UNEXPECTED_TOKEN,
					g_strdup_printf (_("Unexpected token %c"), *last_token),
					(last_token - pstate.expr_backup) + 1, 1);
		}

		deallocate_all ();

		expr = NULL;
	}

	deallocate_uninit ();

	return expr;
}
