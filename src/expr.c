#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include "numbers.h"
#include "symbol.h"
#include "expr.h"

char *parser_expr;
ParseErr parser_error;
EvalNode *parser_result;
int   parser_col, parser_row;

EvalNode *
eval_parse_string (char *expr, int col, int row, char **error_msg)
{
	parser_expr = expr;
	parser_error = PARSE_OK;
	parser_col = col;
	parser_row = row;
	
	yyparse ();
	switch (parser_error){
	case PARSE_OK:
		*error_msg = NULL;
		break;

	case PARSE_ERR_NO_QUOTE:
		*error_msg = _("Missing quote");
		break;
	}
	return NULL;
}

static void
eval_release_value (Value *value)
{
	switch (value->type){
	case VALUE_STRING:
		symbol_unref (value->v.str);
		break;

	case VALUE_INTEGER:
		mpz_clear (value->v.v_int);
		break;
		
	case VALUE_FLOAT:
		mpf_clear (value->v.v_float);
		break;

	default:
		g_warning ("Unknown value type passed to eval_release_value\n");
	}
}

void
eval_release_node (EvalNode *node)
{
	g_return_if_fail (node != NULL);
	
	switch (node->oper){
	case OP_VAR:
		break;
		
	case OP_CONSTANT:
		eval_release_value (node->u.constant);
		break;
		
	case OP_FUNCALL:
		symbol_unref (node->u.function.symbol);
		break;

	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT:
		eval_release_node (node->u.binary.value_a);
		eval_release_node (node->u.binary.value_b);
		break;

	case OP_NEG:
		eval_release_node (node->u.value);
		break;
		
	default:
		g_warning ("Unknown ExprNode type passed to eval_release_node\n");
	}
	g_free (node);
}

Value *
eval_node (EvalNode *node)
{
	case (node->oper){
	}
}
