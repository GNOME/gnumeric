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
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <selection.h>
#include <workbook.h>
#include <sheet.h>
#include <commands.h>
#include <ranges.h>
#include <cmd-edit.h>
#include <workbook-edit.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

#define GLADE_FILE "delete-cells.glade"
#define DELETE_CELL_DIALOG_KEY "delete-cells-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	Range const        *sel;
	Sheet              *sheet;
	GladeXML           *gui;
} DeleteCellState;

static gboolean
delete_cell_destroy (GtkObject *w, DeleteCellState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_delete_cell_ok_clicked (GtkWidget *button, DeleteCellState *state)
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
cb_delete_cell_cancel_clicked (GtkWidget *button, DeleteCellState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

void
dialog_delete_cells (WorkbookControlGUI *wbcg)
{
	DeleteCellState *state;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView	*sv  = wb_control_cur_sheet_view (wbc);
	Range const *sel;
	int  cols, rows;

	g_return_if_fail (wbcg != NULL);

	if (!(sel = selection_first_range (sv, wbc, _("Delete"))))
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

	state = g_new (DeleteCellState, 1);
	state->wbcg  = wbcg;
	state->sel   = sel;
	state->sheet = sv_sheet (sv);

	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
	g_return_if_fail (state->gui != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "Delete_cells");
	if (state->dialog == NULL) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Delete Cell dialog."));
		g_free (state);
		return ;
	}

	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_delete_cell_ok_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancelbutton");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_delete_cell_cancel_clicked), state);
/* FIXME: Add correct helpfile address */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "helpbutton"),
		"delete-cells.html");

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (delete_cell_destroy), state);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       DELETE_CELL_DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
