/*
 * workbook.c:  Workbook management (toplevel windows)
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org).
 *
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <libgnome/lib_date.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "xml-io.h"
#include "plugin.h"
#include "pixmaps.h"

#include "../plugins/excel/ms-ole.h"
#include "../plugins/excel/ms-excel.h"

/* The locations within the main table in the workbook */
#define WB_EA_LINE   0
#define WB_EA_SHEETS 1
#define WB_EA_STATUS 2

#define WB_COLS      1

Workbook *current_workbook;
static int workbook_count;

static GList *workbook_list = NULL;

/**
 * Wrapper that decides which format to use
 **/
Workbook *
workbook_read (const char *filename)
{
  /* A slow and possibly buggy check for now. */
  MS_OLE *f = ms_ole_new (filename) ;
  Workbook *wb;
  if (f)
    {
      wb = ms_excelReadWorkbook(f) ;
      free (f) ;
    }
  else
    wb = gnumericReadXmlWorkbook (filename);
  return wb ;
}

static void
new_cmd (void)
{
	Workbook *wb;
	wb = workbook_new_with_sheets (1);
	gtk_widget_show (wb->toplevel);
}

static void
open_cmd (void)
{
	char *fname = dialog_query_load_file ();
	Workbook *wb;

	if ((wb = workbook_read (fname)))
	  gtk_widget_show (wb->toplevel);
}

static void
save_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_save (wb);
}

static void
save_as_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_save_as (wb);
}

static void
plugins_cmd (GtkWidget *widget, Workbook *wb)
{
	GtkWidget *pm = plugin_manager_new();
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
 * @idx: the index in the X Logical font description to try to replace with
 * @new: list of possible values we want to substitute with.
 *
 * Changes the font for the selection for the range: it does this by replacing
 * the idxth component (counting from zero) of the X logical font description
 * with the values listed (in order of preference) in the new array.
 */
static void
change_selection_font (Workbook *wb, int idx, char *new[])
{
	Sheet *sheet;
	GList *cells, *l;

	sheet = workbook_get_current_sheet (wb);
	cells = sheet_selection_to_list (sheet);

	for (l = cells; l; l = l->next){
		Cell *cell = l->data;
		char *old_name, *new_name;
		int i;
		StyleFont *f;

		for (i = 0; new [i]; i++){
			old_name = cell->style->font->font_name;
			new_name = font_change_component (old_name, idx, new [i]);
			f = style_font_new_simple (new_name, cell->style->font->units);
			g_free (new_name);
			
			if (f){
				cell_set_font_from_style (cell, f);
				break;
			}
		}
	}

	g_list_free (cells);
}

static void
bold_cmd (GtkWidget *widget, Workbook *wb)
{
	GtkToggleButton *t = GTK_TOGGLE_BUTTON (widget);
	char *bold_names   [] = { "bold", NULL };
	char *normal_names [] = { "regular", "medium", "light", NULL };

	if (!t->active)
		change_selection_font (wb, 2, normal_names);
	else
		change_selection_font (wb, 2, bold_names);
}

static void
italic_cmd (GtkWidget *widget, Workbook *wb)
{
	GtkToggleButton *t = GTK_TOGGLE_BUTTON (widget);
	char *italic_names   [] = { "i", "o", NULL };
	char *normal_names [] = { "r", NULL };

	if (!t->active)
		change_selection_font (wb, 3, normal_names);
	else
		change_selection_font (wb, 3, italic_names);
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
workbook_do_destroy (Workbook *wb)
{
	if (wb->filename)
	       g_free (wb->filename);

	g_list_free (wb->eval_queue);

	if (wb->auto_expr){
		expr_tree_unref (wb->auto_expr);
		string_unref (wb->auto_expr_desc);
	}

	workbook_list = g_list_remove (workbook_list, wb);
	workbook_count--;

	symbol_table_destroy (wb->symbol_names);
	
	if (workbook_count == 0)
		gtk_main_quit ();
}

void
workbook_destroy (Workbook *wb)
{
	gtk_widget_destroy (wb->toplevel);
}

static void
workbook_widget_destroy (GtkWidget *widget, Workbook *wb)
{
	workbook_do_destroy (wb);
}

static void
cb_sheet_mark_clean (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;

	sheet_mark_clean (sheet);
}

void
workbook_mark_clean (Workbook *wb)
{
	g_return_if_fail (wb != NULL);

	g_hash_table_foreach (wb->sheets, cb_sheet_mark_clean, NULL);
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
	char *f, *s;
	
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

	if (sheet->workbook->filename)
		f = g_basename (sheet->workbook->filename);
	else
		f = "";
	
	s = g_copy_strings (_("Workbook "), f,
			    _(" has unsaved changes, save them?"),
			    NULL);
	l = gtk_label_new (s);
	gtk_widget_show (l);
	g_free (s);
	
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG(d)->vbox), l, TRUE, TRUE, 0);

	gtk_window_position (GTK_WINDOW (d), GTK_WIN_POS_MOUSE);
	button = gnome_dialog_run (GNOME_DIALOG (d));

	switch (button){
		/* YES */
	case 0:
		workbook_save (sheet->workbook);
		*allow_close = CLOSE_RECHECK;
		break;

		/* NO */
	case 1:
		workbook_mark_unmodified (sheet->workbook);
		break;
		
		/* CANCEL */
	case -1:
	case 2:
		*allow_close = CLOSE_DENY;
		break;

	} 

	gtk_widget_destroy (d);
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
	if (workbook_can_close (wb))
		workbook_destroy (wb);
}

