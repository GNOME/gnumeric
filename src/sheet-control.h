#ifndef _GNM_SHEET_CONTROL_H_
# define _GNM_SHEET_CONTROL_H_

#include <gnumeric.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_SHEET_CONTROL_TYPE	(sheet_control_get_type ())
#define GNM_SHEET_CONTROL(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_SHEET_CONTROL_TYPE, SheetControl))
#define GNM_IS_SHEET_CONTROL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SHEET_CONTROL_TYPE))

/* Lifecycle */
GType sheet_control_get_type	(void);
SheetView	*sc_view	(SheetControl const *sc);
Sheet		*sc_sheet	(SheetControl const *sc);
WorkbookControl *sc_wbc		(SheetControl const *sc);

/**
 * NOTE:
 * The GUI layer accesses the SheetControlGUI methods directly without
 * calling the virtual. Change this if the base class becomes something
 * more than a passthrough.
 */
void sc_resize			(SheetControl *sc, gboolean force_scroll);
void sc_redraw_all		(SheetControl *sc, gboolean headers);
void sc_redraw_range		(SheetControl *sc, GnmRange const *r);
void sc_redraw_headers		(SheetControl *sc,
				 gboolean const col, gboolean const row,
				 GnmRange const * r /* optional == NULL */);
void sc_ant			(SheetControl *sc);
void sc_unant			(SheetControl *sc);

void sc_scrollbar_config	(SheetControl *sc);

void sc_mode_edit		(SheetControl *sc);

void sc_set_top_left		(SheetControl *sc, int col, int row);
void sc_recompute_visible_region(SheetControl *sc, gboolean full_recompute);
void sc_make_cell_visible	(SheetControl *sc, int col, int row,
				 gboolean couple_panes);

void sc_cursor_bound		(SheetControl *sc, GnmRange const *r);
void sc_set_panes		(SheetControl *sc);
void sc_object_create_view	(SheetControl *sc, SheetObject *so);
void sc_scale_changed		(SheetControl *sc);

void sc_show_im_tooltip         (SheetControl *sc,
				 GnmInputMsg *im, GnmCellPos *pos);

void sc_freeze_object_view      (SheetControl *sc, gboolean freeze);

G_END_DECLS

#endif /* _GNM_SHEET_CONTROL_H_ */
