#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

#include <glib.h>
#include "gnumeric.h"
#ifdef ENABLE_BONOBO
#    include <bonobo/bonobo-container.h>
#endif
#include "colrow.h"
#include "solver.h"

struct _StyleRegion {
	Range    range; /* must be 1st */
	guint32  stamp;
	MStyle  *style;
};

struct _SheetSelection {
	/* This range may overlap other regions in the selection list */
        Range user;
};

#define SHEET_MAX_ROWS (64 * 1024)
#define SHEET_MAX_COLS 256	/* 0 - 255 inclusive */

/* The size, mask, and shift must be kept in sync */
#define COLROW_SEGMENT_SIZE	0x80
#define COLROW_SUB_INDEX(i)	((i) & 0x7f)
#define COLROW_SEGMENT_START(i)	((i) & ~(0x7f))
#define COLROW_SEGMENT_END(i)	((i) | 0x7f)
#define COLROW_SEGMENT_INDEX(i)	((i) >> 7)
#define COLROW_GET_SEGMENT(seg_array, i) \
	(g_ptr_array_index ((seg_array)->info, COLROW_SEGMENT_INDEX(i)))


typedef GList ColStyleList;

typedef struct _SheetPrivate SheetPrivate;

struct _Sheet {
	int         signature;

	Workbook    *workbook;
	GList       *sheet_views;

	char        *name_quoted;
	char        *name_unquoted;

	SheetStyleData *style_data; /* See sheet-style.c */

	ColRowCollection cols, rows;

	GHashTable  *cell_hash;	/* The cells in hashed format */

	struct {
		/* Cell that would be edited */
		CellPos	 edit_pos;
		/* Static corner to rubber band the selecton range around */
		CellPos	 base_corner;
		/* Corner that is moved when the selection range is extended */
		CellPos	 move_corner;
	} cursor;

	/*
	 * an ordered list of SheetSelections, the first of
	 * which corresponds to the range base_corner:move_corner
	 */
	GList       *selections;

	/* The list of cells that have a comment */
	GList       *comment_list;

	/* User defined names */
	GList      *names;

	double      last_zoom_factor_used;

	/* Objects */
	GList       *sheet_objects;	/* List of objects in this sheet */
	SheetObject *new_object;	/* A newly created object that has yet to be realized */
	SheetObject *current_object;
	void        *active_object_frame;

	gboolean    pristine;
	gboolean    modified;

	/* Sheet level preferences */
	gboolean    display_formulas;
	gboolean    display_zero;
	gboolean    show_grid;
	gboolean    show_col_header;
	gboolean    show_row_header;

        /* Solver parameters */
        SolverParameters solver_parameters;

	DependencyData  *deps;

	SheetPrivate     *priv;
	PrintInformation *print_info;
};

#define SHEET_SIGNATURE 0x12349876
#define IS_SHEET(x) ((x)->signature == SHEET_SIGNATURE)

Sheet      *sheet_new                  	 (Workbook *wb, const char *name);
Sheet      *sheet_duplicate		 (Sheet const *source_sheet);
void        sheet_destroy              	 (Sheet *sheet);
void        sheet_destroy_contents       (Sheet *sheet);
void        sheet_rename                 (Sheet *sheet, const char *new_name);

void        sheet_set_zoom_factor      	 (Sheet *sheet, double factor,
					  gboolean force, gboolean respan);
void        sheet_cursor_set             (Sheet *sheet,
					  int edit_col, int edit_row,
					  int base_col, int base_row,
					  int move_col, int move_row);
void	    sheet_update_cursor_pos	 (Sheet const *sheet);
void        sheet_set_edit_pos           (Sheet *sheet, int col, int row);
void        sheet_make_cell_visible      (Sheet *sheet, int col, int row);

/* Object Management */
void        sheet_mode_edit		 (Sheet *sheet);
void        sheet_mode_edit_object	 (SheetObject *so);
void        sheet_mode_create_object	 (SheetObject *so);

/* Cell management */
Cell       *sheet_cell_get               (Sheet const *sheet, int col, int row);
Cell       *sheet_cell_fetch             (Sheet *sheet, int col, int row);
Cell       *sheet_cell_new               (Sheet *sheet, int col, int row);
void        sheet_cell_insert            (Sheet *sheet, Cell *cell,
				          int col, int row, gboolean recalc_span);
