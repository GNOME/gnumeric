#ifndef GNUMERIC_CELL_DRAW_H
#define GNUMERIC_CELL_DRAW_H

#include "gnumeric.h"
#include <gdk/gdk.h>

gboolean cell_calc_layout (GnmCell const *cell, RenderedValue *rv, int y_direction,
			   int width, int height, int h_center,
			   GdkColor **res_color, gint *res_x, gint *res_y);
void cell_draw (GnmCell const *cell, GdkGC *gc, GdkDrawable *drawable,
		int x, int y, int height, int width, int h_center);

#endif /* GNUMERIC_CELL_DRAW_H */
