#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"

Sheet *
sheet_new (Workbook *wb, char *name)
{
	Sheet *sheet;

	sheet = g_new0 (Sheet, 1);
	sheet->parent_workbook = wb;
	sheet->name = g_strdup (name);
	sheet->sheet_view = gnumeric_sheet_new (sheet);
	gtk_widget_show (sheet->sheet_view);
	
	return sheet;
}

ColInfo *
sheet_get_col_info (Sheet *sheet, int col)
{
	static ColInfo c;

	c.col   = col;
	c.width = (col + 1) * 15;
	c.style = NULL;

	return &c;
}

RowInfo *
sheet_get_row_info (Sheet *sheet, int row)
{
	static RowInfo r;

	/* Just a stub for now */
	r.row    = row;
	r.height = 20;
	r.style  = NULL;

	return &r;
}

/*
 * Return the number of pixels between from_col to to_col
 */
int
sheet_col_get_distance (Sheet *sheet, int from_col, int to_col)
{
	ColInfo *ci;
	int pixels = 0, i;

	g_assert (from_col <= to_col);
	
	for (i = from_col; i < to_col; i++){
		ci = sheet_get_col_info (sheet, i);
		pixels += ci->width;
	}
	return pixels;
}

/*
 * Return the number of pixels between from_row to to_row
 */
int
sheet_row_get_distance (Sheet *sheet, int from_row, int to_row)
{
	RowInfo *ri;
	int pixels = 0, i;

	g_assert (from_row <= to_row);
	
	for (i = from_row; i < to_row; i++){
		ri = sheet_get_row_info (sheet, i);
		pixels += ri->height;
	}
	return pixels;
}
