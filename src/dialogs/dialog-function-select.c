/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-function-select.c:  Implements the function selector
 *
 * Authors:
 *  Michael Meeks <michael@imaginator.com>
 *  Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <func.h>
#include <workbook.h>
#include <str.h>
#include <workbook-edit.h>
#include <application.h>
#include <gnumeric-gconf.h>

#include <gsf/gsf-impl-utils.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>
#include <ctype.h>

#define GLADE_FILE "function-select.glade"

#define FUNCTION_SELECT_KEY "function-selector-dialog"
#define FUNCTION_SELECT_DIALOG_KEY "function-selector-dialog"

typedef struct {
	WorkbookControlGUI  *wbcg;
	Workbook  *wb;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *ok_button;
	GtkTreeStore  *model;
	GtkTreeView   *treeview;
	GtkListStore  *model_f;
	GtkTreeView   *treeview_f;
	GtkTextBuffer   *description;

	GSList *recent_funcs;

	char const *formula_guru_key;
} FunctionSelectState;

enum {
	CAT_NAME,
	CATEGORY,
	NUM_COLMNS
};
enum {
	FUN_NAME,
	FUNCTION,
	NUM_COLUMNS
};

static void
dialog_function_load_recent_funcs (FunctionSelectState *state)
{
	FunctionDefinition *fd;
	GSList *recent_funcs, *this_funcs;

	recent_funcs = gnm_app_prefs->recent_funcs;

	for (this_funcs = recent_funcs; this_funcs; this_funcs = this_funcs->next) {
		char *name = this_funcs->data;
		if (name) {
			fd = func_lookup_by_name (name, NULL);
			g_free (name);
			if (fd)
				state->recent_funcs = g_slist_prepend (state->recent_funcs, fd);
		}
	}
	g_slist_free (recent_funcs);
}

static void
dialog_function_write_recent_func (FunctionSelectState *state, FunctionDefinition const *fd)
{
	GSList *rec_funcs;
	GSList *gconf_value_list = NULL;
	guint ulimit = gnm_app_prefs->num_of_recent_funcs;

	state->recent_funcs = g_slist_remove (state->recent_funcs, (gpointer) fd);
	state->recent_funcs = g_slist_prepend (state->recent_funcs, (gpointer) fd);

	while (g_slist_length (state->recent_funcs) > ulimit)
		state->recent_funcs = g_slist_remove (state->recent_funcs,
						      g_slist_last (state->recent_funcs)->data);
	
	for (rec_funcs = state->recent_funcs; rec_funcs; rec_funcs = rec_funcs->next) {
		gconf_value_list = g_slist_prepend 
			(gconf_value_list, g_strdup (function_def_get_name (rec_funcs->data)));
	}
	gnm_gconf_set_recent_funcs (gconf_value_list);
	gnm_conf_sync ();
	e_free_string_slist (gconf_value_list);
}


/**
 * dialog_function_select_destroy:
 * @window:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
dialog_function_select_destroy (GtkObject *w, FunctionSelectState  *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	if (state->formula_guru_key && 
	    gnumeric_dialog_raise_if_exists (state->wbcg, state->formula_guru_key)) {
		/* The formula guru is waiting for us.*/
		state->formula_guru_key = NULL;
		dialog_formula_guru (state->wbcg, NULL);	
	}

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

