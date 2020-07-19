/*
 * dialog-sheet-order.c: Dialog to change the order of sheets in the Gnumeric
 * spreadsheet
 *
 * Author:
 *	Jody Goldberg <jody@gnome.org>
 *	Andreas J. Guelzow <aguelzow@taliesin.ca>
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

/* We shouldn't need workbook-priv.h but we need to know whether undo commands are pending */
#include <workbook-priv.h>

#include <sheet.h>
#include <style-color.h>
#include <commands.h>
#include <application.h>
#include <widgets/gnm-cell-renderer-text.h>
#include <widgets/gnm-cell-renderer-toggle.h>
#include <goffice/goffice.h>

#include <string.h>


#define SHEET_ORDER_KEY          "sheet-order-dialog"

typedef struct {
	WBCGtk  *wbcg;

	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkTreeView *sheet_list;
	GtkListStore *model;
	GtkWidget *up_btn;
	GtkWidget *down_btn;
	GtkWidget *add_btn;
	GtkWidget *append_btn;
	GtkWidget *duplicate_btn;
	GtkWidget *delete_btn;
	GtkWidget *apply_names_btn;
	GtkWidget *sort_asc_btn;
	GtkWidget *sort_desc_btn;
	GtkWidget *undo_btn;
	GtkWidget *cancel_btn;
	GtkWidget *advanced_check;
	GtkWidget *ccombo_back;
	GtkWidget *ccombo_fore;
	GtkWidget *warning;

	GdkPixbuf *image_padlock;
	GdkPixbuf *image_padlock_no;

	GdkPixbuf *image_ltr;
	GdkPixbuf *image_rtl;

	GdkPixbuf *image_visible;

	gboolean initial_colors_set;

	GtkTreeViewColumn *dir_column;
	GtkTreeViewColumn *row_max_column;
	GtkTreeViewColumn *col_max_column;

	gulong sheet_order_changed_listener;
	gulong sheet_added_listener;
	gulong sheet_deleted_listener;

	gulong model_selection_changed_listener;
	gulong model_row_insertion_listener;
} SheetManager;

enum {
	SHEET_LOCKED,
	SHEET_LOCK_IMAGE,
	SHEET_VISIBLE,
	SHEET_VISIBLE_IMAGE,
	SHEET_ROW_MAX,
	SHEET_COL_MAX,
	SHEET_NAME,
	SHEET_NEW_NAME,
	SHEET_POINTER,
	BACKGROUND_COLOUR,
	FOREGROUND_COLOUR,
	SHEET_DIRECTION,
	SHEET_DIRECTION_IMAGE,
	NUM_COLUMNS
};

static char *verify_validity (SheetManager *state, gboolean *pchanged);
static void dialog_sheet_order_update_sheet_order (SheetManager *state);


static void
update_undo (SheetManager *state, WorkbookControl *wbc)
{

	gtk_widget_set_sensitive (state->undo_btn, TRUE);
}

static void
workbook_signals_block (SheetManager *state)
{
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);

	g_signal_handler_block (G_OBJECT (wb),
				state->sheet_order_changed_listener);
	g_signal_handler_block (G_OBJECT (wb),
				state->sheet_added_listener);
	g_signal_handler_block (G_OBJECT (wb),
				state->sheet_deleted_listener);
}

static void
workbook_signals_unblock (SheetManager *state)
{
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);

	g_signal_handler_unblock (G_OBJECT (wb),
				state->sheet_order_changed_listener);
	g_signal_handler_unblock (G_OBJECT (wb),
				state->sheet_added_listener);
	g_signal_handler_unblock (G_OBJECT (wb),
				state->sheet_deleted_listener);
}

static void
cb_name_edited (GtkCellRendererText *cell,
	gchar               *path_string,
	gchar               *new_text,
        SheetManager        *state)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean changed = FALSE;
	char *error;

	if (cell != NULL) {
		path = gtk_tree_path_new_from_string (path_string);
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
					     &iter, path))
			gtk_list_store_set (state->model, &iter,
					    SHEET_NEW_NAME, new_text, -1);
		else
			g_warning ("Did not get a valid iterator");
		gtk_tree_path_free (path);
	}

	error = verify_validity (state, &changed);

	if (error != NULL) {
		gtk_widget_set_sensitive (state->apply_names_btn, FALSE);
		gtk_label_set_text (GTK_LABEL (state->warning), error);
	} else {
		gtk_widget_set_sensitive (state->apply_names_btn, changed);
		gtk_label_set_markup (GTK_LABEL (state->warning),
				      changed ? _("<b>Note:</b> A sheet name change is pending.") : "");
	}
}


typedef struct {
	char *key;
	int i;
} gtmff_sort_t;

static gint
gtmff_compare_func (gconstpointer a, gconstpointer b)
{
	gtmff_sort_t const *pa = a, *pb = b;

	return strcmp (pa->key, pb->key);
}


static gboolean
gtmff_asc (GtkTreeModel *model, GtkTreePath *path,
	   GtkTreeIter *iter, gpointer data)
{
	GSList **l = data;
	Sheet *this_sheet;
	char *name;
	gtmff_sort_t *ptr;


	ptr = g_new (gtmff_sort_t, 1);
	gtk_tree_model_get (model, iter,
			    SHEET_POINTER, &this_sheet,
			    SHEET_NAME, &name,
			    -1);
	ptr->i = this_sheet->index_in_wb;
	ptr->key = g_utf8_collate_key_for_filename (name, -1);

	*l = g_slist_insert_sorted (*l, ptr, (GCompareFunc) gtmff_compare_func);

	return FALSE;
}

