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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <tools/tabulate.h>

#include <command-context.h>
#include <workbook-control.h>
#include <ranges.h>
#include <value.h>
#include <sheet.h>
#include <mstyle.h>
#include <workbook.h>
#include <cell.h>
#include <commands.h>
#include <gnm-format.h>
#include <number-match.h>
#include <style-border.h>
#include <sheet-style.h>
#include <style-color.h>
#include <mathfunc.h>
#include <application.h>

typedef struct {
	GObjectClass parent_class;
} GnmTabulateClass;

static GnmValue *
tabulation_eval (int dims, gnm_float const *x,
		 GnmCell **xcells, GnmCell *ycell)
{
	int i;

	for (i = 0; i < dims; i++) {
		gnm_cell_set_value (xcells[i], value_new_float (x[i]));
		gnm_cell_queue_recalc (xcells[i]);
	}

	gnm_cell_eval (ycell);
	return ycell->value
		? value_dup (ycell->value)
		: value_new_error_VALUE (NULL);
}

/**
 * gnm_tabulate:
 * @tab: (transfer none): tabulation object
 * @wbc: control
 *
 * Returns: (transfer full) (element-type int):
 */
GSList *
gnm_tabulate (GnmTabulate *tab, WorkbookControl *wbc)
{
	Workbook *wb = wb_control_get_workbook (wbc);
	Sheet *old_sheet = wb_control_cur_sheet (wbc);
	GSList *sheet_idx = NULL;
	Sheet *sheet = NULL;
	gboolean sheetdim = (!tab->with_coordinates && tab->dims >= 3);
	GOFormat const *targetformat = gnm_cell_get_format (tab->target);
	int row = 0;

	gnm_float *values = g_new (gnm_float, tab->dims);
	int *index = g_new (int, tab->dims);
	int *counts = g_new (int, tab->dims);
	Sheet **sheets = NULL;
	GOFormat const **formats = g_new (GOFormat const *, tab->dims);
	GnmValue **old_values = g_new (GnmValue *, tab->dims);

	/* No real reason to limit to this. */
	int cols = gnm_sheet_get_max_cols (old_sheet);
	int rows = gnm_sheet_get_max_rows (old_sheet);

	{
		int i;
		for (i = 0; i < tab->dims; i++) {
			int max;
			gnm_float full_count;

			values[i] = tab->minima[i];
			index[i] = 0;
			formats[i] = gnm_cell_get_format (tab->cells[i]);
			old_values[i] = value_dup (tab->cells[i]->value);

			/* Silently truncate at the edges.  */
			full_count = 1 + gnm_fake_floor ((tab->maxima[i] - tab->minima[i]) / tab->steps[i]);
			if (tab->with_coordinates) {
				max = rows;
			} else {
				switch (i) {
				case 0: max = rows - 1; break;
				case 1: max = cols - 1; break;
				default: max = 64 * 1024; break;  /* Large number of sheets.  */
				}
			}
			counts[i] = (int)CLAMP (full_count, 0, max);
		}
	}

	if (sheetdim) {
		int dim = 2;
		gnm_float val = tab->minima[dim];
		GOFormat const *sf = gnm_cell_get_format (tab->cells[dim]);
		int i;

		sheets = g_new (Sheet *, counts[dim]);
		for (i = 0; i < counts[dim]; i++) {
			GnmValue *v = value_new_float (val);
			char *base_name = format_value (sf, v, -1,
						workbook_date_conv (wb));
			char *unique_name =
				workbook_sheet_get_free_name (wb,
							      base_name,
							      FALSE, FALSE);

			g_free (base_name);
			value_release (v);
			sheet = sheets[i] = sheet_new (wb, unique_name, cols, rows);
			g_free (unique_name);
			workbook_sheet_attach (wb, sheet);
			sheet_idx = g_slist_prepend (sheet_idx,
						     GINT_TO_POINTER (sheet->index_in_wb));

			val += tab->steps[dim];
		}
	} else {
		char *unique_name =
			workbook_sheet_get_free_name (wb,
						      _("Tabulation"),
						      FALSE, FALSE);
	        sheet = sheet_new (wb, unique_name, cols, rows);
		g_free (unique_name);
		workbook_sheet_attach (wb, sheet);
		sheet_idx = g_slist_prepend (sheet_idx,
					     GINT_TO_POINTER (sheet->index_in_wb));
	}

	while (1) {
		GnmValue *v;
		GnmCell *cell;
		int dim;

		if (tab->with_coordinates) {
			int i;

			for (i = 0; i < tab->dims; i++) {
				GnmValue *v = value_new_float (values[i]);
				value_set_fmt (v, formats[i]);
				sheet_cell_set_value (
					sheet_cell_fetch (sheet, i, row), v);
			}

			cell = sheet_cell_fetch (sheet, tab->dims, row);
		} else {
			Sheet *thissheet = sheetdim ? sheets[index[2]] : sheet;
			int row = (tab->dims >= 1 ? index[0] + 1 : 1);
			int col = (tab->dims >= 2 ? index[1] + 1 : 1);

			/* Fill-in top header.  */
			if (row == 1 && tab->dims >= 2) {
				GnmValue *v = value_new_float (values[1]);
				value_set_fmt (v, formats[1]);
				sheet_cell_set_value (
					sheet_cell_fetch (thissheet, col, 0), v);
			}

			/* Fill-in left header.  */
			if (col == 1 && tab->dims >= 1) {
				GnmValue *v = value_new_float (values[0]);
				value_set_fmt (v, formats[0]);
				sheet_cell_set_value (
					sheet_cell_fetch (thissheet, 0, row), v);
			}

			/* Make a horizontal line on top between header and table.  */
			if (row == 1 && col == 1) {
				GnmStyle *mstyle = gnm_style_new ();
				GnmRange range;
				GnmBorder *border;

				range.start.col = 0;
				range.start.row = 0;
				range.end.col   = (tab->dims >= 2 ?
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
				range.end.row   = counts[0];

				border = gnm_style_border_fetch (GNM_STYLE_BORDER_MEDIUM,
							     style_color_black (),
							     GNM_STYLE_BORDER_VERTICAL);

				gnm_style_set_border (mstyle, MSTYLE_BORDER_RIGHT, border);
				sheet_style_apply_range (thissheet, &range, mstyle);
			}

			cell = sheet_cell_fetch (thissheet, col, row);
		}

		v = tabulation_eval (tab->dims, values, tab->cells, tab->target);
		value_set_fmt (v, targetformat);
		sheet_cell_set_value (cell, v);

		if (tab->with_coordinates) {
			row++;
			if (row >= gnm_sheet_get_max_rows (sheet))
				break;
		}

		for (dim = tab->dims - 1; dim >= 0; dim--) {
			values[dim] += tab->steps[dim];
			index[dim]++;

			if (index[dim] == counts[dim]) {
				index[dim] = 0;
				values[dim] = tab->minima[dim];
			} else
				break;
		}

		if (dim < 0)
			break;
	}

	{
		int i;
		for (i = 0; i < tab->dims; i++) {
			gnm_cell_set_value (tab->cells[i], old_values[i]);
			gnm_cell_queue_recalc (tab->cells[i]);
		}
		gnm_cell_eval (tab->target);
		gnm_app_recalc ();
	}

	g_free (values);
	g_free (index);
	g_free (counts);
	g_free (sheets);
	g_free (formats);
	g_free (old_values);

	return sheet_idx;
}

G_DEFINE_TYPE (GnmTabulate, gnm_tabulate, G_TYPE_OBJECT)

static void
gnm_tabulate_finalize (GObject *obj)
{
	GnmTabulate *tab = GNM_TABULATE (obj);
	g_free (tab->cells);
	g_free (tab->minima);
	g_free (tab->maxima);
	g_free (tab->steps);
	G_OBJECT_CLASS (gnm_tabulate_parent_class)->finalize (obj);
}

static void
gnm_tabulate_class_init (GnmTabulateClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = gnm_tabulate_finalize;
}

static void
gnm_tabulate_init (GnmTabulate *tab)
{
}

/**
 * gnm_tabulate_new:
 * @dims: number of dimensions
 *
 * Returns: (transfer full): a new #GnmTabulate structure.
 */
GnmTabulate *
gnm_tabulate_new (int dims)
{
	GnmTabulate *tab = g_object_new (GNM_TABULATE_TYPE, NULL);
	tab->dims = dims;
	if (dims > 0) {
		tab->cells = g_new0 (GnmCell *, dims);
		tab->minima = g_new0 (gnm_float, dims);
		tab->maxima = g_new0 (gnm_float, dims);
		tab->steps = g_new0 (gnm_float, dims);
	}
	return tab;
}
