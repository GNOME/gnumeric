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

typedef GSList *ColRowVisList;

ColRowVisList col_row_get_visiblity_toggle (Sheet *sheet, gboolean const is_col,
					    gboolean const visible);
ColRowVisList col_row_vis_list_destroy     (ColRowVisList list);
void          col_row_set_visiblity        (Sheet *sheet, gboolean const is_col,
					    gboolean const visible,
					    ColRowVisList list);

#endif /* GNUMERIC_COLROW_H */
