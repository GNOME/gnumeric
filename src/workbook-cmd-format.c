/*
 * workbook.c:  Workbook format commands hooked to the menus
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jody Goldberg (jgoldberg@home.com)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "eval.h"
#include "gnumeric-util.h"
#include "ranges.h"
#include "selection.h"
#include "workbook.h"
#include "application.h"
#include "workbook-cmd-format.h"
#include "dialogs.h"
#include "format.h"
#include "commands.h"

struct closure_colrow_resize {
	gboolean	 is_cols;
	ColRowIndexList *selection;
};

static gboolean
cb_colrow_collect (Sheet *sheet, Range const *r, gpointer user_data)
{
	struct closure_colrow_resize *info = user_data;
	int first, last;

	if (info->is_cols) {
		first = r->start.col;
		last = r->end.col;
	} else {
		first = r->start.row;
		last = r->end.row;
	}

	info->selection = col_row_get_index_list (first, last, info->selection);
	return TRUE;
}

void
workbook_cmd_format_column_width (Sheet *sheet, int new_size_pixels)
{
	struct closure_colrow_resize closure;
	closure.is_cols = TRUE;
	closure.selection = NULL;
	selection_foreach_range (sheet, TRUE, &cb_colrow_collect, &closure);
	cmd_resize_row_col (workbook_command_context_gui (sheet->workbook),
			    sheet, TRUE, closure.selection, new_size_pixels);
}
void
workbook_cmd_format_column_auto_fit (GtkWidget *widget, Workbook *wb)
{
	workbook_cmd_format_column_width (wb->current_sheet, -1);
}

void
sheet_dialog_set_column_width (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	GList *l;
	double value = 0.0;

	/* Find out the initial value to display */
	for (l = sheet->selections; l; l = l->next) {
		SheetSelection *ss = l->data;
		int col;

		for (col = ss->user.start.col; col <= ss->user.end.col; ++col){
			ColRowInfo const *ci = sheet_col_get_info (sheet, col);
			if (value == 0.0)
				value = ci->size_pts;
			else if (value != ci->size_pts){
				/* Values differ, so let the user enter the data */
				value = 0.0;
				break;
			}
		}
	}

	/* Scale and round to 3 decimal places */
	value *= 1000.;
	value = (int)(value + .5);
	value /= 1000.;

loop :
	if (!dialog_get_number (wb, "col-width.glade", &value))
		return;

	if (value <= 0.0) {
		gnumeric_notice (
			wb, GNOME_MESSAGE_BOX_ERROR,
			N_("You entered an invalid column width value.  It must be bigger than 0"));
		goto loop;
	}
	{
		double const scale =
			sheet->last_zoom_factor_used *
			application_display_dpi_get (TRUE) / 72.;
		int size_pixels = (int)(value * scale + 0.5);
		workbook_cmd_format_column_width (wb->current_sheet, size_pixels);
	}
}

void
workbook_cmd_format_row_height (Sheet *sheet, int new_size_pixels)
{
	struct closure_colrow_resize closure;
	closure.is_cols = FALSE;
	closure.selection = NULL;
	selection_foreach_range (sheet, TRUE, &cb_colrow_collect, &closure);
	cmd_resize_row_col (workbook_command_context_gui (sheet->workbook),
			    sheet, FALSE, closure.selection, new_size_pixels);
}

void
workbook_cmd_format_row_auto_fit (GtkWidget *widget, Workbook *wb)
{
	workbook_cmd_format_row_height (wb->current_sheet, -1);
}

void
sheet_dialog_set_row_height (GtkWidget *widget, Workbook *wb)
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
				/* Values differ, so let the user enter the data */
				value = 0.0;
				break;
			}
		}
	}

	/* Scale and round to 3 decimal places */
	value *= 1000.;
	value = (int)(value + .5);
	value /= 1000.;

loop :
	if (!dialog_get_number (wb, "row-height.glade", &value))
		return;

	if (value <= 0.0) {
		gnumeric_notice (
			wb, GNOME_MESSAGE_BOX_ERROR,
			N_("You entered an invalid row height value.  It must be bigger than 0"));
		goto loop;
	}
	{
		double const scale =
			sheet->last_zoom_factor_used *
			application_display_dpi_get (FALSE) / 72.;
		int size_pixels = (int)(value * scale + 0.5);
		workbook_cmd_format_row_height (wb->current_sheet, size_pixels);
	}
}

void
workbook_cmd_format_sheet_change_name (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	char *new_name;

	new_name = dialog_get_sheet_name (wb, sheet->name_unquoted);
	if (!new_name)
		return;

	cmd_rename_sheet (workbook_command_context_gui (wb),
			  wb, sheet->name_unquoted, new_name);
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

