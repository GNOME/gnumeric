/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

#include "gnumeric.h"
#include "colrow.h"
#include <pango/pango.h>

typedef struct _SheetPrivate SheetPrivate;
typedef enum {
	GNM_SHEET_DATA,
	GNM_SHEET_OBJECT,
	GNM_SHEET_XLM
} GnmSheetType;

struct _Sheet {
	GObject	base; /* not really used yet */

	int         signature;

	int         index_in_wb;
	Workbook    *workbook;

	GPtrArray   *sheet_views;

	char        *name_quoted;
	char        *name_unquoted;
	char        *name_unquoted_collate_key;
	char	    *name_case_insensitive;

	SheetStyleData *style_data; /* See sheet-style.c */

	ColRowCollection cols, rows;

	GHashTable  *cell_hash;	/* The cells in hashed format */

	GnmNamedExprCollection *names;

	double      last_zoom_factor_used;

	GList       *sheet_objects;	/* List of objects in this sheet */
	GnmCellPos   max_object_extent;

	gboolean    pristine;
	gboolean    modified;

	/* Sheet level preferences */
	gboolean    r1c1_addresses;
	gboolean    display_formulas;
	gboolean    hide_zero;
	gboolean    hide_grid;
	gboolean    hide_col_header;
	gboolean    hide_row_header;
	gboolean    is_protected;
	gboolean    is_visible;

	gboolean    display_outlines;
	gboolean    outline_symbols_below;
	gboolean    outline_symbols_right;

	gboolean    has_filtered_rows;

        SolverParameters *solver_parameters;
	GList            *scenarios;

	gint simulation_round;

	GnmDepContainer *deps;

	GSList		 *filters;
	GSList		 *pivottables;
	GSList		 *list_merged;
	GHashTable	 *hash_merged;
	SheetPrivate     *priv;
	PrintInformation *print_info;
	GnmColor	 *tab_color;
	GnmColor	 *tab_text_color;
	GnmSheetType	  sheet_type;

	/* This needs to move elsewhere and get shared.  */
	PangoContext *context;
};

#define SHEET_SIGNATURE 0x12349876
#define IS_SHEET(x) (((x) != NULL) && ((x)->signature == SHEET_SIGNATURE))

Sheet    *sheet_new		 (Workbook *wb, char const *name);
Sheet    *sheet_new_with_type	 (Workbook *wb, char const *name,
				  GnmSheetType type);
Sheet    *sheet_dup		 (Sheet const *source_sheet);
void      sheet_destroy		 (Sheet *sheet);
void      sheet_destroy_contents (Sheet *sheet);
void      sheet_rename		 (Sheet *sheet, char const *new_name);
void	  sheet_set_tab_color	 (Sheet *sheet, GnmColor *tab_color,
				  GnmColor *text_color);

void      sheet_set_zoom_factor	 (Sheet *sheet, double factor,
				  gboolean force, gboolean respan);

void	  sheet_set_visibility	 (Sheet *sheet, gboolean visible);

/* GnmCell management */
GnmCell  *sheet_cell_get	 (Sheet const *sheet, int col, int row);
GnmCell  *sheet_cell_fetch	 (Sheet *sheet, int col, int row);
GnmCell  *sheet_cell_new	 (Sheet *sheet, int col, int row);
void      sheet_cell_remove	 (Sheet *sheet, GnmCell *cell, gboolean redraw);
GnmValue *sheet_foreach_cell_in_range	(Sheet *sheet, CellIterFlags flags,
				  int start_col, int start_row,
				  int end_col, int end_row,
				  CellIterFunc callback,
				  gpointer     closure);
void	    sheet_foreach_cell	 (Sheet *sheet, GHFunc callback, gpointer data);
GPtrArray  *sheet_cells          (Sheet *sheet,
				  int start_col, int start_row,
				  int end_col, int end_row,
				  gboolean comments);

void        sheet_recompute_spans_for_col     (Sheet *sheet, int col);

gboolean    sheet_is_region_empty 	      (Sheet *sheet, GnmRange const *r);
gboolean    sheet_is_cell_empty 	      (Sheet *sheet, int col, int row);

gboolean    sheet_col_is_hidden		   (Sheet const *sheet, int col);
gboolean    sheet_row_is_hidden		   (Sheet const *sheet, int row);