static void
sort_asc_desc (SheetManager *state, gboolean asc)
{
	WorkbookSheetState *old_state;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	GSList *l = NULL, *l_tmp;
	gint n = 0;

	gtk_tree_model_foreach (GTK_TREE_MODEL (state->model), gtmff_asc, &l);

	if (!asc)
		l = g_slist_reverse (l);

	workbook_signals_block (state);

	old_state = workbook_sheet_state_new (wb);

	for (l_tmp = l; l_tmp != NULL; l_tmp = l_tmp->next) {
		gtmff_sort_t *ptr = l_tmp->data;
		GtkTreeIter iter;
		Sheet *sheet;

		gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
						&iter, NULL, ptr->i);
		g_free (ptr->key);
		g_free (ptr);
		l_tmp->data = NULL;

		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
				    SHEET_POINTER, &sheet,
				    -1);
		workbook_sheet_move (sheet, n - sheet->index_in_wb);
		n++;
	}
	g_slist_free (l);

	/* Now we change the list store  */
	dialog_sheet_order_update_sheet_order (state);

	cmd_reorganize_sheets (wbc, old_state, NULL);
	update_undo (state, wbc);

	workbook_signals_unblock (state);
}

static void
cb_asc (G_GNUC_UNUSED GtkWidget *w, SheetManager *state)
{
	sort_asc_desc (state, TRUE);
}

static void
cb_desc (G_GNUC_UNUSED GtkWidget *w, SheetManager *state)
{
	sort_asc_desc (state, FALSE);
}

static gboolean
color_equal (const GdkRGBA *color_a, const GnmColor *color_gb)
{
	if (color_gb == NULL)
		return color_a == NULL;
	/* FIXME: What about ->is_auto?  */
	return color_a && GO_COLOR_FROM_GDK_RGBA (*color_a) == color_gb->go_color;
}

static void
cb_color_changed_fore (G_GNUC_UNUSED GOComboColor *go_combo_color,
		       GOColor color, G_GNUC_UNUSED gboolean custom,
		       G_GNUC_UNUSED gboolean by_user,
		       G_GNUC_UNUSED gboolean is_default,
		       SheetManager *state)
{
	GList *selected_rows, *l;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	WorkbookSheetState *old_state;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	GdkRGBA gdk_color;
	GdkRGBA *p_gdk_color;
	GnmColor *gnm_color;

	g_return_if_fail (selection != NULL);

	selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);

	p_gdk_color = (color == 0) ? NULL : go_color_to_gdk_rgba (color, &gdk_color);
	gnm_color = (color == 0) ? NULL : gnm_color_new_gdk (&gdk_color);

	old_state = workbook_sheet_state_new (wb);

	for (l = selected_rows; l != NULL; l = l->next) {
		Sheet *this_sheet;
		GtkTreeIter sel_iter;
		GtkTreePath *path = l->data;

		gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model), &sel_iter, path);
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &sel_iter,
				    SHEET_POINTER, &this_sheet,
				    -1);
		if (color_equal (p_gdk_color, this_sheet->tab_text_color))
			continue;

		gtk_list_store_set (state->model, &sel_iter,
				    FOREGROUND_COLOUR, p_gdk_color,
				    -1);
		g_object_set (this_sheet,
			      "tab-foreground", gnm_color,
			      NULL);
	}

	style_color_unref (gnm_color);
	cmd_reorganize_sheets (wbc, old_state, NULL);
	update_undo (state, wbc);

	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);
}

static void
cb_color_changed_back (G_GNUC_UNUSED GOComboColor *go_combo_color,
		       GOColor color, G_GNUC_UNUSED gboolean custom,
		       G_GNUC_UNUSED gboolean by_user,
		       G_GNUC_UNUSED gboolean is_default,
		       SheetManager *state)
{
	GList *selected_rows, *l;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	WorkbookSheetState *old_state;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	GdkRGBA gdk_color;
	GdkRGBA *p_gdk_color;
	GnmColor *gnm_color;

	g_return_if_fail (selection != NULL);

	selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);

	p_gdk_color = (color == 0) ? NULL : go_color_to_gdk_rgba (color, &gdk_color);
	gnm_color = (color == 0) ? NULL : gnm_color_new_gdk (&gdk_color);

	old_state = workbook_sheet_state_new (wb);

	for (l = selected_rows; l != NULL; l = l->next) {
		Sheet *this_sheet;
		GtkTreeIter sel_iter;
		GtkTreePath *path = l->data;

		gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model), &sel_iter, path);
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &sel_iter,
				    SHEET_POINTER, &this_sheet,
				    -1);
		if (color_equal (p_gdk_color, this_sheet->tab_color))
			continue;

		gtk_list_store_set (state->model, &sel_iter,
				    BACKGROUND_COLOUR, p_gdk_color,
				    -1);
		g_object_set (this_sheet,
			      "tab-background", gnm_color,
			      NULL);
	}

	style_color_unref (gnm_color);
	cmd_reorganize_sheets (wbc, old_state, NULL);
	update_undo (state, wbc);

	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);
}

static gboolean
cb_sheet_order_cnt_visible (GtkTreeModel *model,
			    GtkTreePath *path,
			    GtkTreeIter *iter,
			    gpointer data)
{
	gint *i = data;
	gboolean is_visible;

	gtk_tree_model_get (model, iter,
			    SHEET_VISIBLE, &is_visible,
			    -1);
	if (is_visible)
		(*i)++;

	return FALSE;
}

static gint
sheet_order_cnt_visible (SheetManager *state)
{
	gint data = 0;
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->model),
				cb_sheet_order_cnt_visible,
				&data);
	return data;
}

/*
 * Refreshes the buttons on a row (un)selection and selects the chosen sheet
 * for this view.
 */
