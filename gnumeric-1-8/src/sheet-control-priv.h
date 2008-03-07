/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_CONTROL_PRIV_H_
# define _GNM_SHEET_CONTROL_PRIV_H_

#include "sheet-control.h"

G_BEGIN_DECLS

struct _SheetControl {
	GObject object;

	Sheet		*sheet; /* not really needed, but convenient */
	SheetView     	*view;
	WorkbookControl *wbc;
};

typedef struct {
	GObjectClass   object_class;

	void (*resize)			(SheetControl *sc, gboolean force_scroll);
	void (*redraw_all)		(SheetControl *sc, gboolean headers);
	void (*redraw_range)		(SheetControl *sc, GnmRange const *r);
	void (*redraw_headers)		(SheetControl *sc,
					 gboolean const col, gboolean const row,
					 GnmRange const * r);
	void (*ant)			(SheetControl *sc);
	void (*unant)			(SheetControl *sc);
	void (*scrollbar_config)	(SheetControl const *sc);
	void (*mode_edit)		(SheetControl *sc);
	void (*set_top_left)		(SheetControl *sc, int col, int row);
	void (*recompute_visible_region)(SheetControl *sc,
					 gboolean full_recompute);
	void (*make_cell_visible)	(SheetControl  *sc, int col, int row,
					 gboolean couple_panes);
	void (*cursor_bound)		(SheetControl *sc, GnmRange const *r);
	void (*set_panes)		(SheetControl *sc);
	float (*colrow_distance_get)	(SheetControl const *sc, gboolean is_col,
					 int start, int end);
	void (*object_create_view)	(SheetControl *sc, SheetObject *so);
	void (*scale_changed)		(SheetControl *sc);
} SheetControlClass;

#define SHEET_CONTROL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_CONTROL_TYPE, SheetControlClass))

G_END_DECLS

#endif /* _GNM_SHEET_CONTROL_PRIV_H_ */
