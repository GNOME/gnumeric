#ifndef _GNM_SIMPLE_CANVAS_H_
# define _GNM_SIMPLE_CANVAS_H_

#include <gnumeric-fwd.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

typedef struct {
	GocCanvas   canvas;
	SheetControlGUI *scg;
} GnmSimpleCanvas;

typedef struct {
	GocCanvasClass   canvas;
} GnmSimpleCanvasClass;

#define GNM_SIMPLE_CANVAS_TYPE     (gnm_simple_canvas_get_type ())
#define GNM_SIMPLE_CANVAS(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SIMPLE_CANVAS_TYPE, GnmSimpleCanvas))

GType		 gnm_simple_canvas_get_type (void);
GocCanvas	*gnm_simple_canvas_new      (SheetControlGUI *scg);

void gnm_simple_canvas_ungrab (GocItem *item);
void gnm_simple_canvas_grab   (GocItem *item);

G_END_DECLS

#endif /* _GNM_SIMPLE_CANVAS_H_ */
