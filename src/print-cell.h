#ifndef GNUMERIC_PRINT_CELL_H
#define GNUMERIC_PRINT_CELL_H

gboolean print_cell_range (GnomePrintContext *context,
			   Sheet *sheet,
			   int start_col, int start_row,
			   int end_col, int end_row,
			   double base_x, double base_y,
			   gboolean output);

void print_cell_grid  (GnomePrintContext *context,
		       Sheet *sheet, 
		       int start_col, int start_row,
		       int end_col, int end_row,
		       double base_x, double base_y,
		       double width, double height);

/* This function got introduced when gnome-print switched to UTF-8, and will
 * disappear again once Gnumeric makes the switch */
int print_show_iso8859_1 (GnomePrintContext *pc, char const *text);

#endif
