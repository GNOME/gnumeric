/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-preferences.c: Dialog to change the order of sheets in the Gnumeric
 * spreadsheet
 *
 * Author:
 * 	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001, 2002 Jody Goldberg <jody@gnome.org>
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
#include "mstyle.h"
#include "value.h"
#include "number-match.h"
#include "widgets/widget-font-selector.h"
#include "widgets/gnumeric-cell-renderer-text.h"

#include "gnumeric-gconf-priv.h"
#include "gnumeric-gconf.h"

#include <gui-util.h>
#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <gtk/gtk.h>




typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *notebook;
	GtkTextView *description;
	GSList    *pages;
	GConfClient *gconf;
} PrefState;


typedef struct {
	char const *page_name;
	char const *icon_name;
	GtkWidget * (*page_initializer) (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num);
	void (*page_open) (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num);
	gpointer data;
} page_info_t;

static gboolean
cb_pref_notification_destroy (GtkWidget *page, guint notification)
{
	gconf_client_notify_remove (application_get_gconf_client (), notification);
	return TRUE;
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
dialog_pref_load_description_from_schema (PrefState *state, char const *schema_path)
{
	GConfSchema *the_schema;
	GtkTextBuffer *buffer;

	the_schema = gconf_client_get_schema (state->gconf, schema_path, NULL);
	buffer = gtk_text_view_get_buffer (state->description);

	g_return_if_fail (the_schema != NULL);

	gtk_text_buffer_set_text (buffer, gconf_schema_get_long_desc (the_schema), -1);
	gconf_schema_free (the_schema);
}	

static void
dialog_pref_load_description (PrefState *state, char const *text)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (state->description);

	gtk_text_buffer_set_text (buffer, text, -1);
}	

/*******************************************************************************************/
/*                     Tree View of selected configuration variables                       */
/*******************************************************************************************/

enum {
	PREF_NAME,
	PREF_VALUE,
	PREF_PATH,
	PREF_SCHEMA,
	IS_EDITABLE,
	NUM_COLMNS
};

typedef struct {
	char const *path;
	char const *parent;
	char const *schema;
	GtkTreeView *treeview;
} pref_tree_data_t;

typedef struct {
	char const *path;
	GtkTreeIter iter;
	gboolean iter_valid;
} search_cb_t;


static pref_tree_data_t pref_tree_data[] = { 
	{GNUMERIC_GCONF_GUI_WINDOW_X, NULL, 
	                      "/schemas" GNUMERIC_GCONF_GUI_WINDOW_X},
	{GNUMERIC_GCONF_GUI_WINDOW_Y, NULL, 
	                      "/schemas" GNUMERIC_GCONF_GUI_WINDOW_Y},
	{FUNCTION_SELECT_GCONF_NUM_OF_RECENT, NULL, 
	                      "/schemas" FUNCTION_SELECT_GCONF_NUM_OF_RECENT},
	{GNUMERIC_GCONF_FILE_HISTORY_N, NULL, 
	                      "/schemas" GNUMERIC_GCONF_FILE_HISTORY_N},
	{GNUMERIC_GCONF_WORKBOOK_NSHEETS, NULL, 
	                      "/schemas" GNUMERIC_GCONF_WORKBOOK_NSHEETS},
	{GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE, NULL, 
	                      "/schemas" GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE},
	{GNUMERIC_GCONF_GUI_ED_LIVESCROLLING, NULL, 
	                      "/schemas" GNUMERIC_GCONF_GUI_ED_LIVESCROLLING},
	{NULL, NULL, NULL}
};

static pref_tree_data_t pref_tree_data_danger[] = { 
	{GNUMERIC_GCONF_GUI_RES_H, NULL, 
	                      "/schemas" GNUMERIC_GCONF_GUI_RES_H},
	{GNUMERIC_GCONF_GUI_RES_V, NULL, 
	                      "/schemas" GNUMERIC_GCONF_GUI_RES_V},
	{GNUMERIC_GCONF_GUI_ED_RECALC_LAG, NULL, 
	                      "/schemas" GNUMERIC_GCONF_GUI_ED_RECALC_LAG},
	{NULL, NULL, NULL}
};

