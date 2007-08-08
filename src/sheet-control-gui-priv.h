#ifndef GNUMERIC_SHEET_CONTROL_GUI_PRIV_H
#define GNUMERIC_SHEET_CONTROL_GUI_PRIV_H

#include "sheet-control-gui.h"
#include "sheet-control-priv.h"
#include "sheet-object.h"
#include <gtk/gtktable.h>
#include <gtk/gtkpaned.h>

#define	SCG_NUM_PANES		4
struct _SheetControlGUI {
	SheetControl sheet_control;

	/* Cached SheetControl attribute to reduce casting. */
	WBCGtk *wbcg;

	GtkTable	*table;
	GtkTable	*inner_table;
	GtkTable	*corner;
	GtkWidget	*select_all_btn;
	GtkWidget       *label;
	struct {
		GPtrArray	*buttons;
		GtkWidget	*button_box;
	} col_group, row_group;

	GnmPane	*pane [SCG_NUM_PANES];
	int	 active_panes;

	int grab_stack; /* utility to keep track of grabs in the various canvases */

	/* Scrolling information */
	GtkPaned	*vpane, *hpane;	/* drag panes for freezing */
	GtkWidget	*vs, *hs;	/* Scrollbars */
	GtkAdjustment	*va, *ha;	/* Adjustments */
	guint 		 pane_drag_handler;

	/* SheetObject support */
	SheetObject	 *new_object;	/* A newly created object that has yet to be realized */
	GHashTable	 *selected_objects;

	/* Keep track of a rangeselector state */
	struct {
		gboolean active;
		GnmCellPos	 base_corner;	/* Corner remains static when rubber banding */
		GnmCellPos	 move_corner;	/* Corner to move when extending */
		GnmRange	 displayed;	/* The range to display */
	} rangesel;

	/* Comments */
	struct {
		GnmComment *selected;
		GtkWidget   *item;	/* TODO : make this a canvas item with an arrow */
		int	     timer;
	} comment;

	struct {
		int		timer, counter, n;
		gboolean	jump, horiz;
		SCGUIMoveFunc	handler;
	} delayedMovement;
};
typedef SheetControlClass SheetControlGUIClass;

/* SCG virtual methods called directly from the GUI layer*/
void scg_adjust_preferences     (SheetControlGUI *scg);
void scg_mode_edit		(SheetControlGUI *scg);

#define SCG_FOREACH_PANE(scg, pane, code)		\
  do {							\
	int i;						\
	for (i = scg->active_panes; i-- > 0 ; ) {	\
		GnmPane *pane = scg->pane[i];		\
		if (pane) {				\
			code				\
		}					\
	}						\
  } while (0)

#endif /* GNUMERIC_SHEET_CONTROL_GUI_PRIV_H */
