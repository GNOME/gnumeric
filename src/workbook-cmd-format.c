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
#include "ranges.h"
#include "selection.h"
#include "workbook.h"
#include "workbook-cmd-format.h"
#include "dialogs.h"
#include "format.h"
#include "commands.h"

void
workbook_cmd_format_column_auto_fit (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
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

			sheet_col_set_size_pixels (sheet, col, ideal_size, TRUE);
		}
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_column_width (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
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
				value = ci->size_pts;
			else if (value != ci->size_pts){
				/*
				 * Values differ, so let the user enter the data
				 */
				value = 0.0;
				break;
			}
					
		}
	}
	
	/* Scale and round to 2 decimal places */
	value *= COLUMN_WIDTH_SCALE * 100.;
	value = (int)(value + .5);
	value /= 100.;
	if (!dialog_get_number (wb, "col-width.glade", &value))
		return;
	value /= COLUMN_WIDTH_SCALE;

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
		
		for (col = ss->user.start.col; col <= ss->user.end.col; col++)
			sheet_col_set_size_pts (sheet, col, value, TRUE);
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_row_auto_fit (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
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

			sheet_row_set_size_pixels (sheet, row, ideal_size, FALSE);
		}
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_row_height (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
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
				value = ri->size_pts;
			else if (value != ri->size_pts){
				/*
				 * Values differ, so let the user enter the data
				 */
				value = 0.0;
				break;
			}
		}
	}
	
	/* Scale and round to 2 decimal places */
	value *= ROW_HEIGHT_SCALE * 100.;
	value = (int)(value + .5);
	value /= 100.;
	if (!dialog_get_number (wb, "row-height.glade", &value))
		return;
	value /= ROW_HEIGHT_SCALE;

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
		
		for (row = ss->user.start.row; row <= ss->user.end.row; row++)
			sheet_row_set_size_pts (sheet, row, value, TRUE);
	}
	sheet_set_dirty (sheet, TRUE);
}

void
workbook_cmd_format_sheet_change_name (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	char *new_name;
	
	new_name = dialog_get_sheet_name (wb, sheet->name);
	if (!new_name)
		return;

	cmd_rename_sheet (workbook_command_context_gui (wb),
			  wb, sheet->name, new_name);
	g_free (new_name);
}

void
workbook_cmd_format_column_hide (GtkWidget *widget, Workbook *wb)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (wb),
				      wb->current_sheet,
				      TRUE, FALSE);
}

void
workbook_cmd_format_column_unhide (GtkWidget *widget, Workbook *wb)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (wb),
				      wb->current_sheet,
				      TRUE, TRUE);
}

void
workbook_cmd_format_column_std_width (GtkWidget *widget, Workbook *wb)
{
	/* TODO */
}

void
workbook_cmd_format_row_hide (GtkWidget *widget, Workbook *wb)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (wb),
				      wb->current_sheet,
				      FALSE, FALSE);
}

void
workbook_cmd_format_row_unhide (GtkWidget *widget, Workbook *wb)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (wb),
				      wb->current_sheet,
				      FALSE, TRUE);
}

void
workbook_cmd_format_row_std_height (GtkWidget *widget, Workbook *wb)
{
	/* TODO */
}

