#ifndef SHEET_H
#define SHEET_H

typedef GList ColStyleList;

struct Workbook;

typedef struct {
	GtkWidget  *toplevel;
	GtkWidget  *notebook;
	
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

	ColInfo    default_col_style;
	GList      *cols_info;

	RowInfo    default_row_style;
	GList      *rows_info;
	void       *contents;
} Sheet;

typedef  void (*sheet_col_callback)(Sheet *sheet, ColInfo *ci, void *user_data);
typedef  void (*sheet_row_callback)(Sheet *sheet, RowInfo *ci, void *user_data);

Sheet    *sheet_new                (Workbook *wb, char *name);
ColInfo  *sheet_get_col_info       (Sheet *, int col);
RowInfo  *sheet_get_row_info       (Sheet *, int row);
int       sheet_col_get_distance   (Sheet *sheet, int from_col, int to_col);
int       sheet_row_get_distance   (Sheet *sheet, int from_row, int to_row);
void      sheet_foreach_col        (Sheet *sheet, sheet_col_callback, void *user_data);
void      sheet_foreach_row        (Sheet *sheet, sheet_row_callback, void *user_data);
void      sheet_set_zoom_factor    (Sheet *sheet, double factor);
void      sheet_get_cell_bounds    (Sheet *sheet, ColType col, RowType row, int *x, int *y, int *w, int *h);

Workbook *workbook_new             (void);
Workbook *workbook_new_with_sheets (int sheet_count);
void      workbook_attach_sheet    (Workbook *, Sheet *);

/*
 * Callback routine: invoked when the first view ItemGrid
 * is realized to allocate the default styles
 */
void     workbook_realized         (Workbook *, GdkWindow *);

#endif
