#ifndef GNUMERIC_SHEET_CONTROL_PRIV_H
#define GNUMERIC_SHEET_CONTROL_PRIV_H

#include "sheet-control.h"

struct _SheetControl {
	GObject object;

	Sheet		*sheet; /* not really needed, but convenient */
	SheetView     	*view;
	WorkbookControl *wbc;
};

typedef struct {
	GObjectClass   object_class;

	void (*init_state) (SheetControl *sc);

	void (*resize)			(SheetControl *sc, gboolean force_scroll);
	void (*set_zoom_factor)		(SheetControl *sc);
	void (*redraw_all)		(SheetControl *sc, gboolean headers);
	void (*redraw_range)		(SheetControl *sc, Range const *r);
	void (*redraw_headers)		(SheetControl *sc,
					 gboolean const col, gboolean const row,
					 Range const * r);
	void (*ant)			(SheetControl *sc);
	void (*unant)			(SheetControl *sc);
	void (*adjust_preferences)	(SheetControl *sc);
	void (*scrollbar_config)	(SheetControl const *sc);
	void (*mode_edit)		(SheetControl *sc);
	void (*set_top_left)		(SheetControl *sc, int col, int row);
	void (*compute_visible_region)	(SheetControl *sc,
					 gboolean full_recompute);
	void (*make_cell_visible)	(SheetControl  *sc, int col, int row,
					 gboolean couple_panes);
	void (*cursor_bound)		(SheetControl *sc, Range const *r);
	void (*set_panes)		(SheetControl *sc);
	float (*colrow_distance_get)	(SheetControl const *sc, gboolean is_col,
					 int start, int end);
	void (*object_create_view)	(SheetControl *sc, SheetObject *so);
	void (*object_destroy_view)	(SheetControl *sc, SheetObject *so);
} SheetControlClass;

#define SHEET_CONTROL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_CONTROL_TYPE, SheetControlClass))

#endif /* GNUMERIC_SHEET_CONTROL_PRIV_H */
