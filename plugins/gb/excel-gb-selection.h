/*
 * Gnome Basic Excel Selection object.
 *
 * Author:
 *   Michael Meeks (michael@helixcode.com)
 */

#ifndef EXCEL_GB_SELECTION_H
#define EXCEL_GB_SELECTION_H

#include <gbrun/libgbrun.h>

#define EXCEL_TYPE_GB_SELECTION            (excel_gb_selection_get_type ())
#define EXCEL_GB_SELECTION(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_SELECTION, ExcelGBSelection))
#define EXCEL_GB_SELECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_SELECTION, ExcelGBSelectionClass))
#define EXCEL_IS_GB_SELECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_SELECTION))
#define EXCEL_IS_GB_SELECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_SELECTION))

typedef struct {
	GBRunObject object;

	Sheet      *sheet;
} ExcelGBSelection;

typedef struct {
	GBRunObjectClass klass;
} ExcelGBSelectionClass;

GtkType           excel_gb_selection_get_type       (void);
ExcelGBSelection *excel_gb_selection_new            (Sheet *sheet);

#endif /* EXCEL_GB_SELECTION_H */
