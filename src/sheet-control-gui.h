#ifndef GNUMERIC_SHEET_CONTROL_GUI_H
#define GNUMERIC_SHEET_CONTROL_GUI_H

#include "gui-gnumeric.h"
#include <gtk/gtktable.h>

#define SHEET_CONTROL_GUI_TYPE        (sheet_view_get_type ())
#define SHEET_CONTROL_GUI(obj)        (GTK_CHECK_CAST((obj), SHEET_CONTROL_GUI_TYPE, SheetControlGUI))
#define SHEET_CONTROL_GUI_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), SHEET_CONTROL_GUI_TYPE))
#define IS_SHEET_CONTROL_GUI(o)       (GTK_CHECK_TYPE((o), SHEET_CONTROL_GUI_TYPE))

typedef gboolean (*SheetControlGUISlideHandler) (SheetControlGUI *scg, int col, int row,
						 gpointer user_data);

struct _SheetControlGUI {
	GtkTable  table;

	Sheet          	*sheet;
	GHashTable  	*object_views;
	WorkbookControlGUI	*wbcg;

	GtkWidget        *canvas;
	GtkWidget	 *select_all_btn;
	GnomeCanvas      *col_canvas, *row_canvas;
	GnomeCanvasItem  *col_item, *row_item;

	/* Object group */
	GnomeCanvasGroup *object_group;
	SheetObject	 *new_object;	/* A newly created object that has yet to be realized */
	SheetObject	 *current_object;
	SheetObject	 *drag_object;
	void        	 *active_object_frame;	/* FIXME remove this */

	/* Selection group */
	GnomeCanvasGroup *selection_group;

	/* Control points for the current item */
	GnomeCanvasItem  *control_points [9];

	/* Scrolling information */
	GtkWidget  *vs, *hs;	/* The scrollbars */
	GtkObject  *va, *ha;    /* The adjustments */

	/* Tip for scrolling */
	GtkWidget        *tip;

	/* Anted cursor */
	GList            *anted_cursors;

	/* Sliding scroll */
	SheetControlGUISlideHandler	slide_handler;
	gpointer		slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_col, sliding_row;
	int        sliding_x, sliding_y;

	/* Comments */
	struct {
		CellComment	*selected;
		GtkWidget	*item;	/* TODO : make this a canvas item with an arrow */
		int		 timer;
	} comment;
};

typedef struct {
	GtkTableClass parent_class;
} SheetControlGUIClass;

GtkType          sheet_view_get_type              (void);
GtkWidget       *sheet_view_new                   (Sheet *sheet);

void             sheet_view_set_zoom_factor       (SheetControlGUI *scg,
						   double factor);

void             sheet_view_redraw_all            (SheetControlGUI *scg);
void             sheet_view_redraw_cell_region    (SheetControlGUI *scg,
						   int start_col, int start_row,
						   int end_col, int end_row);
void             sheet_view_redraw_headers        (SheetControlGUI *scg,
						   gboolean const col, gboolean const row,
						   Range const * r /* optional == NULL */);

void             sheet_view_set_header_visibility (SheetControlGUI *scg,
						   gboolean col_headers_visible,
						   gboolean row_headers_visible);

void             sheet_view_scrollbar_config      (SheetControlGUI const *scg);

void             sheet_view_selection_ant         (SheetControlGUI *scg);
void             sheet_view_selection_unant       (SheetControlGUI *scg);

void             sheet_view_adjust_preferences    (SheetControlGUI *scg);
void             sheet_view_update_cursor_pos	  (SheetControlGUI *scg);

StyleFont *      sheet_view_get_style_font        (Sheet const *sheet,
						   MStyle const *mstyle);

void	 sheet_view_stop_sliding  (SheetControlGUI *scg);
gboolean sheet_view_start_sliding (SheetControlGUI *scg,
				   SheetControlGUISlideHandler slide_handler,
				   gpointer user_data,
				   int col, int row, int dx, int dy);

void scg_mode_edit		(SheetControlGUI *scg);
void scg_mode_edit_object	(SheetControlGUI *scg, SheetObject *so);
void scg_mode_create_object	(SheetControlGUI *scg, SheetObject *so);

void scg_context_menu		(SheetControlGUI *scg, GdkEventButton *event,
				 gboolean is_col, gboolean is_row);
void scg_object_register	(SheetObject *so, GnomeCanvasItem *view);
void scg_object_widget_register (SheetObject *so, GtkWidget *widget,
				 GnomeCanvasItem *view);
void scg_object_calc_position	(SheetControlGUI *scg, SheetObject *so, double *coords);
void scg_object_view_position	(SheetControlGUI *scg, SheetObject *so, double *coords);
void scg_object_update_bbox	(SheetControlGUI *scg, SheetObject *so,
				 GnomeCanvasItem *so_view, double const *offset);
void scg_comment_select		(SheetControlGUI *scg, CellComment *cc);
void scg_comment_display	(SheetControlGUI *scg, CellComment *cc);
void scg_comment_unselect	(SheetControlGUI *scg, CellComment *cc);

/* FIXME : Move these around to a mor ereasonable location */
SheetControlGUI *sheet_new_sheet_view (Sheet *sheet);
void       sheet_detach_sheet_view (SheetControlGUI *scg);

#endif /* GNUMERIC_SHEET_CONTROL_GUI_H */
