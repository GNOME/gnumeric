/*
 * cell.c: Printing of cell regions and cells.
 *
 * Author:
 *    Miguel de Icaza 1999 (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "eval.h"
#include "format.h"
#include "color.h"

static void
print_cell (GnomePrintContext *context, Cell *cell)
{
	g_assert (cell != NULL);
	
}

void
print_cell_range (GnomePrintContext *context,
		  Sheet *sheet,
		  int start_col, int start_row,
		  int end_col, int end_row)
{
	int row, col;
	
	g_return_if_fail (context != NULL);
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (context));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col < end_col);
	g_return_if_fail (end_col < end_row);

	for (row = start_row; row <= end_row; row++){
		for (col = start_col; col <= end_col; col++){
			Cell *cell;

			cell = sheet_cell_get (sheet, col, row);
			if (!cell)
				continue;
			print_cell (context, cell);
		}
	}
}

void
print_cell_grid (GnomePrintContext *context,
		 Sheet *sheet, 
		 int start_col, int start_row,
		 int end_col, int end_row)
{
	GList *cols, *rows;
	int last_col_gen = -1, last_row_get = -1;
	
	g_return_if_fail (context != NULL);
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (context));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col < end_col);
	g_return_if_fail (end_col < end_row);

	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci;

		if (ci->pos < start_col)
			continue;
		if (ci->pos > end_col)
			break;

		if ((last_col_gen > 0) && (ci->pos != last_col_gen+1)){
			int i;
			
			for (i = last_col_gen; i < ci->pos; i++)
				vline (context, &sheet->default_col_style);
		}
		vline (context, ci);
		last_col_gen = ci->pos;
	}

	for (rows = sheet->rows_info; rows; rows = rows->next){
		ColRowInfo *ri;

		if (ri->pos < start_row)
			continue;
		if (ri->pos > end_row)
			break;

		if ((last_row_gen > 0) && (ri->pos != last_row_gen+1)){
			int i;
			
			for (i = last_row_gen; i < ri->pos; i++)
				hline (context, &sheet->default_row_style);
		}
		hline (context, ri);
		last_row_gen = ri->pos;
	}
}



