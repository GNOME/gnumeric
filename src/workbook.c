/*
 * workbook.c:  Workbook management (toplevel windows)
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org).
 *
 * (C) 1998, 1999 Miguel de Icaza
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "eval.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "main.h"
#include "file.h"
#include "xml-io.h"
#include "plugin.h"
#include "pixmaps.h"
#include "clipboard.h"
#include "utils.h"
#include "widgets/widget-editable-label.h"
#include "ranges.h"
#include "selection.h"
#include "print.h"

#ifdef ENABLE_BONOBO
#include <bonobo/gnome-persist-file.h>
#include "sheet-object-container.h"
#include "embeddable-grid.h"
#endif

#include <ctype.h>

/* The locations within the main table in the workbook */
#define WB_EA_LINE   0
#define WB_EA_SHEETS 1
#define WB_EA_STATUS 2

#define WB_COLS      1

Workbook *current_workbook;
static int workbook_count;

static GList *workbook_list = NULL;

static WORKBOOK_PARENT_CLASS *workbook_parent_class;

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

	if (fname && (new_wb = workbook_read (fname)))
		gtk_widget_show (new_wb->toplevel);
	g_free (fname);
}

static void
file_import_cmd (GtkWidget *widget, Workbook *wb)
{
	char *fname = dialog_query_load_file (wb);
	Workbook *new_wb;

	if (!fname)
		return;

       new_wb = workbook_import (wb, fname);
       if (new_wb)
	       gtk_widget_show (new_wb->toplevel);
}

static void
file_save_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_save (wb);
}

static void
file_save_as_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_save_as (wb);
}

static void
summary_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_summary_update (widget, wb->summary_info);
}

static void
plugins_cmd (GtkWidget *widget, Workbook *wb)
{
	GtkWidget *pm = plugin_manager_new (wb);
	gtk_widget_show(pm);
}

static void
set_selection_halign (Workbook *wb, StyleHAlignFlags align)
{
	Sheet *sheet;
	GList *cells, *l;

	sheet = workbook_get_current_sheet (wb);
	cells = sheet_selection_to_list (sheet);

	for (l = cells; l; l = l->next){
		Cell *cell = l->data;

		cell_set_halign (cell, align);
	}
	g_list_free (cells);
}

static void
left_align_cmd (GtkWidget *widget, Workbook *wb)
{
	set_selection_halign (wb, HALIGN_LEFT);
}

static void
right_align_cmd (GtkWidget *widget, Workbook *wb)
{
	set_selection_halign (wb, HALIGN_RIGHT);
}

static void
center_cmd (GtkWidget *widget, Workbook *wb)
{
	set_selection_halign (wb, HALIGN_CENTER);
}

/*
 * change_selection_font
 * @wb:  The workbook to operate on
 * @bold: -1 to leave unchanged, 0 to clear, 1 to set
 * @italic: -1 to leave unchanged, 0 to clear, 1 to set
 *
 */
static void
change_selection_font (Workbook *wb, int bold, int italic)
{
	Sheet *sheet;
	GList *cells, *l;

	sheet = workbook_get_current_sheet (wb);
	cells = sheet_selection_to_list (sheet);

	for (l = cells; l; l = l->next){
		StyleFont *cell_font;
		Cell *cell = l->data;
		StyleFont *f;

		cell_font = cell->style->font;

		f = style_font_new (
			cell_font->font_name,
			cell_font->size,
			cell_font->scale,
			bold == -1 ? cell_font->is_bold : bold,
			italic == -1 ? cell_font->is_italic : italic);

		if (f)
			cell_set_font_from_style (cell, f);
	}

	g_list_free (cells);
}

static void
bold_cmd (GtkToggleButton *t, Workbook *wb)
{
	change_selection_font (wb, t->active, -1);
}

static void
italic_cmd (GtkToggleButton *t, Workbook *wb)
{
	change_selection_font (wb, -1, t->active);
}

#ifdef ENABLE_BONOBO
static void
create_graphic_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_GRAPHIC);
}
#endif

static void
create_button_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_BUTTON);
}

static void
create_line_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_LINE);
}

static void
create_arrow_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_ARROW);
}

static void
create_rectangle_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_BOX);
}

static void
create_ellipse_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_set_mode_type (sheet, SHEET_MODE_CREATE_OVAL);
}

static void
cb_sheet_do_erase (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;

	sheet_clear_region (sheet, 0, 0, SHEET_MAX_COLS - 1, SHEET_MAX_ROWS - 1, NULL);
}

