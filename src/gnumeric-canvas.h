#ifndef GNM_CANVAS_H
#define GNM_CANVAS_H

#include "gnumeric-simple-canvas.h"

#define GNUMERIC_CANVAS_TYPE     (gnumeric_canvas_get_type ())
#define GNUMERIC_CANVAS(obj)     (GTK_CHECK_CAST((obj), GNUMERIC_CANVAS_TYPE, GnumericCanvas))
#define GNUMERIC_CANVAS_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), GNUMERIC_CANVAS_TYPE))
#define IS_GNUMERIC_CANVAS(o)    (GTK_CHECK_TYPE((o), GNUMERIC_CANVAS_TYPE))

#define GNUMERIC_CANVAS_FACTOR_X 1000000
#define GNUMERIC_CANVAS_FACTOR_Y 4000000

struct _GnumericCanvas {
	GnmSimpleCanvas simple;

	GnumericPane *pane;

	CellPos first, last_full, last_visible, first_offset;

	GnomeCanvasGroup *anted_group;
	GnomeCanvasGroup *object_group;

	/* Input context for dead key support */
	GdkIC     *ic;
	GdkICAttr *ic_attr;

	/* Sliding scroll */
	GnumericCanvasSlideHandler slide_handler;
	gpointer   slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_x, sliding_y;
	int        sliding_dx, sliding_dy;
	gboolean   sliding_adjacent_h, sliding_adjacent_v;
};

GtkType        gnumeric_canvas_get_type (void);
GnumericCanvas *gnumeric_canvas_new      (SheetControlGUI *scg, GnumericPane *pane);

int gnm_canvas_find_col (GnumericCanvas *gsheet, int x, int *col_origin);
int gnm_canvas_find_row (GnumericCanvas *gsheet, int y, int *row_origin);

void gnm_canvas_compute_visible_region	(GnumericCanvas *gsheet,
					 gboolean const full_recompute);
void gnm_canvas_redraw_region		(GnumericCanvas *gsheet,
					 int start_col, int start_row,
					 int end_col, int end_row);

typedef enum {
	GNM_SLIDE_X = 1,
	GNM_SLIDE_Y = 2,
	GNM_SLIDE_EXTERIOR_ONLY = 4,
	GNM_SLIDE_AT_COLROW_BOUND = 8, /* not implemented */
} GnumericSlideFlags;
void	 gnm_canvas_slide_stop	(GnumericCanvas *gsheet);
void	 gnm_canvas_slide_init	(GnumericCanvas *gsheet);
gboolean gnm_canvas_handle_motion	(GnumericCanvas *gsheet,
					 GnomeCanvas *canvas,
					 GdkEventMotion *event,
					 GnumericSlideFlags slide_flags,
					 GnumericCanvasSlideHandler callback,
					 gpointer user_data);

#endif /* GNM_CANVAS_H */
