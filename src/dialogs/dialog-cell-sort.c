/*
 * dialog-cell-sort.c:  Implements Cell Sort dialog boxes.
 *
 * Authors:
 *  JP Rosevear   <jpr@arcavia.com>
 *  Michael Meeks <michael@imaginator.com>
 *  Andreas J. Guelzow <aguelzow@taliesin.ca>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <workbook-view.h>
#include <gui-util.h>
#include <cell.h>
#include <expr.h>
#include <selection.h>
#include <parse-util.h>
#include <ranges.h>
#include <commands.h>
#include <workbook.h>
#include <sort.h>
#include <sheet.h>
#include <workbook-edit.h>
#include <widgets/gnumeric-expr-entry.h>
#include <value.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>
#include <ctype.h>
#include <stdio.h>

#define GLADE_FILE "cell-sort.glade"

#define CELL_SORT_KEY "cell-sort-dialog"

#define MAX_INITIAL_CLAUSES 32

typedef struct {
	Sheet     *sheet;
	WorkbookControlGUI  *wbcg;
	Workbook  *wb;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *warning_dialog;
	GtkWidget *cancel_button;
	GtkWidget *ok_button;
	GtkWidget *up_button;
	GtkWidget *down_button;
	GtkWidget *add_button;
	GtkWidget *delete_button;
	GnumericExprEntry *range_entry;
	GnumericExprEntry *add_entry;
	GtkListStore  *model;
	GtkTreeView   *treeview;
	GtkTreeSelection   *selection;
	GtkWidget *cell_sort_row_rb;
	GtkWidget *cell_sort_col_rb;
	GtkWidget *cell_sort_header_check;

	Value     *sel;
	gboolean   header;
	gboolean   is_cols;
	int        sort_items;

} SortFlowState;

enum {
	ITEM_IN_USE,
	ITEM_NAME,
	ITEM_DESCENDING,
	ITEM_CASE_SENSITIVE,
	ITEM_SORT_BY_VALUE,
	ITEM_MOVE_FORMAT,
	ITEM_NUMBER,
	NUM_COLMNS
};

static void
dialog_set_focus (GtkWidget *window, GtkWidget *focus_widget,
		  SortFlowState *state)
{
	if (focus_widget != NULL && IS_GNUMERIC_EXPR_ENTRY (focus_widget->parent)) {
		wbcg_set_entry (state->wbcg,
				GNUMERIC_EXPR_ENTRY (focus_widget->parent));
	} else {
		wbcg_set_entry (state->wbcg, NULL);
	}
}


static gchar *
col_row_name (Sheet *sheet, int col, int row, gboolean header, gboolean is_cols)
{
	Cell *cell;
	gchar *str = NULL;

	if (header) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell)
			str = cell_get_rendered_text (cell);
		else if (is_cols)
			str = g_strdup_printf (_("Column %s"), col_name (col));
		else
			str = g_strdup_printf (_("Row %s"), row_name (row));
	} else {
		if (is_cols)
			str = g_strdup_printf (_("Column %s"), col_name (col));
		else
			str = g_strdup_printf (_("Row %s"), row_name (row));
	}

	return str;
}

static gboolean
translate_range (Value *range, SortFlowState *state)
{
	gboolean old_header = state->header;
	gboolean old_is_cols = state->is_cols;

	state->header = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_header_check));
	state->is_cols = !gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_row_rb));

	if (state->sel == NULL) {
		state->sel = range;
		return TRUE;
	}

	if (old_header != state->header || old_is_cols != state->is_cols ||
		state->sel->v_range.cell.a.sheet != range->v_range.cell.a.sheet ||
		state->sel->v_range.cell.a.col != range->v_range.cell.a.col ||
		state->sel->v_range.cell.a.row != range->v_range.cell.a.row ||
		state->sel->v_range.cell.b.col != range->v_range.cell.b.col ||
		state->sel->v_range.cell.b.row != range->v_range.cell.b.row) {
		value_release (state->sel);
		state->sel = range;
		return TRUE;
	} else {
		value_release (state->sel);
		state->sel = range;
		return FALSE;
	}
}

static void
append_data (SortFlowState *state, int i, int index)
{
	gchar *str;
	GtkTreeIter iter;
	Sheet *sheet = state->sel->v_range.cell.a.sheet;

	str  = state->is_cols
		? col_row_name (sheet, i, index, state->header, TRUE)
		: col_row_name (sheet, index, i, state->header, FALSE);
	gtk_list_store_append (state->model, &iter);
	gtk_list_store_set (state->model, &iter,
			    ITEM_IN_USE, TRUE,
			    ITEM_NAME,  str,
			    ITEM_DESCENDING, FALSE,
			    ITEM_CASE_SENSITIVE, FALSE,
			    ITEM_SORT_BY_VALUE, TRUE,
			    ITEM_MOVE_FORMAT, TRUE,
			    ITEM_NUMBER, i,
			    -1);
	state->sort_items++;
	g_free (str);
}

static void
load_model_data (SortFlowState *state)
{
	int start;
	int end;
	int index;
	int i;

	if (state->is_cols) {
		start = state->sel->v_range.cell.a.col;
		end  = state->sel->v_range.cell.b.col;
		index = state->sel->v_range.cell.a.row;
	} else {
		start = state->sel->v_range.cell.a.row;
		end  = state->sel->v_range.cell.b.row;
		index = state->sel->v_range.cell.a.col;
	}

	gtk_list_store_clear (state->model);
	state->sort_items = 0;

	if (end >= start + MAX_INITIAL_CLAUSES)
		end = start + MAX_INITIAL_CLAUSES - 1;

	for (i = start; i <= end; i++)
		append_data (state, i, index);
}

static GlobalRange *
value_to_global_range ( Value *value)
{
	GlobalRange *gr;

	g_return_val_if_fail (value->type == VALUE_CELLRANGE, NULL);

	gr = g_new0 (GlobalRange, 1);
	gr->sheet = value->v_range.cell.a.sheet;
	gr->range.start.row = value->v_range.cell.a.row;
	gr->range.start.col = value->v_range.cell.a.col;
	gr->range.end.row = value->v_range.cell.b.row;
	gr->range.end.col = value->v_range.cell.b.col;
	
	return gr;
}

/**
 * cb_update_add_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
cb_update_add_sensitivity (GtkWidget *dummy, SortFlowState *state)
{
        Value *range_add;

        range_add = gnm_expr_entry_parse_as_value
		(GNUMERIC_EXPR_ENTRY (state->add_entry), state->sheet);

	if (state->sel == NULL || range_add == NULL) {
		gtk_widget_set_sensitive (state->add_button, FALSE);
	} else {
		GlobalRange *a = value_to_global_range (state->sel);
		GlobalRange *b = value_to_global_range (range_add);
		gtk_widget_set_sensitive (state->add_button,
					  global_range_overlap (a, b));
		global_range_free (a);
		global_range_free (b);
	}
	if (range_add != NULL)
		value_release (range_add);
}

/**
 * cb_update_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
cb_update_sensitivity (GtkWidget *dummy, SortFlowState *state)
{
        Value *range;
	int items;

        range = gnm_expr_entry_parse_as_value
		(GNUMERIC_EXPR_ENTRY (state->range_entry), state->sheet);
	if (range == NULL) {
		if (state->sel != NULL) {
			value_release (state->sel);
			state->sel = NULL;
			gtk_list_store_clear (state->model);
			state->sort_items = 0;
		}
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		gtk_widget_set_sensitive (state->add_button, FALSE);
	} else {
		if (translate_range (range, state))
			load_model_data (state);
		items = state->is_cols ? (state->sel->v_range.cell.b.row -
			state->sel->v_range.cell.a.row + 1) :
			(state->sel->v_range.cell.b.col -
			state->sel->v_range.cell.a.col + 1);
		if (state->header)
			items -= 1;
		gtk_widget_set_sensitive (state->ok_button,
					  (state->sort_items != 0) &&
					  (items > 1));
		cb_update_add_sensitivity (dummy, state);
	}
}

/**
 * dialog_destroy:
 * @window:
 * @focus_widget:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
dialog_destroy (GtkObject *w, SortFlowState  *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);

	return FALSE;
}

/**
 * cb_dialog_ok_clicked:
 * @button:
 * @state:
 *
 * Sort
 **/
