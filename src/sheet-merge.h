#ifndef GNUMERIC_SHEET_MERGED_H
#define GNUMERIC_SHEET_MERGED_H

#include "gnumeric.h"

gboolean     sheet_merge_add		(Sheet *sheet, Range const *r,
					 gboolean clear, CommandContext *cc);
gboolean     sheet_merge_remove		(Sheet *sheet, Range const *r,
					 CommandContext *cc);
GSList      *sheet_merge_get_overlap	(Sheet const *sheet, Range const *r);
Range const *sheet_merge_contains_pos	(Sheet const *sheet, CellPos const *pos);
Range const *sheet_merge_is_corner	(Sheet const *sheet, CellPos const *pos);
void	     sheet_merge_relocate	(GnmExprRelocateInfo const *ri);
void	     sheet_merge_find_container	(Sheet const *sheet, Range *r);
void	     sheet_merge_get_adjacent	(Sheet const *sheet, CellPos const *pos,
					 Range const **left, Range const **right);

#endif /* GNUMERIC_SHEET_MERGED_H */
