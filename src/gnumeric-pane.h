#ifndef GNUMERIC_PANE_H
#define GNUMERIC_PANE_H

#include "gui-gnumeric.h"
#include <libfoocanvas/foo-canvas.h>
#include <libfoocanvas/foo-canvas-util.h>

struct _GnumericPane {
	int		 index;
	gboolean	 is_active;
	GnmCanvas	*gcanvas;
	struct {
		FooCanvas *canvas;
		ItemBar     *item;
	} col, row;

	/* Lines for resizing cols and rows */
	struct {
		GtkObject         *guide;
		GtkObject         *start;
		FooCanvasPoints *points;
	} colrow_resize;

	ItemGrid      *grid;
	ItemEdit      *editor;

	struct {
		ItemCursor *std, *rangesel, *special, *rangehighlight;
	} cursor;
	GSList		*anted_cursors;

	SheetObject	 *drag_object;
	FooCanvasItem  *control_points [9]; /* Control points for the current item */

	int	cursor_type;
};

void gnm_pane_init	(GnumericPane *pane, SheetControlGUI *scg,
			 gboolean col_header, gboolean row_header, int index);
void gnm_pane_release	(GnumericPane *pane);
void gnm_pane_bound_set	(GnumericPane *pane,
			 int start_col, int start_row,
			 int end_col, int end_row);

void gnm_pane_edit_start		(GnumericPane *gsheet);
void gnm_pane_edit_stop			(GnumericPane *gsheet);

void gnm_pane_colrow_resize_stop	(GnumericPane *pane);
void gnm_pane_colrow_resize_start	(GnumericPane *pane,
					 gboolean is_cols, int resize_pos);
void gnm_pane_colrow_resize_move	(GnumericPane *pane,
					 gboolean is_cols, int resize_pos);

void gnm_pane_reposition_cursors		(GnumericPane *pane);
gboolean gnm_pane_cursor_bound_set	 	(GnumericPane *pane, Range const *r);
gboolean gnm_pane_rangesel_bound_set		(GnumericPane *pane, Range const *r);
void gnm_pane_rangesel_start			(GnumericPane *pane, Range const *r);
void gnm_pane_rangesel_stop			(GnumericPane *pane);
gboolean gnm_pane_special_cursor_bound_set	(GnumericPane *pane, Range const *r);
void gnm_pane_special_cursor_start 		(GnumericPane *pane, int style, int button);
void gnm_pane_special_cursor_stop		(GnumericPane *pane);

void gnm_pane_object_register	  (SheetObject *so, FooCanvasItem *view);
void gnm_pane_widget_register	  (SheetObject *so, GtkWidget *widget,
				   FooCanvasItem *view);
void gnm_pane_object_stop_editing (GnumericPane *pane);
void gnm_pane_object_set_bounds   (GnumericPane *pane, SheetObject *so,
				   double l, double t, double r, double b);

#endif /* GNUMERIC_PANE_H */
