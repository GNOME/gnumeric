#ifndef GNUMERIC_SHEET_MERGED_H
#define GNUMERIC_SHEET_MERGED_H

#include "gnumeric.h"

gboolean     sheet_merge_add		(Sheet *sheet, GnmRange const *r,
					 gboolean clear, CommandContext *cc);
gboolean     sheet_merge_remove		(Sheet *sheet, GnmRange const *r,
					 CommandContext *cc);
GSList      *sheet_merge_get_overlap	(Sheet const *sheet, GnmRange const *r);
GnmRange const *sheet_merge_contains_pos	(Sheet const *sheet, GnmCellPos const *pos);
GnmRange const *sheet_merge_is_corner	(Sheet const *sheet, GnmCellPos const *pos);
void	     sheet_merge_relocate	(GnmExprRelocateInfo const *ri);
void	     sheet_merge_find_container	(Sheet const *sheet, GnmRange *r);
void	     sheet_merge_get_adjacent	(Sheet const *sheet, GnmCellPos const *pos,
					 GnmRange const **left, GnmRange const **right);

#endif /* GNUMERIC_SHEET_MERGED_H */
