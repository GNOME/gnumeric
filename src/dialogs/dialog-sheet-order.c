/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-sheet-order.c: Dialog to change the order of sheets in the Gnumeric
 * spreadsheet
 *
 * Author:
 * 	Jody Goldberg <jody@gnome.org>
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
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <workbook-control-gui.h>
#include <workbook-edit.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <style-color.h>
#include <commands.h>
#include <application.h>
#include <widgets/gnumeric-cell-renderer-text.h>
#include <widgets/gnumeric-cell-renderer-toggle.h>
#include <goffice/gui-utils/go-combo-box.h>
#include <goffice/gui-utils/go-combo-color.h>
#include <goffice/gui-utils/go-gui-utils.h>


#include <glade/glade.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkbox.h>
#include <string.h>
#include <stdio.h>

typedef struct {
	WorkbookControlGUI  *wbcg;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkTreeView *sheet_list;
	GtkListStore *model;
	GtkWidget *up_btn;
	GtkWidget *down_btn;
	GtkWidget *add_btn;
	GtkWidget *duplicate_btn;
	GtkWidget *delete_btn;
	GtkWidget *ok_btn;
	GtkWidget *cancel_btn;
	GtkWidget *ccombo_back;
	GtkWidget *ccombo_fore;

	GdkPixbuf *image_padlock;
	GdkPixbuf *image_padlock_no;

	GdkPixbuf *image_ltr;
	GdkPixbuf *image_rtl;
	
	GdkPixbuf *image_visible;

	gboolean initial_colors_set;
	GSList *old_order;

	gulong sheet_order_changed_listener;
} SheetManager;

enum {
	SHEET_LOCKED,
	SHEET_LOCK_IMAGE,
	SHEET_VISIBLE,
	SHEET_VISIBLE_IMAGE,
	SHEET_NAME,
	SHEET_NEW_NAME,
	SHEET_POINTER,
	IS_EDITABLE_COLUMN,
	IS_DELETED,
	BACKGROUND_COLOUR,
	FOREGROUND_COLOUR,
	SHEET_DIRECTION,
	SHEET_DIRECTION_IMAGE,
	NUM_COLMNS
};

static void
cb_name_edited (G_GNUC_UNUSED GtkCellRendererText *cell,
	gchar               *path_string,
	gchar               *new_text,
        SheetManager        *state)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	path = gtk_tree_path_new_from_string (path_string);

	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model), &iter, path);
	gtk_list_store_set (state->model, &iter, SHEET_NEW_NAME, new_text, -1);

	gtk_tree_path_free (path);
}

static void
cb_color_changed_fore (G_GNUC_UNUSED GOComboColor *go_combo_color,
		       GOColor color, G_GNUC_UNUSED gboolean custom,
		       G_GNUC_UNUSED gboolean by_user,
		       G_GNUC_UNUSED gboolean is_default,
		       SheetManager *state)
{
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	GdkColor tmp;

	if (gtk_tree_selection_get_selected (selection, NULL, &sel_iter))
		gtk_list_store_set (state->model, &sel_iter,
				    FOREGROUND_COLOUR, 
				    (color == 0) ? NULL : go_color_to_gdk (color, &tmp),
				    -1);
}

static void
cb_color_changed_back (G_GNUC_UNUSED GOComboColor *go_combo_color,
		       GOColor color, G_GNUC_UNUSED gboolean custom,
		       G_GNUC_UNUSED gboolean by_user,
		       G_GNUC_UNUSED gboolean is_default,
		       SheetManager *state)
{
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	GdkColor tmp;

	if (gtk_tree_selection_get_selected (selection, NULL, &sel_iter))
		gtk_list_store_set (state->model, &sel_iter,
				    BACKGROUND_COLOUR, 
				    (color == 0) ? NULL : go_color_to_gdk (color, &tmp),
				    -1);
}

/**
 * Refreshes the buttons on a row (un)selection and selects the chosen sheet
 * for this view.
 */
