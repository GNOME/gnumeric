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
 *    Almer S. Tigelaar (almer@gnome.org)
 */
#include <config.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <gnome.h>
#include "gnumeric.h"
#include "number-match.h"
#include "expr.h"
#include "expr-name.h"
#include "sheet.h"
#include "format.h"
#include "application.h"
#include "parse-util.h"
#include "gutils.h"
#include "auto-format.h"
#include "style.h"
#include "portability.h"

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

		g_ptr_array_set_size (deallocate_stack, pos + 2);
		return;
	}

	g_warning ("Unbalanced allocation registration");
}

/* ------------------------------------------------------------------------- */

#define ERROR -1

/* Bison/Yacc internals */
static int  yylex (void);
static int  yyerror (char *s);

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
	char decimal_point;
	char separator;
	char array_col_separator;

	/* flags */
	gboolean use_excel_reference_conventions;
	gboolean create_place_holder_for_unknown_func;

	/* The suggested format to use for this expression */
	StyleFormat **desired_format;
	ExprTree *result;

	ParseError *error;
} ParserState;

/* The error returned from the */
static ParserState *state;

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
		/* parser_error = PARSE_ERR_SYNTAX; */
		return NULL;
	}

	mx  = 0;
	row = cols->data;
	while (row) {
		mx++;
		row = row->next;
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

	return register_expr_allocation (expr_tree_new_constant (array));
}

static gboolean
parse_string_as_value (ExprTree *str)
{
	/*
	 * Try to parse the entered text as a basic value (empty, bool, int,
	 * float, err) if this succeeds, we store this as a Value otherwise, we
	 * return a string.
	 */
	char const *txt = str->constant.value->v_str.val->str;
	Value *v = format_match_simple (txt);

	if (v != NULL) {
		value_release (str->constant.value);
		str->constant.value = v;
		return TRUE;
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

	expr_name = expr_name_lookup (state->pos, str->constant.value->v_str.val->str);
	if (expr_name != NULL) {
		unregister_allocation (str); expr_tree_unref (str);
		return register_expr_allocation (expr_tree_new_name (expr_name));
	}

	/* NOTE : parse_string_as_value modifies str in place */
	parse_string_as_value (str);
	return str;
}

static int
gnumeric_parse_error (ParserState *state, char *message, int end, int relative_begin)
{
	g_return_val_if_fail (state->error != NULL, ERROR);

	state->error->message    = message;
	state->error->begin_char = (end - relative_begin);
	state->error->end_char   = end;

	return ERROR;
}

/* Make byacc happier */
int yyparse (void);

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
		state->result = $1;
	}

	| error 	{
		if (state->result != NULL) {
			expr_tree_unref (state->result);
			state->result = NULL;
		}
	}
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
		const char *name = $1->constant.value->v_str.val->str;
		FunctionDefinition *f = func_lookup_by_name (name,
			state->pos->wb);

		/* THINK TODO: Do we want to make this workbook-local??  */
		if (f == NULL && state->create_place_holder_for_unknown_func)
			f = function_add_placeholder (name, "");

		unregister_allocation ($3);
		unregister_allocation ($1); expr_tree_unref ($1);

		if (f == NULL) {
			free_expr_list ($3);
			YYERROR;
		} else {
			$$ = register_expr_allocation (expr_tree_new_funcall (f, $3));
		}
	}
	| sheetref string_opt_quote {
		NamedExpression *expr_name;
		char *name = $2->constant.value->v_str.val->str;
		ParsePos pos = *state->pos;

		pos.sheet = $1;
		expr_name = expr_name_lookup (&pos, name);
		if (expr_name == NULL) {
			int retval = gnumeric_parse_error (
				state, g_strdup_printf (_("Expression '%s' does not exist on sheet '%s'"), name, $1->name_quoted),
				state->expr_text - state->expr_backup + 1, strlen (name));
				
			unregister_allocation ($2); expr_tree_unref ($2);
			return retval;
		} else
			unregister_allocation ($2); expr_tree_unref ($2);
	        $$ = register_expr_allocation (expr_tree_new_name (expr_name));
	}
	;

string_opt_quote : STRING
		 | QUOTED_STRING
		 ;

