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
#include <gnm-format.h>
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
#include <glib/gi18n-lib.h>
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
	GtkTreeStore    *store;
	GtkTreeView     *view;
	Workbook	*wb;
	GOConfNode	*root;
} PrefState;

typedef void (* double_conf_setter_t) (gnm_float value);
typedef void (* gint_conf_setter_t) (gint value);
typedef void (* gboolean_conf_setter_t) (gboolean value);

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
dialog_pref_load_description (PrefState *state, char const *text)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (state->description);

	gtk_text_buffer_set_text (buffer, text, -1);
}

static void
dialog_pref_load_description_from_key (PrefState *state, char const *key)
{
	char *long_desc = go_conf_get_long_desc (state->root, key);

	if (long_desc == NULL)
		dialog_pref_load_description (state, "");
	else {
		dialog_pref_load_description (state, long_desc);
		g_free (long_desc);
	}
}

static void
set_tip (GOConfNode *node, char const *key, GtkWidget *item)
{
	char *desc = go_conf_get_long_desc (node, key);
	if (desc != NULL) {
		GtkTooltips *the_tip = gtk_tooltips_new ();
		gtk_tooltips_set_tip (the_tip, item, desc, NULL);
		g_free (desc);
	}
}

static void
cb_pref_notification_destroy (gpointer handle)
{
	go_conf_remove_monitor (GPOINTER_TO_UINT (handle));
}

static void
connect_notification (GOConfNode *node, char const *key, GOConfMonitorFunc func,
		      gpointer data, GtkWidget *container)
{
	guint handle = go_conf_add_monitor (node, key, func, data);
	g_signal_connect_swapped (G_OBJECT (container), "destroy",
		G_CALLBACK (cb_pref_notification_destroy),
		GUINT_TO_POINTER (handle));
}

/*************************************************************************/
static void
bool_pref_widget_to_conf (GtkToggleButton *button, 
			  gboolean_conf_setter_t setter)
{
	g_return_if_fail (setter != NULL);
	
	setter (gtk_toggle_button_get_active (button));
}
static void
bool_pref_conf_to_widget (GOConfNode *node, char const *key, GtkToggleButton *button)
{
	gboolean val_in_button = gtk_toggle_button_get_active (button);
	gboolean val_in_conf = go_conf_get_bool (node, key);
	if ((val_in_button != FALSE) != (val_in_conf != FALSE))
		gtk_toggle_button_set_active (button, val_in_conf);
}
static void
bool_pref_create_widget (GOConfNode *node, char const *key, GtkWidget *table,
			 gint row, gboolean_conf_setter_t setter, 
			 char const *default_text)
{
	char *desc = go_conf_get_short_desc (node, key);
	GtkWidget *item = gtk_check_button_new_with_label (
		(desc != NULL) ? desc : default_text);

	g_free (desc);

	bool_pref_conf_to_widget (node, key, GTK_TOGGLE_BUTTON (item));
	g_signal_connect (G_OBJECT (item),
		"toggled",
		G_CALLBACK (bool_pref_widget_to_conf), (gpointer) setter);
	gtk_table_attach (GTK_TABLE (table), item,
		0, 2, row, row + 1,
		GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);

	connect_notification (node, key, (GOConfMonitorFunc)bool_pref_conf_to_widget,
			      item, table);
	set_tip (node, key, item);
}

/*************************************************************************/

static void
int_pref_widget_to_conf (GtkSpinButton *button, gint_conf_setter_t setter)
{
	g_return_if_fail (setter != NULL);

	setter (gtk_spin_button_get_value_as_int (button));
}

