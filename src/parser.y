%{
/*
 * Gnumeric Parser
 *
 * (C) 1998-2000 the Free Software Foundation
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jody Goldberg (jgoldberg@home.com)
 *    Morten Welinder (terra@diku.dk)
 */
#include <config.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <gnome.h>
#include "gnumeric.h"
#include "number-match.h"
#include "symbol.h"
#include "expr.h"
#include "expr-name.h"
#include "sheet.h"
#include "application.h"
#include "utils.h"
#include "auto-format.h"

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
free_expr_list (GList *list)
{
	GList *l;
	for (l = list; l; l = l->next)
		expr_tree_unref (l->data);
	g_list_free (list);
}

static void
free_expr_list_list (GList *list)
{
	GList *l;
	for (l = list; l; l = l->next)
		free_expr_list (l->data);
	g_list_free (list);
}

typedef void (*ParseDeallocator) (void *);
static GPtrArray *deallocate_stack;

static void
deallocate_init (void)
{
	deallocate_stack = g_ptr_array_new ();
}

#ifndef KEEP_DEALLOCATION_STACK_BETWEEN_CALLS
static void
deallocate_uninit (void)
{
	g_ptr_array_free (deallocate_stack, TRUE);
	deallocate_stack = NULL;
}
#endif

static void
deallocate_all (void)
{
	int i;

	for (i = 0; i < deallocate_stack->len; i += 2) {
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
register_allocation (void *data, ParseDeallocator freer)
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
  register_allocation ((expr), (ParseDeallocator)&expr_tree_unref)

#define register_expr_list_allocation(list) \
  register_allocation ((list), (ParseDeallocator)&free_expr_list)

#define register_expr_list_list_allocation(list) \
  register_allocation ((list), (ParseDeallocator)&free_expr_list_list)

static void
unregister_allocation (const void *data)
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

		g_ptr_array_set_size (deallocate_stack, pos);
		return;
	}

	g_warning ("Unbalanced allocation registration");
}

/* ------------------------------------------------------------------------- */

#define ERROR -1

/* Bison/Yacc internals */
static int  yylex (void);
static int  yyerror (char *s);

/* The expression being parsed */
static const char *parser_expr;

/* The error returned from the */
static ParseErr parser_error;

/* Location where the parsing is taking place */
static int parser_col, parser_row;

/* The workbook context */
static Workbook *parser_wb;

/* The suggested format to use for this expression */
static char **parser_desired_format;

/* Locale info.  */
static char parser_decimal_point;
static char parser_separator;
static char parser_array_col_separator;

static ExprTree **parser_result;

static ExprTree *
build_unary_op (Operation op, ExprTree *expr)
{
	unregister_allocation (expr);
	return register_expr_allocation (expr_tree_new_unary (op, expr));
}

static ExprTree *
build_binop (ExprTree *l, Operation op, ExprTree *r)
{
	unregister_allocation (r);
	unregister_allocation (l);
	return register_expr_allocation (expr_tree_new_binary (l, op, r));
}

static ExprTree *
build_array_formula (ExprTree *func, int cols, int rows, int x, int y)
{
	ExprTree *res = expr_tree_array_formula (x, y, rows, cols);

	/*
	 * Note: for a non-corner cell, caller must arrange to have the
	 * inner expression ("func") unref'ed.  This happens in
	 * cell_set_formula.
	 */
	res->u.array.corner.func.expr = func;
	return res;
}

static ExprTree *
build_array (GList *cols)
{
	Value *array;
	GList *row;
	int x, mx, y;

	if (!cols) {
		parser_error = PARSE_ERR_SYNTAX;
		return NULL;
	}

	mx  = 0;
	row = cols->data;
	while (row) {
		mx++;
		row = g_list_next (row);
	}

	array = value_new_array_empty (mx, g_list_length (cols));

	y = 0;
	while (cols) {
		row = cols->data;
		x = 0;
		while (row && x < mx) {
			ExprTree *expr = row->data;
			Value    *v = expr->u.constant;

			g_assert (expr->oper == OPER_CONSTANT);

			value_array_set (array, x, y, value_duplicate (v));

			x++;
			row = g_list_next (row);
		}
		if (x < mx || row) {
			parser_error = PARSE_ERR_SYNTAX;
			value_release (array);
			return NULL;
		}
		y++;
		cols = g_list_next (cols);
	}

	return register_expr_allocation (expr_tree_new_constant (array));
}

/* Make byacc happier */
int yyparse(void);

%}

