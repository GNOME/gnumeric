#ifndef GNUMERIC_SHEET_OBJECT_H
#define GNUMERIC_SHEET_OBJECT_H

#include "sheet.h"
#include "sheet-view.h"
#include "sheet-object.h"

/*
 * SheetObject
 *
 * General purpose Sheet graphic object
 */
#define SHEET_OBJECT_TYPE     (sheet_object_get_type ())
#define SHEET_OBJECT(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_TYPE, SheetObject))
#define SHEET_OBJECT_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_TYPE, SheetObjectClass))
#define IS_SHEET_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_TYPE))

typedef enum {
	SHEET_OBJECT_ACTION_STATIC,
	SHEET_OBJECT_ACTION_CAN_PRESS
} SheetObjectAction;

typedef struct {
	GtkObject          parent_object;
	SheetObjectAction  type;
	Sheet             *sheet;
	GList             *realized_list;
	gboolean           dragging;

	/* Private data */
	GnomeCanvasPoints *bbox_points; /* use _get / _set_bounds */
} SheetObject;

typedef struct {
	GtkObjectClass parent_class;

	/* Virtual methods */
	GnomeCanvasItem *(*realize) (SheetObject *sheet_object,
				     SheetView   *sheet_view);
	void       (*update_bounds) (SheetObject *sheet_object);
	void         (*start_popup) (SheetObject *sheet_object,
				     GtkMenu     *menu);
	void           (*end_popup) (SheetObject *sheet_object,
				     GtkMenu     *menu);
} SheetObjectClass;

GtkType sheet_object_get_type      (void);
void    sheet_object_construct     (SheetObject *sheet_object, Sheet *sheet);
void    sheet_object_drop_file     (Sheet *sheet, gdouble x, gdouble y,
				    const char *fname);
int     sheet_object_canvas_event  (GnomeCanvasItem *item, GdkEvent *event,
				    SheetObject *so);
void    sheet_object_widget_handle (SheetObject *so, GtkWidget *widget,
				    GnomeCanvasItem *item);

/* b = bottom, t = top, l = left, r = right */
void    sheet_object_get_bounds (SheetObject *sheet_object, double *tlx, double *tly,
				 double *brx, double *bry);
void    sheet_object_set_bounds (SheetObject *sheet_object, double tlx, double tly,
				 double brx, double bry);

/*
 * Sheet modes
 */
typedef enum {
	SHEET_OBJECT_LINE,
	SHEET_OBJECT_BOX,
	SHEET_OBJECT_OVAL,
	SHEET_OBJECT_ARROW,
	SHEET_OBJECT_GRAPHIC,
	SHEET_OBJECT_BUTTON,
	SHEET_OBJECT_CHECKBOX
} SheetObjectType;

/*
 * This routine creates the SheetObject in the SheetViews's Canvases.
 */
void             sheet_object_realize        (SheetObject *object);

void             sheet_object_unrealize      (SheetObject *object);

void             sheet_object_make_current   (SheetObject *object);

SheetObject     *sheet_object_create_line    (Sheet *sheet,   int is_arrow,
					      double x1,      double y1,
					      double x2,      double y2,
					      const char    *color, int width);

SheetObject     *sheet_object_create_filled  (Sheet *sheet, int type,
					      double x1, double y1,
					      double x2, double y2,
					      const char *fill_color,
					      const char *outline_color,
					      int w);

SheetObject     *sheet_object_create_button  (Sheet *sheet,
					      double x1, double y1,
					      double x2, double y2);

SheetObject     *sheet_object_create_checkbox(Sheet *sheet,
					      double x1, double y1,
					      double x2, double y2);

#endif /* GNUMERIC_SHEET_OBJECT_H */
