#ifndef SHEET_OBJECT_H
#define SHEET_OBJECT_H

typedef enum {
	SHEET_OBJECT_LINE,
	SHEET_OBJECT_RECTANGLE,
	SHEET_OBJECT_ELLIPSE,
	SHEET_OBJECT_ARROW,
} SheetObjectType;

typedef struct {
	int               signature;
	Sheet             *sheet;
	GList             *realized_list;
	SheetObjectType   type;
	int               dragging;

	String            *color;
	int               width;
	GnomeCanvasPoints *points;
} SheetObject;

typedef struct {
	SheetObject sheet_object;

	String      *fill_color;
	int         pattern;
} SheetFilledObject;

#define SHEET_OBJECT_SIGNATURE (('S' << 24) | ('O' << 16) | ('b' << 8) | 'e')
#define IS_SHEET_OBJECT(x) (x->signature == SHEET_OBJECT_SIGNATURE)

SheetObject *sheet_object_create_line        (Sheet *sheet,   int is_arrow,
					      double x1,      double y1,
					      double x2,      double y2,
					      char    *color, int width);

SheetObject *sheet_object_create_filled      (Sheet *sheet, int type,
					      double x1, double y1,
					      double x2, double y2,
					      char *fill_color, char *outline_color,
					      int w);

void             sheet_object_destroy        (SheetObject *object);

/*
 * This routine creates the SheetObject in the SheetViews's Canvases.
 */
void             sheet_object_realize        (Sheet *sheet,
					      SheetObject *object);

void             sheet_object_unrealize      (Sheet *sheet,
					      SheetObject *object);

void             sheet_object_make_current   (Sheet *sheet,
					      SheetObject *object);

/* Registers the object in the Sheet, otherwise we cant keep track of it */
void             sheet_object_register       (Sheet *sheet,
					      SheetObject *object);

#endif