#define OBJECT_DATA_PATH_MODEL "treeview %i"

static void 
cb_pref_tree_selection_changed (GtkTreeSelection *selection,
				PrefState *state)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *schema_path;

	g_return_if_fail (selection != NULL);

	if (gtk_tree_selection_get_selected (selection,  &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    PREF_SCHEMA, &schema_path,
				    -1);
		if (schema_path) {
			dialog_pref_load_description_from_schema (state, schema_path);
			g_free (schema_path);
			return;
		}
	}
	dialog_pref_page_open (state);	
	return;
}

static void 
pref_tree_page_open (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num)
{
	char *object_data_path;
	GtkTreeView *view;

	object_data_path = g_strdup_printf (OBJECT_DATA_PATH_MODEL, page_num);
	view =  g_object_get_data (G_OBJECT (notebook), object_data_path);
	g_free (object_data_path);
	
	cb_pref_tree_selection_changed (gtk_tree_view_get_selection (view), state);
}

static gboolean    
pref_tree_find_iter (GtkTreeModel *model, GtkTreePath *tree_path, 
		     GtkTreeIter *iter, search_cb_t *data)
{
	char *path;

	gtk_tree_model_get (model, iter,
			    PREF_PATH, &path,
			    -1);

	if (strcmp (path, data->path) == 0) {
		data->iter = *iter;
		data->iter_valid = TRUE;
	}

	g_free (path);
	return data->iter_valid;
}

static void 
pref_tree_set_model (GConfClient *gconf, GtkTreeModel *model, GtkTreeIter *iter)
{
	char *schema_path;
	char *key;
	GConfSchema *the_schema;
	char *value_string;

	gtk_tree_model_get (model, iter,
			    PREF_SCHEMA, &schema_path,
			    PREF_PATH, &key,
			    -1);
	the_schema = gconf_client_get_schema (gconf, schema_path, NULL);
	
	switch (gconf_schema_get_type (the_schema)) {
	case GCONF_VALUE_STRING:
		value_string = gconf_client_get_string (gconf, key, NULL);
		
		break;
	case GCONF_VALUE_INT:
		value_string = g_strdup_printf ("%i", gconf_client_get_int (gconf, key, 
									    NULL));
		break;
	case GCONF_VALUE_FLOAT:
		value_string = g_strdup_printf ("%f", gconf_client_get_float (gconf, key, 
									    NULL));
		break;
	case GCONF_VALUE_BOOL:
		value_string = gconf_client_get_bool (gconf, key, NULL) ? 
			g_strdup (_("TRUE")) :
			g_strdup (_("FALSE"));
		break;
	default:
		value_string = g_strdup ("ERROR FIXME");
	}
	gtk_tree_store_set (GTK_TREE_STORE (model), iter,
			    PREF_NAME, gconf_schema_get_short_desc (the_schema),
			    PREF_VALUE, value_string,
			    -1);
	g_free (key);
	g_free (schema_path);
	g_free (value_string);
	gconf_schema_free (the_schema);
}

static void
cb_pref_tree_changed_notification (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
				   GtkTreeModel *model)
{
	search_cb_t search_cb;
	search_cb.iter_valid = FALSE;
	search_cb.path = gconf_entry_get_key (entry);

	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) pref_tree_find_iter, 
				&search_cb);
	if (search_cb.iter_valid) {
		pref_tree_set_model (gconf, model, &search_cb.iter);
	} else
		g_warning ("Unexpected gconf notification!");
}

