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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
#include <selection.h>
#include <criteria.h>

#include <tools/filter.h>
#include <tools/analysis-tools.h>

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
		for (i=input_row_b; i<=input_row_e; i++) {
			ColRowInfo *ri = sheet_row_fetch (sheet, i);
			ri->in_advanced_filter = TRUE;
		}
		while (rows != NULL) {
			gint row = GPOINTER_TO_INT (rows->data);
			colrow_set_visibility (sheet, FALSE, TRUE, row, row);
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
			gint row = GPOINTER_TO_INT (rows->data);
			for (i=input_col_b; i<=input_col_e; i++) {
				cell = sheet_cell_get (sheet, i, row);
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
	GnmRange r, s;
	SheetView *sv;
	Sheet *sheet = criteria->v_range.cell.a.sheet;

	/* I don't like this -- minimal fix for now.  509427.  */
	if (!VALUE_IS_CELLRANGE (criteria))
		return analysis_tools_invalid_field;

	crit = parse_database_criteria (
		eval_pos_init_sheet (&ep, wb_control_cur_sheet (wbc)),
		database, criteria);

	if (crit == NULL)
		return analysis_tools_invalid_field;

	rows = find_rows_that_match (sheet,
				     database->v_range.cell.a.col,
				     database->v_range.cell.a.row + 1,
				     database->v_range.cell.b.col,
				     database->v_range.cell.b.row,
				     crit, unique_only_flag);

	free_criterias (crit);

	if (rows == NULL)
		return analysis_tools_no_records_found;

	dao_prepare_output (wbc, dao, _("Filtered"));

	filter (dao, sheet, rows,
		database->v_range.cell.a.col,
		database->v_range.cell.b.col, database->v_range.cell.a.row,
		database->v_range.cell.b.row);

	sv = sheet_get_view (sheet, wb_control_view (wbc));
	s = r = *(selection_first_range (sv, NULL, NULL));
	r.end.row = r.start.row;
	sv_selection_reset (sv);
	sv_selection_add_range (sv, &r);
	sv_selection_add_range (sv, &s);

	wb_control_menu_state_update (wbc, MS_FILTER_STATE_CHANGED);

	return analysis_tools_noerr;
}

static gboolean
cb_show_all (GnmColRowIter const *iter, Sheet *sheet)
{
	if (iter->cri->in_advanced_filter) {
		ColRowInfo *ri = sheet_row_fetch (sheet, iter->pos);
		if (!iter->cri->visible)
			colrow_set_visibility (sheet, FALSE, TRUE,
					       iter->pos, iter->pos);
		ri->in_advanced_filter = FALSE;
	}
	return FALSE;
}

void
filter_show_all (WorkbookControl *wbc)
{
	Sheet *sheet = wb_control_cur_sheet (wbc);

	/* FIXME: This is slow. We should probably have a linked list
	 * containing the filtered rows in the sheet structure. */
	sheet_colrow_foreach (sheet, FALSE, 0, -1,
			(ColRowHandler) cb_show_all, sheet);
	sheet->has_filtered_rows = FALSE;
	sheet_redraw_all (sheet, TRUE);

	wb_control_menu_state_update (wbc, MS_FILTER_STATE_CHANGED);
}

static gboolean
analysis_tool_advanced_filter_engine_run (data_analysis_output_t *dao,
					  analysis_tools_data_advanced_filter_t *info)
{
	GnmRange range;
	char *name;
	GnmValue  *database = info->base.range_1;
	GnmValue  *criteria = info->base.range_2;
	gint err = analysis_tools_noerr;
        GSList  *crit, *rows;
	GnmEvalPos ep;

	dao_set_italic (dao, 0, 0, 0, 2);
	set_cell_text_col (dao, 0, 0, _("/Advanced Filter:"
					"/Source Range:"
					"/Criteria Range:"));
	range_init_value (&range, database);
	name = global_range_name (database->v_range.cell.a.sheet, &range);
	dao_set_cell (dao, 1, 1, name);
	g_free (name);
	range_init_value (&range, criteria);
	name = global_range_name (criteria->v_range.cell.a.sheet, &range);
	dao_set_cell (dao, 1, 2, name);
	g_free (name);

	dao->offset_row = 3;

	crit = parse_database_criteria (
		eval_pos_init_sheet (&ep, wb_control_cur_sheet (info->base.wbc)),
		database, criteria);

	if (crit == NULL) {
		err = analysis_tools_invalid_field;
		goto finish;
	}

	rows = find_rows_that_match (database->v_range.cell.a.sheet,
				     database->v_range.cell.a.col,
				     database->v_range.cell.a.row + 1,
				     database->v_range.cell.b.col,
				     database->v_range.cell.b.row,
				     crit, info->unique_only_flag);

	free_criterias (crit);

	if (rows == NULL) {
		err = analysis_tools_no_records_found;
		goto finish;
	}

	filter (dao, database->v_range.cell.a.sheet, rows,
		database->v_range.cell.a.col,
		database->v_range.cell.b.col, database->v_range.cell.a.row,
		database->v_range.cell.b.row);

finish:
	if (err != analysis_tools_noerr) {
		dao_set_merge (dao, 0,0, 1, 0);
		if (err == analysis_tools_no_records_found)
			dao_set_cell (dao, 0, 0, _("No matching records were found."));
		else if (err == analysis_tools_invalid_field)
			dao_set_cell (dao, 0, 0, _("The given criteria are invalid."));
		else
			dao_set_cell_printf (dao, 0, 0,
					     _("An unexpected error has occurred: "
					       "%d."), err);
	}

	dao_redraw_respan (dao);

	return analysis_tools_noerr;
}


gboolean
analysis_tool_advanced_filter_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_advanced_filter_t *info = specs;
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Advanced Filter (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO: {
		int rows, cols;
		rows = info->base.range_1->v_range.cell.b.row
			- info->base.range_1->v_range.cell.a.row + 1;
		cols = info->base.range_1->v_range.cell.b.col
			- info->base.range_1->v_range.cell.a.col + 1;
		if (cols < 2)
			cols = 2;
		dao_adjust (dao, cols, 3 + rows);
		return FALSE;
	}
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_b_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Advanced Filter"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Advanced Filter"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_advanced_filter_engine_run (dao, info);
	}
	return TRUE;  /* We shouldn't get here */
}