static void
cb_selection_changed (G_GNUC_UNUSED GtkTreeSelection *ignored,
		      SheetManager *state)
{
	GtkTreeIter  iter;
	Sheet *sheet;
	gboolean has_iter;
	GdkRGBA *fore, *back;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->sheet_list);
	GList *selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);
	gboolean multiple = gtk_tree_model_iter_n_children(GTK_TREE_MODEL (state->model), NULL) > 1;
	int cnt_sel = g_list_length (selected_rows);
	gboolean single_sel = (cnt_sel < 2);

	gtk_widget_set_sensitive (state->sort_asc_btn, multiple);
	gtk_widget_set_sensitive (state->sort_desc_btn, multiple);

	if (selected_rows == NULL) {
		gtk_widget_set_sensitive (state->up_btn, FALSE);
		gtk_widget_set_sensitive (state->down_btn, FALSE);
		gtk_widget_set_sensitive (state->delete_btn, FALSE);
		gtk_widget_set_sensitive (state->ccombo_back, FALSE);
		gtk_widget_set_sensitive (state->ccombo_fore, FALSE);
		gtk_widget_set_sensitive (state->add_btn, FALSE);
		gtk_widget_set_sensitive (state->duplicate_btn, FALSE);
		return;
	}

	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				 &iter, (GtkTreePath *) selected_rows->data);

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
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
		gdk_rgba_free (back);
	if (fore != NULL)
		gdk_rgba_free (fore);

	gtk_widget_set_sensitive (state->ccombo_back, TRUE);
	gtk_widget_set_sensitive (state->ccombo_fore, TRUE);
	gtk_widget_set_sensitive (state->delete_btn, sheet_order_cnt_visible (state) > cnt_sel);
	gtk_widget_set_sensitive (state->add_btn, single_sel);
	gtk_widget_set_sensitive (state->duplicate_btn, single_sel);

	has_iter = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (state->model), &iter);
	g_return_if_fail (has_iter);
	gtk_widget_set_sensitive (state->up_btn,
				  single_sel &&
				  !gtk_tree_selection_iter_is_selected (selection, &iter));
	gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (state->model), &iter, NULL,
				       gtk_tree_model_iter_n_children
				       (GTK_TREE_MODEL (state->model), NULL) - 1);
	gtk_widget_set_sensitive (state->down_btn,
				  single_sel &&
				  !gtk_tree_selection_iter_is_selected (selection, &iter));

	if (sheet != NULL)
		wb_view_sheet_focus (
			wb_control_view (GNM_WBC (state->wbcg)), sheet);

	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);
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
	gboolean is_locked = TRUE;
	Sheet *this_sheet = NULL;
	WorkbookSheetState *old_state = NULL;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);

	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_model_get (model, &iter,
				    SHEET_LOCKED, &is_locked,
				    SHEET_POINTER, &this_sheet,
				    -1);

		if (is_locked) {
			gtk_list_store_set
				(GTK_LIST_STORE (model), &iter, SHEET_LOCKED,
				 FALSE, SHEET_LOCK_IMAGE,
				 state->image_padlock_no, -1);
		} else {
			gtk_list_store_set
				(GTK_LIST_STORE (model), &iter, SHEET_LOCKED,
				 TRUE, SHEET_LOCK_IMAGE,
				 state->image_padlock, -1);
		}
	} else {
		g_warning ("Did not get a valid iterator");
		gtk_tree_path_free (path);
		return;
	}
	gtk_tree_path_free (path);

	old_state = workbook_sheet_state_new (wb);
	g_object_set (this_sheet,
		      "protected", !is_locked,
		      NULL);
	cmd_reorganize_sheets (wbc, old_state, this_sheet);
	update_undo (state, wbc);
}

static void
cb_toggled_direction (G_GNUC_UNUSED GtkCellRendererToggle *cell,
		      gchar		*path_string,
		      SheetManager	*state)
{
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreePath  *path  = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter iter;
	gboolean is_rtl = TRUE;
	Sheet *this_sheet = NULL;
	WorkbookSheetState *old_state = NULL;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);

	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_model_get (model, &iter,
				    SHEET_DIRECTION, &is_rtl,
				    SHEET_POINTER, &this_sheet,
				    -1);
		gtk_list_store_set
			(GTK_LIST_STORE (model), &iter,
			 SHEET_DIRECTION,	!is_rtl,
			 SHEET_DIRECTION_IMAGE,
			 is_rtl ? state->image_ltr : state->image_rtl,
			 -1);
	} else {
		g_warning ("Did not get a valid iterator");
		gtk_tree_path_free (path);
		return;
	}

	gtk_tree_path_free (path);

	old_state = workbook_sheet_state_new (wb);
	g_object_set (this_sheet,
		      "text-is-rtl", !is_rtl,
		      NULL);
	cmd_reorganize_sheets (wbc, old_state, this_sheet);
	update_undo (state, wbc);
}

static void populate_sheet_list (SheetManager *state);

static void
cb_toggled_visible (G_GNUC_UNUSED GtkCellRendererToggle *cell,
		 gchar                 *path_string,
		 gpointer               data)
{
	SheetManager *state = data;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	gboolean is_visible;
	Sheet *this_sheet;
	WorkbookSheetState *old_state;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	int cnt;

	if (!gtk_tree_model_get_iter (model, &iter, path)) {
		g_warning ("Did not get a valid iterator");
		gtk_tree_path_free (path);
		return;
	}

	gtk_tree_model_get (model, &iter,
			    SHEET_VISIBLE, &is_visible,
			    SHEET_POINTER, &this_sheet,
			    -1);

	if (is_visible) {
		cnt = sheet_order_cnt_visible (state);
		if (cnt <= 1) {
			go_gtk_notice_dialog (GTK_WINDOW (state->dialog), GTK_MESSAGE_ERROR,
					      _("At least one sheet must remain visible!"));
			gtk_tree_path_free (path);
			return;
		}
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    SHEET_VISIBLE, FALSE,
				    SHEET_VISIBLE_IMAGE, NULL,
				    -1);

	} else {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    SHEET_VISIBLE, TRUE,
				    SHEET_VISIBLE_IMAGE,
				    state->image_visible,
				    -1);
	}
	gtk_tree_path_free (path);

	old_state = workbook_sheet_state_new (wb);
	g_object_set (this_sheet,
		      "visibility",
		      !is_visible ? GNM_SHEET_VISIBILITY_VISIBLE
		      : GNM_SHEET_VISIBILITY_HIDDEN,
		      NULL);

	cmd_reorganize_sheets (wbc, old_state, this_sheet);
	update_undo (state, wbc);

	if (is_visible)
		populate_sheet_list (state);
}

static gboolean
sheet_selection_can_toggle(GtkTreeSelection *selection,
			   GtkTreeModel *model,
			   GtkTreePath *path,
			   gboolean path_currently_selected,
			   G_GNUC_UNUSED gpointer data)
{
	GtkTreeIter iter;
	gboolean is_visible;

	if (path_currently_selected ||
	    !gtk_tree_model_get_iter (model, &iter, path))
		return TRUE;

	gtk_tree_model_get (model, &iter,
			    SHEET_VISIBLE, &is_visible,
			    -1);

	return is_visible;
}

