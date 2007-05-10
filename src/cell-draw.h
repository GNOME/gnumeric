#ifndef GNUMERIC_CELL_DRAW_H
#define GNUMERIC_CELL_DRAW_H

#include "gnumeric.h"
#include <gdk/gdktypes.h>

gboolean cell_calc_layout (GnmCell const *cell, GnmRenderedValue *rv, int y_direction,
			   int width, int height, int h_center,
			   GOColor *res_color, gint *res_x, gint *res_y);

void cell_finish_layout (GnmCell *cell, GnmRenderedValue *rv,
			 int col_width,
			 gboolean inhibit_overflow);

void cell_draw (GnmCell const *cell, GdkGC *gc, GdkDrawable *drawable,
		int x, int y, int height, int width, int h_center);

#endif /* GNUMERIC_CELL_DRAW_H */
