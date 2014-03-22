/*
 * style-conditions.c:
 *
 * Copyright (C) 2005-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2013-2014 Morten Welinder (terra@gnome.org)
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
#include "gnumeric.h"
#include "style-conditions.h"
#include "mstyle.h"
#include "gnm-style-impl.h"
#include "expr.h"
#include "expr-impl.h"
#include "cell.h"
#include "value.h"
#include "sheet.h"
#include <parse-util.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>
#include <func.h>
#include <gutils.h>

typedef GObjectClass GnmStyleConditionsClass;
struct _GnmStyleConditions {
	GObject base;
	GPtrArray *conditions;
	Sheet *sheet;
};

static GObjectClass *parent_class;

static gboolean
debug_style_conds (void)
{
	static int debug = -1;
	if (debug < 0)
		debug = gnm_debug_flag ("style-conds");
	return debug;
}

static unsigned
gnm_style_cond_op_operands (GnmStyleCondOp op)
{
	switch (op) {
	case GNM_STYLE_COND_BETWEEN:
	case GNM_STYLE_COND_NOT_BETWEEN:
		return 2;

	case GNM_STYLE_COND_EQUAL:
	case GNM_STYLE_COND_NOT_EQUAL:
	case GNM_STYLE_COND_GT:
	case GNM_STYLE_COND_LT:
	case GNM_STYLE_COND_GTE:
	case GNM_STYLE_COND_LTE:
	case GNM_STYLE_COND_CUSTOM:
	case GNM_STYLE_COND_CONTAINS_STR:
	case GNM_STYLE_COND_NOT_CONTAINS_STR:
	case GNM_STYLE_COND_BEGINS_WITH_STR:
	case GNM_STYLE_COND_NOT_BEGINS_WITH_STR:
	case GNM_STYLE_COND_ENDS_WITH_STR:
	case GNM_STYLE_COND_NOT_ENDS_WITH_STR:
		return 1;

	case GNM_STYLE_COND_CONTAINS_ERR:
	case GNM_STYLE_COND_NOT_CONTAINS_ERR:
	case GNM_STYLE_COND_CONTAINS_BLANKS:
	case GNM_STYLE_COND_NOT_CONTAINS_BLANKS:
		return 0;
	}
	g_assert_not_reached ();
}


/**
 * gnm_style_cond_is_valid :
 * @cond: #GnmStyleCond
 *
 * Returns: TRUE if @cond is in a reasonable state
 **/
gboolean
gnm_style_cond_is_valid (GnmStyleCond const *cond)
{
	unsigned ui, N;

	g_return_val_if_fail (cond != NULL, FALSE);

	if (cond->overlay == NULL)
		return FALSE;
	if ((unsigned)cond->op > (unsigned)GNM_STYLE_COND_NOT_CONTAINS_BLANKS ||
	    (cond->op > GNM_STYLE_COND_CUSTOM && cond->op < GNM_STYLE_COND_CONTAINS_STR))
		return FALSE;

	N = gnm_style_cond_op_operands (cond->op);
	for (ui = 0; ui < G_N_ELEMENTS (cond->deps); ui++) {
		gboolean need = (ui < N);
		gboolean have = (cond->deps[ui].texpr != NULL);
		if (have != need)
			return FALSE;
	}

	return TRUE;
}

GnmStyleCond *
gnm_style_cond_new (GnmStyleCondOp op, Sheet *sheet)
{
	GnmStyleCond *res;
	unsigned ui;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	res = g_new0 (GnmStyleCond, 1);
	res->op = op;
	for (ui = 0; ui < 2; ui++)
		dependent_managed_init (&res->deps[ui], sheet);
	return res;
}

/**
 * gnm_style_cond_dup:
 * @src: #GnmStyleCond
 *
 * Returns: (transfer full): the newly allocated #GnmStyleCond.
 **/