static void
cb_value_edited (GtkCellRendererText *cell,
	gchar               *path_string,
	gchar               *new_text,
        GtkTreeStore        *model)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	char        *key;
	char        *schema;
	gint        the_int;
	gnum_float  the_float;
	gboolean    the_bool;
	Value       *value;
	GConfClient *client = application_get_gconf_client ();
	GConfSchema *the_schema;
	gboolean    err;

	path = gtk_tree_path_new_from_string (path_string);
	
	gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);

	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
			    PREF_PATH, &key,
			    PREF_SCHEMA, &schema,
			    -1);
	the_schema = gconf_client_get_schema (client, schema, NULL);
	switch (gconf_schema_get_type (the_schema)) {
	case GCONF_VALUE_STRING:
		gconf_client_set_string (client, key, new_text, NULL);
		break;
	case GCONF_VALUE_FLOAT:
		value = format_match_number (new_text, NULL);
		if (value != NULL) {
			the_float =  value_get_as_float (value);
			gconf_client_set_float (client, key, the_float, NULL);
		}
		if (value)
			value_release (value);
		break;
	case GCONF_VALUE_INT:
		value = format_match_number (new_text, NULL);
		if (value != NULL) {
			the_int =  value_get_as_int (value);
			gconf_client_set_int (client, key, the_int, NULL);
		}
		if (value)
			value_release (value);
		break;
	case GCONF_VALUE_BOOL:
		value = format_match_number (new_text, NULL);
		if (value != NULL) {
			err = FALSE;
			the_bool =  value_get_as_bool (value, &err);
			gconf_client_set_bool (client, key, the_bool, NULL);
		}
		if (value)
			value_release (value);
		break;
	default:
		g_warning ("Unsupported gconf type in preference dialog");
	}
	
	
	gtk_tree_path_free (path);
	g_free (key);
	g_free (schema);
	gconf_schema_free (the_schema);
}

static  GtkWidget *pref_tree_initializer (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num)
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
							   "text", PREF_NAME, 
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, PREF_NAME);
	gtk_tree_view_append_column (view, column);

	renderer = gnumeric_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Value"),
							   renderer,
							   "text", PREF_VALUE, 
							   "editable", IS_EDITABLE,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, PREF_VALUE);
	gtk_tree_view_append_column (view, column);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_value_edited), model);

	gtk_tree_view_set_headers_visible (view, TRUE);
	gtk_container_add (GTK_CONTAINER (page), GTK_WIDGET (view));

	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (view)), "changed",
			  G_CALLBACK (cb_pref_tree_selection_changed), state);

	for (i = 0; this_pref_tree_data[i].path; i++) {
		pref_tree_data_t *this_pref = &this_pref_tree_data[i];
		GtkTreeIter      iter;
		guint notification;

		gtk_tree_store_append (model, &iter, NULL);

		gtk_tree_store_set (model, &iter,
				    PREF_PATH, this_pref->path,
				    PREF_SCHEMA, this_pref->schema,
				    IS_EDITABLE, TRUE,
				    -1);
		pref_tree_set_model (state->gconf, GTK_TREE_MODEL (model), &iter);

		notification = gconf_client_notify_add 
			(state->gconf, this_pref_tree_data[i].path,
			 (GConfClientNotifyFunc) cb_pref_tree_changed_notification,
			 model, NULL, NULL);
		
		g_signal_connect (G_OBJECT (page),
				  "destroy",
				  G_CALLBACK (cb_pref_notification_destroy), 
				  GINT_TO_POINTER (notification));
		
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
pref_font_page_open (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num)
{
	dialog_pref_load_description_from_schema (state, "/schemas" GNUMERIC_GCONF_FONT_NAME);
}

static void
cb_pref_font_set_fonts (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
		     GtkWidget *page)
{
	if (entry == NULL || 0 == strcmp (gconf_entry_get_key (entry), 
					  GNUMERIC_GCONF_FONT_NAME)) {
		char      *font_name = gconf_client_get_string (gconf,
					  GNUMERIC_GCONF_FONT_NAME, NULL);
		font_selector_set_name      (FONT_SELECTOR (page), font_name);
		g_free (font_name);	
	}
	if (entry == NULL || 0 == strcmp (gconf_entry_get_key (entry), 
					  GNUMERIC_GCONF_FONT_SIZE)) {
		double    size = gconf_client_get_float (gconf,
					  GNUMERIC_GCONF_FONT_SIZE, NULL);
		font_selector_set_points    (FONT_SELECTOR (page), size);
	}
	if (entry == NULL || 0 == strcmp (gconf_entry_get_key (entry), 
					  GNUMERIC_GCONF_FONT_BOLD)
	    || 0 == strcmp (gconf_entry_get_key (entry), 
			    GNUMERIC_GCONF_FONT_ITALIC)) {
		gboolean  is_bold = gconf_client_get_bool (gconf,
			    GNUMERIC_GCONF_FONT_BOLD, NULL);
		gboolean  is_italic = gconf_client_get_bool (gconf,
			    GNUMERIC_GCONF_FONT_ITALIC, NULL);
		font_selector_set_style     (FONT_SELECTOR (page), is_bold, is_italic);
	}
}

static gboolean 
cb_pref_font_has_changed (FontSelector *fs, MStyle *mstyle, PrefState *state)
{
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		gconf_client_set_float (state->gconf,
					GNUMERIC_GCONF_FONT_SIZE,
					mstyle_get_font_size (mstyle), NULL);
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_NAME))
		gconf_client_set_string (state->gconf,
					 GNUMERIC_GCONF_FONT_NAME,
					 mstyle_get_font_name (mstyle), NULL);
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_FONT_BOLD,
				       mstyle_get_font_bold (mstyle), NULL);
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_FONT_ITALIC,
				       mstyle_get_font_italic (mstyle), NULL);
	return TRUE;
}

