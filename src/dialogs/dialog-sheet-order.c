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
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <workbook-control-gui.h>
#include <workbook-edit.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <style-color.h>
#include <commands.h>
#include <widgets/gnumeric-cell-renderer-text.h>
#include "pixmaps/gnumeric-stock-pixbufs.h"

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <gal/widgets/widget-color-combo.h>

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
	GdkPixbuf *sheet_image;
	GtkWidget *ccombo_back;
	GtkWidget *ccombo_fore;

	GSList *old_order;
} SheetManager;

enum {
	SHEET_PIX,
	SHEET_NAME,
	SHEET_NEW_NAME,
	SHEET_POINTER,
	IS_EDITABLE_COLUMN,
	IS_DELETED,
	BACKGROUND_COLOUR_POINTER,
	FOREGROUND_COLOUR_POINTER,
	NUM_COLMNS
};

static void
cb_name_edited (GtkCellRendererText *cell,
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

static gint
location_of_iter (GtkTreeIter  *iter, GtkListStore *model)
{
	Sheet *sheet, *this_sheet;
	GtkTreeIter this_iter;
	gint n = 0;
	
	gtk_tree_model_get (GTK_TREE_MODEL (model), iter, SHEET_POINTER, &sheet, -1);

	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (model),
					       &this_iter, NULL, n)) {
		gtk_tree_model_get (GTK_TREE_MODEL (model), &this_iter, SHEET_POINTER,
				    &this_sheet, -1);
		if (this_sheet == sheet)
			return n;
		n++;
	}

	g_warning ("We should have never gotten to this point!\n");
	return -1;
}

static void 
cb_color_changed_fore (ColorCombo *color_combo, GdkColor *color, gboolean custom, 
		  gboolean by_user, gboolean is_default, SheetManager *state)
{
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);

	if (gtk_tree_selection_get_selected (selection, NULL, &sel_iter)) {
		gtk_list_store_set (state->model, &sel_iter,
				    FOREGROUND_COLOUR_POINTER, color,
				    -1);
	}
}

static void 
cb_color_changed_back (ColorCombo *color_combo, GdkColor *color, gboolean custom, 
		  gboolean by_user, gboolean is_default, SheetManager *state)
{
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);

	if (gtk_tree_selection_get_selected (selection, NULL, &sel_iter)) {
		gtk_list_store_set (state->model, &sel_iter,
				    BACKGROUND_COLOUR_POINTER, color,
				    -1);
	}
}

/**
 * Refreshes the buttons on a row (un)selection and selects the chosen sheet
 * for this view.
 */
