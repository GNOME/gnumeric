#ifndef GNUMERIC_SHEET_CONTROL_H
#define GNUMERIC_SHEET_CONTROL_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

#define SHEET_CONTROL_TYPE     (sheet_control_get_type ())
#define SHEET_CONTROL(obj)     (GTK_CHECK_CAST ((obj), SHEET_CONTROL_TYPE, SheetControl))
#define IS_SHEET_CONTROL(o)	  (GTK_CHECK_TYPE ((o), SHEET_CONTROL_TYPE))

GtkType sheet_control_get_type    (void);
void sc_init_state  (SheetControl *sc);

Sheet *sc_sheet	(SheetControl *sc);
void   sc_set_sheet (SheetControl *sc, Sheet *sheet);

/**
 * NOTE:
 * The GUI layer accesses the SheetControlGUI methods directly without
 * calling the virtual. Change this if the base class becomes something
 * more than a passthrough.
 */
void sc_resize		       (SheetControl *sc, gboolean force_scroll);
void sc_set_zoom_factor        (SheetControl *sc);
void sc_redraw_all             (SheetControl *sc);
void sc_redraw_region          (SheetControl *sc,
				int start_col, int start_row,
				int end_col, int end_row);
void sc_redraw_headers         (SheetControl *sc,
				gboolean const col, gboolean const row,
				Range const * r /* optional == NULL */);
void sc_ant                    (SheetControl *sc);
void sc_unant                  (SheetControl *sc);

void sc_adjust_preferences     (SheetControl *sc);
void sc_update_cursor_pos      (SheetControl *sc);
void sc_scrollbar_config       (SheetControl const *sc);

void sc_mode_edit		(SheetControl *sc);

void sc_compute_visible_region (SheetControl *sc, gboolean full_recompute);
void sc_make_cell_visible      (SheetControl *sc, int col, int row,
				gboolean force_scroll, gboolean couple_panes);
void sc_cursor_bound	       (SheetControl *sc, Range const *r);
void sc_set_panes	       (SheetControl *sc);

#endif /* GNUMERIC_SHEET_CONTROL_H */