static int
workbook_delete_event (GtkWidget *widget, GdkEvent *event, Workbook *wb)
{
	if (workbook_can_close (wb)){
		workbook_destroy (wb);
		return TRUE;
	} else
		return FALSE;
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

		if (workbook_can_close (wb))
			workbook_destroy (wb);
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
			sheet_object_destroy (sheet->current_object);
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
	flags = dialog_paste_special ();
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
goto_cell_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_goto_cell (wb);
}

static void
define_cell_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_define_names (wb);
}

static void
insert_sheet_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;
	char *name;

	name = workbook_sheet_get_free_name (wb);
	sheet = sheet_new (wb, name);
	g_free (name);
	
	workbook_attach_sheet (wb, sheet);
}

static void
insert_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;

	sheet = workbook_get_current_sheet (wb);
	dialog_insert_cells (sheet);
}

static void
insert_cols_cmd (GtkWidget *widget, Workbook *wb)
{
	SheetSelection *ss;
	Sheet *sheet;
	int cols;
	
	sheet = workbook_get_current_sheet (wb);
	if (!sheet_verify_selection_simple (sheet, _("Insert rows")))
		return;
	
	ss = sheet->selections->data;
	cols = ss->end_col - ss->start_col + 1;
	sheet_insert_col (sheet, ss->start_col, cols);
}

static void
insert_rows_cmd (GtkWidget *widget, Workbook *wb)
{
	SheetSelection *ss;
	Sheet *sheet;
	int rows;
	
	sheet = workbook_get_current_sheet (wb);
	if (!sheet_verify_selection_simple (sheet, _("Insert rows")))
		return;

	ss = sheet->selections->data;
	rows = ss->end_row - ss->start_row + 1;
	sheet_insert_row (sheet, ss->start_row, rows);
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
	dialog_zoom (sheet);
}

static void
format_cells_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet;
	
	sheet = workbook_get_current_sheet (wb);
	dialog_cell_format (sheet);
}

static void
recalc_cmd (GtkWidget *widget, Workbook *wb)
{
	workbook_recalc_all (wb);
}

