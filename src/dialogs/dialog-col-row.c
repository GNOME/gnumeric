/*
 * dialog-col-row.c:  group/ungroup dialog
 *
 * Author:
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (c) Copyright 2002-2006 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>


#define COL_ROW_DIALOG_KEY "col-row-dialog"

typedef struct {
	GtkBuilder         *gui;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	WBCGtk *wbcg;

	gpointer data;
	ColRowCallback_t callback;
} ColRowState;

static void
cb_dialog_col_row_destroy (ColRowState *state)
{
	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_free (state);
}

static void
cb_dialog_col_row_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
				  ColRowState *state)
{
    gtk_widget_destroy (state->dialog);
}


static void
cb_dialog_col_row_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			      ColRowState *state)
{
	state->callback (state->wbcg,
			 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
						       (go_gtk_builder_get_widget (state->gui,
									      "cols"))),
			 state->data);
	gtk_widget_destroy (state->dialog);
}

/**
 * dialog_col_row: (skip)
 */
void
dialog_col_row (WBCGtk *wbcg,  char const *operation,
		ColRowCallback_t callback,
		gpointer data)
{
	GtkBuilder	*gui;
	ColRowState	*state;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, COL_ROW_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/colrow.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (ColRowState, 1);
	state->wbcg  = wbcg;
	state->callback = callback;
	state->data = data;
	state->gui = gui;

	state->dialog = go_gtk_builder_get_widget (state->gui, "dialog");

	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_col_row_ok_clicked), state);

	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_col_row_cancel_clicked), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_GROUP_UNGROUP);

	gtk_window_set_title (GTK_WINDOW (state->dialog), operation);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_col_row_destroy);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       COL_ROW_DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
