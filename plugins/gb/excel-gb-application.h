/*
 * Gnome Basic Excel Application object.
 *
 * Author:
 *   Michael Meeks (michael@helixcode.com)
 */

#ifndef EXCEL_GB_APPLICATION_H
#define EXCEL_GB_APPLICATION_H

#include <gbrun/libgbrun.h>

#define EXCEL_TYPE_GB_APPLICATION            (excel_gb_application_get_type ())
#define EXCEL_GB_APPLICATION(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_APPLICATION, ExcelGBApplication))
#define EXCEL_GB_APPLICATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_APPLICATION, ExcelGBApplicationClass))
#define EXCEL_IS_GB_APPLICATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_APPLICATION))
#define EXCEL_IS_GB_APPLICATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_APPLICATION))

typedef struct {
	GBRunObject object;

	Workbook   *wb;
} ExcelGBApplication;

typedef struct {
	GBRunObjectClass klass;
} ExcelGBApplicationClass;

GtkType             excel_gb_application_get_type       (void);
ExcelGBApplication *excel_gb_application_new            (Workbook *wb);
void                excel_gb_application_register_types (void);

#endif /* EXCEL_GB_APPLICATION_H */
