#ifndef GNUMERIC_RANGES_H
#define GNUMERIC_RANGES_H

#include "gnumeric.h"

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

Range	   *range_init_full_sheet   (Range *r);
Range      *range_init              (Range *r, int start_col, int start_row,
				     int end_col, int end_row);
Value      *range_parse             (Sheet *sheet, char const *range, gboolean strict);
int         parse_range 	    (char const *text, int *start_col, int *start_row,
				     int *end_col, int *end_row);
GSList     *range_list_parse        (Sheet *sheet, char const *cell_name_str, gboolean strict);
void        range_list_destroy      (GSList *ranges);
void        range_list_foreach_full (GSList *ranges,
				     void (*callback)(Cell *cell, void *data),
				     void *data, gboolean create_empty);
void        range_list_foreach_all  (GSList *ranges,
				     void (*callback)(Cell *cell, void *data),
				     void *data);
void        range_list_foreach      (GSList *ranges,
				     void (*callback)(Cell *cell, void *data),
				     void *data);
void        range_list_foreach_area (Sheet *sheet, GSList *ranges,
				     void (*callback)(Sheet       *sheet,
						      Range const *range,
						      gpointer     user_data),
				     gpointer user_data);

/* deprecated */
void        ranges_set_style        (Sheet  *sheet, GSList *ranges,
				     MStyle *mstyle);

int	    range_width		(Range const *r);
int	    range_height	(Range const *r);
gboolean    range_is_singleton  (Range const *r);
gboolean    range_is_infinite   (Range const *r);
gboolean    range_is_full	(Range const *r, gboolean is_cols);
void        range_clip_to_finite(Range *range, Sheet *sheet);
gboolean    range_contained     (Range const *a, Range const *b);
gboolean    range_adjacent      (Range const *a, Range const *b);
Range       range_merge         (Range const *a, Range const *b);
gboolean    range_intersection  (Range *r,
				 Range const *a,
				 Range const *b);
void        range_normalize     (Range *src);
Range       range_union         (Range const *a, Range const *b);
void        range_ensure_sanity (Range *range);
gboolean    range_is_sane	(Range const *range);
gboolean    range_translate     (Range *range, int col_offset, int row_offset);
gboolean    range_transpose     (Range *range, CellPos const *origin);

gboolean    range_has_header    (Sheet const *sheet, Range const *src,
				 gboolean top);
char const *range_name          (Range const *src);
void        range_dump          (Range const *src, char const *suffix);
Range      *range_dup		(Range const *src);

typedef     Range *(*RangeCopyFn) (Range const *r);
GList      *range_split_ranges    (Range const *hard, Range const *soft,
				   RangeCopyFn copy_fn);
GList      *range_fragment        (Range const *a, Range const *b);
GList      *range_fragment_list   (GList const *ranges);
GList      *range_fragment_list_clip (GList const *ranges, Range const *clip);
void        range_fragment_free   (GList *fragments);

#endif /* GNUMERIC_RANGES_H */
