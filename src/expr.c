/*
 * expr.c: Expression evaluation in Gnumeric
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

Value *value_zero = NULL;

EvalPosition *
eval_pos_init (EvalPosition *fp, Sheet *s, int col, int row)
{
	g_return_val_if_fail (s, NULL);
	g_return_val_if_fail (fp, NULL);
	g_return_val_if_fail (IS_SHEET (s), NULL);

	fp->sheet = s;
	fp->eval_col = col;
	fp->eval_row = row;
	return fp;
}

EvalPosition *
eval_pos_cell (EvalPosition *fp, Cell *cell)
{
	g_return_val_if_fail (fp, NULL);
	g_return_val_if_fail (cell, NULL);
	g_return_val_if_fail (cell->sheet, NULL);
	g_return_val_if_fail (IS_SHEET (cell->sheet), NULL);

	return eval_pos_init (fp,
			      cell->sheet,
			      cell->col->pos,
			      cell->row->pos);
}

FunctionEvalInfo *
func_eval_info_init (FunctionEvalInfo *s, Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (s, NULL);
       
	eval_pos_init (&s->pos, sheet, col, row);
	s->error = error_message_new ();
	s->func_def = 0;
	return s;
}

FunctionEvalInfo *
func_eval_info_cell (FunctionEvalInfo *s, Cell *cell)
{
	g_return_val_if_fail (s, NULL);
	g_return_val_if_fail (cell, NULL);

	return func_eval_info_init (s,
				  cell->sheet,
				  cell->col->pos,
				  cell->row->pos);
}

FunctionEvalInfo *
func_eval_info_pos (FunctionEvalInfo *s, const EvalPosition *fp)
{
	g_return_val_if_fail (s, NULL);
	g_return_val_if_fail (fp, NULL);

	return func_eval_info_init (s,
				    fp->sheet,
				    fp->eval_col,
				    fp->eval_row);
}

ErrorMessage *error_message_new (void)
{
	ErrorMessage *em = g_new (ErrorMessage, 1);

	em->err_msg      = 0;
	em->err_alloced  = 0;
	em->small_err[0] = '\0';
	return em;
}

#define ERROR_MESSAGE_CLEAN(em) do { em->err_msg = NULL; \
                                     if (em->err_alloced) { \
					     g_free (em->err_alloced); \
					     em->err_alloced = NULL; \
				     } \
                                     em->small_err[0] = '\0'; \
				} while (0)

void
error_message_set (ErrorMessage *em, const char *message)
{
	g_return_if_fail (em);
	ERROR_MESSAGE_CLEAN (em);

	em->err_msg = message;
}

void
error_message_set_alloc (ErrorMessage *em, char *message)
{
	g_return_if_fail (em);
	ERROR_MESSAGE_CLEAN (em);

	em->err_alloced  = message;
}

void
error_message_set_small (ErrorMessage *em, const char *message)
{
	g_return_if_fail (em);
	g_return_if_fail (message);
	g_return_if_fail (strlen(message) < 19);
	ERROR_MESSAGE_CLEAN (em);

	strcpy (em->small_err, message);
}

const char *
error_message_txt (ErrorMessage *em)
{
	if (!em)
		return _("Internal Error");

	if (em->err_msg)
		return em->err_msg;
	if (em->err_alloced)
		return em->err_msg;
	return em->small_err;
}

/* Can be turned into a #define for speed later */
gboolean
error_message_is_set (ErrorMessage *em)
{
	g_return_val_if_fail (em, FALSE);
	
	if (em->err_msg || em->err_alloced ||
	    em->small_err[0] != '\0')
		return TRUE;
	return FALSE;
}

void
error_message_free (ErrorMessage *em)
{
	if (em->err_alloced) {
		g_free (em->err_alloced);
		em->err_alloced = 0;
	}
	g_free (em);
}

ExprTree *
expr_tree_new_constant (Value *v)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (ans) {
		ans->ref_count = 1;
		ans->oper = OPER_CONSTANT;
		ans->u.constant = v;
	}
	return ans;
}

ExprTree *
expr_tree_new_unary  (Operation op, ExprTree *e)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (ans) {
		ans->ref_count = 1;
		ans->oper = op;
		ans->u.value = e;
	}
	return ans;
}