static void
cb_selection_changed (G_GNUC_UNUSED GtkTreeSelection *ignored,
		      SheetManager *state)
{
	GtkTreeIter  it, iter;
	Sheet *sheet;
	gboolean is_deleted;
	GdkColor *fore, *back;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->sheet_list);

	gtk_widget_set_sensitive (state->add_btn, TRUE);
	gtk_widget_set_sensitive (state->duplicate_btn, FALSE);

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_widget_set_sensitive (state->up_btn, FALSE);
		gtk_widget_set_sensitive (state->down_btn, FALSE);
		gtk_widget_set_sensitive (state->delete_btn, FALSE);
		gtk_widget_set_sensitive (state->ccombo_back, FALSE);
		gtk_widget_set_sensitive (state->ccombo_fore, FALSE);
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
			    IS_DELETED, &is_deleted,
			    SHEET_POINTER, &sheet,
			    BACKGROUND_COLOUR, &back,
			    FOREGROUND_COLOUR, &fore,
			    -1);
	if (!state->initial_colors_set) {
		go_combo_color_set_color_gdk (GO_COMBO_COLOR (state->ccombo_back), back);
		go_combo_color_set_color_gdk (GO_COMBO_COLOR (state->ccombo_fore), fore);
		state->initial_colors_set = TRUE;
	}
	if (back != NULL)
		gdk_color_free (back);
	if (fore != NULL)
		gdk_color_free (fore);

	gtk_widget_set_sensitive (state->ccombo_back, TRUE);
	gtk_widget_set_sensitive (state->ccombo_fore, TRUE);
	gtk_widget_set_sensitive (state->delete_btn, TRUE);
	gtk_button_set_label (GTK_BUTTON (state->delete_btn),
                              is_deleted ? GTK_STOCK_UNDELETE : GTK_STOCK_DELETE);
	gtk_button_set_alignment (GTK_BUTTON (state->delete_btn), 0., .5);

	gtk_tree_model_get_iter_first 
		(GTK_TREE_MODEL (state->model),
		 &iter);
	gtk_widget_set_sensitive (state->up_btn, 
				  !gtk_tree_selection_iter_is_selected (selection, &iter));
	it = iter;
	while (gtk_tree_model_iter_next (GTK_TREE_MODEL (state->model),
					 &it))
		iter = it;
	gtk_widget_set_sensitive (state->down_btn,
				  !gtk_tree_selection_iter_is_selected (selection, &iter));
	
	if (sheet != NULL)
		wb_view_sheet_focus (
			wb_control_view (WORKBOOK_CONTROL (state->wbcg)), sheet);
}

static void
cb_toggled_lock (G_GNUC_UNUSED GtkCellRendererToggle *cell,
		 gchar                 *path_string,
		 gpointer               data)
{
	SheetManager *state = data;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gboolean value;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, SHEET_LOCKED, &value, -1);

	if (value) {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, SHEET_LOCKED, FALSE,
				   SHEET_LOCK_IMAGE, state->image_padlock_no, -1);
	} else {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, SHEET_LOCKED, TRUE,
				   SHEET_LOCK_IMAGE, state->image_padlock, -1);
	}
	gtk_tree_path_free (path);
}

static void
cb_toggled_direction (G_GNUC_UNUSED GtkCellRendererToggle *cell,
		      gchar		*path_string,
		      SheetManager	*state)
{
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreePath  *path  = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter iter;
	gboolean value;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, SHEET_DIRECTION, &value, -1);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
		SHEET_DIRECTION,	!value,
		SHEET_DIRECTION_IMAGE,	value ? state->image_ltr : state->image_rtl,
		-1);
	gtk_tree_path_free (path);
}

static void
cb_toggled_visible (G_GNUC_UNUSED GtkCellRendererToggle *cell,
		 gchar                 *path_string,
		 gpointer               data)
{
	SheetManager *state = data;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gboolean value;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, SHEET_VISIBLE, &value, -1);

	if (value) {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, SHEET_VISIBLE, FALSE,
				   SHEET_VISIBLE_IMAGE, NULL, -1);
		
	} else {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, SHEET_VISIBLE, TRUE,
				   SHEET_VISIBLE_IMAGE, state->image_visible, -1);
	}
	gtk_tree_path_free (path);
}

