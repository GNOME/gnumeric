%{
/*
 * Gnumeric Parser
 *
 * (C) 1998 the Free Software Foundation
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 */
	
#include <glib.h>
#include <ctype.h>
#include <string.h>
#include "numbers.h"
#include "symbol.h"
#include "str.h"
#include "expr.h"
#include "utils.h"

/* Allocation with disposal-on-error */ 
static void *alloc_buffer    (int size);
static void register_symbol  (Symbol *sym);
static void alloc_clean      (void);
static void alloc_glist      (GList *l); 
static void forget_glist     (GList *list);
static void forget_tree      (ExprTree *tree);
static void alloc_list_free  (void); 
static void *v_new (void);
	
#define ERROR -1
 
/* Types of items we know how to dispose on error */ 
typedef enum {
	ALLOC_SYMBOL,
	ALLOC_VALUE,
	ALLOC_BUFFER,
	ALLOC_LIST
} AllocType;

/* How we keep track of them */ 
typedef struct {
	AllocType type;
	void      *data;
} AllocRec;

/* This keeps a list of  AllocRecs */
static GList *alloc_list;
 
/* Debugging */ 
static void dump_tree (ExprTree *tree);

/* Bison/Yacc internals */ 
static int  yylex (void);
static int  yyerror (char *s);

#define p_new(type) \
	((type *) alloc_buffer ((unsigned) sizeof (type)))
		
%}

%union {
	ExprTree *tree;
	CellRef  *cell;
	GList    *list;
}
%type  <tree>  exp
%type  <list>  arg_list
%token <tree>  NUMBER STRING FUNCALL CELLREF GTE LTE NE

%left '<' '>' '=' GTE LTE NE
%left '-' '+' '&'
%left '*' '/'
%left NEG
%left '!'
%right '^'

%%
line:	  exp           { parser_result = $1;
                          /* dump_tree (parser_result);*/
                          alloc_list_free (); 
                        }
	| error 	{
				alloc_clean ();
				parser_error = PARSE_ERR_SYNTAX;
			}
	;

exp:	  NUMBER 	{ $$ = $1 }
        | CELLREF       { $$ = $1 }
	| exp '+' exp	{
		$$ = p_new (ExprTree);
		$$->oper = OP_ADD;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| exp '-' exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_SUB;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| exp '*' exp { 
		$$ = p_new (ExprTree);
		$$->oper = OP_MULT;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| exp '/' exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_DIV;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| exp '=' exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_EQUAL;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}
	| exp '<' exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_LT;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}
	| exp '>' exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_GT;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

        | exp GTE exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_GTE;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

        | exp NE exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_NOT_EQUAL;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

        | exp LTE exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_LTE;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| '(' exp ')'  {
		$$ = p_new (ExprTree);
		$$ = $2;
	}

        | '-' exp %prec NEG {
		$$ = p_new (ExprTree);
		$$->oper = OP_NEG;
		$$->u.value = $2;
	}
	| exp '&' exp {
		$$ = p_new (ExprTree);
		$$->oper = OP_CONCAT;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}
	;

        | CELLREF ':' CELLREF {
		CellRef a, b;

		a = $1->u.constant->v.cell;
		b = $3->u.constant->v.cell;

		$$ = p_new (ExprTree);
		$$->oper = OP_CONSTANT;
		$$->u.constant = v_new ();
		$$->u.constant->type = VALUE_CELLRANGE;
		$$->u.constant->v.cell_range.cell_a = a;
		$$->u.constant->v.cell_range.cell_b = b;

		forget_tree ($1);
		forget_tree ($3);
	}

	| FUNCALL '(' arg_list ')' {
		$$ = $1;
		$$->u.function.arg_list = $3;
	}
	;

arg_list: exp {
		$$ = g_list_prepend (NULL, $1);
		alloc_glist ($$);
        }
	| exp ',' arg_list {
		forget_glist ($3);
		$$ = g_list_prepend ($3, $1);
		alloc_glist ($$);
	}
        | { $$ = NULL; }
	;

%%

static int
return_cellref (char *p)
{
	int col_relative = TRUE;
	int row_relative = TRUE;
	int col = 0;
	int row = 0;
	ExprTree *e;
	Value    *v;
	CellRef  *ref;

	/* Try to parse a column */
	if (*p == '$'){
		col_relative = FALSE;
		p++;
	}
	if (!(toupper (*p) >= 'A' && toupper (*p) <= 'Z'))
		return 0;

	col = toupper (*p++) - 'A';
	
	if (toupper (*p) >= 'A' && toupper (*p) <= 'Z')
		col = col * ('Z'-'A') + toupper (*p++) - 'A';

	/* Try to parse a row */
	if (*p == '$'){
		row_relative = FALSE;
		p++;
	}
	
	if (!(*p >= '1' && *p <= '9'))
		return 0;

	while (isdigit (*p)){
		row = row * 10 + *p - '0';
		p++;
	}
	row--;

	/*  Ok, parsed successfully, create the return value */
	e = p_new (ExprTree);
	v = v_new ();

	e->oper = OP_VAR;

	ref = &v->v.cell;

	/* Setup the cell reference information */
	if (row_relative)
		ref->row = parser_row - row;

	if (col_relative)
		ref->col = parser_col - col;
	
	ref->col = col;
	ref->row = row;
	ref->col_relative = col_relative;
	ref->row_relative = row_relative;
	e->u.constant = v;
	
	yylval.tree = e;

	return CELLREF;
}

