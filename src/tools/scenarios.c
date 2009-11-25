/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

#include "scenarios.h"
#include "dao.h"
#include "clipboard.h"
#include "parse-util.h"
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

void
gnm_scenario_item_set_range (GnmScenarioItem *sci, const GnmSheetRange *sr)
{
	if (sr) {
		GnmValue *v = value_new_cellrange_r
			(sr->sheet != sci->dep.sheet ? sr->sheet : NULL,
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

	if (!sci || !((texpr = sci->dep.texpr)))
		return FALSE;

	vr = gnm_expr_top_get_constant (texpr);
	if (!vr || vr->type != VALUE_CELLRANGE)
		return FALSE;

	if (sr)
		gnm_sheet_range_from_value
			(sr, gnm_expr_top_get_constant (texpr));
	return TRUE;
}

/* ------------------------------------------------------------------------- */

typedef GnmValue * (*ScenarioValueCB) (int col, int row, GnmValue *v, gpointer data);

static void
scenario_for_each_value (GnmScenario *s, ScenarioValueCB fn, gpointer data)
{
	int        i, j, cols, pos;

	if (!s->changing_cells)
		return;

	cols = s->range.end.col - s->range.start.col + 1;
	for (i = s->range.start.row; i <= s->range.end.row; i++)
		for (j = s->range.start.col; j <= s->range.end.col; j++) {
			pos = j - s->range.start.col +
				(i - s->range.start.row) * cols;
			s->changing_cells [pos] = fn (j, i,
						      s->changing_cells [pos],
						      data);
		}
}

static GnmValue *
cb_value_free (int col, int row, GnmValue *v, gpointer data)
{
	value_release (v);

	return NULL;
}

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_scenario_parent_class;

static void
gnm_scenario_finalize (GObject *obj)
{
	GnmScenario *sc = GNM_SCENARIO (obj);

	g_free (sc->name);
	g_free (sc->comment);

	go_slist_free_custom (sc->items, (GFreeFunc)gnm_scenario_item_free);

	g_free (sc->cell_sel_str);

	scenario_for_each_value (sc, cb_value_free, NULL);

	g_free (sc->changing_cells);

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
		 CELL_ITER_IGNORE_NONEXISTENT,
		 sr->range.start.col, sr->range.start.row,
		 sr->range.end.col, sr->range.end.row,
		 cb_save_cells, &data);
	sc->items = g_slist_concat (sc->items,
				    g_slist_reverse (data.items));
}

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

		if (!gnm_scenario_item_valid (sci, &sr))
			continue;

		if (val) {
			/* FIXME: think about arrays.  */
			GnmCell *cell = sheet_cell_fetch
				(eval_sheet (sr.sheet, sc->sheet),
				 sr.range.start.col,
				 sr.range.start.row);
			sheet_cell_set_value (cell, value_dup (val));
		} else {
			GOUndo *u = clipboard_copy_range_undo (sr.sheet,
							       &sr.range);
			undo = go_undo_combine (undo, u);
		}
	}

	return undo;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	gboolean expr_flag;
	Sheet    *sheet;
} collect_cb_t;

static GnmValue *
collect_cb (int col, int row, GnmValue *v, collect_cb_t *p)
{
	GnmCell *cell = sheet_cell_fetch (p->sheet, col, row);

	p->expr_flag |= gnm_cell_has_expr (cell);

	return value_dup (cell->value);
}

/* Scenario: Duplicate sheet ***********************************************/

typedef struct {
	int        rows;
	int        cols;
	int        col_offset;
	int        row_offset;
	GnmScenario *dest;
} copy_cb_t;

static GnmValue *
copy_cb (int col, int row, GnmValue *v, copy_cb_t *p)
{
	p->dest->changing_cells [col - p->col_offset +
				 (row - p->row_offset) * p->cols] =
		value_dup (v);

	return v;
}

GnmScenario *
gnm_scenario_dup (GnmScenario *src, Sheet *new_sheet)
{
	GnmScenario *dst;
	copy_cb_t  cb;

	dst = gnm_scenario_new (src->name, new_sheet);
	gnm_scenario_set_comment (dst, src->comment);
	dst->cell_sel_str = g_strdup (src->cell_sel_str);
	dst->range = src->range;

	cb.rows       = src->range.end.row - src->range.start.row + 1;
	cb.cols       = src->range.end.col - src->range.start.col + 1;
	cb.col_offset = src->range.start.col;
	cb.row_offset = src->range.start.row;
	cb.dest       = dst;

	dst->changing_cells = g_new (GnmValue *, cb.rows * cb.cols);
	scenario_for_each_value (src, (ScenarioValueCB) copy_cb, &cb);

	return dst;
}

static GnmValue *
show_cb (int col, int row, GnmValue *v, data_analysis_output_t *dao)
{
	dao_set_cell_value (dao, col, row, value_dup (v));

	return v;
}

GnmScenario *
scenario_show (GnmScenario             *s,
	       GnmScenario             *old_values,
	       data_analysis_output_t *dao)
{
	GnmScenario   *stored_values;
	int           rows, cols;
	collect_cb_t  cb;

	/* Recover values of the previous show call. */
	if (old_values) {
		scenario_for_each_value (old_values, (ScenarioValueCB) show_cb,
					 dao);
		g_object_unref (old_values);
	}

	if (s == NULL)
		return NULL;

	/* Store values for recovery. */
	stored_values = gnm_sheet_scenario_new (dao->sheet, "");
	stored_values->range = s->range;
	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;
	stored_values->changing_cells = g_new (GnmValue *, rows * cols);

	cb.sheet = dao->sheet;
	scenario_for_each_value (stored_values, (ScenarioValueCB) collect_cb,
				 &cb);

	/* Show scenario and recalculate. */
	scenario_for_each_value (s, (ScenarioValueCB) show_cb, dao);
	workbook_recalc (dao->sheet->workbook);
	sheet_redraw_all (dao->sheet, TRUE);

	return stored_values;
}