static void
dump_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange const * const deprange = key;
	Range const * const range = &(deprange->range);

	/* 2 calls to col_name.  It uses a static buffer */
	printf ("\t%d : %s%d:", deprange->ref_count,
		col_name(range->start.col), range->start.row+1);
	printf ("%s%d\n",
		col_name(range->end.col), range->end.row+1);
}
static void
workbook_do_destroy (Workbook *wb)
{
	gtk_signal_disconnect_by_func (
		GTK_OBJECT (wb->toplevel),
		GTK_SIGNAL_FUNC (workbook_set_focus), wb);

	/*
	 * Do all deletions that leave the workbook in a working
	 * order.
	 */

	summary_info_free (wb->summary_info);
	wb->summary_info = NULL;

	/*
	 * Erase all cells.  In particular this removes all links between
	 * sheets.
	 */
	g_hash_table_foreach (wb->sheets, cb_sheet_do_erase, NULL);

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
			if (gnumeric_debugging > 0 &&
			    sheet->dependency_hash != NULL &&
			    g_hash_table_size (sheet->dependency_hash) != 0) {
				printf ("Hash %s:%s = %d\n",
					sheet->workbook->filename
					?  sheet->workbook->filename
					: "(no name)",
					sheet->name,
					g_hash_table_size (sheet->dependency_hash));
				g_hash_table_foreach (sheet->dependency_hash, dump_dep, NULL);
			}

			/*
			 * Make sure we alwayws keep the focus on an
			 * existing widget (otherwise the destruction
			 * code for wb->toplvel will try to focus a
			 * dead widget at the end of this routine)
			 */
			gtk_window_set_focus (GTK_WINDOW (wb->toplevel), NULL);

			workbook_detach_sheet (sheet->workbook, sheet, TRUE);
			sheet_destroy (sheet);

			gtk_window_set_focus (GTK_WINDOW (wb->toplevel), NULL);
		}
		g_list_free (sheets);
	}

	if (wb->clipboard_contents) {
		clipboard_release (wb->clipboard_contents);
		wb->clipboard_contents = NULL;
	}

	/* Remove ourselves from the list of workbooks.  */
	workbook_list = g_list_remove (workbook_list, wb);
	workbook_count--;

	/* Now do deletions that will put this workbook into a weird
	   state.  Careful here.  */

	g_hash_table_destroy (wb->sheets);

	if (wb->filename)
	       g_free (wb->filename);

	g_free (wb->toolbar);

	symbol_table_destroy (wb->symbol_names);

	expr_name_clean (wb);

	if (!GTK_OBJECT_DESTROYED (wb->toplevel))
		gtk_object_destroy (GTK_OBJECT (wb->toplevel));
	
	if (workbook_count == 0)
		gtk_main_quit ();
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
	if (workbook_can_close (wb)){
#ifdef BONOBO_ENABLED
		if (wb->bonobo_regions) {
			gtk_widget_hide (GTK_WIDGET (wb->toplevel));
			return FALSE;
		}
		gnome_object_unref (GNOME_OBJECT (wb));
#else
		gtk_object_unref (GTK_OBJECT (wb));
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

typedef enum {
	CLOSE_DENY,
	CLOSE_ALLOW,
	CLOSE_RECHECK
} CloseAction;

static void
cb_sheet_check_dirty (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	GtkWidget *d, *l;
	int *allow_close = user_data;
	int button;
	char *s;

	if (!sheet->modified)
		return;

	if (*allow_close != CLOSE_ALLOW)
		return;

	d = gnome_dialog_new (
		_("Warning"),
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		GNOME_STOCK_BUTTON_CANCEL,
		NULL);
	gnome_dialog_set_parent (GNOME_DIALOG (d), GTK_WINDOW (sheet->workbook->toplevel));

	if (sheet->workbook->filename)
		s = g_strdup_printf (
			_("Workbook %s has unsaved changes, save them?"),
			g_basename (sheet->workbook->filename));
	else
		s = g_strdup (_("Workbook has unsaved changes, save them?"));

	l = gtk_label_new (s);
	gtk_widget_show (l);
	g_free (s);

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG(d)->vbox), l, TRUE, TRUE, 0);

	gtk_window_set_position (GTK_WINDOW (d), GTK_WIN_POS_MOUSE);
	button = gnome_dialog_run_and_close (GNOME_DIALOG (d));

	switch (button){
		/* YES */
	case 0:
		workbook_save (sheet->workbook);
		*allow_close = CLOSE_RECHECK;
		break;

		/* NO */
	case 1:
		workbook_mark_clean (sheet->workbook);
		break;

		/* CANCEL */
	case -1:
	case 2:
		*allow_close = CLOSE_DENY;
		break;

	}
}

static int
workbook_can_close (Workbook *wb)
{
	CloseAction allow_close;

	do {
		allow_close = CLOSE_ALLOW;
		g_hash_table_foreach (
			wb->sheets, cb_sheet_check_dirty, &allow_close);
	} while (allow_close == CLOSE_RECHECK);

	g_assert (allow_close == CLOSE_ALLOW || allow_close == CLOSE_DENY);
	return allow_close == CLOSE_ALLOW;
}

static void
close_cmd (GtkWidget *widget, Workbook *wb)
{
	if (workbook_can_close (wb)){
#ifdef ENABLE_BONOBO
		gnome_object_unref (GNOME_OBJECT (wb));
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
			gnome_object_unref (GNOME_OBJECT (wb));
#else
			gtk_object_unref (GTK_OBJECT (wb));
#endif
		}
	}

	g_list_free (n);
}

static void
paste_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_paste (sheet, sheet->cursor_col, sheet->cursor_row,
			       PASTE_DEFAULT, GDK_CURRENT_TIME);
}

static void
copy_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_copy (sheet);
}

static void
cut_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	if (sheet->mode == SHEET_MODE_SHEET)
		sheet_selection_cut (sheet);
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
paste_special_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;
	int flags;

	sheet = workbook_get_current_sheet (wb);
	flags = dialog_paste_special (wb);
	if (flags != 0)
		sheet_selection_paste (sheet, sheet->cursor_col, sheet->cursor_row,
				       flags, GDK_CURRENT_TIME);

}

static void
select_all_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);

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
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_insert_cells (wb, sheet);
}

static void
insert_cols_cmd (GtkWidget *unused, Workbook *wb)
{
	SheetSelection *ss;
	Sheet *sheet;
	int cols;

	/* FIXME : No need to check simplicty.  XL applies for each
	 * non-discrete selected region, (use selection_apply) */
	sheet = workbook_get_current_sheet (wb);
	if (!sheet_verify_selection_simple (sheet, _("Insert cols")))
		return;

	ss = sheet->selections->data;

	/* FIXME FIXME : Have we have selected rows rather than columns */
	cols = ss->user.end.col - ss->user.start.col + 1;
	sheet_insert_col (sheet, ss->user.start.col, cols);
}

static void
insert_rows_cmd (GtkWidget *unused, Workbook *wb)
{
	SheetSelection *ss;
	Sheet *sheet;
	int rows;

	/* FIXME : No need to check simplicty.  XL applies for each
	 * non-discrete selected region, (use selection_apply) */
	sheet = workbook_get_current_sheet (wb);
	if (!sheet_verify_selection_simple (sheet, _("Insert rows")))
		return;

	ss = sheet->selections->data;

	/* FIXME FIXME : Have we have selected columns rather than rows */
	rows = ss->user.end.row - ss->user.start.row + 1;
	sheet_insert_row (sheet, ss->user.start.row, rows);
}

static void
clear_all_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_clear (sheet);
}

static void
clear_formats_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_clear_formats (sheet);
}

static void
clear_comments_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_clear_comments (sheet);
}

static void
clear_content_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_clear_content (sheet);
}

