#ifndef _GNM_SHEET_H_
# define _GNM_SHEET_H_

#include <gnumeric.h>
#include <colrow.h>
#include <position.h>
#include <pango/pango.h>
#include <goffice/goffice.h>
#include <libgnumeric.h>
#include <style-border.h>

G_BEGIN_DECLS

GNM_VAR_DECL Sheet *invalid_sheet;

GType gnm_sheet_size_get_type (void);

struct _ColRowCollection {
	int         max_used;
	ColRowInfo  default_style;
	GPtrArray * info;
	int	    max_outline_level;

	// Index into @info of last ColRowSegment with valid pixel_start
	int         last_valid_pixel_start;
};

typedef struct _SheetPrivate SheetPrivate;
GType gnm_sheet_type_get_type (void);
#define GNM_SHEET_TYPE_TYPE (gnm_sheet_type_get_type ())

GType gnm_sheet_visibility_get_type (void);
#define GNM_SHEET_VISIBILITY_TYPE (gnm_sheet_visibility_get_type ())

struct _Sheet {
	GObject	base;

	int         index_in_wb;
	Workbook    *workbook;
	gboolean    being_invalidated;
	gboolean    being_constructed;
	gboolean    being_destructed;

	GOUndoGroup *revive;

	GPtrArray   *sheet_views;

	char        *name_quoted;
	char        *name_unquoted;
	char        *name_unquoted_collate_key;
	char	    *name_case_insensitive;

	GnmSheetStyleData *style_data; /* See sheet-style.c */

	GnmSheetConditionsData *conditions; // See sheet-conditions.c

	ColRowCollection cols, rows;

	GHashTable  *cell_hash;	/* The cells in hashed format */

	GnmNamedExprCollection *names;

	/* This should eventually be moved to the views.  */
	double      last_zoom_factor_used;
	GnmRenderedValueCollection *rendered_values;

	GSList      *sheet_objects;	/* List of objects in this sheet */
	GnmCellPos   max_object_extent;

	/* Sheet level preferences */
	GnmConventions *convs;
	gboolean    display_formulas;
	gboolean    hide_zero;
	gboolean    hide_grid;
	gboolean    hide_col_header;
	gboolean    hide_row_header;
	gboolean    is_protected;
	struct {
		gboolean edit_objects;		/* TODO */
		gboolean edit_scenarios;	/* TODO */
		gboolean cell_formatting;	/* TODO */
		gboolean column_formatting;	/* TODO */
		gboolean row_formatting;	/* TODO */
		gboolean insert_columns;	/* TODO */
		gboolean insert_rows;		/* TODO */
		gboolean insert_hyperlinks;	/* TODO */
		gboolean delete_columns;	/* TODO */
		gboolean delete_rows;		/* TODO */
		gboolean select_locked_cells;	/* Partial : TODO range selection */
		gboolean sort_ranges;		/* TODO */
		gboolean edit_auto_filters;	/* TODO */
		gboolean edit_pivottable;	/* TODO */
		gboolean select_unlocked_cells;	/* Partial : TODO range selection */
	} protected_allow;

	GnmSheetVisibility visibility;

	gboolean    display_outlines;
	gboolean    outline_symbols_below;
	gboolean    outline_symbols_right;
	gboolean    text_is_rtl;

	gboolean    has_filtered_rows;

        GnmSolverParameters *solver_parameters;
	GList            *scenarios;
	GHashTable       *sort_setups;

	gint simulation_round;

	GnmDepContainer *deps;

	GArray *pending_redraw;
	guint pending_redraw_src;

	GSList		 *slicers;
	GSList		 *filters;
	GSList		 *list_merged;
	GHashTable	 *hash_merged;
	SheetPrivate     *priv;
	GnmPrintInformation *print_info;
	GnmColor	 *tab_color;
	GnmColor	 *tab_text_color;
	GnmSheetType	  sheet_type;

	GnmSheetSize size;

	/* tile related data */
	int tile_top_level;
};

#define GNM_SHEET_TYPE	(gnm_sheet_get_type ())
#define SHEET(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_SHEET_TYPE, Sheet))
#define IS_SHEET(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SHEET_TYPE))
#define GNM_SHEET_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SHEET_TYPE, GnmSheetClass))
GType     gnm_sheet_get_type	 (void);

Sheet    *sheet_new		 (Workbook *wb, char const *name, int columns, int rows);
Sheet    *sheet_new_with_type	 (Workbook *wb, char const *name,
				  GnmSheetType type, int columns, int rows);
Sheet    *sheet_dup		 (Sheet const *source_sheet);
void      sheet_destroy_contents (Sheet *sheet);

