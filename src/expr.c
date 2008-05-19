/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * expr.c : Expression evaluation in Gnumeric
 *
 * Copyright (C) 2001-2006 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1998-2000 Miguel de Icaza (miguel@gnu.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "expr.h"

#include "expr-impl.h"
#include "expr-name.h"
#include "dependent.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "str.h"
#include "value.h"
#include "parse-util.h"
#include "ranges.h"
#include "number-match.h"
#include "workbook-priv.h"
#include "gutils.h"
#include "parse-util.h"
#include "mathfunc.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <goffice/utils/go-locale.h>
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-glib-extras.h>

/*
 * Using pools here probably does not save anything, but it's a darn
 * good debugging tool.
 */
#ifndef USE_EXPR_POOLS
#define USE_EXPR_POOLS 1
#endif

#if USE_EXPR_POOLS
/* Memory pools for expressions.  */
static GOMemChunk *expression_pool_small, *expression_pool_big;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif

/***************************************************************************/

/**
 * gnm_expr_new_constant :
 * @v :
 *
 * Absorbs the value.
 **/
GnmExpr const *
gnm_expr_new_constant (GnmValue *v)
{
	GnmExprConstant *ans;

	ans = CHUNK_ALLOC (GnmExprConstant, expression_pool_small);
	if (!ans)
		return NULL;
	gnm_expr_constant_init (ans, v);

	return (GnmExpr *)ans;
}

/***************************************************************************/

static GnmExpr const *
gnm_expr_new_funcallv (GnmFunc *func, int argc, GnmExprConstPtr *argv)
{
	GnmExprFunction *ans;
	g_return_val_if_fail (func, NULL);

	ans = CHUNK_ALLOC (GnmExprFunction, expression_pool_small);

	ans->oper = GNM_EXPR_OP_FUNCALL;
	gnm_func_ref (func);
	ans->func = func;
	ans->argc = argc;
	ans->argv = argv;

	return (GnmExpr *)ans;
}

GnmExpr const *
gnm_expr_new_funcall (GnmFunc *func, GnmExprList *arg_list)
{
	GnmExprList *arg_list0 = arg_list;
	int argc = gnm_expr_list_length (arg_list);
	GnmExprConstPtr *argv = argc ? g_new (GnmExprConstPtr, argc) : NULL;
	int i;

	for (i = 0; arg_list; i++, arg_list = arg_list->next)
		argv[i] = arg_list->data;
	gnm_expr_list_free (arg_list0);

	return gnm_expr_new_funcallv (func, argc, argv);
}

GnmExpr const *
gnm_expr_new_funcall1 (GnmFunc *func,
		       GnmExpr const *arg0)
{
	GnmExprConstPtr *argv = g_new (GnmExprConstPtr, 1);
	argv[0] = arg0;
	return gnm_expr_new_funcallv (func, 1, argv);
}

GnmExpr const *
gnm_expr_new_funcall2 (GnmFunc *func,
		       GnmExpr const *arg0,
		       GnmExpr const *arg1)
{
	GnmExprConstPtr *argv = g_new (GnmExprConstPtr, 2);
	argv[0] = arg0;
	argv[1] = arg1;
	return gnm_expr_new_funcallv (func, 2, argv);
}

GnmExpr const *
gnm_expr_new_funcall3 (GnmFunc *func,
		       GnmExpr const *arg0,
		       GnmExpr const *arg1,
		       GnmExpr const *arg2)
{
	GnmExprConstPtr *argv = g_new (GnmExprConstPtr, 3);
	argv[0] = arg0;
	argv[1] = arg1;
	argv[2] = arg2;
	return gnm_expr_new_funcallv (func, 3, argv);
}


/***************************************************************************/

GnmExpr const *
gnm_expr_new_unary  (GnmExprOp op, GnmExpr const *e)
{
	GnmExprUnary *ans;

	ans = CHUNK_ALLOC (GnmExprUnary, expression_pool_small);
	if (!ans)
		return NULL;

	ans->oper = op;
	ans->value = e;

	return (GnmExpr *)ans;
}

/***************************************************************************/

GnmExpr const *
gnm_expr_new_binary (GnmExpr const *l, GnmExprOp op, GnmExpr const *r)
{
	GnmExprBinary *ans;

	ans = CHUNK_ALLOC (GnmExprBinary, expression_pool_small);
	if (!ans)
		return NULL;

	ans->oper = op;
	ans->value_a = l;
	ans->value_b = r;

	return (GnmExpr *)ans;
}

/***************************************************************************/

GnmExpr const *
gnm_expr_new_name (GnmNamedExpr *name,
		   Sheet *optional_scope, Workbook *optional_wb_scope)
{
	GnmExprName *ans;

	ans = CHUNK_ALLOC (GnmExprName, expression_pool_big);
	if (!ans)
		return NULL;

	ans->oper = GNM_EXPR_OP_NAME;
	ans->name = name;
	expr_name_ref (name);

	ans->optional_scope = optional_scope;
	ans->optional_wb_scope = optional_wb_scope;

	return (GnmExpr *)ans;
}

/***************************************************************************/

GnmExpr const *
gnm_expr_new_cellref (GnmCellRef const *cr)
{
	GnmExprCellRef *ans;

	ans = CHUNK_ALLOC (GnmExprCellRef, expression_pool_big);
	if (!ans)
		return NULL;

	ans->oper = GNM_EXPR_OP_CELLREF;
	ans->ref = *cr;

	return (GnmExpr *)ans;
}

/***************************************************************************/

/**
 * gnm_expr_new_array_corner :
 * @cols :
 * @rows :
 * @expr : optionally NULL.
 *
 * Absorb a referernce to @expr if it is non NULL.
 **/
static GnmExpr const *
gnm_expr_new_array_corner(int cols, int rows, GnmExpr const *expr)
{
	GnmExprArrayCorner *ans;

	ans = CHUNK_ALLOC (GnmExprArrayCorner, expression_pool_big);
	if (ans == NULL)
		return NULL;

	ans->oper = GNM_EXPR_OP_ARRAY_CORNER;
	ans->rows = rows;
	ans->cols = cols;
	ans->value = NULL;
	ans->expr = expr;
	return (GnmExpr *)ans;
}

static GnmExpr const *
gnm_expr_new_array_elem  (int x, int y)
{
	GnmExprArrayElem *ans;

	ans = CHUNK_ALLOC (GnmExprArrayElem, expression_pool_small);
	if (ans == NULL)
		return NULL;

	ans->oper = GNM_EXPR_OP_ARRAY_ELEM;
	ans->x = x;
	ans->y = y;
	return (GnmExpr *)ans;
}

/***************************************************************************/

static GnmExpr const *
gnm_expr_new_setv (int argc, GnmExprConstPtr *argv)
{
	GnmExprSet *ans = CHUNK_ALLOC (GnmExprSet, expression_pool_small);

	ans->oper = GNM_EXPR_OP_SET;
	ans->argc = argc;
	ans->argv = argv;

	return (GnmExpr *)ans;
}

GnmExpr const *
gnm_expr_new_set (GnmExprList *set)
{
	int i, argc;
	GnmExprConstPtr *argv;
	GnmExprList *set0 = set;

	argc = gnm_expr_list_length (set);
	argv = argc ? g_new (GnmExprConstPtr, argc) : NULL;
	for (i = 0; set; i++, set = set->next)
		argv[i] = set->data;
	gnm_expr_list_free (set0);

	return gnm_expr_new_setv (argc, argv);
}

/***************************************************************************/

GnmExpr const *
gnm_expr_copy (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return gnm_expr_new_binary
			(gnm_expr_copy (expr->binary.value_a),
			 GNM_EXPR_GET_OPER (expr),
			 gnm_expr_copy (expr->binary.value_b));

	case GNM_EXPR_OP_ANY_UNARY:
		return gnm_expr_new_unary
			(GNM_EXPR_GET_OPER (expr),
			 gnm_expr_copy (expr->unary.value));

	case GNM_EXPR_OP_FUNCALL: {
		GnmExprConstPtr *argv =
			g_new (GnmExprConstPtr, expr->func.argc);
		int i;

		for (i = 0; i < expr->func.argc; i++)
			argv[i] = gnm_expr_copy (expr->func.argv[i]);

		return gnm_expr_new_funcallv
			(expr->func.func,
			 expr->func.argc,
			 argv);
	}

	case GNM_EXPR_OP_NAME:
		return gnm_expr_new_name
			(expr->name.name,
			 expr->name.optional_scope,
			 expr->name.optional_wb_scope);

	case GNM_EXPR_OP_CONSTANT:
		return gnm_expr_new_constant
			(value_dup (expr->constant.value));

	case GNM_EXPR_OP_CELLREF:
		return gnm_expr_new_cellref (&expr->cellref.ref);

	case GNM_EXPR_OP_ARRAY_CORNER:
		return gnm_expr_new_array_corner
			(expr->array_corner.cols, expr->array_corner.rows,
			 gnm_expr_copy (expr->array_corner.expr));

	case GNM_EXPR_OP_ARRAY_ELEM:
		return gnm_expr_new_array_elem
			(expr->array_elem.x,
			 expr->array_elem.y);

	case GNM_EXPR_OP_SET: {
		GnmExprConstPtr *argv =
			g_new (GnmExprConstPtr, expr->set.argc);
		int i;

		for (i = 0; i < expr->set.argc; i++)
			argv[i] = gnm_expr_copy (expr->set.argv[i]);

		return gnm_expr_new_setv
			(expr->set.argc,
			 argv);
	}

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		break;
#endif
	}
}

/*
 * gnm_expr_free:
 */
void
gnm_expr_free (GnmExpr const *expr)
{
	g_return_if_fail (expr != NULL);

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		gnm_expr_free (expr->binary.value_a);
		gnm_expr_free (expr->binary.value_b);
		CHUNK_FREE (expression_pool_small, (gpointer)expr);
		break;

	case GNM_EXPR_OP_FUNCALL: {
		int i;

		for (i = 0; i < expr->func.argc; i++)
			gnm_expr_free (expr->func.argv[i]);
		g_free (expr->func.argv);
		gnm_func_unref (expr->func.func);
		CHUNK_FREE (expression_pool_small, (gpointer)expr);
		break;
	}

	case GNM_EXPR_OP_NAME:
		expr_name_unref (expr->name.name);
		CHUNK_FREE (expression_pool_big, (gpointer)expr);
		break;

	case GNM_EXPR_OP_CONSTANT:
		value_release ((GnmValue *)expr->constant.value);
		CHUNK_FREE (expression_pool_small, (gpointer)expr);
		break;

	case GNM_EXPR_OP_CELLREF:
		CHUNK_FREE (expression_pool_big, (gpointer)expr);
		break;

	case GNM_EXPR_OP_ANY_UNARY:
		gnm_expr_free (expr->unary.value);
		CHUNK_FREE (expression_pool_small, (gpointer)expr);
		break;

	case GNM_EXPR_OP_ARRAY_CORNER:
		if (expr->array_corner.value)
			value_release (expr->array_corner.value);
		gnm_expr_free (expr->array_corner.expr);
		CHUNK_FREE (expression_pool_big, (gpointer)expr);
		break;

	case GNM_EXPR_OP_ARRAY_ELEM:
		CHUNK_FREE (expression_pool_small, (gpointer)expr);
		break;

	case GNM_EXPR_OP_SET: {
		int i;

		for (i = 0; i < expr->set.argc; i++)
			gnm_expr_free (expr->set.argv[i]);
		g_free (expr->set.argv);
		CHUNK_FREE (expression_pool_small, (gpointer)expr);
		break;
	}

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		break;
#endif
	}
}

