#ifndef _GNM_SHEET_MERGE_H_
# define _GNM_SHEET_MERGE_H_

#include <gnumeric.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

gboolean     gnm_sheet_merge_add		(Sheet *sheet,
						 GnmRange const *r,
						 gboolean clear,
						 GOCmdContext *cc);
gboolean     gnm_sheet_merge_remove		(Sheet *sheet,
						 GnmRange const *r);
GSList      *gnm_sheet_merge_get_overlap	(Sheet const *sheet, GnmRange const *r);
GnmRange const *gnm_sheet_merge_contains_pos	(Sheet const *sheet, GnmCellPos const *pos);
GnmRange const *gnm_sheet_merge_is_corner	(Sheet const *sheet, GnmCellPos const *pos);
void	     gnm_sheet_merge_relocate		(GnmExprRelocateInfo const *ri,
						 GOUndo **pundo);
void	     gnm_sheet_merge_find_bounding_box	(Sheet const *sheet,
						 GnmRange *r);
void	     gnm_sheet_merge_get_adjacent	(Sheet const *sheet,
						 GnmCellPos const *pos,
						 GnmRange const **left,
						 GnmRange const **right);

G_END_DECLS

#endif /* _GNM_SHEET_MERGE_H_ */
