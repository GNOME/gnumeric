#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

#include <glib.h>

typedef struct _Workbook	Workbook;
typedef struct _Sheet		Sheet;

/* Used to locate cells in a sheet */
typedef struct {
	int col, row;
} CellPos;

typedef struct {
	CellPos start, end;
} Range;

typedef struct {
	int dummy;
} MStyle;

typedef struct {
	/* TODO : Remove this.  It should be part of the sheet cursor
	 * data structures */
        CellPos base;

	/* This range may overlap other regions in the selection list */
        Range user;
} SheetSelection;

typedef struct {
	Sheet *sheet;
	int   col, row;

	unsigned char col_relative;
	unsigned char row_relative;
} CellRef;

typedef struct {
	Sheet *sheet;
	int    eval_col;
	int    eval_row;
} EvalPosition;

typedef struct {
	Workbook *wb;
	int       col;
	int       row;
} ParsePosition;

#ifdef ENABLE_BONOBO
#    include <bonobo/gnome-container.h>
#endif

#include "value.h"
#include "solver.h"
#include "mstyle.h"
#include "style.h"
#include "expr.h"
#include "str.h"
#include "symbol.h"
#include "cell.h"
#include "summary.h"
#include "workbook.h"

#define SHEET_MAX_ROWS (16 * 1024)
#define SHEET_MAX_COLS 256	/* 0 - 255 inclusive */

typedef GList ColStyleList;

typedef struct {
	Range  range;
	MStyle  *style;
} StyleRegion;

typedef enum {
	/* Normal editing mode of the Sheet */
	SHEET_MODE_SHEET,

	/* Drawing object creation */
	SHEET_MODE_CREATE_LINE,
	SHEET_MODE_CREATE_BOX,
	SHEET_MODE_CREATE_OVAL,
	SHEET_MODE_CREATE_ARROW,

	/* Selection for the region for a Graphics object */
	SHEET_MODE_CREATE_GRAPHIC,
	SHEET_MODE_CREATE_CANVAS_ITEM,

	SHEET_MODE_CREATE_BUTTON,
	SHEET_MODE_CREATE_CHECKBOX,
	
	/* Object is selected */
	SHEET_MODE_OBJECT_SELECTED,
} SheetModeType;

struct _Sheet {
	int         signature;
	
	Workbook    *workbook;
	GList       *sheet_views;
	
	char        *name;

	GList       *style_list;	/* The list of styles applied to the sheets */

	ColRowInfo  default_col_style;
	GList       *cols_info;

	ColRowCollection cols, rows;

	GHashTable  *cell_hash;	/* The cells in hashed format */

	GList       *selections;

	/* Cursor information */
	/* TODO switch to CellPos */
	int         cursor_col, cursor_row; /* Where the cursor is */
	SheetSelection *cursor_selection;
	/* TODO : seperate cursor handling from selection */
/*	CellPos  selection_corner;*/	/* A corner of the current selection */
	
	/* The list of cells that have a comment */
	GList       *comment_list;
	
	/* User defined names */
	GList      *names;
	
	double      last_zoom_factor_used;

	/*
	 * When editing a cell: the cell (may be NULL) and
	 * the original text of the cell
	 */
	String      *editing_saved_text;
	Cell        *editing_cell;
	int         editing;

	/* Objects */
	SheetModeType mode;	/* Sheet mode */
	void        *mode_data; /* Sheet per-mode data */
	
	GList       *objects;	/* List of objects in the spreadsheet */
	GList       *coords;	/* During creation time: keeps click coordinates */
	void        *current_object;
	void        *active_object_frame;
	
	
	gboolean    modified;
	
        /* Solver parameters */
        SolverParameters solver_parameters;

	GHashTable *dependency_hash;

	void         *corba_server;

	PrintInformation *print_info;
};

#define SHEET_SIGNATURE 0x12349876
#define IS_SHEET(x) ((x)->signature == SHEET_SIGNATURE)

typedef  gboolean (*sheet_col_row_callback)(Sheet *sheet, ColRowInfo *info,
					    void *user_data);

typedef  Value * (*sheet_cell_foreach_callback)(Sheet *sheet, int col, int row,
						Cell *cell, void *user_data);

Sheet      *sheet_new                  	 (Workbook *wb, const char *name);
void        sheet_rename                 (Sheet *sheet, const char *new_name);
void        sheet_destroy              	 (Sheet *sheet);
void        sheet_destroy_contents       (Sheet *sheet);
void        sheet_foreach_colrow	 (Sheet *sheet, ColRowCollection *infos,
					  int start_col, int end_col,
					  sheet_col_row_callback callback,
					  void *user_data);
