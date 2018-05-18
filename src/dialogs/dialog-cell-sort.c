/*
 * dialog-cell-sort.c:  Implements Cell Sort dialog boxes.
 *
 * Authors:
 *  JP Rosevear   <jpr@arcavia.com>
 *  Michael Meeks <michael@ximian.com>
 *  Andreas J. Guelzow <aguelzow@taliesin.ca>
 *  Morten Welinder <terra@gnome.org>
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
#include <sheet-view.h>
#include <wbc-gtk.h>
#include <gnumeric-conf.h>
#include <widgets/gnm-cell-renderer-toggle.h>
#include <widgets/gnm-expr-entry.h>
#include <value.h>

#include <gsf/gsf-impl-utils.h>
#include <gdk/gdkkeysyms.h>
#include <goffice/goffice.h>

#define CELL_SORT_KEY "cell-sort-dialog"



typedef struct {
	WBCGtk  *wbcg;
	Workbook  *wb;
	SheetView *sv;
	Sheet     *sheet;

	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkWidget *warning_dialog;
	GtkWidget *cancel_button;
	GtkWidget *ok_button;
	GtkWidget *up_button;
	GtkWidget *down_button;
	GtkWidget *add_button;
	GtkWidget *delete_button;
	GtkWidget *clear_button;
	GnmExprEntry *range_entry;
	GnmExprEntry *add_entry;
	GtkListStore  *model;
	GtkTreeView   *treeview;
	GtkTreeViewColumn *header_column;
	GtkTreeSelection   *selection;
	GtkWidget *cell_sort_row_rb;
	GtkWidget *cell_sort_col_rb;
	GtkWidget *cell_sort_header_check;
	GtkWidget *retain_format_check;
	GdkPixbuf *image_ascending;
	GdkPixbuf *image_descending;
	GOLocaleSel *locale_selector;

	GnmValue  *sel;
	gboolean   header;
	gboolean   is_cols;
	int        sort_items;

} SortFlowState;

enum {
	ITEM_HEADER,
	ITEM_NAME,
	ITEM_DESCENDING,
	ITEM_DESCENDING_IMAGE,
	ITEM_CASE_SENSITIVE,
	ITEM_SORT_BY_VALUE,
	ITEM_MOVE_FORMAT,
	ITEM_NUMBER,
	NUM_COLUMNS
};

static const gint MAX_MENU_SIZE = 20;
typedef struct {
	gint index;
	gint start;
	gint end;
	gboolean done_submenu;
	SortFlowState *state;
} AddSortFieldMenuState;

static gchar *
header_name (Sheet *sheet, int col, int row)
{
	GnmCell *cell;
	gchar *str = NULL;

	cell = sheet_cell_get (sheet, col, row);
	if (cell)
		str = value_get_as_string (cell->value);

	return str;
}


static gchar *
col_row_name (Sheet *sheet, int col, int row, gboolean header, gboolean is_cols)
{
	GnmCell *cell;
	gchar *str = NULL;

	if (is_cols)
		str = g_strdup_printf (_("Column %s"), col_name (col));
	else
		str = g_strdup_printf (_("Row %s"), row_name (row));

	if (header) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell && !gnm_cell_is_blank (cell)) {
			gchar *header_str, *generic_str = str;
			header_str = value_get_as_string (cell->value);
			str = g_strdup_printf (_("%s (%s)"), header_str, generic_str);
			g_free (header_str);
			g_free (generic_str);
		}
	}

	return str;
}


static gboolean
already_in_sort_fields(int index, SortFlowState *state)
{
	GtkTreeIter iter;
	int item = 0;
	gint number;

	/* See if index is already in the sort fields */
	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &iter, NULL, item)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
				    ITEM_NUMBER, &number,
				    -1);
		item++;

		if (number == index) {
			return TRUE;
		}
	}

	/* Here means not already in sort fields */
	return FALSE;
}

static gboolean
range_already_in_sort_criteria(gint start, gint end, SortFlowState *state)
{
	gint i;
	for (i=start; i<=end; i++) {
		if (!already_in_sort_fields(i, state))
			return FALSE;
	}
	return TRUE;
}


