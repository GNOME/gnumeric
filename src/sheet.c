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