GnmStyleCond *
gnm_style_cond_dup (GnmStyleCond const *src)
{
	GnmStyleCond *dst;
	unsigned ui;

	g_return_val_if_fail (src != NULL, NULL);

	dst = gnm_style_cond_new (src->op, gnm_style_cond_get_sheet (src));
	gnm_style_cond_set_overlay (dst, src->overlay);
	for (ui = 0; ui < 2; ui++)
		gnm_style_cond_set_expr (dst, src->deps[ui].texpr, ui);

	return dst;
}

void
gnm_style_cond_free (GnmStyleCond *cond)
{
	unsigned ui;

	g_return_if_fail (cond != NULL);

	/* Be very delicate, this is called for invalid conditions too */
	if (cond->overlay)
		gnm_style_unref (cond->overlay);
	for (ui = 0; ui < 2; ui++)
		gnm_style_cond_set_expr (cond, NULL, ui);

	g_free (cond);
}

GType
gnm_style_cond_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmStyleCond",
			 (GBoxedCopyFunc)gnm_style_cond_dup,
			 (GBoxedFreeFunc)gnm_style_cond_free);
	}
	return t;
}

/**
 * gnm_style_cond_get_sheet:
 * @cond: #GnmStyleCond
 *
 * Returns: (transfer none): the #Sheet.
 **/
Sheet *
gnm_style_cond_get_sheet (GnmStyleCond const *cond)
{
	g_return_val_if_fail (cond != NULL, NULL);
	return cond->deps[0].sheet;
}

