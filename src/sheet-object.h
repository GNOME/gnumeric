#ifndef GNUMERIC_SHEET_OBJECT_H
#define GNUMERIC_SHEET_OBJECT_H

/*
 * SheetObject
 *
 * General purpose Sheet graphic object
 */
#define SHEET_OBJECT_TYPE     (sheet_object_get_type ())
#define SHEET_OBJECT(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_TYPE, SheetObject))
#define SHEET_OBJECT_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_TYPE, SheetObjectClass))
#define IS_SHEET_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_TYPE))

typedef struct {
	GtkObject  parent_object;
	Sheet     *sheet;
	GList     *realized_list;
	int        dragging;

	/* Bounding box */
	GnomeCanvasPoints *bbox_points;
} SheetObject;

typedef struct {
	GtkObjectClass parent_class;

	/* Virtual methods */
	GnomeCanvasItem *(*realize) (SheetObject *sheet_object, SheetView *sheet_view);
	void             (*update)  (SheetObject *sheet_object, gdouble x, gdouble y);
	void       (*update_coords) (SheetObject *sheet_object,
				     gdouble x1delta, gdouble y1delta,
				     gdouble x2delta, gdouble y2delta);
	void   (*creation_finished) (SheetObject *sheet_object);
} SheetObjectClass;

GtkType sheet_object_get_type  (void);
void    sheet_object_construct (SheetObject *sheet_object, Sheet *sheet);

/*
 * Sheet modes
 */
typedef enum {
	SHEET_OBJECT_LINE,
	SHEET_OBJECT_RECTANGLE,
	SHEET_OBJECT_ELLIPSE,
	SHEET_OBJECT_ARROW,
} SheetObjectType;

/*
 * This routine creates the SheetObject in the SheetViews's Canvases.
 */
void             sheet_object_realize        (SheetObject *object);

void             sheet_object_unrealize      (SheetObject *object);

void             sheet_object_make_current   (Sheet *sheet,
					      SheetObject *object);

/* Registers the object in the Sheet, otherwise we cant keep track of it */
void             sheet_object_register       (Sheet *sheet,
					      SheetObject *object);

SheetObject     *sheet_object_create_line    (Sheet *sheet,   int is_arrow,
					      double x1,      double y1,
					      double x2,      double y2,
					      char    *color, int width);

SheetObject     *sheet_object_create_filled  (Sheet *sheet, int type,
					      double x1, double y1,
					      double x2, double y2,
					      char *fill_color, char *outline_color,
					      int w);

#endif /* GNUMERIC_SHEET_OBJECT_H */

