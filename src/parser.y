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
#include "numbers.h"
#include "symbol.h"
#include "expr.h"
#include "utils.h"
#include <ctype.h>

/* Allocation with disposal-on-error */ 
static void *alloc_buffer   (int size);
static void register_symbol (Symbol *sym);
static void alloc_clean     (void);
static void *v_new (void);
	
#define ERROR -1
 
/* Types of items we know how to dispose on error */ 
typedef enum {
	ALLOC_SYMBOL,
	ALLOC_VALUE,
	ALLOC_BUFFER,
} AllocType;

/* How we keep track of them */ 
typedef struct {
	AllocType type;
	void      *data;
} AllocRec;

/* This keeps a list of  AllocRecs */
static GList *alloc_list;
 
/* Debugging */ 
static void dump_constant (EvalNode *node);
static void dump_node (EvalNode *node);

/* Bison/Yacc internals */ 
static int  yylex (void);
static int  yyerror (char *s);

#define p_new(type) \
	((type *) alloc_buffer ((unsigned) sizeof (type)))
		
%}

%union {
	EvalNode *node;
	CellRef  *cell;
	GList    *list;
}
%type  <node>  exp
%type  <list>  arg_list
%token <node>  NUMBER STRING FUNCALL CELLREF
%left '-' '+' '&'
%left '*' '/'
%left '!'
%right '^'

%%
line:	  exp           { parser_result = $1; dump_node (parser_result); }
	| error 	{ alloc_clean (); }
	;

exp:	  NUMBER 	{ $$ = $1 }
        | CELLREF       { $$ = $1 }
	| exp '+' exp	{
		$$ = p_new (EvalNode);
		$$->oper = OP_ADD;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| exp '-' exp {
		$$ = p_new (EvalNode);
		$$->oper = OP_SUB;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| exp '*' exp { 
		$$ = p_new (EvalNode);
		$$->oper = OP_MULT;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| exp '/' exp {
		$$ = p_new (EvalNode);
		$$->oper = OP_DIV;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}

	| '(' exp ')'  {
		$$ = p_new (EvalNode);
		$$ = $2;
	}

	| exp '&' exp {
		$$ = p_new (EvalNode);
		$$->oper = OP_CONCAT;
		$$->u.binary.value_a = $1;
		$$->u.binary.value_b = $3;
	}
	;

        | CELLREF ':' CELLREF {}

	| FUNCALL '(' arg_list ')' {
		$$ = p_new (EvalNode);
		$$->oper = OP_FUNCALL;
		$$->u.function.symbol = $1->u.function.symbol;
		$$->u.function.arg_list = $3;
	}
	;

arg_list: {}
	| exp ',' arg_list { } 
	;

%%

static int
return_cellref (char *p)
{
	int col_absolute = FALSE;
	int row_absolute = FALSE;
	int col = 0;
	int row = 0;
	EvalNode *e;
	Value    *v;

	/* Try to parse a column */
	if (*p == '$'){
		col_absolute = TRUE;
		p++;
	}
	if (!(toupper (*p) >= 'A' && toupper (*p) <= 'Z'))
		return 0;

	col = toupper (*p++) - 'A';
	
	if (toupper (*p) >= 'A' && toupper (*p) <= 'Z')
		col = col * ('Z'-'A') + toupper (*p++) - 'A';

	/* Try to parse a row */
	if (*p == '$'){
		row_absolute = TRUE;
		p++;
	}
	
	if (!(*p >= '1' && *p <= '9'))
		return 0;

	while (isdigit (*p))
		row = row * 10 + *p - '0';
	row++;
	
	e = p_new (EvalNode);
	v = v_new ();

	e->oper = OP_VAR;

	ref = &e->u.value.v.cell;

	ref->col = col;
	ref->row = row;
	ref->col_abs = col_absolute;
	ref->row_abs = row_absolute;
	
	yylval.node = e;
	return CELLREF;
}

static int
return_symbol (char *string)
{
	EvalNode *e = p_new (EvalNode);
	Value *v = v_new ();
	Symbol *sym;

	sym = symbol_lookup (string);
	type = STRING;
	if (!sym)
		v->v.str = symbol_install (string, SYMBOL_STRING, NULL);
	else {
		symbol_ref (sym);
		if (sym->type == SYMBOL_FUNCTION)
			type = FUNCALL;
		v->v.str = sym;
	}
	register_symbol (v->v.str);
	
	v->type = VALUE_STRING;
	e->oper = OP_CONSTANT;
	e->u.constant = v;
	
	yylval.node = e;
	return STRING;
}

static int
try_symbol (char *string)
{
	v = return_cellref (string);
	if (v)
		return v;

	return_symbol (string);
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
		EvalNode *e = p_new (EvalNode);
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
		yylval.node = e;

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

		return return_plain_symbol (string);
		
	}
	
	if (isalpha (c)){
		while (islapha*parser_expr)
	}
	if (c == '$'){
		EvalNode *e = p_new (EvalNode);

		e->oper = OP_FUNCALL;
		e->u.function.symbol = symbol_install
			("STRFUNC", SYMBOL_FUNCTION, 0);
		register_symbol (e->u.function.symbol);
		e->u.function.arg_list = NULL;
		yylval.node = e;
		return FUNCALL;
	}
	if (c == '\n')
		return 0;
	
	if (c == EOF)
		return 0;
	
	return c;
}

int
yyerror (char *s)
{
    printf ("Error: %s\n", s);
    return 0;
}

static void
register_symbol (Symbol *sym)
{
	AllocRec *a_info = g_new (AllocRec, 1);

	a_info->type = ALLOC_SYMBOL;
	a_info->data = sym;
	alloc_list = g_list_prepend (alloc_list, a_info);
}

void *
alloc_buffer (int size)
{
	AllocRec *a_info = g_new (AllocRec, 1);
	char *res = g_malloc (size);

	a_info->type = ALLOC_BUFFER;
	a_info->data = res;
	alloc_list = g_list_prepend (alloc_list, a_info);

	return res;
}

void *
v_new (void)
{
	AllocRec *a_info = g_new (AllocRec, 1);
	char *res = g_malloc (sizeof (Value));

	a_info->type = ALLOC_VALUE;
	a_info->data = res;
	alloc_list = g_list_prepend (alloc_list, a_info);

	return res;
	
}

static void
clean_value (Value *v)
{
	switch (v->type){
	case VALUE_FLOAT:
		mpf_clear (v->v.v_float);
		break;

	case VALUE_INTEGER:
	        mpz_clear (v->v.v_int);
		break;

	default:
		g_warning ("Unknown value passed to clean_value\n");
		break;
	}
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
			clean_value ((Value *)rec->data);
		}
	}

	g_list_free (l);
	alloc_list = NULL;
}

