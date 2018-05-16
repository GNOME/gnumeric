#ifndef _GNM_SHEET_CONTROL_GUI_H_
# define _GNM_SHEET_CONTROL_GUI_H_

#include <gnumeric-fwd.h>
#include <sheet-control.h>

G_BEGIN_DECLS

#define GNM_SCG_TYPE        (sheet_control_gui_get_type ())
#define GNM_SCG(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SCG_TYPE, SheetControlGUI))
#define GNM_IS_SCG(o)       (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SCG_TYPE))

GType		 sheet_control_gui_get_type (void);
SheetControlGUI *sheet_control_gui_new	    (SheetView *sv, WBCGtk *wbcg);

void scg_take_focus             (SheetControlGUI *scg);

void scg_context_menu		(SheetControlGUI *scg, GdkEvent *event,
				 gboolean is_col, gboolean is_row);

void scg_object_anchor_to_coords (SheetControlGUI const *scg,
				  SheetObjectAnchor const *anchor, double *coords);
void scg_object_coords_to_anchor (SheetControlGUI const *scg,
				  double const *coords, SheetObjectAnchor *in_out);

void scg_objects_drag		(SheetControlGUI *scg, GnmPane *gcanvas,
				 SheetObject *primary,
				 gdouble *dx, gdouble *dy,
				 int drag_type, gboolean symmetric, gboolean snap_to_grid,
				 gboolean is_mouse_move);
void scg_objects_drag_commit	(SheetControlGUI *scg, int drag_type,
				 gboolean created_objects,
				 GOUndo **pundo, GOUndo **predo,
				 gchar **undo_title);
void scg_objects_nudge		(SheetControlGUI *scg, GnmPane *gcanvas,
				 int drag_type, double dx, double dy,
				 gboolean symmetric,
				 gboolean snap_to_grid);

void scg_object_select		(SheetControlGUI *scg, SheetObject *so);
void scg_object_unselect	(SheetControlGUI *scg, SheetObject *so);
void scg_object_select_next	(SheetControlGUI *scg, gboolean reverse);

void scg_comment_select		(SheetControlGUI *scg, GnmComment *cc,
				 int x, int y);
void scg_comment_display	(SheetControlGUI *scg, GnmComment *cc,
				 int x, int y);
void scg_comment_unselect	(SheetControlGUI *scg, GnmComment *cc);

void scg_select_all		(SheetControlGUI *scg);
gboolean scg_colrow_select	(SheetControlGUI *scg,
				 gboolean is_cols, int index, int modifiers);
void scg_colrow_size_set	(SheetControlGUI *scg,
				 gboolean is_cols, int index, int new_size_pixels);
gint64  scg_colrow_distance_get	(SheetControlGUI const *scg,
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
void scg_cursor_visible		(SheetControlGUI *scg, gboolean is_visible);
void scg_cursor_move		(SheetControlGUI *scg, int dir,
				 gboolean jump_to_bound, gboolean horiz);
void scg_cursor_extend		(SheetControlGUI *scg, int n,
				 gboolean jump_to_bound, gboolean horiz);

void scg_special_cursor_start	(SheetControlGUI *scg, int style, int button);
void scg_special_cursor_stop	(SheetControlGUI *scg);
gboolean scg_special_cursor_bound_set (SheetControlGUI *scg, GnmRange const *r);

void scg_set_left_col		(SheetControlGUI *scg, int new_first_col);
void scg_set_top_row		(SheetControlGUI *scg, int new_first_row);

void scg_size_guide_start	(SheetControlGUI *scg, gboolean vert,
				 int colrow, gboolean is_colrow_resize);
void scg_size_guide_motion	(SheetControlGUI *scg, gboolean vert,
				 gint64 guide_pos);
void scg_size_guide_stop	(SheetControlGUI *scg);

typedef void (*SCGUIMoveFunc)	(SheetControlGUI *scg, int n,
				 gboolean jump, gboolean horiz);
void scg_queue_movement		(SheetControlGUI *scg,
				 SCGUIMoveFunc	  handler,
				 int n, gboolean jump, gboolean horiz);
void  scg_paste_image (SheetControlGUI *scg, GnmRange *where,
		       guint8 const *data, unsigned len);
void scg_drag_data_received (SheetControlGUI *scg, GtkWidget *source_widget,
			     double x, double y,
			     GtkSelectionData *selection_data);
void scg_drag_data_get      (SheetControlGUI *scg,
			     GtkSelectionData *selection_data);

void scg_delete_sheet_if_possible (SheetControlGUI *scg);

void scg_reload_item_edits (SheetControlGUI *scg);

/* Convenience wrappers.  */
SheetView	*scg_view	(SheetControlGUI const *scg);
Sheet		*scg_sheet	(SheetControlGUI const *scg);
WorkbookControl *scg_wbc	(SheetControlGUI const *scg);

/* DO NOT USE THIS WITHOUT ALOT OF THOUGHT */
GnmPane	   *scg_pane		(SheetControlGUI *scg, int pane);

WBCGtk *scg_wbcg	(SheetControlGUI const *scg);

G_END_DECLS

#endif /* _GNM_SHEET_CONTROL_GUI_H_ */
