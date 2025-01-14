/*
 * expr.c : Expression evaluation in Gnumeric
 *
 * Copyright (C) 2001-2006 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1998-2000 Miguel de Icaza (miguel@gnu.org)
 * Copyright (C) 2000-2018 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <gnumeric.h>
#include <expr.h>

#include <expr-impl.h>
#include <expr-name.h>
#include <dependent.h>
#include <application.h>
#include <func.h>
#include <cell.h>
#include <sheet.h>
#include <value.h>
#include <parse-util.h>
#include <ranges.h>
#include <number-match.h>
#include <workbook.h>
#include <gutils.h>
#include <parse-util.h>
#include <mathfunc.h>

#include <goffice/goffice.h>
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
 * gnm_expr_new_constant:
 * @v: (transfer full): #GnmValue
 *
 * Returns: (transfer full): constant expression.
 **/
GnmExpr const *
gnm_expr_new_constant (GnmValue *v)
{
	GnmExprConstant *ans;

	g_return_val_if_fail (v != NULL, NULL);

	ans = CHUNK_ALLOC (GnmExprConstant, expression_pool_small);
	if (!ans)
		return NULL;
	gnm_expr_constant_init (ans, v);

	return (GnmExpr *)ans;
}

/***************************************************************************/

/**
 * gnm_expr_new_funcallv: (skip)
 * @func: #GnmFunc
 * @argc: argument count
 * @argv: (in) (transfer full) (array length=argc): transfers everything
 *
 * Returns: (transfer full): function call expression.
 */
static GnmExpr const *
gnm_expr_new_funcallv (GnmFunc *func, int argc, GnmExprConstPtr *argv)
{
	GnmExprFunction *ans;
	g_return_val_if_fail (func, NULL);

	ans = CHUNK_ALLOC (GnmExprFunction, expression_pool_small);

	ans->oper = GNM_EXPR_OP_FUNCALL;
	gnm_func_inc_usage (func);
	ans->func = func;
	ans->argc = argc;
	ans->argv = argv;

	return (GnmExpr *)ans;
}

/**
 * gnm_expr_new_funcall:
 * @func: #GnmFunc
 * @args: (transfer full): argument list
 *
 * Returns: (transfer full): function call expression.
 */
GnmExpr const *
gnm_expr_new_funcall (GnmFunc *func, GnmExprList *args)
{
	int argc = gnm_expr_list_length (args);
	GnmExprConstPtr *argv = NULL;

	if (args) {
		GnmExprList *args0 = args;
		int i = 0;

		argv = g_new (GnmExprConstPtr, argc);
		for (; args; args = args->next)
			argv[i++] = args->data;
		gnm_expr_list_free (args0);
	}

	return gnm_expr_new_funcallv (func, argc, argv);
}

/**
 * gnm_expr_new_funcall1:
 * @func: #GnmFunc
 * @arg0: (transfer full): argument
 *
 * Returns: (transfer full): function call expression.
 */
GnmExpr const *
gnm_expr_new_funcall1 (GnmFunc *func,
		       GnmExpr const *arg0)
{
	GnmExprConstPtr *argv = g_new (GnmExprConstPtr, 1);
	argv[0] = arg0;
	return gnm_expr_new_funcallv (func, 1, argv);
}

/**
 * gnm_expr_new_funcall2:
 * @func: #GnmFunc
 * @arg0: (transfer full): argument
 * @arg1: (transfer full): argument
 *
 * Returns: (transfer full): function call expression.
 */
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

/**
 * gnm_expr_new_funcall3:
 * @func: #GnmFunc
 * @arg0: (transfer full): argument
 * @arg1: (transfer full): argument
 * @arg2: (transfer full): argument
 *
 * Returns: (transfer full): function call expression.
 */
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

/**
 * gnm_expr_new_funcall4:
 * @func: #GnmFunc
 * @arg0: (transfer full): argument
 * @arg1: (transfer full): argument
 * @arg2: (transfer full): argument
 * @arg3: (transfer full): argument
 *
 * Returns: (transfer full): function call expression.
 */
GnmExpr const *
gnm_expr_new_funcall4 (GnmFunc *func,
		       GnmExpr const *arg0,
		       GnmExpr const *arg1,
		       GnmExpr const *arg2,
		       GnmExpr const *arg3)
{
	GnmExprConstPtr *argv = g_new (GnmExprConstPtr, 4);
	argv[0] = arg0;
	argv[1] = arg1;
	argv[2] = arg2;
	argv[3] = arg3;
	return gnm_expr_new_funcallv (func, 4, argv);
}

/**
 * gnm_expr_new_funcall5:
 * @func: #GnmFunc
 * @arg0: (transfer full): argument
 * @arg1: (transfer full): argument
 * @arg2: (transfer full): argument
 * @arg3: (transfer full): argument
 * @arg4: (transfer full): argument
 *
 * Returns: (transfer full): function call expression.
 */
GnmExpr const *
gnm_expr_new_funcall5 (GnmFunc *func,
		       GnmExpr const *arg0,
		       GnmExpr const *arg1,
		       GnmExpr const *arg2,
		       GnmExpr const *arg3,
		       GnmExpr const *arg4)
{
	GnmExprConstPtr *argv = g_new (GnmExprConstPtr, 5);
	argv[0] = arg0;
	argv[1] = arg1;
	argv[2] = arg2;
	argv[3] = arg3;
	argv[4] = arg4;
	return gnm_expr_new_funcallv (func, 5, argv);
}


/***************************************************************************/

/**
 * gnm_expr_new_unary:
 * @op: Unary operator
 * @e: (transfer full): #GnmExpr
 *
 * Returns: (transfer full): Unary expression
 */
GnmExpr const *
gnm_expr_new_unary (GnmExprOp op, GnmExpr const *e)
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

/**
 * gnm_expr_new_binary:
 * @l: (transfer full): left operand.
 * @op: Unary operator
 * @r: (transfer full): right operand.
 *
 * Returns: (transfer full): Binary expression
 */
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

/**
 * gnm_expr_new_cellref:
 * @cr: (transfer none): cell reference
 *
 * Returns: (transfer full): expression referencing @cr.
 */
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
 * gnm_expr_is_array:
 * @expr: #GnmExpr
 *
 * Returns: %TRUE if @expr is an array expression, either a corner or a
 * non-corner element.
 */
static gboolean
gnm_expr_is_array (GnmExpr const *expr)
{
	return expr &&
		(GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_ARRAY_ELEM ||
		 GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_ARRAY_CORNER);
}

/**
 * gnm_expr_new_array_corner:
 * @cols: Number of columns
 * @rows: Number of rows
 * @expr: (transfer full) (nullable): #GnmExpr
 *
 * Returns: (transfer full): An array corner expression
 **/
static GnmExpr const *
gnm_expr_new_array_corner (int cols, int rows, GnmExpr const *expr)
{
	GnmExprArrayCorner *ans;

	g_return_val_if_fail (!gnm_expr_is_array (expr), NULL);

	ans = CHUNK_ALLOC (GnmExprArrayCorner, expression_pool_big);
	ans->oper = GNM_EXPR_OP_ARRAY_CORNER;
	ans->rows = rows;
	ans->cols = cols;
	ans->value = NULL;
	ans->expr = expr;
	return (GnmExpr *)ans;
}

/**
 * gnm_expr_new_array_elem:
 * @x: Column number relative to corner
 * @y: Row number relative to corner
 *
 * Returns: (transfer full): An array non-corner expression
 **/
