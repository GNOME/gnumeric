/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-preferences.c: Dialog to edit application wide preferences and default values
 *
 * Author:
 * 	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000-2002 Jody Goldberg <jody@gnome.org>
 * (C) Copyright 2003-2004 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include "application.h"
#include "dialogs.h"
#include "help.h"

#include "mstyle.h"
#include "value.h"
#include "format.h"
#include "workbook.h"
#include "number-match.h"
#include "widgets/widget-font-selector.h"
#include "widgets/gnumeric-cell-renderer-text.h"

#include "gnumeric-gconf-priv.h"
#include "gnumeric-gconf.h"

#include <gui-util.h>
#include <glade/glade.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktable.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtktogglebutton.h>
#include <glib/gi18n.h>
#include <string.h>

enum {
	ITEM_ICON,
	ITEM_NAME,
	PAGE_NUMBER,
	NUM_COLUMNS
};

typedef struct {
	GladeXML	*gui;
	GtkWidget	*dialog;
	GtkWidget	*notebook;
	GtkTextView	*description;
	GSList		*pages;
	GtkTreeStore    *store;
	GtkTreeView     *view;
	Workbook	*wb;
} PrefState;

static void
dialog_pref_add_item (PrefState *state, char const *page_name, char const *icon_name, 
		      int page, char const* parent_path)
{
	GtkTreeIter iter, parent;
	GdkPixbuf * icon = gtk_widget_render_icon (state->dialog, icon_name,
						   GTK_ICON_SIZE_MENU,
						   "Gnumeric-Preference-Dialog");
	if ((parent_path != NULL) && gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (state->store),
									  &parent, parent_path))
		gtk_tree_store_append (state->store, &iter, &parent);
	else
		gtk_tree_store_append (state->store, &iter, NULL);

	gtk_tree_store_set (state->store, &iter,
			    ITEM_ICON, icon,
			    ITEM_NAME, _(page_name),
			    PAGE_NUMBER, page,
			    -1);
	g_object_unref (icon);
}

static void
dialog_pref_page_open (PrefState *state)
{
	GtkTextIter start;
	GtkTextIter end;
	GtkTextBuffer *buffer;

	buffer = gtk_text_view_get_buffer (state->description);
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	gtk_text_buffer_delete (buffer, &start, &end);
}

static void
dialog_pref_load_description_from_key (PrefState *state, char const *key)
{
	char *long_desc = go_conf_get_long_desc (key);

	g_return_if_fail (long_desc != NULL);

	gtk_text_buffer_set_text (
		gtk_text_view_get_buffer (state->description), long_desc, -1);
	g_free (long_desc);
}

static void
dialog_pref_load_description (PrefState *state, char const *text)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (state->description);

	gtk_text_buffer_set_text (buffer, text, -1);
}

static void
set_tip (char const *key, GtkWidget *item)
{
	char *desc = go_conf_get_short_desc (key);
	if (desc != NULL) {
		GtkTooltips *the_tip = gtk_tooltips_new ();
		gtk_tooltips_set_tip (the_tip, item, desc, NULL);
		g_free (desc);
	}
}

#ifdef WITH_GNOME
static gboolean
cb_pref_notification_destroy (gpointer notification)
{
	gconf_client_notify_remove (gnm_app_get_gconf_client (),
		GPOINTER_TO_INT (notification));
	return TRUE;
}
#endif

