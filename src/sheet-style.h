#ifndef GNUMERIC_SHEET_STYLE_H
#define GNUMERIC_SHEET_STYLE_H

#include "gnumeric.h"

struct _StyleRegion {
	Range    range; /* must be 1st */
	guint32  stamp;
	MStyle  *style;
};

MStyle        *sheet_style_compute              (const Sheet *sheet,
						 int col, int row);
MStyle	      *sheet_style_compute_from_list	(GList *list,
						 int col, int row);
void           sheet_style_attach               (Sheet  *sheet, Range const *range,
						 MStyle *mstyle);
void           sheet_style_attach_single        (Sheet  *sheet, int col, int row,
						 MStyle *mstyle);
void           sheet_style_optimize             (Sheet *sheet, Range range);
void           sheet_style_insert_colrow        (Sheet *sheet, int pos, int count,
						 gboolean is_col);
void           sheet_style_delete_colrow        (Sheet *sheet, int pos, int count,
						 gboolean is_col);
void           sheet_style_relocate             (const ExprRelocateInfo *rinfo);
void           sheet_style_apply_range          (Sheet *sheet, const Range *r,
						 MStyle *style);
void           sheet_range_set_border           (Sheet *sheet, const Range *r,
						 StyleBorder **borders);
MStyle        *sheet_selection_get_unique_style (Sheet *sheet,
						 StyleBorder **borders);
void           sheet_create_styles              (Sheet *sheet);
void           sheet_destroy_styles             (Sheet *sheet);
GList         *sheet_get_style_list             (Sheet const *sheet);
void           sheet_styles_dump                (Sheet *sheet);
void           sheet_style_get_extent           (Sheet const *sheet, Range *r);
GList         *sheet_get_styles_in_range        (Sheet *sheet, const Range *r);
void           sheet_style_list_destroy         (GList *l);
SpanCalcFlags  sheet_style_attach_list          (Sheet *sheet, const GList *l,
						 const CellPos *corner, gboolean transpose);

#endif /* GNUMERIC_SHEET_STYLE_H */
