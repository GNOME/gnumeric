/*
 * Gnome Basic Excel Worksheet functions.
 *
 * Author:
 *   Michael Meeks (michael@ximian.com)
 */

#ifndef EXCEL_GB_WORKSHEET_FUNCTION_H
#define EXCEL_GB_WORKSHEET_FUNCTION_H

#include <gbrun/libgbrun.h>

#define EXCEL_TYPE_GB_WORKSHEET_FUNCTION            (excel_gb_worksheet_function_get_type ())
#define EXCEL_GB_WORKSHEET_FUNCTION(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_WORKSHEET_FUNCTION, ExcelGBWorksheetFunction))
#define EXCEL_GB_WORKSHEET_FUNCTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_WORKSHEET_FUNCTION, ExcelGBWorksheetFunctionClass))
#define EXCEL_IS_GB_WORKSHEET_FUNCTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_WORKSHEET_FUNCTION))
#define EXCEL_IS_GB_WORKSHEET_FUNCTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_WORKSHEET_FUNCTION))

typedef struct {
	GBRunObject object;

	Sheet      *sheet;
} ExcelGBWorksheetFunction;

typedef struct {
	GBRunObjectClass klass;
} ExcelGBWorksheetFunctionClass;

GtkType                   excel_gb_worksheet_function_get_type (void);
ExcelGBWorksheetFunction *excel_gb_worksheet_function_new      (Sheet *sheet);

#endif /* EXCEL_GB_WORKSHEET_FUNCTION_H */