/* Add all of the sheets to the sheet_list */
static void
populate_sheet_list (SheetManager *state)
{
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	GtkTreeIter iter;
	GtkWidget *scrolled = glade_xml_get_widget (state->gui, "scrolled");
	Sheet *cur_sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (state->wbcg));
	
	int i, n = workbook_sheet_count (wb_control_workbook (WORKBOOK_CONTROL (state->wbcg)));
	GtkCellRenderer *renderer;

	state->model = gtk_list_store_new (NUM_COLMNS,
					   G_TYPE_BOOLEAN,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_BOOLEAN,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_POINTER,
					   G_TYPE_BOOLEAN,
					   G_TYPE_BOOLEAN,
					   GDK_TYPE_COLOR,
					   GDK_TYPE_COLOR,
                       G_TYPE_BOOLEAN,
					   GDK_TYPE_PIXBUF);
	state->sheet_list = GTK_TREE_VIEW (gtk_tree_view_new_with_model
					   (GTK_TREE_MODEL (state->model)));
	selection = gtk_tree_view_get_selection (state->sheet_list);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	for (i = 0 ; i < n ; i++) {
		Sheet *sheet = workbook_sheet_by_index (
			wb_control_workbook (WORKBOOK_CONTROL (state->wbcg)), i);
		
		GdkColor *color = NULL;
		GdkColor *text_color = NULL;

		if (sheet->tab_color)
			color = &sheet->tab_color->gdk_color;
		if (sheet->tab_text_color)
			text_color = &sheet->tab_text_color->gdk_color;

		gtk_list_store_append (state->model, &iter);
		gtk_list_store_set (state->model, &iter,
				    SHEET_LOCKED, sheet->is_protected,
				    SHEET_LOCK_IMAGE, sheet->is_protected ?
				    state->image_padlock : state->image_padlock_no,
				    SHEET_VISIBLE, sheet->is_visible,
				    SHEET_VISIBLE_IMAGE, sheet->is_visible ? 
				    state->image_visible : NULL,
				    SHEET_NAME, sheet->name_unquoted,
				    SHEET_NEW_NAME, "",
				    SHEET_POINTER, sheet,
				    IS_EDITABLE_COLUMN,	TRUE,
				    IS_DELETED,	FALSE,
				    BACKGROUND_COLOUR, color,
				    FOREGROUND_COLOUR, text_color,
                    SHEET_DIRECTION, sheet->text_is_rtl,
                    SHEET_DIRECTION_IMAGE, sheet->text_is_rtl ?
				    state->image_rtl : state->image_ltr,
				    -1);
		if (sheet == cur_sheet)
			gtk_tree_selection_select_iter (selection, &iter);
		state->old_order = g_slist_prepend (state->old_order, sheet);
	}

	state->old_order = g_slist_reverse (state->old_order);

	renderer = gnumeric_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_lock), state);
	column = gtk_tree_view_column_new_with_attributes ("",
							   renderer,
							   "active", SHEET_LOCKED,
							   "pixbuf", SHEET_LOCK_IMAGE,
							   NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	renderer = gnumeric_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_visible), state);
	column = gtk_tree_view_column_new_with_attributes ("",
							   renderer,
							   "active", SHEET_VISIBLE,
							   "pixbuf", SHEET_VISIBLE_IMAGE,
							   NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	renderer = gnumeric_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
		G_CALLBACK (cb_toggled_direction), state);
	column = gtk_tree_view_column_new_with_attributes ("", renderer,
			"active", SHEET_DIRECTION,
			"pixbuf", SHEET_DIRECTION_IMAGE,
			NULL);
	gtk_tree_view_append_column (state->sheet_list, column);
	
	column = gtk_tree_view_column_new_with_attributes (_("Current Name"),
					      gnumeric_cell_renderer_text_new (),
					      "text", SHEET_NAME,
					      "strikethrough", IS_DELETED,
					      "background_gdk",BACKGROUND_COLOUR,
					      "foreground_gdk",FOREGROUND_COLOUR,
					      NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	renderer = gnumeric_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("New Name"),
					      renderer,
					      "text", SHEET_NEW_NAME,
					      "editable", IS_EDITABLE_COLUMN,
					      "strikethrough", IS_DELETED,
					      "background_gdk",BACKGROUND_COLOUR,
					      "foreground_gdk",FOREGROUND_COLOUR,
					      NULL);
	gtk_tree_view_append_column (state->sheet_list, column);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_name_edited), state);
			  
	gtk_tree_view_set_reorderable (state->sheet_list, TRUE);

	/* Init the buttons & selection */
	cb_selection_changed (NULL, state);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_selection_changed), state);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->sheet_list));
}

static void
cb_item_move (SheetManager *state, gnm_iter_search_t iter_search)
{
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	GtkTreeModel *model;
	GtkTreeIter  a, b;

	g_return_if_fail (selection != NULL);

	if (!gtk_tree_selection_get_selected  (selection, &model, &a))
		return;

	b = a;
	if (!iter_search (model, &b))
		return;

	gtk_list_store_swap (state->model, &a, &b);
	cb_selection_changed (NULL, state);
}

static void
cb_up (G_GNUC_UNUSED GtkWidget *w, SheetManager *state)
{
	cb_item_move (state, gnm_tree_model_iter_prev);
}

static void
cb_down (G_GNUC_UNUSED GtkWidget *w, SheetManager *state)
{
	cb_item_move (state, gnm_tree_model_iter_next);
}