static void
create_sheet_list (SheetManager *state)
{
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	GtkWidget *scrolled = go_gtk_builder_get_widget (state->gui, "scrolled");
	GtkCellRenderer *renderer;

	state->model = gtk_list_store_new (NUM_COLUMNS,
					   G_TYPE_BOOLEAN,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_BOOLEAN,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_INT,
					   G_TYPE_INT,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_POINTER,
					   GDK_TYPE_RGBA,
					   GDK_TYPE_RGBA,
					   G_TYPE_BOOLEAN,
					   GDK_TYPE_PIXBUF);
	state->sheet_list = GTK_TREE_VIEW (gtk_tree_view_new_with_model
					   (GTK_TREE_MODEL (state->model)));
	selection = gtk_tree_view_get_selection (state->sheet_list);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	renderer = gnm_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_lock), state);
	column = gtk_tree_view_column_new_with_attributes
		/* xgettext : "Lock" is short for locked.  Keep this short.  */
		(_("Lock"),
		 renderer,
		 "active", SHEET_LOCKED,
		 "pixbuf", SHEET_LOCK_IMAGE,
		 NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	renderer = gnm_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_toggled_visible), state);
	column = gtk_tree_view_column_new_with_attributes
		/* xgettext : "Viz" is short for visibility.  Keep this short.  */
		(_("Viz"),
		 renderer,
		 "active", SHEET_VISIBLE,
		 "pixbuf", SHEET_VISIBLE_IMAGE,
		 NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	renderer = gnm_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
		G_CALLBACK (cb_toggled_direction), state);
	column = gtk_tree_view_column_new_with_attributes
		/* xgettext : "Dir" is short for direction.  Keep this short.  */
		(_("Dir"),
		 renderer,
		 "active", SHEET_DIRECTION,
		 "pixbuf", SHEET_DIRECTION_IMAGE,
		 NULL);
	gtk_tree_view_column_set_visible (column, FALSE);
	gtk_tree_view_append_column (state->sheet_list, column);
	state->dir_column = column;

	column = gtk_tree_view_column_new_with_attributes
		/*Translators: Table header for column with number of "Rows"*/
		(C_("sheetlist", "Rows"),
		 gnm_cell_renderer_text_new (),
		 "text", SHEET_ROW_MAX,
		 NULL);
	gtk_tree_view_column_set_visible (column, FALSE);
	gtk_tree_view_append_column (state->sheet_list, column);
	state->row_max_column = column;

	column = gtk_tree_view_column_new_with_attributes
		/*Translators: Table header for column with number of "Cols"*/
		(C_("sheetlist", "Cols"),
		 gnm_cell_renderer_text_new (),
		 "text", SHEET_COL_MAX,
		 NULL);
	gtk_tree_view_column_set_visible (column, FALSE);
	gtk_tree_view_append_column (state->sheet_list, column);
	state->col_max_column = column;

	column = gtk_tree_view_column_new_with_attributes (_("Current Name"),
					      gnm_cell_renderer_text_new (),
					      "text", SHEET_NAME,
					      "background-rgba",BACKGROUND_COLOUR,
					      "foreground-rgba",FOREGROUND_COLOUR,
					      NULL);
	gtk_tree_view_append_column (state->sheet_list, column);

	renderer = gnm_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer),
		      "editable", TRUE,
		      "editable-set", TRUE,
		      NULL);
	column = gtk_tree_view_column_new_with_attributes (_("New Name"),
					      renderer,
					      "text", SHEET_NEW_NAME,
					      "background-rgba",BACKGROUND_COLOUR,
					      "foreground-rgba",FOREGROUND_COLOUR,
					      NULL);
	gtk_tree_view_append_column (state->sheet_list, column);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_name_edited), state);

	gtk_tree_view_set_reorderable (state->sheet_list, TRUE);

	/* Init the buttons & selection */
	state->model_selection_changed_listener =
		g_signal_connect (selection,
				  "changed",
				  G_CALLBACK (cb_selection_changed), state);
	gtk_tree_selection_set_select_function (selection,
						sheet_selection_can_toggle,
						NULL, NULL);

	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (state->sheet_list));
}

static void
set_sheet_info_at_iter (SheetManager *state, GtkTreeIter *iter, Sheet *sheet)
{
	GdkRGBA cback, *color = NULL;
	GdkRGBA cfore, *text_color = NULL;

	if (sheet->tab_color)
		color = go_color_to_gdk_rgba (sheet->tab_color->go_color, &cback);
	if (sheet->tab_text_color)
		text_color = go_color_to_gdk_rgba (sheet->tab_text_color->go_color, &cfore);

	gtk_list_store_set (state->model, iter,
			    SHEET_LOCKED, sheet->is_protected,
			    SHEET_LOCK_IMAGE, (sheet->is_protected
					       ? state->image_padlock
					       : state->image_padlock_no),
			    SHEET_VISIBLE, (sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE),
			    SHEET_VISIBLE_IMAGE, (sheet->visibility == GNM_SHEET_VISIBILITY_VISIBLE
						  ? state->image_visible
						  : NULL),
			    SHEET_ROW_MAX, gnm_sheet_get_max_rows (sheet),
			    SHEET_COL_MAX, gnm_sheet_get_max_cols (sheet),
			    SHEET_NAME, sheet->name_unquoted,
			    SHEET_NEW_NAME, "",
			    SHEET_POINTER, sheet,
			    BACKGROUND_COLOUR, color,
			    FOREGROUND_COLOUR, text_color,
			    SHEET_DIRECTION, sheet->text_is_rtl,
			    SHEET_DIRECTION_IMAGE, (sheet->text_is_rtl
						    ? state->image_rtl
						    : state->image_ltr),
			    -1);


}

