/*
 * Gnome Basic Excel Range object.
 *
 * Author:
 *   Michael Meeks (michael@ximian.com)
 */

#ifndef EXCEL_GB_RANGE_H
#define EXCEL_GB_RANGE_H

#include <gbrun/libgbrun.h>

#define EXCEL_TYPE_GB_RANGE            (excel_gb_range_get_type ())
#define EXCEL_GB_RANGE(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_RANGE, ExcelGBRange))
#define EXCEL_GB_RANGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_RANGE, ExcelGBRangeClass))
#define EXCEL_IS_GB_RANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_RANGE))
#define EXCEL_IS_GB_RANGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_RANGE))

typedef struct {
	GBRunObject object;

	Sheet      *sheet;
	Range       range;
} ExcelGBRange;

typedef struct {
	GBRunObjectClass klass;
} ExcelGBRangeClass;

GtkType       excel_gb_range_get_type (void);

ExcelGBRange *excel_gb_range_new      (GBRunEvalContext *ec,
				       Sheet            *sheet,
				       Range             range);

ExcelGBRange *excel_gb_range_new_ref  (GBRunEvalContext *ec,
				       Sheet            *sheet,
				       const char       *range);

#endif /* EXCEL_GB_RANGE_H */
