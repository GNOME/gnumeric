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

