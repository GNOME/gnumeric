/*
 * expr.c: Expression evaluation in Gnumeriuc
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 */
#include <config.h>
#include <gnome.h>
#include <math.h>
#include <string.h>
#include "gnumeric.h"
#include "expr.h"
#include "eval.h"
#include "format.h"
#include "func.h"
#include "utils.h"

ExprTree *
expr_parse_string (const char *expr, Sheet *sheet, int col, int row,
		   const char **desired_format, char **error_msg)
{
	ExprTree *tree;
	g_return_val_if_fail (expr != NULL, NULL);

	switch (gnumeric_expr_parser (expr, sheet, col, row, desired_format, &tree)) {
	case PARSE_OK:
		*error_msg = NULL;
		tree->ref_count = 1;
		return tree;

	case PARSE_ERR_SYNTAX:
		*error_msg = _("Syntax error");
		break;

	case PARSE_ERR_NO_QUOTE:
		*error_msg = _("Missing quote");
		break;
	default:
		g_assert_not_reached ();
		*error_msg = _("Impossible!");
		break;
	}
	return NULL;
}


ExprTree *
expr_tree_new (void)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (!ans)
		return NULL;
	
	ans->ref_count = 1;
	ans->oper = OPER_CONSTANT;
	ans->u.constant = NULL;
	return ans;
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

	tree->ref_count++;
}

static void
do_expr_tree_unref (ExprTree *tree)
{
	if (--tree->ref_count > 0)
		return;

	switch (tree->oper){
	case OPER_VAR:
		break;

	case OPER_CONSTANT:
		value_release (tree->u.constant);
		break;

	case OPER_FUNCALL: {
		GList *l;

		for (l = tree->u.function.arg_list; l; l = l->next)
			do_expr_tree_unref (l->data);
		g_list_free (tree->u.function.arg_list);
		symbol_unref (tree->u.function.symbol);
		break;
	}

	case OPER_ANY_BINARY:
		do_expr_tree_unref (tree->u.binary.value_a);
		do_expr_tree_unref (tree->u.binary.value_b);
		break;

	case OPER_ANY_UNARY:
		do_expr_tree_unref (tree->u.value);
		break;
	default:
		g_warning ("do_expr_tree_unref error\n");
		break;
	}

	g_free (tree);
}

/*
 * expr_tree_unref:
 * Decrements the ref_count for part of a tree.  (All trees are expected
 * to have been created with a ref-count of one, so when we hit zero, we
 * go down over the tree and unref the tree and its leaves stuff.)
 */
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
value_string (const Value *value)
{
	switch (value->type){
	case VALUE_STRING:
		return g_strdup (value->v.str->str);

	case VALUE_INTEGER:
		return g_strdup_printf ("%d", value->v.v_int);

	case VALUE_FLOAT:
		return g_strdup_printf ("%g", value->v.v_float);

	case VALUE_ARRAY:
		return g_strdup ("ARRAY");

	case VALUE_CELLRANGE:
		break;
	default:
		g_warning ("value_string problem\n");
		break;
	}

	return g_strdup ("Internal problem");
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

	case VALUE_ARRAY:{
		guint i;

		for (i = 0; i < value->v.array.x; i++)
			g_free (value->v.array.vals [i]);

		g_free (value->v.array.vals);
		break;
	}

	case VALUE_CELLRANGE:
		break;

	default:
		g_warning ("value_release problem\n");
		break;
	}
	g_free (value);
}

/*
 * Copies a Value.
 */
void
value_copy_to (Value *dest, const Value *source)
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
		value_array_copy_to (dest, source);
		break;
	}
	case VALUE_CELLRANGE:
		dest->v.cell_range = source->v.cell_range;
		break;
	default:
		g_warning ("value_copy_to problem\n");
		break;
	}
}

/*
 * Makes a copy of a Value
 */
Value *
value_duplicate (const Value *value)
{
	Value *new_value;

	g_return_val_if_fail (value != NULL, NULL);
	new_value = g_new (Value, 1);
	value_copy_to (new_value, value);

	return new_value;
}

Value *
value_float (float_t f)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = f;

	return v;
}

Value *
value_int (int i)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_INTEGER;
	v->v.v_int = i;

	return v;
}

Value *
value_str (const char *str)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_STRING;
	v->v.str = string_get (str);

	return v;
}

Value *
value_cellrange (const CellRef *a, const CellRef *b)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_CELLRANGE;
	v->v.cell_range.cell_a = *a;
	v->v.cell_range.cell_b = *b;

	return v;
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
value_get_bool (const Value *v, int *err)
{
	*err = 0;

	switch (v->type) {
	case VALUE_STRING:
		return atoi (v->v.str->str);

	case VALUE_CELLRANGE:
		*err = 1;
		return 0;
		
	case VALUE_INTEGER:
		return v->v.v_int != 0;

	case VALUE_FLOAT:
		return v->v.v_float != 0.0;

	case VALUE_ARRAY:
		return 0;
	default:
		g_warning ("Unhandled value in value_get_boolean");
		break;
	}
	return 0;
}

