/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * expr.c : Expression evaluation in Gnumeric
 *
 * Copyright (C) 2001-2002 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "expr.h"

#include "expr-impl.h"
#include "expr-name.h"
#include "dependent.h"
#include "format.h"
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

/*
 * Using pools here probably does not save anything, but it's a darn
 * good debugging tool.
 */
#ifndef USE_EXPR_POOLS
#define USE_EXPR_POOLS 1
#endif

#if USE_EXPR_POOLS
/* Memory pool for expressions.  */
static GnmMemChunk *expression_pool;
#define CHUNK_ALLOC(T,p) ((T*)gnm_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) gnm_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif

/***************************************************************************/

#if 0
static guint
gnm_expr_constant_hash (GnmExprConstant const *expr)
{
	return value_hash (expr->value);
}
static gboolean
gnm_expr_constant_eq (GnmExprConstant const *a,
		      GnmExprConstant const *b)
{
}
#endif
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

	ans = CHUNK_ALLOC (GnmExprConstant, expression_pool);
	if (!ans)
		return NULL;
	gnm_expr_constant_init (ans, v);

	return (GnmExpr *)ans;
}

/***************************************************************************/

#if 0
static guint
gnm_expr_function_hash (GnmExprFunction const *expr)
{
	guint h = expr->oper;
	GnmExprList *l;
	for (l = expr->arg_list; l; l = l->next)
		h = (h * 3) ^ (GPOINTER_TO_INT (l->data));
	return h;
}
static gboolean
gnm_expr_function_eq (GnmExprFunction const *a,
		      GnmExprFunction const *b)
{
}
#endif

GnmExpr const *
gnm_expr_new_funcall (GnmFunc *func, GnmExprList *args)
{
	GnmExprFunction *ans;
	g_return_val_if_fail (func, NULL);

	ans = CHUNK_ALLOC (GnmExprFunction, expression_pool);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	ans->oper = GNM_EXPR_OP_FUNCALL;
	gnm_func_ref (func);
	ans->func = func;;
	ans->arg_list = args;

	return (GnmExpr *)ans;
}

/***************************************************************************/

#if 0
static guint
gnm_expr_unary_hash (GnmExprUnary const *expr)
{
	return  (GPOINTER_TO_INT (expr->value) * 7) ^
		(guint)(expr->oper);
}
static gboolean
gnm_expr_unary_eq (GnmExprUnary const *a,
		   GnmExprUnary const *b)
{
	return  a->oper == b->oper && a->value == b->value;
}
#endif

GnmExpr const *
gnm_expr_new_unary  (GnmExprOp op, GnmExpr const *e)
{
	GnmExprUnary *ans;

	ans = CHUNK_ALLOC (GnmExprUnary, expression_pool);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	ans->oper = op;
	ans->value = e;

	return (GnmExpr *)ans;
}

/***************************************************************************/

#if 0
static guint
gnm_expr_binary_hash (GnmExprBinary const *expr)
{
	return  (GPOINTER_TO_INT (expr->value_a) * 7) ^
		(GPOINTER_TO_INT (expr->value_b) * 3) ^
		(guint)(expr->oper);
}
#endif

GnmExpr const *
gnm_expr_new_binary (GnmExpr const *l, GnmExprOp op, GnmExpr const *r)
{
	GnmExprBinary *ans;

	ans = CHUNK_ALLOC (GnmExprBinary, expression_pool);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	ans->oper = op;
	ans->value_a = l;
	ans->value_b = r;

	return (GnmExpr *)ans;
}

/***************************************************************************/

#if 0
static guint
gnm_expr_name_hash (GnmExprName const *expr)
{
	return GPOINTER_TO_INT (expr->name);
}
#endif

GnmExpr const *
gnm_expr_new_name (GnmNamedExpr *name,
		   Sheet *optional_scope, Workbook *optional_wb_scope)
{
	GnmExprName *ans;

	ans = CHUNK_ALLOC (GnmExprName, expression_pool);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	ans->oper = GNM_EXPR_OP_NAME;
	ans->name = name;
	expr_name_ref (name);

	ans->optional_scope = optional_scope;
	ans->optional_wb_scope = optional_wb_scope;

	return (GnmExpr *)ans;
}

/***************************************************************************/

#if 0
static guint
gnm_expr_cellref_hash (GnmExprCellRef const *expr)
{
}
#endif

GnmExpr const *
gnm_expr_new_cellref (GnmCellRef const *cr)
{
	GnmExprCellRef *ans;

	ans = CHUNK_ALLOC (GnmExprCellRef, expression_pool);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	ans->oper = GNM_EXPR_OP_CELLREF;
	ans->ref = *cr;

	return (GnmExpr *)ans;
}

/***************************************************************************/

#if 0
static guint
gnm_expr_array_hash (GnmExprArray const *expr)
{
}
#endif

/**
 * gnm_expr_new_array :
 * @x :
 * @y :
 * @cols :
 * @rows :
 * @expr : optionally NULL.
 *
 * Absorb a referernce to @expr if it is non NULL.
 **/
GnmExpr const *
gnm_expr_new_array (int x, int y, int cols, int rows, GnmExpr const *expr)
{
	GnmExprArray *ans;

	ans = CHUNK_ALLOC (GnmExprArray, expression_pool);
	if (ans == NULL)
		return NULL;

	ans->ref_count = 1;
	ans->oper = GNM_EXPR_OP_ARRAY;
	ans->x = x;
	ans->y = y;
	ans->rows = rows;
	ans->cols = cols;
	ans->corner.value = NULL;
	ans->corner.expr = expr;
	return (GnmExpr *)ans;
}

/***************************************************************************/

#if 0
static guint
gnm_expr_set_hash (GnmExprSet const *expr)
{
	guint h = expr->oper;
	GnmExprList *l;
	for (l = expr->set; l; l = l->next)
		h = (h * 3) ^ (GPOINTER_TO_INT (l->data));
	return h;
}
#endif

GnmExpr const *
gnm_expr_new_set (GnmExprList *set)
{
	GnmExprSet *ans;

	ans = CHUNK_ALLOC (GnmExprSet, expression_pool);
	if (!ans)
		return NULL;

	ans->ref_count = 1;
	ans->oper = GNM_EXPR_OP_SET;
	ans->set = set;

	return (GnmExpr *)ans;
}

/***************************************************************************/

/**
 * gnm_expr_ref:
 * Increments the ref_count for an expression node.
 */
void
gnm_expr_ref (GnmExpr const *expr)
{
	g_return_if_fail (expr != NULL);
	g_return_if_fail (expr->any.ref_count > 0);

	((GnmExpr *)expr)->any.ref_count++;
}

static void
do_gnm_expr_unref (GnmExpr const *expr)
{
	if (--((GnmExpr *)expr)->any.ref_count > 0)
		return;

	switch (expr->any.oper) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		do_gnm_expr_unref (expr->binary.value_a);
		do_gnm_expr_unref (expr->binary.value_b);
		break;

	case GNM_EXPR_OP_FUNCALL:
		gnm_expr_list_unref (expr->func.arg_list);
		gnm_func_unref (expr->func.func);
		break;

	case GNM_EXPR_OP_NAME:
		expr_name_unref (expr->name.name);
		break;

	case GNM_EXPR_OP_CONSTANT:
		value_release ((GnmValue *)expr->constant.value);
		break;

	case GNM_EXPR_OP_CELLREF:
		break;

	case GNM_EXPR_OP_ANY_UNARY:
		do_gnm_expr_unref (expr->unary.value);
		break;

	case GNM_EXPR_OP_ARRAY:
		if (expr->array.x == 0 && expr->array.y == 0) {
			if (expr->array.corner.value)
				value_release (expr->array.corner.value);
			do_gnm_expr_unref (expr->array.corner.expr);
		}
		break;

	case GNM_EXPR_OP_SET:
		gnm_expr_list_unref (expr->set.set);
		break;

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		break;
#endif
	}

	CHUNK_FREE (expression_pool, (gpointer)expr);
}

/*
 * gnm_expr_unref:
 * Decrements the ref_count for part of a expression.  (All trees are expected
 * to have been created with a ref-count of one, so when we hit zero, we
 * go down over the tree and unref the tree and its leaves stuff.)
 */
