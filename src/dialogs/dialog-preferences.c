/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-preferences.c: Dialog to edit application wide preferences and default values
 *
 * Author:
 *	Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include "workbook-control.h"
#include "wbc-gtk.h"
#include "number-match.h"
#include "widgets/widget-font-selector.h"
#include "widgets/gnumeric-cell-renderer-text.h"

#include "gnumeric-gconf-priv.h"
#include "gnumeric-gconf.h"

#include <gui-util.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define PREF_DIALOG_KEY "pref-dialog"

enum {
	ITEM_ICON,
	ITEM_NAME,
	PAGE_NUMBER,
	NUM_COLUMNS
};

typedef struct {
	GladeXML	*gui;
	GtkWidget	*dialog;
	GtkNotebook	*notebook;
	GtkTreeStore    *store;
	GtkTreeView     *view;
	GOConfNode	*root;
	gulong          app_wb_removed_sig;
} PrefState;

typedef void (* double_conf_setter_t) (gnm_float value);
typedef void (* gint_conf_setter_t) (gint value);
typedef void (* gboolean_conf_setter_t) (gboolean value);
typedef void (* enum_conf_setter_t) (int value);

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
set_tip (GOConfNode *node, char const *key, GtkWidget *w)
{
	char *desc = go_conf_get_long_desc (node, key);
	if (desc != NULL) {
		go_widget_set_tooltip_text (w, desc);
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
pref_create_label (GOConfNode *node, char const *key, GtkWidget *table,
		   gint row, gchar const *default_label, GtkWidget *w)
{
	GtkWidget *label;

	if (NULL == default_label) {
		char *desc = go_conf_get_short_desc (node, key);
		label = gtk_label_new (desc);
		g_free (desc);
	} else
		label = gtk_label_new_with_mnemonic (default_label);

	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1, 
		GTK_FILL | GTK_EXPAND,
		GTK_FILL | GTK_SHRINK, 5, 2);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), w);
	go_atk_setup_label (label, w);
}

/*************************************************************************/

