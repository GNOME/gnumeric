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
#include <locale.h>
#include "gnumeric.h"
#include "expr.h"
#include "eval.h"
#include "format.h"
#include "func.h"
#include "utils.h"
#include "ranges.h"

static Value *eval_expr_real (FunctionEvalInfo *s, ExprTree const *tree);

EvalPosition *
eval_pos_init (EvalPosition *eval_pos, Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	eval_pos->sheet = sheet;
	eval_pos->eval_col = col;
	eval_pos->eval_row = row;

	return eval_pos;
}

ParsePosition *
parse_pos_init (ParsePosition *pp, Workbook *wb, int col, int row)
{
	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (pp != NULL, NULL);

	pp->wb  = wb;
	pp->col = col;
	pp->row = row;

	return pp;
}

EvalPosition *
eval_pos_cell (EvalPosition *eval_pos, Cell *cell)
{
	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cell->sheet), NULL);

	return eval_pos_init (
		eval_pos,
		cell->sheet,
		cell->col->pos,
		cell->row->pos);
}

ParsePosition *
parse_pos_cell (ParsePosition *pp, Cell *cell)
{
	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cell->sheet), NULL);
	g_return_val_if_fail (cell->sheet->workbook != NULL, NULL);

	return parse_pos_init (
		pp,
		cell->sheet->workbook,
		cell->col->pos,
		cell->row->pos);
}

FunctionEvalInfo *
func_eval_info_init (FunctionEvalInfo *eval_info, Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (eval_info != NULL, NULL);
       
	eval_pos_init (&eval_info->pos, sheet, col, row);
	eval_info->func_def = 0;

	return eval_info;
}

FunctionEvalInfo *
func_eval_info_cell (FunctionEvalInfo *eval_info, Cell *cell)
{
	g_return_val_if_fail (eval_info != NULL, NULL);
	g_return_val_if_fail (cell != NULL, NULL);

	return func_eval_info_init (
		eval_info, cell->sheet,
		cell->col->pos, cell->row->pos);
}

FunctionEvalInfo *
func_eval_info_pos (FunctionEvalInfo *eval_info, const EvalPosition *eval_pos)
{
	g_return_val_if_fail (eval_info != NULL, NULL);
	g_return_val_if_fail (eval_pos != NULL, NULL);

	return func_eval_info_init (
		eval_info,
		eval_pos->sheet,
		eval_pos->eval_col,
		eval_pos->eval_row);
}

ExprTree *
expr_tree_new_constant (Value *v)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (!ans)
		return NULL;
	
	ans->ref_count = 1;
	ans->oper = OPER_CONSTANT;
	ans->u.constant = v;

	return ans;
}

ExprTree *
expr_tree_new_unary  (Operation op, ExprTree *e)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	ans->oper = op;
	ans->u.value = e;

	return ans;
}


ExprTree *
expr_tree_new_binary (ExprTree *l, Operation op, ExprTree *r)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (!ans)
		return NULL;
	
	ans->ref_count = 1;
	ans->oper = op;
	ans->u.binary.value_a = l;
	ans->u.binary.value_b = r;

	return ans;
}

ExprTree *
expr_tree_new_funcall (Symbol *sym, GList *args)
{
	ExprTree *ans;
	g_return_val_if_fail (sym, NULL);

	ans = g_new (ExprTree, 1);
	if (!ans)
		return NULL;
	
	ans->ref_count = 1;
	ans->oper = OPER_FUNCALL;
	ans->u.function.symbol = sym;;
	ans->u.function.arg_list = args;

	return ans;
}
       
int
expr_tree_get_const_int (ExprTree const * const expr)
{
	g_return_val_if_fail (expr != NULL, 0);
	g_return_val_if_fail (expr->oper == OPER_CONSTANT, 0);
	g_return_val_if_fail (expr->u.constant, 0);
	g_return_val_if_fail (expr->u.constant->type == VALUE_INTEGER, 0);

	return expr->u.constant->v.v_int;
}

char const *
expr_tree_get_const_str (ExprTree const *const expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (expr->oper == OPER_CONSTANT, NULL);
	g_return_val_if_fail (expr->u.constant, NULL);
	g_return_val_if_fail (expr->u.constant->type == VALUE_STRING, NULL);

	return expr->u.constant->v.str->str;
}

