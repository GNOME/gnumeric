#ifndef GNM_CANVAS_H
#define GNM_CANVAS_H

#include <gtk/gtkimmulticontext.h>
#include "gnumeric-simple-canvas.h"

#define GNUMERIC_CANVAS_TYPE     (gnumeric_canvas_get_type ())
#define GNUMERIC_CANVAS(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNUMERIC_CANVAS_TYPE, GnumericCanvas))
#define GNUMERIC_CANVAS_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNUMERIC_CANVAS_TYPE))
#define IS_GNUMERIC_CANVAS(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), GNUMERIC_CANVAS_TYPE))

#define GNUMERIC_CANVAS_FACTOR_X 1000000
#define GNUMERIC_CANVAS_FACTOR_Y 4000000

typedef gboolean (*GnumericCanvasSlideHandler) (GnumericCanvas *gcanvas,
						int col, int row,
						gpointer user_data);

struct _GnumericCanvas {
	GnmSimpleCanvas simple;

	GnumericPane *pane;

	CellPos first, last_full, last_visible, first_offset;

	GnomeCanvasGroup *anted_group;
	GnomeCanvasGroup *object_group;
	GnomeCanvasGroup *sheet_object_group;

#if 0
	/* Input context for dead key support */
	GdkIC     *ic;
	GdkICAttr *ic_attr;
#endif

	/* Sliding scroll */
	GnumericCanvasSlideHandler slide_handler;
	gpointer   slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_x, sliding_y;
	int        sliding_dx, sliding_dy;
	gboolean   sliding_adjacent_h, sliding_adjacent_v;

	/*  IM */
	guint      need_im_reset :1;
	guint      mask_state;
	GtkIMContext *im_context;
	guint      preedit_length;
	PangoAttrList *preedit_attrs;
};

GType        gnumeric_canvas_get_type (void);
GnumericCanvas *gnumeric_canvas_new      (SheetControlGUI *scg, GnumericPane *pane);

int gnm_canvas_find_col (GnumericCanvas *gsheet, int x, int *col_origin);
int gnm_canvas_find_row (GnumericCanvas *gsheet, int y, int *row_origin);

void gnm_canvas_compute_visible_region (GnumericCanvas *gsheet,
					gboolean const full_recompute);
void gnm_canvas_redraw_range (GnumericCanvas *gsheet, Range const *r);

typedef enum {
	GNM_SLIDE_X = 1,
	GNM_SLIDE_Y = 2,
	GNM_SLIDE_EXTERIOR_ONLY = 4,
	GNM_SLIDE_AT_COLROW_BOUND = 8 /* not implemented */
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