static GnmExpr const *
gnm_expr_new_array_elem  (int x, int y)
{
	GnmExprArrayElem *ans;

	ans = CHUNK_ALLOC (GnmExprArrayElem, expression_pool_small);
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

/**
 * gnm_expr_new_set:
 * @args: (transfer full): element list
 *
 * Returns: (transfer full): set expression.
 */
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

/**
 * gnm_expr_new_range_ctor:
 * @l: (transfer full): start range
 * @r: (transfer full): end range
 *
 * This function builds a range constructor or something simpler,
 * but equivalent, if the arguments allow it.
 *
 * Returns: (transfer full): And expression referencing @l to @r.
 **/
GnmExpr const *
gnm_expr_new_range_ctor (GnmExpr const *l, GnmExpr const *r)
{
	GnmValue *v;

	g_return_val_if_fail (l != NULL, NULL);
	g_return_val_if_fail (r != NULL, NULL);

	if (GNM_EXPR_GET_OPER (l) != GNM_EXPR_OP_CELLREF)
		goto fallback;
	if (GNM_EXPR_GET_OPER (r) != GNM_EXPR_OP_CELLREF)
		goto fallback;

	v = value_new_cellrange_unsafe (&l->cellref.ref, &r->cellref.ref);
	gnm_expr_free (l);
	gnm_expr_free (r);
	return gnm_expr_new_constant (v);

 fallback:
	return gnm_expr_new_binary (l, GNM_EXPR_OP_RANGE_CTOR, r);
}

/***************************************************************************/

/**
 * gnm_expr_copy:
 * @expr: (transfer none): #GnmExpr
 *
 * Returns: (transfer full): A deep copy of @expr.
 **/
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

/**
 * gnm_expr_free:
 * @expr: (transfer full): #GnmExpr
 *
 * Deletes @expr with all its subexpressions.
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
		gnm_func_dec_usage (expr->func.func);
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
		value_release (expr->array_corner.value);
		// A proper corner will not have NULL here, but we explicitly allow it
		// during construction, so allow it here too.
		if (expr->array_corner.expr)
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

GType
gnm_expr_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmExpr",
			 (GBoxedCopyFunc)gnm_expr_copy,
			 (GBoxedFreeFunc)gnm_expr_free);
	}
	return t;
}

GType
gnm_expr_array_corner_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmExprArrayCorner",
			 (GBoxedCopyFunc)gnm_expr_copy,
			 (GBoxedFreeFunc)gnm_expr_free);
	}
	return t;
}

/**
 * gnm_expr_equal:
 * @a: first #GnmExpr
 * @b: first #GnmExpr
 *
 * Returns: %TRUE, if the supplied expressions are exactly the
 *   same and %FALSE otherwise.  No eval position is used to see if they
 *   are effectively the same.  Named expressions must refer the same name,
 *   having equivalent names is insufficient.
 */
gboolean
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
	g_return_val_if_fail (GNM_IS_EXPR_TOP (corner->base.texpr), NULL);

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
		ei.flags = flags;
		v = function_call_with_exprs (&ei);

		if (v != NULL) {
			if (VALUE_IS_CELLRANGE (v)) {
				*res = v->v_range.cell;
				failed = FALSE;
			}
			value_release (v);
		}
		return failed;
	}

	case GNM_EXPR_OP_CELLREF:
		res->a = expr->cellref.ref;
		res->b = expr->cellref.ref;
		return FALSE;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;
		if (VALUE_IS_CELLRANGE (v)) {
			*res = v->v_range.cell;
			return FALSE;
		}
		return TRUE;
	}

	case GNM_EXPR_OP_NAME:
		if (!expr_name_is_active (expr->name.name))
			return TRUE;
		return gnm_expr_extract_ref (res, expr->name.name->texpr->expr,
					     pos, flags);
	default:
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
 * value_intersection:
 * @v: (transfer full): a VALUE_CELLRANGE or VALUE_ARRAY
 * @pos:
 *
 * Handle the implicit union of a single row or column with the eval position.
 *
 * NOTE : We do not need to know if this is expression is being evaluated as an
 * array or not because we can differentiate based on the required type for the
 * argument.
 *
 * Always release the value passed in.
 *
 * NOTE: This should match link_unlink_constant.
 *
 * Return value:
 *     If the intersection succeeded return a duplicate of the value
 *     at the intersection point.  This value needs to be freed.
 *     %NULL if there is no intersection
 * Returns the upper left corner of an array.
 **/
static GnmValue *
value_intersection (GnmValue *v, GnmEvalPos const *pos)
{
	GnmValue *res = NULL;
	GnmRange r;
	Sheet *start_sheet, *end_sheet;
	gboolean found = FALSE;

	if (VALUE_IS_ARRAY (v)) {
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

		if (pos->dep && !dependent_is_cell (pos->dep)) {
			/* See bug #142412.  */
			col = r.start.col;
			row = r.start.row;
			found = TRUE;
		} else if (range_is_singleton (&r)) {
			// A single cell
			col = r.start.col;
			row = r.start.row;
			found = TRUE;
		} else if (r.start.row == r.end.row &&
			   r.start.col <= col && col <= r.end.col) {
			// A horizontal sliver
			row = r.start.row;
			found = TRUE;
		} else if (r.start.col == r.end.col &&
			   r.start.row <= row && row <= r.end.row) {
			// A vertical sliver
			col = r.start.col;
			found = TRUE;
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
		if (vb == 0)
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
		va = format_match_number (value_peek_string (a), NULL,
			sheet_date_conv (ep->sheet));
		if (va == NULL)
			return value_new_error_VALUE (ep);
	} else if (!VALUE_IS_NUMBER (a))
		return value_new_error_VALUE (ep);
	else
		va = (GnmValue *)a;
	if (VALUE_IS_EMPTY (b))
		b = vb = (GnmValue *)value_zero;
	else if (VALUE_IS_STRING (b)) {
		vb = format_match_number (value_peek_string (b), NULL,
			sheet_date_conv (ep->sheet));
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

	/* a must be a cellrange or array, it cannot be NULL */
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
	    (VALUE_IS_CELLRANGE (b) || VALUE_IS_ARRAY (b))) {
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

	/* b must be a cellrange or array, it cannot be NULL */
	iter_info.res = value_new_array_empty (
		value_area_get_width  (b, ep),
		value_area_get_height (b, ep));
	value_area_foreach (b, ep, CELL_ITER_ALL,
		(GnmValueIterFunc) cb_implicit_iter_b_to_scalar_a, &iter_info);
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
			sheet_date_conv (v_iter->ep->sheet));
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
				sheet_date_conv (v_iter->ep->sheet));
			if (conv != NULL)
				v = conv;
		}

		if (VALUE_IS_NUMBER (v)){
			tmp = value_new_float (value_get_as_float (v) / 100);
			value_set_fmt (tmp, go_format_default_percentage ());
		} else
			tmp = value_new_error_VALUE (v_iter->ep);

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

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
		res_range = range_union (&a_range, &b_range);
		/* b_range might be on a bigger sheet.  */
		range_ensure_sanity (&res_range, a_start);
		break;
	case GNM_EXPR_OP_INTERSECT:
		/* 3D references not allowed.  */
		if (a_start != a_end || b_start != b_end)
			return value_new_error_VALUE (ep);

		/* Must be same sheet.  */
		if (a_start != b_start)
			return value_new_error_VALUE (ep);

		if (!range_intersection  (&res_range, &a_range, &b_range))
			return value_new_error_NULL (ep);
		break;
	default:
		g_assert_not_reached ();
		return NULL;
	}

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
 * gnm_expr_eval:
 * @expr: #GnmExpr
 * @pos: evaluation position
 * @flags: #GnmExprEvalFlags
 *
 * Evaluatates the given expression.  If GNM_EXPR_EVAL_PERMIT_EMPTY is not set
 * then return zero if the expression instead of the empty value, or the value
 * of an unused cell.
 *
 * Returns: (transfer full): result.
 **/
