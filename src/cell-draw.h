#ifndef GNUMERIC_CELL_DRAW_H
#define GNUMERIC_CELL_DRAW_H

#include <glib.h>
#include "gnumeric.h"

void cell_draw (Cell const *cell, MStyle *mstyle,
		GdkGC *gc, GdkDrawable *drawable, int x, int y, int height, int width);

#endif /* GNUMERIC_CELL_DRAW_H */