static void
insert_at_cursor (Sheet *sheet, double n, char *format)
{
	char buffer [40];
	Cell *cell;
	
	snprintf (buffer, sizeof (buffer)-1, "%g", n);

	cell = sheet_cell_fetch (sheet, sheet->cursor_col, sheet->cursor_row);
	cell_set_text (cell, buffer);
	cell_set_format (cell, format);

	workbook_recalc (sheet->workbook);
}

static void
insert_current_date_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);
	char *preferred_date_format = _(">mm/dd/yyyy");
	int n;
	
	n = calc_days (tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday) -
		calc_days (1900, 1, 1) + 1;

	insert_at_cursor (sheet, n, preferred_date_format+1);
}

static void
insert_current_time_cmd (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);
	char *preferred_time_format = _(">hh:mm");
	double n;
	
	n = (tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec)/(24*3600.0);
	insert_at_cursor (sheet, n, preferred_time_format);
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
	
	cell_set_comment (cell, "Test comment");
}

static void
about_cmd (GtkWidget *widget, Workbook *wb)
{
	dialog_about ();
}

/* File menu */

static GnomeUIInfo workbook_menu_file [] = {
	{ GNOME_APP_UI_ITEM, N_("_New"), NULL, new_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_NEW, 'n', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Open..."), NULL, open_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_OPEN, 'o', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Save"), NULL, save_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_SAVE, 's', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("Save _as..."), NULL, save_as_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_SAVE_AS },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("Plu_g-ins..."), NULL, plugins_cmd },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_Close"), NULL, close_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_CLOSE, 'w', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("E_xit"), NULL, quit_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_EXIT, 'q', GDK_CONTROL_MASK },
	GNOMEUIINFO_END
};

/* Edit menu */

static GnomeUIInfo workbook_menu_edit_clear [] = {
	{ GNOME_APP_UI_ITEM, N_("_All"),      NULL, clear_all_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Formats"),  NULL, clear_formats_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Comments"), NULL, clear_comments_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Content"),  NULL, clear_content_cmd },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_edit [] = {
	{ GNOME_APP_UI_ITEM, N_("Cu_t"), NULL, cut_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_CUT, 'x', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Copy"), NULL, copy_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_COPY, 'c', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Paste"), NULL, paste_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_PASTE, 'v', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("P_aste special..."), NULL, paste_special_cmd },
	{ GNOME_APP_UI_SUBTREE, N_("C_lear"), NULL, workbook_menu_edit_clear },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_Select All"), NULL, select_all_cmd, NULL, NULL,
	  0, 0, 'a', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Goto cell.."), NULL, goto_cell_cmd, NULL, NULL,
	  0, 0, 'i', GDK_CONTROL_MASK },

	GNOMEUIINFO_SEPARATOR,

#if 0
	{ GNOME_APP_UI_ITEM, N_("_Define cell names"), NULL, define_cell_cmd },
	GNOMEUIINFO_SEPARATOR,
#endif
	
	{ GNOME_APP_UI_ITEM, N_("_Recalculate"), NULL, recalc_cmd, NULL, NULL,
	  0, 0, GDK_F9, 0 },
	GNOMEUIINFO_END
};

/* View menu */

static GnomeUIInfo workbook_menu_view [] = {
	{ GNOME_APP_UI_ITEM, N_("_Zoom..."), NULL, zoom_cmd },
	GNOMEUIINFO_END
};

/* Insert menu */