GnmValue *
gnm_expr_eval (GnmExpr const *expr, GnmEvalPos const *pos,
	       GnmExprEvalFlags flags)
{
	GnmValue *res = NULL, *a = NULL, *b = NULL;

	g_return_val_if_fail (expr != NULL, handle_empty (NULL, flags));
	g_return_val_if_fail (pos != NULL, handle_empty (NULL, flags));

 retry:
	switch (GNM_EXPR_GET_OPER (expr)){
	case GNM_EXPR_OP_EQUAL:
	case GNM_EXPR_OP_NOT_EQUAL:
	case GNM_EXPR_OP_GT:
	case GNM_EXPR_OP_GTE:
	case GNM_EXPR_OP_LT:
	case GNM_EXPR_OP_LTE:
		flags |= GNM_EXPR_EVAL_PERMIT_EMPTY;
		flags &= ~GNM_EXPR_EVAL_WANT_REF;

		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (a != NULL) {
			if (VALUE_IS_ERROR (a))
				return a;
			if (VALUE_IS_CELLRANGE (a) || VALUE_IS_ARRAY (a))
				return bin_array_iter_a (pos, a,
					gnm_expr_eval (expr->binary.value_b, pos, flags),
					(BinOpImplicitIteratorFunc) cb_bin_cmp,
					expr);
		}

		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (b != NULL) {
			if (VALUE_IS_ERROR (b)) {
				value_release (a);
				return b;
			}
			if (VALUE_IS_CELLRANGE (b) || VALUE_IS_ARRAY (b))
				return bin_array_iter_b (pos, a, b,
					(BinOpImplicitIteratorFunc) cb_bin_cmp,
					expr);
		}

		res = bin_cmp (GNM_EXPR_GET_OPER (expr), value_compare (a, b, FALSE), pos);
		value_release (a);
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
		flags &= ~GNM_EXPR_EVAL_WANT_REF;

		/* 1) Error from A */
		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (VALUE_IS_ERROR (a))
			return value_error_set_pos (&a->v_err, pos);

		/* 2) #!VALUE error if A is not a number */
		if (VALUE_IS_STRING (a)) {
			GnmValue *tmp = format_match_number (value_peek_string (a), NULL,
				sheet_date_conv (pos->sheet));

			value_release (a);
			if (tmp == NULL)
				return value_new_error_VALUE (pos);
			a = tmp;
		} else if (VALUE_IS_CELLRANGE (a) || VALUE_IS_ARRAY (a)) {
			b = gnm_expr_eval (expr->binary.value_b, pos, flags);
			if (VALUE_IS_STRING (b)) {
				res = format_match_number (value_peek_string (b), NULL,
					sheet_date_conv (pos->sheet));
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
			GnmValue *tmp = format_match_number (value_peek_string (b), NULL,
				sheet_date_conv (pos->sheet));

			value_release (b);
			if (tmp == NULL) {
				value_release (a);
				return value_new_error_VALUE (pos);
			}
			b = tmp;
		} else if (VALUE_IS_CELLRANGE (b) || VALUE_IS_ARRAY (b))
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

	case GNM_EXPR_OP_PAREN:
		/* Avoid recursive call to save stack.  */
		expr = expr->unary.value;
		goto retry;

	case GNM_EXPR_OP_PERCENTAGE:
	case GNM_EXPR_OP_UNARY_NEG:
	case GNM_EXPR_OP_UNARY_PLUS:
		/* Guarantees value != NULL */
		flags &= ~GNM_EXPR_EVAL_PERMIT_EMPTY;
		flags &= ~GNM_EXPR_EVAL_WANT_REF;

		a = gnm_expr_eval (expr->unary.value, pos, flags);
		if (VALUE_IS_ERROR (a))
			return a;
		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_UNARY_PLUS)
			return a;

		/* 2) #!VALUE error if A is not a number */
		if (VALUE_IS_STRING (a)) {
			GnmValue *tmp = format_match_number (value_peek_string (a), NULL,
				sheet_date_conv (pos->sheet));

			value_release (a);
			if (tmp == NULL)
				return value_new_error_VALUE (pos);
			a = tmp;
		} else if (VALUE_IS_CELLRANGE (a) || VALUE_IS_ARRAY (a)) {
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
			value_set_fmt (res, go_format_default_percentage ());
		}
		value_release (a);
		return res;

	case GNM_EXPR_OP_CAT:
		flags |= GNM_EXPR_EVAL_PERMIT_EMPTY;
		flags &= ~GNM_EXPR_EVAL_WANT_REF;
		a = gnm_expr_eval (expr->binary.value_a, pos, flags);
		if (a != NULL) {
			if (VALUE_IS_ERROR (a))
				return a;
			if (VALUE_IS_CELLRANGE (a) || VALUE_IS_ARRAY (a))
				return bin_array_iter_a (pos, a,
					gnm_expr_eval (expr->binary.value_b, pos, flags),
					(BinOpImplicitIteratorFunc) cb_bin_cat,
					expr);
		}
		b = gnm_expr_eval (expr->binary.value_b, pos, flags);
		if (b != NULL) {
			if (VALUE_IS_ERROR (b)) {
				value_release (a);
				return b;
			}
			if (VALUE_IS_CELLRANGE (b) || VALUE_IS_ARRAY (b))
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
		ei.flags = flags;
		res = function_call_with_exprs (&ei);
		if (res == NULL)
			return (flags & GNM_EXPR_EVAL_PERMIT_EMPTY)
			    ? NULL : value_new_int (0);
		if (VALUE_IS_CELLRANGE (res)) {
			/*
			 * pos->dep really shouldn't be NULL here, but it
			 * will be if someone puts "indirect" into an
			 * expression used for conditional formats.
			 */
			if (pos->dep)
				dependent_add_dynamic_dep (pos->dep,
							   &res->v_range.cell);
			if (!(flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)) {
				res = value_intersection (res, pos);
				return (res != NULL)
					? handle_empty (res, flags)
					: value_new_error_VALUE (pos);
			}
			return res;
		}
		if (VALUE_IS_ARRAY (res) &&
		    !(flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)) {
			a = (res->v_array.x > 0 && res->v_array.y > 0)
				? value_dup (res->v_array.vals[0][0])
				: value_new_error_REF (pos);
			value_release (res);
			return a;
		}
		return res;
	}

	case GNM_EXPR_OP_NAME:
		if (expr_name_is_active (expr->name.name))
			return handle_empty (expr_name_eval (expr->name.name, pos, flags), flags);
		return value_new_error_REF (pos);

	case GNM_EXPR_OP_CELLREF: {
		GnmCell *cell;
		GnmCellRef r;

		gnm_cellref_make_abs (&r, &expr->cellref.ref, pos);

		cell = sheet_cell_get (eval_sheet (r.sheet, pos->sheet),
				       r.col, r.row);
		if (cell)
			gnm_cell_eval (cell);

		if (flags & GNM_EXPR_EVAL_WANT_REF) {
			return value_new_cellrange_unsafe (&r, &r);
		} else {
			GnmValue *v = cell ? value_dup (cell->value) : NULL;
			return handle_empty (v, flags);
		}
	}

	case GNM_EXPR_OP_CONSTANT:
		res = value_dup (expr->constant.value);
		if (VALUE_IS_CELLRANGE (res) || VALUE_IS_ARRAY (res)) {
			if (flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR)
				return res;
			res = value_intersection (res, pos);
			return (res != NULL)
				? handle_empty (res, flags)
				: value_new_error_VALUE (pos);
		}
		return handle_empty (res, flags);

	case GNM_EXPR_OP_ARRAY_CORNER:
	case GNM_EXPR_OP_ARRAY_ELEM:
		g_warning ("Unexpected array expressions encountered");
		return value_new_error_VALUE (pos);

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

	g_assert_not_reached ();
	return value_new_error (pos, _("Unknown evaluation error"));
}

/**
 * gnm_expr_simplify_if:
 * @expr: Expression
 *
 * Simplifies @expr if it is a call to "if" with a constant condition.
 *
 * Returns: (transfer full) (nullable): simpler expression.
 */
GnmExpr const *
gnm_expr_simplify_if (GnmExpr const *expr)
{
	static GnmFunc *f_if = NULL;
	GnmExpr const *cond;
	gboolean c;

	g_return_val_if_fail (expr != NULL, NULL);

	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_FUNCALL)
		return NULL;

	if (!f_if)
		f_if = gnm_func_lookup ("if", NULL);

	if (expr->func.func != f_if || expr->func.argc != 3)
		return NULL;

	cond = expr->func.argv[0];
	if (GNM_EXPR_GET_OPER (cond) == GNM_EXPR_OP_CONSTANT) {
		GnmValue const *condval = cond->constant.value;
		gboolean err;
		c = value_get_as_bool (condval, &err);
		if (err)
			return NULL;
	} else
		return NULL;

	// We used to test for true() and false() as conditions too, but the code
	// never worked and has been unreachable until now.

	return gnm_expr_copy (expr->func.argv[c ? 1 : 2]);
}



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
		{ "",   0, 0, 0, 0 }, /* Parentheses for clarity */
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
		{ "^",  5, 0, 0, 0 }, /* Note: neither left nor right */
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
		{ "",   8, 0, 0, 0 }, /* Set       */
		{ ":", 10, 1, 0, 0 }, /* Range Ctor   */
		{ " ",  9, 1, 0, 0 }  /* Intersection */
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
		 * Avoid getting "-2^2".  We want to make sure files do not
		 * contain that construct as we might later change precedence.
		 *
		 * Always produce either "-(2^2)" or "(-2)^2".
		 *
		 * Note, that the parser introduces an explicit parenthesis in
		 * this case also, so parsed expressions should not be
		 * affected by the code here.
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

	case GNM_EXPR_OP_FUNCALL:
		out->convs->output.func (out, &expr->func);
		return;

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

		if (VALUE_IS_CELLRANGE (v)) {
			out->convs->output.range_ref (out, &v->v_range.cell);
			return;
		}

		if (VALUE_IS_BOOLEAN (v) &&
		    out->convs->output.boolean != NULL) {
			out->convs->output.boolean (out, v->v_bool.val);
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

/**
 * gnm_expr_as_gstring:
 * @expr: #GnmExpr
 * @out: output conventions
 *
 * Renders the expression as a string according to @out and places the
 * result in @out's accumulator.
 */
void
gnm_expr_as_gstring (GnmExpr const *expr, GnmConventionsOut *out)
{
	g_return_if_fail (expr != NULL);
	g_return_if_fail (out  != NULL);

	do_expr_as_string (expr, 0, out);
}

/**
 * gnm_expr_as_string:
 * @expr: #GnmExpr
 * @pp: (nullable): Parse position.  %NULL should be used for debugging only.
 * @convs: (nullable): #GnmConventions.  %NULL should be used for debugging
 * or when @pp identifies a #Sheet.
 *
 * Renders the expression as a string according to @convs.
 *
 * Returns: (transfer full): @expr as a string.
 */
char *
gnm_expr_as_string (GnmExpr const *expr, GnmParsePos const *pp,
		    GnmConventions const *convs)
{
	GnmConventionsOut out;
	GnmParsePos pp0;

	g_return_val_if_fail (expr != NULL, NULL);

	/*
	 * Defaults for debugging only!
	 */
	if (!pp) {
		/* UGH: Just get the first sheet in the first workbook! */
		Workbook *wb = gnm_app_workbook_get_by_index (0);
		Sheet *sheet = workbook_sheet_by_index (wb, 0);
		parse_pos_init (&pp0, NULL, sheet, 0, 0);
		pp = &pp0;
	}
	if (!convs)
		convs = pp->sheet
			? sheet_get_conventions (pp->sheet)
			: gnm_conventions_default;

	out.accum = g_string_new (NULL);
	out.pp    = pp;
	out.convs = convs;
	do_expr_as_string (expr, 0, &out);
	return g_string_free (out.accum, FALSE);
}

/****************************************************************************/

static gboolean
gnm_expr_is_err (GnmExpr const *expr, GnmStdError err)
{
	GnmStdError err2;

	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_CONSTANT)
		return FALSE;

	err2 = value_error_classify (expr->constant.value);
	return err == err2;
}

