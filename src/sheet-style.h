#ifndef GNUMERIC_SHEET_STYLE_H
#define GNUMERIC_SHEET_STYLE_H

#include "gnumeric.h"

struct _GnmStyleRegion {
	GnmRange    range; /* must be 1st */
	GnmStyle  *style;
};

struct _GnmRow {
	gboolean hide_grid;
	int row, start_col, end_col;
	GnmStyle      const **styles;
	GnmBorder const **top;
	GnmBorder const **bottom;
	GnmBorder const **vertical;
};

GnmStyle	*sheet_style_default		(Sheet const *sheet);
GnmStyle	*sheet_style_get		(Sheet const *sheet, int col, int row);
void	 sheet_style_get_row		(Sheet const *sheet, GnmRow *sr);
void	 sheet_style_apply_range	(Sheet *sheet, GnmRange const *r,
					 GnmStyle *style);
void	 sheet_style_apply_border	(Sheet *sheet, GnmRange const *r,
					 GnmBorder **borders);
void	 sheet_style_set_range		(Sheet  *sheet, GnmRange const *range,
					 GnmStyle *mstyle);
void	 sheet_style_set_pos		(Sheet  *sheet, int col, int row,
					 GnmStyle *mstyle);

void	 sheet_style_insert_colrow	(GnmExprRelocateInfo const *rinfo);
void	 sheet_style_relocate		(GnmExprRelocateInfo const *rinfo);
void	 sheet_style_get_uniform	(Sheet const *sheet, GnmRange const *r,
					 GnmStyle **style, GnmBorder **borders);
void	 sheet_style_get_extent		(Sheet const *sheet, GnmRange *r,
					 GnmStyle **most_common_in_cols);
gboolean sheet_style_has_visible_content(Sheet const *sheet, GnmRange *src);
void     style_row_init			(GnmBorder const * * *prev_vert,
					 GnmRow *sr, GnmRow *next_sr,
					 int start_col, int end_col,
					 gpointer mem, gboolean hide_grid);
GnmStyle  *sheet_style_most_common_in_col   (Sheet const *sheet, int col);
GnmHLink*sheet_style_region_contains_link (Sheet const *sheet, GnmRange const *r);
void	 sheet_style_foreach	   	(Sheet const *sheet,
					 GHFunc	    func,
					 gpointer    user_data);

void sheet_style_init     (Sheet *sheet);
void sheet_style_shutdown (Sheet *sheet);

void        sheet_style_set_auto_pattern_color (Sheet  *sheet,
						GnmColor *grid_color);
GnmColor *sheet_style_get_auto_pattern_color (Sheet const *sheet);
void        sheet_style_update_grid_color      (Sheet const *sheet);

GnmStyle const    *style_list_get_style	(GnmStyleList const *l, GnmCellPos const *pos);
void		 style_list_free	(GnmStyleList *l);
GnmStyleList	*sheet_style_get_list	(Sheet const *sheet, GnmRange const *r);
SpanCalcFlags	 sheet_style_set_list	(Sheet *sheet, GnmCellPos const *corner,
					 gboolean transpose, GnmStyleList const *l);

GnmStyleList *sheet_style_get_validation_list (Sheet const *sheet,
					    GnmRange const *r);

void	 sheet_style_unlink		(Sheet *sheet, GnmStyle *st);
GnmStyle *sheet_style_find   (Sheet *sheet, GnmStyle *st);

#endif /* GNUMERIC_SHEET_STYLE_H */
