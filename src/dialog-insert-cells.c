/*
 * dialog-insert-cells.c: Insert a number of cells. 
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"

typedef enum {
	CMD_SHIFT_CELLS_RIGHT,
	CMD_SHIFT_CELLS_DOWN,
	CMD_INSERT_ROW,
	CMD_INSERT_COL
} CommandType;

void
dialog_insert_cells (Sheet *sheet)
{
	CommandType state = CMD_SHIFT_CELLS_RIGHT;
	SheetSelection *ss;
	char *ret;
	int  cols, rows;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	if (!sheet_verify_selection_simple (sheet, _("insert cells")))
		return;

	ret = gtk_dialog_cauldron (
		_("Insert cells"),
		GTK_CAULDRON_DIALOG,
		"( %[ ( %Rd // %Rd / %Rd // %Rd ) ] /   (   %Bqrg || %Bqrg ) )",
		_("Insert"),
		_("Shift cells right"), &state,
		_("Shift cells down"),  &state,
		_("Insert a row"),      &state,
		_("Insert a column"),   &state,
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL);

	printf ("return: %s\n", ret);
	if (strcmp (ret, GNOME_STOCK_BUTTON_CANCEL) == 0)
		return;

	ss = sheet->selections->data;
	cols = ss->end_col - ss->start_col + 1;
	rows = ss->end_row - ss->start_row + 1;
	
	switch (state){
	case CMD_SHIFT_CELLS_RIGHT:
		sheet_shift_rows (sheet, ss->start_col, ss->start_row, ss->end_row, cols);
		break;
		
	case CMD_SHIFT_CELLS_DOWN:
		sheet_shift_cols (sheet, ss->start_col, ss->end_col, ss->start_row, rows);
		break;
		
	case CMD_INSERT_COL:
		sheet_insert_col (sheet, ss->start_col, cols);
		break;
		
	case CMD_INSERT_ROW:
		sheet_insert_row (sheet, ss->start_row, rows);
		break;
	}
}
