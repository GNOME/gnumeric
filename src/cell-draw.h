#ifndef GNUMERIC_CELL_DRAW_H
#define GNUMERIC_CELL_DRAW_H

#include "gnumeric.h"
#include <gdk/gdk.h>

void cell_draw (Cell const *cell, GdkGC *gc, GdkDrawable *drawable,
		int x, int y, int height, int width, int h_center);

#endif /* GNUMERIC_CELL_DRAW_H */