float_t
value_get_as_double (const Value *v)
{
	switch (v->type)
	{
	case VALUE_STRING:
		return atof (v->v.str->str);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a double: what to do?");
		return 0.0;

	case VALUE_INTEGER:
		return (float_t) v->v.v_int;
		
	case VALUE_ARRAY:
		return 0.0;

	case VALUE_FLOAT:
		return (float_t) v->v.v_float;
	default:
		g_warning ("value_get_as_double type error\n");
		break;
	}
	return 0.0;
}

int
value_get_as_int (const Value *v)
{
	switch (v->type)
	{
	case VALUE_STRING:
		return atoi (v->v.str->str);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a double: what to do?");
		return 0.0;

	case VALUE_INTEGER:
		return v->v.v_int;

	case VALUE_ARRAY:
		return 0.0;

	case VALUE_FLOAT:
		return (int) v->v.v_float;
	default:
		g_warning ("value_get_as_int unknown type\n");
		break;
	}
}

Value *
value_array_new (guint width, guint height)
{
	int x, y;

	Value *v = g_new (Value, 1);
	v->type = VALUE_ARRAY;
	v->v.array.x = width;
	v->v.array.y = height;
	v->v.array.vals = g_new (Value *, width);

	for (x = 0; x < width; x++){
		v->v.array.vals [x] = g_new (Value, height);

		for (y = 0; y < height; y++){
			v->v.array.vals[x][y].type = VALUE_INTEGER;
			v->v.array.vals[x][y].v.v_int = 0;
		}
	}
	return v;
}

void
value_array_resize (Value *v, guint width, guint height)
{
	int x, xcpy, ycpy;
	Value *newval;

	g_return_if_fail (v);
	g_return_if_fail (v->type == VALUE_ARRAY);

	newval = value_array_new (width, height);

	if (width>v->v.array.x)
		xcpy = v->v.array.x;
	else
		xcpy = width;

	if (height>v->v.array.y)
		ycpy = v->v.array.y;
	else
		ycpy = height;

	for (x = 0; x < xcpy; x++){
		memcpy (newval->v.array.vals [x],
			v->v.array.vals [x],
			sizeof (Value) * ycpy);
	}
	v->v.array.vals = newval->v.array.vals;
	v->v.array.x = width;
	v->v.array.y = height;
	value_release (newval);
}

void
value_array_copy_to (Value *v, const Value *src)
{
	int x;

	g_return_if_fail (src->type == VALUE_ARRAY);
	v->type = VALUE_ARRAY;
	v->v.array.x = src->v.array.x;
	v->v.array.y = src->v.array.y;
	v->v.array.vals = g_new (Value *, v->v.array.x);

	for (x = 0; x < v->v.array.x; x++){
		v->v.array.vals [x] = g_new (Value,v->v.array.y);

		memcpy (v->v.array.vals   [x],
			src->v.array.vals [x],
			sizeof(Value)*v->v.array.y);
	}
}

guint
value_area_get_width (Value *v)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE, 1);

	if (v->type == VALUE_ARRAY)
		return v->v.array.x;
	else
		return v->v.cell_range.cell_b.col -
		       v->v.cell_range.cell_a.col + 1;
}

guint
value_area_get_height (Value *v)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE, 1);

	if (v->type == VALUE_ARRAY)
		return v->v.array.y;
	else
		return v->v.cell_range.cell_b.row -
		       v->v.cell_range.cell_a.row + 1;
}

const Value *
value_area_get_at_x_y (Value *v, guint x, guint y)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE,
			      value_int (0));

	if (v->type == VALUE_ARRAY){
		g_return_val_if_fail (v->v.array.x < x &&
				      v->v.array.y < y,
				      value_int (0));
		return &v->v.array.vals [x][y];
	} else {
		CellRef *a, *b;
		Cell *cell;

		a = &v->v.cell_range.cell_a;
		b = &v->v.cell_range.cell_b;
		g_return_val_if_fail (!a->col_relative,
				      value_int (0));
		g_return_val_if_fail (!b->col_relative,
				      value_int (0));
		g_return_val_if_fail (!a->row_relative,
				      value_int (0));
		g_return_val_if_fail (!b->row_relative,
				      value_int (0));
		g_return_val_if_fail (a->col<=b->col,
				      value_int (0));
		g_return_val_if_fail (a->row<=b->row,
				      value_int (0));

		cell = sheet_cell_get (a->sheet, a->col+x, a->row+y);

		if (cell && cell->value)
			return cell->value;
		else
			return value_int (0);
	}
	return value_int (0);
}