void
gnm_expr_unref (GnmExpr const *expr)
{
	g_return_if_fail (expr != NULL);
	g_return_if_fail (expr->any.ref_count > 0);

	if (expr->any.ref_count == 1)
		do_gnm_expr_unref (expr);
	else
		((GnmExpr *)expr)->any.ref_count--;
}

/**
 * gnm_expr_is_shared : Returns TRUE if the reference count
 *   for the supplied expression is > 1
 */
gboolean
gnm_expr_is_shared (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, FALSE);

	return (expr->any.ref_count > 1);
}

/**
 * gnm_expr_equal : Returns TRUE if the supplied expressions are exactly the
 *   same.  No eval position is used to see if they are effectively the same.
 *   Named expressions must refer the the same name, having equivalent names is
 *   insufficeient.
 */
gboolean
gnm_expr_equal (GnmExpr const *a, GnmExpr const *b)
{
	if (a == b)
		return TRUE;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a->any.oper != b->any.oper)
		return FALSE;

	switch (a->any.oper) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return	gnm_expr_equal (a->binary.value_a, b->binary.value_a) &&
			gnm_expr_equal (a->binary.value_b, b->binary.value_b);

	case GNM_EXPR_OP_ANY_UNARY:
		return gnm_expr_equal (a->unary.value, b->unary.value);

	case GNM_EXPR_OP_FUNCALL:
		return (a->func.func == b->func.func) &&
			gnm_expr_list_equal (a->func.arg_list, b->func.arg_list);

	case GNM_EXPR_OP_NAME:
		return	a->name.name == b->name.name &&
			a->name.optional_scope == b->name.optional_scope &&
			a->name.optional_wb_scope == b->name.optional_wb_scope;

	case GNM_EXPR_OP_CELLREF:
		return cellref_equal (&a->cellref.ref, &b->cellref.ref);

	case GNM_EXPR_OP_CONSTANT:
		return value_equal (a->constant.value, b->constant.value);

	case GNM_EXPR_OP_ARRAY: {
		GnmExprArray const *aa = &a->array;
		GnmExprArray const *ab = &b->array;

		return	aa->cols == ab->cols &&
			aa->rows == ab->rows &&
			aa->x == ab->x &&
			aa->y == ab->y &&
			gnm_expr_equal (aa->corner.expr, ab->corner.expr);
	}

	case GNM_EXPR_OP_SET:
		return gnm_expr_list_equal (a->set.set, b->set.set);
	}

	return FALSE;
}

static GnmCell *
expr_array_corner (GnmExpr const *expr,
		   Sheet const *sheet, GnmCellPos const *pos)
{
	GnmCell *corner = sheet_cell_get (sheet,
		pos->col - expr->array.x, pos->row - expr->array.y);

	/* Sanity check incase the corner gets removed for some reason */
	g_return_val_if_fail (corner != NULL, NULL);
	g_return_val_if_fail (cell_has_expr (corner), NULL);
	g_return_val_if_fail (corner->base.expression != (void *)0xdeadbeef, NULL);
	g_return_val_if_fail (corner->base.expression->any.oper == GNM_EXPR_OP_ARRAY, NULL);
	g_return_val_if_fail (corner->base.expression->array.x == 0, NULL);
	g_return_val_if_fail (corner->base.expression->array.y == 0, NULL);

	return corner;
}

static gboolean
gnm_expr_extract_ref (GnmRangeRef *res, GnmExpr const *expr,
		      GnmEvalPos const *pos, GnmExprEvalFlags flags)
{
	switch (expr->any.oper) {
	case GNM_EXPR_OP_FUNCALL : {
		gboolean failed = TRUE;
		GnmValue *v;
		FunctionEvalInfo ei;
		ei.pos = pos;
		ei.func_call = (GnmExprFunction const *)expr;

		v = function_call_with_list (&ei, expr->func.arg_list, flags);
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
		return gnm_expr_extract_ref (res, expr->name.name->expr, pos, flags);
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

	if (res->type == VALUE_EMPTY) {
		value_release (res);
		return (flags & GNM_EXPR_EVAL_PERMIT_EMPTY)
		    ? NULL : value_new_int (0);
	}
	return res;
}

/**
 * value_intersection :
 * @v   : a VALUE_CELLRANGE
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
		res = value_dup (v->v_array.vals[0][0]);
		value_release (v);
		return res;
	}

	/* inverted ranges */
	rangeref_normalize (&v->v_range.cell, pos, &start_sheet, &end_sheet, &r);
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
			cell_eval (cell);
			return value_dup (cell->value);
		}
	}

	return value_new_error_VALUE (pos);
}

static GnmValue *
bin_arith (GnmExpr const *expr, GnmEvalPos const *ep,
	   GnmValue const *a, GnmValue const *b)
{
	if (a->type != VALUE_FLOAT && b->type != VALUE_FLOAT){
		int ia = value_get_as_int (a);
		int ib = value_get_as_int (b);
		gnm_float dres;
		int ires;

		/* FIXME: we could use simple (cheap) heuristics to
		   catch most cases where overflow will not happen.  */
		switch (expr->any.oper){
		case GNM_EXPR_OP_ADD:
			dres = (gnm_float)ia + (gnm_float)ib;
			break;

		case GNM_EXPR_OP_SUB:
			dres = (gnm_float)ia - (gnm_float)ib;
			break;

		case GNM_EXPR_OP_MULT:
			dres = (gnm_float)ia * (gnm_float)ib;
			break;

		case GNM_EXPR_OP_DIV:
			if (ib == 0)
				return value_new_error_DIV0 (ep);
			dres = (gnm_float)ia / (gnm_float)ib;
			break;

		case GNM_EXPR_OP_EXP:
			if (ia == 0 && ib <= 0)
				return value_new_error_NUM (ep);
			dres = powgnum ((gnm_float)ia, (gnm_float)ib);
			if (!finitegnum (dres))
				return value_new_error_NUM (ep);
			break;

		default:
			abort ();
		}

		ires = (int)dres;
		if (dres == ires)
			return value_new_int (ires);
		else
			return value_new_float (dres);
	} else {
		gnm_float const va = value_get_as_float (a);
		gnm_float const vb = value_get_as_float (b);

		switch (expr->any.oper){
		case GNM_EXPR_OP_ADD:
			return value_new_float (va + vb);

		case GNM_EXPR_OP_SUB:
			return value_new_float (va - vb);

		case GNM_EXPR_OP_MULT:
			return value_new_float (va * vb);

		case GNM_EXPR_OP_DIV:
			return (vb == 0.0)
				? value_new_error_DIV0 (ep)
				: value_new_float (va / vb);

		case GNM_EXPR_OP_EXP: {
			gnm_float res;
			if ((va == 0 && vb <= 0) ||
			    (va < 0 && vb != (int)vb))
				return value_new_error_NUM (ep);

			res = powgnum (va, vb);
			return finitegnum (res)
				? value_new_float (res)
				: value_new_error_NUM (ep);
		}

		default:
			break;
		}
	}
	return value_new_error (ep, _("Unknown operator"));
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
	case GNM_EXPR_OP_LTE: 	    return value_new_bool (comp != IS_GREATER);
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
	if (a != NULL && a->type == VALUE_ERROR)
		return value_dup (a);
	if (b != NULL && b->type == VALUE_ERROR)
		return value_dup (b);
	return bin_cmp (expr->any.oper, value_compare (a, b, FALSE), ep);
}