ExprTree *
expr_tree_new_binary (ExprTree *l, Operation op, ExprTree *r)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (ans) {
		ans->ref_count = 1;
		ans->oper = op;
		ans->u.binary.value_a = l;
		ans->u.binary.value_b = r;
	}
	return ans;
}

ExprTree *
expr_tree_new_funcall (Symbol *sym, GList *args)
{
	ExprTree *ans;
	g_return_val_if_fail (sym, NULL);

	ans = g_new (ExprTree, 1);
	if (ans) {
		ans->ref_count = 1;
		ans->oper = OPER_FUNCALL;
		ans->u.function.symbol = sym;;
		ans->u.function.arg_list = args;
	}
	return ans;
}
       
Value *
function_error (FunctionEvalInfo *fe, char *error_string)
{
	g_return_val_if_fail (fe, NULL);
	g_return_val_if_fail (error_string, NULL);

	error_message_set (fe->error, error_string);
	return NULL;
}

Value *
function_error_alloc (FunctionEvalInfo *fe, char *error_string)
{
	g_return_val_if_fail (fe, NULL);
	g_return_val_if_fail (error_string, NULL);

	error_message_set_alloc (fe->error, error_string);
	return NULL;
}

ExprTree *
expr_parse_string (const char *expr, const EvalPosition *fp,
		   const char **desired_format, char **error_msg)
{
	ExprTree *tree;
	g_return_val_if_fail (expr != NULL, NULL);

	switch (gnumeric_expr_parser (expr, fp, desired_format, &tree)) {
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


/*ExprTree *
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
}*/

ExprTree *
expr_tree_new_var (const CellRef *cr)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (ans) {
		ans->ref_count = 1;
		ans->oper = OPER_VAR;
		ans->u.ref = *cr;
	}
	return ans;
}

ExprTree *
expr_tree_new_error (const char *txt)
{
	Symbol *func;
	GList *args = NULL;

	if (strcmp (txt, gnumeric_err_NA) == 0) {
		func = symbol_lookup (global_symbol_table, "NA");
	} else {
		func = symbol_lookup (global_symbol_table, "ERROR");
		args = g_list_prepend (NULL,
				       expr_tree_new_constant (value_new_string (txt)));
	}

	symbol_ref (func);
	return expr_tree_new_funcall (func, args);
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
value_get_as_string (const Value *value)
{
	switch (value->type){
	case VALUE_STRING:
		return g_strdup (value->v.str->str);

	case VALUE_INTEGER:
		return g_strdup_printf ("%d", value->v.v_int);

	case VALUE_FLOAT:
		return g_strdup_printf ("%.*g", DBL_DIG, value->v.v_float);

	case VALUE_ARRAY: {
		GString *str = g_string_new ("{");
		guint lpx, lpy;
		char *ans;

		for (lpy = 0; lpy < value->v.array.y; lpy++){
			for (lpx = 0; lpx < value->v.array.x; lpx++){
				const Value *v = value->v.array.vals [lpx][lpy];

				g_return_val_if_fail (v->type == VALUE_STRING ||
						      v->type == VALUE_FLOAT ||
						      v->type == VALUE_INTEGER,
						      "Duff Array contents");
				if (lpx)
					g_string_sprintfa (str, ",");
				if (v->type == VALUE_STRING)
					g_string_sprintfa (str, "\"%s\"",
							   v->v.str->str);
				else
					g_string_sprintfa (str, "%g",
							   value_get_as_float (v));
			}
			if (lpy<value->v.array.y-1)
				g_string_sprintfa (str, ";");
		}
		g_string_sprintfa (str, "}");
		ans = str->str;
		g_string_free (str, FALSE);
		return ans;
	}

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
		guint lpx, lpy;

		for (lpx = 0; lpx < value->v.array.x; lpx++){
			for (lpy = 0; lpy < value->v.array.y; lpy++)
				value_release (value->v.array.vals [lpx][lpy]);
			g_free (value->v.array.vals [lpx]);
		}

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
value_new_float (float_t f)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = f;

	return v;
}

Value *
value_new_int (int i)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_INTEGER;
	v->v.v_int = i;

	return v;
}

Value *
value_new_bool (gboolean b)
{
	/* Currently our booleans are really just ints.  This will have to
	   change if we want Excel's ISLOGICAL.  */
	return value_new_int (b ? 1 : 0);
}

Value *
value_new_string (const char *str)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_STRING;
	v->v.str = string_get (str);

	return v;
}

Value *
value_new_cellrange (const CellRef *a, const CellRef *b)
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
value_get_as_bool (const Value *v, int *err)
{
	*err = 0;

	switch (v->type) {
	case VALUE_STRING:
		/* FIXME FIXME FIXME */
		/* Use locale to support TRUE, FALSE */
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
value_get_as_float (const Value *v)
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
		g_warning ("value_get_as_float type error\n");
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
		g_warning ("Getting range as a int: what to do?");
		return 0;

	case VALUE_INTEGER:
		return v->v.v_int;

	case VALUE_ARRAY:
		return 0;

	case VALUE_FLOAT:
		return (int) v->v.v_float;
	default:
		g_warning ("value_get_as_int unknown type\n");
		return 0;
	}
	return 0.0;
}

Value *
value_array_new (guint width, guint height)
{
	int x, y;

	Value *v = g_new (Value, 1);
	v->type = VALUE_ARRAY;
	v->v.array.x = width;
	v->v.array.y = height;
	v->v.array.vals = g_new (Value **, width);

	for (x = 0; x < width; x++){
		v->v.array.vals [x] = g_new (Value *, height);
		for (y = 0; y < height; y++)
			v->v.array.vals[x][y] = value_new_int (0);
	}
	return v;
}

void
value_array_set (Value *array, guint col, guint row, Value *v)
{
	g_return_if_fail (v);
	g_return_if_fail (array->type == VALUE_ARRAY);
	g_return_if_fail (col>=0);
	g_return_if_fail (row>=0);
	g_return_if_fail (array->v.array.y > row);
	g_return_if_fail (array->v.array.x > col);

	if (array->v.array.vals[col][row])
		value_release (array->v.array.vals[col][row]);
	array->v.array.vals[col][row] = v;
}

void
value_array_resize (Value *v, guint width, guint height)
{
	int x, y, xcpy, ycpy;
	Value *newval;
	Value ***tmp;

	g_warning ("Totally untested");
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

	for (x = 0; x < xcpy; x++)
		for (y = 0; y < ycpy; y++)
			value_array_set (newval, x, y, v->v.array.vals[x][y]);

	tmp = v->v.array.vals;
	v->v.array.vals = newval->v.array.vals;
	newval->v.array.vals = tmp;
	value_release (newval);

	v->v.array.x = width;
	v->v.array.y = height;
}

void
value_array_copy_to (Value *v, const Value *src)
{
	int x, y;

	g_return_if_fail (src->type == VALUE_ARRAY);
	v->type = VALUE_ARRAY;
	v->v.array.x = src->v.array.x;
	v->v.array.y = src->v.array.y;
	v->v.array.vals = g_new (Value **, v->v.array.x);

	for (x = 0; x < v->v.array.x; x++) {
		v->v.array.vals [x] = g_new (Value *, v->v.array.y);
		for (y = 0; y < v->v.array.y; y++)
			v->v.array.vals [x][y] = value_duplicate (src->v.array.vals [x][y]);
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
	else {
		guint ans = v->v.cell_range.cell_b.col -
			    v->v.cell_range.cell_a.col + 1;
		if (v->v.cell_range.cell_a.sheet && 
		    v->v.cell_range.cell_a.sheet->max_col_used < ans)
			ans = v->v.cell_range.cell_a.sheet->max_col_used+1;
		return ans;
	}
}

guint
value_area_get_height (Value *v)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE, 1);

	if (v->type == VALUE_ARRAY)
		return v->v.array.y;
	else {
		guint ans = v->v.cell_range.cell_b.row -
		            v->v.cell_range.cell_a.row + 1;
		if (v->v.cell_range.cell_a.sheet && 
		    v->v.cell_range.cell_a.sheet->max_row_used < ans)
			ans = v->v.cell_range.cell_a.sheet->max_row_used+1;
		return ans;
	}
}

