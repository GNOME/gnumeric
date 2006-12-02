#ifndef GNM_CANVAS_H
#define GNM_CANVAS_H

#include "gnumeric-simple-canvas.h"
#include <gtk/gtkimmulticontext.h>

#define GNM_CANVAS_TYPE	 (gnm_canvas_get_type ())
#define GNM_CANVAS(o)	 (G_TYPE_CHECK_INSTANCE_CAST((o), GNM_CANVAS_TYPE, GnmCanvas))
#define IS_GNM_CANVAS(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_CANVAS_TYPE))

#define GNUMERIC_CANVAS_FACTOR_X 1000000
#define GNUMERIC_CANVAS_FACTOR_Y 6000000

typedef struct {
	int col, row;
	gpointer user_data;
} GnmCanvasSlideInfo;
typedef gboolean (*GnmCanvasSlideHandler) (GnmCanvas *gcanvas, GnmCanvasSlideInfo const *info);

struct _GnmCanvas {
	GnmSimpleCanvas simple;

	GnmPane *pane;	/* what pane contains this canvase */

	GnmCellPos first, last_full, last_visible, first_offset;

	/* In stacking order from lowest to highest */
	FooCanvasGroup *grid_items;	/* grid & cursors */
	FooCanvasGroup *object_views;	/* object views */
	FooCanvasGroup *action_items;	/* drag cursors, and object ctrl pts */

	/* Sliding scroll */
	GnmCanvasSlideHandler slide_handler;
	gpointer   slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_x, sliding_y;
	int        sliding_dx, sliding_dy;
	gboolean   sliding_adjacent_h, sliding_adjacent_v;

	/*  IM */
	guint      reseting_im :1;	/* quick hack to keep gtk_im_context_reset from starting an edit */
	guint      preedit_length;
	GtkIMContext  *im_context;
	PangoAttrList *preedit_attrs;
	gboolean insert_decimal;
};

GType gnm_canvas_get_type (void);
GnmCanvas *gnm_canvas_new (SheetControlGUI *scg, GnmPane *pane);

int  gnm_canvas_find_col (GnmCanvas const *gsheet, int x, int *col_origin);
int  gnm_canvas_find_row (GnmCanvas const *gsheet, int y, int *row_origin);
void gnm_canvas_redraw_range (GnmCanvas *gsheet, GnmRange const *r);
void gnm_canvas_compute_visible_region (GnmCanvas *gsheet,
					gboolean full_recompute);

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

void gnm_canvas_window_to_coord   (GnmCanvas *gcanvas,
				   gint    x,	gint    y,
				   double *wx, double *wy);
void gnm_canvas_object_autoscroll (GnmCanvas *gcanvas, GdkDragContext *context,
				   gint x, gint y, guint time);

/*
 * gnm_foo_canvas_x_w2c:
 * @canvas: a #FooCanvas
 * @x : a position in world coordinate
 *
 * Converts a x position from world coordinates to canvas coordinates.
 */
#define gnm_foo_canvas_x_w2c(canvas,x) -(int)((x) + ((canvas)->scroll_x1 * (canvas)->pixels_per_unit) - 0.5)

/*
 * gnm_canvas_x_w2c:
 * @gcanvas: a #GnmCanvas
 * @x: position in world coordinates
 *
 * Convert an x position from world coordinates to canvas coordinates,
 * taking into account sheet right to left text setting.
 */
#define gnm_canvas_x_w2c(gcanvas,x) 	((gcanvas)->simple.scg->sheet_control.sheet->text_is_rtl) ? \
					gnm_foo_canvas_x_w2c ((FooCanvas *) (gcanvas), (x)) : (x)

#endif /* GNM_CANVAS_H */