/**
 * gnm_expr_equal : Returns TRUE if the supplied expressions are exactly the
 *   same.  No eval position is used to see if they are effectively the same.
 *   Named expressions must refer the same name, having equivalent names is
 *   insufficeient.
 */
static gboolean
gnm_expr_equal (GnmExpr const *a, GnmExpr const *b)
{
	if (a == b)
		return TRUE;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (GNM_EXPR_GET_OPER (a) != GNM_EXPR_GET_OPER (b))
		return FALSE;

	switch (GNM_EXPR_GET_OPER (a)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return	gnm_expr_equal (a->binary.value_a, b->binary.value_a) &&
			gnm_expr_equal (a->binary.value_b, b->binary.value_b);

	case GNM_EXPR_OP_ANY_UNARY:
		return gnm_expr_equal (a->unary.value, b->unary.value);

	case GNM_EXPR_OP_FUNCALL: {
		int i;

		if (a->func.func != b->func.func ||
		    a->func.argc != b->func.argc)
			return FALSE;

		for (i = 0; i < a->func.argc; i++)
			if (!gnm_expr_equal (a->func.argv[i], b->func.argv[i]))
				return FALSE;
		return TRUE;
	}

	case GNM_EXPR_OP_NAME:
		return	a->name.name == b->name.name &&
			a->name.optional_scope == b->name.optional_scope &&
			a->name.optional_wb_scope == b->name.optional_wb_scope;

	case GNM_EXPR_OP_CELLREF:
		return gnm_cellref_equal (&a->cellref.ref, &b->cellref.ref);

	case GNM_EXPR_OP_CONSTANT:
		return value_equal (a->constant.value, b->constant.value);

	case GNM_EXPR_OP_ARRAY_CORNER: {
		GnmExprArrayCorner const *aa = &a->array_corner;
		GnmExprArrayCorner const *ab = &b->array_corner;

		return	aa->cols == ab->cols &&
			aa->rows == ab->rows &&
			gnm_expr_equal (aa->expr, ab->expr);
	}
	case GNM_EXPR_OP_ARRAY_ELEM: {
		GnmExprArrayElem const *aa = &a->array_elem;
		GnmExprArrayElem const *ab = &b->array_elem;
		return	aa->x == ab->x && aa->y == ab->y;
	}

	case GNM_EXPR_OP_SET: {
		int i;

		if (a->set.argc != b->set.argc)
			return FALSE;

		for (i = 0; i < a->set.argc; i++)
			if (!gnm_expr_equal (a->set.argv[i], b->set.argv[i]))
				return FALSE;
		return TRUE;
	}
	}

	return FALSE;
}

static GnmCell *
array_elem_get_corner (GnmExprArrayElem const *elem,
		       Sheet const *sheet, GnmCellPos const *pos)
{
	GnmCell *corner = sheet_cell_get (sheet,
		pos->col - elem->x, pos->row - elem->y);

	/* Sanity check incase the corner gets removed for some reason */
	g_return_val_if_fail (corner != NULL, NULL);
	g_return_val_if_fail (gnm_cell_has_expr (corner), NULL);
	g_return_val_if_fail (corner->base.texpr != (void *)0xdeadbeef, NULL);
	g_return_val_if_fail (IS_GNM_EXPR_TOP (corner->base.texpr), NULL);

	return corner;
}

static gboolean
gnm_expr_extract_ref (GnmRangeRef *res, GnmExpr const *expr,
		      GnmEvalPos const *pos, GnmExprEvalFlags flags)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_FUNCALL : {
		gboolean failed = TRUE;
		GnmValue *v;
		GnmFuncEvalInfo ei;

		ei.pos = pos;
		ei.func_call = &expr->func;
		v = function_call_with_exprs (&ei, flags);

		if (v != NULL) {
			if (v->type == VALUE_CELLRANGE) {
				*res = v->v_range.cell;
				failed = FALSE;
			}
			value_release (v);
		}
		return failed;
	}

	case GNM_EXPR_OP_CELLREF :
		res->a = expr->cellref.ref;
		res->b = expr->cellref.ref;
		return FALSE;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;
		if (v->type == VALUE_CELLRANGE) {
			*res = v->v_range.cell;
			return FALSE;
		}
		return TRUE;
	}

	case GNM_EXPR_OP_NAME:
		if (!expr->name.name->active)
			return TRUE;
		return gnm_expr_extract_ref (res, expr->name.name->texpr->expr,
					     pos, flags);
	default :
		break;
	}
	return TRUE;
}

static inline GnmValue *
handle_empty (GnmValue *res, GnmExprEvalFlags flags)
{
	if (res == NULL)
		return (flags & GNM_EXPR_EVAL_PERMIT_EMPTY)
		    ? NULL : value_new_int (0);

	if (VALUE_IS_EMPTY (res)) {
		value_release (res);
		return (flags & GNM_EXPR_EVAL_PERMIT_EMPTY)
		    ? NULL : value_new_int (0);
	}
	return res;
}

/**
 * value_intersection :
 * @v   : a VALUE_CELLRANGE or VALUE_ARRAY
 * @pos :
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
 *     NULL if there is no intersection
 * Returns the upper left corner of an array.
 **/
static GnmValue *
value_intersection (GnmValue *v, GnmEvalPos const *pos)
{
	GnmValue *res = NULL;
	GnmRange r;
	Sheet *start_sheet, *end_sheet;
	gboolean found = FALSE;

	if (v->type == VALUE_ARRAY) {
		res = (v->v_array.x == 0 || v->v_array.y == 0)
			? value_new_error_VALUE (NULL)
			: value_dup (v->v_array.vals[0][0]);
		value_release (v);
		return res;
	}

	/* inverted ranges */
	gnm_rangeref_normalize (&v->v_range.cell, pos, &start_sheet, &end_sheet, &r);
	value_release (v);

	if (start_sheet == end_sheet || end_sheet == NULL) {
		int col = pos->eval.col;
		int row = pos->eval.row;

		if (r.start.row == r.end.row) {
			if (r.start.col <= col && col <= r.end.col) {
				row = r.start.row;
				found = TRUE;
			} else if (r.start.col == r.end.col) {
				col = r.start.col;
				row = r.start.row;
				found = TRUE;
			}
		} else if (r.start.col == r.end.col) {
			if (r.start.row <= row && row <= r.end.row) {
				col = r.start.col;
				found = TRUE;
			}
		}
		if (found) {
			GnmCell *cell = sheet_cell_get (
				eval_sheet (start_sheet, pos->sheet),
				col, row);
			if (cell == NULL)
				return value_new_empty ();
			gnm_cell_eval (cell);
			return value_dup (cell->value);
		}
	}

	return value_new_error_VALUE (pos);
}

static GnmValue *
bin_arith (GnmExpr const *expr, GnmEvalPos const *ep,
	   GnmValue const *a, GnmValue const *b)
{
	gnm_float const va = value_get_as_float (a);
	gnm_float const vb = value_get_as_float (b);
	gnm_float res;

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_ADD:
		res = va + vb;
		break;

	case GNM_EXPR_OP_SUB:
		res = va - vb;
		break;

	case GNM_EXPR_OP_MULT:
		res = va * vb;
		break;

	case GNM_EXPR_OP_DIV:
		if (vb == 0.0)
			return value_new_error_DIV0 (ep);
		res = va / vb;
		break;

	case GNM_EXPR_OP_EXP:
		if ((va == 0 && vb <= 0) || (va < 0 && vb != (int)vb))
			return value_new_error_NUM (ep);

		res = gnm_pow (va, vb);
		break;

	default:
		g_assert_not_reached ();
	}

	if (gnm_finite (res))
		return value_new_float (res);
	else
		return value_new_error_NUM (ep);
}

static GnmValue *
bin_cmp (GnmExprOp op, GnmValDiff comp, GnmEvalPos const *ep)
{
	if (comp == TYPE_MISMATCH) {
		/* TODO TODO TODO : Make error more informative
		 *    regarding what is comparing to what
		 */
		/* For equality comparisons even errors are ok */
		if (op == GNM_EXPR_OP_EQUAL)
			return value_new_bool (FALSE);
		if (op == GNM_EXPR_OP_NOT_EQUAL)
			return value_new_bool (TRUE);

		return value_new_error_VALUE (ep);
	}

	switch (op) {
	case GNM_EXPR_OP_EQUAL:     return value_new_bool (comp == IS_EQUAL);
	case GNM_EXPR_OP_GT:	    return value_new_bool (comp == IS_GREATER);
	case GNM_EXPR_OP_LT:	    return value_new_bool (comp == IS_LESS);
	case GNM_EXPR_OP_NOT_EQUAL: return value_new_bool (comp != IS_EQUAL);
	case GNM_EXPR_OP_LTE:	    return value_new_bool (comp != IS_GREATER);
	case GNM_EXPR_OP_GTE:	    return value_new_bool (comp != IS_LESS);

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
#endif
	}
	return value_new_error (ep, _("Internal type error"));
}

static GnmValue *
cb_bin_cmp (GnmEvalPos const *ep, GnmValue const *a, GnmValue const *b,
	    GnmExpr const *expr)
{
	if (a != NULL && VALUE_IS_ERROR (a))
		return value_dup (a);
	if (b != NULL && VALUE_IS_ERROR (b))
		return value_dup (b);
	return bin_cmp (GNM_EXPR_GET_OPER (expr), value_compare (a, b, FALSE), ep);
}

static GnmValue *
cb_bin_arith (GnmEvalPos const *ep, GnmValue const *a, GnmValue const *b,
	      GnmExpr const *expr)
{
	GnmValue *res, *va, *vb;

	if (a != NULL && VALUE_IS_ERROR (a))
		return value_dup (a);
	if (b != NULL && VALUE_IS_ERROR (b))
		return value_dup (b);
	if (VALUE_IS_EMPTY (a))
		a = va = (GnmValue *)value_zero;
	else if (VALUE_IS_STRING (a)) {
		va = format_match_number (a->v_str.val->str, NULL,
			workbook_date_conv (ep->sheet->workbook));
		if (va == NULL)
			return value_new_error_VALUE (ep);
	} else if (!VALUE_IS_NUMBER (a))
		return value_new_error_VALUE (ep);
	else
		va = (GnmValue *)a;
	if (VALUE_IS_EMPTY (b))
		b = vb = (GnmValue *)value_zero;
	else if (VALUE_IS_STRING (b)) {
		vb = format_match_number (b->v_str.val->str, NULL,
			workbook_date_conv (ep->sheet->workbook));
		if (vb == NULL) {
			if (va != a)
				value_release (va);
			return value_new_error_VALUE (ep);
		}
	} else if (!VALUE_IS_NUMBER (b)) {
		if (va != a)
			value_release (va);
		return value_new_error_VALUE (ep);
	} else
		vb = (GnmValue *)b;

	res = bin_arith (expr, ep, va, vb);
	if (va != a)
		value_release (va);
	if (vb != b)
		value_release (vb);
	return res;
}

static GnmValue *
cb_bin_cat (GnmEvalPos const *ep, GnmValue const *a, GnmValue const *b,
	    GnmExpr const *expr)
{
	if (a != NULL && VALUE_IS_ERROR (a))
		return value_dup (a);
	if (b != NULL && VALUE_IS_ERROR (b))
		return value_dup (b);
	if (a == NULL) {
		if (b != NULL)
			return value_new_string (value_peek_string (b));
		else
			return value_new_string ("");
	} else if (b == NULL)
		return value_new_string (value_peek_string (a));
	else {
		char *tmp = g_strconcat (value_peek_string (a),
					 value_peek_string (b), NULL);
		return value_new_string_nocopy (tmp);
	}
}