ExprTree *
expr_parse_string (const char *expr, const ParsePosition *pp,
		   const char **desired_format, char **error_msg)
{
	ExprTree *tree;
	g_return_val_if_fail (expr != NULL, NULL);

	switch (gnumeric_expr_parser (expr, pp, desired_format, &tree)) {
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
expr_tree_new_name (const ExprName *name)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (!ans)
		return NULL;
	
	ans->ref_count = 1;
	ans->oper = OPER_NAME;
	ans->u.name = name;

	return ans;
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
	if (!ans)
		return NULL;
	
	ans->ref_count = 1;
	ans->oper = OPER_VAR;
	ans->u.ref = *cr;

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

ExprTree *
expr_tree_array_formula (int const x, int const y, int const rows, int const cols)
{
	ExprTree *ans;

	ans = g_new (ExprTree, 1);
	if (ans == NULL)
		return NULL;
	
	ans->ref_count = 1;
	ans->oper = OPER_ARRAY;
	ans->u.array.x = x;
	ans->u.array.y = y;
	ans->u.array.rows = rows;
	ans->u.array.cols = cols;
	ans->u.array.corner.func.value = NULL;
	ans->u.array.corner.func.expr = NULL;
	ans->u.array.corner.cell = NULL;
	return ans;
}


static ExprTree *
expr_tree_array_formula_corner (ExprTree const *expr)
{
	Cell * const corner = expr->u.array.corner.cell;

	g_return_val_if_fail (corner, NULL);
	g_return_val_if_fail (corner->parsed_node != NULL, NULL);

	/* Sanity check incase the corner gets removed for some reason */
	g_return_val_if_fail (corner->parsed_node != (void *)0xdeadbeef, NULL);
	g_return_val_if_fail (corner->parsed_node->oper == OPER_ARRAY, NULL);

	return corner->parsed_node;
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

	case OPER_NAME:
		break;

	case OPER_ANY_BINARY:
		do_expr_tree_unref (tree->u.binary.value_a);
		do_expr_tree_unref (tree->u.binary.value_b);
		break;

	case OPER_ANY_UNARY:
		do_expr_tree_unref (tree->u.value);
		break;
	case OPER_ARRAY:
		if (tree->u.array.x == 0 && tree->u.array.y == 0) {
			if (tree->u.array.corner.func.value)
				value_release (tree->u.array.corner.func.value);
			do_expr_tree_unref (tree->u.array.corner.func.expr);
		}
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
free_values (Value **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values[i])
			value_release (values [i]);
	g_free (values);
}

static Value *
eval_funcall (FunctionEvalInfo *s, ExprTree const *tree)
{
	const Symbol *sym;
	FunctionDefinition *fd;
	GList *l;
	int argc, arg;
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

	l = tree->u.function.arg_list;
	argc = g_list_length (l);

	if (sym->type != SYMBOL_FUNCTION)
		return value_new_error (&s->pos, _("Internal error"));

	fd = (FunctionDefinition *)sym->data;
	s->func_def = fd;

	if (fd->fn_type == FUNCTION_NODES) {
		/* Functions that deal with ExprNodes */		
		v = fd->fn.fn_nodes (s, l);
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

		if (argc > fn_argc_max || argc < fn_argc_min)
			return value_new_error (&s->pos, _("Invalid number of arguments"));

		values = g_new (Value *, fn_argc_max);

		for (arg = 0; l; l = l->next, arg++, arg_type++){
			ExprTree *t = (ExprTree *) l->data;
			gboolean type_mismatch = FALSE;
			Value *v;

			if (*arg_type=='|')
				arg_type++;

			if ((*arg_type != 'A' &&          /* This is so a cell reference */
			     *arg_type != 'r') ||         /* can be converted to a cell range */
			    !t || (t->oper != OPER_VAR)) { /* without being evaluated */
				if ((v = eval_expr_real (s, t)) == NULL)
					goto free_list;
			} else {
				g_assert (t->oper == OPER_VAR);
				v = value_new_cellrange (&t->u.ref,
							 &t->u.ref);
			}

			switch (*arg_type){
			case 'f':
			case 'b':
				/*
				 * Handle the implicit union of a single row or
				 * column with the eval position
				 */
				if (v->type == VALUE_CELLRANGE) {
					CellRef a = v->v.cell_range.cell_a;
					CellRef b = v->v.cell_range.cell_b;
					cell_ref_make_absolute (&a, eval_col,
								eval_row);
					cell_ref_make_absolute (&b, eval_col,
								eval_row);

					if (a.sheet !=  b.sheet)
						type_mismatch = TRUE;
					else if (a.row == b.row) {
						int const c = s->pos.eval_col;
						if (a.col <= c && c <= b.col)
							v = value_duplicate(value_area_get_x_y (&s->pos, v, c - a.col, 0));
						else
							type_mismatch = TRUE;
					} else if (a.col == b.col) {
						int const r = s->pos.eval_row;
						if (a.row <= r && r <= b.row)
							v = value_duplicate(value_area_get_x_y (&s->pos, v, 0, r - a.row));
						else
							type_mismatch = TRUE;
					} else
						type_mismatch = TRUE;
				} else if (v->type != VALUE_INTEGER &&
					 v->type != VALUE_FLOAT &&
					 v->type != VALUE_BOOLEAN)
					type_mismatch = TRUE;
				break;
			case 's':
				if (v->type != VALUE_STRING)
					type_mismatch = TRUE;
				break;
			case 'r':
				if (v->type != VALUE_CELLRANGE)
					type_mismatch = TRUE;
				else {
					cell_ref_make_absolute (&v->v.cell_range.cell_a, eval_col, eval_row);
					cell_ref_make_absolute (&v->v.cell_range.cell_b, eval_col, eval_row);
				}
				break;
			case 'a':
				if (v->type != VALUE_ARRAY)
					type_mismatch = TRUE;
				break;
			case 'A':
				if (v->type != VALUE_ARRAY &&
				    v->type != VALUE_CELLRANGE)
					type_mismatch = TRUE;

				if (v->type == VALUE_CELLRANGE) {
					cell_ref_make_absolute (&v->v.cell_range.cell_a, eval_col, eval_row);
					cell_ref_make_absolute (&v->v.cell_range.cell_b, eval_col, eval_row);
				}
				break;
			}
			values [arg] = v;
			if (type_mismatch){
				free_values (values, arg + 1);
				return value_new_error (&s->pos,
							gnumeric_err_VALUE);
			}
		}
		while (arg < fn_argc_max)
			values [arg++] = NULL;
		v = fd->fn.fn_args (s, values);

	free_list:
		free_values (values, arg);
	}
	return v;
}

typedef enum {
	IS_EQUAL,
	IS_LESS,
	IS_GREATER,
	TYPE_MISMATCH
} compare_t;

static compare_t
compare_bool_bool (Value const * const va, Value const * const vb)
{
	gboolean err; /* Ignored */
	gboolean const a = value_get_as_bool (va, &err);
	gboolean const b = value_get_as_bool (vb, &err);
	if (a)
		return b ? IS_EQUAL : IS_GREATER;
	return b ? IS_LESS : IS_EQUAL;
}

static compare_t
compare_int_int (Value const * const va, Value const * const vb)
{
	int const a = value_get_as_int (va);
	int const b = value_get_as_int (vb);
	if (a == b)
		return IS_EQUAL;
	else if (a < b)
		return IS_LESS;
	else
		return IS_GREATER;
}

static compare_t
compare_float_float (Value const * const va, Value const * const vb)
{
	float_t const a = value_get_as_float (va);
	float_t const b = value_get_as_float (vb);
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
	ValueType ta, tb;

	/* Handle trivial and double NULL case */
	if (a == b)
		return IS_EQUAL;

	ta = value_is_empty_cell (a) ? VALUE_EMPTY : a->type;
	tb = value_is_empty_cell (b) ? VALUE_EMPTY : b->type;

	/* string > empty */
	if (ta == VALUE_STRING) {
		switch (tb) {
		/* Strings are > (empty, or number) */
		case VALUE_EMPTY : case VALUE_INTEGER : case VALUE_FLOAT :
			return IS_GREATER;

		/* Strings are < FALSE ?? */
		case VALUE_BOOLEAN :
			return IS_LESS;

		/* If both are strings compare as string */
		case VALUE_STRING :
		{
			int const t = strcasecmp (a->v.str->str, b->v.str->str);
			if (t == 0)
				return IS_EQUAL;
			else if (t > 0)
				return IS_GREATER;
			else
				return IS_LESS;
		}
		default :
			/*
			 * TODO : Add implicit intersection here if not in
			 * array mode
			 * */
			return TYPE_MISMATCH;
		}
	} else if (tb == VALUE_STRING) {
		switch (ta) {
		/* (empty, or number) < String */
		case VALUE_EMPTY : case VALUE_INTEGER : case VALUE_FLOAT :
			return IS_LESS;

		/* Strings are < FALSE ?? */
		case VALUE_BOOLEAN :
			return IS_GREATER;

		default :
			/*
			 * TODO : Add implicit intersection here if not in
			 * array mode
			 * */
			return TYPE_MISMATCH;
		}
	}

	/* Booleans > all numbers (Why did excel do this ??) */
	if (ta == VALUE_BOOLEAN && (tb == VALUE_INTEGER || tb == VALUE_FLOAT))
		return IS_GREATER;
	if (tb == VALUE_BOOLEAN && (ta == VALUE_INTEGER || ta == VALUE_FLOAT))
		return IS_LESS;

	switch ((ta > tb) ? ta : tb) {
	case VALUE_EMPTY:	/* Empty Empty compare */
		return IS_EQUAL;

	case VALUE_BOOLEAN:
		return compare_bool_bool (a, b);

	case VALUE_INTEGER:
		return compare_int_int (a, b);

	case VALUE_FLOAT:
		return compare_float_float (a, b);
	default:
		return TYPE_MISMATCH;
	}
}

/*
 * Utility routine to ensure that all elements of a range are recalced as
 * necessary.
 */
static void
eval_range (FunctionEvalInfo *s, Value *v)
{
	int start_col, start_row, end_col, end_row;
	CellRef * a = &v->v.cell_range.cell_a;
	CellRef * b = &v->v.cell_range.cell_b;
	Sheet * sheet = a->sheet ? a->sheet : s->pos.sheet;
	Cell * cell;
	int r, c;
	int const gen = s->pos.sheet->workbook->generation;

	cell_get_abs_col_row (a,
			      s->pos.eval_col, s->pos.eval_row,
			      &start_col, &start_row);
	cell_get_abs_col_row (b,
			      s->pos.eval_col, s->pos.eval_row,
			      &end_col, &end_row);

	if (a->sheet != b->sheet) {
		g_warning ("3D references not-fully supported.\n"
			   "Recalc may be incorrect");
		return;
	}

	for (r = start_row; r <= end_row; ++r)
		for (c = start_col; c <= end_col; ++c) {
			if ((cell = sheet_cell_get (sheet, c, r)) == NULL)
				continue;
			if (cell->generation != gen) {
				cell->generation = gen;
				if (cell->parsed_node &&
				    (cell->flags & CELL_QUEUED_FOR_RECALC))
					cell_eval (cell);
			}

		}
}

static Value *
eval_expr_real (FunctionEvalInfo *s, ExprTree const *tree)
{
	Value *res = NULL, *a = NULL, *b = NULL;
	
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (s != NULL, NULL);

	/* FIXME FIXME : We need to rework support for
	 * ranges and builtin operators to support the
	 * implicit intersection rule AND figure out
	 * how to turn it off when evaluating as an array
	 */
	switch (tree->oper){
	case OPER_EQUAL:
	case OPER_NOT_EQUAL:
	case OPER_GT:
	case OPER_GTE:
	case OPER_LT:
	case OPER_LTE: {
		int comp;

		a = eval_expr_real (s, tree->u.binary.value_a);
		if (a != NULL && a->type == VALUE_ERROR)
			return a;

		b = eval_expr_real (s, tree->u.binary.value_b);
		if (b != NULL && b->type == VALUE_ERROR) {
			if (a != NULL)
				value_release (a);
			return b;
		}

		comp = compare (a, b);

		if (a != NULL)
			value_release (a);
		if (b != NULL)
			value_release (b);

		if (comp == TYPE_MISMATCH){
			/* TODO TODO TODO : Make error more informative
			 *    regarding what is comparing to what
			 */
			/* For equality comparisons even errors are ok */
			if (tree->oper == OPER_EQUAL)
				return value_new_bool (FALSE);
			if (tree->oper == OPER_NOT_EQUAL)
				return value_new_bool (TRUE);

			return value_new_error (&s->pos, gnumeric_err_VALUE);
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

		case OPER_NOT_EQUAL:
			res = value_new_bool (comp != IS_EQUAL);
			break;

		case OPER_LTE:
			res = value_new_bool (comp != IS_GREATER);
			break;

		case OPER_GTE:
			res = value_new_bool (comp != IS_LESS);
			break;

		default:
			g_assert_not_reached ();
			res = value_new_error (&s->pos,
						_("Internal type error"));
		}
		return res;
	}

	case OPER_ADD:
	case OPER_SUB:
	case OPER_MULT:
	case OPER_DIV:
	case OPER_EXP:
	        /* Garantees that a != NULL */
		a = eval_expr (s, tree->u.binary.value_a);

		/*
		 * Priority
		 * 1) Error from A
		 * 2) #!VALUE error if A is not a number
		 * 3) Error from B
		 * 4) #!VALUE error if B is not a number
		 * 5) result of operation, or error specific to the operation
		 */

		/* 1) Error from A */
		if (a->type == VALUE_ERROR)
			return a;

		/* 2) #!VALUE error if A is not a number */
		if (!VALUE_IS_NUMBER (a)) {
			value_release (a);
			return value_new_error (&s->pos, gnumeric_err_VALUE);
		}

	        /* Garantees that b != NULL */
		b = eval_expr (s, tree->u.binary.value_b);

		/* 3) Error from B */
		if (b->type == VALUE_ERROR) {
			value_release (a);
			return b;
		}

		/* 4) #!VALUE error if B is not a number */
		if (!VALUE_IS_NUMBER (b)) {
			value_release (a);
			value_release (b);
			return value_new_error (&s->pos, gnumeric_err_VALUE);
		}

		if (a->type != VALUE_FLOAT && b->type != VALUE_FLOAT){
			int ia = value_get_as_int (a);
			int ib = value_get_as_int (b);
			double dres;
			int ires;

			value_release (a);
			value_release (b);

			/* FIXME: we could use simple (cheap) heuristics to
			   catch most cases where overflow will not happen.  */
			switch (tree->oper){
			case OPER_ADD:
				dres = (double)ia + (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((float_t) dres);

			case OPER_SUB:
				dres = (double)ia - (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((float_t) dres);

			case OPER_MULT:
				dres = (double)ia * (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((float_t) dres);

			case OPER_DIV:
				if (ib == 0)
					return value_new_error (&s->pos, gnumeric_err_DIV0);
				dres = (double)ia / (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((float_t) dres);

			case OPER_EXP:
				if (ia == 0 && ib <= 0)
					return value_new_error (&s->pos, gnumeric_err_NUM);
				dres = pow ((double)ia, (double)ib);
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((float_t) dres);

			default:
				abort ();
			}
		} else {
			float_t const va = value_get_as_float(a);
			float_t const vb = value_get_as_float(b);
			value_release (a);
			value_release (b);

			switch (tree->oper){
			case OPER_ADD:
				return value_new_float(va + vb);

			case OPER_SUB:
				return value_new_float(va - vb);

			case OPER_MULT:
				return value_new_float(va * vb);

			case OPER_DIV:
				return (vb == 0.0)
				    ? value_new_error (&s->pos,
						       gnumeric_err_DIV0)
				    : value_new_float(va / vb);

			case OPER_EXP:
				if (va == 0 && vb <= 0)
					return value_new_error (&s->pos, gnumeric_err_NUM);
				return value_new_float(pow(va, vb));

			default:
				break;
			}
		}
		return value_new_error (&s->pos, _("Unknown operator"));

	case OPER_PERCENT:
	case OPER_NEG:
	        /* Garantees that a != NULL */
		a = eval_expr (s, tree->u.value);
		if (a->type == VALUE_ERROR)
			return a;
		if (!VALUE_IS_NUMBER (a)){
			value_release (a);
			return value_new_error (&s->pos, gnumeric_err_VALUE);
		}
		if (tree->oper == OPER_NEG) {
			if (a->type == VALUE_INTEGER)
				res = value_new_int (-a->v.v_int);
			else if (a->type == VALUE_FLOAT)
				res = value_new_float (-a->v.v_float);
			else
				res = value_new_bool (!a->v.v_float);
		} else
			res = value_new_float (value_get_as_float (a) * .01);
		value_release (a);
		return res;

	case OPER_CONCAT: {
		char *sa, *sb, *tmp;

		a = eval_expr_real (s, tree->u.binary.value_a);
		if (a != NULL && a->type == VALUE_ERROR)
			return a;
		b = eval_expr_real (s, tree->u.binary.value_b);
		if (b != NULL && b->type == VALUE_ERROR) {
			if (a != NULL)
				value_release (a);
			return b;
		}

		sa = value_get_as_string (a);
		sb = value_get_as_string (b);
		tmp = g_strconcat (sa, sb, NULL);
		res = value_new_string (tmp);

		g_free (sa);
		g_free (sb);
		g_free (tmp);

		if (a != NULL)
		value_release (a);
		if (b != NULL)
		value_release (b);
		return res;
	}

	case OPER_FUNCALL:
		return eval_funcall (s, tree);

	case OPER_NAME:
		return eval_expr_name (s, tree->u.name);

	case OPER_VAR: {
		Sheet *cell_sheet;
		CellRef const *ref;
		Cell *cell;
		int col, row;

		if (s->pos.sheet == NULL) {
			/* Only the test program requests this */
			return value_new_float (3.14);
		}

		ref = &tree->u.ref;
		cell_get_abs_col_row (ref, s->pos.eval_col, s->pos.eval_row, &col, &row);

		cell_sheet = eval_sheet (ref->sheet, s->pos.sheet);
		cell = sheet_cell_get (cell_sheet, col, row);
		if (cell == NULL)
			return NULL;

		if (cell->generation != s->pos.sheet->workbook->generation){
			cell->generation = s->pos.sheet->workbook->generation;
			if (cell->parsed_node && (cell->flags & CELL_QUEUED_FOR_RECALC))
				cell_eval (cell);
		}

		return value_duplicate (cell->value);
	}

	case OPER_CONSTANT:
		res = tree->u.constant;
		if (res->type == VALUE_CELLRANGE)
			eval_range (s, res);
		return value_duplicate (res);

	case OPER_ARRAY:
	{
		/* The upper left corner manages the recalc of the expr */
		int const x = tree->u.array.x;
		int const y = tree->u.array.y;
		if (x == 0 && y == 0){
			/* Release old value if necessary */
			a = tree->u.array.corner.func.value;
			if (a != NULL)
				value_release (a);

			/* Store real result (cast away const)*/
			a = eval_expr_real (s, tree->u.array.corner.func.expr);
			*((Value **)&(tree->u.array.corner.func.value)) = a;
		} else {
			ExprTree const * const array =
			    expr_tree_array_formula_corner (tree);
			if (array)
				a = array->u.array.corner.func.value;
			else
				a = NULL;
		}

		if (a != NULL &&
		    (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)) {
			int const num_x = value_area_get_width (&s->pos, a);
			int const num_y = value_area_get_height (&s->pos, a);

			if (x < num_x && y < num_y){
				/* Evaluate relative to the upper left corner */
				EvalPosition tmp_ep = s->pos;
				tmp_ep.eval_col -= x;
				tmp_ep.eval_row -= y;
				a = (Value *)value_area_get_x_y (
					&tmp_ep, a, x, y);
			} else
				return value_new_error (&s->pos, gnumeric_err_NA);
		} else if (x >= 1 || y >= 1)
			return value_new_error (&s->pos, gnumeric_err_NA);

		if (a == NULL)
			return NULL;
		return value_duplicate (a);
	}
	}

	return value_new_error (&s->pos, _("Unknown evaluation error"));
}

Value *
eval_expr (FunctionEvalInfo *s, ExprTree const *tree)
{
	Value * res = eval_expr_real (s, tree);
	if (res == NULL)
		return value_new_int (0);

	if (res->type == VALUE_EMPTY) {
		value_release (res);
		return value_new_int (0);
	}
	return res;
}

int
cell_ref_get_abs_col(CellRef const *ref, EvalPosition const *pos)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (pos != NULL, 0);

	if (ref->col_relative)
		return pos->eval_col + ref->col;
	return ref->col;

}

int
cell_ref_get_abs_row(CellRef const *ref, EvalPosition const *pos)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (pos != NULL, 0);

	if (ref->row_relative)
		return pos->eval_row + ref->row;
	return ref->row;
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
do_expr_decode_tree (ExprTree *tree, const ParsePosition *pp,
		     int paren_level)
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
		{ NULL, 0, 0, 0 }, /* Funcall  */
		{ NULL, 0, 0, 0 }, /* Name     */
		{ NULL, 0, 0, 0 }, /* Constant */
		{ NULL, 0, 0, 0 }, /* Var      */
		{ "-",  5, 0, 0 },
		{ "%",  5, 0, 0 },
		{ NULL, 0, 0, 0 }  /* Array    */
	};
	int op;

	op = tree->oper;

	switch (op){
	case OPER_ANY_BINARY: {
		char *a, *b, *res;
		char const *opname;
		int prec;

		prec = operations[op].prec;

		a = do_expr_decode_tree (tree->u.binary.value_a, pp, prec - operations[op].assoc_left);
		b = do_expr_decode_tree (tree->u.binary.value_b, pp, prec - operations[op].assoc_right);
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
		a = do_expr_decode_tree (tree->u.value, pp, operations[op].prec);
		opname = operations[op].name;

		if (tree->oper == OPER_NEG) {
			if (prec <= paren_level)
				res = g_strconcat ("(", opname, a, ")", NULL);
			else
				res = g_strconcat (opname, a, NULL);
		} else {
			if (prec <= paren_level)
				res = g_strconcat ("(", a, opname, ")", NULL);
			else
				res = g_strconcat (a, opname, NULL);
		}
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

				args [i] = do_expr_decode_tree (t, pp, 0);
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

	case OPER_NAME:
		return g_strdup (tree->u.name->name->str);

	case OPER_VAR: {
		CellRef *cell_ref = &tree->u.ref;
		return cellref_name (cell_ref, pp->col, pp->row);
	}

	case OPER_CONSTANT: {
		Value *v = tree->u.constant;

		switch (v->type) {
		case VALUE_CELLRANGE: {
			char *a, *b, *res;

			a = cellref_name (&v->v.cell_range.cell_a, pp->col, pp->row);
			b = cellref_name (&v->v.cell_range.cell_b, pp->col, pp->row);

			res = g_strconcat (a, ":", b, NULL);

			g_free (a);
			g_free (b);

			return res;
		}

		case VALUE_STRING:
			/* FIXME: handle quotes in string.  */
			return g_strconcat ("\"", v->v.str->str, "\"", NULL);

		case VALUE_EMPTY:
			return g_strdup ("");

		case VALUE_ERROR:
			return g_strdup (v->v.error.mesg->str);

		case VALUE_BOOLEAN:
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
	case OPER_ARRAY:
	{
		int const x = tree->u.array.x;
		int const y = tree->u.array.y;
		char *res;
		if (x != 0 || y != 0) {
			ExprTree *array = expr_tree_array_formula_corner (tree);
			if (array) {
				ParsePosition tmp_pos;
				tmp_pos.wb  = pp->wb;
				tmp_pos.col = pp->col - x;
				tmp_pos.row = pp->row - y;
				res = do_expr_decode_tree (
					array->u.array.corner.func.expr,
					&tmp_pos, 0);
			} else
				res = g_strdup ("<ERROR>");
		} else {
			res = do_expr_decode_tree (
			    tree->u.array.corner.func.expr, pp, 0);
		}

		/* Not exactly Excel format but more useful */
		{
			GString *str = g_string_new ("{");
			g_string_sprintfa (str, "%s}(%d,%d)[%d][%d]", res,
					   tree->u.array.rows,
					   tree->u.array.cols,
					   tree->u.array.y,
					   tree->u.array.x);
			g_free (res);
			res = str->str;
			g_string_free (str, FALSE);
			return res;
		}
        }
	}

	g_warning ("ExprTree: This should not happen\n");
	return g_strdup ("0");
}

char *
expr_decode_tree (ExprTree *tree, const ParsePosition *pp)
{
	g_return_val_if_fail (tree != NULL, NULL);

	return do_expr_decode_tree (tree, pp, 0);
}

enum CellRefRelocate {
	CELLREF_NO_RELOCATE = 0,
	CELLREF_RELOCATE = 1,
	CELLREF_RELOCATE_ERR = 2,
};

static enum CellRefRelocate
cellref_relocate (CellRef * const ref,
		  EvalPosition const * const pos,
		  struct expr_relocate_info const * const rinfo)
{
	/* For row or column refs
	 * Ref	From	To
	 *
	 * Abs	In	In 	: Positive (Sheet) (b)
	 * Abs	In	Out 	: Sheet
	 * Abs	Out	In 	: Negative, Sheet, Range (b)
	 * Abs	Out	Out 	: (a)
	 * Rel	In	In 	: (Sheet)
	 * Rel	In	Out 	: Negative, Sheet, Range (c)
	 * Rel	Out	In 	: Positive, Sheet, Range (b)
	 * Rel	Out	Out 	: (a)
	 *
	 * Positive : Add offset
	 * Negative : Subtract offset
	 * Sheet    : Check that ref sheet is correct
	 * Range    : Test for potential invalid refs
	 *
	 * An action in () is one which is done despite being useless
	 * to simplify the logic.
	 */
	int col = cell_ref_get_abs_col (ref, pos);
	int row = cell_ref_get_abs_row (ref, pos);

	Sheet * ref_sheet = (ref->sheet != NULL) ? ref->sheet : pos->sheet;
	gboolean const to_inside =
		(rinfo->target_sheet == ref_sheet) &&
		range_contains (&rinfo->origin, col, row);
	gboolean const from_inside =
		(rinfo->origin_sheet == pos->sheet) &&
		range_contains (&rinfo->origin, pos->eval_col, pos->eval_row);

	/* Case (a) */
	if (!from_inside && !to_inside)
		return CELLREF_NO_RELOCATE;

	if (from_inside != to_inside) {
		if (to_inside) {
			if (pos->sheet == rinfo->target_sheet)
				ref_sheet = NULL;
		} else {
			if (ref_sheet == rinfo->target_sheet)
				ref_sheet = NULL;
		}
	} else
		ref_sheet = ref->sheet;

	if (to_inside) {
		/* Case (b) */
		if (ref->col_relative != from_inside)
			col += rinfo->col_offset;
		else if (!ref->col_relative && !from_inside)
			col -= rinfo->col_offset;

		if (ref->row_relative != from_inside)
			row += rinfo->row_offset;
		else if (!ref->row_relative && !from_inside)
			row -= rinfo->row_offset;
	} else {
		/* Case (c) */
		if (ref->col_relative && from_inside)
			col -= rinfo->col_offset;
		if (ref->row_relative && from_inside)
			row -= rinfo->row_offset;
	}

	if (col < 0 || col >= SHEET_MAX_COLS ||
	    row < 0 || row >= SHEET_MAX_COLS)
	    return CELLREF_RELOCATE_ERR;

	if (ref->col_relative)
		col -= pos->eval_col;
	if (ref->row_relative)
		row -= pos->eval_row;

	if (ref->sheet != ref_sheet || ref->col != col || ref->row != row) {
		ref->sheet = ref_sheet;
		ref->col = col;
		ref->row = row;
		return CELLREF_RELOCATE;
	}
	return CELLREF_NO_RELOCATE;
}

/*
 * expr_relocate :
 * @expr  : Expression to fixup
 * @pos   : Location of the cell containing @expr.
 * @rinfo : State information required to adjust the references.
 *
 * Find any references to the specified area and adjust them by the supplied
 * deltas.  Check for out of bounds conditions.  Return NULL if no change is
 * required.
 *
 * If the expression is within the range to be moved, its relative references
 * to cells outside the range are adjusted to reference the same cell after
 * the move.
 */
ExprTree *
expr_relocate (ExprTree const *expr,
	       EvalPosition const *pos,
	       struct expr_relocate_info const *rinfo)
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (expr->oper) {
	case OPER_ANY_BINARY: {
		ExprTree *a = expr_relocate (expr->u.binary.value_a, pos, rinfo);
		ExprTree *b = expr_relocate (expr->u.binary.value_b, pos, rinfo);

		if (a == NULL && b == NULL)
			return NULL;

		if (a == NULL)
			expr_tree_ref ((a = expr->u.binary.value_a));
		else if (b == NULL)
			expr_tree_ref ((b = expr->u.binary.value_b));

		return expr_tree_new_binary (a, expr->oper, b);
	}

	case OPER_ANY_UNARY: {
		ExprTree *a = expr_relocate (expr->u.value, pos, rinfo);
		if (a == NULL)
			return NULL;
		return expr_tree_new_unary (expr->oper, a);
	}

	case OPER_FUNCALL: {
		gboolean relocate = FALSE;
		GList *new_args = NULL;
		GList *l;

		for (l = expr->u.function.arg_list; l; l = l->next) {
			ExprTree *arg = expr_relocate (l->data, pos, rinfo);
			new_args = g_list_append (new_args, arg);
			if (arg != NULL)
				relocate = TRUE;
		}

		if (relocate) {
			GList *m;

			for (l = expr->u.function.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					expr_tree_ref ((m->data = l->data));
			}

			symbol_ref (expr->u.function.symbol);
			return expr_tree_new_funcall (expr->u.function.symbol, new_args);
		}
		g_list_free (new_args);
		return NULL;
	}

	case OPER_NAME:
		return NULL;

	case OPER_VAR:
	{
		CellRef res = expr->u.ref; /* Copy */
		switch (cellref_relocate (&res, pos, rinfo)) {
		case CELLREF_NO_RELOCATE :
			return NULL;

		case CELLREF_RELOCATE :
			return expr_tree_new_var (&res);

		case CELLREF_RELOCATE_ERR :
			return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
		}
	}

	case OPER_CONSTANT: {
		Value const * const v = expr->u.constant;
		if  (v->type == VALUE_CELLRANGE) {
			/* TODO : Figure out the logic of 1 ref error 1 ok */
			CellRef ref_a = v->v.cell_range.cell_a;
			CellRef ref_b = v->v.cell_range.cell_b;
			gboolean needs_reloc = FALSE;

			switch (cellref_relocate (&ref_a, pos, rinfo)) {
			case CELLREF_NO_RELOCATE :
				break;
			case CELLREF_RELOCATE :
				needs_reloc = TRUE;
				break;

			case CELLREF_RELOCATE_ERR :
				return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
			}
			switch (cellref_relocate (&ref_b, pos, rinfo)) {
			case CELLREF_NO_RELOCATE :
				break;
			case CELLREF_RELOCATE :
				needs_reloc = TRUE;
				break;

			case CELLREF_RELOCATE_ERR :
				return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
			}

			if (needs_reloc) {
				Value * res;
				/* Dont allow creation of 3D references */
				if (ref_a.sheet == ref_b.sheet)
					res = value_new_cellrange (&ref_a, &ref_b);
				else
					res = value_new_error (NULL, gnumeric_err_REF);
				return expr_tree_new_constant (res);
			}
			return NULL;
		}
	}

	case OPER_ARRAY: {
		ArrayRef const * a = &expr->u.array;
		if (a->x == 0 && a->y == 0) {
			ExprTree *func =
				expr_relocate (a->corner.func.expr, pos, rinfo);

			if (func != NULL) {
				ExprTree *res =
					expr_tree_array_formula (0, 0,
								 a->rows, a->cols);
				res->u.array.corner.func.value = NULL;
				res->u.array.corner.func.expr = func;
				return res;
			}
		}
		return NULL;
	}
	}

	g_assert_not_reached ();
	return NULL;
}

/* Debugging utility to print an expression */
void
expr_dump_tree (const ExprTree *tree)
{
	switch (tree->oper){
	case OPER_VAR: {
		const CellRef *cr;	
		cr = &tree->u.ref;
		printf ("Cell: %s%c%s%d\n",
			cr->col_relative ? "" : "$",
			cr->col + 'A',
			cr->row_relative ? "" : "$",
			cr->row + '1');
		return;
	}
		
	case OPER_CONSTANT:
		value_dump (tree->u.constant);
		return;

	case OPER_FUNCALL: {
		const Symbol *s;
		s = symbol_lookup (global_symbol_table, tree->u.function.symbol->str);
		printf ("Function call: %s\n", s->str);
		return;
	}

	case OPER_NAME:
		printf ("Name : %s\n", tree->u.name->name->str);
		return;

	case OPER_ANY_BINARY:
		expr_dump_tree (tree->u.binary.value_a);
		expr_dump_tree (tree->u.binary.value_b);
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
		return;
		
	case OPER_PERCENT:
	case OPER_NEG:
		expr_dump_tree (tree->u.value);
		if (tree->oper == OPER_PERCENT)
			printf ("PERCENT\n");
		else
			printf ("NEGATIVE\n");
		return;

	case OPER_ARRAY:
		printf ("ARRAY??\n");
		return;
	}
}
