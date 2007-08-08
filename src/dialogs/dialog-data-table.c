/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-data-table.c: Create a Data Table
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <expr.h>
#include <selection.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <gui-util.h>
#include <parse-util.h>
#include <commands.h>
#include <widgets/gnumeric-expr-entry.h>
#include <gtk/gtktable.h>
#include <glib/gi18n.h>

#define DIALOG_DATA_TABLE_KEY "dialog-data-table"

typedef struct {
	GladeXML	*gui;
	GtkWidget	*dialog;
	GnmExprEntry	*row_entry, *col_entry;

	WBCGtk	*wbcg;
} GnmDialogDataTable;

static void
cb_data_table_destroy (GnmDialogDataTable *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static GnmExprEntry *
init_entry (GnmDialogDataTable *state, char const *name)
{
	GtkWidget *w = glade_xml_get_widget (state->gui, name);

	g_return_val_if_fail (w != NULL, NULL);

	gnm_expr_entry_set_flags (GNM_EXPR_ENTRY (w),
		GNM_EE_SINGLE_RANGE, GNM_EE_SINGLE_RANGE);
	g_object_set (G_OBJECT (w),
		"scg", wbcg_cur_scg (state->wbcg),
		"with-icon", TRUE,
		NULL);
	return GNM_EXPR_ENTRY (w);
}

static void
cb_data_table_response (GtkWidget *dialog, gint response_id, GnmDialogDataTable *state)
{
	if (response_id == GTK_RESPONSE_HELP)
		return;
	if (response_id == GTK_RESPONSE_OK) {
	}

	gtk_object_destroy (GTK_OBJECT (dialog));
}

static gboolean
data_table_init (GnmDialogDataTable *state, WBCGtk *wbcg)
{
	GtkTable *table;

	state->wbcg  = wbcg;
	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"data-table.glade", NULL, NULL);
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "DataTable");
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "table"));

	state->row_entry = init_entry (state, "row-entry");
	state->col_entry = init_entry (state, "col-entry");

	g_signal_connect (G_OBJECT (state->dialog), "response",
		G_CALLBACK (cb_data_table_response), state);
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help"),
		GNUMERIC_HELP_LINK_DATA_TABLE);

	/* a candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		DIALOG_DATA_TABLE_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_data_table_destroy);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	return FALSE;
}

void
dialog_data_table (WBCGtk *wbcg)
{
	GnmDialogDataTable *state;

	g_return_if_fail (wbcg != NULL);

	if (wbcg_edit_get_guru (wbcg) ||
	    gnumeric_dialog_raise_if_exists (wbcg, DIALOG_DATA_TABLE_KEY))
		return;

	state = g_new0 (GnmDialogDataTable, 1);
	if (data_table_init (state, wbcg)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
			_("Could not create the Data Table definition dialog."));
		g_free (state);
	}
}
