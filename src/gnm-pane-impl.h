/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PANE_IMPL_H_
# define _GNM_PANE_IMPL_H_

#include <src/gnm-pane.h>
#include <src/gnumeric-simple-canvas.h>
#include <gtk/gtkimmulticontext.h>

G_BEGIN_DECLS

#define GNM_PANE_MAX_X 1000000
#define GNM_PANE_MAX_Y 6000000

struct _GnmPane {
	GnmSimpleCanvas simple;

	GnmCellPos first, last_full, last_visible, first_offset;

	/* In stacking order from lowest to highest */
	FooCanvasGroup *grid_items;	/* grid & cursors */
	FooCanvasGroup *object_views;	/* object views */
	FooCanvasGroup *action_items;	/* drag cursors, and object ctrl pts */

	/* Sliding scroll */
	GnmPaneSlideHandler slide_handler;
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




	int		 index;
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
		ItemCursor *std, *rangesel, *special;
		GSList *animated;

		ItemCursor *expr_range;	/* highlight refs while editing */
	} cursor;

	struct {
		int		 button;	  /* the button that intiated the object drag */
		gboolean	 created_objects;
		gboolean	 had_motion;	  /* while dragging did we actually move */
		GHashTable	*ctrl_pts;	  /* arrays of FooCanvasItems hashed by sheet object */
		double		 last_x, last_y, origin_x, origin_y;
	} drag;

	GdkCursor	*mouse_cursor;
	GtkWidget       *size_tip;
};

G_END_DECLS

#endif /* _GNM_PANE_IMPL_H_ */
