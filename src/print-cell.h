#ifndef GNUMERIC_PRINT_CELL_H
#define GNUMERIC_PRINT_CELL_H

#include "gnumeric.h"
#include <libgnomeprint/gnome-print.h>

void print_cell_range (GnomePrintContext *context,
		       Sheet const *sheet, Range *range,
		       double base_x, double base_y,
		       gboolean hide_grid);

void print_make_rectangle_path (GnomePrintContext *pc,
				double left, double bottom,
				double right, double top);

#endif /* GNUMERIC_PRINT_CELL_H */