/**
 * cb_dialog_function_select_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_function_select_cancel_clicked (GtkWidget *button, FunctionSelectState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_dialog_function_select_ok_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_function_select_ok_clicked (GtkWidget *button, FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	FunctionDefinition const *func;
	GtkTreeSelection *the_selection = gtk_tree_view_get_selection (state->treeview_f);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		WorkbookControlGUI *wbcg = state->wbcg;
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);
		dialog_function_write_recent_func (state, func);
		state->formula_guru_key = NULL;
		gtk_widget_destroy (state->dialog);
		dialog_formula_guru (wbcg, func);
		return;
	}
	g_warning ("Something wrong, we should never get here!");
	gtk_widget_destroy (state->dialog);
	return;
}

static gint
dialog_function_select_by_name (gconstpointer _a, gconstpointer _b)
{
	FunctionDefinition const * const a = (FunctionDefinition const * const)_a;
	FunctionDefinition const * const b = (FunctionDefinition const * const)_b;

	return strcmp (function_def_get_name (a), function_def_get_name (b));
}

static void
dialog_function_select_load_tree (FunctionSelectState *state)
{
	int i = 0;
	GtkTreeIter p_iter;
	FunctionCategory const * cat;

	gtk_tree_store_clear (state->model);

	gtk_tree_store_append (state->model, &p_iter, NULL);
	gtk_tree_store_set (state->model, &p_iter,
			    CAT_NAME, _("Recently Used"),
			    CATEGORY, NULL,
			    -1);

	while ((cat = function_category_get_nth (i++)) != NULL) {
		gtk_tree_store_append (state->model, &p_iter, NULL);
		gtk_tree_store_set (state->model, &p_iter,
				    CAT_NAME, _(cat->display_name->str),
				    CATEGORY, cat,
				    -1);
	}

}


static void
cb_dialog_function_select_fun_selection_changed (GtkTreeSelection *the_selection, 
					     FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	FunctionDefinition const *func;

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);
		{
			TokenizedHelp *help = tokenized_help_new (func);
			char const * f_desc =
				tokenized_help_find (help, "DESCRIPTION");
			char const * cursor;
			GString    * buf = g_string_new ("");
			GtkTextIter  start, end;
			GtkTextTag * tag;
			int          i;

			g_string_append (buf, f_desc);
			gtk_text_buffer_set_text (state->description, buf->str,
						  -1);

			/* Set the fn name Bold */
			cursor = f_desc;
			for (i = 0; !isspace ((unsigned char)*cursor); i++)
				cursor++;

			tag = gtk_text_buffer_create_tag (state->description,
							  NULL,
							  "weight",
							  PANGO_WEIGHT_BOLD,
							  NULL);  
			gtk_text_buffer_get_iter_at_offset (state->description,
							    &start, 0);
			gtk_text_buffer_get_iter_at_offset (state->description,
							    &end, i);
			gtk_text_buffer_apply_tag (state->description, tag,
						   &start, &end);

			/* Set the arguments and errors Italic */
			for ( ; *cursor; cursor++) {
				if (*cursor == '@' || *cursor == '#') {
					cursor++;
					for (i = 0; *cursor
						     && !isspace ((unsigned char)*cursor); i++)
						cursor++;
					tag = gtk_text_buffer_create_tag
						(state->description,
						 NULL, "style",
						 PANGO_STYLE_ITALIC, NULL);  
					gtk_text_buffer_get_iter_at_offset
						(state->description, &start,
						 cursor - f_desc - i);
					gtk_text_buffer_get_iter_at_offset
						(state->description, &end,
						 cursor - f_desc);
					gtk_text_buffer_apply_tag
						(state->description, tag,
						 &start, &end);
				} else if (*cursor == '\n' && cursor[1] == '*'
					   && cursor[2] == ' ') {
					tag = gtk_text_buffer_create_tag
						(state->description, NULL,
						 "weight", PANGO_WEIGHT_BOLD,
						 NULL);
					gtk_text_buffer_get_iter_at_offset
						(state->description, &start,
						 cursor - f_desc + 1);
					gtk_text_buffer_get_iter_at_offset
						(state->description, &end,
						 cursor - f_desc + 2);
					gtk_text_buffer_apply_tag
						(state->description, tag,
						 &start, &end);

					/* Make notes to look cooler. */
					for (i = 2; cursor[i] 
						     && cursor[i] != '\n'; i++)
						;

					tag = gtk_text_buffer_create_tag
						(state->description, NULL,
						 "scale", 0.85, NULL);
					
					gtk_text_buffer_get_iter_at_offset
						(state->description,
						 &start, cursor - f_desc + 1);
					gtk_text_buffer_get_iter_at_offset
						(state->description, &end, 
						 cursor - f_desc + i);
					gtk_text_buffer_apply_tag
						(state->description, tag,
						 &start, &end);
				}
			}

			g_string_free (buf, FALSE);
			tokenized_help_destroy (help);
		}
		gtk_widget_set_sensitive (state->ok_button, TRUE);
		return;
	}
	gtk_widget_set_sensitive (state->ok_button, FALSE);
	gtk_text_buffer_set_text (state->description, "", 0);

}

