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
#include <wbc-gtk.h>
#include <application.h>
#include <gnumeric-gconf.h>

#include <gsf/gsf-impl-utils.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
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
	GtkTextView   *description_view;

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
	FUNCTION_CAT,
	NUM_COLUMNS
};

static void
dialog_function_load_recent_funcs (FunctionSelectState *state)
{
	GSList const *recent_funcs;

	for (recent_funcs = gnm_conf_get_functionselector_recentfunctions ();
	     recent_funcs;
	     recent_funcs = recent_funcs->next) {
		char const *name = recent_funcs->data;
		GnmFunc *fd;

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
	guint ulimit = gnm_conf_get_functionselector_num_of_recent ();

	state->recent_funcs = g_slist_remove (state->recent_funcs, (gpointer) fd);
	state->recent_funcs = g_slist_prepend (state->recent_funcs, (gpointer) fd);

	while (g_slist_length (state->recent_funcs) > ulimit)
		state->recent_funcs = g_slist_remove (state->recent_funcs,
						      g_slist_last (state->recent_funcs)->data);

	for (rec_funcs = state->recent_funcs; rec_funcs; rec_funcs = rec_funcs->next) {
		gconf_value_list = g_slist_prepend
			(gconf_value_list, g_strdup (gnm_func_get_name (rec_funcs->data)));
	}
	gnm_conf_set_functionselector_recentfunctions (gconf_value_list);
	go_slist_free_custom (gconf_value_list, g_free);
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

static GtkTextTag *
make_link (GtkTextBuffer *description, const char *name,
	   GCallback cb, gpointer user)
{
	GtkTextTag *link =
		gtk_text_tag_table_lookup 
		(gtk_text_buffer_get_tag_table (description), name);

	if (!link) {
		link = gtk_text_buffer_create_tag
			(description, name,
			 "underline", PANGO_UNDERLINE_SINGLE,
			 "foreground", "#0000ff",
			 NULL);

		if (cb)
			g_signal_connect (link, "event", cb, user);
	}

	return link;
}

static gboolean
cb_link_event (GtkTextTag *link, GObject *trigger,
	       GdkEvent *event, GtkTextIter *iter,
	       gpointer user)
{
	switch (event->type) {
	case GDK_BUTTON_PRESS:
	case GDK_2BUTTON_PRESS:
	case GDK_3BUTTON_PRESS: {
		GdkEventButton *eb = (GdkEventButton *)event;
		const char *uri = g_object_get_data (G_OBJECT (link), "uri");
		GError *error = NULL;

		if (eb->button != 1)
			break;
		if (event->type != GDK_BUTTON_PRESS)
			return TRUE;

		error = go_gtk_url_show (uri, gdk_event_get_screen (event));
		if (error) {
			g_printerr ("Failed to show %s\n(%s)\n",
				    uri,
				    error->message);
			g_error_free (error);
		}

		return TRUE;
	}

#if 0
	case GDK_ENTER_NOTIFY:
	case GDK_LEAVE_NOTIFY:
		/* We aren't getting these. */
#endif
	default:
		break;
	}

	return FALSE;
}


#define ADD_LTEXT(text,len) gtk_text_buffer_insert (description, &ti, (text), (len))
#define ADD_TEXT(text) ADD_LTEXT((text),-1)
#define ADD_BOLD_TEXT(text,len) gtk_text_buffer_insert_with_tags (description, &ti, (text), (len), bold, NULL)
#define ADD_LINK_TEXT(text,len) gtk_text_buffer_insert_with_tags (description, &ti, (text), (len), link, NULL)
#define ADD_TEXT_WITH_ARGS(text) { const char *t = text; while (*t) { const char *at = strstr (t, "@{"); \
			if (at == NULL) { ADD_TEXT(t); break;} ADD_LTEXT(t, at - t); t = at + 2; at = strchr (t,'}'); \
			if (at != NULL) { ADD_BOLD_TEXT(t, at - t); t = at + 1; } else {ADD_TEXT (t); break;}}} 

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
	gboolean seen_examples = FALSE;
	gboolean seen_extref = FALSE;

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
			ADD_TEXT_WITH_ARGS (colon + 1);
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
			ADD_TEXT_WITH_ARGS (colon + 1);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_DESCRIPTION: {
			const char *text = F_(help->text);
			ADD_TEXT ("\n");
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_NOTE: {
			const char *text = F_(help->text);
			ADD_TEXT ("\n");
			ADD_TEXT (_("Note: "));
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_EXAMPLES: {
			const char *text = help->text;

			if (!seen_examples) {
				seen_examples = TRUE;
				ADD_TEXT ("\n");
				ADD_TEXT (_("Examples:"));
				ADD_TEXT ("\n");
			}

			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_SEEALSO: {
			const char *text = help->text;  /* Not translated */
			const char *pre = _("See also: ");
			GtkTextTag *link =
				make_link (description, "LINK", NULL, NULL);

			ADD_TEXT ("\n");

			while (*text) {
				const char *end = strchr (text, ',');
				if (!end) end = text + strlen (text);

				ADD_TEXT (pre);
				ADD_LINK_TEXT (text, end - text);

				text = *end ? end + 1 : end;

				pre = _(", ");
			}
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_END:
			return;
		case GNM_FUNC_HELP_EXTREF: {
			GtkTextTag *link;
			char *uri, *tagname;
			const char *text;

			/*
			 * We put in just one link and let the web page handle
			 * the rest.  In particular, we do not even look at
			 * what the help->text is here.
			 */
			if (seen_extref)
				break;

			uri = g_strdup_printf ("http://projects.gnome.org/gnumeric/func-doc.shtml?%s", func->name);

			tagname = g_strdup_printf ("EXTLINK-%s", func->name);
			link = make_link
				(description, tagname,
				 G_CALLBACK (cb_link_event),
				 NULL);

			g_object_set_data_full (G_OBJECT (link),
						"uri", uri,
						g_free);

			ADD_TEXT (_("Further information: "));

			text = _("online descriptions");
			ADD_LINK_TEXT (text, strlen (text));

			ADD_TEXT (".\n");

			seen_extref = TRUE;
			break;
		}
		case GNM_FUNC_HELP_EXCEL: {
			const char *text = F_(help->text);
			ADD_TEXT ("\n");
			ADD_TEXT (_("Microsoft Excel: "));
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_ODF: {
			const char *text = F_(help->text);
			ADD_TEXT ("\n");
			ADD_TEXT (_("ODF (OpenFormula): "));
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		default:
			break;
		}
	}
}

#undef ADD_TEXT_WITH_ARGS
#undef ADD_TEXT
#undef ADD_LTEXT
#undef ADD_BOLD_TEXT
#undef ADD_LINK_TEXT

typedef struct {
	GnmFuncGroup const * cat;
	GnmFunc    *fd;
	FunctionSelectState *state;
	GtkTreeIter *iter;
} dialog_function_select_find_func_t;

static gboolean
dialog_function_select_search_cat_func (GtkTreeModel *model,
					GtkTreePath *path,
					GtkTreeIter *iter,
					gpointer dt)
{
	GnmFuncGroup const * cat;
	dialog_function_select_find_func_t *data = dt;

	gtk_tree_model_get (model, iter,
			    CATEGORY, &cat,
			    -1);
	if (cat == data->cat) {
		data->iter = gtk_tree_iter_copy (iter);
		return TRUE;
	}
	return FALSE;
}

static gboolean
dialog_function_select_search_func (GtkTreeModel *model,
				    GtkTreePath *path,
				    GtkTreeIter *iter,
				    gpointer dt)
{
	GnmFunc* fd;
	dialog_function_select_find_func_t *data = dt;

	gtk_tree_model_get (model, iter,
			    FUNCTION, &fd,
			    -1);
	if (fd == data->fd) {
		data->iter = gtk_tree_iter_copy (iter);
		return TRUE;
	}
	return FALSE;
}

static void
dialog_function_select_find_func (FunctionSelectState *state, char* name)
{
	GnmFunc    *fd;

	if (name == NULL)
		return;
	
	fd = gnm_func_lookup (name, state->wb);
	if (fd != NULL) {
		dialog_function_select_find_func_t data;

		data.cat = fd->fn_group;
		data.state = state;
		data.iter = NULL;
		
		gtk_tree_model_foreach (GTK_TREE_MODEL (state->model),
					dialog_function_select_search_cat_func,
					&data);
		if (data.iter != NULL) {
			GtkTreeSelection *selection = gtk_tree_view_get_selection 
				(state->treeview);
			GtkTreePath *path;

			gtk_tree_selection_select_iter (selection,
                                                        data.iter);
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->model),
                                                        data.iter);
			gtk_tree_view_scroll_to_cell (state->treeview, path,
						      NULL, FALSE, 0., 0.);
			gtk_tree_path_free (path);
			gtk_tree_iter_free (data.iter);
			data.iter = NULL;
			data.fd = fd;

			gtk_tree_model_foreach (GTK_TREE_MODEL (state->model_f),
					dialog_function_select_search_func,
					&data);
			if (data.iter != NULL) {
				selection = gtk_tree_view_get_selection 
					(state->treeview_f);
				
				gtk_tree_selection_select_iter (selection,
								data.iter);
				path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->model_f),
								data.iter);
				gtk_tree_view_scroll_to_cell (state->treeview_f, path,
							      NULL, FALSE, 0., 0.);
				gtk_tree_path_free (path);
				gtk_tree_iter_free (data.iter);
				data.iter = NULL;
			} else
				g_warning ("Function %s was not found in its category", name);

		} else
			g_warning ("Category of function %s was not found", name);
	} else
		g_warning ("Function %s was not found", name);
} 

