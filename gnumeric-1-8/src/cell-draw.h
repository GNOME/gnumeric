/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_CELL_DRAW_H_
# define _GNM_CELL_DRAW_H_

#include "gnumeric.h"
#include <gdk/gdktypes.h>

G_BEGIN_DECLS

gboolean cell_calc_layout (GnmCell const *cell, GnmRenderedValue *rv, int y_direction,
			   int width, int height, int h_center,
			   GOColor *res_color, gint *res_x, gint *res_y);

void cell_finish_layout (GnmCell *cell, GnmRenderedValue *rv,
			 int col_width,
			 gboolean inhibit_overflow);

void cell_draw (GnmCell const *cell, GdkGC *gc, GdkDrawable *drawable,
		int x, int y, int height, int width, int h_center);

G_END_DECLS

#endif /* _GNM_CELL_DRAW_H_ */
