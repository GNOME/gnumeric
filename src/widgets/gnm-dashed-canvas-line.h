#ifndef GNM_DASHED_CANVAS_LINE_H
#define GNM_DASHED_CANVAS_LINE_H

/* dashed Line item for the canvas */
#include <goffice/goffice.h>
#include <style-border.h>

#define GNM_DASHED_CANVAS_LINE_TYPE\
    (gnm_dashed_canvas_line_get_type ())
#define GNM_DASHED_CANVAS_LINE(obj)\
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_DASHED_CANVAS_LINE_TYPE, GnmDashedCanvasLine))
#define GNM_IS_DASHED_CANVAS_LINE(obj)\
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_DASHED_CANVAS_LINE_TYPE))


typedef struct GnmDashedCanvasLine_ GnmDashedCanvasLine;

typedef struct {
	GocLineClass parent_class;
} GnmDashedCanvasLineClass;

struct GnmDashedCanvasLine_ {
	GocLine line;

	/* Public : */
	GnmStyleBorderType dash_style_index;
};

void    gnm_dashed_canvas_line_set_dash_index (GnmDashedCanvasLine *line,
					       GnmStyleBorderType const indx);

/* Standard Gtk function */
GType gnm_dashed_canvas_line_get_type (void);

#endif
