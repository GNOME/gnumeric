/*
 * Gnome Basic Excel Worksheets object.
 *
 * Author:
 *   Michael Meeks (michael@helixcode.com)
 */

#ifndef EXCEL_GB_WORKSHEETS_H
#define EXCEL_GB_WORKSHEETS_H

#include <gbrun/libgbrun.h>
#include <gbrun/gbrun-collection.h>

#define EXCEL_TYPE_GB_WORKSHEETS            (excel_gb_worksheets_get_type ())
#define EXCEL_GB_WORKSHEETS(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_WORKSHEETS, ExcelGBWorksheets))
#define EXCEL_GB_WORKSHEETS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_WORKSHEETS, ExcelGBWorksheetsClass))
#define EXCEL_IS_GB_WORKSHEETS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_WORKSHEETS))
#define EXCEL_IS_GB_WORKSHEETS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_WORKSHEETS))

typedef struct {
	GBRunCollection collection;

	Workbook       *wb;
} ExcelGBWorksheets;

typedef struct {
	GBRunCollectionClass klass;
} ExcelGBWorksheetsClass;

GtkType            excel_gb_worksheets_get_type (void);
ExcelGBWorksheets *excel_gb_worksheets_new      (Workbook *wb);

#endif /* EXCEL_GB_WORKSHEETS_H */
