#ifndef GNUMERIC_SHEET_OBJECT_GRAPHIC_H
#define GNUMERIC_SHEET_OBJECT_GRAPHIC_H

#include "sheet-object.h"

SheetObject *sheet_object_line_new  (gboolean with_arrow);
SheetObject *sheet_object_box_new   (gboolean is_oval);

GtkType sheet_object_graphic_get_type (void);
#define SHEET_OBJECT_GRAPHIC_TYPE     (sheet_object_graphic_get_type ())
#define SHEET_OBJECT_GRAPHIC(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphic))
#define IS_SHEET_OBJECT_GRAPHIC(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_GRAPHIC_TYPE))

GtkType sheet_object_filled_get_type (void);
#define SHEET_OBJECT_FILLED_TYPE     (sheet_object_filled_get_type ())
#define SHEET_OBJECT_FILLED(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilled))
#define IS_SHEET_OBJECT_FILLED(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_FILLED_TYPE))

#endif /* GNUMERIC_SHEET_OBJECT_GRAPHIC_H */