static void
cell_ref_make_absolute (CellRef *cell_ref, int eval_col, int eval_row)
{
	g_return_if_fail (cell_ref != NULL);

	if (cell_ref->col_relative)
		cell_ref->col = eval_col + cell_ref->col;

	if (cell_ref->row_relative)
		cell_ref->row = eval_row + cell_ref->row;

	cell_ref->row_relative = 0;
	cell_ref->col_relative = 0;
}

static void
cell_ref_restore_absolute (CellRef *cell_ref, const CellRef *orig, int eval_col, int eval_row)
{
	if (orig->col_relative) {
		cell_ref->col -= eval_col;
		cell_ref->col_relative = 1;
	}

	if (orig->row_relative) {
		cell_ref->row -= eval_row;
		cell_ref->row_relative = 1;
	}
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
	default:
		g_warning ("eval_cell_value error\n");
		break;
	}
	return res;
}

static void
free_values (Value **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		value_release (values [i]);
	g_free (values);
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
		int fn_argc_min = 0, fn_argc_max = 0, var_len = 0;
		char *arg_type = fd->args;
		char *argptr = fd->args;

		/* Get variable limits */
		while (*argptr){
			if (*argptr++ == '|'){
				var_len = 1;
				continue;
			}
			if (!var_len)
				fn_argc_min++;
			fn_argc_max++;
		}

		if (argc > fn_argc_max || argc < fn_argc_min){
			*error_string = _("Invalid number of arguments");
			return NULL;
		}

		values = g_new (Value *, fn_argc_max);

		for (arg = 0; l; l = l->next, arg++, arg_type++){
			ExprTree *t = (ExprTree *) l->data;
			int type_mismatch = 0;

			if (*arg_type=='|')
				arg_type++;

			if ((*arg_type != 'A' &&          /* This is so a cell reference */
			     *arg_type != 'r') ||         /* can be converted to a cell range */
			    !t || (t->oper != OPER_VAR)) { /* without being evaluated */
				if ((v = eval_expr (sheet, t, eval_col,
						    eval_row, error_string))==NULL)
					goto free_list;
			} else {
				g_assert (t->oper == OPER_VAR);
				v = value_cellrange (&t->u.ref,
						     &t->u.ref);
				if (!v->v.cell_range.cell_a.sheet)
					v->v.cell_range.cell_a.sheet = sheet;
				if (!v->v.cell_range.cell_b.sheet)
					v->v.cell_range.cell_b.sheet = sheet;
			}

			switch (*arg_type){
			case 'f':

				if (v->type != VALUE_INTEGER &&
				    v->type != VALUE_FLOAT)
					type_mismatch = 1;
				break;
			case 's':
				if (v->type != VALUE_STRING)
					type_mismatch = 1;
				break;
			case 'r':
				if (v->type != VALUE_CELLRANGE)
					type_mismatch = 1;
				else {
					cell_ref_make_absolute (&v->v.cell_range.cell_a, eval_col, eval_row);
					cell_ref_make_absolute (&v->v.cell_range.cell_b, eval_col, eval_row);
				}
				break;
			case 'a':
				if (v->type != VALUE_ARRAY)
					type_mismatch = 1;
				break;
			case 'A':
				if (v->type != VALUE_ARRAY &&
				    v->type != VALUE_CELLRANGE)
					type_mismatch = 1;

				if (v->type == VALUE_CELLRANGE) {
					cell_ref_make_absolute (&v->v.cell_range.cell_a, eval_col, eval_row);
					cell_ref_make_absolute (&v->v.cell_range.cell_b, eval_col, eval_row);
				}
				break;
			}
			if (type_mismatch){
				free_values (values, arg);
				*error_string = _("Type mismatch");
				return NULL;
			}
			values [arg] = v;
		}
		while (arg < fn_argc_max)
			values [arg++] = NULL;
		v = fd->fn (fd, values, error_string);

	free_list:
		for (i = 0; i < arg; i++){
			if (values [i] != NULL)
				value_release (values [i]);
		}
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
				tree [i].oper = OPER_CONSTANT;
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
function_call_with_values (Sheet *sheet, const char *name, int argc, Value *values[], char **error_string)
{
	FunctionDefinition *fd;
	Value *retval;
	Symbol *sym;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sym = symbol_lookup (global_symbol_table, name);
	if (sym == NULL){
		*error_string = _("Function does not exist");
		return NULL;
	}
	if (sym->type != SYMBOL_FUNCTION){
		*error_string = _("Calling non-function");
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
eval_expr (Sheet *sheet, ExprTree *tree, int eval_col, int eval_row, char **error_string)
{
	Value *a, *b, *res;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (error_string != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	switch (tree->oper){
	case OPER_EQUAL:
	case OPER_NOT_EQUAL:
	case OPER_GT:
	case OPER_GTE:
	case OPER_LT:
	case OPER_LTE: {
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
		case OPER_EQUAL:
			res->v.v_int = comp == IS_EQUAL;
			break;

		case OPER_GT:
			res->v.v_int = comp == IS_GREATER;
			break;

		case OPER_LT:
			res->v.v_int = comp == IS_LESS;
			break;

		case OPER_LTE:
			res->v.v_int = (comp == IS_EQUAL || comp == IS_LESS);
			break;

		case OPER_GTE:
			res->v.v_int = (comp == IS_EQUAL || comp == IS_GREATER);
			break;

		case OPER_NOT_EQUAL:
			res->v.v_int = comp != IS_EQUAL;
			break;

		default:
			g_warning ("This should never be reached: comparission ops\n");
		}
		value_release (a);
		value_release (b);
		return res;
	}

	case OPER_ADD:
	case OPER_SUB:
	case OPER_MULT:
	case OPER_DIV:
	case OPER_EXP:
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

		if (!VALUE_IS_NUMBER (a) || !VALUE_IS_NUMBER (b)){
			value_release (a);
			value_release (b);
			*error_string = _("Type mismatch");
			return NULL;
		}

		res = g_new (Value, 1);
		if (a->type == VALUE_INTEGER && b->type == VALUE_INTEGER){
			int ia = a->v.v_int;
			int ib = b->v.v_int;

			res->type = VALUE_INTEGER;

			switch (tree->oper){
			case OPER_SUB:
			case OPER_ADD: {
				int sum;

				if (tree->oper == OPER_SUB){
					ib = -ib;
				}

				sum = ia + ib;

				if ((ia > 0) && (ib > 0)){

					if (sum < ia){
						res->type = VALUE_FLOAT;
						res->v.v_float = ((double)ia) + ib;
					} else
						res->v.v_int = sum;
				} else if ((ia < 0) && (ib < 0)){
					if (sum > ia){
						res->type = VALUE_FLOAT;
						res->v.v_float = ((double)ia) + ib;
					} else
						res->v.v_int = sum;
				} else
					res->v.v_int = sum;

				break;
			}

			case OPER_MULT:
				res->v.v_int = a->v.v_int * b->v.v_int;
				break;

			case OPER_DIV:
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

			case OPER_EXP:
				res->v.v_int = pow (a->v.v_int, b->v.v_int);
				break;
			default:
				break;
			}
		} else {
			res->type = VALUE_FLOAT;
			res->v.v_float = 0.0;
			a = value_cast_to_float (a);
			b = value_cast_to_float (b);

			switch (tree->oper){
			case OPER_ADD:
				res->v.v_float = a->v.v_float + b->v.v_float;
				break;

			case OPER_SUB:
				res->v.v_float = a->v.v_float - b->v.v_float;
				break;

			case OPER_MULT:
				res->v.v_float = a->v.v_float * b->v.v_float;
				break;

			case OPER_DIV:
				if (b->v.v_float == 0.0){
					value_release (a);
					value_release (b);
					value_release (res);
					*error_string = _("Division by zero");
					return NULL;
				}

				res->v.v_float = a->v.v_float / b->v.v_float;
				break;

			case OPER_EXP:
				res->v.v_float = pow (a->v.v_float, b->v.v_float);
				break;
			default:
				break;
			}
		}
		value_release (a);
		value_release (b);
		return res;

	case OPER_CONCAT: {
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

		tmp = g_strconcat (sa, sb, NULL);
		res->v.str = string_get (tmp);
		g_free (sa);
		g_free (sb);
		g_free (tmp);

		value_release (a);
		value_release (b);
		return res;
	}

	case OPER_FUNCALL:
		return eval_funcall (sheet, tree, eval_col, eval_row, error_string);

	case OPER_VAR: {
		Sheet *cell_sheet;
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

		ref = &tree->u.ref;
		cell_get_abs_col_row (ref, eval_col, eval_row, &col, &row);

		cell_sheet = ref->sheet ? ref->sheet : sheet;

		cell = sheet_cell_get (cell_sheet, col, row);

		if (cell){
			if (cell->generation != sheet->workbook->generation){
				cell->generation = sheet->workbook->generation;

				if (cell->parsed_node)
					cell_eval (cell);
			}

			if (cell->value)
				return eval_cell_value (sheet, cell->value);
			else {
				if (cell->text)
					*error_string = cell->text->str;
				else
					*error_string = _("Reference to newborn cell");
				return NULL;
			}
		}
		res = g_new (Value, 1);

		res->type = VALUE_INTEGER;
		res->v.v_int = 0;
		return res;
	}

	case OPER_CONSTANT:
		return eval_cell_value (sheet, tree->u.constant);

	case OPER_NEG:
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
cell_get_abs_col_row (const CellRef *cell_ref, int eval_col, int eval_row, int *col, int *row)
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
	if (x == OPER_EXP)
		return 3;
	if ((x==OPER_MULT) || (x==OPER_DIV))
		return 2;
	if ((x==OPER_ADD)  || (x==OPER_SUB)   || (x==OPER_CONCAT))
		return 1;
	return 0;
}

static int
bigger_prec (Operation parent, Operation this)
{
	int parent_level, this_level;

	parent_level = evaluate_level (parent);
	this_level   = evaluate_level (this);

	return parent_level >= this_level;
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
do_expr_decode_tree (ExprTree *tree, Sheet *sheet, int col, int row, Operation parent_op)
{
	static const char *operation_names [] = {
		"=", ">", "<", ">=", "<=", "<>",
		"+", "-", "*", "/",  "^",  "&",
		NULL, NULL, NULL, "-"
	};

	switch (tree->oper){
	case OPER_ANY_BINARY: {
		char *a, *b, *res;
		char const *op;

		a = do_expr_decode_tree (tree->u.binary.value_a, sheet, col, row, tree->oper);
		b = do_expr_decode_tree (tree->u.binary.value_b, sheet, col, row, tree->oper);
		op = operation_names [tree->oper];

		if (bigger_prec (parent_op, tree->oper))
			res = g_strconcat ("(", a, op, b, ")", NULL);
		else
			res = g_strconcat (a, op, b, NULL);

		g_free (a);
		g_free (b);
		return res;
	}

	case OPER_ANY_UNARY: {
		char *res, *a;

		a = do_expr_decode_tree (tree->u.value, sheet, col, row, tree->oper);
		res = g_strconcat (operation_names[tree->oper], a, NULL);
		g_free (a);
		return res;
	}

	case OPER_FUNCALL: {
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

				args [i] = do_expr_decode_tree (t, sheet, col, row, OPER_CONSTANT);
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

			res = g_strconcat (
				fd->name, "(", sum, ")", NULL);
			g_free (sum);

			for (i = 0; i < argc; i++)
				g_free (args [i]);
			g_free (args);

			return res;
		} else
			return g_strconcat (fd->name, "()", NULL);
	}

	case OPER_VAR: {
		CellRef *cell_ref;

		cell_ref = &tree->u.ref;
		return cellref_name (cell_ref, sheet, col, row);
	}

	case OPER_CONSTANT: {
		Value *v = tree->u.constant;

		if (v->type == VALUE_CELLRANGE){
			char *a, *b, *res;

			a = cellref_name (&v->v.cell_range.cell_a, sheet, col, row);
			b = cellref_name (&v->v.cell_range.cell_b, sheet, col, row);

			res = g_strconcat (a, ":", b, NULL);

			g_free (a);
			g_free (b);

			return res;
		} else {
			if (v->type == VALUE_STRING){
				return g_strconcat ("\"", v->v.str->str, "\"", NULL);
			} else
				return value_string (v);
		}
	}
	}

	g_warning ("ExprTree: This should not happen\n");
	return g_strdup ("0");
}

char *
expr_decode_tree (ExprTree *tree, Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	return do_expr_decode_tree (tree, sheet, col, row, OPER_CONSTANT);
}

static ExprTree *
do_expr_tree_relocate (ExprTree *tree, int coldiff, int rowdiff)
{
	ExprTree *new_tree;

	new_tree = g_new (ExprTree, 1);
	*new_tree = *tree;

	new_tree->ref_count = 1;

	switch (tree->oper){
	case OPER_ANY_BINARY:
		new_tree->u.binary.value_a =
			do_expr_tree_relocate (tree->u.binary.value_a, coldiff, rowdiff);
		new_tree->u.binary.value_b =
			do_expr_tree_relocate (tree->u.binary.value_b, coldiff, rowdiff);
		break;

	case OPER_ANY_UNARY:
		new_tree->u.value = do_expr_tree_relocate (tree->u.value, coldiff, rowdiff);
		break;

	case OPER_FUNCALL: {
		GList *l, *arg_list;

		symbol_ref (tree->u.function.symbol);
		new_tree->u.function.arg_list = NULL;

		for (l = tree->u.function.arg_list; l; l = l->next){
			ExprTree *tree = l->data;

			new_tree->u.function.arg_list =
				g_list_append (new_tree->u.function.arg_list,
					       do_expr_tree_relocate (tree, coldiff, rowdiff));
		}
		break;
	}

	case OPER_VAR: {
		CellRef *ref;

		ref = &new_tree->u.ref;
		ref->col -= coldiff;
		ref->row -= rowdiff;
		break;
	}

	case OPER_CONSTANT:
		new_tree->u.constant = value_duplicate (tree->u.constant);

		if (new_tree->u.constant->type == VALUE_CELLRANGE){
			CellRef *ref;

			ref = &new_tree->u.constant->v.cell_range.cell_a;
			ref->col -= coldiff;
			ref->row -= rowdiff;

			ref = &new_tree->u.constant->v.cell_range.cell_b;
			ref->col -= coldiff;
			ref->row -= rowdiff;
			break;
		}
		break;
	}

	return new_tree;
}

ExprTree *
expr_tree_relocate (ExprTree *tree, int coldiff, int rowdiff)
{
	g_return_val_if_fail (tree != NULL, NULL);

	return do_expr_tree_relocate (tree, coldiff, rowdiff);
}


static ExprTree *
build_error_string (const char *txt)
{
	ExprTree *val, *call;
	Symbol *func;

	val = g_new (ExprTree, 1);
	val->oper = OPER_CONSTANT;
	val->ref_count = 1;
	val->u.constant = value_str (txt);

	func = symbol_lookup (global_symbol_table, "ERROR");
	if (func == NULL) {
		g_assert_not_reached ();
		return val;
	}

	call = g_new (ExprTree, 1);
	call->oper = OPER_FUNCALL;
	call->ref_count = 1;
	symbol_ref ((call->u.function.symbol = func));
	call->u.function.arg_list = g_list_prepend (NULL, val);

	return call;
}


struct expr_tree_frob_references {
	Sheet *src_sheet;
	int src_col, src_row;

	gboolean invalidating_columns;

	/* Columns being invalidated.  */
	int col, colcount;

	/* Rows being invalidated.  */
	int row, rowcount;

	/* Relative move (fixup only).  */
	int coldelta, rowdelta;

                /* In this sheet */
	Sheet *sheet;

	/* Are we deleting?  (fixup only).  */
	gboolean deleting;
};


static gboolean
cell_in_range (const CellRef *cr, const struct expr_tree_frob_references *info)
{
	return info->invalidating_columns
		? (cr->col >= info->col && cr->col < info->col + info->colcount)
		: (cr->row >= info->row && cr->row < info->row + info->rowcount);
}

static ExprTree *
do_expr_tree_invalidate_references (ExprTree *src, const struct expr_tree_frob_references *info)
{
	switch (src->oper) {
	case OPER_ANY_BINARY: {
		ExprTree *a =
			do_expr_tree_invalidate_references (src->u.binary.value_a, info);
		ExprTree *b =
			do_expr_tree_invalidate_references (src->u.binary.value_b, info);
		if (a == NULL && b == NULL)
			return NULL;
		else {
			ExprTree *dst;

			if (a == NULL)
				expr_tree_ref ((a = src->u.binary.value_a));
			if (b == NULL)
				expr_tree_ref ((b = src->u.binary.value_b));

			dst = g_new (ExprTree, 1);
			dst->oper = src->oper;
			dst->ref_count = 1;
			dst->u.binary.value_a = a;
			dst->u.binary.value_b = b;
			return dst;
		}
	}

	case OPER_ANY_UNARY: {
		ExprTree *a =
			do_expr_tree_invalidate_references (src->u.value, info);
		if (a == NULL)
			return NULL;
		else {
			ExprTree *dst;

			dst = g_new (ExprTree, 1);
			dst->oper = src->oper;
			dst->ref_count = 1;
			dst->u.value = a;
			return dst;
		}
	}

	case OPER_FUNCALL: {
		gboolean any = FALSE;
		GList *new_args = NULL;
		GList *l;

		for (l = src->u.function.arg_list; l; l = l->next) {
			ExprTree *arg = do_expr_tree_invalidate_references (l->data, info);
			new_args = g_list_append (new_args, arg);
			if (arg) any = TRUE;
		}

		if (any) {
			ExprTree *dst;
			GList *m;

			for (l = src->u.function.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					expr_tree_ref ((m->data = l->data));
			}

			dst = g_new (ExprTree, 1);
			dst->oper = OPER_FUNCALL;
			dst->ref_count = 1;
			symbol_ref ((dst->u.function.symbol = src->u.function.symbol));
			dst->u.function.arg_list = new_args;

			return dst;
		} else {
			g_list_free (new_args);
			return NULL;
		}
	}

	case OPER_VAR: {
		CellRef cr = src->u.ref; /* Copy a structure, not a pointer.  */

		/* If the sheet is wrong, do nothing.  */
		if (cr.sheet != info->sheet)
			return NULL;

		cell_ref_make_absolute (&cr, info->src_col, info->src_row);

		if (cell_in_range (&cr, info))
			return build_error_string ("#Reference to deleted cell!");
		else
			return NULL;
	}

	case OPER_CONSTANT: {
		Value *v = src->u.constant;

		switch (v->type) {
		case VALUE_STRING:
		case VALUE_INTEGER:
		case VALUE_FLOAT:
			return NULL;
		case VALUE_CELLRANGE: {
			CellRef ca = v->v.cell_range.cell_a; /* Copy a structure, not a pointer.  */
			CellRef cb = v->v.cell_range.cell_b; /* Copy a structure, not a pointer.  */

			/* If the sheet is wrong, do nothing.  */
			if (ca.sheet != info->sheet)
				return NULL;

			cell_ref_make_absolute (&ca, info->src_col, info->src_row);
			cell_ref_make_absolute (&cb, info->src_col, info->src_row);

			if (cell_in_range (&ca, info) && cell_in_range (&cb, info))
				return build_error_string ("#Reference to deleted range!");
			else
				return NULL;
		}
		case VALUE_ARRAY:
			fprintf (stderr, "Reminder: FIXME in do_expr_tree_invalidate_references\n");
			/* ??? */
			return NULL;
		default:
			g_warning ("do_expr_tree_invalidate_references error\n");
			break;
		}
		g_assert_not_reached ();
		return NULL;
	}
	}

	g_assert_not_reached ();
	return NULL;
}

/*
 * Invalidate (== turn into error expressions) cell and range references that
 * will disappear completely during a deletions of columns or rows.  Note that
 * this is not intended for use when just the cell contents is delete.
 */
ExprTree *
expr_tree_invalidate_references (ExprTree *src, Sheet *src_sheet,
				 int src_col, int src_row,
				 Sheet *sheet,
				 int col, int row,
				 int colcount, int rowcount)
{
	struct expr_tree_frob_references info;
	ExprTree *dst;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (src_sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (src_sheet), NULL);
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	info.src_sheet = src_sheet;
	info.src_col = src_col;
	info.src_row = src_row;
	info.invalidating_columns = (colcount > 0);
	info.sheet = sheet;
	info.col = col;
	info.colcount = colcount;
	info.row = row;
	info.rowcount = rowcount;

	{
		char *str;
		str = expr_decode_tree (src, src_sheet, src_col, src_row);
		printf ("Invalidate: %s: [%s]\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	dst = do_expr_tree_invalidate_references (src, &info);

	{
		char *str;
		str = dst ? expr_decode_tree (dst, src_sheet, src_col, src_row) : g_strdup ("*");
		printf ("Invalidate: %s: [%s]\n\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	return dst;
}

static ExprTree *
do_expr_tree_fixup_references (ExprTree *src, const struct expr_tree_frob_references *info)
{
	switch (src->oper) {
	case OPER_ANY_BINARY: {
		ExprTree *a =
			do_expr_tree_fixup_references (src->u.binary.value_a, info);
		ExprTree *b =
			do_expr_tree_fixup_references (src->u.binary.value_b, info);
		if (a == NULL && b == NULL)
			return NULL;
		else {
			ExprTree *dst;

			if (a == NULL)
				expr_tree_ref ((a = src->u.binary.value_a));
			if (b == NULL)
				expr_tree_ref ((b = src->u.binary.value_b));

			dst = g_new (ExprTree, 1);
			dst->oper = src->oper;
			dst->ref_count = 1;
			dst->u.binary.value_a = a;
			dst->u.binary.value_b = b;
			return dst;
		}
	}

	case OPER_ANY_UNARY: {
		ExprTree *a =
			do_expr_tree_fixup_references (src->u.value, info);
		if (a == NULL)
			return NULL;
		else {
			ExprTree *dst;

			dst = g_new (ExprTree, 1);
			dst->oper = src->oper;
			dst->ref_count = 1;
			dst->u.value = a;
			return dst;
		}
	}

	case OPER_FUNCALL: {
		gboolean any = FALSE;
		GList *new_args = NULL;
		GList *l;

		for (l = src->u.function.arg_list; l; l = l->next) {
			ExprTree *arg = do_expr_tree_fixup_references (l->data, info);
			new_args = g_list_append (new_args, arg);
			if (arg) any = TRUE;
		}

		if (any) {
			ExprTree *dst;
			GList *m;

			for (l = src->u.function.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					expr_tree_ref ((m->data = l->data));
			}

			dst = g_new (ExprTree, 1);
			dst->oper = OPER_FUNCALL;
			dst->ref_count = 1;
			symbol_ref ((dst->u.function.symbol = src->u.function.symbol));
			dst->u.function.arg_list = new_args;

			return dst;
		} else {
			g_list_free (new_args);
			return NULL;
		}
	}

	case OPER_VAR: {
		CellRef cr = src->u.ref; /* Copy a structure, not a pointer.  */

                                /* If the sheet is wrong, do nothing.  */
		if (cr.sheet != info->sheet)
			return NULL;

		cell_ref_make_absolute (&cr, info->src_col, info->src_row);

		if (cr.col >= info->col && cr.row >= info->row) {
			ExprTree *dst;

			cr.col += info->coldelta;
			cr.row += info->rowdelta;

			cell_ref_restore_absolute (&cr, &src->u.ref, info->src_col, info->src_row);

			dst = g_new (ExprTree, 1);
			dst->oper = src->oper;
			dst->ref_count = 1;
			dst->u.ref = cr;

			return dst;
		} else
			return NULL;
	}

	case OPER_CONSTANT: {
		Value *v = src->u.constant;

		switch (v->type) {
		case VALUE_STRING:
		case VALUE_INTEGER:
		case VALUE_FLOAT:
			return NULL;

		case VALUE_CELLRANGE: {
			gboolean any = FALSE;

			CellRef ca = v->v.cell_range.cell_a; /* Copy a structure, not a pointer.  */
			CellRef cb = v->v.cell_range.cell_b; /* Copy a structure, not a pointer.  */

			/* If the sheet is wrong, do nothing.  */
			if (ca.sheet != info->sheet)
				return NULL;

			cell_ref_make_absolute (&ca, info->src_col, info->src_row);
			cell_ref_make_absolute (&cb, info->src_col, info->src_row);

			if (ca.col >= info->col && ca.row >= info->row) {
				any = TRUE;
				if (info->deleting && cell_in_range (&ca, info)) {
					if (info->colcount)
						ca.col = info->col;
					else
						ca.row = info->row;
				} else {
					ca.col += info->coldelta;
					ca.row += info->rowdelta;
				}
			}

			if (cb.col >= info->col && cb.row >= info->row) {
				any = TRUE;
				if (info->deleting && cell_in_range (&cb, info)) {
					if (info->colcount)
						cb.col = info->col - 1;
					else
						cb.row = info->row - 1;
				} else {
					cb.col += info->coldelta;
					cb.row += info->rowdelta;
				}
			}

			if (any) {
				ExprTree *dst;
				Value *nv;

				cell_ref_restore_absolute (&ca, &v->v.cell_range.cell_a, info->src_col, info->src_row);
				cell_ref_restore_absolute (&cb, &v->v.cell_range.cell_b, info->src_col, info->src_row);

				nv = g_new (Value, 1);
				nv->type = v->type;
				nv->v.cell_range.cell_a = ca;
				nv->v.cell_range.cell_b = cb;

				dst = g_new (ExprTree, 1);
				dst->oper = src->oper;
				dst->ref_count = 1;
				dst->u.constant = nv;

				return dst;
			} else
				return NULL;
		}
		case VALUE_ARRAY:
			fprintf (stderr, "Reminder: FIXME in do_expr_tree_fixup_references\n");
			/* ??? */
			return NULL;
		}
		g_assert_not_reached ();
		return NULL;
	}
	}

	g_assert_not_reached ();
	return NULL;
}


ExprTree *
expr_tree_fixup_references (ExprTree *src, Sheet *src_sheet,
			    int src_col, int src_row, Sheet *sheet, int col, int row,
			    int coldelta, int rowdelta)
{
	struct expr_tree_frob_references info;
	ExprTree *dst;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (src_sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (src_sheet), NULL);
	g_return_val_if_fail(sheet != NULL, NULL);
	g_return_val_if_fail(IS_SHEET(sheet), NULL);
        
	info.src_sheet = src_sheet;
	info.src_col = src_col;
	info.src_row = src_row;
	info.sheet = sheet;
	info.col = col;
	info.row = row;
	info.coldelta = coldelta;
	info.rowdelta = rowdelta;
	info.deleting = (coldelta < 0 || rowdelta < 0);
	info.colcount = (coldelta < 0) ? -coldelta : coldelta;
	info.rowcount = (rowdelta < 0) ? -rowdelta : rowdelta;

	{
		char *str;
		str = expr_decode_tree (src, src_sheet, src_col, src_row);
		printf ("Fixup: %s: [%s]\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	dst = do_expr_tree_fixup_references (src, &info);

	{
		char *str;
		str = dst ? expr_decode_tree (dst, src_sheet, src_col, src_row) : g_strdup ("*");
		printf ("Fixup: %s: [%s]\n\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	return dst;
}
