/*
 * Gnome Basic Excel Interior object.
 *
 * Author:
 *   Thomas Meeks
 */

#ifndef EXCEL_GB_INTERIOR_H
#define EXCEL_GB_INTERIOR_H

#include <gbrun/libgbrun.h>

#define EXCEL_TYPE_GB_INTERIOR            (excel_gb_interior_get_type ())
#define EXCEL_GB_INTERIOR(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_INTERIOR, ExcelGBInterior))
#define EXCEL_GB_INTERIOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_INTERIOR, ExcelGBInteriorClass))
#define EXCEL_IS_GB_INTERIOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_INTERIOR))
#define EXCEL_IS_GB_INTERIOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_INTERIOR))

typedef struct {
	GBRunObject object;

	Sheet      *sheet;
	Range       range;
} ExcelGBInterior;

typedef struct {
	GBRunObjectClass klass;
} ExcelGBInteriorClass;

GtkType          excel_gb_interior_get_type (void);
ExcelGBInterior *excel_gb_interior_new      (Sheet *sheet, Range range);

#endif /* EXCEL_GB_INTERIOR_H */