static void
build_sort_field_menu (gint start, gint end, gint index, GtkWidget *menu, SortFlowState *state, int used);

static void
cb_sort_field_menu_activate(GtkWidget *item, AddSortFieldMenuState *menu_state)
{
	GtkWidget *menu = GTK_WIDGET (gtk_menu_item_get_submenu(GTK_MENU_ITEM (item)));

	if (menu_state->done_submenu == FALSE) {
		build_sort_field_menu(menu_state->start,
				      menu_state->end,
				      menu_state->index,
				      menu,
				      menu_state->state, 0);
		menu_state->done_submenu = TRUE;
	}
}

static void
set_button_sensitivity(SortFlowState *state)
{
	int items;

	if (state->sel == NULL) {
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	}

	items = state->is_cols ? (state->sel->v_range.cell.b.row -
				  state->sel->v_range.cell.a.row + 1) :
		(state->sel->v_range.cell.b.col -
		 state->sel->v_range.cell.a.col + 1);
	if (state->header)
		items -= 1;
	gtk_widget_set_sensitive (state->ok_button,
				  (state->sort_items != 0) &&
				  (items > 1));
	gtk_widget_set_sensitive (state->clear_button, state->sort_items != 0);
}

static void
append_data (SortFlowState *state, int i, int index)
{
	gchar *str, *header;
	GtkTreeIter iter;
	Sheet *sheet = state->sel->v_range.cell.a.sheet;
	gboolean sort_asc = gnm_conf_get_core_sort_default_ascending ();

	header = state->is_cols
		? header_name (sheet, i, index)
		: header_name (sheet, index, i);
	str = state->is_cols
		? col_row_name (sheet, i, index, FALSE, TRUE)
		: col_row_name (sheet, index, i, FALSE, FALSE);
	gtk_list_store_append (state->model, &iter);
	gtk_list_store_set (state->model, &iter,
			    ITEM_HEADER,  header,
			    ITEM_NAME,  str,
			    ITEM_DESCENDING, !sort_asc,
			    ITEM_DESCENDING_IMAGE, sort_asc ? state->image_ascending
			    : state->image_descending,
			    ITEM_CASE_SENSITIVE, gnm_conf_get_core_sort_default_by_case (),
			    ITEM_SORT_BY_VALUE, TRUE,
			    ITEM_MOVE_FORMAT, TRUE,
			    ITEM_NUMBER, i,
			    -1);
	state->sort_items++;
	g_free (str);
	g_free (header);
}

static void
cb_sort_field_selection(G_GNUC_UNUSED GtkWidget *item, AddSortFieldMenuState *menu_state)
{
	append_data(menu_state->state,
		    menu_state->start,
		    menu_state->index);
	/* Update sensitivity if this is the first sort item. */
	if (menu_state->state->sort_items == 1)
		set_button_sensitivity(menu_state->state);
}

