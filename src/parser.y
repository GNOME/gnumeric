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
#include "expr.h"
#include "expr-name.h"
#include "sheet.h"
#include "application.h"
#include "parse-util.h"
#include "gutils.h"
#include "auto-format.h"
#include "style.h"

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
static const ParsePos *parser_pos;

/* The suggested format to use for this expression */
static StyleFormat **parser_desired_format;

/* Locale info.  */
static char parser_decimal_point;
static char parser_separator;
static char parser_array_col_separator;
static gboolean parser_use_excel_reference_conventions;
static gboolean parser_create_place_holder_for_unknown_func;

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
			Value    *v = expr->constant.value;

			g_assert (expr->any.oper == OPER_CONSTANT);

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

static gboolean
parse_string_as_value (ExprTree *str)
{
	char const *txt = str->constant.value->v_str.val->str;
	/*
	 * Try to match the entered text against any of the known number
	 * formating codes, if this succeeds, we store this as a float +
	 * format, otherwise, we return a string.  Be extra careful with empty
	 * strings (""),  They may match some formats ....
	 */
	if (txt[0] != '\0') {
		Value *v;

		if (parser_desired_format && *parser_desired_format == NULL)
			v = format_match (txt, parser_desired_format);
		else
			v = format_match (txt, NULL);

		if (v != NULL) {
			value_release (str->constant.value);
			str->constant.value = v;
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * parse_string_as_value_or_name :
 * @str : An expression with oper constant, whose value is a string.
 *
 * Check to see if a string is a name
 * if it is not check to see if it can be parsed as a value
 */
static ExprTree *
parse_string_as_value_or_name (ExprTree *str)
{
	NamedExpression *expr_name;

	expr_name = expr_name_lookup (parser_pos, str->constant.value->v_str.val->str);
	if (expr_name != NULL) {
		unregister_allocation (str); expr_tree_unref (str);
		return register_expr_allocation (expr_tree_new_name (expr_name));
	}

	/* NOTE : parse_string_as_value modifies str in place */
	parse_string_as_value (str);
	return str;
}

static int
gnumeric_parse_error()
{
	/* TODO : Get rid of ParseErr and replace it with something richer. */
	parser_error = PARSE_ERR_SYNTAX;
	return ERROR;
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
%type  <list>     arg_list array_row, array_cols
%type  <tree>     exp array_exp string_opt_quote
%token <tree>     STRING QUOTED_STRING CONSTANT CELLREF GTE LTE NE
%token            SEPARATOR
%type  <tree>     cellref
%type  <sheet>    sheetref opt_sheetref

%left '&'
%left '<' '>' '=' GTE LTE NE
%left '-' '+'
%left '*' '/'
%left NEG PLUS
%left RANGE_SEP SHEET_SEP
%right '^'
%right '%'

%%
line:	  exp {
		unregister_allocation ($1);
		*parser_result = $1;
	}

	| error 	{ parser_error = PARSE_ERR_SYNTAX; }
	;

exp:	  CONSTANT 	{ $$ = $1; }
	| QUOTED_STRING { $$ = $1; }
	| STRING        { $$ = parse_string_as_value_or_name ($1); }
        | cellref       { $$ = $1; }
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

	| STRING '(' arg_list ')' {
		char *name = $1->constant.value->v_str.val->str;
		FunctionDefinition *f = func_lookup_by_name (name,
			parser_pos->wb);

		/* THINK TODO : Do we want to make this workbook local ?? */
		if (f == NULL && parser_create_place_holder_for_unknown_func)
			f = function_add_placeholder (name, "");

		unregister_allocation ($3);
		unregister_allocation ($1); expr_tree_unref ($1);

		if (f == NULL)
			return gnumeric_parse_error();

		$$ = register_expr_allocation (expr_tree_new_funcall (f, $3));
	}
	| sheetref string_opt_quote {
		NamedExpression *expr_name;
		char *name = $2->constant.value->v_str.val->str;
		ParsePos pos = *parser_pos;
		
		pos.sheet = $1;
		expr_name = expr_name_lookup (&pos, name);
		unregister_allocation ($2); expr_tree_unref ($2);
		if (expr_name == NULL)
			return gnumeric_parse_error();
	        $$ = register_expr_allocation (expr_tree_new_name (expr_name));
	}
	;

string_opt_quote : STRING
		 | QUOTED_STRING
		 ;

sheetref: string_opt_quote SHEET_SEP {
		Sheet *sheet = sheet_lookup_by_name (parser_pos->wb, $1->constant.value->v_str.val->str);
		unregister_allocation ($1); expr_tree_unref ($1);
		if (sheet == NULL)
			return gnumeric_parse_error();
	        $$ = sheet;
	}

	| '[' string_opt_quote ']' string_opt_quote SHEET_SEP {
		/* TODO : Get rid of ParseErr and replace it with something richer.
		 * The replace ment should include more detail as to what the error
		 * was,  and where in the expr string to highlight.
		 *
		 * e.g. for =1+Shhet!A1+2
		 *  We should return "Unknow Sheet 'Sheet'" and the indicies 3:7
		 *  to mark the offending region.
		 */
		Workbook * wb =
		    application_workbook_get_by_name ($2->constant.value->v_str.val->str);
		Sheet *sheet = NULL;

		if (wb != NULL)
			sheet = sheet_lookup_by_name (wb, $4->constant.value->v_str.val->str);

		unregister_allocation ($4); expr_tree_unref ($4);
		unregister_allocation ($2); expr_tree_unref ($2);
		if (sheet == NULL)
			return gnumeric_parse_error();
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
		$2->var.ref.sheet = $1;
	        $$ = $2;
	}

	| CELLREF RANGE_SEP CELLREF {
		unregister_allocation ($3);
		unregister_allocation ($1);
		$$ = register_expr_allocation
			(expr_tree_new_constant
			 (value_new_cellrange (&($1->var.ref), &($3->var.ref),
					       parser_pos->eval.col, parser_pos->eval.row)));
		expr_tree_unref ($3);
		expr_tree_unref ($1);
	}

	| sheetref CELLREF RANGE_SEP opt_sheetref CELLREF {
		unregister_allocation ($5);
		unregister_allocation ($2);
		$2->var.ref.sheet = $1;
		$5->var.ref.sheet = $4 ? $4 : $1;
		$$ = register_expr_allocation
			(expr_tree_new_constant
			 (value_new_cellrange (&($2->var.ref), &($5->var.ref),
					       parser_pos->eval.col, parser_pos->eval.row)));

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
	| SEPARATOR arg_list {
		GList *tmp = $2;
		unregister_allocation ($2);

		if (tmp == NULL)
			tmp = g_list_prepend (NULL, expr_tree_new_constant (value_new_empty ()));

		$$ = g_list_prepend (tmp, expr_tree_new_constant (value_new_empty ()));
		register_expr_list_allocation ($$);
	}
        | { $$ = NULL; }
	;

array_exp: CONSTANT		{ $$ = $1; }
	 | string_opt_quote	{ parse_string_as_value ($1); $$ = $1; }
	 ;

array_row: array_exp {
		unregister_allocation ($1);
		$$ = g_list_prepend (NULL, $1);
		register_expr_list_allocation ($$);
        }
	| array_exp SEPARATOR array_row {
		if (parser_array_col_separator == ',') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_list_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else
			return gnumeric_parse_error();
	}
	| array_exp '\\' array_row {
		if (parser_array_col_separator == '\\') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_list_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else
			return gnumeric_parse_error();
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
/**
 * parse_ref_or_string :
 * @string: the string to try.
 *
 * Attempt to parse the text as a cellref, if it fails
 * return a string.
 * DO NOT attempt to do higher level lookups here.
 *     - sheet names
 *     - function names
 *     - expression names
 *     - value parsing
 * must all be handled by the parser not the lexer.
 */
static int
parse_ref_or_string (char *string)
{
	CellRef   ref;
	Value *v = NULL;

	if (cellref_get (&ref, string, &parser_pos->eval)) {
		yylval.tree = register_expr_allocation (expr_tree_new_var (&ref));
		return CELLREF;
	}

	v = value_new_string (string);
	yylval.tree = register_expr_allocation (expr_tree_new_constant (v));
	return STRING;
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

	if (parser_use_excel_reference_conventions) {
		if (c == ':')
			return RANGE_SEP;
		if (c == '!')
			return SHEET_SEP;
	} else {
		if (c == '.' && *parser_expr == '.') {
			parser_expr++;
			return RANGE_SEP;
		}
		if (c == ':')
			return SHEET_SEP;
	}

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
		return CONSTANT;
	}

	case '\'':
	case '"': {
		char *string, *s;
		char quotes_end = c;
		Value *v;

                p = parser_expr;
                while(*parser_expr && *parser_expr != quotes_end) {
                        if (*parser_expr == '\\' && parser_expr [1])
                                parser_expr++;
                        parser_expr++;
                }
                if (!*parser_expr)
			return gnumeric_parse_error();

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

		v = value_new_string (string);
		yylval.tree = register_expr_allocation (expr_tree_new_constant (v));
		return QUOTED_STRING;
	}
	}

	if (isalpha ((unsigned char)c) || c == '_' || c == '$'){
		const char *start = parser_expr - 1;
		char *str;
		int  len;

		while (isalnum ((unsigned char)*parser_expr) || *parser_expr == '_' ||
		       *parser_expr == '$' ||
		       (parser_use_excel_reference_conventions  && *parser_expr == '.'))
			parser_expr++;

		len = parser_expr - start;
		str = alloca (len + 1);
		strncpy (str, start, len);
		str [len] = 0;
		return parse_ref_or_string (str);
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
gnumeric_expr_parser (const char *expr, const ParsePos *pp,
		      gboolean use_excel_range_conventions,
		      gboolean create_place_holder_for_unknown_func,
		      StyleFormat **desired_format, ExprTree **result)
{
	struct lconv *locinfo;

	g_return_val_if_fail (pp, PARSE_ERR_UNKNOWN);
	g_return_val_if_fail (expr, PARSE_ERR_UNKNOWN);
	g_return_val_if_fail (result, PARSE_ERR_UNKNOWN);

	parser_error = PARSE_OK;
	parser_expr = expr;
	parser_pos  = pp;
	parser_desired_format = desired_format;
	parser_result = result;

	if (parser_desired_format)
		*parser_desired_format = NULL;

	parser_use_excel_reference_conventions = use_excel_range_conventions;
	parser_create_place_holder_for_unknown_func = create_place_holder_for_unknown_func;

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
			StyleFormat *format;
			EvalPos pos;

			pos.sheet = pp->sheet;
			pos.eval = pp->eval;
			format = auto_style_format_suggest (*parser_result, &pos);
			if (format) {
				/*
				 * Override the format that came from a
				 * constant somewhere inside.
				 */
				if (*desired_format)
					style_format_unref (*desired_format);
				*desired_format = format;
			}
		}
	} else {
		fprintf (stderr, "Unable to parse '%s'\n", expr);
		deallocate_all ();
		*parser_result = NULL;
		if (desired_format && *desired_format) {
			style_format_unref (*desired_format);
			*desired_format = NULL;
		}
	}

#ifndef KEEP_DEALLOCATION_STACK_BETWEEN_CALLS
	deallocate_uninit ();
#endif

	return parser_error;
}
