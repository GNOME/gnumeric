/*
 * workbook.c:  Workbook management (toplevel windows)
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org).
 *
 * (C) 1998, 1999, 2000 Miguel de Icaza
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "application.h"
#include "eval.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "main.h"
#include "file.h"
#include "xml-io.h"
#include "pixmaps.h"
#include "clipboard.h"
#include "utils.h"
#include "widgets/widget-editable-label.h"
#include "ranges.h"
#include "selection.h"
#include "print.h"
#include "value.h"
#include "widgets/gnumeric-toolbar.h"
#include "workbook-cmd-format.h"
#include "workbook-format-toolbar.h"
#include "workbook-view.h"
#include "command-context-gui.h"
#include "commands.h"
#include "widgets/gtk-combo-text.h"
#include "wizard.h"

#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-persist-file.h>
#include "sheet-object-container.h"
#include "embeddable-grid.h"
#endif

#include <ctype.h>

#include "workbook-private.h"

/* The locations within the main table in the workbook */
#define WB_EA_LINE   0
#define WB_EA_SHEETS 1
#define WB_EA_STATUS 2

#define WB_COLS      1

static int workbook_count;

static GList *workbook_list = NULL;

static WORKBOOK_PARENT_CLASS *workbook_parent_class;

/* Workbook signals */
enum {
	SHEET_CHANGED,
	CELL_CHANGED,
	LAST_SIGNAL
};

static gint workbook_signals [LAST_SIGNAL] = {
	0, /* SHEET_CHANGED, CELL_CHANGED */
};

