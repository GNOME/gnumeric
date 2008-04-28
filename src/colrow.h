/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COLROW_H_
# define _GNM_COLROW_H_

#include "gnumeric.h"

G_BEGIN_DECLS

struct _ColRowInfo {
	/* Size including margins, and right grid line */
	float	 size_pts;
	int      size_pixels;

	unsigned  is_default	: 1;
	unsigned  outline_level : 4;
	unsigned  is_collapsed  : 1;	/* Does this terminate an outline ? */
	unsigned  hard_size     : 1;	/* are dimensions explicitly set ? */
	unsigned  visible       : 1;	/* visible */
	unsigned  in_filter     : 1;	/* in a filter */
	unsigned  needs_respan  : 1;	/* mark a row as needing span generation */

	/* TODO : Add per row/col min/max */

	gpointer spans;	/* Only used for rows */
};

struct _ColRowCollection {
	int         max_used;
	ColRowInfo  default_style;
	GPtrArray * info;
	int	    max_outline_level;
};

/* We never did get around to support 'thick' borders so these are effectively
 * unitless (margins do not scale) constants . */
#define	GNM_COL_MARGIN	2
#define	GNM_ROW_MARGIN	0

/* The size, mask, and shift must be kept in sync */
#define COLROW_SEGMENT_SIZE	0x80
#define COLROW_SUB_INDEX(i)	((i) & 0x7f)
#define COLROW_SEGMENT_START(i)	((i) & ~(0x7f))
#define COLROW_SEGMENT_END(i)	((i) | 0x7f)
#define COLROW_SEGMENT_INDEX(i)	((i) >> 7)
#define COLROW_GET_SEGMENT(seg_array, i) \
	(g_ptr_array_index ((seg_array)->info, COLROW_SEGMENT_INDEX(i)))

struct _ColRowSegment {
	ColRowInfo *info [COLROW_SEGMENT_SIZE];
	float	size_pts;
	int	size_pixels;
};
typedef struct _ColRowState {
	float     size_pts;
	unsigned  is_default	: 1;
	unsigned  outline_level : 4;
	unsigned  is_collapsed  : 1;	/* Does this terminate an outline ? */
	unsigned  hard_size     : 1;	/* are dimensions explicitly set ? */
	unsigned  visible       : 1;	/* visible */
} ColRowState;

typedef struct {
	int         length;
	ColRowState state;
} ColRowRLEState;

#define COL_INTERNAL_WIDTH(col)	\
	((col)->size_pixels - (GNM_COL_MARGIN + GNM_COL_MARGIN + 1))

void	colrow_compute_pixels_from_pts (ColRowInfo *cri, Sheet const *sheet,
					gboolean horizontal);
void	colrow_compute_pts_from_pixels (ColRowInfo *cri, Sheet const *sheet,
					gboolean horizontal);

gboolean colrow_is_default (ColRowInfo const *cri);
gboolean colrow_is_empty   (ColRowInfo const *cri);
gboolean colrow_equal	   (ColRowInfo const *a, ColRowInfo const *b);
void     colrow_copy	   (ColRowInfo *dst, ColRowInfo const *src);
#define  colrow_free	   g_free

typedef struct {
	int	pos;
	ColRowInfo const *cri;
} GnmColRowIter;

typedef gboolean (*ColRowHandler)(GnmColRowIter const *iter, gpointer user_data);
gboolean colrow_foreach	   (ColRowCollection const *infos,
			    int first, int last,
			    ColRowHandler callback,
			    gpointer user_data);

ColRowIndexList *colrow_index_list_destroy   (ColRowIndexList *list);
GString         *colrow_index_list_to_string (ColRowIndexList *list,
					      gboolean is_cols,
					      gboolean *is_single);
ColRowIndexList *colrow_get_index_list	     (int first, int last,
					      ColRowIndexList *list);

ColRowStateList	*colrow_state_list_destroy   (ColRowStateList *list);
ColRowStateList	*colrow_make_state	     (Sheet *sheet, int count,
					      float size_pts, gboolean hard_size,
					      int outline_level);
ColRowStateList	*colrow_get_states	     (Sheet *sheet, gboolean is_cols,
					      int first, int last);
void		 colrow_set_states	     (Sheet *sheet, gboolean is_cols,
					      int first, ColRowStateList *states);

ColRowStateGroup  *colrow_state_group_destroy	(ColRowStateGroup *set);
ColRowStateGroup  *colrow_set_sizes		(Sheet *sheet, gboolean is_cols,
						 ColRowIndexList *src, int new_size);
void		   colrow_restore_state_group	(Sheet *sheet, gboolean is_cols,
						 ColRowIndexList *selection,
						 ColRowStateGroup *saved_state);

/* Support for Col/Row visibility */
void             colrow_set_outline             (ColRowInfo *cri, int outline_level,
						 gboolean is_collapsed);
int		 colrow_find_outline_bound	(Sheet const *sheet, gboolean is_cols,
						 int index, int depth, gboolean inc);
ColRowVisList	*colrow_get_outline_toggle	(Sheet const *sheet, gboolean is_cols,
						 gboolean visible, int first, int last);
ColRowVisList	*colrow_get_visiblity_toggle	(SheetView *sv, gboolean is_cols,
						 gboolean visible);
void		 colrow_set_visibility		(Sheet *sheet, gboolean is_cols,
						 gboolean visible, int first, int last);
void		 colrow_get_global_outline	(Sheet const *sheet, gboolean is_cols, int depth,
						 ColRowVisList	**show, ColRowVisList	**hide);
ColRowVisList	*colrow_vis_list_destroy	(ColRowVisList *list);
void		 colrow_set_visibility_list	(Sheet *sheet, gboolean is_cols,
						 gboolean visible,
						 ColRowVisList *list);

/* Misc */
#define		 colrow_max(is_cols,sheet)	((is_cols) ? gnm_sheet_get_max_cols (sheet) : gnm_sheet_get_max_rows (sheet))
void             colrow_reset_defaults		(Sheet *sheet, gboolean is_cols, int maxima);
int              colrow_find_adjacent_visible   (Sheet *sheet, gboolean is_cols,
						 int index, gboolean forward);

void             rows_height_update		(Sheet *sheet, GnmRange const *range,
						 gboolean shrink);

void             colrow_autofit                 (Sheet *sheet,
						 GnmRange const *r,
						 gboolean is_cols,
						 gboolean ignore_strings,
						 gboolean min_current,
						 gboolean min_default,
						 ColRowIndexList **indices,
						 ColRowStateList **sizes);

G_END_DECLS

#endif /* _GNM_COLROW_H_ */
