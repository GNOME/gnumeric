/*
 * workbook.c:  Workbook management (toplevel windows)
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org).
 *
 * (C) 1998, 1999, 2000 Miguel de Icaza
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "application.h"
#include "eval.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gnumeric-util.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "main.h"
#include "file.h"
#include "xml-io.h"
#include "pixmaps.h"
#include "clipboard.h"
#include "parse-util.h"
#include "widgets/widget-editable-label.h"
#include "ranges.h"
#include "selection.h"
#include "print.h"
#include "expr-name.h"
#include "value.h"
#include "widgets/gnumeric-toolbar.h"
#include "workbook-cmd-format.h"
#include "workbook-format-toolbar.h"
#include "workbook-view.h"
#include "command-context-gui.h"
#include "commands.h"
#include "widgets/gtk-combo-text.h"
#include "widgets/gtk-combo-stack.h"
#include "gutils.h"
#include "rendered-value.h"
#include "cmd-edit.h"
#include "format.h"

#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-persist-file.h>
#include "sheet-object-container.h"
#include "embeddable-grid.h"
#endif

#include <ctype.h>

#include "workbook-private.h"

/* Persistent attribute ids */
enum {
	ARG_VIEW_HSCROLLBAR = 1,
	ARG_VIEW_VSCROLLBAR,
	ARG_VIEW_TABS
};

/* The locations within the main table in the workbook */
#define WB_EA_LINE   0
#define WB_EA_SHEETS 1
#define WB_EA_STATUS 2

#define WB_COLS      1

static int workbook_count;

static GList *workbook_list = NULL;

static GtkObject *workbook_parent_class;

/* Workbook signals */
enum {
	SHEET_ENTERED,
	CELL_CHANGED,
	LAST_SIGNAL
};

static gint workbook_signals [LAST_SIGNAL] = {
	0, /* SHEET_ENTERED, CELL_CHANGED */
};

static void workbook_set_arg (GtkObject *object, GtkArg *arg, guint arg_id);
static void workbook_get_arg (GtkObject *object, GtkArg *arg, guint arg_id);
static void workbook_set_focus (GtkWindow *window, GtkWidget *focus, Workbook *wb);
static gboolean workbook_close_if_user_permits (Workbook *wb);

static void
new_cmd (void)
{
	Workbook *wb;
	wb = workbook_new_with_sheets (1);
	gtk_widget_show (wb->toplevel);
}

static void
file_open_cmd (GtkWidget *widget, Workbook *wb)
{
	char *fname = dialog_query_load_file (wb);
	Workbook *new_wb;

	if (!fname)
		return;

	new_wb = workbook_read (workbook_command_context_gui (wb), fname);

	if (new_wb != NULL) {
		gtk_widget_show (new_wb->toplevel);

		/*
		 * If the current workbook is empty and untouched remove it
		 * in favour of the new book
		 */
		if (workbook_is_pristine (wb))
			workbook_unref (wb);
	}
	g_free (fname);
}

static void
file_import_cmd (GtkWidget *widget, Workbook *wb)
{
	char *fname = dialog_query_load_file (wb);
	Workbook *new_wb;

	if (!fname)
		return;

	new_wb = workbook_import (workbook_command_context_gui (wb), wb,
				  fname);
	if (new_wb) {
		gtk_widget_show (new_wb->toplevel);

		if (workbook_is_pristine (wb))
			workbook_unref (wb);
	}
	g_free (fname);
}

static void
file_save_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_save (workbook_command_context_gui (wb), wb);
}

static void
file_save_as_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_save_as (workbook_command_context_gui (wb), wb);
}

static void
summary_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_summary_update (wb, wb->summary_info);
}

static void
autocorrect_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_autocorrect (wb);
}

static void
autoformat_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_autoformat (wb);
}

static void
autosave_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_autosave (wb);
}

static void
advanced_filter_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_advanced_filter (wb);
}

static void
plugins_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_plugin_manager (wb);
}

#ifndef ENABLE_BONOBO
static void
about_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_about (wb);
}

#else
static void
create_embedded_component_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;

	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_COMPONENT);
}

static void
create_embedded_item_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;

	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_CANVAS_ITEM);
}

static void
launch_graph_guru (GtkWidget *widget, Workbook *wb)
{
	dialog_graph_guru (wb);
}
#endif

#ifdef GNUMERIC_TEST_ACTIVE_OBJECT
static void
create_button_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_BUTTON);
}

static void
create_checkbox_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_CHECKBOX);
}
#endif

static void
create_line_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_LINE);
}

static void
create_arrow_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_ARROW);
}

static void
create_rectangle_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_BOX);
}

static void
create_ellipse_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_OVAL);
}

static void
cb_sheet_destroy_contents (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	sheet_destroy_contents (sheet);
}

static void
sheet_order_cmd (GtkWidget *widget, Workbook *wb)
{
        dialog_sheet_order (wb);
}

static void
workbook_do_destroy (Workbook *wb)
{
	gtk_signal_disconnect_by_func (
		GTK_OBJECT (wb->toplevel),
		GTK_SIGNAL_FUNC (workbook_set_focus), wb);

	wb->priv->during_destruction = TRUE;

	workbook_auto_complete_destroy (wb);
	workbook_autosave_cancel (wb);

	if (wb->file_format_level > FILE_FL_NEW)
		workbook_view_history_update (workbook_list,
					      wb->filename);

	/*
	 * Do all deletions that leave the workbook in a working
	 * order.
	 */

	summary_info_free (wb->summary_info);
	wb->summary_info = NULL;

	command_list_release (wb->undo_commands);
	command_list_release (wb->redo_commands);
	wb->undo_commands = NULL;
	wb->redo_commands = NULL;

	workbook_deps_destroy (wb);
	expr_name_invalidate_refs_wb (wb);

	/*
	 * All formulas are going to be removed.  Unqueue them before removing
	 * the cells so that we need not search the lists.
	 */
	g_list_free (wb->dependents);
	wb->dependents = NULL;

	/* Just drop the eval queue.  */
	g_list_free (wb->eval_queue);
	wb->eval_queue = NULL;

	/* Erase all cells. */
	g_hash_table_foreach (wb->sheets, &cb_sheet_destroy_contents, NULL);

	if (wb->auto_expr_desc)
		string_unref (wb->auto_expr_desc);
	if (wb->auto_expr) {
		expr_tree_unref (wb->auto_expr);
		wb->auto_expr = NULL;
	}

	gtk_window_set_focus (GTK_WINDOW (wb->toplevel), NULL);
	/* Detach and destroy all sheets.  */
	{
		GList *sheets, *l;

		sheets = workbook_sheets (wb);
		for (l = sheets; l; l = l->next){
			Sheet *sheet = l->data;

			/*
			 * We need to put this test BEFORE we detach
			 * the sheet from the workbook.  Its ugly, but should
			 * be ok for debug code.
			 */
			if (gnumeric_debugging > 0)
				sheet_dump_dependencies (sheet);
			/*
			 * Make sure we alwayws keep the focus on an
			 * existing widget (otherwise the destruction
			 * code for wb->toplvel will try to focus a
			 * dead widget at the end of this routine)
			 */
			gtk_window_set_focus (GTK_WINDOW (wb->toplevel), NULL);

			workbook_detach_sheet (sheet->workbook, sheet, TRUE);

			gtk_window_set_focus (GTK_WINDOW (wb->toplevel), NULL);
		}
		g_list_free (sheets);
	}

	/* Remove ourselves from the list of workbooks.  */
	workbook_list = g_list_remove (workbook_list, wb);
	workbook_count--;

	/* Now do deletions that will put this workbook into a weird
	   state.  Careful here.  */

	g_hash_table_destroy (wb->sheets);

	if (wb->filename)
	       g_free (wb->filename);

	wb->names = expr_name_list_destroy (wb->names);

	gtk_object_unref (GTK_OBJECT (wb->priv->gui_context));

	workbook_private_delete (wb->priv);

	if (!GTK_OBJECT_DESTROYED (wb->toplevel))
		gtk_object_destroy (GTK_OBJECT (wb->toplevel));

	if (initial_worbook_open_complete && workbook_count == 0) {
		application_history_write_config ();
		gtk_main_quit ();
	}
}

static void
workbook_destroy (GtkObject *wb_object)
{
	Workbook *wb = WORKBOOK (wb_object);

	workbook_do_destroy (wb);
	GTK_OBJECT_CLASS (workbook_parent_class)->destroy (wb_object);
}

static int
workbook_delete_event (GtkWidget *widget, GdkEvent *event, Workbook *wb)
{
	return workbook_close_if_user_permits (wb);
}

static void
cb_sheet_mark_dirty (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	int dirty = GPOINTER_TO_INT (user_data);

	sheet_set_dirty (sheet, dirty);
}

void
workbook_set_dirty (Workbook *wb, gboolean is_dirty)
{
	g_return_if_fail (wb != NULL);

	g_hash_table_foreach (wb->sheets, cb_sheet_mark_dirty, GINT_TO_POINTER (is_dirty));
}

static void
cb_sheet_check_dirty (gpointer key, gpointer value, gpointer user_data)
{
	Sheet    *sheet = value;
	gboolean *dirty = user_data;

	if (*dirty)
		return;

	if (!sheet->modified)
		return;

	*dirty = TRUE;
}

gboolean
workbook_is_dirty (Workbook *wb)
{
	gboolean dirty = FALSE;

	g_return_val_if_fail (wb != NULL, FALSE);

	g_hash_table_foreach (wb->sheets, cb_sheet_check_dirty,
			      &dirty);

	return dirty;
}

static void
cb_sheet_check_pristine (gpointer key, gpointer value, gpointer user_data)
{
	Sheet    *sheet = value;
	gboolean *pristine = user_data;

	if (!sheet_is_pristine (sheet))
		*pristine = FALSE;
}

/**
 * workbook_is_pristine:
 * @wb:
 *
 *   This checks to see if the workbook has ever been
 * used ( approximately )
 *
 * Return value: TRUE if we can discard this workbook.
 **/
gboolean
workbook_is_pristine (Workbook *wb)
{
	gboolean pristine = TRUE;

	g_return_val_if_fail (wb != NULL, FALSE);

	if (workbook_is_dirty (wb))
		return FALSE;

	if (wb->names || wb->dependents ||
#ifdef ENABLE_BONOBO
	    wb->priv->workbook_views ||
#endif
	    wb->eval_queue || (wb->file_format_level > FILE_FL_NEW))
		return FALSE;

	/* Check if we seem to contain anything */
	g_hash_table_foreach (wb->sheets, cb_sheet_check_pristine,
			      &pristine);

	return pristine;
}

/**
 * workbook_close_if_user_permits : If the workbook is dirty the user is
 *  		prompted to see if they should exit.
 *
 * Returns : TRUE is the book remains open.
 *           FALSE if it is closed.
 */
static gboolean
workbook_close_if_user_permits (Workbook *wb)
{
	gboolean   can_close = TRUE;
	gboolean   done      = FALSE;
	int        iteration = 0;
	static int in_can_close;

	if (in_can_close)
		return FALSE;
	in_can_close = TRUE;

	/* If we were editing when the quit request came in save the edit. */
	workbook_finish_editing (wb, TRUE);

	while (workbook_is_dirty (wb) && !done) {

		GtkWidget *d, *l, *cancel_button;
		int button;
		char *s;

		iteration++;

		d = gnome_dialog_new (
			_("Warning"),
			GNOME_STOCK_BUTTON_YES,
			GNOME_STOCK_BUTTON_NO,
			GNOME_STOCK_BUTTON_CANCEL,
			NULL);
		cancel_button = g_list_last (GNOME_DIALOG (d)->buttons)->data;
		gtk_widget_grab_focus (cancel_button);
		gnome_dialog_set_parent (GNOME_DIALOG (d), GTK_WINDOW (wb->toplevel));

		if (wb->filename)
			s = g_strdup_printf (
				_("Workbook %s has unsaved changes, save them?"),
				g_basename (wb->filename));
		else
			s = g_strdup (_("Workbook has unsaved changes, save them?"));

		l = gtk_label_new (s);
		gtk_widget_show (l);
		g_free (s);

		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (d)->vbox), l, TRUE, TRUE, 0);

		gtk_window_set_position (GTK_WINDOW (d), GTK_WIN_POS_MOUSE);
		button = gnome_dialog_run_and_close (GNOME_DIALOG (d));

		switch (button) {
			/* YES */
		case 0:
			done = workbook_save (workbook_command_context_gui (wb), wb);
			break;

			/* NO */
		case 1:
			can_close = TRUE;
			done      = TRUE;
			workbook_set_dirty (wb, FALSE);
			break;

			/* CANCEL */
		case -1:
		case 2:
			can_close = FALSE;
			done      = TRUE;
			break;
		}
	}

	in_can_close = FALSE;

	if (can_close) {
		workbook_unref (wb);
		return FALSE;
	} else
		return TRUE;
}

static void
close_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_close_if_user_permits (wb);
}

static void
quit_cmd (void)
{
	GList *l, *n = NULL;

	/*
	 * Duplicate the list as the workbook_list is modified during
	 * workbook destruction
	 */
	for (l = workbook_list; l; l = l->next)
		n = g_list_prepend (n, l->data);

	for (l = n; l; l = l->next){
		Workbook *wb = l->data;

		workbook_close_if_user_permits (wb);
	}

	g_list_free (n);
}

static gboolean
cb_editline_focus_in (GtkWidget *w, GdkEventFocus *event, Workbook *wb)
{
	if (!wb->editing)
		workbook_start_editing_at_cursor (wb, FALSE, TRUE);

	return TRUE;
}

