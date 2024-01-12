/*
 * dialog-quit.c:
 *   Dialog for quit (selecting what to save)
 *
 * Author:
 *   Morten Welinder (terra@gnome.org)
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
#include <gui-file.h>
#include <gui-util.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <gui-clipboard.h>
#include <application.h>

#include <goffice/goffice.h>

#include <glib/gi18n-lib.h>

#include <string.h>

enum {
	RESPONSE_ALL = 1,
	RESPONSE_NONE = 2
};

enum {
	QUIT_COL_CHECK,
	QUIT_COL_DOC
};

/* ------------------------------------------------------------------------- */

static void
url_renderer_func (GtkTreeViewColumn *tree_column,
		   GtkCellRenderer   *cell,
		   GtkTreeModel      *model,
		   GtkTreeIter       *iter,
		   gpointer           user_data)
{
	GODoc *doc = NULL;
	const char *uri;
	char *markup, *shortname, *filename, *longname, *duri;

	gtk_tree_model_get (model, iter,
			    QUIT_COL_DOC, &doc,
			    -1);
	g_return_if_fail (GO_IS_DOC (doc));

	uri = go_doc_get_uri (doc);
	filename = go_filename_from_uri (uri);
	if (filename) {
		shortname = g_filename_display_basename (filename);
	} else {
		shortname = g_filename_display_basename (uri);
	}

	duri = g_uri_unescape_string (uri, NULL);
	longname = duri
		? g_filename_display_name (duri)
		: g_strdup (uri);

	markup = g_markup_printf_escaped (_("<b>%s</b>\n"
					    "<small>Location: %s</small>"),
					  shortname,
					  longname);
	g_object_set (cell, "markup", markup, NULL);
	g_free (markup);
	g_free (shortname);
	g_free (longname);
	g_free (duri);
	g_free (filename);
	g_object_unref (doc);
}

static void
age_renderer_func (GtkTreeViewColumn *tree_column,
		   GtkCellRenderer   *cell,
		   GtkTreeModel      *model,
		   GtkTreeIter       *iter,
		   gpointer           user_data)
{
	GODoc *doc = NULL;

	gtk_tree_model_get (model, iter,
			    QUIT_COL_DOC, &doc,
			    -1);
	g_return_if_fail (GO_IS_DOC (doc));

	if (go_doc_is_dirty (doc)) {
		time_t quitting_time = GPOINTER_TO_SIZE
			(g_object_get_data (G_OBJECT (tree_column),
					    "quitting_time"));
		int age = quitting_time -
			go_doc_get_dirty_time (doc) / 1000000;
		char *agestr;
		if (age < 0)
			agestr = g_strdup (_("unknown"));
		else if (age < 60)
			agestr = g_strdup_printf
				(ngettext ("%d second", "%d seconds", age),
				 age);
		else if (age < 60 * 60) {
			int mins = age / 60;
			agestr = g_strdup_printf
				(ngettext ("%d minute", "%d minutes", mins),
				 mins);
		} else
			agestr = g_strdup (_("a long time"));

		g_object_set (cell, "text", agestr, NULL);
		g_free (agestr);
	} else {
		/* What are we doing here? */
		g_object_set (cell, "text", "", NULL);
	}
	g_object_unref (doc);
}

static gboolean
foreach_is_file_set (GtkTreeModel *model, GtkTreePath *path,
		     GtkTreeIter *iter, gboolean *data)
{
	gboolean value;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    QUIT_COL_CHECK, &value, -1);

	*data = value;

	return value;
}

static gboolean
files_set (GtkTreeModel *model)
{
	gboolean files_set_state = FALSE;

	gtk_tree_model_foreach (GTK_TREE_MODEL (model),
				(GtkTreeModelForeachFunc) foreach_is_file_set,
				&files_set_state);

	return files_set_state;
}

static void
cb_toggled_save (GtkCellRendererToggle *cell,
		 gchar                 *path_string,
		 gpointer               data)
{
	GtkTreeModel *model = GTK_TREE_MODEL (data);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);

	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gboolean value;
		gtk_tree_model_get (model, &iter,
				    QUIT_COL_CHECK, &value, -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    QUIT_COL_CHECK, !value, -1);
	} else {
		g_warning ("Did not get a valid iterator");
	}

	gtk_tree_path_free (path);
}