const Value *
value_area_get_at_x_y (Value *v, guint x, guint y)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE,
			     value_new_int (0));

	if (v->type == VALUE_ARRAY){
		g_return_val_if_fail (v->v.array.x < x &&
				      v->v.array.y < y,
				     value_new_int (0));
		return v->v.array.vals [x][y];
	} else {
		CellRef *a, *b;
		Cell *cell;

		a = &v->v.cell_range.cell_a;
		b = &v->v.cell_range.cell_b;
		g_return_val_if_fail (!a->col_relative, value_zero);
		g_return_val_if_fail (!b->col_relative, value_zero);
		g_return_val_if_fail (!a->row_relative, value_zero);
		g_return_val_if_fail (!b->row_relative, value_zero);
		g_return_val_if_fail (a->col<=b->col, value_zero);
		g_return_val_if_fail (a->row<=b->row, value_zero);
		g_return_val_if_fail (a->sheet,       value_zero);

		/* Speedup */
		if (a->sheet->max_col_used < a->col+x ||
		    a->sheet->max_row_used < a->row+y)
			return value_zero;

		cell = sheet_cell_get (a->sheet, a->col+x, a->row+y);

		if (cell && cell->value)
			return cell->value;
	}
	
	return value_zero;
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

static void
free_values (Value **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values[i])
			value_release (values [i]);
	g_free (values);
}