static void
cb_selection_changed (GtkTreeSelection *ignored, SheetManager *state)
{
	GtkTreeIter  iter;
	GtkTreeIter this_iter;
	gint row;
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
			    BACKGROUND_COLOUR_POINTER, &back,
			    FOREGROUND_COLOUR_POINTER, &fore,
			    -1);
	color_combo_set_color  (COLOR_COMBO (state->ccombo_back), back);
	if (back)
		gdk_color_free (back);
	color_combo_set_color  (COLOR_COMBO (state->ccombo_fore), fore);
	if (fore)
		gdk_color_free (fore);
	gtk_widget_set_sensitive (state->ccombo_back, TRUE);
	gtk_widget_set_sensitive (state->ccombo_fore, TRUE);
	gtk_widget_set_sensitive (state->delete_btn, TRUE);
	gtk_button_set_label (GTK_BUTTON (state->delete_btn),
                              is_deleted ? GTK_STOCK_UNDELETE : GTK_STOCK_DELETE);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->delete_btn), 0., .5, 0., 0.);

	gtk_widget_set_sensitive (state->up_btn,
				  location_of_iter (&iter, state->model) > 0);

	row = location_of_iter (&iter, state->model);
	gtk_widget_set_sensitive (state->down_btn,
				  gtk_tree_model_iter_nth_child  
				  (GTK_TREE_MODEL (state->model),
				   &this_iter, NULL, row+1));

	if (sheet != NULL)
		wb_view_sheet_focus (
			wb_control_view (WORKBOOK_CONTROL (state->wbcg)), sheet);
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
					   GDK_TYPE_PIXBUF,
					   G_TYPE_STRING, 	
					   G_TYPE_STRING, 
					   G_TYPE_POINTER, 
					   G_TYPE_BOOLEAN, 
					   G_TYPE_BOOLEAN, 
					   GDK_TYPE_COLOR, 
					   GDK_TYPE_COLOR); 
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
			color = &sheet->tab_color->color;
		if (sheet->tab_text_color)
			text_color = &sheet->tab_text_color->color;

		gtk_list_store_append (state->model, &iter);
		gtk_list_store_set (state->model, &iter,
				    SHEET_PIX, state->sheet_image,
				    SHEET_NAME, sheet->name_unquoted,
				    SHEET_NEW_NAME, "",
				    SHEET_POINTER, sheet,
				    IS_EDITABLE_COLUMN,	TRUE,   
				    IS_DELETED,	FALSE,   
				    BACKGROUND_COLOUR_POINTER, color,
				    FOREGROUND_COLOUR_POINTER, text_color,
				    -1);
		if (sheet == cur_sheet)
			gtk_tree_selection_select_iter (selection, &iter);
		state->old_order = g_slist_prepend (state->old_order, sheet);
	}

	state->old_order = g_slist_reverse (state->old_order);

	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_pixbuf_new (),
							   "pixbuf", SHEET_PIX, 
							   NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	column = gtk_tree_view_column_new_with_attributes (_("Current Name"),
					      gnumeric_cell_renderer_text_new (),
					      "text", SHEET_NAME, 
					      "strikethrough", IS_DELETED,
					      "background_gdk",BACKGROUND_COLOUR_POINTER,
					      "foreground_gdk",FOREGROUND_COLOUR_POINTER,
					      NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	renderer = gnumeric_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("New Name"),
					      renderer,
					      "text", SHEET_NEW_NAME, 
					      "editable", IS_EDITABLE_COLUMN,
					      "strikethrough", IS_DELETED,
					      "background_gdk",BACKGROUND_COLOUR_POINTER,
					      "foreground_gdk",FOREGROUND_COLOUR_POINTER,
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
cb_item_move (SheetManager *state, gint direction)
{
	char* name;
	char* new_name;
	Sheet * sheet;
	GtkTreeIter iter;
	gint row;
	gboolean is_deleted;
	gboolean is_editable;
	GdkColor *back, *fore;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->sheet_list);

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
			    SHEET_NAME, &name,
			    SHEET_NEW_NAME, &new_name,
			    IS_EDITABLE_COLUMN, &is_editable,
			    SHEET_POINTER, &sheet,
			    IS_DELETED,	&is_deleted,   
			    BACKGROUND_COLOUR_POINTER, &back,
			    FOREGROUND_COLOUR_POINTER, &fore,
			    -1);
	row = location_of_iter (&iter, state->model);
	if (row + direction < 0)
		return;
	gtk_list_store_remove (state->model, &iter);
	gtk_list_store_insert (state->model, &iter, row + direction);
	gtk_list_store_set (state->model, &iter,
			    SHEET_PIX, state->sheet_image,
			    SHEET_NAME, name,
			    SHEET_NEW_NAME, new_name,
			    IS_EDITABLE_COLUMN, is_editable,
			    SHEET_POINTER, sheet,
			    IS_DELETED,	is_deleted,   
			    BACKGROUND_COLOUR_POINTER, back,
			    FOREGROUND_COLOUR_POINTER, fore,
			    -1);
	if (back)
		gdk_color_free (back);
	if (fore)
		gdk_color_free (fore);	
	g_free (name);
	g_free (new_name);
	gtk_tree_selection_select_iter (selection, &iter);

	/* this is a little hack-ish, but we need to refresh the buttons */
	cb_selection_changed (NULL, state);
}

static void cb_up   (GtkWidget *w, SheetManager *state) { cb_item_move (state, -1); }
static void cb_down (GtkWidget *w, SheetManager *state) { cb_item_move (state,  1); }

