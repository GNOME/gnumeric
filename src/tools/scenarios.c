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


/* Generic stuff **********************************************************/

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

/* Scenario: Show **********************************************************/

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
				 (s->changing_cells [j-s->range.start.col +
						     (i-s->range.start.row) *
						     cols]));
		}

	workbook_recalc (wb_control_workbook (wbc));
	sheet_redraw_all (dao->sheet, TRUE);
}


/* Scenario: Add ***********************************************************/

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

/*
 * Collects cell values of a scenario. If any of the cells contain an
 * expression return TRUE so that the user can be warned about it.
 */
static gboolean
collect_values (Sheet *sheet, scenario_t *s, ValueRange *range)
{
	int      i, j;
	int      cols, rows;
	Cell     *cell;
	CellRef  *a, *b;
	gboolean flag = FALSE;

	range_init_value (&s->range, (Value *) range);

	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;
	s->changing_cells = g_new (Value *, rows * cols);

	for (i = s->range.start.row; i <= s->range.end.row; i++)
		for (j = s->range.start.col; j <= s->range.end.col; j++) {
			cell = sheet_cell_fetch (sheet, j, i);
			flag |= cell_has_expr (cell);
			s->changing_cells [j-s->range.start.col + 
					   (i-s->range.start.row) * cols] = 
				value_duplicate (cell->value);
		}

	return flag;
}

gboolean
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
	gboolean   res;

	scenario = scenario_new (name, comment);

	res = collect_values (dao->sheet, scenario,
			      (ValueRange *) changing_cells);

	scenario->cell_sel_str = g_strdup (cell_sel_str);
	dao->sheet->scenarios = g_list_append (dao->sheet->scenarios, 
					       (gpointer) scenario);

	sheet_redraw_all (dao->sheet, TRUE);

	return res;
}

/* Scenario: Duplicate sheet ***********************************************/

static scenario_t *
scenario_copy (scenario_t *s, Sheet *new_sheet)
{
	scenario_t *p;
	int        i, j, cols, rows;
	
	p = g_new (scenario_t, 1);
	
	p->name         = g_strdup (s->name);
	p->comment      = g_strdup (s->comment);
	/* FIXME: Sheet name change */
	p->cell_sel_str = g_strdup (s->cell_sel_str);
	range_init (&p->range, s->range.start.col, s->range.start.row,
		    s->range.end.col, s->range.end.row);

	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;

	p->changing_cells = g_new (Value *, rows * cols);
	for (i = s->range.start.row; i <= s->range.end.row; i++)
		for (j = s->range.start.col; j <= s->range.end.col; j++)
			p->changing_cells [j-s->range.start.col +
					   (i-s->range.start.row) * cols] = 
				value_duplicate (s->changing_cells
						 [j-s->range.start.col +
						  (i-s->range.start.row) *
						  cols]);

	return p;
}

GList *
scenario_copy_all (GList *list, Sheet *ns)
{
	GList *cpy = NULL;

	while (list != NULL) {
		cpy = g_list_append (cpy, scenario_copy (list->data, ns));
		list = list->next;
	}

	return cpy;
}

/* Scenario: Remove sheet *************************************************/

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
			value_release
				(s->changing_cells [j-s->range.start.col
						    + (i-s->range.start.row) *
						    cols]);

	g_free (s->changing_cells);
	g_free (s);
}

static void
cb_free (scenario_t *data, gpointer ignore)
{
	scenario_free (data);
}

/*
 * Frees all scenarios in a list.
 */
