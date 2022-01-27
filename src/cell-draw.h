#ifndef _GNM_CELL_DRAW_H_
# define _GNM_CELL_DRAW_H_

#include <gnumeric.h>

G_BEGIN_DECLS

gboolean cell_calc_layout (GnmCell const *cell, GnmRenderedValue *rv, int y_direction,
			   int width, int height, int h_center,
			   GOColor *res_color, gint *res_x, gint *res_y);

void cell_finish_layout (GnmCell *cell, GnmRenderedValue *rv,
			 int col_width,
			 gboolean inhibit_overflow);

typedef struct {
	GdkRGBA extension_marker_color;
	int extension_marker_size;
} GnmCellDrawStyle;

void cell_draw (GnmCell const *cell, cairo_t* cr,
		int x, int y, int height, int width, int h_center,
		gboolean show_extension_markers,
		GnmCellDrawStyle const *style);

G_END_DECLS

#endif /* _GNM_CELL_DRAW_H_ */