static GnmValue *
cb_bin_arith (GnmEvalPos const *ep, GnmValue const *a, GnmValue const *b,
	      GnmExpr const *expr)
{
	GnmValue *res, *va, *vb;

	if (a != NULL && a->type == VALUE_ERROR)
		return value_dup (a);
	if (b != NULL && b->type == VALUE_ERROR)
		return value_dup (b);
	if (a == NULL)
		va = value_new_int (0);
	else if (a->type == VALUE_STRING) {
		va = format_match_number (a->v_str.val->str, NULL,
			workbook_date_conv (ep->sheet->workbook));
		if (va == NULL)
			return value_new_error_VALUE (ep);
	} else if (!VALUE_IS_NUMBER (a))
		return value_new_error_VALUE (ep);
	else
		va = (GnmValue *)a;
	if (b == NULL)
		vb = value_new_int (0);
	else if (b->type == VALUE_STRING) {
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
	if (a != NULL && a->type == VALUE_ERROR)
		return value_dup (a);
	if (b != NULL && b->type == VALUE_ERROR)
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
	GnmValue *res;
	GnmValue const *a, *b;
	BinOpImplicitIteratorFunc	func;
	gpointer	user_data;
} BinOpImplicitIteratorState;

static GnmValue *
cb_implicit_iter_a_to_b (GnmValue const *v, GnmEvalPos const *ep,
			 int x, int y, BinOpImplicitIteratorState const *state)
{
	state->res->v_array.vals [x][y] = (*state->func) (ep,
		v, value_area_get_x_y (state->b, x, y, ep), state->user_data);
	return NULL;
}
static GnmValue *
cb_implicit_iter_a_to_scalar_b (GnmValue const *v, GnmEvalPos const *ep,
				int x, int y, BinOpImplicitIteratorState const *state)
{
	state->res->v_array.vals [x][y] = (*state->func) (ep,
		v, state->b, state->user_data);
	return NULL;
}
static GnmValue *
cb_implicit_iter_b_to_scalar_a (GnmValue const *v, GnmEvalPos const *ep,
				int x, int y, BinOpImplicitIteratorState const *state)
{
	state->res->v_array.vals [x][y] = (*state->func) (ep,
		state->a, v, state->user_data);
	return NULL;
}

static GnmValue *
bin_array_op (GnmEvalPos const *ep, GnmValue *sizer, GnmValue *a, GnmValue *b,
	      BinOpImplicitIteratorFunc func, gpointer user_data)
{
	BinOpImplicitIteratorState iter_info;
	
	if (sizer != a || b == NULL || b->type != VALUE_ERROR) {
		iter_info.func = func;
		iter_info.user_data = user_data;
		iter_info.a = a;
		iter_info.b = b;
		iter_info.res = value_new_array_empty (
			value_area_get_width  (sizer, ep),
			value_area_get_height (sizer, ep));
		if (sizer == b)
			value_area_foreach (b, ep, CELL_ITER_ALL,
				(ValueAreaFunc) cb_implicit_iter_b_to_scalar_a, &iter_info);
		else if (b != NULL &&
			   (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY))
			value_area_foreach (a, ep, CELL_ITER_ALL,
				(ValueAreaFunc) cb_implicit_iter_a_to_b, &iter_info);
		else
			value_area_foreach (a, ep, CELL_ITER_ALL,
				(ValueAreaFunc) cb_implicit_iter_a_to_scalar_b, &iter_info);
	} else
		/* you have to love the aymetry of MS XL */
		iter_info.res = value_new_error_VALUE (ep);
	if (a != NULL)
		value_release (a);
	if (b != NULL)
		value_release (b);
	return iter_info.res;
}

static inline GnmValue *
negate_value (GnmValue const *v)
{
	GnmValue *tmp;
	GnmFormat *fmt; 

	if (v->type == VALUE_INTEGER) {
		int i = v->v_int.val;
		if (i < 0 && -i < 0)
			tmp = value_new_float (-(gnm_float)i);
		else
			tmp = value_new_int (-i);
		fmt = VALUE_FMT (v);
	} else if (v->type == VALUE_FLOAT) {
		tmp = value_new_float (-v->v_float.val);
		fmt = VALUE_FMT (v);
	} else if (v->type == VALUE_BOOLEAN) {
		/* Silly, but XL compatible.  */
		tmp = value_new_int (v->v_bool.val ? -1 : 0);
		fmt = VALUE_FMT (v);
	} else
		return NULL;

	if (fmt != NULL) {
		VALUE_FMT (tmp) = fmt;
		style_format_ref (fmt);
	}

	return tmp;
}

static GnmValue *
cb_iter_unary_neg (GnmValue const *v, GnmEvalPos const *ep,
		   int x, int y, GnmValue *res)
{
	GnmValue *tmp = NULL;

	if (VALUE_IS_EMPTY (v))
		tmp = value_new_int (0);
	else if (v->type == VALUE_ERROR)
		tmp = value_dup (v);
	else if (v->type == VALUE_STRING) {
		GnmValue *conv = format_match_number
			(v->v_str.val->str, NULL,
			 workbook_date_conv (ep->sheet->workbook));
		if (conv != NULL) {
			tmp = negate_value (conv);
			value_release (conv);
		}
	} else
		tmp = negate_value (v);

	if (!tmp)
		tmp = value_new_error_VALUE (ep);

	res->v_array.vals [x][y] = tmp;
	return NULL;
}

static GnmValue *
cb_iter_percentage (GnmValue const *v, GnmEvalPos const *ep,
		    int x, int y, GnmValue *res)
{
	GnmValue *tmp;

	if (VALUE_IS_EMPTY (v))
		tmp = value_new_int (0);
	else if (v->type == VALUE_ERROR)
		tmp = value_dup (v);
	else {
		GnmValue *conv = NULL;
		if (v->type == VALUE_STRING) {
			conv = format_match_number (v->v_str.val->str, NULL,
						    workbook_date_conv (ep->sheet->workbook));
			if (conv != NULL)
				v = conv;
		}

		if (VALUE_IS_NUMBER (v)){
			tmp = value_new_float (value_get_as_float (v) / 100);
			VALUE_FMT (tmp) = style_format_default_percentage ();
			style_format_ref (VALUE_FMT (tmp));
		} else
			tmp = value_new_error_VALUE (ep);

		if (conv != NULL)
			value_release (conv);
	}

	res->v_array.vals [x][y] = tmp;
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

	rangeref_normalize (&a_ref, ep, &a_start, &a_end, &a_range);
	rangeref_normalize (&b_ref, ep, &b_start, &b_end, &b_range);

	if (expr->any.oper != GNM_EXPR_OP_INTERSECT)
		res_range = range_union (&a_range, &b_range);
	else if (!range_intersection  (&res_range, &a_range, &b_range))
		return value_new_error_NULL (ep);

	res = value_new_cellrange_r (a_start, &res_range);
	dependent_add_dynamic_dep (ep->dep, &res->v_range);
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

	switch (expr->any.oper){
	case GNM_EXPR_OP_EQUAL:
	case GNM_EXPR_OP_NOT_EQUAL:
	case GNM_EXPR_OP_GT:
	case GNM_EXPR_OP_GTE:
	case GNM_EXPR_OP_LT:
	case GNM_EXPR_OP_LTE:
		flags |= GNM_EXPR_EVAL_PERMIT_EMPTY;

		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (a != NULL) {
			if (a->type == VALUE_ERROR)
				return a;
			if (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)
				return bin_array_op (pos, a, a,
					gnm_expr_eval (expr->binary.value_b, pos, flags),
					(BinOpImplicitIteratorFunc) cb_bin_cmp,
					(gpointer) expr);
		}

		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (b != NULL) {
			if (b->type == VALUE_ERROR) {
				if (a != NULL)
					value_release (a);
				return b;
			}
			if (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY)
				return bin_array_op (pos, b, a, b,
					(BinOpImplicitIteratorFunc) cb_bin_cmp,
					(gpointer) expr);
		}

		res = bin_cmp (expr->any.oper, value_compare (a, b, FALSE), pos);
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
		if (a->type == VALUE_ERROR)
			return value_error_set_pos (&a->v_err, pos);

		/* 2) #!VALUE error if A is not a number */
		if (a->type == VALUE_STRING) {
			GnmValue *tmp = format_match_number (a->v_str.val->str, NULL,
				workbook_date_conv (pos->sheet->workbook));

			value_release (a);
			if (tmp == NULL)
				return value_new_error_VALUE (pos);
			a = tmp;
		} else if (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY) {
			b = gnm_expr_eval (expr->binary.value_b, pos, flags);
			if (b->type == VALUE_STRING) {
				res = format_match_number (b->v_str.val->str, NULL,
					workbook_date_conv (pos->sheet->workbook));
				value_release (b);
				b = (res == NULL) ? value_new_error_VALUE (pos) : res;
			}
			return bin_array_op (pos, a, a, b,
				(BinOpImplicitIteratorFunc) cb_bin_arith,
				(gpointer) expr);
		} else if (!VALUE_IS_NUMBER (a)) {
			value_release (a);
			return value_new_error_VALUE (pos);
		}

		/* 3) Error from B */
		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (b->type == VALUE_ERROR) {
			value_release (a);
			return value_error_set_pos (&b->v_err, pos);
		}

		/* 4) #!VALUE error if B is not a number */
		if (b->type == VALUE_STRING) {
			GnmValue *tmp = format_match_number (b->v_str.val->str, NULL,
				workbook_date_conv (pos->sheet->workbook));

			value_release (b);
			if (tmp == NULL) {
				value_release (a);
				return value_new_error_VALUE (pos);
			}
			b = tmp;
		} else if (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY)
			return bin_array_op (pos, b, a, b,
				(BinOpImplicitIteratorFunc) cb_bin_arith,
				(gpointer) expr);
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
		if (a->type == VALUE_ERROR)
			return a;
		if (expr->any.oper == GNM_EXPR_OP_UNARY_PLUS)
			return a;

		/* 2) #!VALUE error if A is not a number */
		if (a->type == VALUE_STRING) {
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
				(ValueAreaFunc) ((expr->any.oper == GNM_EXPR_OP_UNARY_NEG) 
					? cb_iter_unary_neg : cb_iter_percentage),
				res);
			value_release (a);
			return res;
		}
		if (!VALUE_IS_NUMBER (a))
			res = value_new_error_VALUE (pos);
		else if (expr->any.oper == GNM_EXPR_OP_UNARY_NEG)
			res = negate_value (a);
		else {
			res = value_new_float (value_get_as_float (a) / 100);
			VALUE_FMT (res) = style_format_default_percentage ();
			style_format_ref (VALUE_FMT (res));
		}
		value_release (a);
		return res;

	case GNM_EXPR_OP_CAT:
		flags |= GNM_EXPR_EVAL_PERMIT_EMPTY;
		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (a != NULL) {
			if (a->type == VALUE_ERROR)
				return a;
			if (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)
				return bin_array_op (pos, a, a,
					gnm_expr_eval (expr->binary.value_b, pos, flags),
					(BinOpImplicitIteratorFunc) cb_bin_cat,
					(gpointer) expr);
		}
		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (b != NULL) {
			if (b->type == VALUE_ERROR) {
				if (a != NULL)
					value_release (a);
				return b;
			}
			if (b->type == VALUE_CELLRANGE || b->type == VALUE_ARRAY)
				return bin_array_op (pos, b, a, b,
					(BinOpImplicitIteratorFunc) cb_bin_cat,
					(gpointer) expr);
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
		FunctionEvalInfo ei;
		ei.pos = pos;
		ei.func_call = (GnmExprFunction const *)expr;
		res = function_call_with_list (&ei, expr->func.arg_list, flags);
		if (res != NULL && res->type == VALUE_CELLRANGE) {
			dependent_add_dynamic_dep (pos->dep, &res->v_range);
			if (!(flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)) {
				res = value_intersection (res, pos);
				return (res != NULL)
					? handle_empty (res, flags)
					: value_new_error_VALUE (pos);
			}
			return res;
		}
		if (res == NULL)
			return (flags & GNM_EXPR_EVAL_PERMIT_EMPTY)
			    ? NULL : value_new_int (0);
		return res;
	}

	case GNM_EXPR_OP_NAME:
		if (expr->name.name->active)
			return handle_empty (expr_name_eval (expr->name.name, pos, flags), flags);
		return value_new_error_REF (pos);

	case GNM_EXPR_OP_CELLREF: {
		GnmCell *cell;
		GnmCellPos dest;

		cellref_get_abs_pos (&expr->cellref.ref, &pos->eval, &dest);

		cell = sheet_cell_get (eval_sheet (expr->cellref.ref.sheet, pos->sheet),
			dest.col, dest.row);
		if (cell == NULL)
			return handle_empty (NULL, flags);

		cell_eval (cell);

		return handle_empty (value_dup (cell->value), flags);
	}

	case GNM_EXPR_OP_CONSTANT:
		res = value_dup (expr->constant.value);
		if (res->type != VALUE_CELLRANGE)
			return handle_empty (res, flags);

		if (flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR) {
#if 0
			workbook_foreach_cell_in_range (pos, res,
				CELL_ITER_IGNORE_BLANK,
				cb_range_eval, NULL);
#endif
			return res;
		} else {
			res = value_intersection (res, pos);
			return (res != NULL)
				? handle_empty (res, flags)
				: value_new_error_VALUE (pos);
		}

	case GNM_EXPR_OP_ARRAY: {
		/* The upper left corner manages the recalc of the expr */
		int x = expr->array.x;
		int y = expr->array.y;
		if (x == 0 && y == 0){
			/* Release old value if necessary */
			a = expr->array.corner.value;
			if (a != NULL)
				value_release (a);

			a = gnm_expr_eval (expr->array.corner.expr, pos,
				flags | GNM_EXPR_EVAL_PERMIT_NON_SCALAR);

			/* Store real result (cast away const)*/
			*((GnmValue **)&(expr->array.corner.value)) = a;
		} else {
			GnmCell *corner = expr_array_corner (expr,
				pos->sheet, &pos->eval);
			if (corner != NULL) {
				cell_eval (corner);
				a = corner->base.expression->array.corner.value;
			} else
				a = NULL;
		}

		if (a != NULL &&
		    (a->type == VALUE_CELLRANGE || a->type == VALUE_ARRAY)) {
			int const num_x = value_area_get_width (a, pos);
			int const num_y = value_area_get_height (a, pos);

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
		return value_new_error_VALUE (pos);

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		return gnm_expr_range_op (expr, pos, flags);
	}

	return value_new_error (pos, _("Unknown evaluation error"));
}

static void
gnm_expr_list_as_string (GString *target,
			 GnmExprList const *list, GnmParsePos const *pp,
			 GnmExprConventions const *fmt);


/*
 * Converts a parsed tree into its string representation
 * assuming that we are evaluating at col, row
 *
 * This routine is pretty simple: it walks the GnmExpr and
 * appends a string representation to the target.
 */
static void
do_expr_as_string (GString *target, GnmExpr const *expr, GnmParsePos const *pp,
		   int paren_level, GnmExprConventions const *conv)
{
	static struct {
		char const name[4];
		guint8 prec;	                 /* Precedences -- should match parser.y  */
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
		{ "",   0, 0, 0, 0 }, /* Array    */
		{ "",   0, 0, 0, 0 }, /* Set      */
		{ ":",  9, 1, 0, 0 }, /* Range Ctor   */
		{ " ",  8, 1, 0, 0 }  /* Intersection */
	};
	int const op = expr->any.oper;

	switch (op) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY: {
		char const *opname = operations[op].name;
		int prec = operations[op].prec;
		gboolean need_par = (prec <= paren_level);
		size_t prelen = target->len;

		if (need_par) g_string_append_c (target, '(');
		do_expr_as_string (target, expr->binary.value_a, pp,
				   prec - operations[op].assoc_left, conv);

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

		g_string_append (target, opname);
		do_expr_as_string (target, expr->binary.value_b, pp,
				   prec - operations[op].assoc_right, conv);
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
		do_expr_as_string (target, expr->unary.value, pp, prec, conv);
		if (!is_prefix) g_string_append (target, opname);
		if (need_par) g_string_append_c (target, ')');
		return;
	}

	case GNM_EXPR_OP_FUNCALL: {
		GnmExprList const * const arg_list = expr->func.arg_list;
		char const *name = gnm_func_get_name (expr->func.func);

		g_string_append (target, name);
		/* FIXME: possibly a space here.  */
		gnm_expr_list_as_string (target, arg_list, pp, conv);
		return;
	}

	case GNM_EXPR_OP_NAME:
		conv->expr_name_handler (target, pp, &expr->name, conv);
		return;

	case GNM_EXPR_OP_CELLREF: {
		conv->cell_ref_handler (target, conv, &expr->cellref.ref, pp, FALSE);
		return;
	}

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;
		size_t prelen = target->len;

		if (v->type == VALUE_STRING) {
			gnm_strescape (target, v->v_str.val->str);
			return;
		}

		if (v->type == VALUE_CELLRANGE) {
			conv->range_ref_handler (target, conv, &v->v_range.cell, pp);
			return;
		}

		value_get_as_gstring (v, target, conv);

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

	case GNM_EXPR_OP_ARRAY: {
		int const x = expr->array.x;
		int const y = expr->array.y;
		if (x != 0 || y != 0) {
			GnmCell *corner = expr_array_corner (expr,
				pp->sheet, &pp->eval);
			if (corner) {
				GnmParsePos tmp_pos = *pp;
				tmp_pos.eval.col -= x;
				tmp_pos.eval.row -= y;
				do_expr_as_string (
					target,
					corner->base.expression->array.corner.expr,
					&tmp_pos, 0, conv);
			} else
				break;
		} else
			do_expr_as_string (
				target,
				expr->array.corner.expr, pp, 0, conv);
		return;
        }

	case GNM_EXPR_OP_SET: {
		gnm_expr_list_as_string (target, expr->set.set, pp, conv);
		return;
	}
	};

	g_assert_not_reached ();
	g_string_append (target, "<ERROR>");
}

char *
gnm_expr_as_string (GnmExpr const *expr, GnmParsePos const *pp,
		    GnmExprConventions const *fmt)
{
	GString *res;
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (pp != NULL, NULL);

	res = g_string_new (NULL);
	do_expr_as_string (res, expr, pp, 0, fmt);
	return g_string_free (res, FALSE);
}

void
gnm_expr_as_gstring (GString *target,
		     GnmExpr const *expr, GnmParsePos const *pp,
		     GnmExprConventions const *fmt)
{
	g_return_if_fail (expr != NULL);
	g_return_if_fail (pp != NULL);
	do_expr_as_string (target, expr, pp, 0, fmt);
}

typedef enum {
	CELLREF_NO_RELOCATE,
	CELLREF_RELOCATE_FROM_IN,
	CELLREF_RELOCATE_FROM_OUT,
	CELLREF_RELOCATE_ERR
} CellRefRelocate;

/*
 * FIXME :
 * C3 : =sum(a$1:b$2)
 * Cut B2:C3
 * Paste to the right or diagonal.
 * range changes when it should not.
 */
static CellRefRelocate
cellref_relocate (GnmCellRef *ref, GnmExprRelocateInfo const *rinfo)
{
	/* For row or column refs
	 * Ref	From	To
	 *
	 * Abs	In	In 	: Positive (Sheet) (b)
	 * Abs	In	Out 	: Sheet
	 * Abs	Out	In 	: Positive, Sheet, Range (b)
	 * Abs	Out	Out 	: (a)
	 * Rel	In	In 	: Sheet, Range
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
	gboolean to_inside, from_inside;
	int tmp;
	int col = ref->col;
	int row = ref->row;
	Sheet *ref_sheet = (ref->sheet != NULL) ? ref->sheet : rinfo->pos.sheet;

	if (ref->col_relative)
		col += rinfo->pos.eval.col;
	if (ref->row_relative)
		row += rinfo->pos.eval.row;

	/* fprintf (stderr, "%s\n", cellref_as_string (ref, &rinfo->pos, FALSE)); */

	/* All references should be valid initially.  We assume that later. */
	if (col < 0 || col >= SHEET_MAX_COLS ||
	    row < 0 || row >= SHEET_MAX_ROWS)
		return CELLREF_RELOCATE_ERR;

	/* Inside is based on the current location of the reference.
	 * Hence we need to use the ORIGIN_sheet rather than the target.
	 */
	to_inside = (rinfo->origin_sheet == ref_sheet) &&
		range_contains (&rinfo->origin, col, row);
	from_inside = (rinfo->origin_sheet == rinfo->pos.sheet) &&
		range_contains (&rinfo->origin, rinfo->pos.eval.col, rinfo->pos.eval.row);

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
		tmp = col + rinfo->col_offset;
		if (!from_inside || !ref->col_relative)
			col = tmp;
		if (tmp < 0 || tmp >= SHEET_MAX_COLS)
			return CELLREF_RELOCATE_ERR;

		tmp = row + rinfo->row_offset;
		if (!from_inside || !ref->row_relative)
			row = tmp;
		if (tmp < 0 || tmp >= SHEET_MAX_ROWS)
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

	if (ref->sheet == ref_sheet && ref->col == col && ref->row == row)
		return CELLREF_NO_RELOCATE;

	ref->sheet = ref_sheet;
	ref->col = col;
	ref->row = row;
	return from_inside ? CELLREF_RELOCATE_FROM_IN : CELLREF_RELOCATE_FROM_OUT;
}

/**
 * A utility routine that assumes @ref is from the origin sheet but is not
 * contained by the origin range, and did not require relocation.  However,
 * @ref is part of a range whose opposing corned DID require relocation.
 * So we check to see if the range should be extended using the heuristic
 * that if movement is in only 1 dimension, and that for @ref that col/row is
 * into the target range then we want to adjust the range.
 */
static gboolean
cellref_shift (GnmCellRef const *ref, GnmExprRelocateInfo const *rinfo)
{
	if (rinfo->col_offset == 0) {
		int col = ref->col;
		if (ref->col_relative)
			col += rinfo->pos.eval.col;
		return  col < rinfo->origin.start.col ||
			col > rinfo->origin.end.col;
	} else if (rinfo->row_offset == 0) {
		int row = ref->row;
		if (ref->row_relative)
			row += rinfo->pos.eval.row;
		return  row < rinfo->origin.start.row ||
			row > rinfo->origin.end.row;
	}
	return TRUE;
}

static GnmExpr const *
cellrange_relocate (GnmValue const *v, GnmExprRelocateInfo const *rinfo)
{
	/*
	 * If either end is an error then the whole range is an error.
	 * If both ends need to relocate -> relocate
	 * If either end is relcated from inside the range -> relocate
	 * 	otherwise we can end up with invalid references
	 * If only 1 end needs relocation, relocate only if movement is
	 *	in only 1 dimension, and the
	 * otherwise remain static
	 */
	GnmCellRef ref_a = v->v_range.cell.a;
	GnmCellRef ref_b = v->v_range.cell.b;
	int needs = 0;

	/* FIXME : should not be necessary.  We need to audit the code to
	 * define whether both refs need a sheet, or just ref_a for normal
	 * non-3d references */
	if (ref_b.sheet == NULL && ref_a.sheet != NULL)
		ref_b.sheet = ref_a.sheet;

	switch (cellref_relocate (&ref_a, rinfo)) {
	case CELLREF_NO_RELOCATE :	break;
	case CELLREF_RELOCATE_FROM_IN :  needs = 0x4;	break;
	case CELLREF_RELOCATE_FROM_OUT : needs = 0x1;	break;
	case CELLREF_RELOCATE_ERR : return gnm_expr_new_constant (
		value_new_error_REF (NULL));
	}
	switch (cellref_relocate (&ref_b, rinfo)) {
	case CELLREF_NO_RELOCATE :	break;
	case CELLREF_RELOCATE_FROM_IN :  needs = 0x4;	break;
	case CELLREF_RELOCATE_FROM_OUT : needs |= 0x2;	break;
	case CELLREF_RELOCATE_ERR : return gnm_expr_new_constant (
		value_new_error_REF (NULL));
	}

	if (needs != 0) {
		GnmValue *res;
		Sheet const *sheet_a = ref_a.sheet;
		Sheet const *sheet_b = ref_b.sheet;

		if (sheet_a == NULL) {
			g_return_val_if_fail (sheet_b == NULL, NULL);
			sheet_a = sheet_b = rinfo->pos.sheet;
		} else if (sheet_b == NULL)
			sheet_b = sheet_a;

		/* Dont allow creation of 3D references */
		if (sheet_a == sheet_b) {
			/* If just 1 end is moving do not change the reference */
			if ((needs == 0x1 && cellref_shift (&ref_b, rinfo)) ||
			    (needs == 0x2 && cellref_shift (&ref_a, rinfo)))
				return NULL;
			res = value_new_cellrange (&ref_a, &ref_b,
						   rinfo->pos.eval.col,
						   rinfo->pos.eval.row);
		} else
			res = value_new_error_REF (NULL);

		return gnm_expr_new_constant (res);
	}

	return NULL;
}

/*
 * gnm_expr_rewrite :
 * @expr   : Expression to fixup
 * @pos    : Location of the cell containing @expr.
 * @rwinfo : State information required to rewrite the reference.
 *
 * Either:
 *
 * GNM_EXPR_REWRITE_SHEET:
 *
 *	Find any references to rwinfo->u.sheet and re-write them to #REF!
 *
 * or
 *
 * GNM_EXPR_REWRITE_WORKBOOK:
 *
 *	Find any references to rwinfo->u.workbook re-write them to #REF!
 *
 * or
 *
 * GNM_EXPR_REWRITE_RELOCATE:
 *
 *	Find any references to the specified area and adjust them by the
 * supplied deltas.  Check for out of bounds conditions.  Return NULL if
 * no change is required.
 *
 *	If the expression is within the range to be moved, its relative
 * references to cells outside the range are adjusted to reference the
 * same cell after the move.
 */
GnmExpr const *
gnm_expr_rewrite (GnmExpr const *expr, GnmExprRewriteInfo const *rwinfo)
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (expr->any.oper) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY: {
		GnmExpr const *a = gnm_expr_rewrite (expr->binary.value_a, rwinfo);
		GnmExpr const *b = gnm_expr_rewrite (expr->binary.value_b, rwinfo);

		if (a == NULL && b == NULL)
			return NULL;

		if (a == NULL)
			gnm_expr_ref ((a = expr->binary.value_a));
		else if (b == NULL)
			gnm_expr_ref ((b = expr->binary.value_b));

		return gnm_expr_new_binary (a, expr->any.oper, b);
	}

	case GNM_EXPR_OP_ANY_UNARY: {
		GnmExpr const *a = gnm_expr_rewrite (expr->unary.value, rwinfo);
		if (a == NULL)
			return NULL;
		return gnm_expr_new_unary (expr->any.oper, a);
	}

	case GNM_EXPR_OP_FUNCALL: {
		gboolean rewrite = FALSE;
		GnmExprList *new_args = NULL;
		GnmExprList *l;

		for (l = expr->func.arg_list; l; l = l->next) {
			GnmExpr const *arg = gnm_expr_rewrite (l->data, rwinfo);
			new_args = gnm_expr_list_append (new_args, arg);
			if (arg != NULL)
				rewrite = TRUE;
		}

		if (rewrite) {
			GnmExprList *m;

			for (l = expr->func.arg_list, m = new_args; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					gnm_expr_ref ((m->data = l->data));
			}

			return gnm_expr_new_funcall (expr->func.func, new_args);
		}
		g_slist_free (new_args);
		return NULL;
	}
	case GNM_EXPR_OP_SET: {
		gboolean rewrite = FALSE;
		GnmExprList *new_set = NULL;
		GnmExprList *l;

		for (l = expr->set.set; l; l = l->next) {
			GnmExpr const *arg = gnm_expr_rewrite (l->data, rwinfo);
			new_set = gnm_expr_list_append (new_set, arg);
			if (arg != NULL)
				rewrite = TRUE;
		}

		if (rewrite) {
			GnmExprList *m;

			for (l = expr->set.set, m = new_set; l; l = l->next, m = m->next) {
				if (m->data == NULL)
					gnm_expr_ref ((m->data = l->data));
			}

			return gnm_expr_new_set (new_set);
		}
		g_slist_free (new_set);
		return NULL;
	}

	case GNM_EXPR_OP_NAME: {
		GnmNamedExpr *nexpr = expr->name.name;
		GnmExpr const *tmp;

		/* we cannot invalidate references to the name that are
		 * sitting in the undo queue, or the clipboard.  So we just
		 * flag the name as inactive and remove the reference here.
		 */
		if (!nexpr->active ||
		    (rwinfo->type == GNM_EXPR_REWRITE_SHEET && rwinfo->u.sheet == nexpr->pos.sheet) ||
		    (rwinfo->type == GNM_EXPR_REWRITE_WORKBOOK && rwinfo->u.workbook == nexpr->pos.wb))
			return gnm_expr_new_constant (value_new_error_REF (NULL));

		if (rwinfo->type != GNM_EXPR_REWRITE_RELOCATE)
			return NULL;

		/* If the name is not officially scoped, check that it is
		 * available in the new scope ?  */
		if (expr->name.optional_scope == NULL &&
		    rwinfo->u.relocate.target_sheet != rwinfo->u.relocate.origin_sheet) {
			GnmNamedExpr *new_nexpr;
			GnmParsePos pos;
			parse_pos_init_sheet (&pos,
				rwinfo->u.relocate.target_sheet);

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
		}

		/* Do NOT rewrite the name.  Just invalidate the use of the name */
		tmp = gnm_expr_rewrite (expr->name.name->expr, rwinfo);
		if (tmp != NULL) {
			gnm_expr_unref (tmp);
			return gnm_expr_new_constant (
				value_new_error_REF (NULL));
		}

		return NULL;
	}

	case GNM_EXPR_OP_CELLREF:
		switch (rwinfo->type) {
		case GNM_EXPR_REWRITE_SHEET :
			if (expr->cellref.ref.sheet == rwinfo->u.sheet)
				return gnm_expr_new_constant (value_new_error_REF (NULL));
			return NULL;

		case GNM_EXPR_REWRITE_WORKBOOK :
			if (expr->cellref.ref.sheet != NULL &&
			    expr->cellref.ref.sheet->workbook == rwinfo->u.workbook)
				return gnm_expr_new_constant (value_new_error_REF (NULL));
			return NULL;

		default : {
			GnmCellRef res = expr->cellref.ref; /* Copy */

			switch (cellref_relocate (&res, &rwinfo->u.relocate)) {
			case CELLREF_NO_RELOCATE :
				return NULL;
			case CELLREF_RELOCATE_FROM_IN :
			case CELLREF_RELOCATE_FROM_OUT :
				return gnm_expr_new_cellref (&res);
			case CELLREF_RELOCATE_ERR :
				return gnm_expr_new_constant (value_new_error_REF (NULL));
			}
		}
		}
		return NULL;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;

		if (v->type == VALUE_CELLRANGE) {
			GnmCellRef const *ref_a = &v->v_range.cell.a;
			GnmCellRef const *ref_b = &v->v_range.cell.b;

			if (rwinfo->type == GNM_EXPR_REWRITE_SHEET) {
				GnmValue *v = NULL;

				if (ref_a->sheet == rwinfo->u.sheet) {
					if (ref_b->sheet != NULL &&
					    ref_b->sheet != rwinfo->u.sheet) {
						GnmCellRef new_a = *ref_a;
						new_a.sheet = workbook_sheet_by_index (ref_a->sheet->workbook,
							(ref_a->sheet->index_in_wb < ref_b->sheet->index_in_wb)
							? ref_a->sheet->index_in_wb + 1
							: ref_a->sheet->index_in_wb - 1);
						v = value_new_cellrange_unsafe (&new_a, ref_b);
					}
				} else if (ref_b->sheet == rwinfo->u.sheet) {
					GnmCellRef new_b = *ref_b;
					new_b.sheet = workbook_sheet_by_index (ref_b->sheet->workbook,
						(ref_b->sheet->index_in_wb > ref_a->sheet->index_in_wb)
						? ref_b->sheet->index_in_wb - 1
						: ref_b->sheet->index_in_wb + 1);
					v = value_new_cellrange_unsafe (ref_a, &new_b);
				} else
					return NULL;
				if (v == NULL)
					v = value_new_error_REF (NULL);
				return gnm_expr_new_constant (v);

			} else if (rwinfo->type == GNM_EXPR_REWRITE_WORKBOOK) {
				if (ref_a->sheet != NULL &&
				    ref_a->sheet->workbook == rwinfo->u.workbook)
					return gnm_expr_new_constant (value_new_error_REF (NULL));
				if (ref_b->sheet != NULL &&
				    ref_b->sheet->workbook == rwinfo->u.workbook)
					return gnm_expr_new_constant (value_new_error_REF (NULL));
				return NULL;
			} else
				return cellrange_relocate (v, &rwinfo->u.relocate);
		}

		return NULL;
	}

	case GNM_EXPR_OP_ARRAY: {
		GnmExprArray const *a = &expr->array;
		if (a->x == 0 && a->y == 0) {
			GnmExpr const *func = gnm_expr_rewrite (a->corner.expr, rwinfo);
			if (func != NULL)
				return gnm_expr_new_array (0, 0, a->cols, a->rows, func);
		}
		return NULL;
	}
	};

	g_assert_not_reached ();
	return NULL;
}

