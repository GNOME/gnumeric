#ifndef GNUMERIC_SHEET_CONTROL_GUI_H
#define GNUMERIC_SHEET_CONTROL_GUI_H

#include "gui-gnumeric.h"
#include "sheet-control.h"
#include <gtk/gtkwidget.h>

#define SHEET_CONTROL_GUI_TYPE        (sheet_control_gui_get_type ())
#define SHEET_CONTROL_GUI(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_CONTROL_GUI_TYPE, SheetControlGUI))
#define SHEET_CONTROL_GUI_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), SHEET_CONTROL_GUI_TYPE))
#define IS_SHEET_CONTROL_GUI(o)       (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_CONTROL_GUI_TYPE))

#define	SHEET_CONTROL_KEY	"SheetControl"

GType		 sheet_control_gui_get_type (void);
SheetControlGUI *sheet_control_gui_new	    (SheetView *sv, WorkbookControlGUI *wbcg);

GtkWidget *scg_toplevel		(SheetControlGUI *scg);
void scg_take_focus             (SheetControlGUI *scg);

void scg_mode_edit_object	(SheetControlGUI *scg, SheetObject *so);
void scg_mode_create_object	(SheetControlGUI *scg, SheetObject *so);

void scg_context_menu		(SheetControlGUI *scg, GdkEventButton *event,
				 gboolean is_col, gboolean is_row);
void scg_object_nudge		(SheetControlGUI *scg, int x_offset, int y_offset);
void scg_object_update_bbox	(SheetControlGUI *scg, SheetObject *so, double const *offset);
void scg_object_calc_position	(SheetControlGUI *scg, SheetObject *so, double const *coords);
void scg_object_view_position	(SheetControlGUI *scg, SheetObject *so, double *coords);
void scg_object_stop_editing	(SheetControlGUI *scg, SheetObject *so);

void scg_comment_select		(SheetControlGUI *scg, CellComment *cc);
void scg_comment_display	(SheetControlGUI *scg, CellComment *cc);
void scg_comment_unselect	(SheetControlGUI *scg, CellComment *cc);

void scg_select_all		(SheetControlGUI *scg);
gboolean scg_colrow_select	(SheetControlGUI *scg,
				 gboolean is_cols, int index, int modifiers);
void scg_colrow_size_set	(SheetControlGUI *scg,
				 gboolean is_cols, int index, int new_size_pixels);
int  scg_colrow_distance_get	(SheetControlGUI const *scg,
				 gboolean is_cols, int from, int to);

void scg_edit_start		(SheetControlGUI *scg);
void scg_edit_stop		(SheetControlGUI *scg);

void scg_rangesel_start		(SheetControlGUI *scg,
				 int base_col, int base_row,
				 int move_col, int move_row);
void scg_rangesel_bound		(SheetControlGUI *scg,
				 int base_col, int base_row,
				 int move_col, int move_row);
void scg_rangesel_stop		(SheetControlGUI *scg, gboolean clear_str);
void scg_rangesel_extend_to	(SheetControlGUI *scg, int col, int row);
void scg_rangesel_move		(SheetControlGUI *scg, int dir,
				 gboolean jump_to_bound, gboolean horiz);
void scg_rangesel_extend	(SheetControlGUI *scg, int n,
				 gboolean jump_to_bound, gboolean horiz);
void scg_make_cell_visible	(SheetControlGUI *scg, int col, int row,
				 gboolean force_scroll, gboolean couple_panes);

void scg_set_display_cursor	(SheetControlGUI *scg);
void scg_cursor_move		(SheetControlGUI *scg, int dir,
				 gboolean jump_to_bound, gboolean horiz);
void scg_cursor_extend		(SheetControlGUI *scg, int n,
				 gboolean jump_to_bound, gboolean horiz);

void scg_special_cursor_start	(SheetControlGUI *scg, int style, int button);
void scg_special_cursor_stop	(SheetControlGUI *scg);
gboolean scg_special_cursor_bound_set (SheetControlGUI *scg, Range const *r);

void scg_set_left_col		(SheetControlGUI *scg, int new_first_col);
void scg_set_top_row		(SheetControlGUI *scg, int new_first_row);

void scg_colrow_resize_stop	(SheetControlGUI *scg);
void scg_colrow_resize_start	(SheetControlGUI *scg,
				 gboolean is_cols, int resize_first);
void scg_colrow_resize_move	(SheetControlGUI *scg,
				 gboolean is_cols, int resize_last);

typedef void (*SCGUIMoveFunc)	(SheetControlGUI *, int n,
				 gboolean jump, gboolean horiz);
void scg_queue_movement		(SheetControlGUI *scg,
				 SCGUIMoveFunc	  handler,
				 int n, gboolean jump, gboolean horiz);

/* DO NOT USE THIS WITHOUT ALOT OF THOUGHT */
GnmCanvas	   *scg_pane		(SheetControlGUI *scg, int pane);

WorkbookControlGUI *scg_get_wbcg	(SheetControlGUI const *scg);

/* FIXME : Move this around to a more reasonable location */
StyleFont * scg_get_style_font (Sheet const *sheet, MStyle const *style);

#endif /* GNUMERIC_SHEET_CONTROL_GUI_H */
