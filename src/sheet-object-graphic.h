#ifndef GNUMERIC_SHEET_OBJECT_GRAPHIC_H
#define GNUMERIC_SHEET_OBJECT_GRAPHIC_H

#include "sheet-object.h"

/*
 * SheetObjectGraphic:
 *
 * Sample graphic objects
 */
#define SHEET_OBJECT_GRAPHIC_TYPE     (sheet_object_graphic_get_type ())
#define SHEET_OBJECT_GRAPHIC(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_GRAPHIC_TYPE, SheetObjectGraphic))
#define SHEET_OBJECT_GRAPHIC_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPHIC_TYPE))
#define IS_SHEET_GRAPHIC_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_GRAPHIC_TYPE))

typedef struct {
	SheetObject     parent_object;
	SheetObjectType type;

	String            *color;
	int               width;
} SheetObjectGraphic;

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectGraphicClass;

GtkType sheet_object_graphic_get_type (void);

/*
 * SheetObjectFilled
 *
 * Derivative of SheetObjectGraphic, with filled parameter
 */
#define SHEET_OBJECT_FILLED_TYPE     (sheet_object_filled_get_type ())
#define SHEET_OBJECT_FILLED(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilled))
#define SHEET_OBJECT_FILLED_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_FILLED_TYPE, SheetObjectFilledClass))
#define IS_SHEET_FILLED_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_FILLED_TYPE))

typedef struct {
	SheetObjectGraphic parent_object;

	String      *fill_color;
	int         pattern;
} SheetObjectFilled;

typedef struct {
	SheetObjectGraphicClass parent_class;
} SheetObjectFilledClass;

GtkType sheet_object_filled_get_type (void);



SheetObject *sheet_object_create_line (Sheet *sheet, int is_arrow,
				       double x1, double y1,
				       double x2, double y2,
				       const char *color, int w);

#endif