GnmFunc *
gnm_expr_get_func_def (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (expr->any.oper == GNM_EXPR_OP_FUNCALL, NULL);

	return expr->func.func;
}

/**
 * gnm_expr_first_func :
 * @expr :
 *
 */
GnmExpr const *
gnm_expr_first_func (GnmExpr const *expr)
{
	GnmExpr const *tmp;

	g_return_val_if_fail (expr != NULL, NULL);

	switch (expr->any.oper) {
	default :
	case GNM_EXPR_OP_NAME:
	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_CONSTANT:
		return NULL;

	case GNM_EXPR_OP_FUNCALL:
		return expr;

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		tmp = gnm_expr_first_func (expr->binary.value_a);
		if (tmp != NULL)
			return tmp;
		return gnm_expr_first_func (expr->binary.value_b);

	case GNM_EXPR_OP_ANY_UNARY:
		return gnm_expr_first_func (expr->unary.value);

	case GNM_EXPR_OP_ARRAY:
		return gnm_expr_first_func (expr->array.corner.expr);
	}

	g_assert_not_reached ();
	return NULL;
}

static void
cellref_boundingbox (GnmCellRef const *cr, GnmRange *bound)
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
do_referenced_sheets (GnmExpr const *expr, GSList *sheets)
{
	switch (expr->any.oper) {
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
		GnmExprList *l;
		for (l = expr->func.arg_list; l; l = l->next)
			sheets = do_referenced_sheets (l->data, sheets);
		return sheets;
	}
	case GNM_EXPR_OP_SET: {
		GnmExprList *l;
		for (l = expr->set.set; l; l = l->next)
			sheets = do_referenced_sheets (l->data, sheets);
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

	/* constant arrays can only contain simple values, no references */
	case GNM_EXPR_OP_ARRAY:
		break;
	}
	return sheets;
}