void
scenario_free_all (GList *list)
{
	g_list_foreach (list, (GFunc) cb_free, NULL);
	g_list_free (list);
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

/* Scenario: Insert columns(s)/row(s) *************************************/

static void
insert_cols (scenario_t *s, int col, int count) 
{
	if (s->range.start.col >= col) {
		s->range.start.col += count;
		s->range.end.col += count;
		g_free (s->cell_sel_str);

		/* Scenarios do not allow cross sheet references. */
		s->cell_sel_str = g_strdup (range_name (&s->range));
	}
}

void
scenario_insert_cols (GList *list, int col, int count)
{
	while (list != NULL) {
		insert_cols (list->data, col, count);
		list = list->next;
	}
}

static void
insert_rows (scenario_t *s, int row, int count) 
{
	if (s->range.start.row >= row) {
		s->range.start.row += count;
		s->range.end.row += count;
		g_free (s->cell_sel_str);

		/* Scenarios do not allow cross sheet references. */
		s->cell_sel_str = g_strdup (range_name (&s->range));
	}
}

void
scenario_insert_rows (GList *list, int row, int count)
{
	while (list != NULL) {
		insert_rows (list->data, row, count);
		list = list->next;
	}
}

/* Scenario: Delete columns(s)/row(s) *************************************/

static void
delete_cols (scenario_t *s, int col, int count) 
{
	if (s->range.start.col >= col) {
		s->range.start.col -= count;
		s->range.end.col -= count;
		g_free (s->cell_sel_str);

		/* Scenarios do not allow cross sheet references. */
		s->cell_sel_str = g_strdup (range_name (&s->range));
	}
}

void
scenario_delete_cols (GList *list, int col, int count)
{
	while (list != NULL) {
		delete_cols (list->data, col, count);
		list = list->next;
	}
}

static void
delete_rows (scenario_t *s, int row, int count) 
{
	if (s->range.start.row >= row) {
		s->range.start.row -= count;
		s->range.end.row -= count;
		g_free (s->cell_sel_str);

		/* Scenarios do not allow cross sheet references. */
		s->cell_sel_str = g_strdup (range_name (&s->range));
	}
}

void
scenario_delete_rows (GList *list, int row, int count)
{
	while (list != NULL) {
		delete_rows (list->data, row, count);
		list = list->next;
	}
}

/* Scenario: Ok button pressed ********************************************/

void
scenarios_ok (WorkbookControl        *wbc,
	      data_analysis_output_t *dao)
{
	sheet_redraw_all (dao->sheet, TRUE);
}

/* Scenario: Create summary report ***************************************/

static void
rm_fun_cb (gpointer key, gpointer value, gpointer user_data)
{
	g_free (value);
}


void
scenario_summary (WorkbookControl        *wbc,
		  Sheet                  *sheet)
{
	data_analysis_output_t dao;
	GList                  *cur;
	GList                  *scenarios = sheet->scenarios;
	GHashTable             *names;  /* A hash table for cell names->row. */
	int                    i, j, col, row, cols;

	/* Initialize: Currently only new sheet output supported. */
	dao_init (&dao, NewSheetOutput);
	dao_prepare_output (wbc, &dao, _("Scenario Summary"));

	/* Titles. */
	dao_set_cell (&dao, 1, 1, _("Current Values"));
	dao_set_cell (&dao, 0, 2, _("Changing Cells"));

	/* Go through all scenarios. */
	row   = 0;
	names = g_hash_table_new (g_str_hash, g_str_equal);
	for (col = 0, cur = scenarios; cur != NULL; col++, cur = cur->next) {
		scenario_t *s = (scenario_t *) cur->data;

		/* Scenario name. */
		dao_set_cell (&dao, 2 + col, 1, s->name);

		/* Go through the changing cells. */
		cols = s->range.end.col - s->range.start.col + 1;
		for (i = s->range.start.row; i <= s->range.end.row; i++)
			for (j = s->range.start.col; j <= s->range.end.col;
			     j++) {
				char  *tmp = dao_find_name (sheet, j, i);
				Value *v;
				int   *index;

				v = value_duplicate (s->changing_cells
						     [j-s->range.start.col +
						      (i-s->range.start.row) *
						      cols]);

				/* Check if some of the previous scenarios
				 * already included that cell. If so, it's
				 * row will be put into *index. */
				index = g_hash_table_lookup (names, tmp);
				if (index != NULL)
					dao_set_cell_value (&dao, 2 + col,
							    3 + *index, v);
				else {
					/* New cell. */
					Cell *cell;
					int  *r;

					/* Changing cell name. */
					dao_set_cell (&dao, 0, 3 + row, tmp);

					/* Value of the cell in this
					 * scenario. */
					dao_set_cell_value (&dao, 2 + col,
							    3 + row, v);

					/* Current value of the cell. */
					cell = sheet_cell_fetch (sheet, j, i);
					v    = value_duplicate (cell->value);
					dao_set_cell_value (&dao, 1, 3 + row,
							    v);

					/* Insert row number into the hash 
					 * table. */
					r  = g_new (int, 1);
					*r = row;
					g_hash_table_insert (names, tmp, r);

					/* Increment the nbr of rows. */
					row++;
				}
			}
	}

	/* Destroy the hash table. */
	g_hash_table_foreach (names, (GHFunc) rm_fun_cb, NULL);
	g_hash_table_destroy (names);

	/* Clean up the report output. */
	dao_set_bold (&dao, 0, 0, 0, 2 + row);
	dao_autofit_columns (&dao);
	dao_set_cell (&dao, 0, 0, _("Scenario Summary"));
}