static void
cb_add_clicked (G_GNUC_UNUSED GtkWidget *ignore, SheetManager *state)
{
	GtkTreeIter iter;
	GtkTreeIter sel_iter;
	GtkTreeIter this_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	int i = 0;
	char *name, *old_name, *new_name;

	if (!gtk_tree_selection_get_selected (selection, NULL, &sel_iter))
		gtk_list_store_append (state->model, &iter);
	else
		gtk_list_store_insert_before (state->model, &iter, &sel_iter);

	/* We can't use workbook_sheet_get_free_name since that would give us the same */
	/* name for multiple adds */

	name = g_new (char, strlen (_("Sheet%d")) + 12);
	for ( ; ++i ; ){
		int n = 0;
		gboolean match = FALSE;
		sprintf (name, _("Sheet%d"), i);
		while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
						       &this_iter, NULL, n)) {
			gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter,
					    SHEET_NAME, &old_name,
					    SHEET_NEW_NAME, &new_name,
					    -1);
			n++;
			match = (new_name != NULL && !strcmp (name, new_name)) ||
				(old_name != NULL && !strcmp (name, old_name));
			if (match)
				break;
		}
		if (!match)
			break;
	}
	gtk_list_store_set (state->model, &iter,
			    SHEET_LOCKED, FALSE,
			    SHEET_LOCK_IMAGE, state->image_padlock_no,
			    SHEET_VISIBLE, TRUE,
			    SHEET_VISIBLE_IMAGE, state->image_visible,
			    SHEET_NAME, "",
			    SHEET_NEW_NAME, name,
			    SHEET_POINTER, NULL,
			    IS_EDITABLE_COLUMN,	TRUE,
			    IS_DELETED,	FALSE,
			    BACKGROUND_COLOUR, 0,
			    FOREGROUND_COLOUR, 0,
			    SHEET_DIRECTION, FALSE,
			    SHEET_DIRECTION_IMAGE, state->image_ltr,
			    -1);
	gtk_tree_selection_select_iter (selection, &iter);
	g_free (name);
}

static void
cb_duplicate_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		      G_GNUC_UNUSED SheetManager *state)
{
#warning implement this
	g_warning ("'Duplicate' not implemented.");
}

static void
cb_delete_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		   SheetManager *state)
{
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	Sheet *sheet;
	gboolean is_deleted;

	if (gtk_tree_selection_get_selected (selection, NULL, &sel_iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &sel_iter,
				    SHEET_POINTER, &sheet,
				    IS_DELETED, &is_deleted,
				    -1);
		if (is_deleted) {
			gtk_list_store_set (state->model, &sel_iter,
					    IS_DELETED,	FALSE,
					    IS_EDITABLE_COLUMN, TRUE,
					    -1);
		} else {
			if (sheet == NULL) {
				gtk_list_store_remove (state->model, &sel_iter);
			} else {
				gtk_list_store_set (state->model, &sel_iter,
						    IS_DELETED,	TRUE,
						    IS_EDITABLE_COLUMN, FALSE,
						    -1);
			}
		}
		cb_selection_changed (NULL, state);
	}
}

static void
cb_cancel_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		   SheetManager *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static gboolean
sheet_order_gdk_color_equal (GdkColor *color_a, GdkColor *color_b)
{
	if (color_a == NULL && color_b == NULL)
		return TRUE;
	if (color_a != NULL && color_b != NULL)
		return gdk_color_equal (color_a, color_b);
	return FALSE;
}

