#ifndef GNUMERIC_SHEET_CONTROL_GUI_H
#define GNUMERIC_SHEET_CONTROL_GUI_H

#include "sheet-control-priv.h"
#include "gui-gnumeric.h"
#include <gtk/gtktable.h>

#define SHEET_CONTROL_GUI_TYPE        (sheet_control_gui_get_type ())
#define SHEET_CONTROL_GUI(obj)        (GTK_CHECK_CAST((obj), SHEET_CONTROL_GUI_TYPE, SheetControlGUI))
#define SHEET_CONTROL_GUI_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), SHEET_CONTROL_GUI_TYPE))
#define IS_SHEET_CONTROL_GUI(o)       (GTK_CHECK_TYPE((o), SHEET_CONTROL_GUI_TYPE))

#define	SHEET_CONTROL_KEY	"SheetControl"

typedef gboolean (*SheetControlGUISlideHandler) (SheetControlGUI *scg, int col, int row,
						 gpointer user_data);

struct _SheetControlGUI {
	SheetControl sheet_control;

	Sheet          		*sheet;
	WorkbookControlGUI	*wbcg;

	GtkTable  	 *table;
	GtkWidget	 *select_all_btn;
	GtkWidget        *canvas;
	GnomeCanvas      *col_canvas, *row_canvas;
	GnomeCanvasItem  *col_item, *row_item;

	/* Scrolling information */
	GtkWidget  *vs, *hs;	/* The scrollbars */
	GtkObject  *va, *ha;    /* The adjustments */
	GtkWidget        *tip;	/* Tip for scrolling */

	/* Anted cursors */
	GList            *anted_cursors;
	GnomeCanvasGroup *anted_group;

	/* Sliding scroll */
	SheetControlGUISlideHandler	slide_handler;
	gpointer   slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_col, sliding_row;
	int        sliding_x, sliding_y;

	/* SheetObject support */
	GnomeCanvasGroup *object_group;
	SheetObject	 *new_object;	/* A newly created object that has yet to be realized */
	SheetObject	 *current_object;
	SheetObject	 *drag_object;
	double		  object_coords [4];
	double		  last_x, last_y;
	void        	 *active_object_frame;	/* FIXME remove this */
	GnomeCanvasItem  *control_points [9]; /* Control points for the current item */

	/* Keep track of a rangeselector state */
	struct {
		gboolean active;
		int	 cursor_pos;
		CellPos	 base_corner;	/* Corner remains static when rubber banding */
		CellPos	 move_corner;	/* Corner to move when extending */
		Range	 displayed;	/* The range to display */
	} rangesel;

	/* Comments */
	struct {
		CellComment *selected;
		GtkWidget   *item;	/* TODO : make this a canvas item with an arrow */
		int	     timer;
	} comment;
};

typedef struct {
	SheetControlClass parent_class;
} SheetControlGUIClass;

GtkType    sheet_control_gui_get_type (void);
GtkObject *sheet_control_gui_new      (Sheet *sheet);

void	 scg_stop_sliding	      (SheetControlGUI *scg);
gboolean scg_start_sliding	      (SheetControlGUI *scg,
				       SheetControlGUISlideHandler handler,
				       gpointer user_data,
				       int col, int row, int dx, int dy);

void scg_resize			(SheetControlGUI *scg);
void scg_set_zoom_factor        (SheetControlGUI *scg);
void scg_redraw_all             (SheetControlGUI *scg);
void scg_redraw_cell_region     (SheetControlGUI *scg,
				 int start_col, int start_row,
				 int end_col, int end_row);
void scg_redraw_headers         (SheetControlGUI *scg,
				 gboolean const col, gboolean const row,
				 Range const * r /* optional == NULL */);
void scg_ant                    (SheetControlGUI *scg);
void scg_unant                  (SheetControlGUI *scg);
void scg_take_focus             (SheetControlGUI *scg);

void scg_adjust_preferences     (SheetControlGUI *scg);
void scg_update_cursor_pos      (SheetControlGUI *scg);
void scg_scrollbar_config       (SheetControlGUI const *scg);

void scg_mode_edit		(SheetControlGUI *scg);
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

void scg_compute_visible_region (SheetControlGUI *scg, gboolean full_recompute);
void scg_make_cell_visible	(SheetControlGUI  *scg, int col, int row,
				 gboolean force_scroll);
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

void scg_cursor_bound	(SheetControlGUI *scg, Range const *r);
void scg_cursor_move_to (SheetControlGUI *Scg, int col, int row,
			 gboolean clear_selection);
void scg_cursor_move    (SheetControlGUI *scg, int dir,
			 gboolean jump_to_bound, gboolean horiz);
void scg_cursor_extend  (SheetControlGUI *scg, int n,
			 gboolean jump_to_bound, gboolean horiz);

/* FIXME : Move these around to a more reasonable location */
SheetControlGUI *sheet_new_scg (Sheet *sheet);
void       sheet_detach_scg    (SheetControlGUI *scg);
StyleFont * scg_get_style_font (Sheet const *sheet, MStyle const *style);

#endif /* GNUMERIC_SHEET_CONTROL_GUI_H */