gboolean  gnm_sheet_valid_size   (int cols, int rows);
void      gnm_sheet_suggest_size (int *cols, int *rows);

GOUndo   *gnm_sheet_resize       (Sheet *sheet, int cols, int rows,
				  GOCmdContext *cc, gboolean *perr);

GnmSheetSize const *gnm_sheet_get_size (Sheet const *sheet);
GnmSheetSize const *gnm_sheet_get_size2 (Sheet const *sheet,
					 Workbook const *wb);
#define gnm_sheet_get_max_rows(sheet) (gnm_sheet_get_size(sheet)->max_rows)
#define gnm_sheet_get_max_cols(sheet) (gnm_sheet_get_size(sheet)->max_cols)
#define gnm_sheet_get_last_col(sheet) (gnm_sheet_get_max_cols(sheet) - 1)
#define gnm_sheet_get_last_row(sheet) (gnm_sheet_get_max_rows(sheet) - 1)

/* GnmCell management */
GnmCell  *sheet_cell_get	 (Sheet const *sheet, int col, int row);
GnmCell  *sheet_cell_fetch	 (Sheet *sheet, int col, int row);
GnmCell  *sheet_cell_create	 (Sheet *sheet, int col, int row);
void      sheet_cell_remove	 (Sheet *sheet, GnmCell *cell,
				  gboolean redraw, gboolean queue_recalc);
/* TODO TODO TODO
 * Merge with sheet_cell_foreach
 *
 **/
struct _GnmCellIter {
	GnmCell	    *cell;
	GnmParsePos  pp;
	ColRowInfo  *ci, *ri;
};
GnmValue *sheet_foreach_cell_in_region (Sheet *sheet, CellIterFlags flags,
				       int start_col, int start_row,
				       int end_col, int end_row,
				       CellIterFunc callback,
				       gpointer     closure);
GnmValue *sheet_foreach_cell_in_range (Sheet *sheet, CellIterFlags flags,
				       GnmRange const *r,
				       CellIterFunc callback,
				       gpointer     closure);
void	    sheet_cell_foreach	 (Sheet const *sheet,
				  GHFunc callback, gpointer data);
unsigned    sheet_cells_count	 (Sheet const *sheet);
GPtrArray  *sheet_cell_positions (Sheet *sheet, gboolean comments);

GPtrArray  *sheet_cells          (Sheet *sheet, const GnmRange *r);

void        sheet_recompute_spans_for_col     (Sheet *sheet, int col);
void        sheet_range_unrender (Sheet *sheet, GnmRange const *r);

gboolean    sheet_is_region_empty	      (Sheet *sheet, GnmRange const *r);
gboolean    sheet_is_cell_empty		      (Sheet *sheet, int col, int row);

gboolean    sheet_col_is_hidden		   (Sheet const *sheet, int col);
gboolean    sheet_row_is_hidden		   (Sheet const *sheet, int row);

GnmComment *sheet_get_comment		   (Sheet const *sheet,
					    GnmCellPos const *pos);

int	    sheet_find_boundary_horizontal (Sheet *sheet, int col, int move_row,
					    int base_row, int count,
					    gboolean jump_to_boundaries);
int	    sheet_find_boundary_vertical   (Sheet *sheet, int move_col, int row,
					    int base_col, int count,
					    gboolean jump_to_boundaries);

ColRowInfo const *sheet_colrow_get_default (Sheet const *sheet,
					    gboolean is_cols);

void        sheet_colrow_optimize          (Sheet *sheet);

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

void sheet_colrow_copy_info               (Sheet *sheet, int colrow,
					   gboolean is_cols, ColRowInfo const *cri);

gboolean          sheet_colrow_foreach	   (Sheet const *sheet,
					    gboolean is_cols,
					    int first, int last,
					    ColRowHandler callback,
					    gpointer user_data);

void gnm_sheet_mark_colrow_changed (Sheet *sheet, int colrow, gboolean is_cols);

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

gint64  sheet_colrow_get_distance_pixels (Sheet const *sheet, gboolean is_cols,
					  int from, int to);

/* Col width */
double  sheet_col_get_distance_pts	  (Sheet const *sheet, int from, int to);
gint64  sheet_col_get_distance_pixels     (Sheet const *sheet, int from, int to);
void    sheet_col_set_size_pts		  (Sheet *sheet, int col, double width_pts,
					   gboolean set_by_user);
void    sheet_col_set_size_pixels	  (Sheet *sheet, int col, int width_pixels,
					   gboolean set_by_user);