static void
int_pref_conf_to_widget (GOConfNode *node, char const *key, GtkSpinButton *button)
{
	gint val_in_button = gtk_spin_button_get_value_as_int (button);
	gint val_in_conf = go_conf_get_int (node, key);
	if (val_in_conf != val_in_button)
		gtk_spin_button_set_value (button, (gdouble) val_in_conf);
}
static void
int_pref_create_widget (GOConfNode *node, char const *key, GtkWidget *table,
			gint row, gint val, gint from, gint to, gint step, 
			gint_conf_setter_t setter, char const *default_text)
{
	char *desc = go_conf_get_short_desc (node, key);
	GtkWidget *item = gtk_label_new 
		((desc != NULL) ? desc : default_text);

	g_free (desc);

	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (item), 0, 0);
	gtk_table_attach (GTK_TABLE (table), item, 0, 1, row, row + 1, 
			  GTK_FILL | GTK_EXPAND,
			  GTK_FILL | GTK_SHRINK, 5, 2);
	item = gtk_spin_button_new (GTK_ADJUSTMENT (
				    gtk_adjustment_new (val, from, to, step, 
							step, step)),
				    1, 0);
	int_pref_conf_to_widget (node, key, GTK_SPIN_BUTTON (item));
	g_signal_connect (G_OBJECT (item),
			  "value-changed",
			  G_CALLBACK (int_pref_widget_to_conf), (gpointer) setter);
	gtk_table_attach (GTK_TABLE (table), item,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 2);

	connect_notification (node, key, (GOConfMonitorFunc)int_pref_conf_to_widget,
			      item, table);
	set_tip (node, key, item);
}

/*************************************************************************/

static void
double_pref_widget_to_conf (GtkSpinButton *button, double_conf_setter_t setter)
{
	g_return_if_fail (setter != NULL);

	setter (gtk_spin_button_get_value (button));
}

static void
double_pref_conf_to_widget (GOConfNode *node, char const *key, GtkSpinButton *button)
{
	double val_in_button = gtk_spin_button_get_value (button);
	double val_in_conf = go_conf_get_double (node, key);

	if (fabs (val_in_conf - val_in_button) > 1e-10) /* dead simple */
		gtk_spin_button_set_value (button, val_in_conf);
}
static void
double_pref_create_widget (GOConfNode *node, char const *key, GtkWidget *table,
			   gint row, gnm_float val, gnm_float from,gnm_float to, 
			   gnm_float step,
			   gint digits, double_conf_setter_t setter,
			   char const *default_text)
{
	char *desc = go_conf_get_short_desc (node, key);
	GtkWidget *item = gtk_label_new (desc ? desc : default_text);
	
	g_free (desc);

	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (item), 0, 0);
	gtk_table_attach (GTK_TABLE (table), item, 0, 1, row, row + 1,
			  GTK_FILL | GTK_EXPAND,
			  GTK_FILL | GTK_SHRINK, 5, 2);
	item =  gtk_spin_button_new (GTK_ADJUSTMENT (
				     gtk_adjustment_new (val, from, to, step, step, step)),
				     1, digits);
	double_pref_conf_to_widget (node, key, GTK_SPIN_BUTTON (item));
	g_signal_connect (G_OBJECT (item),
			  "value-changed",
			  G_CALLBACK (double_pref_widget_to_conf), 
			  (gpointer) setter);
	gtk_table_attach (GTK_TABLE (table), item,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 2);

	connect_notification (node, key, (GOConfMonitorFunc)double_pref_conf_to_widget,
			      item, table);
	set_tip (node, key, item);
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
cb_pref_font_set_fonts (GOConfNode *node, char const *key, GtkWidget *page)
{
	if (!key || g_str_has_suffix (key, GNM_CONF_FONT_NAME))
		font_selector_set_name (FONT_SELECTOR (page),
			gnm_app_prefs->default_font.name);
	if (!key || g_str_has_suffix (key, GNM_CONF_FONT_SIZE))
		font_selector_set_points (FONT_SELECTOR (page),
			gnm_app_prefs->default_font.size);
	if (!key ||
	    g_str_has_suffix (key, GNM_CONF_FONT_BOLD) ||
	    g_str_has_suffix (key, GNM_CONF_FONT_ITALIC))
		font_selector_set_style (FONT_SELECTOR (page),
			gnm_app_prefs->default_font.is_bold,
			gnm_app_prefs->default_font.is_italic);
}

