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
	GtkWidget *up_btn;
	GtkWidget *down_btn;
	GtkWidget *close_btn;
	gint       current_row;
} SheetManager;

/**
 * Refreshes the buttons on a row (un)selection
 * And moves the representative page/sheet in the notebook
 * To the foreground
 */
void
cb_row_activated (GtkTreeView  *tree_view,
                  GtkTreePath  *path,
		  SheetManager *state)
{
#if 0
	state->current_row = row;

	gtk_widget_set_sensitive (state->up_btn, row > 0);
	gtk_widget_set_sensitive (state->down_btn, can_go);
	can_go = !(row >= (numrows - 1)); /* bottom row test */
	gint numrows = state->sheet_list->rows;

	/* Display/focus on the selected sheet underneath us */
	wb_control_sheet_focus (WORKBOOK_CONTROL (state->wbcg), sheet);
		gtk_clist_get_row_data (GTK_CLIST (state->sheet_list), row);
#endif
}

/* Add all of the sheets to the sheet_list */
static void
populate_sheet_list (SheetManager *state)
{
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GtkWidget *scrolled = glade_xml_get_widget (state->gui, "scrolled");
	GtkListStore *store = gtk_list_store_new (1, G_TYPE_STRING);
	int i, n = workbook_sheet_count (state->wb);

	for (i = 0 ; i < n ; i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
			  0, sheet->name_unquoted,
			  -1);
	}

	state->sheet_list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	column = gtk_tree_view_column_new_with_attributes ("Sheets",
			gtk_cell_renderer_text_new (),
			"text", 0, NULL);
	gtk_tree_view_column_set_sort_column_id (column, 0);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->sheet_list), column);
	gtk_widget_show_all (GTK_WIDGET (state->sheet_list));
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->sheet_list));

	/* Init the buttons & selection */
	cb_row_activated (state->sheet_list, NULL, state);
}

/* Actual implementation of the re-ordering sheets */
static void
move_cb (SheetManager *state, gint direction)
{
	gint numrows = 0;

#if 0
	if (numrows && state->current_row >= 0) {
		GList *selection = GTK_CLIST (state->sheet_list)->selection;
		Sheet *sheet = gtk_clist_get_row_data (GTK_CLIST (state->sheet_list), state->current_row);
		gint source = GPOINTER_TO_INT (g_list_nth_data (selection, 0));
		gint dest = source + direction;
		WorkbookView *view = wb_control_view (
			WORKBOOK_CONTROL (state->wbcg));

		gtk_clist_row_move (GTK_CLIST (state->sheet_list), source, dest);

		workbook_sheet_move (sheet, direction);
		wb_view_sheet_focus (view, sheet);

		/* this is a little hack-ish, but we need to refresh the buttons */
		cb_row_activated (state->sheet_list, NULL, state);
	}
#endif
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

	state->current_row = -1;

	populate_sheet_list (state);

	gtk_signal_connect (GTK_OBJECT (state->sheet_list),
		"row_activated",
		GTK_SIGNAL_FUNC (cb_row_activated), state);
	gtk_signal_connect (GTK_OBJECT (state->up_btn),
		"clicked",
		GTK_SIGNAL_FUNC (cb_up), state);
	gtk_signal_connect (GTK_OBJECT (state->down_btn),
		"clicked",
		GTK_SIGNAL_FUNC (cb_down), state);
	gtk_signal_connect (GTK_OBJECT (state->close_btn),
		"clicked",
		GTK_SIGNAL_FUNC (close_clicked_cb), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog),
		"destroy",
		GTK_SIGNAL_FUNC (cb_sheet_order_destroy), state);

	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));
}