static void workbook_set_focus (GtkWindow *window, GtkWidget *focus, Workbook *wb);
static int  workbook_can_close (Workbook *wb);

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

		if (workbook_is_pristine (wb)) {
#ifdef ENABLE_BONOBO
			bonobo_object_unref (BONOBO_OBJECT (wb));
#else
			gtk_object_unref (GTK_OBJECT (wb));
#endif
		}
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

		if (workbook_is_pristine (wb)) {
#ifdef ENABLE_BONOBO
			bonobo_object_unref (BONOBO_OBJECT (wb));
#else
			gtk_object_unref (GTK_OBJECT (wb));
#endif
		}
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
launch_graphics_wizard_cmd (GtkWidget *widget, Workbook *wb)
{
	graphics_wizard (wb);
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
	Sheet *sheet wb->current_sheet;
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
workbook_do_destroy_private (Workbook *wb)
{
	g_free (wb->priv);
}

static void
workbook_do_destroy (Workbook *wb)
{
	gtk_signal_disconnect_by_func (
		GTK_OBJECT (wb->toplevel),
		GTK_SIGNAL_FUNC (workbook_set_focus), wb);

	workbook_autosave_cancel (wb);

	if (!wb->needs_name)
		workbook_view_history_update (workbook_list, wb->filename);

	/*
	 * Do all deletions that leave the workbook in a working
	 * order.
	 */

	summary_info_free (wb->summary_info);
	wb->summary_info = NULL;

	command_list_release (wb->undo_commands);
	command_list_release (wb->redo_commands);

	/* Release the clipboard if it is associated with this workbook */
	{
		Sheet *tmp_sheet;
		if ((tmp_sheet = application_clipboard_sheet_get ()) != NULL &&
		    tmp_sheet->workbook == wb)
			application_clipboard_clear ();
	}

	/* Erase all cells.  This does NOT remove links between sheets
	 * but we don't care beacuse the other sheets are going to be
	 * deleted too.
	 */
	g_hash_table_foreach (wb->sheets, &cb_sheet_destroy_contents, NULL);

	if (wb->auto_expr_desc)
		string_unref (wb->auto_expr_desc);
	if (wb->auto_expr) {
		expr_tree_unref (wb->auto_expr);
		wb->auto_expr = NULL;
	}

	/* Problems with insert/delete column/row caused formula_cell_list
	   to be messed up.  */
	if (wb->formula_cell_list) {
		fprintf (stderr, "Reminder: FIXME in workbook_do_destroy\n");
		g_list_free (wb->formula_cell_list);
		wb->formula_cell_list = NULL;
	}

	/* Just drop the eval queue.  */
	g_list_free (wb->eval_queue);
	wb->eval_queue = NULL;

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

	symbol_table_destroy (wb->symbol_names);

	expr_name_clean_workbook (wb);

	gtk_object_destroy (GTK_OBJECT (wb->priv->gui_context));
	
	workbook_do_destroy_private (wb);

	if (!GTK_OBJECT_DESTROYED (wb->toplevel))
		gtk_object_destroy (GTK_OBJECT (wb->toplevel));
	
	if (workbook_count == 0) {
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
	if (workbook_can_close (wb)) {
#ifdef ENABLE_BONOBO
		if (wb->workbook_views) {
			gtk_widget_hide (GTK_WIDGET (wb->toplevel));
			return FALSE;
		}
		bonobo_object_unref (BONOBO_OBJECT (wb));
#else
		gtk_object_unref   (GTK_OBJECT   (wb));
#endif
		return FALSE;
	} else
		return TRUE;
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

void
workbook_mark_clean (Workbook *wb)
{
	g_return_if_fail (wb != NULL);

	g_hash_table_foreach (wb->sheets, cb_sheet_mark_dirty, GINT_TO_POINTER (0));
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
/*	{
		GtkEntry   *entry;
		const char *txt;
		Cell       *cell;

		if (sheet != sheet->workbook->current_sheet)
			return;

		entry = GTK_ENTRY (sheet->workbook->ea_input);
		txt   = gtk_entry_get_text (entry);
		cell = sheet_cell_get (sheet, sheet->cursor.edit_pos.col,
				       sheet->cursor.edit_pos.row);
		if (!cell) {
			if (!strlen (txt))
				return;
		} else {
			char *cell_txt = cell_get_text (cell);
			gboolean same  = !strcmp (txt, cell_txt);
			g_free (cell_txt);
			if (same)
				return;
		}
		return;
		}*/

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

	if (wb->names || wb->formula_cell_list ||
#ifdef ENABLE_BONOBO
	    wb->workbook_views ||
#endif
	    wb->eval_queue || !wb->needs_name)
		return FALSE;

	/* Check if we seem to contain anything */
	g_hash_table_foreach (wb->sheets, cb_sheet_check_pristine,
			      &pristine);

	return pristine;
}

static int
workbook_can_close (Workbook *wb)
{
	gboolean   can_close = TRUE;
	gboolean   done      = FALSE;
	int        iteration = 0;
	static int in_can_close;

	if (in_can_close)
		return FALSE;
	
	in_can_close = TRUE;
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
			workbook_mark_clean (wb);
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
	
	return can_close;
}

static void
close_cmd (GtkWidget *widget, Workbook *wb)
{
	if (workbook_can_close (wb)){
#ifdef ENABLE_BONOBO
		bonobo_object_unref (BONOBO_OBJECT (wb));
#else
		gtk_object_unref (GTK_OBJECT (wb));
#endif
	}
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

		if (workbook_can_close (wb)){
#ifdef ENABLE_BONOBO
			bonobo_object_unref (BONOBO_OBJECT (wb));
#else
			gtk_object_unref (GTK_OBJECT (wb));
#endif
		}
	}

	g_list_free (n);
}

static void
accept_input (GtkWidget *IGNORED, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;

	/* Force sheet into edit mode */
	sheet->editing = TRUE;
	sheet_accept_pending_input (sheet);
	workbook_focus_current_sheet (wb);
}

static void
cancel_input (GtkWidget *IGNORED, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet_cancel_pending_input (sheet);
	workbook_focus_current_sheet (wb);
}

static void
undo_cmd (GtkWidget *widget, Workbook *wb)
{
	cancel_input (NULL, wb);
	command_undo (workbook_command_context_gui (wb), wb);
}

static void
redo_cmd (GtkWidget *widget, Workbook *wb)
{
	cancel_input (NULL, wb);
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

	sheet_selection_paste (workbook_command_context_gui (wb), sheet,
			       sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row,
			       PASTE_DEFAULT, GDK_CURRENT_TIME);
}

static void
paste_special_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	int flags;

	/* These menu items should be insensitive when there is nothing to paste */
	g_return_if_fail (!application_clipboard_is_empty ());

	flags = dialog_paste_special (wb);
	if (flags != 0)
		sheet_selection_paste (workbook_command_context_gui (wb), sheet,
				       sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row,
				       flags, GDK_CURRENT_TIME);

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

static void
select_all_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;

	sheet_select_all (sheet);
	sheet_redraw_all (sheet);
}

static void
goto_cell_cmd (GtkWidget *unused, Workbook *wb)
{
	dialog_goto_cell (wb);
}

static void
define_cell_cmd (GtkWidget *unused, Workbook *wb)
{
	dialog_define_names (wb);
}

static void
insert_sheet_cmd (GtkWidget *unused, Workbook *wb)
{
	Sheet *sheet;
	char *name;

	name = workbook_sheet_get_free_name (wb);
	sheet = sheet_new (wb, name);
	g_free (name);

	workbook_attach_sheet (wb, sheet);
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

static void
cb_cell_rerender (gpointer cell, gpointer data)
{
        cell_render_value (cell);
        cell_queue_redraw (cell);
}

/***********************************************************************/
/* Sheet preferences */

static void
cb_sheet_pref_display_formulas (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	sheet->display_formulas = !sheet->display_formulas;
	g_list_foreach (wb->formula_cell_list, &cb_cell_rerender, NULL);
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

/***********************************************************************/
/* Workbook level preferences */

static void
cb_wb_pref_hide_hscroll (GtkWidget *widget, Workbook *wb)
{
	wb->show_horizontal_scrollbar = ! wb->show_horizontal_scrollbar;
	workbook_view_pref_visibility (wb);
}

static void
cb_wb_pref_hide_vscroll (GtkWidget *widget, Workbook *wb)
{
	wb->show_vertical_scrollbar = ! wb->show_vertical_scrollbar;
	workbook_view_pref_visibility (wb);
}

static void
cb_wb_pref_hide_tabs (GtkWidget *widget, Workbook *wb)
{
	wb->show_notebook_tabs = ! wb->show_notebook_tabs;
	workbook_view_pref_visibility (wb);
}

/***********************************************************************/

static void
format_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	dialog_cell_format (wb, sheet);
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
}

static void
insert_current_date_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	cmd_set_date_time (workbook_command_context_gui (wb), TRUE,
			   sheet, sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row);
}

static void
insert_current_time_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	cmd_set_date_time (workbook_command_context_gui (wb), TRUE,
			   sheet, sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row);
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
		cell_set_value (cell, value_new_empty ());
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

#ifdef ENABLE_BONOBO
static void
insert_object_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	char *goadid;

	goadid = gnome_bonobo_select_goad_id (_("Select an object"), NULL);
	if (goadid != NULL)
		sheet_insert_object (sheet, goadid);
}
#endif

/* File menu */