/**
 * gnm_expr_referenced_sheets :
 * @expr :
 * @sheets : usually NULL.
 *
 * Generates a list of the sheets referenced by the supplied expression.
 * Caller must free the list.
 */
GSList *
gnm_expr_referenced_sheets (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	return do_referenced_sheets (expr, NULL);
}

/**
 * gnm_expr_containts_subtotal :
 * @expr :
 *
 * return TRUE if the expression calls the SUBTOTAL function
 **/
gboolean
gnm_expr_containts_subtotal (GnmExpr const *expr)
{
	switch (expr->any.oper) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return gnm_expr_containts_subtotal (expr->binary.value_a) ||
		       gnm_expr_containts_subtotal (expr->binary.value_b);
	case GNM_EXPR_OP_ANY_UNARY:
		return gnm_expr_containts_subtotal (expr->unary.value);

	case GNM_EXPR_OP_FUNCALL: {
		GnmExprList *l;
		if (!strcmp (expr->func.func->name, "subtotal"))
			return TRUE;
		for (l = expr->func.arg_list; l; l = l->next)
			if (gnm_expr_containts_subtotal (l->data))
				return TRUE;
		return FALSE;
	}
	case GNM_EXPR_OP_SET: {
		GnmExprList *l;
		for (l = expr->set.set; l; l = l->next)
			if (gnm_expr_containts_subtotal (l->data))
				return TRUE;
		return FALSE;
	}

	case GNM_EXPR_OP_NAME:
		if (expr->name.name->active)
			return gnm_expr_containts_subtotal (expr->name.name->expr);

	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_CONSTANT:
	case GNM_EXPR_OP_ARRAY:
		;
	}
	return FALSE;
}

