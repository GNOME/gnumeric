#ifndef SHEET_H
#define SHEET_H

#define SHEET_MAX_ROWS (16 * 1024)
#define SHEET_MAX_COLS 256

typedef GList ColStyleList;

typedef struct {
	int start_col, start_row;
	int end_col, end_row;
} Range;

gboolean   range_contains (Range *range, int col, int row);

typedef struct {
	Range  range;
	Style  *style;
} StyleRegion;

typedef struct {
	GtkWidget  *toplevel;
	GtkWidget  *notebook;
	GtkWidget  *table;

	/* Edit area */
	GtkWidget  *ea_status;
	GtkWidget  *ea_button_box;
	GtkWidget  *ea_input;

	/* The auto-expression */
	ExprTree   *auto_expr;
	String     *auto_expr_desc;
	GnomeCanvasItem  *auto_expr_label;
	
	/* Styles */
	Style      style;

	/* The sheets */ 
	GHashTable *sheets;	/* keeps a list of the Sheets on this workbook */

	
	/* A list with all of the formulas */
	GList      *formula_cell_list;

	/* A queue with the cells to be evaluated */
	GList      *eval_queue;
	int        max_iterations;

	int        generation;
	
	/* The clipboard for this workbook */
	CellRegion *clipboard_contents;
} Workbook;

typedef struct {
	int        base_col, base_row;
	int        start_col, start_row;
	int        end_col, end_row;
} SheetSelection;

typedef struct {
	int        signature;
	
	Workbook   *workbook;
	GList      *sheet_views;
	
	char       *name;

	GList      *style_list;	/* The list of styles applied to the sheets */

	ColRowInfo default_col_style;
	GList      *cols_info;

	ColRowInfo default_row_style;
	GList      *rows_info;

	GHashTable *cell_hash;	/* The cells in hashed format */

	GList      *selections;

	int        max_col_used;
	int        max_row_used;

	int         cursor_col, cursor_row;
	
	/* The list of formulas */
	GList      *formula_cell_list;

	double     last_zoom_factor_used;

	/*
	 * When editing a cell: the cell (may be NULL) and
	 * the original text of the cell
	 */
	String    *editing_saved_text;
	Cell      *editing_cell;
	int        editing;
	
	/* For walking trough a selection */
	struct {
		SheetSelection *current;
	} walk_info;
} Sheet;

#define SHEET_SIGNATURE 0x12349876
#define IS_SHEET(x) (((Sheet *) x)->signature == SHEET_SIGNATURE)

typedef  void (*sheet_col_row_callback)(Sheet *sheet, ColRowInfo *ci,
					void *user_data);

typedef  int (*sheet_cell_foreach_callback)(Sheet *sheet, int col, int row,
					    Cell *cell, void *user_data);

Sheet      *sheet_new                  	 (Workbook *wb, char *name);
void        sheet_destroy              	 (Sheet *sheet);
void        sheet_foreach_col          	 (Sheet *sheet,
					  sheet_col_row_callback callback,
					  void *user_data);
void        sheet_foreach_row          	 (Sheet *sheet,
					  sheet_col_row_callback,
					  void *user_data);
void        sheet_set_zoom_factor      	 (Sheet *sheet, double factor);
void        sheet_cursor_set             (Sheet *sheet,
					  int start_col, int start_row,
					  int end_col,   int end_row);
void        sheet_cursor_move            (Sheet *sheet, int col, int row);

/* Selection management */
void        sheet_select_all             (Sheet *sheet);
void        sheet_selection_append       (Sheet *sheet, int col, int row);
void        sheet_selection_extend_to    (Sheet *sheet, int col, int row);
void        sheet_selection_reset        (Sheet *sheet);
void        sheet_selection_reset_only   (Sheet *sheet);
int         sheet_selection_equal        (SheetSelection *a, SheetSelection *b);
void        sheet_selection_append_range (Sheet *sheet,
					  int base_col,  int base_row,
					  int start_col, int start_row,
					  int end_col,   int end_row);
CellList   *sheet_selection_to_list      (Sheet *sheet);

/* Operations on the selection */
void        sheet_selection_clear_content     (Sheet *sheet);
void        sheet_selection_clear_formats     (Sheet *sheet);
void        sheet_selection_clear             (Sheet *sheet);

/* Cut/Copy/Paste on the workbook selection */
gboolean    sheet_selection_copy              (Sheet *sheet);
gboolean    sheet_selection_cut               (Sheet *sheet);
void        sheet_selection_paste             (Sheet *sheet,
					       int dest_col, int dest_row,
					       int paste_flags);
int         sheet_selection_walk_step         (Sheet *sheet,
					       int   forward,     int horizontal,
					       int   current_col, int current_row,
					       int   *new_col,    int *new_row);
void        sheet_selection_extend_horizontal (Sheet *sheet, int count);
void        sheet_selection_extend_vertical   (Sheet *sheet, int count);
int         sheet_selection_is_cell_selected  (Sheet *sheet, int col, int row);

gboolean    sheet_verify_selection_simple     (Sheet *sheet, char *command_name);

/* Cell management */
Cell       *sheet_cell_new                (Sheet *sheet, int col, int row);
void        sheet_cell_add                (Sheet *sheet, Cell *cell,
				           int col, int row);