/* Create new ColRowInfos from the default sheet style */
ColRowInfo *sheet_col_new                  (Sheet *sheet);
ColRowInfo *sheet_row_new                  (Sheet *sheet);
int	    sheet_find_boundary_horizontal (Sheet *sheet, int col, int move_row,
					    int base_row, int count,
					    gboolean jump_to_boundaries);
int	    sheet_find_boundary_vertical   (Sheet *sheet, int move_col, int row,
					    int base_col, int count,
					    gboolean jump_to_boundaries);

ColRowInfo const *sheet_colrow_get_default (Sheet const *sheet,
					    gboolean is_cols);

/* Returns a pointer to a ColRowInfo: existing or NULL */
ColRowInfo *sheet_col_get                 (Sheet const *sheet, int col);
ColRowInfo *sheet_row_get                 (Sheet const *sheet, int row);
ColRowInfo *sheet_colrow_get              (Sheet const *sheet,
					   int colrow, gboolean is_cols);
/* Returns a pointer to a ColRowInfo: existing or freshly created */
ColRowInfo *sheet_col_fetch               (Sheet *sheet, int col);
ColRowInfo *sheet_row_fetch               (Sheet *sheet, int row);
ColRowInfo *sheet_colrow_fetch            (Sheet *sheet,
					   int colrow, gboolean is_cols);
/* Returns a pointer to a ColRowInfo: existing or default */
ColRowInfo const *sheet_col_get_info	  (Sheet const *sheet, int col);
ColRowInfo const *sheet_row_get_info	  (Sheet const *sheet, int row);
ColRowInfo const *sheet_colrow_get_info	  (Sheet const *sheet,
					   int colrow, gboolean is_cols);

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
int     sheet_col_size_fit_pixels    (Sheet *sheet, int col);
int     sheet_row_size_fit_pixels    (Sheet *sheet, int row);

gboolean sheet_colrow_can_group	     (Sheet *sheet, GnmRange const *r,
				      gboolean is_cols);
gboolean sheet_colrow_group_ungroup  (Sheet *sheet, GnmRange const *r,
				      gboolean is_cols, gboolean inc);
void     sheet_colrow_gutter 	     (Sheet *sheet,
				      gboolean is_cols, int max_outline);

gboolean sheet_range_splits_array    (Sheet const *sheet,
				      GnmRange const *r, GnmRange const *ignore,
				      GnmCmdContext *cc, char const *cmd);
gboolean sheet_range_splits_region   (Sheet const *sheet,
				      GnmRange const *r, GnmRange const *ignore,
				      GnmCmdContext *cc, char const *cmd);
gboolean sheet_ranges_split_region   (Sheet const *sheet, GSList const *ranges,
				      GnmCmdContext *cc, char const *cmd);
gboolean sheet_range_contains_region (Sheet const *sheet, GnmRange const *r,
				      GnmCmdContext *cc, char const *cmd);
void	 sheet_range_bounding_box    (Sheet const *sheet, GnmRange *r);

/* Redraw */
void     sheet_redraw_all       (Sheet const *sheet, gboolean header);
void     sheet_redraw_cell      (GnmCell const *cell);
void     sheet_redraw_range     (Sheet const *sheet, GnmRange const *r);
void     sheet_redraw_region    (Sheet const *sheet,
				 int start_col, int start_row,
				 int end_col,   int end_row);

void	 sheet_flag_status_update_cell	(GnmCell const *c);
void	 sheet_flag_status_update_range	(Sheet const *s, GnmRange const *r);
void     sheet_flag_format_update_range	(Sheet const *s, GnmRange const *r);
void	 sheet_flag_recompute_spans	(Sheet const *s);
void	 sheet_update_only_grid		(Sheet const *s);
void     sheet_update                   (Sheet const *s);
void	 sheet_scrollbar_config		(Sheet const *s);
void	 sheet_toggle_show_formula	(Sheet       *s);
void	 sheet_toggle_hide_zeros	(Sheet       *s);
void     sheet_adjust_preferences	(Sheet const *s,
					 gboolean redraw, gboolean resize);

void     sheet_set_dirty	(Sheet *sheet, gboolean is_dirty);
gboolean sheet_is_pristine	(Sheet const *sheet);
GnmRange    sheet_get_extent	(Sheet const *sheet,
				 gboolean spans_and_merges_extend);

