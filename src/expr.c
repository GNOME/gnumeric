#include <config.h>
#include <gnome.h>
#include <math.h>
#include <string.h>
#include "gnumeric.h"
#include "expr.h"
#include "eval.h"
#include "format.h"
#include "func.h"

/* Shared variables with parser.y */

/*       The expression being parsed */
char     *parser_expr;

/*        The suggested format to use for this expression */
char     *parser_desired_format;

/*        The error returned from the */
ParseErr  parser_error;

/*        The expression tree returned from the parser */
ExprTree *parser_result;

/*        Location where the parsing is taking place */
int       parser_col, parser_row;

ExprTree *
expr_parse_string (char *expr, int col, int row, char **desired_format, char **error_msg)
{
	parser_expr = expr;
	parser_error = PARSE_OK;
	parser_col = col;
	parser_row = row;
	parser_desired_format = NULL;
	
	yyparse ();
	switch (parser_error){
	case PARSE_OK:
		*error_msg = NULL;
		if (desired_format)
			*desired_format = parser_desired_format;
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

static void
do_expr_tree_ref (ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (tree->ref_count > 0);

	tree->ref_count++;
	switch (tree->oper){
	case OP_VAR:
	case OP_CONSTANT:
	case OP_FUNCALL:
		break;

	case OP_EQUAL:
	case OP_GT:
	case OP_LT:
	case OP_GTE:
	case OP_LTE:
	case OP_NOT_EQUAL:
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT:
		do_expr_tree_ref (tree->u.binary.value_a);
		do_expr_tree_ref (tree->u.binary.value_b);
		break;

	case OP_NEG:
		do_expr_tree_ref (tree->u.value);
		break;
	}
}

/*
 * expr_tree_ref:
 * Increments the ref_count for part of a tree
 */
void
expr_tree_ref (ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (tree->ref_count > 0);

	do_expr_tree_ref (tree);
}

static void
do_expr_tree_unref (ExprTree *tree)
{
	tree->ref_count--;
	switch (tree->oper){
	case OP_VAR:
		break;
		
	case OP_CONSTANT:
		if (tree->ref_count == 0)
			value_release (tree->u.constant);
		break;
		
	case OP_FUNCALL:
		if (tree->ref_count == 0)
			symbol_unref (tree->u.function.symbol);
		break;

	case OP_EQUAL:
	case OP_GT:
	case OP_LT:
	case OP_GTE:
	case OP_LTE:
	case OP_NOT_EQUAL:
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT:
		do_expr_tree_unref (tree->u.binary.value_a);
		do_expr_tree_unref (tree->u.binary.value_b);
		break;

	case OP_NEG:
		do_expr_tree_unref (tree->u.value);
		break;
	}
	
	if (tree->ref_count == 0)
		g_free (tree);
}

void
expr_tree_unref (ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (tree->ref_count > 0);

	do_expr_tree_unref (tree);
}

/*
 * simplistic value rendering
 */
char *
value_string (Value *value)
{
	char buffer [40];
		
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
		return g_strdup ("Internal problem");
	}
	return g_strdup (buffer);
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

	case VALUE_ARRAY: {
		GList *l, *new = NULL;

		for (l = source->v.array; l; l = l->next){
			Value *copy;

			copy = value_duplicate (l->data);
			
			new = g_list_append (new, copy);
		}
		dest->v.array = new;
		break;
	}
	case VALUE_CELLRANGE:
		dest->v.cell_range = source->v.cell_range;
		break;
	}
}

/*
 * Makes a copy of a Value
 */
Value *
value_duplicate (Value *value)
{
	Value *new_value;

	g_return_val_if_fail (value != NULL, NULL);
	new_value = g_new (Value, 1);
	value_copy_to (new_value, value);
	
	return new_value;
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
		res->v.v_int = value->v.v_int;
		break;
		
	case VALUE_FLOAT:
		res->v.v_float = value->v.v_float;
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
		v = fd->fn (fd, values, error_string);

	free_list:
		for (i = 0; i < arg; i++)
			value_release (values [i]);
		g_free (values);
		return v;
	}
	return v;
}

