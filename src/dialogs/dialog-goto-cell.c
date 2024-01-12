/*
 * dialog-goto-cell.c:  Implements the "goto cell/navigator" functionality
 *
 * Author:
 * Andreas J. Guelzow <aguelzow@pyrshep.ca>
 *
 * Copyright (C) Andreas J. Guelzow <aguelzow@pyrshep.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <workbook-control.h>
#include <ranges.h>
#include <value.h>
#include <expr-name.h>
#include <expr.h>
#include <sheet.h>
#include <workbook.h>
#include <workbook-view.h>
#include <workbook-control.h>
#include <selection.h>
#include <parse-util.h>
#include <sheet-view.h>

#include <wbc-gtk.h>


#define GOTO_KEY "goto-dialog"

typedef struct {
	WBCGtk  *wbcg;
	Workbook  *wb;

	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkWidget *close_button;
	GtkWidget *go_button;
	GtkEntry *goto_text;

	GtkSpinButton *spin_rows, *spin_cols;

	GtkTreeStore  *model;
	GtkTreeView   *treeview;
	GtkTreeSelection   *selection;

	gulong sheet_order_changed_listener;
	gulong sheet_added_listener;
	gulong sheet_deleted_listener;
} GotoState;

enum {
	ITEM_NAME,
	SHEET_NAME,
	SHEET_POINTER,
	EXPRESSION,
	NUM_COLUMNS
};

static void
cb_dialog_goto_free (GotoState  *state)
{
	if (state->sheet_order_changed_listener)
		g_signal_handler_disconnect (G_OBJECT (state->wb),
					     state->sheet_order_changed_listener);
	if (state->sheet_added_listener)
		g_signal_handler_disconnect (G_OBJECT (state->wb),
					     state->sheet_added_listener);
	if (state->sheet_deleted_listener)
		g_signal_handler_disconnect (G_OBJECT (state->wb),
					     state->sheet_deleted_listener);

	if (state->gui != NULL)
		g_object_unref (state->gui);
	if (state->model != NULL)
		g_object_unref (state->model);
	g_free (state);
}

static void
cb_dialog_goto_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			      GotoState *state)
{
	gtk_widget_destroy (state->dialog);
}

static GnmValue *
dialog_goto_get_val (GotoState *state)
{
	char const *text = gtk_entry_get_text (state->goto_text);
	Sheet *sheet = wb_control_cur_sheet (GNM_WBC (state->wbcg));
	GnmValue *val = value_new_cellrange_str (sheet, text);

	if (val == NULL) {
		GnmParsePos pp;
		GnmNamedExpr *nexpr = expr_name_lookup
			(parse_pos_init_sheet (&pp, sheet), text);
		if (nexpr != NULL && !expr_name_is_placeholder (nexpr)) {
			val = gnm_expr_top_get_range (nexpr->texpr);
		}
	}
	return val;
}

static void
cb_dialog_goto_go_clicked (G_GNUC_UNUSED GtkWidget *button,
			   GotoState *state)
{
	GnmEvalPos ep;
	GnmRangeRef range;
	gint cols = gtk_spin_button_get_value_as_int (state->spin_cols);
	gint rows = gtk_spin_button_get_value_as_int (state->spin_rows);
	GnmValue *val = dialog_goto_get_val (state);
	Sheet *sheet = wb_control_cur_sheet (GNM_WBC (state->wbcg));

	if (val == NULL)
		return;

	val->v_range.cell.b.row = val->v_range.cell.a.row + (rows - 1);
	val->v_range.cell.b.col = val->v_range.cell.a.col + (cols - 1);
	eval_pos_init_sheet (&ep, sheet);
	gnm_cellref_make_abs (&range.a, &val->v_range.cell.a, &ep);
	gnm_cellref_make_abs (&range.b, &val->v_range.cell.b, &ep);
	value_release (val);

	wb_control_jump (GNM_WBC (state->wbcg), sheet, &range);
	return;
}

static void
cb_dialog_goto_update_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
				   GotoState *state)
{
	GnmValue *val = dialog_goto_get_val (state);
	if (val != NULL) {
		gint cols, rows;
		Sheet *sheet = val->v_range.cell.a.sheet;
		GnmSheetSize const *ssz;

		if (sheet == NULL)
			sheet = wb_control_cur_sheet (GNM_WBC (state->wbcg));
		ssz = gnm_sheet_get_size (sheet);

		cols = ssz->max_cols;
		rows = ssz->max_rows;

		if (val->v_range.cell.a.sheet != NULL &&
		    val->v_range.cell.b.sheet != NULL &&
		    val->v_range.cell.a.sheet != val->v_range.cell.b.sheet) {
			ssz = gnm_sheet_get_size (sheet);
			if (cols > ssz->max_cols)
				cols = ssz->max_cols;
			if (rows > ssz->max_rows)
				cols = ssz->max_rows;
		}
		cols -= val->v_range.cell.a.col;
		rows -= val->v_range.cell.a.row;

		if (cols < 1) cols = 1;
		if (rows < 1) rows = 1;

		gtk_spin_button_set_range (state->spin_cols, 1, cols);
		gtk_spin_button_set_range (state->spin_rows, 1, rows);

		gtk_widget_set_sensitive (state->go_button, TRUE);

		value_release (val);
	} else
		gtk_widget_set_sensitive (state->go_button, FALSE);
	gtk_entry_set_activates_default (state->goto_text, (val != NULL));
}

typedef struct {
	GtkTreeIter  iter;
	GotoState   *state;
} LoadNames;

static void
cb_load_names (G_GNUC_UNUSED gpointer key,
	       GnmNamedExpr *nexpr, LoadNames *user)
{
	GtkTreeIter iter;
	gboolean is_address = gnm_expr_top_is_rangeref (nexpr->texpr);

	if (expr_name_is_placeholder (nexpr))
		return;

	if (is_address) {
		gtk_tree_store_append (user->state->model, &iter, &user->iter);
		gtk_tree_store_set (user->state->model, &iter,
				    ITEM_NAME, expr_name_name (nexpr),
				    SHEET_POINTER, nexpr->pos.sheet,
				    EXPRESSION,	nexpr,
				    -1);
	}
}

static void
dialog_goto_load_names (GotoState *state)
{
	Sheet *sheet;
	LoadNames closure;
	int i, l;

	gtk_tree_store_clear (state->model);

	closure.state = state;
	gtk_tree_store_append (state->model, &closure.iter, NULL);
	gtk_tree_store_set (state->model, &closure.iter,
			    SHEET_NAME,		_("Workbook Level"),
			    ITEM_NAME,		NULL,
			    SHEET_POINTER,	NULL,
			    EXPRESSION,		NULL,
			    -1);
	workbook_foreach_name (state->wb, FALSE,
			       (GHFunc)cb_load_names, &closure);

	l = workbook_sheet_count (state->wb);
	for (i = 0; i < l; i++) {
		sheet = workbook_sheet_by_index (state->wb, i);
		gtk_tree_store_append (state->model, &closure.iter, NULL);
		gtk_tree_store_set (state->model, &closure.iter,
				    SHEET_NAME,		sheet->name_unquoted,
				    ITEM_NAME,		NULL,
				    SHEET_POINTER,	sheet,
				    EXPRESSION,		NULL,
				    -1);
	}
}

static void
cb_dialog_goto_selection_changed (GtkTreeSelection *the_selection, GotoState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	Sheet        *sheet;
	GnmNamedExpr *name;

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    SHEET_POINTER, &sheet,
				    EXPRESSION, &name,
				    -1);
		if (name && gnm_expr_top_is_rangeref (name->texpr)) {
			GnmParsePos pp;
			char *where_to;

			if (NULL == sheet)
				sheet = wb_control_cur_sheet ( GNM_WBC (state->wbcg));

			parse_pos_init_sheet (&pp, sheet);
			where_to = expr_name_as_string  (name, &pp, gnm_conventions_default);
			if (wb_control_parse_and_jump (GNM_WBC (state->wbcg), where_to))
				gtk_entry_set_text (state->goto_text,
						    where_to);
			g_free (where_to);
			return;
		}
		if (sheet)
			wb_view_sheet_focus (
				wb_control_view (GNM_WBC (state->wbcg)), sheet);
	}
}


static void
cb_sheet_order_changed (G_GNUC_UNUSED Workbook *wb, GotoState *state)
{
	dialog_goto_load_names (state);
}

static void
cb_sheet_deleted (G_GNUC_UNUSED Workbook *wb, GotoState *state)
{
	dialog_goto_load_names (state);
	cb_dialog_goto_update_sensitivity (NULL, state);
}

static void
cb_sheet_added (G_GNUC_UNUSED Workbook *wb, GotoState *state)
{
	dialog_goto_load_names (state);
	cb_dialog_goto_update_sensitivity (NULL, state);
}

static void
dialog_goto_load_selection (GotoState *state)
{
	SheetView *sv = wb_control_cur_sheet_view
		(GNM_WBC (state->wbcg));
	GnmRange const *first = selection_first_range (sv, NULL, NULL);

	if (first != NULL) {
		gint rows = range_height (first);
		gint cols = range_width (first);
		GnmConventionsOut out;
		GString *str = g_string_new (NULL);
		GnmParsePos pp;
		GnmRangeRef rr;

		out.accum = str;
		out.pp = parse_pos_init_sheet (&pp, sv->sheet);
		out.convs = sheet_get_conventions (sv->sheet);
		gnm_cellref_init (&rr.a, NULL, first->start.col,
				  first->start.row, TRUE);
		gnm_cellref_init (&rr.b, NULL, first->start.col,
				  first->start.row, TRUE);
		rangeref_as_string (&out, &rr);
		gtk_entry_set_text (state->goto_text, str->str);
		gtk_editable_select_region (GTK_EDITABLE (state->goto_text),
					    0, -1);
		g_string_free (str, TRUE);
		cb_dialog_goto_update_sensitivity (NULL, state);
		gtk_spin_button_set_value (state->spin_rows, rows);
		gtk_spin_button_set_value (state->spin_cols, cols);
	} else
		cb_dialog_goto_update_sensitivity (NULL, state);

}

/**
 * dialog_goto_init:
 * @state:
 *
 * Create the dialog (guru).
 **/