void
gnm_style_cond_set_sheet (GnmStyleCond *cond, Sheet *sheet)
{
	int ui;

	g_return_if_fail (cond != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (ui = 0; ui < 2; ui++)
		dependent_managed_set_sheet (&cond->deps[ui], sheet);
}

GnmExprTop const *
gnm_style_cond_get_expr (GnmStyleCond const *cond, unsigned idx)
{
	g_return_val_if_fail (cond != NULL, NULL);
	g_return_val_if_fail (idx < G_N_ELEMENTS (cond->deps), NULL);

	return cond->deps[idx].texpr;
}

void
gnm_style_cond_set_expr (GnmStyleCond *cond,
			 GnmExprTop const *texpr,
			 unsigned idx)
{
	g_return_if_fail (cond != NULL);
	g_return_if_fail (idx < G_N_ELEMENTS (cond->deps));

	dependent_managed_set_expr (&cond->deps[idx], texpr);
}

void
gnm_style_cond_set_overlay (GnmStyleCond *cond, GnmStyle *overlay)
{
	g_return_if_fail (cond != NULL);

	if (overlay)
		gnm_style_ref (overlay);
	if (cond->overlay)
		gnm_style_unref (cond->overlay);
	cond->overlay = overlay;
}

static GnmExpr const *
generate_end_match (const char *endfunc, gboolean force, gboolean negate,
		    GnmExprTop const *sexpr, GnmCellRef *cr)
{
	GnmValue const *v = gnm_expr_get_constant (sexpr->expr);
	GnmExpr const *len_expr;

	if (v && VALUE_IS_STRING (v)) {
		int len = g_utf8_strlen (value_peek_string (v), -1);
		len_expr = gnm_expr_new_constant (value_new_int (len));
	} else if (force) {
		/*
		 * This is imperfect because the expression gets
		 * evaluated twice.
		 */
		len_expr = gnm_expr_new_funcall1
			(gnm_func_lookup_or_add_placeholder ("LEN"),
			 gnm_expr_copy (sexpr->expr));
	} else
		return NULL;

	return gnm_expr_new_binary
		(gnm_expr_new_funcall2
		 (gnm_func_lookup_or_add_placeholder (endfunc),
		  gnm_expr_new_cellref (cr),
		  len_expr),
		 negate ? GNM_EXPR_OP_NOT_EQUAL : GNM_EXPR_OP_EQUAL,
		 gnm_expr_copy (sexpr->expr));
}


/**
 * gnm_style_cond_get_alternate_expr:
 * @cond: condition
 *
 * Returns: (transfer full) (allow-none): An custom expression that can be
 * used in place of @cond.
 **/
GnmExprTop const *
gnm_style_cond_get_alternate_expr (GnmStyleCond const *cond)
{
	GnmCellRef self;
	GnmExpr const *expr;
	gboolean negate = FALSE;
	GnmExprTop const *sexpr = NULL;

	g_return_val_if_fail (cond != NULL, NULL);

	gnm_cellref_init (&self, NULL, 0, 0, TRUE);

	if (gnm_style_cond_op_operands (cond->op) > 0) {
		sexpr = gnm_style_cond_get_expr (cond, 0);
		if (!sexpr)
			return NULL;
	}

	switch (cond->op) {
	case GNM_STYLE_COND_NOT_CONTAINS_ERR:
		negate = TRUE; /* ...and fall through */
	case GNM_STYLE_COND_CONTAINS_ERR:
		expr = gnm_expr_new_funcall1
			(gnm_func_lookup_or_add_placeholder ("ISERROR"),
			 gnm_expr_new_cellref (&self));
		break;

	case GNM_STYLE_COND_CONTAINS_STR:
		negate = TRUE; /* ...and fall through */
	case GNM_STYLE_COND_NOT_CONTAINS_STR:
		expr = gnm_expr_new_funcall1
			(gnm_func_lookup_or_add_placeholder ("ISERROR"),
			 gnm_expr_new_funcall2
			 (gnm_func_lookup_or_add_placeholder ("FIND"),
			  gnm_expr_copy (sexpr->expr),
			  gnm_expr_new_cellref (&self)));
		break;

	case GNM_STYLE_COND_NOT_CONTAINS_BLANKS:
		negate = TRUE; /* ...and fall through */
	case GNM_STYLE_COND_CONTAINS_BLANKS:
		/* This means blanks-only */

		expr = gnm_expr_new_binary
			(gnm_expr_new_funcall1
			 (gnm_func_lookup_or_add_placeholder ("LEN"),
			  gnm_expr_new_funcall1
			  (gnm_func_lookup_or_add_placeholder ("TRIM"),
			   gnm_expr_new_cellref (&self))),
			 negate ? GNM_EXPR_OP_GT : GNM_EXPR_OP_EQUAL,
			 gnm_expr_new_constant (value_new_int (0)));
		negate = FALSE;
		break;

	case GNM_STYLE_COND_NOT_BEGINS_WITH_STR:
		negate = TRUE; /* ...and fall through */
	case GNM_STYLE_COND_BEGINS_WITH_STR:
		/*
		 * We are constrained by using only Excel functions and not
		 * evaluating the needle more than once.  We cannot fulfill
		 * that and end up computing the needle twice.
		 */
		expr = generate_end_match ("LEFT", TRUE, negate, sexpr, &self);
		negate = FALSE;
		break;

	case GNM_STYLE_COND_NOT_ENDS_WITH_STR:
		negate = TRUE; /* ...and fall through */
	case GNM_STYLE_COND_ENDS_WITH_STR:
		/*
		 * We are constrained by using only Excel functions and not
		 * evaluating the needle more than once.  We cannot fulfill
		 * that and end up computing the needle twice.
		 */
		expr = generate_end_match ("RIGHT", TRUE, negate, sexpr, &self);
		negate = FALSE;
		break;

	default:
		return NULL;
	}

	if (negate)
		expr = gnm_expr_new_funcall1
			(gnm_func_lookup_or_add_placeholder ("NOT"), expr);

	return gnm_expr_top_new (expr);
}

static gboolean
isself (GnmExpr const *expr)
{
	GnmCellRef const *cr;

	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_CELLREF)
		return FALSE;
	cr = &expr->cellref.ref;

	return (cr->sheet == NULL &&
		cr->col == 0 && cr->row == 0 &&
		cr->col_relative && cr->row_relative);
}

