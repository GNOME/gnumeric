#ifndef GNUMERIC_GNUMERIC_SHEET_H
#define GNUMERIC_GNUMERIC_SHEET_H

#include "item-grid.h"
#include "item-cursor.h"
#include "item-bar.h"
#include "item-edit.h"
#include "sheet.h"

#define GNUMERIC_TYPE_SHEET     (gnumeric_sheet_get_type ())
#define GNUMERIC_SHEET(obj)     (GTK_CHECK_CAST((obj), GNUMERIC_TYPE_SHEET, GnumericSheet))
#define GNUMERIC_SHEET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), GNUMERIC_TYPE_SHEET))
#define GNUMERIC_IS_SHEET(o)    (GTK_CHECK_TYPE((o), GNUMERIC_TYPE_SHEET))

typedef struct {
	GnomeCanvas   canvas;

	GtkWidget     *entry;
	SheetView     *sheet_view;
	
	int           top_col, last_visible_col, last_full_col;
	int           top_row, last_visible_row, last_full_row;

	ItemGrid      *item_grid;
	ItemCursor    *item_cursor;
	ItemBar       *item_bar_col;

	ItemEdit      *item_editor;

	SheetModeType mode;
	
	/*
	 * This flag keeps track of a cell selector
	 * (ie, when the user uses the cursor keys
	 * to select a cell for an expression).
	 */
	int           selecting_cell;
	int           sel_cursor_pos;
	int           sel_text_len;
	ItemCursor    *selection;

	/*
	 * The column and row bars
	 */
	ItemBar       *colbar;
	ItemBar       *rowbar;
} GnumericSheet;

GtkType    gnumeric_sheet_get_type               (void);

GtkWidget *gnumeric_sheet_new            	 (SheetView *sheet, ItemBar *colbar, ItemBar *rowbar);
void       gnumeric_sheet_set_selection  	 (GnumericSheet *gsheet, SheetSelection const *ss);
void       gnumeric_sheet_set_top_row            (GnumericSheet *gsheet, int new_top_row);
void       gnumeric_sheet_set_top_col            (GnumericSheet *gsheet, int new_top_col);
void       gnumeric_sheet_cursor_set     	 (GnumericSheet *gsheet,
					 	  int col, int row);
void       gnumeric_sheet_move_cursor            (GnumericSheet *gsheet,
						  int col, int row);
int        gnumeric_sheet_can_move_cursor        (GnumericSheet *gsheet);
void       gnumeric_sheet_set_cursor_bounds      (GnumericSheet *gsheet,
						  int start_col, int start_row,
						  int end_col,   int end_row);
void       gnumeric_sheet_compute_visible_ranges (GnumericSheet *gsheet);
void       gnumeric_sheet_make_cell_visible      (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_get_cell_bounds        (GnumericSheet *gsheet,
						  int col, int row,
						  int *x, int *y, int *w, int *h);
void       gnumeric_sheet_stop_cell_selection    (GnumericSheet *gsheet);
void       gnumeric_sheet_create_editing_cursor  (GnumericSheet *gsheet);
void       gnumeric_sheet_destroy_editing_cursor (GnumericSheet *gsheet);

/* Managing the selection of cell ranges when editing a formula */

void       gnumeric_sheet_start_cell_selection   (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_stop_cell_selection    (GnumericSheet *gsheet);
void       gnumeric_sheet_selection_cursor_place (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_selection_cursor_base  (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_selection_extend       (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_stop_editing           (GnumericSheet *sheet);

typedef struct {
	GnomeCanvasClass parent_class;
} GnumericSheetClass;

#endif /* GNUMERIC_GNUMERIC_SHEET_H */