static void
accept_input (GtkWidget *IGNORED, Workbook *wb)
{
	workbook_finish_editing (wb, TRUE);
}

static void
cancel_input (GtkWidget *IGNORED, Workbook *wb)
{
	workbook_finish_editing (wb, FALSE);
}

static void
undo_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_finish_editing (wb, FALSE);
	command_undo (workbook_command_context_gui (wb), wb);
}

static void
undo_combo_cmd (GtkWidget *widget, gint num, Workbook *wb)
{
	int i;
	
	workbook_finish_editing (wb, FALSE);
	for (i = 0; i < num; i++)
		command_undo (workbook_command_context_gui (wb), wb);
}

static void
redo_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_finish_editing (wb, FALSE);
	command_redo (workbook_command_context_gui (wb), wb);
}

static void
redo_combo_cmd (GtkWidget *widget, gint num, Workbook *wb)
{
	int i;
	
	workbook_finish_editing (wb, FALSE);
	for (i = 0; i < num; i++)
		command_redo (workbook_command_context_gui (wb), wb);
}

static void
copy_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_selection_copy (workbook_command_context_gui (wb), sheet);
}

static void
cut_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;

	if (sheet->mode == SHEET_MODE_SHEET)
		sheet_selection_cut (workbook_command_context_gui (wb), sheet);
	else {
		if (sheet->current_object){
			gtk_object_unref (GTK_OBJECT (sheet->current_object));
			sheet->current_object = NULL;
			sheet_set_mode_type (sheet, SHEET_MODE_SHEET);
		} else
			printf ("no object selected\n");
	}
}

static void
paste_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	cmd_paste_to_selection (workbook_command_context_gui (wb),
				sheet, PASTE_DEFAULT);
}

static void
paste_special_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	int flags = dialog_paste_special (wb);
	if (flags != 0)
		cmd_paste_to_selection (workbook_command_context_gui (wb),
					sheet, flags);
}

static void
delete_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_delete_cells (wb, wb->current_sheet);
}

static void sheet_action_delete_sheet (GtkWidget *ignored, Sheet *current_sheet);

static void
delete_sheet_cmd (GtkWidget *widget, Workbook *wb)
{
	sheet_action_delete_sheet (widget, wb->current_sheet);
}

/* Callbacks for the selection commands */
static void
cb_edit_select_all (GtkWidget *widget, Workbook *wb)
{
	cmd_select_all (wb->current_sheet);
}
static void
cb_edit_select_row (GtkWidget *widget, Workbook *wb)
{
	cmd_select_cur_row (wb->current_sheet);
}
static void
cb_edit_select_col (GtkWidget *widget, Workbook *wb)
{
	cmd_select_cur_col (wb->current_sheet);
}
static void
cb_edit_select_array (GtkWidget *widget, Workbook *wb)
{
	cmd_select_cur_array (wb->current_sheet);
}
static void
cb_edit_select_depends (GtkWidget *widget, Workbook *wb)
{
	cmd_select_cur_depends (wb->current_sheet);
}

static void
goto_cell_cmd (GtkWidget *unused, Workbook *wb)
{
	dialog_goto_cell (wb);
}

static void
cb_edit_named_expr (GtkWidget *unused, Workbook *wb)
{
	dialog_define_names (wb);
}
#if 0
static void
cb_auto_generate__named_expr (GtkWidget *unused, Workbook *wb)
{
	dialog_auto_generate_names (wb);
}
#endif

static void
insert_sheet_cmd (GtkWidget *unused, Workbook *wb)
{
	Sheet *sheet;
	char *name;

	name = workbook_sheet_get_free_name (wb, _("Sheet"), TRUE, FALSE);
	sheet = sheet_new (wb, name);
	g_free (name);

	workbook_attach_sheet (wb, sheet);
	sheet_set_dirty (sheet, TRUE);
}

static void
insert_cells_cmd (GtkWidget *unused, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_insert_cells (wb, sheet);
}

static void
insert_cols_cmd (GtkWidget *unused, Workbook *wb)
{
	SheetSelection *ss;
	int cols;
	Sheet *sheet = wb->current_sheet;

	/* TODO : No need to check simplicty.  XL applies for each
	 * non-discrete selected region, (use selection_apply) */
	if (!selection_is_simple (workbook_command_context_gui (wb), sheet, _("Insert cols")))
		return;

	ss = sheet->selections->data;

	/* TODO : Have we have selected rows rather than columns
	 * This menu item should be disabled when a full row is selected
	 *
	 * at minimum a warning if things are about to be cleared ?
	 */
	cols = ss->user.end.col - ss->user.start.col + 1;
	cmd_insert_cols (workbook_command_context_gui (wb), sheet,
			 ss->user.start.col, cols);
}

static void
insert_rows_cmd (GtkWidget *unused, Workbook *wb)
{
	SheetSelection *ss;
	int rows;
	Sheet *sheet = wb->current_sheet;

	/* TODO : No need to check simplicty.  XL applies for each
	 * non-discrete selected region, (use selection_apply) */
	if (!selection_is_simple (workbook_command_context_gui (wb), sheet, _("Insert rows")))
		return;

	ss = sheet->selections->data;

	/* TODO : Have we have selected columns rather than rows
	 * This menu item should be disabled when a full column is selected
	 *
	 * at minimum a warning if things are about to be cleared ?
	 */
	rows = ss->user.end.row - ss->user.start.row + 1;
	cmd_insert_rows (workbook_command_context_gui (wb), sheet,
			 ss->user.start.row, rows);
}

static void
clear_all_cmd (GtkWidget *widget, Workbook *wb)
{
	cmd_clear_selection (workbook_command_context_gui (wb),
			     wb->current_sheet,
			     CLEAR_VALUES | CLEAR_FORMATS | CLEAR_COMMENTS);
}

static void
clear_formats_cmd (GtkWidget *widget, Workbook *wb)
{
	cmd_clear_selection (workbook_command_context_gui (wb),
			     wb->current_sheet,
			     CLEAR_FORMATS);
}

static void
clear_comments_cmd (GtkWidget *widget, Workbook *wb)
{
	cmd_clear_selection (workbook_command_context_gui (wb),
			     wb->current_sheet,
			     CLEAR_COMMENTS);
}

static void
clear_content_cmd (GtkWidget *widget, Workbook *wb)
{
	cmd_clear_selection (workbook_command_context_gui (wb),
			     wb->current_sheet,
			     CLEAR_VALUES);
}

static void
zoom_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_zoom (wb, sheet);
}

/***********************************************************************/
/* Sheet preferences */

static void
cb_cell_rerender (gpointer element, gpointer userdata)
{
	Dependent *dep = element;
	if (dep->flags & DEPENDENT_CELL) {
		Cell *cell = DEP_TO_CELL (dep);
		cell_render_value (cell);
		sheet_redraw_cell (cell);
	}
}

static void
cb_sheet_pref_display_formulas (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet->display_formulas = !sheet->display_formulas;
	g_list_foreach (wb->dependents, &cb_cell_rerender, NULL);
}
static void
cb_sheet_pref_hide_zeros (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet->display_zero = ! sheet->display_zero;
	sheet_redraw_all (sheet);
}
static void
cb_sheet_pref_hide_grid_lines (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet->show_grid = !sheet->show_grid;
	sheet_redraw_all (sheet);
}
static void
cb_sheet_pref_hide_col_header (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet->show_col_header = ! sheet->show_col_header;
	sheet_adjust_preferences (sheet);
}
static void
cb_sheet_pref_hide_row_header (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet->show_row_header = ! sheet->show_row_header;
	sheet_adjust_preferences (sheet);
}

static void
format_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_cell_format (wb, sheet);
}

static void
workbook_attr_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_workbook_attr (wb);
}

static void
sort_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_cell_sort (wb, sheet);
}

static void
recalc_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_recalc_all (wb);
	sheet_update (wb->current_sheet);
}

static void
insert_current_date_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	cmd_set_date_time (workbook_command_context_gui (wb),
			   sheet, &sheet->cursor.edit_pos, TRUE);
}

static void
insert_current_time_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	cmd_set_date_time (workbook_command_context_gui (wb),
			   sheet, &sheet->cursor.edit_pos, FALSE);
}

static void
workbook_edit_comment (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	Cell *cell;

	cell = sheet_cell_get (sheet,
			       sheet->cursor.edit_pos.col,
			       sheet->cursor.edit_pos.row);

	if (!cell) {
		cell = sheet_cell_new (sheet,
				       sheet->cursor.edit_pos.col,
				       sheet->cursor.edit_pos.row);
		sheet_cell_set_value (cell, value_new_empty (), NULL);
	}

	dialog_cell_comment (wb, cell);
}

static void
goal_seek_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_goal_seek (wb, sheet);
}

static void
solver_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_solver (wb, sheet);
}

static void
data_analysis_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_data_analysis (wb, sheet);
}

static void
print_setup_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_printer_setup (wb, sheet);
}

