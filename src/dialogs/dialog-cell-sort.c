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
#include <utils-dialog.h>
#include <ranges.h>
#include <commands.h>
#include <workbook.h>
#include <sort.h>
#include <sheet.h>
#include <workbook-edit.h>
#include <widgets/gnumeric-expr-entry.h>
#include <value.h>
#include <analysis-tools.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>
#include <ctype.h>
#include <stdio.h>

#define GLADE_FILE "cell-sort.glade"

#define CELL_SORT_KEY "cell-sort-dialog"

typedef struct {
	Sheet     *sheet;
	WorkbookControlGUI  *wbcg;
	Workbook  *wb;

	GladeXML  *gui;
	GtkWidget *dialog;
	GtkWidget *warning_dialog;
	GtkWidget *cancel_button;
	GtkWidget *ok_button;
	GtkWidget *help_button;
	GtkWidget *up_button;
	GtkWidget *down_button;
	GnumericExprEntry *range_entry;
	GtkListStore  *model;
	GtkTreeView   *treeview;
	GtkTreeSelection   *selection;
	GtkWidget *cell_sort_row_rb;
	GtkWidget *cell_sort_header_check;
	Value     *sel;
	gboolean   header;
	gboolean   is_cols;

	int        num_clause;
	int        max_col_clause;
	int        max_row_clause;
	GtkWidget *clause_box;
	gboolean   top;
	GList     *colnames_plain;
	GList     *colnames_header;
	GList     *rownames_plain;
	GList     *rownames_header;
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
cb_dialog_help (GtkWidget *button, char const *link)
{
	gnumeric_help_display (link);
}

/**
 * dialog_set_focus:
 * @window:
 * @focus_widget:
 * @state:
 *
 **/
static void
dialog_set_focus (GtkWidget *window, GtkWidget *focus_widget,
			SortFlowState *state)
{
	if (IS_GNUMERIC_EXPR_ENTRY (focus_widget)) {
		wbcg_set_entry (state->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
		gnumeric_expr_entry_set_absolute (GNUMERIC_EXPR_ENTRY (focus_widget));
	} else
		wbcg_set_entry (state->wbcg, NULL);
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
			str = g_strdup (col_name (col));
		else
			str = g_strdup_printf (_("Row %s"), row_name (row));
	} else {
		if (is_cols)
			str = g_strdup (col_name (col));
		else
			str = g_strdup_printf (_("Row %s"), row_name (row));
	}

	return str;
}

#warning Cell-sort temporarily disabled we will fix this soon (AJG)
#if 0  

static GList *
col_row_name_list (Sheet *sheet, int start, int end,
		   int index, gboolean header, gboolean is_cols)
{
	GList *list = NULL;
	gchar *str;
	int i;

	for (i = start; i <= end; i++) {
		str  = is_cols
			? col_row_name (sheet, i, index, header, TRUE)
			: col_row_name (sheet, index, i, header, FALSE);
		list = g_list_prepend (list, str);
	}

	return g_list_reverse (list);
}


static gint
string_pos_in_list (const char *str, GList *list)
{
	GList *l;
	int i = 0;

	for (l = list; l; l = l->next) {
		if (!strcmp (str, (char *) l->data))
			return i;
		i++;
	}

	return -1;
}

/* Advanced dialog */
static void
dialog_cell_sort_adv (GtkWidget *widget, OrderBox *orderbox)
{
	GladeXML *gui;
	GtkWidget *check;
	GtkWidget *rb1, *rb2;
	GtkWidget *dialog;
	gint btn;

	/* Get the dialog and check for errors */
	gui = gnumeric_glade_xml_new (orderbox->wbcg, GLADE_FILE);
        if (gui == NULL)
                return;

	dialog = glade_xml_get_widget (gui, "CellSortAdvanced");
	check  = glade_xml_get_widget (gui, "cell_sort_adv_case");
	rb1    = glade_xml_get_widget (gui, "cell_sort_adv_value");
	rb2    = glade_xml_get_widget (gui, "cell_sort_adv_text");
	if (!dialog || !check || !rb1 || !rb2) {
		g_warning ("Corrupt file " GLADE_FILE "\n");
		g_object_unref (G_OBJECT (gui));
		return;
	}

	/* Set the button states */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				      orderbox->cs);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rb1),
				      orderbox->val);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rb2),
				      !(orderbox->val));

	/* Run the dialog and save the state if necessary */
	btn = gnumeric_dialog_run (orderbox->wbcg, GTK_DIALOG (dialog));
	if (btn == 0) {
		orderbox->cs  = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));
		orderbox->val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rb1));
	}

	if (btn != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));
	g_object_unref (G_OBJECT (gui));
}

