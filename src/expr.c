/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * expr.c: Expression evaluation in Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "expr.h"

#include "expr-name.h"
#include "eval.h"
#include "format.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "str.h"
#include "parse-util.h"
#include "ranges.h"
#include "number-match.h"
#include "workbook.h"
#include "gutils.h"
#include "parse-util.h"

#include <math.h>
#include <string.h>

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
	ExprList *args = NULL;

	if (strcmp (txt, gnumeric_err_NA) != 0) {
		func = func_lookup_by_name ("ERROR", NULL);
		args = g_slist_prepend (NULL,
			expr_tree_new_constant (value_new_string (txt)));
	} else
		func = func_lookup_by_name ("NA", NULL);

	func_ref (func);
	return expr_tree_new_funcall (func, args);
}

ExprTree *
expr_tree_new_funcall (FunctionDefinition *func, ExprList *args)
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
expr_tree_new_name (NamedExpression *name,
		    Sheet *optional_scope, Workbook *optional_wb_scope)
{
	ExprName *ans;

	ans = g_new (ExprName, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = OPER_NAME;
	ans->name = name;
	expr_name_ref (name);

	ans->optional_scope = optional_scope;
	ans->optional_wb_scope = optional_wb_scope;

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
expr_tree_new_array (int x, int y, int cols, int rows)
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

ExprTree *
expr_tree_new_set (ExprList *set)
{
	ExprSet *ans;

	ans = g_new (ExprSet, 1);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	*((Operation *)&(ans->oper)) = OPER_SET;
	ans->set = set;

	return (ExprTree *)ans;
}

static Cell *
expr_tree_array_corner (ExprTree const *expr,
			Sheet const *sheet, CellPos const *pos)
{
	Cell *corner = sheet_cell_get (sheet,
		pos->col - expr->array.x, pos->row - expr->array.y);

	/* Sanity check incase the corner gets removed for some reason */
	g_return_val_if_fail (corner != NULL, NULL);
	g_return_val_if_fail (cell_has_expr (corner), NULL);
	g_return_val_if_fail (corner->base.expression != (void *)0xdeadbeef, NULL);
	g_return_val_if_fail (corner->base.expression->any.oper == OPER_ARRAY, NULL);
	g_return_val_if_fail (corner->base.expression->array.x == 0, NULL);
	g_return_val_if_fail (corner->base.expression->array.y == 0, NULL);

	return corner;
}

/*
 * expr_tree_ref:
 * Increments the ref_count for part of a tree
 */
void
expr_tree_ref (ExprTree *expr)
{
	g_return_if_fail (expr != NULL);
	g_return_if_fail (expr->any.ref_count > 0);

	expr->any.ref_count++;
}

static void
do_expr_tree_unref (ExprTree *expr)
{
	if (--expr->any.ref_count > 0)
		return;

	switch (expr->any.oper){
	case OPER_VAR:
		break;

	case OPER_CONSTANT:
		value_release (expr->constant.value);
		break;

	case OPER_FUNCALL:
		expr_list_unref (expr->func.arg_list);
		func_unref (expr->func.func);
		break;

	case OPER_NAME:
		expr_name_unref (expr->name.name);
		break;

	case OPER_ANY_BINARY:
		do_expr_tree_unref (expr->binary.value_a);
		do_expr_tree_unref (expr->binary.value_b);
		break;

	case OPER_ANY_UNARY:
		do_expr_tree_unref (expr->unary.value);
		break;
	case OPER_ARRAY:
		if (expr->array.x == 0 && expr->array.y == 0) {
			if (expr->array.corner.value)
				value_release (expr->array.corner.value);
			do_expr_tree_unref (expr->array.corner.expr);
		}
		break;
	case OPER_SET:
		expr_list_unref (expr->set.set);
		break;

	default:
		g_warning ("do_expr_tree_unref error\n");
		break;
	}

	g_free (expr);
}

/*
 * expr_tree_unref:
 * Decrements the ref_count for part of a tree.  (All trees are expected
 * to have been created with a ref-count of one, so when we hit zero, we
 * go down over the tree and unref the tree and its leaves stuff.)
 */
void
expr_tree_unref (ExprTree *expr)
{
	g_return_if_fail (expr != NULL);
	g_return_if_fail (expr->any.ref_count > 0);

	do_expr_tree_unref (expr);
}

/**
 * expr_tree_is_shared : Returns TRUE if the reference count
 *   for the supplied expression is > 1
 */
gboolean
expr_tree_is_shared (ExprTree const *expr)
{
	g_return_val_if_fail (expr != NULL, FALSE);

	return (expr->any.ref_count > 1);
}

/**
 * expr_tree_equal : Returns TRUE if the supplied expressions are exactly the
 *   same.  No eval position is used to see if they are effectively the same.
 *   Named expressions must refer the the same name, having equivalent names is
 *   insufficeient.
 */
gboolean
expr_tree_equal (ExprTree const *a, ExprTree const *b)
{
	if (a == b)
		return TRUE;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a->any.oper != b->any.oper)
		return FALSE;

	switch (a->any.oper) {
	case OPER_ANY_BINARY:
		return	expr_tree_equal (a->binary.value_a, b->binary.value_a) &&
			expr_tree_equal (a->binary.value_b, b->binary.value_b);

	case OPER_ANY_UNARY:
		return expr_tree_equal (a->unary.value, b->unary.value);

	case OPER_FUNCALL:
		return (a->func.func == b->func.func) &&
			expr_list_equal (a->func.arg_list, b->func.arg_list);

	case OPER_NAME:
		return a->name.name == b->name.name;

	case OPER_VAR:
		return cellref_equal (&a->var.ref, &b->var.ref);

	case OPER_CONSTANT: {
		Value const *va = a->constant.value;
		Value const *vb = b->constant.value;

		if (va->type != vb->type)
			return FALSE;

		if (va->type == VALUE_CELLRANGE)
			return	cellref_equal (&va->v_range.cell.a, &vb->v_range.cell.a) &&
				cellref_equal (&va->v_range.cell.b, &vb->v_range.cell.b);

		return value_compare (va, vb, TRUE) == IS_EQUAL;
	}

	case OPER_ARRAY: {
		ExprArray const *aa = &a->array;
		ExprArray const *ab = &b->array;

		return	aa->cols == ab->cols &&
			aa->rows == ab->rows &&
			aa->x == ab->x &&
			aa->y == ab->y &&
			expr_tree_equal (aa->corner.expr, ab->corner.expr);
	}

	case OPER_SET:
		return expr_list_equal (a->set.set, b->set.set);

	default :
		g_assert_not_reached ();
	}

	return FALSE;
}

static Value *
eval_funcall (EvalPos const *pos, ExprTree const *expr,
	      ExprEvalFlags flags)
{
	FunctionEvalInfo ei;
	FunctionDefinition *fd;
	ExprList *args;

	g_return_val_if_fail (pos != NULL, NULL);
	g_return_val_if_fail (expr != NULL, NULL);

	fd = expr->func.func;
	ei.func_def = fd;
	ei.pos = pos;
	args = expr->func.arg_list;

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
expr_implicit_intersection (EvalPos const *pos, Value *v)
{
	Value *res = NULL;
	Range rng;
	Sheet *start_sheet, *end_sheet;

	/* handle inverted ranges */
	value_cellrange_normalize (pos, v, &start_sheet, &end_sheet, &rng);

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

static Value *
cb_range_eval (Sheet *sheet, int col, int row, Cell *cell, void *ignore)
{
	cell_eval (cell);
	return NULL;
}

static Value *
expr_eval_real (ExprTree const *expr, EvalPos const *pos,
		ExprEvalFlags flags)
{
	Value *res = NULL, *a = NULL, *b = NULL;

	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	switch (expr->any.oper){
	case OPER_EQUAL:
	case OPER_NOT_EQUAL:
	case OPER_GT:
	case OPER_GTE:
	case OPER_LT:
	case OPER_LTE: {
		ValueCompare comp;

		a = expr_eval_real (expr->binary.value_a, pos, flags);
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

		b = expr_eval_real (expr->binary.value_b, pos, flags);
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
			if (expr->any.oper == OPER_EQUAL)
				return value_new_bool (FALSE);
			if (expr->any.oper == OPER_NOT_EQUAL)
				return value_new_bool (TRUE);

			return value_new_error (pos, gnumeric_err_VALUE);
		}

		switch (expr->any.oper) {
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
		a = expr_eval (expr->binary.value_a, pos,
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
			return value_error_set_pos (&a->v_err, pos);

		/* 2) #!VALUE error if A is not a number */
		if (a->type == VALUE_STRING) {
			Value *tmp = format_match_number (a->v_str.val->str, NULL, NULL);

			value_release (a);
			if (tmp == NULL)
				return value_new_error (pos, gnumeric_err_VALUE);
			a = tmp;
		} else if (!VALUE_IS_NUMBER (a)) {
			value_release (a);
			return value_new_error (pos, gnumeric_err_VALUE);
		}

	        /* Guarantees that b != NULL */
		b = expr_eval (expr->binary.value_b, pos,
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
			return value_error_set_pos (&b->v_err, pos);
		}

		/* 4) #!VALUE error if B is not a number */
		if (b->type == VALUE_STRING) {
			Value *tmp = format_match_number (b->v_str.val->str, NULL, NULL);

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
			switch (expr->any.oper){
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

			switch (expr->any.oper){
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
		a = expr_eval (expr->unary.value, pos,
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

		if (a->type == VALUE_ERROR)
			return a;

		if (expr->any.oper == OPER_UNARY_PLUS)
			return a;

		if (!VALUE_IS_NUMBER (a)){
			value_release (a);
			return value_new_error (pos, gnumeric_err_VALUE);
		}
		if (expr->any.oper == OPER_UNARY_NEG) {
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

	case OPER_CONCAT:
		a = expr_eval_real (expr->binary.value_a, pos, flags);
		if (a != NULL && a->type == VALUE_ERROR)
			return a;
		b = expr_eval_real (expr->binary.value_b, pos, flags);
		if (b != NULL && b->type == VALUE_ERROR) {
			if (a != NULL)
				value_release (a);
			return b;
		}

		if (a == NULL) {
			if (b != NULL) {
				res = value_new_string (value_peek_string (b));
				value_release (b);
			} else
				res = value_new_string ("");
		} else if (b == NULL) {
			res = value_new_string (value_peek_string (a));
			value_release (a);
		} else {
			char *tmp = g_strconcat (value_peek_string (a),
						 value_peek_string (b), NULL);
			res = value_new_string (tmp);
			g_free (tmp);
			value_release (a);
			value_release (b);
		}

		return res;

	case OPER_FUNCALL:
		return eval_funcall (pos, expr, flags);

	case OPER_NAME:
		if (expr->name.name->active)
			return expr_name_eval (expr->name.name, pos, flags);
		return value_new_error (pos, gnumeric_err_REF);

	case OPER_VAR: {
		CellRef const * const ref = &expr->var.ref;
		Cell *cell;
		CellPos dest;

		cellref_get_abs_pos (ref, &pos->eval, &dest);

		cell = sheet_cell_get (eval_sheet (ref->sheet, pos->sheet),
			dest.col, dest.row);
		if (cell == NULL)
			return NULL;

		cell_eval (cell);

		return value_duplicate (cell->value);
	}

	case OPER_CONSTANT:
		res = expr->constant.value;
		if (res->type != VALUE_CELLRANGE)
			return value_duplicate (res);
		if (flags & EVAL_PERMIT_NON_SCALAR) {
			workbook_foreach_cell_in_range (pos, res, TRUE,
							cb_range_eval, NULL);
			return value_duplicate (res);
		} else {
			/*
			 * Handle the implicit union of a single row or
			 * column with the eval position.
			 * NOTE : We do not need to know if this is expression is
			 * being evaluated as an array or not because we can differentiate
			 * based on the required type for the argument.
			 */
			CellRef const * const ref_a = & res->v_range.cell.a;
			CellRef const * const ref_b = & res->v_range.cell.b;
			gboolean found = FALSE;

			if (ref_a->sheet == ref_b->sheet) {
				CellPos a, b;
				int c = pos->eval.col;
				int r = pos->eval.row;

				cellref_get_abs_pos (ref_a, &pos->eval, &a);
				cellref_get_abs_pos (ref_b, &pos->eval, &b);
				if (a.row == b.row) {
					if (a.col <= c && c <= b.col) {
						r = a.row;
						found = TRUE;
					}
				} else if (a.col == b.col) {
					if (a.row <= r && r <= b.row) {
						c = a.col;
						found = TRUE;
					}
				}
				if (found) {
					Cell * cell = sheet_cell_get (pos->sheet, c, r);
					if (cell == NULL)
						return NULL;

					cell_eval (cell);

					return value_duplicate (cell->value);
				}
			}
			return value_new_error (pos, gnumeric_err_VALUE);
		}

	case OPER_ARRAY:
	{
		/* The upper left corner manages the recalc of the expr */
		int x = expr->array.x;
		int y = expr->array.y;
		if (x == 0 && y == 0){
			/* Release old value if necessary */
			a = expr->array.corner.value;
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
			a = expr_eval_real (expr->array.corner.expr, pos,
					    EVAL_PERMIT_NON_SCALAR);

			/* Store real result (cast away const)*/
			*((Value **)&(expr->array.corner.value)) = a;
		} else {
			Cell *corner = expr_tree_array_corner (expr,
				pos->sheet, &pos->eval);
			if (corner != NULL) {
				cell_eval (corner);
				a = corner->base.expression->array.corner.value;
			} else
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
	case OPER_SET:
		g_warning ("Not implemented until 1.1");
	}

	return value_new_error (pos, _("Unknown evaluation error"));
}

Value *
expr_eval (ExprTree const *expr, EvalPos const *pos,
	   ExprEvalFlags flags)
{
	Value * res = expr_eval_real (expr, pos, flags);

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

/*
 * Converts a parsed tree into its string representation
 * assuming that we are evaluating at col, row
 *
 * This routine is pretty simple: it walks the ExprTree and
 * creates a string representation.
 */
static char *
do_expr_tree_as_string (ExprTree const *expr, ParsePos const *pp,
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
		{ "-",  5, 0, 0 }, /* Unary -  */
		{ "+",  5, 0, 0 }, /* Unary +  */
		{ "%",  5, 0, 0 }, /* Percentage (NOT MODULO) */
		{ NULL, 0, 0, 0 }, /* Array    */
		{ NULL, 0, 0, 0 }  /* Set      */
	};
	int const op = expr->any.oper;

	switch (op) {
	case OPER_ANY_BINARY: {
		char *a, *b, *res;
		char const *opname;
		int const prec = operations[op].prec;

		a = do_expr_tree_as_string (expr->binary.value_a, pp,
					    prec - operations[op].assoc_left);
		b = do_expr_tree_as_string (expr->binary.value_b, pp,
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

		a = do_expr_tree_as_string (expr->unary.value, pp,
					    operations[op].prec);
		opname = operations[op].name;

		if (expr->any.oper != OPER_PERCENT) {
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
		ExprList const * const arg_list = expr->func.arg_list;

		if (arg_list != NULL) {
			char *sum = expr_list_as_string (arg_list, pp);
			char *res = g_strconcat (function_def_get_name (expr->func.func),
				"(", sum, ")", NULL);
			g_free (sum);
			return res;
		} else
			return g_strconcat (function_def_get_name (expr->func.func),
					    "()", NULL);
	}

	case OPER_NAME:
		if (!expr->name.name->active)
			return g_strdup (gnumeric_err_REF);
		if (expr->name.optional_scope != NULL) {
			if (expr->name.optional_scope->workbook != pp->wb)
				return g_strconcat (
					"[", expr->name.optional_wb_scope->filename, "]",
					expr->name.name->name->str, NULL);
			return g_strconcat (expr->name.optional_scope->name_quoted, "!",
					    expr->name.name->name->str, NULL);
		}

		return g_strdup (expr->name.name->name->str);

	case OPER_VAR:
		return cellref_name (&expr->var.ref, pp, FALSE);

	case OPER_CONSTANT: {
		char *res;
		Value const *v = expr->constant.value;
		if (v->type == VALUE_STRING)
			return gnumeric_strescape (v->v_str.val->str);
		if (v->type == VALUE_CELLRANGE) {
			char *b, *a = cellref_name (&v->v_range.cell.a, pp, FALSE);
			if (cellref_equal (&v->v_range.cell.a, &v->v_range.cell.b))
				return a;
			b = cellref_name (&v->v_range.cell.b, pp,
				v->v_range.cell.a.sheet == v->v_range.cell.b.sheet);
			res = g_strconcat (a, ":", b, NULL);
			g_free (a);
			g_free (b);
		} else {
			res = value_get_as_string (v);

			/* If the number has a sign, pretend that it is the result of
			 * OPER_UNARY_{NEG,PLUS}.  It is not clear how we would
			 * currently get negative numbers here, but some loader might
			 * do it.
			 */
			if ((v->type == VALUE_INTEGER || v->type == VALUE_FLOAT) &&
			    (res [0] == '-' || res [0] == '+') &&
			    operations [OPER_UNARY_NEG].prec <= paren_level) {
				char *new_res = g_strconcat ("(", res, ")", NULL);
				g_free (res);
				return new_res;
			}
		}
		return res;
	}

	case OPER_ARRAY: {
		int const x = expr->array.x;
		int const y = expr->array.y;
		if (x != 0 || y != 0) {
			Cell *corner = expr_tree_array_corner (expr,
				pp->sheet, &pp->eval);
			if (corner) {
				ParsePos tmp_pos;
				tmp_pos.wb  = pp->wb;
				tmp_pos.eval.col = pp->eval.col - x;
				tmp_pos.eval.row = pp->eval.row - y;
				return do_expr_tree_as_string (
					corner->base.expression->array.corner.expr,
					&tmp_pos, 0);
			} else
				return g_strdup ("<ERROR>");
		} else
			return do_expr_tree_as_string (
			    expr->array.corner.expr, pp, 0);
        }

	case OPER_SET:
		return expr_list_as_string (expr->set.set, pp);
	}

	g_warning ("ExprTree: This should not happen\n");
	return g_strdup ("0");
}

char *
expr_tree_as_string (ExprTree const *expr, ParsePos const *pp)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (pp != NULL, NULL);

	return do_expr_tree_as_string (expr, pp, 0);
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
	int col = cellref_get_abs_col (ref, &rinfo->pos);
	int row = cellref_get_abs_row (ref, &rinfo->pos);

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

	if (from_inside != to_inside && ref->sheet == NULL) {
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
cellrange_relocate (Value const *v, ExprRelocateInfo const *rinfo)
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
expr_rewrite (ExprTree const *expr, ExprRewriteInfo const *rwinfo)
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
		ExprList *new_args = NULL;
		ExprList *l;

		for (l = expr->func.arg_list; l; l = l->next) {
			ExprTree *arg = expr_rewrite (l->data, rwinfo);
			new_args = expr_list_append (new_args, arg);
			if (arg != NULL)
				rewrite = TRUE;
		}

		if (rewrite) {
			ExprList *m;

			for (l = expr->func.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					expr_tree_ref ((m->data = l->data));
			}

			return expr_tree_new_funcall (expr->func.func, new_args);
		}
		g_slist_free (new_args);
		return NULL;
	}

	case OPER_NAME: {
		NamedExpression *nexpr = expr->name.name;
		ExprTree *tmp;

		if (nexpr->builtin)
			return NULL;

		/* we can not invalidate references to the name that are
		 * sitting in the undo queue, or the clipboard.  So we just
		 * flag the name as inactive and remove the reference here.
		 */
		if (!nexpr->active ||
		    (rwinfo->type == EXPR_REWRITE_SHEET && rwinfo->u.sheet == nexpr->pos.sheet) ||
		    (rwinfo->type == EXPR_REWRITE_WORKBOOK && rwinfo->u.workbook == nexpr->pos.wb))
			return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));

		if (rwinfo->type != EXPR_REWRITE_RELOCATE)
			return NULL;

		/* If the nme is not officially scope check that it is
		 * available in the new scope ?
		 */
		if (expr->name.optional_scope == NULL &&
		    rwinfo->u.relocate.target_sheet != rwinfo->u.relocate.origin_sheet) {
			NamedExpression *new_nexpr;
			ParsePos pos;
			parse_pos_init (&pos,  NULL,
				rwinfo->u.relocate.target_sheet, 0, 0);

			/* If the name is not available in the new scope explicitly scope it */
			new_nexpr = expr_name_lookup (&pos, nexpr->name->str);
			if (new_nexpr == NULL) {
				if (nexpr->pos.sheet != NULL)
					return expr_tree_new_name (nexpr, nexpr->pos.sheet, NULL);
				return expr_tree_new_name (nexpr, NULL, nexpr->pos.wb);
			}

			/* replace it with the new name using qualified as
			 * local to the target sheet
			 */
			return expr_tree_new_name (new_nexpr, pos.sheet, NULL);
		}

		/* Do NOT rewrite the name.  Just invalidate the use of the name */
		tmp = expr_rewrite (expr->name.name->t.expr_tree, rwinfo);
		if (tmp != NULL) {
			expr_tree_unref (tmp);
			return expr_tree_new_constant (
				value_new_error (NULL, gnumeric_err_REF));
		}

		return NULL;
	}

	case OPER_VAR:
		switch (rwinfo->type) {
		case EXPR_REWRITE_SHEET :
			if (expr->var.ref.sheet == rwinfo->u.sheet)
				return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
			return NULL;

		case EXPR_REWRITE_WORKBOOK :
			if (expr->var.ref.sheet != NULL &&
			    expr->var.ref.sheet->workbook == rwinfo->u.workbook)
				return expr_tree_new_constant (value_new_error (NULL, gnumeric_err_REF));
			return NULL;

		case EXPR_REWRITE_RELOCATE : {
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
		return NULL;

	case OPER_CONSTANT: {
		Value const *v = expr->constant.value;

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
		ExprArray const *a = &expr->array;
		if (a->x == 0 && a->y == 0) {
			ExprTree *func = expr_rewrite (a->corner.expr, rwinfo);

			if (func != NULL) {
				ExprTree *res =
					expr_tree_new_array (0, 0, a->cols, a->rows);
				res->array.corner.value = NULL;
				res->array.corner.expr = func;
				return res;
			}
		}
		return NULL;
	}
	case OPER_SET:
		return NULL;
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

/**
 * expr_tree_first_func :
 * @expr :
 *
 */
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

static void
cellref_boundingbox (CellRef const *cr, Range *bound)
{
	if (cr->col_relative) {
		if (cr->col >= 0) {
			int const c = SHEET_MAX_COLS - cr->col - 1;
			if (bound->end.col > c)
				bound->end.col = c;
		} else {
			int const c = -cr->col;
			if (bound->start.col < c)
				bound->start.col = c;
		}
	}
	if (cr->row_relative) {
		if (cr->row >= 0) {
			int const r = SHEET_MAX_ROWS - cr->row - 1;
			if (bound->end.row > r)
				bound->end.row = r;
		} else {
			int const r = -cr->row;
			if (bound->start.row < r)
				bound->start.row = r;
		}
	}
}

static GSList *
g_slist_insert_unique (GSList *list, gpointer data)
{
	if (data != NULL && g_slist_find (list, data) == NULL)
		return g_slist_prepend (list, data);
	return list;
}

static GSList *
do_referenced_sheets (ExprTree const *expr, GSList *sheets)
{
	switch (expr->any.oper) {
	case OPER_ANY_BINARY:
		return do_referenced_sheets (
			expr->binary.value_b,
			do_referenced_sheets (
				expr->binary.value_b,
				sheets));

	case OPER_ANY_UNARY:
		return do_referenced_sheets (expr->unary.value, sheets);

	case OPER_FUNCALL: {
		ExprList *l;
		for (l = expr->func.arg_list; l; l = l->next)
			sheets = do_referenced_sheets (l->data, sheets);
		return sheets;
	}

	case OPER_NAME:
		return sheets;

	case OPER_VAR:
		return g_slist_insert_unique (sheets, expr->var.ref.sheet);

	case OPER_CONSTANT: {
		Value const *v = expr->constant.value;
		if (v->type != VALUE_CELLRANGE)
			return sheets;
		return g_slist_insert_unique (
				g_slist_insert_unique (sheets,
						       v->v_range.cell.a.sheet),
				v->v_range.cell.b.sheet);
	}

	case OPER_ARRAY:
		g_warning ("An array in a NAME ?");
		break;

	default :
		break;
	}
	return sheets;
}

/**
 * expr_tree_referenced_sheets :
 * @expr :
 * @sheets : usually NULL.
 *
 * Generates a list of the sheets referenced by the supplied expression.
 * Caller must free the list.
 */
GSList *
expr_tree_referenced_sheets (ExprTree const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	return do_referenced_sheets (expr, NULL);
}

/**
 * expr_tree_boundingbox :
 *
 * Returns the range of cells in which the expression can be used without going
 * out of bounds.
 */
void
expr_tree_boundingbox (ExprTree const *expr, Range *bound)
{
	g_return_if_fail (expr != NULL);

	switch (expr->any.oper) {
	case OPER_ANY_BINARY:
		expr_tree_boundingbox (expr->binary.value_a, bound);
		expr_tree_boundingbox (expr->binary.value_b, bound);
		break;

	case OPER_ANY_UNARY:
		expr_tree_boundingbox (expr->unary.value, bound);
		break;

	case OPER_FUNCALL: {
		ExprList *l;
		for (l = expr->func.arg_list; l; l = l->next)
			expr_tree_boundingbox (l->data, bound);
		break;
	}

	case OPER_NAME:
		/* Do NOT validate the name. */
		/* TODO : is that correct ? */
		break;

	case OPER_VAR:
		cellref_boundingbox (&expr->var.ref, bound);
		break;

	case OPER_CONSTANT: {
		Value const *v = expr->constant.value;

		if (v->type == VALUE_CELLRANGE) {
			cellref_boundingbox (&v->v_range.cell.a, bound);
			cellref_boundingbox (&v->v_range.cell.b, bound);
		}
		break;
	}

	case OPER_ARRAY: {
		ExprArray const *a = &expr->array;
		if (a->x == 0 && a->y == 0)
			expr_tree_boundingbox (a->corner.expr, bound);
		break;
	}

	default :
		g_assert_not_reached ();
	}
}

/**
 * expr_tree_get_range:
 * @expr :
 * 
 * If this expression contains a single range return it.
 */
Value *
expr_tree_get_range (ExprTree const *expr) 
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (expr->any.oper) {
	case OPER_VAR :
		return value_new_cellrange_unsafe (
			&expr->var.ref, &expr->var.ref);

	case OPER_CONSTANT:
		if (expr->constant.value->type == VALUE_CELLRANGE)
			return value_duplicate (expr->constant.value);
		return NULL;

	case OPER_NAME:
		if (!expr->name.name->active || expr->name.name->builtin)
			return NULL;
		return expr_tree_get_range (expr->name.name->t.expr_tree);

	default:
		return NULL;
	}
}

void
expr_list_unref (ExprList *list)
{
	ExprList *l;
	for (l = list; l; l = l->next)
		do_expr_tree_unref (l->data);
	expr_list_free (list);
}

gboolean
expr_list_equal (ExprList const *la, ExprList const *lb)
{
	for (; la != NULL && lb != NULL; la = la->next, lb =lb->next)
		if (!expr_tree_equal (la->data, lb->data))
			return FALSE;
	return (la == NULL) && (lb == NULL);
}

char *
expr_list_as_string (ExprList const *list, ParsePos const *pp)
{
	int i, len = 0;
	int argc = expr_list_length ((ExprList *)list);
	char sep [2] = { '\0', '\0' };
	char *sum, **args;
	ExprList const *l;

	sep [0] = format_get_arg_sep ();

	i = 0;
	args = g_malloc (sizeof (char *) * argc);
	for (l = list; l; l = l->next, i++) {
		ExprTree *t = l->data;

		args [i] = do_expr_tree_as_string (t, pp, 0);
		len += strlen (args [i]) + 1;
	}
	len++;
	sum = g_malloc (len + 2);

	i = 0;
	sum [0] = 0;
	for (l = list; l != NULL; l = l->next) {
		strcat (sum, args [i++]);
		if (l->next)
			strcat (sum, sep);
	}

	for (i = 0; i < argc; i++)
		g_free (args [i]);
	g_free (args);

	return sum;
}