static void
zoom_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_zoom (wb, sheet);
}

static void
format_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_cell_format (wb, sheet);
}

static void
sort_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_cell_sort (wb, sheet);
}

static void
recalc_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_recalc_all (wb);
}

static void
insert_at_cursor (Sheet *sheet, Value *value, const char *format)
{
	Cell *cell;

	cell = sheet_cell_fetch (sheet, sheet->cursor_col, sheet->cursor_row);
	cell_set_value (cell, value);
	cell_set_format (cell, format);

	workbook_recalc (sheet->workbook);
}

static void
insert_current_date_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	const char *preferred_date_format = _(">mm/dd/yyyy");
	int n;
	GDate *date = g_date_new();

	g_date_set_time (date, time (NULL));

	n = g_date_serial (date);

	g_date_free( date );

	insert_at_cursor (sheet, value_new_int (n), preferred_date_format+1);
}

static void
insert_current_time_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);
	const char *preferred_time_format = _(">hh:mm");
	float_t serial;

	serial = (tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec)/(24*3600.0);
	insert_at_cursor (sheet, value_new_float (serial), preferred_time_format);
}

static void
workbook_edit_comment (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	Cell *cell;

	cell = sheet_cell_get (sheet, sheet->cursor_col, sheet->cursor_row);

	if (!cell){
		cell = sheet_cell_new (sheet, sheet->cursor_col, sheet->cursor_row);
		cell_set_text (cell, "");
	}

	dialog_cell_comment (wb, cell);
}

static void
goal_seek_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_goal_seek (wb, sheet);
}

static void
solver_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_solver (wb, sheet);
}

static void
data_analysis_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_data_analysis (wb, sheet);
}

static void
print_setup_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_printer_setup (sheet);
}

static void
file_print_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_print (sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
file_print_preview_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_print (sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static void
about_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_about (wb);
}

#ifdef ENABLE_BONOBO
static void
insert_object_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	char *repoid;

	repoid = gnome_bonobo_select_goad_id (_("Select an object"), NULL);
	if (repoid != NULL)
		sheet_insert_object (sheet, repoid);
}
#endif

/* File menu */

static GnomeUIInfo workbook_menu_file [] = {
        GNOMEUIINFO_MENU_NEW_ITEM(N_("_New"), N_("Create a new spreadsheet"),
				  new_cmd, NULL),

	GNOMEUIINFO_MENU_OPEN_ITEM(file_open_cmd, NULL),
	GNOMEUIINFO_ITEM_STOCK (N_("_Import"), N_("Imports a file"),
				file_import_cmd, GNOME_STOCK_MENU_OPEN),
	GNOMEUIINFO_MENU_SAVE_ITEM(file_save_cmd, NULL),

	GNOMEUIINFO_MENU_SAVE_AS_ITEM(file_save_as_cmd, NULL),

	{ GNOME_APP_UI_ITEM, N_("Su_mmary..."), N_("Summary information"),
	  summary_cmd },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("Plu_g-ins..."), N_("Gnumeric plugins"),
	  plugins_cmd },

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
	  NULL, clear_all_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Formats"),
	  N_("Clear the selected cells' formats"), clear_formats_cmd },
	{ GNOME_APP_UI_ITEM, N_("Co_mments"),
	  N_("Clear the selected cells' comments"), clear_comments_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Content"),
	  N_("Clear the selected cells' contents"), clear_content_cmd },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_edit [] = {
        GNOMEUIINFO_MENU_CUT_ITEM(cut_cmd, NULL),
	GNOMEUIINFO_MENU_COPY_ITEM(copy_cmd, NULL),
	GNOMEUIINFO_MENU_PASTE_ITEM(paste_cmd, NULL),
	{ GNOME_APP_UI_ITEM, N_("P_aste special..."), NULL, paste_special_cmd },
        { GNOME_APP_UI_SUBTREE, N_("C_lear"),
	  N_("Clear the selected cell(s)"), workbook_menu_edit_clear },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_Select All"),
	  N_("Select all cells in the spreadsheet"), select_all_cmd, NULL,
	  NULL, 0, 0, 'a', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Goto cell..."),
	  N_("Jump to a specified cell"), goto_cell_cmd, NULL, NULL,
	  0, 0, 'i', GDK_CONTROL_MASK },

	GNOMEUIINFO_SEPARATOR,

#ifdef ENABLE_BONOBO
	GNOMEUIINFO_ITEM_NONE(N_("Insert object..."),
			      N_("Inserts a Bonobo object"),
			      insert_object_cmd),
#endif

	GNOMEUIINFO_SEPARATOR,

#if 1
	{ GNOME_APP_UI_ITEM, N_("_Define cell names"), NULL, define_cell_cmd },
	GNOMEUIINFO_SEPARATOR,
#endif

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
	  NULL, NULL, 0, 0, ';', GDK_CONTROL_MASK | GDK_SHIFT_MASK },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_insert [] = {
	{ GNOME_APP_UI_ITEM, N_("_Sheet"), N_("Insert a new spreadsheet"),
	  insert_sheet_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Cells..."), N_("Insert new cells"),
	  insert_cells_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Rows"), N_("Insert new rows"),
	  insert_rows_cmd  },
	{ GNOME_APP_UI_ITEM, N_("C_olumns"), N_("Insert new columns"),
	  insert_cols_cmd  },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_Add/modify comment..."),
	  N_("Edit the selected cell's comment"), workbook_edit_comment },
	{ GNOME_APP_UI_SUBTREE, N_("_Special"), NULL, workbook_menu_insert_special },

	GNOMEUIINFO_END
};

/* Format menu */