static void
connect_notification (char const *key, GCallback func,
		      gpointer data, GtkWidget *container)
{
#ifdef FIXMEFIXME
	/***********
	 * BROKEN
	 * BROKEN
	 *
	 * Callback has an incorrect signature for gconf
	 *
	 * BROKEN
	 * BROKEN
	 **************/
	notif = gconf_client_notify_add (state->gconf, key, func,
					 data, NULL, NULL);
	g_signal_connect_swapped (G_OBJECT (table),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
#endif
}

/*************************************************************************/
static void
bool_pref_widget_to_conf (GtkToggleButton *button, char const *key)
{
	go_conf_set_bool (key, gtk_toggle_button_get_active (button));
}
static void
bool_pref_conf_to_widget (char const *key, GtkToggleButton *button)
{
	gboolean val_in_button = gtk_toggle_button_get_active (button);
	gboolean val_in_conf = go_conf_get_bool (key);
	if ((val_in_button != FALSE) != (val_in_conf != FALSE))
		gtk_toggle_button_set_active (button, val_in_conf);
}
static void
bool_pref_create_widget (char const *key, GtkWidget *table, gint row)
{
	char *desc = go_conf_get_short_desc (key);
	GtkWidget *item = gtk_check_button_new_with_label (
		desc ? desc : "schema missing");
	g_free (desc);

	bool_pref_conf_to_widget (key, GTK_TOGGLE_BUTTON (item));
	g_signal_connect (G_OBJECT (item),
		"toggled",
		G_CALLBACK (bool_pref_widget_to_conf), (gpointer)key);
	gtk_table_attach (GTK_TABLE (table), item,
		0, 2, row, row + 1,
		GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);

	connect_notification (key, G_CALLBACK (bool_pref_conf_to_widget), item, table);
	set_tip (key, item);
}

/*************************************************************************/

static void
int_pref_widget_to_conf (GtkSpinButton *button, char const *key)
{
	go_conf_set_int (key, gtk_spin_button_get_value_as_int (button));
}
static void
int_pref_conf_to_widget (char const *key, GtkSpinButton *button)
{
	gint val_in_button = gtk_spin_button_get_value_as_int (button);
	gint val_in_conf = go_conf_get_int (key);
	if (val_in_conf != val_in_button)
		gtk_spin_button_set_value (button, (gdouble) val_in_conf);
}
static void
int_pref_create_widget (char const *key, GtkWidget *table, gint row,
			gint val, gint from, gint to, gint step)
{
	char *desc = go_conf_get_short_desc (key);
	GtkWidget *item = gtk_label_new (desc ? desc : "schema missing");
	g_free (desc);

	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_table_attach (GTK_TABLE (table), item, 0, 1, row, row + 1, 0,
			  GTK_FILL, 5, 5);
	item = gtk_spin_button_new (GTK_ADJUSTMENT (
				    gtk_adjustment_new (val, from, to, step, step, step)),
				    1, 0);
	int_pref_conf_to_widget (key, GTK_SPIN_BUTTON (item));
	g_signal_connect (G_OBJECT (item),
		"value-changed",
		G_CALLBACK (int_pref_widget_to_conf), (gpointer) key);
	gtk_table_attach (GTK_TABLE (table), item,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 5);

	connect_notification (key, G_CALLBACK (int_pref_conf_to_widget), item, table);
	set_tip (key, item);
}

/*************************************************************************/

static void
double_pref_widget_to_conf (GtkSpinButton *button, char const *key)
{
	go_conf_set_double (key, gtk_spin_button_get_value (button));
}
static void
double_pref_conf_to_widget (char const *key, GtkSpinButton *button)
{
	double val_in_button = gtk_spin_button_get_value (button);
	double val_in_conf = go_conf_get_double (key);
	if (abs (val_in_conf - val_in_button) > 1e-10) /* dead simple */
		gtk_spin_button_set_value (button, val_in_conf);
}
static void
double_pref_create_widget (char const *key, GtkWidget *table, gint row,
			   gnm_float val, gnm_float from, gnm_float to, gnm_float step,
			   gint digits)
{
	char *desc = go_conf_get_short_desc (key);
	GtkWidget *item = gtk_label_new (desc ? desc : "schema missing");
	g_free (desc);

	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_table_attach (GTK_TABLE (table), item, 0, 1, row, row + 1, 0,
			  GTK_FILL, 5, 5);
	item =  gtk_spin_button_new (GTK_ADJUSTMENT (
				     gtk_adjustment_new (val, from, to, step, step, step)),
				     1, digits);
	double_pref_conf_to_widget (key, GTK_SPIN_BUTTON (item));
	g_signal_connect (G_OBJECT (item),
		"value-changed",
		G_CALLBACK (double_pref_widget_to_conf), (gpointer)key);
	gtk_table_attach (GTK_TABLE (table), item,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 5);

	connect_notification (key, G_CALLBACK (double_pref_conf_to_widget), item, table);
	set_tip (key, item);
}

/*******************************************************************************************/
/*                     Tree View of selected configuration variables                       */
/*******************************************************************************************/

enum {
	PREF_KEY,
	PREF_VALUE,
	PREF_SHORT_DESC,
	IS_EDITABLE,
	NUM_COLMNS
};

typedef struct {
	char const *key;
	char const *parent;
	GtkTreeView *treeview;
} pref_tree_data_t;

typedef struct {
	char const *key;
	GtkTreeIter iter;
	gboolean iter_valid;
} search_cb_t;

static pref_tree_data_t pref_tree_data[] = {
	{ FUNCTION_SELECT_GCONF_NUM_OF_RECENT, NULL, NULL },
	{ GNM_CONF_GUI_ED_AUTOCOMPLETE, NULL, NULL },
	{ DIALOGS_GCONF_UNFOCUSED_RS, NULL, NULL },
	{ NULL, NULL, NULL}
};

static pref_tree_data_t pref_tree_data_danger[] = {
	{ GNM_CONF_GUI_RES_H, NULL, NULL },
	{ GNM_CONF_GUI_RES_V, NULL, NULL },
	{ GNM_CONF_GUI_ED_RECALC_LAG, NULL, NULL },
	{ NULL, NULL, NULL }
};

#define OBJECT_DATA_PATH_MODEL "treeview %i"

static void
cb_pref_tree_selection_changed (GtkTreeSelection *selection,
				PrefState *state)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (selection != NULL);

	if (gtk_tree_selection_get_selected (selection,  &model, &iter)) {
		char *key;
		gtk_tree_model_get (model, &iter,
				    PREF_KEY, &key,
				    -1);
		if (key != NULL) {
			dialog_pref_load_description_from_key (state, key);
			g_free (key);
			return;
		}
	}
	dialog_pref_page_open (state);
	return;
}

