#ifndef GNM_CANVAS_H
#define GNM_CANVAS_H

#include "gnumeric-simple-canvas.h"
#include <gtk/gtkimmulticontext.h>

#define GNM_CANVAS_TYPE	 (gnm_canvas_get_type ())
#define GNM_CANVAS(o)	 (G_TYPE_CHECK_INSTANCE_CAST((o), GNM_CANVAS_TYPE, GnmCanvas))
#define GNM_IS_CANVAS(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_CANVAS_TYPE))

#define GNUMERIC_CANVAS_FACTOR_X 1000000
#define GNUMERIC_CANVAS_FACTOR_Y 4000000

typedef gboolean (*GnmCanvasSlideHandler) (GnmCanvas *gcanvas,
						int col, int row,
						gpointer user_data);

struct _GnmCanvas {
	GnmSimpleCanvas simple;

	GnumericPane *pane;

	CellPos first, last_full, last_visible, first_offset;

	FooCanvasGroup *anted_group;
	FooCanvasGroup *object_group;
	FooCanvasGroup *sheet_object_group;

	/* Sliding scroll */
	GnmCanvasSlideHandler slide_handler;
	gpointer   slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_x, sliding_y;
	int        sliding_dx, sliding_dy;
	gboolean   sliding_adjacent_h, sliding_adjacent_v;

	/*  IM */
	guint      need_im_reset :1;
	guint      mask_state;
	guint      preedit_length;
	GtkIMContext  *im_context;
	PangoAttrList *preedit_attrs;
};

GType gnm_canvas_get_type (void);
GnmCanvas *gnm_canvas_new (SheetControlGUI *scg, GnumericPane *pane);

int  gnm_canvas_find_col (GnmCanvas *gsheet, int x, int *col_origin);
int  gnm_canvas_find_row (GnmCanvas *gsheet, int y, int *row_origin);
void gnm_canvas_redraw_range (GnmCanvas *gsheet, Range const *r);
void gnm_canvas_compute_visible_region (GnmCanvas *gsheet,
					gboolean const full_recompute);

typedef enum {
	GNM_CANVAS_SLIDE_X = 1,
	GNM_CANVAS_SLIDE_Y = 2,
	GNM_CANVAS_SLIDE_EXTERIOR_ONLY = 4,
	GNM_CANVAS_SLIDE_AT_COLROW_BOUND = 8 /* not implemented */
} GnmCanvasSlideFlags;

void	 gnm_canvas_slide_stop	  (GnmCanvas *gsheet);
void	 gnm_canvas_slide_init	  (GnmCanvas *gsheet);
gboolean gnm_canvas_handle_motion (GnmCanvas *gsheet,
				   FooCanvas    *canvas,
				   GdkEventMotion *event,
				   GnmCanvasSlideFlags	 slide_flags,
				   GnmCanvasSlideHandler handler,
				   gpointer user_data);

#endif /* GNM_CANVAS_H */
