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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

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
	GladeXML *gui;
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
	*cols = 1 << (int)adj->value;

	adj = gtk_range_get_adjustment (GTK_RANGE (state->rows_scale));
	*rows = 1 << (int)adj->value;
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

static void
init_scale (GtkWidget *scale, int N)
{
	GtkAdjustment *adj = gtk_range_get_adjustment (GTK_RANGE (scale));
	int l2 = 0;

	while (N > 1)
		N >>= 1, l2++;

	g_object_set (G_OBJECT (adj), "value", (double)l2, NULL);
}

static void
cb_destroy (ResizeState *state)
{
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));

	g_free (state);
}

static void
cb_ok_clicked (ResizeState *state)
{
	GSList *sheets, *l;
	GSList *changed_sheets = NULL;
	WorkbookControl *wbc;
	Workbook *wb;
	gboolean all_sheets;
	int cols, rows;

	get_sizes (state, &cols, &rows);
	all_sheets = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->all_sheets_button));

	wbc = WORKBOOK_CONTROL (state->wbcg);
	wb = wb_control_get_workbook (wbc);
	sheets = workbook_sheets (wb);
	for (l = sheets; l; l = l->next) {
		Sheet *this_sheet = l->data;

		if (!all_sheets && this_sheet != state->sheet)
			continue;

		if (cols == gnm_sheet_get_max_cols (this_sheet) &&
		    rows == gnm_sheet_get_max_rows (this_sheet))
			continue;

		changed_sheets = g_slist_prepend (changed_sheets, this_sheet);
	}
	g_slist_free (sheets);

	if (changed_sheets)
		cmd_resize_sheets (wbc, g_slist_reverse (changed_sheets),
				   cols, rows);

	gtk_widget_destroy (state->dialog);
}

void
dialog_sheet_resize (WBCGtk *wbcg)
{
	GladeXML *gui;
	ResizeState *state;

	if (gnumeric_dialog_raise_if_exists (wbcg, RESIZE_DIALOG_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"sheet-resize.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new (ResizeState, 1);
	state->wbcg   = wbcg;
	state->gui    = gui;
	state->dialog = glade_xml_get_widget (state->gui, "Resize");
	state->sheet = wbcg_cur_sheet (wbcg);
	g_return_if_fail (state->dialog != NULL);

	state->columns_scale = glade_xml_get_widget (state->gui, "columns_scale");
	state->columns_label = glade_xml_get_widget (state->gui, "columns_label");
	state->rows_scale = glade_xml_get_widget (state->gui, "rows_scale");
	state->rows_label = glade_xml_get_widget (state->gui, "rows_label");
	state->all_sheets_button = glade_xml_get_widget (state->gui, "all_sheets_button");
	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	state->cancel_button = glade_xml_get_widget (state->gui, "cancel_button");

	g_signal_connect_swapped (G_OBJECT (state->columns_scale),
				  "value-changed", G_CALLBACK (cb_scale_changed),
				  state);
	init_scale (state->columns_scale, gnm_sheet_get_max_cols (state->sheet));

	g_signal_connect_swapped (G_OBJECT (state->rows_scale),
				  "value-changed", G_CALLBACK (cb_scale_changed),
				  state);
	init_scale (state->rows_scale, gnm_sheet_get_max_rows (state->sheet));

	g_signal_connect_swapped (G_OBJECT (state->cancel_button),
				  "clicked", G_CALLBACK (gtk_widget_destroy),
				  state->dialog);

	g_signal_connect_swapped (G_OBJECT (state->ok_button),
				  "clicked", G_CALLBACK (cb_ok_clicked),
				  state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       RESIZE_DIALOG_KEY);

	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state,
				(GDestroyNotify) cb_destroy);

	gtk_widget_show (state->dialog);
}