static GnomeUIInfo workbook_menu_insert_special [] = {
	{ GNOME_APP_UI_ITEM, N_("Current _date"), NULL, insert_current_date_cmd,
	  NULL, NULL, 0, 0, ';', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("Current _time"), NULL, insert_current_time_cmd,
	  NULL, NULL, 0, 0, ';', GDK_CONTROL_MASK | GDK_SHIFT_MASK },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_insert [] = {
	{ GNOME_APP_UI_ITEM, N_("_Sheet"),    NULL, insert_sheet_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Cells..."), NULL, insert_cells_cmd },
	{ GNOME_APP_UI_ITEM, N_("_Rows"),     NULL, insert_rows_cmd  },
	{ GNOME_APP_UI_ITEM, N_("C_olumns"),  NULL, insert_cols_cmd  },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_Add/Modify comment"), NULL, workbook_edit_comment },
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
	{ GNOME_APP_UI_ITEM, N_("_Cells..."),   NULL, format_cells_cmd, NULL, NULL,
	  0, 0, GDK_1, GDK_CONTROL_MASK },
#if 0
	{ GNOME_APP_UI_SUBTREE, N_("C_olumn"), NULL, workbook_menu_format_column },
	{ GNOME_APP_UI_SUBTREE, N_("_Row"),    NULL, workbook_menu_format_row },
	{ GNOME_APP_UI_SUBTREE, N_("_Sheet"),  NULL, workbook_menu_format_sheet },
#endif
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_help [] = {
	{ GNOME_APP_UI_ITEM, N_("_About Gnumeric..."), NULL, about_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_ABOUT, 0, 0, NULL },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu [] = {
	{ GNOME_APP_UI_SUBTREE, N_("_File"),   NULL, workbook_menu_file },
	{ GNOME_APP_UI_SUBTREE, N_("_Edit"),   NULL, workbook_menu_edit },
	{ GNOME_APP_UI_SUBTREE, N_("_View"),   NULL, workbook_menu_view },
	{ GNOME_APP_UI_SUBTREE, N_("_Insert"), NULL, workbook_menu_insert },
	{ GNOME_APP_UI_SUBTREE, N_("F_ormat"), NULL, workbook_menu_format },
	{ GNOME_APP_UI_SUBTREE, N_("_Help"),   NULL, workbook_menu_help },
	GNOMEUIINFO_END
};

#define TOOLBAR_BOLD_BUTTON_INDEX 12
#define TOOLBAR_ITALIC_BUTTON_INDEX 13
static GnomeUIInfo workbook_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("New"), N_("Create a new sheet"),
		new_cmd, GNOME_STOCK_PIXMAP_NEW),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Open"), N_("Opens an existing workbook"),
		open_cmd, GNOME_STOCK_PIXMAP_OPEN),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Save"), N_("Saves the workbook"),
		save_cmd, GNOME_STOCK_PIXMAP_SAVE),

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

	GNOMEUIINFO_ITEM_DATA (
		N_("Left align"), N_("Sets the cell alignment to the left"),
		left_align_cmd, NULL, align_left),
	GNOMEUIINFO_ITEM_DATA (
		N_("Center"), N_("Centers the cell contents"),
		center_cmd, NULL, align_center),
	GNOMEUIINFO_ITEM_DATA (
		N_("Right align"), N_("Sets the cell alignment to the right"),
		right_align_cmd, NULL, align_right),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_TOGGLEITEM_DATA (
		N_("Bold"), N_("Sets the bold attribute on the cell"),
		bold_cmd, NULL, bold_xpm),

	GNOMEUIINFO_TOGGLEITEM_DATA (
		N_("Italic"), N_("Sets the italic attribute on the cell"),
		italic_cmd, NULL, italic_xpm),
	
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
	workbook_focus_current_sheet (wb);
}

static void
workbook_setup_sheets (Workbook *wb)
{
	wb->notebook = gtk_notebook_new ();
	GTK_WIDGET_UNSET_FLAGS (wb->notebook, GTK_CAN_FOCUS);
	gtk_signal_connect_after (GTK_OBJECT (wb->notebook), "switch_page",
				  GTK_SIGNAL_FUNC(do_focus_sheet), wb);
				  
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (wb->notebook), GTK_POS_BOTTOM);
		
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

static void
wb_input_finished (GtkEntry *entry, Workbook *wb)
{
	Sheet *sheet;
	
	sheet = workbook_get_current_sheet (wb);

	sheet_set_current_value (sheet);
	workbook_focus_current_sheet (wb);
}