typedef GnmValue *(*BinOpImplicitIteratorFunc) (GnmEvalPos const *ep,
						GnmValue const *a,
						GnmValue const *b,
						gpointer user_data);
typedef struct {
	GnmEvalPos const *ep;
	GnmValue *res;
	GnmValue const *a, *b;
	BinOpImplicitIteratorFunc	func;

	/* multiply by 0 in unused dimensions.
	 * this is simpler than lots of conditions
	 *	state->use_x.a ? x : 0
	 **/
	struct {
		int a, b;
	} x, y;
	gpointer	user_data;
} BinOpImplicitIteratorState;

static GnmValue *
cb_implicit_iter_a_and_b (GnmValueIter const *v_iter,
			  BinOpImplicitIteratorState const *state)
{
	state->res->v_array.vals [v_iter->x][v_iter->y] =
		(*state->func) (v_iter->ep,
			value_area_get_x_y (state->a,
				state->x.a * v_iter->x,
				state->y.a * v_iter->y, v_iter->ep),
			value_area_get_x_y (state->b,
				state->x.b * v_iter->x,
				state->y.b * v_iter->y, v_iter->ep),
			state->user_data);
	return NULL;
}
static GnmValue *
cb_implicit_iter_a_to_scalar_b (GnmValueIter const *v_iter,
				BinOpImplicitIteratorState const *state)
{
	state->res->v_array.vals [v_iter->x][v_iter->y] =
		(*state->func) (v_iter->ep,
			v_iter->v, state->b, state->user_data);
	return NULL;
}

/* This is only triggered if something returns an array or a range which can
 * only happen if we are in array eval mode. */
static GnmValue *
bin_array_iter_a (GnmEvalPos const *ep,
		  GnmValue *a, GnmValue *b,
		  BinOpImplicitIteratorFunc func,
		  GnmExpr const *expr)
{
	BinOpImplicitIteratorState iter_info;

	/* a must be a cellrange or array, it can not be NULL */
	iter_info.ep   = ep;
	iter_info.func = func;
	iter_info.user_data = (gpointer) expr;
	iter_info.a = a;
	iter_info.b = b;

	/* matrix to matrix
	 * Use matching positions unless the dimension is singular, in which
	 * case use the zero item
	 * res[x][y] = f(a[singular.a.x ? 0 : x, b[singular.b.x ? 0 : x)
	 *
	 * If both items have non-singular sizes for
	 * the same dimension use the min size (see samples/array.xls) */
	if (b != NULL &&
	    (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY)) {
		int sa, sb, w = 1, h = 1;

		sa = value_area_get_width  (a, ep);
		sb = value_area_get_width  (b, ep);
		if ((iter_info.x.a = (sa == 1) ? 0 : 1))
			w = sa;
		if ((iter_info.x.b = (sb == 1) ? 0 : 1) && (w > sb || w == 1))
			w = sb;

		sa = value_area_get_height  (a, ep);
		sb = value_area_get_height  (b, ep);
		if ((iter_info.y.a = (sa == 1) ? 0 : 1))
			h = sa;
		if ((iter_info.y.b = (sb == 1) ? 0 : 1) && (h > sb || h == 1))
			h = sb;

		iter_info.res = value_new_array_empty (w, h);
		value_area_foreach (iter_info.res, ep, CELL_ITER_ALL,
			(GnmValueIterFunc) cb_implicit_iter_a_and_b, &iter_info);
	} else {
		iter_info.res = value_new_array_empty (
			value_area_get_width  (a, ep),
			value_area_get_height (a, ep));
		value_area_foreach (a, ep, CELL_ITER_ALL,
			(GnmValueIterFunc) cb_implicit_iter_a_to_scalar_b, &iter_info);
	}

	value_release (a);
	if (b != NULL)
		value_release (b);
	return iter_info.res;
}

static GnmValue *
cb_implicit_iter_b_to_scalar_a (GnmValueIter const *v_iter,
				BinOpImplicitIteratorState const *state)
{
	state->res->v_array.vals [v_iter->x][v_iter->y] =
		(*state->func) (v_iter->ep,
			state->a, v_iter->v, state->user_data);
	return NULL;
}
static GnmValue *
bin_array_iter_b (GnmEvalPos const *ep,
		  GnmValue *a, GnmValue *b,
		  BinOpImplicitIteratorFunc func,
		  GnmExpr const *expr)
{
	BinOpImplicitIteratorState iter_info;

	iter_info.func = func;
	iter_info.user_data = (gpointer) expr;
	iter_info.a = a;
	iter_info.b = b;

	/* b must be a cellrange or array, it can not be NULL */
	iter_info.res = value_new_array_empty (
		value_area_get_width  (b, ep),
		value_area_get_height (b, ep));
	value_area_foreach (b, ep, CELL_ITER_ALL,
		(GnmValueIterFunc) cb_implicit_iter_b_to_scalar_a, &iter_info);
	if (a != NULL)
		value_release (a);
	value_release (b);

	return iter_info.res;
}

static GnmValue *
negate_value (GnmValue const *v)
{
	if (VALUE_IS_NUMBER (v)) {
		GnmValue *tmp = value_new_float (0 - value_get_as_float (v));
		value_set_fmt (tmp, VALUE_FMT (v));
		return tmp;
	} else
		return NULL;
}

static GnmValue *
cb_iter_unary_neg (GnmValueIter const *v_iter, GnmValue *res)
{
	GnmValue const *v = v_iter->v;
	GnmValue *tmp = NULL;

	if (VALUE_IS_EMPTY (v))
		tmp = value_new_int (0);
	else if (VALUE_IS_ERROR (v))
		tmp = value_dup (v);
	else if (VALUE_IS_STRING (v)) {
		GnmValue *conv = format_match_number (
			value_peek_string (v), NULL,
			workbook_date_conv (v_iter->ep->sheet->workbook));
		if (conv != NULL) {
			tmp = negate_value (conv);
			value_release (conv);
		}
	} else {
		/* BOOL goes here.  */
		tmp = negate_value (v);
	}

	if (NULL == tmp)
		tmp = value_new_error_VALUE (v_iter->ep);
	res->v_array.vals[v_iter->x][v_iter->y] = tmp;
	return NULL;
}

static GnmValue *
cb_iter_percentage (GnmValueIter const *v_iter, GnmValue *res)
{
	GnmValue const *v = v_iter->v;
	GnmValue *tmp;

	if (VALUE_IS_EMPTY (v))
		tmp = value_new_int (0);
	else if (VALUE_IS_ERROR (v))
		tmp = value_dup (v);
	else {
		GnmValue *conv = NULL;
		if (VALUE_IS_STRING (v)) {
			conv = format_match_number (
				value_peek_string (v), NULL,
				workbook_date_conv (v_iter->ep->sheet->workbook));
			if (conv != NULL)
				v = conv;
		}

		if (VALUE_IS_NUMBER (v)){
			tmp = value_new_float (value_get_as_float (v) / 100);
			value_set_fmt (tmp, go_format_default_percentage ());
		} else
			tmp = value_new_error_VALUE (v_iter->ep);

		if (conv != NULL)
			value_release (conv);
	}

	res->v_array.vals[v_iter->x][v_iter->y] = tmp;
	return NULL;
}

static GnmValue *
gnm_expr_range_op (GnmExpr const *expr, GnmEvalPos const *ep,
		   GnmExprEvalFlags flags)
{
	GnmRangeRef a_ref, b_ref;
	GnmRange a_range, b_range, res_range;
	Sheet *a_start, *a_end, *b_start, *b_end;
	GnmValue *res = NULL;

	if (gnm_expr_extract_ref (&a_ref, expr->binary.value_a, ep, flags) ||
	    gnm_expr_extract_ref (&b_ref, expr->binary.value_b, ep, flags))
		return value_new_error_REF (ep);

	gnm_rangeref_normalize (&a_ref, ep, &a_start, &a_end, &a_range);
	gnm_rangeref_normalize (&b_ref, ep, &b_start, &b_end, &b_range);

	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_INTERSECT)
		res_range = range_union (&a_range, &b_range);
	else if (!range_intersection  (&res_range, &a_range, &b_range))
		return value_new_error_NULL (ep);

	res = value_new_cellrange_r (a_start, &res_range);
	dependent_add_dynamic_dep (ep->dep, &res->v_range.cell);
	if (!(flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)) {
		res = value_intersection (res, ep);
		return (res != NULL)
			? handle_empty (res, flags)
			: value_new_error_VALUE (ep);
	}
	return res;
}

/**
 * gnm_expr_eval :
 * @expr :
 * @ep   :
 * @flags:
 *
 * if GNM_EXPR_EVAL_PERMIT_EMPTY is not set then return int(0) if the
 * expression returns empty, or the  value of an unused cell.
 **/