Value *
function_def_call_with_values (Sheet *sheet, FunctionDefinition *fd, int argc,
			       Value *values [], char **error_string)
{
	Value *retval;
	
	if (fd->expr_fn){
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		ExprTree *tree = NULL;
		GList *l = NULL;
		int i;

		if (argc){
			tree = g_new (ExprTree, argc);
		
			for (i = 0; i < argc; i++){
				tree [i].oper = OP_CONSTANT;
				tree [i].ref_count = 1;
				tree [i].u.constant = values [i];
				
				l = g_list_append (l, &(tree[i]));
			}
		}
		
		retval = fd->expr_fn (sheet, l, 0, 0, error_string);

		if (tree){
			g_free (tree);
			g_list_free (l);
		}

	} else 
		retval = fd->fn (fd, values, error_string);

	return retval;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
Value *
function_call_with_values (Sheet *sheet, char *name, int argc, Value *values[], char **error_string)
{
	FunctionDefinition *fd;
	Value *retval;
	Symbol *sym;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sym = symbol_lookup (name);
	if (sym == NULL){
		*error_string = "Function does not exist";
		return NULL;
	}
	if (sym->type != SYMBOL_FUNCTION){
		*error_string = "Calling non-function";
		return NULL;
	}
	
	fd = sym->data;

	symbol_ref (sym);
	retval = function_def_call_with_values (sheet, fd, argc, values, error_string);

	symbol_unref (sym);

	return retval;
}

typedef enum {
	IS_EQUAL,
	IS_LESS,
	IS_GREATER,
	TYPE_ERROR
} compare_t;

static compare_t
compare_int_int (int a, int b)
{
	if (a == b)
		return IS_EQUAL;
	else if (a < b)
		return IS_LESS;
	else
		return IS_GREATER;
}

static compare_t
compare_float_float (float_t a, float_t b)
{
	if (a == b)
		return IS_EQUAL;
	else if (a < b)
		return IS_LESS;
	else
		return IS_GREATER;
}

/*
 * Compares two (Value *) and returns one of compare_t
 */
static compare_t
compare (Value *a, Value *b)
{
	if (a->type == VALUE_INTEGER){
		int f;
		
		switch (b->type){
		case VALUE_INTEGER:
			return compare_int_int (a->v.v_int, b->v.v_int);

		case VALUE_FLOAT:
			return compare_float_float (a->v.v_int, b->v.v_float);

		case VALUE_STRING:
			f = value_get_as_double (b);
			return compare_float_float (a->v.v_int, f);

		default:
			return TYPE_ERROR;
		}
	}
	
	if (a->type == VALUE_FLOAT){
		float_t f;

		switch (b->type){
		case VALUE_INTEGER:
			return compare_float_float (a->v.v_float, b->v.v_int);

		case VALUE_FLOAT:
			return compare_float_float (a->v.v_float, b->v.v_float);

		case VALUE_STRING:
			f = value_get_as_double (b);
			return compare_float_float (a->v.v_float, f);

		default:
			return TYPE_ERROR;
		}
	}

	if (a->type == VALUE_STRING && b->type == VALUE_STRING){
		int t;

		t = strcasecmp (a->v.str->str, b->v.str->str);
		if (t == 0)
			return IS_EQUAL;
		else if (t > 0)
			return IS_GREATER;
		else
			return IS_LESS;
	}

	return TYPE_ERROR;
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

		if (comp == TYPE_ERROR){
			value_release (a);
			value_release (b);
			*error_string = _("Type error");
			return NULL;
		}
		
		switch (tree->oper){
		case OP_EQUAL:
			res->v.v_int = comp == IS_EQUAL;
			break;

		case OP_GT:
			res->v.v_int = comp == IS_GREATER;
			break;

		case OP_LT:
			res->v.v_int = comp == IS_LESS;
			break;

		case OP_LTE:
			res->v.v_int = (comp == IS_EQUAL || comp == IS_LESS);
			break;

		case OP_GTE:
			res->v.v_int = (comp == IS_EQUAL || comp == IS_GREATER);
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
				res->type = VALUE_FLOAT;
				res->v.v_float =  a->v.v_int / (float_t)b->v.v_int;
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
		
	case OP_CONCAT: {
		char *sa, *sb, *tmp;

		a = eval_expr (sheet, tree->u.binary.value_a,
			       eval_col, eval_row, error_string);
		if (!a)
			return NULL;
		b = eval_expr (sheet, tree->u.binary.value_b,
			       eval_col, eval_row, error_string);
		if (!b){
			value_release (a);
			return NULL;
		}

		res = g_new (Value, 1);
		res->type = VALUE_STRING;
		sa = value_string (a);
		sb = value_string (b);
		
		tmp = g_copy_strings (sa, sb, NULL);
		res->v.str = string_get (tmp);
		g_free (sa);
		g_free (sb);
		g_free (tmp);

		value_release (a);
		value_release (b);
		return res;
	}

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

		if (cell){
			if (cell->generation != sheet->workbook->generation){
				cell->generation = sheet->workbook->generation;

				if (cell->parsed_node)
					cell_eval (cell);
			}

			if (cell->value)
				return eval_cell_value (sheet, cell->value);
		}
		res = g_new (Value, 1);
			
		res->type = VALUE_INTEGER;
		res->v.v_int = 0;
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
		res->type = a->type;
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

static int
evaluate_level (Operation x)
{
	if (x == OP_EXP)
		return 3;
	if ((x==OP_MULT) || (x==OP_DIV))
		return 2;
	if ((x==OP_ADD)  || (x==OP_SUB)   || (x==OP_CONCAT))
		return 1;
	return 0;
}

static int
bigger_prec (Operation parent, Operation this)
{
	int parent_level, this_level;

	parent_level = evaluate_level (parent);
	this_level   = evaluate_level (this);

	return parent_level > this_level;
}

/*
 * Converts a parsed tree into its string representation
 * assuming that we are evaluating at col, row (This is
 * only used during copying to "render" a new text
 * representation for a copied cell.
 *
 * This routine is pretty simple: it walks the ExprTree and
 * create a string representation.
 */
static char *
do_expr_decode_tree (ExprTree *tree, int col, int row, Operation parent_op)
{
	static const char *binary_operation_names [] = {
		"=", ">", "<", ">=", "<=", "<>",
		"+", "-", "*", "/",  "^",  "&"
	};

	switch (tree->oper){

		/* The binary operations */
	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_GT:
	case OP_GTE:
	case OP_LT:
	case OP_LTE:
	case OP_ADD:
	case OP_SUB:
	case OP_MULT:
	case OP_DIV:
	case OP_EXP:
	case OP_CONCAT: {	
		char *a, *b, *res;
		char const *op;
		
		a = do_expr_decode_tree (tree->u.binary.value_a, col, row, tree->oper);
		b = do_expr_decode_tree (tree->u.binary.value_b, col, row, tree->oper);
		op = binary_operation_names [tree->oper];

		if (bigger_prec (parent_op, tree->oper))
			res = g_copy_strings ("(", a, op, b, ")", NULL);
		else
			res = g_copy_strings (a, op, b, NULL);
		
		g_free (a);
		g_free (b);
		return res;
	}
	
	case OP_NEG: {
		char *res, *a;

		a = do_expr_decode_tree (tree->u.value, col, row, tree->oper);
		res = g_copy_strings ("-", a);
		g_free (a);
		return res;
	}
	
	case OP_FUNCALL: {
		FunctionDefinition *fd;
		GList *arg_list, *l;
		char *res, *sum;
		char **args;
		int  argc;

		fd = tree->u.function.symbol->data;
		arg_list = tree->u.function.arg_list;
		argc = g_list_length (arg_list);

		if (argc){
			int i, len = 0;
			args = g_malloc (sizeof (char *) * argc);

			i = 0;
			for (l = arg_list; l; l = l->next, i++){
				ExprTree *t = l->data;
				
				args [i] = do_expr_decode_tree (t, col, row, OP_CONSTANT);
				len += strlen (args [i]) + 1;
			}
			len++;
			sum = g_malloc (len + 2);
			
			i = 0;
			sum [0] = 0;
			for (l = arg_list; l; l = l->next, i++){
				strcat (sum, args [i]);
				if (l->next)
					strcat (sum, ",");
			}
			
			res = g_copy_strings (
				fd->name, "(", sum, ")", NULL);

			for (i = 0; i < argc; i++)
				g_free (args [i]);
			g_free (args);

			return res;
		} else
			return g_copy_strings (fd->name, "()", NULL);
	}
	
	case OP_CONSTANT: {
		Value *v = tree->u.constant;

		if (v->type == VALUE_CELLRANGE){
			char buffer_a [20], buffer_b [20], *a;

			a = cellref_name (&v->v.cell_range.cell_a, col, row);
			strcpy (buffer_a, a);
			a = cellref_name (&v->v.cell_range.cell_b, col, row);
			strcpy (buffer_b, a);

			return g_copy_strings (buffer_a, ":", buffer_b, NULL);
		} else {
			if (v->type == VALUE_STRING){
				return g_copy_strings ("\"", v->v.str->str, "\"", NULL);
			} else
				return value_string (v);
		}
	}
	
	case OP_VAR: {
		CellRef *cell_ref;

		cell_ref = &tree->u.constant->v.cell;
		return g_strdup (cellref_name (cell_ref, col, row));
	}
	}

	g_warning ("ExprTree: This should not happen\n");
	return g_strdup ("0");
}

char *
expr_decode_tree (ExprTree *tree, int col, int row)
{
	g_return_val_if_fail (tree != NULL, NULL);

	return do_expr_decode_tree (tree, col, row, OP_CONSTANT);
}
