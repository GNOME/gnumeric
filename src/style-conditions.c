/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * style-conditions.c:
 *
 * Copyright (C) 2005-2007 Jody Goldberg (jody@gnome.org)
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
#include "cell.h"
#include "value.h"
#include "sheet.h"
#include <parse-util.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>
#include <parse-util.h>

#define BLANKS_STRING_FOR_MATCHING " \t\n\r"

typedef GObjectClass GnmStyleConditionsClass;
struct _GnmStyleConditions {
	GObject base;
	GPtrArray *conditions;
	Sheet *sheet;
};

static GObjectClass *parent_class;

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
 * @cond : #GnmStyleCond
 *
 * Returns: TRUE if @cond is in a reasonable state
 **/
gboolean
gnm_style_cond_is_valid (GnmStyleCond const *cond)
{
	unsigned ui, N;

	g_return_val_if_fail (cond != NULL, FALSE);

	if (cond->overlay == NULL) return FALSE;

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
static GSF_CLASS (GnmStyleConditions, gnm_style_conditions,
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
 * gnm_style_conditions_details :
 * @sc : #GnmStyleConditions
 *
 * Returns an array of GnmStyleCond which should not be modified.
 **/
GPtrArray const *
gnm_style_conditions_details (GnmStyleConditions const *sc)
{
	g_return_val_if_fail (sc != NULL, NULL);

	return sc->conditions;
}

/**
 * gnm_style_conditions_insert :
 * @sc : #GnmStyleConditions
 * @cond : #GnmStyleCond
 * @pos : position.
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
 * gnm_style_conditions_eval :
 * @sc : #GnmStyleConditions
 * @ep : #GnmEvalPos
 *
 * Returns the condition to use or -1 if none match.
 **/
int
gnm_style_conditions_eval (GnmStyleConditions const *sc, GnmEvalPos const *ep)
{
	unsigned i;
	gboolean use_this = FALSE;
	GnmValue *val = NULL;
	GPtrArray const *conds;
	GnmParsePos pp;
	GnmCell const *cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	GnmValue const *cv = cell ? cell->value : NULL;
	/*We should really assert that cv is not NULL, but asserts are apparently frowned upon.*/

	g_return_val_if_fail (sc != NULL, -1);
	g_return_val_if_fail (sc->conditions != NULL, -1);

	conds = sc->conditions;
	parse_pos_init_evalpos (&pp, ep);
	for (i = 0 ; i < conds->len ; i++) {
		GnmStyleCond const *cond = g_ptr_array_index (conds, i);

		if (cond->op == GNM_STYLE_COND_CONTAINS_ERR)
			use_this = (cv != NULL) && VALUE_IS_ERROR (cv);
		else if (cond->op == GNM_STYLE_COND_NOT_CONTAINS_ERR)
			use_this = (cv == NULL) || !VALUE_IS_ERROR (cv);
		else if (cond->op == GNM_STYLE_COND_CONTAINS_BLANKS ||
			 cond->op == GNM_STYLE_COND_NOT_CONTAINS_BLANKS) {
			if (cv && VALUE_IS_STRING (cv)) {
				char const *cvstring = value_peek_string (cv);
				switch (cond->op) {
				case GNM_STYLE_COND_CONTAINS_BLANKS :
					use_this = NULL != strpbrk (cvstring, BLANKS_STRING_FOR_MATCHING);
					break;
				case GNM_STYLE_COND_NOT_CONTAINS_BLANKS :
					use_this = NULL == strpbrk (cvstring, BLANKS_STRING_FOR_MATCHING);
					break;
				default:
					break;
				}
			}
		} else {
			val = gnm_expr_top_eval (cond->deps[0].texpr, ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
			if (cond->op == GNM_STYLE_COND_CUSTOM) {
				use_this = value_get_as_bool (val, NULL);
#if 0
				char *str = gnm_expr_as_string (cond->expr[0],
								&pp, NULL);
				g_print ("'%s' = %s\n", str, use_this ? "true" : "false");
				g_free (str);
#endif
			} else if (cond->op < GNM_STYLE_COND_CONTAINS_STR) {
				GnmValDiff diff = value_compare (cv, val, TRUE);

				switch (cond->op) {
				default:
				case GNM_STYLE_COND_EQUAL:	use_this = (diff == IS_EQUAL); break;
				case GNM_STYLE_COND_NOT_EQUAL:	use_this = (diff != IS_EQUAL); break;
				case GNM_STYLE_COND_NOT_BETWEEN:
					if (diff == IS_LESS) {
						use_this = TRUE;
						break;
					}
					value_release (val);
					val = gnm_expr_top_eval (cond->deps[1].texpr, ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
					diff = value_compare (cv, val, TRUE);
					/* fall through */

				case GNM_STYLE_COND_GT:		use_this = (diff == IS_GREATER); break;
				case GNM_STYLE_COND_LT:		use_this = (diff == IS_LESS); break;
				case GNM_STYLE_COND_GTE:	use_this = (diff == IS_GREATER || diff == IS_EQUAL); break;
				case GNM_STYLE_COND_BETWEEN:
					if (diff == IS_LESS)
						break;
					value_release (val);
					val = gnm_expr_top_eval (cond->deps[1].texpr, ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
					diff = value_compare (cv, val, TRUE);
					/* fall through */
				case GNM_STYLE_COND_LTE:	use_this = (diff == IS_LESS || diff == IS_EQUAL); break;
				}
			} else if (cv && VALUE_IS_STRING (cv)) {
				char const *valstring = value_peek_string (val);
				char const *cvstring = value_peek_string (cv);

				switch (cond->op) {
				default : g_warning ("Unknown condition operator %d", cond->op);
					break;
				case GNM_STYLE_COND_CONTAINS_STR :
					use_this = (NULL != strstr (cvstring, valstring));
					break;
				case GNM_STYLE_COND_NOT_CONTAINS_STR :
					use_this = (NULL == strstr (cvstring, valstring));
					break;
				case GNM_STYLE_COND_BEGINS_WITH_STR :
					use_this = g_str_has_prefix (cvstring, valstring);
					break;
				case GNM_STYLE_COND_NOT_BEGINS_WITH_STR :
					use_this = !g_str_has_prefix (cvstring, valstring);
					break;
				case GNM_STYLE_COND_ENDS_WITH_STR :
					use_this = g_str_has_suffix (cvstring, valstring);
					break;
				case GNM_STYLE_COND_NOT_ENDS_WITH_STR :
					use_this = !g_str_has_suffix (cvstring, valstring);
					break;
				}
			}
			value_release (val);
		}

		if (use_this)
			return i;
	}
	return -1;
}