typedef struct {
	FunctionSelectState *state;
	gchar * name;
} cb_dialog_function_select_idle_handler_t;

static gboolean
cb_dialog_function_select_idle_handler (gpointer dt)
{
	cb_dialog_function_select_idle_handler_t *data = dt;

	dialog_function_select_find_func (data->state, data->name);

	g_free (data->name);
	g_free (data);

	return FALSE;
}

static void                
cb_description_clicked (GtkTextBuffer *textbuffer,
			GtkTextIter   *location,
			GtkTextMark   *mark,
			FunctionSelectState *state) 
{
	const char * mark_name;
	GtkTextTag *link;
	GtkTextIter   *start;
	GtkTextIter   *end;
	cb_dialog_function_select_idle_handler_t *data;

	if ((mark == NULL) || ((mark_name = gtk_text_mark_get_name (mark)) == NULL) 
	    || (strcmp(mark_name, "selection_bound") != 0))
		return;

	link = gtk_text_tag_table_lookup 
		(gtk_text_buffer_get_tag_table (textbuffer), "LINK");

	if ((link == NULL) || !gtk_text_iter_has_tag (location, link))
		return;

	start = gtk_text_iter_copy (location);
	end = gtk_text_iter_copy (location);

	if (!gtk_text_iter_begins_tag (start, link))
		gtk_text_iter_backward_to_tag_toggle (start, link);
	if (!gtk_text_iter_ends_tag (end, link))
		gtk_text_iter_forward_to_tag_toggle (end, link);

	data = g_new(cb_dialog_function_select_idle_handler_t, 1);

	data->name = gtk_text_buffer_get_text (textbuffer, start, end, FALSE);
	gtk_text_iter_free (start);
	gtk_text_iter_free (end);
	data->state = state;

	g_idle_add_full (G_PRIORITY_HIGH_IDLE, cb_dialog_function_select_idle_handler,
			 data, NULL);
}

