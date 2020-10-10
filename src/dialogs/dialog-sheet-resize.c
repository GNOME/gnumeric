/*
 * dialog-sheet-resize.c: Dialog to resize current or all sheets.
 *
 * Author:
 *	Morten Welinder <terra@gnome.org>
 *
 * (C) Copyright 2009 Morten Welinder <terra@gnome.org>
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
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <commands.h>

#define RESIZE_DIALOG_KEY "sheet-resize-dialog"

typedef struct {
	WBCGtk *wbcg;
	Sheet *sheet;
	GtkWidget *dialog;
	GtkWidget *columns_scale, *rows_scale;
	GtkWidget *columns_label, *rows_label;
	GtkWidget *ok_button, *cancel_button;
	GtkWidget *all_sheets_button;
} ResizeState;

static void
get_sizes (ResizeState *state, int *cols, int *rows)
{
	GtkAdjustment *adj;

	adj = gtk_range_get_adjustment (GTK_RANGE (state->columns_scale));
	*cols = 1 << (int)gtk_adjustment_get_value (adj);

	adj = gtk_range_get_adjustment (GTK_RANGE (state->rows_scale));
	*rows = 1 << (int)gtk_adjustment_get_value (adj);
}

static void
set_count (GtkWidget *l, int count)
{
	char *text;

	if (count >= (1 << 20))
		text = g_strdup_printf ("%dM", count >> 20);
	else
		text = g_strdup_printf ("%d", count);
	gtk_label_set_text (GTK_LABEL (l), text);
	g_free (text);
}

static void
cb_scale_changed (ResizeState *state)
{
	int cols, rows;
	get_sizes (state, &cols, &rows);
	set_count (state->columns_label, cols);
	set_count (state->rows_label, rows);
	gtk_widget_set_sensitive (state->ok_button,
				  gnm_sheet_valid_size (cols, rows));
}

static int
mylog2 (int N)
{
	int l2 = 0;
	while (N > 1)
		N >>= 1, l2++;
	return l2;
}

static void
init_scale (GtkWidget *scale, int N, int lo, int hi)
{
	GtkAdjustment *adj = gtk_range_get_adjustment (GTK_RANGE (scale));
	g_object_set (G_OBJECT (adj),
		      "lower", (double)mylog2 (lo),
		      "upper", (double)mylog2 (hi) + 1.,
		      NULL);
	gtk_adjustment_set_value (adj, mylog2 (N));
}

static void
cb_ok_clicked (ResizeState *state)
{
	GSList *changed_sheets = NULL;
	WorkbookControl *wbc;
	Workbook *wb;
	gboolean all_sheets;
	int cols, rows;

	get_sizes (state, &cols, &rows);
	all_sheets = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->all_sheets_button));

	wbc = GNM_WBC (state->wbcg);
	wb = wb_control_get_workbook (wbc);

	if (all_sheets) {
		GPtrArray *sheets = workbook_sheets (wb);
		unsigned ui;

		for (ui = 0; ui < sheets->len; ui++) {
			Sheet *this_sheet = g_ptr_array_index (sheets, ui);

			if (this_sheet == state->sheet)
				continue;

			if (cols == gnm_sheet_get_max_cols (this_sheet) &&
			    rows == gnm_sheet_get_max_rows (this_sheet))
				continue;

			changed_sheets = g_slist_prepend (changed_sheets, this_sheet);
		}
		g_ptr_array_unref (sheets);
	}

	if (changed_sheets ||
	    cols != gnm_sheet_get_max_cols (state->sheet) ||
	    rows != gnm_sheet_get_max_rows (state->sheet)) {
		/* We also append the sheet if it isn't changed in size */
		/* to ensure that the focus stays on the current sheet. */
		changed_sheets = g_slist_prepend (changed_sheets, state->sheet);
	}



	if (changed_sheets)
		cmd_resize_sheets (wbc, changed_sheets,
				   cols, rows);

	gtk_widget_destroy (state->dialog);
}

void
dialog_sheet_resize (WBCGtk *wbcg)
{
	GtkBuilder *gui;
	ResizeState *state;
	int slider_width;

	if (gnm_dialog_raise_if_exists (wbcg, RESIZE_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/sheet-resize.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (ResizeState, 1);
	state->wbcg   = wbcg;
	state->dialog = go_gtk_builder_get_widget (gui, "Resize");
	state->sheet = wbcg_cur_sheet (wbcg);
	g_return_if_fail (state->dialog != NULL);

	slider_width = mylog2 (MAX (GNM_MAX_ROWS / GNM_MIN_ROWS,
				    GNM_MAX_COLS / GNM_MIN_COLS)) *
		gnm_widget_measure_string (GTK_WIDGET (wbcg_toplevel (wbcg)),
					   "00");

	state->columns_scale = go_gtk_builder_get_widget (gui, "columns_scale");
	gtk_widget_set_size_request (state->columns_scale, slider_width, -1);
	state->columns_label = go_gtk_builder_get_widget (gui, "columns_label");
	state->rows_scale = go_gtk_builder_get_widget (gui, "rows_scale");
	gtk_widget_set_size_request (state->rows_scale, slider_width, -1);
	state->rows_label = go_gtk_builder_get_widget (gui, "rows_label");
	state->all_sheets_button = go_gtk_builder_get_widget (gui, "all_sheets_button");
	state->ok_button = go_gtk_builder_get_widget (gui, "ok_button");
	state->cancel_button = go_gtk_builder_get_widget (gui, "cancel_button");

	g_signal_connect_swapped (G_OBJECT (state->columns_scale),
				  "value-changed", G_CALLBACK (cb_scale_changed),
				  state);
	init_scale (state->columns_scale,
		    gnm_sheet_get_max_cols (state->sheet),
		    GNM_MIN_COLS, GNM_MAX_COLS);

	g_signal_connect_swapped (G_OBJECT (state->rows_scale),
				  "value-changed", G_CALLBACK (cb_scale_changed),
				  state);
	init_scale (state->rows_scale,
		    gnm_sheet_get_max_rows (state->sheet),
		    GNM_MIN_ROWS, GNM_MAX_ROWS);

	cb_scale_changed (state);

	g_signal_connect_swapped (G_OBJECT (state->cancel_button),
				  "clicked", G_CALLBACK (gtk_widget_destroy),
				  state->dialog);

	g_signal_connect_swapped (G_OBJECT (state->ok_button),
				  "clicked", G_CALLBACK (cb_ok_clicked),
				  state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       RESIZE_DIALOG_KEY);

	g_object_set_data_full (G_OBJECT (state->dialog),
	                        "state", state,
	                        (GDestroyNotify) g_free);
	g_object_unref (gui);

	gtk_widget_show (state->dialog);
}
