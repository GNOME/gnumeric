#ifndef GNUMERIC_SHEET_VIEW_H
#define GNUMERIC_SHEET_VIEW_H

#include "sheet.h"
#include <gtk/gtktable.h>
#include <gnome.h>

#define SHEET_VIEW_TYPE        (sheet_view_get_type ())
#define SHEET_VIEW(obj)        (GTK_CHECK_CAST((obj), SHEET_VIEW_TYPE, SheetView))
#define SHEET_VIEW_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), SHEET_VIEW_TYPE))
#define IS_SHEET_VIEW(o)       (GTK_CHECK_TYPE((o), SHEET_VIEW_TYPE))

struct _SheetView;
typedef struct _SheetView SheetView;

typedef gboolean (*SheetViewSlideHandler) (SheetView *sheet_view, int col, int row,
					   gpointer user_data);

struct _SheetView {
	GtkTable  table;

	Sheet            *sheet;
	GtkWidget        *sheet_view;
	GtkWidget	 *select_all_btn;
	GnomeCanvas      *col_canvas, *row_canvas;
	GnomeCanvasItem  *col_item, *row_item;

	/* Object group */
	GnomeCanvasGroup *object_group;

	/* Selection group */
	GnomeCanvasGroup *selection_group;
	
	/*
	 * Control points for the current item
	 */
	GnomeCanvasItem  *control_points [9];
	
	/* Scrolling information */
	GtkWidget  *vs, *hs;	/* The scrollbars */
	GtkObject  *va, *ha;    /* The adjustments */

	/* Tip for scrolling */
	GtkWidget        *tip;

	/* Anted cursor */
	GList            *anted_cursors;

	/* Sliding scroll */
	SheetViewSlideHandler	slide_handler;
	gpointer		slide_data;
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_col, sliding_row;
	int        sliding_x, sliding_y;
};

GtkType          sheet_view_get_type              (void);
GtkWidget       *sheet_view_new                   (Sheet *sheet);

void             sheet_view_set_zoom_factor       (SheetView *sheet_view,
						   double factor);

void             sheet_view_redraw_all            (SheetView *sheet_view);
void             sheet_view_redraw_cell_region    (SheetView *sheet_view,
						   int start_col, int start_row,
						   int end_col, int end_row);
void             sheet_view_redraw_headers        (SheetView *sheet_view,
						   gboolean const col, gboolean const row,
						   Range const * r /* optional == NULL */);

void             sheet_view_hide_cursor           (SheetView *sheet_view);
void             sheet_view_show_cursor           (SheetView *sheet_view);

GnomeCanvasItem *sheet_view_comment_create_marker (SheetView *sheet_view,
						   int col, int row);
void             sheet_view_comment_relocate      (SheetView *sheet_view,
						   int col, int row,
						   GnomeCanvasItem *o);
void             sheet_view_set_header_visibility (SheetView *sheet_view,
						   gboolean col_headers_visible,
						   gboolean row_headers_visible);

void             sheet_view_scrollbar_config      (SheetView const *sheet_view);

void             sheet_view_selection_ant         (SheetView *sheet_view);
void             sheet_view_selection_unant       (SheetView *sheet_view);

void             sheet_view_adjust_preferences    (SheetView *sheet_view);

void             sheet_view_update_cursor_pos	  (SheetView *sheet_view);

StyleFont *      sheet_view_get_style_font        (Sheet const *sheet,
						   MStyle const * const mstyle);

gboolean sheet_view_start_sliding (SheetView *sheet_view,
				   SheetViewSlideHandler slide_handler,
				   gpointer user_data,
				   int col, int row, int dx, int dy);
void sheet_view_stop_sliding (SheetView *sheet_view);

typedef struct {
	GtkTableClass parent_class;
} SheetViewClass;

/*
 * These actually belong in sheet.h, but the structure dependency
 * forces me to put them here
 */
SheetView *sheet_new_sheet_view     (Sheet *sheet);
void       sheet_destroy_sheet_view (Sheet *sheet, SheetView *sheet_view);

#endif /* GNUMERIC_SHEET_VIEW_H */
