#ifndef _GNM_RANGES_H_
# define _GNM_RANGES_H_

#include <gnumeric.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * range_equal:
 * @a: First range
 * @b: Second range
 *
 * NB. commutative, symmetric, and transitive.
 *
 * Returns: %TRUE if both ranges are equal.
 **/
#define range_equal(a,b)   ((a)->start.row == (b)->start.row && \
			    (a)->end.row   == (b)->end.row && \
			    (a)->start.col == (b)->start.col && \
			    (a)->end.col   == (b)->end.col)

GType	  gnm_range_get_type (void); /* GBoxedType */
GnmRange *gnm_range_dup	  (GnmRange const *r);
gboolean  gnm_range_equal (GnmRange const *a, GnmRange const *b);
int       gnm_range_compare (GnmRange const *a, GnmRange const *b);

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
 * Returns: %TRUE if co-ordinate contained.
 **/
#define range_contains(r,x,y)	(((y) <= (r)->end.row) && \
				 ((y) >= (r)->start.row) && \
				 ((x) >= (r)->start.col) && \
				 ((x) <= (r)->end.col))

void gnm_range_simplify (GArray *arr);

/*
 * Quickly Test if a range is valid
 */
#define range_valid(r)          ((r)->start.col <= (r)->end.col && \
				 (r)->start.row <= (r)->end.row)

#define range_fragment_free(f) g_slist_free_full ((f), g_free)

GnmRange   *range_init_full_sheet   (GnmRange *r, Sheet const *sheet);
GnmRange   *range_init_cols   	    (GnmRange *r, Sheet const *sheet,
				     int start_col, int end_col);
GnmRange   *range_init_rows         (GnmRange *r, Sheet const *sheet,
				     int start_row, int end_row);
GnmRange   *range_init_invalid	    (GnmRange *r);
GnmRange   *range_init_rangeref	    (GnmRange *r, GnmRangeRef const *rr);
GnmRange   *range_init_value	    (GnmRange *r, GnmValue const *v);
GnmRange   *range_init_cellpos	    (GnmRange *r, GnmCellPos const *pos);
GnmRange   *range_init_cellpos_size (GnmRange *r, GnmCellPos const *start,
				     int cols, int rows);
GnmRange   *range_init              (GnmRange *r, int start_col, int start_row,
				     int end_col, int end_row);
gboolean    range_parse 	    (GnmRange *r, char const *text,
				     GnmSheetSize const *ss);

void        range_list_destroy      (GSList *ranges);

int	    range_width		(GnmRange const *r);
int	    range_height	(GnmRange const *r);
gboolean    range_is_singleton  (GnmRange const *r);
gboolean    range_is_full	(GnmRange const *r, Sheet const *sheet, gboolean horiz);
void        range_clip_to_finite(GnmRange *range, Sheet *sheet);
gboolean    range_contained     (GnmRange const *a, GnmRange const *b);
gboolean    range_intersection  (GnmRange *r,
				 GnmRange const *a,
				 GnmRange const *b);
void        range_normalize     (GnmRange *src);
GnmRange    range_union         (GnmRange const *a, GnmRange const *b);
void        range_ensure_sanity (GnmRange *range, Sheet const *sheet);
gboolean    range_is_sane	(GnmRange const *range);
gboolean    range_translate     (GnmRange *range, Sheet const *sheet,
				 int col_offset, int row_offset);
gboolean    range_transpose     (GnmRange *range, Sheet const *sheet,
				 GnmCellPos const *origin);

char const *range_as_string	(GnmRange const *r);
void        range_dump		(GnmRange const *r, char const *suffix);

GSList     *range_split_ranges    (GnmRange const *hard, GnmRange const *soft);
GSList     *range_fragment        (GnmRange const *a, GnmRange const *b);

GType	       gnm_sheet_range_get_type   (void); /* GBoxedType */
GnmSheetRange *gnm_sheet_range_new	  (Sheet *sheet, GnmRange const *r);
void           gnm_sheet_range_free       (GnmSheetRange *r);
gboolean       gnm_sheet_range_from_value (GnmSheetRange *r, GnmValue const *v);
gboolean       gnm_sheet_range_overlap    (GnmSheetRange const *a, GnmSheetRange const *b);
GnmSheetRange *gnm_sheet_range_dup	  (GnmSheetRange const *sr);
gboolean       gnm_sheet_range_equal      (GnmSheetRange const *a,
					   GnmSheetRange const *b);

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
