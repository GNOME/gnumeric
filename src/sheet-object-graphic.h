#ifndef GNUMERIC_SHEET_OBJECT_GRAPHIC_H
#define GNUMERIC_SHEET_OBJECT_GRAPHIC_H

#include "sheet-object.h"
#include <pango/pango-attributes.h>

#define SHEET_OBJECT_GRAPHIC_TYPE  (sheet_object_graphic_get_type ())
GType sheet_object_graphic_get_type (void);
SheetObject *sheet_object_line_new    (gboolean with_arrow);
void         gnm_so_graphic_set_fill_color (SheetObject *so, GnmColor *color);
void	     gnm_so_graphic_set_width	   (SheetObject *so, double width);

#define SHEET_OBJECT_FILLED_TYPE  (sheet_object_filled_get_type ())
#define IS_SHEET_OBJECT_FILLED(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_FILLED_TYPE))

GType sheet_object_filled_get_type (void);
SheetObject *sheet_object_box_new            (gboolean is_oval);
void gnm_so_filled_set_outline_color (SheetObject *so, GnmColor *color);
void gnm_so_filled_set_outline_style (SheetObject *so, int style);

#define SHEET_OBJECT_POLYGON_TYPE  (sheet_object_polygon_get_type ())
#define IS_SHEET_OBJECT_POLYGON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_POLYGON_TYPE))

GType sheet_object_polygon_get_type (void);
void  gnm_so_polygon_set_points	       (SheetObject *so, GArray *pairs);
void  gnm_so_polygon_set_fill_color    (SheetObject *so, GnmColor *color);
void  gnm_so_polygon_set_outline_color (SheetObject *so, GnmColor *color);

#define SHEET_OBJECT_TEXT_TYPE     (sheet_object_text_get_type ())
#define SHEET_OBJECT_TEXT(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_OBJECT_TEXT_TYPE, SheetObjectText))

GType sheet_object_text_get_type (void);
void  gnm_so_text_set_text	 (SheetObject *so, char const *str);

#endif /* GNUMERIC_SHEET_OBJECT_GRAPHIC_H */