/**
 * gnm_expr_get_boundingbox :
 *
 * Returns the range of cells in which the expression can be used without going
 * out of bounds.
 */
void
gnm_expr_get_boundingbox (GnmExpr const *expr, GnmRange *bound)
{
	g_return_if_fail (expr != NULL);

	switch (expr->any.oper) {
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
		GnmExprList *l;
		for (l = expr->func.arg_list; l; l = l->next)
			gnm_expr_get_boundingbox (l->data, bound);
		break;
	}
	case GNM_EXPR_OP_SET: {
		GnmExprList *l;
		for (l = expr->set.set; l; l = l->next)
			gnm_expr_get_boundingbox (l->data, bound);
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

	case GNM_EXPR_OP_ARRAY: {
		GnmExprArray const *a = &expr->array;
		if (a->x == 0 && a->y == 0)
			gnm_expr_get_boundingbox (a->corner.expr, bound);
		break;
	}
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

	switch (expr->any.oper) {
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
		return gnm_expr_get_range (expr->name.name->expr);

	default:
		return NULL;
	}
}

/**
 * gnm_expr_get_ranges:
 * @expr :
 *
 * A collect the set of rangerefs in @expr.
 * Return a list of the unique references.
 * Caller is responsible for releasing.
 */
GSList *
gnm_expr_get_ranges (GnmExpr const *expr)
{
#warning implement this
	/* GHashTable *singles, *ranges; */

	g_return_val_if_fail (expr != NULL, NULL);

	switch (expr->any.oper) {
	case GNM_EXPR_OP_CELLREF :
		return NULL;

	case GNM_EXPR_OP_CONSTANT:
		return NULL;

	case GNM_EXPR_OP_NAME:
		if (!expr->name.name->active)
			return NULL;
		return NULL;

	default:
		return NULL;
	}
}

/**
 * gnm_expr_get_constant:
 * @expr :
 *
 * If this expression consists of just a constant, return it.
 */
GnmValue const *
gnm_expr_get_constant (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);

	if (expr->any.oper != GNM_EXPR_OP_CONSTANT)
		return NULL;

	return expr->constant.value;
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

	switch (expr->any.oper) {
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
			return gnm_expr_is_rangeref (expr->name.name->expr);
		return FALSE;

	case GNM_EXPR_OP_ARRAY: /* I don't think this is possible */
	default :
		return FALSE;
	};
}

