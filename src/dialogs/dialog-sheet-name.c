/*
 * dialog-simple-input.c: Implements various dialogs for simple
 * input values
 *
 * Authors:
 *   Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Copyright (c) 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <sheet.h>
#include <workbook-edit.h>
#include <gui-util.h>
#include <workbook.h>
#include <commands.h>

#include <math.h>
#include <glade/glade.h>
#include <libgnome/gnome-i18n.h>

#define SHEET_NAME_CHANGE_DIALOG_KEY "sheet-name-change-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	Sheet              *sheet;
	GtkWidget          *dialog;
	GtkWidget          *entry;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GladeXML           *gui;
} SheetNameChangeState;

/**
 * sheet_name_destroy:
 * @window:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
sheet_name_destroy (GtkObject *w, SheetNameChangeState *state)
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
cb_sheet_name_ok_clicked (GtkWidget *button, SheetNameChangeState *state)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);
	char const *new_name;

	new_name = gtk_entry_get_text (GTK_ENTRY (state->entry));
	cmd_rename_sheet (wbc, state->sheet, NULL, new_name);
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_sheet_name_cancel_clicked (GtkWidget *button, SheetNameChangeState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

void
dialog_sheet_name (WorkbookControlGUI *wbcg)
{
	SheetNameChangeState *state;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_NAME_CHANGE_DIALOG_KEY))
		return;

	state = g_new (SheetNameChangeState, 1);
	state->wbcg  = wbcg;
	state->sheet = sheet;

	state->gui = gnumeric_glade_xml_new (wbcg, "sheet-rename.glade");
	g_return_if_fail (state->gui != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "dialog");
	if (state->dialog == NULL) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Sheet Name Change dialog."));
		g_free (state);
		return ;
	}

	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_sheet_name_ok_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancelbutton");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_sheet_name_cancel_clicked), state);

	state->entry = glade_xml_get_widget (state->gui, "entry");
	gtk_entry_set_text (GTK_ENTRY (state->entry), sheet->name_unquoted);
	gtk_editable_select_region (GTK_EDITABLE (state->entry), 0, -1);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog), GTK_WIDGET (state->entry));

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (sheet_name_destroy), state);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       SHEET_NAME_CHANGE_DIALOG_KEY);
	gtk_widget_show (state->dialog);
	gtk_widget_grab_focus (state->entry);
}



