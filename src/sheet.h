#ifndef SHEET_H
#define SHEET_H

typedef GList ColStyleList;

struct Workbook;

typedef struct {
	RowType    row;
	int        height;
	Style      *style;		/* if existant, this row style */
} RowInfo;

typedef struct {
	ColType    col;
	int        width;
	Style      *style;		/* if existant, this column style */
} ColInfo;

typedef struct {
	GtkWidget  *toplevel;
	GtkWidget  *notebook;
	
	Style      style;
	GHashTable *sheets;	/* keeps a list of the Sheets on this workbook */
} Workbook;

typedef struct {
	Workbook   *parent_workbook;
	GtkWidget  *sheet_view;
	char       *name;
		   
	Style      style;

	ColInfo    default_col_style;
	GList      *cols_info;

	RowInfo    default_row_style;
	GList      *rows_info;
	void       *contents;
} Sheet;

Sheet    *sheet_new                (Workbook *wb, char *name);
ColInfo  *sheet_get_col_info       (Sheet *, int col);
RowInfo  *sheet_get_row_info       (Sheet *, int row);
int       sheet_col_get_distance   (Sheet *sheet, int from_col, int to_col);
int       sheet_row_get_distance   (Sheet *sheet, int from_row, int to_row);


Workbook *workbook_new             (void);
Workbook *workbook_new_with_sheets (int sheet_count);
void      workbook_attach_sheet    (Workbook *, Sheet *);

/*
 * Callback routine: invoked when the first view ItemGrid
 * is realized to allocate the default styles
 */
void     workbook_realized         (Workbook *, GdkWindow *);

#endif
