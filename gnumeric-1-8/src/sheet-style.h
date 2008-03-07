/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_STYLE_H_
# define _GNM_SHEET_STYLE_H_

#include "gnumeric.h"

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
GnmStyle *sheet_style_find   		(Sheet const *sheet, GnmStyle *st);
void	 sheet_style_get_row		(Sheet const *sheet, GnmStyleRow *sr);
void	 sheet_style_apply_border	(Sheet *sheet, GnmRange const *r,
					 GnmBorder **borders);
void	 sheet_style_apply_range	(Sheet *sheet, GnmRange const *r,
					 GnmStyle *style);
void	 sheet_style_set_range		(Sheet  *sheet, GnmRange const *range,
					 GnmStyle *style);
void	 sheet_style_set_col		(Sheet  *sheet, int col,
					 GnmStyle *style);
void	 sheet_style_apply_col		(Sheet  *sheet, int col,
					 GnmStyle *style);
void	 sheet_style_set_row		(Sheet  *sheet, int row,
					 GnmStyle *style);
void	 sheet_style_apply_row		(Sheet  *sheet, int row,
					 GnmStyle *style);
void	 sheet_style_set_pos		(Sheet  *sheet, int col, int row,
					 GnmStyle *style);
void	 sheet_style_apply_pos		(Sheet  *sheet, int col, int row,
					 GnmStyle *style);

void	 sheet_style_insert_colrow	(GnmExprRelocateInfo const *rinfo);
void	 sheet_style_relocate		(GnmExprRelocateInfo const *rinfo);
unsigned int sheet_style_find_conflicts (Sheet const *sheet, GnmRange const *r,
					 GnmStyle **style, GnmBorder **borders);
void	 sheet_style_get_extent		(Sheet const *sheet, GnmRange *r,
					 GnmStyle **most_common_in_cols);
gboolean sheet_style_has_visible_content(Sheet const *sheet, GnmRange *src);
void     style_row_init			(GnmBorder const * * *prev_vert,
					 GnmStyleRow *sr, GnmStyleRow *next_sr,
					 int start_col, int end_col,
					 gpointer mem, gboolean hide_grid);
GnmStyle *sheet_style_most_common_in_col   (Sheet const *sheet, int col);
GnmHLink *sheet_style_region_contains_link (Sheet const *sheet, GnmRange const *r);
void	  sheet_style_foreach (Sheet const *sheet,
			       GHFunc	    func,
			       gpointer    user_data);

void sheet_style_init     (Sheet *sheet);
void sheet_style_shutdown (Sheet *sheet);

void      sheet_style_set_auto_pattern_color (Sheet  *sheet,
					      GnmColor *grid_color);
GnmColor *sheet_style_get_auto_pattern_color (Sheet const *sheet);
void      sheet_style_update_grid_color      (Sheet const *sheet);

GnmStyle const    *style_list_get_style	(GnmStyleList const *l, int col, int row);
void		 style_list_free	(GnmStyleList *l);
GnmStyleList	*sheet_style_get_list	(Sheet const *sheet, GnmRange const *r);
GnmSpanCalcFlags	 sheet_style_set_list	(Sheet *sheet, GnmCellPos const *corner,
					 gboolean transpose, GnmStyleList const *l);

GnmStyleList *sheet_style_collect_conditions	(Sheet const *s, GnmRange const *r);
GnmStyleList *sheet_style_collect_hlinks	(Sheet const *s, GnmRange const *r);
GnmStyleList *sheet_style_collect_validations	(Sheet const *s, GnmRange const *r);

/* For internal use only */
void	  sheet_style_unlink (Sheet *sheet, GnmStyle *st);

G_END_DECLS

#endif /* _GNM_SHEET_STYLE_H_ */
