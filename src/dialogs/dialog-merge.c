/*
 * dialog-merge.c: Dialog to merge a list of data into a given range
 *
 *
 * Author:
 *	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Copyright (C) 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <ranges.h>
#include <value.h>
#include <commands.h>
#include <selection.h>
#include <widgets/gnm-expr-entry.h>


#define MERGE_KEY            "merge-dialog"

typedef struct {
	WBCGtk  *wbcg;
	Sheet *sheet;
	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkWidget *warning_dialog;
	GtkTreeView *list;
	GtkListStore *model;
	GnmExprEntry *zone;
	GnmExprEntry *data;
	GnmExprEntry *field;
	GtkWidget *add_btn;
	GtkWidget *change_btn;
	GtkWidget *delete_btn;
	GtkWidget *merge_btn;
	GtkWidget *cancel_btn;

} MergeState;

enum {
	DATA_RANGE,
	FIELD_LOCATION,
	NUM_COLUMNS
};

static void
cb_merge_update_buttons (G_GNUC_UNUSED gpointer ignored,
			 MergeState *state)
{
/* Note: ignored could be NULL or  an expr-entry. */
	GtkTreeIter  iter;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->list);
	gboolean has_selection, has_data, has_work;

	has_selection = gtk_tree_selection_get_selected (selection, NULL, &iter);
	has_data = gnm_expr_entry_is_cell_ref (state->data, state->sheet, TRUE)
		&& gnm_expr_entry_is_cell_ref (state->field, state->sheet, FALSE);
	has_work = (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (state->model), NULL) > 0)
		&& gnm_expr_entry_is_cell_ref (state->zone, state->sheet, TRUE);

	gtk_widget_set_sensitive (state->add_btn, has_data);
	gtk_widget_set_sensitive (state->change_btn, has_data && has_selection);
	gtk_widget_set_sensitive (state->delete_btn, has_selection);
	gtk_widget_set_sensitive (state->merge_btn, has_work);
}

static void
cb_merge_selection_changed (GtkTreeSelection *selection, MergeState *state)
{
	GtkTreeIter  iter;
	gboolean has_selection;
	char *data_string = NULL, *field_string = NULL;

	has_selection = gtk_tree_selection_get_selected (selection, NULL, &iter);

	if (has_selection) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
				    DATA_RANGE, &data_string,
				    FIELD_LOCATION, &field_string,
				    -1);
		gnm_expr_entry_load_from_text (state->data, data_string);
		gnm_expr_entry_load_from_text (state->field, field_string);
		g_free (data_string);
		g_free (field_string);
	}
	cb_merge_update_buttons (NULL, state);
}

static void
merge_store_info_in_list (GtkTreeIter *iter, MergeState *state)
{
	char     *data_text, *field_text;
	GtkTreeSelection  *selection;

	data_text = gnm_expr_entry_global_range_name (state->data, state->sheet);
	field_text = gnm_expr_entry_global_range_name (state->field,  state->sheet);

	gtk_list_store_set (state->model, iter,
			    DATA_RANGE, data_text,
			    FIELD_LOCATION, field_text,
			    -1);

	g_free (data_text);
	g_free (field_text);

	selection = gtk_tree_view_get_selection (state->list);
	gtk_tree_selection_select_iter (selection, iter);
}


static void
cb_merge_add_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		      MergeState *state)
{
	GtkTreeIter iter;
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->list);

	if (!gtk_tree_selection_get_selected (selection, NULL, &sel_iter))
		gtk_list_store_append (state->model, &iter);
	else
		gtk_list_store_insert_before (state->model, &iter, &sel_iter);

	merge_store_info_in_list (&iter, state);
}

static void
cb_merge_change_clicked (G_GNUC_UNUSED GtkWidget *ignore,
			 MergeState *state)
{
	GtkTreeIter iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->list);

	if (gtk_tree_selection_get_selected (selection, NULL, &iter))
		merge_store_info_in_list (&iter, state);
}

static void
cb_merge_delete_clicked (G_GNUC_UNUSED GtkWidget *ignore,
			 MergeState *state)
{
	GtkTreeIter iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->list);

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_list_store_remove (state->model, &iter);
	}
}

