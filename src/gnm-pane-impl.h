#ifndef _GNM_PANE_IMPL_H_
# define _GNM_PANE_IMPL_H_

#include <src/gnm-pane.h>
#include <src/gnumeric-simple-canvas.h>
#include <goffice/canvas/goc-structs.h>

G_BEGIN_DECLS

#define GNM_PANE_MAX_X 1600000
#define GNM_PANE_MAX_Y 1536000000

struct _GnmPane {
	GnmSimpleCanvas simple;

	GnmCellPos first, last_full, last_visible;
	struct {
		gint64 x, y;
	} first_offset;

	/* In stacking order from lowest to highest */
	GocGroup *grid_items;	/* grid & cursors */
	GocGroup *object_views;	/* object views */
	GocGroup *action_items;	/* drag cursors, and object ctrl pts */

	/* Sliding scroll */
	GnmPaneSlideHandler slide_handler;
	gpointer   slide_data;
	guint      sliding_timer;	/* a gtk_timeout tag, 0 means not set */
	int        sliding_x, sliding_y;
	int        sliding_dx, sliding_dy;
	gboolean   sliding_adjacent_h, sliding_adjacent_v;

	/*  IM */
	guint im_preedit_started :1;
	guint preedit_length;
	GtkIMContext  *im_context;
	PangoAttrList *preedit_attrs;

	gboolean insert_decimal;

	int		 index;
	struct {
		GocCanvas *canvas;
		GnmItemBar *item;
	} col, row;

	/* Lines across the grid.  Used for col/row resize and the creation of
	 * frozen panes */
	struct {
		GocItem   *guide, *start;
		GocPoint  *points;
	} size_guide;

	GnmItemGrid *grid;
	GnmItemEdit *editor;

	struct {
		GnmItemCursor *std, *rangesel, *special;
		GSList *animated;

		GSList *expr_range;	/* highlight refs while editing */
	} cursor;

	struct {
		int		 button;	  /* the button that intiated the object drag */
		gboolean	 created_objects;
		gboolean	 had_motion;	  /* while dragging did we actually move */
		GHashTable	*ctrl_pts;	  /* arrays of GocItems hashed by sheet object */
		double		 last_x, last_y, origin_x, origin_y;
	} drag;

	GdkCursor	*mouse_cursor;
	GtkWidget       *size_tip;
	SheetObject     *cur_object;

	// Style on behalf of objects with, potentially, numerous instances.
	GHashTable *object_style;
};

G_END_DECLS

#endif /* _GNM_PANE_IMPL_H_ */
