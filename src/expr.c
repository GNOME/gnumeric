#include <glib.h>
#include <gmp.h>
#include "symbol.h"
#include "expr.h"

char *parser_expr;
char **parser_error_message;
EvalNode *parser_result;

EvalNode *
eval_parse_string (char *expr, int col, int row, char **error_msg)
{
	parser_expr = expr;
	parser_error_message = error_msg;

	yyparse ();
	
	return NULL;
}

void
eval_release_node (EvalNode *node)
{
	switch (node->oper){
	case OP_CONSTANT:
	default:
	}
}