void        sheet_cell_remove            (Sheet *sheet, Cell *cell, gboolean redraw);
void	    sheet_cell_remove_simple	 (Sheet *sheet, Cell *cell);

/* Iteration utilities */
/* See also : workbook_foreach_cell_in_range */
Value      *sheet_cell_foreach_range     (Sheet *sheet, int only_existing,
				          int start_col, int start_row,
				          int end_col, int end_row,
					  ForeachCellCB callback,
				          void *closure);

void        sheet_cell_comment_link      (Cell *cell);
void        sheet_cell_comment_unlink    (Cell *cell);

void        sheet_recompute_spans_for_col     (Sheet *sheet, int col);

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

/*
 * Definitions of row/col size terminology :
 *
 * _pixels == measurments are in screen pixels.
 * _pts == measurments are in 'points' and should be the same size on all displays
 *         (printers and monitors).
 *
 * distance == pixels from the leading edge of the 'from' col/row
 *             to the leading edge of the 'to' col/row
 *             INCLUDING all internal margins.
 *             INCLUDING the leading grid line
 *             EXCLUDING the trailing grid line.
 *
 * _default == The size of all cols/rows that do not have explicit sizes.
 */
/* Col width */
int     sheet_col_get_distance_pixels	  (Sheet const *sheet, int from_col, int to_col);
double  sheet_col_get_distance_pts	  (Sheet const *sheet, int from_col, int to_col);
void    sheet_col_set_size_pts		  (Sheet *sheet, int col, double width_pts,
					   gboolean set_by_user);
void    sheet_col_set_size_pixels	  (Sheet *sheet, int col, int width_pixels,
					   gboolean set_by_user);
double  sheet_col_get_default_size_pts	  (Sheet const *sheet);
int     sheet_col_get_default_size_pixels (Sheet const *sheet);
void    sheet_col_set_default_size_pts	  (Sheet *sheet, double width_pts);
void    sheet_col_set_default_size_pixels (Sheet *sheet, int width_pixels);

/* Row height */
int     sheet_row_get_distance_pixels	  (Sheet const *sheet, int from_row, int to_row);
double  sheet_row_get_distance_pts	  (Sheet const *sheet, int from_row, int to_row);

void    sheet_row_set_size_pts		  (Sheet *sheet, int row, double height_pts,
					   gboolean set_by_user);
void    sheet_row_set_size_pixels	  (Sheet *sheet, int row, int height_pixels,
					   gboolean set_by_user);
double  sheet_row_get_default_size_pts	  (Sheet const *sheet);
int     sheet_row_get_default_size_pixels (Sheet const *sheet);
void    sheet_row_set_default_size_pts	  (Sheet *sheet, double height_pts);
void    sheet_row_set_default_size_pixels (Sheet *sheet, int height_pixels);

/* Find minimum pixel size to display contents (including margins and far grid line) */
int     sheet_col_size_fit_pixels     (Sheet *sheet, int col);
int     sheet_row_size_fit_pixels     (Sheet *sheet, int row);

/* sheet-style.c */
MStyle        *sheet_style_compute              (const Sheet *sheet,
						 int col, int row);
MStyle	      *sheet_style_compute_from_list	(GList *list,
						 int col, int row);
void           sheet_style_attach               (Sheet  *sheet, Range   range,
						 MStyle *mstyle);
void           sheet_style_attach_single        (Sheet  *sheet, int col, int row,
						 MStyle *mstyle);
void           sheet_style_optimize             (Sheet *sheet, Range range);
void           sheet_style_insert_colrow        (Sheet *sheet, int pos, int count,
						 gboolean is_col);
void           sheet_style_delete_colrow        (Sheet *sheet, int pos, int count,
						 gboolean is_col);
void           sheet_style_relocate             (const ExprRelocateInfo *rinfo);
void           sheet_range_apply_style          (Sheet *sheet, const Range *r,
						 MStyle *style);
void           sheet_range_set_border           (Sheet *sheet, const Range *r,
						 MStyleBorder **borders);
MStyle        *sheet_selection_get_unique_style (Sheet *sheet,
						 MStyleBorder **borders);
void           sheet_create_styles              (Sheet *sheet);
void           sheet_destroy_styles             (Sheet *sheet);
GList         *sheet_get_style_list             (Sheet const *sheet);
void           sheet_styles_dump                (Sheet *sheet);
Range          sheet_get_full_range             (void);
void           sheet_style_get_extent           (Range *r, const Sheet *sheet);
Range          sheet_get_extent                 (const Sheet *sheet);