static int
return_symbol (char *string)
{
	ExprTree *e = p_new (ExprTree);
	Symbol *sym;
	int type;
	
	sym = symbol_lookup (string);
	type = STRING;
	if (!sym){
		Value *v = v_new ();
		
		v->v.str = string_get (string);
		v->type = VALUE_STRING;

		e->oper = OP_CONSTANT;
		e->u.constant = v;
	} else {
		symbol_ref (sym);
		if (sym->type == SYMBOL_FUNCTION)
			type = FUNCALL;
		else {
			g_warning ("Unreachable\n");
			type = -1;
		}
		e->oper = OP_FUNCALL;
		e->u.function.symbol = sym;
		e->u.function.arg_list = NULL;
		register_symbol (sym);
	}
	
	yylval.tree = e;
	return type;
}

static int
try_symbol (char *string)
{
	int v;
	
	v = return_cellref (string);
	if (v)
		return v;

	return return_symbol (string);
}

int yylex (void)
{
	int c;
	char *p, *tmp;
	int is_float;
	
        while(isspace (*parser_expr))
                parser_expr++;                                                                                       

	c = *parser_expr++;
        if (c == '(' || c == ',' || c == ')')
                return c;

	switch (c){
        case '0': case '1': case '2': case '3': case '4': case '5':
	case '6': case '7': case '8': case '9': case '.': {
		ExprTree *e = p_new (ExprTree);
		Value *v = v_new ();
		
		is_float = c == '.';
		p = parser_expr-1;
		tmp = parser_expr;
		
                while (isdigit (*tmp) || (!is_float && *tmp=='.' && ++is_float))
                        tmp++;
		
		if (*tmp == 'e' || *tmp == 'E') {
			is_float = 1;
			tmp++;
			if (*tmp == '-' || *tmp == '+')
				tmp++;
			while (isdigit (*tmp))
				tmp++;
		}

		/* Ok, we have skipped over a number, now load its value */
		if (is_float){
			v->type = VALUE_FLOAT;
			float_get_from_range (p, tmp, &v->v.v_float);
		} else {
			v->type = VALUE_INTEGER;
			int_get_from_range (p, tmp, &v->v.v_int);
		}

		/* Return the value to the parser */
		e->oper = OP_CONSTANT;
		e->u.constant = v;
		yylval.tree = e;

		parser_expr = tmp;
		return NUMBER;
	}
	case '"': {
		char *string;

                p = parser_expr;
                while(*parser_expr && *parser_expr != '"') {
                        if (*parser_expr == '\\' && parser_expr [1])
                                parser_expr++;
                        parser_expr++;
                }
                if(!*parser_expr){
                        parser_error = PARSE_ERR_NO_QUOTE;
                        return ERROR;
                }
		
		string = (char *) alloca (1 + parser_expr - p);
		while (p != parser_expr){
			if (*p== '\\'){
				p++;
				*string++ = *p++;
			} else
				*string++ = *p++;
			
		}
		*string = 0;
		parser_expr++;

		return return_symbol (string);
	}
	}
	
	if (isalpha (c) || c == '$'){
		char *start = parser_expr - 1;
		char *str;
		int  len;
		
		while (isalnum (*parser_expr) || *parser_expr == '$')
			parser_expr++;

		len = parser_expr - start;
		str = alloca (len + 1);
		strncpy (str, start, len);
		str [len] = 0;
		return try_symbol (str);
	}
	if (c == '\n')
		return 0;
	
	if (c == EOF)
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
    printf ("Error: %s\n", s);
    return 0;
}

static void
alloc_register (void *a_info)
{
	alloc_list = g_list_prepend (alloc_list, a_info);
}

static void
register_symbol (Symbol *sym)
{
	AllocRec *a_info = g_new (AllocRec, 1);

	a_info->type = ALLOC_SYMBOL;
	a_info->data = sym;
	alloc_register (a_info);
}

void *
alloc_buffer (int size)
{
	AllocRec *a_info = g_new (AllocRec, 1);
	char *res = g_malloc (size);

	a_info->type = ALLOC_BUFFER;
	a_info->data = res;
	alloc_register (a_info);

	return res;
}

static void *
v_new (void)
{
	AllocRec *a_info = g_new (AllocRec, 1);
	char *res = g_malloc (sizeof (Value));

	a_info->type = ALLOC_VALUE;
	a_info->data = res;
	alloc_register (a_info);

	return res;
}