sheetref: string_opt_quote SHEET_SEP {
	        char  *name = $1->constant.value->v_str.val->str;
		Sheet *sheet = sheet_lookup_by_name (state->pos->wb, name);
		if (sheet == NULL) {
			int retval = gnumeric_parse_error (
				state, g_strdup_printf (_("Unknown sheet '%s'"), name),
				state->expr_text - state->expr_backup, strlen (name));
				
			unregister_allocation ($1); expr_tree_unref ($1);
			return retval;
		} else
			unregister_allocation ($1); expr_tree_unref ($1);
	        $$ = sheet;
	}

	| '[' string_opt_quote ']' string_opt_quote SHEET_SEP {
		/* TODO : Get rid of ParseErr and replace it with something richer.
		 * The replace ment should include more detail as to what the error
		 * was,  and where in the expr string to highlight.
		 *
		 * e.g. for =1+Sheet!A1+2
		 *  We should return "Unknow Sheet 'Sheet'" and the indicies 3:7
		 *  to mark the offending region.
		 */
		Workbook * wb =
		    application_workbook_get_by_name ($2->constant.value->v_str.val->str);
		Sheet *sheet = NULL;
		char *sheetname = $4->constant.value->v_str.val->str;

		if (wb != NULL)
			sheet = sheet_lookup_by_name (wb, sheetname);
			
		unregister_allocation ($2); expr_tree_unref ($2);
		if (sheet == NULL) {
			int retval = gnumeric_parse_error (
				state, g_strdup_printf (_("Unknown sheet '%s'"), sheetname),
				state->expr_text - state->expr_backup, strlen (sheetname));
				
			unregister_allocation ($4); expr_tree_unref ($4);
			return retval;
		} else
			unregister_allocation ($4); expr_tree_unref ($4);

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
					       state->pos->eval.col, state->pos->eval.row)));
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
					       state->pos->eval.col, state->pos->eval.row)));

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
		if (state->array_col_separator == ',') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_list_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else {
			return gnumeric_parse_error (
				state, g_strdup_printf (_("The character %c can not be used to separate array elements"),
				state->array_col_separator), state->expr_text - state->expr_backup + 1, 1);
		}
	}
	| array_exp '\\' array_row {
		if (state->array_col_separator == '\\') {
			unregister_allocation ($3);
			unregister_allocation ($1);
			$$ = g_list_prepend ($3, $1);
			register_expr_list_allocation ($$);
		} else {
			/* FIXME: Is this the right error to display? */
			return gnumeric_parse_error (
				state, g_strdup_printf (_("The character %c can not be used to separate array elements"),
				state->array_col_separator), state->expr_text - state->expr_backup + 1, 1);
		}
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
parse_ref_or_string (const char *string)
{
	CellRef   ref;
	Value *v = NULL;

	if (cellref_get (&ref, string, &state->pos->eval)) {
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
	const char *start;
	gboolean is_number = FALSE;

        while (isspace ((unsigned char)*state->expr_text))
                state->expr_text++;

	start = state->expr_text;
	c = (unsigned char) (*state->expr_text++);
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
	}

	if (c == state->separator)
		return SEPARATOR;

	if (c == state->decimal_point) {
		/* Could be a number or a stand alone  */
		if (!isdigit ((unsigned char)(*state->expr_text)))
			return c;
		is_number = TRUE;
	} else if (isdigit (c)) {
		while (isdigit ((c = (unsigned char)(*state->expr_text++))))
			;
		is_number = TRUE;
	}

	if (is_number) {
		Value *v = NULL;

		if (c == state->decimal_point || tolower (c) == 'e') {
			/* This is float */
			char *end;
			double d;

			errno = 0;
			d = strtod (start, &end);
			if (start != end) {
				if (errno != ERANGE) {
					v = value_new_float ((gnum_float)d);
					state->expr_text = end;
				} else {
					if (tolower (c) != 'e') {
						return gnumeric_parse_error (
							state, g_strdup (_("The number is out of range")),
							state->expr_text - state->expr_backup, end - start);
					} else {
						/*
						 * For an exponent it's hard to highlight
						 * the right region w/o it turning into an
						 * ugly hack, for now the cursor is put
						 * at the end.
						 */
						return gnumeric_parse_error (
							state, g_strdup (_("The number is out of range")),
							0, 0);
					}
				}
			} else
				g_warning ("%s is not a double, but was expected to be one", start);
		} else {
			/* This could be a row range ref or an integer */
			char *end;
			long l;

			errno = 0;
			l = strtol (start, &end, 10);
			if (start != end) {
				if (errno != ERANGE) {
					/* Check for a Row range ref (3:4 == A3:IV4) */
					if (*end == ':' && l < SHEET_MAX_COLS) {
					    /* TODO : adjust parser to allow returning
					     * a range, not just a cellref
					     */
					}
					v = value_new_int (l);
					state->expr_text = end;
				} else {
					if (l == LONG_MIN || l == LONG_MAX) {
						return gnumeric_parse_error (
							state, g_strdup (_("The number is out of range")),
							state->expr_text - state->expr_backup, end - start);
					}
				}
			} else
				g_warning ("%s is not an integer, but was expected to be one", start);
		}

		/* Very odd string,  Could be a bound problem.  Trigger an error */
		if (v == NULL)
			return c;

		yylval.tree = register_expr_allocation (expr_tree_new_constant (v));
		return CONSTANT;
	}

	switch (c){
	case '\'':
	case '"': {
		const char *p;
		char *string, *s;
		char quotes_end = c;
		Value *v;

                p = state->expr_text;
                while(*state->expr_text && *state->expr_text != quotes_end) {
                        if (*state->expr_text == '\\' && state->expr_text [1])
                                state->expr_text++;
                        state->expr_text++;
                }
                if (!*state->expr_text) {
			return gnumeric_parse_error (
				state, g_strdup (_("Could not find matching closing quote")),
				(p - state->expr_backup) + 1, 1);
		}

		s = string = (char *) alloca (1 + state->expr_text - p);
		while (p != state->expr_text){
			if (*p== '\\'){
				p++;
				*s++ = *p++;
			} else
				*s++ = *p++;
		}
		*s = 0;
		state->expr_text++;

		v = value_new_string (string);
		yylval.tree = register_expr_allocation (expr_tree_new_constant (v));
		return QUOTED_STRING;
	}
	}

	if (isalpha ((unsigned char)c) || c == '_' || c == '$'){
		const char *start = state->expr_text - 1;
		char *str;
		int  len;

		while (isalnum ((unsigned char)*state->expr_text) || *state->expr_text == '_' ||
		       *state->expr_text == '$' ||
		       (state->use_excel_reference_conventions && *state->expr_text == '.'))
			state->expr_text++;

		len = state->expr_text - start;
		str = alloca (len + 1);
		strncpy (str, start, len);
		str [len] = 0;
		return parse_ref_or_string (str);
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
yyerror (char *s)
{
#if 0
	printf ("Error: %s\n", s);
#endif
	return 0;
}

ExprTree *
gnumeric_expr_parser (char const *expr_text, ParsePos const *pos,
		      gboolean use_excel_range_conventions,
		      gboolean create_place_holder_for_unknown_func,
		      StyleFormat **desired_format,
		      ParseError *error)
{
	ParserState pstate;

	pstate.expr_text   = expr_text;
	pstate.expr_backup = expr_text;
	pstate.pos	   = pos;

	pstate.decimal_point	   = format_get_decimal ();
	pstate.separator 	   = format_get_arg_sep ();
	pstate.array_col_separator = format_get_col_sep ();

	pstate.use_excel_reference_conventions	    = use_excel_range_conventions;
	pstate.create_place_holder_for_unknown_func = create_place_holder_for_unknown_func;

	pstate.result = NULL;
	pstate.desired_format = desired_format;
	if (pstate.desired_format)
		*pstate.desired_format = NULL;

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
		if (desired_format) {
			StyleFormat *format;
			EvalPos tmp;

			tmp.sheet = pos->sheet;
			tmp.eval = pos->eval;
			format = auto_style_format_suggest (pstate.result, &tmp);
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
#if 0
		fprintf (stderr, "Unable to parse '%s'\n", expr_text);
#endif
		deallocate_all ();
		if (desired_format && *desired_format) {
			style_format_unref (*desired_format);
			*desired_format = NULL;
		}
	}

#ifndef KEEP_DEALLOCATION_STACK_BETWEEN_CALLS
	deallocate_uninit ();
#endif
	/*
	 * If an error has occured we ALWAYS return NULL.
	 * In some cases the expression tree may have been partially
	 * built-up before an error occurs. Therefore the pstate.result
	 * (which is resulting ExprTree) must be freed in case it's non-null
	 * and an error has occured.
	 */
	if (pstate.error->message != NULL) {
		if (pstate.result)
			expr_tree_unref (pstate.result);

		return NULL;
	} else
		return pstate.result;
}
