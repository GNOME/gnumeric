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
#include "expr-name.h"
#include "eval.h"
#include "format.h"
#include "func.h"
#include "cell.h"
#include "parse-util.h"
#include "ranges.h"
#include "number-match.h"
#include "workbook.h"
#include "gutils.h"

/***************************************************************************/

ExprTree *
expr_tree_new_constant (Value *v)
{
	ExprConstant *ans;

	ans = g_new (ExprConstant, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = OPER_CONSTANT;
	ans->value = v;

	return (ExprTree *)ans;
}

ExprTree *
expr_tree_new_error (char const *txt)
{
	FunctionDefinition *func;
	GList *args = NULL;

	if (strcmp (txt, gnumeric_err_NA) != 0) {
		func = func_lookup_by_name ("ERROR", NULL);
		args = g_list_prepend (NULL,
				       expr_tree_new_constant (value_new_string (txt)));
	} else
		func = func_lookup_by_name ("NA", NULL);

	func_ref (func);
	return expr_tree_new_funcall (func, args);
}

ExprTree *
expr_tree_new_funcall (FunctionDefinition *func, GList *args)
{
	ExprFunction *ans;
	g_return_val_if_fail (func, NULL);

	ans = g_new (ExprFunction, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = OPER_FUNCALL;
	func_ref (func);
	ans->func = func;;
	ans->arg_list = args;

	return (ExprTree *)ans;
}

ExprTree *
expr_tree_new_unary  (Operation op, ExprTree *e)
{
	ExprUnary *ans;

	ans = g_new (ExprUnary, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = op;
	ans->value = e;

	return (ExprTree *)ans;
}


ExprTree *
expr_tree_new_binary (ExprTree *l, Operation op, ExprTree *r)
{
	ExprBinary *ans;

	ans = g_new (ExprBinary, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = op;
	ans->value_a = l;
	ans->value_b = r;

	return (ExprTree *)ans;
}

ExprTree *
expr_tree_new_name (NamedExpression const *name)
{
	ExprName *ans;

	ans = g_new (ExprName, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = OPER_NAME;
	ans->name = name;

	return (ExprTree *)ans;
}

ExprTree *
expr_tree_new_var (CellRef const *cr)
{
	ExprVar *ans;

	ans = g_new (ExprVar, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = OPER_VAR;
	ans->ref = *cr;

	return (ExprTree *)ans;
}

ExprTree *
expr_tree_new_array (int x, int y, int rows, int cols)
{
	ExprArray *ans;

	ans = g_new (ExprArray, 1);
	if (ans == NULL)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = OPER_ARRAY;
	ans->x = x;
	ans->y = y;
	ans->rows = rows;
	ans->cols = cols;
	ans->corner.value = NULL;
	ans->corner.expr = NULL;
	return (ExprTree *)ans;
}

int
expr_tree_get_const_int (ExprTree const *expr)
{
	g_return_val_if_fail (expr != NULL, 0);
	g_return_val_if_fail (expr->any.oper == OPER_CONSTANT, 0);
	g_return_val_if_fail (expr->constant.value, 0);
	g_return_val_if_fail (expr->constant.value->type == VALUE_INTEGER, 0);

	return expr->constant.value->v_int.val;
}

char const *
expr_tree_get_const_str (ExprTree const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (expr->any.oper == OPER_CONSTANT, NULL);
	g_return_val_if_fail (expr->constant.value, NULL);
	g_return_val_if_fail (expr->constant.value->type == VALUE_STRING, NULL);

	return expr->constant.value->v_str.val->str;
}

ExprTree *
expr_parse_string (char const *expr_text, ParsePos const *pp,
		   StyleFormat **desired_format, char **error_msg)
{
	ExprTree   *tree;
	ParseError  perr;

	g_return_val_if_fail (expr_text != NULL, NULL);

	tree = gnumeric_expr_parser (expr_text, pp, TRUE, FALSE, desired_format,
				     parse_error_init (&perr));

	/* TODO : use perr when we populate it */
	if (tree == NULL)
		*error_msg = perr.message;
	else
		*error_msg = NULL;
	parse_error_free (&perr);
	return tree;
}


static ExprTree *
expr_tree_array_formula_corner (ExprTree const *expr,
				Sheet const *sheet, CellPos const *pos)
{
	Cell *corner;

	g_return_val_if_fail (pos != NULL, NULL);

	corner = sheet_cell_get (sheet,
				 pos->col - expr->array.x,
				 pos->row - expr->array.y);

	/* Sanity check incase the corner gets removed for some reason */
	g_return_val_if_fail (corner != NULL, NULL);
	g_return_val_if_fail (cell_has_expr (corner), NULL);
	g_return_val_if_fail (corner->base.expression != (void *)0xdeadbeef, NULL);
	g_return_val_if_fail (corner->base.expression->any.oper == OPER_ARRAY, NULL);
	g_return_val_if_fail (corner->base.expression->array.x == 0, NULL);
	g_return_val_if_fail (corner->base.expression->array.y == 0, NULL);

	return corner->base.expression;
}

/*
 * expr_tree_ref:
 * Increments the ref_count for part of a tree
 */
void
expr_tree_ref (ExprTree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (tree->any.ref_count > 0);

	tree->any.ref_count++;
}

static void
do_expr_tree_unref (ExprTree *tree)
{
	if (--tree->any.ref_count > 0)
		return;

	switch (tree->any.oper){
	case OPER_VAR:
		break;

	case OPER_CONSTANT:
		value_release (tree->constant.value);
		break;

	case OPER_FUNCALL: {
		GList *l;

		for (l = tree->func.arg_list; l; l = l->next)
			do_expr_tree_unref (l->data);
		g_list_free (tree->func.arg_list);
		func_unref (tree->func.func);
		break;
	}

	case OPER_NAME:
		break;

	case OPER_ANY_BINARY:
		do_expr_tree_unref (tree->binary.value_a);
		do_expr_tree_unref (tree->binary.value_b);
		break;

	case OPER_ANY_UNARY:
		do_expr_tree_unref (tree->unary.value);
		break;
	case OPER_ARRAY:
		if (tree->array.x == 0 && tree->array.y == 0) {
			if (tree->array.corner.value)
				value_release (tree->array.corner.value);
			do_expr_tree_unref (tree->array.corner.expr);
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
	g_return_if_fail (tree->any.ref_count > 0);

	do_expr_tree_unref (tree);
}

/**
 * expr_tree_shared : Returns TRUE if the reference count
 *   for the supplied expression is > 1
 */
gboolean
expr_tree_shared (ExprTree const *tree)
{
	g_return_val_if_fail (tree != NULL, FALSE);

	return (tree->any.ref_count > 1);
}

static Value *
eval_funcall (EvalPos const *pos, ExprTree const *tree,
	      ExprEvalFlags flags)
{
	FunctionEvalInfo ei;
	FunctionDefinition *fd;
	GList *args;

	g_return_val_if_fail (pos != NULL, NULL);
	g_return_val_if_fail (tree != NULL, NULL);

	fd = tree->func.func;
	ei.func_def = fd;
	ei.pos = pos;
	args = tree->func.arg_list;

	/*if (flags & EVAL_PERMIT_NON_SCALAR)*/
	return function_call_with_list (&ei, args);
}

/**
 * expr_implicit_intersection :
 * @ei: EvalInfo containing valid fd!
 * @v: a VALUE_CELLRANGE
 *
 * Handle the implicit union of a single row or column with the eval position.
 *
 * NOTE : We do not need to know if this is expression is being evaluated as an
 * array or not because we can differentiate based on the required type for the
 * argument.
 *
 * Always release the value passed in.
 *
 * Return value:
 *     If the intersection succeeded return a duplicate of the value
 *     at the intersection point.  This value needs to be freed.
 **/
Value *
expr_implicit_intersection (EvalPos const *pos,
			    Value *v)
{
	Value *res = NULL;
	Range rng;
	Sheet *start_sheet, *end_sheet;

	/* handle inverted ranges */
	range_ref_normalize  (&rng, &start_sheet, &end_sheet, v, pos);

	if (start_sheet == end_sheet) {
		if (rng.start.row == rng.end.row) {
			int const c = pos->eval.col;
			if (rng.start.col <= c && c <= rng.end.col)
				res = value_duplicate (
					value_area_get_x_y (pos, v,
							    c - rng.start.col,
							    0));
		}

		if (rng.start.col == rng.end.col) {
			int const r = pos->eval.row;
			if (rng.start.row <= r && r <= rng.end.row)
				res = value_duplicate (
					value_area_get_x_y (pos, v, 0,
							    r - rng.start.row));
		}
	}
	value_release (v);
	return res;
}

/**
 * expr_array_intersection :
 * @v: a VALUE_ARRAY
 *
 * Returns the upper left corner of an array.
 *
 * Always release the value passed in.
 *
 * FIXME FIXME FIXME : This will need to be reworked
 * cellrange for array expressions
 *
 * Return value:
 *     duplicate of the value in the upper left of the array
 **/
Value *
expr_array_intersection (Value *a)
{
	Value *tmp = value_duplicate (a->v_array.vals [0][0]);
	value_release (a);
	return tmp;
}

/*
 * Utility routine to ensure that all elements of a range are recalced as
 * necessary.
 */
static void
eval_range (EvalPos const *pos, Value *v)
{
	int start_col, start_row, end_col, end_row;
	CellRef * a = &v->v_range.cell.a;
	CellRef * b = &v->v_range.cell.b;
	Sheet * sheet = a->sheet ? a->sheet : pos->sheet;
	Cell * cell;
	int r, c;
	int const gen = pos->sheet->workbook->generation;

	cell_get_abs_col_row (a, &pos->eval, &start_col, &start_row);
	cell_get_abs_col_row (b, &pos->eval, &end_col, &end_row);

	if (b->sheet && a->sheet != b->sheet) {
		g_warning ("3D references not-fully supported.\n"
			   "Recalc may be incorrect");
		return;
	}

	for (r = start_row; r <= end_row; ++r)
		for (c = start_col; c <= end_col; ++c) {
			if ((cell = sheet_cell_get (sheet, c, r)) == NULL)
				continue;
			if (cell->base.generation != gen)
				cell_eval (cell);
		}
}

static Value *
eval_expr_real (EvalPos const *pos, ExprTree const *tree,
		ExprEvalFlags flags)
{
	Value *res = NULL, *a = NULL, *b = NULL;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	switch (tree->any.oper){
	case OPER_EQUAL:
	case OPER_NOT_EQUAL:
	case OPER_GT:
	case OPER_GTE:
	case OPER_LT:
	case OPER_LTE: {
		ValueCompare comp;

		a = eval_expr_real (pos, tree->binary.value_a, flags);
		if (a != NULL) {
			if (a->type == VALUE_CELLRANGE) {
				a = expr_implicit_intersection (pos, a);
				if (a == NULL)
					return value_new_error (pos, gnumeric_err_VALUE);
			} else if (a->type == VALUE_ARRAY) {
				a = expr_array_intersection (a);
				if (a == NULL)
					return value_new_error (pos, gnumeric_err_VALUE);
			} else if (a->type == VALUE_ERROR)
				return a;
		}

		b = eval_expr_real (pos, tree->binary.value_b, flags);
		if (b != NULL) {
			Value *res = NULL;
			if (b->type == VALUE_CELLRANGE) {
				b = expr_implicit_intersection (pos, b);
				if (b == NULL)
					res = value_new_error (pos, gnumeric_err_VALUE);
			} else if (b->type == VALUE_ARRAY) {
				b = expr_array_intersection (b);
				if (b == NULL)
					return value_new_error (pos, gnumeric_err_VALUE);
			} else if (b->type == VALUE_ERROR)
				res = b;

			if (res != NULL) {
				if (a != NULL)
					value_release (a);
				return res;
			}
		}

		comp = value_compare (a, b, FALSE);

		if (a != NULL)
			value_release (a);
		if (b != NULL)
			value_release (b);

		if (comp == TYPE_MISMATCH) {
			/* TODO TODO TODO : Make error more informative
			 *    regarding what is comparing to what
			 */
			/* For equality comparisons even errors are ok */
			if (tree->any.oper == OPER_EQUAL)
				return value_new_bool (FALSE);
			if (tree->any.oper == OPER_NOT_EQUAL)
				return value_new_bool (TRUE);

			return value_new_error (pos, gnumeric_err_VALUE);
		}

		switch (tree->any.oper) {
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
			res = value_new_error (pos,
						_("Internal type error"));
		}
		return res;
	}

	case OPER_ADD:
	case OPER_SUB:
	case OPER_MULT:
	case OPER_DIV:
	case OPER_EXP:
		/*
		 * Priority
		 * 1) Error from A
		 * 2) #!VALUE error if A is not a number
		 * 3) Error from B
		 * 4) #!VALUE error if B is not a number
		 * 5) result of operation, or error specific to the operation
		 */

	        /* Guarantees a != NULL */
		a = eval_expr (pos, tree->binary.value_a,
			       flags & (~EVAL_PERMIT_EMPTY));

		/* Handle implicit intersection */
		if (a->type == VALUE_CELLRANGE) {
			a = expr_implicit_intersection (pos, a);
			if (a == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
		} else if (a->type == VALUE_ARRAY) {
			a = expr_array_intersection (a);
			if (a == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
		}

		/* 1) Error from A */
		if (a->type == VALUE_ERROR)
			return value_new_error_err (pos, &a->v_err);

		/* 2) #!VALUE error if A is not a number */
		if (a->type == VALUE_STRING) {
			Value *tmp = format_match (a->v_str.val->str, NULL, NULL);

			value_release (a);
			if (tmp == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
			a = tmp;
		} else if (!VALUE_IS_NUMBER (a)) {
			value_release (a);
			return value_new_error (pos, gnumeric_err_VALUE);
		}

	        /* Guarantees that b != NULL */
		b = eval_expr (pos, tree->binary.value_b,
			       flags & (~EVAL_PERMIT_EMPTY));

		/* Handle implicit intersection */
		if (b->type == VALUE_CELLRANGE) {
			b = expr_implicit_intersection (pos, a);
			if (b == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
		} else if (b->type == VALUE_ARRAY) {
			b = expr_array_intersection (b);
			if (b == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
		}

		/* 3) Error from B */
		if (b->type == VALUE_ERROR) {
			value_release (a);
			return value_new_error_err (pos, &b->v_err);
		}

		/* 4) #!VALUE error if B is not a number */
		if (b->type == VALUE_STRING) {
			Value *tmp = format_match (b->v_str.val->str, NULL, NULL);

			value_release (b);
			if (tmp == NULL) {
				value_release (a);
				return value_new_error (pos, gnumeric_err_VALUE);
			}
			b = tmp;
		} else if (!VALUE_IS_NUMBER (b)) {
			value_release (a);
			value_release (b);
			return value_new_error (pos, gnumeric_err_VALUE);
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
			switch (tree->any.oper){
			case OPER_ADD:
				dres = (double)ia + (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((gnum_float) dres);

			case OPER_SUB:
				dres = (double)ia - (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((gnum_float) dres);

			case OPER_MULT:
				dres = (double)ia * (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((gnum_float) dres);

			case OPER_DIV:
				if (ib == 0)
					return value_new_error (pos, gnumeric_err_DIV0);
				dres = (double)ia / (double)ib;
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((gnum_float) dres);

			case OPER_EXP:
				if (ia == 0 && ib <= 0)
					return value_new_error (pos, gnumeric_err_NUM);
				dres = pow ((double)ia, (double)ib);
				ires = (int)dres;
				if (dres == ires)
					return value_new_int (ires);
				else
					return value_new_float ((gnum_float) dres);

			default:
				abort ();
			}
		} else {
			gnum_float const va = value_get_as_float (a);
			gnum_float const vb = value_get_as_float (b);
			value_release (a);
			value_release (b);

			switch (tree->any.oper){
			case OPER_ADD:
				return value_new_float (va + vb);

			case OPER_SUB:
				return value_new_float (va - vb);

			case OPER_MULT:
				return value_new_float (va * vb);

			case OPER_DIV:
				return (vb == 0.0)
				    ? value_new_error (pos,
						       gnumeric_err_DIV0)
				    : value_new_float (va / vb);

			case OPER_EXP:
				if ((va == 0 && vb <= 0) ||
				    (va < 0 && vb != (int)vb))
					return value_new_error (pos, gnumeric_err_NUM);
				return value_new_float (pow (va, vb));

			default:
				break;
			}
		}
		return value_new_error (pos, _("Unknown operator"));

	case OPER_PERCENT:
	case OPER_UNARY_NEG:
	case OPER_UNARY_PLUS:
	        /* Garantees that a != NULL */
		a = eval_expr (pos, tree->unary.value, flags & (~EVAL_PERMIT_EMPTY));

		/* Handle implicit intersection */
		if (a->type == VALUE_CELLRANGE) {
			a = expr_implicit_intersection (pos, a);
			if (a == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
		} else if (a->type == VALUE_ARRAY) {
			a = expr_array_intersection (a);
			if (a == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
		}

		if (a->type == VALUE_ERROR)
			return a;

		if (tree->any.oper == OPER_UNARY_PLUS)
			return a;

		if (!VALUE_IS_NUMBER (a)){
			value_release (a);
			return value_new_error (pos, gnumeric_err_VALUE);
		}
		if (tree->any.oper == OPER_UNARY_NEG) {
			if (a->type == VALUE_INTEGER)
				res = value_new_int (-a->v_int.val);
			else if (a->type == VALUE_FLOAT)
				res = value_new_float (-a->v_float.val);
			else
				res = value_new_bool (!a->v_float.val);
		} else
			res = value_new_float (value_get_as_float (a) * .01);
		value_release (a);
		return res;

	case OPER_CONCAT: {
		char *sa, *sb, *tmp;

		a = eval_expr_real (pos, tree->binary.value_a, flags);
		if (a != NULL && a->type == VALUE_ERROR)
			return a;
		b = eval_expr_real (pos, tree->binary.value_b, flags);
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
		return eval_funcall (pos, tree, flags);

	case OPER_NAME:
		return eval_expr_name (pos, tree->name.name, flags);

	case OPER_VAR: {
		Sheet *cell_sheet;
		CellRef const *ref;
		Cell *cell;
		int col, row;

		ref = &tree->var.ref;
		cell_get_abs_col_row (ref, &pos->eval, &col, &row);

		cell_sheet = eval_sheet (ref->sheet, pos->sheet);
		cell = sheet_cell_get (cell_sheet, col, row);
		if (cell == NULL)
			return NULL;

		if (cell->base.generation != pos->sheet->workbook->generation)
			cell_eval (cell);

		return value_duplicate (cell->value);
	}

	case OPER_CONSTANT:
		res = tree->constant.value;
		if (res->type != VALUE_CELLRANGE)
			return value_duplicate (res);
		if (flags & EVAL_PERMIT_NON_SCALAR) {
			eval_range (pos, res);
			return value_duplicate (res);
		} else {
			/*
			 * Handle the implicit union of a single row or
			 * column with the eval position.
			 * NOTE : We do not need to know if this is expression is
			 * being evaluated as an array or not because we can differentiate
			 * based on the required type for the argument.
			 */
			CellRef const * const a = & res->v_range.cell.a;
			CellRef const * const b = & res->v_range.cell.b;
			gboolean found = FALSE;

			if (a->sheet == b->sheet) {
				int a_col, a_row, b_col, b_row;
				int c = pos->eval.col;
				int r = pos->eval.row;

				cell_get_abs_col_row (a, &pos->eval, &a_col, &a_row);
				cell_get_abs_col_row (b, &pos->eval, &b_col, &b_row);
				if (a_row == b_row) {
					if (a_col <= c && c <= b_col) {
						r = a_row;
						found = TRUE;
					}
				} else if (a_col == b_col) {
					if (a_row <= r && r <= b_row) {
						c = a_col;
						found = TRUE;
					}
				}
				if (found) {
					Cell * cell = sheet_cell_get (pos->sheet, c, r);
					if (cell == NULL)
						return NULL;

					if (cell->base.generation != pos->sheet->workbook->generation)
						cell_eval (cell);

					return value_duplicate (cell->value);
				}
			}
			return value_new_error (pos, gnumeric_err_VALUE);
		}

	case OPER_ARRAY:
	{
		/* The upper left corner manages the recalc of the expr */
		int x = tree->array.x;
		int y = tree->array.y;
		if (x == 0 && y == 0){
			/* Release old value if necessary */
			a = tree->array.corner.value;
			if (a != NULL)
				value_release (a);

			/*
			 * FIXME : Call a wrapper routine that will iterate
			 * over the the array and evaluate the expression for
			 * all elements
			 *
			 * Figure out when to iterate and when to do array
			 * operations.
			 * ie
			 * 	A1:A3 = '=B1:B3^2'
			 * Will iterate over all the elements and re-evaluate.
			 * whereas
			 *	 A1:A3 = '=bob(B1:B3)'
			 * Will call bob once if it returns an array.
			 *
			 * This may be as simple as evaluating the corner.  If
			 * that is is an array return the result, else build an
			 * array and iterate over the elements, but that theory
			 * needs validation.
			 */
			a = eval_expr_real (pos, tree->array.corner.expr,
					    EVAL_PERMIT_NON_SCALAR);

			/* Store real result (cast away const)*/
			*((Value **)&(tree->array.corner.value)) = a;
		} else {
			ExprTree const * const array =
			    expr_tree_array_formula_corner (tree, pos->sheet, &pos->eval);
			if (array)
				a = array->array.corner.value;
			else
				a = NULL;
		}

		if (a != NULL &&
		    (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)) {
			int const num_x = value_area_get_width (pos, a);
			int const num_y = value_area_get_height (pos, a);

			/* Evaluate relative to the upper left corner */
			EvalPos tmp_ep = *pos;
			tmp_ep.eval.col -= x;
			tmp_ep.eval.row -= y;

			/* If the src array is 1 element wide or tall we wrap */
			if (x >= 1 && num_x == 1)
				x = 0;
			if (y >= 1 && num_y == 1)
				y = 0;
			if (x >= num_x || y >= num_y)
				return value_new_error (pos, gnumeric_err_NA);

			a = (Value *)value_area_get_x_y (&tmp_ep, a, x, y);
		}

		if (a == NULL)
			return NULL;
		return value_duplicate (a);
	}
	}

	return value_new_error (pos, _("Unknown evaluation error"));
}

Value *
eval_expr (EvalPos const *pos, ExprTree const *tree,
	   ExprEvalFlags flags)
{
	Value * res = eval_expr_real (pos, tree, flags);

	if (res == NULL)
		return (flags & EVAL_PERMIT_EMPTY)
		    ? NULL : value_new_int (0);

	if (res->type == VALUE_EMPTY) {
		value_release (res);
		if (flags & EVAL_PERMIT_EMPTY)
			return NULL;
		return value_new_int (0);
	}
	return res;
}

void
cell_ref_make_abs (CellRef *dest,
		   CellRef const *src,
		   EvalPos const *ep)
{
	g_return_if_fail (dest != NULL);
	g_return_if_fail (src != NULL);
	g_return_if_fail (ep != NULL);

	*dest = *src;
	if (src->col_relative)
		dest->col += ep->eval.col;

	if (src->row_relative)
		dest->row += ep->eval.row;

	dest->row_relative = dest->col_relative = FALSE;
}

int
cell_ref_get_abs_col (CellRef const *ref, EvalPos const *pos)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (pos != NULL, 0);

	if (ref->col_relative)
		return pos->eval.col + ref->col;
	return ref->col;

}
int
cell_ref_get_abs_row (CellRef const *ref, EvalPos const *pos)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (pos != NULL, 0);

	if (ref->row_relative)
		return pos->eval.row + ref->row;
	return ref->row;
}


void
cell_get_abs_col_row (CellRef const *cell_ref,
		      CellPos const *pos,
		      int *col, int *row)
{
	g_return_if_fail (cell_ref != NULL);

	if (cell_ref->col_relative)
		*col = pos->col + cell_ref->col;
	else
		*col = cell_ref->col;

	if (cell_ref->row_relative)
		*row = pos->row + cell_ref->row;
	else
		*row = cell_ref->row;
}

/*
 * Converts a parsed tree into its string representation
 * assuming that we are evaluating at col, row
 *
 * This routine is pretty simple: it walks the ExprTree and
 * creates a string representation.
 */
static char *
do_expr_tree_to_string (ExprTree const *tree, ParsePos const *pp,
			int paren_level)
{
	static const struct {
		char const *name;
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
		{ "-",  5, 0, 0 }, /* Unary - */
		{ "+",  5, 0, 0 }, /* Unary + */
		{ "%",  5, 0, 0 }, /* Percentage (NOT MODULO) */
		{ NULL, 0, 0, 0 }  /* Array    */
	};
	int op;

	op = tree->any.oper;

	switch (op) {
	case OPER_ANY_BINARY: {
		char *a, *b, *res;
		char const *opname;
		int const prec = operations[op].prec;

		a = do_expr_tree_to_string (tree->binary.value_a, pp,
					    prec - operations[op].assoc_left);
		b = do_expr_tree_to_string (tree->binary.value_b, pp,
					    prec - operations[op].assoc_right);
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
		int const prec = operations[op].prec;

		a = do_expr_tree_to_string (tree->unary.value, pp,
					    operations[op].prec);
		opname = operations[op].name;

		if (tree->any.oper != OPER_PERCENT) {
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

		fd = tree->func.func;
		arg_list = tree->func.arg_list;
		argc = g_list_length (arg_list);

		if (argc) {
			int i, len = 0;
			char sep [2] = { '\0', '\0' };

			sep [0] = format_get_arg_sep ();

			i = 0;
			args = g_malloc (sizeof (char *) * argc);
			for (l = arg_list; l; l = l->next, i++) {
				ExprTree *t = l->data;

				args [i] = do_expr_tree_to_string (t, pp, 0);
				len += strlen (args [i]) + 1;
			}
			len++;
			sum = g_malloc (len + 2);

			i = 0;
			sum [0] = 0;
			for (l = arg_list; l; l = l->next, i++) {
				strcat (sum, args [i]);
				if (l->next)
					strcat (sum, sep);
			}

			res = g_strconcat (function_def_get_name (fd),
					   "(", sum, ")", NULL);
			g_free (sum);

			for (i = 0; i < argc; i++)
				g_free (args [i]);
			g_free (args);

			return res;
		} else
			return g_strconcat (function_def_get_name (fd),
					    "()", NULL);
	}

	case OPER_NAME:
		return g_strdup (tree->name.name->name->str);

	case OPER_VAR: {
		CellRef const *cell_ref = &tree->var.ref;
		return cellref_name (cell_ref, pp, FALSE);
	}

	case OPER_CONSTANT: {
		Value *v = tree->constant.value;

		switch (v->type) {
		case VALUE_CELLRANGE: {
			char *a, *b, *res;

			a = cellref_name (&v->v_range.cell.a, pp, FALSE);
			b = cellref_name (&v->v_range.cell.b, pp,
					  v->v_range.cell.a.sheet ==
					  v->v_range.cell.b.sheet);

			res = g_strconcat (a, ":", b, NULL);

			g_free (a);
			g_free (b);

			return res;
		}

		case VALUE_STRING:
			return gnumeric_strescape (v->v_str.val->str);

		case VALUE_EMPTY:
			return g_strdup ("");

		case VALUE_ERROR:
			return g_strdup (v->v_err.mesg->str);

		case VALUE_BOOLEAN:
		case VALUE_INTEGER:
		case VALUE_FLOAT: {
			char *res, *vstr;
			vstr = value_get_as_string (v);

			/* If the number has a sign, pretend that it is the
			   result of OPER_UNARY_{NEG,PLUS}.  It is not clear how we would
			   currently get negative numbers here, but some
			   loader might do it.  */
			if ((vstr[0] == '-' || vstr[0] == '+') &&
			    operations[OPER_UNARY_NEG].prec <= paren_level) {
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
		int const x = tree->array.x;
		int const y = tree->array.y;
		char *res;
		if (x != 0 || y != 0) {
			ExprTree *array = expr_tree_array_formula_corner (tree,
						pp->sheet, &pp->eval);
			if (array) {
				ParsePos tmp_pos;
				tmp_pos.wb  = pp->wb;
				tmp_pos.eval.col = pp->eval.col - x;
				tmp_pos.eval.row = pp->eval.row - y;
				res = do_expr_tree_to_string (
					array->array.corner.expr,
					&tmp_pos, 0);
			} else
				res = g_strdup ("<ERROR>");
		} else {
			res = do_expr_tree_to_string (
			    tree->array.corner.expr, pp, 0);
		}

		return res;
        }
	}

	g_warning ("ExprTree: This should not happen\n");
	return g_strdup ("0");
}

char *
expr_tree_as_string (ExprTree const *tree, ParsePos const *pp)
{
	g_return_val_if_fail (tree != NULL, NULL);

	return do_expr_tree_to_string (tree, pp, 0);
}

typedef enum {
	CELLREF_NO_RELOCATE,
	CELLREF_RELOCATE_ERR,
	CELLREF_RELOCATE,

	/* Both ends of the range must be treated as if they are inside the range */
	CELLREF_RELOCATE_FORCE_TO_IN,
	CELLREF_RELOCATE_FORCE_FROM_IN,
} CellRefRelocate;

/*
 * FIXME :
 * C3 : =sum(a$1:b$2)
 * Cut B2:C3
 * Paste to the right or diagonal.
 * range changes when it should not.
 */
static CellRefRelocate
cellref_relocate (CellRef *ref, ExprRelocateInfo const *rinfo,
		  gboolean force_to_inside, gboolean force_from_inside)
{
	/* For row or column refs
	 * Ref	From	To
	 *
	 * Abs	In	In 	: Positive (Sheet) (b)
	 * Abs	In	Out 	: Sheet
	 * Abs	Out	In 	: Positive, Sheet, Range (b)
	 * Abs	Out	Out 	: (a)
	 * Rel	In	In 	: Sheet
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
	int col = cell_ref_get_abs_col (ref, &rinfo->pos);
	int row = cell_ref_get_abs_row (ref, &rinfo->pos);

	Sheet * ref_sheet = (ref->sheet != NULL) ? ref->sheet : rinfo->pos.sheet;

	/* Inside is based on the current location of the reference.
	 * Hence we need to use the ORIGIN_sheet rather than the target.
	 */
	gboolean const to_inside = force_to_inside ||
		((rinfo->origin_sheet == ref_sheet) &&
		range_contains (&rinfo->origin, col, row));
	gboolean const from_inside = force_from_inside ||
		((rinfo->origin_sheet == rinfo->pos.sheet) &&
		range_contains (&rinfo->origin, rinfo->pos.eval.col, rinfo->pos.eval.row));

	/* fprintf (stderr, "%s\n", cellref_name (ref, &rinfo->pos, FALSE)); */

	/* All references should be valid initially.  We assume that later. */
	if (col < 0 || col >= SHEET_MAX_COLS ||
	    row < 0 || row >= SHEET_MAX_ROWS)
		return CELLREF_RELOCATE_ERR;

	/* Case (a) */
	if (!from_inside && !to_inside)
		return CELLREF_NO_RELOCATE;

	if (from_inside != to_inside) {
		if (to_inside) {
			if (rinfo->pos.sheet == rinfo->target_sheet)
				ref_sheet = NULL;
		} else {
			if (ref_sheet == rinfo->target_sheet)
				ref_sheet = NULL;
		}
	} else
		ref_sheet = ref->sheet;

	if (to_inside) {
		/* Case (b) */
		if (!from_inside || !ref->col_relative)
			col += rinfo->col_offset;

		if (!from_inside || !ref->row_relative)
			row += rinfo->row_offset;

		if (col < 0 || col >= SHEET_MAX_COLS ||
		    row < 0 || row >= SHEET_MAX_ROWS)
			return CELLREF_RELOCATE_ERR;
	} else if (from_inside) {
		/* Case (c) */
		if (ref->col_relative)
			col -= rinfo->col_offset;
		if (ref->row_relative)
			row -= rinfo->row_offset;
	}

	if (ref->col_relative)
		col -= rinfo->pos.eval.col;
	if (ref->row_relative)
		row -= rinfo->pos.eval.row;

	if (ref->sheet != ref_sheet) {
		ref->sheet = ref_sheet;
		ref->col = col;
		ref->row = row;
		return CELLREF_RELOCATE;
	} else if (ref->col != col) {
		ref->col = col;
		if (ref->row != row) {
			ref->row = row;
			return CELLREF_RELOCATE;
		}
		/* FIXME : We should only do this if the start ? end ?
		 * col is included in the translation region (figure this out
		 * in relation to abs/rel references)
		 * Using offset == 0 would relocates too much.  If you cut B2
		 * and paste it into B3 the source region A1:B2 will resize to
		 * A1:B3 even though it should not.  This is correct for
		 * insert/delete row/col but not when dealing with cut & paste.
		 * To work around the problem I added a kludge to only do it if
		 * the target range is an entire row/col.  This gives the
		 * desired behavior most of the time.  However, it is probably
		 * not absolutely correct.
		 */
		if (rinfo->row_offset == 0 &&
		    rinfo->origin.start.row == 0 && rinfo->origin.end.row >= SHEET_MAX_ROWS-1)
			return from_inside ? CELLREF_RELOCATE_FORCE_FROM_IN
					   : CELLREF_RELOCATE_FORCE_TO_IN;
		return CELLREF_RELOCATE;
	} else if (ref->row != row) {
		ref->row = row;
		/* FIXME : As above */
		if (rinfo->col_offset == 0 &&
		    rinfo->origin.start.col == 0 && rinfo->origin.end.col >= SHEET_MAX_COLS-1)
			return from_inside ? CELLREF_RELOCATE_FORCE_FROM_IN
					   : CELLREF_RELOCATE_FORCE_TO_IN;
		return CELLREF_RELOCATE;
	}

	return CELLREF_NO_RELOCATE;
}

static ExprTree *
cellrange_relocate (const Value *v,
		    const ExprRelocateInfo *rinfo)
{
	/*
	 * If either end is an error then the whole range is an error.
	 * If both ends need relocation relocate
	 * otherwise remain static
	 */
	CellRef ref_a = v->v_range.cell.a;
	CellRef ref_b = v->v_range.cell.b;
	int needs_reloc = 0;

	switch (cellref_relocate (&ref_a, rinfo, FALSE, FALSE)) {
	case CELLREF_NO_RELOCATE :
		break;
	case CELLREF_RELOCATE_ERR :
		return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
	case CELLREF_RELOCATE :			needs_reloc++; break;
	case CELLREF_RELOCATE_FORCE_TO_IN :	needs_reloc += 0x10;  break;
	case CELLREF_RELOCATE_FORCE_FROM_IN :	needs_reloc += 0x100; break;
	}
	switch (cellref_relocate (&ref_b, rinfo, FALSE, FALSE)) {
	case CELLREF_NO_RELOCATE :
		break;
	case CELLREF_RELOCATE_ERR :
		return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
	case CELLREF_RELOCATE :			needs_reloc++; break;
	case CELLREF_RELOCATE_FORCE_TO_IN :	needs_reloc += 0x20;  break;
	case CELLREF_RELOCATE_FORCE_FROM_IN :	needs_reloc += 0x200; break;
	}

	/* Only relocate if both ends of the range need relocation */
	if (needs_reloc >= 2) {
		Value *res;
		Sheet const *sheet_a = ref_a.sheet;
		Sheet const *sheet_b = ref_b.sheet;

		if (sheet_a == NULL)
			sheet_a = rinfo->pos.sheet;
		if (sheet_b == NULL)
			sheet_b = rinfo->pos.sheet;

#if 0
		/* Force only happens when inserting row/col
		 * we want to deform the region
		 */
		switch (needs_reloc) {
		case 2 : break;
		case 0x10  : cellref_relocate (&ref_b, rinfo, TRUE, FALSE); break;
		case 0x20  : cellref_relocate (&ref_a, rinfo, TRUE, FALSE); break;
		case 0x100 : cellref_relocate (&ref_b, rinfo, FALSE, TRUE); break;
		case 0x200 : cellref_relocate (&ref_a, rinfo, FALSE, TRUE); break;
		default : g_warning ("Unexpected relocation type 0x%x", needs_reloc);
		};
#endif

		/* Dont allow creation of 3D references */
		if (sheet_a == sheet_b)
			res = value_new_cellrange (&ref_a, &ref_b,
						   rinfo->pos.eval.col,
						   rinfo->pos.eval.row);
		else
			res = value_new_error (NULL, gnumeric_err_REF);
		return expr_tree_new_constant (res);
	}

	return NULL;
}

/*
 * expr_rewrite :
 * @expr   : Expression to fixup
 * @pos    : Location of the cell containing @expr.
 * @rwinfo : State information required to rewrite the reference.
 *
 * Either:
 *
 * EXPR_REWRITE_SHEET:
 *
 *	Find any references to rwinfo->u.sheet and re-write them to #REF!
 *
 * or
 *
 * EXPR_REWRITE_WORKBOOK:
 *
 *	Find any references to rwinfo->u.workbook re-write them to #REF!
 *
 * or
 *
 * EXPR_REWRITE_RELOCATE:
 *
 *	Find any references to the specified area and adjust them by the
 * supplied deltas.  Check for out of bounds conditions.  Return NULL if
 * no change is required.
 *
 *	If the expression is within the range to be moved, its relative
 * references to cells outside the range are adjusted to reference the
 * same cell after the move.
 */
ExprTree *
expr_rewrite (ExprTree        const *expr,
	      ExprRewriteInfo const *rwinfo)
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (expr->any.oper) {
	case OPER_ANY_BINARY: {
		ExprTree *a = expr_rewrite (expr->binary.value_a, rwinfo);
		ExprTree *b = expr_rewrite (expr->binary.value_b, rwinfo);

		if (a == NULL && b == NULL)
			return NULL;

		if (a == NULL)
			expr_tree_ref ((a = expr->binary.value_a));
		else if (b == NULL)
			expr_tree_ref ((b = expr->binary.value_b));

		return expr_tree_new_binary (a, expr->any.oper, b);
	}

	case OPER_ANY_UNARY: {
		ExprTree *a = expr_rewrite (expr->unary.value, rwinfo);
		if (a == NULL)
			return NULL;
		return expr_tree_new_unary (expr->any.oper, a);
	}

	case OPER_FUNCALL: {
		gboolean rewrite = FALSE;
		GList *new_args = NULL;
		GList *l;

		for (l = expr->func.arg_list; l; l = l->next) {
			ExprTree *arg = expr_rewrite (l->data, rwinfo);
			new_args = g_list_append (new_args, arg);
			if (arg != NULL)
				rewrite = TRUE;
		}

		if (rewrite) {
			GList *m;

			for (l = expr->func.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					expr_tree_ref ((m->data = l->data));
			}

			return expr_tree_new_funcall (expr->func.func, new_args);
		}
		g_list_free (new_args);
		return NULL;
	}

	case OPER_NAME:
		if (!expr->name.name->builtin) {
			/* Do NOT rewrite the name.  Just invalidate the use of the name */
			ExprTree *tmp = expr_rewrite (expr->name.name->t.expr_tree, rwinfo);
			if (tmp != NULL) {
				expr_tree_unref (tmp);
				return expr_tree_new_constant (
					value_new_error (NULL, gnumeric_err_REF));
			}
		}
		return NULL;

	case OPER_VAR:
	{
		if (rwinfo->type == EXPR_REWRITE_SHEET) {
			if (expr->var.ref.sheet == rwinfo->u.sheet)
				return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
			else
				return NULL;

		} else if (rwinfo->type == EXPR_REWRITE_WORKBOOK) {
			if (expr->var.ref.sheet &&
			    expr->var.ref.sheet->workbook == rwinfo->u.workbook)
				return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
			else
				return NULL;

		} else {
			CellRef res = expr->var.ref; /* Copy */

			switch (cellref_relocate (&res, &rwinfo->u.relocate, FALSE, FALSE)) {
			case CELLREF_NO_RELOCATE :
				return NULL;
			case CELLREF_RELOCATE_ERR :
				return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
			case CELLREF_RELOCATE :
			case CELLREF_RELOCATE_FORCE_TO_IN :
			case CELLREF_RELOCATE_FORCE_FROM_IN :
				return expr_tree_new_var (&res);
			}
		}
	}

	case OPER_CONSTANT: {
		const Value *v = expr->constant.value;

		if (v->type == VALUE_CELLRANGE) {
			CellRef ref_a = v->v_range.cell.a;
			CellRef ref_b = v->v_range.cell.b;

			if (rwinfo->type == EXPR_REWRITE_SHEET) {

				if (ref_a.sheet == rwinfo->u.sheet ||
				    ref_b.sheet == rwinfo->u.sheet)
					return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
				else
					return NULL;

			} else if (rwinfo->type == EXPR_REWRITE_WORKBOOK) {

				if      (ref_a.sheet &&
					 ref_a.sheet->workbook == rwinfo->u.workbook)
					return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));

				else if (ref_b.sheet &&
					 ref_b.sheet->workbook == rwinfo->u.workbook)
					return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));

				else
					return NULL;

			} else
				return cellrange_relocate (v, &rwinfo->u.relocate);
		}

		return NULL;
	}

	case OPER_ARRAY: {
		ExprArray const * a = &expr->array;
		if (a->x == 0 && a->y == 0) {
			ExprTree *func = expr_rewrite (a->corner.expr, rwinfo);

			if (func != NULL) {
				ExprTree *res =
					expr_tree_new_array (0, 0, a->rows, a->cols);
				res->array.corner.value = NULL;
				res->array.corner.expr = func;
				return res;
			}
		}
		return NULL;
	}
	}

	g_assert_not_reached ();
	return NULL;
}

FunctionDefinition *
expr_tree_get_func_def (ExprTree const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (expr->any.oper == OPER_FUNCALL, NULL);

	return expr->func.func;
}

ExprTree const *
expr_tree_first_func (ExprTree const *expr)
{
	ExprTree const *tmp;

	g_return_val_if_fail (expr != NULL, NULL);

	switch (expr->any.oper) {
	default :
	case OPER_NAME:
	case OPER_VAR:
	case OPER_CONSTANT:
		return NULL;

	case OPER_FUNCALL:
		return expr;

	case OPER_ANY_BINARY:
		tmp = expr_tree_first_func (expr->binary.value_a);
		if (tmp != NULL)
			return tmp;
		return expr_tree_first_func (expr->binary.value_b);

	case OPER_ANY_UNARY:
		return expr_tree_first_func (expr->unary.value);

	case OPER_ARRAY:
		return expr_tree_first_func (expr->array.corner.expr);
	}

	g_assert_not_reached ();
	return NULL;
}
