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
Range      *range_init_pos          (Range *r, CellPos const *start, CellPos const *end);
Range      *range_init              (Range *r, int start_col, int start_row,
				     int end_col, int end_row);
Value      *range_parse             (Sheet *sheet, char const *range, gboolean strict);
gboolean    parse_range 	    (char const *text, Range *r);
void        range_list_destroy      (GSList *ranges);

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

gboolean    setup_range_from_value (Range *range, Value *v, gboolean release);
gboolean    setup_range_from_range_ref (Range *range, RangeRef *v, gboolean release);

/* TODO : Do these 2 belong here ? or in sheet.h
 * Probably sheet.h but that is overfull.
 */
gboolean    range_trim		(Sheet const *sheet, Range *r,
				 gboolean cols);
gboolean    range_has_header    (Sheet const *sheet, Range const *src,
				 gboolean top, gboolean ignore_styles);

char const *range_name          (Range const *src);
void        range_dump          (Range const *src, char const *suffix);
Range      *range_dup		(Range const *src);

GSList     *range_split_ranges    (Range const *hard, Range const *soft);
GSList     *range_fragment        (Range const *a, Range const *b);
void        range_fragment_free   (GSList *fragments);

GlobalRange *global_range_new     (Sheet *sheet, Range const *r);
GlobalRange *value_to_global_range (Value *v);
void         global_range_free    (GlobalRange *gr);
gboolean     global_range_overlap (GlobalRange const *a, GlobalRange const *b);
GlobalRange *global_range_dup     (GlobalRange const *src);
Value       *global_range_parse   (Sheet *sheet, char const *range);
char        *global_range_name    (Sheet *sheet, Range const *r);

GSList      *global_range_list_parse   (Sheet *sheet, char const *str);
Value	    *global_range_list_foreach (GSList *gr_list, EvalPos const *ep,
					CellIterFlags	flags,
					CellIterFunc	handler,
					gpointer	closure);
gboolean    global_range_contained (Value *a, Value *b);


#endif /* GNUMERIC_RANGES_H */
