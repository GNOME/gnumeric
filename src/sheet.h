#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

#include <glib.h>
#include <gtk/gtktypeutils.h>
#include "gnumeric.h"
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


typedef GList ColStyleList;

typedef struct _SheetPrivate SheetPrivate;

struct _Sheet {
	int         signature;

	Workbook    *workbook;
	GList       *s_controls;

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

	/* User defined names */
	GList      *names;

	double      last_zoom_factor_used;

	GList       *sheet_objects;	/* List of objects in this sheet */

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

	GSList		 *list_merged;
	GHashTable	 *hash_merged;
	SheetPrivate     *priv;
	PrintInformation *print_info;
};

#define SHEET_SIGNATURE 0x12349876
#define IS_SHEET(x) (((x) != NULL) && ((x)->signature == SHEET_SIGNATURE))

Sheet      *sheet_new			(Workbook *wb, const char *name);
Sheet      *sheet_duplicate		(Sheet const *source_sheet);
void        sheet_destroy		(Sheet *sheet);
void        sheet_destroy_contents	(Sheet *sheet);
void        sheet_rename		(Sheet *sheet, const char *new_name);

void        sheet_set_zoom_factor	(Sheet *sheet, double factor,
					 gboolean force, gboolean respan);
void        sheet_cursor_set		(Sheet *sheet,
					 int edit_col, int edit_row,
					 int base_col, int base_row,
					 int move_col, int move_row);
void	    sheet_update_cursor_pos	(Sheet const *sheet);
void        sheet_set_edit_pos		(Sheet *sheet, int col, int row);
void        sheet_make_cell_visible	(Sheet *sheet, int col, int row);

/* Cell management */
Cell       *sheet_cell_get		(Sheet const *sheet, int col, int row);
Cell       *sheet_cell_fetch		(Sheet *sheet, int col, int row);
Cell       *sheet_cell_new		(Sheet *sheet, int col, int row);
void        sheet_cell_insert		(Sheet *sheet, Cell *cell,
					 int col, int row, gboolean recalc_span);
void        sheet_cell_remove		(Sheet *sheet, Cell *cell, gboolean redraw);
void	    sheet_cell_remove_simple	(Sheet *sheet, Cell *cell);

/* Iteration utilities */
/* See also : workbook_foreach_cell_in_range */
Value      *sheet_cell_foreach_range	(Sheet *sheet, int only_existing,
					 int start_col, int start_row,
					 int end_col, int end_row,
					 ForeachCellCB callback,
					 void *closure);

void        sheet_recompute_spans_for_col     (Sheet *sheet, int col);

gboolean    sheet_is_region_empty_or_selected (Sheet *sheet, Range const *r);
gboolean    sheet_is_region_empty 	      (Sheet *sheet, Range const *r);
gboolean    sheet_is_cell_empty 	      (Sheet *sheet, int col, int row);

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
ColRowInfo *sheet_col_get                 (Sheet const *sheet, int const pos);
ColRowInfo *sheet_row_get                 (Sheet const *sheet, int const pos);

/* Returns a pointer to a ColRowInfo: existed or freshly created */
ColRowInfo *sheet_col_fetch               (Sheet *sheet, int pos);
ColRowInfo *sheet_row_fetch               (Sheet *sheet, int pos);

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
					   gboolean col, gboolean row,
					   Range const* r /* optional == NULL */);

void	    sheet_flag_status_update_cell (Cell const *cell);
void	    sheet_flag_status_update_range(Sheet const *sheet, Range const *range);
void        sheet_flag_format_update_range(Sheet const *sheet, Range const *range);
void        sheet_flag_selection_change   (Sheet const *sheet);
void	    sheet_update_only_grid	  (Sheet const *sheet);
void        sheet_update                  (Sheet const *sheet);
void        sheet_compute_visible_ranges  (Sheet const *sheet);

void        sheet_mark_clean              (Sheet *sheet);
void        sheet_set_dirty               (Sheet *sheet, gboolean is_dirty);
gboolean    sheet_is_pristine             (Sheet *sheet);

/* Sheet information manipulation */
void        sheet_move_range              (WorkbookControl *context,
					   ExprRelocateInfo const * rinfo,
					   GSList **reloc_storage);

char       *sheet_name_quote              (const char *unquoted_name);
Sheet      *sheet_lookup_by_name          (Workbook *wb, const char *name);

/* Utilities for various flavours of cursor */
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
void sheet_regen_adjacent_spans (Sheet *sheet,
			     int start_col, int start_row,
			     int end_col, int end_row,
			     int *min_col, int *max_col);
SpanCalcFlags required_updates_for_style (MStyle *style);

/*
 * Commands
 * These have undo/redo capabilities
 * and will route error messages to the caller appropriately.
 */
gboolean  sheet_insert_cols (WorkbookControl *context, Sheet *sheet,
			     int col, int count, GSList **reloc_storage);
gboolean  sheet_delete_cols (WorkbookControl *context, Sheet *sheet,
			     int col, int count, GSList **reloc_storage);
gboolean  sheet_insert_rows (WorkbookControl *context, Sheet *sheet,
			     int row, int count, GSList **reloc_storage);
gboolean  sheet_delete_rows (WorkbookControl *context, Sheet *sheet,
			     int row, int count, GSList **reloc_storage);

void sheet_adjust_preferences (Sheet const *sheet);

typedef enum
{
	CLEAR_VALUES   = 0x1,
	CLEAR_FORMATS  = 0x2,
	CLEAR_COMMENTS = 0x4,
	CLEAR_NOCHECKARRAY = 0x8,
} SheetClearFlags;

void  sheet_clear_region (WorkbookControl *context,
			  Sheet *sheet,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  int clear_flags);

gboolean     sheet_region_merge		(CommandContext *cc,
					 Sheet *sheet, Range const *r);
gboolean     sheet_region_unmerge	(CommandContext *cc,
					 Sheet *sheet, Range const *r);
GSList      *sheet_region_get_merged	(Sheet *sheet, Range const *r);
Range const *sheet_region_is_merge_cell (Sheet const *sheet, CellPos const *pos);
void	     sheet_region_adjacent_merge(Sheet const *sheet, CellPos const *pos,
					 Range const **left, Range const **right);

#define SHEET_FOREACH_CONTROL(sheet, control, code)			\
do {									\
	GList *PtR;							\
	for (PtR = (sheet)->s_controls; PtR != NULL ; PtR = PtR->next) {\
		SheetControlGUI *control = PtR->data;			\
		code							\
	}								\
} while (0)

#endif /* GNUMERIC_SHEET_H */
