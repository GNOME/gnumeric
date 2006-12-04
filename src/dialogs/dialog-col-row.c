/**
 * dialog-col-row.c:  group/ungroup dialog
 *
 * Author:
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (c) Copyright 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include "help.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <workbook-edit.h>

#include <glade/glade.h>
#include <gtk/gtkframe.h>
#include <gtk/gtktogglebutton.h>

#define COL_ROW_DIALOG_KEY "col-row-dialog"

typedef struct {
	GladeXML           *gui;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	WorkbookControlGUI *wbcg;

	gpointer data;
	ColRowCallback_t callback;
} ColRowState;

static void
cb_dialog_col_row_destroy (GtkObject *w, ColRowState *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static void
cb_dialog_col_row_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
				  ColRowState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}


static void
cb_dialog_col_row_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			      ColRowState *state)
{
	state->callback (state->wbcg,
			 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
						       (glade_xml_get_widget (state->gui,
									      "cols"))),
			 state->data);
	gtk_widget_destroy (state->dialog);
	return;
}


GtkWidget *
dialog_col_row (WorkbookControlGUI *wbcg,  char const *operation,
		ColRowCallback_t callback,
		gpointer data)
{
	GladeXML	*gui;
	ColRowState	*state;

	g_return_val_if_fail (wbcg != NULL, NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, COL_ROW_DIALOG_KEY))
		return NULL;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"colrow.glade", NULL, NULL);
	if (gui == NULL)
		return NULL;

	state = g_new (ColRowState, 1);
	state->wbcg  = wbcg;
	state->callback = callback;
	state->data = data;
	state->gui = gui;

	state->dialog = glade_xml_get_widget (state->gui, "dialog");

	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_col_row_ok_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_col_row_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_GROUP_UNGROUP);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_col_row_destroy);

	gtk_frame_set_label (GTK_FRAME (glade_xml_get_widget (state->gui, "frame")), operation);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       COL_ROW_DIALOG_KEY);
	return state->dialog;
}
