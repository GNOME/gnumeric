#ifndef GNUMERIC_SHEET_CONTROL_PRIV_H
#define GNUMERIC_SHEET_CONTROL_PRIV_H

#include "sheet-control.h"

struct _SheetControl {
	GtkObject object;

	Sheet          	*sheet;
	WorkbookControl *wbc;
};

typedef struct {
	GtkObjectClass   object_class;

	void (*init_state) (SheetControl *sc);

	void (*resize)			(SheetControl *sc, gboolean force_scroll);
	void (*set_zoom_factor)		(SheetControl *sc);
	void (*redraw_all)		(SheetControl *sc);
	void (*redraw_region)		(SheetControl *sc,
					 int start_col, int start_row,
					 int end_col, int end_row);
	void (*redraw_headers)		(SheetControl *sc,
					 gboolean const col, gboolean const row,
					 Range const * r);
	void (*ant)			(SheetControl *sc);
	void (*unant)			(SheetControl *sc);
	void (*adjust_preferences)	(SheetControl *sc);
	void (*update_cursor_pos)	(SheetControl *sc);
	void (*scrollbar_config)	(SheetControl const *sc);
	void (*mode_edit)		(SheetControl *sc);
	void (*set_top_left)		(SheetControl *sc, int col, int row);
	void (*compute_visible_region)	(SheetControl *sc,
					 gboolean full_recompute);
	void (*make_cell_visible)	(SheetControl  *sc, int col, int row,
					 gboolean force_scroll, gboolean couple_panes);
	void (*cursor_bound)		(SheetControl *sc, Range const *r);
	void (*set_panes)		(SheetControl *sc);
} SheetControlClass;

#define SHEET_CONTROL_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_CONTROL_TYPE, SheetControlClass))

#endif /* GNUMERIC_SHEET_CONTROL_PRIV_H */