static void
bool_pref_widget_to_conf (GtkToggleButton *button, 
			  gboolean_conf_setter_t setter)
{
	setter (gtk_toggle_button_get_active (button));
}
static void
bool_pref_conf_to_widget (GOConfNode *node, char const *key, GtkToggleButton *button)
{
	gboolean val_in_button = gtk_toggle_button_get_active (button);
	gboolean val_in_conf = go_conf_get_bool (node, key);
	if ((!val_in_button) != (!val_in_conf))
		gtk_toggle_button_set_active (button, val_in_conf);
}
static void
bool_pref_create_widget (GOConfNode *node, char const *key, GtkWidget *table,
			 gint row, gboolean_conf_setter_t setter, 
			 char const *default_label)
{
	char *desc = go_conf_get_short_desc (node, key);
	GtkWidget *item = gtk_check_button_new_with_label (
		(desc != NULL) ? desc : default_label);

	g_free (desc);

	bool_pref_conf_to_widget (node, key, GTK_TOGGLE_BUTTON (item));
	g_signal_connect (G_OBJECT (item), "toggled",
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
cb_enum_changed (GtkComboBox *combo, enum_conf_setter_t setter)
{
	GtkTreeIter  iter;
	if (gtk_combo_box_get_active_iter (combo, &iter)) {
		GtkTreeModel *model = gtk_combo_box_get_model (combo);
		GEnumValue *enum_val;
		gtk_tree_model_get (model, &iter, 1, &enum_val, -1);
		(*setter) (enum_val->value);
	}
}

typedef struct {
	char		*val;
	GtkComboBox	*combo;
} FindEnumClosure;

static  gboolean
cb_find_enum (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
	      FindEnumClosure *close)
{
	gboolean res = FALSE;
	char *combo_val;

	g_return_val_if_fail (model != NULL, FALSE);
	g_return_val_if_fail (close->val != NULL, FALSE);

	gtk_tree_model_get (model, iter, 0, &combo_val, -1);
	if (combo_val) {
		if (0 == strcmp (close->val, combo_val)) {
			res = TRUE;
			gtk_combo_box_set_active_iter (close->combo, iter);
		}
		g_free (combo_val);
	}
	return res;
}

static void
enum_pref_conf_to_widget (GOConfNode *node, char const *key, GtkComboBox *combo)
{
	FindEnumClosure close;
	GtkTreeModel *model = gtk_combo_box_get_model (combo);

	close.combo = combo;
	close.val   = go_conf_get_enum_as_str (node, key);
	if (NULL != close.val) {	/* in case go_conf fails */
		gtk_tree_model_foreach (model,
			(GtkTreeModelForeachFunc) cb_find_enum, &close);
		g_free (close.val);
	}
}

static void
enum_pref_create_widget (GOConfNode *node, char const *key, GtkWidget *table,
			 gint row, GType enum_type,
			 enum_conf_setter_t setter,
			 gchar const *default_label)
{
	unsigned int	 i;
	GtkTreeIter	 iter;
	GtkCellRenderer	*renderer;
	GEnumClass	*enum_class = G_ENUM_CLASS (g_type_class_ref (enum_type));
	GtkWidget	*combo = gtk_combo_box_new ();
	GtkListStore	*model = gtk_list_store_new (2,
		G_TYPE_STRING, G_TYPE_POINTER);

	for (i = 0; i < enum_class->n_values ; i++) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
			0,	enum_class->values[i].value_nick,
			1,	enum_class->values + i,
			-1);
	}
	
	g_type_class_unref (enum_class);

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (model));
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), renderer, "text", 0, NULL);

	enum_pref_conf_to_widget (node, key, GTK_COMBO_BOX (combo));
	gtk_table_attach (GTK_TABLE (table), combo,
		1, 2, row, row + 1,
		GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);

	g_signal_connect (G_OBJECT (combo), "changed",
		G_CALLBACK (cb_enum_changed), (gpointer) setter);
	connect_notification (node, key,
		(GOConfMonitorFunc)enum_pref_conf_to_widget, combo, table);

	pref_create_label (node, key, table, row, default_label, combo);
	set_tip (node, key, combo);
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
			gint_conf_setter_t setter, char const *default_label)
{
	GtkWidget *w = gtk_spin_button_new (GTK_ADJUSTMENT (
		gtk_adjustment_new (val, from, to, step, step, step)),
		1, 0);
	int_pref_conf_to_widget (node, key, GTK_SPIN_BUTTON (w));
	gtk_table_attach (GTK_TABLE (table), w,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 2);

	g_signal_connect (G_OBJECT (w), "value-changed",
		G_CALLBACK (int_pref_widget_to_conf), (gpointer) setter);
	connect_notification (node, key,
		(GOConfMonitorFunc)int_pref_conf_to_widget, w, table);

	pref_create_label (node, key, table, row, default_label, w);
	set_tip (node, key, w);
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
			   char const *default_label)
{
	GtkWidget *w =  gtk_spin_button_new (GTK_ADJUSTMENT (
		gtk_adjustment_new (val, from, to, step, step, step)),
		1, digits);
	double_pref_conf_to_widget (node, key, GTK_SPIN_BUTTON (w));
	gtk_table_attach (GTK_TABLE (table), w,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 2);

	g_signal_connect (G_OBJECT (w), "value-changed",
		G_CALLBACK (double_pref_widget_to_conf), (gpointer) setter);
	connect_notification (node, key,
		(GOConfMonitorFunc)double_pref_conf_to_widget, w, table);

	pref_create_label (node, key, table, row, default_label, w);
	set_tip (node, key, w);
}

/*******************************************************************************************/
/*                     Default Font Selector                                               */
/*******************************************************************************************/

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

static GtkWidget *
pref_tool_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	enum_pref_create_widget (state->root,
				 GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_ENTER_MOVES_DIR,
				 page, row++, 
				 GO_DIRECTION_TYPE,
				 (enum_conf_setter_t) gnm_gconf_set_enter_moves_dir,
				 _("Enter _Moves Selection"));
	bool_pref_create_widget (state->root,
				 GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_TRANSITION_KEYS,
				 page, row++, 
				 gnm_gconf_set_gui_transition_keys,
				 _("Transition Keys"));
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
	int_pref_create_widget (state->root,
				FUNCTION_SELECT_GCONF_DIR "/" FUNCTION_SELECT_GCONF_NUM_OF_RECENT,
				page, row++, 10, 0, 40, 1, 
				gnm_gconf_set_num_recent_functions,
				_("Maximum Length of Recently "
				  "Used Functions List"));
	
	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Copy/Paste Preferences Page                                               */
