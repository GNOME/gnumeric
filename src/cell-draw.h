#ifndef GNUMERIC_CELL_DRAW_H
#define GNUMERIC_CELL_DRAW_H

#include <glib.h>
#include "gnumeric.h"

int cell_draw (Cell *cell, MStyle *mstyle,
	       SheetView *sheet_view,
	       GdkGC *gc, GdkDrawable *drawable,
	       int x, int y, gboolean const is_selected);

#endif /* GNUMERIC_CELL_DRAW_H */