static void
cb_dialog_function_select_cat_selection_changed (GtkTreeSelection *the_selection, 
					     FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	FunctionCategory const * cat;
	GList *funcs, *this_func;

	gtk_list_store_clear (state->model_f);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    CATEGORY, &cat,
				    -1);
		if (cat != NULL) {
			funcs = g_list_sort (g_list_copy (cat->functions), 
					     dialog_function_select_by_name);
			for (this_func = funcs; this_func; this_func = this_func->next) {
				FunctionDefinition const *a_func = this_func->data;
				TokenizedHelp *help = tokenized_help_new (a_func);
				char const *f_syntax = tokenized_help_find (help, "SYNTAX");
				
				gtk_list_store_append (state->model_f, &iter);
				gtk_list_store_set (state->model_f, &iter,
						    FUN_NAME, f_syntax,
						    FUNCTION, a_func,
						    -1);
				tokenized_help_destroy (help);
			}
			g_list_free (funcs);
		} else {
			GSList *rec_funcs;
			for (rec_funcs = state->recent_funcs; rec_funcs; 
			     rec_funcs = rec_funcs->next) {
				FunctionDefinition const *a_func = rec_funcs->data;

				TokenizedHelp *help = tokenized_help_new (a_func);
				char const *f_syntax = tokenized_help_find (help, "SYNTAX");
				
				gtk_list_store_append (state->model_f, &iter);
				gtk_list_store_set (state->model_f, &iter,
						    FUN_NAME, f_syntax,
						    FUNCTION, a_func,
						    -1);
				tokenized_help_destroy (help);
			}
		}
	}
}

static gboolean
dialog_function_select_init (FunctionSelectState *state)
{
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTextView *textview;
	
	dialog_function_load_recent_funcs (state);

	g_object_set_data (G_OBJECT (state->dialog), FUNCTION_SELECT_DIALOG_KEY, 
			   state);

	/* Set-up first treeview */
	scrolled = glade_xml_get_widget (state->gui, "scrolled_tree");
	state->model = gtk_tree_store_new (NUM_COLMNS, G_TYPE_STRING, G_TYPE_POINTER);
	state->treeview = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_dialog_function_select_cat_selection_changed), state);

	column = gtk_tree_view_column_new_with_attributes (_("Name"),
							   gtk_cell_renderer_text_new (),
							   "text", CAT_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, CAT_NAME);
	gtk_tree_view_append_column (state->treeview, column);

	gtk_tree_view_set_headers_visible (state->treeview, FALSE);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview));
	dialog_function_select_load_tree (state);
	/* Finished set-up of first treeview */

	/* Set-up second treeview */
	scrolled = glade_xml_get_widget (state->gui, "scrolled_list");
	state->model_f = gtk_list_store_new (NUM_COLMNS, G_TYPE_STRING, G_TYPE_POINTER);
	state->treeview_f = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model_f)));
	selection = gtk_tree_view_get_selection (state->treeview_f);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_dialog_function_select_fun_selection_changed), state);

	column = gtk_tree_view_column_new_with_attributes (_("Name"),
							   gtk_cell_renderer_text_new (),
							   "text", FUN_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, FUN_NAME);
	gtk_tree_view_append_column (state->treeview_f, column);

	gtk_tree_view_set_headers_visible (state->treeview_f, FALSE);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview_f));
	/* Finished set-up of second treeview */

	gtk_paned_set_position (GTK_PANED (glade_xml_get_widget 
					   (state->gui, "vpaned1")), 300);

	textview = GTK_TEXT_VIEW (glade_xml_get_widget (state->gui, "description"));
	state->description =  gtk_text_view_get_buffer (textview);

	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	gtk_widget_set_sensitive (state->ok_button, FALSE);
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_function_select_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_dialog_function_select_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"cell-sort.html");

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (dialog_function_select_destroy), state);

	return FALSE;
}

void
dialog_function_select (WorkbookControlGUI *wbcg, char const *key)
{
	FunctionSelectState* state;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, FUNCTION_SELECT_KEY))
		return;

	state = g_new (FunctionSelectState, 1);
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->formula_guru_key = key;
        state->recent_funcs = NULL;

	/* Get the dialog and check for errors */
	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (state->gui == NULL) {
		g_warning ("glade file missing or corrupted");
		g_free (state);
                return;
	}

        state->dialog = glade_xml_get_widget (state->gui, "selection_dialog");

	if (dialog_function_select_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the function selector dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       FUNCTION_SELECT_KEY);

	gtk_widget_show_all (state->dialog);

	return;
}
