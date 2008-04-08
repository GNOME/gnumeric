/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_RANGES_H_
# define _GNM_RANGES_H_

#include "gnumeric.h"

G_BEGIN_DECLS

/**
 * range_equal:
 * @a: First range
 * @b: Second range
 *
 * NB. commutative, symmetric, and transitive.
 *
 * Returns: True if both ranges are equal.
 **/
#define range_equal(a,b)   ((a)->start.row == (b)->start.row && \
			    (a)->end.row   == (b)->end.row && \
			    (a)->start.col == (b)->start.col && \
			    (a)->end.col   == (b)->end.col)

gboolean gnm_range_equal (const GnmRange *a, const GnmRange *b);
guint gnm_range_hash (const GnmRange *r);

/**
 * range_overlap:
 * @a: First range
 * @b: Second range
 *
 * NB. commutative, symmetric, but not transitive.
 *
 * Returns: True if the ranges overlap at all.
 **/
#define range_overlap(a,b) (((a)->end.row >= (b)->start.row) && \
			    ((b)->end.row >= (a)->start.row) && \
			    ((a)->end.col >= (b)->start.col) && \
			    ((b)->end.col >= (a)->start.col))

/**
 * range_contains:
 * @r:   range to operate on
 * @x:   column,
 * @y:   row co-ordinate
 *
 * Determine if a range contains a col,row co-ordinate.
 *
 * Return value: TRUE if co-ordinate contained.
 **/
#define range_contains(r,x,y)	(((y) <= (r)->end.row) && \
				 ((y) >= (r)->start.row) && \
				 ((x) >= (r)->start.col) && \
				 ((x) <= (r)->end.col))

/*
 * Quickly Test if a range is valid
 */
#define range_valid(r)          ((r)->start.col <= (r)->end.col && \
				 (r)->start.row <= (r)->end.row)

GnmRange   *range_init_full_sheet   (GnmRange *r);
GnmRange   *range_init_cols   	    (GnmRange *r, int start_col, int end_col);
GnmRange   *range_init_rows         (GnmRange *r, int start_row, int end_row);
GnmRange   *range_init_rangeref	    (GnmRange *r, GnmRangeRef const *rr);
GnmRange   *range_init_value	    (GnmRange *r, GnmValue const *v);
GnmRange   *range_init_cellpos	    (GnmRange *r, GnmCellPos const *pos);
GnmRange   *range_init_cellpos_size (GnmRange *r, GnmCellPos const *start,
				     int cols, int rows);
GnmRange   *range_init              (GnmRange *r, int start_col, int start_row,
				     int end_col, int end_row);
gboolean    range_parse 	    (GnmRange *r, char const *text);

void        range_list_destroy      (GSList *ranges);

int	    range_width		(GnmRange const *r);
int	    range_height	(GnmRange const *r);
gboolean    range_is_singleton  (GnmRange const *r);
gboolean    range_is_full	(GnmRange const *r, gboolean horiz);
void        range_make_full	(GnmRange *r, gboolean full_col, gboolean full_row);
void        range_clip_to_finite(GnmRange *range, Sheet *sheet);
gboolean    range_contained     (GnmRange const *a, GnmRange const *b);
gboolean    range_intersection  (GnmRange *r,
				 GnmRange const *a,
				 GnmRange const *b);
void        range_normalize     (GnmRange *src);
GnmRange    range_union         (GnmRange const *a, GnmRange const *b);
void        range_ensure_sanity (GnmRange *range);
gboolean    range_is_sane	(GnmRange const *range);
gboolean    range_translate     (GnmRange *range, int col_offset, int row_offset);
gboolean    range_transpose     (GnmRange *range, GnmCellPos const *origin);

char const *range_as_string	(GnmRange const *r);
void        range_dump		(GnmRange const *r, char const *suffix);
GnmRange   *range_dup		(GnmRange const *r);

GSList     *range_split_ranges    (GnmRange const *hard, GnmRange const *soft);
GSList     *range_fragment        (GnmRange const *a, GnmRange const *b);
void        range_fragment_free   (GSList *fragments);

GnmSheetRange *gnm_sheet_range_new	  (Sheet *sheet, GnmRange const *r);
void           gnm_sheet_range_free       (GnmSheetRange *r);
gboolean       gnm_sheet_range_from_value (GnmSheetRange *r, GnmValue const *v);
gboolean       gnm_sheet_range_overlap    (GnmSheetRange const *a, GnmSheetRange const *b);
GnmSheetRange *gnm_sheet_range_dup	  (GnmSheetRange const *sr);
gboolean       gnm_sheet_range_equal      (const GnmSheetRange *a,
					   const GnmSheetRange *b);
guint          gnm_sheet_range_hash       (const GnmSheetRange *sr);

char	      *global_range_name	  (Sheet const *sheet, GnmRange const *r);
char	      *undo_cell_pos_name	  (Sheet const *sheet, GnmCellPos const *pos);
char	      *undo_range_name		  (Sheet const *sheet, GnmRange const *r);
char	      *undo_range_list_name	  (Sheet const *sheet, GSList const *ranges);

GSList	      *global_range_list_parse    (Sheet *sheet, char const *str);
GnmValue      *global_range_list_foreach  (GSList *gr_list, GnmEvalPos const *ep,
					   CellIterFlags	flags,
					   CellIterFunc	handler,
					   gpointer	closure);
gboolean       global_range_contained	  (Sheet const *sheet,
					   GnmValue const *a, GnmValue const *b);

G_END_DECLS

#endif /* _GNM_RANGES_H_ */
