#ifndef GNUMERIC_PRINT_CELL_H
#define GNUMERIC_PRINT_CELL_H

void print_cell_range (GnomePrintContext *context,
		       Sheet const *sheet, Range *range,
		       double base_x, double base_y,
		       gboolean hide_grid);

/* This function got introduced when gnome-print switched to UTF-8, and will
 * disappear again once Gnumeric makes the switch */
int print_show (GnomePrintContext *pc, char const *text);

/* Use these instead of gnome_font_get_width_string[_n] ! */
double get_width_string_n (GnomeFont *font,char const* text,guint n);
double get_width_string (GnomeFont *font,char const* text);

void print_make_rectangle_path (GnomePrintContext *pc,
				double left, double bottom,
				double right, double top);
#endif
