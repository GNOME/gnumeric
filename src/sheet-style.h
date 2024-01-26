#ifndef _GNM_SHEET_STYLE_H_
# define _GNM_SHEET_STYLE_H_

#include <gnumeric.h>
#include <style-border.h>

G_BEGIN_DECLS

struct _GnmStyleRegion {
	GnmRange  range; /* must be 1st */
	GnmStyle *style;
};

struct _GnmStyleRow {
	gboolean hide_grid;
	int row, start_col, end_col;
	Sheet const     *sheet;
	GnmStyle  const **styles;
	GnmBorder const **top;
	GnmBorder const **bottom;
	GnmBorder const **vertical;
};

GnmStyle *sheet_style_default		(Sheet const *sheet);
GnmStyle const *sheet_style_get		(Sheet const *sheet, int col, int row);
GnmStyle *sheet_style_find  		(Sheet const *sheet, GnmStyle *st);
void	 sheet_style_get_row		(Sheet const *sheet, GnmStyleRow *sr);
GnmStyle **sheet_style_get_row2		(Sheet const *sheet, int row);
void	 sheet_style_apply_border	(Sheet *sheet, GnmRange const *range,
					 GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX]);
void	 sheet_style_apply_range	(Sheet *sheet, GnmRange const *range,
					 GnmStyle *pstyle);
void	 sheet_style_apply_range2	(Sheet *sheet, GnmRange const *range,
					 GnmStyle *pstyle);
void	 sheet_style_set_range		(Sheet  *sheet, GnmRange const *range,
					 GnmStyle *style);
void	 sheet_style_apply_col		(Sheet  *sheet, int col,
					 GnmStyle *style);
void	 sheet_style_apply_row		(Sheet  *sheet, int row,
					 GnmStyle *style);
void	 sheet_style_set_pos		(Sheet  *sheet, int col, int row,
					 GnmStyle *style);
void	 sheet_style_apply_pos		(Sheet  *sheet, int col, int row,
					 GnmStyle *style);

void	 sheet_style_insdel_colrow	(GnmExprRelocateInfo const *rinfo);
void	 sheet_style_relocate		(GnmExprRelocateInfo const *rinfo);
unsigned int sheet_style_find_conflicts (Sheet const *sheet, GnmRange const *r,
					 GnmStyle **style,
					 GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX]);
void	 sheet_style_get_extent		(Sheet const *sheet, GnmRange *r);
void	 sheet_style_get_nondefault_extent (Sheet const *sheet, GnmRange *extent,
					    const GnmRange *src, GPtrArray *col_defaults);
GByteArray* sheet_style_get_nondefault_rows (Sheet const *sheet,
					     GPtrArray *col_defaults);

gboolean sheet_style_is_default         (Sheet const *sheet, const GnmRange *r, GPtrArray *col_defaults);
void     style_row_init			(GnmBorder const * * *prev_vert,
					 GnmStyleRow *sr, GnmStyleRow *next_sr,
					 int start_col, int end_col,
					 gpointer mem, gboolean hide_grid);
GnmHLink *sheet_style_region_contains_link (Sheet const *sheet, GnmRange const *r);
void	  sheet_style_foreach (Sheet const *sheet,
			       GFunc func,
			       gpointer user_data);
void	  sheet_style_range_foreach (Sheet const *sheet, GnmRange const *r,
				     GHFunc	  func,
				     gpointer     user_data);

GPtrArray *sheet_style_most_common (Sheet const *sheet, gboolean is_col);

void sheet_style_init     (Sheet *sheet);
void sheet_style_resize   (Sheet *sheet, int cols, int rows);
void sheet_style_shutdown (Sheet *sheet);

void      sheet_style_set_auto_pattern_color (Sheet  *sheet,
					      GnmColor *grid_color);
GnmColor *sheet_style_get_auto_pattern_color (Sheet const *sheet);
void      sheet_style_update_grid_color      (Sheet const *sheet, GtkStyleContext *context);

GnmStyle const    *style_list_get_style	 (GnmStyleList const *l, int col, int row);
void		   style_list_free	 (GnmStyleList *l);
GnmStyleList	  *sheet_style_get_range (Sheet const *sheet, GnmRange const *r);

typedef  gboolean (*sheet_style_set_list_cb_t) (GnmRange *range,
						Sheet const *sheet,
						gpointer data);
GnmSpanCalcFlags   sheet_style_set_list  (Sheet *sheet,
					  GnmCellPos const *corner,
					  GnmStyleList const *l,
					  sheet_style_set_list_cb_t range_modify,
					  gpointer data);

GnmStyleList *sheet_style_collect_conditions	(Sheet const *sheet,
						 GnmRange const *r);
GnmStyleList *sheet_style_collect_hlinks	(Sheet const *sheet,
						 GnmRange const *r);
GnmStyleList *sheet_style_collect_validations	(Sheet const *sheet,
						 GnmRange const *r);

GType gnm_style_region_get_type (void); /* boxed type */
GnmStyleRegion *gnm_style_region_new (GnmRange const *range, GnmStyle *style);
void gnm_style_region_free (GnmStyleRegion *sr);


/* For internal use only */
void	  sheet_style_unlink (Sheet *sheet, GnmStyle *st);

void      sheet_style_optimize (Sheet *sheet);

G_END_DECLS

#endif /* _GNM_SHEET_STYLE_H_ */
