#ifndef GNUMERIC_SHEET_CONTROL_GUI_PRIV_H
#define GNUMERIC_SHEET_CONTROL_GUI_PRIV_H

#include "sheet-control-gui.h"
#include "sheet-control-priv.h"
/* #include "gui-gnumeric.h" */
#include <gtk/gtktable.h>

struct _GnumericPane {
	GList		*anted_cursors;
	int		 index;
	GnumericSheet	*gsheet;
	struct {
		GnomeCanvas *canvas;
		ItemBar     *item;
	} col, row;
};

struct _SheetControlGUI {
	SheetControl sheet_control;

	GtkTable	*table;
	GtkTable	*inner_table;
	GtkWidget	*select_all_btn;
	GnumericPane	 pane [4];
	int		 active_panes;

	/* Scrolling information */
	GtkWidget	*vs, *hs;	/* Scrollbars */
	GtkObject	*va, *ha;	/* Adjustments */
	GtkWidget	*tip;		/* Scrolling tip (unused till gtk2) */

	/* Sliding scroll */
	SheetControlGUISlideHandler	slide_handler;
	gpointer   slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_col, sliding_row;
	int        sliding_x, sliding_y;

	/* SheetObject support */
	SheetObject	 *new_object;	/* A newly created object that has yet to be realized */
	SheetObject	 *current_object;
	SheetObject	 *drag_object;
	double		  object_coords [4];
	double		  last_x, last_y;
	void        	 *active_object_frame;	/* FIXME remove this */
	GnomeCanvasItem  *control_points [9]; /* Control points for the current item */

	/* Keep track of a rangeselector state */
	struct {
		gboolean active;
		int	 cursor_pos;
		CellPos	 base_corner;	/* Corner remains static when rubber banding */
		CellPos	 move_corner;	/* Corner to move when extending */
		Range	 displayed;	/* The range to display */
	} rangesel;

	/* Comments */
	struct {
		CellComment *selected;
		GtkWidget   *item;	/* TODO : make this a canvas item with an arrow */
		int	     timer;
	} comment;

	/* Cached SheetControl attribute to reduce casting. */
	WorkbookControlGUI *wbcg;
};

typedef struct {
	SheetControlClass parent_class;
} SheetControlGUIClass;

/* SCG virtual methods are called directly from the GUI layer*/
void scg_set_zoom_factor        (SheetControl *sc);
void scg_adjust_preferences     (SheetControl *sc);
void scg_scrollbar_config       (SheetControl const *sc);
void scg_mode_edit		(SheetControl *sc);

#endif /* GNUMERIC_SHEET_CONTROL_GUI_PRIV_H */

