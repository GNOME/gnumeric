#ifndef GNUMERIC_RANGES_H
#define GNUMERIC_RANGES_H

#include "sheet.h"
#include "expr.h"
#include "cell.h"
#include "style.h"

#define range_equal(a,b) (((Range *)(a))->start.row == ((Range *)(b))->start.row && \
			  ((Range *)(a))->end.row   == ((Range *)(b))->end.row && \
			  ((Range *)(a))->start.col == ((Range *)(b))->start.col && \
			  ((Range *)(a))->end.col   == ((Range *)(b))->end.col)
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
#define range_contains(r,x,y)	(((y) <= ((Range *)(r))->end.row) && \
				 ((y) >= ((Range *)(r))->start.row) && \
				 ((x) >= ((Range *)(r))->start.col) && \
				 ((x) <= ((Range *)(r))->end.col))

gboolean    range_parse             (Sheet *sheet, const char *range, Value **v);
GSList     *range_list_parse        (Sheet *sheet, const char *cell_name_str);
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

gboolean    range_is_singleton  (Range const *r);
gboolean    range_overlap       (Range const *a, Range const *b);
gboolean    range_contained     (Range const *a, Range const *b);
gboolean    range_adjacent      (Range const *a, Range const *b);
Range       range_merge         (Range const *a, Range const *b);
void        range_clip          (Range *clipped, Range const *master,
				 Range const *slave);
void        range_dump          (Range const *src);
Range      *range_copy          (Range const *src);

typedef     Range *(*RangeCopyFn) (const Range *r);
GList      *range_split_ranges  (const Range *hard, const Range *soft,
				 RangeCopyFn copy_fn);
GList      *range_fragment      (const Range *a, const Range *b);
GList      *range_fragment_list (const GList *ranges);
void        range_fragment_free (GList *fragments);

#endif /* GNUMERIC_RANGES_H */
