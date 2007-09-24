#ifndef GNUMERIC_PRINT_CELL_H
#define GNUMERIC_PRINT_CELL_H

#include "gnumeric.h"
#include <cairo.h>
#include <gtk/gtkprintcontext.h>

void gnm_gtk_print_cell_range (GtkPrintContext *print_context,
			       cairo_t *context,
			       Sheet const *sheet, GnmRange *range,
			       double base_x, double base_y,
			       gboolean hide_grid);

#endif /* GNUMERIC_PRINT_CELL_H */
