/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COLROW_H_
# define _GNM_COLROW_H_

#include "gnumeric.h"

G_BEGIN_DECLS

struct _ColRowInfo {
	/* Size including margins, and right grid line */
	double	 size_pts;
	int      size_pixels;

	unsigned  is_default	: 1;
	unsigned  outline_level : 4;
	unsigned  is_collapsed  : 1;	/* Does this terminate an outline ? */
	unsigned  hard_size     : 1;	/* are dimensions explicitly set ? */
	unsigned  visible       : 1;	/* visible */
	unsigned  in_filter     : 1;	/* in a filter */
	unsigned  in_advanced_filter : 1; /* in an advanced filter */
	unsigned  needs_respan  : 1;	/* mark a row as needing span generation */

	/* TODO : Add per row/col min/max */

	gpointer spans;	/* Only used for rows */
};
GType col_row_info_get_type (void);

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
	double	size_pts;
	int	size_pixels;
};
typedef struct _ColRowState {
	double    size_pts;
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

double  colrow_compute_pixel_scale (Sheet const *sheet, gboolean horizontal);
void	colrow_compute_pixels_from_pts (ColRowInfo *cri, Sheet const *sheet,
					gboolean horizontal, double scale);
void	colrow_compute_pts_from_pixels (ColRowInfo *cri, Sheet const *sheet,
					gboolean horizontal, double scale);

gboolean colrow_is_default (ColRowInfo const *cri);
gboolean colrow_is_empty   (ColRowInfo const *cri);
gboolean colrow_equal	   (ColRowInfo const *a, ColRowInfo const *b);
void     colrow_copy	   (ColRowInfo *dst, ColRowInfo const *src);
ColRowInfo *col_row_info_new (void);
void colrow_free (ColRowInfo *cri);

typedef struct {
	int	pos;
	ColRowInfo const *cri;
} GnmColRowIter;

typedef gboolean (*ColRowHandler)(GnmColRowIter const *iter, gpointer user_data);
gboolean colrow_foreach	   (ColRowCollection const *infos,
			    int first, int last,
			    ColRowHandler callback,
			    gpointer user_data);

void colrow_resize (ColRowCollection *infos, int size);

#define colrow_index_list_destroy(l) g_list_free_full ((l), g_free)

GString         *colrow_index_list_to_string (ColRowIndexList *list,
					      gboolean is_cols,
					      gboolean *is_single);
ColRowIndexList *colrow_get_index_list	     (int first, int last,
					      ColRowIndexList *list);
ColRowIndexList *colrow_index_list_copy	     (ColRowIndexList *list);

ColRowStateList *colrow_state_list_destroy   (ColRowStateList *list);

ColRowStateList	*colrow_get_states	     (Sheet *sheet, gboolean is_cols,
					      int first, int last);
void		 colrow_set_states	     (Sheet *sheet, gboolean is_cols,
					      int first, ColRowStateList *states);

ColRowStateGroup  *colrow_state_group_destroy	(ColRowStateGroup *set);
ColRowStateGroup  *colrow_set_sizes		(Sheet *sheet, gboolean is_cols,
						 ColRowIndexList *src, int new_size,
						 int from, int to);
ColRowStateGroup  *colrow_get_sizes		(Sheet *sheet, gboolean is_cols,
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
#define colrow_vis_list_destroy(l) (g_slist_free_full ((l), g_free), NULL)
gint             colrow_vis_list_length         (ColRowVisList *list);
void		 colrow_set_visibility_list	(Sheet *sheet, gboolean is_cols,
						 gboolean visible,
						 ColRowVisList *list);

/* Misc */
#define		 colrow_max(is_cols,sheet)	((is_cols) ? gnm_sheet_get_max_cols (sheet) : gnm_sheet_get_max_rows (sheet))
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
void             colrow_autofit_col             (Sheet *sheet, GnmRange *r);
void             colrow_autofit_row             (Sheet *sheet, GnmRange *r);

G_END_DECLS

#endif /* _GNM_COLROW_H_ */
