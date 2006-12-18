#ifndef GNUMERIC_PANE_H
#define GNUMERIC_PANE_H

#include "gui-gnumeric.h"
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-util.h>
#include "gui-util.h"

struct _GnumericPane {
	int		 index;
	gboolean	 is_active;
	GnmCanvas	*gcanvas;
	struct {
		FooCanvas *canvas;
		ItemBar   *item;
	} col, row;

	/* Lines across the grid.  Used for col/row resize and the creation of
	 * frozen panes */
	struct {
		FooCanvasItem   *guide, *start;
		FooCanvasPoints *points;
	} size_guide;

	ItemGrid      *grid;
	ItemEdit      *editor;

	struct {
		ItemCursor *std, *rangesel, *special, *rangehighlight;
	} cursor;
	GSList		*anted_cursors;

	struct {
	    int		 button;	  /* the button that intiated the object drag */
	    gboolean	 created_objects;
	    gboolean	 had_motion;	  /* while dragging did we actually move */
	    GHashTable	*ctrl_pts;	  /* arrays of FooCanvasItems hashed by sheet object */
	    double	 last_x, last_y, origin_x, origin_y;
	} drag;

	GdkCursor	*mouse_cursor;
	GtkWidget       *size_tip;
};

void gnm_pane_init	(GnmPane *pane, SheetControlGUI *scg,
			 gboolean col_header, gboolean row_header, int index);
void gnm_pane_release	(GnmPane *pane);
void gnm_pane_bound_set	(GnmPane *pane,
			 int start_col, int start_row,
			 int end_col, int end_row);

void gnm_pane_edit_start	(GnmPane *p);
void gnm_pane_edit_stop		(GnmPane *p);

void gnm_pane_size_guide_start  (GnmPane *p, gboolean vert, int colrow,
				 int width);
void gnm_pane_size_guide_motion	(GnmPane *p, gboolean vert, int guide_pos);
void gnm_pane_size_guide_stop	(GnmPane *p);

void gnm_pane_reposition_cursors		(GnmPane *pane);
gboolean gnm_pane_cursor_bound_set	 	(GnmPane *pane, GnmRange const *r);
gboolean gnm_pane_rangesel_bound_set		(GnmPane *pane, GnmRange const *r);
void gnm_pane_rangesel_start			(GnmPane *pane, GnmRange const *r);
void gnm_pane_rangesel_stop			(GnmPane *pane);
gboolean gnm_pane_special_cursor_bound_set	(GnmPane *pane, GnmRange const *r);
void gnm_pane_special_cursor_start 		(GnmPane *pane, int style, int button);
void gnm_pane_special_cursor_stop		(GnmPane *pane);
void gnm_pane_mouse_cursor_set                  (GnmPane *pane, GdkCursor *c);

void gnm_pane_objects_drag        (GnmPane *pane, SheetObject *so,
				   gdouble new_x, gdouble new_y,int drag_type, 
				   gboolean symmetric,gboolean snap_to_grid);
void gnm_pane_object_unselect	  (GnmPane *pane, SheetObject *so);
void gnm_pane_object_update_bbox  (GnmPane *pane, SheetObject *so);
void gnm_pane_object_start_resize (GnmPane *pane, GdkEventButton *event,
				   SheetObject *so, int drag_type, gboolean is_creation);

/* A convenience api */
SheetObjectView *gnm_pane_object_register (SheetObject *so, FooCanvasItem *view, gboolean selectable);
void		 gnm_pane_widget_register (SheetObject *so, GtkWidget *w, FooCanvasItem *view);

#endif /* GNUMERIC_PANE_H */
