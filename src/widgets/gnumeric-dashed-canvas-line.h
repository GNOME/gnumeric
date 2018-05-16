#ifndef GNUMERIC_DASHED_CANVAS_LINE_H
#define GNUMERIC_DASHED_CANVAS_LINE_H

/* dashed Line item for the canvas */
#include <goffice/goffice.h>
#include <style-border.h>

#define GNM_DASHED_CANVAS_LINE_TYPE\
    (gnumeric_dashed_canvas_line_get_type ())
#define GNM_DASHED_CANVAS_LINE(obj)\
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_DASHED_CANVAS_LINE_TYPE, GnumericDashedCanvasLine))
#define GNM_IS_DASHED_CANVAS_LINE(obj)\
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_DASHED_CANVAS_LINE_TYPE))


typedef struct _GnumericDashedCanvasLine GnumericDashedCanvasLine;
typedef GocLineClass GnumericDashedCanvasLineClass;

struct _GnumericDashedCanvasLine {
	GocLine line;

	/* Public : */
	GnmStyleBorderType dash_style_index;
};

void    gnumeric_dashed_canvas_line_set_dash_index (GnumericDashedCanvasLine *line,
						    GnmStyleBorderType const indx);

/* Standard Gtk function */
GType gnumeric_dashed_canvas_line_get_type (void);

#endif
