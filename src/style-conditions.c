/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * style-conditions.c: 
 *
 * Copyright (C) 2005 Jody Goldberg (jody@gnome.org)
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
#include "gnumeric.h"
#include "style-conditions.h"
#include "mstyle.h"
#include "gnm-style-impl.h"
#include "expr.h"
#include "value.h"
#include <gsf/gsf-impl-utils.h>

typedef GObjectClass GnmStyleConditionsClass;
struct _GnmStyleConditions {
	GObject base;
	GArray *conditions;
};

static GObjectClass *parent_class;

static gboolean
cond_validate (GnmStyleCond const *cond)
{
	g_return_val_if_fail (cond != NULL, FALSE);
	g_return_val_if_fail (cond->overlay != NULL, FALSE);
	g_return_val_if_fail (cond->expr[0] != NULL, FALSE);
	g_return_val_if_fail ((cond->op == GNM_STYLE_COND_BETWEEN ||
			   cond->op == GNM_STYLE_COND_NOT_BETWEEN) ^
			  (cond->expr[1] == NULL), FALSE);

	return TRUE;
}
	
static void
cond_unref (GnmStyleCond const *cond)
{
	/* Be very delicate, this is called for invalid conditions too */
	if (cond->overlay)
		gnm_style_unref (cond->overlay);
	if (cond->expr[0])
		gnm_expr_unref (cond->expr[0]);
	if (cond->expr[1])
		gnm_expr_unref (cond->expr[1]);
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

	if (sc == NULL || !cond_validate (cond)) {
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
		merge = gnm_style_merge (base, overlay);
		/* We only draw a background colour is the pattern != 0 */
		if (merge->pattern == 0 &&
		     elem_is_set (overlay, MSTYLE_COLOR_BACK) &&
		    !elem_is_set (overlay, MSTYLE_PATTERN))
			merge->pattern = 1;
		g_ptr_array_add (res, merge);
	}
	return res;
}

#include <parse-util.h>
int
gnm_style_conditions_eval (GnmStyleConditions const *sc, GnmEvalPos const *ep)
{
	unsigned i;
	gboolean use_this = FALSE;
	GnmValue *val;
	GArray const *conds;
	GnmStyleCond const *cond;
	char *str;
	GnmParsePos pp;

	g_return_val_if_fail (sc != NULL, -1);
	g_return_val_if_fail (sc->conditions != NULL, -1);

	conds = sc->conditions;
	parse_pos_init_evalpos (&pp, ep);
	for (i = 0 ; i < conds->len ; i++) {
		cond = &g_array_index (conds, GnmStyleCond, i);

		val = gnm_expr_eval (cond->expr[0], ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		switch (cond->op) {
		case GNM_STYLE_COND_BETWEEN:
		case GNM_STYLE_COND_NOT_BETWEEN:
		case GNM_STYLE_COND_EQUAL:
		case GNM_STYLE_COND_NOT_EQUAL:
		case GNM_STYLE_COND_GT:
		case GNM_STYLE_COND_LT:
		case GNM_STYLE_COND_GTE:
		case GNM_STYLE_COND_LTE:
			break;

		case GNM_STYLE_COND_CUSTOM:
			use_this = value_get_as_bool (val, NULL);
#if 0
			str = gnm_expr_as_string (cond->expr[0], &pp, gnm_expr_conventions_default);
			g_print ("'%s' = %s\n", str, use_this ? "true" : "false");
			g_free (str);
#endif
		}
		value_release (val);
		if (use_this)
			return i;

	}
	return -1;
}