/* Order boxes */
static OrderBox *
order_box_new (GtkWidget * parent, const gchar *frame_text,
	       GList *names, gboolean empty, WorkbookControlGUI *wbcg)
{
	OrderBox *orderbox;
	GtkWidget *hbox = gtk_hbox_new (FALSE, 2);

	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);

	orderbox  = g_new (OrderBox, 1);
	orderbox->parent = parent;
	orderbox->main_frame = gtk_frame_new (frame_text);
	orderbox->wbcg = wbcg;

	/* Set up the column names combo boxes */
	orderbox->rangetext = gtk_combo_new ();
	gtk_combo_set_popdown_strings (GTK_COMBO (orderbox->rangetext), names);
	if (empty)
		gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (orderbox->rangetext)->entry), "");
	gtk_box_pack_start (GTK_BOX (hbox), orderbox->rangetext, FALSE, FALSE, 0);

	{	/* Ascending / Descending buttons */
		GtkWidget *item;
		GSList *group = NULL;

		orderbox->asc = 1;
		orderbox->asc_desc = gtk_hbox_new (FALSE, 0);

		item = gtk_radio_button_new_with_label (group, _("Ascending"));
		gtk_box_pack_start (GTK_BOX (orderbox->asc_desc), item, FALSE, FALSE, 0);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (item));
		item = gtk_radio_button_new_with_label (group, _("Descending"));
		gtk_box_pack_start (GTK_BOX (orderbox->asc_desc), item, FALSE, FALSE, 0);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (item));
		orderbox->group = group;

		gtk_box_pack_start (GTK_BOX (hbox), orderbox->asc_desc, FALSE, FALSE, 0);
	}

	/* Advanced button */
	orderbox->cs = FALSE;
	orderbox->val = TRUE;

	/* Advanced button */
	orderbox->adv_button = gtk_button_new_from_stock (GTK_STOCK_PROPERTIES);

	gtk_box_pack_start (GTK_BOX (hbox), orderbox->adv_button,
			    FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (orderbox->adv_button), "clicked",
		GTK_SIGNAL_FUNC (dialog_cell_sort_adv),  orderbox);

	gtk_container_add  (GTK_CONTAINER (orderbox->main_frame), hbox);
	gtk_box_pack_start (GTK_BOX (parent), GTK_WIDGET (orderbox->main_frame),
			    FALSE, TRUE, 0);

	return orderbox;
}

static char const *
order_box_get_text (OrderBox *orderbox)
{
	return gtk_entry_get_text (GTK_ENTRY (
		GTK_COMBO (orderbox->rangetext)->entry));
}

static void
order_box_get_clause (OrderBox *orderbox, SortClause *clause)
{
	clause->asc = gtk_radio_group_get_selected (orderbox->group);
	clause->cs  = orderbox->cs;
	clause->val = orderbox->val;
}

static void
order_box_set_default (OrderBox *orderbox)
{
	gtk_widget_grab_focus (GTK_COMBO (orderbox->rangetext)->entry);
}

static void
order_box_remove (OrderBox *orderbox)
{
	g_return_if_fail (orderbox != NULL);
	if (orderbox->main_frame)
		gtk_container_remove (GTK_CONTAINER (orderbox->parent),
				      orderbox->main_frame);
	orderbox->main_frame = NULL;
}

static void
order_box_destroy (OrderBox *orderbox)
{
	g_return_if_fail (orderbox != NULL);
	g_free (orderbox);
}

