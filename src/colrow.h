#ifndef GNUMERIC_COLROW_H
#define GNUMERIC_COLROW_H

#include <glib.h>
#include "gnumeric.h"

struct _ColRowInfo {
	int	pos;		/* the column or row number */

	/* These are not scaled, and are the same in points and pixels */
	int	margin_a;  	/* top/left margin */
	int	margin_b; 	/* bottom/right margin */

	/* Size including margins, and right grid line */
	float	size_pts;	/* In points */
	int	size_pixels;	/* In pixels */

	int	 hard_size:1;	/* has the user explicitly set the dimensions? */
	int	 visible:1;	/* Is the row/col visible */

	/* TODO : Add per row/col min/max */

	void *spans;	/* Only used for rows */
};

struct _ColRowCollection
{
	int         max_used;
	ColRowInfo  default_style;
	GPtrArray * info;
};

#define COL_INTERNAL_WIDTH(col) ((col)->size_pixels - ((col)->margin_b + (col)->margin_a + 1))
#define ROW_INTERNAL_HEIGHT(row) ((row)->size_pixels - ((row)->margin_b + (row)->margin_a + 1))

gboolean col_row_foreach (ColRowCollection const *infos,
			  int first, int last,
			  col_row_callback callback,
			  void *user_data);

/* Support for Col/Row resizing */
ColRowSizeList	*col_row_size_list_destroy    (ColRowSizeList *list);
ColRowIndexList *col_row_index_list_destroy   (ColRowIndexList *list);
ColRowIndexList *col_row_get_index_list	      (int first, int last, ColRowIndexList *list);
double		*col_row_save_sizes	      (Sheet *sheet, gboolean const is_cols,
					       int first, int last);
ColRowSizeList	*col_row_set_sizes	      (Sheet *sheet, gboolean const is_cols,
					       ColRowIndexList *src, int new_size);
void		 col_row_restore_sizes	      (Sheet *sheet, gboolean const is_cols,
					       int first, int last, double *);
void		 col_row_restore_sizes_group  (Sheet *sheet, gboolean const is_cols,
					       ColRowIndexList *selection,
					       ColRowSizeList *saved_sizes);

void		 rows_height_update	      (Sheet *sheet, Range const *range);

/* Support for Col/Row visibility */
void		 col_row_set_visibility	      (Sheet *sheet, gboolean const is_col,
					       gboolean const visible,
					       int first, int last);

ColRowVisList	*col_row_get_visiblity_toggle (Sheet *sheet, gboolean const is_col,
					       gboolean const visible);
ColRowVisList	*col_row_vis_list_destroy     (ColRowVisList *list);
void		 col_row_set_visibility_list  (Sheet *sheet, gboolean const is_col,
					       gboolean const visible,
					       ColRowVisList *list);

#endif /* GNUMERIC_COLROW_H */
