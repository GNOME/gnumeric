#include <config.h>
#include <gnome.h>
#include "gnumeric.h"

char *parser_expr;
ParseErr parser_error;
EvalNode *parser_result;

EvalNode *
eval_parse_string (char *expr, char **error_msg)
{
	parser_expr = expr;
	parser_error = PARSE_OK;
	
	yyparse ();
	switch (parser_error){
	case PARSE_OK:
		*error_msg = NULL;
		return parser_result;

	case PARSE_ERR_SYNTAX:
		*error_msg = _("Syntax error");
		break;
			
	case PARSE_ERR_NO_QUOTE:
		*error_msg = _("Missing quote");
		break;
	}
	return NULL;
}

void
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

/*
 * Casts a value to float if it is integer, and returns
 * a new Value * if required
 */
Value *
eval_cast_to_float (Value *v)
{
	Value *newv;
	
	g_return_val_if_fail (VALUE_IS_NUMBER (v), NULL);

	if (v->type == VALUE_FLOAT)
		return v;
	
	newv = g_new (Value, 1);
	newv->type = VALUE_FLOAT;
	mpf_set_z (newv->v.v_float, v->v.v_int);
	eval_release_value (v);
	
	return newv;
}

static Value *
eval_cell_value (Sheet *sheet, Value *value)
{
	Value *res;
	
	res = g_new (Value, 1);
	res->type = value->type;
	
	switch (res->type){
	case VALUE_STRING:
		res->v.str = value->v.str;
		symbol_ref (res->v.str);
		break;
		
	case VALUE_INTEGER:
		mpz_init (res->v.v_int);
		mpz_set  (res->v.v_int, value->v.v_int);
		break;
		
	case VALUE_FLOAT:
		mpf_init (res->v.v_float);
		mpf_set (res->v.v_float, value->v.v_float);
		break;
		
	case VALUE_CELLRANGE:
		res->v.cell_range = value->v.cell_range;
		break;
	}
	return res;
}

Value *
eval_node_value (void *asheet, EvalNode *node, char **error_string)
{
	Value *a, *b, *res;
	Sheet *sheet = asheet;
	
	switch (node->oper){
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
		a = eval_node_value (sheet, node->u.binary.value_a,
				     error_string);
		b = eval_node_value (sheet, node->u.binary.value_b,
				     error_string);

		if (!(a && b)){
			if (a)
				eval_release_value (a);
			if (b)
				eval_release_value (b);
			return NULL;
		}
		
		if (!VALUE_IS_NUMBER (a) || !VALUE_IS_NUMBER (b)){
			eval_release_value (a);
			eval_release_value (b);
			*error_string = _("Type mismatch");
			return NULL;
		}
		
		res = g_new (Value, 1);
		if (a->type == VALUE_INTEGER && b->type == VALUE_INTEGER){
			res->type = VALUE_INTEGER;
			mpz_init (res->v.v_int);
			
			switch (node->oper){
			case OP_ADD:
				mpz_add (res->v.v_int, a->v.v_int, b->v.v_int);
				break;
				
			case OP_SUB:
				mpz_sub (res->v.v_int, a->v.v_int, b->v.v_int);
				break;
				
			case OP_MULT:
				mpz_mul (res->v.v_int, a->v.v_int, b->v.v_int);
				break;
				
			case OP_DIV:
				if (mpz_cmp_si (b->v.v_int, 0)){
					eval_release_value (a);
					eval_release_value (b);
					eval_release_value (res);
					*error_string = _("Division by zero");
					return NULL;
				}
					
				mpz_tdivq(res->v.v_int, a->v.v_int,b->v.v_int);
				break;
				
			case OP_EXP:
				mpz_set_si (res->v.v_int, 0);
				g_warning ("INT EXP not implemented yet\n");
				break;
			default:
			}
		} else {
			res->type = VALUE_FLOAT;
			mpf_init (res->v.v_float);
			a = eval_cast_to_float (a);
			b = eval_cast_to_float (b);
			
			switch (node->oper){
			case OP_ADD:
				mpf_add (res->v.v_float,
					 a->v.v_float, b->v.v_float);
				break;
				
			case OP_SUB:
				mpf_sub (res->v.v_float,
					 a->v.v_float, b->v.v_float);
				break;
				
			case OP_MULT:
				mpf_mul (res->v.v_float,
					 a->v.v_float, b->v.v_float);
				break;
				
			case OP_DIV:
				if (mpf_cmp_si (b->v.v_int, 0)){
					eval_release_value (a);
					eval_release_value (b);
					eval_release_value (res);
					*error_string = _("Division by zero");
					return NULL;
				}
					
				mpf_div (res->v.v_float,
					 a->v.v_float, b->v.v_float);
				break;
				
			case OP_EXP:
				mpz_set_si (res->v.v_int, 0);
				g_warning ("FLOAT EXP not implemented yet\n");
				break;
			default:
			}
		}
		eval_release_value (a);
		eval_release_value (b);
		return res;
		
	case OP_CONCAT:
		g_warning ("Concat not implemented yet\n");
		*error_string = _("OOPS");
		return NULL;

	case OP_FUNCALL:
		g_warning ("Function call not implemented yet\n");
		*error_string = _("OOPS");
		return NULL;

	case OP_CONSTANT:
		return eval_cell_value (sheet, node->u.constant);

	case OP_VAR:{
		Cell *cell;
		int col, row;
		
		if (sheet == NULL){
			/* Only the test program requests this */
			res = g_new (Value, 1);

			res->type = VALUE_FLOAT;
			res->v.v_float = 3.14;
			
			return res;
		}

		col = node->u.constant->v.cell.col;
		row = node->u.constant->v.cell.row;

		cell = sheet_cell_get (sheet, col, row);
		if (!cell)
			cell = sheet_cell_new (sheet, col, row);
		
		if (!cell->value){
			res = g_new (Value, 1);
			
			res->type = VALUE_INTEGER;
			res->v.v_int = 0;
		} else {
			return eval_cell_value (sheet, cell->value);
		}
		return res;
	}
	case OP_NEG:
		a = eval_node_value (sheet, node->u.value,
				     error_string);
		if (!a)
			return NULL;
		if (!VALUE_IS_NUMBER (a)){
			*error_string = _("Type mismatch");
			eval_release_value (a);
			return NULL;
		}
		res = g_new (Value, 1);
		if (a->type == VALUE_INTEGER){
			mpz_init_set (res->v.v_int, a->v.v_int);
			mpz_neg (res->v.v_int, a->v.v_int);
		} else {
			mpf_init_set (res->v.v_int, a->v.v_int);
			mpf_neg (res->v.v_float, a->v.v_float);
		}
		eval_release_value (a);
		return res;
	}
	
	*error_string = _("Unknown evaluation error");
	return NULL;
}