static void
dump_constant (EvalNode *node)
{
	Value *value = node->u.constant;

	switch (value->type){
	case VALUE_STRING:
		printf ("STRING: %s\n", value->v.str->str);
		break;

	case VALUE_INTEGER:
		printf ("NUM: %d\n", 0);
		break;

	case VALUE_FLOAT:
		printf ("Float: %f\n", 0.0);
		break;
		
	default:
		printf ("Unhandled item type\n");
	}
}

void
dump_node (EvalNode *node)
{
	Symbol *s;
	CellRef *cr;
	
	switch (node->oper){
	case OP_VAR:
		cr = &node->u.value.v.cell;
		printf ("Cell: %s%c%s%d\n",
			cr->col_abs ? "$" : "",
			cr->col + 'A',
			cr->row_abs ? "$" : "",
			cr->row + '1');
		return;
		
	case OP_CONSTANT:
		dump_constant (node);
		return;

	case OP_FUNCALL:
		s = symbol_lookup (node->u.function.symbol->str);
		printf ("Function call: %s\n", s->str);
		break;
		
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT:
		dump_node (node->u.binary.value_a);
		dump_node (node->u.binary.value_b);
		switch (node->oper){
		case OP_ADD: printf ("ADD\n"); break;
		case OP_SUB: printf ("SUB\n"); break;
		case OP_MULT: printf ("MULT\n"); break;
		case OP_DIV: printf ("DIV\n"); break;
		case OP_CONCAT: printf ("CONCAT\n"); break;
		case OP_EXP: printf ("EXP\n"); break;
		default:
			printf ("Error\n");
		}
		break;
		
	case OP_NEG:
		dump_node (node->u.value);
		printf ("NEGATIVE\n");
		break;
		
	}
}
