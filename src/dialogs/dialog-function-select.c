/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-function-select.c:  Implements the function selector
 *
 * Authors:
 *  Michael Meeks <michael@ximian.com>
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <func.h>
#include <workbook.h>
#include <str.h>
#include <wbc-gtk.h>
#include <application.h>
#include <gnumeric-gconf.h>

#include <gsf/gsf-impl-utils.h>
#include <glade/glade.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkpaned.h>
#include <gtk/gtkliststore.h>
#include <string.h>

#undef F_
#define F_(s) dgettext ("gnumeric-functions", (s))

#define FUNCTION_SELECT_KEY "function-selector-dialog"
#define FUNCTION_SELECT_DIALOG_KEY "function-selector-dialog"

typedef struct {
	WBCGtk  *wbcg;
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
	GnmFunc *fd;
	GSList const *recent_funcs, *this_funcs;

	recent_funcs = gnm_app_prefs->recent_funcs;

	for (this_funcs = recent_funcs; this_funcs; this_funcs = this_funcs->next) {
		char const *name = this_funcs->data;
		if (name == NULL)
			continue;
		fd = gnm_func_lookup (name, NULL);
		if (fd)
			state->recent_funcs = g_slist_prepend (state->recent_funcs, fd);
	}
}

static void
dialog_function_write_recent_func (FunctionSelectState *state, GnmFunc const *fd)
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
			(gconf_value_list, g_strdup (gnm_func_get_name (rec_funcs->data)));
	}
	gnm_gconf_set_recent_funcs (gconf_value_list);
	go_conf_sync (NULL);
}


static void
cb_dialog_function_select_destroy (FunctionSelectState  *state)
{
	if (state->formula_guru_key &&
	    gnumeric_dialog_raise_if_exists (state->wbcg, state->formula_guru_key)) {
		/* The formula guru is waiting for us.*/
		state->formula_guru_key = NULL;
		dialog_formula_guru (state->wbcg, NULL);
	}

	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	if (state->model != NULL)
		g_object_unref (G_OBJECT (state->model));
	if (state->model_f != NULL)
		g_object_unref (G_OBJECT (state->model_f));
	g_slist_free (state->recent_funcs);
	g_free (state);
}