static void
cb_merge_cancel_clicked (G_GNUC_UNUSED GtkWidget *ignore,
			 MergeState *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_merge_find_shortest_column (gpointer data, gpointer lp)
{
	GnmValue *range = data;
	int *length = lp;
	int r_length = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;

	if (r_length < *length)
		*length = r_length;
}

static void
cb_merge_find_longest_column (gpointer data, gpointer lp)
{
	GnmValue *range = data;
	int *length = lp;
	int r_length = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;

	if (r_length > *length)
		*length = r_length;
}

static void
cb_merge_trim_data (gpointer data, gpointer lp)
{
	GnmValue *range = data;
	int *length = lp;
	int r_length = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;

	if (r_length > *length)
		range->v_range.cell.b.row = range->v_range.cell.a.row + *length - 1;
	range->v_range.cell.b.col = range->v_range.cell.a.col;
}


static void
cb_merge_merge_clicked (G_GNUC_UNUSED GtkWidget *ignore,
			MergeState *state)
{
	GtkTreeIter this_iter;
	gint n = 0;
	char *data_string = NULL, *field_string = NULL;
	GSList *data_list = NULL, *field_list = NULL;
	GnmValue *v_zone;
	gint field_problems = 0;
	gint min_length = gnm_sheet_get_max_rows (state->sheet);
	gint max_length = 0;

	v_zone = gnm_expr_entry_parse_as_value (state->zone, state->sheet);
	g_return_if_fail (v_zone != NULL);

	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, n)) {
		GnmValue *v_data, *v_field;
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter,
				    DATA_RANGE, &data_string,
				    FIELD_LOCATION, &field_string,
				    -1);
		v_data = value_new_cellrange_str (state->sheet, data_string);
		v_field = value_new_cellrange_str (state->sheet, field_string);
		g_free (data_string);
		g_free (field_string);

		g_return_if_fail (v_data != NULL && v_field != NULL);
		if (!global_range_contained (state->sheet, v_field, v_zone))
			field_problems++;
		data_list = g_slist_prepend (data_list, v_data);
		field_list = g_slist_prepend (field_list, v_field);
		n++;
	}

	if (field_problems > 0) {
		char *text;
		if (field_problems == 1)
			text = g_strdup (_("One field is not part of the merge zone!"));
		else
			text = g_strdup_printf (_("%i fields are not part of the merge zone!"),
						field_problems);
		go_gtk_notice_nonmodal_dialog ((GtkWindow *) state->dialog,
					       &(state->warning_dialog),
					       GTK_MESSAGE_ERROR,
					       "%s", text);
		g_free (text);
		value_release (v_zone);
		range_list_destroy (data_list);
		range_list_destroy (field_list);
		return;
	}

	g_slist_foreach (data_list, cb_merge_find_shortest_column, &min_length);
	g_slist_foreach (data_list, cb_merge_find_longest_column, &max_length);

	if (min_length < max_length) {
		char *text = g_strdup_printf (_("The data columns range in length from "
						"%i to %i. Shall we trim the lengths to "
						"%i and proceed?"), min_length, max_length,
					      min_length);

		if (go_gtk_query_yes_no (GTK_WINDOW (state->dialog), TRUE,
					 "%s", text)) {
			g_slist_foreach (data_list, cb_merge_trim_data, &min_length);
			g_free (text);
		} else {
			g_free (text);
			value_release (v_zone);
			range_list_destroy (data_list);
			range_list_destroy (field_list);
			return;
		}

	}

	if (!cmd_merge_data (GNM_WBC (state->wbcg), state->sheet,
			     v_zone, field_list, data_list))
		gtk_widget_destroy (state->dialog);
}

static void
cb_merge_destroy (MergeState *state)
{
	if (state->model != NULL)
		g_object_unref (state->model);
	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_free (state);
}