void        sheet_cell_remove             (Sheet *sheet, Cell *cell);
int         sheet_cell_foreach_range      (Sheet *sheet, int only_existing,
				           int start_col, int start_row,
				           int end_col, int end_row,
				           sheet_cell_foreach_callback callback,
				           void *closure);
Cell       *sheet_cell_get                (Sheet *sheet, int col, int row);
void        sheet_cell_formula_link       (Cell *cell);
void        sheet_cell_formula_unlink     (Cell *cell);

/* Create new ColRowInfos from the default sheet style */
ColRowInfo *sheet_col_new                  (Sheet *sheet);
ColRowInfo *sheet_row_new                  (Sheet *sheet);
int         sheet_row_check_bound          (int row, int diff);
int         sheet_col_check_bound          (int col, int diff);

/* Duplicates the information of a col/row */
ColRowInfo *sheet_duplicate_colrow        (ColRowInfo *original);

/* Retrieve information from a col/row */
ColRowInfo *sheet_col_get_info            (Sheet *sheet, int col);
ColRowInfo *sheet_row_get_info            (Sheet *sheet, int row);

/* Returns a pointer to a ColRowInfo: existed or freshly created */
ColRowInfo *sheet_row_get                 (Sheet *sheet, int pos);
ColRowInfo *sheet_col_get                 (Sheet *sheet, int pos);

/* Add a ColRowInfo to the Sheet */
void        sheet_col_add                 (Sheet *sheet, ColRowInfo *cp);
void        sheet_row_add                 (Sheet *sheet, ColRowInfo *cp);

/* Measure distances in pixels from one col/row to another */
int         sheet_col_get_distance        (Sheet *sheet, int from_col, int to_col);
int         sheet_row_get_distance        (Sheet *sheet, int from_row, int to_row);
 
void        sheet_clear_region            (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row);
void        sheet_clear_region_formats    (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row);
void        sheet_clear_region_content    (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row);

/* Sets the width/height of a column row in terms of pixels */
void        sheet_col_set_width           (Sheet *sheet,
				           int col, int width);
void        sheet_row_set_height          (Sheet *sheet,
				           int row, int width,
				           gboolean height_set_by_user);
void        sheet_row_set_internal_height (Sheet *sheet, ColRowInfo *ri, int height);
void        sheet_col_set_selection       (Sheet *sheet,
					   ColRowInfo *ci, int value);
void        sheet_row_set_selection       (Sheet *sheet,
					   ColRowInfo *ri, int value);
				       
Style      *sheet_style_compute           (Sheet *sheet, int col, int row);

/* Redraw */
void        sheet_compute_visible_ranges  (Sheet *sheet);
void        sheet_redraw_cell_region      (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row);
void        sheet_redraw_selection        (Sheet *sheet, SheetSelection *ss);
void        sheet_redraw_all              (Sheet *sheet);
				       
void        sheet_update_auto_expr        (Sheet *sheet);

/* Sheet information manipulation */
void        sheet_insert_col              (Sheet *sheet,  int col, int count);
void        sheet_delete_col              (Sheet *sheet,  int col, int count);
void        sheet_insert_row              (Sheet *sheet,  int row, int count);
void        sheet_delete_row              (Sheet *sheet,  int row, int count);
void        sheet_shift_row               (Sheet *sheet,  int col, int row, int count);
void        sheet_shift_rows              (Sheet *sheet,  int col,
				           int start_row, int end_row, int count);
void        sheet_shift_col               (Sheet *sheet,  int col, int row, int count);
void        sheet_shift_cols              (Sheet *sheet,
				           int start_col, int end_col,
				           int row,       int count);

void        sheet_style_attach            (Sheet *sheet,
					   int    start_col, int start_row,
					   int    end_col,   int end_row,
					   Style  *style);

/*
 * Sheet visual editing
 */
void        sheet_start_editing_at_cursor (Sheet *sheet);
void        sheet_set_current_value       (Sheet *sheet);
void        sheet_accept_pending_output   (Sheet *sheet);
void        sheet_cancel_pending_input    (Sheet *sheet);
void        sheet_load_cell_val           (Sheet *sheet);
void        sheet_selection_col_extend_to (Sheet *sheet, int col);
void        sheet_selection_row_extend_to (Sheet *sheet, int row);

/*
 * Workbook
 */
Workbook   *workbook_new                 (void);
Workbook   *workbook_new_with_sheets     (int sheet_count);
void        workbook_attach_sheet        (Workbook *, Sheet *);
Sheet      *workbook_focus_current_sheet (Workbook *wb);
Sheet      *workbook_get_current_sheet   (Workbook *wb);
void        workbook_auto_expr_label_set (Workbook *wb, char *text);
void        workbook_next_generation     (Workbook *wb);
void        workbook_set_region_status   (Workbook *wb, char *str);
int         workbook_parse_and_jump      (Workbook *wb, char *text);

/*
 * Does any pending recalculations
 */
void        workbook_recalc              (Workbook *wb);
void        workbook_recalc_all          (Workbook *wb);

/*
 * Callback routine: invoked when the first view ItemGrid
 * is realized to allocate the default styles
 */
void     workbook_realized            (Workbook *, GdkWindow *);

extern   Workbook *current_workbook;
#endif
