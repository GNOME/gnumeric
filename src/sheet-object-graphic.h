#ifndef GNUMERIC_SHEET_OBJECT_GRAPHIC_H
#define GNUMERIC_SHEET_OBJECT_GRAPHIC_H

#include "sheet-object.h"

#define SHEET_OBJECT_GRAPHIC_TYPE  (sheet_object_graphic_get_type ())
#define IS_SHEET_OBJECT_GRAPHIC(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_GRAPHIC_TYPE))

GType	     sheet_object_graphic_get_type (void);
SheetObject *sheet_object_line_new  (gboolean with_arrow);
void         sheet_object_graphic_fill_color_set (SheetObject *so,
						  StyleColor *color);

#define SHEET_OBJECT_FILLED_TYPE  (sheet_object_filled_get_type ())
#define IS_SHEET_OBJECT_FILLED(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_FILLED_TYPE))

GType      sheet_object_filled_get_type (void);
SheetObject *sheet_object_box_new   (gboolean is_oval);
void	     sheet_object_filled_outline_color_set (SheetObject *so,
						    StyleColor *color);

#endif /* GNUMERIC_SHEET_OBJECT_GRAPHIC_H */