static void
build_sort_field_menu (gint start, gint end, gint index, GtkWidget *menu, SortFlowState *state, int used)
{
	Sheet *sheet = state->sel->v_range.cell.a.sheet;
	GtkWidget *item;
	GtkWidget *submenu;
	int i;
	int this_end;
	char *str;
	char *str_start;
	char *str_end;
	AddSortFieldMenuState *menu_state;
	gint menu_size;

	menu_size = 1 + end - start;
	if (MAX_MENU_SIZE < menu_size - used) {
		gint submenu_size;
		gint balanced_submenu_size;

		submenu_size = (menu_size + MAX_MENU_SIZE - 1) / MAX_MENU_SIZE;
		balanced_submenu_size = sqrt((double)
					     (menu_size + MAX_MENU_SIZE - 1));
		if (balanced_submenu_size > submenu_size)
			submenu_size = balanced_submenu_size;

		for (i = start; i <= end; i+=submenu_size) {
			this_end = i + submenu_size - 1;
			if (this_end > end)
				this_end = end;

			/* See if there are any fields in this range that aren't already
			   in the sort.
			*/
			if (range_already_in_sort_criteria(i, this_end, state))
				continue;

			str_start = state->is_cols
				? col_row_name (sheet, i, index, state->header, TRUE)
				: col_row_name (sheet, index, i, state->header, FALSE);

			str_end = state->is_cols
				? col_row_name (sheet, this_end, index, state->header, TRUE)
				: col_row_name (sheet, index, this_end, state->header, FALSE);

			str = g_strdup_printf(_("%s to %s"), str_start, str_end);
			g_free(str_start);
			g_free(str_end);

			item = (GtkWidget *) gtk_menu_item_new_with_label(str);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);

			menu_state = g_new(AddSortFieldMenuState, 1);
			menu_state->start = i;
			menu_state->end = this_end;
			menu_state->index = index;
			menu_state->state = state;
			menu_state->done_submenu = FALSE;
			submenu = gtk_menu_new();
			gtk_menu_item_set_submenu(GTK_MENU_ITEM (item), submenu);
			g_signal_connect (item, "activate",
					  G_CALLBACK (cb_sort_field_menu_activate), menu_state);
		}
	}  else {
		for (i = start; i <= end; i++) {
			if (FALSE == already_in_sort_fields(i, state)) {

				str = state->is_cols
					? col_row_name (sheet, i, index, state->header, TRUE)
					: col_row_name (sheet, index, i, state->header, FALSE);
				item = (GtkWidget *) gtk_menu_item_new_with_label(str);
				gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
				gtk_widget_show (item);
				menu_state = g_new(AddSortFieldMenuState, 1);
				menu_state->start = i;
				menu_state->end = i;
				menu_state->index = index;
				menu_state->state = state;
				menu_state->done_submenu = FALSE;
				g_signal_connect (item, "activate",
						  G_CALLBACK (cb_sort_field_selection),
						  menu_state);
			}
		}
	}
}

static void
load_model_data (SortFlowState *state)
{
	int start;
	int end;
	int index;
	int i;
	int limit = gnm_conf_get_core_sort_dialog_max_initial_clauses ();

	if (state->is_cols) {
		start = state->sel->v_range.cell.a.col;
		end = state->sel->v_range.cell.b.col;
		index = state->sel->v_range.cell.a.row;
	} else {
		start = state->sel->v_range.cell.a.row;
		end = state->sel->v_range.cell.b.row;
		index = state->sel->v_range.cell.a.col;
	}

	gtk_list_store_clear (state->model);
	state->sort_items = 0;

	if (end >= start + limit)
		end = start + limit - 1;

	for (i = start; i <= end; i++)
		append_data (state, i, index);
}

static void
translate_range (GnmValue *range, SortFlowState *state)
{
	state->is_cols = !gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_row_rb));
	state->header = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_header_check));

	value_release (state->sel);
	state->sel = range;
	load_model_data(state);
}

static void
cb_sort_header_check(SortFlowState *state)
{
	state->header = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_header_check));

	gtk_tree_view_column_set_visible (state->header_column, state->header);
	set_button_sensitivity (state);
}

static void
cb_update_to_new_range (SortFlowState *state)
{
        GnmValue *range;

        range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->range_entry), state->sheet);
	if (range == NULL) {
		if (state->sel != NULL) {
			value_release (state->sel);
			state->sel = NULL;
			gtk_list_store_clear (state->model);
			state->sort_items = 0;
		}
	} else
		translate_range (range, state);
	set_button_sensitivity (state);
}

static void
cb_dialog_destroy (SortFlowState  *state)
{
	value_release (state->sel);
	state->sel = NULL;

	g_clear_object (&state->model);
	g_clear_object (&state->gui);

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;

	g_clear_object (&state->image_ascending);
	g_clear_object (&state->image_descending);

	g_free (state);
}