static void
cb_ok_clicked (G_GNUC_UNUSED GtkWidget *ignore, SheetManager *state)
{
	GSList *new_order = NULL;        /* list of indices in the new order */
	                                 /* 1,0,2,-1  means switch the first */
	                                 /* two sheets and append a new one  */

	GSList *changed_names = NULL;    /* list of indices of the sheets to */
                                         /* change names */
	GSList *new_names = NULL;        /* list of new names (NULL for      */
                                         /* automatic names)                 */

	GSList *deleted_sheets = NULL;   /* list of indices of sheets to be  */
                                         /* deleted                          */

	GSList *color_changed = NULL;    /* list of indices of sheets with   */
                                         /* modified back of foreground      */
	GSList *new_colors_back = NULL;  /* list of new back colours         */
	GSList *new_colors_fore  = NULL; /* list of new fore colours         */

	GSList *protection_changed  = NULL;/* list of indices of sheets to be*/
                                         /* with changed lock status         */
	GSList *new_locks  = NULL;       /* that lock status                 */
	GSList *visibility_changed  = NULL;/* list of indices of sheets to be*/
                                         /* with changed visibility status   */
	GSList *new_visibility  = NULL;  /* that visibility status           */

	Sheet *this_sheet;
	int this_sheet_idx;
	char *old_name, *new_name;
	GtkTreeIter this_iter;
	gint n = 0;
	gint i = 0;
	GSList *this_new, *list;
	gboolean order_has_changed = FALSE;
	gboolean is_deleted, is_locked, is_visible, is_rtl;
	GdkColor *back, *fore;
	gboolean fore_changed, back_changed, lock_changed, vis_changed, one_is_visible = FALSE;
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));

	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, n)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter,
				    SHEET_LOCKED, &is_locked,
				    SHEET_VISIBLE, &is_visible,
				    SHEET_POINTER, &this_sheet,
				    SHEET_NAME, &old_name,
				    SHEET_NEW_NAME, &new_name,
				    IS_DELETED, &is_deleted,
				    BACKGROUND_COLOUR, &back,
				    FOREGROUND_COLOUR, &fore,
				    SHEET_DIRECTION, &is_rtl,
				    -1);
		this_sheet_idx = (this_sheet == NULL ? -1 
				  : this_sheet->index_in_wb); 
		if (!is_deleted) {
			new_order = g_slist_prepend 
				(new_order, 
				 GINT_TO_POINTER (this_sheet_idx));
			if (this_sheet == NULL ||
			    (strlen (new_name) > 0 && 
			     strcmp (old_name, new_name))) {
				changed_names = g_slist_prepend 
					(changed_names, 
					 GINT_TO_POINTER (this_sheet_idx));
				new_names = g_slist_prepend 
					(new_names,
					 strlen(new_name) > 0 ? new_name 
					 : NULL);
			} else {
				g_free (new_name);
			}
			g_free (old_name);

			back_changed = (this_sheet == NULL) ||
				!sheet_order_gdk_color_equal (back,
					      this_sheet->tab_color ?
					      &this_sheet->tab_color->gdk_color : NULL);
			fore_changed = (this_sheet == NULL) ||
				!sheet_order_gdk_color_equal (fore,
					      this_sheet->tab_text_color ?
					      &this_sheet->tab_text_color->gdk_color : NULL);
			if (fore_changed || back_changed) {
				color_changed = g_slist_prepend (color_changed, 
					 GINT_TO_POINTER (this_sheet_idx));
				new_colors_back = g_slist_prepend 
					(new_colors_back, back);
				new_colors_fore = g_slist_prepend 
					(new_colors_fore, fore);
			} else {
				if (back)
					gdk_color_free (back);
				if (fore)
					gdk_color_free (fore);
			}

			lock_changed = (this_sheet == NULL) ||
				(is_locked != this_sheet->is_protected);
			if (lock_changed) {
				protection_changed = g_slist_prepend 
					(protection_changed,
					 GINT_TO_POINTER (this_sheet_idx));
				new_locks = g_slist_prepend 
					(new_locks,
					 GINT_TO_POINTER (is_locked));
			}
			one_is_visible = one_is_visible || is_visible;
			vis_changed = (this_sheet == NULL) ||
				(is_visible != this_sheet->is_visible);
			if (vis_changed) {
				visibility_changed = g_slist_prepend 
					(visibility_changed,
					 GINT_TO_POINTER (this_sheet_idx));
				new_visibility = g_slist_prepend 
					(new_visibility,
					 GINT_TO_POINTER (is_visible));
			}
			
			sheet_set_direction (this_sheet, is_rtl);
		} else {
			deleted_sheets = g_slist_prepend (deleted_sheets, 
				 GINT_TO_POINTER (this_sheet_idx));
			g_free (new_name);
			g_free (old_name);
			if (back)
				gdk_color_free (back);
			if (fore)
				gdk_color_free (fore);
		}
		n++;
	}

	color_changed = g_slist_reverse (color_changed);
	new_colors_back = g_slist_reverse (new_colors_back);
	new_colors_fore = g_slist_reverse (new_colors_fore);

	protection_changed = g_slist_reverse (protection_changed);
	new_locks = g_slist_reverse (new_locks);
	visibility_changed = g_slist_reverse (visibility_changed);
	new_visibility = g_slist_reverse (new_visibility);

	new_order = g_slist_reverse (new_order);
	new_names = g_slist_reverse (new_names);
	changed_names = g_slist_reverse (changed_names);

	if (!one_is_visible) {
		go_gtk_notice_dialog (GTK_WINDOW (state->dialog), GTK_MESSAGE_ERROR,
				 _("At least one sheet must remain visible!"));
		goto cleanup;
	}
	if (new_order == NULL) {
		go_gtk_notice_dialog (GTK_WINDOW (state->dialog), GTK_MESSAGE_ERROR,
				 _("You may not delete all sheets in a workbook!"));
		goto cleanup;
	}
	if (workbook_sheet_count (wb) <= (int)g_slist_length (deleted_sheets) ) {
		go_gtk_notice_dialog (GTK_WINDOW (state->dialog), GTK_MESSAGE_ERROR,
				 _("To replace all exisiting sheets, please "
				   "delete the current workbook and create "
				   "a new one!"));
		goto cleanup;
	}

	this_new = new_order;
	i = 0;
	while (this_new != NULL) {
		if (GPOINTER_TO_INT (this_new->data) != i++) {
			order_has_changed = TRUE;
			break;
		}
		this_new = this_new->next;
	}

	if (!order_has_changed) {
		g_slist_free (new_order);
		new_order = NULL;
	}

	/* Stop listening to changes in the sheet order. */
	g_signal_handler_block (G_OBJECT (wb),
				state->sheet_order_changed_listener);
	wbcg_edit_detach_guru (state->wbcg);

	if ((new_order == NULL && changed_names == NULL && color_changed == NULL
	     && protection_changed == NULL && visibility_changed == NULL 
	     && deleted_sheets == NULL)
	    || !cmd_reorganize_sheets (WORKBOOK_CONTROL (state->wbcg),
				       new_order,
				       changed_names, new_names, 
				       deleted_sheets,
				       color_changed, new_colors_back, 
				       new_colors_fore,
				       protection_changed, new_locks,
				       visibility_changed, new_visibility)) {
		gtk_widget_destroy (GTK_WIDGET (state->dialog));
	} else {
		wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
		g_signal_handler_unblock (G_OBJECT (wb),
					  state->sheet_order_changed_listener);
	}

	return;

 cleanup:
	g_slist_free (deleted_sheets);
	deleted_sheets = NULL;
	g_slist_free (new_order);
	new_order = NULL;
	g_slist_free (changed_names);
	changed_names = NULL;
	g_slist_foreach (new_names, (GFunc)g_free, NULL);
	g_slist_free (new_names);
	new_names = NULL;
	
	g_slist_free (color_changed);
	color_changed = NULL;
	for (list = new_colors_back; list != NULL; list = list->next)
		if (list->data)
			gdk_color_free ((GdkColor *)list->data);
	g_slist_free (new_colors_back);
	new_colors_back = NULL;
	for (list = new_colors_fore; list != NULL; list = list->next)
		if (list->data)
			gdk_color_free ((GdkColor *)list->data);
	g_slist_free (new_colors_fore);
	new_colors_fore = NULL;
	
	g_slist_free (protection_changed);
	protection_changed = NULL;
	g_slist_free (new_locks);
	new_locks = NULL;
	g_slist_free (visibility_changed);
	visibility_changed = NULL;
	g_slist_free (new_visibility);
	new_visibility = NULL;
	return;
}

