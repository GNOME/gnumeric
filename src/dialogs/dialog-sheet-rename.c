/*
 * dialog-sheet-rename.c: Dialog to rename current sheet.
 *
 * Author:
 *	Morten Welinder <terra@gnome.org>
 *
 * (C) Copyright 2013 Morten Welinder <terra@gnome.org>
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

#define RENAME_DIALOG_KEY "sheet-rename-dialog"

typedef struct {
	WBCGtk *wbcg;
	Sheet *sheet;
	GtkWidget *dialog;
	GtkWidget *old_name, *new_name;
	GtkWidget *ok_button, *cancel_button;
} RenameState;

static void
cb_name_changed (GtkEntry *e, RenameState *state)
{
	const char *name = gtk_entry_get_text (e);
	gboolean valid;
	Sheet *sheet2 = workbook_sheet_by_name (state->sheet->workbook, name);

	valid = (*name != 0) && (sheet2 == NULL || sheet2 == state->sheet);

	gtk_widget_set_sensitive (state->ok_button, valid);
}

static void
cb_ok_clicked (RenameState *state)
{
	const char *name = gtk_entry_get_text (GTK_ENTRY (state->new_name));

	cmd_rename_sheet (WORKBOOK_CONTROL (state->wbcg),
			  state->sheet,
			  name);

	gtk_widget_destroy (state->dialog);
}

void
dialog_sheet_rename (WBCGtk *wbcg, Sheet *sheet)
{
	GtkBuilder *gui;
	RenameState *state;

	if (gnumeric_dialog_raise_if_exists (wbcg, RENAME_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("sheet-rename.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (RenameState, 1);
	state->wbcg   = wbcg;
	state->dialog = go_gtk_builder_get_widget (gui, "Rename");
	state->sheet = sheet;
	g_return_if_fail (state->dialog != NULL);

	state->old_name = go_gtk_builder_get_widget (gui, "old_name");
	gtk_entry_set_text (GTK_ENTRY (state->old_name), sheet->name_unquoted);

	state->new_name = go_gtk_builder_get_widget (gui, "new_name");
	gtk_entry_set_text (GTK_ENTRY (state->new_name), sheet->name_unquoted);
	gtk_editable_select_region (GTK_EDITABLE (state->new_name), 0, -1);
	gtk_widget_grab_focus (state->new_name);
	g_signal_connect (state->new_name,
			  "changed", G_CALLBACK (cb_name_changed),
			  state);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog), state->new_name);

	state->ok_button = go_gtk_builder_get_widget (gui, "ok_button");
	g_signal_connect_swapped (G_OBJECT (state->ok_button),
				  "clicked", G_CALLBACK (cb_ok_clicked),
				  state);

	state->cancel_button = go_gtk_builder_get_widget (gui, "cancel_button");
	g_signal_connect_swapped (G_OBJECT (state->cancel_button),
				  "clicked", G_CALLBACK (gtk_widget_destroy),
				  state->dialog);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       RENAME_DIALOG_KEY);

	g_object_set_data_full (G_OBJECT (state->dialog),
	                        "state", state,
	                        (GDestroyNotify) g_free);
	g_object_unref (gui);

	gtk_widget_show (state->dialog);
}
