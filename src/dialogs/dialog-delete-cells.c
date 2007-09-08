/*
 * dialog-insert-cells.c: Insert a number of cells.
 *
 * Authors:
 *                Miguel de Icaza (miguel@gnu.org)
 * Copyright (C)  Andreas J. Guelzow (aguelzow@taliesin.ca)
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
#include <selection.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-view.h>
#include <commands.h>
#include <ranges.h>
#include <cmd-edit.h>
#include <wbc-gtk.h>
#include <command-context.h>

#include <glade/glade.h>
#include <gtk/gtkradiobutton.h>

#define DELETE_CELL_DIALOG_KEY "delete-cells-dialog"

typedef struct {
	WBCGtk *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GnmRange const     *sel;
	Sheet              *sheet;
	GladeXML           *gui;
} DeleteCellState;

static void
cb_delete_cell_destroy (DeleteCellState *state)
{
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static void
cb_delete_cell_ok_clicked (DeleteCellState *state)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);
	GtkWidget *radio_0;
	int  cols, rows;
	int i;

	radio_0 = glade_xml_get_widget (state->gui, "radio_0");
	g_return_if_fail (radio_0 != NULL);

	i = gtk_radio_group_get_selected
		(GTK_RADIO_BUTTON (radio_0)->group);

	cols = state->sel->end.col - state->sel->start.col + 1;
	rows = state->sel->end.row - state->sel->start.row + 1;

	switch (i) {
	case 0 :
		cmd_shift_rows (wbc, state->sheet,
				state->sel->end.col + 1,
				state->sel->start.row,
				state->sel->end.row, - cols);
		break;
	case 1 :
		cmd_shift_cols (wbc, state->sheet,
				state->sel->start.col,
				state->sel->end.col,
				state->sel->end.row + 1, - rows);
		break;
	case 2 :
		cmd_delete_rows (wbc, state->sheet,
				 state->sel->start.row, rows);
		break;
	default :
		cmd_delete_cols (wbc, state->sheet,
				 state->sel->start.col, cols);
		break;
	}
	gtk_widget_destroy (state->dialog);
}

static void
cb_delete_cell_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			       DeleteCellState *state)
{
	gtk_widget_destroy (state->dialog);
}

void
dialog_delete_cells (WBCGtk *wbcg)
{
	DeleteCellState *state;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView	*sv  = wb_control_cur_sheet_view (wbc);
	GnmRange const *sel;
	GladeXML    *gui;
	GtkWidget   *w;
	int  cols, rows;

	g_return_if_fail (wbcg != NULL);

	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Delete"))))
		return;
	cols = sel->end.col - sel->start.col + 1;
	rows = sel->end.row - sel->start.row + 1;

	if (range_is_full (sel, FALSE)) {
		cmd_delete_cols (wbc, sv_sheet (sv), sel->start.col, cols);
		return;
	}
	if (range_is_full (sel, TRUE)) {
		cmd_delete_rows (wbc, sv_sheet (sv), sel->start.row, rows);
		return;
	}

	if (gnumeric_dialog_raise_if_exists (wbcg, DELETE_CELL_DIALOG_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"delete-cells.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new (DeleteCellState, 1);
	state->wbcg  = wbcg;
	state->sel   = sel;
	state->gui   = gui;
	state->sheet = sv_sheet (sv);

	state->dialog = glade_xml_get_widget (state->gui, "Delete_cells");
	if (state->dialog == NULL) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Delete Cell dialog."));
		g_free (state);
		return ;
	}

	w = glade_xml_get_widget (state->gui, "okbutton");
	g_signal_connect_swapped (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_delete_cell_ok_clicked), state);
	w = glade_xml_get_widget (state->gui, "cancelbutton");
	g_signal_connect (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_delete_cell_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "helpbutton"),
		GNUMERIC_HELP_LINK_DELETE_CELLS);

	gtk_toggle_button_set_active 
		(GTK_TOGGLE_BUTTON (glade_xml_get_widget 
				    (state->gui, cols < rows 
				     ? "radio_0" : "radio_1")), 
		 TRUE);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_delete_cell_destroy);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       DELETE_CELL_DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
