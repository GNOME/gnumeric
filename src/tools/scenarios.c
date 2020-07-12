/*
 * scenarios.c:
 *
 * (C) Copyright 2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2009 by Morten Welinder <terra@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <gsf/gsf-impl-utils.h>

#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <value.h>
#include <workbook.h>
#include <expr.h>

#include <tools/scenarios.h>
#include <tools/dao.h>
#include <clipboard.h>
#include <parse-util.h>
#include <goffice/goffice.h>

#include <string.h>

/* ------------------------------------------------------------------------- */

GnmScenarioItem *
gnm_scenario_item_new (Sheet *sheet)
{
	GnmScenarioItem *sci = g_new0 (GnmScenarioItem, 1);
	dependent_managed_init (&sci->dep, sheet);
	return sci;
}

void
gnm_scenario_item_free (GnmScenarioItem *sci)
{
	gnm_scenario_item_set_range (sci, NULL);
	gnm_scenario_item_set_value (sci, NULL);
	g_free (sci);
}

static GnmScenarioItem *
gnm_scenario_item_dup (GnmScenarioItem *src)
{
	GnmScenarioItem *dst = gnm_scenario_item_new (src->dep.base.sheet);
	dependent_managed_set_expr (&dst->dep, src->dep.base.texpr);
	dst->value = value_dup (src->value);
	return dst;
}

GType
gnm_scenario_item_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmScenarioItem",
			 (GBoxedCopyFunc)gnm_scenario_item_dup,
			 (GBoxedFreeFunc)gnm_scenario_item_free);
	}
	return t;
}

void
gnm_scenario_item_set_range (GnmScenarioItem *sci, const GnmSheetRange *sr)
{
	if (sr) {
		GnmValue *v = value_new_cellrange_r
			(sr->sheet != sci->dep.base.sheet ? sr->sheet : NULL,
			 &sr->range);
		GnmExprTop const *texpr = gnm_expr_top_new_constant (v);
		dependent_managed_set_expr (&sci->dep, texpr);
		gnm_expr_top_unref (texpr);
	} else
		dependent_managed_set_expr (&sci->dep, NULL);
}

void
gnm_scenario_item_set_value (GnmScenarioItem *sci, const GnmValue *v)
{
	value_release (sci->value);
	sci->value = value_dup (v);
}

gboolean
gnm_scenario_item_valid (const GnmScenarioItem *sci, GnmSheetRange *sr)
{
	GnmExprTop const *texpr;
	GnmValue const *vr;

	if (!sci || !((texpr = sci->dep.base.texpr)))
		return FALSE;

	vr = gnm_expr_top_get_constant (texpr);
	if (!vr || !VALUE_IS_CELLRANGE (vr))
		return FALSE;

	if (sr)
		gnm_sheet_range_from_value
			(sr, gnm_expr_top_get_constant (texpr));
	return TRUE;
}

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_scenario_parent_class;

static void
gnm_scenario_finalize (GObject *obj)
{
	GnmScenario *sc = GNM_SCENARIO (obj);

	g_free (sc->name);
	g_free (sc->comment);

	g_slist_free_full (sc->items, (GDestroyNotify)gnm_scenario_item_free);

	gnm_scenario_parent_class->finalize (obj);
}

static void
gnm_scenario_class_init (GObjectClass *object_class)
{
	gnm_scenario_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_scenario_finalize;
}

GSF_CLASS (GnmScenario, gnm_scenario,
	   &gnm_scenario_class_init, NULL, G_TYPE_OBJECT)

GnmScenario *
gnm_scenario_new (char const *name, Sheet *sheet)
{
	GnmScenario *sc = g_object_new (GNM_SCENARIO_TYPE, NULL);

	sc->sheet = sheet;
	sc->name = g_strdup (name);

	return sc;
}

/**
 * gnm_scenario_dup:
 * @sc: #GnmScenario
 * @new_sheet: #Sheet
 *
 * Returns: (transfer full): the duplicated scenario.
 **/
