/*
 * dialog-delete-cells.c: Delete a number of cells. 
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
dialog_delete_cells (Workbook *wb, Sheet *sheet)
{
	int state [4] = { 1, 0, 0, 0 };
	SheetSelection *ss;
	char *ret;
	int  cols, rows;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	if (!sheet_verify_selection_simple (sheet, _("delete cells")))
		return;

	ss = sheet->selections->data;
	cols = ss->user.end.col - ss->user.start.col + 1;
	rows = ss->user.end.row - ss->user.start.row + 1;

	/* short circuit the dialog if an entire row/column is selected */
	if (ss->user.start.row == 0 && ss->user.end.row  >= SHEET_MAX_ROWS-1)
	{
		sheet_delete_col (sheet, ss->user.start.col, cols);
		return;
	}
	if (ss->user.start.col == 0 && ss->user.end.col  >= SHEET_MAX_COLS-1)
	{
		sheet_delete_row (sheet, ss->user.start.row, rows);
		return;
	}

	ret = gtk_dialog_cauldron (
		_("Delete cells"),
		GTK_CAULDRON_DIALOG,
		"( %[ ( %Rd // %Rd / %Rd // %Rd ) ] /   (   %Bqrg || %Bqrg ) )",
		_("Delete"),
		_("Shift cells left"),   &state[0],
		_("Shift cells up"),     &state[1],
		_("Delete row(s)"),      &state[2],
		_("Delete column(s)"),   &state[3],
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL);

	if (ret == NULL)
		return;
			
	if (strcmp (ret, GNOME_STOCK_BUTTON_CANCEL) == 0)
		return;

	if (state [0]){
		sheet_shift_rows (sheet, ss->user.start.col, ss->user.start.row, ss->user.end.row, -cols);
		return;
	}

	if (state [1]){
		sheet_shift_cols (sheet, ss->user.start.col, ss->user.end.col, ss->user.start.row, -rows);
		return;
	}

	if (state [2]){
		sheet_delete_row (sheet, ss->user.start.row, rows);
		return;
	}

	/* default */
	sheet_delete_col (sheet, ss->user.start.col, cols);
}