/**
 * gnm_expr_get_constant:
 * @expr: #GnmExpr
 *
 * Returns: (transfer none) (nullable): If this expression consists of just
 * a constant, return it.  Otherwise, %NULL.
 */
GnmValue const *
gnm_expr_get_constant (GnmExpr const *expr)
{
	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_CONSTANT)
		return NULL;

	return expr->constant.value;
}

/**
 * gnm_expr_get_name:
 * @expr: #GnmExpr
 *
 * Returns: (transfer none) (nullable): If this expression consists of just
 * a name, return it.  Otherwise, %NULL.
 */
GnmNamedExpr const *
gnm_expr_get_name (GnmExpr const *expr)
{
	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_NAME)
		return NULL;

	return expr->name.name;
}


/**
 * gnm_expr_get_cellref:
 * @expr: #GnmExpr
 *
 * Returns: (transfer none) (nullable): If this expression consists of just
 * a cell reference, return it.  Otherwise, %NULL.
 */
GnmCellRef const *
gnm_expr_get_cellref (GnmExpr const *expr)
{
	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_CELLREF)
		return NULL;

	return &expr->cellref.ref;
}


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

/* Return %TRUE if @pos is out of bounds */
static gboolean
reloc_restore_cellref (RelocInfoInternal const *rinfo,
		       GnmSheetSize const *ss, GnmCellPos const *pos,
		       GnmCellRef *res)
{
	if (res->sheet == rinfo->details->origin_sheet) {
		res->sheet = rinfo->details->target_sheet;
		if (res->sheet)
			ss = gnm_sheet_get_size (res->sheet);
	}

	if (!res->col_relative || rinfo->check_rels) {
		if (pos->col < 0 || ss->max_cols <= pos->col)
			return TRUE;
		res->col = pos->col;
		if (res->col_relative) {
			res->col -= rinfo->details->pos.eval.col;
			if (rinfo->from_inside)
				res->col -= rinfo->details->col_offset;
		}
	}

	if (!res->row_relative || rinfo->check_rels) {
		if (pos->row < 0 || ss->max_rows <= pos->row)
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
reloc_cellrange (RelocInfoInternal const *rinfo, GnmValueRange const *v,
		 gboolean sticky_end)
{
	GnmRange r;
	Sheet   *start_sheet, *end_sheet;
	GnmSheetSize const *start_ss, *end_ss;
	gboolean full_col, full_row;
	gboolean full_col_begin, full_row_begin;

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
	start_ss = gnm_sheet_get_size2 (start_sheet, rinfo->details->pos.wb);
	end_ss = gnm_sheet_get_size2 (end_sheet, rinfo->details->pos.wb);

	full_col = sticky_end && r.end.row >= start_ss->max_rows - 1;
	full_col_begin = full_col && r.start.row == 0;

	full_row = sticky_end && r.end.col >= start_ss->max_cols - 1;
	full_row_begin = full_row && r.start.col == 0;

	if (reloc_range (rinfo->details, start_sheet, end_sheet, &r) ||
	    rinfo->from_inside) {
		GnmRangeRef res = v->cell;

		if (full_col)
			r.end.row = start_ss->max_rows - 1;
		if (full_col_begin)
			r.start.row = 0;
		if (full_row)
			r.end.col = start_ss->max_cols - 1;
		if (full_row_begin)
			r.start.col = 0;

		if (reloc_restore_cellref (rinfo, start_ss, &r.start, &res.a) ||
		    reloc_restore_cellref (rinfo, end_ss,   &r.end,   &res.b))
			return gnm_expr_new_constant (value_new_error_REF (NULL));
		if (gnm_rangeref_equal (&res, &v->cell))
			return NULL;
		return gnm_expr_new_constant (value_new_cellrange_unsafe (&res.a, &res.b));
	}

	return NULL;
}

static GnmExpr const *
gnm_expr_relocate (GnmExpr const *expr, RelocInfoInternal const *rinfo);

static GnmExpr const *
cb_relocate (GnmExpr const *expr, GnmExprWalk *data)
{
	RelocInfoInternal const *rinfo = data->user;

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_NAME: {
		GnmNamedExpr *nexpr = expr->name.name;

		/* we cannot invalidate references to the name that are
		 * sitting in the undo queue, or the clipboard.  So we just
		 * flag the name as inactive and remove the reference here.
		 */
		if (!expr_name_is_active (nexpr))
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
				new_nexpr = expr_name_lookup (&pos, expr_name_name (nexpr));
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
				 *
				 * Just invalidate the use of the name if the
				 * name's expression, if relocated, would
				 * become invalid.
				 */
				GnmExpr const *tmp =
					gnm_expr_relocate (nexpr->texpr->expr,
							   rinfo);
				if (tmp && gnm_expr_is_err (tmp, GNM_ERROR_REF))
					return tmp;

				if (tmp)
					gnm_expr_free (tmp);

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

		case GNM_EXPR_RELOCATE_MOVE_RANGE:
		case GNM_EXPR_RELOCATE_COLS:
		case GNM_EXPR_RELOCATE_ROWS: {
			GnmRange r;
			Sheet *sheet;
			GnmSheetSize const *ss;

			reloc_normalize_cellref (rinfo, ref, &sheet, &r.start);
			r.end = r.start;
			ss = gnm_sheet_get_size2 (sheet, rinfo->details->pos.wb);

			if (reloc_range (rinfo->details, sheet, sheet, &r) ||
			    rinfo->from_inside) {
				GnmCellRef res = *ref;
				if (reloc_restore_cellref (rinfo, ss, &r.start, &res))
					return gnm_expr_new_constant (value_new_error_REF (NULL));
				if (gnm_cellref_equal (&res, ref))
					return NULL;
				return gnm_expr_new_cellref (&res);
			}
			return NULL;
		}

		default:
			g_assert_not_reached ();
		}

		return NULL;
	}

	case GNM_EXPR_OP_CONSTANT:
		if (VALUE_IS_CELLRANGE (expr->constant.value)) {
			GnmValueRange const *vr = &expr->constant.value->v_range;
			switch (rinfo->details->reloc_type) {
			case GNM_EXPR_RELOCATE_INVALIDATE_SHEET:
				return invalidate_sheet_cellrange (rinfo, vr);
			case GNM_EXPR_RELOCATE_MOVE_RANGE:
				return reloc_cellrange (rinfo, vr, TRUE);
			case GNM_EXPR_RELOCATE_COLS:
			case GNM_EXPR_RELOCATE_ROWS:
				return reloc_cellrange (rinfo, vr, rinfo->details->sticky_end);
			default:
				g_assert_not_reached ();
			}
		}
		return NULL;

	default:
		return NULL;
	}
}