/* The cell sort dialog and its callbacks */
static gboolean
dialog_cell_sort_ok (SortFlow *sf)
{
	SortData *data;
	SortClause *array;
	gint divstart, divend;
	int lp;

	if (sf->top) {
		divstart = sf->sel->start.col;
		divend = sf->sel->end.col;
	} else {
		divstart = sf->sel->start.row;
		divend = sf->sel->end.row;
	}

	array = g_new (SortClause, sf->num_clause);
	for (lp = 0; lp < sf->num_clause; lp++) {
		int division;
		const char *txt = order_box_get_text (sf->clauses[lp]);

		order_box_get_clause (sf->clauses[lp], &array[lp]);
		if (strlen (txt)) {
			division = -1;
			if (sf->top) {
				if (sf->header)
					division = divstart + string_pos_in_list (
						txt, sf->colnames_header);
				else
					division = parse_col_name (txt, NULL);
			} else {
				if (sf->header)
					division = divstart + string_pos_in_list (
						txt, sf->rownames_header);
				else {
					/*
					 * FIXME: this is a bit of a hack.  We need
					 * to skip "Row ".
					 */
					while (*txt && !isdigit ((unsigned char)*txt))
						txt++;
					division = atoi (txt) - 1;
				}
			}
			if (division < divstart || division > divend) {
				gnumeric_notice (sf->wbcg,
						 GTK_MESSAGE_ERROR,
						 sf->top
						 ? _("Column must be within range")
						 : _("Row must be within range"));
				g_free (array);
				return TRUE;
			}
			array[lp].offset = division - divstart;
		} else if (lp <= 0) {
			gnumeric_notice (sf->wbcg, GTK_MESSAGE_ERROR,
					 sf->top
					 ? _("First column must be valid")
					 : _("First row must be valid"));
			g_free (array);
			return TRUE;
		} else	/* Just duplicate the last condition: slow but sure */
			array[lp].offset = array[lp - 1].offset;
	}

	if (sf->header) {
		if (sf->top)
			sf->sel->start.row += 1;
		else
			sf->sel->start.col += 1;
	}

	data = g_new (SortData, 1);
	data->sheet = sf->sheet;
	data->range = range_dup (sf->sel);
	data->num_clause = sf->num_clause;
	data->clauses = array;
	data->top = sf->top;

	cmd_sort (WORKBOOK_CONTROL (sf->wbcg), data);

	return FALSE;
}

static void
dialog_cell_sort_del_clause (SortFlow *sf)
{
	if (sf->num_clause > 1) {
		sf->num_clause--;
		order_box_remove  (sf->clauses[sf->num_clause]);
		order_box_destroy (sf->clauses[sf->num_clause]);
/* FIXME: bit nasty !
		gtk_container_queue_resize (GTK_CONTAINER (sf->dialog));
 * */
		gtk_widget_show_all (sf->dialog);
		sf->clauses[sf->num_clause] = NULL;
	} else
		gnumeric_notice (sf->wbcg, GTK_MESSAGE_ERROR,
				 _("At least one clause is required."));
}

static void
dialog_cell_sort_add_clause (SortFlow *sf, WorkbookControlGUI *wbcg)
{
	if ((sf->num_clause >= sf->max_col_clause && sf->top)
	    || (sf->num_clause >= sf->max_row_clause && !(sf->top)))
		gnumeric_notice (sf->wbcg, GTK_MESSAGE_ERROR,
				 _("Can't add more than the selection length."));
	else if (sf->num_clause >= MAX_CLAUSE)
		gnumeric_notice (sf->wbcg, GTK_MESSAGE_ERROR,
				 _("Maximum number of clauses has been reached."));
	else {
		if (sf->header)
			sf->clauses[sf->num_clause] = order_box_new (sf->clause_box, _("then by"),
								      sf->colnames_header, TRUE, wbcg);
		else
			sf->clauses[sf->num_clause] = order_box_new (sf->clause_box, _("then by"),
								      sf->colnames_plain, TRUE, wbcg);

		gtk_widget_show_all (sf->dialog);
		sf->num_clause++;
	}
}