static void
cb_dialog_ok_clicked (SortFlowState *state)
{
	GnmSortData *data, *data_copy;
	GnmSortClause *array, *this_array_item;
	int item = 0;
	GtkTreeIter iter;
	gboolean descending, case_sensitive, sort_by_value, move_format;
	gint number;
	gint base;
	char const *text;

	array = g_new (GnmSortClause, state->sort_items);
	this_array_item = array;
	base = (state->is_cols ? state->sel->v_range.cell.a.col : state->sel->v_range.cell.a.row);

	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &iter, NULL, item)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
				    ITEM_DESCENDING,&descending,
				    ITEM_CASE_SENSITIVE, &case_sensitive,
				    ITEM_SORT_BY_VALUE, &sort_by_value,
				    ITEM_MOVE_FORMAT, &move_format,
				    ITEM_NUMBER, &number,
				    -1);
		item++;
		this_array_item->offset = number - base;
		this_array_item->asc = !!descending;
		this_array_item->cs = case_sensitive;
		this_array_item->val = sort_by_value;
		this_array_item++;
	}


	data = g_new (GnmSortData, 1);
	data->sheet = state->sel->v_range.cell.a.sheet;
	data->range = g_new (GnmRange, 1);
	data->range = range_init (data->range, state->sel->v_range.cell.a.col
				  + ((state->header && !state->is_cols) ? 1 : 0),
				  state->sel->v_range.cell.a.row
				  + ((state->header && state->is_cols) ? 1 : 0),
				  state->sel->v_range.cell.b.col,
				  state->sel->v_range.cell.b.row);
	data->num_clause = state->sort_items;
	data->clauses = array;
	data->top = state->is_cols;
	data->retain_formats = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->retain_format_check));
	data->locale = go_locale_sel_get_locale (state->locale_selector);

	data_copy = gnm_sort_data_copy (data);
	text = gnm_expr_entry_get_text (state->range_entry);
	gnm_sheet_add_sort_setup
		(data->sheet,
		 g_strdup((text != NULL && text[0] != '\0') ? text : "Other"),
		 data_copy);

	cmd_sort (GNM_WBC (state->wbcg), data);

	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_dialog_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			  SortFlowState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
dialog_cell_sort_load_sort_setup (SortFlowState *state, GnmSortData const *data)
{
	int i;
	GnmSortClause *this = data->clauses;
	gint base, max, index;
	Sheet *sheet = state->sel->v_range.cell.a.sheet;

	if (sheet == NULL)
		sheet = state->sheet;

	go_locale_sel_set_locale (state->locale_selector, data->locale);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (state->retain_format_check), data->retain_formats);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_row_rb), !data->top);
	state->is_cols = data->top;

	index = (data->top ? state->sel->v_range.cell.a.row : state->sel->v_range.cell.a.col);
	base = (data->top ? state->sel->v_range.cell.a.col : state->sel->v_range.cell.a.row);
	max = (data->top ? state->sel->v_range.cell.b.col : state->sel->v_range.cell.b.row);
	gtk_list_store_clear (state->model);
	state->sort_items = 0;
	for (i = 0; i < data->num_clause; i++) {
		if (data->clauses[i].offset <= max ) {
			GtkTreeIter iter;
			gchar *str, *header;
			int id = data->clauses[i].offset + base;

			header = state->is_cols
				? header_name (sheet, id, index)
				: header_name (sheet, index, id);
			str = col_row_name (sheet, id, id, FALSE, state->is_cols);

			gtk_list_store_append (state->model, &iter);
			gtk_list_store_set (state->model, &iter,
					    ITEM_HEADER,  header,
					    ITEM_NAME,  str,
					    ITEM_DESCENDING, data->clauses[i].asc,
					    ITEM_DESCENDING_IMAGE,
					    !data->clauses[i].asc
					    ? state->image_ascending
					    : state->image_descending,
					    ITEM_CASE_SENSITIVE, data->clauses[i].cs,
					    ITEM_SORT_BY_VALUE, data->clauses[i].val,
					    ITEM_MOVE_FORMAT, TRUE,
					    ITEM_NUMBER, id,
					    -1);
			state->sort_items++;
		}
		this++;
	}
	set_button_sensitivity (state);
}

static GnmRange const *
dialog_load_selection (SortFlowState *state, gboolean *col_rb)
{
	GnmRange const *first;
	GnmSortData const *data;

	first = selection_first_range (state->sv, NULL, NULL);

	if (first != NULL) {
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (state->cell_sort_col_rb),
			(*col_rb = (first->end.row - first->start.row > first->end.col - first->start.col)));
		gnm_expr_entry_load_from_range (state->range_entry,
						state->sheet, first);
	} else
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (state->cell_sort_col_rb),
			(*col_rb = TRUE));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->cell_sort_header_check),
				      sheet_range_has_heading
				      (state->sheet, first, *col_rb, FALSE));
	cb_sort_header_check (state);

	data = gnm_sheet_find_sort_setup (state->sheet,
					  gnm_expr_entry_get_text (state->range_entry));
	if (data != NULL)
		dialog_cell_sort_load_sort_setup (state, data);
	else
		cb_update_to_new_range (state);

	return first;
}

