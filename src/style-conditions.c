/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * style-conditions.c:
 *
 * Copyright (C) 2005-2007 Jody Goldberg (jody@gnome.org)
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
	GArray *conditions;
};

static GObjectClass *parent_class;

/**
 * gnm_style_cond_is_valid :
 * @cond : #GnmStyleCond
 *
 * Returns: TRUE if @cond is in a reasonable state
 **/
gboolean
gnm_style_cond_is_valid (GnmStyleCond const *cond)
{
	g_return_val_if_fail (cond != NULL, FALSE);

	if (cond->overlay == NULL) return FALSE;
	if ((cond->texpr[0] != NULL) ^ 
	    (cond->op != GNM_STYLE_COND_CONTAINS_ERR &&
	     cond->op != GNM_STYLE_COND_NOT_CONTAINS_ERR &&
	     cond->op != GNM_STYLE_COND_CONTAINS_BLANKS &&
	     cond->op != GNM_STYLE_COND_NOT_CONTAINS_BLANKS)) return FALSE;
	if ((cond->texpr[1] != NULL) ^
	    (cond->op == GNM_STYLE_COND_BETWEEN ||
	     cond->op == GNM_STYLE_COND_NOT_BETWEEN))
	    return FALSE;
	return TRUE;
}

static void
cond_unref (GnmStyleCond const *cond)
{
	/* Be very delicate, this is called for invalid conditions too */
	if (cond->overlay)
		gnm_style_unref (cond->overlay);
	if (cond->texpr[0])
		gnm_expr_top_unref (cond->texpr[0]);
	if (cond->texpr[1])
		gnm_expr_top_unref (cond->texpr[1]);
}

static void
gnm_style_conditions_finalize (GObject *obj)
{
	GnmStyleConditions *sc = (GnmStyleConditions *)obj;

	if (sc->conditions != NULL) {
		int i = sc->conditions->len;
		while (i-- > 0)
			cond_unref (&g_array_index (sc->conditions, GnmStyleCond, i));
		g_array_free (sc->conditions, TRUE);
		sc->conditions = NULL;
	}
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
gnm_style_conditions_new (void)
{
	return g_object_new (gnm_style_conditions_get_type (), NULL);
}

GnmStyleConditions *
gnm_style_conditions_dup  (GnmStyleConditions const *cond)
{
	GnmStyleConditions  *dup;
	GArray const *ga;
	if (cond == NULL)
		return NULL;

	dup =  gnm_style_conditions_new ();
	ga = gnm_style_conditions_details (cond);
	if (ga != NULL) {
		guint i;
		GArray *ga_dup = g_array_sized_new (FALSE, FALSE, sizeof (GnmStyleCond),
						    ga->len);
		for (i = 0; i < ga->len; i++) {
			GnmStyleCond gsc = g_array_index(ga, GnmStyleCond, i);

			gnm_style_ref (gsc.overlay);
			if (gsc.texpr[0])
				gnm_expr_top_ref (gsc.texpr[0]);
			if (gsc.texpr[1])
				gnm_expr_top_ref (gsc.texpr[1]);
			g_array_append_val (ga_dup, gsc);
		}
		dup->conditions = ga_dup;
	}
	return dup;
}


/**
 * gnm_style_conditions_details :
 * @sc : #GnmStyleConditions
 *
 * Returns an array of GnmStyleCond which should not be modified.
 **/
GArray const	*
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
			     GnmStyleCond const *cond, int pos)
{
	g_return_if_fail (cond != NULL);

	if (sc == NULL || !gnm_style_cond_is_valid (cond)) {
		cond_unref (cond); /* be careful not to leak */
		return;
	}

	if (sc->conditions == NULL)
		sc->conditions = g_array_new (FALSE, FALSE, sizeof (GnmStyleCond));

	if (pos < 0)
		g_array_append_val (sc->conditions, *cond);
	else
		g_array_insert_val (sc->conditions, pos, *cond);
}

void 
gnm_style_conditions_delete  (GnmStyleConditions *sc,
			      guint pos)
{
	g_return_if_fail (sc != NULL);
	g_return_if_fail (sc->conditions != NULL);
	g_return_if_fail (sc->conditions->len > pos);

	cond_unref (&g_array_index (sc->conditions, GnmStyleCond, pos));
	g_array_remove_index (sc->conditions, pos);
}


GPtrArray *
gnm_style_conditions_overlay (GnmStyleConditions const *sc,
			      GnmStyle const *base)
{
	GPtrArray *res;
	GnmStyle const *overlay;
	GnmStyle *merge;
	unsigned i;

	g_return_val_if_fail (sc != NULL, NULL);
	g_return_val_if_fail (sc->conditions != NULL, NULL);

	res = g_ptr_array_sized_new (sc->conditions->len);
	for (i = 0 ; i < sc->conditions->len; i++) {
		overlay = g_array_index (sc->conditions, GnmStyleCond, i).overlay;
		merge = gnm_style_new_merged (base, overlay);
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
	GArray const *conds;
	GnmStyleCond const *cond;
	GnmParsePos pp;
	GnmCell const *cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	GnmValue const *cv = cell ? cell->value : NULL;
	/*We should really assert that cv is not NULL, but asserts are apparently frowned upon.*/

	g_return_val_if_fail (sc != NULL, -1);
	g_return_val_if_fail (sc->conditions != NULL, -1);

	conds = sc->conditions;
	parse_pos_init_evalpos (&pp, ep);
	for (i = 0 ; i < conds->len ; i++) {
		cond = &g_array_index (conds, GnmStyleCond, i);

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
			val = gnm_expr_top_eval (cond->texpr[0], ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
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
					val = gnm_expr_top_eval (cond->texpr[1], ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
					diff = value_compare (cv, val, TRUE);
					/* fall through */

				case GNM_STYLE_COND_GT:		use_this = (diff == IS_GREATER); break;
				case GNM_STYLE_COND_LT:		use_this = (diff == IS_LESS); break;
				case GNM_STYLE_COND_GTE:	use_this = (diff == IS_GREATER || diff == IS_EQUAL); break;
				case GNM_STYLE_COND_BETWEEN:
					if (diff == IS_LESS)
						break;
					value_release (val);
					val = gnm_expr_top_eval (cond->texpr[1], ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
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