%union {
	ExprTree *tree;
	CellRef  *cell;
	GList    *list;
	Sheet	 *sheet;
}
%type  <tree>     exp array_exp
%type  <list>     arg_list array_row, array_cols
%token <tree>     NUMBER STRING FUNCALL CONSTANT CELLREF GTE LTE NE
%token            SEPARATOR
%type  <tree>     cellref
%type  <sheet>    sheetref opt_sheetref

%left '&'
%left '<' '>' '=' GTE LTE NE
%left '-' '+'
%left '*' '/'
%left NEG PLUS
%left '!'
%right '^'
%right '%'

%%
line:	  exp {
		unregister_allocation ($1);
		*parser_result = $1;
	}

        | '{' exp '}' '(' NUMBER SEPARATOR NUMBER ')' '[' NUMBER ']' '[' NUMBER ']' {
		const int num_cols = expr_tree_get_const_int ($7);
		const int num_rows = expr_tree_get_const_int ($5);
		const int x = expr_tree_get_const_int ($13);
		const int y = expr_tree_get_const_int ($10);

		/*
		 * Notice that we have no use for the ExprTrees for the NUMBERS,
		 * so we deallocate them.
		 */
		unregister_allocation ($13);
		expr_tree_unref ($13);
		unregister_allocation ($10);
		expr_tree_unref ($10);
		unregister_allocation ($7);
		expr_tree_unref ($7);
		unregister_allocation ($5);
		expr_tree_unref ($5);

		unregister_allocation ($2);
		*parser_result = build_array_formula ($2, num_cols, num_rows, x, y);
	}

	| error 	{ parser_error = PARSE_ERR_SYNTAX; }
	;

exp:	  NUMBER 	{ $$ = $1; }
	| STRING        { $$ = $1; }
        | cellref       { $$ = $1; }
	| CONSTANT      { $$ = $1; }
	| exp '+' exp	{ $$ = build_binop ($1, OPER_ADD,       $3); }
	| exp '-' exp	{ $$ = build_binop ($1, OPER_SUB,       $3); }
	| exp '*' exp	{ $$ = build_binop ($1, OPER_MULT,      $3); }
	| exp '/' exp	{ $$ = build_binop ($1, OPER_DIV,       $3); }
	| exp '^' exp	{ $$ = build_binop ($1, OPER_EXP,       $3); }
	| exp '&' exp	{ $$ = build_binop ($1, OPER_CONCAT,    $3); }
	| exp '=' exp	{ $$ = build_binop ($1, OPER_EQUAL,     $3); }
	| exp '<' exp	{ $$ = build_binop ($1, OPER_LT,        $3); }
	| exp '>' exp	{ $$ = build_binop ($1, OPER_GT,        $3); }
	| exp GTE exp	{ $$ = build_binop ($1, OPER_GTE,       $3); }
	| exp NE  exp	{ $$ = build_binop ($1, OPER_NOT_EQUAL, $3); }
	| exp LTE exp	{ $$ = build_binop ($1, OPER_LTE,       $3); }
	| '(' exp ')'   { $$ = $2; }

        | exp '%' { $$ = build_unary_op (OPER_PERCENT, $1); }
        | '-' exp %prec NEG { $$ = build_unary_op (OPER_UNARY_NEG, $2); }
        | '+' exp %prec PLUS { $$ = build_unary_op (OPER_UNARY_PLUS, $2); }

        | '{' array_cols '}' {
		unregister_allocation ($2);
		$$ = build_array ($2);
		free_expr_list_list ($2);
	}

	| FUNCALL '(' arg_list ')' {
		unregister_allocation ($3);
		$$ = $1;
		$$->u.function.arg_list = $3;
	}
	;