GnmValue *
gnm_expr_eval (GnmExpr const *expr, GnmEvalPos const *pos,
	       GnmExprEvalFlags flags)
{
	GnmValue *res = NULL, *a = NULL, *b = NULL;

	g_return_val_if_fail (expr != NULL, handle_empty (NULL, flags));
	g_return_val_if_fail (pos != NULL, handle_empty (NULL, flags));

	switch (GNM_EXPR_GET_OPER (expr)){
	case GNM_EXPR_OP_EQUAL:
	case GNM_EXPR_OP_NOT_EQUAL:
	case GNM_EXPR_OP_GT:
	case GNM_EXPR_OP_GTE:
	case GNM_EXPR_OP_LT:
	case GNM_EXPR_OP_LTE:
		flags |= GNM_EXPR_EVAL_PERMIT_EMPTY;

		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (a != NULL) {
			if (VALUE_IS_ERROR (a))
				return a;
			if (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)
				return bin_array_iter_a (pos, a,
					gnm_expr_eval (expr->binary.value_b, pos, flags),
					(BinOpImplicitIteratorFunc) cb_bin_cmp,
					expr);
		}

		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (b != NULL) {
			if (VALUE_IS_ERROR (b)) {
				if (a != NULL)
					value_release (a);
				return b;
			}
			if (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY)
				return bin_array_iter_b (pos, a, b,
					(BinOpImplicitIteratorFunc) cb_bin_cmp,
					expr);
		}

		res = bin_cmp (GNM_EXPR_GET_OPER (expr), value_compare (a, b, FALSE), pos);
		if (a != NULL)
			value_release (a);
		if (b != NULL)
			value_release (b);
		return res;

	case GNM_EXPR_OP_ADD:
	case GNM_EXPR_OP_SUB:
	case GNM_EXPR_OP_MULT:
	case GNM_EXPR_OP_DIV:
	case GNM_EXPR_OP_EXP:
		/*
		 * Priority
		 * 1) Error from A
		 * 2) #!VALUE error if A is not a number
		 * 3) Error from B
		 * 4) #!VALUE error if B is not a number
		 * 5) result of operation, or error specific to the operation
		 */

		/* Guarantees value != NULL */
		flags &= ~GNM_EXPR_EVAL_PERMIT_EMPTY;

		/* 1) Error from A */
		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (VALUE_IS_ERROR (a))
			return value_error_set_pos (&a->v_err, pos);

		/* 2) #!VALUE error if A is not a number */
		if (VALUE_IS_STRING (a)) {
			GnmValue *tmp = format_match_number (a->v_str.val->str, NULL,
				workbook_date_conv (pos->sheet->workbook));

			value_release (a);
			if (tmp == NULL)
				return value_new_error_VALUE (pos);
			a = tmp;
		} else if (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY) {
			b = gnm_expr_eval (expr->binary.value_b, pos, flags);
			if (VALUE_IS_STRING (b)) {
				res = format_match_number (b->v_str.val->str, NULL,
					workbook_date_conv (pos->sheet->workbook));
				value_release (b);
				b = (res == NULL) ? value_new_error_VALUE (pos) : res;
			}
			return bin_array_iter_a (pos, a, b,
				(BinOpImplicitIteratorFunc) cb_bin_arith,
				expr);
		} else if (!VALUE_IS_NUMBER (a)) {
			value_release (a);
			return value_new_error_VALUE (pos);
		}

		/* 3) Error from B */
		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (VALUE_IS_ERROR (b)) {
			value_release (a);
			return value_error_set_pos (&b->v_err, pos);
		}

		/* 4) #!VALUE error if B is not a number */
		if (VALUE_IS_STRING (b)) {
			GnmValue *tmp = format_match_number (b->v_str.val->str, NULL,
				workbook_date_conv (pos->sheet->workbook));

			value_release (b);
			if (tmp == NULL) {
				value_release (a);
				return value_new_error_VALUE (pos);
			}
			b = tmp;
		} else if (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY)
			return bin_array_iter_b (pos, a, b,
				(BinOpImplicitIteratorFunc) cb_bin_arith,
				expr);
		else if (!VALUE_IS_NUMBER (b)) {
			value_release (a);
			value_release (b);
			return value_new_error_VALUE (pos);
		}

		res = bin_arith (expr, pos, a, b);
		value_release (a);
		value_release (b);
		return res;

	case GNM_EXPR_OP_PERCENTAGE:
	case GNM_EXPR_OP_UNARY_NEG:
	case GNM_EXPR_OP_UNARY_PLUS:
		/* Guarantees value != NULL */
		flags &= ~GNM_EXPR_EVAL_PERMIT_EMPTY;

		a = gnm_expr_eval (expr->unary.value, pos, flags);
		if (VALUE_IS_ERROR (a))
			return a;
		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_UNARY_PLUS)
			return a;

		/* 2) #!VALUE error if A is not a number */
		if (VALUE_IS_STRING (a)) {
			GnmValue *tmp = format_match_number (a->v_str.val->str, NULL,
				workbook_date_conv (pos->sheet->workbook));

			value_release (a);
			if (tmp == NULL)
				return value_new_error_VALUE (pos);
			a = tmp;
		} else if (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY) {
			res = value_new_array_empty (
				value_area_get_width  (a, pos),
				value_area_get_height (a, pos));
			value_area_foreach (a, pos, CELL_ITER_ALL,
				(GnmValueIterFunc) ((GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_UNARY_NEG)
					? cb_iter_unary_neg : cb_iter_percentage),
				res);
			value_release (a);
			return res;
		}
		if (!VALUE_IS_NUMBER (a))
			res = value_new_error_VALUE (pos);
		else if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_UNARY_NEG)
			res = negate_value (a);
		else {
			res = value_new_float (value_get_as_float (a) / 100);
			VALUE_FMT (res) = go_format_default_percentage ();
			go_format_ref (VALUE_FMT (res));
		}
		value_release (a);
		return res;

	case GNM_EXPR_OP_CAT:
		flags |= GNM_EXPR_EVAL_PERMIT_EMPTY;
		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (a != NULL) {
			if (VALUE_IS_ERROR (a))
				return a;
			if (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)
				return bin_array_iter_a (pos, a,
					gnm_expr_eval (expr->binary.value_b, pos, flags),
					(BinOpImplicitIteratorFunc) cb_bin_cat,
					expr);
		}
		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (b != NULL) {
			if (VALUE_IS_ERROR (b)) {
				if (a != NULL)
					value_release (a);
				return b;
			}
			if (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY)
				return bin_array_iter_b (pos, a, b,
					(BinOpImplicitIteratorFunc) cb_bin_cat,
					expr);
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
			res = value_new_string_nocopy (tmp);
			value_release (a);
			value_release (b);
		}
		return res;

	case GNM_EXPR_OP_FUNCALL: {
		GnmFuncEvalInfo ei;
		ei.pos = pos;
		ei.func_call = &expr->func;
		res = function_call_with_exprs (&ei, flags);
		if (res == NULL)
			return (flags & GNM_EXPR_EVAL_PERMIT_EMPTY)
			    ? NULL : value_new_int (0);
		if (res->type == VALUE_CELLRANGE) {
			dependent_add_dynamic_dep (pos->dep, &res->v_range.cell);
			if (!(flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)) {
				res = value_intersection (res, pos);
				return (res != NULL)
					? handle_empty (res, flags)
					: value_new_error_VALUE (pos);
			}
			return res;
		}
		if (res->type == VALUE_ARRAY &&
		    !(flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)) {
			a = value_dup (res->v_array.vals[0][0]);
			value_release (res);
			return a;
		}
		return res;
	}

	case GNM_EXPR_OP_NAME:
		if (expr->name.name->active)
			return handle_empty (expr_name_eval (expr->name.name, pos, flags), flags);
		return value_new_error_REF (pos);

	case GNM_EXPR_OP_CELLREF: {
		GnmCell *cell;
		GnmCellPos dest;

		gnm_cellpos_init_cellref (&dest, &expr->cellref.ref, &pos->eval);

		cell = sheet_cell_get (eval_sheet (expr->cellref.ref.sheet, pos->sheet),
			dest.col, dest.row);
		if (cell == NULL)
			return handle_empty (NULL, flags);

		gnm_cell_eval (cell);

		return handle_empty (value_dup (cell->value), flags);
	}

	case GNM_EXPR_OP_CONSTANT:
		res = value_dup (expr->constant.value);
		if (res->type == VALUE_CELLRANGE || res->type == VALUE_ARRAY) {
			if (flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)
				return res;
			res = value_intersection (res, pos);
			return (res != NULL)
				? handle_empty (res, flags)
				: value_new_error_VALUE (pos);
		}
		return handle_empty (res, flags);

	case GNM_EXPR_OP_ARRAY_CORNER: {
		GnmEvalPos range_pos = *pos;
		range_pos.array = &expr->array_corner;

		a = gnm_expr_eval (expr->array_corner.expr, &range_pos,
			flags | GNM_EXPR_EVAL_PERMIT_NON_SCALAR);

		if (expr->array_corner.value)
			value_release (expr->array_corner.value);

		/* Store real result (cast away const)*/
		((GnmExpr*)expr)->array_corner.value = a;

		if (a != NULL &&
		    (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)) {
			if (value_area_get_width (a, pos) <= 0 ||
			    value_area_get_height (a, pos) <= 0)
				return value_new_error_NA (pos);
			a = (GnmValue *)value_area_get_x_y (a, 0, 0, pos);
		}
		return handle_empty ((a != NULL) ? value_dup (a) : NULL, flags);
	}

	case GNM_EXPR_OP_ARRAY_ELEM: {
		/* The upper left corner manages the recalc of the expr */
		GnmCell *corner = array_elem_get_corner (&expr->array_elem,
			pos->sheet, &pos->eval);
		if (corner == NULL)
			return handle_empty (NULL, flags);

		gnm_cell_eval (corner);
		a = corner->base.texpr->expr->array_corner.value;
		if (a == NULL)
			return handle_empty (NULL, flags);

		if ((a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)) {
			int const num_x = value_area_get_width (a, pos);
			int const num_y = value_area_get_height (a, pos);
			int x = expr->array_elem.x;
			int y = expr->array_elem.y;

			/* Evaluate relative to the upper left corner */
			GnmEvalPos tmp_ep = *pos;
			tmp_ep.eval.col -= x;
			tmp_ep.eval.row -= y;

			/* If the src array is 1 element wide or tall we wrap */
			if (x >= 1 && num_x == 1)
				x = 0;
			if (y >= 1 && num_y == 1)
				y = 0;
			if (x >= num_x || y >= num_y)
				return value_new_error_NA (pos);

			a = (GnmValue *)value_area_get_x_y (a, x, y, &tmp_ep);
		}

		return handle_empty ((a != NULL) ? value_dup (a) : NULL, flags);
	}
	case GNM_EXPR_OP_SET:
		if (flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR) {
			int i;
			int argc = expr->set.argc;

			res = value_new_array_non_init (1, expr->set.argc);
			res->v_array.vals[0] = g_new (GnmValue *, expr->set.argc);
			for (i = 0; i < argc; i++)
				res->v_array.vals[0][i] = gnm_expr_eval (
					expr->set.argv[i], pos,
					GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
			return res;
		}
		return value_new_error_VALUE (pos);

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		return gnm_expr_range_op (expr, pos, flags);
	}

	return value_new_error (pos, _("Unknown evaluation error"));
}

static void
gnm_expr_list_as_string (int argc, GnmExprConstPtr const *argv,
			 GnmConventionsOut *out);


/*
 * Converts a parsed tree into its string representation
 * assuming that we are evaluating at col, row
 *
 * This routine is pretty simple: it walks the GnmExpr and
 * appends a string representation to the target.
 */