static void
cb_add_clicked (GtkWidget *ignore, SheetManager *state)
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
			match = (0 == g_str_compare (name, new_name) ||
				 0 == g_str_compare (name, old_name));
			if (match)
				break;
		}
		if (!match)
			break;
	}
	gtk_list_store_set (state->model, &iter,
			    SHEET_PIX, state->sheet_image,
			    SHEET_NAME, _(""),
			    SHEET_NEW_NAME, name,
			    SHEET_POINTER, NULL,
			    IS_EDITABLE_COLUMN,	TRUE,   
			    IS_DELETED,	FALSE,   
			    BACKGROUND_COLOUR_POINTER, NULL,
			    FOREGROUND_COLOUR_POINTER, NULL,
			    -1);
	gtk_tree_selection_select_iter (selection, &iter);
	g_free (name);
}

static void
cb_duplicate_clicked (GtkWidget *ignore, SheetManager *state)
{
	g_warning ("'Duplicate' not implemented\n");
}

static void
cb_delete_clicked (GtkWidget *ignore, SheetManager *state)
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
cb_cancel_clicked (GtkWidget *ignore, SheetManager *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_delete_sheets (gpointer data, gpointer dummy)
{
	Sheet *sheet = data;

	sheet->pristine = FALSE;
	workbook_sheet_delete (sheet);
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
cb_ok_clicked (GtkWidget *ignore, SheetManager *state)
{
	GSList *new_order = NULL;
	GSList *changed_names = NULL;
	GSList *new_names = NULL;
	GSList *deleted_sheets = NULL;
	GSList *color_changed = NULL;
	GSList *new_colors_back = NULL;
	GSList *new_colors_fore  = NULL;
	GSList * old_order;
	Sheet *this_sheet;
	char *old_name, *new_name;
	GtkTreeIter this_iter;
	gint n = 0;
	GSList *this_new, *this_old;
	gboolean order_has_changed = FALSE;
	gboolean is_deleted;
	GdkColor *back, *fore;
	gboolean fore_changed, back_changed;

	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, n)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter, 
				    SHEET_POINTER, &this_sheet, 
				    SHEET_NAME, &old_name,
				    SHEET_NEW_NAME, &new_name,
				    IS_DELETED, &is_deleted,
				    BACKGROUND_COLOUR_POINTER, &back,
				    FOREGROUND_COLOUR_POINTER, &fore,
				    -1);
		if (!is_deleted) {
			new_order = g_slist_prepend (new_order, this_sheet);
			
			if (this_sheet == NULL || 
			    (strlen(new_name) > 0 && 
			     0 != g_str_compare (old_name, new_name))) {
				changed_names = g_slist_prepend (changed_names, this_sheet);
				new_names = g_slist_prepend (new_names, 
							     strlen(new_name) > 0 ? new_name : NULL);
			} else {
				g_free (new_name);
			}
			g_free (old_name);

			back_changed = (this_sheet == NULL) || 
				!sheet_order_gdk_color_equal (back, 
					      this_sheet->tab_color ? 
					      &this_sheet->tab_color->color : NULL);
			fore_changed = (this_sheet == NULL) || 
				!sheet_order_gdk_color_equal (fore, 
					      this_sheet->tab_text_color ? 
					      &this_sheet->tab_text_color->color : NULL);

			if (fore_changed || back_changed) {
				color_changed = g_slist_prepend (color_changed, this_sheet);
				new_colors_back = g_slist_prepend (new_colors_back, back);
				new_colors_fore = g_slist_prepend (new_colors_fore, fore);
			} else {
				if (back)
					gdk_color_free (back);
				if (fore)
					gdk_color_free (fore);
			}
			    

		} else
			deleted_sheets = g_slist_prepend (deleted_sheets, this_sheet);	
		n++;
	}

	color_changed= g_slist_reverse (color_changed);
	new_colors_back= g_slist_reverse (new_colors_back);
	new_colors_fore= g_slist_reverse (new_colors_fore);
	new_order = g_slist_reverse (new_order);
	new_names = g_slist_reverse (new_names);
	changed_names = g_slist_reverse (changed_names);
	old_order = g_slist_copy (state->old_order);
	
	if (deleted_sheets && 
	    !gnumeric_dialog_question_yes_no (state->wbcg,
					      _("Deletion of sheets is not undoable. "
						"Do you want to proceed?"), FALSE)) {
		/* clean-up */
		g_slist_free (deleted_sheets);
		deleted_sheets = NULL;
		g_slist_free (new_order);
		new_order = NULL;
		g_slist_free (changed_names);
		changed_names = NULL;
		g_slist_free (old_order);
		old_order = NULL;
		e_free_string_slist (new_names);
		new_names = NULL;
		return;
	}

	this_new = new_order;
	this_old = old_order;
	while (this_new != NULL && this_old != NULL) {
		if (this_new->data != this_old->data) {
			order_has_changed = TRUE;
			break;
		}
		this_new = this_new->next;
		this_old = this_old->next;
	}

	if (!order_has_changed) {
		g_slist_free (new_order);
		new_order = NULL;
		g_slist_free (old_order);
		old_order = NULL;
	}

	if ((new_order == NULL && changed_names == NULL && color_changed == NULL)
	    || !cmd_reorganize_sheets (WORKBOOK_CONTROL (state->wbcg), 
				       old_order, new_order,
				       changed_names, new_names, NULL,
				       color_changed, new_colors_back,
				       new_colors_fore)) {
		gtk_widget_destroy (GTK_WIDGET (state->dialog));
	} 
	if (deleted_sheets) {
		g_slist_foreach (deleted_sheets, cb_delete_sheets, NULL);
		g_slist_free (deleted_sheets);
		deleted_sheets = NULL;
	}
}

