#ifndef SHEET_H
#define SHEET_H

typedef GList ColStyleList;

struct Workbook;

typedef struct {
	GtkWidget  *toplevel;
	GtkWidget  *notebook;
	GtkWidget  *table;

	/* Edit area */
	GtkWidget  *ea_status;
	GtkWidget  *ea_button_box;
	GtkWidget  *ea_input;
	
	Style      style;
	GHashTable *sheets;	/* keeps a list of the Sheets on this workbook */
} Workbook;

typedef struct {
	Workbook   *parent_workbook;
	GtkWidget  *toplevel, *col_canvas, *row_canvas;
	GtkWidget  *sheet_view;
	GnomeCanvasItem *col_item, *row_item;
	
	double     last_zoom_factor_used;
	char       *name;
		   
	Style      style;

	ColRowInfo default_col_style;
	GList      *cols_info;

	ColRowInfo default_row_style;
	GList      *rows_info;
	void       *contents;
} Sheet;

typedef  void (*sheet_col_row_callback)(Sheet *sheet, ColRowInfo *ci, void *user_data);

Sheet      *sheet_new                (Workbook *wb, char *name);
void        sheet_foreach_col        (Sheet *sheet, sheet_col_row_callback, void *user_data);
void        sheet_foreach_row        (Sheet *sheet, sheet_col_row_callback, void *user_data);
void        sheet_set_zoom_factor    (Sheet *sheet, double factor);
void        sheet_get_cell_bounds    (Sheet *sheet, ColType col, RowType row, int *x, int *y, int *w, int *h);

/* Create new ColRowInfos from the default sheet style */
ColRowInfo *sheet_col_new            (Sheet *sheet);
ColRowInfo *sheet_row_new            (Sheet *sheet);

/* Duplicates the information of a col/row */
ColRowInfo *sheet_duplicate_colrow   (ColRowInfo *original);

/* Retrieve information from a col/row */
ColRowInfo *sheet_col_get_info       (Sheet *sheet, int col);
ColRowInfo *sheet_row_get_info       (Sheet *sheet, int row);

/* Add a ColRowInfo to the Sheet */
void        sheet_col_add            (Sheet *sheet, ColRowInfo *cp);
void        sheet_row_add            (Sheet *sheet, ColRowInfo *cp);

/* Measure distances in pixels from one col/row to another */
int         sheet_col_get_distance   (Sheet *sheet, int from_col, int to_col);
int         sheet_row_get_distance   (Sheet *sheet, int from_row, int to_row);

/* Sets the width/height of a column row in terms of pixels */
void        sheet_col_set_width      (Sheet *sheet, int col, int width);
void        sheet_row_set_height     (Sheet *sheet, int row, int width);

Workbook   *workbook_new             (void);
Workbook   *workbook_new_with_sheets (int sheet_count);
void        workbook_attach_sheet    (Workbook *, Sheet *);

/*
 * Callback routine: invoked when the first view ItemGrid
 * is realized to allocate the default styles
 */
void     workbook_realized         (Workbook *, GdkWindow *);

#endif