int
workbook_parse_and_jump (Workbook *wb, char *text)
{
	int col, row;

	col = row = 0;

	if (!parse_cell_name (text, &col, &row)){
		gnumeric_notice (_("You should introduce a valid cell name"));
		return FALSE;
	} else {
		Sheet *sheet = workbook_get_current_sheet (wb);

		if (col > SHEET_MAX_COLS-1){
			gnumeric_notice (_("Column out of range"));
			return FALSE;
		}

		if (row > SHEET_MAX_ROWS-1){
			gnumeric_notice (_("Row number out of range"));
			return FALSE;
		}
		
		sheet_make_cell_visible (sheet, col, row);
		sheet_cursor_move (sheet, col, row);
		return TRUE;
	}
}

static void
wb_jump_to_cell (GtkEntry *entry, Workbook *wb)
{
	char *text = gtk_entry_get_text (entry);

	workbook_parse_and_jump (wb, text);
	workbook_focus_current_sheet (wb);
}

void
workbook_set_region_status (Workbook *wb, char *str)
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
workbook_setup_edit_area (Workbook *wb)
{
	GtkWidget *ok_button, *cancel_button, *box, *box2;
	GtkWidget *pix;
	
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
	gtk_box_pack_start (GTK_BOX (box2), box, 0, 0, 0);
	gtk_box_pack_end   (GTK_BOX (box2), wb->ea_input, 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wb->table), box2,
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* Do signal setup for the editing input line */
	gtk_signal_connect (GTK_OBJECT (wb->ea_input), "activate",
			    GTK_SIGNAL_FUNC(wb_input_finished),
			    wb);
#if 0
	gtk_signal_connect (GTK_OBJECT (wb->ea_input), "key_press_event",
			    GTK_SIGNAL_FUNC(wb_edit_key_pressed),
			    wb);
#endif
	
	/* Do signal setup for the status input line */
	gtk_signal_connect (GTK_OBJECT (wb->ea_status), "activate",
			    GTK_SIGNAL_FUNC (wb_jump_to_cell),
			    wb);
}


static struct {
	char *displayed_name;
	char *function;
} quick_compute_routines [] = {
	{ N_("Sum"),   	       "SUM(SELECTION())" },
	{ N_("Min"),   	       "MIN(SELECTION())" },
	{ N_("Max"),   	       "MAX(SELECTION())" },
	{ N_("Average"),       "AVERAGE(SELECTION())" },
	{ N_("Count"),         "COUNT(SELECTION())" },
	{ NULL, NULL }
};

/*
 * Sets the expression that gets evaluated whenever the
 * selection in the sheet changes
 */
static char *
workbook_set_auto_expr (Workbook *wb, char *description, char *expression)
{
	char *error = NULL;
	
	if (wb->auto_expr){
		g_assert (wb->auto_expr->ref_count == 1);
		expr_tree_unref (wb->auto_expr);
		string_unref (wb->auto_expr_desc);
	}
	wb->auto_expr = expr_parse_string (expression, 0, 0, 0, NULL, &error);
	wb->auto_expr_desc = string_get (description);

	return error;
}

static void
change_auto_expr (GtkWidget *item, Workbook *wb)
{
	char *expr, *name;

	expr = gtk_object_get_data (GTK_OBJECT (item), "expr");
	name = gtk_object_get_data (GTK_OBJECT (item), "name");
	workbook_set_auto_expr (wb, name, expr);
	sheet_update_auto_expr (workbook_get_current_sheet (wb));
}

static void
change_auto_expr_menu (GtkWidget *widget, GdkEventButton *event, Workbook *wb)
{
	static GtkWidget *menu;

	if (!menu){
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
	}
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, 0, NULL, 1, event->time);
}