void        sheet_set_zoom_factor      	 (Sheet *sheet, double factor);
void        sheet_cursor_set             (Sheet *sheet,
					  int base_col,  int base_row,
					  int start_col, int start_row,
					  int end_col,   int end_row);
void        sheet_cursor_move            (Sheet *sheet, int col, int row,
					  gboolean clear_selection, gboolean add_dest_to_selection);
void        sheet_make_cell_visible      (Sheet *sheet, int col, int row);

/* Cell management */
Cell       *sheet_cell_new                (Sheet *sheet, int col, int row);
void        sheet_cell_add                (Sheet *sheet, Cell *cell,
				           int col, int row);
void        sheet_cell_remove             (Sheet *sheet, Cell *cell);
Value      *sheet_cell_foreach_range      (Sheet *sheet, int only_existing,
				           int start_col, int start_row,
				           int end_col, int end_row,
				           sheet_cell_foreach_callback callback,
				           void *closure);
 /* Returns NULL if doesn't exist */
Cell       *sheet_cell_get                (Sheet const *sheet, int col, int row);
 /* Returns new Cell if doesn't exist */
Cell       *sheet_cell_fetch              (Sheet *sheet, int col, int row);
void        sheet_cell_comment_link       (Cell *cell);
void        sheet_cell_comment_unlink     (Cell *cell);

void        sheet_cell_formula_link       (Cell *cell);
void        sheet_cell_formula_unlink     (Cell *cell);
gboolean    sheet_is_region_empty_or_selected (Sheet *sheet, int start_col, int start_row,
					       int end_col, int end_row);

/* Create new ColRowInfos from the default sheet style */
ColRowInfo *sheet_col_new                  (Sheet *sheet);
ColRowInfo *sheet_row_new                  (Sheet *sheet);
int         sheet_row_check_bound          (int row, int diff);
int         sheet_col_check_bound          (int col, int diff);
int	    sheet_find_boundary_horizontal (Sheet *sheet, int start_col, int row,
					    int count, gboolean jump_to_boundaries);
int	    sheet_find_boundary_vertical   (Sheet *sheet, int col, int start_row,
					    int count, gboolean jump_to_boundaries);

/* Duplicates the information of a col/row */
ColRowInfo *sheet_duplicate_colrow        (ColRowInfo *original);

/* Retrieve information from a col/row */
ColRowInfo *sheet_col_get_info            (Sheet const *sheet, int const col);
ColRowInfo *sheet_row_get_info            (Sheet const *sheet, int const row);

/* Returns a pointer to a ColRowInfo: existed or NULL */
ColRowInfo *sheet_row_get                 (Sheet const *sheet, int const pos);
ColRowInfo *sheet_col_get                 (Sheet const *sheet, int const pos);

/* Returns a pointer to a ColRowInfo: existed or freshly created */
ColRowInfo *sheet_row_fetch               (Sheet *sheet, int pos);
ColRowInfo *sheet_col_fetch               (Sheet *sheet, int pos);

/* Add a ColRowInfo to the Sheet */
void        sheet_col_add                 (Sheet *sheet, ColRowInfo *cp);
void        sheet_row_add                 (Sheet *sheet, ColRowInfo *cp);

/* Measure distances in pixels from one col/row to another */
int         sheet_col_get_distance        (Sheet const *sheet, int from_col, int to_col);
int         sheet_row_get_distance        (Sheet const *sheet, int from_row, int to_row);
double      sheet_row_get_unit_distance   (Sheet const *sheet, int from_row, int to_row);
double      sheet_col_get_unit_distance   (Sheet const *sheet, int from_col, int to_col);
 
void        sheet_clear_region            (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row,
					   void *closure);
void        sheet_clear_region_formats    (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row,
					   void *closure);
void        sheet_clear_region_content    (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row,
					   void *closure);
void        sheet_clear_region_comments   (Sheet *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row,
					   void *closure);	

/* Sets the width/height of a column row in terms of pixels */
void        sheet_col_set_width           (Sheet *sheet,
				           int col, int width);
void        sheet_col_info_set_width      (Sheet *sheet,
				           ColRowInfo *ci, int width);
void        sheet_col_set_width_units     (Sheet *sheet, int col, double width);
void        sheet_col_set_internal_width  (Sheet *sheet, ColRowInfo *ci,
					   double width);

