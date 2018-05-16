/*
 * dialog-recent.c:
 *   Dialog for selecting from recently used files.
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <wbc-gtk.h>
#include <gui-file.h>
#include <gui-util.h>
#include <string.h>
#include <goffice/goffice.h>

#define RECENT_KEY "recent-dialog"

enum {
	RECENT_COL_INFO
};

/* ------------------------------------------------------------------------- */

static void
cb_selected (GtkTreeModel *model,
             G_GNUC_UNUSED GtkTreePath *path,
             GtkTreeIter *iter,
             WBCGtk *wbcg)
{
	char *uri = NULL;
	GtkRecentInfo *info;
	gtk_tree_model_get (model, iter, RECENT_COL_INFO, &info, -1);
	uri = g_strdup (gtk_recent_info_get_uri (info));
	gtk_recent_info_unref (info);
	if (uri) {
		gui_file_read (wbcg, uri, NULL, NULL);
		g_free (uri);
	}
}

static void
cb_response (GtkWidget *dialog,
	     gint response_id,
	     WBCGtk *wbcg)
{
	GtkBuilder *gui = g_object_get_data (G_OBJECT (dialog), "gui");
	GtkTreeView *tv = GTK_TREE_VIEW (gtk_builder_get_object (gui, "docs_treeview"));
	GtkTreeSelection *tsel = gtk_tree_view_get_selection (tv);

	switch (response_id) {
	case GTK_RESPONSE_OK:
		gtk_tree_selection_selected_foreach (tsel, (GtkTreeSelectionForeachFunc) cb_selected, wbcg);
		gtk_widget_destroy (dialog);
		break;

	default:
		gtk_widget_destroy (dialog);
	}
}

static void
cb_destroy (GtkDialog *dialog)
{
	/* Trigger tear-down.  */
	g_object_set_data (G_OBJECT (dialog), "gui", NULL);
}


static gboolean
cb_key_press (GtkWidget *widget, GdkEventKey *event)
{
  GtkTreeView *tree_view = (GtkTreeView *) widget;

  switch (event->keyval) {
  case GDK_KEY_KP_Delete:
  case GDK_KEY_Delete: {
	GtkTreeSelection *tsel = gtk_tree_view_get_selection (tree_view);
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (tsel, &model, &iter)) {
		GtkRecentInfo *info;
		const char *uri;
		GtkRecentManager *manager = gtk_recent_manager_get_default ();

		gtk_tree_model_get (model, &iter, RECENT_COL_INFO, &info, -1);
		uri = gtk_recent_info_get_uri (info);

		gtk_recent_manager_remove_item (manager, uri, NULL);
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
		gtk_recent_info_unref (info);
	}
	return TRUE;
  }

  default:
	  break;
  }

  return FALSE;
}

static gboolean
cb_button_press (GtkWidget *w, GdkEventButton *ev, WBCGtk *wbcg)
{
	if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
		GtkWidget *dlg = gtk_widget_get_toplevel (w);
		if (GTK_IS_DIALOG (dlg)) {
			cb_response (dlg, GTK_RESPONSE_OK, wbcg);
			return TRUE;
		}
	}
	return FALSE;
}

static void
url_renderer_func (GtkTreeViewColumn *tree_column,
		   GtkCellRenderer   *cell,
		   GtkTreeModel      *model,
		   GtkTreeIter       *iter,
		   gpointer           user_data)
{
	GtkRecentInfo *ri = NULL;
	const char *uri;
	char *markup, *shortname, *filename, *longname;

	gtk_tree_model_get (model, iter, RECENT_COL_INFO, &ri, -1);

	uri = gtk_recent_info_get_uri (ri);
	filename = go_filename_from_uri (uri);
	if (filename) {
		shortname = g_filename_display_basename (filename);
	} else {
		shortname = g_filename_display_basename (uri);
	}

	if (filename) {
		longname = g_strdup (filename);
	} else {
		char *duri = g_uri_unescape_string (uri, NULL);
		longname = duri
			? g_filename_display_name (duri)
			: g_strdup (uri);
		g_free (duri);
	}

	markup = g_markup_printf_escaped (_("<b>%s</b>\n"
					    "<small>Location: %s</small>"),
					  shortname,
					  longname);
	g_object_set (cell, "markup", markup, NULL);
	g_free (markup);
	g_free (shortname);
	g_free (longname);
	g_free (filename);
	gtk_recent_info_unref (ri);
}

static void
age_renderer_func (GtkTreeViewColumn *tree_column,
		   GtkCellRenderer   *cell,
		   GtkTreeModel      *model,
		   GtkTreeIter       *iter,
		   gpointer           user_data)
{
	GtkRecentInfo *ri = NULL;
	GDateTime *now = user_data;
	GDateTime *last_used;
	GTimeSpan age;
	char *text;
	const char *date_format;
	const char *p;

	gtk_tree_model_get (model, iter, RECENT_COL_INFO, &ri, -1);
	last_used = g_date_time_new_from_unix_local (gtk_recent_info_get_modified (ri));
	gtk_recent_info_unref (ri);

	age = g_date_time_difference (now, last_used);
	if (age < G_TIME_SPAN_DAY &&
	    g_date_time_get_day_of_month (now) == g_date_time_get_day_of_month (last_used)) {
		if (go_locale_24h ())
			/*
			 * xgettext: This is a time format for
			 * g_date_time_format used in locales that use a
			 * 24 hour clock.  You probably do not need to change
			 * this.  The default will show things like "09:50"
			 * and "21:50".
			 */
			date_format = _("%H:%M");
		else
			/*
			 * xgettext: This is a time format for
			 * g_date_time_format used in locales that use
			 * a 12 hour clock. You probably do not need
			 * to change this.  The default will show
			 * things like " 9:50 am" and " 9:50 pm".
			 */
			date_format = _("%l:%M %P");
	} else {
		date_format = "%x";
	}

	p = text = g_date_time_format (last_used, date_format);
	while (g_ascii_isspace (*p))
		p++;
	g_object_set (cell, "text", p, "xalign", 0.5, NULL);
	g_free (text);

	g_date_time_unref (last_used);
}