static void
pref_tree_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     GtkNotebook *notebook, gint page_num)
{
	char *object_data_path = g_strdup_printf (OBJECT_DATA_PATH_MODEL, page_num);
	GtkTreeView *view =  g_object_get_data (G_OBJECT (notebook), object_data_path);
	g_free (object_data_path);
	cb_pref_tree_selection_changed (gtk_tree_view_get_selection (view), state);
}

static gboolean
pref_tree_find_iter (GtkTreeModel *model,
		     G_GNUC_UNUSED GtkTreePath *tree_path,
		     GtkTreeIter *iter, search_cb_t *data)
{
	char *key;

	gtk_tree_model_get (model, iter,
			    PREF_KEY, &key,
			    -1);

	if (strcmp (key, data->key) == 0) {
		data->iter = *iter;
		data->iter_valid = TRUE;
	}

	g_free (key);
	return data->iter_valid;
}

static void
pref_tree_set_model (GtkTreeModel *model, GtkTreeIter *iter)
{
	char *key, *value_string, *desc;

	gtk_tree_model_get (model, iter, PREF_KEY, &key, -1);
	value_string = go_conf_get_value_as_str (key);
	desc = go_conf_get_short_desc (key);
	gtk_tree_store_set (GTK_TREE_STORE (model), iter,
			    PREF_SHORT_DESC,	desc,
			    PREF_VALUE,		value_string,
			    -1);
	g_free (desc);
	g_free (value_string);
	g_free (key);
}

static void
cb_pref_tree_changed_notification (char const *key, GtkTreeModel *model)
{
	search_cb_t search_cb;
	search_cb.key = key;
	search_cb.iter_valid = FALSE;

	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) pref_tree_find_iter,
				&search_cb);
	if (search_cb.iter_valid) {
		pref_tree_set_model (model, &search_cb.iter);
	} else
		g_warning ("Unexpected gconf notification!");
}

static void
cb_value_edited (GtkCellRendererText *cell,
		 gchar		*path_string,
		 gchar		*new_text,
		 PrefState	*state)
{
	GtkTreeIter iter;
	char        *key;
	GtkTreeModel *model = g_object_get_data (G_OBJECT (cell), "model");
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, PREF_KEY, &key, -1);
	go_conf_set_value_from_str (key, new_text);
	gtk_tree_path_free (path);
	g_free (key);
}