static gboolean
dialog_goto_init (GotoState *state)
{
	GtkGrid *grid;
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;

	grid = GTK_GRID (go_gtk_builder_get_widget (state->gui, "names"));
	state->goto_text = GTK_ENTRY (gtk_entry_new ());
	gtk_widget_set_hexpand (GTK_WIDGET (state->goto_text), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->goto_text), 0, 2, 1, 1);
	g_signal_connect_after (G_OBJECT (state->goto_text),
		"changed",
		G_CALLBACK (cb_dialog_goto_update_sensitivity), state);

	state->spin_rows = GTK_SPIN_BUTTON
		(go_gtk_builder_get_widget (state->gui, "spin-rows"));
	state->spin_cols = GTK_SPIN_BUTTON
		(go_gtk_builder_get_widget (state->gui, "spin-columns"));

	/* Set-up treeview */
	scrolled = go_gtk_builder_get_widget (state->gui, "scrolled");
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
					     GTK_SHADOW_ETCHED_IN);

	state->model = gtk_tree_store_new (NUM_COLUMNS, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_POINTER,
					   G_TYPE_POINTER);
	state->treeview = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_BROWSE);
	g_signal_connect (state->selection,
		"changed",
		G_CALLBACK (cb_dialog_goto_selection_changed), state);

	column = gtk_tree_view_column_new_with_attributes (_("Sheet"),
							   gtk_cell_renderer_text_new (),
							   "text", SHEET_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, SHEET_NAME);
	gtk_tree_view_append_column (state->treeview, column);

	column = gtk_tree_view_column_new_with_attributes (_("Cell"),
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, ITEM_NAME);
	gtk_tree_view_append_column (state->treeview, column);
	gtk_tree_view_set_headers_visible (state->treeview, TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview));
	dialog_goto_load_names (state);
	/* Finished set-up of treeview */

	/* Listen for sheet changes */
	state->sheet_order_changed_listener = g_signal_connect (G_OBJECT (state->wb),
		"sheet_order_changed", G_CALLBACK (cb_sheet_order_changed),
		state);
	state->sheet_added_listener = g_signal_connect (G_OBJECT (state->wb),
		"sheet_added", G_CALLBACK (cb_sheet_added),
		state);
	state->sheet_deleted_listener = g_signal_connect (G_OBJECT (state->wb),
		"sheet_deleted", G_CALLBACK (cb_sheet_deleted),
		state);

	state->close_button  = go_gtk_builder_get_widget (state->gui, "close_button");
	g_signal_connect (G_OBJECT (state->close_button),
		"clicked",
		G_CALLBACK (cb_dialog_goto_close_clicked), state);

	state->go_button  = go_gtk_builder_get_widget (state->gui, "go_button");
	g_signal_connect (G_OBJECT (state->go_button),
		"clicked",
		G_CALLBACK (cb_dialog_goto_go_clicked), state);
	gtk_window_set_default (GTK_WINDOW (state->dialog), state->go_button);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_GOTO_CELL);

	dialog_goto_load_selection (state);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_goto_free);

	return FALSE;
}


void
dialog_goto_cell (WBCGtk *wbcg)
{
	GotoState* state;
	GtkBuilder *gui;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, GOTO_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/goto.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	state = g_new (GotoState, 1);
	state->wbcg   = wbcg;
	state->wb     = wb_control_get_workbook (GNM_WBC (wbcg));
	state->gui    = gui;
        state->dialog = go_gtk_builder_get_widget (state->gui, "goto_dialog");

	if (dialog_goto_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the goto dialog."));
		g_free (state);
		return;
	}

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       GOTO_KEY);

	gtk_widget_show_all (state->dialog);
}
