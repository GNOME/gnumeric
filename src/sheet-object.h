#ifndef GNUMERIC_SHEET_OBJECT_H
#define GNUMERIC_SHEET_OBJECT_H

#ifdef ENABLE_BONOBO
#       include <bonobo/bonobo-print-client.h>
#endif

#include "sheet.h"
#include "sheet-view.h"
#include "gnumeric-sheet.h"

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

struct _SheetObject {
	GtkObject          parent_object;
	SheetObjectAction  type;
	Sheet             *sheet;
	GList             *realized_list;
	gboolean           dragging;

	/* Private data */
	GnomeCanvasPoints *bbox_points; /* use _get / _set_bounds */
};

typedef struct {
	SheetObject     *so;
#ifdef ENABLE_BONOBO
	BonoboPrintData *pd;
#endif
	double           x_pos_pts;
	double           y_pos_pts;

	double           scale_x;
	double           scale_y;
} SheetObjectPrintInfo;

typedef struct {
	GtkObjectClass parent_class;

	/* Virtual methods */
	GnomeCanvasItem *(*new_view) (SheetObject *sheet_object,
				      SheetView   *sheet_view);
	void        (*update_bounds) (SheetObject *sheet_object);
	void        (*populate_menu) (SheetObject *sheet_object,
				      GtkMenu     *menu);
	void                (*print) (SheetObject *so, SheetObjectPrintInfo *pi);
	GtkWidget *   (*user_config) (SheetObject *, SheetView *);
	void           (*set_active) (SheetObject *so, gboolean val);
} SheetObjectClass;

GtkType sheet_object_get_type      (void);
void    sheet_object_construct     (SheetObject *sheet_object, Sheet *sheet);
int     sheet_object_canvas_event  (GnomeCanvasItem *item, GdkEvent *event,
				    SheetObject *so);
void    sheet_object_print         (SheetObject *so, SheetObjectPrintInfo *pi);

/* b = bottom, t = top, l = left, r = right */
void    sheet_object_get_bounds (SheetObject *sheet_object, double *tlx, double *tly,
				 double *brx, double *bry);
void    sheet_object_set_bounds (SheetObject *sheet_object, double tlx, double tly,
				 double brx, double bry);

/*
 * FIXME : Totally broken.
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

int 		 sheet_object_begin_creation (GnumericSheet *gsheet,
					      GdkEventButton *event);

#endif /* GNUMERIC_SHEET_OBJECT_H */