static void
cb_dialog_ok_clicked (GtkWidget *button, SortFlowState *state)
{
	SortData *data;
	SortClause *array, *this_array_item;
	int item = 0;
	GtkTreeIter iter;
	gboolean in_use, descending, case_sensitive, sort_by_value, move_format;
	gint number;
	gint base;

	array = g_new (SortClause, state->sort_items);
	this_array_item = array;
	base = (state->is_cols ? state->sel->v_range.cell.a.col : state->sel->v_range.cell.a.row);

	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &iter, NULL, item)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
				    ITEM_IN_USE, &in_use,
				    ITEM_DESCENDING,&descending,
				    ITEM_CASE_SENSITIVE, &case_sensitive,
				    ITEM_SORT_BY_VALUE, &sort_by_value,
				    ITEM_MOVE_FORMAT, &move_format,
				    ITEM_NUMBER, &number,
				    -1);
		item++;
		if (in_use) {
			this_array_item->offset = number - base;
			this_array_item->asc = descending ? 1 : 0;
			this_array_item->cs = case_sensitive;
			this_array_item->val = sort_by_value;
			this_array_item++;
		}
	}


	data = g_new (SortData, 1);
	data->sheet = state->sel->v_range.cell.a.sheet;
	data->range = g_new (Range, 1);
	data->range = range_init (data->range, state->sel->v_range.cell.a.col
				  + ((state->header && !state->is_cols) ? 1 : 0),
				  state->sel->v_range.cell.a.row
				  + ((state->header && state->is_cols) ? 1 : 0),
				  state->sel->v_range.cell.b.col,
				  state->sel->v_range.cell.b.row);
	data->num_clause = state->sort_items;
	data->clauses = array;
	data->top = state->is_cols;

	cmd_sort (WORKBOOK_CONTROL (state->wbcg), data);

	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_dialog_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_cancel_clicked (GtkWidget *button, SortFlowState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
dialog_load_selection (SortFlowState *state)
{
	Range const *first;

	first = selection_first_range (state->sheet, NULL, NULL);

	if (first != NULL) {
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (state->cell_sort_col_rb),
			first->end.row - first->start.row > first->end.col - first->start.col);
		gnm_expr_entry_load_from_range (state->range_entry,
			state->sheet, first);
	} else
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (state->cell_sort_col_rb),
			TRUE);
}