static GtkWidget *
pref_tree_initializer (PrefState *state, gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       gint page_num)
{
	pref_tree_data_t  *this_pref_tree_data = data;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	GtkTreeStore      *model;
	GtkTreeView       *view;
	GtkWidget         *page = gtk_scrolled_window_new (NULL, NULL);
	gint               i;
	GtkCellRenderer   *renderer;
	gchar             *object_data_path;

	gtk_widget_set_size_request (page, 350, 250);
	gtk_scrolled_window_set_policy  (GTK_SCROLLED_WINDOW (page),
					 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	model = gtk_tree_store_new (NUM_COLMNS,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_BOOLEAN);
	view = GTK_TREE_VIEW (gtk_tree_view_new_with_model
			      (GTK_TREE_MODEL (model)));
	selection = gtk_tree_view_get_selection (view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	column = gtk_tree_view_column_new_with_attributes (_("Description"),
							   gtk_cell_renderer_text_new (),
							   "text", PREF_SHORT_DESC,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, PREF_SHORT_DESC);
	gtk_tree_view_append_column (view, column);

	renderer = gnumeric_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Value"),
							   renderer,
							   "text", PREF_VALUE,
							   "editable", IS_EDITABLE,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, PREF_VALUE);
	gtk_tree_view_append_column (view, column);
	g_signal_connect (G_OBJECT (renderer),
		"edited",
		G_CALLBACK (cb_value_edited), state);
	g_object_set_data (G_OBJECT (renderer), "model", model);

	gtk_tree_view_set_headers_visible (view, TRUE);
	gtk_container_add (GTK_CONTAINER (page), GTK_WIDGET (view));

	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (view)),
		"changed",
		G_CALLBACK (cb_pref_tree_selection_changed), state);

	for (i = 0; this_pref_tree_data[i].key; i++) {
		pref_tree_data_t *this_pref = &this_pref_tree_data[i];
		GtkTreeIter      iter;

		gtk_tree_store_append (model, &iter, NULL);

		gtk_tree_store_set (model, &iter,
				    PREF_KEY, this_pref->key,
				    IS_EDITABLE, TRUE,
				    -1);
		pref_tree_set_model (GTK_TREE_MODEL (model), &iter);
		connect_notification (this_pref_tree_data[i].key,
			G_CALLBACK (cb_pref_tree_changed_notification), model, page);
	}

	object_data_path = g_strdup_printf (OBJECT_DATA_PATH_MODEL, page_num);
	g_object_set_data_full (G_OBJECT (state->notebook), object_data_path, view, NULL);
	g_free (object_data_path);

	gtk_widget_show_all (page);

	return page;
}

/*******************************************************************************************/
/*                     Default Font Selector                                               */
/*******************************************************************************************/

static void
pref_font_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description_from_key (state, GNM_CONF_FONT_NAME);
}

static void
cb_pref_font_set_fonts (char const *key, GtkWidget *page)
{
	if (key == NULL || 0 == strcmp (key, GNM_CONF_FONT_NAME))
		font_selector_set_name (FONT_SELECTOR (page),
			gnm_app_prefs->default_font.name);
	if (key == NULL || 0 == strcmp (key, GNM_CONF_FONT_SIZE))
		font_selector_set_points (FONT_SELECTOR (page),
			gnm_app_prefs->default_font.size);
	if (key == NULL ||
	    0 == strcmp (key, GNM_CONF_FONT_BOLD) ||
	    0 == strcmp (key, GNM_CONF_FONT_ITALIC))
		font_selector_set_style (FONT_SELECTOR (page),
			gnm_app_prefs->default_font.is_bold,
			gnm_app_prefs->default_font.is_italic);
}