static GnomeUIInfo workbook_menu_file [] = {
        GNOMEUIINFO_MENU_NEW_ITEM(N_("_New"), N_("Create a new spreadsheet"),
				  new_cmd, NULL),

	GNOMEUIINFO_MENU_OPEN_ITEM(file_open_cmd, NULL),
	GNOMEUIINFO_ITEM_STOCK (N_("_Import..."), N_("Imports a file"),
				file_import_cmd, GNOME_STOCK_MENU_OPEN),
	GNOMEUIINFO_MENU_SAVE_ITEM(file_save_cmd, NULL),

	GNOMEUIINFO_MENU_SAVE_AS_ITEM(file_save_as_cmd, NULL),

	{ GNOME_APP_UI_ITEM, N_("Su_mmary..."), N_("Summary information"),
	  summary_cmd },

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_PRINT_SETUP_ITEM(print_setup_cmd, NULL),
	GNOMEUIINFO_MENU_PRINT_ITEM(file_print_cmd, NULL),
	{ GNOME_APP_UI_ITEM, N_("Print pre_view"), N_("Print preview"),
	  file_print_preview_cmd },
	
	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_CLOSE_ITEM(close_cmd, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_EXIT_ITEM(quit_cmd, NULL),
	GNOMEUIINFO_END
};

/* Edit menu */

static GnomeUIInfo workbook_menu_edit_clear [] = {
	{ GNOME_APP_UI_ITEM, N_("_All"),
	  N_("Clear the selected cells' formats, comments, and contents"),
	  clear_all_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Formats"),
	  N_("Clear the selected cells' formats"),
	  clear_formats_cmd },
	{ GNOME_APP_UI_ITEM, N_("Co_mments"),
	  N_("Clear the selected cells' comments"),
	  clear_comments_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Content"),
	  N_("Clear the selected cells' contents"),
	  clear_content_cmd },
	GNOMEUIINFO_END
};

#define PASTE_SPECIAL_NAME N_("P_aste special...")
#define GNOME_MENU_EDIT_PATH D_("_Edit/")

static GnomeUIInfo workbook_menu_edit [] = {
	GNOMEUIINFO_MENU_UNDO_ITEM(undo_cmd, NULL),
	GNOMEUIINFO_MENU_REDO_ITEM(redo_cmd, NULL),

	GNOMEUIINFO_SEPARATOR,

        GNOMEUIINFO_MENU_CUT_ITEM(cut_cmd, NULL),
	GNOMEUIINFO_MENU_COPY_ITEM(copy_cmd, NULL),
	GNOMEUIINFO_MENU_PASTE_ITEM(paste_cmd, NULL),

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, PASTE_SPECIAL_NAME, NULL,
	  paste_special_cmd },
        { GNOME_APP_UI_SUBTREE, N_("C_lear"),
	  N_("Clear the selected cell(s)"), workbook_menu_edit_clear },
        { GNOME_APP_UI_ITEM, N_("_Delete..."), NULL,
	  delete_cells_cmd },
        { GNOME_APP_UI_ITEM, N_("De_lete Sheet"), NULL,
	  delete_sheet_cmd },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_Select All"),
	  N_("Select all cells in the spreadsheet"), select_all_cmd, NULL,
	  NULL, 0, 0, 'a', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Goto cell..."),
	  N_("Jump to a specified cell"), goto_cell_cmd, NULL, NULL,
	  0, 0, 'i', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("_Recalculate"),
	  N_("Recalculate the spreadsheet"), recalc_cmd, NULL, NULL,
	  0, 0, GDK_F9, 0 },
	GNOMEUIINFO_END
};

/* View menu */

static GnomeUIInfo workbook_menu_view [] = {
	{ GNOME_APP_UI_ITEM, N_("_Zoom..."),
	  N_("Zoom the spreadsheet in or out"), zoom_cmd },
	GNOMEUIINFO_END
};

/* Insert menu */

static GnomeUIInfo workbook_menu_insert_special [] = {
	{ GNOME_APP_UI_ITEM, N_("Current _date"),
	  N_("Insert the current date into the selected cell(s)"),
	  insert_current_date_cmd,
	  NULL, NULL, 0, 0, ';', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("Current _time"),
	  N_("Insert the current time into the selected cell(s)"),
	  insert_current_time_cmd,
	  NULL, NULL, 0, 0, ':', GDK_CONTROL_MASK | GDK_SHIFT_MASK },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_insert [] = {
	{ GNOME_APP_UI_ITEM, N_("_Sheet"), N_("Insert a new spreadsheet"),
	  insert_sheet_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Rows"), N_("Insert new rows"),
	  insert_rows_cmd  },
	{ GNOME_APP_UI_ITEM, N_("_Columns"), N_("Insert new columns"),
	  insert_cols_cmd  },
	{ GNOME_APP_UI_ITEM, N_("C_ells..."), N_("Insert new cells"),
	  insert_cells_cmd },

#ifdef ENABLE_BONOBO
	GNOMEUIINFO_ITEM_NONE(N_("_Object..."),
			      N_("Inserts a Bonobo object"),
			      insert_object_cmd),
#endif

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("Define _Name"), NULL, define_cell_cmd },

	{ GNOME_APP_UI_ITEM, N_("_Add/modify comment..."),
	  N_("Edit the selected cell's comment"), workbook_edit_comment },
	{ GNOME_APP_UI_SUBTREE, N_("S_pecial"), NULL, workbook_menu_insert_special },

	GNOMEUIINFO_END
};

/* Format menu */