#if 0
static GnomeUIInfo workbook_menu_format_column [] = {
	{ GNOME_APP_UI_ITEM, N_("_Autoadjust"),   NULL, format_column_autoadjust_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Width"),        NULL, format_column_width_cmd },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_row [] = {
	{ GNOME_APP_UI_ITEM, N_("_Autoadjust"),   NULL,  format_row_autoadjust_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Height"),        NULL, format_row_height_cmd },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_sheet [] = {
	{ GNOME_APP_UI_ITEM, N_("_Change name"),   NULL,  format_sheet_change_name_cmd },
	GNOMEUIINFO_END
};
#endif

static GnomeUIInfo workbook_menu_format [] = {
	{ GNOME_APP_UI_ITEM, N_("_Cells..."),
	  N_("Modify the formatting of the selected cells"),
	  format_cells_cmd, NULL, NULL, 0, 0, GDK_1, GDK_CONTROL_MASK },
#if 0
	{ GNOME_APP_UI_SUBTREE, N_("C_olumn"), NULL, workbook_menu_format_column },
	{ GNOME_APP_UI_SUBTREE, N_("_Row"),    NULL, workbook_menu_format_row },
	{ GNOME_APP_UI_SUBTREE, N_("_Sheet"),  NULL, workbook_menu_format_sheet },
#endif
	GNOMEUIINFO_END
};

/* Tools menu */
static GnomeUIInfo workbook_menu_tools [] = {
	{ GNOME_APP_UI_ITEM, N_("_Sort..."),
	  N_("Sort the selected cells"), sort_cells_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Goal Seek..."), NULL, goal_seek_cmd },
#if 0
	{ GNOME_APP_UI_ITEM, N_("_Solver..."),    NULL, solver_cmd },
#endif
	{ GNOME_APP_UI_ITEM, N_("_Data Analysis..."), NULL, data_analysis_cmd },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_help [] = {
	GNOMEUIINFO_HELP ("gnumeric"),
        GNOMEUIINFO_MENU_ABOUT_ITEM(about_cmd, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu [] = {
        GNOMEUIINFO_MENU_FILE_TREE(workbook_menu_file),
	GNOMEUIINFO_MENU_EDIT_TREE(workbook_menu_edit),
	GNOMEUIINFO_MENU_VIEW_TREE(workbook_menu_view),
	{ GNOME_APP_UI_SUBTREE, N_("_Insert"), NULL, workbook_menu_insert },
	{ GNOME_APP_UI_SUBTREE, N_("F_ormat"), NULL, workbook_menu_format },
	{ GNOME_APP_UI_SUBTREE, N_("_Tools"), NULL, workbook_menu_tools },
#ifdef ENABLE_BONOBO
#warning Should enable this when Bonobo gets menu help support
#else
	GNOMEUIINFO_MENU_HELP_TREE(workbook_menu_help),
#endif
	GNOMEUIINFO_END
};

#define TOOLBAR_BOLD_BUTTON_INDEX 13
#define TOOLBAR_ITALIC_BUTTON_INDEX 14
static GnomeUIInfo workbook_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("New"), N_("Creates a new sheet"),
		new_cmd, GNOME_STOCK_PIXMAP_NEW),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Open"), N_("Opens an existing workbook"),
		file_open_cmd, GNOME_STOCK_PIXMAP_OPEN),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Save"), N_("Saves the workbook"),
		file_save_cmd, GNOME_STOCK_PIXMAP_SAVE),
	GNOMEUIINFO_ITEM_STOCK(
		N_("Print"), N_("Prints the workbook"),
		file_print_cmd, GNOME_STOCK_PIXMAP_PRINT),

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

	GNOMEUIINFO_ITEM_STOCK (
		N_("Left align"), N_("Left justifies the cell contents"),
		left_align_cmd, GNOME_STOCK_PIXMAP_ALIGN_LEFT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Center"), N_("Centers the cell contents"),
		center_cmd, GNOME_STOCK_PIXMAP_ALIGN_CENTER),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Right align"), N_("Right justifies the cell contents"),
		right_align_cmd, GNOME_STOCK_PIXMAP_ALIGN_RIGHT),

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_TOGGLEITEM, N_("Bold"), N_("Sets the bold font"),
	  bold_cmd, NULL, NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_TEXT_BOLD },

	{ GNOME_APP_UI_TOGGLEITEM, N_("Italic"), N_("Makes the font italic"),
	  italic_cmd, NULL, NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_TEXT_ITALIC },

	GNOMEUIINFO_SEPARATOR,

#ifdef ENABLE_BONOBO
	GNOMEUIINFO_ITEM_DATA (
		N_("Graphic"), N_("Creates a graphic in the spreadsheet"),
		create_graphic_cmd, NULL, graphic_xpm),
#endif
#if 0
	GNOMEUIINFO_ITEM_DATA (
		N_("Button"), N_("Creates a button object"),
		create_button_cmd, NULL, button_xpm),
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

	GNOMEUIINFO_END
};

static void
do_focus_sheet (GtkNotebook *notebook, GtkNotebookPage *page, guint page_num, Workbook *wb)
{
	workbook_focus_current_sheet (wb);
}

static void
workbook_setup_sheets (Workbook *wb)
{
	wb->notebook = gtk_notebook_new ();
	gtk_signal_connect_after (GTK_OBJECT (wb->notebook), "switch_page",
				  GTK_SIGNAL_FUNC(do_focus_sheet), wb);

	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (wb->notebook), GTK_POS_BOTTOM);
	gtk_notebook_set_tab_border (GTK_NOTEBOOK (wb->notebook), 0);

	gtk_table_attach (GTK_TABLE (wb->table), wb->notebook,
			  0, WB_COLS, WB_EA_SHEETS, WB_EA_SHEETS+1,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
			  0, 0);
}

Sheet *
workbook_get_current_sheet (Workbook *wb)
{
	GtkWidget *current_notebook;
	Sheet *sheet;

	g_return_val_if_fail (wb != NULL, NULL);

	current_notebook = GTK_NOTEBOOK (wb->notebook)->cur_page->child;
	sheet = gtk_object_get_data (GTK_OBJECT (current_notebook), "sheet");

	if (sheet == NULL)
		g_warning ("There is no current sheet in this workbook");

	return sheet;
}

Sheet *
workbook_focus_current_sheet (Workbook *wb)
{
	SheetView *sheet_view;
	Sheet *sheet;

	g_return_val_if_fail (wb != NULL, NULL);

	sheet = workbook_get_current_sheet (wb);
	sheet_view = SHEET_VIEW (sheet->sheet_views->data);

	gtk_window_set_focus (GTK_WINDOW (wb->toplevel), sheet_view->sheet_view);
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

static void
wb_input_finished (GtkEntry *entry, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);

	sheet_set_current_value (sheet);
	workbook_focus_current_sheet (wb);
}

