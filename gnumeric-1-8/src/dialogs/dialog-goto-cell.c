/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-goto-cell.c:  Implements the "goto cell/navigator" functionality
 *
 * Author:
 * Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Copyright (C) Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <workbook-priv.h> /* for Workbook::names */
#include <workbook-control.h>
#include <ranges.h>
#include <value.h>
#include <expr-name.h>
#include <sheet.h>
#include <workbook-view.h>

#include <wbc-gtk.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktable.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkcellrenderertext.h>

#define GOTO_KEY "goto-dialog"

typedef struct {
	WBCGtk  *wbcg;
	Workbook  *wb;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *close_button;
	GtkWidget *go_button;
	GtkEntry *goto_text;

	GtkTreeStore  *model;
	GtkTreeView   *treeview;
	GtkTreeSelection   *selection;
} GotoState;

enum {
	ITEM_NAME,
	SHEET_NAME,
	SHEET_POINTER,
	EXPRESSION,
	NUM_COLMNS
};

static void
cb_dialog_goto_free (GotoState  *state)
{
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	if (state->model != NULL)
		g_object_unref (G_OBJECT (state->model));
	g_free (state);
}

static void
cb_dialog_goto_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			      GotoState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_goto_go_clicked (G_GNUC_UNUSED GtkWidget *button,
			   GotoState *state)
{
	char *text = g_strdup (gtk_entry_get_text (state->goto_text));

	if (wb_control_parse_and_jump (WORKBOOK_CONTROL (state->wbcg), text)) {
#if 0
		gnome_entry_append_history (state->goto_text, TRUE, text);
#endif
	}
	g_free (text);
	return;
}

static void
cb_dialog_goto_update_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				   GotoState *state)
{
	GnmValue *val = value_new_cellrange_str (wb_control_cur_sheet (WORKBOOK_CONTROL (state->wbcg)),
					    gtk_entry_get_text (state->goto_text));
	if (val != NULL) {
		gtk_widget_set_sensitive (state->go_button, TRUE);
		value_release (val);
	} else
		gtk_widget_set_sensitive (state->go_button, FALSE);
	gtk_entry_set_activates_default (state->goto_text, (val != NULL));
}

typedef struct {
	GtkTreeIter  iter;
	GotoState   *state;
} LoadNames;

static void
cb_load_names (G_GNUC_UNUSED gpointer key,
	       GnmNamedExpr *nexpr, LoadNames *user)
{
	GtkTreeIter iter;
	char *expr_name = NULL;

	gtk_tree_store_append (user->state->model, &iter, &user->iter);

	if (nexpr->pos.sheet != NULL)
		expr_name = g_strdup_printf ("%s!%s",
					     nexpr->pos.sheet->name_unquoted,
					     nexpr->name->str);
	gtk_tree_store_set (user->state->model, &iter,
		ITEM_NAME,	expr_name ? expr_name : nexpr->name->str,
		SHEET_POINTER,	nexpr->pos.sheet,
		EXPRESSION,	nexpr,
		-1);
	g_free (expr_name);
}

static void
dialog_goto_load_names (GotoState *state)
{
	Sheet *sheet;
	LoadNames closure;
	int i, l;

	gtk_tree_store_clear (state->model);

	closure.state = state;
	gtk_tree_store_append (state->model, &closure.iter, NULL);
	gtk_tree_store_set (state->model, &closure.iter,
			    SHEET_NAME,		_("Workbook Level"),
			    ITEM_NAME,		NULL,
			    SHEET_POINTER,	NULL,
			    EXPRESSION,		NULL,
			    -1);
	if (state->wb->names != NULL)
		g_hash_table_foreach (state->wb->names->names,
			(GHFunc) cb_load_names, &closure);

	l = workbook_sheet_count (state->wb);
	for (i = 0; i < l; i++) {
		sheet = workbook_sheet_by_index (state->wb, i);
		gtk_tree_store_append (state->model, &closure.iter, NULL);
		gtk_tree_store_set (state->model, &closure.iter,
				    SHEET_NAME,		sheet->name_unquoted,
				    ITEM_NAME,		NULL,
				    SHEET_POINTER,	sheet,
				    EXPRESSION,		NULL,
				    -1);

		if (sheet->names != NULL)
			g_hash_table_foreach (sheet->names->names,
				(GHFunc) cb_load_names, &closure);
	}

}

