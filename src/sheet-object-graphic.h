#ifndef GNUMERIC_SHEET_OBJECT_GRAPHIC_H
#define GNUMERIC_SHEET_OBJECT_GRAPHIC_H

#include "sheet-object.h"

#define SHEET_OBJECT_GRAPHIC_TYPE     (sheet_object_graphic_get_type ())
#define SHEET_OBJECT_GRAPHIC(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphic))
#define IS_SHEET_OBJECT_GRAPHIC(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_GRAPHIC_TYPE))

GtkType	     sheet_object_graphic_get_type (void);
SheetObject *sheet_object_line_new  (gboolean with_arrow);
void         sheet_object_graphic_fill_color_set (SheetObject *so,
						  StyleColor *color);

#define SHEET_OBJECT_FILLED_TYPE     (sheet_object_filled_get_type ())
#define SHEET_OBJECT_FILLED(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilled))
#define IS_SHEET_OBJECT_FILLED(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_FILLED_TYPE))

GtkType      sheet_object_filled_get_type (void);
SheetObject *sheet_object_box_new   (gboolean is_oval);
void	     sheet_object_filled_outline_color_set (SheetObject *so,
						    StyleColor *color);

#endif /* GNUMERIC_SHEET_OBJECT_GRAPHIC_H */