static gint
location_of_iter (GtkTreeIter  *iter, GtkListStore *model)
{
	gint row, this_row;
	GtkTreeIter this_iter;
	gint n = 0;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter, ITEM_NUMBER, &row, -1);

	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (model),
					       &this_iter, NULL, n)) {
		gtk_tree_model_get (GTK_TREE_MODEL (model), &this_iter, ITEM_NUMBER,
				    &this_row, -1);
		if (this_row == row)
			return n;
		n++;
	}

	g_warning ("We should have never gotten to this point!\n");
	return -1;
}

/**
 * Refreshes the buttons on a row (un)selection
 *
 */
static void
cb_sort_selection_changed (GtkTreeSelection *ignored, SortFlowState *state)
{
	GtkTreeIter iter;
	GtkTreeIter this_iter;
	gint row;

	if (!gtk_tree_selection_get_selected (state->selection, NULL, &iter)) {
		gtk_widget_set_sensitive (state->up_button, FALSE);
		gtk_widget_set_sensitive (state->down_button, FALSE);
		gtk_widget_set_sensitive (state->delete_button, FALSE);
		return;
	}

	gtk_widget_set_sensitive (state->up_button,
				  location_of_iter (&iter, state->model) > 0);

	row = location_of_iter (&iter, state->model);
	gtk_widget_set_sensitive (state->down_button,
				  gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, row+1));
	gtk_widget_set_sensitive (state->delete_button, TRUE);
}

static void
toggled (GtkCellRendererToggle *cell,
	 gchar                 *path_string,
	 gpointer               data,
	 int                    column)
{
	SortFlowState *state = data;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gboolean value;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, column, &value, -1);

	value = !value;
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, column, value, -1);

	gtk_tree_path_free (path);
}

