/*
 * dialog-delete-cells.c: Delete a number of cells.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

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


#define DELETE_CELL_DIALOG_KEY "delete-cells-dialog"

typedef struct {
	WBCGtk *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GnmRange const     *sel;
	Sheet              *sheet;
	GtkBuilder         *gui;
} DeleteCellState;

static void
cb_delete_cell_destroy (DeleteCellState *state)
{
	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_free (state);
}

static void
cb_delete_cell_ok_clicked (DeleteCellState *state)
{
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	GtkWidget *radio_0;
	int  cols, rows;
	int i;

	radio_0 = go_gtk_builder_get_widget (state->gui, "radio_0");
	g_return_if_fail (radio_0 != NULL);

	i = gnm_gtk_radio_group_get_selected
	  (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio_0)));

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
	WorkbookControl *wbc = GNM_WBC (wbcg);
	SheetView	*sv  = wb_control_cur_sheet_view (wbc);
	Sheet *sheet = sv_sheet (sv);
	GnmRange const *sel;
	GtkBuilder *gui;
	GtkWidget   *w;
	int  cols, rows;

	g_return_if_fail (wbcg != NULL);

	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Delete"))))
		return;
	cols = sel->end.col - sel->start.col + 1;
	rows = sel->end.row - sel->start.row + 1;

	if (range_is_full (sel, sheet, FALSE)) {
		cmd_delete_cols (wbc, sheet, sel->start.col, cols);
		return;
	}
	if (range_is_full (sel, sheet, TRUE)) {
		cmd_delete_rows (wbc, sheet, sel->start.row, rows);
		return;
	}

	if (gnm_dialog_raise_if_exists (wbcg, DELETE_CELL_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/delete-cells.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (DeleteCellState, 1);
	state->wbcg  = wbcg;
	state->sel   = sel;
	state->gui   = gui;
	state->sheet = sv_sheet (sv);

	state->dialog = go_gtk_builder_get_widget (state->gui, "Delete_cells");
	if (state->dialog == NULL) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Delete Cell dialog."));
		g_free (state);
		return ;
	}

	w = go_gtk_builder_get_widget (state->gui, "okbutton");
	g_signal_connect_swapped (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_delete_cell_ok_clicked), state);
	w = go_gtk_builder_get_widget (state->gui, "cancelbutton");
	g_signal_connect (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_delete_cell_cancel_clicked), state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "helpbutton"),
		GNUMERIC_HELP_LINK_DELETE_CELLS);

	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget
				    (state->gui, cols < rows
				     ? "radio_0" : "radio_1")),
		 TRUE);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_delete_cell_destroy);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       DELETE_CELL_DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
