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
Value      *range_parse             (Sheet *sheet, const char *range, gboolean strict);
int         parse_range 	    (const char *text, int *start_col, int *start_row,
				     int *end_col, int *end_row);
GSList     *range_list_parse        (Sheet *sheet, const char *cell_name_str, gboolean strict);
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
						      const Range *range,
						      gpointer     user_data),
				     gpointer user_data);

void        ranges_set_style        (Sheet  *sheet, GSList *ranges,
				     MStyle *mstyle);

gboolean    range_is_singleton  (const Range *r);
gboolean    range_is_infinite   (const Range *r);
void        range_clip_to_finite(Range *range, Sheet *sheet);
gboolean    range_contained     (const Range *a, const Range *b);
gboolean    range_adjacent      (const Range *a, const Range *b);
Range       range_merge         (const Range *a, const Range *b);
gboolean    range_intersection  (Range *r,
				 const Range *a,
				 const Range *b);
void        range_normalize     (Range *src);
Range       range_union         (const Range *a, const Range *b);
void        range_ensure_sanity (Range *range);
gboolean    range_is_sane	(Range const *range);
gboolean    range_translate     (Range *range, int col_offset, int row_offset);
gboolean    range_transpose     (Range *range, const CellPos *origin);
gboolean    range_expand        (Range *range,
				 int d_tlx, int d_tly,
				 int d_brx, int d_bry);

gboolean    range_has_header    (const Sheet *sheet, const Range *src,
				 gboolean top);
const char *range_name          (const Range *src);
void        range_dump          (const Range *src, char const *suffix);
Range      *range_copy          (const Range *src);

typedef     Range *(*RangeCopyFn) (const Range *r);
GList      *range_split_ranges    (const Range *hard, const Range *soft,
				   RangeCopyFn copy_fn);
GList      *range_fragment        (const Range *a, const Range *b);
GList      *range_fragment_list   (const GList *ranges);
GList      *range_fragment_list_clip (const GList *ranges, const Range *clip);
void        range_fragment_free   (GList *fragments);

#endif /* GNUMERIC_RANGES_H */