/* Add all of the sheets to the sheet_list */
static void
populate_sheet_list (SheetManager *state)
{
	GtkTreeSelection  *selection;
	GtkTreeIter iter;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	Sheet *cur_sheet = wb_control_cur_sheet (wbc);
	int i, n = workbook_sheet_count (wb);
	GtkTreePath *sel_path = NULL;

	selection = gtk_tree_view_get_selection (state->sheet_list);

	g_signal_handler_block (selection, state->model_selection_changed_listener);
	if (state->model_row_insertion_listener)
		g_signal_handler_block (state->model, state->model_row_insertion_listener);

	gtk_list_store_clear (state->model);
	gtk_label_set_text (GTK_LABEL (state->warning), "");

	for (i = 0 ; i < n ; i++) {
		Sheet *sheet = workbook_sheet_by_index (wb, i);

		gtk_list_store_append (state->model, &iter);
		set_sheet_info_at_iter (state, &iter, sheet);

		if (sheet == cur_sheet)
			sel_path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->model),
							    &iter);
	}

	if (sel_path) {
		gtk_tree_selection_select_path (selection, sel_path);
		gtk_tree_path_free (sel_path);
	}

	if (state->model_row_insertion_listener)
		g_signal_handler_unblock (state->model, state->model_row_insertion_listener);
	g_signal_handler_unblock (selection, state->model_selection_changed_listener);

	/* Init the buttons & selection */
	cb_selection_changed (NULL, state);
}

static void
cb_item_move (SheetManager *state, gnm_iter_search_t iter_search)
{
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	GtkTreeIter  a, b;
	GList *selected_rows;

	g_return_if_fail (selection != NULL);
	g_return_if_fail (gtk_tree_selection_count_selected_rows (selection) == 1);

	selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				 &a, (GtkTreePath *) selected_rows->data);
	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

	b = a;
	if (!iter_search (GTK_TREE_MODEL (state->model), &b))
		return;

	gtk_list_store_swap (state->model, &a, &b);
	cb_selection_changed (NULL, state);
}

static void
cb_up (G_GNUC_UNUSED GtkWidget *w, SheetManager *state)
{
	cb_item_move (state, gtk_tree_model_iter_previous);
}

static void
cb_down (G_GNUC_UNUSED GtkWidget *w, SheetManager *state)
{
	cb_item_move (state, gtk_tree_model_iter_next);
}

static void
cb_add_clicked (G_GNUC_UNUSED GtkWidget *ignore, SheetManager *state)
{
	GtkTreeIter sel_iter, iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	GList *selected_rows;
	int index = -1;
	WorkbookSheetState *old_state;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	Sheet *sheet, *old_sheet = NULL;

	g_return_if_fail (selection != NULL);
	g_return_if_fail (gtk_tree_selection_count_selected_rows (selection) == 1);

	selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				 &sel_iter, (GtkTreePath *) selected_rows->data);
	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &sel_iter,
			    SHEET_POINTER, &old_sheet,
			    -1);
	index = old_sheet->index_in_wb;

	workbook_signals_block (state);

	old_state = workbook_sheet_state_new (wb);
	workbook_sheet_add (wb, index,
			    gnm_sheet_get_max_cols (old_sheet),
			    gnm_sheet_get_max_rows (old_sheet));
	cmd_reorganize_sheets (wbc, old_state, NULL);
	update_undo (state, wbc);

	workbook_signals_unblock (state);

	g_signal_handler_block (state->model, state->model_row_insertion_listener);
	sheet = workbook_sheet_by_index (wb, index);
	gtk_list_store_insert_before (state->model, &iter, &sel_iter);
	g_signal_handler_unblock (state->model, state->model_row_insertion_listener);

	set_sheet_info_at_iter (state, &iter, sheet);

	cb_selection_changed (NULL, state);
}

static void
cb_append_clicked (G_GNUC_UNUSED GtkWidget *ignore, SheetManager *state)
{
	WorkbookSheetState *old_state;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	GtkTreeIter iter;
	Sheet *sheet, *old_sheet;

	workbook_signals_block (state);

	old_state = workbook_sheet_state_new (wb);
	old_sheet = workbook_sheet_by_index (wb, 0);
	workbook_sheet_add (wb, -1,
			    gnm_sheet_get_max_cols (old_sheet),
			    gnm_sheet_get_max_rows (old_sheet));
	cmd_reorganize_sheets (wbc, old_state, NULL);
	update_undo (state, wbc);

	workbook_signals_unblock (state);

	sheet = workbook_sheet_by_index (wb, workbook_sheet_count (wb) - 1);

	g_signal_handler_block (state->model, state->model_row_insertion_listener);
	gtk_list_store_append (state->model, &iter);
	g_signal_handler_unblock (state->model, state->model_row_insertion_listener);

	set_sheet_info_at_iter (state, &iter, sheet);

	cb_selection_changed (NULL, state);
}

static void
cb_duplicate_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		      SheetManager *state)
{
	GtkTreeIter sel_iter, iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	GList *selected_rows;
	WorkbookSheetState *old_state;
	int index;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	Sheet *new_sheet, *this_sheet;

	g_return_if_fail (selection != NULL);
	g_return_if_fail (gtk_tree_selection_count_selected_rows (selection) == 1);

	selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model),
				 &sel_iter, (GtkTreePath *) selected_rows->data);
	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

	gtk_tree_model_get (GTK_TREE_MODEL (state->model), &sel_iter,
			    SHEET_POINTER, &this_sheet,
			    -1);

	workbook_signals_block (state);

	old_state = workbook_sheet_state_new (wb);
	index = this_sheet->index_in_wb;
	new_sheet = sheet_dup (this_sheet);
	workbook_sheet_attach_at_pos (wb, new_sheet, index + 1);
	g_signal_emit_by_name (G_OBJECT (wb), "sheet_added", 0);
	cmd_reorganize_sheets (wbc, old_state, NULL);
	update_undo (state, wbc);

	workbook_signals_unblock (state);

	g_signal_handler_block (state->model, state->model_row_insertion_listener);
	gtk_list_store_insert_after (state->model, &iter, &sel_iter);
	g_signal_handler_unblock (state->model, state->model_row_insertion_listener);

	set_sheet_info_at_iter (state, &iter, new_sheet);
	g_object_unref (new_sheet);

	cb_selection_changed (NULL, state);
}