gboolean
gnm_expr_is_err (GnmExpr const *expr, GnmStdError err)
{
	GnmStdError err2;
	g_return_val_if_fail (expr != NULL, FALSE);

	if (expr->any.oper != GNM_EXPR_OP_CONSTANT)
		return FALSE;

	err2 = value_error_classify (expr->constant.value);
	return err == err2;
}

void
gnm_expr_list_unref (GnmExprList *list)
{
	GnmExprList *l;
	for (l = list; l; l = l->next)
		do_gnm_expr_unref (l->data);
	gnm_expr_list_free (list);
}

gboolean
gnm_expr_list_equal (GnmExprList const *la, GnmExprList const *lb)
{
	for (; la != NULL && lb != NULL; la = la->next, lb = lb->next)
		if (!gnm_expr_equal (la->data, lb->data))
			return FALSE;
	return (la == NULL) && (lb == NULL);
}

/* Same as above, but uses pointer equality.  */
static gboolean
gnm_expr_list_eq (GnmExprList const *la, GnmExprList const *lb)
{
	for (; la != NULL && lb != NULL; la = la->next, lb = lb->next)
		if (la->data != lb->data)
			return FALSE;
	return (la == NULL) && (lb == NULL);
}

static void
gnm_expr_list_as_string (GString *target,
			 GnmExprList const *list, GnmParsePos const *pp,
			 GnmExprConventions const *conv)
{
	char const *sep;
	char arg_sep[2];
	if (conv->output_argument_sep)
		sep = conv->output_argument_sep;
	else {
		arg_sep[0] = format_get_arg_sep ();
		arg_sep[1] = 0;
		sep = arg_sep;
	}

	g_string_append_c (target, '(');
	while (list) {
		do_expr_as_string (target, list->data, pp, 0, conv);
		if (list->next) g_string_append (target, sep);
		list = list->next;
	}
	g_string_append_c (target, ')');
}

