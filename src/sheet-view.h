#ifndef SHEET_VIEW_H
#define SHEET_VIEW_H

#include <gtk/gtktable.h>

#define SHEET_VIEW_TYPE        (sheet_view_get_type ())
#define SHEET_VIEW(obj)        (GTK_CHECK_CAST((obj), SHEET_VIEW_TYPE, SheetView))
#define SHEET_VIEW_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), SHEET_VIEW_TYPE))
#define IS_SHEET_VIEW(o)       (GTK_CHECK_TYPE((o), SHEET_VIEW_TYPE))

typedef struct {
	GtkTable  table;

	Sheet            *sheet;
	GtkWidget        *sheet_view;
	GnomeCanvas      *col_canvas, *row_canvas;
	GnomeCanvasItem  *col_item, *row_item;

	/* Object group */
	GnomeCanvasGroup *object_group;

	/* Temporary object used during the creation of objects
	 * in the canvas
	 */
	void             *temp_item;
	
	/* Scrolling information */
	GtkWidget  *vs, *hs;	/* The scrollbars */
	GtkObject  *va, *ha;    /* The adjustments */

	/* Tip for scrolling */
	GtkWidget        *tip;
} SheetView;

GtkType     sheet_view_get_type              (void);
GtkWidget  *sheet_view_new                   (Sheet *sheet);

void        sheet_view_set_zoom_factor       (SheetView *sheet_view, double factor);

void        sheet_view_col_set_selection     (SheetView *sheet_view, ColRowInfo *ci);
void        sheet_view_row_set_selection     (SheetView *sheet_view, ColRowInfo *ci);

void        sheet_view_redraw_all            (SheetView *sheet_view);
void        sheet_view_redraw_cell_region    (SheetView *sheet_view,
					      int start_col, int start_row,
					      int end_col, int end_row);
void        sheet_view_redraw_rows           (SheetView *sheet_view);
void        sheet_view_redraw_columns        (SheetView *sheet_view);

typedef struct {
	GtkTableClass parent_class;
} SheetViewClass;

#endif /* SHEET_VIEW_H */