static GnmExpr const *
gnm_expr_relocate (GnmExpr const *expr, RelocInfoInternal const *rinfo)
{
	g_return_val_if_fail (expr != NULL, NULL);
	return gnm_expr_walk (expr, cb_relocate, (gpointer)rinfo);
}

/**
 * gnm_expr_get_func_def:
 * @expr: Function call expressions
 *
 * Returns: (transfer none): the called function.
 */
GnmFunc *
gnm_expr_get_func_def (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL, NULL);

	return expr->func.func;
}

/**
 * gnm_expr_get_func_arg:
 * @expr: Function call expressions
 * @i: argument index
 *
 * Returns: (transfer none): the @i'th argument of the function call @expr.
 */
GnmExpr const *
gnm_expr_get_func_arg (GnmExpr const *expr, int i)
{
	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL, NULL);
	g_return_val_if_fail (i >= 0 && i < expr->func.argc, NULL);

	return expr->func.argv[i];
}


static void
cellref_boundingbox (GnmCellRef const *cr, Sheet const *sheet, GnmRange *bound)
{
	GnmSheetSize const *ss;

	if (cr->sheet)
		sheet = cr->sheet;
	ss = gnm_sheet_get_size (sheet);

	if (cr->col_relative) {
		if (cr->col >= 0) {
			int const c = ss->max_cols - cr->col - 1;
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
			int const r = ss->max_rows - cr->row - 1;
			if (bound->end.row > r)
				bound->end.row = r;
		} else {
			int const r = -cr->row;
			if (bound->start.row < r)
				bound->start.row = r;
		}
	}
}

static GnmExpr const *
cb_contains_subtotal (GnmExpr const *expr, GnmExprWalk *data)
{
	gboolean *res = data->user;
	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL &&
	    strcmp (expr->func.func->name, "subtotal") == 0) {
		*res = TRUE;
		data->stop = TRUE;
	}
	return NULL;
}

/**
 * gnm_expr_containts_subtotal:
 * @expr: #GnmExpr
 *
 * Returns: %TRUE if the expression calls the SUBTOTAL function
 **/
gboolean
gnm_expr_contains_subtotal (GnmExpr const *expr)
{
	gboolean res = FALSE;
	gnm_expr_walk (expr, cb_contains_subtotal, &res);
	return res;
}

/**
 * gnm_expr_get_range:
 * @expr: #GnmExpr
 *
 * Returns: (transfer full) (nullable): If this expression contains a
 * single range, return it.  Otherwise, %NULL.  A cell reference is
 * returned as a singleton range.
 */
GnmValue *
gnm_expr_get_range (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, NULL);

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_CELLREF:
		return value_new_cellrange_unsafe (
			&expr->cellref.ref, &expr->cellref.ref);

	case GNM_EXPR_OP_CONSTANT:
		if (VALUE_IS_CELLRANGE (expr->constant.value))
			return value_dup (expr->constant.value);
		return NULL;

	case GNM_EXPR_OP_NAME:
		if (!expr_name_is_active (expr->name.name))
			return NULL;
		return gnm_expr_top_get_range (expr->name.name->texpr);

	case GNM_EXPR_OP_PAREN:
		return gnm_expr_get_range (expr->unary.value);

	default:
		return NULL;
	}
}