static void
cb_sheet_order_destroy (SheetManager *state)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));

	/* Stop to listen to changes in the sheet order. */
	g_signal_handler_disconnect (G_OBJECT (wb),
				     state->sheet_order_changed_listener);

	wbcg_edit_detach_guru (state->wbcg);
	g_object_unref (G_OBJECT (state->gui));
	state->gui = NULL;
	g_object_unref (state->image_padlock);
	state->image_padlock = NULL;
	g_object_unref (state->image_padlock_no);
	state->image_padlock_no = NULL;
	g_object_unref (state->image_visible);
	state->image_visible = NULL;

	g_object_unref (state->image_rtl);
	state->image_rtl = NULL;
	g_object_unref (state->image_ltr);
    state->image_ltr = NULL;
	
	if (state->old_order != NULL) {
		g_slist_free (state->old_order);
		state->old_order = NULL;
	}
	g_free (state);
}

static void
dialog_sheet_order_update_sheet_order (SheetManager *state)
{
	gchar *name, *new_name;
	gboolean is_deleted;
	gboolean is_editable;
	gboolean is_locked;
	gboolean is_visible;
	gboolean is_rtl;
	GdkColor *back, *fore;
	GtkTreeIter iter;
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));
	gint i, j, n_sheets, n_children;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	Sheet *sheet_wb, *sheet_model;
	GtkTreeSelection *sel = gtk_tree_view_get_selection (state->sheet_list);
	gboolean selected;

	n_sheets = workbook_sheet_count (wb);
	n_children = gtk_tree_model_iter_n_children (model, NULL);
	g_return_if_fail (n_sheets == n_children);

	for (i = 0; i < n_sheets; i++) {
		sheet_wb = workbook_sheet_by_index (wb, i);
		for (j = i; j < n_children; j++) {
			if (!gtk_tree_model_iter_nth_child (model, &iter,
							    NULL, j))
				break;
			gtk_tree_model_get (model, &iter, SHEET_POINTER,
					    &sheet_model, -1);
			if (sheet_model == sheet_wb)
				break;
		}
		if (j == i)
			continue;

		if (!gtk_tree_model_iter_nth_child (model, &iter, NULL, j))
			break;
		selected = gtk_tree_selection_iter_is_selected (sel, &iter);
		gtk_tree_model_get (model, &iter,
			SHEET_LOCKED, &is_locked,
			SHEET_VISIBLE, &is_visible,
			SHEET_NAME, &name,
			SHEET_NEW_NAME, &new_name,
			IS_EDITABLE_COLUMN, &is_editable,
			SHEET_POINTER, &sheet_model,
			IS_DELETED, &is_deleted,
			BACKGROUND_COLOUR, &back,
			FOREGROUND_COLOUR, &fore,
			SHEET_DIRECTION, &is_rtl,
			-1);
		gtk_list_store_remove (state->model, &iter);
		gtk_list_store_insert (state->model, &iter, i);
		gtk_list_store_set (state->model, &iter,
				    SHEET_LOCKED, is_locked,
				    SHEET_LOCK_IMAGE, is_locked ?
				    state->image_padlock : state->image_padlock_no,
				    SHEET_VISIBLE, is_visible,
				    SHEET_VISIBLE_IMAGE, is_visible ? 
				    state->image_visible : NULL,
				    SHEET_NAME, name,
				    SHEET_NEW_NAME, new_name,
				    IS_EDITABLE_COLUMN, is_editable,
				    SHEET_POINTER, sheet_model,
				    IS_DELETED, is_deleted,
				    BACKGROUND_COLOUR, back,
				    FOREGROUND_COLOUR, fore,
				    SHEET_DIRECTION, is_rtl,
				    SHEET_DIRECTION_IMAGE,
					    is_rtl ? state->image_rtl : state->image_ltr,
				    -1);
		if (back)
			gdk_color_free (back);
		if (fore)
			gdk_color_free (fore);
		g_free (name);
		g_free (new_name);
		if (selected)
			gtk_tree_selection_select_iter (sel, &iter);
	}

	g_slist_free (state->old_order);
	state->old_order = NULL;
	for (i = 0; i < n_sheets; i++)
		state->old_order = g_slist_append (state->old_order,
					workbook_sheet_by_index (wb, i));

	cb_selection_changed (NULL, state);
}