static void
do_expr_as_string (GnmExpr const *expr, int paren_level,
		   GnmConventionsOut *out)
{
	static struct {
		char const name[4];
		guint8 prec;			 /* Precedences -- should match parser.y  */
		guint8 assoc_left, assoc_right;  /* 0: no, 1: yes.  */
		guint8 is_prefix;                /* for unary operators */
	} const operations[] = {
		{ "=",  1, 1, 0, 0 },
		{ ">",  1, 1, 0, 0 },
		{ "<",  1, 1, 0, 0 },
		{ ">=", 1, 1, 0, 0 },
		{ "<=", 1, 1, 0, 0 },
		{ "<>", 1, 1, 0, 0 },
		{ "+",  3, 1, 0, 0 },
		{ "-",  3, 1, 0, 0 },
		{ "*",  4, 1, 0, 0 },
		{ "/",  4, 1, 0, 0 },
		{ "^",  5, 0, 1, 0 },
		{ "&",  2, 1, 0, 0 },
		{ "",   0, 0, 0, 0 }, /* Funcall  */
		{ "",   0, 0, 0, 0 }, /* Name     */
		{ "",   0, 0, 0, 0 }, /* Constant */
		{ "",   0, 0, 0, 0 }, /* Var      */
		{ "-",  7, 0, 0, 1 }, /* Unary -  */
		{ "+",  7, 0, 0, 1 }, /* Unary +  */
		{ "%",  6, 0, 0, 0 }, /* Percentage (NOT MODULO) */
		{ "",   0, 0, 0, 0 }, /* ArrayCorner    */
		{ "",   0, 0, 0, 0 }, /* ArrayElem */
		{ "",   0, 0, 0, 0 }, /* Set       */
		{ ":",  9, 1, 0, 0 }, /* Range Ctor   */
		{ " ",  8, 1, 0, 0 }  /* Intersection */
	};
	GnmExprOp const op = GNM_EXPR_GET_OPER (expr);
	GString *target = out->accum;

	switch (op) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY: {
		char const *opname = operations[op].name;
		int prec = operations[op].prec;
		gboolean need_par = (prec <= paren_level);
		size_t prelen = target->len;

		if (need_par) g_string_append_c (target, '(');
		do_expr_as_string (expr->binary.value_a,
			prec - operations[op].assoc_left, out);

		/*
		 * Avoid getting "-2^2".  We want to make sure files do not contain
		 * that construct as we might later change precedence.
		 *
		 * Always produce either "-(2^2)" or "(-2)^2".
		 */
		if (op == GNM_EXPR_OP_EXP &&
		    (target->str[prelen] == '-' || target->str[prelen] == '+')) {
			g_string_insert_c (target, prelen, '(');
			g_string_append_c (target, ')');
		}

		/* Instead of this we ought to move the whole operations
		   table into the conventions.  */
		if (op == GNM_EXPR_OP_INTERSECT)
			g_string_append_unichar (target, out->convs->intersection_char);
		else
			g_string_append (target, opname);

		do_expr_as_string (expr->binary.value_b,
			prec - operations[op].assoc_right, out);
		if (need_par) g_string_append_c (target, ')');
		return;
	}

	case GNM_EXPR_OP_ANY_UNARY: {
		char const *opname = operations[op].name;
		int prec = operations[op].prec;
		gboolean is_prefix = operations[op].is_prefix;
		gboolean need_par = (prec <= paren_level);

		if (need_par) g_string_append_c (target, '(');
		if (is_prefix) g_string_append (target, opname);
		do_expr_as_string (expr->unary.value, prec, out);
		if (!is_prefix) g_string_append (target, opname);
		if (need_par) g_string_append_c (target, ')');
		return;
	}

	case GNM_EXPR_OP_FUNCALL: {
		GnmExprFunction const *func = &expr->func;
		char const *name = gnm_func_get_name (func->func);

		g_string_append (target, name);
		/* FIXME: possibly a space here.  */
		gnm_expr_list_as_string (func->argc, func->argv, out);
		return;
	}

	case GNM_EXPR_OP_NAME:
		out->convs->output.name (out, &expr->name);
		return;

	case GNM_EXPR_OP_CELLREF:
		out->convs->output.cell_ref (out, &expr->cellref.ref, FALSE);
		return;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;
		size_t prelen = target->len;

		if (VALUE_IS_STRING (v)) {
			out->convs->output.string (out, v->v_str.val);
			return;
		}

		if (v->type == VALUE_CELLRANGE) {
			out->convs->output.range_ref (out, &v->v_range.cell);
			return;
		}

		value_get_as_gstring (v, target, out->convs);

		/* If the number has a sign, pretend that it is the result of
		 * OPER_UNARY_{NEG,PLUS}.
		 */
		if ((target->str[prelen] == '-' || target->str[prelen] == '+') &&
		    operations[GNM_EXPR_OP_UNARY_NEG].prec <= paren_level) {
			g_string_insert_c (target, prelen, '(');
			g_string_append_c (target, ')');
		}
		return;
	}

	case GNM_EXPR_OP_ARRAY_CORNER:
		do_expr_as_string (expr->array_corner.expr, 0, out);
		return;

	case GNM_EXPR_OP_ARRAY_ELEM: {
		GnmCell const *corner = array_elem_get_corner (&expr->array_elem,
			out->pp->sheet, &out->pp->eval);
		if (NULL != corner) {
			GnmParsePos const *real_pp = out->pp;
			GnmParsePos  pp = *real_pp;

			pp.eval.col -= expr->array_elem.x;
			pp.eval.row -= expr->array_elem.y;
			out->pp = &pp;
			do_expr_as_string (
				corner->base.texpr->expr->array_corner.expr,
				0, out);
			out->pp = real_pp;
			return;
		}
		break;
	}

	case GNM_EXPR_OP_SET:
		gnm_expr_list_as_string (expr->set.argc, expr->set.argv, out);
		return;
	}

	g_string_append (target, "<ERROR>");
}

void
gnm_expr_as_gstring (GnmExpr const *expr, GnmConventionsOut *out)
{
	g_return_if_fail (expr != NULL);
	g_return_if_fail (out  != NULL);

	do_expr_as_string (expr, 0, out);
}

char *
gnm_expr_as_string (GnmExpr const *expr, GnmParsePos const *pp,
		    GnmConventions const *convs)
{
	GnmConventionsOut out;

	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (pp != NULL, NULL);

	out.accum = g_string_new (NULL);
	out.pp    = pp;
	out.convs = convs;
	do_expr_as_string (expr, 0, &out);
	return g_string_free (out.accum, FALSE);
}

/****************************************************************************/
typedef struct {
	GnmExprRelocateInfo const *details;
	gboolean from_inside;
	gboolean check_rels;
} RelocInfoInternal;

static GnmExpr const *
invalidate_sheet_cellrange (RelocInfoInternal const *rinfo,
			    GnmValueRange const *v)
{
	GnmCellRef ref_a = v->cell.a;
	GnmCellRef ref_b = v->cell.b;

	Sheet const *sheet_a = ref_a.sheet;
	Sheet const *sheet_b = ref_b.sheet;
	Workbook *wb;
	gboolean hit_a = sheet_a && sheet_a->being_invalidated;
	gboolean hit_b = sheet_b && sheet_b->being_invalidated;
	int dir;

	if (!hit_a && !hit_b)
		return NULL;

	if (sheet_a == NULL || sheet_b == NULL ||
	    sheet_a->workbook != sheet_b->workbook)
		/* A 3D reference between workbooks?  */
		return gnm_expr_new_constant (value_new_error_REF (NULL));

	/* Narrow the sheet range.  */
	wb = sheet_a->workbook;
	dir = (sheet_a->index_in_wb < sheet_b->index_in_wb) ? +1 : -1;
	while (sheet_a != sheet_b && sheet_a->being_invalidated)
		sheet_a = workbook_sheet_by_index (wb, sheet_a->index_in_wb + dir);
	while (sheet_a != sheet_b && sheet_b->being_invalidated)
		sheet_b = workbook_sheet_by_index (wb, sheet_b->index_in_wb - dir);

	if (sheet_a->being_invalidated)
		return gnm_expr_new_constant (value_new_error_REF (NULL));

	ref_a.sheet = (Sheet *)sheet_a;
	ref_b.sheet = (Sheet *)sheet_b;
	return gnm_expr_new_constant (value_new_cellrange_unsafe (&ref_a, &ref_b));
}

static gboolean
reloc_range (GnmExprRelocateInfo const *rinfo,
	     Sheet const *start_sheet, Sheet const *end_sheet,
	     GnmRange *rng)
{
	GnmRange t, b, l, r;
	gboolean start, end;

	if (start_sheet != end_sheet ||		/* ignore 3d refs */
	    start_sheet != rinfo->origin_sheet)	/* ref is to a different sheet */
		return FALSE;

	t.start.col = b.start.col = l.start.col = l.end.col   = rng->start.col;
	t.end.col   = b.end.col   = r.start.col = r.end.col   = rng->end.col;
	t.start.row = t.end.row   = l.start.row = r.start.row = rng->start.row;
	b.start.row = b.end.row   = l.end.row   = r.end.row   = rng->end.row;

	start = range_contained (&t, &rinfo->origin);
	end   = range_contained (&b, &rinfo->origin);
	if (start && end) { /* full enclosure */
		rng->start.col += rinfo->col_offset;
		rng->end.col   += rinfo->col_offset;
		rng->start.row += rinfo->row_offset;
		rng->end.row   += rinfo->row_offset;
		return TRUE;
	}

	if (rinfo->col_offset == 0) {
		if (start && rinfo->row_offset < range_height (rng)) {
			rng->start.row += rinfo->row_offset;
			return TRUE;
		}
		if (end && rinfo->row_offset > -range_height (rng)) {
			/* Special case invalidating the bottom of a range while
			 * deleting rows. Otherwise we #REF! before  we can shorten
			 * The -1 is safe, origin.start.row == 0 is handled above */
			if (rinfo->reloc_type == GNM_EXPR_RELOCATE_ROWS &&
			    rinfo->row_offset >= gnm_sheet_get_max_rows (end_sheet))
				rng->end.row  = rinfo->origin.start.row - 1;
			else
				rng->end.row += rinfo->row_offset;
			return TRUE;
		}
	}

	if (rinfo->row_offset == 0) {
		if (range_contained (&l, &rinfo->origin) &&
		    rinfo->col_offset < range_width (rng)) {
			rng->start.col += rinfo->col_offset;
			return TRUE;
		}
		if (range_contained (&r, &rinfo->origin) &&
		    rinfo->col_offset > -range_width (rng)) {
			/* Special case invalidating the right side of a range while
			 * deleting cols. Otherwise we #REF! before  we can shorten.
			 * The -1 is safe, origin.start.col == 0 is handled above */
			if (rinfo->reloc_type == GNM_EXPR_RELOCATE_COLS &&
			    rinfo->col_offset >= gnm_sheet_get_max_cols (end_sheet))
				rng->end.col  = rinfo->origin.start.col - 1;
			else
				rng->end.col += rinfo->col_offset;
			return TRUE;
		}
	}

	return FALSE;
}

static void
reloc_normalize_cellref (RelocInfoInternal const *rinfo, GnmCellRef const *ref,
			 Sheet **sheet, GnmCellPos *res)
{
	*sheet = eval_sheet (ref->sheet, rinfo->details->pos.sheet);
	res->col = ref->col;
	if (ref->col_relative) {
		if (rinfo->check_rels)
			res->col += rinfo->details->pos.eval.col;
		else
			res->col = 0;
	}
	res->row = ref->row;
	if (ref->row_relative) {
		if (rinfo->check_rels)
			res->row += rinfo->details->pos.eval.row;
		else
			res->row = 0;
	}
}

/* Return TRUE if @pos is out of bounds */
static gboolean
reloc_restore_cellref (RelocInfoInternal const *rinfo,
		       Sheet *sheet, GnmCellPos const *pos,
		       GnmCellRef *res)
{
	if (res->sheet == rinfo->details->origin_sheet)
		res->sheet = rinfo->details->target_sheet;

	if (!res->col_relative  || rinfo->check_rels) {
		if (pos->col < 0 || gnm_sheet_get_max_cols (sheet) <= pos->col)
			return TRUE;
		res->col = pos->col;
		if (res->col_relative) {
			res->col -= rinfo->details->pos.eval.col;
			if (rinfo->from_inside)
				res->col -= rinfo->details->col_offset;
		}
	}
	if (!res->row_relative  || rinfo->check_rels) {
		if (pos->row < 0 || gnm_sheet_get_max_rows (sheet) <= pos->row)
			return TRUE;
		res->row = pos->row;
		if (res->row_relative) {
			res->row -= rinfo->details->pos.eval.row;
			if (rinfo->from_inside)
				res->row -= rinfo->details->row_offset;
		}
	}

	return FALSE;
}