static int
wb_edit_key_pressed (GtkEntry *entry, GdkEventKey *event, Workbook *wb)
{
	switch (event->keyval) {
	case GDK_Escape:
		sheet_cancel_pending_input (workbook_get_current_sheet (wb));
		workbook_focus_current_sheet (wb);
		return TRUE;

	case GDK_F4:
	{
		int end_pos = GTK_EDITABLE (entry)->current_pos;
		int start_pos;
		int row_status_pos, col_status_pos;
		gboolean abs_row = FALSE, abs_col = FALSE;

		/* Ignore this character */
		event->keyval = GDK_VoidSymbol;

		/* Only apply do this for formulas */
		if (entry->text [0] != '=')
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

		/* Select the current range */
		gtk_editable_select_region (GTK_EDITABLE (entry), start_pos+1, end_pos);
		GTK_EDITABLE (entry)->current_pos = start_pos+1;
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
		Sheet *sheet = workbook_get_current_sheet (wb);

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

	gtk_entry_set_text (GTK_ENTRY (wb->ea_status), str);
}

static void
accept_input (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);

	sheet_set_current_value (sheet);
	workbook_focus_current_sheet (wb);
}

static void
cancel_input (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);

	sheet_cancel_pending_input (sheet);
	workbook_focus_current_sheet (wb);
}

static void
wizard_input (GtkWidget *widget, Workbook *wb)
{
	FunctionDefinition *fd = dialog_function_select (wb);
	GtkEntry *entry = GTK_ENTRY(wb->ea_input);
	gchar *txt, *edittxt;
	int pos;

	if (!fd)
		return;

	txt = dialog_function_wizard (wb, fd);

       	if (!txt || !wb || !entry)
		return;

	pos = gtk_editable_get_position (GTK_EDITABLE (entry));

	gtk_editable_insert_text (GTK_EDITABLE(entry),
				  txt, strlen(txt), &pos);
	g_free (txt);
	txt = gtk_entry_get_text (entry);

	if (txt [0] != '=')
	        edittxt = g_strconcat ("=", txt, NULL);
	else
		edittxt = g_strdup (txt);
	gtk_entry_set_text (entry, edittxt);
	g_free (edittxt);
}

static void
deps_output (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	int col, row, dummy;
	Cell *cell;
	GList *list;

	summary_info_dump (wb->summary_info);

	if (!sheet_selection_first_range (
		sheet, &col, &row, &dummy, &dummy)){
		gnumeric_notice (
			wb, GNOME_MESSAGE_BOX_ERROR,
			_("Selection must be a single range"));
		return;
	}
	printf ("The cells that depend on %s\n", cell_name (col, row));

	if (!(cell = sheet_cell_get (sheet, col, row))){
		printf ("must contain some data\n");
		return;
	}

	list = cell_get_dependencies (sheet, col, row);
	if (!list)
		printf ("No dependencies\n");

	while (list){
		Cell *cell = list->data;

		list = g_list_next (list);
		if (!cell)
			continue;

		if (sheet != cell->sheet && cell->sheet)
			printf ("%s", cell->sheet->name);

		printf ("%s\n", cell_name (cell->col->pos, cell->row->pos));
	}
}

