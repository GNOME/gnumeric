#ifndef GNUMERIC_PRINT_CELL_H
#define GNUMERIC_PRINT_CELL_H

#include "gnumeric.h"
#include <gtk/gtk.h>
#include <libgnomeprint/gnome-print.h>

void gnm_gtk_print_cell_range (GtkPrintContext *print_context,
			       cairo_t *context,
			       Sheet const *sheet, GnmRange *range,
			       double base_x, double base_y,
			       gboolean hide_grid);

void gnm_print_cell_range (GnomePrintContext *context,
			   Sheet const *sheet, GnmRange *range,
			   double base_x, double base_y,
			   gboolean hide_grid);

void gnm_print_make_rect_path (GnomePrintContext *pc,
			       double left, double bottom,
			       double right, double top);

#endif /* GNUMERIC_PRINT_CELL_H */