static GnmExpr const *
reloc_cellrange (RelocInfoInternal const *rinfo, GnmValueRange const *v)
{
	GnmRange r;
	Sheet   *start_sheet, *end_sheet;
	gboolean full_col, full_row;

	/* Normalize the rangeRef, and remember if we had a full col/row
	 *  ref.  If relocating the result changes things, or if we're from
	 *  inside the range that is moving map back to a RangeRef from the
	 *  target position.  If the result is different that the original
	 *  generate a new expression. */
	reloc_normalize_cellref (rinfo, &v->cell.a, &start_sheet, &r.start);
	reloc_normalize_cellref (rinfo, &v->cell.b, &end_sheet,   &r.end);
	/* (Foo,NULL) in Bar will generate (Foo,Bar) in normalize */
	if (NULL == v->cell.b.sheet)
		end_sheet = start_sheet;

	full_col = range_is_full (&r, FALSE);
	full_row = range_is_full (&r, TRUE);

	if (reloc_range (rinfo->details, start_sheet, end_sheet, &r) ||
	    rinfo->from_inside) {
		GnmRangeRef res = v->cell;
		range_make_full (&r, full_col, full_row);
		if (reloc_restore_cellref (rinfo, start_sheet, &r.start, &res.a) ||
		    reloc_restore_cellref (rinfo, end_sheet,   &r.end,   &res.b))
			return gnm_expr_new_constant (value_new_error_REF (NULL));
		if (gnm_rangeref_equal (&res, &v->cell))
			return NULL;
		return gnm_expr_new_constant (value_new_cellrange_unsafe (&res.a, &res.b));
	}

	return NULL;
}

static GnmExpr const *
gnm_expr_relocate (GnmExpr const *expr, RelocInfoInternal const *rinfo)
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY: {
		GnmExpr const *a = gnm_expr_relocate (expr->binary.value_a, rinfo);
		GnmExpr const *b = gnm_expr_relocate (expr->binary.value_b, rinfo);

		if (a == NULL && b == NULL)
			return NULL;

		if (a == NULL)
			a = gnm_expr_copy (expr->binary.value_a);
		else if (b == NULL)
			b = gnm_expr_copy (expr->binary.value_b);

		return gnm_expr_new_binary (a, GNM_EXPR_GET_OPER (expr), b);
	}

	case GNM_EXPR_OP_ANY_UNARY: {
		GnmExpr const *a = gnm_expr_relocate (expr->unary.value, rinfo);
		if (a == NULL)
			return NULL;
		return gnm_expr_new_unary (GNM_EXPR_GET_OPER (expr), a);
	}

	case GNM_EXPR_OP_FUNCALL: {
		gboolean rewrite = FALSE;
		int i;
		int argc = expr->func.argc;
		GnmExprConstPtr *argv =
			argc ? g_new (GnmExprConstPtr, argc) : NULL;

		for (i = 0; i < argc; i++) {
			argv[i] = gnm_expr_relocate (expr->func.argv[i], rinfo);
			if (argv[i])
				rewrite = TRUE;
		}

		if (rewrite) {
			for (i = 0; i < argc; i++)
				if (!argv[i])
					argv[i] = gnm_expr_copy (expr->func.argv[i]);

			return gnm_expr_new_funcallv
				(expr->func.func,
				 argc,
				 argv);
		}
		g_free (argv);
		return NULL;
	}
	case GNM_EXPR_OP_SET: {
		gboolean rewrite = FALSE;
		int i;
		int argc = expr->set.argc;
		GnmExprConstPtr *argv =
			argc ? g_new (GnmExprConstPtr, argc) : NULL;

		for (i = 0; i < argc; i++) {
			argv[i] = gnm_expr_relocate (expr->set.argv[i], rinfo);
			if (argv[i])
				rewrite = TRUE;
		}

		if (rewrite) {
			for (i = 0; i < argc; i++)
				if (!argv[i])
					argv[i] = gnm_expr_copy (expr->set.argv[i]);

			return gnm_expr_new_setv (argc, argv);
		}
		g_free (argv);
		return NULL;
	}

	case GNM_EXPR_OP_NAME: {
		GnmNamedExpr *nexpr = expr->name.name;

		/* we cannot invalidate references to the name that are
		 * sitting in the undo queue, or the clipboard.  So we just
		 * flag the name as inactive and remove the reference here.
		 */
		if (!nexpr->active)
			return gnm_expr_new_constant (value_new_error_REF (NULL));

		switch (rinfo->details->reloc_type) {
		case GNM_EXPR_RELOCATE_INVALIDATE_SHEET:
			if (nexpr->pos.sheet && nexpr->pos.sheet->being_invalidated)
				return gnm_expr_new_constant (value_new_error_REF (NULL));
			else
				return NULL;

		case GNM_EXPR_RELOCATE_MOVE_RANGE:
			/*
			 * If the name is not officially scoped, check
			 * that it is available in the new scope
			 */
			if (expr->name.optional_scope == NULL &&
			    rinfo->details->target_sheet != rinfo->details->origin_sheet) {
				GnmNamedExpr *new_nexpr;
				GnmParsePos pos;
				parse_pos_init_sheet (&pos, rinfo->details->target_sheet);

				/* If the name is not available in the new scope explicitly scope it */
				new_nexpr = expr_name_lookup (&pos, nexpr->name->str);
				if (new_nexpr == NULL) {
					if (nexpr->pos.sheet != NULL)
						return gnm_expr_new_name (nexpr, nexpr->pos.sheet, NULL);
					return gnm_expr_new_name (nexpr, NULL, nexpr->pos.wb);
				}

				/* replace it with the new name using qualified as
				 * local to the target sheet
				 */
				return gnm_expr_new_name (new_nexpr, pos.sheet, NULL);
			} else {
				/*
				 * Do NOT rewrite the name.
				 * Just invalidate the use of the name
				 */
				GnmExpr const *tmp =
					gnm_expr_relocate (nexpr->texpr->expr,
							   rinfo);
				if (tmp != NULL) {
					gnm_expr_free (tmp);
					return gnm_expr_new_constant (
						value_new_error_REF (NULL));
				}

				return NULL;
			}

		case GNM_EXPR_RELOCATE_COLS:
		case GNM_EXPR_RELOCATE_ROWS:
			return NULL;

		default:
			g_assert_not_reached ();
		}
	}

	case GNM_EXPR_OP_CELLREF: {
		GnmCellRef const *ref = &expr->cellref.ref;
		switch (rinfo->details->reloc_type) {
		case GNM_EXPR_RELOCATE_INVALIDATE_SHEET:
			if (ref->sheet &&
			    ref->sheet->being_invalidated)
				return gnm_expr_new_constant (value_new_error_REF (NULL));
			return NULL;

		default : {
			GnmRange r;
			Sheet   *sheet;

			reloc_normalize_cellref (rinfo, ref, &sheet, &r.start);
			r.end = r.start;

			if (reloc_range (rinfo->details, sheet, sheet, &r) ||
			    rinfo->from_inside) {
				GnmCellRef res = *ref;
				if (reloc_restore_cellref (rinfo, sheet, &r.start, &res))
					return gnm_expr_new_constant (value_new_error_REF (NULL));
				if (gnm_cellref_equal (&res, ref))
					return NULL;
				return gnm_expr_new_cellref (&res);
			}
			return NULL;
		}
		}
		return NULL;
	}

	case GNM_EXPR_OP_CONSTANT:
		if (expr->constant.value->type == VALUE_CELLRANGE) {
			switch (rinfo->details->reloc_type) {
			case GNM_EXPR_RELOCATE_INVALIDATE_SHEET:
				return invalidate_sheet_cellrange (rinfo,
					&expr->constant.value->v_range);
			default :
				return reloc_cellrange (rinfo,
					&expr->constant.value->v_range);
			}
		}
		return NULL;

	case GNM_EXPR_OP_ARRAY_ELEM:
		return NULL;

	case GNM_EXPR_OP_ARRAY_CORNER: {
		GnmExpr const *e = gnm_expr_relocate (expr->array_corner.expr, rinfo);
		if (e)
			return gnm_expr_new_array_corner (
				expr->array_corner.cols,
				expr->array_corner.rows, e);
		return NULL;
	}
	}

	g_assert_not_reached ();
	return NULL;
}

GnmFunc *
gnm_expr_get_func_def (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL, NULL);

	return expr->func.func;
}


static GnmExpr const *
gnm_expr_first_funcall (GnmExpr const *expr)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	default :
	case GNM_EXPR_OP_NAME:
	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_CONSTANT:
	case GNM_EXPR_OP_ARRAY_ELEM:
		return NULL;

	case GNM_EXPR_OP_FUNCALL:
		return expr;

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY: {
		GnmExpr const *res =
			gnm_expr_first_funcall (expr->binary.value_a);
		if (res)
			return res;
		else
			return gnm_expr_first_funcall (expr->binary.value_b);
	}

	case GNM_EXPR_OP_ANY_UNARY:
		return gnm_expr_first_funcall (expr->unary.value);

	case GNM_EXPR_OP_ARRAY_CORNER:
		return gnm_expr_first_funcall (expr->array_corner.expr);
	}

	g_assert_not_reached ();
	return NULL;
}

static void
cellref_boundingbox (GnmCellRef const *cr, GnmRange *bound)
{
	if (cr->col_relative) {
		if (cr->col >= 0) {
			int const c = gnm_sheet_get_max_cols (cr->sheet) - cr->col - 1;
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
			int const r = gnm_sheet_get_max_rows (cr->sheet) - cr->row - 1;
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
do_referenced_sheets (GnmExpr const *expr, GSList *sheets)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return do_referenced_sheets (
			expr->binary.value_a,
			do_referenced_sheets (
				expr->binary.value_b,
				sheets));

	case GNM_EXPR_OP_ANY_UNARY:
		return do_referenced_sheets (expr->unary.value, sheets);

	case GNM_EXPR_OP_FUNCALL: {
		int i;
		for (i = 0; i < expr->func.argc; i++)
			sheets = do_referenced_sheets (expr->func.argv[i],
						       sheets);
		return sheets;
	}
	case GNM_EXPR_OP_SET: {
		int i;
		for (i = 0; i < expr->set.argc; i++)
			sheets = do_referenced_sheets (expr->set.argv[i],
						       sheets);
		return sheets;
	}

	case GNM_EXPR_OP_NAME:
		return sheets;

	case GNM_EXPR_OP_CELLREF:
		return g_slist_insert_unique (sheets, expr->cellref.ref.sheet);

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;
		if (v->type != VALUE_CELLRANGE)
			return sheets;
		return g_slist_insert_unique (
			g_slist_insert_unique (sheets,
					       v->v_range.cell.a.sheet),
			v->v_range.cell.b.sheet);
	}

	case GNM_EXPR_OP_ARRAY_CORNER:
		return do_referenced_sheets (expr->array_corner.expr, sheets);

	case GNM_EXPR_OP_ARRAY_ELEM:
		break;
	}

	return sheets;
}

/**
 * gnm_expr_containts_subtotal :
 * @expr :
 *
 * return TRUE if the expression calls the SUBTOTAL function
 **/
gboolean
gnm_expr_contains_subtotal (GnmExpr const *expr)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return gnm_expr_contains_subtotal (expr->binary.value_a) ||
		       gnm_expr_contains_subtotal (expr->binary.value_b);
	case GNM_EXPR_OP_ANY_UNARY:
		return gnm_expr_contains_subtotal (expr->unary.value);

	case GNM_EXPR_OP_FUNCALL: {
		int i;
		if (!strcmp (expr->func.func->name, "subtotal"))
			return TRUE;
		for (i = 0; i < expr->func.argc; i++)
			if (gnm_expr_contains_subtotal (expr->func.argv[i]))
				return TRUE;
		return FALSE;
	}
	case GNM_EXPR_OP_SET: {
		int i;
		for (i = 0; i < expr->set.argc; i++)
			if (gnm_expr_contains_subtotal (expr->set.argv[i]))
				return TRUE;
		return FALSE;
	}

	case GNM_EXPR_OP_NAME:
		if (expr->name.name->active)
			return gnm_expr_contains_subtotal (expr->name.name->texpr->expr);

	case GNM_EXPR_OP_ARRAY_CORNER:
		return gnm_expr_contains_subtotal (expr->array_corner.expr);

	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_CONSTANT:
	case GNM_EXPR_OP_ARRAY_ELEM:
		;
	}
	return FALSE;
}

