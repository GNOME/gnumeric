#ifndef GNUMERIC_SHEET_CONTROL_GUI_PRIV_H
#define GNUMERIC_SHEET_CONTROL_GUI_PRIV_H

#include "sheet-control-gui.h"
#include "sheet-control-priv.h"
#include "gnumeric-pane.h"
#include "sheet-object.h"
#include <gtk/gtktable.h>

struct _SheetControlGUI {
	SheetControl sheet_control;

	/* Cached SheetControl attribute to reduce casting. */
	WorkbookControlGUI *wbcg;

	GtkTable	*table;
	GtkTable	*inner_table;
	GtkTable	*corner;
	GtkWidget	*select_all_btn;
	GtkWidget       *label;
	struct {
		GPtrArray	*buttons;
		GtkWidget	*button_box;
	} col_group, row_group;

	GnumericPane	 pane [4];
	int		 active_panes;

	int grab_stack; /* utility to keep track of grabs in the various canvases */

	/* Scrolling information */
	GtkWidget	*vs, *hs;	/* Scrollbars */
	GtkObject	*va, *ha;	/* Adjustments */

	/* SheetObject support */
	SheetObject	 *new_object;	/* A newly created object that has yet to be realized */
	SheetObject	 *current_object;
	SheetObject	 *drag_object;
	SheetObjectAnchor old_anchor;
	gboolean	  object_was_resized;
	double		  object_coords [4];
	double		  last_x, last_y;
	GnomeCanvasItem  *control_points [9]; /* Control points for the current item */

	/* Keep track of a rangeselector state */
	struct {
		gboolean active;
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
