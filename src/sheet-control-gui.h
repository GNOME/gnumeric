#ifndef GNUMERIC_SHEET_CONTROL_GUI_H
#define GNUMERIC_SHEET_CONTROL_GUI_H

#include "gui-gnumeric.h"
#include "sheet-control.h"

#define SHEET_CONTROL_GUI_TYPE        (sheet_control_gui_get_type ())
#define SHEET_CONTROL_GUI(obj)        (GTK_CHECK_CAST((obj), SHEET_CONTROL_GUI_TYPE, SheetControlGUI))
#define SHEET_CONTROL_GUI_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), SHEET_CONTROL_GUI_TYPE))
#define IS_SHEET_CONTROL_GUI(o)       (GTK_CHECK_TYPE((o), SHEET_CONTROL_GUI_TYPE))

#define	SHEET_CONTROL_KEY	"SheetControl"

GtkType sheet_control_gui_get_type (void);
SheetControlGUI *sheet_control_gui_new      (Sheet *sheet);

void scg_take_focus             (SheetControlGUI *scg);

void scg_mode_edit_object	(SheetControlGUI *scg, SheetObject *so);
void scg_mode_create_object	(SheetControlGUI *scg, SheetObject *so);

void scg_context_menu		(SheetControlGUI *scg, GdkEventButton *event,
				 gboolean is_col, gboolean is_row);
void scg_object_register	(SheetObject *so, GnomeCanvasItem *view);
void scg_object_widget_register (SheetObject *so, GtkWidget *widget,
				 GnomeCanvasItem *view);
void scg_object_calc_position	(SheetControlGUI *scg, SheetObject *so, double const *coords);
void scg_object_view_position	(SheetControlGUI *scg, SheetObject *so, double *coords);
void scg_object_update_bbox	(SheetControlGUI *scg, SheetObject *so,
				 GnomeCanvasItem *so_view, double const *offset);
void scg_comment_select		(SheetControlGUI *scg, CellComment *cc);
void scg_comment_display	(SheetControlGUI *scg, CellComment *cc);
void scg_comment_unselect	(SheetControlGUI *scg, CellComment *cc);

void scg_colrow_select		(SheetControlGUI *scg,
				 gboolean is_cols, int index, int modifiers);
void scg_colrow_size_set	(SheetControlGUI *scg,
				 gboolean is_cols, int index, int new_size_pixels);
int  scg_colrow_distance_get	(SheetControlGUI const *scg,
				 gboolean is_cols, int from, int to);

void scg_create_editor		(SheetControlGUI *scg);
void scg_stop_editing		(SheetControlGUI *scg);

void scg_rangesel_start		(SheetControlGUI *scg, int col, int row);
void scg_rangesel_stop		(SheetControlGUI *scg, gboolean clear_str);
void scg_rangesel_extend_to	(SheetControlGUI *scg, int col, int row);
void scg_rangesel_bound		(SheetControlGUI *scg,
				 int base_col, int base_row,
				 int move_col, int move_row);
void scg_rangesel_move		(SheetControlGUI *scg, int dir,
				 gboolean jump_to_bound, gboolean horiz);
void scg_rangesel_extend	(SheetControlGUI *scg, int n,
				 gboolean jump_to_bound, gboolean horiz);
void scg_make_cell_visible	(SheetControlGUI *scg, int col, int row,
				 gboolean force_scroll, gboolean couple_panes);

void scg_cursor_move_to		(SheetControlGUI *Scg, int col, int row,
				 gboolean clear_selection);
void scg_cursor_move		(SheetControlGUI *scg, int dir,
				 gboolean jump_to_bound, gboolean horiz);
void scg_cursor_extend		(SheetControlGUI *scg, int n,
				 gboolean jump_to_bound, gboolean horiz);
void scg_set_left_col		(SheetControlGUI *scg, int new_first_col);
void scg_set_top_row		(SheetControlGUI *scg, int new_first_row);

void scg_colrow_resize_end	(SheetControlGUI *scg);
void scg_colrow_resize_start	(SheetControlGUI *scg,
				 gboolean is_cols, int resize_first);
void scg_colrow_resize_move	(SheetControlGUI *scg,
				 gboolean is_cols, int resize_last);

/* DO NOT USE THIS WITHOUT ALOT OF THOUGHT */
GnumericSheet      *scg_pane		(SheetControlGUI *scg, int pane);

WorkbookControlGUI *scg_get_wbcg	(SheetControlGUI const *scg);

/* FIXME : Move this around to a more reasonable location */
StyleFont * scg_get_style_font (Sheet const *sheet, MStyle const *style);

#endif /* GNUMERIC_SHEET_CONTROL_GUI_H */