static void
gnm_expr_get_boundingbox (GnmExpr const *expr, GnmRange *bound)
{
	g_return_if_fail (expr != NULL);

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		gnm_expr_get_boundingbox (expr->binary.value_a, bound);
		gnm_expr_get_boundingbox (expr->binary.value_b, bound);
		break;

	case GNM_EXPR_OP_ANY_UNARY:
		gnm_expr_get_boundingbox (expr->unary.value, bound);
		break;

	case GNM_EXPR_OP_FUNCALL: {
		int i;
		for (i = 0; i < expr->func.argc; i++)
			gnm_expr_get_boundingbox (expr->func.argv[i], bound);
		break;
	}
	case GNM_EXPR_OP_SET: {
		int i;
		for (i = 0; i < expr->set.argc; i++)
			gnm_expr_get_boundingbox (expr->set.argv[i], bound);
		break;
	}

	case GNM_EXPR_OP_NAME:
		/* Do NOT validate the name. */
		/* TODO : is that correct ? */
		break;

	case GNM_EXPR_OP_CELLREF:
		cellref_boundingbox (&expr->cellref.ref, bound);
		break;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;

		if (v->type == VALUE_CELLRANGE) {
			cellref_boundingbox (&v->v_range.cell.a, bound);
			cellref_boundingbox (&v->v_range.cell.b, bound);
		}
		break;
	}

	case GNM_EXPR_OP_ARRAY_CORNER:
		gnm_expr_get_boundingbox (expr->array_corner.expr, bound);
		break;

	case GNM_EXPR_OP_ARRAY_ELEM:
		/* Think about this */
		break;
	}
}

/**
 * gnm_expr_get_range:
 * @expr :
 *
 * If this expression contains a single range return it.
 * Caller is responsible for value_releasing the result.
 */
GnmValue *
gnm_expr_get_range (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_CELLREF :
		return value_new_cellrange_unsafe (
			&expr->cellref.ref, &expr->cellref.ref);

	case GNM_EXPR_OP_CONSTANT:
		if (expr->constant.value->type == VALUE_CELLRANGE)
			return value_dup (expr->constant.value);
		return NULL;

	case GNM_EXPR_OP_NAME:
		if (!expr->name.name->active)
			return NULL;
		return gnm_expr_top_get_range (expr->name.name->texpr);

	default:
		return NULL;
	}
}


static GSList *
do_gnm_expr_get_ranges (GnmExpr const *expr, GSList *ranges)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return do_gnm_expr_get_ranges (
			expr->binary.value_a,
			do_gnm_expr_get_ranges (
				expr->binary.value_b,
				ranges));
	case GNM_EXPR_OP_ANY_UNARY:
		return do_gnm_expr_get_ranges (expr->unary.value, ranges);
	case GNM_EXPR_OP_FUNCALL: {
		int i;
		for (i = 0; i < expr->func.argc; i++)
			ranges = do_gnm_expr_get_ranges (expr->func.argv[i],
							 ranges);
		return ranges;
	}
	case GNM_EXPR_OP_SET: {
		int i;
		for (i = 0; i < expr->set.argc; i++)
			ranges = do_gnm_expr_get_ranges (expr->set.argv[i],
							 ranges);
		return ranges;
	}

	case GNM_EXPR_OP_NAME:
		/* What?  */

	default: {
		GnmValue *v = gnm_expr_get_range (expr);
		if (v)
			return g_slist_insert_unique (ranges, v);
		return ranges;
	}
	}
}


/**
 * gnm_expr_is_rangeref :
 * @expr :
 *
 * Returns TRUE if the expression can generate a reference.
 * NOTE : in the future it would be nice to know if a function
 * can return a reference to tighten that up a bit.
 **/
gboolean
gnm_expr_is_rangeref (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, FALSE);

	switch (GNM_EXPR_GET_OPER (expr)) {
	/* would be better if we could differential which functions can return refs */
	case GNM_EXPR_OP_FUNCALL:

	/* a set in a set, do we need this ? */
	case GNM_EXPR_OP_SET:

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_CELLREF:
		return TRUE;

	case GNM_EXPR_OP_CONSTANT:
		if (expr->constant.value->type == VALUE_CELLRANGE)
			return TRUE;
		return FALSE;

	case GNM_EXPR_OP_NAME:
		if (expr->name.name->active)
			return gnm_expr_is_rangeref (expr->name.name->texpr->expr);
		return FALSE;

	case GNM_EXPR_OP_ARRAY_CORNER: /* I don't think this is possible */
	case GNM_EXPR_OP_ARRAY_ELEM:
	default :
		return FALSE;
	}
}

gboolean
gnm_expr_is_data_table (GnmExpr const *expr, GnmCellPos *c_in, GnmCellPos *r_in)
{
	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL) {
		char const *name = gnm_func_get_name (expr->func.func);
		if (name && 0 == strcmp (name, "table")) {
			if (NULL != r_in) {
				GnmExpr const *r = (expr->func.argc <= 0)
					? NULL
					: expr->func.argv[0];

				if (r != NULL && GNM_EXPR_GET_OPER (r) == GNM_EXPR_OP_CELLREF) {
					r_in->col = r->cellref.ref.col;
					r_in->row = r->cellref.ref.row;
				} else
					r_in->col = r_in->row = 0; /* impossible */
			}
			if (NULL != c_in) {
				GnmExpr const *c = (expr->func.argc <= 1)
					? NULL
					: expr->func.argv[1];

				if (c != NULL && GNM_EXPR_GET_OPER (c) == GNM_EXPR_OP_CELLREF) {
					c_in->col = c->cellref.ref.col;
					c_in->row = c->cellref.ref.row;
				} else
					c_in->col = c_in->row = 0; /* impossible */
			}
			return TRUE;
		}
	}

	/* Do we need anything else here ? */
	return FALSE;
}

/*
 * This frees the data pointers and the list.
 */
void
gnm_expr_list_unref (GnmExprList *list)
{
	GnmExprList *l;
	for (l = list; l; l = l->next)
		gnm_expr_free (l->data);
	gnm_expr_list_free (list);
}

static void
gnm_expr_list_as_string (int argc,
			 GnmExprConstPtr const *argv,
			 GnmConventionsOut *out)
{
	int i;
	gunichar arg_sep;
	if (out->convs->arg_sep)
		arg_sep = out->convs->arg_sep;
	else
		arg_sep = go_locale_get_arg_sep ();

	g_string_append_c (out->accum, '(');
	for (i = 0; i < argc; i++) {
		if (i != 0)
			g_string_append_unichar (out->accum, arg_sep);
		do_expr_as_string (argv[i], 0, out);
	}
	g_string_append_c (out->accum, ')');
}

static guint
gnm_expr_hash (GnmExpr const *expr)
{
	guint h = (guint)(GNM_EXPR_GET_OPER (expr));

	switch (GNM_EXPR_GET_OPER (expr)){
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_ANY_BINARY:
		return ((gnm_expr_hash (expr->binary.value_a) * 7) ^
			(gnm_expr_hash (expr->binary.value_b) * 3) ^
			h);

	case GNM_EXPR_OP_ANY_UNARY:
		return ((gnm_expr_hash (expr->unary.value) * 7) ^
			h);

	case GNM_EXPR_OP_FUNCALL: {
		int i;
		for (i = 0; i < expr->func.argc; i++)
			h = (h * 3) ^ gnm_expr_hash (expr->func.argv[i]);
		return h;
	}

	case GNM_EXPR_OP_SET: {
		int i;
		for (i = 0; i < expr->set.argc; i++)
			h = (h * 3) ^ gnm_expr_hash (expr->set.argv[i]);
		return h;
	}

	case GNM_EXPR_OP_CONSTANT:
		return value_hash (expr->constant.value);

	case GNM_EXPR_OP_NAME:
		/* all we need is a somewhat unique hash, ignore int != ptr */
		return GPOINTER_TO_UINT (expr->name.name);

	case GNM_EXPR_OP_CELLREF:
		return gnm_cellref_hash (&expr->cellref.ref);

	case GNM_EXPR_OP_ARRAY_CORNER:
		return gnm_expr_hash (expr->array_corner.expr);

	case GNM_EXPR_OP_ARRAY_ELEM:
		return ((expr->array_elem.x << 16) ^
			(expr->array_elem.y));
	}

	return h;
}


/***************************************************************************/

GnmExprSharer *
gnm_expr_sharer_new (void)
{
	GnmExprSharer *es = g_new (GnmExprSharer, 1);
	es->nodes_in = 0;
	es->nodes_stored = 0;
	es->nodes_killed = 0;
	es->exprs = g_hash_table_new_full
		((GHashFunc)gnm_expr_top_hash,
		 (GEqualFunc)gnm_expr_top_equal,
		 (GDestroyNotify)gnm_expr_top_unref,
		 NULL);
	return es;
}

void
gnm_expr_sharer_destroy (GnmExprSharer *es)
{
	g_hash_table_destroy (es->exprs);
	g_free (es);
}

GnmExprTop const *
gnm_expr_sharer_share (GnmExprSharer *es, GnmExprTop const *texpr)
{
	GnmExprTop const *shared;

        g_return_val_if_fail (es != NULL, texpr);
        g_return_val_if_fail (texpr != NULL, NULL);

	es->nodes_in++;

	/* Corners must not get shared.  */
	if (GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER)
		return texpr;

	shared = g_hash_table_lookup (es->exprs, texpr);
	if (shared) {
		gnm_expr_top_ref (shared);
		if (texpr->refcount == 1)
			es->nodes_killed++;
		gnm_expr_top_unref (texpr);
		return shared;
	}

	gnm_expr_top_ref (texpr);
	g_hash_table_insert (es->exprs, (gpointer)texpr, (gpointer)texpr);
	es->nodes_stored++;

	return texpr;
}

/***************************************************************************/

GnmExprTop const *
gnm_expr_top_new (GnmExpr const *expr)
{
	GnmExprTop *res;

	if (expr == NULL)
		return NULL;

	res = g_new (GnmExprTop, 1);
	res->magic = GNM_EXPR_TOP_MAGIC;
	res->hash = 0;
	res->refcount = 1;
	res->expr = expr;
	return res;
}

GnmExprTop const *
gnm_expr_top_new_constant (GnmValue *v)
{
	return gnm_expr_top_new (gnm_expr_new_constant (v));
}

void
gnm_expr_top_ref (GnmExprTop const *texpr)
{
	g_return_if_fail (IS_GNM_EXPR_TOP (texpr));

	((GnmExprTop *)texpr)->refcount++;
}

void
gnm_expr_top_unref (GnmExprTop const *texpr)
{
	g_return_if_fail (IS_GNM_EXPR_TOP (texpr));

	((GnmExprTop *)texpr)->refcount--;
	if (texpr->refcount == 0) {
		gnm_expr_free (texpr->expr);
		((GnmExprTop *)texpr)->magic = 0;
		g_free ((GnmExprTop *)texpr);
	}
}

gboolean
gnm_expr_top_is_shared (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), FALSE);

	return texpr->refcount > 1;
}

GnmExprTop const *
gnm_expr_top_new_array_corner (int cols, int rows, GnmExpr const *expr)
{
	return gnm_expr_top_new (gnm_expr_new_array_corner (cols, rows, expr));
}

