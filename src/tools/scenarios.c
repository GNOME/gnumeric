/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * scenarios.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
#include <sheet-filter.h>
#include <cell.h>
#include <ranges.h>
#include <value.h>
#include <workbook.h>
#include <workbook-control.h>

#include "mathfunc.h"
#include "scenarios.h"
#include "dao.h"
#include "style-color.h"
#include "parse-util.h"
#include <goffice/goffice.h>

#include <string.h>

/* ------------------------------------------------------------------------- */

typedef GnmValue * (*ScenarioValueCB) (int col, int row, GnmValue *v, gpointer data);

static void
scenario_for_each_value (GnmScenario *s, ScenarioValueCB fn, gpointer data)
{
	int        i, j, cols, pos;

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

	g_free (sc->cell_sel_str);

	scenario_for_each_value (sc, cb_value_free, NULL);

	g_free (sc->changing_cells);

	gnm_scenario_parent_class->finalize (obj);
}

GnmScenario *
gnm_scenario_new (char const *name, char const *comment, Sheet *sheet)
{
	GnmScenario *sc = g_object_new (GNM_SCENARIO_TYPE, NULL);

	sc->sheet = sheet;
	sc->name = g_strdup (name);
	sc->comment = g_strdup (comment);

	return sc;
}

static void
gnm_scenario_class_init (GObjectClass *object_class)
{
	gnm_scenario_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_scenario_finalize;
}

GSF_CLASS (GnmScenario, gnm_scenario,
	   &gnm_scenario_class_init, NULL, G_TYPE_OBJECT)

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

/*
 * Collects cell values of a scenario. If any of the cells contain an
 * expression return TRUE so that the user can be warned about it.
 */
static gboolean
collect_values (Sheet *sheet, GnmScenario *s, GnmValueRange *range)
{
	int          cols, rows;
	collect_cb_t cb;

	range_init_value (&s->range, (GnmValue *) range);

	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;
	s->changing_cells = g_new0 (GnmValue *, rows * cols);

	cb.expr_flag = FALSE;
	cb.sheet     = sheet;
	scenario_for_each_value (s, (ScenarioValueCB) collect_cb, &cb);

	return cb.expr_flag;
}

/* Doesn't actually add the new scenario into the sheet's scenario list. */
gboolean
scenario_add_new (gchar const *name,
		  GnmValue *changing_cells,
		  gchar const *cell_sel_str,
		  gchar const *comment,
		  Sheet *sheet,
		  GnmScenario **new_scenario)
{
	GnmScenario *scenario = gnm_sheet_scenario_new (sheet, name);
	gboolean    res = collect_values (sheet, scenario, (GnmValueRange *) changing_cells);

	scenario->comment = g_strdup (comment);
	scenario->cell_sel_str = g_strdup (cell_sel_str);
	sheet_redraw_all (sheet, TRUE);

	*new_scenario = scenario;
	return res;
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

	dst = gnm_scenario_new (src->name, src->comment, new_sheet);
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
	stored_values = gnm_sheet_scenario_new (s->sheet, "");
	stored_values->range = s->range;
	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;
	stored_values->changing_cells = g_new (GnmValue *, rows * cols);

	cb.sheet = s->sheet;
	scenario_for_each_value (stored_values, (ScenarioValueCB) collect_cb,
				 &cb);

	/* Show scenario and recalculate. */
	scenario_for_each_value (s, (ScenarioValueCB) show_cb, dao);
	workbook_recalc (s->sheet->workbook);
	sheet_redraw_all (s->sheet, TRUE);

	return stored_values;
}
