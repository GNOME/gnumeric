%{
#include <glib.h>
#include <gmp.h>
#include "symbol.h"
#include "expr.h"
#include <ctype.h>
static GList *alloc_list;

static void *alloc_buffer (int size);
static void dump_node (EvalNode *node);
static void register_symbol (Symbol *sym);
static void alloc_clean (void);
static void dump_constant (EvalNode *node);
static int yylex (void);
static int yyerror (char *s);
 
	
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
%token <node>  NUMBER STRING FUNCALL
%left '-' '+' '&'
%left '*' '/'
%left '!'
%right '^'

%%
line:	  exp           { parser_result = $1; dump_node (parser_result); }
	| error 	{ alloc_clean (); }
	;

exp:	  NUMBER 	{ $$ = $1 }
	
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

int yylex (void)
{
	int c;

	c = getchar ();
	if (isdigit (c)){
		EvalNode *e = p_new (EvalNode);
		Value    *v = p_new (Value);

		v->type = VALUE_NUMBER;
		e->oper = OP_CONSTANT;
		e->u.constant = v;

		yylval.node = e;
		return NUMBER;
	}
	if (isalpha (c)){
		EvalNode *e = p_new (EvalNode);
		Value    *v = p_new (Value);
		char buf [2] = { 0 , 0 };

		buf [0] = c;
		v->v.str = symbol_install (buf, SYMBOL_STRING, NULL);
		register_symbol (v->v.str);
		v->type = VALUE_STRING;
		e->oper = OP_CONSTANT;
		e->u.constant = v;
		
		yylval.node = e;
		return STRING;
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

typedef enum {
	ALLOC_SYMBOL,
	ALLOC_BUFFER,
} AllocType;

typedef struct {
	AllocType type;
	void      *data;
} AllocRec;

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

	case VALUE_NUMBER:
		printf ("NUM: %d\n", 0);
		break;

	default:
		printf ("Unhandled item type\n");
	}
}

void
dump_node (EvalNode *node)
{
	Symbol *s;
	
	switch (node->oper){
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
		
	case OP_CAST_TO_STRING:
		dump_node (node->u.value);
		printf ("CAST TO STRING\n");
		break;

	}
}