void
dialog_merge (WBCGtk *wbcg)
{
	MergeState *state;
	GtkBuilder *gui;
	GtkGrid *grid;
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	GnmRange const *r;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, MERGE_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/merge.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	state = g_new0 (MergeState, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->sheet = wb_control_cur_sheet (GNM_WBC (state->wbcg));
	state->dialog     = go_gtk_builder_get_widget (gui, "Merge");
	state->warning_dialog = NULL;

	state->add_btn   = go_gtk_builder_get_widget (gui, "add_button");
	state->delete_btn   = go_gtk_builder_get_widget (gui, "remove_button");
	state->merge_btn  = go_gtk_builder_get_widget (gui, "merge_button");
	state->change_btn  = go_gtk_builder_get_widget (gui, "change_button");
	state->cancel_btn  = go_gtk_builder_get_widget (gui, "cancel_button");
	gtk_widget_set_size_request (state->delete_btn, 100, -1);

	gtk_button_set_alignment (GTK_BUTTON (state->add_btn),    0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->delete_btn), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->change_btn), 0., .5);

	grid = GTK_GRID (go_gtk_builder_get_widget (gui, "main-grid"));
	state->zone = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->zone, GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->zone));
	gtk_label_set_mnemonic_widget (GTK_LABEL (go_gtk_builder_get_widget (gui, "var1-label")),
				       GTK_WIDGET (state->zone));
	gtk_widget_set_hexpand (GTK_WIDGET (state->zone), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->zone), 1, 0, 2, 1);
	r = selection_first_range (
		wb_control_cur_sheet_view (GNM_WBC (wbcg)), NULL, NULL);
	if (r != NULL)
		gnm_expr_entry_load_from_range (state->zone,
			state->sheet, r);

	state->data = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->data, GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->data), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->data), 0, 5, 1, 1);

	state->field = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->field, GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->field), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->field), 1, 5, 1, 1);

	scrolled = go_gtk_builder_get_widget (state->gui, "scrolled");
	state->model = gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
	state->list = GTK_TREE_VIEW (gtk_tree_view_new_with_model
					   (GTK_TREE_MODEL (state->model)));
	selection = gtk_tree_view_get_selection (state->list);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	column = gtk_tree_view_column_new_with_attributes (_("Input Data"),
							   gtk_cell_renderer_text_new (),
							   "text", DATA_RANGE,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, DATA_RANGE);
	gtk_tree_view_column_set_min_width (column, 150);
	gtk_tree_view_append_column (state->list, column);
	column = gtk_tree_view_column_new_with_attributes (_("Merge Field"),
							   gtk_cell_renderer_text_new (),
							   "text", FIELD_LOCATION,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, FIELD_LOCATION);
	gtk_tree_view_column_set_min_width (column, 100);
	gtk_tree_view_append_column (state->list, column);

	gtk_tree_view_set_headers_clickable (state->list, TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->list));

	cb_merge_update_buttons (NULL, state);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_merge_selection_changed), state);

	g_signal_connect_after (G_OBJECT (state->zone),
		"changed",
		G_CALLBACK (cb_merge_update_buttons), state);
	g_signal_connect_after (G_OBJECT (state->data),
		"changed",
		G_CALLBACK (cb_merge_update_buttons), state);
	g_signal_connect_after (G_OBJECT (state->field),
		"changed",
		G_CALLBACK (cb_merge_update_buttons), state);

	g_signal_connect (G_OBJECT (state->add_btn),
		"clicked",
		G_CALLBACK (cb_merge_add_clicked), state);
	g_signal_connect (G_OBJECT (state->change_btn),
		"clicked",
		G_CALLBACK (cb_merge_change_clicked), state);
	g_signal_connect (G_OBJECT (state->delete_btn),
		"clicked",
		G_CALLBACK (cb_merge_delete_clicked), state);
	g_signal_connect (G_OBJECT (state->merge_btn),
		"clicked",
		G_CALLBACK (cb_merge_merge_clicked), state);
	g_signal_connect (G_OBJECT (state->cancel_btn),
		"clicked",
		G_CALLBACK (cb_merge_cancel_clicked), state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_DATA_MERGE);

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       MERGE_KEY);

	/* a candidate for merging into attach guru */
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_merge_destroy);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