static gboolean
cb_pref_font_has_changed (G_GNUC_UNUSED FontSelector *fs,
			  GnmStyle *mstyle, PrefState *state)
{
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		go_conf_set_double (GNM_CONF_FONT_SIZE,
			mstyle_get_font_size (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_NAME))
		go_conf_set_string (GNM_CONF_FONT_NAME,
			mstyle_get_font_name (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		go_conf_set_bool (GNM_CONF_FONT_BOLD,
			mstyle_get_font_bold (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		go_conf_set_bool (GNM_CONF_FONT_ITALIC,
			mstyle_get_font_italic (mstyle));
	return TRUE;
}

static GtkWidget *
pref_font_initializer (PrefState *state,
		       G_GNUC_UNUSED gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = font_selector_new ();

	cb_pref_font_set_fonts (NULL, page);

	connect_notification (GNM_CONF_FONT_DIRECTORY,
		G_CALLBACK (cb_pref_font_set_fonts), page, page);
	g_signal_connect (G_OBJECT (page),
		"font_changed",
		G_CALLBACK (cb_pref_font_has_changed), state);

	gtk_widget_show_all (page);

	return page;
}

/*******************************************************************************************/
/*                     Default Header/Footer Font Selector                                 */
/*******************************************************************************************/

static void
pref_font_hf_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
			G_GNUC_UNUSED GtkNotebook *notebook,
			G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description_from_key (state, PRINTSETUP_GCONF_HF_FONT_NAME);
}

static void
cb_pref_font_hf_set_fonts (char const *key, GtkWidget *page)
{
	if (key == NULL || 0 == strcmp (key, PRINTSETUP_GCONF_HF_FONT_NAME)) {
		char *name = go_conf_load_string (PRINTSETUP_GCONF_HF_FONT_NAME);
		font_selector_set_name (FONT_SELECTOR (page), name);
		g_free (name);
	}
	if (key == NULL || 0 == strcmp (key, PRINTSETUP_GCONF_HF_FONT_SIZE))
		font_selector_set_points (FONT_SELECTOR (page),
			go_conf_get_double (PRINTSETUP_GCONF_HF_FONT_SIZE));

	if (key == NULL ||
	    0 == strcmp (key, PRINTSETUP_GCONF_HF_FONT_BOLD) ||
	    0 == strcmp (key, PRINTSETUP_GCONF_HF_FONT_ITALIC))
		font_selector_set_style (FONT_SELECTOR (page),
			go_conf_get_bool (PRINTSETUP_GCONF_HF_FONT_BOLD),
			go_conf_get_bool (PRINTSETUP_GCONF_HF_FONT_ITALIC));
}

static gboolean
cb_pref_font_hf_has_changed (G_GNUC_UNUSED FontSelector *fs,
			  GnmStyle *mstyle, PrefState *state)
{
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		go_conf_set_double (PRINTSETUP_GCONF_HF_FONT_SIZE,
			mstyle_get_font_size (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_NAME))
		go_conf_set_string (PRINTSETUP_GCONF_HF_FONT_NAME,
			mstyle_get_font_name (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		go_conf_set_bool (PRINTSETUP_GCONF_HF_FONT_BOLD,
			mstyle_get_font_bold (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		go_conf_set_bool (PRINTSETUP_GCONF_HF_FONT_ITALIC,
			mstyle_get_font_italic (mstyle));
	return TRUE;
}

static GtkWidget *
pref_font_hf_initializer (PrefState *state,
			  G_GNUC_UNUSED gpointer data,
			  G_GNUC_UNUSED GtkNotebook *notebook,
			  G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = font_selector_new ();

	cb_pref_font_hf_set_fonts (NULL, page);
	connect_notification (PRINTSETUP_GCONF_DIRECTORY,
		G_CALLBACK (cb_pref_font_hf_set_fonts), page, page);
	g_signal_connect (G_OBJECT (page),
		"font_changed",
		G_CALLBACK (cb_pref_font_hf_has_changed), state);

	gtk_widget_show_all (page);

	return page;
}

/*******************************************************************************************/
/*                     Undo Preferences Page                                              */
/*******************************************************************************************/

static void
pref_undo_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
		_("The items on this page customize the behaviour of the undo/redo system."));
}

static GtkWidget *
pref_undo_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (4, 2, FALSE);
	gint row = 0;

	bool_pref_create_widget (GNM_CONF_UNDO_SHOW_SHEET_NAME,
		page, row++);
	int_pref_create_widget (GNM_CONF_UNDO_MAX_DESCRIPTOR_WIDTH,
		page, row++, 5, 5, 200, 1);
	int_pref_create_widget (GNM_CONF_UNDO_SIZE,
		page, row++, 1000, 0, 30000, 100);
	int_pref_create_widget (GNM_CONF_UNDO_MAXNUM,
		page, row++, 20, 1, 200, 1);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Sort Preferences Page                                              */
/*******************************************************************************************/

static void
pref_sort_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page customize the "
					"inital settings of the sort dialog and "
					"the behaviour of the sort toolbar buttons."));
}

static GtkWidget *
pref_sort_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (3, 2, FALSE);
	gint row = 0;

	bool_pref_create_widget (GNM_CONF_SORT_DEFAULT_RETAIN_FORM,
		page, row++);
	bool_pref_create_widget (GNM_CONF_SORT_DEFAULT_BY_CASE,
		page, row++);
	bool_pref_create_widget (GNM_CONF_SORT_DEFAULT_ASCENDING,
		page, row++);
	int_pref_create_widget (GNM_CONF_SORT_DIALOG_MAX_INITIAL,
		page, row++, 10, 0, 50, 1);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Window Preferences Page                                              */
/*******************************************************************************************/

static void
pref_window_page_open (PrefState *state,
		       G_GNUC_UNUSED gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page customize the "
					"new default workbook."));
}

