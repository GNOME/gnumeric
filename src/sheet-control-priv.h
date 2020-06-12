#ifndef _GNM_SHEET_CONTROL_PRIV_H_
# define _GNM_SHEET_CONTROL_PRIV_H_

#include <sheet-control.h>

G_BEGIN_DECLS

struct _SheetControl {
	GObject object;

	SheetView	*view;
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
	void (*scrollbar_config)	(SheetControl *sc);
	void (*mode_edit)		(SheetControl *sc);
	void (*set_top_left)		(SheetControl *sc, int col, int row);
	void (*recompute_visible_region)(SheetControl *sc,
					 gboolean full_recompute);
	void (*make_cell_visible)	(SheetControl  *sc, int col, int row,
					 gboolean couple_panes);
	void (*cursor_bound)		(SheetControl *sc, GnmRange const *r);
	void (*set_panes)		(SheetControl *sc);
	void (*object_create_view)	(SheetControl *sc, SheetObject *so);
	void (*scale_changed)		(SheetControl *sc);
	void (*show_im_tooltip)         (SheetControl *sc,
					 GnmInputMsg *im, GnmCellPos *pos);
	void (*freeze_object_view)      (SheetControl *sc, gboolean freeze);
} SheetControlClass;

#define SHEET_CONTROL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SHEET_CONTROL_TYPE, SheetControlClass))

G_END_DECLS

#endif /* _GNM_SHEET_CONTROL_PRIV_H_ */