static 
GtkWidget *pref_font_initializer (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num)
{
	GtkWidget *page = font_selector_new ();
	guint notification;

	cb_pref_font_set_fonts (state->gconf, 0, NULL, page);

	notification = gconf_client_notify_add (state->gconf, GNUMERIC_GCONF_FONT_DIRECTORY,
						(GConfClientNotifyFunc) cb_pref_font_set_fonts,
						page, NULL, NULL);
	
	g_signal_connect (G_OBJECT (page),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notification));
	g_signal_connect (G_OBJECT (page),
		"font_changed",
		G_CALLBACK (cb_pref_font_has_changed), state);

	gtk_widget_show_all (page);
	
	return page;
}

/*******************************************************************************************/
/*                     Undo Preferences Page                                              */
/*******************************************************************************************/

#define GCONF_FONT_NAME "/apps/gnumeric/core/defaultfont/name"
#define GCONF_FONT_SIZE "/apps/gnumeric/core/defaultfont/size"
#define GCONF_FONT_BOLD "/apps/gnumeric/core/defaultfont/bold"
#define GCONF_FONT_ITALIC "/apps/gnumeric/core/defaultfont/italic"

static void 
pref_undo_page_open (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num)
{
	dialog_pref_load_description (state, 
				      _("The items on this page customize the "
					"behaviour of the undo/redo system."));
}

static void
cb_pref_undo_set_sheet_name (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
			     GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME, 
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void 
cb_pref_undo_sheet_name_toggled (GtkToggleButton *button, PrefState *state)
{
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
				       gtk_toggle_button_get_active (button), 
				       NULL);
}

static void
cb_pref_undo_set_max_descriptor_width (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
			     GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH, 
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void 
cb_pref_undo_max_descriptor_width_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				       GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
				       gtk_spin_button_get_value_as_int (button), 
				       NULL);
}

static void
cb_pref_undo_set_size (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
			     GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_UNDO_SIZE, 
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void 
cb_pref_undo_size_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				       GNUMERIC_GCONF_UNDO_SIZE,
				       gtk_spin_button_get_value_as_int (button), 
				       NULL);
}

static void
cb_pref_undo_set_maxnum (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
			     GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_UNDO_MAXNUM, 
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void 
cb_pref_undo_maxnum_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				       GNUMERIC_GCONF_UNDO_MAXNUM,
				       gtk_spin_button_get_value_as_int (button), 
				       NULL);
}

