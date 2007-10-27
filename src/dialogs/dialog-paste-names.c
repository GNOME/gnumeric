/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-paste-names.c: A compatibility dialog to paste the list of available
 *	names into the current cell/selection
 *
 * Copyright (C) 2005 Jody Goldberg (jody@gnome.org)
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <expr.h>
#include <str.h>
#include <expr-name.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <value.h>
#include <commands.h>
#include <widgets/gnumeric-expr-entry.h>

#include <glade/glade.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkcellrenderertext.h>
#include <string.h>

#define PASTE_NAMES_KEY "dialog-paste-names"

typedef struct {
	GladeXML		*gui;
	GtkWidget		*dialog;
	GtkWidget		*treeview;
	GtkListStore		*model;
	GtkTreeSelection	*selection;

	WBCGtk	*wbcg;
} DialogPasteNames;

static void
dialog_paste_names_free (DialogPasteNames *state)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);

	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);
	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;

	g_free (state);
}

static gboolean
paste_names_init (DialogPasteNames *state, WBCGtk *wbcg)
{
	GtkTreeViewColumn *column;

	state->wbcg  = wbcg;
	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"paste-names.glade", NULL, NULL);
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "PasteNames");
	state->model	 = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
	state->treeview  = glade_xml_get_widget (state->gui, "name_list");
	gtk_tree_view_set_model (GTK_TREE_VIEW (state->treeview),
				 GTK_TREE_MODEL (state->model));
	column = gtk_tree_view_column_new_with_attributes (_("Name"),
			gtk_cell_renderer_text_new (),
			"text", 0,
			NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->treeview), column);

	state->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (state->treeview));
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_BROWSE);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_DEFINE_NAMES);

	/* a candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		PASTE_NAMES_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)dialog_paste_names_free);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	return FALSE;
}

void
dialog_paste_names (WBCGtk *wbcg)
{
	DialogPasteNames *state;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, PASTE_NAMES_KEY))
		return;

	state = g_new0 (DialogPasteNames, 1);
	if (paste_names_init (state, wbcg)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Name Guru."));
		g_free (state);
		return;
	}
}