static void
set_all (GtkTreeModel *model, gboolean value)
{
	GtkTreeIter iter;
	gboolean ok = gtk_tree_model_get_iter_first (model, &iter);

	while (ok) {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    QUIT_COL_CHECK, value, -1);
		ok = gtk_tree_model_iter_next (model, &iter);
	}
}

static void
cb_select_all (G_GNUC_UNUSED GtkWidget *button,
	       GtkTreeModel *model)
{
	set_all (model, TRUE);
}

static void
cb_clear_all (G_GNUC_UNUSED GtkWidget *button,
	      GtkTreeModel *model)
{
	set_all (model, FALSE);
}

static void
cb_list_row_changed_save_sensitivity (GtkListStore *list, GtkTreePath *path_string,
				      GtkTreeIter *iter, GtkWidget *widget)
{
	GtkTreeModel *model = GTK_TREE_MODEL (list);

	if (files_set (model) == TRUE)
		gtk_widget_set_sensitive (GTK_WIDGET (widget), TRUE);
	else
		gtk_widget_set_sensitive (GTK_WIDGET (widget), FALSE);
}

static void
cb_list_row_changed_discard_sensitivity (GtkListStore *list,
					 GtkTreePath *path_string,
					 GtkTreeIter *iter,
					 GtkWidget *widget)
{
	gtk_widget_set_sensitive (GTK_WIDGET (widget),
				  !files_set (GTK_TREE_MODEL (list)));
}