void        sheet_row_set_height          (Sheet *sheet,
				           int row, int height,
				           gboolean height_set_by_user);
void        sheet_row_set_height_units    (Sheet *sheet, int row, double height,
					   gboolean height_set_by_user);
void        sheet_row_info_set_height     (Sheet *sheet,
				           ColRowInfo *ri, int height,
				           gboolean height_set_by_user);
void        sheet_row_set_internal_height (Sheet *sheet, ColRowInfo *ri, double height);

int         sheet_col_size_fit            (Sheet *sheet, int col);
int         sheet_row_size_fit            (Sheet *sheet, int row);

void        sheet_col_set_selection       (Sheet *sheet,
					   ColRowInfo *ci, int value);
void        sheet_row_set_selection       (Sheet *sheet,
					   ColRowInfo *ri, int value);
void        sheet_set_selection           (Sheet *sheet, SheetSelection const *ss);
				       
Style         *sheet_style_compute            (Sheet const *sheet,
					       int col, int row);
Style         *sheet_style_compute_blank      (Sheet const *sheet,
					       int col, int row);
void           sheet_selection_apply_style    (Sheet *sheet, MStyle *style);
MStyleElement *sheet_selection_get_uniq_style (Sheet *sheet);

/* Redraw */
void        sheet_compute_visible_ranges  (Sheet const *sheet);
void        sheet_redraw_cell_region      (Sheet const *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row);
void	    sheet_redraw_cols		  (Sheet const *sheet);
void	    sheet_redraw_rows		  (Sheet const *sheet);
void        sheet_redraw_selection        (Sheet const *sheet, SheetSelection const *ss);
void        sheet_redraw_all              (Sheet const *sheet);
				       
void        sheet_update_auto_expr        (Sheet *sheet);

/*
 * A Pair of utilities to temporarily suspend auto_expr's while reading
 * worksheets.
 */
void        sheet_suspend_auto_expr       (Workbook *wb,
					   ExprTree **expr, String **desc);
void        sheet_resume_auto_expr        (Workbook *wb,
					   ExprTree *expr, String *desc);

void        sheet_mark_clean              (Sheet *sheet);
void        sheet_set_dirty               (Sheet *sheet, gboolean is_dirty);
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

void        sheet_style_attach            (Sheet  *sheet, Range   range,
					   MStyle  *style);
void        sheet_style_attach_old        (Sheet *sheet,
					   int    start_col, int start_row,
					   int    end_col,   int end_row,
					   MStyle  *style);
Sheet      *sheet_lookup_by_name          (Workbook *wb, const char *name);

void        sheet_update_controls         (Sheet *sheet);
/*
 * Sheet visual editing
 */
void        sheet_start_editing_at_cursor (Sheet *sheet, gboolean blankp, gboolean cursorp);
void        sheet_set_current_value       (Sheet *sheet);
void        sheet_accept_pending_input    (Sheet *sheet);
void        sheet_cancel_pending_input    (Sheet *sheet);
void        sheet_load_cell_val           (Sheet *sheet);

int         sheet_col_selection_type      (Sheet const *sheet, int col);
int         sheet_row_selection_type      (Sheet const *sheet, int row);

/*
 * Event state manipulation (for mode operation)
 */
void        sheet_set_mode_type           (Sheet *sheet, SheetModeType type);


/*
 * Callback routines.
 */
void        sheet_fill_selection_with     (Sheet *sheet, const char *text,
					   gboolean const is_array);

/*
 * Hiding/showing the cursor
 */
void        sheet_show_cursor             (Sheet *sheet);
void        sheet_hide_cursor             (Sheet *sheet);

char        *cellref_name                 (CellRef *cell_ref,
					   int eval_col,
					   int eval_row);
gboolean     cellref_get                  (CellRef *out, const char *in,
					   int parse_col, int parse_row);
gboolean     cellref_a1_get               (CellRef *out, const char *in,
					   int parse_col, int parse_row);
gboolean     cellref_r1c1_get             (CellRef *out, const char *in,
					   int parse_col, int parse_row);

/*
 * Sheet, Bobobo objects
 */
void sheet_insert_object (Sheet *sheet, char *repoid);

/*
 * Hooks for CORBA bootstrap: they create the 
 */
void sheet_corba_setup       (Sheet *);
void sheet_corba_shutdown    (Sheet *);
#endif /* GNUMERIC_SHEET_H */