static GnmExprTop const *
decode_end_match (const char *endfunc, GnmExpr const *expr, gboolean *negated)
{
	GnmExpr const *needle;
	GnmExpr const *expr2;

	*negated = (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_NOT_EQUAL);

	if ((GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_EQUAL ||
	     GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_NOT_EQUAL) &&
	    (needle = expr->binary.value_b) &&
	    (expr2 = expr->binary.value_a) &&
	    GNM_EXPR_GET_OPER (expr2) == GNM_EXPR_OP_FUNCALL &&
	    expr2->func.argc == 2 &&
	    expr2->func.func == gnm_func_lookup_or_add_placeholder (endfunc) &&
	    isself (expr2->func.argv[0])) {
		GnmExpr const *len_expr = expr2->func.argv[1];
		GnmValue const *v, *vl;

		if (GNM_EXPR_GET_OPER (len_expr) == GNM_EXPR_OP_FUNCALL &&
		    len_expr->func.argc == 1 &&
		    len_expr->func.func == gnm_func_lookup_or_add_placeholder ("LEN") &&
		    gnm_expr_equal (len_expr->func.argv[0], needle))
			return gnm_expr_top_new (gnm_expr_copy (needle));

		if ((v = gnm_expr_get_constant (needle)) &&
		    VALUE_IS_STRING (v) &&
		    (vl = gnm_expr_get_constant (len_expr)) &&
		    VALUE_IS_NUMBER (vl) &&
		    value_get_as_float (vl) == g_utf8_strlen (value_peek_string (v), -1))
			return gnm_expr_top_new (gnm_expr_copy (needle));
	}

	return NULL;
}

/**
 * gnm_style_cond_canonicalize:
 * @cond: condition
 *
 * Turns a custom condition into a more specific one, i.e., reverses the
 * effect of using gnm_style_cond_get_alternate_expr.  Leaves the condition
 * alone if it is not recognized.
 **/
