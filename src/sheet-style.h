#ifndef GNUMERIC_SHEET_STYLE_H
#define GNUMERIC_SHEET_STYLE_H

#include "gnumeric.h"

struct _StyleRegion {
	Range    range; /* must be 1st */
	MStyle  *style;
};

struct _StyleRow {
	gboolean hide_grid;
	int row, start_col, end_col;
	MStyle      const **styles;
	StyleBorder const **top;
	StyleBorder const **bottom;
	StyleBorder const **vertical;
};

MStyle	*sheet_style_default		(Sheet const *sheet);
MStyle	*sheet_style_get		(Sheet const *sheet, int col, int row);
void	 sheet_style_get_row		(Sheet const *sheet, StyleRow *sr);
void	 sheet_style_apply_range	(Sheet *sheet, Range const *r,
					 MStyle *style);
void	 sheet_style_apply_border	(Sheet *sheet, Range const *r,
					 StyleBorder **borders);
void	 sheet_style_set_range		(Sheet  *sheet, Range const *range,
					 MStyle *mstyle);
void	 sheet_style_set_pos		(Sheet  *sheet, int col, int row,
					 MStyle *mstyle);

void	 sheet_style_insert_colrow	(GnmExprRelocateInfo const *rinfo);
void	 sheet_style_relocate		(GnmExprRelocateInfo const *rinfo);
void	 sheet_style_get_uniform	(Sheet const *sheet, Range const *r,
					 MStyle **style, StyleBorder **borders);
void	 sheet_style_get_extent		(Sheet const *sheet, Range *r);
gboolean sheet_style_has_visible_content(Sheet const *sheet, Range *src);
void     style_row_init			(StyleBorder const * * *prev_vert,
					 StyleRow *sr, StyleRow *next_sr,
					 int start_col, int end_col,
					 gpointer mem, gboolean hide_grid);
MStyle  *sheet_style_most_common_in_col   (Sheet const *sheet, int col);
GnmHLink*sheet_style_region_contains_link (Sheet const *sheet, Range const *r);
void	 sheet_style_foreach	   	(Sheet const *sheet,
					 GHFunc	    func,
					 gpointer    user_data);

void sheet_style_init     (Sheet *sheet);
void sheet_style_shutdown (Sheet *sheet);

void        sheet_style_set_auto_pattern_color (Sheet  *sheet,
						StyleColor *grid_color);
StyleColor *sheet_style_get_auto_pattern_color (Sheet const *sheet);
void        sheet_style_update_grid_color      (Sheet const *sheet);

MStyle const    *style_list_get_style	(StyleList const *l, CellPos const *pos);
void		 style_list_free	(StyleList *l);
StyleList	*sheet_style_get_list	(Sheet const *sheet, Range const *r);
SpanCalcFlags	 sheet_style_set_list	(Sheet *sheet, CellPos const *corner,
					 gboolean transpose, StyleList const *l);

/* For internal use only */
void	 sheet_style_unlink		(Sheet *sheet, MStyle *st);

#endif /* GNUMERIC_SHEET_STYLE_H */