static gint
by_age_uri (gconstpointer a_, gconstpointer b_)
{
	GtkRecentInfo *a = (gpointer)a_;
	GtkRecentInfo *b = (gpointer)b_;
	int res;

	res = gtk_recent_info_get_modified (b) - gtk_recent_info_get_modified (a);
	if (res) return res;

	res = strcmp (gtk_recent_info_get_uri (a), gtk_recent_info_get_uri (b));
	return res;
}


static void
populate_recent_model (GtkBuilder *gui)
{
	GtkListStore *list = GTK_LIST_STORE (gtk_builder_get_object (gui, "recent_model"));
	gboolean existing_only = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (gtk_builder_get_object (gui, "existing_only_button")));
	gboolean gnumeric_only = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (gtk_builder_get_object (gui, "gnumeric_only_button")));
	GtkRecentManager *manager = gtk_recent_manager_get_default ();
	GList *docs, *l;

	gtk_list_store_clear (list);

	docs = gtk_recent_manager_get_items (manager);
	docs = g_list_sort (docs, by_age_uri);
	for (l = docs; l; l = l->next) {
		GtkRecentInfo *ri = l->data;
		GtkTreeIter iter;

		if (existing_only) {
			gboolean exists = gtk_recent_info_is_local (ri)
				? gtk_recent_info_exists (ri)
				: TRUE;  /* Just assume so */
			if (!exists)
				continue;
		}

		if (gnumeric_only) {
			if (!gtk_recent_info_has_application (ri, g_get_application_name ()))
				continue;
		}

		gtk_list_store_append (list, &iter);
		gtk_list_store_set (list, &iter, RECENT_COL_INFO, ri, -1);
	}
	g_list_free_full (docs, (GDestroyNotify)gtk_recent_info_unref);
}


void
dialog_recent_used (WBCGtk *wbcg)
{
	GtkBuilder *gui;
	GtkDialog *dialog;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, RECENT_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/recent.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (go_gtk_builder_get_widget (gui, "recent_dialog"));

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (cb_response), wbcg);


	{
		GtkWidget *w;
		int width, height, vsep;
		PangoLayout *layout;
		GtkTreeView *tv;
		GtkTreeSelection *tsel;

		w = GTK_WIDGET (wbcg_toplevel (wbcg));
		layout = gtk_widget_create_pango_layout (w, "Mg19");

		w = go_gtk_builder_get_widget (gui, "docs_treeview");
		gtk_widget_style_get (w, "vertical_separator", &vsep, NULL);
		g_signal_connect (w, "key-press-event",
				  G_CALLBACK (cb_key_press),
				  NULL);
		g_signal_connect (w, "button-press-event",
				  G_CALLBACK (cb_button_press),
				  wbcg);

		pango_layout_get_pixel_size (layout, &width, &height);
		gtk_widget_set_size_request (go_gtk_builder_get_widget (gui, "docs_scrolledwindow"),
					     width * 60 / 4,
					     (2 * height + vsep) * (5 + 1));
		g_object_unref (layout);
		tv = GTK_TREE_VIEW (gtk_builder_get_object (gui, "docs_treeview"));
		tsel = gtk_tree_view_get_selection (tv);
		gtk_tree_selection_set_mode (tsel, GTK_SELECTION_MULTIPLE);
	}

	g_signal_connect_swapped (gtk_builder_get_object (gui, "existing_only_button"),
				  "toggled", G_CALLBACK (populate_recent_model), gui);
	g_signal_connect_swapped (gtk_builder_get_object (gui, "gnumeric_only_button"),
				  "toggled", G_CALLBACK (populate_recent_model), gui);

	populate_recent_model (gui);

	gtk_tree_view_column_set_cell_data_func
		(GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (gui, "url_column")),
		 GTK_CELL_RENDERER (gtk_builder_get_object (gui, "url_renderer")),
		 url_renderer_func,
		 NULL,
		 NULL);

	gtk_tree_view_column_set_cell_data_func
		(GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (gui, "age_column")),
		 GTK_CELL_RENDERER (gtk_builder_get_object (gui, "age_renderer")),
		 age_renderer_func,
		 g_date_time_new_now_local (),
		 (GDestroyNotify)g_date_time_unref);

	/* ---------------------------------------- */

	g_object_set_data_full (G_OBJECT (dialog), "gui", gui, g_object_unref);
	g_signal_connect (dialog, "destroy", G_CALLBACK (cb_destroy), NULL);
	go_gtk_nonmodal_dialog (wbcg_toplevel (wbcg), GTK_WINDOW (dialog));
	gtk_widget_show_all (GTK_WIDGET (dialog));
}

/* ------------------------------------------------------------------------- */