/**
 * cb_dialog_function_select_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_function_select_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
					  FunctionSelectState *state)
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
cb_dialog_function_select_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
				      FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	GnmFunc const *func;
	GtkTreeSelection *the_selection = gtk_tree_view_get_selection (state->treeview_f);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		WBCGtk *wbcg = state->wbcg;
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);
		dialog_function_write_recent_func (state, func);
		state->formula_guru_key = NULL;
		gtk_widget_destroy (state->dialog);
		dialog_formula_guru (wbcg, func);
		return;
	}

	g_assert_not_reached ();
	gtk_widget_destroy (state->dialog);
	return;
}

static gint
dialog_function_select_by_name (gconstpointer _a, gconstpointer _b)
{
	GnmFunc const * const a = (GnmFunc const * const)_a;
	GnmFunc const * const b = (GnmFunc const * const)_b;

	return strcmp (gnm_func_get_name (a), gnm_func_get_name (b));
}

static void
dialog_function_select_load_tree (FunctionSelectState *state)
{
	int i = 0;
	GtkTreeIter p_iter;
	GnmFuncGroup const * cat;

	gtk_tree_store_clear (state->model);

	gtk_tree_store_append (state->model, &p_iter, NULL);
	gtk_tree_store_set (state->model, &p_iter,
			    CAT_NAME, _("Recently Used"),
			    CATEGORY, NULL,
			    -1);
	gtk_tree_store_append (state->model, &p_iter, NULL);
	gtk_tree_store_set (state->model, &p_iter,
			    CAT_NAME, _("All Functions (long list)"),
			    CATEGORY, GINT_TO_POINTER(-1),
			    -1);

	while ((cat = gnm_func_group_get_nth (i++)) != NULL) {
		gtk_tree_store_append (state->model, &p_iter, NULL);
		gtk_tree_store_set (state->model, &p_iter,
				    CAT_NAME, _(cat->display_name->str),
				    CATEGORY, cat,
				    -1);
	}

}


static void
describe_old_style (GtkTextBuffer *description, GnmFunc const *func)
{
	TokenizedHelp *help = tokenized_help_new (func);
	char const * f_desc = tokenized_help_find (help, "DESCRIPTION");
	char const *f_syntax = tokenized_help_find (help, "SYNTAX");

	char const * cursor;
	GString    * buf = g_string_new (NULL);
	GtkTextIter  start, end;
	GtkTextTag * tag;
	int          syntax_length =  g_utf8_strlen (f_syntax,-1);

	g_string_append (buf, f_syntax);
	g_string_append (buf, "\n\n");
	g_string_append (buf, f_desc);
	gtk_text_buffer_set_text (description, buf->str, -1);

	/* Set the syntax Bold */
	tag = gtk_text_buffer_create_tag (description,
					  NULL,
					  "weight",
					  PANGO_WEIGHT_BOLD,
					  NULL);
	gtk_text_buffer_get_iter_at_offset (description,
					    &start, 0);
	gtk_text_buffer_get_iter_at_offset (description,
					    &end, syntax_length);
	gtk_text_buffer_apply_tag (description, tag,
				   &start, &end);
	syntax_length += 2;

	/* Set the arguments and errors Italic */
	for (cursor = f_desc; *cursor; cursor = g_utf8_next_char (cursor)) {
		int i;
		if (*cursor == '@' || *cursor == '#') {
			int j;

			cursor++;
			for (i = 0;
			     *cursor && !g_unichar_isspace (g_utf8_get_char (cursor));
			     i++)
				cursor = g_utf8_next_char (cursor);

			j = g_utf8_pointer_to_offset (f_desc, cursor);

			if (i > 0)
				cursor = g_utf8_prev_char (cursor);

			tag = gtk_text_buffer_create_tag
				(description,
				 NULL, "style",
				 PANGO_STYLE_ITALIC, NULL);
			gtk_text_buffer_get_iter_at_offset
				(description, &start,
				 j - i + syntax_length);
			gtk_text_buffer_get_iter_at_offset
				(description, &end,
				 j + syntax_length);
			gtk_text_buffer_apply_tag
				(description, tag,
				 &start, &end);
		} else if (*cursor == '\n' &&
			   cursor[1] == '*' &&
			   cursor[2] == ' ') {
			int j = g_utf8_pointer_to_offset (f_desc, cursor);
			char const *p;

			tag = gtk_text_buffer_create_tag
				(description, NULL,
				 "weight", PANGO_WEIGHT_BOLD,
				 NULL);
			gtk_text_buffer_get_iter_at_offset
				(description, &start,
				 j + 1 + syntax_length);
			gtk_text_buffer_get_iter_at_offset
				(description, &end,
				 j + 2 + syntax_length);
			gtk_text_buffer_apply_tag
				(description, tag,
				 &start, &end);

			/* Make notes to look cooler. */
			p = cursor + 2;
			for (i = 0; *p && *p != '\n'; i++)
				p = g_utf8_next_char (p);

			tag = gtk_text_buffer_create_tag
				(description, NULL,
				 "scale", 0.85, NULL);

			gtk_text_buffer_get_iter_at_offset
				(description,
				 &start, j + 1 + syntax_length);
			gtk_text_buffer_get_iter_at_offset
				(description, &end,
				 j + i + 1 + syntax_length);
			gtk_text_buffer_apply_tag
				(description, tag,
				 &start, &end);
		}
	}

	g_string_free (buf, TRUE);
	tokenized_help_destroy (help);
}

#define ADD_LTEXT(text,len) gtk_text_buffer_insert (description, &ti, (text), (len))
#define ADD_TEXT(text) ADD_LTEXT((text),-1)
#define ADD_BOLD_TEXT(text,len) gtk_text_buffer_insert_with_tags (description, &ti, (text), (len), bold, NULL)