static gint
gnm_insert_unique_value_cmp (gconstpointer a, gconstpointer b)
{
	return (value_equal (a,b) ? 0 : 1);
}



static GSList *
gnm_insert_unique_value (GSList *list, GnmValue *data)
{
	if (g_slist_find_custom (list, data,
				 gnm_insert_unique_value_cmp)
	    == NULL)
		return g_slist_prepend (list, data);
	value_release (data);
	return list;
}

/**
 * gnm_expr_is_rangeref:
 * @expr: #GnmExpr
 *
 * Returns: %TRUE if the expression can generate a reference.
 * NOTE: in the future it would be nice to know if a function
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
		if (VALUE_IS_CELLRANGE (expr->constant.value))
			return TRUE;
		return FALSE;

	case GNM_EXPR_OP_NAME:
		if (expr_name_is_active (expr->name.name))
			return gnm_expr_is_rangeref (expr->name.name->texpr->expr);
		return FALSE;

	case GNM_EXPR_OP_ARRAY_CORNER: /* I don't think this is possible */
	case GNM_EXPR_OP_ARRAY_ELEM:
	default:
		return FALSE;
	}
}

gboolean
gnm_expr_is_data_table (GnmExpr const *expr, GnmCellPos *c_in, GnmCellPos *r_in)
{
	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL) {
		char const *name = gnm_func_get_name (expr->func.func, FALSE);
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


static GnmExpr const *
do_expr_walk (GnmExpr const *expr, GnmExprWalkerFunc walker, GnmExprWalk *data)
{
	GnmExpr const *res;

	res = walker (expr, data);
	if (data->stop) {
		if (res) gnm_expr_free (res);
		return NULL;
	}
	if (res)
		return res;

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY: {
		GnmExpr const *a, *b;

		a = do_expr_walk (expr->binary.value_a, walker, data);
		if (data->stop)
			return NULL;

		b = do_expr_walk (expr->binary.value_b, walker, data);
		if (data->stop) {
			if (a) gnm_expr_free (a);
			return NULL;
		}

		if (!a && !b)
			return NULL;

		if (!a)
			a = gnm_expr_copy (expr->binary.value_a);
		else if (!b)
			b = gnm_expr_copy (expr->binary.value_b);

		return gnm_expr_new_binary (a, GNM_EXPR_GET_OPER (expr), b);
	}

	case GNM_EXPR_OP_ANY_UNARY: {
		GnmExpr const *a = do_expr_walk (expr->unary.value, walker, data);
		return a
			? gnm_expr_new_unary (GNM_EXPR_GET_OPER (expr), a)
			: NULL;
	}

	case GNM_EXPR_OP_FUNCALL: {
		gboolean any = FALSE;
		int i;
		int argc = expr->func.argc;
		GnmExprConstPtr *argv =
			argc ? g_new (GnmExprConstPtr, argc) : NULL;

		for (i = 0; i < argc; i++) {
			argv[i] = do_expr_walk (expr->func.argv[i], walker, data);
			if (data->stop) {
				while (--i >= 0)
					if (argv[i])
						gnm_expr_free (argv[i]);
				any = FALSE;
				break;
			}
			if (argv[i])
				any = TRUE;
		}

		if (any) {
			int i;
			for (i = 0; i < argc; i++)
				if (!argv[i])
					argv[i] = gnm_expr_copy (expr->func.argv[i]);
			return gnm_expr_new_funcallv (expr->func.func,
						      argc, argv);
		} else {
			g_free (argv);
			return NULL;
		}
	}
	case GNM_EXPR_OP_SET: {
		gboolean any = FALSE;
		int i;
		int argc = expr->set.argc;
		GnmExprConstPtr *argv =
			argc ? g_new (GnmExprConstPtr, argc) : NULL;

		for (i = 0; i < argc; i++) {
			argv[i] = do_expr_walk (expr->set.argv[i], walker, data);
			if (data->stop) {
				while (--i >= 0)
					if (argv[i])
						gnm_expr_free (argv[i]);
				any = FALSE;
				break;
			}
			if (argv[i])
				any = TRUE;
		}

		if (any) {
			int i;
			for (i = 0; i < argc; i++)
				if (!argv[i])
					argv[i] = gnm_expr_copy (expr->set.argv[i]);
			return gnm_expr_new_setv (argc, argv);
		} else {
			g_free (argv);
			return NULL;
		}
	}

	case GNM_EXPR_OP_ARRAY_CORNER: {
		GnmExpr const *e = do_expr_walk (expr->array_corner.expr, walker, data);
		return e
			? gnm_expr_new_array_corner (
				expr->array_corner.cols,
				expr->array_corner.rows, e)
			: NULL;
	}

	default:
		return NULL;
	}
}

/**
 * gnm_expr_walk:
 * @expr: expression to walk
 * @walker: (scope call): callback for each sub-expression
 * @user: user data pointer
 *
 * Returns: (transfer full) (nullable): transformed expression.
 *
 * This function walks the expression and calls the walker function for
 * each subexpression.  If the walker returns a non-%NULL expression,
 * a new expression is built.
 *
 * The walker will be called for an expression before its subexpressions.
 * It will receive the expression as its first argument and a GnmExprWalk
 * pointer as its second.  It may set the stop flag to terminate the walk
 * in which case gnm_expr_walk will return %NULL.
 **/
GnmExpr const *
gnm_expr_walk (GnmExpr const *expr, GnmExprWalkerFunc walker, gpointer user)
{
	GnmExprWalk data;

	g_return_val_if_fail (expr != NULL, NULL);

	data.user = user;
	data.stop = FALSE;
	data.flags = 0;
	return do_expr_walk (expr, walker, &data);
}

/**
 * gnm_expr_is_empty:
 * @expr: #GnmExpr
 *
 * Returns: %TRUE if @expr is a constant expression with the empty value.
 */
gboolean
gnm_expr_is_empty (GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, FALSE);

	return (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_CONSTANT &&
		VALUE_IS_EMPTY (expr->constant.value));
}

/**
 * gnm_expr_list_unref:
 * @list: (transfer full): expression list
 *
 * This frees list and all the expressions in it.
 */
void
gnm_expr_list_unref (GnmExprList *list)
{
	GnmExprList *l;
	for (l = list; l; l = l->next)
		gnm_expr_free (l->data);
	gnm_expr_list_free (list);
}

/**
 * gnm_expr_list_copy:
 * @list: (transfer none): list of expressions
 *
 * Returns: (transfer full): a copy of the list and all the
 * expressions in it.
 */
GnmExprList *
gnm_expr_list_copy (GnmExprList *list)
{
	GnmExprList *res = g_slist_copy (list); /* shallow */
	GnmExprList *l;

	for (l = res; l; l = l->next)
		l->data = (GnmExpr *) gnm_expr_copy (l->data);

	return res;
}


void
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
	es->ref_count = 1;
	return es;
}

void
gnm_expr_sharer_unref (GnmExprSharer *es)
{
	if (!es || es->ref_count-- > 1)
		return;
	g_hash_table_destroy (es->exprs);
	g_free (es);
}

static GnmExprSharer *
gnm_expr_sharer_ref (GnmExprSharer *es)
{
	es->ref_count++;
	return es;
}

GType
gnm_expr_sharer_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmExprSharer",
			 (GBoxedCopyFunc)gnm_expr_sharer_ref,
			 (GBoxedFreeFunc)gnm_expr_sharer_unref);
	}
	return t;
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

