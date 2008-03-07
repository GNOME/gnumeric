/*
 * tabulate.c:
 *
 * Authors:
 *   COPYRIGHT (C) 2003 Morten Welinder (terra@gnome.org)
 *   COPYRIGHT (C) 2003 Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
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
#include "gnumeric.h"
#include "tabulate.h"

#include "tools.h"
#include "command-context.h"

#include <workbook-control.h>
#include "ranges.h"
#include "value.h"
#include "sheet.h"
#include "mstyle.h"
#include "workbook.h"
#include "cell.h"
#include "commands.h"
#include "gnm-format.h"
#include "number-match.h"
#include "mstyle.h"
#include "style-border.h"
#include "sheet-style.h"
#include "style-color.h"
#include "mathfunc.h"


static GnmValue *
tabulation_eval (Workbook *wb, int dims, gnm_float const *x,
		 GnmCell **xcells, GnmCell *ycell)
{
	int i;

	for (i = 0; i < dims; i++) {
		gnm_cell_set_value (xcells[i], value_new_float (x[i]));
		cell_queue_recalc (xcells[i]);
	}
	workbook_recalc (wb);

	return ycell->value
		? value_dup (ycell->value)
		: value_new_error_VALUE (NULL);
}

static GOFormat const *
my_get_format (GnmCell const *cell)
{
	GOFormat const *format = gnm_style_get_format (gnm_cell_get_style (cell));

	if (go_format_is_general (format) &&
	    cell->value != NULL && VALUE_FMT (cell->value) != NULL)
		return VALUE_FMT (cell->value);
	return format;
}

GSList *
do_tabulation (WorkbookControl *wbc,
	       GnmTabulateInfo *data)
{
	Workbook *wb = wb_control_get_workbook (wbc);
	GSList *sheet_idx = NULL;
	Sheet *sheet = NULL;
	gboolean sheetdim = (!data->with_coordinates && data->dims >= 3);
	GOFormat const *targetformat = my_get_format (data->target);
	int row = 0;

	gnm_float *values = g_new (gnm_float, data->dims);
	int *index = g_new (int, data->dims);
	int *counts = g_new (int, data->dims);
	Sheet **sheets = NULL;
	GOFormat const **formats = g_new (GOFormat const *, data->dims);

	{
		int i;
		for (i = 0; i < data->dims; i++) {
			values[i] = data->minima[i];
			index[i] = 0;
			formats[i] = my_get_format (data->cells[i]);

			counts[i] = 1 + gnm_fake_floor ((data->maxima[i] - data->minima[i]) / data->steps[i]);
			/* Silently truncate at the edges.  */
			if (!data->with_coordinates && i == 0 && counts[i] > SHEET_MAX_COLS - 1) {
				counts[i] = SHEET_MAX_COLS - 1;
			} else if (!data->with_coordinates && i == 1 && counts[i] > SHEET_MAX_ROWS - 1) {
				counts[i] = SHEET_MAX_ROWS - 1;
			}
		}
	}

	if (sheetdim) {
		int dim = 2;
		gnm_float val = data->minima[dim];
		GOFormat const *sf = my_get_format (data->cells[dim]);
		int i;

		sheets = g_new (Sheet *, counts[dim]);
		for (i = 0; i < counts[dim]; i++) {
			GnmValue *v = value_new_float (val);
			char *base_name = format_value (sf, v, NULL, -1,
						workbook_date_conv (wb));
			char *unique_name =
				workbook_sheet_get_free_name (wb,
							      base_name,
							      FALSE, FALSE);

			g_free (base_name);
			value_release (v);
			sheet = sheets[i] = sheet_new (wb, unique_name);
			g_free (unique_name);
			workbook_sheet_attach (wb, sheet);
			sheet_idx = g_slist_prepend (sheet_idx, 
						     GINT_TO_POINTER (sheet->index_in_wb));

			val += data->steps[dim];
		}
	} else {
		char *unique_name =
			workbook_sheet_get_free_name (wb,
						      _("Tabulation"),
						      FALSE, FALSE);
	        sheet = sheet_new (wb, unique_name);
		g_free (unique_name);
		workbook_sheet_attach (wb, sheet);
		sheet_idx = g_slist_prepend (sheet_idx, 
					     GINT_TO_POINTER (sheet->index_in_wb));
	}

	while (1) {
		GnmValue *v;
		GnmCell *cell;
		int dim;

		if (data->with_coordinates) {
			int i;

			for (i = 0; i < data->dims; i++) {
				GnmValue *v = value_new_float (values[i]);
				value_set_fmt (v, formats[i]);
				sheet_cell_set_value (
					sheet_cell_fetch (sheet, i, row), v);
			}

			cell = sheet_cell_fetch (sheet, data->dims, row);
		} else {
			Sheet *thissheet = sheetdim ? sheets[index[2]] : sheet;
			int row = (data->dims >= 1 ? index[0] + 1 : 1);
			int col = (data->dims >= 2 ? index[1] + 1 : 1);

			/* Fill-in top header.  */
			if (row == 1 && data->dims >= 2) {
				GnmValue *v = value_new_float (values[1]);
				value_set_fmt (v, formats[1]);
				sheet_cell_set_value (
					sheet_cell_fetch (thissheet, col, 0), v);
			}

			/* Fill-in left header.  */
			if (col == 1 && data->dims >= 1) {
				GnmValue *v = value_new_float (values[0]);
				value_set_fmt (v, formats[0]);
				sheet_cell_set_value (
					sheet_cell_fetch (thissheet, 0, row), v);
			}

			/* Make a horizon line on top between header and table.  */
			if (row == 1 && col == 1) {
				GnmStyle *mstyle = gnm_style_new ();
				GnmRange range;
				GnmBorder *border;

				range.start.col = 0;
				range.start.row = 0;
				range.end.col   = (data->dims >= 2 ? 
						   counts[1] : 1);
				range.end.row   = 0;

				border = gnm_style_border_fetch (GNM_STYLE_BORDER_MEDIUM,
							     style_color_black (),
							     GNM_STYLE_BORDER_HORIZONTAL);

				gnm_style_set_border (mstyle, MSTYLE_BORDER_BOTTOM, border);
				sheet_style_apply_range (thissheet, &range, mstyle);
			}

			/* Make a vertical line on left between header and table.  */
			if (row == 1 && col == 1) {
				GnmStyle *mstyle = gnm_style_new ();
				GnmRange range;
				GnmBorder *border;

				range.start.col = 0;
				range.start.row = 0;
				range.end.col   = 0;
				range.end.row   = counts[0];;

				border = gnm_style_border_fetch (GNM_STYLE_BORDER_MEDIUM,
							     style_color_black (),
							     GNM_STYLE_BORDER_VERTICAL);

				gnm_style_set_border (mstyle, MSTYLE_BORDER_RIGHT, border);
				sheet_style_apply_range (thissheet, &range, mstyle);
			}

			cell = sheet_cell_fetch (thissheet, col, row);
		}

		v = tabulation_eval (wb, data->dims, values, data->cells, data->target);
		value_set_fmt (v, targetformat);
		sheet_cell_set_value (cell, v);

		if (data->with_coordinates) {
			row++;
			if (row >= SHEET_MAX_ROWS)
				break;
		}

		for (dim = data->dims - 1; dim >= 0; dim--) {
			values[dim] += data->steps[dim];
			index[dim]++;

			if (index[dim] == counts[dim]) {
				index[dim] = 0;
				values[dim] = data->minima[dim];
			} else
				break;
		}

		if (dim < 0)
			break;
	}

	g_free (values);
	g_free (index);
	g_free (counts);
	g_free (sheets);
	g_free (formats);

	return sheet_idx;
}