static void
dialog_cell_sort_set_clauses (SortFlow *sf, int clauses) {
	int i;

	if (sf->num_clause <= clauses) return;

	for (i = 0; i < (sf->num_clause - clauses); i++)
		dialog_cell_sort_del_clause (sf);
}

static void
dialog_cell_sort_header_toggled (GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->header = GTK_TOGGLE_BUTTON (widget)->active;
	for (i = 0; i < sf->num_clause; i++) {
		if (sf->header) {
			if (sf->top)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->colnames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->rownames_header);
		} else {
			if (sf->top)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->colnames_plain);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->rownames_plain);
		}
		if (i > 0)
			gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (
				sf->clauses[i]->rangetext)->entry), "");
	}
}


static void
dialog_cell_sort_rows_toggled (GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->top = !(GTK_TOGGLE_BUTTON (widget)->active);
	if (!(sf->top)) {
		if (sf->num_clause > sf->max_row_clause)
			dialog_cell_sort_set_clauses (sf, sf->max_row_clause);
		for (i=0; i<sf->num_clause; i++) {
			if (sf->header)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->rownames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->rownames_plain);
			if (i > 0)
				gtk_entry_set_text
					(GTK_ENTRY (GTK_COMBO (sf->clauses[i]->rangetext)->entry), "");
		}
	}
}

static void
dialog_cell_sort_cols_toggled (GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->top = GTK_TOGGLE_BUTTON (widget)->active;
	if ((sf->top)) {
		if (sf->num_clause > sf->max_col_clause)
			dialog_cell_sort_set_clauses (sf, sf->max_col_clause);
		for (i = 0; i < sf->num_clause; i++) {
			if (sf->header)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->colnames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->colnames_plain);
			if (i > 0)
				gtk_entry_set_text
					(GTK_ENTRY (GTK_COMBO (sf->clauses[i]->rangetext)->entry), "");
		}
	}
}

#endif


static gboolean
translate_range (Value *range, SortFlowState *state) 
{
	gboolean old_header = state->header;
	gboolean old_is_cols = state->is_cols;

	state->header = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_header_check));;
	state->is_cols = !gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (state->cell_sort_row_rb));;

	if (state->sel == NULL) {
		state->sel = range;
		return TRUE;
	}

#warning FIXME:
#warning the next check is completely useless since the expr entry widget always 
#warning first set the widget to empty and then to the right content, causing 2 signals!

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
load_model_data (SortFlowState *state) 
{
	int start;
	int end;
	int index;
	int i;
	gchar *str;
	GtkTreeIter iter;
	Sheet *sheet = state->sel->v_range.cell.a.sheet;

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

	for (i = start; i <= end; i++) {
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
		g_free (str);
	}
}

/**
 * cb_update_sensitivity:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are one or two standard input and and one output item, permitting multiple 
 * areas as first input.
 **/
static void
cb_update_sensitivity (GtkWidget *dummy, SortFlowState *state)
{
        Value *range;


        range = gnumeric_expr_entry_parse_to_value 
		(GNUMERIC_EXPR_ENTRY (state->range_entry), state->sheet);
	if (range == NULL) {
		if (state->sel != NULL) {
			value_release (state->sel);
			state->sel = NULL;
			gtk_list_store_clear (state->model);
		}
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		fprintf (stderr, "Range  is NULL\n");
	} else {
		if (translate_range (range, state))
			load_model_data (state);
		gtk_widget_set_sensitive (state->ok_button, TRUE);
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
	char *name;
	Range const *first;

	first = selection_first_range (state->sheet, NULL, NULL);
	
	if (first != NULL) {
		name =  global_range_name (state->sheet, first);
		gtk_entry_set_text (GTK_ENTRY (state->range_entry), name);
		g_free (name);
	}
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
		return;
	}

	gtk_widget_set_sensitive (state->up_button,
				  location_of_iter (&iter, state->model) > 0);

	row = location_of_iter (&iter, state->model);
	gtk_widget_set_sensitive (state->down_button,
				  gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, row+1));
}