GnmExprTop const *
gnm_expr_top_new_array_elem  (int x, int y)
{
	return gnm_expr_top_new (gnm_expr_new_array_elem (x, y));
}

/**
 * gnm_expr_top_get_ranges:
 * @texpr :
 *
 * A collect the set of GnmRanges in @expr.
 * Return a list of the unique references Caller is responsible for releasing
 * the list and the content.
 **/
GSList *
gnm_expr_top_get_ranges (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);

	return do_gnm_expr_get_ranges (texpr->expr, NULL);
}

GnmValue *
gnm_expr_top_get_range (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);

	return gnm_expr_get_range (texpr->expr);
}

char *
gnm_expr_top_as_string (GnmExprTop const *texpr,
			GnmParsePos const *pp,
			GnmConventions const *convs)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);

	return gnm_expr_as_string (texpr->expr, pp, convs);
}

void
gnm_expr_top_as_gstring (GnmExprTop const *texpr,
			 GnmConventionsOut *out)
{
	g_return_if_fail (IS_GNM_EXPR_TOP (texpr));
	g_return_if_fail (out != NULL);

	do_expr_as_string (texpr->expr, 0, out);
}

guint
gnm_expr_top_hash (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), 0);

	if (texpr->hash == 0) {
		((GnmExprTop *)texpr)->hash = gnm_expr_hash (texpr->expr);
		/* The following line tests the truncated value.  */
		if (texpr->hash == 0)
			((GnmExprTop *)texpr)->hash = 1;
	}
	return texpr->hash;
}

gboolean
gnm_expr_top_equal (GnmExprTop const *te1, GnmExprTop const *te2)
{
	if (te1 == te2)
		return TRUE;

	g_return_val_if_fail (IS_GNM_EXPR_TOP (te1), FALSE);
	g_return_val_if_fail (IS_GNM_EXPR_TOP (te2), FALSE);

	if (te1->hash && te2->hash && te1->hash != te2->hash)
		return FALSE;

	return gnm_expr_equal (te1->expr, te2->expr);
}

/*
 * gnm_expr_top_relocate :
 * @texpr : #GnmExprTop to fixup
 * @rinfo : #GnmExprRelocateInfo details of relocation
 * @ignore_rel : Do not adjust relative refs (for internal use when
 *		  relocating named expressions.   Most callers will want FALSE.
 *
 * GNM_EXPR_RELOCATE_INVALIDATE_SHEET :
 *	Convert any references to  sheets marked being_invalidated into #REF!
 * GNM_EXPR_RELOCATE_MOVE_RANGE,
 *	Find any references to the specified area and adjust them by the
 *	supplied deltas.  Check for out of bounds conditions.  Return NULL if
 *	no change is required.
 *	If the expression is within the range to be moved, its relative
 *	references to cells outside the range are adjusted to reference the
 *	same cell after the move.
 * GNM_EXPR_RELOCATE_COLS
 * GNM_EXPR_RELOCATE_ROWS
 *
 */
GnmExprTop const *
gnm_expr_top_relocate (GnmExprTop const *texpr,
		       GnmExprRelocateInfo const *rinfo,
		       gboolean ignore_rel)
{
	RelocInfoInternal rinfo_tmp;

	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);
	g_return_val_if_fail (NULL != rinfo, NULL);

	rinfo_tmp.details = rinfo;
	rinfo_tmp.check_rels = !ignore_rel;
	if (rinfo->reloc_type != GNM_EXPR_RELOCATE_INVALIDATE_SHEET)
		rinfo_tmp.from_inside = (rinfo->origin_sheet == rinfo->pos.sheet) &&
			range_contains (&rinfo->origin, rinfo->pos.eval.col, rinfo->pos.eval.row);

	return gnm_expr_top_new (gnm_expr_relocate (texpr->expr, &rinfo_tmp));
}

/*
 * Convenience function to change an expression from one sheet to another.
 */
GnmExprTop const *
gnm_expr_top_relocate_sheet (GnmExprTop const *texpr,
			     Sheet const *src,
			     Sheet const *dst)
{
	GnmExprRelocateInfo rinfo;
	GnmExprTop const *res;

	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);
	g_return_val_if_fail (IS_SHEET (src), NULL);
	g_return_val_if_fail (IS_SHEET (dst), NULL);

	rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
	rinfo.origin_sheet = (Sheet *)src;
	rinfo.target_sheet = (Sheet *)dst;
	rinfo.col_offset = rinfo.row_offset = 0;
	range_init_full_sheet (&rinfo.origin);
	/* Not sure what sheet to use, but it doesn't seem to matter.  */
	parse_pos_init_sheet (&rinfo.pos, rinfo.target_sheet);

	res = gnm_expr_top_relocate (texpr, &rinfo, FALSE);
	if (!res) {
		if (gnm_expr_top_is_array_corner (texpr))
			res = gnm_expr_top_new (gnm_expr_copy (texpr->expr));
		else
			gnm_expr_top_ref ((res = texpr));
	}

	return res;
}

gboolean
gnm_expr_top_contains_subtotal (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), FALSE);

	return gnm_expr_contains_subtotal (texpr->expr);
}

GnmValue *
gnm_expr_top_eval (GnmExprTop const *texpr,
		   GnmEvalPos const *pos,
		   GnmExprEvalFlags flags)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);

	return gnm_expr_eval (texpr->expr, pos, flags);
}

/**
 * gnm_expr_top_referenced_sheets :
 * @texpr :
 * @sheets : usually NULL.
 *
 * Generates a list of the sheets referenced by the supplied expression.
 * Caller must free the list.
 */
GSList *
gnm_expr_top_referenced_sheets (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);

	return do_referenced_sheets (texpr->expr, NULL);
}

gboolean
gnm_expr_top_is_err (GnmExprTop const *texpr, GnmStdError err)
{
	GnmStdError err2;
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), FALSE);

	if (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_CONSTANT)
		return FALSE;

	err2 = value_error_classify (texpr->expr->constant.value);
	return err == err2;
}

/**
 * gnm_expr_top_get_constant:
 * @expr :
 *
 * If this expression consists of just a constant, return it.
 */
GnmValue const *
gnm_expr_top_get_constant (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);

	if (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_CONSTANT)
		return NULL;

	return texpr->expr->constant.value;
}

/**
 * gnm_expr_top_first_funcall :
 * @texpr :
 *
 */
GnmExpr const *
gnm_expr_top_first_funcall (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);

	return gnm_expr_first_funcall (texpr->expr);
}

/**
 * gnm_expr_top_get_boundingbox :
 *
 * Returns the range of cells in which the expression can be used without going
 * out of bounds.
 **/
void
gnm_expr_top_get_boundingbox (GnmExprTop const *texpr, GnmRange *bound)
{
	g_return_if_fail (IS_GNM_EXPR_TOP (texpr));

	gnm_expr_get_boundingbox (texpr->expr, bound);
}

gboolean
gnm_expr_top_is_rangeref (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), FALSE);

	return gnm_expr_is_rangeref (texpr->expr);
}

GnmExprArrayCorner const *
gnm_expr_top_get_array_corner (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);
	return GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER
		? &texpr->expr->array_corner
		: NULL;
}

gboolean
gnm_expr_top_is_array_corner (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), FALSE);
	return GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER;
}

gboolean
gnm_expr_top_is_array_elem (GnmExprTop const *texpr, int *x, int *y)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), FALSE);

	if (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_ARRAY_ELEM)
		return FALSE;

	if (x) *x = texpr->expr->array_elem.x;
	if (y) *y = texpr->expr->array_elem.y;
	return TRUE;
}

GnmExprTop const *
gnm_expr_top_transpose (GnmExprTop const *texpr)
{
	g_return_val_if_fail (IS_GNM_EXPR_TOP (texpr), NULL);
	switch (GNM_EXPR_GET_OPER (texpr->expr)) {
	case GNM_EXPR_OP_ARRAY_CORNER:
		/* Transpose size  */
		return gnm_expr_top_new_array_corner
			(texpr->expr->array_corner.rows,
			 texpr->expr->array_corner.cols,
			 gnm_expr_copy (texpr->expr));
	case GNM_EXPR_OP_ARRAY_ELEM:
		/* Transpose coordinates  */
		return gnm_expr_top_new_array_elem
			(texpr->expr->array_elem.y,
			 texpr->expr->array_elem.x);
	default:
		return NULL;
	}
}

/****************************************************************************/

#if USE_EXPR_POOLS
typedef union {
	guint32                 oper_and_refcount;
	GnmExprConstant		constant;
	GnmExprFunction		func;
	GnmExprUnary		unary;
	GnmExprBinary		binary;
	GnmExprArrayElem	array_elem;
	GnmExprSet		set;
} GnmExprSmall;
typedef union {
	guint32                 oper_and_refcount;
	GnmExprName		name;
	GnmExprCellRef		cellref;
	GnmExprArrayCorner	array_corner;
} GnmExprBig;
#endif

void
expr_init (void)
{
#if 0
	GnmExpr e;

#if USE_EXPR_POOLS
	/* 12 is an excellent size for a pool.  */
	g_print ("sizeof(GnmExprSmall) = %d\n", (int)sizeof (GnmExprSmall));
	g_print ("sizeof(GnmExprBig) = %d\n", (int)sizeof (GnmExprBig));
#endif
	g_print ("sizeof(e.func) = %d\n", (int)sizeof (e.func));
	g_print ("sizeof(e.unary) = %d\n", (int)sizeof (e.unary));
	g_print ("sizeof(e.binary) = %d\n", (int)sizeof (e.binary));
	g_print ("sizeof(e.name) = %d\n", (int)sizeof (e.name));
	g_print ("sizeof(e.cellref) = %d\n", (int)sizeof (e.cellref));
	g_print ("sizeof(e.array_corner) = %d\n", (int)sizeof (e.array_corner));
	g_print ("sizeof(e.array_elem) = %d\n", (int)sizeof (e.array_elem));
	g_print ("sizeof(e.set) = %d\n", (int)sizeof (e.set));
#endif
#if USE_EXPR_POOLS
	expression_pool_small =
		go_mem_chunk_new ("expression pool for small nodes",
				   sizeof (GnmExprSmall),
				   16 * 1024 - 128);
	expression_pool_big =
		go_mem_chunk_new ("expression pool for big nodes",
				   sizeof (GnmExprBig),
				   16 * 1024 - 128);
#endif
}

#if USE_EXPR_POOLS
static void
cb_expression_pool_leak (gpointer data, G_GNUC_UNUSED gpointer user)
{
	GnmExpr const *expr = data;
	GnmParsePos pp;
	char *s;

	pp.eval.col = 0;
	pp.eval.row = 0;
	pp.sheet = NULL;
	pp.wb = NULL;
	s = gnm_expr_as_string (expr, &pp, gnm_conventions_default);
	g_printerr ("Leaking expression at %p: %s.\n", expr, s);
	g_free (s);
}
#endif

void
expr_shutdown (void)
{
#if USE_EXPR_POOLS
	go_mem_chunk_foreach_leak (expression_pool_small, cb_expression_pool_leak, NULL);
	go_mem_chunk_destroy (expression_pool_small, FALSE);
	expression_pool_small = NULL;

	go_mem_chunk_foreach_leak (expression_pool_big, cb_expression_pool_leak, NULL);
	go_mem_chunk_destroy (expression_pool_big, FALSE);
	expression_pool_big = NULL;
#endif
}

/****************************************************************************/