sheetref: STRING '!' {
		Sheet *sheet = sheet_lookup_by_name (parser_wb, $1->u.constant->v.str->str);
		/* TODO : Get rid of ParseErr and replace it with something richer. */
		unregister_allocation ($1); expr_tree_unref ($1);
		if (sheet == NULL) {
			parser_error = PARSE_ERR_SYNTAX;
                        return ERROR;
		}
	        $$ = sheet;
	}

	| '[' STRING ']' STRING '!' {
		/* TODO : Get rid of ParseErr and replace it with something richer.
		 * The replace ment should include more detail as to what the error
		 * was,  and where in the expr string to highlight.
		 *
		 * e.g. for =1+Shhet!A1+2
		 *  We should return "Unknow Sheet 'Shhet'" and the indicies 3:7
		 *  to mark the offending region.
		 */
		Workbook * wb =
		    application_workbook_get_by_name ($2->u.constant->v.str->str);
		Sheet *sheet = NULL;

		if (wb != NULL)
			sheet = sheet_lookup_by_name (wb, $4->u.constant->v.str->str);

		unregister_allocation ($4); expr_tree_unref ($4);
		unregister_allocation ($2); expr_tree_unref ($2);
		if (sheet == NULL) {
			/* TODO : Do we need to free things here too ? */
			parser_error = PARSE_ERR_SYNTAX;
                        return ERROR;
		}
	        $$ = sheet;
        }
	;

opt_sheetref: sheetref
	    | { $$ = NULL; }
	    ;

cellref:  CELLREF {
	        $$ = $1;
	}

	| sheetref CELLREF {
		$2->u.ref.sheet = $1;
	        $$ = $2;
	}

	| CELLREF ':' CELLREF {
		unregister_allocation ($3);
		unregister_allocation ($1);
		$$ = register_expr_allocation
			(expr_tree_new_constant
			 (value_new_cellrange (&($1->u.ref), &($3->u.ref),
					       parser_col, parser_row)));
		expr_tree_unref ($3);
		expr_tree_unref ($1);
	}

	| sheetref CELLREF ':' opt_sheetref CELLREF {
		unregister_allocation ($5);
		unregister_allocation ($2);
		$2->u.ref.sheet = $1;
		$5->u.ref.sheet = $4 ? $4 : $1;
		$$ = register_expr_allocation
			(expr_tree_new_constant
			 (value_new_cellrange (&($2->u.ref), &($5->u.ref),
					       parser_col, parser_row)));

		expr_tree_unref ($5);
		expr_tree_unref ($2);
	}
	;

arg_list: exp {
		unregister_allocation ($1);
		$$ = g_list_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }
	| exp SEPARATOR arg_list {
		unregister_allocation ($3);
		unregister_allocation ($1);
		$$ = g_list_prepend ($3, $1);
		register_expr_list_allocation ($$);
	}
        | { $$ = NULL; }
	;

array_exp:	  NUMBER 	{ $$ = $1; }
		| STRING        { $$ = $1; }
	;

array_row: array_exp {
		unregister_allocation ($1);
		$$ = g_list_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }
	| array_exp ',' array_row {
		if (parser_array_col_separator == ',') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_list_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else
			parser_error = PARSE_ERR_SYNTAX;
	}
	| array_exp '\\' array_row {
		if (parser_array_col_separator == '\\') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_list_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else
			parser_error = PARSE_ERR_SYNTAX;
	}
        | { $$ = NULL; }
	;

array_cols: array_row {
		unregister_allocation ($1);
		$$ = g_list_prepend (NULL, $1);
		register_expr_list_list_allocation ($$);
        }
        | array_row ';' array_cols {
		unregister_allocation ($3);
		unregister_allocation ($1);
		$$ = g_list_prepend ($3, $1);
		register_expr_list_list_allocation ($$);
	}
	;
%%

static int
return_cellref (char *p)
{
	CellRef   ref;

	if (cellref_get (&ref, p, parser_col, parser_row)) {
		yylval.tree = register_expr_allocation (expr_tree_new_var (&ref));
		return CELLREF;
	} else
		return 0;
}

