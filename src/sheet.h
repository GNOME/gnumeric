/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

#include "gnumeric.h"
#include "colrow.h"
#include "solver.h"
#include <gtk/gtktypeutils.h>

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

	CellPos	 edit_pos;	/* Cell that would be edited */
	CellPos	 edit_pos_real;	/* Even in the middle of a merged cell */

	struct {
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

	/*
	 * Same as the above, contains the currently anted regions
	 * on the sheet
	 */
	GList       *ants;

	/* User defined names */
	GList      *names;

	double      last_zoom_factor_used;

	GList       *sheet_objects;	/* List of objects in this sheet */
	CellPos	     max_object_extent;

	gboolean    pristine;
	gboolean    modified;

	/* Sheet level preferences */
	gboolean    display_formulas;
	gboolean    hide_zero;
	gboolean    hide_grid;
	gboolean    hide_col_header;
	gboolean    hide_row_header;

	gboolean    display_outlines;
	gboolean    outline_symbols_below;
	gboolean    outline_symbols_right;

        /* Solver parameters */
        SolverParameters solver_parameters;

	DependencyData  *deps;

	GSList		 *list_merged;
	GHashTable	 *hash_merged;
	SheetPrivate     *priv;
	PrintInformation *print_info;

	/* place holders for new features */
	CellPos	    frozen_corner;
};

#define SHEET_SIGNATURE 0x12349876
#define IS_SHEET(x) (((x) != NULL) && ((x)->signature == SHEET_SIGNATURE))

Sheet      *sheet_new			(Workbook *wb, char const *name);
Sheet      *sheet_duplicate		(Sheet const *source_sheet);
void        sheet_destroy		(Sheet *sheet);
void        sheet_destroy_contents	(Sheet *sheet);
void	    sheet_freeze_panes		(Sheet *sheet, CellPos const *pos);
void        sheet_rename		(Sheet *sheet, char const *new_name);

void        sheet_set_zoom_factor	(Sheet *sheet, double factor,
					 gboolean force, gboolean respan);
void        sheet_cursor_set		(Sheet *sheet,
					 int edit_col, int edit_row,
					 int base_col, int base_row,
					 int move_col, int move_row,
					 Range const *cursor_bound);
void        sheet_set_edit_pos		(Sheet *sheet, int col, int row);
void	    sheet_update_cursor_pos	(Sheet const *sheet);
void        sheet_make_cell_visible	(Sheet *sheet, int col, int row);

/* Cell management */
Cell       *sheet_cell_get		(Sheet const *sheet, int col, int row);
Cell       *sheet_cell_fetch		(Sheet *sheet, int col, int row);
Cell       *sheet_cell_new		(Sheet *sheet, int col, int row);
void        sheet_cell_remove		(Sheet *sheet, Cell *cell, gboolean redraw);

/* Iteration utilities */
/* See also : workbook_foreach_cell_in_range */
Value      *sheet_foreach_cell_in_range	(Sheet *sheet, gboolean only_existing,
					 int start_col, int start_row,
					 int end_col, int end_row,
					 ForeachCellCB callback,
					 void *closure);
GPtrArray  *sheet_cells                  (Sheet *sheet,
					  int start_col, int start_row,
					  int end_col, int end_row,
					  gboolean comments);

void        sheet_recompute_spans_for_col     (Sheet *sheet, int col);

gboolean    sheet_is_region_empty_or_selected (Sheet *sheet, Range const *r);
gboolean    sheet_is_region_empty 	      (Sheet *sheet, Range const *r);
gboolean    sheet_is_cell_empty 	      (Sheet *sheet, int col, int row);

/* Create new ColRowInfos from the default sheet style */
ColRowInfo *sheet_col_new                  (Sheet *sheet);
ColRowInfo *sheet_row_new                  (Sheet *sheet);
int         sheet_row_check_bound          (int row, int diff);
int         sheet_col_check_bound          (int col, int diff);
int	    sheet_find_boundary_horizontal (Sheet *sheet, int col, int move_row,
					    int base_row, int count,
					    gboolean jump_to_boundaries);
int	    sheet_find_boundary_vertical   (Sheet *sheet, int move_col, int row,
					    int base_col, int count,
					    gboolean jump_to_boundaries);

/* Retrieve information from a col/row */
ColRowInfo *sheet_col_get_info            (Sheet const *sheet, int col);
ColRowInfo *sheet_row_get_info            (Sheet const *sheet, int row);

/* Returns a pointer to a ColRowInfo: existed or NULL */
ColRowInfo *sheet_col_get                 (Sheet const *sheet, int pos);
ColRowInfo *sheet_row_get                 (Sheet const *sheet, int pos);

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
int     sheet_col_size_fit_pixels	  (Sheet *sheet, int col);
int     sheet_row_size_fit_pixels	  (Sheet *sheet, int row);