/**
 * cb_sort_selection_changed:
 *
 * Refreshes the buttons on a row (un)selection
 */
static void
cb_sort_selection_changed (SortFlowState *state)
{
	GtkTreeIter iter, test;

	if (!gtk_tree_selection_get_selected (state->selection, NULL, &iter)) {
		gtk_widget_set_sensitive (state->up_button, FALSE);
		gtk_widget_set_sensitive (state->down_button, FALSE);
		gtk_widget_set_sensitive (state->delete_button, FALSE);
		return;
	}

	test = iter;
	gtk_widget_set_sensitive
		(state->up_button,
		 gtk_tree_model_iter_previous (GTK_TREE_MODEL (state->model),
					   &test));

	test = iter;
	gtk_widget_set_sensitive
		(state->down_button,
		 gtk_tree_model_iter_next (GTK_TREE_MODEL (state->model),
					   &test));

	gtk_widget_set_sensitive (state->delete_button, TRUE);
	set_button_sensitivity (state);
}

static void
toggled (SortFlowState *state, const gchar *path_string, int column)
{
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gboolean value;

	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_model_get (model, &iter, column, &value, -1);
		value = !value;
		gtk_list_store_set (GTK_LIST_STORE (model),
				    &iter, column, value, -1);
	} else {
		g_warning ("Did not get a valid iterator");
	}

	gtk_tree_path_free (path);
}

static void
move_cb (SortFlowState *state,
	 gboolean (*mover) (GtkTreeModel *, GtkTreeIter *))
{
	GtkTreeIter iter, this_iter;

	if (!gtk_tree_selection_get_selected (state->selection, NULL,
					      &this_iter))
		return;

	iter = this_iter;
	if (!mover (GTK_TREE_MODEL(state->model), &iter))
		return;

	gtk_list_store_swap (state->model, &this_iter, &iter);
	cb_sort_selection_changed (state);
}

static void
cb_up (SortFlowState *state)
{
	move_cb (state, gtk_tree_model_iter_previous);
}

static void
cb_down (SortFlowState *state)
{
	move_cb (state, gtk_tree_model_iter_next);
}

static void
cb_delete_clicked (G_GNUC_UNUSED GtkWidget *w, SortFlowState *state)
{
	GtkTreeIter iter, iter2;
	gboolean ok;

	if (!gtk_tree_selection_get_selected (state->selection, NULL, &iter))
		return;

	iter2 = iter;
	ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (state->model), &iter2);
	if (!ok) {
		iter2 = iter;
		ok = gtk_tree_model_iter_previous (GTK_TREE_MODEL (state->model), &iter2);
	}

	if (ok)
		gtk_tree_selection_select_iter (state->selection, &iter2);

	gtk_list_store_remove (state->model, &iter);
	state->sort_items--;
	set_button_sensitivity (state);
}


static void
cb_clear_clicked (SortFlowState *state)
{
	state->sort_items = 0;
	gtk_list_store_clear (state->model);
	set_button_sensitivity (state);
}