/*******************************************************************************************/

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
	gpointer data;
} page_info_t;

static page_info_t const page_info[] = {
	{N_("Font"),          GTK_STOCK_ITALIC,		 NULL, &pref_font_initializer,		NULL},
	{N_("Copy and Paste"),GTK_STOCK_PASTE,		 NULL, &pref_copypaste_page_initializer,NULL},
	{N_("Files"),         GTK_STOCK_FLOPPY,		 NULL, &pref_file_page_initializer,	NULL},
	{N_("Tools"),       GTK_STOCK_EXECUTE,           NULL, &pref_tool_page_initializer,	NULL},
	{N_("Undo"),          GTK_STOCK_UNDO,		 NULL, &pref_undo_page_initializer,	NULL},
	{N_("Windows"),       "Gnumeric_ObjectCombo",	 NULL, &pref_window_page_initializer,	NULL},
	{N_("Header/Footer"), GTK_STOCK_ITALIC,		 "0",  &pref_font_hf_initializer,	NULL},
	{N_("Sorting"),       GTK_STOCK_SORT_ASCENDING,  "3", &pref_sort_page_initializer,	NULL},
	{N_("Screen"),        GTK_STOCK_PREFERENCES,     "5", &pref_screen_page_initializer,	NULL},
	{NULL, NULL, NULL, NULL, NULL },
};

static void
dialog_pref_select_page (PrefState *state, char const *page)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->view);
	GtkTreeIter iter;
	GtkTreePath *path;

	path = gtk_tree_path_new_from_string (page);
	
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
		gtk_notebook_set_current_page (state->notebook, page);
	} else {
		dialog_pref_select_page (state, "0");
	}
}

static void
cb_preferences_destroy (PrefState *state)
{
	go_conf_sync (state->root);
	if (state->store) {
		g_object_unref (state->store);
		state->store = NULL;
	}
	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	if (state->app_wb_removed_sig) {
		g_signal_handler_disconnect (gnm_app_get_app (),
					     state->app_wb_removed_sig);
		state->app_wb_removed_sig = 0;
	}
	g_object_set_data (gnm_app_get_app (), PREF_DIALOG_KEY, NULL);
}

static void
cb_close_clicked (PrefState *state)
{
	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_workbook_removed (PrefState *state)
{
	if (gnm_app_workbook_list () == NULL)
		cb_close_clicked (state);
}


/* Note: The first page listed below is opened through File/Preferences, */
/*       and the second through Format/Workbook */
static char const * const startup_pages[] = {"1", "0"};

void
dialog_preferences (WBCGtk *wbcg, gint page)
{
	PrefState *state;
	GladeXML *gui;
	GtkWidget *w;
	gint i;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;

	w = g_object_get_data (gnm_app_get_app (), PREF_DIALOG_KEY);
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
	state->notebook = (GtkNotebook*)glade_xml_get_widget (gui, "notebook");

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

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_PREFERENCES);
	g_signal_connect_swapped (G_OBJECT (state->dialog), "destroy",
				  G_CALLBACK (cb_preferences_destroy),
				  state);
	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state,	(GDestroyNotify)g_free);

	g_object_set_data (gnm_app_get_app (), PREF_DIALOG_KEY, state->dialog);

	state->app_wb_removed_sig =
		g_signal_connect_swapped (gnm_app_get_app (),
					  "workbook_removed",
					  G_CALLBACK (cb_workbook_removed),
					  state);

	for (i = 0; page_info[i].page_initializer; i++) {
		const page_info_t *this_page =  &page_info[i];
		GtkWidget *page =
			this_page->page_initializer (state, this_page->data,
						     state->notebook, i);
		GtkWidget *label = NULL;

		if (this_page->icon_name)
			label = gtk_image_new_from_stock (this_page->icon_name,
							  GTK_ICON_SIZE_BUTTON);
		else if (this_page->page_name)
			label = gtk_label_new (this_page->page_name);
		gtk_notebook_append_page (state->notebook, page, label);
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