static int
make_string_return (char const *string, gboolean const possible_number)
{
	Value *v;
	double fv;
	char *format;

	/*
	 * Try to match the entered text against any
	 * of the known number formating codes, if this
	 * succeeds, we store this as a float + format,
	 * otherwise, we return a string.
	 * Be extra careful with empty strings (""),  They may
	 * match some formats ....
	 */
	if (possible_number && string[0] != '\0' &&
	    format_match (string, &fv, &format)){
		v = value_new_float (fv);
		if (parser_desired_format && *parser_desired_format == NULL)
			*parser_desired_format = g_strdup (format);
	} else
		v = value_new_string (string);

	yylval.tree = register_expr_allocation (expr_tree_new_constant (v));
	return STRING;
}

static int
return_symbol (Symbol *sym)
{
	ExprTree *e = NULL;
	int type = STRING;

	switch (sym->type){
	case SYMBOL_FUNCTION:
		symbol_ref (sym);
		e = expr_tree_new_funcall (sym, NULL);
		type = FUNCALL;
		break;

	case SYMBOL_VALUE:
	case SYMBOL_STRING: {
		e = expr_tree_new_constant (value_duplicate (sym->data));
		type = CONSTANT;
		break;
	}

	default:
		g_assert_not_reached ();
	} /* switch */

	yylval.tree = register_expr_allocation (e);
	return type;
}

static int
return_name (ExprName *exprn)
{
	yylval.tree = register_expr_allocation (expr_tree_new_name (exprn));
	return CONSTANT;
}

/**
 * try_symbol:
 * @string: the string to try.
 * @try_cellref_and_number: If it may be a cellref or a number.
 *
 * Attempts to figure out what @string refers to.
 * if @try_cellref_and_number is TRUE it will also attempt to match the
 * string as a cellname reference or a number.
 */
static int
try_symbol (char *string, gboolean try_cellref_and_number)
{
	Symbol *sym;
	int v;

	if (try_cellref_and_number){
		v = return_cellref (string);
		if (v)
			return v;
	}

	sym = symbol_lookup (global_symbol_table, string);
	if (sym)
		return return_symbol (sym);

	{ /* Name ? */
		/*
		 * FIXME: we need a good bit of work to get sheet
		 * scope names working well
		 */
		ExprName *name = expr_name_lookup (parser_wb, NULL,
						   string);
		if (name)
			return return_name (name);
	}

	return make_string_return (string, try_cellref_and_number);
}