static GnomeUIInfo workbook_menu_format_column [] = {
	{ GNOME_APP_UI_ITEM, N_("_Auto fit selection"),
	  NULL, workbook_cmd_format_column_auto_fit },
	{ GNOME_APP_UI_ITEM, N_("_Width"),
	  NULL, workbook_cmd_format_column_width },
	{ GNOME_APP_UI_ITEM, N_("_Hide"),
	  NULL, workbook_cmd_format_column_hide },
	{ GNOME_APP_UI_ITEM, N_("_Unhide"),
	  NULL, workbook_cmd_format_column_unhide },
	{ GNOME_APP_UI_ITEM, N_("_Standard Width"),
	  NULL, workbook_cmd_format_column_std_width },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_row [] = {
	{ GNOME_APP_UI_ITEM, N_("_Auto fit selection"),
	  NULL,  workbook_cmd_format_row_auto_fit },
	{ GNOME_APP_UI_ITEM, N_("_Height"),
	  NULL, workbook_cmd_format_row_height },
	{ GNOME_APP_UI_ITEM, N_("_Hide"),
	  NULL, workbook_cmd_format_row_hide },
	{ GNOME_APP_UI_ITEM, N_("_Unhide"),
	  NULL, workbook_cmd_format_row_unhide },
	{ GNOME_APP_UI_ITEM, N_("_Standard Height"),
	  NULL, workbook_cmd_format_row_std_height },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_sheet [] = {
	{ GNOME_APP_UI_ITEM, N_("_Change name"),   NULL,
	  workbook_cmd_format_sheet_change_name },

	{ GNOME_APP_UI_TOGGLEITEM, N_("Display _Formulas"),
	    NULL, &cb_sheet_pref_display_formulas },
	{ GNOME_APP_UI_TOGGLEITEM, N_("Hide _Zeros"),
	    NULL, &cb_sheet_pref_hide_zeros },

	{ GNOME_APP_UI_TOGGLEITEM, N_("Hide _Gridlines"),
	    NULL, &cb_sheet_pref_hide_grid_lines },
	{ GNOME_APP_UI_TOGGLEITEM, N_("Hide _Column Header"),
	    NULL, &cb_sheet_pref_hide_col_header },
	{ GNOME_APP_UI_TOGGLEITEM, N_("Hide _Row Header"),
	    NULL, &cb_sheet_pref_hide_row_header },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_workbook [] = {
	{ GNOME_APP_UI_TOGGLEITEM, N_("Hide _Horizontal Scrollbar"),
	    NULL, &cb_wb_pref_hide_hscroll },
	{ GNOME_APP_UI_TOGGLEITEM, N_("Hide _Vertical Scrollbar"),
	    NULL, &cb_wb_pref_hide_vscroll },
	{ GNOME_APP_UI_TOGGLEITEM, N_("Hide Sheet _Tabs"),
	    NULL, &cb_wb_pref_hide_tabs },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format [] = {
	{ GNOME_APP_UI_ITEM, N_("_Cells..."),
	  N_("Modify the formatting of the selected cells"),
	  format_cells_cmd, NULL, NULL, 0, 0, GDK_1, GDK_CONTROL_MASK },
	{ GNOME_APP_UI_SUBTREE, N_("C_olumn"), NULL, workbook_menu_format_column },
	{ GNOME_APP_UI_SUBTREE, N_("_Row"),    NULL, workbook_menu_format_row },
	{ GNOME_APP_UI_SUBTREE, N_("_Sheet"),  NULL, workbook_menu_format_sheet },
	{ GNOME_APP_UI_SUBTREE, N_("_Workbook"),  NULL, workbook_menu_format_workbook },
	GNOMEUIINFO_END
};

/* Tools menu */
static GnomeUIInfo workbook_menu_tools [] = {
	{ GNOME_APP_UI_ITEM, N_("_Plug-ins..."), N_("Gnumeric plugins"),
	  plugins_cmd },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("Auto _Correct..."), N_("Auto Correct"),
	  autocorrect_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Auto Save..."), N_("Auto Save"),
	  autosave_cmd },
	GNOMEUIINFO_SEPARATOR,
	{ GNOME_APP_UI_ITEM, N_("_Goal Seek..."), NULL, goal_seek_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Solver..."),    NULL, solver_cmd },
	GNOMEUIINFO_SEPARATOR,
	{ GNOME_APP_UI_ITEM, N_("_Data Analysis..."), NULL, data_analysis_cmd },
	GNOMEUIINFO_END
};

/* Data menu */
static GnomeUIInfo workbook_menu_data [] = {
	{ GNOME_APP_UI_ITEM, N_("_Sort..."),
	  N_("Sort the selected cells"), sort_cells_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Filter..."),
	  N_("Filter date with given criterias"), advanced_filter_cmd },
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
	{ GNOME_APP_UI_SUBTREE, N_("_Insert"), NULL, workbook_menu_insert },
	{ GNOME_APP_UI_SUBTREE, N_("F_ormat"), NULL, workbook_menu_format },
	{ GNOME_APP_UI_SUBTREE, N_("_Tools"), NULL, workbook_menu_tools },
	{ GNOME_APP_UI_SUBTREE, N_("_Data"), NULL, workbook_menu_data },
#ifdef ENABLE_BONOBO
#warning Should enable this when Bonobo gets menu help support
#else
	GNOMEUIINFO_MENU_HELP_TREE (workbook_menu_help),
#endif
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_standard_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("New"), N_("Creates a new sheet"),
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
	
#ifdef ENABLE_BONOBO
	GNOMEUIINFO_ITEM_DATA (
		N_("Creates a graphic"), N_("Invokes the graphic wizard to create a graphic"),
		launch_graphics_wizard_cmd, NULL, graphic_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Insert Object"), N_("Inserts an object in the spreadsheet"),
		create_embedded_component_cmd, NULL, object_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Insert shaped object"), N_("Inserts a shaped object in the spreadsheet"),
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

	GNOMEUIINFO_SEPARATOR,

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

	GNOMEUIINFO_END
};

static void
do_focus_sheet (GtkNotebook *notebook, GtkNotebookPage *page, guint page_num, Workbook *wb)
{
	/* Cache the current sheet BEFORE we switch*/
	Sheet *sheet = wb->current_sheet;

	/* FIXME : Cancel any pending editing if there was a previous sheet.
	 * This needs to be fixed to handle selecting cells on a different sheet.
	 * Currently that logic is too closely coupled to GnumericSheet to do that easily.
	 *
	 * NOTE : Excel ACCEPTS the input and cancels the sheet switch if the input fails.
	 */
	if (sheet != NULL)
		sheet_cancel_pending_input (sheet);

	/* Look up the sheet associated with the currently select
	 * notebook tab */
	sheet = workbook_focus_current_sheet (wb);

	/* FIXME : For now we will set the edit area no matter what.
	 * eventually we should hande cell selection across sheets.
	 */
	sheet_load_cell_val (sheet);
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

		gtk_window_set_focus (GTK_WINDOW (wb->toplevel), sheet_view->sheet_view);

		gtk_signal_emit (GTK_OBJECT (wb), workbook_signals [SHEET_CHANGED], sheet);
		
		wb->current_sheet = sheet;
	} else
		g_warning ("There is no current sheet in this workbook");

	return sheet;
}

void
workbook_focus_sheet (Sheet *sheet)
{
	GtkNotebook *notebook;
	int sheets, i;

	g_return_if_fail (sheet);
	g_return_if_fail (sheet->workbook);
	g_return_if_fail (sheet->workbook->notebook);
	g_return_if_fail (IS_SHEET (sheet));

	notebook = GTK_NOTEBOOK (sheet->workbook->notebook);
	sheets = workbook_sheet_count (sheet->workbook);

	if (sheets == 1)
		return;

	for (i = 0; i < sheets; i++){
		Sheet *this_sheet;
		GtkWidget *w;

		w = gtk_notebook_get_nth_page (notebook, i);

		this_sheet = gtk_object_get_data (GTK_OBJECT (w), "sheet");

		if (this_sheet == sheet){
			gtk_notebook_set_page (notebook, i);
			break;
		}
	}
}

static int
wb_edit_key_pressed (GtkEntry *entry, GdkEventKey *event, Workbook *wb)
{
	switch (event->keyval) {
	case GDK_Escape:
		sheet_cancel_pending_input (wb->current_sheet);
		workbook_focus_current_sheet (wb);
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

		/* Only apply do this for formulas */
		if (!gnumeric_char_start_expr_p (entry->text[0]) ||
		    entry->text_length < 1)
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
		while (isdigit (entry->text[end_pos]))
			++end_pos;

		/*
		 * Try to find the begining of the current range
		 * starting from the end we just found
		 */
		start_pos = end_pos - 1;
		while (start_pos >= 0 && isdigit (entry->text[start_pos]))
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

	default:
		return FALSE;
	}
}


int
workbook_parse_and_jump (Workbook *wb, const char *text)
{
	int col, row;

	if (!parse_cell_name (text, &col, &row)){
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell name"));
		return FALSE;
	} else {
		Sheet *sheet = wb->current_sheet;

#if 0
		/* This cannot happen anymore, see parse_cell_name.  */
		if (col > SHEET_MAX_COLS-1){
			gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("Column out of range"));
			return FALSE;
		}

		if (row > SHEET_MAX_ROWS-1){
			gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("Row number out of range"));
			return FALSE;
		}
#endif

		sheet_make_cell_visible (sheet, col, row);
		sheet_cursor_move (sheet, col, row, TRUE, TRUE);
		return TRUE;
	}
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

	gtk_entry_set_text (GTK_ENTRY (wb->priv->ea_status), str);
}

static void
wizard_input (GtkWidget *widget, Workbook *wb)
{
	FunctionDefinition *fd = dialog_function_select (wb);
	GtkEntry *entry = GTK_ENTRY (wb->ea_input);
	gchar *txt, *edittxt;
	int pos;

	if (!fd)
		return;

	txt = dialog_function_wizard (wb, fd);

       	if (!txt || !wb || !entry)
		return;

	pos = gtk_editable_get_position (GTK_EDITABLE (entry));

	gtk_editable_insert_text (GTK_EDITABLE (entry),
				  txt, strlen (txt), &pos);
	g_free (txt);
	txt = gtk_entry_get_text (entry);

	if (!gnumeric_char_start_expr_p (txt[0]))
	        edittxt = g_strconcat ("=", txt, NULL);
	else
		edittxt = g_strdup (txt);
	gtk_entry_set_text (entry, edittxt);
	g_free (edittxt);
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
	GtkWidget *ok_button, *cancel_button, *wizard_button;
	GtkWidget *pix, *deps_button, *box, *box2;

	wb->priv->ea_status = gtk_entry_new ();
	wb->ea_input  = gtk_entry_new ();
	ok_button     = gtk_button_new ();
	cancel_button = gtk_button_new ();
	box           = gtk_hbox_new (0, 0);
	box2          = gtk_hbox_new (0, 0);

	gtk_widget_set_usize (wb->priv->ea_status, 100, 0);

	/* Ok */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_OK);
	gtk_container_add (GTK_CONTAINER (ok_button), pix);
	GTK_WIDGET_UNSET_FLAGS (ok_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (ok_button), "clicked",
			    GTK_SIGNAL_FUNC (accept_input), wb);

	/* Cancel */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_CANCEL);
	gtk_container_add (GTK_CONTAINER (cancel_button), pix);
	GTK_WIDGET_UNSET_FLAGS (cancel_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cancel_input), wb);

	gtk_box_pack_start (GTK_BOX (box2), wb->priv->ea_status, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), ok_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), cancel_button, 0, 0, 0);

	/* Function Wizard */
	if (gnumeric_debugging > 0) {
		wizard_button = gtk_button_new ();
		pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_PIXMAP_BOOK_GREEN);
		gtk_container_add (GTK_CONTAINER (wizard_button), pix);
		GTK_WIDGET_UNSET_FLAGS (wizard_button, GTK_CAN_FOCUS);
		gtk_signal_connect (GTK_OBJECT (wizard_button), "clicked",
				    GTK_SIGNAL_FUNC (wizard_input), wb);
		gtk_box_pack_start (GTK_BOX (box), wizard_button, 0, 0, 0);
	}

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
	gtk_box_pack_end   (GTK_BOX (box2), wb->ea_input, 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wb->priv->table), box2,
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* Do signal setup for the editing input line */
	gtk_signal_connect (GTK_OBJECT (wb->ea_input), "activate",
			    GTK_SIGNAL_FUNC (accept_input),
			    wb);
	gtk_signal_connect (GTK_OBJECT (wb->ea_input), "key_press_event",
			    GTK_SIGNAL_FUNC (wb_edit_key_pressed),
			    wb);

	/* Do signal setup for the status input line */
	gtk_signal_connect (GTK_OBJECT (wb->priv->ea_status), "activate",
			    GTK_SIGNAL_FUNC (wb_jump_to_cell),
			    wb);
}