static void
cb_sheet_order_changed (Workbook *wb, SheetManager *state)
{
	GtkTreeIter iter;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	guint i, n = 0;
	Sheet *sheet;

	/*
	 * First question: Has the user already changed the order via
	 * the dialog? If no, we assume that the user wants to see the
	 * sheet order change reflected in the dialog.
	 */
	n = g_slist_length (state->old_order);
	for (i = 0; i < n; i++) {
		if (!gtk_tree_model_iter_nth_child (model, &iter, NULL, i))
			break;
		gtk_tree_model_get (model, &iter, SHEET_POINTER, &sheet, -1);
		if (sheet != g_slist_nth_data (state->old_order, i))
			break;
	}
	if (n == i) {
		dialog_sheet_order_update_sheet_order (state);
		return;
	}

	/*
	 * The user has already changed the order via the dialog.
	 * Let's check if the new sheet order is already reflected
	 * in the dialog. If yes, things are easy.
	 */
	n = workbook_sheet_count (wb);
	for (i = 0; i < n; i++) {
		if (!gtk_tree_model_iter_nth_child (model, &iter, NULL, i))
			break;
		gtk_tree_model_get (model, &iter, SHEET_POINTER, &sheet, -1);
		if (sheet != workbook_sheet_by_index (wb, i))
			break;
	}
	if (i == n) {
		g_slist_free (state->old_order);
		state->old_order = NULL;
		for (i = 0; i < n; i++)
			state->old_order = g_slist_append (state->old_order,
				workbook_sheet_by_index (wb, i));
		return;
	}

	/*
	 * The order in the dialog and the new sheet order are totally
	 * different. Ask the user what to do.
	 */
	if (go_gtk_query_yes_no (GTK_WINDOW (state->dialog),
			_("The sheet order has changed. Do you want to "
			  "update the list?"), TRUE))
		dialog_sheet_order_update_sheet_order (state);
}