static void
file_print_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_print (sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
file_print_preview_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_print (sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static void
sort_cmd (Workbook *wb, int asc)
{
	Sheet *sheet;
	Range *sel;
	SortData *data;
	SortClause *clause;
	int numclause, i;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->current_sheet != NULL);

	sheet = wb->current_sheet;
	sel = range_copy (selection_first_range (sheet, TRUE));

	/* We can't sort complex ranges */
	if (!selection_is_simple (workbook_command_context_gui (wb),
				  sheet, _("sort")))
		return;

	range_clip_to_finite (sel, sheet);

	numclause = sel->end.col - sel->start.col + 1;
	clause = g_new0 (SortClause, numclause);
	for (i=0; i < numclause; i++) {
		clause[i].offset = i;
		clause[i].asc = asc;
		clause[i].cs = FALSE;
		clause[i].val = TRUE;
	}

	data = g_new (SortData, 1);
	data->sheet = sheet;
	data->range = sel;
	data->num_clause = numclause;
	data->clauses = clause;
	data->top = TRUE;

	if (range_has_header (data->sheet, data->range, TRUE)) {
		data->range->start.row += 1;
	}
	
	cmd_sort (workbook_command_context_gui (wb), data);	
}

static void
cb_autofunction (GtkWidget *widget, Workbook *wb)
{
	GtkEntry *entry;
	gchar const *txt;

	if (wb->editing)
		return;

	entry = workbook_get_entry (wb);
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=", 1)) {
		workbook_start_editing_at_cursor (wb, TRUE, TRUE);
		gtk_entry_set_text (entry, "=");
		gtk_entry_set_position (entry, 1);
	} else {
		workbook_start_editing_at_cursor (wb, FALSE, TRUE);

		/* FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_entry_set_position (entry, entry->text_length-1);
	}
}

static void
autosum_cmd (GtkWidget *widget, Workbook *wb)
{
	GtkEntry *entry;
	gchar *txt;

	if (wb->editing)
		return;

	entry = workbook_get_entry (wb);
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=sum(", 5)) {
		workbook_start_editing_at_cursor (wb, TRUE, TRUE);
		gtk_entry_set_text (entry, "=sum()");
		gtk_entry_set_position (entry, 5);
	} else {
		workbook_start_editing_at_cursor (wb, FALSE, TRUE);

		/*
		 * FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_entry_set_position (entry, entry->text_length-1);
	}
}

static void
formula_guru (GtkWidget *widget, Workbook *wb)
{
	dialog_formula_guru (wb);
}

static void
sort_ascend_cmd (GtkWidget *widget, Workbook *wb)
{
	sort_cmd (wb, 0);
}

static void
sort_descend_cmd (GtkWidget *widget, Workbook *wb)
{
	sort_cmd (wb, 1);
}

#ifdef ENABLE_BONOBO
static void
insert_object_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	char  *obj_id;

	obj_id = bonobo_selector_select_id (
		_("Select an object to add"), NULL);

	if (obj_id != NULL)
		sheet_object_insert (sheet, obj_id);
}
#endif

/* File menu */

static GnomeUIInfo workbook_menu_file [] = {
        GNOMEUIINFO_MENU_NEW_ITEM(N_("_New"), N_("Create a new spreadsheet"),
				  &new_cmd, NULL),

	GNOMEUIINFO_MENU_OPEN_ITEM(file_open_cmd, NULL),
	GNOMEUIINFO_ITEM_STOCK (N_("_Import..."), N_("Imports a file"),
				file_import_cmd, GNOME_STOCK_MENU_OPEN),
	GNOMEUIINFO_MENU_SAVE_ITEM(file_save_cmd, NULL),

	GNOMEUIINFO_MENU_SAVE_AS_ITEM(file_save_as_cmd, NULL),

	GNOMEUIINFO_ITEM_NONE(N_("Su_mmary..."),
			      N_("Summary information"),
			      &summary_cmd),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_PRINT_SETUP_ITEM(print_setup_cmd, NULL),
	GNOMEUIINFO_MENU_PRINT_ITEM(file_print_cmd, NULL),
	GNOMEUIINFO_ITEM(N_("Print pre_view"), N_("Print preview"),
			 &file_print_preview_cmd, preview_xpm),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_CLOSE_ITEM(close_cmd, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_EXIT_ITEM(quit_cmd, NULL),
	GNOMEUIINFO_END
};

/* Edit menu */

static GnomeUIInfo workbook_menu_edit_clear [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_All"),
		N_("Clear the selected cells' formats, comments, and contents"),
		&clear_all_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("_Formats"),
		N_("Clear the selected cells' formats"),
		&clear_formats_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("Co_mments"),
		N_("Clear the selected cells' comments"),
		&clear_comments_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("_Content"),
		N_("Clear the selected cells' contents"),
		&clear_content_cmd),
	GNOMEUIINFO_END
};

#define GNOME_MENU_EDIT_PATH D_("_Edit/")

static GnomeUIInfo workbook_menu_edit_select [] = {
	{ GNOME_APP_UI_ITEM, N_("Select _All"),
	  N_("Select all cells in the spreadsheet"),
	  &cb_edit_select_all, NULL,
	  NULL, 0, 0, 'a', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Row"),
	  N_("Select an entire row"),
	  &cb_edit_select_row, NULL,
	  NULL, 0, 0, ' ', GDK_SHIFT_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Column"),
	  N_("Select an entire column"),
	  &cb_edit_select_col, NULL,
	  NULL, 0, 0, ' ', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select Arra_y"),
	  N_("Select an entire column"),
	  &cb_edit_select_array, NULL,
	  NULL, 0, 0, '/', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Depends"),
	  N_("Select all the cells that depend on the current edit cell."),
	  & cb_edit_select_depends, NULL,
	  NULL, 0, 0, 0, 0 },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_edit [] = {
	GNOMEUIINFO_MENU_UNDO_ITEM(undo_cmd, NULL),
	GNOMEUIINFO_MENU_REDO_ITEM(redo_cmd, NULL),

	GNOMEUIINFO_SEPARATOR,

        GNOMEUIINFO_MENU_CUT_ITEM(cut_cmd, NULL),
	GNOMEUIINFO_MENU_COPY_ITEM(copy_cmd, NULL),
	GNOMEUIINFO_MENU_PASTE_ITEM(paste_cmd, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE(N_("P_aste special..."),
		N_("Paste with optional filters and transformations"),
		&paste_special_cmd),

	GNOMEUIINFO_SUBTREE(N_("C_lear"), workbook_menu_edit_clear),

	GNOMEUIINFO_ITEM_NONE(N_("_Delete..."),
		N_("Remove selected cells, shifting other into their place"),
		&delete_cells_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("De_lete Sheet"),
		N_("Irrevocably remove an entire sheet"),
		&delete_sheet_cmd),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("_Select..."), workbook_menu_edit_select),

	/* Default <Ctrl-G> to be goto */
	{ GNOME_APP_UI_ITEM, N_("_Goto cell..."),
	  N_("Jump to a specified cell"), &goto_cell_cmd,
	  NULL, NULL, 0, 0, GDK_G, GDK_CONTROL_MASK },

	/* Default <F9> to recalculate */
	{ GNOME_APP_UI_ITEM, N_("_Recalculate"),
	  N_("Recalculate the spreadsheet"), &recalc_cmd,
	  NULL, NULL, 0, 0, GDK_F9, 0 },

	GNOMEUIINFO_END
};

/* View menu */

static GnomeUIInfo workbook_menu_view [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Zoom..."),
		N_("Zoom the spreadsheet in or out"),
		&zoom_cmd),
	GNOMEUIINFO_END
};

/* Insert menu */

static GnomeUIInfo workbook_menu_insert_special [] = {
	/* Default <Ctrl-;> (control semi-colon) to insert the current date */
	{ GNOME_APP_UI_ITEM, N_("Current _date"),
	  N_("Insert the current date into the selected cell(s)"),
	  insert_current_date_cmd,
	  NULL, NULL, 0, 0, ';', GDK_CONTROL_MASK },

	/* Default <Ctrl-:> (control colon) to insert the current time */
	{ GNOME_APP_UI_ITEM, N_("Current _time"),
	  N_("Insert the current time into the selected cell(s)"),
	  insert_current_time_cmd,
	  NULL, NULL, 0, 0, ':', GDK_CONTROL_MASK },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_names [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Names..."),
		N_("Edit sheet and workbook names"),
		&cb_edit_named_expr),
#if 0
	GNOMEUIINFO_ITEM_NONE(N_("_Auto generate names..."),
		N_("Use the current selection to create names"),
		&cb_auto_generate__named_expr),
#endif
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_insert [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Sheet"),
		N_("Insert a new spreadsheet"),
		&insert_sheet_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("_Rows"),
		N_("Insert new rows"),
		&insert_rows_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("_Columns"),
		N_("Insert new columns"),
		&insert_cols_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("C_ells..."),
		N_("Insert new cells"),
		&insert_cells_cmd),

#ifdef ENABLE_BONOBO
	GNOMEUIINFO_ITEM_NONE(N_("_Object..."),
		N_("Inserts a Bonobo object"),
		insert_object_cmd),
#endif

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("N_ames"), workbook_menu_names),

	GNOMEUIINFO_ITEM_NONE(N_("_Add/modify comment..."),
		N_("Edit the selected cell's comment"),
		&workbook_edit_comment),

	GNOMEUIINFO_SUBTREE(N_("S_pecial"), workbook_menu_insert_special),
	GNOMEUIINFO_END
};

/* Format menu */

static GnomeUIInfo workbook_menu_format_column [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Auto fit selection"),
		NULL,
		&workbook_cmd_format_column_auto_fit),
	GNOMEUIINFO_ITEM_NONE(N_("_Width"),
		NULL,
		&workbook_cmd_format_column_width),
	GNOMEUIINFO_ITEM_NONE(N_("_Hide"),
		NULL,
		&workbook_cmd_format_column_hide),
	GNOMEUIINFO_ITEM_NONE(N_("_Unhide"),
		NULL,
		&workbook_cmd_format_column_unhide),
	GNOMEUIINFO_ITEM_NONE(N_("_Standard Width"),
		NULL,
		&workbook_cmd_format_column_std_width),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_row [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Auto fit selection"),
		NULL,
		&workbook_cmd_format_row_auto_fit),
	GNOMEUIINFO_ITEM_NONE(N_("_Height"),
		NULL,
		&workbook_cmd_format_row_height),
	GNOMEUIINFO_ITEM_NONE(N_("_Hide"),
		NULL,
		&workbook_cmd_format_row_hide),
	GNOMEUIINFO_ITEM_NONE(N_("_Unhide"),
		NULL,
		&workbook_cmd_format_row_unhide),
	GNOMEUIINFO_ITEM_NONE(N_("_Standard Height"),
		NULL,
		&workbook_cmd_format_row_std_height),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_sheet [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Change name"),
		NULL,
		&workbook_cmd_format_sheet_change_name),
	GNOMEUIINFO_ITEM_NONE(N_("Re-_Order Sheets"),
		NULL,
		&sheet_order_cmd),
	{
	    GNOME_APP_UI_TOGGLEITEM,
	    N_("Display _Formulas"), NULL,
	    (gpointer)&cb_sheet_pref_display_formulas, NULL, NULL,
	    GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{
	    GNOME_APP_UI_TOGGLEITEM,
	    N_("Hide _Zeros"), NULL,
	    (gpointer)&cb_sheet_pref_hide_zeros, NULL, NULL,
	    GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{
	    GNOME_APP_UI_TOGGLEITEM,
	    N_("Hide _Gridlines"), NULL,
	    (gpointer)&cb_sheet_pref_hide_grid_lines, NULL, NULL,
	    GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{
	    GNOME_APP_UI_TOGGLEITEM,
	    N_("Hide _Column Header"), NULL,
	    (gpointer)&cb_sheet_pref_hide_col_header, NULL, NULL,
	    GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{
	    GNOME_APP_UI_TOGGLEITEM,
	    N_("Hide _Row Header"), NULL,
	    (gpointer)&cb_sheet_pref_hide_row_header, NULL, NULL,
	    GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format [] = {
	/* Default <Ctrl-1> invoke the format dialog */
	{ GNOME_APP_UI_ITEM, N_("_Cells..."),
	  N_("Modify the formatting of the selected cells"),
	  format_cells_cmd, NULL, NULL, 0, 0, GDK_1, GDK_CONTROL_MASK },

	GNOMEUIINFO_SUBTREE(N_("C_olumn"), workbook_menu_format_column),
	GNOMEUIINFO_SUBTREE(N_("_Row"),    workbook_menu_format_row),
	GNOMEUIINFO_SUBTREE(N_("_Sheet"),  workbook_menu_format_sheet),

	GNOMEUIINFO_ITEM_NONE(N_("_Autoformat..."),
			      N_("Format a region of cells according to a pre-defined template"),
			      &autoformat_cmd),
			      
	GNOMEUIINFO_ITEM_NONE(N_("_Workbook..."),
		N_("Modify the workbook attributes"),
		&workbook_attr_cmd),
	GNOMEUIINFO_END
};

/* Tools menu */
static GnomeUIInfo workbook_menu_tools [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Plug-ins..."),
		N_("Manage available plugin modules."),
		&plugins_cmd),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE(N_("Auto _Correct..."),
		N_("Automaticly perform simple spell checking"),
		&autocorrect_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("_Auto Save..."),
		N_("Automaticly save the current document at regular intervals"),
		&autosave_cmd),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE(N_("_Goal Seek..."),
		N_("Iteratively recalculate to find a target value"),
		&goal_seek_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("_Solver..."),
		N_("Iteratively recalculate with constraints to approach a target value"),
		&solver_cmd),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE(N_("_Data Analysis..."),
		N_("Statistical methods."),
		&data_analysis_cmd),

	GNOMEUIINFO_END
};

/* Data menu */
static GnomeUIInfo workbook_menu_data [] = {
	GNOMEUIINFO_ITEM_NONE(N_("_Sort..."),
		N_("Sort the selected cells"),
		&sort_cells_cmd),
	GNOMEUIINFO_ITEM_NONE(N_("_Filter..."),
		N_("Filter date with given criterias"),
		&advanced_filter_cmd),

	GNOMEUIINFO_END
};

#ifndef ENABLE_BONOBO
static GnomeUIInfo workbook_menu_help [] = {
	GNOMEUIINFO_HELP ("gnumeric"),
        GNOMEUIINFO_MENU_ABOUT_ITEM(about_cmd, NULL),
	GNOMEUIINFO_END
};
#endif

static GnomeUIInfo workbook_menu [] = {
        GNOMEUIINFO_MENU_FILE_TREE (workbook_menu_file),
	GNOMEUIINFO_MENU_EDIT_TREE (workbook_menu_edit),
	GNOMEUIINFO_MENU_VIEW_TREE (workbook_menu_view),
	GNOMEUIINFO_SUBTREE(N_("_Insert"), workbook_menu_insert),
	GNOMEUIINFO_SUBTREE(N_("F_ormat"), workbook_menu_format),
	GNOMEUIINFO_SUBTREE(N_("_Tools"),  workbook_menu_tools),
	GNOMEUIINFO_SUBTREE(N_("_Data"),   workbook_menu_data),
#ifdef ENABLE_BONOBO
#warning Should enable this when Bonobo gets menu help support
#else
	GNOMEUIINFO_MENU_HELP_TREE (workbook_menu_help),
#endif
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_standard_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("New"), N_("Creates a new workbook"),
		new_cmd, GNOME_STOCK_PIXMAP_NEW),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Open"), N_("Opens an existing workbook"),
		file_open_cmd, GNOME_STOCK_PIXMAP_OPEN),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Save"), N_("Saves the workbook"),
		file_save_cmd, GNOME_STOCK_PIXMAP_SAVE),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Print"), N_("Prints the workbook"),
		file_print_cmd, GNOME_STOCK_PIXMAP_PRINT),
	GNOMEUIINFO_ITEM_DATA (
		N_("Print pre_view"), N_("Print preview"),
		file_print_preview_cmd, NULL, preview_xpm),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Cut"), N_("Cuts the selection to the clipboard"),
		cut_cmd, GNOME_STOCK_PIXMAP_CUT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Copy"), N_("Copies the selection to the clipboard"),
		copy_cmd, GNOME_STOCK_PIXMAP_COPY),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Paste"), N_("Pastes the clipboard"),
		paste_cmd, GNOME_STOCK_PIXMAP_PASTE),

	GNOMEUIINFO_SEPARATOR,

#if 0
	GNOMEUIINFO_ITEM_STOCK (
		N_("Undo"), N_("Undo the operation"),
		undo_cmd, GNOME_STOCK_PIXMAP_UNDO),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Redo"), N_("Redo the operation"),
		redo_cmd, GNOME_STOCK_PIXMAP_REDO),
#else
#define TB_UNDO_POS 11
#define TB_REDO_POS 12
#endif

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_DATA (
		N_("Sum"), N_("Sum into the current cell."),
		autosum_cmd, NULL, auto_sum_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Function"), N_("Edit a function in the current cell."),
		&formula_guru, NULL, formula_guru_xpm),

	GNOMEUIINFO_ITEM_DATA (
		N_("Sort Ascending"), N_("Sorts the selected region in ascending order based on the first column selected."),
		sort_ascend_cmd, NULL, sort_ascending_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Sort Descending"), N_("Sorts the selected region in descending order based on the first column selected."),
		sort_descend_cmd, NULL, sort_descending_xpm),

#ifdef ENABLE_BONOBO
	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_DATA (
		N_("Create a Graph"), N_("Invokes the graph guru to help create a graph"),
		launch_graph_guru, NULL, graph_guru_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Insert Object"), N_("Inserts an object into the spreadsheet"),
		create_embedded_component_cmd, NULL, insert_bonobo_component_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Insert shaped object"), N_("Inserts a shaped object into the spreadsheet"),
		create_embedded_item_cmd, NULL, object_xpm),
#endif
#ifdef GNUMERIC_TEST_ACTIVE_OBJECT
	GNOMEUIINFO_ITEM_DATA (
		N_("Button"), N_("Creates a button"),
		&create_button_cmd, NULL, button_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Checkbox"), N_("Creates a checkbox"),
		&create_checkbox_cmd, NULL, checkbox_xpm),