/***************************************************************************/

/*
 * Special hash function for expressions that assumes that equal
 * sub-expressions are pointer-equal.  (Thus no need for recursion.)
 */
static guint
ets_hash (gconstpointer key)
{
	GnmExpr const *expr = (GnmExpr const *)key;
	guint h = (guint)(expr->any.oper);

	switch (expr->any.oper){
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_ANY_BINARY:
		return ((GPOINTER_TO_UINT (expr->binary.value_a) * 7) ^
			(GPOINTER_TO_UINT (expr->binary.value_b) * 3) ^
			h);

	case GNM_EXPR_OP_ANY_UNARY:
		return ((GPOINTER_TO_UINT (expr->unary.value) * 7) ^
			h);

	case GNM_EXPR_OP_FUNCALL: {
		GnmExprList *l;

		for (l = expr->func.arg_list; l; l = l->next)
			h = (h * 3) ^ (GPOINTER_TO_UINT (l->data));
		return h;
	}

	case GNM_EXPR_OP_SET: {
		GnmExprList *l;

		for (l = expr->set.set; l; l = l->next)
			h = (h * 3) ^ (GPOINTER_TO_UINT (l->data));
		return h;
	}

	case GNM_EXPR_OP_CONSTANT:
		return value_hash (expr->constant.value);

	case GNM_EXPR_OP_NAME:
		/* all we need is a somewhat unique hash, ignore int != ptr */
		return (guint)(expr->name.name);

	case GNM_EXPR_OP_CELLREF:
		return cellref_hash (&expr->cellref.ref);

	case GNM_EXPR_OP_ARRAY:
		break;
	}
	return h;
}

/*
 * Special equality function for expressions that assumes that equal
 * sub-expressions are pointer-equal.  (Thus no need for recursion.)
 */
static gboolean
ets_equal (gconstpointer _a, gconstpointer _b)
{
	GnmExpr const *ea = _a;
	GnmExpr const *eb = _b;

	if (ea->any.oper != eb->any.oper)
		return FALSE;

	switch (ea->any.oper){
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return (ea->binary.value_a == eb->binary.value_a &&
			ea->binary.value_b == eb->binary.value_b);
	case GNM_EXPR_OP_ANY_UNARY:
		return (ea->unary.value == eb->unary.value);
	case GNM_EXPR_OP_FUNCALL:
		return (ea->func.func == eb->func.func &&
			gnm_expr_list_eq (ea->func.arg_list, eb->func.arg_list));
	case GNM_EXPR_OP_SET:
		return gnm_expr_list_eq (ea->set.set, eb->set.set);

	default:
		/* No sub-expressions.  */
		return gnm_expr_equal (ea, eb);
	}
}


ExprTreeSharer *
expr_tree_sharer_new (void)
{
	ExprTreeSharer *es = g_new (ExprTreeSharer, 1);
	es->nodes_in = es->nodes_stored = 0;
	es->exprs = g_hash_table_new (ets_hash, ets_equal);
	es->ptrs = g_hash_table_new (g_direct_hash, g_direct_equal);
	return es;
}

static void
cb_ets_unref_key (gpointer key, G_GNUC_UNUSED gpointer value,
		  G_GNUC_UNUSED gpointer user_data)
{
	GnmExpr *e = key;
	gnm_expr_unref (e);
}


void
expr_tree_sharer_destroy (ExprTreeSharer *es)
{
	g_hash_table_foreach (es->exprs, cb_ets_unref_key, NULL);
	g_hash_table_destroy (es->exprs);
	g_hash_table_foreach (es->ptrs, cb_ets_unref_key, NULL);
	g_hash_table_destroy (es->ptrs);
	g_free (es);
}

GnmExpr const *
expr_tree_sharer_share (ExprTreeSharer *es, GnmExpr const *e)
{
	GnmExpr const *e2;
	gboolean wasshared;

	g_return_val_if_fail (es != NULL, NULL);
	g_return_val_if_fail (e != NULL, NULL);

	wasshared = (e->any.ref_count > 1);
	if (wasshared) {
		e2 = g_hash_table_lookup (es->ptrs, e);
		if (e2 != NULL) {
			gnm_expr_ref (e2);
			gnm_expr_unref (e);
			return e2;
		}
	}

	es->nodes_in++;

	/* First share all sub-expressions.  */
	switch (e->any.oper) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_ANY_BINARY:
		((GnmExpr*)e)->binary.value_a =
			expr_tree_sharer_share (es, e->binary.value_a);
		((GnmExpr*)e)->binary.value_b =
			expr_tree_sharer_share (es, e->binary.value_b);
		break;

	case GNM_EXPR_OP_ANY_UNARY:
		((GnmExpr*)e)->unary.value =
			expr_tree_sharer_share (es, e->unary.value);
		break;

	case GNM_EXPR_OP_FUNCALL: {
		GnmExprList *l;

		for (l = e->func.arg_list; l; l = l->next)
			l->data = (gpointer)expr_tree_sharer_share (es, l->data);
		break;
	}

	case GNM_EXPR_OP_SET: {
		GnmExprList *l;

		for (l = e->set.set; l; l = l->next)
			l->data = (gpointer)expr_tree_sharer_share (es, l->data);
		break;
	}

	case GNM_EXPR_OP_ARRAY:
		/*
		 * I don't want to deal with the complications of arrays
		 * right here.  Non-corners must point to the corner.
		 */
		return e;

	default:
		break; /* Nothing -- no sub-expressions.  */
	}

	/* Now look in the hash table.  */
	e2 = g_hash_table_lookup (es->exprs, e);
	if (e2 == NULL) {
#if 0
		GnmParsePos pp;
		char *s;

		pp.eval.col = 0;
		pp.eval.row = 0;
		pp.sheet = NULL;
		pp.wb = NULL;
		s = gnm_expr_as_string (e, &pp, gnm_expr_conventions_default);
		g_print ("N %p %d -- %u [%s]\n",
			 e, e->any.ref_count,
			 ets_hash (e),
			 s);
		g_free (s);
#endif

		/* Not there -- insert it.  */
		gnm_expr_ref (e);
		es->nodes_stored++;
		g_hash_table_insert (es->exprs, (gpointer)e, (gpointer)e);
		e2 = e;
	} else {
#if 0
		GnmParsePos pp;
		char *s;

		pp.eval.col = 0;
		pp.eval.row = 0;
		pp.sheet = NULL;
		pp.wb = NULL;
		s = gnm_expr_as_string (e, &pp, gnm_expr_conventions_default);
		g_print ("S %p %d -- %u %p %d [%s]\n",
			 e, e->any.ref_count,
			 ets_hash (e),
			 e2, e2->any.ref_count,
			 s);
		g_free (s);
#endif

		/* Found -- share the stored value.  */
		gnm_expr_ref (e2);
		gnm_expr_unref (e);
	}

	/*
	 * Note: we have to use a variable for this because a non-shared node
	 * might not exist anymore.
	 */
	if (wasshared) {
		gnm_expr_ref (e);
		g_hash_table_insert (es->ptrs, (gpointer)e, (gpointer)e2);
	}

	return e2;
}

/***************************************************************************/

void
expr_init (void)
{
#if USE_EXPR_POOLS
	expression_pool =
		gnm_mem_chunk_new ("expression pool",
				   sizeof (GnmExpr),
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
	s = gnm_expr_as_string (expr, &pp, gnm_expr_conventions_default);
	fprintf (stderr, "Leaking expression at %p: %s.\n", expr, s);
	g_free (s);
}
#endif

void
expr_shutdown (void)
{
#if USE_EXPR_POOLS
	gnm_mem_chunk_foreach_leak (expression_pool, cb_expression_pool_leak, NULL);
	gnm_mem_chunk_destroy (expression_pool, FALSE);
	expression_pool = NULL;
#endif
}

/****************************************************************************/