GList         *sheet_get_styles_in_range        (Sheet *sheet, const Range *r);
void           sheet_style_list_destroy         (GList *l);
SpanCalcFlags  sheet_style_attach_list          (Sheet *sheet, const GList *l,
						 const CellPos *corner, gboolean transpose);

gboolean       sheet_range_splits_array   (Sheet const *sheet, Range const *r);

/* Redraw */
void        sheet_redraw_all              (Sheet const *sheet);
void        sheet_redraw_cell             (Cell const *r);
void        sheet_redraw_range            (Sheet const *sheet, Range const *r);
void        sheet_redraw_cell_region      (Sheet const *sheet,
				           int start_col, int start_row,
				           int end_col,   int end_row);
void	    sheet_redraw_headers          (Sheet const *sheet,
					   gboolean const col, gboolean const row,
					   Range const * r /* optional == NULL */);

void	    sheet_flag_status_update_cell (Cell const *cell);
void	    sheet_flag_status_update_range(Sheet const *sheet, Range const *range);
void        sheet_flag_selection_change   (Sheet const *sheet);
void        sheet_update                  (Sheet const *sheet);
void        sheet_compute_visible_ranges  (Sheet const *sheet);

void        sheet_update_auto_expr        (Sheet const *sheet);

void        sheet_mark_clean              (Sheet *sheet);
void        sheet_set_dirty               (Sheet *sheet, gboolean is_dirty);
gboolean    sheet_is_pristine             (Sheet *sheet);

/* Sheet information manipulation */
void        sheet_move_range              (CommandContext *context,
					   ExprRelocateInfo const * rinfo,
					   GSList **reloc_storage);

char       *sheet_name_quote              (const char *unquoted_name);
Sheet      *sheet_lookup_by_name          (Workbook *wb, const char *name);

/* Utilities for various flavours of cursor */
void        sheet_show_cursor                (Sheet *sheet);
void        sheet_hide_cursor                (Sheet *sheet);
void        sheet_create_edit_cursor         (Sheet *sheet);
void        sheet_stop_editing               (Sheet *sheet);
void        sheet_destroy_cell_select_cursor (Sheet *sheet, gboolean clear_string);

/*
 * Utilities to set cell contents, queueing recalcs,
 * redraws and rendering as required.  Does NOT check for
 * division of arrays.
 */
void sheet_cell_set_expr  (Cell *cell, ExprTree *expr);
void sheet_cell_set_value (Cell *cell, Value *v, StyleFormat *opt_fmt);
void sheet_cell_set_text  (Cell *cell, char const *str);
void sheet_range_set_text (EvalPos const *pos, Range const *r, char const *str);

void sheet_calc_spans	    (Sheet const *sheet,	SpanCalcFlags flags);
void sheet_range_calc_spans (Sheet *sheet, Range r,	SpanCalcFlags flags);
void sheet_cell_calc_span   (Cell const *cell,		SpanCalcFlags flags);
SpanCalcFlags required_updates_for_style (MStyle *style);

/*
 * Hooks for CORBA bootstrap: they create the
 */
void sheet_corba_setup       (Sheet *);
void sheet_corba_shutdown    (Sheet *);

/*
 * Commands
 * These have undo/redo capabilities
 * and will route error messages to the caller appropriately.
 */
gboolean  sheet_insert_cols (CommandContext *context, Sheet *sheet,
			     int col, int count, GSList **reloc_storage);
gboolean  sheet_delete_cols (CommandContext *context, Sheet *sheet,
			     int col, int count, GSList **reloc_storage);
gboolean  sheet_insert_rows (CommandContext *context, Sheet *sheet,
			     int row, int count, GSList **reloc_storage);
gboolean  sheet_delete_rows (CommandContext *context, Sheet *sheet,
			     int row, int count, GSList **reloc_storage);

void sheet_adjust_preferences (Sheet const *sheet);

typedef enum
{
	CLEAR_VALUES   = 0x1,
	CLEAR_FORMATS  = 0x2,
	CLEAR_COMMENTS = 0x4,
	CLEAR_NOCHECKARRAY = 0x8,
} SheetClearFlags;

void  sheet_clear_region (CommandContext *context,
			  Sheet *sheet,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  int clear_flags);

#endif /* GNUMERIC_SHEET_H */