#endif
	GNOMEUIINFO_ITEM_DATA (
		N_("Line"), N_("Creates a line object"),
		create_line_cmd, NULL, line_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Arrow"), N_("Creates an arrow object"),
		create_arrow_cmd, NULL, arrow_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Rectangle"), N_("Creates a rectangle object"),
		create_rectangle_cmd, NULL, rect_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Ellipse"), N_("Creates an ellipse object"),
		create_ellipse_cmd, NULL, oval_xpm),

	GNOMEUIINFO_SEPARATOR,


	GNOMEUIINFO_END
};

static void
do_focus_sheet (GtkNotebook *notebook, GtkNotebookPage *page, guint page_num, Workbook *wb)
{
	/* Hang on to old sheet */
	Sheet *old_sheet = wb->current_sheet;

	/* Lookup sheet associated with the current notebook tab */
	Sheet *sheet = workbook_focus_current_sheet (wb);
	gboolean accept = TRUE;

	/* Remove the cell seletion cursor if it exists */
	if (old_sheet != NULL)
		sheet_destroy_cell_select_cursor (old_sheet, TRUE);

	if (wb->editing) {
		/* If we are not at a subexpression boundary then finish editing */
		accept = !workbook_editing_expr (wb);

		if (accept)
			workbook_finish_editing (wb, TRUE);
	}
	if (accept && wb->current_sheet != NULL) {
		/* force an update of the status and edit regions */
		sheet_flag_status_update_range (sheet, NULL);

		sheet_update (sheet);
	}
}

static void
workbook_setup_sheets (Workbook *wb)
{
	wb->notebook = gtk_notebook_new ();
	gtk_signal_connect_after (GTK_OBJECT (wb->notebook), "switch_page",
				  GTK_SIGNAL_FUNC (do_focus_sheet), wb);

	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (wb->notebook), GTK_POS_BOTTOM);
	gtk_notebook_set_tab_border (GTK_NOTEBOOK (wb->notebook), 0);

	gtk_table_attach (GTK_TABLE (wb->priv->table), wb->notebook,
			  0, WB_COLS, WB_EA_SHEETS, WB_EA_SHEETS+1,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
			  0, 0);
}

Sheet *
workbook_focus_current_sheet (Workbook *wb)
{
	GtkWidget *current_notebook;
	Sheet *sheet;

	g_return_val_if_fail (wb != NULL, NULL);

	current_notebook = GTK_NOTEBOOK (wb->notebook)->cur_page->child;
	sheet = gtk_object_get_data (GTK_OBJECT (current_notebook), "sheet");

	if (sheet != NULL) {
		SheetView *sheet_view = SHEET_VIEW (sheet->sheet_views->data);

		g_return_val_if_fail (sheet_view != NULL, NULL);

		/* This is silly.  We have no business assuming there is only 1 view */
		gtk_window_set_focus (GTK_WINDOW (wb->toplevel), sheet_view->sheet_view);

		if (wb->current_sheet != sheet) {
			gtk_signal_emit (GTK_OBJECT (wb), workbook_signals [SHEET_ENTERED], sheet);
			wb->current_sheet = sheet;
		}
	} else
		g_warning ("There is no current sheet in this workbook");

	return sheet;
}

/*
 * Returns the ZERO based index of the requested sheet
 *   or -1 on error.
 */
static int
workbook_get_sheet_position (Sheet *sheet)
{
	GtkNotebook *notebook;
	int sheets, i;

	notebook = GTK_NOTEBOOK (sheet->workbook->notebook);
	sheets = workbook_sheet_count (sheet->workbook);

	for (i = 0; i < sheets; i++) {
		Sheet *this_sheet;
		GtkWidget *w;

		w = gtk_notebook_get_nth_page (notebook, i);

		this_sheet = gtk_object_get_data (GTK_OBJECT (w), "sheet");

		if (this_sheet == sheet)
			return i;
	}

	/* Emit a warning for now.  Should probably make this stronger */
	g_warning ("Sheet index not found ?");
	return -1;
}

void
workbook_focus_sheet (Sheet *sheet)
{
	GtkNotebook *notebook;
	int i;

	g_return_if_fail (sheet);
	g_return_if_fail (sheet->workbook);
	g_return_if_fail (sheet->workbook->notebook);
	g_return_if_fail (IS_SHEET (sheet));

	notebook = GTK_NOTEBOOK (sheet->workbook->notebook);
	i = workbook_get_sheet_position (sheet);

	if (i >= 0)
		gtk_notebook_set_page (notebook, i);
}

static int
wb_edit_key_pressed (GtkEntry *entry, GdkEventKey *event, Workbook *wb)
{
	switch (event->keyval) {
	case GDK_Escape:
		workbook_finish_editing (wb, FALSE);
		return TRUE;

	case GDK_F4:
	{
		/* FIXME FIXME FIXME
		 * 1) Make sure that a cursor move after an F4
		 *    does not insert.
		 * 2) Handle calls for a range rather than a single cell.
		 */
		int end_pos = GTK_EDITABLE (entry)->current_pos;
		int start_pos;
		int row_status_pos, col_status_pos;
		gboolean abs_row = FALSE, abs_col = FALSE;

		/* Ignore this character */
		event->keyval = GDK_VoidSymbol;

		/* Only applies while editing */
		if (!wb->editing)
			return TRUE;

		/* Only apply do this for formulas */
		if (NULL == gnumeric_char_start_expr_p (entry->text_mb))
			return TRUE;

		/*
		 * Find the end of the current range
		 * starting from the current position.
		 * Don't bother validating.  The goal is the find the
		 * end.  We'll validate on the way back.
		 */
		if (entry->text[end_pos] == '$')
			++end_pos;
		while (isalpha (entry->text[end_pos]))
			++end_pos;
		if (entry->text[end_pos] == '$')
			++end_pos;
		while (isdigit ((unsigned char)entry->text[end_pos]))
			++end_pos;

		/*
		 * Try to find the begining of the current range
		 * starting from the end we just found
		 */
		start_pos = end_pos - 1;
		while (start_pos >= 0 && isdigit ((unsigned char)entry->text[start_pos]))
			--start_pos;
		if (start_pos == end_pos)
			return TRUE;

		row_status_pos = start_pos + 1;
		if ((abs_row = (entry->text[start_pos] == '$')))
			--start_pos;

		while (start_pos >= 0 && isalpha (entry->text[start_pos]))
			--start_pos;
		if (start_pos == end_pos)
			return TRUE;

		col_status_pos = start_pos + 1;
		if ((abs_col = (entry->text[start_pos] == '$')))
			--start_pos;

		/* Toggle the relative vs absolute flags */
		if (abs_col) {
			--end_pos;
			--row_status_pos;
			gtk_editable_delete_text (GTK_EDITABLE (entry),
						  col_status_pos-1, col_status_pos);
		} else {
			++end_pos;
			++row_status_pos;
			gtk_editable_insert_text (GTK_EDITABLE (entry), "$", 1,
						  &col_status_pos);
		}

		if (!abs_col) {
			if (abs_row) {
				--end_pos;
				gtk_editable_delete_text (GTK_EDITABLE (entry),
							  row_status_pos-1, row_status_pos);
			} else {
				++end_pos;
				gtk_editable_insert_text (GTK_EDITABLE (entry), "$", 1,
							  &row_status_pos);
			}
		}

		/* Do not select the current range, and do not change the position. */
		gtk_entry_set_position (entry, end_pos);
	}

	case GDK_KP_Up:
	case GDK_Up:
	case GDK_KP_Down:
	case GDK_Down:
		/* Ignore these keys.  The default behaviour is certainly
		   not what we want.  */
		/* FIXME: what is the proper way to stop the key getting to
		   the gtkentry?  */
		event->keyval = GDK_VoidSymbol;
		return TRUE;

	case GDK_KP_Enter:
	case GDK_Return:
		if (wb->editing) {
			if (event->state == GDK_CONTROL_MASK ||
			    event->state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
				gboolean const is_array = (event->state & GDK_SHIFT_MASK);
				const char *text = gtk_entry_get_text (workbook_get_entry (wb));
				Sheet *sheet = wb->editing_sheet;
				EvalPos pos;
				
				/* Be careful to use the editing sheet */
				gboolean const trouble =
					cmd_area_set_text (
						workbook_command_context_gui (wb),
						eval_pos_init (&pos, sheet, &sheet->cursor.edit_pos),
						text, is_array);

				/*
				 * If the assignment was successful finish
				 * editing but do NOT store the results
				 */
				if (!trouble)
					workbook_finish_editing (wb, FALSE);
				return TRUE;
			}

			/* Is this the right way to append a newline ?? */
			if (event->state == GDK_MOD1_MASK)
				gtk_entry_append_text (workbook_get_entry (wb), "\n");
		}
	default:
		return FALSE;
	}
}


int
workbook_parse_and_jump (Workbook *wb, const char *text)
{
	int col, row;

	if (parse_cell_name (text, &col, &row, TRUE, NULL)){
		Sheet *sheet = wb->current_sheet;

		sheet_cursor_set (sheet, col, row, col, row, col, row);
		sheet_make_cell_visible (sheet, col, row);
		return TRUE;
	}

	gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
			 _("You should introduce a valid cell name"));
	return FALSE;
}

static void
wb_jump_to_cell (GtkEntry *entry, Workbook *wb)
{
	const char *text = gtk_entry_get_text (entry);

	workbook_parse_and_jump (wb, text);
	workbook_focus_current_sheet (wb);
}

void
workbook_set_region_status (Workbook *wb, const char *str)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (str != NULL);

	gtk_entry_set_text (GTK_ENTRY (wb->priv->selection_descriptor), str);
}

static void
misc_output (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	Range const * selection;

	if (gnumeric_debugging > 3) {
		summary_info_dump (wb->summary_info);

		if ((selection = selection_first_range (sheet, FALSE)) == NULL) {
			gnumeric_notice (
				wb, GNOME_MESSAGE_BOX_ERROR,
				_("Selection must be a single range"));
			return;
		}
	}

	if (style_debugging > 0) {
		printf ("Style list\n");
		sheet_styles_dump (sheet);
	}

	if (dependency_debugging > 0) {
		printf ("Dependencies\n");
		sheet_dump_dependencies (sheet);
	}
}

static void
workbook_setup_edit_area (Workbook *wb)
{
	GtkWidget *pix, *deps_button, *box, *box2;
	GtkEntry *entry;
	
	wb->priv->selection_descriptor     = gtk_entry_new ();
	wb->priv->ok_button     = gtk_button_new ();
	wb->priv->cancel_button = gtk_button_new ();
	wb->priv->func_button	= gtk_button_new ();

	workbook_edit_init (wb);
	entry = workbook_get_entry (wb);
	
	box           = gtk_hbox_new (0, 0);
	box2          = gtk_hbox_new (0, 0);

	gtk_widget_set_usize (wb->priv->selection_descriptor, 100, 0);

	/* Cancel */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_CANCEL);
	gtk_container_add (GTK_CONTAINER (wb->priv->cancel_button), pix);
	gtk_widget_set_sensitive (wb->priv->cancel_button, FALSE);
	GTK_WIDGET_UNSET_FLAGS (wb->priv->cancel_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (wb->priv->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cancel_input), wb);

	/* Ok */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_OK);
	gtk_container_add (GTK_CONTAINER (wb->priv->ok_button), pix);
	gtk_widget_set_sensitive (wb->priv->ok_button, FALSE);
	GTK_WIDGET_UNSET_FLAGS (wb->priv->ok_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (wb->priv->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (accept_input), wb);

	/* Auto function */
	pix = gnome_pixmap_new_from_xpm_d (equal_sign_xpm);
	gtk_container_add (GTK_CONTAINER (wb->priv->func_button), pix);
	GTK_WIDGET_UNSET_FLAGS (wb->priv->func_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (wb->priv->func_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_autofunction), wb);

	gtk_box_pack_start (GTK_BOX (box2), wb->priv->selection_descriptor, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wb->priv->cancel_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wb->priv->ok_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wb->priv->func_button, 0, 0, 0);

	/* Dependency + Style debugger */
	if (gnumeric_debugging > 9 ||
	    style_debugging > 0 ||
	    dependency_debugging > 0) {
		deps_button = gtk_button_new ();
		pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_PIXMAP_BOOK_RED);
		gtk_container_add (GTK_CONTAINER (deps_button), pix);
		GTK_WIDGET_UNSET_FLAGS (deps_button, GTK_CAN_FOCUS);
		gtk_signal_connect (GTK_OBJECT (deps_button), "clicked",
				    GTK_SIGNAL_FUNC (misc_output), wb);
		gtk_box_pack_start (GTK_BOX (box), deps_button, 0, 0, 0);
	}

	gtk_box_pack_start (GTK_BOX (box2), box, 0, 0, 0);
	gtk_box_pack_end   (GTK_BOX (box2), GTK_WIDGET (entry), 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wb->priv->table), box2,
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* Do signal setup for the editing input line */
	gtk_signal_connect (GTK_OBJECT (entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_editline_focus_in),
			    wb);
	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (accept_input),
			    wb);
	gtk_signal_connect (GTK_OBJECT (entry), "key_press_event",
			    GTK_SIGNAL_FUNC (wb_edit_key_pressed),
			    wb);

	/* Do signal setup for the status input line */
	gtk_signal_connect (GTK_OBJECT (wb->priv->selection_descriptor), "activate",
			    GTK_SIGNAL_FUNC (wb_jump_to_cell),
			    wb);
}