double  sheet_col_get_default_size_pts	  (Sheet const *sheet);
int     sheet_col_get_default_size_pixels (Sheet const *sheet);
void    sheet_col_set_default_size_pts	  (Sheet *sheet, double width_pts);
void    sheet_col_set_default_size_pixels (Sheet *sheet, int width_pixels);

/* Row height */
double  sheet_row_get_distance_pts	  (Sheet const *sheet, int from, int to);
gint64  sheet_row_get_distance_pixels     (Sheet const *sheet, int from, int to);
void    sheet_row_set_size_pts		  (Sheet *sheet, int row, double height_pts,
					   gboolean set_by_user);
void    sheet_row_set_size_pixels	  (Sheet *sheet, int row, int height_pixels,
					   gboolean set_by_user);
double  sheet_row_get_default_size_pts	  (Sheet const *sheet);
int     sheet_row_get_default_size_pixels (Sheet const *sheet);
void    sheet_row_set_default_size_pts	  (Sheet *sheet, double height_pts);
void    sheet_row_set_default_size_pixels (Sheet *sheet, int height_pixels);

/* Find minimum pixel size to display contents (including margins and far grid line) */
int     sheet_col_size_fit_pixels    (Sheet *sheet, int col,
				      int srow, int erow,
				      gboolean ignore_strings);
int     sheet_row_size_fit_pixels    (Sheet *sheet, int row,
				      int scol, int ecol,
				      gboolean ignore_strings);

gboolean sheet_colrow_can_group	     (Sheet *sheet, GnmRange const *r,
				      gboolean is_cols);
gboolean sheet_colrow_group_ungroup  (Sheet *sheet, GnmRange const *r,
				      gboolean is_cols, gboolean inc);
void     sheet_colrow_gutter	     (Sheet *sheet,
				      gboolean is_cols, int max_outline);

gboolean sheet_range_splits_array    (Sheet const *sheet,
				      GnmRange const *r, GnmRange const *ignore,
				      GOCmdContext *cc, char const *cmd);
gboolean sheet_range_splits_region   (Sheet const *sheet,
				      GnmRange const *r, GnmRange const *ignore,
				      GOCmdContext *cc, char const *cmd);
gboolean sheet_ranges_split_region   (Sheet const *sheet, GSList const *ranges,
				      GOCmdContext *cc, char const *cmd);
gboolean sheet_range_contains_merges_or_arrays (Sheet const *sheet,
						GnmRange const *r,
						GOCmdContext *cc,
						char const *cmd,
						gboolean merges,
						gboolean arrays);
void	 sheet_range_bounding_box    (Sheet const *sheet, GnmRange *r);
gboolean sheet_range_trim	     (Sheet const *sheet, GnmRange *r,
				      gboolean cols, gboolean rows);
gboolean sheet_range_has_heading     (Sheet const *sheet, GnmRange const *src,
				      gboolean top, gboolean ignore_styles);


void gnm_sheet_foreach_name (Sheet const *sheet, GHFunc func, gpointer data);

void gnm_sheet_set_solver_params (Sheet *sheet, GnmSolverParameters *param);

GnmScenario *gnm_sheet_scenario_new (Sheet *sheet, const char *name);
GnmScenario *gnm_sheet_scenario_find (Sheet *sheet, const char *name);
void gnm_sheet_scenario_add (Sheet *sheet, GnmScenario *sc);
void gnm_sheet_scenario_remove (Sheet *sheet, GnmScenario *sc);

GHashTable *gnm_sheet_get_sort_setups (Sheet *sheet);
void gnm_sheet_add_sort_setup (Sheet *sheet, char *key, gpointer setup);
gconstpointer gnm_sheet_find_sort_setup (Sheet *sheet, char const *key);

/* Redraw */
#define sheet_is_visible(_sheet) ((_sheet)->visibility == GNM_SHEET_VISIBILITY_VISIBLE)
void     sheet_redraw_all       (Sheet const *sheet, gboolean header);
void     sheet_redraw_range     (Sheet const *sheet, GnmRange const *range);
void     sheet_queue_redraw_range (Sheet *sheet, GnmRange const *range);
void     sheet_redraw_region    (Sheet const *sheet,
				 int start_col, int start_row,
				 int end_col,   int end_row);

void	 sheet_flag_status_update_cell	(GnmCell const *cell);
void	 sheet_flag_status_update_range	(Sheet const *sheet, GnmRange const *range);
void     sheet_flag_style_update_range	(Sheet const *sheet, GnmRange const *range);
void	 sheet_flag_recompute_spans	(Sheet const *sheet);
void	 sheet_update_only_grid		(Sheet const *sheet);
void     sheet_update                   (Sheet const *sheet);
void	 sheet_scrollbar_config		(Sheet const *sheet);