static GtkWidget *
pref_window_page_initializer (PrefState *state,
			      G_GNUC_UNUSED gpointer data,
			      G_GNUC_UNUSED GtkNotebook *notebook,
			      G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (4, 2, FALSE);
	gint row = 0;

	bool_pref_create_widget (GNM_CONF_GUI_ED_TRANSITION_KEYS,
		page, row++);
	double_pref_create_widget (GNM_CONF_GUI_WINDOW_Y,
		page, row++, 0.75, 0.25, 1, 0.05, 2);
	double_pref_create_widget (GNM_CONF_GUI_WINDOW_X,
		page, row++, 0.75, 0.25, 1, 0.05, 2);
	double_pref_create_widget (GNM_CONF_GUI_ZOOM,
		page, row++, 1.00, 0.10, 5.00, 0.05, 2);
	int_pref_create_widget (GNM_CONF_WORKBOOK_NSHEETS,
		page, row++, 1, 1, 100, 1);
	bool_pref_create_widget (GNM_CONF_GUI_ED_LIVESCROLLING,
		page, row++);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     File/XML Preferences Page                                           */
/*******************************************************************************************/

static void
pref_file_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page are related to the saving "
					"and opening of files."));
}

static GtkWidget *
pref_file_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	int_pref_create_widget (GNM_CONF_FILE_HISTORY_N,
		page, row++, 4, 0, 40, 1);
	int_pref_create_widget (GNM_CONF_XML_COMPRESSION,
		page, row++, 9, 0, 9, 1);
	bool_pref_create_widget (GNM_CONF_FILE_OVERWRITE_DEFAULT,
		page, row++);
	bool_pref_create_widget (GNM_CONF_FILE_SINGLE_SHEET_SAVE,
		page, row++);
	bool_pref_create_widget (PLUGIN_GCONF_LATEX_USE_UTF8,
		page, row++);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*               General Preference Dialog Routines                                        */
/*******************************************************************************************/

typedef struct {
	char const *page_name;
	char const *icon_name;
	char const *parent_path;
	GtkWidget * (*page_initializer) (PrefState *state, gpointer data,
					 GtkNotebook *notebook, gint page_num);
	void	    (*page_open)	(PrefState *state, gpointer data,
					 GtkNotebook *notebook, gint page_num);
	gpointer data;
} page_info_t;

static page_info_t page_info[] = {
	{N_("Font"),          GTK_STOCK_ITALIC,	         NULL, &pref_font_initializer,		&pref_font_page_open,	NULL},
	{N_("Windows"),       "Gnumeric_ObjectCombo",	 NULL, &pref_window_page_initializer,	&pref_window_page_open,	NULL},
	{N_("Files"),         GTK_STOCK_FLOPPY,	         NULL, &pref_file_page_initializer,	&pref_file_page_open,	NULL},
	{N_("Undo"),          GTK_STOCK_UNDO,		 NULL, &pref_undo_page_initializer,	&pref_undo_page_open,	NULL},
	{N_("Sorting"),       GTK_STOCK_SORT_ASCENDING,  NULL, &pref_sort_page_initializer,	&pref_sort_page_open,	NULL},
	{N_("Various"),       GTK_STOCK_PREFERENCES,     NULL, &pref_tree_initializer,		&pref_tree_page_open,	pref_tree_data},
	{N_("Internal"),      GTK_STOCK_DIALOG_ERROR,    "5",  &pref_tree_initializer,		&pref_tree_page_open,	pref_tree_data_danger},
	{N_("Header/Footer"), GTK_STOCK_ITALIC,	         "0",  &pref_font_hf_initializer,	&pref_font_hf_page_open, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL},
};

static void
dialog_pref_select_page (PrefState *state, char const *page)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->view);
	GtkTreeIter iter;
	GtkTreePath *path;

	path = gtk_tree_path_new_from_string  (page);
	
	if (path != NULL) {
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);
	} else if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (state->store),
						  &iter)) {
		gtk_tree_selection_select_iter (selection, &iter);
	}
}

