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
#include "style-color.h"
#include "parse-util.h"

/* Generic stuff **********************************************************/

scenario_t *
scenario_by_name (GList *scenarios, const gchar *name, gboolean *all_deleted)
{
	scenario_t *s, *res = NULL;
 
	if (all_deleted)
		*all_deleted = TRUE;

	while (scenarios != NULL) {
		s = (scenario_t *) scenarios->data;

		if (strcmp (s->name, name) == 0)
			res = s;
		else if (all_deleted)
			*all_deleted &= s->marked_deleted;

		scenarios = scenarios->next;
	}

	return res;
}

typedef Value * (*ScenarioValueCB) (int col, int row, Value *v, gpointer data);

static void
scenario_for_each_value (scenario_t *s, ScenarioValueCB fn, gpointer data)
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

/* Scenario: Add ***********************************************************/

static scenario_t *
scenario_new (Sheet *sheet, const gchar *name, const gchar *comment)
{
	scenario_t *s;
	GList      *scenarios = sheet->scenarios;

	s = g_new (scenario_t, 1);
	s->sheet = sheet;

	/* Check if a scenario having the same name already exists. */
	if (scenario_by_name (scenarios, name, NULL)) {
		GString *str;
		gchar   *tmp;
		int     i, j, len;

		len = strlen (name);
		if (len > 1 && name [len - 1] == ']') {
			for (i = len - 2; i > 0; i--) {
				if (! g_ascii_isdigit (name [i]))
					break;
			}

			tmp = g_strdup (name);
			if (i > 0 && name [i] == '[')
				tmp [i] = '\0';
		} else
			tmp = g_strdup (name);

		for (j = 1; j < 10000; j++) {
			str = g_string_new (NULL);
			g_string_append_printf (str, "%s [%d]",	tmp, j);
			if (scenario_by_name (scenarios, str->str, NULL) ==
			    NULL)
				break;
			g_string_free (str, FALSE);
		}
		s->name = g_strdup (str->str);
		g_free (tmp);
		g_string_free (str, FALSE);
	} else
		s->name           = g_strdup (name);

	s->comment        = g_strdup (comment);
	s->changing_cells = NULL;
	s->cell_sel_str   = NULL;
	s->marked_deleted = FALSE;

	return s;
}


typedef struct {
	gboolean expr_flag;
	Sheet    *sheet;
} collect_cb_t;

static Value *
collect_cb (int col, int row, Value *v, collect_cb_t *p)
{
	Cell *cell = sheet_cell_fetch (p->sheet, col, row);

	p->expr_flag |= cell_has_expr (cell);

	return value_duplicate (cell->value);
}

/*
 * Collects cell values of a scenario. If any of the cells contain an
 * expression return TRUE so that the user can be warned about it.
 */
static gboolean
collect_values (Sheet *sheet, scenario_t *s, ValueRange *range)
{
	int          i, j;
	int          cols, rows;
	Cell         *cell;
	CellRef      *a, *b;
	collect_cb_t cb;

	range_init_value (&s->range, (Value *) range);

	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;
	s->changing_cells = g_new (Value *, rows * cols);

	cb.expr_flag = FALSE;
	cb.sheet     = sheet;
	scenario_for_each_value (s, (ScenarioValueCB) collect_cb, &cb);

	return cb.expr_flag;
}

/* Doesn't actually add the new scenario into the sheet's scenario list. */
gboolean
scenario_add_new (gchar *name,
		  Value *changing_cells,
		  gchar *cell_sel_str,
		  gchar *comment,
		  Sheet *sheet,
		  scenario_t **new_scenario)
{
	scenario_t *scenario;
	int        i, j;
	int        cols, rows;
	gboolean   res;

	scenario = scenario_new (sheet, name, comment);

	res = collect_values (sheet, scenario, (ValueRange *) changing_cells);

	scenario->cell_sel_str = g_strdup (cell_sel_str);
	sheet_redraw_all (sheet, TRUE);

	*new_scenario = scenario;
	return res;
}

void
scenario_add (Sheet *sheet, scenario_t *scenario)
{
	sheet->scenarios = g_list_append (sheet->scenarios, scenario);
}

/* Scenario: Duplicate sheet ***********************************************/

typedef struct {
	int        rows;
	int        cols;
	int        col_offset;
	int        row_offset;
	scenario_t *dest;
} copy_cb_t;

static Value *
copy_cb (int col, int row, Value *v, copy_cb_t *p)
{
	p->dest->changing_cells [col - p->col_offset +
				 (row - p->row_offset) * p->cols] = 
		value_duplicate (v);

	return v;
}