void
gnm_expr_sharer_report (GnmExprSharer *es)
{
	g_printerr ("Expressions in: %d\n", es->nodes_in);
	g_printerr ("Expressions stored: %d\n", es->nodes_stored);
	g_printerr ("Expressions killed: %d\n", es->nodes_killed);
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

GnmExprTop const *
gnm_expr_top_ref (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	((GnmExprTop *)texpr)->refcount++;
	return texpr;
}

void
gnm_expr_top_unref (GnmExprTop const *texpr)
{
	g_return_if_fail (GNM_IS_EXPR_TOP (texpr));

	((GnmExprTop *)texpr)->refcount--;
	if (texpr->refcount == 0) {
		gnm_expr_free (texpr->expr);
		((GnmExprTop *)texpr)->magic = 0;
		g_free ((GnmExprTop *)texpr);
	}
}

GType
gnm_expr_top_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmExprTop",
			 (GBoxedCopyFunc)gnm_expr_top_ref,
			 (GBoxedFreeFunc)gnm_expr_top_unref);
	}
	return t;
}

gboolean
gnm_expr_top_is_shared (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);

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

static GnmExpr const *
cb_get_ranges (GnmExpr const *expr, GnmExprWalk *data)
{
	GSList **pranges = data->user;

	/* There's no real reason to exclude names here, except that
	   we used to do so.  */
	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_NAME) {
		GnmValue *v = gnm_expr_get_range (expr);
		if (v)
			*pranges = gnm_insert_unique_value (*pranges, v);
	}

	return NULL;
}

/**
 * gnm_expr_top_get_ranges:
 * @texpr:
 *
 * A collect the set of GnmRanges in @expr.
 * Returns: (element-type GnmRange) (transfer full): a list of the unique
 * references Caller is responsible for releasing the list and the content.
 **/
GSList *
gnm_expr_top_get_ranges (GnmExprTop const *texpr)
{
	GSList *res = NULL;

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	gnm_expr_walk (texpr->expr, cb_get_ranges, &res);
	return res;
}

GnmValue *
gnm_expr_top_get_range (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);

	return gnm_expr_get_range (texpr->expr);
}

char *
gnm_expr_top_as_string (GnmExprTop const *texpr,
			GnmParsePos const *pp,
			GnmConventions const *convs)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);

	return gnm_expr_as_string (texpr->expr, pp, convs);
}

// This differs from gnm_expr_as_string in that a top-level set
// representing multiple expressions is rendered as a comma-separated
// list of expressions with no outside parenthesis.
char *
gnm_expr_top_multiple_as_string  (GnmExprTop const *texpr,
				  GnmParsePos const *pp,
				  GnmConventions const *convs)
{
	char *res;

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);

	res = gnm_expr_top_as_string (texpr, pp, convs);

	if (GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_SET) {
		// Get rid of '(' and ')'.  This is crude and probably should
		// have made it into convs, but it'll do.
		size_t l = strlen (res);
		if (l >= 2 && res[0] == '(' && res[l - 1] == ')') {
			memmove (res, res + 1, l - 2);
			res[l - 2] = 0;
		}
	}

	return res;
}

void
gnm_expr_top_as_gstring (GnmExprTop const *texpr,
			 GnmConventionsOut *out)
{
	g_return_if_fail (GNM_IS_EXPR_TOP (texpr));
	g_return_if_fail (out != NULL);

	do_expr_as_string (texpr->expr, 0, out);
}

guint
gnm_expr_top_hash (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), 0);

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
	if (te1 == NULL || te2 == NULL)
		return FALSE;

	g_return_val_if_fail (GNM_IS_EXPR_TOP (te1), FALSE);
	g_return_val_if_fail (GNM_IS_EXPR_TOP (te2), FALSE);

	if (te1->hash && te2->hash && te1->hash != te2->hash)
		return FALSE;

	return gnm_expr_equal (te1->expr, te2->expr);
}

/*
 * gnm_expr_top_relocate:
 * @texpr: #GnmExprTop to fixup
 * @rinfo: #GnmExprRelocateInfo details of relocation
 * @ignore_rel: Do not adjust relative refs (for internal use when
 *		  relocating named expressions.   Most callers will want FALSE.
 *
 * GNM_EXPR_RELOCATE_INVALIDATE_SHEET:
 *	Convert any references to  sheets marked being_invalidated into #REF!
 * GNM_EXPR_RELOCATE_MOVE_RANGE,
 *	Find any references to the specified area and adjust them by the
 *	supplied deltas.  Check for out of bounds conditions.  Return %NULL if
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

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
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

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	g_return_val_if_fail (IS_SHEET (src), NULL);
	g_return_val_if_fail (IS_SHEET (dst), NULL);

	rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
	rinfo.origin_sheet = (Sheet *)src;
	rinfo.target_sheet = (Sheet *)dst;
	rinfo.col_offset = rinfo.row_offset = 0;
	range_init_full_sheet (&rinfo.origin, src);
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
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);

	return gnm_expr_contains_subtotal (texpr->expr);
}

static GnmExpr const *
cb_is_volatile (GnmExpr const *expr, GnmExprWalk *data)
{
	gboolean *res = data->user;
	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL &&
	    (gnm_func_get_flags (expr->func.func) & GNM_FUNC_VOLATILE)) {
		*res = TRUE;
		data->stop = TRUE;
	}
	return NULL;
}

gboolean
gnm_expr_top_is_volatile (GnmExprTop const *texpr)
{
	gboolean res = FALSE;

	/*
	 * An expression is volatile if it contains a call to a volatile
	 * function, even in cases like IF(TRUE,12,RAND()) where the
	 * volatile function won't even be reached.
	 */

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);
	gnm_expr_walk (texpr->expr, cb_is_volatile, &res);
	return res;
}


static GnmValue *
gnm_expr_top_eval_array_corner (GnmExprTop const *texpr,
				GnmEvalPos const *pos,
				GnmExprEvalFlags flags)
{
	GnmExpr const *expr = texpr->expr;
	GnmEvalPos pos2;
	GnmValue *a;

	pos2 = *pos;
	pos2.array_texpr = texpr;
	a = gnm_expr_eval (expr->array_corner.expr, &pos2,
				     flags | GNM_EXPR_EVAL_PERMIT_NON_SCALAR);

	value_release (expr->array_corner.value);

	/* Store real result (cast away const)*/
	((GnmExpr*)expr)->array_corner.value = a;

	if (a != NULL &&
	    (VALUE_IS_CELLRANGE (a) || VALUE_IS_ARRAY (a))) {
		if (value_area_get_width (a, pos) <= 0 ||
		    value_area_get_height (a, pos) <= 0)
			return value_new_error_NA (pos);
		a = (GnmValue *)value_area_get_x_y (a, 0, 0, pos);
	}
	return handle_empty ((a != NULL) ? value_dup (a) : NULL, flags);
}