static void
alloc_clean (void)
{
	GList *l = alloc_list;
	
	for (; l; l = l->next){
		AllocRec *rec = l->data;

		switch (rec->type){
		case ALLOC_BUFFER:
			g_free (rec->data);
			break;
			
		case ALLOC_SYMBOL:
			symbol_unref ((Symbol *)rec->data);
			break;

		case ALLOC_VALUE:
			value_release ((Value *)rec->data);
			break;
			
		case ALLOC_LIST:
			g_list_free ((GList *) rec->data);
			break;
		}
		g_free (rec);
	}

	g_list_free (l);
	alloc_list = NULL;
}

static void
alloc_list_free (void)
{
	GList *l = alloc_list;
	
	for (; l; l = l->next)
		g_free (l->data);

	g_list_free (l);
	alloc_list = NULL;
}

static void
alloc_glist (GList *list)
{
	AllocRec *a_info = g_new (AllocRec, 1);

	a_info->type = ALLOC_LIST;
	a_info->data = list;
	alloc_register (a_info);
}

static void
forget (int type, void *data)
{
	GList *l;

	for (l = alloc_list; l; l = l->next){
		AllocRec *a_info = (AllocRec *) l->data;

		if (a_info->type == type && a_info->data == data){
			alloc_list = g_list_remove_link (alloc_list, l);
			return;
		}
	}
}

static void
forget_glist (GList *list)
{
	forget (ALLOC_LIST, list);
}

static void
forget_tree (ExprTree *tree)
{
	forget (ALLOC_BUFFER, tree);
	eval_expr_release (tree);
}

/*
 * FIXME: For now this is only supporting double/int, not
 * mpz/mpf.
 * FIXME: This is not using the format_* routines as
 * Chris has been working on those and has not commited
 * his new version
 */
char *
value_string (Value *value)
{
	char buffer [1024];
		
	switch (value->type){
	case VALUE_STRING:
		return g_strdup (value->v.str->str);

	case VALUE_INTEGER:
		snprintf (buffer, sizeof (buffer)-1, "%d", value->v.v_int);
		break;

	case VALUE_FLOAT:
		snprintf (buffer, sizeof (buffer)-1, "%g", value->v.v_float);
		break;

	case VALUE_ARRAY:
		snprintf (buffer, sizeof (buffer)-1, "ARRAY");
		break;
		
	case VALUE_CELLRANGE:
		g_warning ("Cellrange on a value!");
		return g_strdup ("Internal problem");
	}
	return g_strdup (buffer);
}

void
value_dump (Value *value)
{
	switch (value->type){
	case VALUE_STRING:
		printf ("STRING: %s\n", value->v.str->str);
		break;

	case VALUE_INTEGER:
		printf ("NUM: %d\n", value->v.v_int);
		break;

	case VALUE_FLOAT:
		printf ("Float: %f\n", value->v.v_float);
		break;

	case VALUE_ARRAY: {
		GList *l;

		printf ("Array: { ");
		for (l = value->v.array; l; l = l->next){
			value_dump (l->data);
		}
		printf ("}\n");
	}
	default:
		printf ("Unhandled item type\n");
	}
}

void
dump_tree (ExprTree *tree)
{
	Symbol *s;
	CellRef *cr;
	
	switch (tree->oper){
	case OP_VAR:
		cr = &tree->u.constant->v.cell;
		printf ("Cell: %s%c%s%d\n",
			cr->col_relative ? "" : "$",
			cr->col + 'A',
			cr->row_relative ? "" : "$",
			cr->row + '1');
		return;
		
	case OP_CONSTANT:
		value_dump (tree->u.constant);
		return;

	case OP_FUNCALL:
		s = symbol_lookup (tree->u.function.symbol->str);
		printf ("Function call: %s\n", s->str);
		break;

	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_LT:
	case OP_LTE:
	case OP_GT:
	case OP_GTE:
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT:
		dump_tree (tree->u.binary.value_a);
		dump_tree (tree->u.binary.value_b);
		switch (tree->oper){
		case OP_ADD: printf ("ADD\n"); break;
		case OP_SUB: printf ("SUB\n"); break;
		case OP_MULT: printf ("MULT\n"); break;
		case OP_DIV: printf ("DIV\n"); break;
		case OP_CONCAT: printf ("CONCAT\n"); break;
		case OP_EQUAL: printf ("==\n"); break;
		case OP_NOT_EQUAL: printf ("!=\n"); break;
		case OP_LT: printf ("<\n"); break;
		case OP_GT: printf (">\n"); break;
		case OP_GTE: printf (">=\n"); break;
		case OP_LTE: printf ("<=\n"); break;
		case OP_EXP: printf ("EXP\n"); break;
		default:
			printf ("Error\n");
		}
		break;
		
	case OP_NEG:
		dump_tree (tree->u.value);
		printf ("NEGATIVE\n");
		break;
		
	}
}
