#ifndef GNUMERIC_SHEET_CONTROL_GUI_PRIV_H
#define GNUMERIC_SHEET_CONTROL_GUI_PRIV_H

#include "sheet-control-gui.h"
#include "sheet-control-priv.h"
#include "gnumeric-pane.h"
#include "sheet-object.h"
#include <gtk/gtktable.h>

#define	SCG_NUM_PANES		4
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

	GnmPane	 pane [SCG_NUM_PANES];
	int	 active_panes;

	int grab_stack; /* utility to keep track of grabs in the various canvases */

	/* Scrolling information */
	GtkWidget	*vs, *hs;	/* Scrollbars */
	GtkObject	*va, *ha;	/* Adjustments */

	/* SheetObject support */
	SheetObject	 *new_object;	/* A newly created object that has yet to be realized */
	SheetObject	 *current_object;
	SheetObjectAnchor old_anchor;
	gboolean	  object_was_resized;
	double		  object_coords [4];
	double		  last_x, last_y;

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

typedef struct {
	SheetControlClass parent_class;
} SheetControlGUIClass;

/* SCG virtual methods are called directly from the GUI layer*/
void scg_set_zoom_factor        (SheetControl *sc);
void scg_adjust_preferences     (SheetControl *sc);
void scg_scrollbar_config       (SheetControl const *sc);
void scg_mode_edit		(SheetControl *sc);

#define SCG_FOREACH_PANE(scg, pane, code)		\
  do {							\
	int i;						\
	GnmPane *pane;					\
	for (i = scg->active_panes; i-- > 0 ; ) {	\
		pane = scg->pane + i;			\
		if (pane->is_active) {			\
			code				\
		}					\
	}						\
  } while (0)

#endif /* GNUMERIC_SHEET_CONTROL_GUI_PRIV_H */