static void
cb_delete_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		   SheetManager *state)
{
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheet_list);
	GList *selected_rows, *l;
	WorkbookSheetState *old_state;
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);

	g_return_if_fail (selection != NULL);

	selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);

	for (l = selected_rows; l != NULL; l = l->next)
		l->data = gtk_tree_row_reference_new (GTK_TREE_MODEL (state->model),
						      (GtkTreePath *) l->data);
	workbook_signals_block (state);
	old_state = workbook_sheet_state_new (wb);

	for (l = selected_rows; l != NULL; l = l->next) {
		GtkTreeRowReference *ref = l->data;
		if (gtk_tree_row_reference_valid (ref)) {
			GtkTreePath *path = gtk_tree_row_reference_get_path (ref);
			GtkTreeIter sel_iter;
			Sheet *sheet;

			gtk_tree_model_get_iter (GTK_TREE_MODEL (state->model), &sel_iter, path);
			gtk_tree_path_free (path);
			gtk_tree_model_get (GTK_TREE_MODEL (state->model), &sel_iter,
					    SHEET_POINTER, &sheet,
					    -1);
			gtk_list_store_remove (state->model, &sel_iter);
			workbook_sheet_delete (sheet);
		}
	}

	cmd_reorganize_sheets (wbc, old_state, NULL);
	update_undo (state, wbc);
	workbook_signals_unblock (state);

	populate_sheet_list (state);
	cb_name_edited (NULL, NULL, NULL, state);

	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_row_reference_free);
}

static void
cb_cancel_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		   SheetManager *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static char *
verify_validity (SheetManager *state, gboolean *pchanged)
{
	char *result = NULL;
	gboolean changed = FALSE;
	GHashTable *names = g_hash_table_new_full (g_str_hash, g_str_equal,
						   (GDestroyNotify)g_free, NULL);
	GtkTreeIter this_iter;
	gint n = 0;

	while (result == NULL &&
	       gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, n)) {
		Sheet *this_sheet;
		char *old_name, *new_name, *new_name2;

		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter,
				    SHEET_POINTER, &this_sheet,
				    SHEET_NAME, &old_name,
				    SHEET_NEW_NAME, &new_name,
				    -1);

		new_name2 = g_utf8_casefold (*new_name != 0 ? new_name : old_name, -1);
		if (g_hash_table_lookup (names, new_name2)) {
			result = g_strdup_printf (_("You may not call more than one sheet \"%s\"."),
						  *new_name != 0 ? new_name : old_name);
			g_free (new_name2);
		} else
			g_hash_table_insert (names, new_name2, new_name2);

		if (*new_name && strcmp (old_name, new_name))
				changed = TRUE;

		g_free (old_name);
		g_free (new_name);
		n++;
	}

	g_hash_table_destroy (names);
	*pchanged = changed;
	return result;
}


static void
cb_apply_names_clicked (G_GNUC_UNUSED GtkWidget *ignore, SheetManager *state)
{
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	WorkbookSheetState *old_state;
	GtkTreeIter this_iter;
	gint n = 0;

	/* Stop listening to changes in the sheet order. */
	workbook_signals_block (state);

	old_state = workbook_sheet_state_new (wb);
	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, n)) {
		Sheet *this_sheet;
		char *new_name;

		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter,
				    SHEET_POINTER, &this_sheet,
				    SHEET_NEW_NAME, &new_name,
				    -1);

		if (*new_name) {
			g_object_set (this_sheet,
				      "name", new_name,
				      NULL);
			gtk_list_store_set (state->model, &this_iter,
					    SHEET_NAME, new_name,
					    SHEET_NEW_NAME, "",
					    -1);
		}

		g_free (new_name);
		n++;
	}

	cmd_reorganize_sheets (wbc, old_state, NULL);
	gtk_label_set_text (GTK_LABEL (state->warning), "");
	update_undo (state, wbc);

	workbook_signals_unblock (state);
}

static void
cb_sheet_order_destroy (SheetManager *state)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (state->wbcg));

	/* Stop to listen to changes in the sheet order. */
	if (state->sheet_order_changed_listener)
		g_signal_handler_disconnect (G_OBJECT (wb),
					     state->sheet_order_changed_listener);
	if (state->sheet_added_listener)
		g_signal_handler_disconnect (G_OBJECT (wb),
					     state->sheet_added_listener);
	if (state->sheet_deleted_listener)
		g_signal_handler_disconnect (G_OBJECT (wb),
					     state->sheet_deleted_listener);

	if (state->model != NULL) {
		g_object_unref (state->model);
		state->model = NULL;
	}
	g_object_unref (state->gui);
	g_object_set_data (G_OBJECT (wb), SHEET_ORDER_KEY, NULL);
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

	g_free (state);
}