void
dialog_sheet_order (WorkbookControlGUI *wbcg)
{
	SheetManager *state;
	GladeXML *gui;
	GtkBox *vbox;
	GOColorGroup *cg;
	Workbook *wb;

	g_return_if_fail (wbcg != NULL);

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"sheet-order.glade", NULL, NULL);
        if (gui == NULL)
                return;

	state = g_new0 (SheetManager, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->dialog     = glade_xml_get_widget (gui, "sheet-order-dialog");
	state->up_btn     = glade_xml_get_widget (gui, "up_button");
	state->down_btn   = glade_xml_get_widget (gui, "down_button");
	state->add_btn   = glade_xml_get_widget (gui, "add_button");
	state->duplicate_btn   = glade_xml_get_widget (gui, "duplicate_button");
	state->delete_btn   = glade_xml_get_widget (gui, "delete_button");

	state->ok_btn  = glade_xml_get_widget (gui, "ok_button");
	state->cancel_btn  = glade_xml_get_widget (gui, "cancel_button");
	state->old_order  = NULL;
	state->initial_colors_set = FALSE;
	state->image_padlock =  gtk_widget_render_icon (state->dialog,
                                             "Gnumeric_Protection_Yes",
                                             GTK_ICON_SIZE_LARGE_TOOLBAR,
                                             "Gnumeric-Sheet-Manager");
	state->image_padlock_no =  gtk_widget_render_icon (state->dialog,
                                             "Gnumeric_Protection_No",
                                             GTK_ICON_SIZE_LARGE_TOOLBAR,
                                             "Gnumeric-Sheet-Manager");
	state->image_visible = gtk_widget_render_icon (state->dialog,
                                             "Gnumeric_Visible",
                                             GTK_ICON_SIZE_LARGE_TOOLBAR,
                                             "Gnumeric-Sheet-Manager");
	state->image_ltr =  gtk_widget_render_icon (state->dialog,
                                             "gtk-go-forward",
                                             GTK_ICON_SIZE_LARGE_TOOLBAR,
                                             "Gnumeric-Sheet-Manager");
	state->image_rtl =  gtk_widget_render_icon (state->dialog,
                                             "gtk-go-back",
                                             GTK_ICON_SIZE_LARGE_TOOLBAR,
                                             "Gnumeric-Sheet-Manager");
	/* Listen for changes in the sheet order. */
	wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet_order_changed_listener = g_signal_connect (G_OBJECT (wb),
		"sheet_order_changed", G_CALLBACK (cb_sheet_order_changed),
		state);

	gtk_button_set_alignment (GTK_BUTTON (state->up_btn),     0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->down_btn),   0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->add_btn),    0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->delete_btn), 0., .5);

	vbox = GTK_BOX (glade_xml_get_widget (gui,"sheet_order_buttons_vbox"));
	cg = go_color_group_fetch ("back_color_group",
		wb_control_view (WORKBOOK_CONTROL (wbcg)));
	state->ccombo_back = go_combo_color_new (gnm_app_get_pixbuf ("bucket"),
		_("Default"), 0, cg);
	go_combo_color_set_instant_apply (
		GO_COMBO_COLOR (state->ccombo_back), TRUE);
	gtk_box_pack_start (vbox, state->ccombo_back, FALSE, FALSE, 0);
	gtk_widget_set_sensitive (state->ccombo_back, FALSE);

	cg = go_color_group_fetch ("fore_color_group",
		wb_control_view (WORKBOOK_CONTROL (wbcg)));
	state->ccombo_fore = go_combo_color_new (gnm_app_get_pixbuf ("font"),
		_("Default"), 0, cg);
	go_combo_color_set_instant_apply (
		GO_COMBO_COLOR (state->ccombo_fore), TRUE);
	gtk_box_pack_start (vbox, state->ccombo_fore, TRUE, TRUE, 0);
	gtk_widget_set_sensitive (state->ccombo_fore, FALSE);

	populate_sheet_list (state);

	g_signal_connect (G_OBJECT (state->up_btn),
		"clicked",
		G_CALLBACK (cb_up), state);
	g_signal_connect (G_OBJECT (state->down_btn),
		"clicked",
		G_CALLBACK (cb_down), state);
	g_signal_connect (G_OBJECT (state->add_btn),
		"clicked",
		G_CALLBACK (cb_add_clicked), state);
	g_signal_connect (G_OBJECT (state->duplicate_btn),
		"clicked",
		G_CALLBACK (cb_duplicate_clicked), state);
	g_signal_connect (G_OBJECT (state->delete_btn),
		"clicked",
		G_CALLBACK (cb_delete_clicked), state);
	g_signal_connect (G_OBJECT (state->ok_btn),
		"clicked",
		G_CALLBACK (cb_ok_clicked), state);
	g_signal_connect (G_OBJECT (state->cancel_btn),
		"clicked",
		G_CALLBACK (cb_cancel_clicked), state);
	g_signal_connect (G_OBJECT (state->ccombo_back),
		"color_changed",
		G_CALLBACK (cb_color_changed_back), state);
	g_signal_connect (G_OBJECT (state->ccombo_fore),
		"color_changed",
		G_CALLBACK (cb_color_changed_fore), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SHEET_MANAGER);

	/* a candidate for merging into attach guru */
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_sheet_order_destroy);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