static void
move_cb (SortFlowState *state, gint direction)
{
	GtkTreeIter iter;
	gboolean in_use, descending, case_sensitive, sort_by_value, move_format;
	gint number, row;
	char* name;

	if (!gtk_tree_selection_get_selected (state->selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
			    ITEM_IN_USE, &in_use,
			    ITEM_NAME, &name,
			    ITEM_DESCENDING,&descending,
			    ITEM_CASE_SENSITIVE, &case_sensitive,
			    ITEM_SORT_BY_VALUE, &sort_by_value,
			    ITEM_MOVE_FORMAT, &move_format,
			    ITEM_NUMBER, &number,
			    -1);
	row = location_of_iter (&iter, state->model);
	if (row + direction < 0)
		return;
	gtk_list_store_remove (state->model, &iter);
	gtk_list_store_insert (state->model, &iter, row + direction);
	gtk_list_store_set (state->model, &iter,
			    ITEM_IN_USE, in_use,
			    ITEM_NAME, name,
			    ITEM_DESCENDING,descending,
			    ITEM_CASE_SENSITIVE, case_sensitive,
			    ITEM_SORT_BY_VALUE, sort_by_value,
			    ITEM_MOVE_FORMAT, move_format,
			    ITEM_NUMBER, number,
			    -1);
	gtk_tree_selection_select_iter (state->selection, &iter);
	g_free (name);
}

static void cb_up   (GtkWidget *w, SortFlowState *state) { move_cb (state, -1); }
static void cb_down (GtkWidget *w, SortFlowState *state) { move_cb (state,  1); }

static void cb_delete_clicked (GtkWidget *w, SortFlowState *state)
{
	GtkTreeIter iter;
	gboolean in_use;

	if (!gtk_tree_selection_get_selected (state->selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
			    ITEM_IN_USE, &in_use,
			    -1);
	if (in_use)
		state->sort_items -= 1;
	gtk_list_store_remove (state->model, &iter);
	if (state->sort_items == 0 || state->sort_items == 1)
		cb_update_sensitivity (NULL, state);
}

static void cb_add_clicked (GtkWidget *w, SortFlowState *state)
{
        Value *range_add;
	GlobalRange *grange_sort;
	GlobalRange *grange_add;
	Range intersection;
	int start;
	int end;
	int index;
	int i;

        range_add = gnm_expr_entry_parse_as_value
		(GNUMERIC_EXPR_ENTRY (state->add_entry), state->sheet);

	g_return_if_fail (range_add != NULL && state->sel != NULL);
	
	grange_sort = value_to_global_range (state->sel);
	grange_add = value_to_global_range (range_add);

	if (range_intersection (&intersection, &grange_sort->range, &grange_add->range)) {

		if (state->is_cols) {
			start = intersection.start.col;
			end  = intersection.end.col;
			index = intersection.start.row;
		} else {
			start = intersection.start.row;
			end  = intersection.end.row;
			index = intersection.start.col;
		}
		
		for (i = start; i <= end; i++) {
			
			int item = 0;
			GtkTreeIter iter;
			gboolean found = FALSE;
			gint number;

			while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
							       &iter, NULL, item)) {
				gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
						    ITEM_NUMBER, &number,
						    -1);
				item++;
				if (number == i) {
					found = TRUE;
					break;
				}
			}
			
			if (!found) {
				append_data (state, i, index);
				break;
			}
		}		
	}

	global_range_free (grange_sort);
	global_range_free (grange_add);
}

static void
cb_toggled_in_use (GtkCellRendererToggle *cell,
	 gchar                 *path_string,
	 gpointer               data)
{
	SortFlowState *state = data;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gboolean value;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, ITEM_IN_USE, &value, -1);

	state->sort_items += (value ? -1 : 1);
	if (state->sort_items == 0 || state->sort_items == 1)
		cb_update_sensitivity (NULL, state);

	value = !value;
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, ITEM_IN_USE, value, -1);

	gtk_tree_path_free (path);
}

static void
cb_toggled_descending (GtkCellRendererToggle *cell,
	 gchar                 *path_string,
	 gpointer               data)
{
	toggled (cell, path_string, data, ITEM_DESCENDING);
}

static void
cb_toggled_sort_by_value (GtkCellRendererToggle *cell,
	 gchar                 *path_string,
	 gpointer               data)
{
	toggled (cell, path_string, data, ITEM_SORT_BY_VALUE);
}

static void
cb_toggled_case_sensitive (GtkCellRendererToggle *cell,
	 gchar                 *path_string,
	 gpointer               data)
{
	toggled (cell, path_string, data, ITEM_CASE_SENSITIVE);
}

/**
 * dialog_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_init (SortFlowState *state)
{
	GtkTable *table;
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "cell_sort_table"));

/* setup range entry */
	state->range_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->range_entry,
                                      GNUM_EE_SINGLE_RANGE,
                                      GNUM_EE_MASK);
        gnm_expr_entry_set_scg (state->range_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->range_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->range_entry));
	gnumeric_expr_entry_set_update_policy (state->range_entry, GTK_UPDATE_DISCONTINUOUS);
	gtk_widget_show (GTK_WIDGET (state->range_entry));
	g_signal_connect (G_OBJECT (state->range_entry),
		"update",
		G_CALLBACK (cb_update_sensitivity), state);

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "cell_sort_buttons_table"));

/* setup add entry */
	state->add_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->add_entry,
                                      GNUM_EE_SINGLE_RANGE,
                                      GNUM_EE_MASK);
        gnm_expr_entry_set_scg (state->add_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->add_entry),
			  0, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->add_entry));
	gtk_widget_show (GTK_WIDGET (state->add_entry));
	g_signal_connect (G_OBJECT (state->add_entry),
		"changed",
		G_CALLBACK (cb_update_add_sensitivity), state);