static void
dialog_sheet_order_update_sheet_order (SheetManager *state)
{
	GtkTreeIter iter;
	Workbook *wb = wb_control_get_workbook (GNM_WBC (state->wbcg));
	int i, n_sheets, n_children;
	GtkTreeModel *model = GTK_TREE_MODEL (state->model);
	GtkTreeSelection *sel = gtk_tree_view_get_selection (state->sheet_list);

	n_sheets = workbook_sheet_count (wb);
	n_children = gtk_tree_model_iter_n_children (model, NULL);

	if (n_sheets != n_children) {
	  /* This signal also occurs when sheets are added or deleted. We handle this */
	  /* when those signals arrive.                                               */
	  return;
	}

	for (i = 0; i < n_sheets; i++) {
		gchar *name, *new_name;
		gboolean is_locked;
		gboolean is_visible;
		gboolean is_rtl;
		GdkRGBA *back, *fore;
		Sheet *sheet_wb = workbook_sheet_by_index (wb, i);
		Sheet *sheet_model;
		gboolean selected;
		int j, row_max, col_max;

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
				    SHEET_ROW_MAX, &row_max,
				    SHEET_COL_MAX, &col_max,
				    SHEET_NAME, &name,
				    SHEET_NEW_NAME, &new_name,
				    SHEET_POINTER, &sheet_model,
				    BACKGROUND_COLOUR, &back,
				    FOREGROUND_COLOUR, &fore,
				    SHEET_DIRECTION, &is_rtl,
				    -1);
		gtk_list_store_remove (state->model, &iter);
		g_signal_handler_block (state->model, state->model_row_insertion_listener);
		gtk_list_store_insert (state->model, &iter, i);
		g_signal_handler_unblock (state->model, state->model_row_insertion_listener);
		gtk_list_store_set (state->model, &iter,
				    SHEET_LOCKED, is_locked,
				    SHEET_LOCK_IMAGE, is_locked ?
				    state->image_padlock : state->image_padlock_no,
				    SHEET_VISIBLE, is_visible,
				    SHEET_VISIBLE_IMAGE, is_visible ?
				    state->image_visible : NULL,
				    SHEET_ROW_MAX, row_max,
				    SHEET_COL_MAX, col_max,
				    SHEET_NAME, name,
				    SHEET_NEW_NAME, new_name,
				    SHEET_POINTER, sheet_model,
				    BACKGROUND_COLOUR, back,
				    FOREGROUND_COLOUR, fore,
				    SHEET_DIRECTION, is_rtl,
				    SHEET_DIRECTION_IMAGE,
					    is_rtl ? state->image_rtl : state->image_ltr,
				    -1);
		if (back)
			gdk_rgba_free (back);
		if (fore)
			gdk_rgba_free (fore);
		g_free (name);
		g_free (new_name);
		if (selected)
			gtk_tree_selection_select_iter (sel, &iter);
	}

	cb_selection_changed (NULL, state);
}

static void
cb_sheet_order_changed (Workbook *wb, SheetManager *state)
{
	dialog_sheet_order_update_sheet_order (state);
}

static void
cb_sheet_deleted (Workbook *wb, SheetManager *state)
{
	populate_sheet_list (state);
}

static void
cb_sheet_added (Workbook *wb, SheetManager *state)
{
	populate_sheet_list (state);
}



static void
dialog_sheet_order_changed (SheetManager *state)
{
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	WorkbookSheetState *old_state;
	GtkTreeIter this_iter;
	gint n = 0, changes = 0;

	workbook_signals_block (state);

	old_state = workbook_sheet_state_new (wb);
	while (gtk_tree_model_iter_nth_child  (GTK_TREE_MODEL (state->model),
					       &this_iter, NULL, n)) {
		Sheet *this_sheet;
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &this_iter,
				    SHEET_POINTER, &this_sheet,
				    -1);
		if (this_sheet->index_in_wb != n) {
			changes++;
			workbook_sheet_move (this_sheet, n - this_sheet->index_in_wb);
		}
		n++;
	}

	if (changes > 0) {
		cmd_reorganize_sheets (wbc, old_state, NULL);
		update_undo (state, wbc);
	} else
		workbook_sheet_state_unref (old_state);

	workbook_signals_unblock (state);
}

static void
cb_dialog_order_changed (G_GNUC_UNUSED GtkListStore *model,
			 G_GNUC_UNUSED GtkTreePath  *path,
			 G_GNUC_UNUSED GtkTreeIter  *iter,
			 G_GNUC_UNUSED gpointer arg3,
			 SheetManager *state)
{
	dialog_sheet_order_changed (state);
}

static gboolean
dialog_sheet_order_changed_idle_handler (SheetManager *state)
{
	dialog_sheet_order_changed (state);
	return FALSE;
}


static void
cb_dialog_order_changed_by_insertion (G_GNUC_UNUSED GtkListStore *model,
			 G_GNUC_UNUSED GtkTreePath  *path,
			 G_GNUC_UNUSED GtkTreeIter  *iter,
			 SheetManager *state)
{
	g_idle_add_full (G_PRIORITY_HIGH_IDLE,
			 (GSourceFunc)dialog_sheet_order_changed_idle_handler,
			 state, NULL);
}

static void
cb_undo_clicked (G_GNUC_UNUSED GtkWidget *ignore, SheetManager *state)
{
	WorkbookControl *wbc = GNM_WBC (state->wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);

	command_undo (wbc);
	gtk_widget_set_sensitive (state->undo_btn, wb->undo_commands != NULL);

	populate_sheet_list (state);
}

static void
cb_adv_check_toggled (G_GNUC_UNUSED GtkToggleButton *ignored,
		      SheetManager *state)
{
	gboolean visible = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (state->advanced_check));

	gtk_tree_view_column_set_visible (state->dir_column, visible);
	gtk_tree_view_column_set_visible (state->col_max_column, visible);
	gtk_tree_view_column_set_visible (state->row_max_column, visible);
}

static void
destroy_cb (GObject *obj)
{
	g_object_set_data (obj, "state", NULL);
}