static void
toggled (GtkCellRendererToggle *cell,
	 gchar                 *path_string,
	 gpointer               data,
	 int                    column)
{
  GtkTreeModel *model = GTK_TREE_MODEL (data);
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


static void
cb_toggled_in_use (GtkCellRendererToggle *cell,
	 gchar                 *path_string,
	 gpointer               data)
{
	toggled (cell, path_string, data, ITEM_IN_USE);
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
	state->range_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->range_entry,
                                      GNUM_EE_SINGLE_RANGE, 
                                      GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->range_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->range_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->range_entry));
	gtk_widget_show (GTK_WIDGET (state->range_entry));
	gtk_signal_connect (GTK_OBJECT (state->range_entry), "changed",
			    GTK_SIGNAL_FUNC (cb_update_sensitivity), state);

/* Set-up tree view */
	scrolled = glade_xml_get_widget (state->gui, "scrolled_cell_sort_list");
	state->model = gtk_list_store_new (NUM_COLMNS, G_TYPE_BOOLEAN, G_TYPE_STRING,
					   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, 
					   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_INT);
	state->treeview = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (state->model)));
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_BROWSE);
	g_signal_connect (state->selection, "changed",
			  G_CALLBACK (cb_sort_selection_changed), state);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (cb_toggled_in_use), state->model);
	column = gtk_tree_view_column_new_with_attributes (_("Sort"),
							   renderer,
							   "active", ITEM_IN_USE, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	column = gtk_tree_view_column_new_with_attributes (_("Row/\nColumn"),
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (cb_toggled_descending), state->model);
	column = gtk_tree_view_column_new_with_attributes (_("Descend"),
							   renderer,
							   "active", ITEM_DESCENDING, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (cb_toggled_case_sensitive), state->model);
	column = gtk_tree_view_column_new_with_attributes (_("Case\nSensitive"),
							   renderer,
							   "active", ITEM_CASE_SENSITIVE, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (cb_toggled_sort_by_value), state->model);
	column = gtk_tree_view_column_new_with_attributes (_("By\nValue"),
							   renderer,
							   "active", ITEM_SORT_BY_VALUE, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->treeview));
	gtk_widget_show (GTK_WIDGET (state->treeview));

/* Set-up other widgets */
	state->cell_sort_row_rb = glade_xml_get_widget (state->gui, "cell_sort_row_rb");
	gtk_signal_connect (GTK_OBJECT (state->cell_sort_row_rb), "toggled",
			    GTK_SIGNAL_FUNC (cb_update_sensitivity), state);

	state->cell_sort_header_check = glade_xml_get_widget (state->gui, 
							      "cell_sort_header_check");
	gtk_signal_connect (GTK_OBJECT (state->cell_sort_header_check), "toggled",
			    GTK_SIGNAL_FUNC (cb_update_sensitivity), state);


/* Set-up buttons */
	state->up_button  = glade_xml_get_widget (state->gui, "up_button");
	gtk_signal_connect (GTK_OBJECT (state->up_button),
		"clicked",
		GTK_SIGNAL_FUNC (cb_up), state);
	state->down_button  = glade_xml_get_widget (state->gui, "down_button");
	gtk_signal_connect (GTK_OBJECT (state->down_button),
		"clicked",
		GTK_SIGNAL_FUNC (cb_down), state);
	state->help_button  = glade_xml_get_widget (state->gui, "help_button");
	gtk_signal_connect (GTK_OBJECT (state->help_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_help), "cell-sort.html");

	state->ok_button  = glade_xml_get_widget (state->gui, "ok_button");
	gtk_signal_connect (GTK_OBJECT (state->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_cancel_clicked),
			    state);
	state->cancel_button  = glade_xml_get_widget (state->gui, "cancel_button");
	gtk_signal_connect (GTK_OBJECT (state->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_cancel_clicked),
			    state);

/* Finish dialog signals */
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (dialog_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (dialog_destroy), state);
	cb_sort_selection_changed (NULL, state);
	dialog_load_selection (state);

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