static GtkMenu *
build_sort_field_base_menu (SortFlowState *state)
{
	gint start;
	gint end;
	gint index;

	GtkWidget *menu = gtk_menu_new ();
	GList* items = NULL;

	if (state->sel != NULL) {
		if (state->is_cols) {
			start = state->sel->v_range.cell.a.col;
			end = state->sel->v_range.cell.b.col;
			index = state->sel->v_range.cell.a.row;
		} else {
			start = state->sel->v_range.cell.a.row;
			end = state->sel->v_range.cell.b.row;
			index = state->sel->v_range.cell.a.col;
		}

		build_sort_field_menu (start, end, index, menu, state,
				       state->sort_items);

		items = gtk_container_get_children (GTK_CONTAINER (menu));
	}

	if (items == NULL) {
		GtkWidget *item;
		item = (GtkWidget *) gtk_menu_item_new_with_label(state->is_cols ?
								  _("no available column"): _("no available row"));
		gtk_widget_set_sensitive( GTK_WIDGET (item), FALSE);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	g_list_free (items);

	return GTK_MENU (menu);
}

static void
show_add_menu (SortFlowState *state)
{
	gnumeric_popup_menu (build_sort_field_base_menu(state),
			     NULL);
}

static void
cb_add_clicked (SortFlowState *state)
{
        GnmValue *range_add;
	GnmSheetRange grange_sort, grange_add;
	GnmRange intersection;
	int start;
	int end;
	int index;
	int i;
	gboolean had_items = (state->sort_items > 0);

        range_add = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->add_entry), state->sheet);

	if (range_add == NULL) {
		show_add_menu (state);
		return;
	}

	g_return_if_fail (range_add != NULL && state->sel != NULL);

	gnm_sheet_range_from_value (&grange_sort, state->sel);
	gnm_sheet_range_from_value (&grange_add, range_add);

	value_release (range_add);

	if (range_intersection (&intersection, &grange_sort.range, &grange_add.range)) {

		if (state->is_cols) {
			start = intersection.start.col;
			end = intersection.end.col;
			index = state->sel->v_range.cell.a.row;
		} else {
			start = intersection.start.row;
			end = intersection.end.row;
			index = state->sel->v_range.cell.a.col;
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
			}
		}
		if (!had_items && (state->sort_items > 0))
			set_button_sensitivity(state);
	} else
		show_add_menu (state);
	gnm_expr_entry_load_from_text (GNM_EXPR_ENTRY (state->add_entry), "");
}

static gint
cb_treeview_button_press(G_GNUC_UNUSED GtkWidget *w, GdkEvent *event, SortFlowState *state)
{
	if ((event->type == GDK_BUTTON_PRESS) &&
	    (event->button.button == 3)) {
		gnumeric_popup_menu (build_sort_field_base_menu(state),
				     event);
		return TRUE;
	}

	return FALSE;
}

static gint
cb_treeview_keypress (G_GNUC_UNUSED GtkWidget *w, GdkEventKey *event,
		      SortFlowState *state)
{
	gboolean ctrl = (event->state & GDK_CONTROL_MASK);
	GtkTreeIter iter;

	switch (event->keyval) {
	case GDK_KEY_Delete:
	case GDK_KEY_KP_Delete:
		cb_delete_clicked (w, state);
		return TRUE;
	case GDK_KEY_KP_Up:
	case GDK_KEY_Up:
		if (ctrl) {
			cb_up (state);
			return TRUE;
		}

		if (gtk_tree_selection_get_selected (state->selection, NULL, &iter) &&
		    gtk_tree_model_iter_previous (GTK_TREE_MODEL (state->model), &iter))
			gtk_tree_selection_select_iter (state->selection,
							&iter);
		return TRUE;

	case GDK_KEY_KP_Down:
	case GDK_KEY_Down:
		if (ctrl) {
			cb_down (state);
			return TRUE;
		}

		if (gtk_tree_selection_get_selected (state->selection, NULL, &iter) &&
		    gtk_tree_model_iter_next (GTK_TREE_MODEL (state->model), &iter))
			gtk_tree_selection_select_iter (state->selection,
							&iter);
		return TRUE;
	}
	return FALSE;
}

static void
cb_toggled_descending (G_GNUC_UNUSED GtkCellRendererToggle *cell,
		       const gchar *path_string,
		       SortFlowState *state)
{
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gboolean value;

	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_model_get (model, &iter, ITEM_DESCENDING, &value, -1);
		if (value) {
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    ITEM_DESCENDING, FALSE,
					    ITEM_DESCENDING_IMAGE, state->image_ascending,
					    -1);
		} else {
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    ITEM_DESCENDING, TRUE,
					    ITEM_DESCENDING_IMAGE, state->image_descending,
					    -1);
		}
	} else {
		g_warning ("Did not get a valid iterator");
	}
	gtk_tree_path_free (path);
}

#if 0
/* We are currently not supporting `by-value' vs not. */
static void
cb_toggled_sort_by_value (G_GNUC_UNUSED GtkCellRendererToggle *cell,
			  const gchar *path_string,
			  SortFlowState *state)
{
	toggled (state, path_string, ITEM_SORT_BY_VALUE);
}
#endif

