%{
/*
 * Gnumeric Parser
 *
 * (C) 1998 the Free Software Foundation
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <glib.h>
#include <ctype.h>
#include <string.h>
#include "numbers.h"
#include "symbol.h"	
#include "str.h"
#include "expr.h"
#include "utils.h"
#include "number-match.h"
	
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

#define p_new(type) ((type *) alloc_buffer ((unsigned) sizeof (type)))

static ExprTree *
build_binop (ExprTree *l, Operation op, ExprTree *r)
{
  ExprTree *res = p_new (ExprTree);
  res->ref_count = 1;
  res->u.binary.value_a = l;
  res->oper = op;
  res->u.binary.value_b = r;
  return res;
}

		
%}

%union {
	ExprTree *tree;
	CellRef  *cell;
	GList    *list;
}
%type  <tree>  exp
%type  <list>  arg_list
%token <tree>  NUMBER STRING FUNCALL CONSTANT CELLREF GTE LTE NE

%left '<' '>' '=' GTE LTE NE
%left '-' '+' '&'
%left '*' '/'
%left NEG PLUS
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
	| STRING        { $$ = $1 }
        | CELLREF       { $$ = $1 }
	| CONSTANT      { $$ = $1 }
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

        | '-' exp %prec NEG {
		$$ = p_new (ExprTree);
		$$->ref_count = 1;
		$$->oper = OPER_NEG;
		$$->u.value = $2;
	}

        | '+' exp %prec PLUS {
		$$ = $2;
	}

        | CELLREF ':' CELLREF {
		CellRef a, b;

		a = $1->u.constant->v.cell;
		b = $3->u.constant->v.cell;

		$$ = p_new (ExprTree);
		$$->ref_count = 1;
		$$->oper = OPER_CONSTANT;
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
		col = (col+1) * ('Z'-'A'+1) + toupper (*p++) - 'A';

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
	e->ref_count = 1;
	v = v_new ();

	e->oper = OPER_VAR;

	ref = &v->v.cell;

	/* Setup the cell reference information */
	if (row_relative)
		ref->row = row - parser_row;
	else
		ref->row = row;

	if (col_relative)
		ref->col = col - parser_col;
	else
		ref->col = col;
	
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

	e->ref_count = 1;
	sym = symbol_lookup (global_symbol_table, string);
	type = STRING;
	
	if (!sym)
	{
		Value *v = v_new ();
		double fv;
		char *format;
		
		if (format_match (string, &fv, &format)){
			v->type = VALUE_FLOAT;
			v->v.v_float = fv;
			if (!parser_desired_format)
				parser_desired_format = format;
		} else {
			v->v.str = string_get (string);
			v->type = VALUE_STRING;
		}
		
		e->oper = OPER_CONSTANT;
		e->u.constant = v;
	}
	else
	{
		symbol_ref (sym);
		if (sym->type == SYMBOL_FUNCTION)
		{
			e->oper = OPER_FUNCALL;
			type = FUNCALL;
			e->u.function.symbol = sym;
			e->u.function.arg_list = NULL;
		}
		else
		{
			Value *v, *dv;

			/* Make a copy of the value */
			dv = (Value *) sym->data;
			v = v_new ();
			value_copy_to (v, dv);
			
			e->oper = OPER_CONSTANT;
			e->u.constant = v;
			type = CONSTANT;
		}
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

		e->ref_count = 1;
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
		e->oper = OPER_CONSTANT;
		e->u.constant = v;
		yylval.tree = e;

		parser_expr = tmp;
		return NUMBER;
	}
	case '"': {
		char *string, *s;
		int v;

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

		v = return_symbol (string);
		return v;
	}
	}
	
	if (isalpha (c) || c == '_' || c == '$'){
		char *start = parser_expr - 1;
		char *str;
		int  len;
		
		while (isalnum (*parser_expr) || *parser_expr == '_' || *parser_expr == '$')
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
forget (AllocType type, void *data)
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
	expr_tree_unref (tree); 
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
	case OPER_VAR:
		cr = &tree->u.constant->v.cell;
		printf ("Cell: %s%c%s%d\n",
			cr->col_relative ? "" : "$",
			cr->col + 'A',
			cr->row_relative ? "" : "$",
			cr->row + '1');
		return;
		
	case OPER_CONSTANT:
		value_dump (tree->u.constant);
		return;

	case OPER_FUNCALL:
		s = symbol_lookup (global_symbol_table, tree->u.function.symbol->str);
		printf ("Function call: %s\n", s->str);
		break;

	case OPER_EQUAL:
	case OPER_NOT_EQUAL:
	case OPER_LT:
	case OPER_LTE:
	case OPER_GT:
	case OPER_GTE:
	case OPER_ADD:
	case OPER_SUB:
	case OPER_MULT:
	case OPER_DIV:
	case OPER_EXP:
	case OPER_CONCAT:
		dump_tree (tree->u.binary.value_a);
		dump_tree (tree->u.binary.value_b);
		switch (tree->oper){
		case OPER_ADD: printf ("ADD\n"); break;
		case OPER_SUB: printf ("SUB\n"); break;
		case OPER_MULT: printf ("MULT\n"); break;
		case OPER_DIV: printf ("DIV\n"); break;
		case OPER_CONCAT: printf ("CONCAT\n"); break;
		case OPER_EQUAL: printf ("==\n"); break;
		case OPER_NOT_EQUAL: printf ("!=\n"); break;
		case OPER_LT: printf ("<\n"); break;
		case OPER_GT: printf (">\n"); break;
		case OPER_GTE: printf (">=\n"); break;
		case OPER_LTE: printf ("<=\n"); break;
		case OPER_EXP: printf ("EXP\n"); break;
		default:
			printf ("Error\n");
		}
		break;
		
	case OPER_NEG:
		dump_tree (tree->u.value);
		printf ("NEGATIVE\n");
		break;
		
	}
}