static void
workbook_setup_edit_area (Workbook *wb)
{
	GtkWidget *ok_button, *cancel_button, *wizard_button;
	GtkWidget *pix, *deps_button, *box, *box2;

	wb->ea_status = gtk_entry_new ();
	wb->ea_input  = gtk_entry_new ();
	ok_button     = gtk_button_new ();
	cancel_button = gtk_button_new ();
	box           = gtk_hbox_new (0, 0);
	box2          = gtk_hbox_new (0, 0);

	gtk_widget_set_usize (wb->ea_status, 100, 0);

	/* Ok */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_OK);
	gtk_container_add (GTK_CONTAINER (ok_button), pix);
	GTK_WIDGET_UNSET_FLAGS (ok_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (ok_button), "clicked",
			    GTK_SIGNAL_FUNC(accept_input), wb);

	/* Cancel */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_CANCEL);
	gtk_container_add (GTK_CONTAINER (cancel_button), pix);
	GTK_WIDGET_UNSET_FLAGS (cancel_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (cancel_button), "clicked",
			    GTK_SIGNAL_FUNC(cancel_input), wb);

	gtk_box_pack_start (GTK_BOX (box2), wb->ea_status, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), ok_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), cancel_button, 0, 0, 0);

	/* Function Wizard */
	if (gnumeric_debugging > 0) {
		wizard_button = gtk_button_new ();
		pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_PIXMAP_BOOK_GREEN);
		gtk_container_add (GTK_CONTAINER (wizard_button), pix);
		GTK_WIDGET_UNSET_FLAGS (wizard_button, GTK_CAN_FOCUS);
		gtk_signal_connect (GTK_OBJECT (wizard_button), "clicked",
				    GTK_SIGNAL_FUNC(wizard_input), wb);
		gtk_box_pack_start (GTK_BOX (box), wizard_button, 0, 0, 0);
	}

	/* Dependency Debugger, currently only enabled if you run with --debug=10 */
	if (gnumeric_debugging > 9){
		deps_button = gtk_button_new ();
		pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_PIXMAP_BOOK_RED);
		gtk_container_add (GTK_CONTAINER (deps_button), pix);
		GTK_WIDGET_UNSET_FLAGS (deps_button, GTK_CAN_FOCUS);
		gtk_signal_connect (GTK_OBJECT (deps_button), "clicked",
				    GTK_SIGNAL_FUNC(deps_output), wb);
		gtk_box_pack_start (GTK_BOX (box), deps_button, 0, 0, 0);
	}

	gtk_box_pack_start (GTK_BOX (box2), box, 0, 0, 0);
	gtk_box_pack_end   (GTK_BOX (box2), wb->ea_input, 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wb->table), box2,
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* Do signal setup for the editing input line */
	gtk_signal_connect (GTK_OBJECT (wb->ea_input), "activate",
			    GTK_SIGNAL_FUNC(wb_input_finished),
			    wb);
	gtk_signal_connect (GTK_OBJECT (wb->ea_input), "key_press_event",
			    GTK_SIGNAL_FUNC(wb_edit_key_pressed),
			    wb);

	/* Do signal setup for the status input line */
	gtk_signal_connect (GTK_OBJECT (wb->ea_status), "activate",
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

/*
 * A utility to temporarily remove a workbook auto expr.  The current
 * expression and description are returned to the caller in @expr and @desc so
 * that they can be reset via sheet_resume_auto_expr later.  The caller is
 * responsible for managing the string and expression until they are returned
 * to the workbook via sheet_suspend_auto_expr.
 */
void
sheet_suspend_auto_expr(Workbook *wb, ExprTree **expr, String **desc)
{
	g_return_if_fail (wb != NULL);

	*expr = wb->auto_expr;
	wb->auto_expr = NULL;
	*desc = wb->auto_expr_desc;
	wb->auto_expr_desc = NULL;
}

/*
 * The counter point to sheet_suspend_auto_expr it reset previously suspended
 * auto expressions and retakes responsibility for managing the strings and
 * expressions.
 */
void
sheet_resume_auto_expr(Workbook *wb, ExprTree *expr, String *desc)
{
	Sheet *sheet;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (expr != NULL);
	g_return_if_fail (desc != NULL);

       	sheet = workbook_get_current_sheet (wb);
	wb->auto_expr = expr;
	wb->auto_expr_desc = desc;
	sheet_update_auto_expr (sheet);
}

static void
change_auto_expr (GtkWidget *item, Workbook *wb)
{
	char *expr, *name;
	Sheet *sheet = workbook_get_current_sheet (wb);

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
				    GTK_SIGNAL_FUNC(change_auto_expr), wb);
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
	wb->auto_expr_label = GNOME_CANVAS_ITEM (gnome_canvas_item_new (
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
	gtk_box_pack_start (GTK_BOX (wb->appbar), frame, FALSE, TRUE, 0);
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
	wb->appbar = GNOME_APPBAR (gnome_appbar_new (FALSE, TRUE,
						    GNOME_PREFERENCES_USER));
	gnome_app_set_statusbar (GNOME_APP (wb->toplevel),
				 GTK_WIDGET (wb->appbar));

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
	gnome_canvas_item_set (wb->auto_expr_label, "text", res, NULL);
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
	wb->bonobo_regions = g_list_remove (wb->bonobo_regions, embeddable_grid);
}

static GNOME_Unknown
workbook_container_get_object (GnomeObject *container, CORBA_char *item_name,
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
	if (p){
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
	if (range){
		CellRef *a = &range->v.cell_range.cell_a;
		CellRef *b = &range->v.cell_range.cell_b;

		embeddable_grid_set_range (eg, a->col, a->row, b->col, b->row);
	}

	gtk_signal_connect (GTK_OBJECT (eg), "destroy",
			    grid_destroyed, wb);
	
	wb->bonobo_regions = g_list_prepend (wb->bonobo_regions, eg);

	return CORBA_Object_duplicate (
		gnome_object_corba_objref (GNOME_OBJECT (eg)), ev);
}

static int
workbook_persist_file_load (GnomePersistFile *ps, const CORBA_char *filename, void *closure)
{
	Workbook *wb = closure;

	if (workbook_load_from (wb, filename))
		return 0;

	return -1;
}

static int
workbook_persist_file_save (GnomePersistFile *ps, const CORBA_char *filename, void *closure)
{
	Workbook *wb = closure;

	return gnumeric_xml_write_workbook (wb, filename);
}
     
static void
workbook_bonobo_setup (Workbook *wb)
{
	wb->gnome_container = GNOME_CONTAINER (gnome_container_new ());
	wb->persist_file = gnome_persist_file_new (
		GNUMERIC_WORKBOOK_GOAD_ID,
		workbook_persist_file_load,
		workbook_persist_file_save,
		wb);
		
	gnome_object_add_interface (
		GNOME_OBJECT (wb),
		GNOME_OBJECT (wb->gnome_container));
	gnome_object_add_interface (
		GNOME_OBJECT (wb),
		GNOME_OBJECT (wb->persist_file));
	
	gtk_signal_connect (
		GTK_OBJECT (wb->gnome_container), "get_object",
		GTK_SIGNAL_FUNC (workbook_container_get_object), wb);
}
#endif

static void
workbook_init (GtkObject *object)
{
	Workbook *wb = WORKBOOK (object);

	wb->sheets       = g_hash_table_new (gnumeric_strcase_hash, gnumeric_strcase_equal);
	wb->names        = NULL;
	wb->symbol_names = symbol_table_new ();
	wb->max_iterations = 1;
	wb->summary_info   = summary_info_new ();
	summary_info_default (wb->summary_info);

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
	WorkbookClass *workbook_class = (WorkbookClass *) object_class;
	
	workbook_parent_class = gtk_type_class (WORKBOOK_PARENT_CLASS_TYPE);
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
		type = gtk_type_unique (gnome_object_get_type (), &info);
#else
		type = gtk_type_unique (gtk_object_get_type (), &info);
#endif
	}

	return type;
}

/**
 * workbook_new:
 *
 * Creates a new empty Workbook.
 */
Workbook *
workbook_new (void)
{
	GnomeDockItem *item;
	GtkWidget *toolbar;
	Workbook  *wb;
	int        sx, sy;

	wb = gtk_type_new (workbook_get_type ());
	wb->toplevel  = gnome_app_new ("Gnumeric", "Gnumeric");
	wb->table     = gtk_table_new (0, 0, 0);

	gtk_window_set_policy (GTK_WINDOW(wb->toplevel), 1, 1, 0);
	sx = MAX (gdk_screen_width  () - 64, 400);
	sy = MAX (gdk_screen_height () - 64, 200);
	sx = (sx * 3) / 4;
	sy = (sx * 3) / 4;
	gtk_window_set_default_size (GTK_WINDOW(wb->toplevel), sx, sy);

	workbook_set_title (wb, _("Untitled.gnumeric"));

	workbook_setup_status_area (wb);
	workbook_setup_edit_area (wb);
	workbook_setup_sheets (wb);
	gnome_app_set_contents (GNOME_APP (wb->toplevel), wb->table);
#ifndef ENABLE_BONOBO
	gnome_app_create_menus_with_data (GNOME_APP (wb->toplevel), workbook_menu, wb);
	gnome_app_install_menu_hints(GNOME_APP (wb->toplevel), workbook_menu);
#else
	{
		GnomeUIHandlerMenuItem *list;
		
		wb->uih = gnome_ui_handler_new ();
		gnome_ui_handler_set_app (wb->uih, GNOME_APP (wb->toplevel));
		gnome_ui_handler_create_menubar (wb->uih);
		list = gnome_ui_handler_menu_parse_uiinfo_list_with_data (workbook_menu, wb);
		gnome_ui_handler_menu_add_list (wb->uih, "/", list);
	}
#endif

	wb->toolbar = g_malloc (sizeof (workbook_toolbar));
	memcpy (wb->toolbar, &workbook_toolbar, sizeof (workbook_toolbar));
	gnome_app_create_toolbar_with_data (GNOME_APP (wb->toplevel), (GnomeUIInfo *) wb->toolbar, wb);

	item = gnome_app_get_dock_item_by_name (GNOME_APP (wb->toplevel), GNOME_APP_TOOLBAR_NAME);
	toolbar = gnome_dock_item_get_child (item);

	gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_ICONS);

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

	gtk_signal_connect (GTK_OBJECT(wb->toplevel),
			    "drag_data_received",
			    GTK_SIGNAL_FUNC(filenames_dropped), wb);
#endif

	/* clipboard setup */
	x_clipboard_bind_workbook (wb);

	gtk_widget_show_all (wb->table);

	return wb;
}

static void
zoom_in (GtkButton *b, Sheet *sheet)
{
	double pix = sheet->last_zoom_factor_used;

	if (pix < 10.0){
		pix += 0.5;
		sheet_set_zoom_factor (sheet, pix);
	}
}

static void
zoom_out (GtkButton *b, Sheet *sheet)
{
	double pix = sheet->last_zoom_factor_used;

	if (pix > 1.0){
		pix -= 0.5;
		sheet_set_zoom_factor (sheet, pix);
	}
}

static void
zoom_change (GtkAdjustment *adj, Sheet *sheet)
{
	sheet_set_zoom_factor (sheet, adj->value);
}

static void
buttons (Sheet *sheet, GtkTable *table)
{
	GtkWidget *b;

	if (0){
		b = gtk_button_new_with_label (_("Zoom out"));
		GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_FOCUS);
		gtk_table_attach (table, b,
				  0, 1, 1, 2, 0, 0, 0, 0);
		gtk_signal_connect (GTK_OBJECT (b), "clicked",
				    GTK_SIGNAL_FUNC (zoom_out), sheet);

		b = gtk_button_new_with_label (_("Zoom in"));
		GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_FOCUS);
		gtk_table_attach (table, b,
				  1, 2, 1, 2, 0, 0, 0, 0);
		gtk_signal_connect (GTK_OBJECT (b), "clicked",
				    GTK_SIGNAL_FUNC (zoom_in), sheet);
	} else {
		static GtkAdjustment *adj;
		GtkWidget *sc;

		adj = GTK_ADJUSTMENT (gtk_adjustment_new (1.0, 0.5, 10.0, 0.1, 0.5, 0.5));
		sc = gtk_hscrollbar_new (adj);
		gtk_widget_show (sc);
		gtk_table_attach (table, sc,
				  0, 2, 1, 2, GTK_FILL|GTK_EXPAND, 0, 0, 0);
		gtk_signal_connect (GTK_OBJECT (adj), "value_changed", zoom_change, sheet);
	}
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

	return TRUE;
}