void    sheet_col_row_set_outline_level	  (Sheet *sheet, int index,
					   gboolean is_cols, int outline,
					   gboolean is_collapsed);
void    sheet_col_row_gutter		  (Sheet *sheet,
					   int max_col_indent,
					   int max_row_indent);

gboolean sheet_range_splits_array    (Sheet const *sheet,
				      Range const *r, Range const *ignore,
				      WorkbookControl *wbc, char const *cmd);
gboolean sheet_range_splits_region   (Sheet const *sheet,
				      Range const *r, Range const *ignore,
				      WorkbookControl *wbc, char const *cmd);
gboolean sheet_ranges_split_region   (Sheet const *sheet,
				      GSList const *ranges,
				      WorkbookControl *wbc, char const *cmd);
gboolean sheet_range_contains_region (Sheet const *sheet, Range const *r,
				      WorkbookControl *wbc, char const *cmd);

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

void        sheet_unant                    (Sheet *sheet);
void        sheet_ant                      (Sheet *sheet, GList *ranges);

void	    sheet_flag_status_update_cell  (Cell const *c);
void	    sheet_flag_status_update_range (Sheet const *s, Range const *r);
void        sheet_flag_format_update_range (Sheet const *s, Range const *r);
void        sheet_flag_selection_change    (Sheet const *s);
void	    sheet_flag_recompute_spans	   (Sheet const *s);
void	    sheet_update_only_grid	   (Sheet const *s);
void        sheet_update                   (Sheet const *s);
void	    sheet_scrollbar_config	   (Sheet const *s);
void        sheet_adjust_preferences   	   (Sheet const *s,
					    gboolean redraw, gboolean resize);
void        sheet_menu_state_enable_insert (Sheet *s,
					    gboolean col, gboolean row);

void        sheet_set_dirty               (Sheet *sheet, gboolean is_dirty);
gboolean    sheet_is_pristine             (Sheet const *sheet);
Range       sheet_get_extent		  (Sheet const *sheet);

/* Sheet information manipulation */
void        sheet_move_range              (WorkbookControl *context,
					   ExprRelocateInfo const * rinfo,
					   GSList **reloc_storage);

char       *sheet_name_quote              (char const *unquoted_name);
Sheet      *sheet_lookup_by_name          (Workbook *wb, char const *name);

/*
 * Utilities to set cell contents, queueing recalcs,
 * redraws and rendering as required.  Does NOT check for
 * division of arrays.
 */
void sheet_cell_set_expr  (Cell *cell, ExprTree *expr);
void sheet_cell_set_value (Cell *cell, Value *v, StyleFormat *opt_fmt);
void sheet_cell_set_text  (Cell *cell, char const *str);
void sheet_range_set_text (EvalPos const *pos, Range const *r, char const *str);
void sheet_apply_style	  (Sheet  *sheet, Range const *range, MStyle *mstyle);
void sheet_calc_spans	    (Sheet const *sheet,	SpanCalcFlags flags);
void sheet_range_calc_spans (Sheet *sheet, Range r,	SpanCalcFlags flags);
void sheet_cell_calc_span   (Cell const *cell,		SpanCalcFlags flags);
void sheet_regen_adjacent_spans (Sheet *sheet,
			     int start_col, int start_row,
			     int end_col, int end_row,
			     int *min_col, int *max_col);
SpanCalcFlags required_updates_for_style (MStyle *style);

/* TODO : give this decent undo capabilities */
void sheet_adjust_outline_dir (Sheet *sheet, gboolean is_cols);

/* Implementation for commands, no undo */
gboolean  sheet_insert_cols (WorkbookControl *context, Sheet *sheet,
			     int col, int count, GSList **reloc_storage);
gboolean  sheet_delete_cols (WorkbookControl *context, Sheet *sheet,
			     int col, int count, GSList **reloc_storage);
gboolean  sheet_insert_rows (WorkbookControl *context, Sheet *sheet,
			     int row, int count, GSList **reloc_storage);
gboolean  sheet_delete_rows (WorkbookControl *context, Sheet *sheet,
			     int row, int count, GSList **reloc_storage);

typedef enum
{
	CLEAR_VALUES	   = 0x1,
	CLEAR_FORMATS	   = 0x2,
	CLEAR_COMMENTS	   = 0x4,
	CLEAR_NOCHECKARRAY = 0x8,
	CLEAR_NORESPAN	   = 0x10,
} SheetClearFlags;

void  sheet_clear_region (WorkbookControl *context,
			  Sheet *sheet,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  int clear_flags);

#define SHEET_FOREACH_CONTROL(sheet, control, code)			\
do {									\
	GList *PtR, *NexTPtR;						\
	for (PtR = (sheet)->s_controls; PtR != NULL ; PtR = NexTPtR) {	\
		SheetControlGUI *control = PtR->data;			\
		NexTPtR = PtR->next;					\
		code							\
	}								\
} while (0)

#endif /* GNUMERIC_SHEET_H */