static void
cb_dialog_function_select_fun_selection_changed (GtkTreeSelection *the_selection,
						 FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	GnmFunc const *func;
	GtkTextBuffer *description;
	GtkTextMark *mark;

	description =  gtk_text_view_get_buffer (state->description_view);

	mark = gtk_text_buffer_get_mark (description, "start-mark");
	gtk_text_view_scroll_to_mark (state->description_view, mark,
				      0.1, TRUE, 0.0, 0.0);
	gtk_text_buffer_set_text (description, "", 0);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);

		gnm_func_load_if_stub ((GnmFunc *)func);

		if (func->help == NULL)
			gtk_text_buffer_set_text (description, "?", -1);
		else if (func->help[0].type == GNM_FUNC_HELP_OLD)
			describe_old_style (description, func);
		else
			describe_new_style (description, func);

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
	GSList *funcs = NULL, *ptr;
	GnmFunc const *func;
	gboolean cat_specific = FALSE;

	gtk_list_store_clear (state->model_f);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    CATEGORY, &cat,
				    -1);
		if (cat != NULL) {
			if (cat == GINT_TO_POINTER(-1)) {
				/*  Show all functions */
				int i = 0;

				while ((cat = gnm_func_group_get_nth (i++)) != NULL)
					funcs = g_slist_concat (funcs,
							g_slist_copy (cat->functions));

				funcs = g_slist_sort (funcs,
						      dialog_function_select_by_name);
			} else {
				/* Show category cat */
				funcs = g_slist_sort (g_slist_copy (cat->functions),
						      dialog_function_select_by_name);
				cat_specific = TRUE;
			}
		} else
			/* Show recent functions */
			funcs = state->recent_funcs;

		for (ptr = funcs; ptr; ptr = ptr->next) {
			func = ptr->data;
			if (!(func->flags & GNM_FUNC_INTERNAL)) {
				gtk_list_store_append (state->model_f, &iter);
				gtk_list_store_set (state->model_f, &iter,
						    FUN_NAME, gnm_func_get_name (func),
						    FUNCTION_CAT, 
						    cat_specific ? "" : _(func->fn_group->display_name->str),
						    FUNCTION, func,
						    -1);
			}
		}

		gtk_tree_view_scroll_to_point (state->treeview_f, 0, 0);

		if (funcs != state->recent_funcs)
			g_slist_free (funcs);
	}
}

static void
dialog_function_select_init (FunctionSelectState *state)
{
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTextIter where;
	GtkTextBuffer *description;

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
	state->model_f = gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING);
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
	column = gtk_tree_view_column_new_with_attributes (_("Name"),
							   gtk_cell_renderer_text_new (),
							   "text", FUNCTION_CAT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, FUN_NAME);
	gtk_tree_view_append_column (state->treeview_f, column);

	gtk_tree_view_set_headers_visible (state->treeview_f, FALSE);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview_f));
	/* Finished set-up of second treeview */

	gtk_paned_set_position (GTK_PANED (glade_xml_get_widget
					   (state->gui, "vpaned1")), 300);

	state->description_view = GTK_TEXT_VIEW (glade_xml_get_widget (state->gui, "description"));
	description = gtk_text_view_get_buffer (state->description_view);
	gtk_text_buffer_get_start_iter (description, &where);
	gtk_text_buffer_create_mark (description, "start-mark", &where, TRUE);
	
	g_signal_connect_after (G_OBJECT (description),
		"mark-set",
		G_CALLBACK (cb_description_clicked), state);	

	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	gtk_widget_set_sensitive (state->ok_button, FALSE);
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_function_select_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_dialog_function_select_cancel_clicked), state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), 
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);
	
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