/*
 * WARNING * WARNING * WARNING
 *
 * Keep the functions in lower case.
 * We currently register the functions in lower case and some locales
 * (notably tr_TR) do not have the same encoding for tolower that
 * locale C does.
 *
 * eg tolower ('I') != 'i'
 * Which would break function lookup when looking up for funtion 'selectIon'
 * when it wa sregistered as 'selection'
 *
 * WARNING * WARNING * WARNING
 */
static struct {
	char *displayed_name;
	char *function;
} quick_compute_routines [] = {
	{ N_("Sum"),   	       "sum(selection(0))" },
	{ N_("Min"),   	       "min(selection(0))" },
	{ N_("Max"),   	       "max(selection(0))" },
	{ N_("Average"),       "average(selection(0))" },
	{ N_("Count"),         "count(selection(0))" },
	{ NULL, NULL }
};

/*
 * Sets the expression that gets evaluated whenever the
 * selection in the sheet changes
 */
static void
workbook_set_auto_expr (Workbook *wb,
			const char *description, const char *expression)
{
	char *old_num_locale, *old_msg_locale;
	ParsePos pp;
	ParseErr res;

	if (wb->auto_expr)
		expr_tree_unref (wb->auto_expr);

	old_num_locale = g_strdup (gnumeric_setlocale (LC_NUMERIC, NULL));
	gnumeric_setlocale (LC_NUMERIC, "C");
	old_msg_locale = g_strdup (textdomain (NULL));

	res = gnumeric_expr_parser (expression,
				    parse_pos_init (&pp, wb, NULL, 0, 0),
				    TRUE, FALSE, NULL, &wb->auto_expr);

	g_assert (res == PARSE_OK);

	if (wb->auto_expr_desc)
		string_unref (wb->auto_expr_desc);

	wb->auto_expr_desc = string_get (description);

	textdomain (old_msg_locale);
	g_free (old_msg_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);
}

static void
change_auto_expr (GtkWidget *item, Workbook *wb)
{
	char *expr, *name;
	Sheet *sheet = wb->current_sheet;

	expr = gtk_object_get_data (GTK_OBJECT (item), "expr");
	name = gtk_object_get_data (GTK_OBJECT (item), "name");
	workbook_set_auto_expr (wb, name, expr);
	sheet_update_auto_expr (sheet);
}

static void
change_auto_expr_menu (GtkWidget *widget, GdkEventButton *event, Workbook *wb)
{
	GtkWidget *menu;
	GtkWidget *item;
	int i;

	menu = gtk_menu_new ();

	for (i = 0; quick_compute_routines [i].displayed_name; i++){
		item = gtk_menu_item_new_with_label (
			_(quick_compute_routines [i].displayed_name));
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (change_auto_expr), wb);
		gtk_object_set_data (GTK_OBJECT (item), "expr",
				     quick_compute_routines [i].function);
		gtk_object_set_data (GTK_OBJECT (item), "name",
				     _(quick_compute_routines [i].displayed_name));
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}

/*
 * Sets up the autocalc label on the workbook.
 *
 * This code is more complex than it should for a number of
 * reasons:
 *
 *    1. GtkLabels flicker a lot, so we use a GnomeCanvas to
 *       avoid the unnecessary flicker
 *
 *    2. Using a Canvas to display a label is tricky, there
 *       are a number of ugly hacks here to do what we want
 *       to do.
 *
 * When GTK+ gets a flicker free label (Owen mentions that the
 * new repaint engine in GTK+ he is working on will be flicker free)
 * we can remove most of these hacks
 */
static void
workbook_setup_auto_calc (Workbook *wb)
{
	GtkWidget *canvas;
	GnomeCanvasGroup *root;
	GtkWidget *l, *frame;

	canvas = gnome_canvas_new ();

	l = gtk_label_new ("Info");
	gtk_widget_ensure_style (l);

	/* The canvas that displays text */
	root = GNOME_CANVAS_GROUP (GNOME_CANVAS (canvas)->root);
	wb->priv->auto_expr_label = GNOME_CANVAS_ITEM (gnome_canvas_item_new (
		root, gnome_canvas_text_get_type (),
		"text",     "x",
		"x",        (double) 0,
		"y",        (double) 0,	/* FIXME :-) */
		"font_gdk", l->style->font,
		"anchor",   GTK_ANCHOR_NW,
		"fill_color", "black",
		NULL));
	gtk_widget_set_usize (
		GTK_WIDGET (canvas),
		gdk_text_measure (l->style->font, "W", 1) * 15, -1);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame), canvas);
	gtk_box_pack_start (GTK_BOX (wb->priv->appbar), frame, FALSE, TRUE, 0);
	gtk_signal_connect (GTK_OBJECT (canvas), "button_press_event",
			    GTK_SIGNAL_FUNC (change_auto_expr_menu), wb);

	gtk_object_unref (GTK_OBJECT (l));
	gtk_widget_show_all (frame);
}

/*
 * Sets up the status display area
 */
static void
workbook_setup_status_area (Workbook *wb)
{
	/*
	 * Create the GnomeAppBar
	 */
	wb->priv->appbar = GNOME_APPBAR (gnome_appbar_new (TRUE, TRUE,
							   GNOME_PREFERENCES_USER));
	gnome_app_set_statusbar (GNOME_APP (wb->toplevel),
				 GTK_WIDGET (wb->priv->appbar));

	/*
	 * Add the auto calc widgets.
	 */
	workbook_setup_auto_calc (wb);
}

void
workbook_auto_expr_label_set (Workbook *wb, const char *text)
{
	char *res;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (text != NULL);

	res = g_strconcat (wb->auto_expr_desc->str, "=", text, NULL);
	gnome_canvas_item_set (wb->priv->auto_expr_label, "text", res, NULL);
	g_free (res);
}

/*
 * We must not crash on focus=NULL. We're called like that as a result of
 * gtk_window_set_focus (toplevel, NULL) if the first sheet view is destroyed
 * just after being created. This happens e.g when we cancel a file import or
 * the import fails.
 */
static void
workbook_set_focus (GtkWindow *window, GtkWidget *focus, Workbook *wb)
{
	if (focus && !window->focus_widget)
		workbook_focus_current_sheet (wb);
}

static void
workbook_configure_minimized_pixmap (Workbook *wb)
{
	/* FIXME: Use the new function provided by Raster */
}

#ifdef ENABLE_BONOBO

static void
grid_destroyed (GtkObject *embeddable_grid, Workbook *wb)
{
	wb->priv->workbook_views = g_list_remove (wb->priv->workbook_views, embeddable_grid);
}

static Bonobo_Unknown
workbook_container_get_object (BonoboObject *container, CORBA_char *item_name,
			       CORBA_boolean only_if_exists, CORBA_Environment *ev,
			       Workbook *wb)
{
	EmbeddableGrid *eg;
	Sheet *sheet;
	char *p;
	Value *range = NULL;

	sheet = workbook_sheet_lookup (wb, item_name);

	if (!sheet)
		return CORBA_OBJECT_NIL;

	p = strchr (item_name, '!');
	if (p) {
		*p++ = 0;

		/* this handles inversions, and relative ranges */
		range = range_parse (sheet, p, TRUE);
		if (range){
			CellRef *a = &range->v_range.cell.a;
			CellRef *b = &range->v_range.cell.b;

			if ((a->col < 0 || a->row < 0) ||
			    (b->col < 0 || b->row < 0) ||
			    (a->col > b->col) ||
			    (a->row > b->row)){
				value_release (range);
				return CORBA_OBJECT_NIL;
			}
		}
	}

	eg = embeddable_grid_new (wb, sheet);
	if (!eg)
		return CORBA_OBJECT_NIL;

	/*
	 * Do we have further configuration information?
	 */
	if (range) {
		CellRef *a = &range->v_range.cell.a;
		CellRef *b = &range->v_range.cell.b;

		embeddable_grid_set_range (eg, a->col, a->row, b->col, b->row);
	}

	gtk_signal_connect (GTK_OBJECT (eg), "destroy",
			    grid_destroyed, wb);

	wb->priv->workbook_views = g_list_prepend (wb->priv->workbook_views, eg);

	return CORBA_Object_duplicate (
		bonobo_object_corba_objref (BONOBO_OBJECT (eg)), ev);
}

static int
workbook_persist_file_load (BonoboPersistFile *ps, const CORBA_char *filename, void *closure)
{
	Workbook *wb = closure;
	CommandContext *context = workbook_command_context_gui (wb);
	return workbook_load_from (context, wb, filename);
}

static int
workbook_persist_file_save (BonoboPersistFile *ps, const CORBA_char *filename, void *closure)
{
	Workbook *wb = closure;
	CommandContext *context = workbook_command_context_gui (wb);

	return gnumeric_xml_write_workbook (context, wb, filename);
}

static void
workbook_bonobo_setup (Workbook *wb)
{
	wb->priv->bonobo_container = BONOBO_CONTAINER (bonobo_container_new ());
	wb->priv->persist_file = bonobo_persist_file_new (
		workbook_persist_file_load,
		workbook_persist_file_save,
		wb);

	bonobo_object_add_interface (
		BONOBO_OBJECT (wb->priv),
		BONOBO_OBJECT (wb->priv->bonobo_container));
	bonobo_object_add_interface (
		BONOBO_OBJECT (wb->priv),
		BONOBO_OBJECT (wb->priv->persist_file));

	gtk_signal_connect (
		GTK_OBJECT (wb->priv->bonobo_container), "get_object",
		GTK_SIGNAL_FUNC (workbook_container_get_object), wb);
}
#endif

static void
workbook_init (GtkObject *object)
{
	Workbook *wb = WORKBOOK (object);

	wb->priv = workbook_private_new ();

	wb->sheets       = g_hash_table_new (gnumeric_strcase_hash, gnumeric_strcase_equal);
	wb->current_sheet= NULL;
	wb->names        = NULL;
	wb->max_iterations = 1;
	wb->summary_info   = summary_info_new ();
	summary_info_default (wb->summary_info);

	/* We are not in edit mode */
	wb->use_absolute_cols = wb->use_absolute_rows = FALSE;
	wb->editing = FALSE;
	wb->editing_sheet = NULL;
	wb->editing_cell = NULL;

	/* Nothing to undo or redo */
	wb->undo_commands = wb->redo_commands = NULL;

	/* Set the default operation to be performed over selections */
	wb->auto_expr      = NULL;
	wb->auto_expr_desc = NULL;
	workbook_set_auto_expr (wb,
		_(quick_compute_routines [0].displayed_name),
		quick_compute_routines [0].function);

	workbook_count++;
	workbook_list = g_list_prepend (workbook_list, wb);

	workbook_corba_setup (wb);
#ifdef ENABLE_BONOBO
	workbook_bonobo_setup (wb);
#endif
}

static void
workbook_class_init (GtkObjectClass *object_class)
{
	workbook_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->set_arg = workbook_set_arg;
	object_class->get_arg = workbook_get_arg;

	gtk_object_add_arg_type ("Workbook::show_horizontal_scrollbar",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_HSCROLLBAR);
	gtk_object_add_arg_type ("Workbook::show_vertical_scrollbar",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_VSCROLLBAR);
	gtk_object_add_arg_type ("Workbook::show_notebook_tabs",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_TABS);

	workbook_signals [SHEET_ENTERED] =
		gtk_signal_new (
			"sheet_entered",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (WorkbookClass,
					   sheet_entered),
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE,
			1,
			GTK_TYPE_POINTER);
	/*
	 * WARNING :
	 * This is a preliminary hook used by screen reading software,
	 * etc.  The signal does NOT trigger for all cell changes and
	 * should be used with care.
	 */
	workbook_signals [CELL_CHANGED] =
		gtk_signal_new (
			"cell_changed",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (WorkbookClass,
					   cell_changed),
			gtk_marshal_NONE__POINTER_POINTER_INT_INT,
			GTK_TYPE_NONE,
			1,
			GTK_TYPE_POINTER);
	gtk_object_class_add_signals (object_class, workbook_signals, LAST_SIGNAL);

	object_class->destroy = workbook_destroy;
}

GtkType
workbook_get_type (void)
{
	static GtkType type = 0;

	if (!type) {
		GtkTypeInfo info = {
			"Workbook",
			sizeof (Workbook),
			sizeof (WorkbookClass),
			(GtkClassInitFunc) workbook_class_init,
			(GtkObjectInitFunc) workbook_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_object_get_type (), &info);
	}

	return type;
}

static void
change_displayed_zoom_cb (GtkObject *unused, Sheet* sheet, gpointer data)
{
	Workbook* wb = (Workbook*)data;
	gchar *str;
	int factor = (int) (sheet->last_zoom_factor_used * 100);
	GtkWidget *combo = wb->priv->zoom_entry;

	g_return_if_fail (combo != NULL);

	str = g_strdup_printf("%d%%", factor);

	gtk_combo_text_set_text (GTK_COMBO_TEXT (combo), str);

	g_free (str);
}

static void
change_zoom_in_current_sheet_cb (GtkWidget *caller, Workbook *wb)
{
	int factor;

	if (!wb->current_sheet)
		return;

	factor = atoi (gtk_entry_get_text (GTK_ENTRY (caller)));
	sheet_set_zoom_factor (wb->current_sheet, (double) factor / 100, FALSE);

	/* Restore the focus to the sheet */
	workbook_focus_current_sheet (wb);
}

/*
 * Updates the zoom control state
 */
void
workbook_zoom_feedback_set (Workbook *wb, double zoom_factor)
{
	g_return_if_fail (wb->current_sheet);

	/* Do no update the zoom when we update the status display */
	gtk_signal_handler_block_by_func
		(GTK_OBJECT (GTK_COMBO_TEXT (wb->priv->zoom_entry)->entry),
		 change_zoom_in_current_sheet_cb, wb);
	
	change_displayed_zoom_cb (NULL, wb->current_sheet, wb);

	/* Restore callback */
	gtk_signal_handler_unblock_by_func
		(GTK_OBJECT (GTK_COMBO_TEXT (wb->priv->zoom_entry)->entry),
		 change_zoom_in_current_sheet_cb, wb);
}

