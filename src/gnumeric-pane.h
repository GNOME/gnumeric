#ifndef GNUMERIC_PANE_H
#define GNUMERIC_PANE_H

#include "gui-gnumeric.h"
#include <libfoocanvas/foo-canvas.h>
#include <libfoocanvas/foo-canvas-util.h>
#include "gui-util.h"

struct _GnumericPane {
	int		 index;
	gboolean	 is_active;
	GnmCanvas	*gcanvas;
	struct {
		FooCanvas *canvas;
		ItemBar   *item;
	} col, row;

	/* Lines for resizing cols and rows */
	struct {
		GtkObject       *guide;
		GtkObject       *start;
		FooCanvasPoints *points;
	} colrow_resize;

	ItemGrid      *grid;
	ItemEdit      *editor;

	struct {
		ItemCursor *std, *rangesel, *special, *rangehighlight;
	} cursor;
	GSList		*anted_cursors;

	SheetObject	*drag_object;
	FooCanvasItem   *control_points [9]; /* Control points for the current item */

	GdkCursor	*mouse_cursor;
	GtkWidget       *size_tip;
};

void gnm_pane_init	(GnmPane *pane, SheetControlGUI *scg,
			 gboolean col_header, gboolean row_header, int index);
void gnm_pane_release	(GnmPane *pane);
void gnm_pane_bound_set	(GnmPane *pane,
			 int start_col, int start_row,
			 int end_col, int end_row);

void gnm_pane_edit_start		(GnmPane *gsheet);
void gnm_pane_edit_stop			(GnmPane *gsheet);

void gnm_pane_colrow_resize_stop	(GnmPane *pane);
void gnm_pane_colrow_resize_start	(GnmPane *pane,
					 gboolean is_cols, int resize_pos);
void gnm_pane_colrow_resize_move	(GnmPane *pane,
					 gboolean is_cols, int resize_pos);

void gnm_pane_reposition_cursors		(GnmPane *pane);
gboolean gnm_pane_cursor_bound_set	 	(GnmPane *pane, GnmRange const *r);
gboolean gnm_pane_rangesel_bound_set		(GnmPane *pane, GnmRange const *r);
void gnm_pane_rangesel_start			(GnmPane *pane, GnmRange const *r);
void gnm_pane_rangesel_stop			(GnmPane *pane);
gboolean gnm_pane_special_cursor_bound_set	(GnmPane *pane, GnmRange const *r);
void gnm_pane_special_cursor_start 		(GnmPane *pane, int style, int button);
void gnm_pane_special_cursor_stop		(GnmPane *pane);
void gnm_pane_mouse_cursor_set                  (GnmPane *pane, GdkCursor *c);

void gnm_pane_object_stop_editing (GnmPane *pane);
void gnm_pane_object_set_bounds   (GnmPane *pane, SheetObject *so,
				   double l, double t, double r, double b);

/* A convenience api */
typedef void (*GnmPaneObjectBoundsChanged) (SheetObject *so, FooCanvasItem *view);
void gnm_pane_object_register (SheetObject *so, FooCanvasItem *view,
			       GnmPaneObjectBoundsChanged bounds_changed);
void gnm_pane_widget_register (SheetObject *so, GtkWidget *w, FooCanvasItem *view,
			       GnmPaneObjectBoundsChanged bounds_changed);

#endif /* GNUMERIC_PANE_H */
