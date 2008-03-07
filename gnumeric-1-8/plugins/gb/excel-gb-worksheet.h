/*
 * Gnome Basic Excel Worksheet object.
 *
 * Author:
 *   Michael Meeks (michael@ximian.com)
 */

#ifndef EXCEL_GB_WORKSHEET_H
#define EXCEL_GB_WORKSHEET_H

#include <gbrun/libgbrun.h>

#define EXCEL_TYPE_GB_WORKSHEET            (excel_gb_worksheet_get_type ())
#define EXCEL_GB_WORKSHEET(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_WORKSHEET, ExcelGBWorksheet))
#define EXCEL_GB_WORKSHEET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_WORKSHEET, ExcelGBWorksheetClass))
#define EXCEL_IS_GB_WORKSHEET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_WORKSHEET))
#define EXCEL_IS_GB_WORKSHEET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_WORKSHEET))

typedef struct {
	GBRunObject object;

	Sheet      *sheet;
} ExcelGBWorksheet;

typedef struct {
	GBRunObjectClass klass;
} ExcelGBWorksheetClass;

GtkType           excel_gb_worksheet_get_type       (void);
ExcelGBWorksheet *excel_gb_worksheet_new            (Sheet *sheet);

#endif /* EXCEL_GB_WORKSHEET_H */