static gboolean
cb_pref_font_has_changed (G_GNUC_UNUSED FontSelector *fs,
			  GnmStyle *mstyle, PrefState *state)
{
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		gnm_gconf_set_default_font_size 
			(gnm_style_get_font_size (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_NAME))
		gnm_gconf_set_default_font_name (
			gnm_style_get_font_name (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		gnm_gconf_set_default_font_bold (
			gnm_style_get_font_bold (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		gnm_gconf_set_default_font_italic (
			gnm_style_get_font_italic (mstyle));
	return TRUE;
}

static GtkWidget *
pref_font_initializer (PrefState *state,
		       G_GNUC_UNUSED gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = font_selector_new ();

	cb_pref_font_set_fonts (NULL, NULL, page);

	connect_notification (state->root, GNM_CONF_FONT_DIR,
		(GOConfMonitorFunc) cb_pref_font_set_fonts,
		page, page);
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
cb_pref_font_hf_set_fonts (GOConfNode *node, char const *key, GtkWidget *page)
{
	node = gnm_conf_get_root ();
	if (!key ||
	    g_str_has_suffix (key, PRINTSETUP_GCONF_HF_FONT_NAME)) {
		gchar *name = go_conf_load_string (
			node, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HF_FONT_NAME);
		font_selector_set_name (FONT_SELECTOR (page), name);
		g_free (name);
	}
	if (!key ||
	    g_str_has_suffix (key, PRINTSETUP_GCONF_HF_FONT_SIZE))
		font_selector_set_points (FONT_SELECTOR (page),
			go_conf_get_double (
				node, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HF_FONT_SIZE));
	if (!key ||
	    g_str_has_suffix (key, PRINTSETUP_GCONF_HF_FONT_BOLD) ||
	    g_str_has_suffix (key, PRINTSETUP_GCONF_HF_FONT_ITALIC))
		font_selector_set_style (FONT_SELECTOR (page),
			go_conf_get_bool (
				node, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HF_FONT_BOLD),
			go_conf_get_bool (
				node, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HF_FONT_ITALIC));
}

static gboolean
cb_pref_font_hf_has_changed (G_GNUC_UNUSED FontSelector *fs,
			     GnmStyle *mstyle, PrefState *state)
{
	gnm_gconf_set_hf_font (mstyle);
	return TRUE;
}

static GtkWidget *
pref_font_hf_initializer (PrefState *state,
			  G_GNUC_UNUSED gpointer data,
			  G_GNUC_UNUSED GtkNotebook *notebook,
			  G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = font_selector_new ();

	cb_pref_font_hf_set_fonts (state->root, NULL, page);
	connect_notification (state->root, PRINTSETUP_GCONF_DIR,
		(GOConfMonitorFunc) cb_pref_font_hf_set_fonts,
		page, page);
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
	GOConfNode *node;

	node = go_conf_get_node (state->root, GNM_CONF_UNDO_DIR);
	int_pref_create_widget (node, GNM_CONF_UNDO_MAX_DESCRIPTOR_WIDTH,
				page, row++, 5, 5, 200, 1, 
				gnm_gconf_set_max_descriptor_width,
				_("Length of Undo Descriptors"));
	int_pref_create_widget (node, GNM_CONF_UNDO_SIZE,
				page, row++, 1000, 0, 30000, 100, 
				gnm_gconf_set_undo_size,
				_("Maximal Undo Size"));
	int_pref_create_widget (node, GNM_CONF_UNDO_MAXNUM,
				page, row++, 20, 1, 200, 1, 
				gnm_gconf_set_undo_max_number,
				_("Number of Undo Items"));
	bool_pref_create_widget (node, GNM_CONF_UNDO_SHOW_SHEET_NAME,
				 page, row++, 
				 gnm_gconf_set_show_sheet_name,
				_("Show Sheet Name in Undo List"));
	go_conf_free_node (node);

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
	GOConfNode *node;

	node = go_conf_get_node (state->root, GNM_CONF_SORT_DIR);
	int_pref_create_widget (node, GNM_CONF_SORT_DIALOG_MAX_INITIAL,
				page, row++, 10, 0, 50, 1, 
				gnm_gconf_set_sort_dialog_max_initial,
				_("Number of Automatic Clauses"));
	bool_pref_create_widget (node, GNM_CONF_SORT_DEFAULT_RETAIN_FORM,
				 page, row++, 
				 gnm_gconf_set_sort_retain_form,
				 _("Sorting Preserves Formats"));
	bool_pref_create_widget (node, GNM_CONF_SORT_DEFAULT_BY_CASE,
				 page, row++, 
				 gnm_gconf_set_sort_by_case,
				 _("Sorting is Case-Sensitive"));
	bool_pref_create_widget (node, GNM_CONF_SORT_DEFAULT_ASCENDING,
				 page, row++, 
				 gnm_gconf_set_sort_ascending,
				 _("Sort Ascending"));
	bool_pref_create_widget (node, GNM_CONF_SORT_DEFAULT_HAS_HEADER,
				 page, row++, 
				 gnm_gconf_set_sort_has_header,
				 _("Sort Area Has a HEADER"));
	go_conf_free_node (node);

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
	GOConfNode *node;
	
	node = go_conf_get_node (state->root, GNM_CONF_GUI_DIR);
	double_pref_create_widget (node, GNM_CONF_GUI_WINDOW_Y,
				   page, row++, 0.75, 0.25, 1, 0.05, 2, 
				   gnm_gconf_set_gui_window_y,
				   _("Default Vertical Window Size"));
	double_pref_create_widget (node, GNM_CONF_GUI_WINDOW_X,
				   page, row++, 0.75, 0.25, 1, 0.05, 2, 
				   gnm_gconf_set_gui_window_x,
				   _("Default Horizontal Window Size"));
	double_pref_create_widget (node, GNM_CONF_GUI_ZOOM,
				   page, row++, 1.00, 0.10, 5.00, 0.05, 2, 
				   gnm_gconf_set_gui_zoom,
				   _("Default Zoom Factor"));
	int_pref_create_widget (state->root, GNM_CONF_WORKBOOK_NSHEETS,
				page, row++, 1, 1, 64, 1, 
				gnm_gconf_set_workbook_nsheets,
				_("Default Number of Sheets"));
	bool_pref_create_widget (node, GNM_CONF_GUI_ED_TRANSITION_KEYS,
				 page, row++, 
				 gnm_gconf_set_gui_transition_keys,
				 _("Transition Keys"));
	bool_pref_create_widget (node, GNM_CONF_GUI_ED_LIVESCROLLING,
				 page, row++, 
				 gnm_gconf_set_gui_livescrolling,
				 _("Live Scrolling"));
	go_conf_free_node (node);

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
	GOConfNode *node;

	node = go_conf_get_node (state->root, GNM_CONF_FILE_DIR);
	int_pref_create_widget (node, GNM_CONF_FILE_HISTORY_N,
				page, row++, 4, 0, 40, 1, 
				gnm_gconf_set_file_history_number,
				_("Length of File History"));
	int_pref_create_widget (state->root, GNM_CONF_XML_COMPRESSION,
				page, row++, 9, 0, 9, 1, 
				gnm_gconf_set_xml_compression,
				_("Default Compression Level For "
				  "Gnumeric Files"));
	bool_pref_create_widget (node, GNM_CONF_FILE_OVERWRITE_DEFAULT,
				 page, row++, 
				 gnm_gconf_set_file_overwrite,
				 _("Default To Overwriting Files"));
	bool_pref_create_widget (node, GNM_CONF_FILE_SINGLE_SHEET_SAVE,
				 page, row++, 
				 gnm_gconf_set_file_single_sheet_save,
				 _("Warn When Exporting Into Single "
				   "Sheet Format"));
	bool_pref_create_widget (state->root,
				 PLUGIN_GCONF_LATEX "/" PLUGIN_GCONF_LATEX_USE_UTF8,
				 page, row++, 
				 gnm_gconf_set_latex_use_utf8,
				 _("Use UTF-8 in LaTeX Export"));
	go_conf_free_node (node);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Screen Preferences Page                                           */
/*******************************************************************************************/

static void
pref_screen_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page are related to "
					"the screen layout and resolution."));
}

static GtkWidget *
pref_screen_page_initializer (PrefState *state,
			      G_GNUC_UNUSED gpointer data,
			      G_GNUC_UNUSED GtkNotebook *notebook,
			      G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;
	GOConfNode *node;
	
	node = go_conf_get_node (state->root, GNM_CONF_GUI_DIR);
	double_pref_create_widget (node, GNM_CONF_GUI_RES_H, page, row++,
				   96, 50, 250, 1, 1, 
				   gnm_gconf_set_gui_resolution_h,
				   _("Horizontal DPI"));
	double_pref_create_widget (node, GNM_CONF_GUI_RES_V, page, row++,
				   96, 50, 250, 1, 1, 
				   gnm_gconf_set_gui_resolution_v,
				   _("Vertical DPI"));
	go_conf_free_node (node);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Tool Preferences Page                                               */
/*******************************************************************************************/

static void
pref_tool_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page and its subpages are "
                                        "related to "
					"various gnumeric tools."));
}

static GtkWidget *
pref_tool_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	int_pref_create_widget (state->root,
				FUNCTION_SELECT_GCONF_DIR "/" FUNCTION_SELECT_GCONF_NUM_OF_RECENT,
				page, row++, 10, 0, 40, 1, 
				gnm_gconf_set_num_recent_functions,
				_("Maximum Length of Recently "
				  "Used Functions List"));
	bool_pref_create_widget (state->root,
				 GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_AUTOCOMPLETE,
				 page, row++, 
				 gnm_gconf_set_autocomplete,
				_("Autocomplete"));
	bool_pref_create_widget (state->root,
				 DIALOGS_GCONF_DIR "/" DIALOGS_GCONF_UNFOCUSED_RS,
				 page, row++, 
				 gnm_gconf_set_unfocused_rs,
				_("Allow Unfocused Range Selections"));
	
	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Copy/Paste Preferences Page                                               */
/*******************************************************************************************/

static void
pref_copypaste_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
			  G_GNUC_UNUSED GtkNotebook *notebook,
			  G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page are "
                                        "related to "
					"copy, cut and paste."));
}