static Value *
eval_funcall (FunctionEvalInfo *s, ExprTree *tree)
{
	const Symbol *sym;
	FunctionDefinition *fd;
	GList *l;
	int argc, arg, i;
	Value *v = NULL;
	Sheet *sheet;
	int eval_col;
	int eval_row;

	g_return_val_if_fail (s, NULL);
	g_return_val_if_fail (tree, NULL);

	sym = tree->u.function.symbol;

	sheet    = s->pos.sheet;
	eval_col = s->pos.eval_col;
	eval_row = s->pos.eval_row;

	s->func_def = fd;

	l = tree->u.function.arg_list;
	argc = g_list_length (l);

	if (sym->type != SYMBOL_FUNCTION)
		return function_error (s, _("Internal error"));

	fd = (FunctionDefinition *)sym->data;

	if (fd->fn_type == FUNCTION_NODES) {
		/* Functions that deal with ExprNodes */		
		v = ((FunctionNodes *)fd->fn) (s, l);
	} else {
		/* Functions that take pre-computed Values */
		Value **values;
		int fn_argc_min = 0, fn_argc_max = 0, var_len = 0;
		const char *arg_type = fd->args;
		const char *argptr = fd->args;

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

		if (argc > fn_argc_max || argc < fn_argc_min) {
			error_message_set (s->error, _("Invalid number of arguments"));
			return NULL;
		}

		values = g_new (Value *, fn_argc_max);

		for (arg = 0; l; l = l->next, arg++, arg_type++){
			ExprTree *t = (ExprTree *) l->data;
			int type_mismatch = 0;
			Value *v;

			if (*arg_type=='|')
				arg_type++;

			if ((*arg_type != 'A' &&          /* This is so a cell reference */
			     *arg_type != 'r') ||         /* can be converted to a cell range */
			    !t || (t->oper != OPER_VAR)) { /* without being evaluated */
				if ((v = (Value *)eval_expr (s, t)) == NULL)
					goto free_list;
			} else {
				g_assert (t->oper == OPER_VAR);
				v = value_new_cellrange (&t->u.ref,
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
			values [arg] = v;
			if (type_mismatch){
				free_values (values, arg + 1);
				error_message_set (s->error, _("Type mismatch"));
				return NULL;
			}
		}
		while (arg < fn_argc_max)
			values [arg++] = NULL;
		v = ((FunctionArgs *)fd->fn) (s, values);

	free_list:
		free_values (values, arg);
	}
	return v;
}

Value *
function_def_call_with_values (const EvalPosition *ep,
			       FunctionDefinition *fd,
			       int                 argc,
			       Value              *values [],
			       ErrorMessage       *error)
{
	Value *retval;
	FunctionEvalInfo s;

	func_eval_info_pos (&s, ep);
	s.error = error;

	if (fd->fn_type == FUNCTION_NODES) {
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

		retval = ((FunctionNodes *)fd->fn) (&s, l);

		if (tree){
			g_free (tree);
			g_list_free (l);
		}

	} else
		retval = ((FunctionArgs *)fd->fn) (&s, values);

	return retval;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
Value *
function_call_with_values (const EvalPosition *ep, const char *name,
			   int argc, Value *values[], ErrorMessage *error)
{
	FunctionDefinition *fd;
	Value *retval;
	Symbol *sym;

	g_return_val_if_fail (ep, NULL);
	g_return_val_if_fail (ep->sheet != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sym = symbol_lookup (global_symbol_table, name);
	if (sym == NULL){
		error_message_set (error, _("Function does not exist"));
		return NULL;
	}
	if (sym->type != SYMBOL_FUNCTION){
		error_message_set (error, _("Calling non-function"));
		return NULL;
	}

	fd = sym->data;

	symbol_ref (sym);
	retval = function_def_call_with_values (ep, fd, argc,
						values, error);
	
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
compare (const Value *a, const Value *b)
{
	if (a->type == VALUE_INTEGER){
		int f;

		switch (b->type){
		case VALUE_INTEGER:
			return compare_int_int (a->v.v_int, b->v.v_int);

		case VALUE_FLOAT:
			return compare_float_float (a->v.v_int, b->v.v_float);

		case VALUE_STRING:
			f = value_get_as_float (b);
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
			f = value_get_as_float (b);
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
eval_expr (FunctionEvalInfo *s, ExprTree *tree)
{
	Value *res, *a, *b;
	
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (s != NULL, NULL);

	switch (tree->oper){
	case OPER_EQUAL:
	case OPER_NOT_EQUAL:
	case OPER_GT:
	case OPER_GTE:
	case OPER_LT:
	case OPER_LTE: {
		int comp;

		a = (Value *)eval_expr (s, tree->u.binary.value_a);
		if (!a)
			return NULL;

		b = (Value *)eval_expr (s, tree->u.binary.value_b);
		if (!b) {
			value_release (a);
			return NULL;
		}

		comp = compare (a, b);
		value_release (a);
		value_release (b);

		if (comp == TYPE_ERROR){
			error_message_set (s->error, _("Type error"));
			return NULL;
		}

		switch (tree->oper){
		case OPER_EQUAL:
			res = value_new_bool (comp == IS_EQUAL);
			break;

		case OPER_GT:
			res = value_new_bool (comp == IS_GREATER);
			break;

		case OPER_LT:
			res = value_new_bool (comp == IS_LESS);
			break;

		case OPER_LTE:
			res = value_new_bool (comp == IS_EQUAL || comp == IS_LESS);
			break;

		case OPER_GTE:
			res = value_new_bool (comp == IS_EQUAL || comp == IS_GREATER);
			break;

		case OPER_NOT_EQUAL:
			res = value_new_bool (comp != IS_EQUAL);
			break;

		default:
			g_assert_not_reached ();
			error_message_set (s->error, _("Internal type error"));
			res = NULL;
		}
		return (Value *)res;
	}

	case OPER_ADD:
	case OPER_SUB:
	case OPER_MULT:
	case OPER_DIV:
	case OPER_EXP:
		a = (Value *)eval_expr (s, tree->u.binary.value_a);

		if (!a)
			return NULL;

		b = (Value *)eval_expr (s, tree->u.binary.value_b);

		if (!b){
			value_release (a);
			return NULL;
		}

		if (!VALUE_IS_NUMBER (a) || !VALUE_IS_NUMBER (b)){
			value_release (a);
			value_release (b);
			error_message_set (s->error,  _("Type mismatch"));
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
				       
					return function_error (s, gnumeric_err_DIV0);
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

					return function_error (s, gnumeric_err_DIV0);
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
		return (Value *)res;

	case OPER_CONCAT: {
		char *sa, *sb, *tmp;

		a = (Value *)eval_expr (s, tree->u.binary.value_a);
		if (!a)
			return NULL;
		b = (Value *)eval_expr (s, tree->u.binary.value_b);
		if (!b){
			value_release (a);
			return NULL;
		}

		sa = value_get_as_string (a);
		sb = value_get_as_string (b);
		tmp = g_strconcat (sa, sb, NULL);
		res = value_new_string (tmp);

		g_free (sa);
		g_free (sb);
		g_free (tmp);

		value_release (a);
		value_release (b);
		return (Value *)res;
	}

	case OPER_FUNCALL:
		return eval_funcall (s, tree);

	case OPER_VAR: {
		Sheet *cell_sheet;
		CellRef *ref;
		Cell *cell;
		int col, row;

		if (s->pos.sheet == NULL) {
			/* Only the test program requests this */
			return value_new_float (3.14);
		}

		ref = &tree->u.ref;
		cell_get_abs_col_row (ref, s->pos.eval_col, s->pos.eval_row, &col, &row);

		cell_sheet = ref->sheet ? ref->sheet : s->pos.sheet;

		cell = sheet_cell_get (cell_sheet, col, row);

		if (cell){
			if (cell->generation != s->pos.sheet->workbook->generation){
				cell->generation = s->pos.sheet->workbook->generation;
				if (cell->parsed_node && (cell->flags & CELL_QUEUED_FOR_RECALC))
					cell_eval (cell);
			}

			if (cell->value)
				return (Value *)value_duplicate (cell->value);
			else {
				if (cell->text)
					error_message_set (s->error, cell->text->str);
				else
					error_message_set (s->error, _("Reference to newborn cell"));
				return NULL;
			}
		}

		return (Value *)value_new_int (0);
	}

	case OPER_CONSTANT:
		return (Value *)value_duplicate (tree->u.constant);

	case OPER_NEG:
		a = (Value *)eval_expr (s, tree->u.value);
		if (!a)
			return NULL;
		if (!VALUE_IS_NUMBER (a)){
			error_message_set (s->error, _("Type mismatch"));
			value_release (a);
			return NULL;
		}
		if (a->type == VALUE_INTEGER)
			res = value_new_int (-a->v.v_int);
		else
			res = value_new_float (-a->v.v_float);
		value_release (a);
		return (Value *)res;
	}

	error_message_set (s->error, _("Unknown evaluation error"));
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

/*
 * Converts a parsed tree into its string representation
 * assuming that we are evaluating at col, row
 *
 * This routine is pretty simple: it walks the ExprTree and
 * creates a string representation.
 *
 * FIXME: strings containing quotes will come out wrong.
 */
static char *
do_expr_decode_tree (ExprTree *tree, const EvalPosition *fp, int paren_level)
{
	static struct {
		const char *name;
		int prec;	              /* Precedences -- should match parser.y  */
		int assoc_left, assoc_right;  /* 0: no, 1: yes.  */
	} operations [] = {
		{ "=",  1, 1, 0 },
		{ ">",  1, 1, 0 },
		{ "<",  1, 1, 0 },
		{ ">=", 1, 1, 0 },
		{ "<=", 1, 1, 0 },
		{ "<>", 1, 1, 0 },
		{ "+",  3, 1, 0 },
		{ "-",  3, 1, 0 },
		{ "*",  4, 1, 0 },
		{ "/",  4, 1, 0 },
		{ "^",  6, 0, 1 },
		{ "&",  2, 1, 0 },
		{ NULL, 0, 0, 0 },
		{ NULL, 0, 0, 0 },
		{ NULL, 0, 0, 0 },
		{ "-",  5, 0, 0 }
	};
	int op;
	Sheet *sheet = fp->sheet;
	int col      = fp->eval_col;
	int row      = fp->eval_row;

	op = tree->oper;

	switch (op){
	case OPER_ANY_BINARY: {
		char *a, *b, *res;
		char const *opname;
		int prec;

		prec = operations[op].prec;

		a = do_expr_decode_tree (tree->u.binary.value_a, fp, prec - operations[op].assoc_left);
		b = do_expr_decode_tree (tree->u.binary.value_b, fp, prec - operations[op].assoc_right);
		opname = operations[op].name;

		if (prec <= paren_level)
			res = g_strconcat ("(", a, opname, b, ")", NULL);
		else
			res = g_strconcat (a, opname, b, NULL);

		g_free (a);
		g_free (b);
		return res;
	}

	case OPER_ANY_UNARY: {
		char *res, *a;
		char const *opname;
		int prec;

		prec = operations[op].prec;
		a = do_expr_decode_tree (tree->u.value, fp, operations[op].prec);
		opname = operations[op].name;

		if (prec <= paren_level)
			res = g_strconcat ("(", opname, a, ")", NULL);
		else
			res = g_strconcat (opname, a, NULL);
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

				args [i] = do_expr_decode_tree (t, fp, 0);
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

			res = g_strconcat (fd->name, "(", sum, ")", NULL);
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

		switch (v->type) {
		case VALUE_CELLRANGE: {
			char *a, *b, *res;

			a = cellref_name (&v->v.cell_range.cell_a, sheet, col, row);
			b = cellref_name (&v->v.cell_range.cell_b, sheet, col, row);

			res = g_strconcat (a, ":", b, NULL);

			g_free (a);
			g_free (b);

			return res;
		}

		case VALUE_STRING:
			/* FIXME: handle quotes in string.  */
			return g_strconcat ("\"", v->v.str->str, "\"", NULL);

		case VALUE_INTEGER:
		case VALUE_FLOAT: {
			char *res, *vstr;
			vstr = value_get_as_string (v);

			/* If the number has a sign, pretend that it is the
			   result of OPER_NEG.  It is not clear how we would
			   currently get negative numbers here, but some
			   loader might do it.  */
			if ((vstr[0] == '-' || vstr[0] == '+') &&
			    operations[OPER_NEG].prec <= paren_level) {
				res = g_strconcat ("(", vstr, ")", NULL);
				g_free (vstr);
			} else
				res = vstr;
			return res;
		}

		case VALUE_ARRAY:
			return value_get_as_string (v);

		default:
			g_assert_not_reached ();
			return NULL;
		}
	}
	}

	g_warning ("ExprTree: This should not happen\n");
	return g_strdup ("0");
}

char *
expr_decode_tree (ExprTree *tree, const EvalPosition *fp)
{
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (fp->sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (fp->sheet), NULL);

	return do_expr_decode_tree (tree, fp, 0);
}


static ExprTree *
build_error_string (const char *txt)
{
	ExprTree *val, *call;
	Symbol *func;

	val = g_new (ExprTree, 1);
	val->oper = OPER_CONSTANT;
	val->ref_count = 1;
	val->u.constant = value_new_string (txt);

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
			if (a == NULL)
				expr_tree_ref ((a = src->u.binary.value_a));
			if (b == NULL)
				expr_tree_ref ((b = src->u.binary.value_b));

			return expr_tree_new_binary (a, src->oper, b);
		}
	}

	case OPER_ANY_UNARY: {
		ExprTree *a =
			do_expr_tree_invalidate_references (src->u.value, info);
		if (a == NULL)
			return NULL;
		else
			return expr_tree_new_unary (src->oper, a);
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
			GList *m;

			for (l = src->u.function.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					expr_tree_ref ((m->data = l->data));
			}

			symbol_ref (src->u.function.symbol);
			return expr_tree_new_funcall (src->u.function.symbol, new_args);
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
			return expr_tree_new_error (_("#Reference to deleted cell!"));
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
				return expr_tree_new_error (_("#Reference to deleted range!"));
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
expr_tree_invalidate_references (ExprTree *src, EvalPosition *src_fp,
				 const EvalPosition *fp,
				 int colcount, int rowcount)
{
	struct expr_tree_frob_references info;
	ExprTree *dst;
	Sheet *src_sheet = src_fp->sheet;
	int    src_col   = src_fp->eval_col;
	int    src_row   = src_fp->eval_row;
	Sheet *sheet     = fp->sheet;
	int    col       = fp->eval_col;
	int    row       = fp->eval_row;

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

	if (0) {
		char *str;
		str = expr_decode_tree (src, src_fp);
		printf ("Invalidate: %s: [%s]\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	dst = do_expr_tree_invalidate_references (src, &info);

	if (0) {
		char *str;
		str = dst ? expr_decode_tree (dst, src_fp) : g_strdup ("*");
		printf ("Invalidate: %s: [%s]\n\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	return dst;
}


static gboolean
fixup_calc_new_cellref (CellRef *crp, const struct expr_tree_frob_references *info)
{
	CellRef oldcr = *crp;
	int src_col = info->src_col;
	int src_row = info->src_row;
	cell_ref_make_absolute (crp, src_col, src_row);

	if (info->deleting) {
		if (info->colcount) {
			if (crp->col >= info->col)
				crp->col = MAX (crp->col - info->colcount, info->col);
			if (src_col >= info->col)
				src_col = MAX (src_col - info->colcount, info->col);
		} else {
			if (crp->row >= info->row)
				crp->row = MAX (crp->row - info->rowcount, info->row);
			if (src_row >= info->row)
				src_row = MAX (src_row - info->rowcount, info->row);
		}
	} else {
		if (info->colcount) {
			if (crp->col >= info->col)
				crp->col += info->colcount;
			if (src_col >= info->col)
				src_col += info->colcount;
		} else {
			if (crp->row >= info->row)
				crp->row += info->rowcount;
			if (src_row >= info->row)
				src_row += info->rowcount;
		}
	}

	cell_ref_restore_absolute (crp, &oldcr, src_col, src_row);
	return (crp->col != oldcr.col || crp->row != oldcr.row);
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
			if (a == NULL)
				expr_tree_ref ((a = src->u.binary.value_a));
			if (b == NULL)
				expr_tree_ref ((b = src->u.binary.value_b));

			return expr_tree_new_binary (a, src->oper, b);
		}
	}

	case OPER_ANY_UNARY: {
		ExprTree *a =
			do_expr_tree_fixup_references (src->u.value, info);
		if (a == NULL)
			return NULL;
		else
			return expr_tree_new_unary (src->oper, a);
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
			GList *m;

			for (l = src->u.function.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					expr_tree_ref ((m->data = l->data));
			}

			symbol_ref (src->u.function.symbol);
			return expr_tree_new_funcall (src->u.function.symbol, new_args);
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

		if (!fixup_calc_new_cellref (&cr, info))
			return NULL;

		return expr_tree_new_var (&cr);
	}

	case OPER_CONSTANT: {
		Value *v = src->u.constant;

		switch (v->type) {
		case VALUE_STRING:
		case VALUE_INTEGER:
		case VALUE_FLOAT:
			return NULL;

		case VALUE_CELLRANGE: {
			gboolean a_changed, b_changed;

			CellRef ca = v->v.cell_range.cell_a; /* Copy a structure, not a pointer.  */
			CellRef cb = v->v.cell_range.cell_b; /* Copy a structure, not a pointer.  */

			/* If the sheet is wrong, do nothing.  */
			if (ca.sheet != info->sheet)
				return NULL;

			a_changed = fixup_calc_new_cellref (&ca, info);
			b_changed = fixup_calc_new_cellref (&cb, info);

			if (!a_changed && !b_changed)
				return NULL;

			return expr_tree_new_constant (value_new_cellrange (&ca, &cb));
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
expr_tree_fixup_references (ExprTree *src, EvalPosition *src_fp,
			    const EvalPosition *fp,
			    int coldelta, int rowdelta)
{
	struct expr_tree_frob_references info;
	ExprTree *dst;
	Sheet *src_sheet = src_fp->sheet;
	int    src_col   = src_fp->eval_col;
	int    src_row   = src_fp->eval_row;
	Sheet *sheet     = fp->sheet;
	int    col       = fp->eval_col;
	int    row       = fp->eval_row;

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

	if (0) {
		char *str;
		str = expr_decode_tree (src, src_fp);
		printf ("Fixup: %s: [%s]\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	dst = do_expr_tree_fixup_references (src, &info);

	if (0) {
		char *str;
		str = dst ? expr_decode_tree (dst, src_fp) : g_strdup ("*");
		printf ("Fixup: %s: [%s]\n\n", cell_name (src_col, src_row), str);
		g_free (str);
	}

	return dst;
}
