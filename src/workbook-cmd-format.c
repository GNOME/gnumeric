/*
 * workbook.c:  Workbook format commands hooked to the menus
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "eval.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "ranges.h"
#include "selection.h"
#include "workbook.h"
#include "workbook-cmd-format.h"
#include "dialogs.h"
#include "format.h"
static const char *money_format   = "Default Money Format:$#,##0_);($#,##0)";
static const char *percent_format = "Default Percent Format:0.00%";

static Value *
set_cell_format_style (Sheet *sheet, int col, int row, Cell *cell, void *_style)
{
	Style *style = _style;

	cell_set_format_from_style (cell, style->format);
	return NULL;
}

static void
apply_format (Sheet *sheet,
	      int start_col, int start_row,
	      int end_col,   int end_row,
	      void *closure)
{
	Style *copy = style_duplicate (closure);
	
	sheet_style_attach (
		sheet, start_col, start_row, end_col, end_row, copy);
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row, end_col, end_row,
		set_cell_format_style, closure);
}

static void
do_apply_style_to_selection (Sheet *sheet, const char *format)
{
	Style *style;
	const char *real_format = strchr (_(format), ':');

	if (real_format)
		real_format++;
	else
		return;
	
	style = style_new_empty ();
	style->valid_flags = STYLE_FORMAT;
	style->format = style_format_new (real_format);
	
	selection_apply (sheet, apply_format, TRUE, style);
	sheet_set_dirty (sheet, TRUE);
	style_destroy (style);
}

void
workbook_cmd_format_as_money (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	
	do_apply_style_to_selection (sheet, _(money_format));
}

void
workbook_cmd_format_as_percent (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	
	do_apply_style_to_selection (sheet, _(percent_format));
}

/*
 * The routines that modify the format of a cell using the
 * helper routines in format.c.
 */
typedef char *(*format_modify_fn) (const char *format);
	
static Value *
modify_cell_format (Sheet *sheet, int col, int row, Cell *cell, void *closure)
{
	StyleFormat *sf = cell->style->format;
	format_modify_fn modify_format = closure;
	char *new_fmt;
		
	new_fmt = (*modify_format) (sf->format);
	if (new_fmt == NULL)
		return NULL;

	cell_set_format (cell, new_fmt);
	g_free (new_fmt);
	return NULL;
}

static void
modify_cell_region (Sheet *sheet,
		    int start_col, int start_row,
		    int end_col,   int end_row,
		    void *closure)
{
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row, end_col, end_row,
		modify_cell_format, closure);
}

/*
 * This is sort of broken, it just operates on the
 * existing cells, rather than the empty spots.
 * To do: think if it is worth doing.
 *
 * Ie, the user could set and set styles, and work on them
 * with no visible cell.  Does it matter?
 */
static void
do_modify_format (Workbook *wb, format_modify_fn modify_fn)
{
	Sheet *sheet = workbook_get_current_sheet (wb);

	selection_apply (sheet, modify_cell_region, FALSE, modify_fn);
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_add_thousands (GtkWidget *widget, Workbook *wb)
{
	do_modify_format (wb, format_add_thousand);
}

void
workbook_cmd_format_add_decimals (GtkWidget *widget, Workbook *wb)
{
	do_modify_format (wb, format_add_decimal);
}

void
workbook_cmd_format_remove_decimals (GtkWidget *widget, Workbook *wb)
{
	do_modify_format (wb, format_remove_decimal);
}

void
workbook_cmd_format_column_auto_fit (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	GList *l;
	int col;
	
	/*
	 * Apply autofit to any columns where the selection is
	 */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;

		for (col = ss->user.start.col; col <= ss->user.end.col; col++){
			int ideal_size;

			ideal_size = sheet_col_size_fit (sheet, col);
			if (ideal_size == 0)
				continue;

			sheet_col_set_width (sheet, col, ideal_size);
		}
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_column_width (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	GList *l;
	double value = 0.0;
	int col;
	
	/*
	 * Find out the initial value to display
	 */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		
		for (col = ss->user.start.col; col <= ss->user.end.col; col++){
			ColRowInfo *ci;

			ci = sheet_col_get_info (sheet, col);
			if (value == 0.0)
				value = ci->units;
			else if (value != ci->units){
				/*
				 * Values differ, so let the user enter the data
				 */
				value = 0.0;
				break;
			}
					
		}
	}
	
	if (!dialog_get_number (wb, "col-width.glade", &value))
		return;

	if (value <= 0.0){
		gnumeric_notice (
			wb, GNOME_MESSAGE_BOX_ERROR,
			N_("You entered an invalid column width value.  It must be bigger than 0"));
		return;
	}
	
	/*
	 * Apply the new value to all data
	 */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		int col;
		
		for (col = ss->user.start.col; col < ss->user.end.col; col++)
			sheet_col_set_width_units (sheet, col, value);
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_row_auto_fit (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	GList *l;
	int row;
	
	/*
	 * Apply autofit to any columns where the selection is
	 */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;

		for (row = ss->user.start.row; row <= ss->user.end.row; row++){
			int ideal_size;

			ideal_size = sheet_row_size_fit (sheet, row);
			if (ideal_size == 0)
				continue;

			sheet_row_set_height (sheet, row, ideal_size, FALSE);
		}
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_row_height (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	GList *l;
	double value = 0.0;
	
	/*
	 * Find out the initial value to display
	 */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		int row;
	
		for (row = ss->user.start.row; row <= ss->user.end.row; row++){
			ColRowInfo *ri;

			ri = sheet_row_get_info (sheet, row);
			if (value == 0.0)
				value = ri->units;
			else if (value != ri->units){
				/*
				 * Values differ, so let the user enter the data
				 */
				value = 0.0;
				break;
			}
		}
	}
	
	if (!dialog_get_number (wb, "row-height.glade", &value))
		return;

	if (value <= 0.0){
		gnumeric_notice (
			wb, GNOME_MESSAGE_BOX_ERROR,
			N_("You entered an invalid row height value.  It must be bigger than 0"));
		return;
	}
	
	/*
	 * Apply the new value to all data
	 */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		int row;
		
		for (row = ss->user.start.row; row < ss->user.end.row; row++)
			sheet_row_set_height_units (sheet, row, value, TRUE);
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_sheet_change_name (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	char *new_name;
	
	new_name = dialog_get_sheet_name (wb, sheet->name);
	if (!new_name)
		return;

	workbook_rename_sheet (wb, sheet->name, new_name);
	g_free (new_name);
}