/*
 * Signal handler for EditableLabel's text_changed signal.
 */
static gboolean
sheet_label_text_changed_signal (EditableLabel *el, const char *new_name, Workbook *wb)
{
	gboolean ans;

	if (strchr (new_name, '"'))
		return FALSE;
	if (strchr (new_name, '\''))
		return FALSE;

	ans = workbook_rename_sheet (wb, el->text, new_name);
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
sheet_action_delete_sheet (GtkWidget *widget, Sheet *current_sheet)
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
	gnome_dialog_set_parent (GNOME_DIALOG (d), GTK_WINDOW (wb->toplevel));

	r = gnome_dialog_run (GNOME_DIALOG (d));

	if (r != 0)
		return;

	if (!workbook_can_detach_sheet (wb, current_sheet)){
		gnumeric_notice (
			wb, GNOME_MESSAGE_BOX_ERROR,
			_("Other sheets depend on the values on this sheet; "
			  "I cannot remove it."));
		return;
	}

	/*
	 * All is fine, remove the sheet
	 */
	workbook_detach_sheet (wb, current_sheet, FALSE);
	sheet_destroy (current_sheet);
	workbook_recalc_all (wb);
}

#define SHEET_CONTEXT_TEST_SIZE 1

struct {
	const char *text;
	void (*function) (GtkWidget *widget, Sheet *sheet);
	int  flags;
} sheet_label_context_actions [] = {
	{ N_("Add another sheet"), sheet_action_add_sheet, 0 },
	{ N_("Remove this sheet"), sheet_action_delete_sheet, SHEET_CONTEXT_TEST_SIZE },
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

	if (gnumeric_debugging)
		buttons (sheet, GTK_TABLE (t));

	gtk_widget_show_all (t);
	gtk_object_set_data (GTK_OBJECT (t), "sheet", sheet);

	sheet_label = editable_label_new (sheet->name);
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
 * workbook_can_detach_sheet:
 * @wb: workbook.
 * @sheet: the sheet that we want to detach from the workbook
 *
 * Returns true whether the sheet can be safely detached from the
 * workbook.
 */
gboolean
workbook_can_detach_sheet (Workbook *wb, Sheet *sheet)
{
	GList *dependency_list;

	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->workbook != NULL, FALSE);
	g_return_val_if_fail (sheet->workbook == wb, FALSE);
	g_return_val_if_fail (workbook_sheet_lookup (wb, sheet->name) == sheet, FALSE);

	dependency_list = region_get_dependencies (sheet, 0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS);
	if (dependency_list == NULL)
		return TRUE;

	g_list_free (dependency_list);

	return FALSE;
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

	if (!force) {
		if (sheets == 1)
			return FALSE;

		if (!workbook_can_detach_sheet (wb, sheet))
			return FALSE;
	}

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

	/*
	 * Make the sheet drop its workbook pointer.
	 */
	sheet->workbook = NULL;

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
		char *name;

		name = g_strdup_printf (_("Sheet%d"), i);
		sheet = sheet_new (wb, name);
		workbook_attach_sheet (wb, sheet);
		g_free (name);
	}

	workbook_focus_current_sheet (wb);

	return wb;
}