static void
cb_dialog_goto_selection_changed (GtkTreeSelection *the_selection, GotoState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	Sheet        *sheet;
	GnmNamedExpr *name;

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    SHEET_POINTER, &sheet,
				    EXPRESSION, &name,
				    -1);
		if (name) {
			GnmParsePos pp;
			char *where_to;

			if (NULL == sheet)
				sheet = wb_control_cur_sheet ( WORKBOOK_CONTROL (state->wbcg));

			parse_pos_init_sheet (&pp, sheet);
			where_to = expr_name_as_string  (name, &pp, gnm_conventions_default);
			if (wb_control_parse_and_jump (WORKBOOK_CONTROL (state->wbcg), where_to))
				gtk_entry_set_text (state->goto_text,
						    where_to);
			g_free (where_to);
			return;
		}
		if (sheet)
			wb_view_sheet_focus (
				wb_control_view (WORKBOOK_CONTROL (state->wbcg)), sheet);
	}
}


/**
 * dialog_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_goto_init (GotoState *state)
{
	GtkTable *table;
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "names"));
	state->goto_text = GTK_ENTRY (gtk_entry_new ());
	gtk_table_attach (table, GTK_WIDGET (state->goto_text),
			  0, 1, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	g_signal_connect_after (G_OBJECT (state->goto_text),
		"changed",
		G_CALLBACK (cb_dialog_goto_update_sensitivity), state);

	/* Set-up treeview */
	scrolled = glade_xml_get_widget (state->gui, "scrolled");
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
					     GTK_SHADOW_ETCHED_IN);

	state->model = gtk_tree_store_new (NUM_COLMNS, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_POINTER, G_TYPE_POINTER);
	state->treeview = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_BROWSE);
	g_signal_connect (state->selection,
		"changed",
		G_CALLBACK (cb_dialog_goto_selection_changed), state);

	column = gtk_tree_view_column_new_with_attributes (_("Sheet"),
							   gtk_cell_renderer_text_new (),
							   "text", SHEET_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, SHEET_NAME);
	gtk_tree_view_append_column (state->treeview, column);

	column = gtk_tree_view_column_new_with_attributes (_("Cell"),
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, ITEM_NAME);
	gtk_tree_view_append_column (state->treeview, column);
	gtk_tree_view_set_headers_visible (state->treeview, TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview));
	dialog_goto_load_names (state);
	/* Finished set-up of treeview */

	state->close_button  = glade_xml_get_widget (state->gui, "close_button");
	g_signal_connect (G_OBJECT (state->close_button),
		"clicked",
		G_CALLBACK (cb_dialog_goto_close_clicked), state);

	state->go_button  = glade_xml_get_widget (state->gui, "go_button");
	g_signal_connect (G_OBJECT (state->go_button),
		"clicked",
		G_CALLBACK (cb_dialog_goto_go_clicked), state);
	gtk_window_set_default (GTK_WINDOW (state->dialog), state->go_button);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_GOTO_CELL);

	cb_dialog_goto_update_sensitivity (NULL, state);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_goto_free);

	return FALSE;
}


void
dialog_goto_cell (WBCGtk *wbcg)
{
	GotoState* state;
	GladeXML *gui;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, GOTO_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"goto.glade", NULL, NULL);
        if (gui == NULL)
                return;

	state = g_new (GotoState, 1);
	state->wbcg   = wbcg;
	state->wb     = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	state->gui    = gui;
        state->dialog = glade_xml_get_widget (state->gui, "goto_dialog");

	if (dialog_goto_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the goto dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       GOTO_KEY);

	gtk_widget_show_all (state->dialog);
}