static void
describe_new_style (GtkTextBuffer *description, GnmFunc const *func)
{
	GnmFuncHelp const *help;
	GtkTextIter ti;
	GtkTextTag *bold =
		gtk_text_buffer_create_tag
		(description, NULL,
		 "weight", PANGO_WEIGHT_BOLD,
		 NULL);
	gboolean seen_args = FALSE;

	gtk_text_buffer_get_end_iter (description, &ti);

	for (help = func->help; 1; help++) {
		switch (help->type) {
		case GNM_FUNC_HELP_NAME: {
			const char *text = F_(help->text);
			const char *colon = strchr (text, ':');
			if (!colon)
				break;
			ADD_BOLD_TEXT (text, colon - text);
			ADD_TEXT (": ");
			ADD_TEXT (colon + 1);
			ADD_TEXT ("\n\n");
			break;
		}
		case GNM_FUNC_HELP_ARG: {
			const char *text = F_(help->text);
			const char *colon = strchr (text, ':');
			if (!colon)
				break;

			if (!seen_args) {
				seen_args = TRUE;
				ADD_TEXT (_("Arguments:"));
				ADD_TEXT ("\n");
			}

			ADD_BOLD_TEXT (text, colon - text);
			ADD_TEXT (": ");
			ADD_TEXT (colon + 1);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_DESCRIPTION: {
			const char *text = F_(help->text);
			ADD_TEXT ("\n");
			ADD_TEXT (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_SEEALSO: {
			const char *text = help->text;  /* Not translated */

			ADD_TEXT ("\n");
			ADD_TEXT (_("See also: "));

			while (*text) {
				const char *end = strchr (text, ',');
				if (!end) end = text + strlen (text);

				ADD_LTEXT (text, end - text);
				ADD_TEXT (", ");

				text = *end ? end + 1 : end;
			}
			ADD_TEXT ("\n");
		}
		case GNM_FUNC_HELP_END:
			return;
		default:
			break;
		}
	}
}

#undef ADD_TEXT
#undef ADD_LTEXT
#undef ADD_BOLD_TEXT

static void
cb_dialog_function_select_fun_selection_changed (GtkTreeSelection *the_selection,
						 FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	GnmFunc const *func;

	gtk_text_buffer_set_text (state->description, "", 0);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);

		gnm_func_load_if_stub ((GnmFunc *)func);

		if (func->help == NULL)
			gtk_text_buffer_set_text (state->description, "?", -1);
		else if (func->help[0].type == GNM_FUNC_HELP_OLD)
			describe_old_style (state->description, func);
		else
			describe_new_style (state->description, func);
		gtk_widget_set_sensitive (state->ok_button, TRUE);
	} else {
		gtk_widget_set_sensitive (state->ok_button, FALSE);
	}
}

static void
cb_dialog_function_select_cat_selection_changed (GtkTreeSelection *the_selection,
						 FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	GnmFuncGroup const * cat;
	GSList *funcs, *ptr;
	GnmFunc const *func;

	gtk_list_store_clear (state->model_f);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    CATEGORY, &cat,
				    -1);
		if (cat != NULL) {
			if (cat == GINT_TO_POINTER(-1)) {
				int i = 0;
				funcs = NULL;

				while ((cat = gnm_func_group_get_nth (i++)) != NULL)
					funcs = g_slist_concat (funcs,
							g_slist_copy (cat->functions));

				funcs = g_slist_sort (funcs,
						      dialog_function_select_by_name);
			} else
				funcs = g_slist_sort (g_slist_copy (cat->functions),
						      dialog_function_select_by_name);

			for (ptr = funcs; ptr; ptr = ptr->next) {
				func = ptr->data;
				if (!(func->flags & GNM_FUNC_INTERNAL)) {
					gtk_list_store_append (state->model_f, &iter);
					gtk_list_store_set (state->model_f, &iter,
						FUN_NAME, gnm_func_get_name (func),
						FUNCTION, func,
						-1);
				}
			}
			g_slist_free (funcs);
		} else if (cat == NULL) {
			for (ptr = state->recent_funcs; ptr != NULL; ptr = ptr->next) {
				func = ptr->data;
				gtk_list_store_append (state->model_f, &iter);
				gtk_list_store_set (state->model_f, &iter,
					FUN_NAME, gnm_func_get_name (func),
					FUNCTION, func,
					-1);
			}
		} else {
			int i = 0;
			funcs = NULL;

			while ((cat = gnm_func_group_get_nth (i++)) != NULL)
				funcs = g_slist_concat (funcs, g_slist_copy (cat->functions));

			funcs = g_slist_sort (funcs, dialog_function_select_by_name);
		}
	}
}

static void
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
		GNUMERIC_HELP_LINK_FUNCTION_SELECT);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_function_select_destroy);
}

void
dialog_function_select (WBCGtk *wbcg, char const *key)
{
	FunctionSelectState* state;
	GladeXML  *gui;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, FUNCTION_SELECT_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"function-select.glade", NULL, NULL);
        if (gui == NULL)
		return;

	state = g_new (FunctionSelectState, 1);
	state->wbcg  = wbcg;
	state->wb    = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
        state->gui   = gui;
        state->dialog = glade_xml_get_widget (state->gui, "selection_dialog");
	state->formula_guru_key = key;
        state->recent_funcs = NULL;

	dialog_function_select_init (state);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       FUNCTION_SELECT_KEY);

	gtk_widget_show_all (state->dialog);
}