/*
 * Hide/show some toolbar items depending on the toolbar orientation
 */
static void
workbook_standard_toolbar_orient (GtkToolbar *toolbar,
				  GtkOrientation orientation,
				  gpointer data)
{
	Workbook* wb = (Workbook*)data;

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		gtk_widget_show (wb->priv->zoom_entry);
	else
		gtk_widget_hide (wb->priv->zoom_entry);
}

static char const * const preset_zoom [] = {
	"200%",
	"100%",
	"75%",
	"50%",
	"25%",
	NULL
};

/*
 * These create toolbar routines are kept independent, as they
 * will need some manual customization in the future (like adding
 * special purposes widgets for fonts, size, zoom
 */
static GtkWidget *
workbook_create_standard_toobar (Workbook *wb)
{
	GnomeDockItemBehavior behavior;
	const char *name = "StandardToolbar";
	int i, len;
	GtkWidget *toolbar, *zoom, *entry, *undo, *redo;
	GnomeApp *app = GNOME_APP (wb->toplevel);

	g_return_val_if_fail (app != NULL, NULL);

	toolbar = gnumeric_toolbar_new (workbook_standard_toolbar,
					app->accel_group, wb);

	behavior = GNOME_DOCK_ITEM_BEH_NORMAL;
	if(!gnome_preferences_get_menubar_detachable())
		behavior |= GNOME_DOCK_ITEM_BEH_LOCKED;

	gnome_app_add_toolbar (
		GNOME_APP (wb->toplevel),
		GTK_TOOLBAR (toolbar),
		name,
		behavior,
		GNOME_DOCK_TOP, 1, 0, 0);

	/* Zoom combo box */
	zoom = wb->priv->zoom_entry = gtk_combo_text_new (FALSE);
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (zoom), GTK_RELIEF_NONE);
	entry = GTK_COMBO_TEXT (zoom)->entry;
	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (change_zoom_in_current_sheet_cb), wb);
	gtk_combo_box_set_title (GTK_COMBO_BOX (zoom), _("Zoom"));

	/* Change the value when the displayed sheet is changed */
	gtk_signal_connect (GTK_OBJECT (wb), "sheet_entered",
			    (GtkSignalFunc) (change_displayed_zoom_cb), wb);

	/* Set a reasonable default width */
	len = gdk_string_measure (entry->style->font, "%10000");
	gtk_widget_set_usize (entry, len, 0);

	/* Preset values */
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		gtk_combo_text_add_item(GTK_COMBO_TEXT (zoom),
					preset_zoom[i], preset_zoom[i]);

	/* Add it to the toolbar */
	gtk_widget_show (zoom);
	gnumeric_toolbar_append_with_eventbox (GTK_TOOLBAR (toolbar),
				   zoom, _("Zoom"), NULL);

	/* Undo dropdown list */
	undo = wb->priv->undo_combo = gtk_combo_stack_new (GNOME_STOCK_PIXMAP_UNDO, TRUE);
	gtk_combo_box_set_title (GTK_COMBO_BOX (undo), _("Undo"));
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (undo), GTK_RELIEF_NONE);
	gtk_widget_show (undo);	
	gtk_signal_connect (GTK_OBJECT (undo), "pop",
			    (GtkSignalFunc) undo_combo_cmd, wb);

	/* Redo dropdown list */
	redo = wb->priv->redo_combo = gtk_combo_stack_new (GNOME_STOCK_PIXMAP_REDO, TRUE);
	gtk_combo_box_set_title (GTK_COMBO_BOX (redo), _("Redo"));
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (redo), GTK_RELIEF_NONE);
	gtk_widget_show (redo);
	gtk_signal_connect (GTK_OBJECT (redo), "pop",
			    (GtkSignalFunc) redo_combo_cmd, wb);
	
	/* Add them to the toolbar */
	gnumeric_toolbar_insert_with_eventbox (GTK_TOOLBAR (toolbar),
					       undo, _("Undo"), NULL, TB_UNDO_POS);
	gnumeric_toolbar_insert_with_eventbox (GTK_TOOLBAR (toolbar),
					       redo, _("Redo"), NULL, TB_REDO_POS);

	gtk_signal_connect (
		GTK_OBJECT(toolbar), "orientation-changed",
		GTK_SIGNAL_FUNC (&workbook_standard_toolbar_orient), wb);

	return toolbar;
}

static void
workbook_create_toolbars (Workbook *wb)
{
	wb->priv->standard_toolbar = workbook_create_standard_toobar (wb);
	gtk_widget_show (wb->priv->standard_toolbar);

	wb->priv->format_toolbar = workbook_create_format_toolbar (wb);
	gtk_widget_show (wb->priv->format_toolbar);
}

static gboolean
cb_scroll_wheel_support (GtkWidget *w, GdkEventButton *event, Workbook *wb)
{
	/* This is a stub routine to handle scroll wheel events
	 * Unfortunately the toplevel window is currently owned by the workbook
	 * rather than a workbook-view so we can not really scroll things
	 * unless we scrolled all the views at once which is ugly.
	 */
#if 0
	if (event->button == 4)
	    puts("up");
	else if (event->button == 5)
	    puts("down");
#endif
	return FALSE;
}

/**
 * workbook_new:
 *
 * Creates a new empty Workbook
 * and assigns a unique name.
 */
Workbook *
workbook_new (void)
{
	static int count = 0;
	gboolean is_unique;
	Workbook  *wb;
	int        sx, sy;

	wb = gtk_type_new (workbook_get_type ());
	wb->toplevel  = gnome_app_new ("Gnumeric", "Gnumeric");
	wb->priv->table     = gtk_table_new (0, 0, 0);

	wb->autosave_minutes = 10;
	wb->autosave_prompt = FALSE;

	wb->show_horizontal_scrollbar = TRUE;
	wb->show_vertical_scrollbar = TRUE;
	wb->show_notebook_tabs = TRUE;

	/* Assign a default name */
	do {
		char *name = g_strdup_printf (_("Book%d.gnumeric"), ++count);
		is_unique = workbook_set_filename (wb, name);
		g_free (name);
	} while (!is_unique);
	wb->file_format_level = FILE_FL_NEW;
	wb->file_save_fn      = gnumeric_xml_write_workbook;

	workbook_setup_status_area (wb);
	workbook_setup_edit_area (wb);
	workbook_setup_sheets (wb);
	gnome_app_set_contents (GNOME_APP (wb->toplevel), wb->priv->table);

	wb->priv->gui_context = command_context_gui_new (wb);
	wb->priv->during_destruction = FALSE;

#ifndef ENABLE_BONOBO
	gnome_app_create_menus_with_data (GNOME_APP (wb->toplevel), workbook_menu, wb);
	gnome_app_install_menu_hints (GNOME_APP (wb->toplevel), workbook_menu);

	/* Get the menu items that will be enabled disabled based on
	 * workbook state.
	 */
	wb->priv->menu_item_undo	  = workbook_menu_edit[0].widget;
	wb->priv->menu_item_redo	  = workbook_menu_edit[1].widget;
	wb->priv->menu_item_paste_special = workbook_menu_edit[6].widget;
#else
	{
		BonoboUIHandlerMenuItem *list;

		wb->priv->workbook_views  = NULL;
		wb->priv->persist_file    = NULL;
		wb->priv->uih = bonobo_ui_handler_new ();
		bonobo_ui_handler_set_app (wb->priv->uih, GNOME_APP (wb->toplevel));
		bonobo_ui_handler_create_menubar (wb->priv->uih);
		list = bonobo_ui_handler_menu_parse_uiinfo_list_with_data (workbook_menu, wb);
		bonobo_ui_handler_menu_add_list (wb->priv->uih, "/", list);
		bonobo_ui_handler_menu_free_list (list);
	}
#endif
	/* Create dynamic history menu items. */
	workbook_view_history_setup (wb);

	/* There is nothing to undo/redo yet */
	workbook_view_set_undo_redo_state (wb, NULL, NULL);

	/*
	 * Enable paste special, this assumes that the default is to
	 * paste from the X clipboard.  It will be disabled when
	 * something is cut.
	 */
	workbook_view_set_paste_special_state (wb, TRUE);

 	workbook_create_toolbars (wb);

	/* Minimized pixmap */
	workbook_configure_minimized_pixmap (wb);

	/* Focus handling */
	gtk_signal_connect_after (
		GTK_OBJECT (wb->toplevel), "set_focus",
		GTK_SIGNAL_FUNC (workbook_set_focus), wb);

	gtk_signal_connect_after (
		GTK_OBJECT (wb->toplevel), "delete_event",
		GTK_SIGNAL_FUNC (workbook_delete_event), wb);

#if 0
	/* Enable toplevel as a drop target */

	gtk_drag_dest_set (wb->toplevel,
			   GTK_DEST_DEFAULT_ALL,
			   drag_types, n_drag_types,
			   GDK_ACTION_COPY);

	gtk_signal_connect (GTK_OBJECT (wb->toplevel),
			    "drag_data_received",
			    GTK_SIGNAL_FUNC (filenames_dropped), wb);
#endif

	/* clipboard setup */
	x_clipboard_bind_workbook (wb);

	gtk_signal_connect (GTK_OBJECT (wb->toplevel), "button-release-event",
			    GTK_SIGNAL_FUNC (cb_scroll_wheel_support),
			    wb);

	/* Now that everything is initialized set the size */
	/* TODO : use gnome-config ? */
	gtk_window_set_policy (GTK_WINDOW (wb->toplevel), TRUE, TRUE, FALSE);
	sx = MAX (gdk_screen_width  () - 64, 600);
	sy = MAX (gdk_screen_height () - 64, 200);
	sx = (sx * 3) / 4;
	sy = (sy * 3) / 4;
	workbook_view_set_size (wb, sx, sy);

	gtk_widget_show_all (wb->priv->table);

	return wb;
}

static void
workbook_set_arg (GtkObject  *object,
		    GtkArg     *arg,
		    guint	arg_id)
{
	Workbook *wb;

	wb = WORKBOOK (object);

	switch (arg_id) {
	case ARG_VIEW_HSCROLLBAR:
		wb->show_horizontal_scrollbar = GTK_VALUE_BOOL (*arg);
		workbook_view_pref_visibility (wb);
		break;
	case ARG_VIEW_VSCROLLBAR:
		wb->show_vertical_scrollbar = GTK_VALUE_BOOL (*arg);
		workbook_view_pref_visibility (wb);
		break;
	case ARG_VIEW_TABS:
		wb->show_notebook_tabs = GTK_VALUE_BOOL (*arg);
		workbook_view_pref_visibility (wb);
		break;
	}
}

static void
workbook_get_arg (GtkObject  *object,
		    GtkArg     *arg,
		    guint	arg_id)
{
	Workbook *wb;

	wb = WORKBOOK (object);

	switch (arg_id) {
	case ARG_VIEW_HSCROLLBAR:
		GTK_VALUE_BOOL (*arg) = wb->show_horizontal_scrollbar;
		break;

	case ARG_VIEW_VSCROLLBAR:
		GTK_VALUE_BOOL (*arg) = wb->show_vertical_scrollbar;
		break;

	case ARG_VIEW_TABS:
		GTK_VALUE_BOOL (*arg) = wb->show_notebook_tabs;
		break;
	}
}

void
workbook_set_attributev (Workbook *wb, GList *list)
{
	gint length, i;

	length = g_list_length(list);

	for (i = 0; i < length; i++){
		GtkArg *arg = g_list_nth_data (list, i);

		gtk_object_arg_set (GTK_OBJECT(wb), arg, NULL);
	}
}

GtkArg *
workbook_get_attributev (Workbook *wb, guint *n_args)
{
	GtkArg *args;
	guint num;

	args = gtk_object_query_args (WORKBOOK_TYPE, NULL, &num);
	gtk_object_getv (GTK_OBJECT(wb), num, args);

	*n_args = num;

	return args;
}

/**
 * workbook_rename_sheet:
 * @wb:       the workbook where the sheet is
 * @old_name: the name of the sheet we want to rename
 * @new_name: new name we want to assing to the sheet.
 *
 * Returns TRUE if there was a problem changing the name
 * return FALSE otherwise.
 */
gboolean
workbook_rename_sheet (CommandContext *context,
		       Workbook *wb,
		       const char *old_name,
		       const char *new_name)
{
	Sheet *sheet;
	GtkNotebook *notebook;
	int sheets, i;

	g_return_val_if_fail (wb != NULL, TRUE);
	g_return_val_if_fail (old_name != NULL, TRUE);
	g_return_val_if_fail (new_name != NULL, TRUE);

	/* Did the name change? */
	if (strcmp (old_name, new_name) == 0)
		return TRUE;
	
	if (strlen (new_name) < 1) {
		gnumeric_error_invalid (context,
					_("Sheet name"),
					_("must have at least 1 letter"));
		return TRUE;
	}

	/* Do not let two sheets in the workbook have the same name */
	if (g_hash_table_lookup (wb->sheets, new_name)) {
		gnumeric_error_invalid (context,
					_("Duplicate Sheet name"),
					new_name);
		return TRUE;
	}

	sheet = (Sheet *) g_hash_table_lookup (wb->sheets, old_name);

	g_return_val_if_fail (sheet != NULL, TRUE);

	g_hash_table_remove (wb->sheets, old_name);
	sheet_rename (sheet, new_name);
	g_hash_table_insert (wb->sheets, sheet->name_unquoted, sheet);

	sheet_set_dirty (sheet, TRUE);

	/* Update the notebook label */
	notebook = GTK_NOTEBOOK (wb->notebook);
	sheets = workbook_sheet_count (wb);

	for (i = 0; i < sheets; i++) {
		Sheet *this_sheet;
		GtkWidget *w;

		w = gtk_notebook_get_nth_page (notebook, i);

		this_sheet = gtk_object_get_data (GTK_OBJECT (w), "sheet");

		if (this_sheet == sheet) {
			EditableLabel *label =
				gtk_object_get_data (GTK_OBJECT (w), "sheet_label");
			g_return_val_if_fail (label != NULL, FALSE);
			editable_label_set_text (label, new_name);
		}
	}

	return FALSE;
}


