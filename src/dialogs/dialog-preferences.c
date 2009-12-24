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

typedef void (* double_conf_setter_t) (double value);
typedef void (* gint_conf_setter_t) (gint value);
typedef void (* gboolean_conf_setter_t) (gboolean value);
typedef void (* enum_conf_setter_t) (int value);
typedef void (* wordlist_conf_setter_t) (GSList *value);

static void
dialog_pref_add_item (PrefState *state, char const *page_name,
		      char const *icon_name,
		      int page, char const* parent_path)
{
	GtkTreeIter iter, parent;
	GdkPixbuf * icon = NULL;

	if (icon_name != NULL)
		icon = gtk_widget_render_icon (state->dialog, icon_name,
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
	if (icon != NULL)
		g_object_unref (icon);
}

static void
set_tip (GOConfNode *node, GtkWidget *w)
{
	char *desc = go_conf_get_long_desc (node, NULL);
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
connect_notification (GOConfNode *node, GOConfMonitorFunc func,
		      gpointer data, GtkWidget *container)
{
	guint handle = go_conf_add_monitor (node, NULL, func, data);
	g_signal_connect_swapped (G_OBJECT (container), "destroy",
		G_CALLBACK (cb_pref_notification_destroy),
		GUINT_TO_POINTER (handle));
}

/*************************************************************************/

static void
pref_create_label (GOConfNode *node, GtkWidget *table,
		   gint row, gchar const *default_label, GtkWidget *w)
{
	GtkWidget *label;

	if (NULL == default_label) {
		char *desc = go_conf_get_short_desc (node, NULL);
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
	GOConfNode *node = g_object_get_data (G_OBJECT (button), "node");
	gboolean val_in_button = gtk_toggle_button_get_active (button);
	gboolean val_in_conf = go_conf_get_bool (node, NULL);
	if ((!val_in_button) != (!val_in_conf))
		setter (val_in_button);
}

static void
bool_pref_conf_to_widget (GOConfNode *node, G_GNUC_UNUSED char const *key,
			  GtkToggleButton *button)
{
	gboolean val_in_button = gtk_toggle_button_get_active (button);
	gboolean val_in_conf = go_conf_get_bool (node, NULL);
	if ((!val_in_button) != (!val_in_conf))
		gtk_toggle_button_set_active (button, val_in_conf);
}

static void
bool_pref_create_widget (GOConfNode *node, GtkWidget *table,
			 gint row, gboolean_conf_setter_t setter,
			 char const *default_label)
{
	char *desc = go_conf_get_short_desc (node, NULL);
	GtkWidget *item = gtk_check_button_new_with_label (
		(desc != NULL) ? desc : default_label);

	g_free (desc);

	bool_pref_conf_to_widget (node, NULL, GTK_TOGGLE_BUTTON (item));
	g_object_set_data (G_OBJECT (item), "node", node);
	g_signal_connect (G_OBJECT (item), "toggled",
			  G_CALLBACK (bool_pref_widget_to_conf),
			  (gpointer) setter);
	gtk_table_attach (GTK_TABLE (table), item,
		0, 2, row, row + 1,
		GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);

	connect_notification (node, (GOConfMonitorFunc)bool_pref_conf_to_widget,
			      item, table);
	set_tip (node, item);
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
	      FindEnumClosure *cls)
{
	gboolean res = FALSE;
	char *combo_val;

	g_return_val_if_fail (model != NULL, FALSE);
	g_return_val_if_fail (cls->val != NULL, FALSE);

	gtk_tree_model_get (model, iter, 0, &combo_val, -1);
	if (combo_val) {
		if (0 == strcmp (cls->val, combo_val)) {
			res = TRUE;
			gtk_combo_box_set_active_iter (cls->combo, iter);
		}
		g_free (combo_val);
	}
	return res;
}

static void
enum_pref_conf_to_widget (GOConfNode *node, G_GNUC_UNUSED char const *key,
			  GtkComboBox *combo)
{
	FindEnumClosure cls;
	GtkTreeModel *model = gtk_combo_box_get_model (combo);

	cls.combo = combo;
	cls.val   = go_conf_get_enum_as_str (node, NULL);
	if (NULL != cls.val) {	/* in case go_conf fails */
		gtk_tree_model_foreach (model,
					(GtkTreeModelForeachFunc)cb_find_enum,
					&cls);
		g_free (cls.val);
	}
}

static void
enum_pref_create_widget (GOConfNode *node, GtkWidget *table,
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

	enum_pref_conf_to_widget (node, NULL, GTK_COMBO_BOX (combo));
	gtk_table_attach (GTK_TABLE (table), combo,
		1, 2, row, row + 1,
		GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);

	g_signal_connect (G_OBJECT (combo), "changed",
		G_CALLBACK (cb_enum_changed), (gpointer) setter);
	connect_notification (node, (GOConfMonitorFunc)enum_pref_conf_to_widget,
			      combo, table);

	pref_create_label (node, table, row, default_label, combo);
	set_tip (node, combo);
}

/*************************************************************************/

static void
int_pref_widget_to_conf (GtkSpinButton *button, gint_conf_setter_t setter)
{
	GOConfNode *node = g_object_get_data (G_OBJECT (button), "node");
	gint val_in_button = gtk_spin_button_get_value_as_int (button);
	gint val_in_conf = go_conf_get_int (node, NULL);

	if (val_in_conf != val_in_button)
		setter (val_in_button);
}

static void
int_pref_conf_to_widget (GOConfNode *node, G_GNUC_UNUSED char const *key,
			 GtkSpinButton *button)
{
	gint val_in_button = gtk_spin_button_get_value_as_int (button);
	gint val_in_conf = go_conf_get_int (node, NULL);

	if (val_in_conf != val_in_button)
		gtk_spin_button_set_value (button, (gdouble) val_in_conf);
}

static GtkWidget *
int_pref_create_widget (GOConfNode *node, GtkWidget *table,
			gint row, gint val, gint from, gint to, gint step,
			gint_conf_setter_t setter, char const *default_label)
{
	GtkAdjustment *adj = GTK_ADJUSTMENT
		(gtk_adjustment_new (val, from, to, step, step, 0));
	GtkWidget *w = gtk_spin_button_new (adj, 1, 0);

	int_pref_conf_to_widget (node, NULL, GTK_SPIN_BUTTON (w));
	g_object_set_data (G_OBJECT (w), "node", node);
	gtk_table_attach (GTK_TABLE (table), w,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 2);

	g_signal_connect (G_OBJECT (w), "value-changed",
			  G_CALLBACK (int_pref_widget_to_conf),
			  (gpointer) setter);
	connect_notification (node, (GOConfMonitorFunc)int_pref_conf_to_widget,
			      w, table);

	pref_create_label (node, table, row, default_label, w);
	set_tip (node, w);
	return w;
}

static gboolean
powerof2 (int i)
{
	return i > 0 && (i & (i - 1)) == 0;
}

static void
cb_power_of_2 (GtkAdjustment *adj)
{
	int val = (int)adj->value;

	if (powerof2 (val - 1))
		gtk_adjustment_set_value (adj, (val - 1) * 2);
	else if (powerof2 (val + 1))
		gtk_adjustment_set_value (adj, (val + 1) / 2);
}

static void
power_of_2_handlers (GtkWidget *w)
{
	GtkSpinButton *spin = GTK_SPIN_BUTTON (w);
	GtkAdjustment *adj = gtk_spin_button_get_adjustment (spin);
	g_signal_connect (G_OBJECT (adj), "value_changed",
			  G_CALLBACK (cb_power_of_2), NULL);
}

/*************************************************************************/

static void
double_pref_widget_to_conf (GtkSpinButton *button, double_conf_setter_t setter)
{
	GOConfNode *node = g_object_get_data (G_OBJECT (button), "node");
	double val_in_button = gtk_spin_button_get_value (button);
	double val_in_conf = go_conf_get_double (node, NULL);

	if (fabs (val_in_conf - val_in_button) > 1e-10) /* dead simple */
		setter (val_in_button);
}

static void
double_pref_conf_to_widget (GOConfNode *node, G_GNUC_UNUSED char const *key,
			    GtkSpinButton *button)
{
	double val_in_button = gtk_spin_button_get_value (button);
	double val_in_conf = go_conf_get_double (node, NULL);

	if (fabs (val_in_conf - val_in_button) > 1e-10) /* dead simple */
		gtk_spin_button_set_value (button, val_in_conf);
}
static void
double_pref_create_widget (GOConfNode *node, GtkWidget *table,
			   gint row, gnm_float val, gnm_float from,gnm_float to,
			   gnm_float step,
			   gint digits, double_conf_setter_t setter,
			   char const *default_label)
{
	GtkWidget *w =  gtk_spin_button_new (GTK_ADJUSTMENT (
		gtk_adjustment_new (val, from, to, step, step, 0)),
		1, digits);
	double_pref_conf_to_widget (node, NULL, GTK_SPIN_BUTTON (w));
	g_object_set_data (G_OBJECT (w), "node", node);
	gtk_table_attach (GTK_TABLE (table), w,
		1, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 2);

	g_signal_connect (G_OBJECT (w), "value-changed",
		G_CALLBACK (double_pref_widget_to_conf), (gpointer) setter);
	connect_notification (node,
			      (GOConfMonitorFunc)double_pref_conf_to_widget,
			      w, table);

	pref_create_label (node, table, row, default_label, w);
	set_tip (node, w);
}


/*************************************************************************/



static void
wordlist_pref_conf_to_widget (GOConfNode *node, G_GNUC_UNUSED char const *key,
			 GtkListStore *store)
{
	GSList *l, *list = go_conf_get_str_list (node, NULL);
	GtkTreeIter  iter;

	gtk_list_store_clear (store);

	for (l = list; l != NULL; l = l->next) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    0, l->data,
				    -1);
		g_free (l->data);
	}
	g_slist_free (list);
}

static void                
wordlist_pref_remove (GtkButton *button, GOConfNode *node) {
	GtkTreeView *tree = g_object_get_data (G_OBJECT (button), "treeview");
	GtkTreeSelection *select = gtk_tree_view_get_selection (tree);
	GtkTreeIter iter;
	GtkTreeModel *model;

	if (gtk_tree_selection_get_selected (select, &model, &iter)) {
		char *text;
		GSList *l, *list = go_conf_get_str_list (node, NULL);

		gtk_tree_model_get (model, &iter,
				    0, &text,
				    -1);
		l = g_slist_find_custom (list, text, (GCompareFunc)strcmp);
		if (l != NULL) {
			g_free (l->data);
			list = g_slist_delete_link (list, l);
			go_conf_set_str_list (node, NULL, list);
		}
		g_free (text);
		go_slist_free_custom (list, g_free);
	}
}

static void                
wordlist_pref_add (GtkButton *button, GOConfNode *node)
{
	GtkEntry *entry = g_object_get_data (G_OBJECT (button), "entry");
	const gchar *text = gtk_entry_get_text (entry);

	if (text[0]) {
		GSList *l, *list = go_conf_get_str_list (node, NULL);
		l = g_slist_find_custom (list, text, (GCompareFunc)strcmp);
		if (l == NULL) {
			list = g_slist_append (list, g_strdup (text));
			go_conf_set_str_list (node, NULL, list);
		}
		go_slist_free_custom (list, g_free);
	}
}

static void
wordlist_pref_update_remove_button (GtkTreeSelection *selection, GtkButton *button)
{
	gtk_widget_set_sensitive (GTK_WIDGET (button),
				  gtk_tree_selection_get_selected (selection, NULL, NULL));
}

static GtkWidget *
wordlist_pref_create_widget (GOConfNode *node, GtkWidget *table,
			     gint row, 
			     char const *default_label) 
{
	GtkWidget *w= gtk_table_new (5, 2, FALSE);
	GtkWidget *sw= gtk_scrolled_window_new (NULL, NULL);
	GtkWidget *tv= gtk_tree_view_new ();
	GtkWidget *entry = gtk_entry_new ();
	GtkWidget *add_button = gtk_button_new_from_stock (GTK_STOCK_ADD);
	GtkWidget *remove_button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
	GtkListStore	*model = gtk_list_store_new (1, G_TYPE_STRING);
	GtkTreeSelection *selection;

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), 
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
					     GTK_SHADOW_ETCHED_IN);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tv), FALSE);
	gtk_container_add (GTK_CONTAINER (sw), tv);
	
	gtk_table_attach (GTK_TABLE (table), w,
		0, 2, row, row + 1,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 5, 2);
	gtk_table_attach (GTK_TABLE (w), sw,
		0, 1, 1, 4,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 5, 2);
	gtk_table_attach (GTK_TABLE (w), entry,
		0, 1, 4, 5,
		GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 5, 2);
	gtk_table_attach (GTK_TABLE (w), remove_button,
		1, 2, 3, 4,
		GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 2);
	gtk_table_attach (GTK_TABLE (w), add_button,
		1, 2, 4, 5,
		GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 2);

	gtk_tree_view_set_model (GTK_TREE_VIEW (tv),
				 GTK_TREE_MODEL (model));
	gtk_tree_view_append_column (GTK_TREE_VIEW (tv),
				     gtk_tree_view_column_new_with_attributes 
				     (NULL,
				      gtk_cell_renderer_text_new (),
				      "text", 0,
				      NULL));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	wordlist_pref_conf_to_widget (node, "", model);

	g_object_set_data (G_OBJECT (remove_button), "treeview", tv);
	g_object_set_data (G_OBJECT (add_button), "entry", entry);
	g_signal_connect (G_OBJECT (remove_button), "clicked",
		G_CALLBACK (wordlist_pref_remove), node);
	g_signal_connect (G_OBJECT (add_button), "clicked",
		G_CALLBACK (wordlist_pref_add), node);
	g_signal_connect (G_OBJECT (selection), "changed",
		G_CALLBACK (wordlist_pref_update_remove_button), remove_button);
	wordlist_pref_update_remove_button (selection, 
					    GTK_BUTTON (remove_button));

	connect_notification (node, (GOConfMonitorFunc)wordlist_pref_conf_to_widget,
			      model, table);

	pref_create_label (node, w, 0, default_label, tv);
	set_tip (node, tv);
	return w;
}