/*
 * This routine sets up the default styles for the
 * workbook
 */
void
workbook_realized (Workbook *workbook, GdkWindow *window)
{
}

void
workbook_feedback_set (Workbook *workbook, WorkbookFeedbackType type, void *data)
{
	GtkToggleButton *t;
	GnomeUIInfo *toolbar = workbook->toolbar;
	int set;

	g_return_if_fail (workbook != NULL);

	switch (type){
	case WORKBOOK_FEEDBACK_BOLD:
		t = GTK_TOGGLE_BUTTON (
			toolbar [TOOLBAR_BOLD_BUTTON_INDEX].widget);
		set = data != NULL;

		gtk_signal_handler_block_by_func (GTK_OBJECT (t),
						  (GtkSignalFunc)&bold_cmd,
						  workbook);
		gtk_toggle_button_set_active (t, set);
		gtk_signal_handler_unblock_by_func (GTK_OBJECT (t),
						    (GtkSignalFunc)&bold_cmd,
						    workbook);
		break;

	case WORKBOOK_FEEDBACK_ITALIC:
		t = GTK_TOGGLE_BUTTON (
			toolbar [TOOLBAR_ITALIC_BUTTON_INDEX].widget);
		set = data != NULL;

		gtk_signal_handler_block_by_func (GTK_OBJECT (t),
						  (GtkSignalFunc)&italic_cmd,
						  workbook);
		gtk_toggle_button_set_active (t, set);
		gtk_signal_handler_unblock_by_func (GTK_OBJECT (t),
						    (GtkSignalFunc)&italic_cmd,
						    workbook);
	}
}

/**
 * workbook_set_title:
 * @wb: the workbook to modify
 * @title: the title for the toplevel window
 *
 * Sets the toplelve window title of @wb to be @title
 */
void
workbook_set_title (Workbook *wb, const char *title)
{
	char *full_title;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (title != NULL);

	full_title = g_strconcat (_("Gnumeric: "), title, NULL);

 	gtk_window_set_title (GTK_WINDOW (wb->toplevel), full_title);
	g_free (full_title);
}

/**
 * workbook_set_filename:
 * @wb: the workbook to modify
 * @name: the file name for this worksheet.
 *
 * Sets the internal filename to @name and changes
 * the title bar for the toplevel window to be the name
 * of this file.
 */
void
workbook_set_filename (Workbook *wb, const char *name)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (name != NULL);

	if (wb->filename)
		g_free (wb->filename);

	wb->filename = g_strdup (name);

	workbook_set_title (wb, g_basename (name));
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

/**
 * workbook_fixup_references:
 * @wb: the workbook to modify
 * @sheet: the sheet containing the column/row that was moved
 * @col: starting column that was moved.
 * @row: starting row that was moved.
 * @coldelta: signed column distance that cells were moved.
 * @rowcount: signed row distance that cells were moved.
 *
 * Fixes references after a column or row move.
 */

void
workbook_fixup_references (Workbook *wb, Sheet *sheet, int col, int row,
			   int coldelta, int rowdelta)
{
	GList *cells, *l;
	EvalPosition epa, epb;

	g_return_if_fail (wb != NULL);

	if (coldelta == 0 && rowdelta == 0) return;

	/* Copy the list since it will change underneath us.  */
	cells = g_list_copy (wb->formula_cell_list);

	for (l = cells; l; l = l->next)	{
		Cell *cell = l->data;
		ExprTree *newtree;

		newtree = expr_tree_fixup_references (cell->parsed_node,
						      eval_pos_cell (&epa, cell),
						      eval_pos_init (&epb, sheet, col, row),
						      coldelta, rowdelta);
		if (newtree)
			cell_set_formula_tree (cell, newtree);
	}

	g_list_free (cells);
}

/**
 * workbook_invalidate_references:
 * @wb: the workbook to modify
 * @sheet:  the sheet containing column and row to be invalidated
 * @col: starting column to be invalidated.
 * @row: starting row to be invalidated.
 * @colcount: number of columns to be invalidated.
 * @rowcount: number of rows to be invalidated.
 *
 * Invalidates *either* a number of columns *or* a number of rows.
 */

void
workbook_invalidate_references (Workbook *wb, Sheet *sheet, int col, int row,
				int colcount, int rowcount)
{
	GList *cells, *l;
	EvalPosition epa, epb;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (colcount == 0 || rowcount == 0);
	g_return_if_fail (colcount >= 0 && rowcount >= 0);

	if (colcount == 0 && rowcount == 0) return;

	/* Copy the list since it will change underneath us.  */
	cells = g_list_copy (wb->formula_cell_list);

	for (l = cells; l; l = l->next)	{
		Cell *cell = l->data;
		ExprTree *newtree;

		newtree = expr_tree_invalidate_references (cell->parsed_node,
							   eval_pos_cell (&epa, cell),
							   eval_pos_init (&epb, sheet, col, row),
							   colcount, rowcount);
		if (newtree)
			cell_set_formula_tree (cell, newtree);
	}

	g_list_free (cells);
}


/* Old code discarded the order
static void
cb_workbook_sheets (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	GList **l = user_data;

	*l = g_list_prepend (*l, sheet);
}
*/

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

	for (i = 0; i < sheets; i++){
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

	sel = sheet_selection_to_string (sheet, include_prefix);
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

