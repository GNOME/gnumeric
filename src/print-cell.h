#ifndef GNUMERIC_PRINT_CELL_H
#define GNUMERIC_PRINT_CELL_H

void print_cell_range (GnomePrintContext *context,
		       Sheet *sheet,
		       int start_col, int start_row,
		       int end_col, int end_row,
		       double base_x, double base_y);
void print_cell_grid  (GnomePrintContext *context,
		       Sheet *sheet, 
		       int start_col, int start_row,
		       int end_col, int end_row,
		       double base_x, double base_y,
		       double width, double height);

#endif
