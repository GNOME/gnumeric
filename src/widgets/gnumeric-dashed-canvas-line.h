#ifndef GNUMERIC_DASHED_CANVAS_LINE_H
#define GNUMERIC_DASHED_CANVAS_LINE_H

/* dashed Line item for the canvas */
#include <libgnomeui/libgnomeui.h>
#include "style-border.h"

#define GNUMERIC_TYPE_DASHED_CANVAS_LINE\
    (gnumeric_dashed_canvas_line_get_type ())
#define GNUMERIC_DASHED_CANVAS_LINE(obj)\
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNUMERIC_TYPE_DASHED_CANVAS_LINE, GnumericDashedCanvasLine))
#define GNUMERIC_DASHED_CANVAS_LINE_CLASS(klass)\
    (G_TYPE_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_DASHED_CANVAS_LINE, GnumericDashedCanvasLineClass))
#define GNUMERIC_IS_DASHED_CANVAS_LINE(obj)\
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNUMERIC_TYPE_DASHED_CANVAS_LINE))
#define GNUMERIC_IS_DASHED_CANVAS_LINE_CLASS(klass)\
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_DASHED_CANVAS_LINE))


typedef struct _GnumericDashedCanvasLine GnumericDashedCanvasLine;
typedef struct _GnumericDashedCanvasLineClass GnumericDashedCanvasLineClass;

struct _GnumericDashedCanvasLine {
	GnomeCanvasLine line;

	/* Public : */
	StyleBorderType dash_style_index;
};

struct _GnumericDashedCanvasLineClass {
	GnomeCanvasLineClass parent_class;
	void (*real_draw)(GnomeCanvasItem *item, GdkDrawable *drawable,
			  int x, int y, int width, int height);
};

void    gnumeric_dashed_canvas_line_set_dash_index (GnumericDashedCanvasLine *line,
						    StyleBorderType const indx);

/* Standard Gtk function */
GType gnumeric_dashed_canvas_line_get_type (void);

#endif