int
yylex (void)
{
	int c;
	const char *p, *tmp;
	int is_float, digits;

        while(isspace ((unsigned char)*parser_expr))
                parser_expr++;

	c = *parser_expr++;
        if (c == '(' || c == ')')
                return c;

	if (c == parser_separator)
		return SEPARATOR;

	/* Translate locale's decimal marker into a dot.  */
	if (c == parser_decimal_point)
		c = '.';

	switch (c){
        case '0': case '1': case '2': case '3': case '4': case '5':
	case '6': case '7': case '8': case '9': case '.': {
		Value *v;

		is_float = c == '.';
		p = parser_expr-1;
		tmp = parser_expr;

		digits = 1;
		while (isdigit ((unsigned char)*tmp) ||
		       (!is_float && *tmp == parser_decimal_point && ++is_float)){
			tmp++;
			digits++;
		}

		/* Can't store it in a gint32 */
		is_float |= (digits > 9);

		if (*tmp == 'e' || *tmp == 'E') {
			is_float = 1;
			tmp++;
			if (*tmp == '-' || *tmp == '+')
				tmp++;
			while (isdigit ((unsigned char)*tmp))
				tmp++;
		}

		/* Ok, we have skipped over a number, now load its value */
		if (is_float) {
			float_t f;
			float_get_from_range (p, tmp, &f);
			v = value_new_float (f);
		} else {
			int i;
			int_get_from_range (p, tmp, &i);
			v = value_new_int (i);
		}

		/* Return the value to the parser */
		yylval.tree = register_expr_allocation (expr_tree_new_constant (v));
		parser_expr = tmp;
		return NUMBER;
	}

	case '#' :
	{
		int offset = 0;
		/* we already took the leading '#' off */
		Value *err = value_is_error (parser_expr-1, &offset);
		if (err != NULL) {
			yylval.tree = register_expr_allocation (expr_tree_new_constant (err));
			parser_expr += offset - 1;
			return CONSTANT;
		}
	}
	break;

	case '\'':
	case '"': {
		char *string, *s;
		char quotes_end = c;

                p = parser_expr;
                while(*parser_expr && *parser_expr != quotes_end) {
                        if (*parser_expr == '\\' && parser_expr [1])
                                parser_expr++;
                        parser_expr++;
                }
                if(!*parser_expr){
                        parser_error = PARSE_ERR_NO_QUOTE;
                        return ERROR;
                }

		s = string = (char *) alloca (1 + parser_expr - p);
		while (p != parser_expr){
			if (*p== '\\'){
				p++;
				*s++ = *p++;
			} else
				*s++ = *p++;

		}
		*s = 0;
		parser_expr++;

		return make_string_return (string, FALSE);
	}

	}

	if (isalpha ((unsigned char)c) || c == '_' || c == '$'){
		const char *start = parser_expr - 1;
		char *str;
		int  len;

		while (isalnum ((unsigned char)*parser_expr) || *parser_expr == '_' ||
		       *parser_expr == '$' || *parser_expr == '.')
			parser_expr++;

		len = parser_expr - start;
		str = alloca (len + 1);
		strncpy (str, start, len);
		str [len] = 0;
		return try_symbol (str, TRUE);
	}

	if (c == '\n' || c == 0)
		return 0;

	if (c == '<'){
		if (*parser_expr == '='){
			parser_expr++;
			return LTE;
		}
		if (*parser_expr == '>'){
			parser_expr++;
			return NE;
		}
		return c;
	}

	if (c == '>'){
		if (*parser_expr == '='){
			parser_expr++;
			return GTE;
		}
		return c;
	}

	return c;
}

int
yyerror (char *s)
{
#if 0
	printf ("Error: %s\n", s);
#endif
	return 0;
}

ParseErr
gnumeric_expr_parser (const char *expr, const ParsePosition *pp,
		      char **desired_format, ExprTree **result)
{
	struct lconv *locinfo;

	g_return_val_if_fail (pp, PARSE_ERR_UNKNOWN);
	g_return_val_if_fail (expr, PARSE_ERR_UNKNOWN);
	g_return_val_if_fail (result, PARSE_ERR_UNKNOWN);

	parser_error = PARSE_OK;
	parser_expr = expr;
	parser_wb    = pp->wb;
	parser_col   = pp->col;
	parser_row   = pp->row;
	parser_desired_format = desired_format;
	parser_result = result;

	if (parser_desired_format)
		*parser_desired_format = NULL;

	locinfo = localeconv ();
	if (locinfo->decimal_point && locinfo->decimal_point[0] &&
	    locinfo->decimal_point[1] == 0)
		parser_decimal_point = locinfo->decimal_point[0];
	else
		parser_decimal_point = '.';

	if (parser_decimal_point == ',') {
		parser_separator = ';';
		parser_array_col_separator = '\\'; /* ! */
	} else {
		parser_separator = ',';
		parser_array_col_separator = ',';
	}

	if (deallocate_stack == NULL)
		deallocate_init ();

	yyparse ();

	if (parser_error == PARSE_OK) {
		deallocate_assert_empty ();
		if (desired_format) {
			char *format;
			EvalPosition pos;

			pos.sheet = pp->sheet;
			pos.eval.col = pp->col;
			pos.eval.row = pp->row;
			format = auto_format_suggest (*parser_result, &pos);
			if (format) {
				/*
				 * Override the format that came from a
				 * constant somewhere inside.
				 */
				if (*desired_format)
					g_free (*desired_format);
				*desired_format = format;
			}
		}
	} else {
		fprintf (stderr, "Unable to parse '%s'\n", expr);
		deallocate_all ();
		*parser_result = NULL;
		if (desired_format && *desired_format) {
			g_free (*desired_format);
			*desired_format = NULL;
		}
	}

#ifndef KEEP_DEALLOCATION_STACK_BETWEEN_CALLS
	deallocate_uninit ();
#endif

	return parser_error;
}