static void
cb_dialog_pref_selection_changed (GtkTreeSelection *selection,
				  PrefState *state)
{
	GtkTreeIter iter;
	int page;

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->store), &iter,
				    PAGE_NUMBER, &page,
				    -1);
		gtk_notebook_set_current_page (GTK_NOTEBOOK(state->notebook),
					       page);
	} else {
		dialog_pref_select_page (state, "0");
	}
}

static gboolean
cb_preferences_destroy (PrefState *state)
{
	go_conf_sync ();

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	state->dialog = NULL;
	g_free (state);

	gnm_app_set_pref_dialog (NULL);

	return FALSE;
}

static void
cb_close_clicked (PrefState *state)
{
	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_dialog_pref_switch_page  (GtkNotebook *notebook,
			     G_GNUC_UNUSED GtkNotebookPage *page,
			     gint page_num, PrefState *state)
{
	if (page_info[page_num].page_open)
		page_info[page_num].page_open (state, page_info[page_num].data,
					       notebook, page_num);
	else
		dialog_pref_page_open (state);
}

/* Note: The first page listed below is opened through File/Preferences, */
/*       and the second through Format/Workbook */
static char const *startup_pages[] = {"1", "0"};

void
dialog_preferences (WorkbookControlGUI *wbcg, gint page)
{
	PrefState *state;
	GladeXML *gui;
	GtkWidget *w;
	gint i;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;

	w = gnm_app_get_pref_dialog ();
	if (w) {
		gtk_widget_show (w);
		gdk_window_raise (w->window);
		return;
	}

	gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"preferences.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new0 (PrefState, 1);
	state->gui = gui;
	state->dialog     = glade_xml_get_widget (gui, "preferences");
	state->notebook   = glade_xml_get_widget (gui, "notebook");
	state->pages      = NULL;
	state->description = GTK_TEXT_VIEW (glade_xml_get_widget (gui, "description"));
	state->wb	  = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

	state->view = GTK_TREE_VIEW(glade_xml_get_widget (gui, "itemlist"));
	state->store = gtk_tree_store_new (NUM_COLUMNS,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_STRING,
					   G_TYPE_INT);
	gtk_tree_view_set_model (state->view, GTK_TREE_MODEL(state->store));
	selection = gtk_tree_view_get_selection (state->view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_pixbuf_new (),
							   "pixbuf", ITEM_ICON,
							   NULL);
	gtk_tree_view_append_column (state->view, column);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME,
							   NULL);
	gtk_tree_view_append_column (state->view, column);
	gtk_tree_view_set_expander_column (state->view, column);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cb_dialog_pref_selection_changed), state);
	
	g_signal_connect_swapped (G_OBJECT (glade_xml_get_widget (gui, "close_button")),
		"clicked",
		G_CALLBACK (cb_close_clicked), state);

	g_signal_connect (G_OBJECT (state->notebook),
		"switch-page",
		G_CALLBACK (cb_dialog_pref_switch_page), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_PREFERENCES);

	g_signal_connect_swapped (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_preferences_destroy), state);

	gnm_app_set_pref_dialog (state->dialog);

	for (i = 0; page_info[i].page_initializer; i++) {
		page_info_t *this_page =  &page_info[i];
		GtkWidget *page = this_page->page_initializer (state, this_page->data,
							       GTK_NOTEBOOK (state->notebook), i);
		GtkWidget *label = NULL;

		state->pages = g_slist_append (state->pages, page);

		if (this_page->icon_name)
			label = gtk_image_new_from_stock (this_page->icon_name,
							  GTK_ICON_SIZE_BUTTON);
		else if (this_page->page_name)
			label = gtk_label_new (this_page->page_name);
		gtk_notebook_append_page (GTK_NOTEBOOK (state->notebook), page, label);
		dialog_pref_add_item (state, this_page->page_name, this_page->icon_name, i, this_page->parent_path);
	}

	if (page != 0 && page != 1) {
		g_warning ("Selected page is %i but should be 0 or 1", page);
		page = 0;
	}

	wbcg_set_transient (wbcg, GTK_WINDOW (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));

	dialog_pref_select_page (state, startup_pages[page]);
}