static struct {
	char *displayed_name;
	char *function;
} quick_compute_routines [] = {
	{ N_("Sum"),   	       "SUM(SELECTION(FALSE))" },
	{ N_("Min"),   	       "MIN(SELECTION(FALSE))" },
	{ N_("Max"),   	       "MAX(SELECTION(FALSE))" },
	{ N_("Average"),       "AVERAGE(SELECTION(FALSE))" },
	{ N_("Count"),         "COUNT(SELECTION(FALSE))" },
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
	ParsePosition pp;

	if (wb->auto_expr)
		expr_tree_unref (wb->auto_expr);

	g_assert (gnumeric_expr_parser (expression, parse_pos_init (&pp, wb, 0, 0),
					NULL, &wb->auto_expr) == PARSE_OK);

	if (wb->auto_expr_desc)
		string_unref (wb->auto_expr_desc);

	wb->auto_expr_desc = string_get (description);
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
	wb->priv->appbar = GNOME_APPBAR (gnome_appbar_new (FALSE, TRUE,
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

static void
workbook_set_focus (GtkWindow *window, GtkWidget *focus, Workbook *wb)
{
	if (!window->focus_widget)
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
	wb->workbook_views = g_list_remove (wb->workbook_views, embeddable_grid);
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

		if (!range_parse (sheet, p, &range))
			range = NULL;
		else {
			CellRef *a = &range->v.cell_range.cell_a;
			CellRef *b = &range->v.cell_range.cell_b;
			
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
		CellRef *a = &range->v.cell_range.cell_a;
		CellRef *b = &range->v.cell_range.cell_b;

		embeddable_grid_set_range (eg, a->col, a->row, b->col, b->row);
	}

	gtk_signal_connect (GTK_OBJECT (eg), "destroy",
			    grid_destroyed, wb);
	
	wb->workbook_views = g_list_prepend (wb->workbook_views, eg);

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
	wb->bonobo_container = BONOBO_CONTAINER (bonobo_container_new ());
	wb->persist_file = bonobo_persist_file_new (
		workbook_persist_file_load,
		workbook_persist_file_save,
		wb);
		
	bonobo_object_add_interface (
		BONOBO_OBJECT (wb),
		BONOBO_OBJECT (wb->bonobo_container));
	bonobo_object_add_interface (
		BONOBO_OBJECT (wb),
		BONOBO_OBJECT (wb->persist_file));
	
	gtk_signal_connect (
		GTK_OBJECT (wb->bonobo_container), "get_object",
		GTK_SIGNAL_FUNC (workbook_container_get_object), wb);
}
#endif

static void
workbook_init (GtkObject *object)
{
	Workbook *wb = WORKBOOK (object);

	wb->priv = g_new (WorkbookPrivate, 1);
	
	wb->sheets       = g_hash_table_new (gnumeric_strcase_hash, gnumeric_strcase_equal);
	wb->current_sheet= NULL;
	wb->names        = NULL;
	wb->symbol_names = symbol_table_new ();
	wb->max_iterations = 1;
	wb->summary_info   = summary_info_new ();
	summary_info_default (wb->summary_info);

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
	workbook_parent_class = gtk_type_class (WORKBOOK_PARENT_CLASS_TYPE);

	workbook_signals [SHEET_CHANGED] =
		gtk_signal_new (
			"sheet_changed",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (WorkbookClass,
					   sheet_changed),
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE,
			1,
			GTK_TYPE_POINTER);
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

#ifdef ENABLE_BONOBO
		type = gtk_type_unique (bonobo_object_get_type (), &info);
#else
		type = gtk_type_unique (gtk_object_get_type (), &info);
#endif
	}

	return type;
}

static void
change_zoom_in_current_sheet_cb (GtkWidget *caller, Workbook *wb)
{
	int factor = atoi (gtk_entry_get_text (GTK_ENTRY (caller)));
	sheet_set_zoom_factor(wb->current_sheet, (double)factor / 100);
}

static void
change_displayed_zoom_cb (GtkObject *caller, Sheet* sheet, gpointer data)
{
	GtkWidget *combo;
	gchar *str;
	int factor = (int) (sheet->last_zoom_factor_used * 100);
	
	g_return_if_fail (combo = WORKBOOK (caller)->priv->zoom_entry);

	str = g_strdup_printf("%d%%", factor);
	
	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO_TEXT (combo)->entry),
			    str);

	g_free (str);
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

/*
 * These create toolbar routines are kept independent, as they
 * will need some manual customization in the future (like adding
 * special purposes widgets for fonts, size, zoom
 */
static GtkWidget *
workbook_create_standard_toobar (Workbook *wb)
{
	char const * const preset_zoom[] =
	{
	    "200%",
	    "100%",
	    "75%",
	    "50%",
	    "25%",
	    NULL
	};
	int i, len;
	
	GtkWidget *toolbar, *zoom, *entry;
	
	const char *name = "StandardToolbar";
	
	toolbar = gnumeric_toolbar_new (workbook_standard_toolbar, wb);

	gnome_app_add_toolbar (
		GNOME_APP (wb->toplevel),
		GTK_TOOLBAR (toolbar),
		name,
		GNOME_DOCK_ITEM_BEH_NORMAL,
		GNOME_DOCK_TOP, 1, 0, 0);

	/* Zoom combo box */
	zoom = wb->priv->zoom_entry = gtk_combo_text_new ();
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (zoom), GTK_RELIEF_NONE);
	entry = GTK_COMBO_TEXT (zoom)->entry;
	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (change_zoom_in_current_sheet_cb), wb);

	/* Change the value when the displayed sheet is changed */
	gtk_signal_connect (GTK_OBJECT (wb), "sheet_changed",
			    (GtkSignalFunc) (change_displayed_zoom_cb), NULL);
	
	/* Set a reasonable default width */
	len = gdk_string_measure (entry->style->font, "000000");
	gtk_widget_set_usize (entry, len, 0);

	/* Preset values */
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		gtk_combo_text_add_item(GTK_COMBO_TEXT (zoom),
					preset_zoom[i], preset_zoom[i]);

	/* Add it to the toolbar */
	gtk_widget_show (zoom);
	gtk_toolbar_append_widget (GTK_TOOLBAR (toolbar),
				   zoom, _("Zoom"), NULL);


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
	 * unless we scrolled al lthe views at once which is ugly.
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

	gtk_window_set_policy (GTK_WINDOW (wb->toplevel), 1, 1, 0);
	sx = MAX (gdk_screen_width  () - 64, 400);
	sy = MAX (gdk_screen_height () - 64, 200);
	sx = (sx * 3) / 4;
	sy = (sy * 3) / 4;
	workbook_view_set_size (wb, sx, sy);

	/* Assign a default name */
	do {
		char *name = g_strdup_printf (_("Book%d.gnumeric"), ++count);
		is_unique = workbook_set_filename (wb, name);
		g_free (name);
	} while (!is_unique);
	wb->needs_name = TRUE;

	workbook_setup_status_area (wb);
	workbook_setup_edit_area (wb);
	workbook_setup_sheets (wb);
	gnome_app_set_contents (GNOME_APP (wb->toplevel), wb->priv->table);

	wb->priv->gui_context = command_context_gui_new (wb);
	
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

		wb->workbook_views  = NULL;
		wb->persist_file    = NULL;
		wb->uih = bonobo_ui_handler_new ();
		bonobo_ui_handler_set_app (wb->uih, GNOME_APP (wb->toplevel));
		bonobo_ui_handler_create_menubar (wb->uih);
		list = bonobo_ui_handler_menu_parse_uiinfo_list_with_data (workbook_menu, wb);
		bonobo_ui_handler_menu_add_list (wb->uih, "/", list);
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

	gtk_signal_connect (
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

	gtk_widget_show_all (wb->priv->table);

	return wb;
}