static void
workbook_setup_status_area (Workbook *wb)
{
	GtkWidget *canvas;
	GnomeCanvasGroup *root;
	GtkWidget *t;
	
	t = gtk_table_new (0, 0, 0);
	gtk_table_attach (GTK_TABLE (wb->table), t,
			  0, WB_COLS, WB_EA_STATUS, WB_EA_STATUS+1, GTK_FILL | GTK_EXPAND, 0, 0, 0);

	canvas = gnome_canvas_new ();
	gtk_widget_set_usize (canvas, 1, 1);

	/* The canvas that displays text */
	root = GNOME_CANVAS_GROUP (GNOME_CANVAS (canvas)->root);
	wb->auto_expr_label = GNOME_CANVAS_ITEM (gnome_canvas_item_new (
		root, gnome_canvas_text_get_type (),
		"text",   "x",
		"x",      (double) 0,
		"y",      (double) 10,	/* FIXME :-) */
		"font",   "fixed", /* FIXME :-) */
		"anchor", GTK_ANCHOR_W,
		"fill_color", "black",
		NULL));
	
	gtk_table_attach (GTK_TABLE (t), canvas, 1, 2, 0, 1,
			  GTK_FILL, GTK_FILL, 0, 0);
	/* The following is just a trick to get the canvas to "adjust" his
	 * size to the proper value (as we created it with 1,1 and
	 * GTK_FILL options for X and Y
	 */
	gtk_table_attach (GTK_TABLE (t), gtk_label_new ("WWWWWWWWW"), 1, 2, 0, 1,
			  GTK_FILL, GTK_FILL, 0, 0);
	gtk_signal_connect (GTK_OBJECT (canvas), "button_press_event",
			    GTK_SIGNAL_FUNC (change_auto_expr_menu), wb);
	
	gtk_table_attach (GTK_TABLE (t), gtk_label_new ("Info"), 0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_widget_show_all (t);
}

void
workbook_auto_expr_label_set (Workbook *wb, char *text)
{
	char *res;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (text != NULL);
	
	res = g_copy_strings (wb->auto_expr_desc->str, "=",
			      text, NULL);
	gnome_canvas_item_set (wb->auto_expr_label,
			       "text", res,
			       NULL);
	g_free (res);
}

static void
workbook_set_focus (GtkWindow *window, GtkWidget *focus, Workbook *wb)
{
	if (!window->focus_widget)
		workbook_focus_current_sheet (wb);
}

/*
 * Sets up the workbook.
 * Right now it is adding some decorations to the window,
 * this is for testing purposes.
 */
Workbook *
workbook_new (void)
{
	Workbook *wb;

	wb = g_new0 (Workbook, 1);
	wb->toplevel  = gnome_app_new ("Gnumeric", "Gnumeric");
	wb->sheets    = g_hash_table_new (gnumeric_strcase_hash, gnumeric_strcase_equal);
	wb->table     = gtk_table_new (0, 0, 0);

	wb->symbol_names = symbol_table_new ();

	gtk_window_set_policy(GTK_WINDOW(wb->toplevel), 1, 1, 0);

	wb->max_iterations = 1;

	workbook_set_title (wb, _("Untitled.gnumeric"));
	
	workbook_setup_status_area (wb);
	workbook_setup_edit_area (wb);
	workbook_setup_sheets (wb);
	gnome_app_set_contents (GNOME_APP (wb->toplevel), wb->table);
	gnome_app_create_menus_with_data (GNOME_APP (wb->toplevel), workbook_menu, wb);
	gnome_app_create_toolbar_with_data (GNOME_APP (wb->toplevel), workbook_toolbar, wb);
	gtk_toolbar_set_style (GTK_TOOLBAR (GNOME_APP (wb->toplevel)->toolbar), GTK_TOOLBAR_ICONS);

	/* Focus handling */
	gtk_signal_connect_after (
		GTK_OBJECT (wb->toplevel), "set_focus",
		GTK_SIGNAL_FUNC (workbook_set_focus), wb);

	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "destroy",
		GTK_SIGNAL_FUNC (workbook_widget_destroy), wb);

	/* clipboard setup */
	x_clipboard_bind_workbook (wb);
	
	/* delete_event */
	gtk_signal_connect (
		GTK_OBJECT (wb->toplevel), "delete_event",
		GTK_SIGNAL_FUNC (workbook_delete_event), wb);
	
	/* Set the default operation to be performed over selections */
	workbook_set_auto_expr (wb, "SUM", "SUM(SELECTION())");

	workbook_count++;

	workbook_list = g_list_prepend (workbook_list, wb);
	
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
buttons (Sheet *sheet, GtkTable *table)
{
	GtkWidget *b;
	
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
	
}

void
workbook_attach_sheet (Workbook *wb, Sheet *sheet)
{
	GtkWidget *t;

	g_hash_table_insert (wb->sheets, sheet->name, sheet);

	t = gtk_table_new (0, 0, 0);
	gtk_table_attach (GTK_TABLE (t), GTK_WIDGET (sheet->sheet_views->data),
			  0, 3, 0, 1,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	buttons (sheet, GTK_TABLE (t));
	gtk_widget_show_all (t);
	gtk_object_set_data (GTK_OBJECT (t), "sheet", sheet);
	gtk_notebook_append_page (GTK_NOTEBOOK (wb->notebook),
				  t, gtk_label_new (sheet->name));
}

Sheet *
workbook_sheet_lookup (Workbook *wb, char *sheet_name)
{
	Sheet *sheet;
	
	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (sheet_name != NULL, NULL);

	sheet = g_hash_table_lookup (wb->sheets, sheet_name);

	return sheet;
}

char *
workbook_sheet_get_free_name (Workbook *wb)
{
	char name [80];
	int  i;
	
	g_return_val_if_fail (wb != NULL, NULL);

	for (i = 0; ; i++){
		g_snprintf (name, sizeof (name), _("Sheet %d"), i);
		if (workbook_sheet_lookup (wb, name) == NULL)
			return g_strdup (name);
	}
	g_assert_not_reached ();
	return NULL;
}

Workbook *
workbook_new_with_sheets (int sheet_count)
{
	Workbook *wb;
	Sheet *first_sheet = 0;
	int i;

	wb = workbook_new ();

	for (i = 0; i < sheet_count; i++){
		Sheet *sheet;
		char name [80];

		snprintf (name, sizeof (name), _("Sheet %d"), i);
		sheet = sheet_new (wb, name);
		workbook_attach_sheet (wb, sheet);

		if (!first_sheet)
			first_sheet = sheet;
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
	int set;
	
	g_return_if_fail (workbook != NULL);
	
	switch (type){
	case WORKBOOK_FEEDBACK_BOLD:
		t = GTK_TOGGLE_BUTTON (
			workbook_toolbar [TOOLBAR_BOLD_BUTTON_INDEX].widget);
		set = data != NULL;

		gtk_toggle_button_set_state (t, set);
		break;
		
	case WORKBOOK_FEEDBACK_ITALIC:
		t = GTK_TOGGLE_BUTTON (
			workbook_toolbar [TOOLBAR_ITALIC_BUTTON_INDEX].widget);
		set = data != NULL;

		gtk_toggle_button_set_state (t, set);
	}
}

void
workbook_set_title (Workbook *wb, char *title)
{
	char *full_title;
	
	g_return_if_fail (wb != NULL);
	g_return_if_fail (title != NULL);

	full_title = g_copy_strings ("Gnumeric: ", title, NULL);
	
 	gtk_window_set_title (GTK_WINDOW (wb->toplevel), full_title);
	g_free (full_title);
}

void
workbook_set_filename (Workbook *wb, char *name)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (name != NULL);

	if (wb->filename)
		g_free (wb->filename);
	
	wb->filename = g_strdup (name);

	workbook_set_title (wb, g_basename (name));
}