static void
sheet_label_editing_stopped_signal (EditableLabel *el, Workbook *wb)
{
	workbook_focus_current_sheet (wb);
}

/*
 * Signal handler for EditableLabel's text_changed signal.
 */
static gboolean
sheet_label_text_changed_signal (EditableLabel *el,
				 const char *new_name,
				 Workbook *wb)
{
	gboolean ans;

	ans = !cmd_rename_sheet (workbook_command_context_gui (wb),
				 wb, el->text, new_name);
	workbook_focus_current_sheet (wb);

	return ans;
}

/**
 * sheet_action_add_sheet:
 * Invoked when the user selects the option to add a sheet
 */
static void
sheet_action_add_sheet (GtkWidget *widget, Sheet *current_sheet)
{
	insert_sheet_cmd (NULL, current_sheet->workbook);
}

/*
 * sheet_action_clone_sheet:
 * Invoked when the user selects the option to copy a sheet
 */
static void
sheet_action_clone_sheet (GtkWidget *widget, Sheet *current_sheet)
{
     	Sheet *sheet;
	Workbook *wb = current_sheet->workbook;

	sheet = sheet_duplicate (current_sheet);
	workbook_attach_sheet (wb, sheet);
	sheet_set_dirty (sheet, TRUE);
}

/**
 * sheet_action_delete_sheet:
 * Invoked when the user selects the option to remove a sheet
 */
static void
sheet_action_delete_sheet (GtkWidget *ignored, Sheet *sheet)
{
	GtkWidget *d, *button_no;
	Workbook *wb = sheet->workbook;
	char *message;
	int r;

	/*
	 * If this is the last sheet left, ignore the request
	 */
	if (workbook_sheet_count (wb) == 1)
		return;

	message = g_strdup_printf (
		_("Are you sure you want to remove the sheet called `%s'?"),
		sheet->name_unquoted);

	d = gnome_message_box_new (
		message, GNOME_MESSAGE_BOX_QUESTION,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	g_free (message);
	button_no = g_list_last (GNOME_DIALOG (d)->buttons)->data;
	gtk_widget_grab_focus (button_no);

	r = gnumeric_dialog_run (wb, GNOME_DIALOG (d));

	if (r != 0)
		return;

	workbook_delete_sheet (sheet);
	workbook_recalc_all (wb);
	sheet_update (wb->current_sheet);
}

/*
 * sheet_action_rename_sheet:
 * Invoked when the user selects the option to rename a sheet
 */
static void
sheet_action_rename_sheet (GtkWidget *widget, Sheet *current_sheet)
{
	char *new_name;
	Workbook *wb = current_sheet->workbook;

	new_name = dialog_get_sheet_name (wb, current_sheet->name_unquoted);
	if (!new_name)
		return;

	/* We do not care if it fails */
	cmd_rename_sheet (workbook_command_context_gui (wb),
				 wb, current_sheet->name_unquoted, new_name);
	g_free (new_name);
}

/*
 * sheet_action_reorder_sheet
 * Invoked when the user selects the option to re-order the sheets
 * When more than 1 sheet exists
 */
static void
sheet_action_reorder_sheet (GtkWidget *widget, Sheet *current_sheet)
{
	dialog_sheet_order (current_sheet->workbook);
}

#define SHEET_CONTEXT_TEST_SIZE 1

struct {
	const char *text;
	void (*function) (GtkWidget *widget, Sheet *sheet);
	int  flags;
} sheet_label_context_actions [] = {
	{ N_("Add another sheet"), &sheet_action_add_sheet, 0 },
	{ N_("Remove this sheet"), &sheet_action_delete_sheet, SHEET_CONTEXT_TEST_SIZE },
	{ N_("Rename this sheet"), &sheet_action_rename_sheet, 0 },
	{ N_("Duplicate this sheet"), &sheet_action_clone_sheet, 0 },
	{ N_("Re-order sheets"), &sheet_action_reorder_sheet, SHEET_CONTEXT_TEST_SIZE },
	{ NULL, NULL }
};


/**
 * sheet_menu_label_run:
 *
 */
static void
sheet_menu_label_run (Sheet *sheet, GdkEventButton *event)
{
	GtkWidget *menu;
	GtkWidget *item;
	int i;

	menu = gtk_menu_new ();

	for (i = 0; sheet_label_context_actions [i].text != NULL; i++){
		int flags = sheet_label_context_actions [i].flags;

		if (flags & SHEET_CONTEXT_TEST_SIZE){
			if (workbook_sheet_count (sheet->workbook) < 2)
				continue;
		}
		item = gtk_menu_item_new_with_label (
			_(sheet_label_context_actions [i].text));
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);

		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (sheet_label_context_actions [i].function),
			sheet);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}

/**
 * sheet_label_button_press:
 *
 * Invoked when the user has clicked on the EditableLabel widget.
 * This takes care of switching to the notebook that contains the label
 */
static gint
sheet_label_button_press (GtkWidget *widget, GdkEventButton *event, GtkWidget *child)
{
	GtkWidget *notebook;
	gint page_number;
	Sheet *sheet;

	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	sheet = gtk_object_get_data (GTK_OBJECT (child), "sheet");
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	notebook = child->parent;
	page_number = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), child);

	if (event->button == 1){
		gtk_notebook_set_page (GTK_NOTEBOOK (notebook), page_number);
		return TRUE;
	}

	if (event->button == 3){
		sheet_menu_label_run (sheet, event);
		return TRUE;
	}

	return FALSE;
}

int
workbook_sheet_count (Workbook *wb)
{
	g_return_val_if_fail (wb != NULL, 0);

	return g_hash_table_size (wb->sheets);
}

/*
 * yield_focus
 * @widget   widget
 * @toplevel toplevel
 *
 * Give up focus if we have it. This is called when widget is destroyed. A
 * widget which no longer exists should not have focus!
 */
static gboolean
yield_focus (GtkWidget *widget, GtkWindow *toplevel)
{
	if (toplevel && (toplevel->focus_widget == widget))
		gtk_window_set_focus (toplevel, NULL);

	return FALSE;
}

/**
 * workbook_attach_sheet:
 * @wb: the target workbook
 * @sheet: a sheet
 *
 * Attaches the @sheet to the @wb.
 *
 * A callback to yield focus when the sheet view is destroyed is attached
 * to the sheet view. This solves a problem which we encountered e.g. when
 * a file import is canceled or fails:
 * - First, a sheet is attached to the workbook in this routine. The sheet
 *   view gets focus.
 * - Then, the sheet is detached, sheet view is destroyed, but GTK
 *   still believes that the sheet view has focus.
 * - GTK accesses already freed memory. Due to the excellent cast checking
 *   in GTK, we didn't get anything worse than warnings. But the bug had
 *   to be fixed anyway.
 */
void
workbook_attach_sheet (Workbook *wb, Sheet *sheet)
{
	GtkWidget *t, *sheet_label;
	SheetView *sheet_view;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (sheet != NULL);

	/*
	 * We do not want to attach sheets that are attached
	 * to a different workbook.
	 */
	g_return_if_fail (sheet->workbook == wb || sheet->workbook == NULL);

	sheet->workbook = wb;

	g_hash_table_insert (wb->sheets, sheet->name_unquoted, sheet);

	t = gtk_table_new (0, 0, 0);
	gtk_table_attach (
		GTK_TABLE (t), GTK_WIDGET (sheet->sheet_views->data),
		0, 3, 0, 1, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	sheet_view = SHEET_VIEW (sheet->sheet_views->data);
	gtk_signal_connect (
		GTK_OBJECT (sheet_view->sheet_view), "destroy",
		GTK_SIGNAL_FUNC (yield_focus), (gpointer) wb->toplevel);

	gtk_widget_show_all (t);
	gtk_object_set_data (GTK_OBJECT (t), "sheet", sheet);

	g_return_if_fail (wb->notebook != NULL);

	sheet_label = editable_label_new (sheet->name_unquoted);
	/*
	 * NB. this is so we can use editable_label_set_text since
	 * gtk_notebook_set_tab_label kills our widget & replaces with a label.
	 */
	gtk_object_set_data (GTK_OBJECT (t), "sheet_label", sheet_label);
	gtk_signal_connect_after (
		GTK_OBJECT (sheet_label), "text_changed",
		GTK_SIGNAL_FUNC (sheet_label_text_changed_signal), wb);
	gtk_signal_connect (
		GTK_OBJECT (sheet_label), "editing_stopped",
		GTK_SIGNAL_FUNC (sheet_label_editing_stopped_signal), wb);
	gtk_signal_connect (
		GTK_OBJECT (sheet_label), "button_press_event",
		GTK_SIGNAL_FUNC (sheet_label_button_press), t);

	gtk_widget_show (sheet_label);
	gtk_notebook_append_page (GTK_NOTEBOOK (wb->notebook),
				  t, sheet_label);

	if (workbook_sheet_count (wb) > 3)
		gtk_notebook_set_scrollable (GTK_NOTEBOOK (wb->notebook), TRUE);
}

/**
 * workbook_detach_sheet:
 * @wb: workbook.
 * @sheet: the sheet that we want to detach from the workbook
 *
 * Detaches @sheet from the workbook @wb.
 */
gboolean
workbook_detach_sheet (Workbook *wb, Sheet *sheet, gboolean force)
{
	GtkNotebook *notebook;
	int sheets, i;

	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->workbook != NULL, FALSE);
	g_return_val_if_fail (sheet->workbook == wb, FALSE);
	g_return_val_if_fail (workbook_sheet_lookup (wb, sheet->name_unquoted)
			      == sheet, FALSE);

	notebook = GTK_NOTEBOOK (wb->notebook);
	sheets = workbook_sheet_count (sheet->workbook);

	/*
	 * Remove our reference to this sheet
	 */
	g_hash_table_remove (wb->sheets, sheet->name_unquoted);

	for (i = 0; i < sheets; i++) {
		Sheet *this_sheet;
		GtkWidget *w;

		w = gtk_notebook_get_nth_page (notebook, i);

		this_sheet = gtk_object_get_data (GTK_OBJECT (w), "sheet");

		if (this_sheet == sheet) {
			gtk_notebook_remove_page (notebook, i);
			break;
		}
	}

	sheet_destroy (sheet);

	/* No need to recalc if we are exiting */
	if (!wb->priv->during_destruction) {
		workbook_recalc_all (wb);
		sheet_update (wb->current_sheet);
	}

	/*
	 * GUI-adjustments
	 */
	if (workbook_sheet_count (wb) < 4)
		gtk_notebook_set_scrollable (GTK_NOTEBOOK (wb->notebook), FALSE);

	return TRUE;
}

/**
 * workbook_sheet_lookup:
 * @wb: workbook to lookup the sheet on
 * @sheet_name: the sheet name we are looking for.
 *
 * Returns a pointer to a Sheet or NULL if the sheet
 * was not found.
 */
Sheet *
workbook_sheet_lookup (Workbook *wb, const char *sheet_name)
{
	Sheet *sheet;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (sheet_name != NULL, NULL);

	sheet = g_hash_table_lookup (wb->sheets, sheet_name);

	return sheet;
}

/**
 * workbook_sheet_name_strip_number:
 * @name: name to strip number from 
 * @number: returns the number stripped off in *number
 * 
 * Gets a name in the form of "Sheet (10)", "Stuff" or "Dummy ((((,"
 * and returns the real name of the sheet "Sheet","Stuff","Dymmy ((((,"
 * without the copy number.
 **/
static void
workbook_sheet_name_strip_number (char *name, int* number)
{
	char *end;
	
	*number = 1;

	end = strrchr (name, ')');
	if (end == NULL || end[1] != '\0')
		return;

	while (--end >= name) {
		if (*end == '(') {
			*number = atoi (end+1);
			*end = '\0';
			return;
		}
		if (!isdigit(*end))
			return;
	}
}

/**
 * workbook_sheet_get_free_name:
 * @wb:   workbook to look for
 * @base: base for the name, e. g. "Sheet"
 * @name_format : optionally null format for handling dupilicates.
 * @always_suffix: if true, add suffix even if the name "base" is not in use.
 *
 * Gets a new unquoted name for a sheets such that it does not exist on the
 * workbook.
 *
 * Returns the name assigned to the sheet.  */
char *
workbook_sheet_get_free_name (Workbook *wb,
			      const char *base,
			      gboolean always_suffix,
			      gboolean handle_counter)
{
	const char *name_format;
	char *name, *base_name;
	int  i = 0;

	g_return_val_if_fail (wb != NULL, NULL);

	if (!always_suffix && (workbook_sheet_lookup (wb, base) == NULL))
		return g_strdup (base); /* Name not in use */

	base_name = g_strdup (base);
	if (handle_counter) {
		workbook_sheet_name_strip_number (base_name, &i);
		name_format = "%s(%d)";
	} else
		name_format = "%s%d";

	name = g_malloc (strlen (base_name) + strlen (name_format) + 10);
	for ( ; ++i < 1000 ; ){
		sprintf (name, name_format, base_name, i);
		if (workbook_sheet_lookup (wb, name) == NULL) {
			g_free (base_name);
			return name;
		}
	}
	g_assert_not_reached ();
	g_free (name);
	g_free (base_name);
	return NULL;
}