void
gnm_style_cond_canonicalize (GnmStyleCond *cond)
{
	GnmExpr const *expr, *expr2;
	GnmExprTop const *texpr;
	GnmValue const *v;
	gboolean negate = FALSE;
	gboolean match_negated;
	GnmFunc const *iserror;
	GnmFunc const *iferror;
	GnmFunc const *find;
	GnmStyleCondOp newop = GNM_STYLE_COND_CUSTOM;

	g_return_if_fail (cond != NULL);

	if (cond->op != GNM_STYLE_COND_CUSTOM)
		return;

	texpr = gnm_style_cond_get_expr (cond, 0);
	if (!texpr)
		return;
	expr = texpr->expr;
	texpr = NULL;

	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL &&
	    expr->func.argc == 1 &&
	    expr->func.func == gnm_func_lookup_or_add_placeholder ("NOT")) {
		negate = TRUE;
		expr = expr->func.argv[0];
	}

	iserror = gnm_func_lookup_or_add_placeholder ("ISERROR");
	iferror = gnm_func_lookup_or_add_placeholder ("IFERROR");
	find = gnm_func_lookup_or_add_placeholder ("FIND");

	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL &&
	    expr->func.argc == 1 && expr->func.func == iserror &&
	    isself (expr->func.argv[0])) {
		newop = negate
			? GNM_STYLE_COND_NOT_CONTAINS_ERR
			: GNM_STYLE_COND_CONTAINS_ERR;
	} else if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL &&
		   expr->func.argc == 1 && expr->func.func == iserror &&
		   (expr2 = expr->func.argv[0]) &&
		   GNM_EXPR_GET_OPER (expr2) == GNM_EXPR_OP_FUNCALL &&
		   expr2->func.argc == 2 && expr2->func.func == find &&
		   isself (expr2->func.argv[1])) {
		texpr = gnm_expr_top_new (gnm_expr_copy (expr2->func.argv[0]));
		newop = negate
			? GNM_STYLE_COND_CONTAINS_STR
			: GNM_STYLE_COND_NOT_CONTAINS_STR;
	} else if ((GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_EQUAL ||
		    GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_GT) &&
		   (v = gnm_expr_get_constant (expr->binary.value_b)) &&
		   VALUE_IS_FLOAT (v) && value_get_as_float (v) == 0 &&
		   (expr2 = expr->binary.value_a) &&
		   GNM_EXPR_GET_OPER (expr2) == GNM_EXPR_OP_FUNCALL &&
		   expr2->func.argc == 1 &&
		   expr2->func.func == gnm_func_lookup_or_add_placeholder ("LEN") &&
		   (expr2 = expr2->func.argv[0]) &&
		   GNM_EXPR_GET_OPER (expr2) == GNM_EXPR_OP_FUNCALL &&
		   expr2->func.argc == 1 &&
		   expr2->func.func == gnm_func_lookup_or_add_placeholder ("TRIM") &&
		   isself (expr2->func.argv[0])) {
		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_GT)
			negate = !negate;

		newop = negate
			? GNM_STYLE_COND_NOT_CONTAINS_BLANKS
			: GNM_STYLE_COND_CONTAINS_BLANKS;
	} else if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_EQUAL &&
		   (v = gnm_expr_get_constant (expr->binary.value_b)) &&
		   VALUE_IS_FLOAT (v) && value_get_as_float (v) == 1 &&
		   (expr2 = expr->binary.value_a) &&
		   GNM_EXPR_GET_OPER (expr2) == GNM_EXPR_OP_FUNCALL &&
		   expr2->func.argc == 2 && expr2->func.func == iferror &&
		   (v = gnm_expr_get_constant (expr2->func.argv[1])) &&
		   VALUE_IS_FLOAT (v) && value_get_as_float (v) != 1 &&
		   (expr2 = expr2->func.argv[0]) &&
		   GNM_EXPR_GET_OPER (expr2) == GNM_EXPR_OP_FUNCALL &&
		   expr2->func.argc == 2 && expr2->func.func == find &&
		   isself (expr2->func.argv[1])) {
		texpr = gnm_expr_top_new (gnm_expr_copy (expr2->func.argv[0]));
		newop = negate
			? GNM_STYLE_COND_NOT_BEGINS_WITH_STR
			: GNM_STYLE_COND_BEGINS_WITH_STR;
	} else if ((texpr = decode_end_match ("LEFT", expr, &match_negated))) {
		newop = (negate ^ match_negated)
			? GNM_STYLE_COND_NOT_BEGINS_WITH_STR
			: GNM_STYLE_COND_BEGINS_WITH_STR;
	} else if ((texpr = decode_end_match ("RIGHT", expr, &match_negated))) {
		newop = (negate ^ match_negated)
			? GNM_STYLE_COND_NOT_ENDS_WITH_STR
			: GNM_STYLE_COND_ENDS_WITH_STR;
	}

	if (newop != GNM_STYLE_COND_CUSTOM) {
		gnm_style_cond_set_expr (cond, texpr, 0);
		if (texpr)
			gnm_expr_top_unref (texpr);
		cond->op = newop;
	}
}

static gboolean
case_insensitive_has_fix (GnmValue const *vs, GnmValue const *vp,
			  gboolean is_prefix)
{
	size_t plen = g_utf8_strlen (value_peek_string (vp), -1);
	const char *s = value_peek_string (vs);
	size_t slen = g_utf8_strlen (s, -1);
	GnmValue *vs2;
	gboolean res;

	if (plen > slen)
		return FALSE;

	vs2 = value_new_string_nocopy
		(is_prefix
		 ? g_strndup (s, g_utf8_offset_to_pointer (s, plen) - s)
		 : g_strdup (g_utf8_offset_to_pointer (s, slen - plen)));
	res = (value_compare (vs2, vp, FALSE) == IS_EQUAL);
	value_release (vs2);

	return res;
}


