#ifndef GNM_SIMPLE_CANVAS_H
#define GNM_SIMPLE_CANVAS_H

#include "gui-gnumeric.h"

typedef struct {
	GnomeCanvas   canvas;
	SheetControlGUI *scg;
} GnmSimpleCanvas;

typedef struct {
	GnomeCanvasClass   canvas;
} GnmSimpleCanvasClass;

#define GNM_SIMPLE_CANVAS_TYPE     (gnm_simple_canvas_get_type ())
#define GNM_SIMPLE_CANVAS(obj)     (GTK_CHECK_CAST((obj), GNM_SIMPLE_CANVAS_TYPE, GnmSimpleCanvas))

GtkType	     gnm_simple_canvas_get_type (void);
GnomeCanvas *gnm_simple_canvas_new      (SheetControlGUI *scg);

void gnm_simple_canvas_ungrab (GnomeCanvasItem *item, guint32 etime);
int  gnm_simple_canvas_grab   (GnomeCanvasItem *item, unsigned int event_mask,
			       GdkCursor *cursor, guint32 etime);

#endif /* GNM_SIMPLE_CANVAS_H */
