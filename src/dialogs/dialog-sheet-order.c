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
	GtkWidget *close_btn;
} SheetManager;

enum {
	SHEET_NAME,
	SHEET_POINTER,
	NUM_COLMNS
};

static Sheet *
get_selected_sheet (SheetManager *state)
{
	GtkTreeSelection  *selection;
	GtkTreeIter iter;
	GValue value = {0, };
	Sheet *sheet;

	g_return_val_if_fail (state != NULL, NULL);

	selection = gtk_tree_view_get_selection (state->sheet_list);
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return NULL;

	gtk_tree_model_get_value (GTK_TREE_MODEL (state->model),
		&iter, SHEET_POINTER, &value);
	sheet = g_value_get_pointer (&value);
	g_value_unset (&value);

	if (sheet != NULL) {
		g_return_val_if_fail (IS_SHEET (sheet), NULL);
	}

	return sheet;
}

/**
 * Refreshes the buttons on a row (un)selection and selects the chosen sheet
 * for this view.
 */
void
cb_selection_changed (GtkTreeSelection *ignored, SheetManager *state)
{
	Sheet *sheet = get_selected_sheet (state);
	if (sheet != NULL) {
		int i = workbook_sheet_index_get (state->wb, sheet);
		wb_control_sheet_focus (WORKBOOK_CONTROL (state->wbcg), sheet);
		gtk_widget_set_sensitive (state->up_btn,
			i > 0);
		gtk_widget_set_sensitive (state->down_btn,
			i < (workbook_sheet_count (state->wb)-1));
	}
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
	}

	column = gtk_tree_view_column_new_with_attributes ("Sheets",
			gtk_cell_renderer_text_new (),
			"text", SHEET_NAME, NULL);
/*	gtk_tree_view_column_set_sort_column_id (column, 0);*/
	gtk_tree_view_append_column (state->sheet_list, column);
	gtk_tree_view_set_reorderable (state->sheet_list, FALSE);

	/* Init the buttons & selection */
	cb_selection_changed (NULL, state);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_selection_changed), state);
	g_signal_connect (selection,
		"row_inserted",
		G_CALLBACK (cb_row_inserted), state);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->sheet_list));
}

/* Actual implementation of the re-ordering sheets */
static void
move_cb (SheetManager *state, gint direction)
{
	Sheet *sheet = get_selected_sheet (state);
	if (sheet != NULL) {
		GtkTreeIter iter;
		GtkTreeSelection *selection = gtk_tree_view_get_selection (state->sheet_list);

		workbook_sheet_move (sheet, direction);

		if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
			return;

		gtk_list_store_remove (state->model, &iter);
		gtk_list_store_insert (state->model, &iter,
			workbook_sheet_index_get (sheet->workbook, sheet));
		gtk_list_store_set (state->model, &iter,
			  SHEET_NAME, sheet->name_unquoted,
			  SHEET_POINTER, sheet,
			  -1);
		gtk_tree_selection_select_iter (selection, &iter);

		wb_view_sheet_focus (
			wb_control_view (WORKBOOK_CONTROL (state->wbcg)),
			sheet);

		/* this is a little hack-ish, but we need to refresh the buttons */
		cb_selection_changed (NULL, state);
	}
}

static void cb_up   (GtkWidget *w, SheetManager *state) { move_cb (state, -1); }
static void cb_down (GtkWidget *w, SheetManager *state) { move_cb (state,  1); }

static void
close_clicked_cb (GtkWidget *ignore, SheetManager *state)
{
	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_sheet_order_destroy (GtkWidget *ignored, SheetManager *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	g_object_unref (G_OBJECT (state->gui));
	state->gui = NULL;
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
	state->close_btn  = glade_xml_get_widget (gui, "close_button");

	populate_sheet_list (state);

	gtk_signal_connect (GTK_OBJECT (state->up_btn),
		"clicked",
		GTK_SIGNAL_FUNC (cb_up), state);
	gtk_signal_connect (GTK_OBJECT (state->down_btn),
		"clicked",
		GTK_SIGNAL_FUNC (cb_down), state);
	gtk_signal_connect (GTK_OBJECT (state->close_btn),
		"clicked",
		GTK_SIGNAL_FUNC (close_clicked_cb), state);

	/* a candidate for merging into attach guru */
	gtk_signal_connect (GTK_OBJECT (state->dialog),
		"destroy",
		GTK_SIGNAL_FUNC (cb_sheet_order_destroy), state);
	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
