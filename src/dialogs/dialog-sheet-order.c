/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-sheet-order.c: Dialog to change the order of sheets in the Gnumeric
 * spreadsheet
 *
 * Author:
 * 	Jody Goldberg <jody@gnome.org>
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
#include <commands.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

typedef struct {
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkTreeView *sheet_list;
	GtkListStore *model;
	GtkWidget *up_btn;
	GtkWidget *down_btn;
	GtkWidget *ok_btn;
	GtkWidget *cancel_btn;

	GSList *old_order;
} SheetManager;

enum {
	SHEET_NAME,
	SHEET_POINTER,
	NUM_COLMNS
};

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
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->sheet_list);

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_widget_set_sensitive (state->up_btn, FALSE);
		gtk_widget_set_sensitive (state->down_btn, FALSE);
		return;
	}

	gtk_widget_set_sensitive (state->up_btn,
				  location_of_iter (&iter, state->model) > 0);

	row = location_of_iter (&iter, state->model);
	gtk_widget_set_sensitive (state->down_btn,
				  gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, row+1));
}

static void
cb_row_inserted (GtkTreeModel *tree_model,
		 GtkTreePath  *path,
		 GtkTreeIter  *iter,
		 SheetManager *state)
{
	GValue value = {0, };
	Sheet *sheet;

	gtk_tree_model_get_value (GTK_TREE_MODEL (state->model),
		iter, SHEET_POINTER, &value);
	sheet = g_value_get_pointer (&value);
	g_value_unset (&value);

	if (sheet != NULL) {
		puts (sheet->name_unquoted);
	}
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
	int i, n = workbook_sheet_count (state->wb);

	state->model = gtk_list_store_new (NUM_COLMNS,
		G_TYPE_STRING, G_TYPE_POINTER);
	state->sheet_list = GTK_TREE_VIEW (gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	selection = gtk_tree_view_get_selection (state->sheet_list);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	for (i = 0 ; i < n ; i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
		gtk_list_store_append (state->model, &iter);
		gtk_list_store_set (state->model, &iter,
			  SHEET_NAME, sheet->name_unquoted,
			  SHEET_POINTER, sheet,
			  -1);
		if (sheet == cur_sheet)
			gtk_tree_selection_select_iter (selection, &iter);
		state->old_order = g_slist_prepend (state->old_order, sheet);
	}

	state->old_order = g_slist_reverse (state->old_order);

	column = gtk_tree_view_column_new_with_attributes ("Sheets",
			gtk_cell_renderer_text_new (),
			"text", SHEET_NAME, NULL);
	gtk_tree_view_append_column (state->sheet_list, column);
	gtk_tree_view_set_reorderable (state->sheet_list, TRUE);
	gtk_tree_view_set_headers_visible (state->sheet_list, FALSE);

	/* Init the buttons & selection */
	cb_selection_changed (NULL, state);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_selection_changed), state);
	g_signal_connect (GTK_TREE_MODEL (state->model),
		"row_inserted",
		G_CALLBACK (cb_row_inserted), state);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->sheet_list));
}

static void
move_cb (SheetManager *state, gint direction)
{
	char* name;
	Sheet * sheet;
	GtkTreeIter iter;
	gint row;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->sheet_list);

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
			    SHEET_NAME, &name,
			    SHEET_POINTER, &sheet,
			    -1);
	row = location_of_iter (&iter, state->model);
	if (row + direction < 0)
		return;
	gtk_list_store_remove (state->model, &iter);
	gtk_list_store_insert (state->model, &iter, row + direction);
	gtk_list_store_set (state->model, &iter,
			    SHEET_NAME, name,
			    SHEET_POINTER, sheet,
			    -1);
	gtk_tree_selection_select_iter (selection, &iter);
	g_free (name);

	wb_view_sheet_focus (
		wb_control_view (WORKBOOK_CONTROL (state->wbcg)),
		sheet);

	/* this is a little hack-ish, but we need to refresh the buttons */
	cb_selection_changed (NULL, state);
}

static void cb_up   (GtkWidget *w, SheetManager *state) { move_cb (state, -1); }
static void cb_down (GtkWidget *w, SheetManager *state) { move_cb (state,  1); }

static void
cb_cancel_clicked (GtkWidget *ignore, SheetManager *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_ok_clicked (GtkWidget *ignore, SheetManager *state)
{
	GSList *new_order = NULL;
	Sheet *this_sheet;
	GtkTreeIter this_iter;
	gint n = 0;
	
	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, n)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter, SHEET_POINTER,
				    &this_sheet, -1);
		new_order = g_slist_prepend (new_order, this_sheet);
		n++;
	}

	new_order = g_slist_reverse (new_order);

/* FIXME we shouldn't bother invoking cmd_reorganize_sheets unless there are changes */

	if (!cmd_reorganize_sheets (WORKBOOK_CONTROL (state->wbcg), state->old_order,
				   new_order, NULL, NULL)) {
		state->old_order = NULL;
		gtk_widget_destroy (GTK_WIDGET (state->dialog));
	} else 
		g_slist_free (new_order);
}

static void
cb_sheet_order_destroy (GtkWidget *ignored, SheetManager *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	g_object_unref (G_OBJECT (state->gui));
	state->gui = NULL;
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

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "sheet-order.glade");
        if (gui == NULL)
                return;

	state = g_new0 (SheetManager, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->wb	 = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->dialog     = glade_xml_get_widget (gui, "sheet-order-dialog");
	state->up_btn     = glade_xml_get_widget (gui, "up_button");
	state->down_btn   = glade_xml_get_widget (gui, "down_button");
	state->ok_btn  = glade_xml_get_widget (gui, "ok_button");
	state->cancel_btn  = glade_xml_get_widget (gui, "cancel_button");
	state->old_order  = NULL;

	gtk_button_stock_alignment_set (GTK_BUTTON (state->up_btn),   0., .5, 0., 0.);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->down_btn), 0., .5, 0., 0.);

	populate_sheet_list (state);

	g_signal_connect (G_OBJECT (state->up_btn),
		"clicked",
		G_CALLBACK (cb_up), state);
	g_signal_connect (G_OBJECT (state->down_btn),
		"clicked",
		G_CALLBACK (cb_down), state);
	g_signal_connect (G_OBJECT (state->ok_btn),
		"clicked",
		G_CALLBACK (cb_ok_clicked), state);
	g_signal_connect (G_OBJECT (state->cancel_btn),
		"clicked",
		G_CALLBACK (cb_cancel_clicked), state);

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