/**
 * workbook_rename_sheet:
 * @wb:       the workbook where the sheet is
 * @old_name: the name of the sheet we want to rename
 * @new_name: new name we want to assing to the sheet.
 *
 * Returns TRUE if it was possible to rename the sheet to @new_name,
 * otherwise it returns FALSE for any possible error condition.
 */
gboolean
workbook_rename_sheet (Workbook *wb, const char *old_name, const char *new_name)
{
	Sheet *sheet;
	GtkNotebook *notebook;
	int sheets, i;

	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (old_name != NULL, FALSE);
	g_return_val_if_fail (new_name != NULL, FALSE);

	/* Do not let two sheets in the workbook have the same name */
	if (g_hash_table_lookup (wb->sheets, new_name))
		return FALSE;

	sheet = (Sheet *) g_hash_table_lookup (wb->sheets, old_name);
	if (sheet == NULL)
		return FALSE;

	g_hash_table_remove (wb->sheets, old_name);
	sheet_rename (sheet, new_name);
	g_hash_table_insert (wb->sheets, sheet->name, sheet);

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

	return TRUE;
}

/*
 * Signal handler for EditableLabel's text_changed signal.
 */
static gboolean
sheet_label_text_changed_signal (EditableLabel *el, const char *new_name, Workbook *wb)
{
	gboolean ans;

	/* FIXME : Why do we care ?
	 * Why are the tests here ?
	 */
	if (strchr (new_name, '"'))
		return FALSE;
	if (strchr (new_name, '\''))
		return FALSE;

	ans = cmd_rename_sheet (workbook_command_context_gui (wb),
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

/**
 * sheet_action_delete_sheet:
 * Invoked when the user selects the option to remove a sheet
 */
static void
sheet_action_delete_sheet (GtkWidget *ignored, Sheet *current_sheet)
{
	GtkWidget *d;
	Workbook *wb = current_sheet->workbook;
	char *message;
	int r;

	/*
	 * If this is the last sheet left, ignore the request
	 */
	if (workbook_sheet_count (wb) == 1)
		return;

	message = g_strdup_printf (
		_("Are you sure you want to remove the sheet called `%s'?"),
		current_sheet->name);

	d = gnome_message_box_new (
		message, GNOME_MESSAGE_BOX_QUESTION,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	g_free (message);

	r = gnumeric_dialog_run (wb, GNOME_DIALOG (d));

	if (r != 0)
		return;

	/*
	 * Invalidate references to the deleted sheet from other sheets in the
	 * workbook by pretending to move the contents of the deleted sheet
	 * to infinity (and beyond)
	 */
	{
		ExprRelocateInfo rinfo;
		rinfo.origin.start.col = 0;
		rinfo.origin.start.row = 0;
		rinfo.origin.end.col = SHEET_MAX_COLS-1;
		rinfo.origin.end.row = SHEET_MAX_ROWS-1;
		rinfo.origin_sheet = rinfo.target_sheet = current_sheet;
		rinfo.col_offset = SHEET_MAX_COLS-1;
		rinfo.row_offset = SHEET_MAX_ROWS-1;

		workbook_expr_unrelocate_free (workbook_expr_relocate (wb, &rinfo));
	}

	/* Clear the cliboard to avoid dangling references to the deleted sheet */
	if (current_sheet == application_clipboard_sheet_get ())
		application_clipboard_clear ();

	/*
	 * All is fine, remove the sheet
	 */
	workbook_detach_sheet (wb, current_sheet, FALSE);

	workbook_recalc_all (wb);
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
	
	new_name = dialog_get_sheet_name (wb, current_sheet->name);
	if (!new_name)
		return;

	/* We do not care if it fails */
	(void) cmd_rename_sheet (workbook_command_context_gui (wb),
				 wb, current_sheet->name, new_name);
	g_free (new_name);
}

#define SHEET_CONTEXT_TEST_SIZE 1

struct {
	const char *text;
	void (*function) (GtkWidget *widget, Sheet *sheet);
	int  flags;
} sheet_label_context_actions [] = {
	{ N_("Add another sheet"), sheet_action_add_sheet, 0 },
	{ N_("Remove this sheet"), sheet_action_delete_sheet, SHEET_CONTEXT_TEST_SIZE },
	{ N_("Rename this sheet"), sheet_action_rename_sheet, 0 },
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

/**
 * workbook_attach_sheet:
 * @wb: the target workbook
 * @sheet: a sheet
 *
 * Attaches the @sheet to the @wb.
 */
void
workbook_attach_sheet (Workbook *wb, Sheet *sheet)
{
	GtkWidget *t, *sheet_label;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (sheet != NULL);

	/*
	 * We do not want to attach sheets that are attached
	 * to a different workbook.
	 */
	g_return_if_fail (sheet->workbook == wb || sheet->workbook == NULL);

	sheet->workbook = wb;

	g_hash_table_insert (wb->sheets, sheet->name, sheet);

	t = gtk_table_new (0, 0, 0);
	gtk_table_attach (
		GTK_TABLE (t), GTK_WIDGET (sheet->sheet_views->data),
		0, 3, 0, 1, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

	gtk_widget_show_all (t);
	gtk_object_set_data (GTK_OBJECT (t), "sheet", sheet);

	sheet_label = editable_label_new (sheet->name);
	/*
	 * NB. this is so we can use editable_label_set_text since
	 * gtk_notebook_set_tab_label kills our widget & replaces with a label.
	 */
	gtk_object_set_data (GTK_OBJECT (t), "sheet_label", sheet_label);
	gtk_signal_connect (
		GTK_OBJECT (sheet_label), "text_changed",
		GTK_SIGNAL_FUNC (sheet_label_text_changed_signal), wb);
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
	g_return_val_if_fail (workbook_sheet_lookup (wb, sheet->name) == sheet, FALSE);

	notebook = GTK_NOTEBOOK (wb->notebook);
	sheets = workbook_sheet_count (sheet->workbook);

	/*
	 * Remove our reference to this sheet
	 */
	g_hash_table_remove (wb->sheets, sheet->name);

	for (i = 0; i < sheets; i++){
		Sheet *this_sheet;
		GtkWidget *w;

		w = gtk_notebook_get_nth_page (notebook, i);

		this_sheet = gtk_object_get_data (GTK_OBJECT (w), "sheet");

		if (this_sheet == sheet){
			gtk_notebook_remove_page (notebook, i);
			break;
		}
	}

	sheet_destroy (sheet);

	/*
	 * Queue a recalc
	 */
	workbook_recalc_all (wb);

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
 * workbook_sheet_get_free_name:
 * @wb: workbook to look for
 *
 * Gets a new name for a sheets such that it does
 * not exist on the workbook.
 *
 * Returns the name assigned to the sheet.
 */
char *
workbook_sheet_get_free_name (Workbook *wb)
{
        const char *name_format = _("Sheet%d");
	char *name = g_malloc (strlen (name_format) + 4 * sizeof (int));
	int  i;

	g_return_val_if_fail (wb != NULL, NULL);

	for (i = 1; ; i++){
		sprintf (name, name_format, i);
		if (workbook_sheet_lookup (wb, name) == NULL)
			return name;
	}
	g_assert_not_reached ();
	g_free (name);
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

	wb->needs_name = FALSE;
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
	EvalPosition pos;
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

		cell_set_formula_tree (cell, tmp->oldtree);
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
	GList *cells, *l;
	GSList *undo_info = NULL;

	if (info->col_offset == 0 && info->row_offset == 0 &&
	    info->origin_sheet == info->target_sheet)
		return NULL;

	g_return_val_if_fail (wb != NULL, NULL);

	/* Copy the list since it will change underneath us.  */
	cells = g_list_copy (wb->formula_cell_list);

	for (l = cells; l; l = l->next)	{
		Cell *cell = l->data;
		EvalPosition pos;
		ExprTree *newtree = expr_relocate (cell->parsed_node,
						   eval_pos_cell (&pos, cell),
						   info);

		if (newtree) {
			/* Don't store relocations if they were inside the region
			 * being moved.  That is handled elsewhere */
			if (info->origin_sheet != pos.sheet ||
			    !range_contains (&info->origin, pos.eval.col, pos.eval.row)) {
				struct expr_relocate_storage *tmp =
				    g_new (struct expr_relocate_storage, 1);
				tmp->pos = pos;
				tmp->oldtree = cell->parsed_node;
				expr_tree_ref (tmp->oldtree);
				undo_info = g_slist_prepend (undo_info, tmp);
			}

			cell_set_formula_tree (cell, newtree);
			expr_tree_unref (newtree);
		}
	}

	g_list_free (cells);

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
