#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

#include "item-grid.h"
#include "item-cursor.h"
#include "item-bar.h"
#include "item-edit.h"

#define GNUMERIC_TYPE_SHEET     (gnumeric_sheet_get_type ())
#define GNUMERIC_SHEET(obj)     (GTK_CHECK_CAST((obj), GNUMERIC_TYPE_SHEET, GnumericSheet))
#define GNUMERIC_SHEET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), GNUMERIC_TYPE_SHEET))
#define GNUMERIC_IS_SHEET(o)    (GTK_CHECK_TYPE((o), GNUMERIC_TYPE_SHEET))

typedef struct {
	GnomeCanvas canvas;

	GtkWidget   *entry;
	Sheet       *sheet;
	
	ColType     top_col, last_visible_col, last_full_col;
	RowType     top_row, last_visible_row, last_full_row;

	int         cursor_col, cursor_row;
	ItemGrid    *item_grid;
	ItemCursor  *item_cursor;
	ItemBar     *item_bar_col;
	ItemEdit    *item_editor;

	/* This flag keeps track of a cell selector
	 * (ie, when the user uses the cursor keys
	 * to select a cell for an expression).
	 */
	int         selecting_cell;
	int         sel_cursor_pos;
	int         sel_text_len;
	ItemCursor  *selection;
	ItemBar     *colbar;
	ItemBar     *rowbar;
} GnumericSheet;

GtkType    gnumeric_sheet_get_type               (void);

GtkWidget *gnumeric_sheet_new            	 (Sheet *sheet, ItemBar *colbar, ItemBar *rowbar);
void       gnumeric_sheet_set_selection  	 (GnumericSheet *sheet, SheetSelection *ss);
void       gnumeric_sheet_cursor_set     	 (GnumericSheet *sheet,
					 	  int col, int row);
void       gnumeric_sheet_load_cell_val          (GnumericSheet *gsheet);
void       gnumeric_sheet_accept_pending_output  (GnumericSheet *sheet);
void       gnumeric_sheet_compute_visible_ranges (GnumericSheet *gsheet);
void       gnumeric_sheet_set_current_value      (GnumericSheet *sheet);
void       gnumeric_sheet_color_alloc            (GnomeCanvas *canvas);
void       gnumeric_sheet_make_cell_visible      (GnumericSheet *gsheet,
						  int col, int row);

/* Colors used by any GnumericSheet item */
extern GdkColor gs_white, gs_light_gray, gs_dark_gray, gs_black;

typedef struct {
	GnomeCanvasClass parent_class;
} GnumericSheetClass;
#endif