static void
cb_toggled_case_sensitive (G_GNUC_UNUSED GtkCellRendererToggle *cell,
			   const gchar           *path_string,
			   SortFlowState *state)
{
	toggled (state, path_string, ITEM_CASE_SENSITIVE);
}


static void
dialog_init (SortFlowState *state)
{
	GtkGrid *grid;
	GtkWidget *scrolled;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	gboolean col_rb;

	grid = GTK_GRID (go_gtk_builder_get_widget (state->gui, "cell-sort-grid"));
	/* setup range entry */
	state->range_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->range_entry,
				  GNM_EE_SINGLE_RANGE,
				  GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->range_entry), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->range_entry),
			  1, 1, 2, 1);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->range_entry));
	gnm_expr_entry_set_update_policy (state->range_entry, GNM_UPDATE_DISCONTINUOUS);
	gtk_widget_show (GTK_WIDGET (state->range_entry));
	g_signal_connect_swapped (G_OBJECT (state->range_entry),
				  "changed",
				  G_CALLBACK (cb_update_to_new_range), state);

	state->locale_selector = GO_LOCALE_SEL (go_locale_sel_new ());
	gtk_widget_set_hexpand (GTK_WIDGET (state->locale_selector), TRUE);
	gtk_widget_show_all (GTK_WIDGET (state->locale_selector));
	gtk_grid_attach (grid, GTK_WIDGET (state->locale_selector),
			  1, 5, 2, 1);

	grid = GTK_GRID (go_gtk_builder_get_widget (state->gui, "cell-sort-spec-grid"));
	/* setup add entry */
	state->add_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->add_entry,
				  GNM_EE_SINGLE_RANGE,
				  GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (state->add_entry), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (state->add_entry),
			  0, 5, 1, 1);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->add_entry));
	gtk_widget_show (GTK_WIDGET (state->add_entry));

	/* Set-up tree view */
	scrolled = go_gtk_builder_get_widget (state->gui, "scrolled_cell_sort_list");
	state->model = gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_BOOLEAN,
					   GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN,
					   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
					   G_TYPE_INT);
	state->treeview = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_BROWSE);
	g_signal_connect_swapped (state->selection,
				  "changed",
				  G_CALLBACK (cb_sort_selection_changed), state);

	state->header_column = gtk_tree_view_column_new_with_attributes
		(_("Header"),
		 gtk_cell_renderer_text_new (),
		 "text", ITEM_HEADER, NULL);
	gtk_tree_view_append_column (state->treeview, state->header_column);

	column = gtk_tree_view_column_new_with_attributes
		(_("Row/Column"),
		 gtk_cell_renderer_text_new (),
		 "text", ITEM_NAME, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gnm_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
			  "toggled",
			  G_CALLBACK (cb_toggled_descending), state);
	column = gtk_tree_view_column_new_with_attributes ("",
							   renderer,
							   "active", ITEM_DESCENDING,
							   "pixbuf", ITEM_DESCENDING_IMAGE,
							   NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
			  "toggled",
			  G_CALLBACK (cb_toggled_case_sensitive), state);
	column = gtk_tree_view_column_new_with_attributes (_("Case Sensitive"),
							   renderer,
							   "active", ITEM_CASE_SENSITIVE, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	gtk_tree_view_columns_autosize (state->treeview);


	g_signal_connect (G_OBJECT (state->treeview),
			  "key_press_event",
			  G_CALLBACK (cb_treeview_keypress), state);
	g_signal_connect (G_OBJECT (state->treeview),
			  "button_press_event",
			  G_CALLBACK (cb_treeview_button_press), state);
#if 0
	/* We are currently not supporting `by-value' vs not. */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
			  "toggled",
			  G_CALLBACK (cb_toggled_sort_by_value), state);
	column = gtk_tree_view_column_new_with_attributes (_("By Value"),
							   renderer,
							   "active", ITEM_SORT_BY_VALUE, NULL);
	gtk_tree_view_append_column (state->treeview, column);