static void
cb_sheet_order_destroy (GtkWidget *ignored, SheetManager *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	g_object_unref (G_OBJECT (state->gui));
	state->gui = NULL;
	g_object_unref (state->sheet_image);
	state->sheet_image = NULL;
	if (state->old_order != NULL) {
		g_slist_free (state->old_order);
		state->old_order = NULL;
	}
	g_free (state);
}

void
dialog_sheet_order (WorkbookControlGUI *wbcg)
{
	SheetManager *state;
	GladeXML *gui;
	GtkTable *table;
	ColorGroup *cg;

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "sheet-order.glade");
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
	gtk_widget_set_size_request (state->delete_btn, 100,-1);

	state->ok_btn  = glade_xml_get_widget (gui, "ok_button");
	state->cancel_btn  = glade_xml_get_widget (gui, "cancel_button");
	state->old_order  = NULL;
	state->sheet_image =  gtk_widget_render_icon (state->dialog,
                                             "Gnumeric_MergeCells",
                                             GTK_ICON_SIZE_SMALL_TOOLBAR,
                                             "Gnumeric-Sheet-Manager");

	gtk_button_stock_alignment_set (GTK_BUTTON (state->up_btn),   0., .5, 0., 0.);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->down_btn), 0., .5, 0., 0.);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->add_btn), 0., .5, 0., 0.);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->delete_btn), 0., .5, 0., 0.);

	table = GTK_TABLE (glade_xml_get_widget (gui, "sheet_order_buttons_table"));
	cg = color_group_fetch ("back_color_group", wb_control_view (WORKBOOK_CONTROL (wbcg)));
	state->ccombo_back = color_combo_new  (gdk_pixbuf_new_from_inline (-1, gnm_bucket, 
									   FALSE, NULL),
					       _("Default"), NULL, cg);
	gtk_table_attach (table, state->ccombo_back,
			  0, 1, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_set_sensitive (state->ccombo_back, FALSE);

	cg = color_group_fetch ("fore_color_group", wb_control_view (WORKBOOK_CONTROL (wbcg)));
	state->ccombo_fore = color_combo_new  (gdk_pixbuf_new_from_inline (-1, gnm_font, 
									   FALSE, NULL),
					       _("Default"), NULL, cg);
	gtk_table_attach (table, state->ccombo_fore,
			  0, 1, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
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

/* FIXME: Add correct helpfile address */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"sheet-order.html");

	/* a candidate for merging into attach guru */
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_sheet_order_destroy), state);
	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