static 
GtkWidget *pref_undo_page_initializer (PrefState *state, gpointer data, 
					  GtkNotebook *notebook, gint page_num)
{
	GtkWidget *page = gtk_table_new (3, 2, FALSE);
	guint notif;
	GtkWidget *item;
	GConfSchema *the_schema;
	GtkTooltips *the_tip;

	/* Sheet name check box */
	the_schema = gconf_client_get_schema (state->gconf, 
					      "/schemas" GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME, 
					      NULL);
	item = gtk_check_button_new_with_label (gconf_schema_get_short_desc (the_schema));
	cb_pref_undo_set_sheet_name (state->gconf, 0, NULL, GTK_TOGGLE_BUTTON (item));
	notif = gconf_client_notify_add (state->gconf, GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
					 (GConfClientNotifyFunc) cb_pref_undo_set_sheet_name,
						item, NULL, NULL);
	g_signal_connect (G_OBJECT (page),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
	g_signal_connect (G_OBJECT (item),
		"toggled",
		G_CALLBACK (cb_pref_undo_sheet_name_toggled), state);
	gtk_table_attach (GTK_TABLE (page), item, 0, 2, 0, 1, GTK_FILL | GTK_SHRINK, 
			  GTK_FILL | GTK_SHRINK, 5, 5);
	the_tip = gtk_tooltips_new ();
	gtk_tooltips_set_tip (the_tip, item, gconf_schema_get_long_desc (the_schema), NULL);
	gconf_schema_free (the_schema);

	/* Descriptor Width Spin Button */
	the_schema = gconf_client_get_schema (state->gconf, 
					      "/schemas" GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH, 
					      NULL);
	item = gtk_label_new (gconf_schema_get_short_desc (the_schema));
	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_table_attach (GTK_TABLE (page), item, 0, 1, 1, 2, 0, 
			  GTK_FILL | GTK_SHRINK, 5, 5);
	item =  gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (5,
									 5, 200, 1, 1, 1)),
				     1, 0);
	cb_pref_undo_set_max_descriptor_width (state->gconf, 0, NULL, GTK_SPIN_BUTTON (item));
	notif = gconf_client_notify_add (state->gconf, GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
			  (GConfClientNotifyFunc) cb_pref_undo_set_max_descriptor_width,
			  item, NULL, NULL);
	g_signal_connect (G_OBJECT (page),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
	g_signal_connect (G_OBJECT (item),
		"value-changed",
		G_CALLBACK (cb_pref_undo_max_descriptor_width_changed), state);
	gtk_table_attach (GTK_TABLE (page), item, 1, 2, 1, 2, GTK_FILL | GTK_EXPAND, 
			  GTK_FILL | GTK_SHRINK, 5, 5);
	the_tip = gtk_tooltips_new ();
	gtk_tooltips_set_tip (the_tip, item, gconf_schema_get_long_desc (the_schema), NULL);
	gconf_schema_free (the_schema);
	
	/* Undo Size Spin Button */
	the_schema = gconf_client_get_schema (state->gconf, 
					      "/schemas" GNUMERIC_GCONF_UNDO_SIZE, 
					      NULL);
	item = gtk_label_new (gconf_schema_get_short_desc (the_schema));
	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_table_attach (GTK_TABLE (page), item, 0, 1, 2, 3, 0, 
			  GTK_FILL | GTK_SHRINK, 5, 5);
	item =  gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (1000,
									 0, 30000, 100, 
									 1000, 1000)),
				     1, 0);
	
	cb_pref_undo_set_size (state->gconf, 0, NULL, GTK_SPIN_BUTTON (item));
	notif = gconf_client_notify_add (state->gconf, GNUMERIC_GCONF_UNDO_SIZE,
			  (GConfClientNotifyFunc) cb_pref_undo_set_size,
			  item, NULL, NULL);
	g_signal_connect (G_OBJECT (page),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
	g_signal_connect (G_OBJECT (item),
		"value-changed",
		G_CALLBACK (cb_pref_undo_size_changed), state);
	gtk_table_attach (GTK_TABLE (page), item, 1, 2, 2, 3, GTK_FILL | GTK_EXPAND, 
			  GTK_FILL | GTK_SHRINK, 5, 5);
	the_tip = gtk_tooltips_new ();
	gtk_tooltips_set_tip (the_tip, item, gconf_schema_get_long_desc (the_schema), NULL);
	gconf_schema_free (the_schema);

	/* Undo Size Spin Button */
	the_schema = gconf_client_get_schema (state->gconf, 
					      "/schemas" GNUMERIC_GCONF_UNDO_MAXNUM, 
					      NULL);
	item = gtk_label_new (gconf_schema_get_short_desc (the_schema));
	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_table_attach (GTK_TABLE (page), item, 0, 1, 3, 4, 0, 
			  GTK_FILL | GTK_SHRINK, 5, 5);
	item =  gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (20,
									 1, 200, 1, 
									 5, 5)),
				     1, 0);
	
	cb_pref_undo_set_maxnum (state->gconf, 0, NULL, GTK_SPIN_BUTTON (item));
	notif = gconf_client_notify_add (state->gconf, GNUMERIC_GCONF_UNDO_MAXNUM,
			  (GConfClientNotifyFunc) cb_pref_undo_set_maxnum,
			  item, NULL, NULL);
	g_signal_connect (G_OBJECT (page),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
	g_signal_connect (G_OBJECT (item),
		"value-changed",
		G_CALLBACK (cb_pref_undo_maxnum_changed), state);
	gtk_table_attach (GTK_TABLE (page), item, 1, 2, 3, 4, GTK_FILL | GTK_EXPAND, 
			  GTK_FILL | GTK_SHRINK, 5, 5);
	the_tip = gtk_tooltips_new ();
	gtk_tooltips_set_tip (the_tip, item, gconf_schema_get_long_desc (the_schema), NULL);
	gconf_schema_free (the_schema);


	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*               General Preference Dialog Routines                                        */
/*******************************************************************************************/

static page_info_t page_info[] = {
	{NULL, GTK_STOCK_ITALIC, pref_font_initializer, pref_font_page_open, NULL},
	{NULL, GTK_STOCK_UNDO, pref_undo_page_initializer, pref_undo_page_open, NULL},
	{NULL, GTK_STOCK_PREFERENCES, pref_tree_initializer, pref_tree_page_open, pref_tree_data},
	{NULL, GTK_STOCK_DIALOG_ERROR, pref_tree_initializer, pref_tree_page_open, pref_tree_data_danger},
	{NULL, NULL, NULL, NULL, NULL},
};


static gboolean
cb_preferences_destroy (GtkWidget *widget, PrefState *state)
{
	if (state->gconf)
		gconf_client_suggest_sync (state->gconf, NULL);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;

	g_free (state);

	application_set_pref_dialog (NULL);

	return FALSE;
}

static void
cb_close_clicked (GtkWidget *ignore, PrefState *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void        
cb_dialog_pref_switch_page  (GtkNotebook *notebook, GtkNotebookPage *page,
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
static gint startup_pages[] = {1, 0};

void
dialog_preferences (gint page)
{
	PrefState *state;
	GladeXML *gui;
	GtkWidget *w;
	gint i;

	w = application_get_pref_dialog ();
	if (w) {
		gtk_widget_show (w);
		gdk_window_raise (w->window);
		return;
	}

	gui = gnumeric_glade_xml_new (NULL, "preferences.glade");

	g_return_if_fail (gui != NULL);

	state = g_new0 (PrefState, 1);
	state->gui = gui;
	state->dialog     = glade_xml_get_widget (gui, "preferences");
	state->notebook   = glade_xml_get_widget (gui, "notebook");
	state->pages      = NULL;
	state->gconf      = application_get_gconf_client ();
	state->description = GTK_TEXT_VIEW (glade_xml_get_widget (gui, "description"));

	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "close_button")),
		"clicked",
		G_CALLBACK (cb_close_clicked), state);

	g_signal_connect (G_OBJECT (state->notebook),
		"switch-page",
		G_CALLBACK (cb_dialog_pref_switch_page), state);

/* FIXME: Add correct helpfile address */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"sheet-order.html");

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_preferences_destroy), state);

	application_set_pref_dialog (state->dialog);

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
	}

	gtk_notebook_set_current_page   (GTK_NOTEBOOK (state->notebook), startup_pages[page]);

	gtk_widget_show (GTK_WIDGET (state->dialog));
}