/*******************************************************************************************/
/*                     Default Font Selector                                               */
/*******************************************************************************************/

static void
cb_pref_font_set_fonts (G_GNUC_UNUSED GOConfNode *node,
			G_GNUC_UNUSED char const *key,
			GtkWidget *page)
{
	FontSelector *fs = FONT_SELECTOR (page);

	font_selector_set_name (fs, gnm_conf_get_core_defaultfont_name ());
	font_selector_set_points (fs, gnm_conf_get_core_defaultfont_size ());
	font_selector_set_style (fs,
				 gnm_conf_get_core_defaultfont_bold (),
				 gnm_conf_get_core_defaultfont_italic ());
}

static gboolean
cb_pref_font_has_changed (G_GNUC_UNUSED FontSelector *fs,
			  GnmStyle *mstyle, PrefState *state)
{
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		gnm_conf_set_core_defaultfont_size
			(gnm_style_get_font_size (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_NAME))
		gnm_conf_set_core_defaultfont_name
			(gnm_style_get_font_name (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		gnm_conf_set_core_defaultfont_bold
			(gnm_style_get_font_bold (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		gnm_conf_set_core_defaultfont_italic
			(gnm_style_get_font_italic (mstyle));
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

	connect_notification (gnm_conf_get_core_defaultfont_dir_node (),
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
cb_pref_font_hf_set_fonts (G_GNUC_UNUSED GOConfNode *node,
			   G_GNUC_UNUSED char const *key,
			   GtkWidget *page)
{
	FontSelector *fs = FONT_SELECTOR (page);

	font_selector_set_name (fs, gnm_conf_get_printsetup_hf_font_name ());
	font_selector_set_points (fs, gnm_conf_get_printsetup_hf_font_size ());
	font_selector_set_style (fs,
				 gnm_conf_get_printsetup_hf_font_bold (),
				 gnm_conf_get_printsetup_hf_font_italic ());
}

static gboolean
cb_pref_font_hf_has_changed (G_GNUC_UNUSED FontSelector *fs,
			     GnmStyle *mstyle, PrefState *state)
{
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		gnm_conf_set_printsetup_hf_font_size
			(gnm_style_get_font_size (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_NAME))
		gnm_conf_set_printsetup_hf_font_name
			(gnm_style_get_font_name (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		gnm_conf_set_printsetup_hf_font_bold
			(gnm_style_get_font_bold (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		gnm_conf_set_printsetup_hf_font_italic
			(gnm_style_get_font_italic (mstyle));

	return TRUE;
}

static GtkWidget *
pref_font_hf_initializer (PrefState *state,
			  G_GNUC_UNUSED gpointer data,
			  G_GNUC_UNUSED GtkNotebook *notebook,
			  G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = font_selector_new ();

	cb_pref_font_hf_set_fonts (NULL, NULL, page);
	connect_notification (gnm_conf_get_printsetup_dir_node (),
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

	int_pref_create_widget (gnm_conf_get_undo_max_descriptor_width_node (),
				page, row++, 5, 5, 200, 1,
				gnm_conf_set_undo_max_descriptor_width,
				_("Length of Undo Descriptors"));
	int_pref_create_widget (gnm_conf_get_undo_size_node (),
				page, row++, 1000, 0, 30000, 100,
				gnm_conf_set_undo_size,
				_("Maximal Undo Size"));
	int_pref_create_widget (gnm_conf_get_undo_maxnum_node (),
				page, row++, 20, 1, 200, 1,
				gnm_conf_set_undo_maxnum,
				_("Number of Undo Items"));
	bool_pref_create_widget (gnm_conf_get_undo_show_sheet_name_node (),
				 page, row++,
				 gnm_conf_set_undo_show_sheet_name,
				_("Show Sheet Name in Undo List"));

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

	int_pref_create_widget (gnm_conf_get_core_sort_dialog_max_initial_clauses_node (),
				page, row++, 10, 0, 50, 1,
				gnm_conf_set_core_sort_dialog_max_initial_clauses,
				_("Number of Automatic Clauses"));
	bool_pref_create_widget (gnm_conf_get_core_sort_default_retain_formats_node (),
				 page, row++,
				 gnm_conf_set_core_sort_default_retain_formats,
				 _("Sorting Preserves Formats"));
	bool_pref_create_widget (gnm_conf_get_core_sort_default_by_case_node (),
				 page, row++,
				 gnm_conf_set_core_sort_default_by_case,
				 _("Sorting is Case-Sensitive"));
	bool_pref_create_widget (gnm_conf_get_core_sort_default_ascending_node (),
				 page, row++,
				 gnm_conf_set_core_sort_default_ascending,
				 _("Sort Ascending"));

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
	GtkWidget *page = gtk_table_new (7, 2, FALSE);
	gint row = 0;
	GtkWidget *w;

	double_pref_create_widget (gnm_conf_get_core_gui_window_y_node (),
				   page, row++, 0.75, 0.25, 1, 0.05, 2,
				   gnm_conf_set_core_gui_window_y,
				   _("Default Vertical Window Size"));
	double_pref_create_widget (gnm_conf_get_core_gui_window_x_node (),
				   page, row++, 0.75, 0.25, 1, 0.05, 2,
				   gnm_conf_set_core_gui_window_x,
				   _("Default Horizontal Window Size"));
	double_pref_create_widget (gnm_conf_get_core_gui_window_zoom_node (),
				   page, row++, 1.00, 0.10, 5.00, 0.05, 2,
				   gnm_conf_set_core_gui_window_zoom,
				   _("Default Zoom Factor"));
	int_pref_create_widget (gnm_conf_get_core_workbook_n_sheet_node (),
				page, row++, 1, 1, 64, 1,
				gnm_conf_set_core_workbook_n_sheet,
				_("Default Number of Sheets"));

	w = int_pref_create_widget (gnm_conf_get_core_workbook_n_rows_node (),
				    page, row++,
				    GNM_DEFAULT_ROWS, GNM_MIN_ROWS, GNM_MAX_ROWS, 1,
				    gnm_conf_set_core_workbook_n_rows,
				    _("Default Number of Rows in a Sheet"));
	power_of_2_handlers (w);

	w = int_pref_create_widget (gnm_conf_get_core_workbook_n_cols_node (),
				    page, row++,
				    GNM_DEFAULT_COLS, GNM_MIN_COLS, GNM_MAX_COLS, 1,
				    gnm_conf_set_core_workbook_n_cols,
				    _("Default Number of Columns in a Sheet"));
	power_of_2_handlers (w);

	bool_pref_create_widget (gnm_conf_get_core_gui_editing_livescrolling_node (),
				 page, row++,
				 gnm_conf_set_core_gui_editing_livescrolling,
				 _("Live Scrolling"));

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

	int_pref_create_widget (gnm_conf_get_core_xml_compression_level_node (),
				page, row++, 9, 0, 9, 1,
				gnm_conf_set_core_xml_compression_level,
				_("Default Compression Level For "
				  "Gnumeric Files"));
	int_pref_create_widget (gnm_conf_get_core_workbook_autosave_time_node (),
				page, row++, 0, 0, 365*24*60*60, 60,
				gnm_conf_set_core_workbook_autosave_time,
				_("Default autosave frequency in seconds"));
	bool_pref_create_widget (gnm_conf_get_core_file_save_def_overwrite_node (),
				 page, row++,
				 gnm_conf_set_core_file_save_def_overwrite,
				 _("Default To Overwriting Files"));
	bool_pref_create_widget (gnm_conf_get_core_file_save_single_sheet_node (),
				 page, row++,
				 gnm_conf_set_core_file_save_single_sheet,
				 _("Warn When Exporting Into Single "
				   "Sheet Format"));
	bool_pref_create_widget (gnm_conf_get_plugin_latex_use_utf8_node (),
				 page, row++,
				 gnm_conf_set_plugin_latex_use_utf8,
				 _("Use UTF-8 in LaTeX Export"));

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

	double_pref_create_widget (gnm_conf_get_core_gui_screen_horizontaldpi_node (),
				   page, row++, 96, 50, 250, 1, 1,
				   gnm_conf_set_core_gui_screen_horizontaldpi,
				   _("Horizontal DPI"));
	double_pref_create_widget (gnm_conf_get_core_gui_screen_verticaldpi_node (),
				   page, row++, 96, 50, 250, 1, 1,
				   gnm_conf_set_core_gui_screen_verticaldpi,
				   _("Vertical DPI"));

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

	enum_pref_create_widget (gnm_conf_get_core_gui_editing_enter_moves_dir_node (),
				 page, row++,
				 GO_TYPE_DIRECTION,
				 (enum_conf_setter_t)gnm_conf_set_core_gui_editing_enter_moves_dir,
				 _("Enter _Moves Selection"));
	bool_pref_create_widget (gnm_conf_get_core_gui_editing_transitionkeys_node (),
				 page, row++,
				 gnm_conf_set_core_gui_editing_transitionkeys,
				 _("Transition Keys"));
	bool_pref_create_widget (gnm_conf_get_core_gui_editing_autocomplete_node (),
				 page, row++,
				 gnm_conf_set_core_gui_editing_autocomplete,
				_("Autocomplete"));
	bool_pref_create_widget (gnm_conf_get_dialogs_rs_unfocused_node (),
				 page, row++,
				 gnm_conf_set_dialogs_rs_unfocused,
				_("Allow Unfocused Range Selections"));
	int_pref_create_widget (gnm_conf_get_functionselector_num_of_recent_node (),
				page, row++, 10, 0, 40, 1,
				gnm_conf_set_functionselector_num_of_recent,
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

	bool_pref_create_widget (gnm_conf_get_cut_and_paste_prefer_clipboard_node (),
				 page, row++,
				 gnm_conf_set_cut_and_paste_prefer_clipboard,
				 _("Prefer CLIPBOARD Over PRIMARY Selection"));

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     AutoCorrect Preferences Page (General)                                              */
/*******************************************************************************************/

static GtkWidget *
pref_autocorrect_general_page_initializer (PrefState *state,
				 G_GNUC_UNUSED gpointer data,
				 G_GNUC_UNUSED GtkNotebook *notebook,
				 G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	bool_pref_create_widget (gnm_conf_get_autocorrect_names_of_days_node (),
				 page, row++,
				 gnm_conf_set_autocorrect_names_of_days,
				 _("Capitalize _names of days"));

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     AutoCorrect Preferences Page (InitialCaps)                                              */
/*******************************************************************************************/

static GtkWidget *
pref_autocorrect_initialcaps_page_initializer (PrefState *state,
				 G_GNUC_UNUSED gpointer data,
				 G_GNUC_UNUSED GtkNotebook *notebook,
				 G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	bool_pref_create_widget (gnm_conf_get_autocorrect_init_caps_node (),
				 page, row++,
				 gnm_conf_set_autocorrect_init_caps,
				 _("Correct _TWo INitial CApitals"));
	wordlist_pref_create_widget (gnm_conf_get_autocorrect_init_caps_list_node (), page,
				     row++, "Do _not correct:");

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     AutoCorrect Preferences Page (InitialCaps)                                              */
/*******************************************************************************************/

static GtkWidget *
pref_autocorrect_firstletter_page_initializer (PrefState *state,
				 G_GNUC_UNUSED gpointer data,
				 G_GNUC_UNUSED GtkNotebook *notebook,
				 G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	bool_pref_create_widget (gnm_conf_get_autocorrect_first_letter_node (),
				 page, row++,
				 gnm_conf_set_autocorrect_first_letter,
				 _("Capitalize _first letter of sentence"));
	wordlist_pref_create_widget (gnm_conf_get_autocorrect_first_letter_list_node (), page,
				     row++, "Do _not capitalize after:");

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
} page_info_t;

/* Note that the first two items must remain here in that order */
static page_info_t const page_info[] = {
	{N_("Copy and Paste"),GTK_STOCK_PASTE,		 NULL, &pref_copypaste_page_initializer},
	{N_("Auto Correct"),  GTK_STOCK_DIALOG_ERROR,	 NULL, &pref_autocorrect_general_page_initializer},
	{N_("Font"),          GTK_STOCK_ITALIC,		 NULL, &pref_font_initializer	       },
	{N_("Files"),         GTK_STOCK_FLOPPY,		 NULL, &pref_file_page_initializer     },
	{N_("Tools"),       GTK_STOCK_EXECUTE,           NULL, &pref_tool_page_initializer     },
	{N_("Undo"),          GTK_STOCK_UNDO,		 NULL, &pref_undo_page_initializer     },
	{N_("Windows"),       "Gnumeric_ObjectCombo",	 NULL, &pref_window_page_initializer   },
	{N_("Header/Footer"), GTK_STOCK_ITALIC,		 "2",  &pref_font_hf_initializer       },
	{N_("Sorting"),       GTK_STOCK_SORT_ASCENDING,  "4", &pref_sort_page_initializer      },
	{N_("Screen"),        GTK_STOCK_PREFERENCES,     "6", &pref_screen_page_initializer    },
	{N_("INitial CApitals"), NULL, "1", &pref_autocorrect_initialcaps_page_initializer     },
	{N_("First Letter"), NULL, "1", &pref_autocorrect_firstletter_page_initializer         },
	{NULL, NULL, NULL, NULL },
};

typedef struct {
	int  const page;
	GtkTreePath *path;
} page_search_t;

static gboolean   
dialog_pref_select_page_search (GtkTreeModel *model,
				GtkTreePath *path,
				GtkTreeIter *iter,
				page_search_t *pst)
{
	int page;
	gtk_tree_model_get (model, iter, PAGE_NUMBER, &page, -1);
	if (page == pst->page) {
		pst->path = gtk_tree_path_copy (path);
		return TRUE;
	} else
		return FALSE;
}

static void
dialog_pref_select_page (PrefState *state, int page)
{
	page_search_t pst = {page, NULL};

	if (page >= 0)
		gtk_tree_model_foreach (GTK_TREE_MODEL (state->store),
					(GtkTreeModelForeachFunc) dialog_pref_select_page_search,
					&pst);
	
	if (pst.path == NULL)
		pst.path = gtk_tree_path_new_from_string ("0");

	if (pst.path != NULL) {
		gtk_tree_view_set_cursor (state->view, pst.path, NULL, FALSE);
		gtk_tree_view_expand_row (state->view, pst.path, TRUE);
		gtk_tree_path_free (pst.path);
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
		dialog_pref_select_page (state, 0);
	}
}

static void
cb_preferences_destroy (PrefState *state)
{
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
	state->dialog = glade_xml_get_widget (gui, "preferences");
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
		GtkWidget *page_widget =
			this_page->page_initializer (state, NULL,
						     state->notebook, i);
		gtk_notebook_append_page (state->notebook, page_widget, NULL);
		dialog_pref_add_item (state, this_page->page_name, 
				      this_page->icon_name, i, 
				      this_page->parent_path);
	}
	
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (state->store),
					      ITEM_NAME, GTK_SORT_ASCENDING);

	wbcg_set_transient (wbcg, GTK_WINDOW (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));

	dialog_pref_select_page (state, page);
}