void     sheet_mark_dirty	(Sheet *sheet);
GnmRange    sheet_get_extent	(Sheet const *sheet,
				 gboolean spans_and_merges_extend,
				 gboolean include_hidden);
GnmRange    sheet_get_cells_extent (Sheet const *sheet);
GnmRange    sheet_get_printarea	(Sheet const *sheet,
				 gboolean include_styles,
				 gboolean ignore_printarea);
GnmRange    *sheet_get_nominal_printarea (Sheet const *sheet);

/*
 * Utilities to set cell contents, queueing recalcs,
 * redraws and rendering as required.  Does NOT check for
 * division of arrays.
 */
void	     sheet_cell_set_expr    (GnmCell *cell, GnmExprTop const *texpr);
void	     sheet_cell_set_value   (GnmCell *cell, GnmValue *v);
void	     sheet_cell_set_text    (GnmCell *cell, char const *str,
				     PangoAttrList *markup);
GnmValue const *sheet_cell_get_value(Sheet *sheet, int col, int row);
void	     sheet_range_set_text   (GnmParsePos const *pos,
				     GnmRange const *r, char const *str);
GOUndo *     sheet_range_set_text_undo (GnmSheetRange *sr,
					char const *text);
GOUndo *     sheet_range_set_expr_undo (GnmSheetRange *sr,
					GnmExprTop const  *texpr);
GOUndo *     sheet_range_set_markup_undo (GnmSheetRange *sr,
					  PangoAttrList *markup);
void	     sheet_apply_style	    (Sheet  *sheet, GnmRange const *range, GnmStyle *style);
GOUndo *     sheet_apply_style_undo (GnmSheetRange *sr,
				     GnmStyle      *style);
void	     sheet_apply_border	    (Sheet  *sheet, GnmRange const *range,
				     GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX]);
void	     sheet_queue_respan     (Sheet const *sheet, int start_row, int end_row);
void         sheet_cell_queue_respan (GnmCell *cell);
void	     sheet_range_calc_spans (Sheet *sheet, GnmRange const *r, GnmSpanCalcFlags flags);

void	     sheet_set_outline_direction (Sheet *sheet, gboolean is_cols);
gboolean     sheet_selection_is_allowed (Sheet const *sheet, GnmCellPos const *pos);

GnmConventions const *sheet_get_conventions (Sheet const *sheet);

gboolean  sheet_insert_cols (Sheet *sheet, int col, int count,
			     GOUndo **pundo, GOCmdContext *cc);
gboolean  sheet_delete_cols (Sheet *sheet, int col, int count,
			     GOUndo **pundo, GOCmdContext *cc);
gboolean  sheet_insert_rows (Sheet *sheet, int row, int count,
			     GOUndo **pundo, GOCmdContext *cc);
gboolean  sheet_delete_rows (Sheet *sheet, int row, int count,
			     GOUndo **pundo, GOCmdContext *cc);
void      sheet_move_range   (GnmExprRelocateInfo const *rinfo,
			      GOUndo **pundo, GOCmdContext *cc);

typedef enum {
	CLEAR_VALUES	   = 0x01,
	CLEAR_FORMATS	   = 0x02,
	CLEAR_COMMENTS	   = 0x04,
	CLEAR_NOCHECKARRAY = 0x08,
	CLEAR_NORESPAN	   = 0x10,
	CLEAR_RECALC_DEPS  = 0x20,
	CLEAR_MERGES	   = 0x40,
	CLEAR_OBJECTS	   = 0x80,
	CLEAR_FILTERED_ONLY = 0x100
} SheetClearFlags;

void  sheet_clear_region (Sheet *sheet,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  SheetClearFlags clear_flags, GOCmdContext *cc);

GOUndo *sheet_clear_region_undo (GnmSheetRange *sr,
				 int clear_flags);

SheetView *sheet_get_view (Sheet const *sheet, WorkbookView const *wbv);

void sheet_freeze_object_views (Sheet const *sheet, gboolean qfreeze);

GODateConventions const *sheet_date_conv (Sheet const *sheet);

// Introspection support
void sheet_cell_set_value_gi (Sheet *sheet, int col, int row, GnmValue *v);
void sheet_cell_set_text_gi  (Sheet *sheet, int col, int row, char const *str);
void sheet_apply_style_gi (Sheet *sheet, GnmRange const *range, GnmStyle *style);


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
	SHEET_FOREACH_VIEW((sheet), view,				\
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

G_END_DECLS

#endif /* _GNM_SHEET_H_ */
