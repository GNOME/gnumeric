/*
 * dialog-insert-cells.c: Insert a number of cells.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "selection.h"
#include "dialogs.h"

void
dialog_insert_cells (Workbook *wb, Sheet *sheet)
{
	int state [4] = { 1, 0, 0, 0 };
	SheetSelection *ss;
	char *ret;
	int  cols, rows;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!sheet_verify_selection_simple (sheet, _("insert cells")))
		return;

	ss = sheet->selections->data;
	cols = ss->end_col - ss->start_col + 1;
	rows = ss->end_row - ss->start_row + 1;

	/* short circuit the dialog if an entire row/column is selected */
	if (ss->start_row == 0 && ss->end_row  >= SHEET_MAX_ROWS-1)
	{
		sheet_insert_col (sheet, ss->start_col, cols);
		return;
	}
	if (ss->start_col == 0 && ss->end_col  >= SHEET_MAX_COLS-1)
	{
		sheet_insert_row (sheet, ss->start_row, rows);
		return;
	}

	ret = gtk_dialog_cauldron (
		_("Insert cells"),
		GTK_CAULDRON_DIALOG,
		"( %[ ( %Rd // %Rd / %Rd // %Rd ) ] /   (   %Bqrg || %Bqrg ) )",
		_("Insert"),
		_("Shift cells right"), &state[0],
		_("Shift cells down"),  &state[1],
		_("Insert row(s)"),      &state[2],
		_("Insert column(s)"),   &state[3],
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL);

	if (ret == NULL)
		return;

	if (strcmp (ret, GNOME_STOCK_BUTTON_CANCEL) == 0)
		return;

	if (state [0]){
		sheet_shift_rows (sheet, ss->start_col, ss->start_row, ss->end_row, cols);
		return;
	}

	if (state [1]){
		sheet_shift_cols (sheet, ss->start_col, ss->end_col, ss->start_row, rows);
		return;
	}

	if (state [2]){
		sheet_insert_row (sheet, ss->start_row, rows);
		return;
	}

	/* default */
	sheet_insert_col (sheet, ss->start_col, cols);
}
