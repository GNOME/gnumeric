#ifndef GNUMERIC_SHEET_CONTROL_GUI_H
#define GNUMERIC_SHEET_CONTROL_GUI_H

#include "sheet-control-priv.h"
#include "gui-gnumeric.h"

#define SHEET_CONTROL_GUI_TYPE        (sheet_control_gui_get_type ())
#define SHEET_CONTROL_GUI(obj)        (GTK_CHECK_CAST((obj), SHEET_CONTROL_GUI_TYPE, SheetControlGUI))
#define SHEET_CONTROL_GUI_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), SHEET_CONTROL_GUI_TYPE))
#define IS_SHEET_CONTROL_GUI(o)       (GTK_CHECK_TYPE((o), SHEET_CONTROL_GUI_TYPE))

#define	SHEET_CONTROL_KEY	"SheetControl"

typedef gboolean (*SheetControlGUISlideHandler) (SheetControlGUI *scg, int col, int row,
						 gpointer user_data);

GtkType    sheet_control_gui_get_type (void);
GtkObject *sheet_control_gui_new      (Sheet *sheet);

void	 scg_stop_sliding	      (SheetControlGUI *scg);
gboolean scg_start_sliding	      (SheetControlGUI *scg,
				       SheetControlGUISlideHandler handler,
				       gpointer user_data,
				       int col, int row, int dx, int dy);

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

void	 scg_rangesel_start	    (SheetControlGUI *scg, int col, int row);
void	 scg_rangesel_stop	    (SheetControlGUI *scg, gboolean clear_str);
void	 scg_rangesel_extend_to	    (SheetControlGUI *scg, int col, int row);
void	 scg_rangesel_bound	    (SheetControlGUI *scg,
				     int base_col, int base_row,
				     int move_col, int move_row);
void	 scg_rangesel_move	    (SheetControlGUI *scg, int dir,
				     gboolean jump_to_bound, gboolean horiz);
void	 scg_rangesel_extend	    (SheetControlGUI *scg, int n,
				     gboolean jump_to_bound, gboolean horiz);

void scg_cursor_move_to (SheetControlGUI *Scg, int col, int row,
			 gboolean clear_selection);
void scg_cursor_move    (SheetControlGUI *scg, int dir,
			 gboolean jump_to_bound, gboolean horiz);
void scg_cursor_extend  (SheetControlGUI *scg, int n,
			 gboolean jump_to_bound, gboolean horiz);

/* FIXME : Move these around to a more reasonable location */
SheetControlGUI *sheet_new_scg (Sheet *sheet);
void       sheet_detach_control    (SheetControl *sc);
StyleFont * scg_get_style_font (Sheet const *sheet, MStyle const *style);

#endif /* GNUMERIC_SHEET_CONTROL_GUI_H */