scenario_t *
scenario_copy (scenario_t *s, Sheet *new_sheet)
{
	scenario_t *p;
	copy_cb_t  cb;

	p = g_new (scenario_t, 1);
	
	p->name         = g_strdup (s->name);
	p->comment      = g_strdup (s->comment);
	/* FIXME: Sheet name change */
	p->cell_sel_str = g_strdup (s->cell_sel_str);
	range_init (&p->range, s->range.start.col, s->range.start.row,
		    s->range.end.col, s->range.end.row);

	cb.rows       = s->range.end.row - s->range.start.row + 1;
	cb.cols       = s->range.end.col - s->range.start.col + 1;
	cb.col_offset = s->range.start.col;
	cb.row_offset = s->range.start.row;
	cb.dest       = p;

	p->changing_cells = g_new (Value *, cb.rows * cb.cols);
	scenario_for_each_value (s, (ScenarioValueCB) copy_cb, &cb);

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

static Value *
cb_value_free (int col, int row, Value *v, gpointer data)
{
	value_release (v);

	return NULL;
}

void
scenario_free (scenario_t *s)
{
	g_free (s->name);
	g_free (s->comment);
	g_free (s->cell_sel_str);

	scenario_for_each_value (s, cb_value_free, NULL);

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

gboolean
scenario_mark_deleted (GList *scenarios, gchar *name)
{
	scenario_t *s;
	gboolean   all_deleted;

	s = scenario_by_name (scenarios, name, &all_deleted);
	s->marked_deleted = TRUE;

	return all_deleted;
}

GList *
scenario_delete (GList *scenarios, gchar *name)
{
	scenario_t *s;
	GList      *list;

	s = scenario_by_name (scenarios, name, NULL);
	list = g_list_remove (scenarios, s);
	scenario_free (s);

	return list;
}

/* Scenario: Show **********************************************************/


static Value *
show_cb (int col, int row, Value *v, data_analysis_output_t *dao)
{
	dao_set_cell_value (dao, col, row, value_duplicate (v));

	return v;
}

scenario_t *
scenario_show (WorkbookControl        *wbc,
	       scenario_t             *s,
	       scenario_t             *old_values,
	       data_analysis_output_t *dao)
{
	scenario_t   *stored_values;
	int           rows, cols;
	collect_cb_t  cb;

	/* Recover values of the previous show call. */
	if (old_values) {
		scenario_for_each_value (old_values, (ScenarioValueCB) show_cb,
					 dao);
		scenario_free (old_values);
	}

	if (s == NULL)
		return;

	/* Store values for recovery. */
	stored_values = scenario_new (dao->sheet, "", "");
	stored_values->range = s->range;
	rows = s->range.end.row - s->range.start.row + 1;
	cols = s->range.end.col - s->range.start.col + 1;
	stored_values->changing_cells = g_new (Value *, rows * cols);

	cb.sheet = dao->sheet;
	scenario_for_each_value (stored_values, (ScenarioValueCB) collect_cb,
				 &cb);

	/* Show scenario and recalculate. */
	scenario_for_each_value (s, (ScenarioValueCB) show_cb, dao);
	workbook_recalc (wb_control_workbook (wbc));
	sheet_redraw_all (dao->sheet, TRUE);

	return stored_values;
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

/* Scenario Manager: Ok/Cancel buttons************************************/

/* Ok button pressed. */
void
scenario_manager_ok (Sheet *sheet, scenario_t *old_values)
{
	GList *cur, *scenarios = sheet->scenarios;
	GList *list = NULL;

	/* Update scenarios (free the deleted ones). */
	for (cur = scenarios; cur != NULL; cur = cur->next) {
		scenario_t *s = (scenario_t *) cur->data;

		if (s->marked_deleted)
			scenario_free (s);
		else
			list = g_list_append (list, s);
	}
	g_list_free (scenarios);
	sheet->scenarios = list;

	if (old_values)
		scenario_free (old_values);
	sheet_redraw_all (sheet, TRUE);
}

/* Cancel button pressed. */
void
scenario_recover_all (GList *scenarios)
{
	while (scenarios) {
		scenario_t *s = (scenario_t *) scenarios->data;

		s->marked_deleted = FALSE;
		scenarios = scenarios->next;
	}
}

/* Scenario: Create summary report ***************************************/

static void
rm_fun_cb (gpointer key, gpointer value, gpointer user_data)
{
	g_free (value);
}

typedef struct {
	data_analysis_output_t dao;

	Sheet      *sheet;
	GHashTable *names;  /* A hash table for cell names->row. */
	int        col;
	int        row;
	Value      *results;
} summary_cb_t;

static Value *
summary_cb (int col, int row, Value *v, summary_cb_t *p)
{
	char  *tmp = dao_find_name (p->sheet, col, row);
	int   *index;

	/* Check if some of the previous scenarios already included that
	 * cell. If so, it's row will be put into *index. */
	index = g_hash_table_lookup (p->names, tmp);
	if (index != NULL)
		dao_set_cell_value (&p->dao, 2 + p->col, 3 + *index, 
				    value_duplicate (v));
	else {
		/* New cell. */
		Cell *cell;
		int  *r;
		
		/* Changing cell name. */
		dao_set_cell (&p->dao, 0, 3 + p->row, tmp);
		
		/* Value of the cell in this scenario. */
		dao_set_cell_value (&p->dao, 2 + p->col, 3 + p->row, 
				    value_duplicate (v));
		
		/* Current value of the cell. */
		cell = sheet_cell_fetch (p->sheet, col, row);
		dao_set_cell_value (&p->dao, 1, 3 + p->row,
				    value_duplicate (cell->value));
		
		/* Insert row number into the hash table. */
		r  = g_new (int, 1);
		*r = row;
		g_hash_table_insert (p->names, tmp, r);
		
		/* Increment the nbr of rows. */
		p->row++;
	}

	return v;
}

static void
scenario_summary_res_cells (WorkbookControl *wbc, Value *results,
			    summary_cb_t *cb)
{
	int        i, j, col, tmp_row = 4 + cb->row;
	Range      r;

	dao_set_cell (&cb->dao, 0, 3 + cb->row++, _("Result Cells:"));

	range_init_value (&r, (Value *) results);
	for (i = r.start.col; i <= r.end.col; i++)
		for (j = r.start.row; j <= r.end.row; j++) {
			scenario_t *ov = NULL;
			Cell       *cell;
			GList      *cur;
			
			cell = sheet_cell_fetch (cb->sheet, i, j);
			
			/* Names of the result cells. */
			dao_set_cell (&cb->dao, 0, 3 + cb->row,
				      cell_name (cell));
			
			/* Current value. */
			dao_set_cell_value (&cb->dao, 1, 3 + cb->row,
					    value_duplicate (cell->value));
			
			/* Evaluate and write the value of the cell
			 * with all different scenario values. */
			col = 2;
			for (cur = cb->sheet->scenarios; cur != NULL;
			     cur = cur->next) {
				scenario_t *s = (scenario_t *) cur->data;
				
				ov = scenario_show (wbc, s, ov, &cb->dao);
				
				cell = sheet_cell_fetch (cb->sheet, i, j);
				
				dao_set_cell_value (&cb->dao, col++,
						    3 + cb->row,
						    value_duplicate
						    (cell->value));
			}
			cb->row++;
			
			/* Use show to clean up 'ov'. */
			scenario_show (wbc, NULL, ov, &cb->dao);
			ov = NULL;
		}
	
	/* Set the alignment of names of result cells to be right. */
	dao_set_align (&cb->dao, 0, tmp_row, 0, 2 + cb->row,
		       HALIGN_RIGHT, VALIGN_BOTTOM);
}

void
scenario_summary (WorkbookControl *wbc,
		  Sheet           *sheet,
		  Value           *results,
		  Sheet           **new_sheet)
{
	summary_cb_t cb;
	GList        *cur;
	GList        *scenarios = sheet->scenarios;

	/* Initialize: Currently only new sheet output supported. */
	dao_init (&cb.dao, NewSheetOutput);
	dao_prepare_output (wbc, &cb.dao, _("Scenario Summary"));

	/* Titles. */
	dao_set_cell (&cb.dao, 1, 1, _("Current Values"));
	dao_set_cell (&cb.dao, 0, 2, _("Changing Cells:"));

	/* Go through all scenarios. */
	cb.row     = 0;
	cb.names   = g_hash_table_new (g_str_hash, g_str_equal);
	cb.sheet   = sheet;
	cb.results = results;
	for (cb.col = 0, cur = scenarios; cur != NULL; cb.col++,
		     cur = cur->next) {
		scenario_t *s = (scenario_t *) cur->data;

		/* Scenario name. */
		dao_set_cell (&cb.dao, 2 + cb.col, 1, s->name);

		scenario_for_each_value (s, (ScenarioValueCB) summary_cb, &cb);
	}

	/* Set the alignment of names of the changing cells to be right. */
	dao_set_align (&cb.dao, 0, 3, 0, 2 + cb.row, HALIGN_RIGHT, 
		       VALIGN_BOTTOM);

	/* Result cells. */
	if (results != NULL)
		scenario_summary_res_cells (wbc, results, &cb);

	/* Destroy the hash table. */
	g_hash_table_foreach (cb.names, (GHFunc) rm_fun_cb, NULL);
	g_hash_table_destroy (cb.names);

	/* Clean up the report output. */
	dao_set_bold (&cb.dao, 0, 0, 0, 2 + cb.row);
	dao_autofit_columns (&cb.dao);
	dao_set_cell (&cb.dao, 0, 0, _("Scenario Summary"));

	dao_set_colors (&cb.dao, 0, 0, cb.col + 1, 1,
			style_color_new (gs_white.red, gs_white.green, 
					 gs_white.blue),
			style_color_new (gs_dark_gray.red, gs_dark_gray.green,
					 gs_dark_gray.blue));
	dao_set_align (&cb.dao, 1, 1, cb.col + 1, 1, HALIGN_RIGHT, 
		       VALIGN_BOTTOM);

	*new_sheet = cb.dao.sheet;
}
