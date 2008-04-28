/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * filter.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2000, 2001, 2002 by Jukka-Pekka Iivonen <iivonen@iki.fi>
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

#include <sheet.h>
#include <sheet-filter.h>
#include <workbook-control.h>
#include <cell.h>
#include <ranges.h>
#include <value.h>

#include "filter.h"


static void
free_rows (GSList *row_list)
{
	GSList *list;

	for (list = row_list; list != NULL; list = list->next)
	        g_free (list->data);
	g_slist_free (row_list);
}


static void
filter (data_analysis_output_t *dao, Sheet *sheet, GSList *rows,
	gint input_col_b, gint input_col_e, gint input_row_b, gint input_row_e)
{
        GnmCell *cell;
	int  i, r=0;

	if (dao->type == InPlaceOutput) {
		sheet->has_filtered_rows = TRUE;
		colrow_set_visibility (sheet, FALSE,
				       FALSE, input_row_b+1, input_row_e);
		while (rows != NULL) {
			gint *row = (gint *) rows->data;
			colrow_set_visibility (sheet, FALSE, TRUE, *row, *row);
			rows = rows->next;
		}
		sheet_redraw_all (sheet, TRUE);
/* FIXME: what happens if we just have hidden the selection? */

	} else {
		for (i=input_col_b; i<=input_col_e; i++) {
			cell = sheet_cell_get (sheet, i, input_row_b);
			if (cell == NULL)
				dao_set_cell (dao, i - input_col_b, r, NULL);
			else {
				GnmValue *value = value_dup (cell->value);
				dao_set_cell_value (dao, i - input_col_b, r,
						    value);
			}
		}
		++r;

		while (rows != NULL) {
			gint *row = (gint *) rows->data;
			for (i=input_col_b; i<=input_col_e; i++) {
				cell = sheet_cell_get (sheet, i, *row);
				if (cell == NULL)
					dao_set_cell (dao, i - input_col_b, r,
						      NULL);
				else {
					GnmValue *value =
						value_dup (cell->value);
					dao_set_cell_value (dao,
							    i - input_col_b, r,
							    value);
				}
			}
			++r;
			rows = rows->next;
		}
	}
}

/*
 * Advanced Filter tool.
 */
gint
advanced_filter (WorkbookControl        *wbc,
		 data_analysis_output_t *dao,
		 GnmValue               *database,
		 GnmValue               *criteria,
		 gboolean               unique_only_flag)
{
        GSList  *crit, *rows;
	GnmEvalPos ep;

	/* I don't like this -- minimal fix for now.  509427.  */
	if (criteria->type != VALUE_CELLRANGE)
		return ERR_INVALID_FIELD;

	crit = parse_database_criteria (
		eval_pos_init_sheet (&ep, wb_control_cur_sheet (wbc)),
		database, criteria);

	if (crit == NULL)
		return ERR_INVALID_FIELD;

	rows = find_rows_that_match (database->v_range.cell.a.sheet,
				     database->v_range.cell.a.col,
				     database->v_range.cell.a.row + 1,
				     database->v_range.cell.b.col,
				     database->v_range.cell.b.row,
				     crit, unique_only_flag);

	free_criterias (crit);

	if (rows == NULL)
		return NO_RECORDS_FOUND;

	dao_prepare_output (wbc, dao, _("Filtered"));

	filter (dao, database->v_range.cell.a.sheet, rows,
		database->v_range.cell.a.col,
		database->v_range.cell.b.col, database->v_range.cell.a.row,
		database->v_range.cell.b.row);

	free_rows (rows);

	dao_autofit_columns (dao);

	return OK;
}

static gboolean 
cb_show_all (GnmColRowIter const *iter, Sheet *sheet)
{
	if (iter->cri->in_filter && !iter->cri->visible)
		colrow_set_visibility (sheet, FALSE, TRUE,
			iter->pos, iter->pos);
	return FALSE;
}

void
filter_show_all (Sheet *sheet)
{
	GSList *ptr = sheet->filters;
	GnmFilter *filter;
	unsigned i;

	for (; ptr != NULL ; ptr = ptr->next) {
		filter = ptr->data;
		for (i = filter->fields->len; i-- > 0 ;)
			gnm_filter_set_condition (filter, i, NULL, FALSE);
	}

	/* FIXME: This is slow. We should probably have a linked list
	 * containing the filtered rows in the sheet structure. */
	colrow_foreach (&sheet->rows, 0, gnm_sheet_get_max_rows (sheet),
			(ColRowHandler) cb_show_all, sheet);
	sheet->has_filtered_rows = FALSE;
	sheet_redraw_all (sheet, TRUE);
}