GnmScenario *
gnm_scenario_dup (GnmScenario *src, Sheet *new_sheet)
{
	GnmScenario *dst;

	dst = gnm_scenario_new (src->name, new_sheet);
	gnm_scenario_set_comment (dst, src->comment);
	dst->items = g_slist_copy_deep
		(src->items, (GCopyFunc)gnm_scenario_item_dup, NULL);
	return dst;
}

void
gnm_scenario_set_comment (GnmScenario *sc, const char *comment)
{
	char *s = g_strdup (comment);
	g_free (sc->comment);
	sc->comment = s;
}

struct cb_save_cells {
	GSList *items;
	GnmScenario *sc;
};

static GnmValue *
cb_save_cells (GnmCellIter const *iter, gpointer user)
{
	struct cb_save_cells *pdata = user;
	GnmCell *cell = iter->cell;
	GnmScenarioItem *sci = gnm_scenario_item_new (pdata->sc->sheet);
	GnmSheetRange sr;

	/* FIXME: Think about arrays.  */

	sr.sheet = cell->base.sheet;
	sr.range.start = sr.range.end = iter->pp.eval;
	gnm_scenario_item_set_range (sci, &sr);
	gnm_scenario_item_set_value (sci, cell->value);

	pdata->items = g_slist_prepend (pdata->items, sci);

	return NULL;
}


void
gnm_scenario_add_area (GnmScenario *sc, const GnmSheetRange *sr)
{
	GnmScenarioItem *sci;
	struct cb_save_cells data;

	g_return_if_fail (GNM_IS_SCENARIO (sc));
	g_return_if_fail (sr != NULL);

	sci = gnm_scenario_item_new (sc->sheet);
	gnm_scenario_item_set_range (sci, sr);
	sc->items = g_slist_prepend (sc->items, sci);

	data.items = NULL;
	data.sc = sc;
	sheet_foreach_cell_in_range
		(eval_sheet (sr->sheet, sc->sheet),
		 CELL_ITER_IGNORE_NONEXISTENT, &sr->range,
		 cb_save_cells, &data);
	sc->items = g_slist_concat (sc->items,
				    g_slist_reverse (data.items));
}

/**
 * gnm_scenario_apply:
 * @sc: #GnmScenario
 *
 * Returns: (transfer full): the newly allocated #GOUndo.
 **/
GOUndo *
gnm_scenario_apply (GnmScenario *sc)
{
	GOUndo *undo = NULL;
	GSList *l;

	g_return_val_if_fail (GNM_IS_SCENARIO (sc), NULL);

	for (l = sc->items; l; l = l->next) {
		GnmScenarioItem *sci = l->data;
		GnmValue const *val = sci->value;
		GnmSheetRange sr;
		Sheet *sheet;

		if (!gnm_scenario_item_valid (sci, &sr))
			continue;
		sheet = eval_sheet (sr.sheet, sc->sheet);

		if (val) {
			/* FIXME: think about arrays.  */
			GnmCell *cell = sheet_cell_fetch
				(sheet,
				 sr.range.start.col,
				 sr.range.start.row);
			sheet_cell_set_value (cell, value_dup (val));
		} else {
			GOUndo *u = clipboard_copy_range_undo (sheet,
							       &sr.range);
			/* FIXME: Clear the range.  */
			undo = go_undo_combine (undo, u);
		}
	}

	return undo;
}

char *
gnm_scenario_get_range_str (const GnmScenario *sc)
{
	GString *str;
	GSList *l;

	g_return_val_if_fail (GNM_IS_SCENARIO (sc), NULL);

	str = g_string_new (NULL);
	for (l = sc->items; l; l = l->next) {
		GnmScenarioItem const *sci = l->data;
		GnmValue const *vrange;
		if (sci->value || !gnm_scenario_item_valid (sci, NULL))
			continue;
		if (str->len)
			g_string_append_c (str, ',');
		vrange = gnm_expr_top_get_constant (sci->dep.base.texpr);
		g_string_append (str, value_peek_string (vrange));
	}

	return g_string_free (str, FALSE);
}

/* ------------------------------------------------------------------------- */