void
dialog_sheet_order (WBCGtk *wbcg)
{
	SheetManager *state;
	GtkBuilder *gui;
	GtkGrid *grid;
	GOColorGroup *cg;
	Workbook *wb;
	GtkWidget *widget;
	GdkPixbuf *icon;

	g_return_if_fail (wbcg != NULL);

	widget = GTK_WIDGET (wbcg_toplevel (wbcg));

	gui = gnm_gtk_builder_load ("res:ui/sheet-order.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	wb = wb_control_get_workbook (GNM_WBC (wbcg));
	if (g_object_get_data (G_OBJECT (wb), SHEET_ORDER_KEY)) {
		GtkWidget *dialog = gtk_message_dialog_new
			(wbcg_toplevel (wbcg),
			 GTK_DIALOG_DESTROY_WITH_PARENT,
			 GTK_MESSAGE_WARNING,
			 GTK_BUTTONS_CLOSE,
			 _("Another view is already managing sheets"));
		go_gtk_dialog_run (GTK_DIALOG (dialog), wbcg_toplevel (wbcg));
		return;
	}
	g_object_set_data (G_OBJECT (wb), SHEET_ORDER_KEY, (gpointer) gui);
	state = g_new0 (SheetManager, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->dialog     = go_gtk_builder_get_widget (gui, "sheet-order-dialog");
	state->warning     = go_gtk_builder_get_widget (gui, "warning");
	state->up_btn     = go_gtk_builder_get_widget (gui, "up_button");
	state->down_btn   = go_gtk_builder_get_widget (gui, "down_button");
	state->add_btn   = go_gtk_builder_get_widget (gui, "add_button");
	state->append_btn   = go_gtk_builder_get_widget (gui, "append_button");
	state->duplicate_btn   = go_gtk_builder_get_widget (gui, "duplicate_button");
	state->delete_btn   = go_gtk_builder_get_widget (gui, "delete_button");

	state->apply_names_btn  = go_gtk_builder_get_widget (gui, "ok_button");
	state->sort_asc_btn  = go_gtk_builder_get_widget (gui, "sort-asc-button");
	state->sort_desc_btn  = go_gtk_builder_get_widget (gui, "sort-desc-button");
	state->undo_btn  = go_gtk_builder_get_widget (gui, "undo-button");
	state->cancel_btn  = go_gtk_builder_get_widget (gui, "cancel_button");
	state->advanced_check  = go_gtk_builder_get_widget (gui, "advanced-check");
	state->initial_colors_set = FALSE;
	state->image_padlock = go_gtk_widget_render_icon_pixbuf (widget, "gnumeric-protection-yes", GTK_ICON_SIZE_MENU);
	state->image_padlock_no = go_gtk_widget_render_icon_pixbuf (widget, "gnumeric-protection-no", GTK_ICON_SIZE_MENU);
	state->image_visible = go_gtk_widget_render_icon_pixbuf (widget, "gnumeric-visible", GTK_ICON_SIZE_MENU);
	state->image_ltr = go_gtk_widget_render_icon_pixbuf (widget, "format-text-direction-ltr", GTK_ICON_SIZE_MENU);
	state->image_rtl = go_gtk_widget_render_icon_pixbuf (widget, "format-text-direction-rtl", GTK_ICON_SIZE_MENU);
	/* Listen for changes in the sheet order. */
	state->sheet_order_changed_listener = g_signal_connect (G_OBJECT (wb),
		"sheet_order_changed", G_CALLBACK (cb_sheet_order_changed),
		state);
	state->sheet_added_listener = g_signal_connect (G_OBJECT (wb),
		"sheet_added", G_CALLBACK (cb_sheet_added),
		state);
	state->sheet_deleted_listener = g_signal_connect (G_OBJECT (wb),
		"sheet_deleted", G_CALLBACK (cb_sheet_deleted),
		state);

	grid = GTK_GRID (go_gtk_builder_get_widget (gui,"main-grid"));
	cg = go_color_group_fetch ("back_color_group",
		wb_control_view (GNM_WBC (wbcg)));
	icon = go_gtk_widget_render_icon_pixbuf (widget, "gnumeric-bucket", GTK_ICON_SIZE_LARGE_TOOLBAR);
	state->ccombo_back = go_combo_color_new (icon, _("Default"), 0, cg);
	g_object_unref (icon);
	g_object_unref (cg);
	go_combo_color_set_instant_apply (
		GO_COMBO_COLOR (state->ccombo_back), TRUE);
	gtk_grid_attach (grid, state->ccombo_back, 1, 4, 1, 1);
	gtk_widget_set_sensitive (state->ccombo_back, FALSE);

	cg = go_color_group_fetch ("fore_color_group",
		wb_control_view (GNM_WBC (wbcg)));
	icon = go_gtk_widget_render_icon_pixbuf (widget, "font", GTK_ICON_SIZE_LARGE_TOOLBAR);
	state->ccombo_fore = go_combo_color_new (icon, _("Default"), 0, cg);
	g_object_unref (icon);
	g_object_unref (cg);
	go_combo_color_set_instant_apply (
		GO_COMBO_COLOR (state->ccombo_fore), TRUE);
	gtk_grid_attach (grid, state->ccombo_fore, 2, 4, 1, 1);
	gtk_widget_set_sensitive (state->ccombo_fore, FALSE);

	create_sheet_list (state);
	populate_sheet_list (state);

#define CONNECT(o,s,c) g_signal_connect(G_OBJECT(o),s,G_CALLBACK(c),state)
	CONNECT (state->up_btn, "clicked", cb_up);
	CONNECT (state->down_btn, "clicked", cb_down);
	CONNECT (state->sort_asc_btn, "clicked", cb_asc);
	CONNECT (state->sort_desc_btn, "clicked", cb_desc);
	CONNECT (state->add_btn, "clicked", cb_add_clicked);
	CONNECT (state->append_btn, "clicked", cb_append_clicked);
	CONNECT (state->duplicate_btn, "clicked", cb_duplicate_clicked);
	CONNECT (state->delete_btn, "clicked", cb_delete_clicked);
	CONNECT (state->apply_names_btn, "clicked", cb_apply_names_clicked);
	CONNECT (state->cancel_btn, "clicked", cb_cancel_clicked);
	CONNECT (state->undo_btn, "clicked", cb_undo_clicked);
	CONNECT (state->advanced_check, "toggled", cb_adv_check_toggled);
	CONNECT (state->ccombo_back, "color_changed", cb_color_changed_back);
	CONNECT (state->ccombo_fore, "color_changed", cb_color_changed_fore);
	CONNECT (state->model, "rows-reordered", cb_dialog_order_changed);
	state->model_row_insertion_listener =
		CONNECT (state->model, "row-inserted", cb_dialog_order_changed_by_insertion);
#undef CONNECT

	cb_adv_check_toggled (NULL, state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SHEET_MANAGER);

	gtk_widget_set_sensitive (state->undo_btn, wb->undo_commands != NULL);
	gtk_widget_set_sensitive (state->apply_names_btn, FALSE);

	/* a candidate for merging into attach guru */
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_sheet_order_destroy);
	g_signal_connect (G_OBJECT (state->dialog), "destroy", G_CALLBACK (destroy_cb), NULL);

	gnm_restore_window_geometry (GTK_WINDOW (state->dialog),
					  SHEET_ORDER_KEY);

	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
