/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * scenarios.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <sheet-filter.h>
#include <cell.h>
#include <ranges.h>
#include <gui-util.h>
#include <tool-dialogs.h>
#include <dao-gui-utils.h>
#include <value.h>
#include <workbook-edit.h>

#include <glade/glade.h>
#include <widgets/gnumeric-expr-entry.h>
#include "mathfunc.h"
#include "scenarios.h"
#include "dao.h"


void
scenarios_ok (WorkbookControl        *wbc,
	      data_analysis_output_t *dao)
{
	sheet_redraw_all (dao->sheet, TRUE);
}

static scenario_t *
find_scenario (GList *scenarios, gchar *name)
{
	scenario_t *s;

	while (scenarios != NULL) {
		s = (scenario_t *) scenarios->data;

		if (strcmp (s->name, name) == 0)
			return s;
		scenarios = scenarios->next;
	}
	return NULL;
}

void
scenario_show (WorkbookControl        *wbc,
	       gchar                  *name,
	       data_analysis_output_t *dao)
{
	scenario_t *s;
	int        i, j, cols;

	s = find_scenario (dao->sheet->scenarios, name);
	if (s == NULL)
		return;

	cols = s->range.end.col - s->range.start.col + 1;
	for (i = s->range.start.row; i <= s->range.end.row; i++)
		for (j = s->range.start.col; j <= s->range.end.col; j++) {
			dao_set_cell_value
				(dao, j, i, value_duplicate
				 (s->changing_cells [j + i * cols]));
		}

	workbook_recalc (wb_control_workbook (wbc));
	sheet_redraw_all (dao->sheet, TRUE);
}


/* Scenario Add ************************************************************/

static scenario_t *
scenario_new (gchar *name, gchar *comment)
{
	scenario_t *s;

	s = g_new (scenario_t, 1);

	s->name           = name;
	s->comment        = comment;
	s->changing_cells = NULL;
	s->cell_sel_str   = NULL;

	return s;
}

static void
scenario_free (scenario_t *s)
{
	int i, j, cols;

	g_free (s->name);
	g_free (s->comment);
	g_free (s->cell_sel_str);

	cols = s->range.end.col - s->range.start.col + 1;
	for (i = s->range.start.row; i <= s->range.end.row; i++)
		for (j = s->range.start.col; j <= s->range.end.col; j++)
			value_release (s->changing_cells [j + i * cols]);

	g_free (s->changing_cells);
	g_free (s);
}

static void
collect_values (Sheet *sheet, scenario_t *s, ValueRange *range)
{
	int     i, j;
	int     cols, rows;
	Cell    *cell;
	CellRef *a, *b;

	range_init_value (&s->range, (Value *) range);

	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;
	s->changing_cells = g_new (Value *, rows * cols);

	for (i = s->range.start.row; i <= s->range.end.row; i++)
		for (j = s->range.start.col; j <= s->range.end.col; j++) {
			cell = sheet_cell_fetch (sheet, j, i);
			s->changing_cells [j + i * cols] = 
				value_duplicate (cell->value);
		}
}

void
scenario_add_new (WorkbookControl        *wbc,
		  gchar                  *name,
		  Value                  *changing_cells,
		  gchar                  *cell_sel_str,
		  gchar                  *comment,
		  data_analysis_output_t *dao)
{
	scenario_t *scenario;
	int        i, j;
	int        cols, rows;

	scenario = scenario_new (name, comment);

	collect_values (dao->sheet, scenario, (ValueRange *) changing_cells);

	scenario->cell_sel_str = g_strdup (cell_sel_str);
	dao->sheet->scenarios = g_list_append (dao->sheet->scenarios, 
					       (gpointer) scenario);

	sheet_redraw_all (dao->sheet, TRUE);
}

void
scenario_delete (WorkbookControl        *wbc,
		 gchar                  *name,
		 data_analysis_output_t *dao)
{
	scenario_t *s;

	s = find_scenario (dao->sheet->scenarios, name);
	dao->sheet->scenarios = g_list_remove (dao->sheet->scenarios,
					       (gpointer) s);
	scenario_free (s);
}

void
scenario_summary (WorkbookControl        *wbc,
		  data_analysis_output_t *dao)
{
	/* Fixme: Implement */

	sheet_redraw_all (dao->sheet, TRUE);
}
