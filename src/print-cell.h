#ifndef GNUMERIC_PRINT_CELL_H
#define GNUMERIC_PRINT_CELL_H

void print_cell_range (GnomePrintContext *context,
		       Sheet const *sheet,
		       int start_col, int start_row,
		       int end_col, int end_row,
		       double base_x, double base_y,
		       gboolean show_grid);

/* This function got introduced when gnome-print switched to UTF-8, and will
 * disappear again once Gnumeric makes the switch */
int print_show_iso8859_1 (GnomePrintContext *pc, char const *text);

void print_make_rectangle_path (GnomePrintContext *pc,
				double left, double bottom,
				double right, double top);
#endif
