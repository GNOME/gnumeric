#ifndef GNUMERIC_SHEET_CONTROL_GUI_PRIV_H
#define GNUMERIC_SHEET_CONTROL_GUI_PRIV_H

#include "sheet-control-gui.h"
#include "sheet-control-priv.h"
/* #include "gui-gnumeric.h" */
#include <gtk/gtktable.h>

struct _SheetControlGUI {
	SheetControl sheet_control;

	GtkTable  	 *table;
	GtkWidget	 *select_all_btn;
	GtkWidget        *gsheet;
	GnomeCanvas      *col_canvas, *row_canvas;
	GnomeCanvasItem  *col_item, *row_item;

	/* Scrolling information */
	GtkWidget  *vs, *hs;	/* The scrollbars */
	GtkObject  *va, *ha;    /* The adjustments */
	GtkWidget        *tip;	/* Tip for scrolling */

	/* Anted cursors */
	GList            *anted_cursors;
	GnomeCanvasGroup *anted_group;

	/* Sliding scroll */
	SheetControlGUISlideHandler	slide_handler;
	gpointer   slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_col, sliding_row;
	int        sliding_x, sliding_y;

	/* SheetObject support */
	GnomeCanvasGroup *object_group;
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