/* Set-up tree view */
	scrolled = glade_xml_get_widget (state->gui, "scrolled_cell_sort_list");
	state->model = gtk_list_store_new (NUM_COLMNS, G_TYPE_BOOLEAN, G_TYPE_STRING,
					   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
					   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_INT);
	state->treeview = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_BROWSE);
	g_signal_connect (state->selection,
		"changed",
		G_CALLBACK (cb_sort_selection_changed), state);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_in_use), state);
	column = gtk_tree_view_column_new_with_attributes (_("Sort"),
							   renderer,
							   "active", ITEM_IN_USE, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	column = gtk_tree_view_column_new_with_attributes (_("Row/Column"),
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_descending), state);
	column = gtk_tree_view_column_new_with_attributes (_("Descend"),
							   renderer,
							   "active", ITEM_DESCENDING, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_case_sensitive), state);
 	column = gtk_tree_view_column_new_with_attributes (_("Case Sensitive"),
							   renderer,
							   "active", ITEM_CASE_SENSITIVE, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_sort_by_value), state);
	column = gtk_tree_view_column_new_with_attributes (_("By Value"),
							   renderer,
							   "active", ITEM_SORT_BY_VALUE, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	gtk_tree_view_set_reorderable (state->treeview,TRUE);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview));
	gtk_widget_show (GTK_WIDGET (state->treeview));

/* Set-up other widgets */
	state->cell_sort_row_rb = glade_xml_get_widget (state->gui, "cell_sort_row_rb");
	state->cell_sort_col_rb = glade_xml_get_widget (state->gui, "cell_sort_col_rb");
	g_signal_connect (GTK_OBJECT (state->cell_sort_row_rb),
		"toggled",
		G_CALLBACK (cb_update_sensitivity), state);

	state->cell_sort_header_check = glade_xml_get_widget (state->gui,
							      "cell_sort_header_check");
	g_signal_connect (G_OBJECT (state->cell_sort_header_check),
		"toggled",
		G_CALLBACK (cb_update_sensitivity), state);

/* Set-up buttons */
	state->up_button  = glade_xml_get_widget (state->gui, "up_button");
	g_signal_connect (GTK_OBJECT (state->up_button),
		"clicked",
		G_CALLBACK (cb_up), state);
	state->down_button  = glade_xml_get_widget (state->gui, "down_button");
	g_signal_connect (GTK_OBJECT (state->down_button),
		"clicked",
		G_CALLBACK (cb_down), state);
	state->add_button  = glade_xml_get_widget (state->gui, "add_button");
	g_signal_connect (GTK_OBJECT (state->add_button),
		"clicked",
		G_CALLBACK (cb_add_clicked), state);
	gtk_widget_set_sensitive (state->add_button, FALSE);
	state->delete_button  = glade_xml_get_widget (state->gui, "delete_button");
	g_signal_connect (GTK_OBJECT (state->delete_button),
		"clicked",
		G_CALLBACK (cb_delete_clicked), state);
	gtk_widget_set_sensitive (state->delete_button, FALSE);

	gtk_button_stock_alignment_set (GTK_BUTTON (state->up_button),
		0., .5, 0., 0.);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->down_button),
		0., .5, 0., 0.);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->add_button),
		0., .5, 0., 0.);
	gtk_button_stock_alignment_set (GTK_BUTTON (state->delete_button),
		0., .5, 0., 0.);
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"cell-sort.html");

	state->ok_button  = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_ok_clicked), state);
	state->cancel_button  = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_cancel_clicked), state);

/* Finish dialog signals */
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	g_signal_connect (G_OBJECT (state->dialog),
		"set-focus",
		G_CALLBACK (dialog_set_focus), state);
	g_signal_connect (GTK_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (dialog_destroy), state);
	cb_sort_selection_changed (NULL, state);
	dialog_load_selection (state);
	cb_update_sensitivity (NULL, state);

	return FALSE;
}

/*
 * Main entry point for the Cell Sort dialog box
 */
void
dialog_cell_sort (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	SortFlowState* state;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (gnumeric_dialog_raise_if_exists (wbcg, CELL_SORT_KEY))
		return;

	state = g_new (SortFlowState, 1);
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->warning_dialog = NULL;
	state->sel = NULL;
	state->sort_items = 0;

	/* Get the dialog and check for errors */
	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (state->gui == NULL) {
		g_free (state);
                return;
	}

        state->dialog = glade_xml_get_widget (state->gui, "CellSort");

	if (dialog_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Cell-Sort dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       CELL_SORT_KEY);

	gtk_widget_show (state->dialog);
}