#if 0
	if (!(sel = selection_first_range (sheet, WORKBOOK_CONTROL (wbcg), _("Sort"))))
		return;

	/* Initialize some important stuff */
	sort_flow.sel = range_dup (sel);
	sort_flow.sheet = sheet;
	sort_flow.wbcg = wbcg;

	/* Correct selection if necessary */
	range_clip_to_finite (sort_flow.sel, sort_flow.sheet);

	/* Set up the dialog information */
	sort_flow.header = FALSE;
	sort_flow.top = TRUE;
	sort_flow.colnames_plain  = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.col,
						       sort_flow.sel->end.col,
						       sort_flow.sel->start.row,
						       FALSE, TRUE);
	sort_flow.colnames_header = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.col,
						       sort_flow.sel->end.col,
						       sort_flow.sel->start.row,
						       TRUE, TRUE);
	sort_flow.rownames_plain  = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.row,
						       sort_flow.sel->end.row,
						       sort_flow.sel->start.col,
						       FALSE, FALSE);
	sort_flow.rownames_header = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.row,
						       sort_flow.sel->end.row,
						       sort_flow.sel->start.col,
						       TRUE, FALSE);

#endif
	/* Get the dialog and check for errors */
	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (state->gui == NULL) {
		g_free (state);
                return;
	}

        state->dialog = glade_xml_get_widget (state->gui, "CellSort");
#if 0
	table = glade_xml_get_widget (gui, "cell_sort_table");
	check = glade_xml_get_widget (gui, "cell_sort_header_check");
	rb1   = glade_xml_get_widget (gui, "cell_sort_row_rb");
	rb2   = glade_xml_get_widget (gui, "cell_sort_col_rb");
	if (!sort_flow.dialog || !table || !check || !rb1 || !rb2) {
		g_warning ("Corrupt file cell-sort.glade\n");
		g_object_unref (G_OBJECT (gui));
		return;
	}

	/* Init clauses */
	sort_flow.max_col_clause = sort_flow.sel->end.col
		- sort_flow.sel->start.col + 1;
	sort_flow.max_row_clause = sort_flow.sel->end.row
		- sort_flow.sel->start.row + 1;
	sort_flow.num_clause = sort_flow.max_col_clause > 1 ? 2 : 1;
	for (lp = 0; lp < MAX_CLAUSE; lp++)
		sort_flow.clauses[lp] = NULL;

	/* Build the rest of the dialog */
	gnome_dialog_close_hides (GNOME_DIALOG (sort_flow.dialog), TRUE);

	sort_flow.clause_box = gtk_vbox_new (FALSE, FALSE);
	gtk_table_attach_defaults (GTK_TABLE (table),
				   sort_flow.clause_box, 0, 1, 0, 1);

	for (lp = 0; lp < sort_flow.num_clause; lp++) {
		sort_flow.clauses[lp] = order_box_new (sort_flow.clause_box,
							lp
							? _("then by")
							: _("Sort by"),
							sort_flow.colnames_plain,
							lp ? TRUE : FALSE, wbcg);
	}
	order_box_set_default (sort_flow.clauses[0]);

	/* Hook up the signals */
	gtk_signal_connect (GTK_OBJECT (check), "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_header_toggled),
			    &sort_flow);
	gtk_signal_connect (GTK_OBJECT (rb1),   "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_rows_toggled),
			    &sort_flow);
	gtk_signal_connect (GTK_OBJECT (rb2),   "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_cols_toggled),
			    &sort_flow);

	/* Set the header button and drop down boxes correctly */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
		range_has_header (sort_flow.sheet, sort_flow.sel, TRUE, FALSE));

	gtk_widget_show_all (sort_flow.clause_box);

	/* Clean up */
	g_free (sort_flow.sel);

	if (sort_flow.dialog)
		gtk_object_destroy (GTK_OBJECT (sort_flow.dialog));

	for (lp = 0; lp < sort_flow.num_clause; lp++)
		order_box_destroy (sort_flow.clauses[lp]);

	e_free_string_list (sort_flow.colnames_plain);
	e_free_string_list (sort_flow.colnames_header);
	e_free_string_list (sort_flow.rownames_plain);
	e_free_string_list (sort_flow.rownames_header);
	g_object_unref (G_OBJECT (gui));
#endif

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