#endif

	gtk_tree_view_set_reorderable (state->treeview,TRUE);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview));
	gtk_widget_show (GTK_WIDGET (state->treeview));

	/* Set-up other widgets */
	state->cell_sort_row_rb = go_gtk_builder_get_widget (state->gui, "cell_sort_row_rb");
	state->cell_sort_col_rb = go_gtk_builder_get_widget (state->gui, "cell_sort_col_rb");
	g_signal_connect_swapped (G_OBJECT (state->cell_sort_row_rb),
				  "toggled",
				  G_CALLBACK (cb_update_to_new_range), state);

	state->cell_sort_header_check = go_gtk_builder_get_widget (state->gui,
							      "cell_sort_header_check");
	g_signal_connect_swapped (G_OBJECT (state->cell_sort_header_check),
				  "toggled",
				  G_CALLBACK (cb_sort_header_check), state);

	state->retain_format_check = go_gtk_builder_get_widget (state->gui,
							   "retain_format_button");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->retain_format_check),
				      gnm_conf_get_core_sort_default_retain_formats ());


	/* Set-up buttons */
	state->up_button = go_gtk_builder_get_widget (state->gui, "up_button");
	g_signal_connect_swapped (G_OBJECT (state->up_button),
				  "clicked",
				  G_CALLBACK (cb_up), state);
	state->down_button = go_gtk_builder_get_widget (state->gui, "down_button");
	g_signal_connect_swapped (G_OBJECT (state->down_button),
				  "clicked",
				  G_CALLBACK (cb_down), state);
	state->add_button = go_gtk_builder_get_widget (state->gui, "add_button");
	g_signal_connect_swapped (G_OBJECT (state->add_button),
				  "clicked",
				  G_CALLBACK (cb_add_clicked), state);
	gtk_widget_set_sensitive (state->add_button, TRUE);
	state->delete_button = go_gtk_builder_get_widget (state->gui, "delete_button");
	g_signal_connect (G_OBJECT (state->delete_button),
			  "clicked",
			  G_CALLBACK (cb_delete_clicked), state);
	gtk_widget_set_sensitive (state->delete_button, FALSE);

	state->clear_button = go_gtk_builder_get_widget (state->gui, "clear_button");
	g_signal_connect_swapped (G_OBJECT (state->clear_button),
				  "clicked",
				  G_CALLBACK (cb_clear_clicked), state);
	gtk_widget_set_sensitive (state->clear_button, FALSE);

	gtk_button_set_alignment (GTK_BUTTON (state->up_button), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->down_button), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->add_button), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->delete_button), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->clear_button), 0., .5);
	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_CELL_SORT);

	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect_swapped (G_OBJECT (state->ok_button),
				  "clicked",
				  G_CALLBACK (cb_dialog_ok_clicked), state);
	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_cancel_clicked), state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	/* Finish dialog signals */
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state, (GDestroyNotify) cb_dialog_destroy);

	dialog_load_selection (state, &col_rb);

	cb_sort_selection_changed (state);
	gnm_expr_entry_grab_focus(GNM_EXPR_ENTRY (state->add_entry), TRUE);
}

/*
 * Main entry point for the Cell Sort dialog box
 */
void
dialog_cell_sort (WBCGtk *wbcg)
{
	SortFlowState *state;
	GtkBuilder *gui;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, CELL_SORT_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/cell-sort.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
            return;

	state = g_new (SortFlowState, 1);
	state->wbcg  = wbcg;
	state->wb    = wb_control_get_workbook (GNM_WBC (wbcg));
	state->sv    = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	state->sheet = sv_sheet (state->sv);
	state->warning_dialog = NULL;
	state->sel = NULL;
	state->sort_items = 0;
	state->gui = gui;
        state->dialog = go_gtk_builder_get_widget (state->gui, "CellSort");

	state->image_ascending =
		go_gtk_widget_render_icon_pixbuf (state->dialog,
						  "view-sort-ascending",
						  GTK_ICON_SIZE_LARGE_TOOLBAR);
	state->image_descending =
		go_gtk_widget_render_icon_pixbuf (state->dialog,
						  "view-sort-descending",
						  GTK_ICON_SIZE_LARGE_TOOLBAR);
	dialog_init (state);

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       CELL_SORT_KEY);

	gtk_widget_show (state->dialog);
}