char	*sheet_name_quote	(char const *unquoted_name);

/*
 * Utilities to set cell contents, queueing recalcs,
 * redraws and rendering as required.  Does NOT check for
 * division of arrays.
 */
void	     sheet_cell_set_expr    (GnmCell *cell, GnmExpr const *expr);
void	     sheet_cell_set_value   (GnmCell *cell, GnmValue *v);
void	     sheet_cell_set_text    (GnmCell *cell, char const *str,
				     PangoAttrList *markup);
GnmValue const *sheet_cell_get_value(Sheet *sheet, int col, int row);
void	     sheet_range_set_text   (GnmParsePos const *pos, GnmRange const *r, char const *str);
void	     sheet_apply_style	    (Sheet  *sheet, GnmRange const *range, GnmStyle *mstyle);
void	     sheet_queue_respan     (Sheet const *sheet, int start_row, int end_row);
void	     sheet_range_calc_spans (Sheet *sheet, GnmRange const *r, SpanCalcFlags flags);
void	     sheet_cell_calc_span   (GnmCell *cell, SpanCalcFlags flags);

void	     sheet_adjust_outline_dir (Sheet *sheet, gboolean is_cols);

/* Implementation for commands, no undo */
typedef struct {
	GSList *exprs;
} GnmRelocUndo;

gboolean  sheet_insert_cols (Sheet *sheet,
			     int col, int count, ColRowStateList *states,
			     GnmRelocUndo *reloc_storage, GnmCmdContext *cc);
gboolean  sheet_delete_cols (Sheet *sheet,
			     int col, int count, ColRowStateList *states,
			     GnmRelocUndo *reloc_storage, GnmCmdContext *cc);
gboolean  sheet_insert_rows (Sheet *sheet,
			     int row, int count, ColRowStateList *states,
			     GnmRelocUndo *reloc_storage, GnmCmdContext *cc);
gboolean  sheet_delete_rows (Sheet *sheet,
			     int row, int count, ColRowStateList *states,
			     GnmRelocUndo *reloc_storage, GnmCmdContext *cc);
void      sheet_move_range   (GnmExprRelocateInfo const *rinfo,
			      GnmRelocUndo *reloc_storage, GnmCmdContext *cc);


typedef enum {
	CLEAR_VALUES	   = 0x01,
	CLEAR_FORMATS	   = 0x02,
	CLEAR_COMMENTS	   = 0x04,
	CLEAR_NOCHECKARRAY = 0x08,
	CLEAR_NORESPAN	   = 0x10,
	CLEAR_RECALC_DEPS  = 0x20,
	CLEAR_MERGES	   = 0x40,
	CLEAR_OBJECTS	   = 0x80
} SheetClearFlags;

void  sheet_clear_region (Sheet *sheet,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  int clear_flags, GnmCmdContext *cc);

void	sheet_attach_view (Sheet *sheet, SheetView *sv);
void    sheet_detach_view (SheetView *sv);
SheetView *sheet_get_view (Sheet const *sheet, WorkbookView const *wbv);

#define SHEET_FOREACH_VIEW(sheet, view, code)					\
do {										\
	int InD;								\
	GPtrArray *views = (sheet)->sheet_views;				\
	if (views != NULL) /* Reverse is important during destruction */	\
		for (InD = views->len; InD-- > 0; ) {				\
			SheetView *view = g_ptr_array_index (views, InD);	\
			code							\
		}								\
} while (0)

#define SHEET_FOREACH_CONTROL(sheet, view, control, code)		\
	SHEET_FOREACH_VIEW((sheet), view, 				\
		SHEET_VIEW_FOREACH_CONTROL(view, control, code);)

/*
 * Walk the dependents.  WARNING: Note, that it is only valid to muck with
 * the current dependency in the code.
 */
#define SHEET_FOREACH_DEPENDENT(sheet, dep, code)					\
  do {											\
	/* Maybe external deps here.  */						\
											\
	if ((sheet)->deps) {								\
		DEPENDENT_CONTAINER_FOREACH_DEPENDENT ((sheet)->deps, dep, code);	\
	}										\
  } while (0)

#endif /* GNUMERIC_SHEET_H */