static gboolean
gnm_style_cond_eval (GnmStyleCond const *cond, GnmValue const *cv,
		     GnmEvalPos const *ep)
{
	gboolean negate = FALSE;
	gboolean res;
	GnmValue *val0 = NULL;
	GnmValue *val1 = NULL;

	switch (gnm_style_cond_op_operands (cond->op)) {
	case 2:
		val1 = gnm_expr_top_eval (cond->deps[1].texpr, ep,
					  GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		/* Fall through */
	case 1:
		val0 = gnm_expr_top_eval (cond->deps[0].texpr, ep,
					  GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		/* Fall through */
	case 0:
		break;
	default:
		g_assert_not_reached ();
	}

	switch (cond->op) {
	case GNM_STYLE_COND_NOT_EQUAL:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_EQUAL:
		res = value_compare (cv, val0, FALSE) == IS_EQUAL;
		break;

	case GNM_STYLE_COND_LTE:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_GT:
		res = value_compare (cv, val0, FALSE) == IS_GREATER;
		break;
		
	case GNM_STYLE_COND_GTE:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_LT:
		res = value_compare (cv, val0, FALSE) == IS_LESS;
		break;

	case GNM_STYLE_COND_NOT_BETWEEN:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_BETWEEN:
		res = !(value_compare (cv, val0, FALSE) == IS_LESS ||
			value_compare (cv, val1, FALSE) == IS_GREATER);
		break;

	case GNM_STYLE_COND_NOT_CONTAINS_ERR:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_CONTAINS_ERR:
		res = cv && VALUE_IS_ERROR (cv);
		break;

	case GNM_STYLE_COND_NOT_CONTAINS_BLANKS:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_CONTAINS_BLANKS: {
		const char *s = cv ? value_peek_string (cv) : "";
		while (*s) {
			gunichar uc = g_utf8_get_char (s);
			if (!g_unichar_isspace (uc))
				break;
			s = g_utf8_next_char (s);
		}
		res = (*s == 0);
		break;
	}

	case GNM_STYLE_COND_NOT_CONTAINS_STR:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_CONTAINS_STR:
		res = (cv &&
		       gnm_excel_search_impl (value_peek_string (val0),
					      value_peek_string (cv),
					      0) >= 0);
		break;

	case GNM_STYLE_COND_NOT_BEGINS_WITH_STR:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_BEGINS_WITH_STR:
		res = (cv && case_insensitive_has_fix (cv, val0, TRUE));
		break;

	case GNM_STYLE_COND_NOT_ENDS_WITH_STR:
		negate = TRUE;  /* ...and fall through */
	case GNM_STYLE_COND_ENDS_WITH_STR:
		res = (cv && case_insensitive_has_fix (cv, val0, FALSE));
		break;

	case GNM_STYLE_COND_CUSTOM:
		res = value_get_as_bool (val0, NULL);
		break;

	default:
		g_assert_not_reached ();
	}

	value_release (val0);
	value_release (val1);

	return negate ? !res : res;
}

static void
gnm_style_conditions_finalize (GObject *obj)
{
	GnmStyleConditions *sc = (GnmStyleConditions *)obj;

	while (sc->conditions)
		gnm_style_conditions_delete (sc, sc->conditions->len - 1);
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gnm_style_conditions_init (GnmStyleConditions *sc)
{
	sc->conditions = NULL;
}

static void
gnm_style_conditions_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);
	gobject_class->finalize         = gnm_style_conditions_finalize;
}
GSF_CLASS (GnmStyleConditions, gnm_style_conditions,
	   gnm_style_conditions_class_init, gnm_style_conditions_init,
	   G_TYPE_OBJECT)

/**
 * gnm_style_conditions_new :
 *
 * Convenience tool to create a GnmStyleCondition.  straight g_object_new will work too.
 *
 * Returns a GnmStyleConditions that the caller is resoinsible for.
 **/
GnmStyleConditions  *
gnm_style_conditions_new (Sheet *sheet)
{
	GnmStyleConditions *res;
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	res = g_object_new (gnm_style_conditions_get_type (), NULL);
	res->sheet = sheet;
	return res;
}

/**
 * gnm_style_conditions_dup:
 * @sc: the #GnmStyleConditions to duplicate.
 *
 * Returns: (transfer full): the duplicated #GnmStyleConditions.
 **/
GnmStyleConditions *
gnm_style_conditions_dup (GnmStyleConditions const *sc)
{
	GnmStyleConditions *dup;
	GPtrArray const *ga;
	if (sc == NULL)
		return NULL;

	dup = gnm_style_conditions_new (gnm_style_conditions_get_sheet (sc));
	ga = gnm_style_conditions_details (sc);
	if (ga != NULL) {
		guint i;
		GPtrArray *ga_dup = g_ptr_array_sized_new (ga->len);
		for (i = 0; i < ga->len; i++) {
			GnmStyleCond *cond = g_ptr_array_index (ga, i);
			g_ptr_array_add (ga_dup, gnm_style_cond_dup (cond));
		}
		dup->conditions = ga_dup;
	}
	return dup;
}

#define MIX(H) do {				\
  H *= G_GUINT64_CONSTANT(123456789012345);	\
  H ^= (H >> 31);				\
} while (0)