/**
 * workbook_new_with_sheets:
 * @sheet_count: initial number of sheets to create.
 *
 * Returns a Workbook with @sheet_count allocated
 * sheets on it
 */
Workbook *
workbook_new_with_sheets (int sheet_count)
{
	Workbook *wb;
	int i;

	wb = workbook_new ();

	for (i = 1; i <= sheet_count; i++){
		Sheet *sheet;
		char *name = g_strdup_printf (_("Sheet%d"), i);

		sheet = sheet_new (wb, name);
		workbook_attach_sheet (wb, sheet);
		g_free (name);
	}

	workbook_focus_current_sheet (wb);

	return wb;
}

/**
 * workbook_set_filename:
 * @wb: the workbook to modify
 * @name: the file name for this worksheet.
 *
 * Sets the internal filename to @name and changes
 * the title bar for the toplevel window to be the name
 * of this file.
 *
 * Returns : TRUE if the name was set succesfully.
 *
 * FIXME : Add a check to ensure the name is unique.
 */
gboolean
workbook_set_filename (Workbook *wb, const char *name)
{
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (wb->filename)
		g_free (wb->filename);

	wb->filename = g_strdup (name);

	workbook_view_set_title (wb, g_basename (name));

	return TRUE;
}

/**
 * workbook_set_saveinfo:
 * @wb: the workbook to modify
 * @name: the file name for this worksheet.
 * @level: the file format level - new, manual or auto
 *
 * Provided level is at least as high as current level,
 * calls workbook_set_filename, and sets level and saver.
 *
 * Returns : TRUE if save info was set succesfully.
 *
 * FIXME : Add a check to ensure the name is unique.
 */
gboolean
workbook_set_saveinfo (Workbook *wb, const char *name,
		       FileFormatLevel level, FileFormatSave save_fn)
{
	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (level > FILE_FL_NONE && level <= FILE_FL_AUTO,
			      FALSE);

	if (level < wb->file_format_level)
		return FALSE;

	if (!workbook_set_filename (wb, name))
		return FALSE;
	wb->file_format_level = level;
	if (save_fn != NULL)
	wb->file_save_fn = save_fn ? save_fn : gnumeric_xml_write_workbook;
	workbook_view_set_title (wb, g_basename (name));

	return TRUE;
}

void
workbook_foreach (WorkbookCallback cback, gpointer data)
{
	GList *l;

	for (l = workbook_list; l; l = l->next){
		Workbook *wb = l->data;

		if (!(*cback)(wb, data))
			return;
	}
}

struct expr_relocate_storage
{
	EvalPos pos;
	ExprTree *oldtree;
};

/**
 * workbook_expr_unrelocate_free : Release the storage associated with
 *    the list.
 */
void
workbook_expr_unrelocate_free (GSList *info)
{
	while (info != NULL) {
		GSList *cur = info;
		struct expr_relocate_storage *tmp =
		    (struct expr_relocate_storage *)(info->data);

		info = info->next;
		expr_tree_unref (tmp->oldtree);
		g_free (tmp);
		g_slist_free_1 (cur);
	}
}

void
workbook_expr_unrelocate (Workbook *wb, GSList *info)
{
	while (info != NULL) {
		GSList *cur = info;
		struct expr_relocate_storage *tmp =
		    (struct expr_relocate_storage *)info->data;
		Cell *cell = sheet_cell_get (tmp->pos.sheet,
					     tmp->pos.eval.col,
					     tmp->pos.eval.row);

		g_return_if_fail (cell != NULL);

		sheet_cell_set_expr (cell, tmp->oldtree);
		expr_tree_unref (tmp->oldtree);

		info = info->next;
		g_free (tmp);
		g_slist_free_1 (cur);
	}
}

/**
 * workbook_expr_relocate:
 * Fixes references to or from a region that is going to be moved.
 *
 * @wb: the workbook to modify
 * @info : the descriptor record for what is being moved where.
 *
 * Returns a list of the locations and expressions that were changed
 * outside of the region.
 */
GSList *
workbook_expr_relocate (Workbook *wb, ExprRelocateInfo const *info)
{
	GList *dependents, *l;
	GSList *undo_info = NULL;

	if (info->col_offset == 0 && info->row_offset == 0 &&
	    info->origin_sheet == info->target_sheet)
		return NULL;

	g_return_val_if_fail (wb != NULL, NULL);

	/* Copy the list since it will change underneath us.  */
	dependents = g_list_copy (wb->dependents);

	for (l = dependents; l; l = l->next)	{
		Cell *cell = l->data;
		ExprRewriteInfo rwinfo;
		ExprTree *newtree; 
		
		rwinfo.type = EXPR_REWRITE_RELOCATE;
		memcpy (&rwinfo.u.relocate, info, sizeof (ExprRelocateInfo));
		eval_pos_init_cell (&rwinfo.u.relocate.pos, cell);

		newtree = expr_rewrite (cell->base.expression, &rwinfo);

		if (newtree) {
			/* Don't store relocations if they were inside the region
			 * being moved.  That is handled elsewhere */
			if (info->origin_sheet != rwinfo.u.relocate.pos.sheet ||
			    !range_contains (&info->origin,
					     rwinfo.u.relocate.pos.eval.col,
					     rwinfo.u.relocate.pos.eval.row)) {
				struct expr_relocate_storage *tmp =
				    g_new (struct expr_relocate_storage, 1);
				tmp->pos = rwinfo.u.relocate.pos;
				tmp->oldtree = cell->base.expression;
				expr_tree_ref (tmp->oldtree);
				undo_info = g_slist_prepend (undo_info, tmp);
			}

			sheet_cell_set_expr (cell, newtree);
			expr_tree_unref (newtree);
		}
	}

	g_list_free (dependents);

	return undo_info;
}

GList *
workbook_sheets (Workbook *wb)
{
	GList *list = NULL;
	GtkNotebook *notebook;
	int sheets, i;

	g_return_val_if_fail (wb, NULL);
	g_return_val_if_fail (wb->notebook, NULL);

	notebook = GTK_NOTEBOOK (wb->notebook);
	sheets = workbook_sheet_count (wb);

	for (i = 0; i < sheets; i++) {
		Sheet *this_sheet;
		GtkWidget *w;

		w = gtk_notebook_get_nth_page (notebook, i);
		this_sheet = gtk_object_get_data (GTK_OBJECT (w), "sheet");

		list = g_list_prepend (list, this_sheet);
	}

	return g_list_reverse (list);
}

typedef struct {
	Sheet   *base_sheet;
	GString *result;
} selection_assemble_closure_t;

static void
cb_assemble_selection (gpointer key, gpointer value, gpointer user_data)
{
	selection_assemble_closure_t *info = (selection_assemble_closure_t *) user_data;
	Sheet *sheet = value;
	gboolean include_prefix;
	char *sel;

	if (*info->result->str)
		g_string_append_c (info->result, ',');

	/*
	 * If a base sheet is specified, use this to avoid prepending
	 * the full path to the cell region.
	 */
	if (info->base_sheet && (info->base_sheet != value))
		include_prefix = TRUE;
	else
		include_prefix = FALSE;

	sel = selection_to_string (sheet, include_prefix);
	g_string_append (info->result, sel);
	g_free (sel);
}

char *
workbook_selection_to_string (Workbook *wb, Sheet *base_sheet)
{
	selection_assemble_closure_t info;
	char *result;

	g_return_val_if_fail (wb != NULL, NULL);

	if (base_sheet == NULL){
		g_return_val_if_fail (IS_SHEET (base_sheet), NULL);
	}

	info.result = g_string_new ("");
	info.base_sheet = base_sheet;
	g_hash_table_foreach (wb->sheets, cb_assemble_selection, &info);

	result = info.result->str;
	g_string_free (info.result, FALSE);

	return result;
}

CommandContext *
workbook_command_context_gui (Workbook *wb)
{
	/* When we are operating before a workbook is created
	 * wb can be NULL
	 */
	if (wb == NULL) {
		static CommandContext *cc = NULL;
		if (cc == NULL)
			cc = command_context_gui_new (NULL);
		return cc;
	}

	return wb->priv->gui_context;
}

/*
 * Autosave
 */
static gint
dialog_autosave_callback (gpointer *data)
{
        Workbook *wb = (Workbook *) data;

	if (wb->autosave && workbook_is_dirty (wb)) {
	        if (wb->autosave_prompt) {
			if (!dialog_autosave_prompt (wb))
				return 1;
		}
		workbook_save (workbook_command_context_gui (wb), wb);
	}
	return 1;
}

void
workbook_autosave_cancel (Workbook *wb)
{
	if (wb->autosave_timer != 0)
		gtk_timeout_remove (wb->autosave_timer);
	wb->autosave_timer = 0;
}

void
workbook_autosave_set (Workbook *wb, int minutes, gboolean prompt)
{
	if (wb->autosave_timer != 0){
		gtk_timeout_remove (wb->autosave_timer);
		wb->autosave_timer = 0;
	}

	wb->autosave_minutes = minutes;
	wb->autosave_prompt = prompt;

	if (minutes == 0)
		wb->autosave = FALSE;
	else {
		wb->autosave = TRUE;

		wb->autosave_timer =
			gtk_timeout_add (minutes * 60000,
					 (GtkFunction) dialog_autosave_callback, wb);
	}
}

/*
 * Moves the sheet up or down @direction spots in the sheet list
 * If @direction is positive, move left. If positive, move right.
 */
void
workbook_move_sheet (Sheet *sheet, int direction)
{
	gint source, dest;

        g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

        source = workbook_get_sheet_position (sheet);
	dest = source + direction;

	if (0 <= dest && dest < workbook_sheet_count (sheet->workbook)) {
		GtkNotebook *nb =
		    GTK_NOTEBOOK (sheet->workbook->notebook);
		GtkWidget *w =
		    gtk_notebook_get_nth_page (nb, source);

		gtk_notebook_reorder_child (nb, w, dest);
		sheet_set_dirty (sheet, TRUE);
	}
}

/*
 * Unlike workbook_detach_sheet, this function
 * Not only detaches the given sheet from its parent workbook,
 * But also invalidates all references to the deleted sheet
 * From other sheets in the workbook and clears all references
 * In the clipboard to this sheet
 * Finally, it also detaches the sheet from the workbook
 */
void
workbook_delete_sheet (Sheet *sheet)
{
        Workbook *wb;
	g_return_if_fail (sheet != NULL);
        g_return_if_fail (IS_SHEET (sheet));

	wb = sheet->workbook;

	/*
	 * FIXME : Deleting a sheet plays havoc with our data structures.
	 * Be safe for now and empty the undo/redo queues
	 */
	command_list_release (wb->undo_commands);
	command_list_release (wb->redo_commands);
	wb->undo_commands = NULL;
	wb->redo_commands = NULL;
	workbook_view_clear_undo (wb);
	workbook_view_clear_redo (wb);
	workbook_view_set_undo_redo_state (wb, NULL, NULL);

	/* Important to do these BEFORE detaching the sheet */
	sheet_deps_destroy (sheet);
	expr_name_invalidate_refs_sheet (sheet);

	/*
	 * All is fine, remove the sheet
	 */
	workbook_detach_sheet (wb, sheet, FALSE);
}

static void
cb_sheet_calc_spans (gpointer key, gpointer value, gpointer flags)
{
	sheet_calc_spans (value, GPOINTER_TO_INT(flags));
}
void
workbook_calc_spans (Workbook *wb, SpanCalcFlags const flags)
{
	g_return_if_fail (wb != NULL);

	g_hash_table_foreach (wb->sheets, &cb_sheet_calc_spans, GINT_TO_POINTER (flags));
}

void
workbook_unref (Workbook *wb)
{
	gtk_object_unref (GTK_OBJECT (wb));
}

/**
 * workbook_foreach_cell_in_range :
 *
 * @pos : The position the range is relative to.
 * @cell_range : A value containing a range;
 * @only_existing : if TRUE only existing cells are sent to the handler.
 * @handler : The operator to apply to each cell.
 * @closure : User data.
 *
 * The supplied value must be a cellrange.
 * The range bounds are calculated relative to the eval position
 * and normalized.
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is TRUE, then
 * callbacks are only invoked for existing cells.
 *
 * NOTE : Does not yet handle 3D references.
 *
 * Return value:
 *    non-NULL on error, or value_terminate() if some invoked routine requested
 *    to stop (by returning non-NULL).
 */
Value *
workbook_foreach_cell_in_range (EvalPos const *pos,
				Value const	*cell_range,
				gboolean	 only_existing,
				ForeachCellCB	 handler,
				void		*closure)
{
	Range  r;
	Sheet *start_sheet, *end_sheet;

	g_return_val_if_fail (pos != NULL, NULL);
	g_return_val_if_fail (cell_range != NULL, NULL);

	g_return_val_if_fail (cell_range->type == VALUE_CELLRANGE, NULL);

	range_ref_normalize  (&r, &start_sheet, &end_sheet, cell_range, pos);

	/* We can not support this until the Sheet management is tidied up.  */
	if (start_sheet != end_sheet)
		g_warning ("3D references are not supported yet, using 1st sheet");

	return sheet_cell_foreach_range (start_sheet, only_existing,
					 r.start.col, r.start.row,
					 r.end.col, r.end.row,
					 handler, closure);
}