static GnmValue *
gnm_expr_top_eval_array_elem (GnmExprTop const *texpr,
			      GnmEvalPos const *pos,
			      GnmExprEvalFlags flags)
{
	GnmExpr const *expr = texpr->expr;
	/* The upper left corner manages the recalc of the expr */
	GnmCell *corner = array_elem_get_corner (&expr->array_elem,
						 pos->sheet, &pos->eval);
	GnmValue *a;

	if (!corner ||
	    !gnm_expr_top_is_array_corner (corner->base.texpr)) {
		g_warning ("Funky array setup.");
		return handle_empty (NULL, flags);
	}

	gnm_cell_eval (corner);
	a = gnm_expr_top_get_array_value (corner->base.texpr);
	if (a == NULL)
		return handle_empty (NULL, flags);

	if ((VALUE_IS_CELLRANGE (a) || VALUE_IS_ARRAY (a))) {
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

GnmValue *
gnm_expr_top_eval (GnmExprTop const *texpr,
		   GnmEvalPos const *pos,
		   GnmExprEvalFlags flags)
{
	GnmValue *res;

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);

	gnm_app_recalc_start ();

	if (gnm_expr_top_is_array_corner (texpr))
		res = gnm_expr_top_eval_array_corner (texpr, pos, flags);
	else if (gnm_expr_top_is_array_elem (texpr, NULL, NULL))
		res = gnm_expr_top_eval_array_elem (texpr, pos, flags);
	else
		res = gnm_expr_eval (texpr->expr, pos, flags);
	gnm_app_recalc_finish ();

	return res;
}

GnmValue *
gnm_expr_top_eval_fake_array (GnmExprTop const *texpr,
			      GnmEvalPos const *pos,
			      GnmExprEvalFlags flags)
{
	if (eval_pos_is_array_context (pos))
		return gnm_expr_top_eval (texpr, pos, flags);
	else {
		GnmEvalPos pos2 = *pos;
		GnmExprTop const *fake = gnm_expr_top_new_array_corner (1, 1, NULL);
		GnmValue *res;
		((GnmExpr *)(fake->expr))->array_corner.expr = texpr->expr; // Patch in our expr
		pos2.array_texpr = fake;
		res = gnm_expr_eval (texpr->expr, &pos2, flags);
		((GnmExpr *)(fake->expr))->array_corner.expr = NULL;
		gnm_expr_top_unref (fake);
		return res;
	}
}


static GSList *
gnm_insert_unique (GSList *list, gpointer data)
{
	if (g_slist_find (list, data) == NULL)
		return g_slist_prepend (list, data);
	return list;
}

static GnmExpr const *
cb_referenced_sheets (GnmExpr const *expr, GnmExprWalk *data)
{
	GSList **psheets = data->user;

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_CELLREF:
		*psheets = gnm_insert_unique (*psheets, expr->cellref.ref.sheet);
		break;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;
		if (!VALUE_IS_CELLRANGE (v))
			break;
		*psheets = gnm_insert_unique (*psheets, v->v_range.cell.a.sheet);
		/* A NULL b sheet means a's sheet.  Do not insert that.  */
		if (v->v_range.cell.b.sheet)
			*psheets = gnm_insert_unique (*psheets, v->v_range.cell.b.sheet);
		break;
	}

	default:
		break;
	}

	return NULL;
}

/**
 * gnm_expr_top_referenced_sheets:
 * @texpr:
 *
 * Generates a list of the sheets referenced by the supplied expression.
 * Caller must free the list.  Note, that %NULL may occur in the result
 * if the expression has a range or cellref without a sheet.
 * Returns: (element-type Sheet) (transfer container): the created list.
 */
GSList *
gnm_expr_top_referenced_sheets (GnmExprTop const *texpr)
{
	GSList *res = NULL;

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	gnm_expr_walk (texpr->expr, cb_referenced_sheets, &res);
	return res;
}

gboolean
gnm_expr_top_is_err (GnmExprTop const *texpr, GnmStdError err)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);
	return gnm_expr_is_err (texpr->expr, err);
}

/**
 * gnm_expr_top_get_constant:
 * @texpr:
 *
 * If this expression consists of just a constant, return it.
 */
GnmValue const *
gnm_expr_top_get_constant (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);

	return gnm_expr_get_constant (texpr->expr);
}

GnmCellRef const *
gnm_expr_top_get_cellref (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	return gnm_expr_get_cellref (texpr->expr);
}

static GnmExpr const *
cb_first_funcall (GnmExpr const *expr, GnmExprWalk *data)
{
	GnmExprConstPtr *user = data->user;
	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL) {
		*user = expr;
		data->stop = TRUE;
	}
	return NULL;
}

/**
 * gnm_expr_top_first_funcall:
 * @texpr:
 *
 */
GnmExpr const *
gnm_expr_top_first_funcall (GnmExprTop const *texpr)
{
	GnmExpr const *res = NULL;

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	gnm_expr_walk (texpr->expr, cb_first_funcall, &res);
	return res;
}

struct cb_get_boundingbox {
	Sheet const *sheet;
	GnmRange *bound;
};

static GnmExpr const *
cb_get_boundingbox (GnmExpr const *expr, GnmExprWalk *data)
{
	struct cb_get_boundingbox *args = data->user;

	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_CELLREF:
		cellref_boundingbox (&expr->cellref.ref, args->sheet, args->bound);
		break;

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;

		if (VALUE_IS_CELLRANGE (v)) {
			cellref_boundingbox (&v->v_range.cell.a, args->sheet, args->bound);
			cellref_boundingbox (&v->v_range.cell.b, args->sheet, args->bound);
		}
		break;
	}

	default:
		break;
	}

	return NULL;
}

/**
 * gnm_expr_top_get_boundingbox:
 *
 * Returns the range of cells in which the expression can be used without going
 * out of bounds.
 **/
void
gnm_expr_top_get_boundingbox (GnmExprTop const *texpr, Sheet const *sheet,
			      GnmRange *bound)
{
	struct cb_get_boundingbox args;

	g_return_if_fail (GNM_IS_EXPR_TOP (texpr));

	range_init_full_sheet (bound, sheet);

	args.sheet = sheet;
	args.bound = bound;
	gnm_expr_walk (texpr->expr, cb_get_boundingbox, &args);
}

gboolean
gnm_expr_top_is_rangeref (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);

	return gnm_expr_is_rangeref (texpr->expr);
}

gboolean
gnm_expr_top_is_array_corner (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);
	return GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER;
}

void
gnm_expr_top_get_array_size (GnmExprTop const *texpr, int *cols, int *rows)
{
	g_return_if_fail (GNM_IS_EXPR_TOP (texpr));
	g_return_if_fail (GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER);

	if (cols)
		*cols = texpr->expr->array_corner.cols;
	if (rows)
		*rows = texpr->expr->array_corner.rows;
}

GnmValue *
gnm_expr_top_get_array_value (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	g_return_val_if_fail (GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER, NULL);
	return texpr->expr->array_corner.value;
}

GnmExpr const *
gnm_expr_top_get_array_expr (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	g_return_val_if_fail (GNM_EXPR_GET_OPER (texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER, NULL);
	return texpr->expr->array_corner.expr;
}

gboolean
gnm_expr_top_is_array_elem (GnmExprTop const *texpr, int *x, int *y)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);

	if (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_ARRAY_ELEM)
		return FALSE;

	if (x) *x = texpr->expr->array_elem.x;
	if (y) *y = texpr->expr->array_elem.y;
	return TRUE;
}

gboolean
gnm_expr_top_is_array (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), FALSE);
	return gnm_expr_is_array (texpr->expr);
}

GnmExprTop const *
gnm_expr_top_transpose (GnmExprTop const *texpr)
{
	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
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

/**
 * gnm_expr_init_: (skip)
 */
void
gnm_expr_init_ (void)
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
	s = gnm_expr_as_string (expr, &pp, NULL);
	g_printerr ("Leaking expression at %p: %s.\n", (void *)expr, s);
	g_free (s);
}
#endif

/**
 * gnm_expr_shutdown_: (skip)
 */
void
gnm_expr_shutdown_ (void)
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