static gboolean
show_quit_dialog (GList *dirty, WBCGtk *wbcg)
{
	GtkBuilder *gui;
	GtkDialog *dialog;
	gboolean multiple = (dirty->next != NULL);
	GObject *model;
	GtkWidget *save_selected_button;
	GtkCellRenderer *save_renderer;
	GList *l;
	int res;
	gboolean quit;
	GObject *age_column;
	time_t quitting_time = g_get_real_time () / 1000000;

	gui = gnm_gtk_builder_load ("res:ui/quit.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return FALSE;

	dialog = GTK_DIALOG (go_gtk_builder_get_widget (gui, "quit_dialog"));
	model = gtk_builder_get_object (gui, "quit_model");
	save_selected_button = go_gtk_builder_get_widget (gui, "save_selected_button");
	save_renderer = GTK_CELL_RENDERER (gtk_builder_get_object (gui, "save_renderer"));

	if (multiple) {
		GObject *model = gtk_builder_get_object (gui, "quit_model");
		g_signal_connect (model,
				  "row-changed",
				  G_CALLBACK (cb_list_row_changed_discard_sensitivity),
				  gtk_builder_get_object (gui, "discard_all_button"));

		g_signal_connect (model,
				  "row-changed",
				  G_CALLBACK (cb_list_row_changed_save_sensitivity),
				  save_selected_button);

		gtk_widget_destroy (go_gtk_builder_get_widget (gui, "save_button"));

		g_signal_connect (gtk_builder_get_object (gui, "select_all_button"),
				  "clicked", G_CALLBACK (cb_select_all),
				  model);
		g_signal_connect (gtk_builder_get_object (gui, "clear_all_button"),
				  "clicked", G_CALLBACK (cb_clear_all),
				  model);

		g_signal_connect (G_OBJECT (save_renderer),
				  "toggled",
				  G_CALLBACK (cb_toggled_save), model);

	} else {
		gtk_tree_view_column_set_visible (GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (gui, "save_column")),
						  FALSE);
		gtk_widget_destroy (save_selected_button);
		gtk_widget_destroy (go_gtk_builder_get_widget (gui, "selection_box"));
	}

	gtk_tree_view_column_set_cell_data_func
		(GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (gui, "url_column")),
		 GTK_CELL_RENDERER (gtk_builder_get_object (gui, "url_renderer")),
		 url_renderer_func,
		 NULL,
		 NULL);

	age_column = gtk_builder_get_object (gui, "age_column");
	g_object_set_data (age_column, "quitting_time",
			   GSIZE_TO_POINTER (quitting_time));
	gtk_tree_view_column_set_cell_data_func
		(GTK_TREE_VIEW_COLUMN (age_column),
		 GTK_CELL_RENDERER (gtk_builder_get_object (gui, "age_renderer")),
		 age_renderer_func,
		 NULL,
		 NULL);

	gtk_dialog_set_default_response (dialog, GTK_RESPONSE_OK);

	/* ---------------------------------------- */
	/* Size the dialog.  */

	{
		GtkWidget *w = GTK_WIDGET (wbcg_toplevel (wbcg));
		int width, height, vsep;
		PangoLayout *layout =
			gtk_widget_create_pango_layout (w, "Mg19");

		gtk_widget_style_get (go_gtk_builder_get_widget (gui, "docs_treeview"),
				      "vertical_separator", &vsep,
				      NULL);

		pango_layout_get_pixel_size (layout, &width, &height);
		gtk_widget_set_size_request (go_gtk_builder_get_widget (gui, "docs_scrolledwindow"),
					     width * 60 / 4,
					     (2 * height + vsep) * (4 + 1));
		g_object_unref (layout);
	}

	/* ---------------------------------------- */
	/* Populate the model.  */

	for (l = dirty; l; l = l->next) {
		GODoc *doc = l->data;
		GtkTreeIter iter;
		GtkListStore *list = GTK_LIST_STORE (model);

		gtk_list_store_append (list, &iter);
		gtk_list_store_set (list,
				    &iter,
				    QUIT_COL_CHECK, TRUE,
				    QUIT_COL_DOC, doc,
				    -1);
	}

	/* ---------------------------------------- */

	atk_object_set_role (gtk_widget_get_accessible (GTK_WIDGET (dialog)),
			     ATK_ROLE_ALERT);

	res = go_gtk_dialog_run (dialog, wbcg_toplevel (wbcg));
	switch (res) {
	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		quit = FALSE;
		break;

	case GTK_RESPONSE_NO:
		quit = TRUE;
		break;

	default: {
		GtkTreeModel *tmodel = GTK_TREE_MODEL (model);
		GtkTreeIter iter;
		gboolean ok = gtk_tree_model_get_iter_first (tmodel, &iter);

		g_return_val_if_fail (ok, FALSE);
		quit = TRUE;
		do {
			gboolean save = TRUE;
			GODoc *doc = NULL;

			gtk_tree_model_get (tmodel, &iter,
					    QUIT_COL_CHECK, &save,
					    QUIT_COL_DOC, &doc,
					    -1);
			if (save) {
				gboolean ok;
				Workbook *wb = WORKBOOK (doc);
				WBCGtk *wbcg2 = wbcg_find_for_workbook (wb, wbcg, NULL, NULL);

				ok = wbcg2 && gui_file_save (wbcg2, wb_control_view (GNM_WBC (wbcg2)));
				if (!ok)
					quit = FALSE;
			}
			g_object_unref (doc);

			ok = gtk_tree_model_iter_next (tmodel, &iter);
		} while (ok);
		break;
	}
	}

	g_object_unref (gui);

	return quit;
}

/* ------------------------------------------------------------------------- */

static gint
doc_order (gconstpointer a_, gconstpointer b_)
{
	GODoc *a = (GODoc *)a_;
	GODoc *b = (GODoc *)b_;

	/* Primitive, but will work for now.  */
	return go_str_compare (go_doc_get_uri (a), go_doc_get_uri (b));
}

void
dialog_quit (WBCGtk *wbcg)
{
	GList *l, *dirty = NULL;
	gboolean quit;

	for (l = gnm_app_workbook_list (); l; l = l->next) {
		GODoc *doc = l->data;
		if (go_doc_is_dirty (GO_DOC (doc)))
			dirty = g_list_prepend (dirty, doc);
	}

	if (dirty) {
		dirty = g_list_sort (dirty, doc_order);
		quit = show_quit_dialog (dirty, wbcg);
		g_list_free (dirty);
		if (!quit)
			return;
	}

	l = g_list_copy (gnm_app_workbook_list ());
	while (l) {
		Workbook *wb = l->data;
		l = g_list_remove (l, wb);
		go_doc_set_dirty (GO_DOC (wb), FALSE);

		gnm_x_store_clipboard_if_needed (wb);

		/* This is how we kill it?  Ugh!  */
		g_object_unref (wb);
	}
}

/* ------------------------------------------------------------------------- */
