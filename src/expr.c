#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "expr.h"

char *parser_expr;
ParseErr parser_error;
ExprTree *parser_result;
int parser_col, parser_row;

ExprTree *
expr_parse_string (char *expr, int col, int row, char **error_msg)
{
	parser_expr = expr;
	parser_error = PARSE_OK;
	parser_col = col;
	parser_row = row;
		
	yyparse ();
	switch (parser_error){
	case PARSE_OK:
		*error_msg = NULL;
		parser_result->ref_count = 1;
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

/*
 * eval_expr_release:
 * @ExprTree:  The tree to be released
 *
 * This releases all of the resources used by a tree.  
 * It is only used internally by eval_expr_unref
 */
void
eval_expr_release (ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	
	switch (tree->oper){
	case OP_VAR:
		break;
		
	case OP_CONSTANT:
		value_release (tree->u.constant);
		break;
		
	case OP_FUNCALL:
		symbol_unref (tree->u.function.symbol);
		break;

	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT:
		eval_expr_release (tree->u.binary.value_a);
		eval_expr_release (tree->u.binary.value_b);
		break;

	case OP_NEG:
		eval_expr_release (tree->u.value);
		break;
		
	default:
		g_warning ("Unknown ExprTree type passed to eval_expr_release\n");
	}
	g_free (tree);
}

void
expr_tree_ref (ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (tree->ref_count > 0);

	tree->ref_count++;
}

void
expr_tree_unref (ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (tree->ref_count > 0);

	tree->ref_count--;
	if (tree->ref_count == 0)
		eval_expr_release (tree);
}

void
value_release (Value *value)
{
	g_return_if_fail (value != NULL);
	
	switch (value->type){
	case VALUE_STRING:
		string_unref (value->v.str);
		break;

	case VALUE_INTEGER:
		mpz_clear (value->v.v_int);
		break;
		
	case VALUE_FLOAT:
		mpf_clear (value->v.v_float);
		break;

	case VALUE_ARRAY: {
		GList *l;

		for (l = value->v.array; l; l = l->next)
			value_release (l->data);
		g_list_free (l);
	}
	
	case VALUE_CELLRANGE:
		break;
		
	default:
		g_warning ("Unknown value type passed to value_release\n");
	}
	g_free (value);
}

/*
 * Copies a Value.
 */
void
value_copy_to (Value *dest, Value *source)
{
	g_return_if_fail (dest != NULL);
	g_return_if_fail (source != NULL);

	dest->type = source->type;
	
	switch (source->type){
	case VALUE_STRING:
		dest->v.str = source->v.str;
		string_ref (dest->v.str);
		break;

	case VALUE_INTEGER:
		dest->v.v_int = source->v.v_int;
		break;

	case VALUE_FLOAT:
		dest->v.v_float = source->v.v_float;
		break;

	default:
		g_warning ("value_copy_to: VALUE type not yet supported\n");
	}
}

/*
 * Casts a value to float if it is integer, and returns
 * a new Value * if required
 */
Value *
value_cast_to_float (Value *v)
{
	Value *newv;
	
	g_return_val_if_fail (VALUE_IS_NUMBER (v), NULL);

	if (v->type == VALUE_FLOAT)
		return v;
	
	newv = g_new (Value, 1);
	newv->type = VALUE_FLOAT;
	mpf_set_z (newv->v.v_float, v->v.v_int);
	value_release (v);
	
	return newv;
}

int
value_get_bool (Value *v, int *err)
{
	*err = 0;

	if (v->type == VALUE_STRING)
		return atoi (v->v.str->str);

	if (v->type == VALUE_CELLRANGE){
		*err = 1;
		return 0;
	}

	if (v->type == VALUE_INTEGER)
		return v->v.v_int != 0;

	if (v->type == VALUE_FLOAT)
		return v->v.v_float != 0.0;

	if (v->type == VALUE_ARRAY)
		return 0;
	
	g_warning ("Unhandled value in value_get_boolean");

	return 0;
}

float_t
value_get_as_double (Value *v)
{
	if (v->type == VALUE_STRING){
		return atof (v->v.str->str);
	}

	if (v->type == VALUE_CELLRANGE){
		g_warning ("Getting range as a double: what to do?");
		return 0.0;
	}

	if (v->type == VALUE_INTEGER)
		return (float_t) v->v.v_int;

	if (v->type == VALUE_ARRAY)
		return 0.0;
	
	return (float_t) v->v.v_float;
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
		string_ref (res->v.str);
		break;
		
	case VALUE_INTEGER:
		mpz_init (res->v.v_int);
		mpz_set  (res->v.v_int, value->v.v_int);
		break;
		
	case VALUE_FLOAT:
		mpf_init (res->v.v_float);
		mpf_set (res->v.v_float, value->v.v_float);
		break;

	case VALUE_ARRAY:
		g_warning ("VALUE_ARRAY not handled in eval_cell_value\n");
		res->type = VALUE_INTEGER;
		res->v.v_int = 0;
		break;
			
	case VALUE_CELLRANGE:
		res->v.cell_range = value->v.cell_range;
		break;
	}
	return res;
}

static Value *
eval_funcall (Sheet *sheet, ExprTree *tree, int eval_col, int eval_row, char **error_string)
{
	FunctionDefinition *fd;
	GList *l;
	int argc, arg, i;
	Value *v;
	
	fd = (FunctionDefinition *) tree->u.function.symbol->data;
	
	l = tree->u.function.arg_list;
	argc = g_list_length (l);

	if (fd->expr_fn)
	{
		/* Functions that deal with ExprNodes */
		v = fd->expr_fn (sheet, l, eval_col, eval_row, error_string);
	}
	else
	{
		/* Functions that take pre-computed Values */
		Value **values;
		int fn_argc;
		char *arg_type = fd->args;
		
		fn_argc = strlen (fd->args);
		
		if (fn_argc != argc){
			*error_string = _("Invalid number of arguments");
			return NULL;
		}

		values = g_new (Value *, argc);
		
		for (arg = 0; l; l = l->next, arg++, arg_type++){
			ExprTree *t = (ExprTree *) l->data;
			
			v = eval_expr (sheet, t, eval_col, eval_row, error_string);
			if (v == NULL)
				goto free_list;
			
			values [arg] = v;
		}
		v = fd->fn (values, error_string);

	free_list:
		for (i = 0; i < arg; i++)
			value_release (values [i]);
		g_free (values);
		return v;
	}
	return v;
}

enum {
	IS_EQUAL,
	IS_LESS,
	IS_BIGGER,
};

static int
compare (Value *a, Value *b)
{
	g_warning ("Value comparission is not yet implemented\n");
	return IS_EQUAL;
}

Value *
eval_expr (void *asheet, ExprTree *tree, int eval_col, int eval_row, char **error_string)
{
	Value *a, *b, *res;
	Sheet *sheet = asheet;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (asheet != NULL, NULL);
	g_return_val_if_fail (error_string != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (asheet), NULL);
	
	switch (tree->oper){
	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_GT:
	case OP_GTE:
	case OP_LT:
	case OP_LTE: {
		int comp;
		
		a = eval_expr (sheet, tree->u.binary.value_a,
			       eval_col, eval_row, error_string);
		b = eval_expr (sheet, tree->u.binary.value_b,
			       eval_col, eval_row, error_string);
		if (!(a && b)){
			if (a)
				value_release (a);
			if (b)
				value_release (b);
			return NULL;
		}
		res = g_new (Value, 1);
		res->type = VALUE_INTEGER;

		comp = compare (a, b);

		switch (tree->oper){
		case OP_EQUAL:
			res->v.v_int = comp == IS_EQUAL;
			break;

		case OP_GT:
			res->v.v_int = comp == IS_BIGGER;
			break;

		case OP_LT:
			res->v.v_int = comp == IS_LESS;
			break;

		case OP_LTE:
			res->v.v_int = (comp == IS_EQUAL || comp == IS_LESS);
			break;

		case OP_GTE:
			res->v.v_int = (comp == IS_EQUAL || comp == IS_BIGGER);
			break;

		case OP_NOT_EQUAL:
			res->v.v_int = comp != IS_EQUAL;
			break;
			
		default:
			g_warning ("This should never be reached: comparission ops\n");
		}
		value_release (a);
		value_release (b);
		return res;
	}
	
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
		a = eval_expr (sheet, tree->u.binary.value_a,
			       eval_col, eval_row, error_string);
		b = eval_expr (sheet, tree->u.binary.value_b,
			       eval_col, eval_row, error_string);

		if (!(a && b)){
			if (a)
				value_release (a);
			if (b)
				value_release (b);
			return NULL;
		}
		
		if (!VALUE_IS_NUMBER (a) || !VALUE_IS_NUMBER (b)){
			value_release (a);
			value_release (b);
			*error_string = _("Type mismatch");
			return NULL;
		}
		
		res = g_new (Value, 1);
		if (a->type == VALUE_INTEGER && b->type == VALUE_INTEGER){
			res->type = VALUE_INTEGER;
			
			switch (tree->oper){
			case OP_ADD:
				res->v.v_int = a->v.v_int + b->v.v_int;
				break;
				
			case OP_SUB:
				res->v.v_int = a->v.v_int - b->v.v_int;
				break;
				
			case OP_MULT:
				res->v.v_int = a->v.v_int * b->v.v_int;
				break;

			case OP_DIV:
				if (b->v.v_int == 0){
					value_release (a);
					value_release (b);
					value_release (res);
					*error_string = _("Division by zero");
					return NULL;
				}
					
				res->v.v_int =  a->v.v_int / b->v.v_int;
				break;
				
			case OP_EXP: 
				res->v.v_int = pow (a->v.v_int, b->v.v_int);
				break;
			default:
			}
		} else {
			res->type = VALUE_FLOAT;
			res->v.v_float = 0.0;
			a = value_cast_to_float (a);
			b = value_cast_to_float (b);
			
			switch (tree->oper){
			case OP_ADD:
				res->v.v_float = a->v.v_float + b->v.v_float;
				break;
				
			case OP_SUB:
				res->v.v_float = a->v.v_float - b->v.v_float;
				break;
				
			case OP_MULT:
				res->v.v_float = a->v.v_float * b->v.v_float;
				break;
				
			case OP_DIV:
				if (b->v.v_float == 0.0){
					value_release (a);
					value_release (b);
					value_release (res);
					*error_string = _("Division by zero");
					return NULL;
				}

				res->v.v_float = a->v.v_float / b->v.v_float;
				break;
				
			case OP_EXP:
				res->v.v_float = pow (a->v.v_float, b->v.v_float);
				break;
			default:
			}
		}
		value_release (a);
		value_release (b);
		return res;
		
	case OP_CONCAT:
		g_warning ("Concat not implemented yet\n");
		*error_string = _("OOPS");
		return NULL;

	case OP_FUNCALL:
		return eval_funcall (sheet, tree, eval_col, eval_row, error_string);

	case OP_CONSTANT:
		return eval_cell_value (sheet, tree->u.constant);

	case OP_VAR:{
		CellRef *ref;
		Cell *cell;
		int col, row;
		
		if (sheet == NULL){
			/* Only the test program requests this */
			res = g_new (Value, 1);

			res->type = VALUE_FLOAT;
			res->v.v_float = 3.14;
			
			return res;
		}

		ref = &tree->u.constant->v.cell;
		cell_get_abs_col_row (&tree->u.constant->v.cell, eval_col, eval_row, &col, &row);
		
		cell = sheet_cell_get (sheet, col, row);
		
		if (!cell || !cell->value){
			res = g_new (Value, 1);
			
			res->type = VALUE_INTEGER;
			res->v.v_int = 0;
		} else {
			return eval_cell_value (sheet, cell->value);
		}
		return res;
	}
	case OP_NEG:
		a = eval_expr (sheet, tree->u.value,
			       eval_col, eval_row, error_string);
		if (!a)
			return NULL;
		if (!VALUE_IS_NUMBER (a)){
			*error_string = _("Type mismatch");
			value_release (a);
			return NULL;
		}
		res = g_new (Value, 1);
		if (a->type == VALUE_INTEGER){
			res->v.v_int = -a->v.v_int;
		} else {
			res->v.v_float = -a->v.v_float;
		}
		value_release (a);
		return res;
	}
	
	*error_string = _("Unknown evaluation error");
	return NULL;
}

void
cell_get_abs_col_row (CellRef *cell_ref, int eval_col, int eval_row, int *col, int *row)
{
	g_return_if_fail (cell_ref != NULL);

	if (cell_ref->col_relative)
		*col = eval_col + cell_ref->col;
	else
		*col = cell_ref->col;

	if (cell_ref->row_relative)
		*row = eval_row + cell_ref->row;
	else
		*row = cell_ref->row;
}