guint32
gnm_style_conditions_hash (GnmStyleConditions const *sc)
{
	guint64 hash = 42;
	GPtrArray const *ga;
	unsigned ui;

	/*
	 * Note: this hash must not depend on the expressions stored
	 * in ->deps.  And probably not on the sheet either.
	 */

	g_return_val_if_fail (sc != NULL, 0u);

	ga = gnm_style_conditions_details (sc);
	for (ui = 0; ui < (ga ? ga->len : 0u); ui++) {
		GnmStyleCond *cond = g_ptr_array_index (ga, ui);
		if (cond->overlay)
			hash ^= gnm_style_hash_XL (cond->overlay);
		MIX (hash);
		hash ^= cond->op;
		MIX (hash);
	}

	return hash;
}

#undef MIX


/**
 * gnm_style_conditions_get_sheet:
 * @sc: #GnmStyleConditions
 *
 * Returns: (transfer none): the #Sheet.
 **/
Sheet *
gnm_style_conditions_get_sheet (GnmStyleConditions const *sc)
{
	g_return_val_if_fail (sc != NULL, NULL);
	return sc->sheet;
}

void
gnm_style_conditions_set_sheet (GnmStyleConditions *sc, Sheet *sheet)
{
	GPtrArray const *ga;
	unsigned ui;

	g_return_if_fail (sc != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sc->sheet = sheet;
	ga = gnm_style_conditions_details (sc);
	for (ui = 0; ga && ui < ga->len; ui++) {
		GnmStyleCond *cond = g_ptr_array_index (ga, ui);
		gnm_style_cond_set_sheet (cond, sheet);
	}
}


/**
 * gnm_style_conditions_details:
 * @sc: #GnmStyleConditions
 *
 * Returns: (element-type GnmStyleCond) (transfer none): style details.
 **/
GPtrArray const *
gnm_style_conditions_details (GnmStyleConditions const *sc)
{
	g_return_val_if_fail (sc != NULL, NULL);

	return sc->conditions;
}

/**
 * gnm_style_conditions_insert :
 * @sc: #GnmStyleConditions
 * @cond: #GnmStyleCond
 * @pos: position.
 *
 * Insert @cond before @pos (append if @pos < 0).
 **/
void
gnm_style_conditions_insert (GnmStyleConditions *sc,
			     GnmStyleCond const *cond_, int pos)
{
	GnmStyleCond *cond;

	g_return_if_fail (sc != NULL);
	g_return_if_fail (cond_ != NULL);

	g_return_if_fail (gnm_style_cond_is_valid (cond_));

	g_return_if_fail (gnm_style_conditions_get_sheet (sc) ==
			  gnm_style_cond_get_sheet (cond_));

	if (sc->conditions == NULL)
		sc->conditions = g_ptr_array_new ();

	cond = gnm_style_cond_dup (cond_);
	g_ptr_array_add (sc->conditions, cond);
	if (pos >= 0) {
		int i;

		for (i = sc->conditions->len - 1;
		     i > pos;
		     i--)
			g_ptr_array_index (sc->conditions, i) =
				g_ptr_array_index (sc->conditions, i - 1);
		g_ptr_array_index (sc->conditions, pos) = cond;
	}
}

void
gnm_style_conditions_delete (GnmStyleConditions *sc, guint pos)
{
	g_return_if_fail (sc != NULL);
	g_return_if_fail (sc->conditions != NULL);
	g_return_if_fail (sc->conditions->len > pos);

	gnm_style_cond_free (g_ptr_array_index (sc->conditions, pos));
	if (sc->conditions->len <= 1) {
		g_ptr_array_free (sc->conditions, TRUE);
		sc->conditions = NULL;
	} else
		g_ptr_array_remove_index (sc->conditions, pos);
}


/**
 * gnm_style_conditions_overlay:
 * @sc: #GnmStyleConditions
 * @base: #GnmStyle
 *
 * Returns: (element-type GnmStyle) (transfer full): an array of #GnmStyle.
 **/
GPtrArray *
gnm_style_conditions_overlay (GnmStyleConditions const *sc,
			      GnmStyle const *base)
{
	GPtrArray *res;
	unsigned i;

	g_return_val_if_fail (sc != NULL, NULL);
	g_return_val_if_fail (sc->conditions != NULL, NULL);

	res = g_ptr_array_sized_new (sc->conditions->len);
	for (i = 0 ; i < sc->conditions->len; i++) {
		GnmStyleCond const *cond =
			g_ptr_array_index (sc->conditions, i);
		GnmStyle const *overlay = cond->overlay;
		GnmStyle *merge = gnm_style_new_merged (base, overlay);
		/* We only draw a background colour is the pattern != 0 */
		if (merge->pattern == 0 &&
		     elem_is_set (overlay, MSTYLE_COLOR_BACK) &&
		    !elem_is_set (overlay, MSTYLE_PATTERN))
			merge->pattern = 1;
		g_ptr_array_add (res, merge);
	}
	return res;
}

/**
 * gnm_style_conditions_eval:
 * @sc: #GnmStyleConditions
 * @pos: #GnmEvalPos
 *
 * Returns: the condition to use or -1 if none match.
 **/
int
gnm_style_conditions_eval (GnmStyleConditions const *sc, GnmEvalPos const *ep)
{
	unsigned i;
	GPtrArray const *conds;
	GnmCell const *cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	GnmValue const *cv = cell ? cell->value : NULL;

	g_return_val_if_fail (sc != NULL, -1);
	g_return_val_if_fail (sc->conditions != NULL, -1);

	conds = sc->conditions;

	if (debug_style_conds ()) {
		GnmParsePos pp;
		parse_pos_init_evalpos (&pp, ep);

		g_printerr ("Evaluating conditions %p at %s with %d clauses\n",
			    sc,
			    parsepos_as_string (&pp),
			    conds->len);
	}

	for (i = 0 ; i < conds->len ; i++) {
		GnmStyleCond const *cond = g_ptr_array_index (conds, i);
		gboolean use_this = gnm_style_cond_eval (cond, cv, ep);

		if (use_this) {
			if (debug_style_conds ())
				g_printerr ("  Using clause %d\n", i);
			return i;
		}
	}

	if (debug_style_conds ())
		g_printerr ("  No matching clauses\n");

	return -1;
}