static GtkWidget *
pref_copypaste_page_initializer (PrefState *state,
				 G_GNUC_UNUSED gpointer data,
				 G_GNUC_UNUSED GtkNotebook *notebook,
				 G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	bool_pref_create_widget (state->root,
				 GNM_CONF_CUTANDPASTE_DIR "/" GNM_CONF_CUTANDPASTE_PREFER_CLIPBOARD,
				 page, row++, 
				 gnm_gconf_set_prefer_clipboard,
				 _("Prefer CLIPBOARD over PRIMARY selection"));
	
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

static const page_info_t page_info[] = {
	{N_("Font"),          GTK_STOCK_ITALIC,	         NULL, &pref_font_initializer,		&pref_font_page_open,	NULL},
	{N_("Copy and Paste"),GTK_STOCK_PASTE,		 NULL, &pref_copypaste_page_initializer,&pref_copypaste_page_open,	NULL},
	{N_("Files"),         GTK_STOCK_FLOPPY,	         NULL, &pref_file_page_initializer,	&pref_file_page_open,	NULL},
	{N_("Tools"),       GTK_STOCK_EXECUTE,           NULL, &pref_tool_page_initializer,	&pref_tool_page_open,	NULL},
	{N_("Undo"),          GTK_STOCK_UNDO,		 NULL, &pref_undo_page_initializer,	&pref_undo_page_open,	NULL},
	{N_("Windows"),       "Gnumeric_ObjectCombo",	 NULL, &pref_window_page_initializer,	&pref_window_page_open,	NULL},
	{N_("Header/Footer"), GTK_STOCK_ITALIC,	         "0",  &pref_font_hf_initializer,	&pref_font_hf_page_open, NULL},
	{N_("Sorting"),       GTK_STOCK_SORT_ASCENDING,  "3", &pref_sort_page_initializer,	&pref_sort_page_open,	NULL},
	{N_("Screen"),        GTK_STOCK_PREFERENCES,     "5", &pref_screen_page_initializer,	&pref_screen_page_open,	NULL},
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

static void
cb_preferences_destroy (PrefState *state)
{
	go_conf_sync (state->root);
	if (state->store)
		g_object_unref (state->store);
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
	gnm_app_set_pref_dialog (NULL);
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
static char const * const startup_pages[] = {"1", "0"};

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

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"preferences.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new0 (PrefState, 1);
	state->root = gnm_conf_get_root ();
	state->gui = gui;
	state->dialog     = glade_xml_get_widget (gui, "preferences");
	state->notebook   = glade_xml_get_widget (gui, "notebook");
	state->description = GTK_TEXT_VIEW (glade_xml_get_widget (gui, "description"));
	state->wb	  = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));

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
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_preferences_destroy);

	gnm_app_set_pref_dialog (state->dialog);

	for (i = 0; page_info[i].page_initializer; i++) {
		const page_info_t *this_page =  &page_info[i];
		GtkWidget *page = this_page->page_initializer (state, this_page->data,
							       GTK_NOTEBOOK (state->notebook), i);
		GtkWidget *label = NULL;

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
