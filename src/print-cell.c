/*
 * cell.c: Printing of cell regions and cells.
 *
 * Author:
 *    Miguel de Icaza 1999 (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "eval.h"
#include "format.h"
#include "color.h"
#include <libgnomeprint/gnome-print.h>
#include "print-cell.h"

#define CELL_DIM(cell,p) (cell->p->units + cell->p->margin_a + cell->p->margin_b)
#define CELL_HEIGHT(cell) CELL_DIM(cell,row)
#define CELL_WIDTH(cell)  CELL_DIM(cell,col)

static void
print_cell_border (GnomePrintContext *context, Cell *cell, double x, double y)
{
	gdouble cell_width = CELL_WIDTH (cell);
	gdouble cell_height = CELL_HEIGHT (cell);
	
	gnome_print_moveto (context, x, y);
	gnome_print_lineto (context, x, y - cell_height);
	gnome_print_lineto (context, x + cell_width,  y - cell_height);
	gnome_print_lineto (context, x + cell_width, y);
	gnome_print_lineto (context, x, y);
	gnome_print_stroke (context);
}

static void
print_overflow (GnomePrintContext *context, Cell *cell)
{
}

static inline int
cell_is_number (Cell *cell)
{
        if (cell->value)
                if (cell->value->type == VALUE_FLOAT || cell->value->type == VALUE_INTEGER)
                        return TRUE;

        return FALSE;
}

static void
print_cell_text (GnomePrintContext *context, Cell *cell, double base_x, double base_y)
{
	Style *style = cell->style;
	GnomeFont *print_font = style->font->font;
	double text_width;
	double font_height;
	gboolean do_multi_line;
	int start_col, end_col;
	int halign;
	
	cell_get_span (cell, &start_col, &end_col);
	
	text_width = gnome_font_get_width_string (print_font, cell->text->str);

	if (text_width > cell->col->units && cell_is_number (cell)){
		print_overflow (context, cell);
		return;
	}

	halign = cell_get_horizontal_align (cell);
	if (halign == HALIGN_JUSTIFY || style->valign == VALIGN_JUSTIFY || style->fit_in_cell)
		do_multi_line = TRUE;
	else
		do_multi_line = FALSE;

	if (do_multi_line){
	} else {
		double x, diff, total, len;

		/*
		 * x1, y1 are relative to this cell origin, but the cell might be using
		 * columns to the left (if it is set to right justify or center justify)
		 * compute the pixel difference 
		 */
		if (start_col != cell->col->pos)
			diff = -sheet_col_get_unit_distance (cell->sheet, start_col, cell->col->pos);
		else
			diff = 0;

		g_warning ("Set clipping");
		len = 0;
		switch (halign){
		case HALIGN_FILL:
			g_warning ("Unhandled");
			len = text_width;
			/* fall down */

		case HALIGN_LEFT:
			x = 0;
			break;
			
		case HALIGN_RIGHT:
			x = cell->col->units - text_width;
			break;

		case HALIGN_CENTER:
			x = (cell->col->units - text_width)/2;
			break;

		default:
			g_warning ("Single-line justitfication style not supported\n");
			x = 0;
			break;
		}

		gnome_print_setfont (context, print_font);
		total = 0;
		do {
			gnome_print_moveto (context, base_x + x, base_y - cell->row->units);
			gnome_print_show (context, cell->text->str);
			gnome_print_stroke (context);
			
			x += len;
			total += len;
		} while (halign == HALIGN_FILL && total < cell->col->units);
	}
}

static void
print_cell (GnomePrintContext *context, Cell *cell, double x, double y)
{
	g_assert (cell != NULL);

	print_cell_border (context, cell, x, y);

	print_cell_text (context, cell,
			 x + cell->col->margin_a,
			 y + cell->row->margin_b);
}

void
print_cell_range (GnomePrintContext *context,
		  Sheet *sheet,
		  int start_col, int start_row,
		  int end_col, int end_row,
		  double base_x, double base_y)
{
	ColRowInfo *ci, *ri;
	int row, col;
	double x, y;
	
	g_return_if_fail (context != NULL);
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (context));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col < end_col);
	g_return_if_fail (end_col < end_row);

	y = 0;
	for (row = 0; row <= end_row; row++){
		ri = sheet_row_get_info (sheet, row);

		x = 0;
		for (col = start_col; col <= end_col; col++){
			Cell *cell;

			cell = sheet_cell_get (sheet, col, row);
			if (cell){
				print_cell (context, cell, base_x + x, base_y + y);
				ci = cell->col;
			} else
				ci = sheet_col_get_info (sheet, col);
			
			x += ci->units + ci->margin_a + ci->margin_b;
		}
		y -= ri->units + ci->margin_a + ci->margin_b;
	}
}

static void
vline (GnomePrintContext *context, ColRowInfo *ci)
{
}

static void
hline (GnomePrintContext *context, ColRowInfo *ri)
{
}

void
print_cell_grid (GnomePrintContext *context,
		 Sheet *sheet, 
		 int start_col, int start_row,
		 int end_col, int end_row)
{
	GList *cols, *rows;
	int last_col_gen = -1, last_row_gen = -1;
	
	g_return_if_fail (context != NULL);
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (context));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col < end_col);
	g_return_if_fail (end_col < end_row);

	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;

		if (ci->pos < start_col)
			continue;
		if (ci->pos > end_col)
			break;

		if ((last_col_gen > 0) && (ci->pos != last_col_gen+1)){
			int i;
			
			for (i = last_col_gen; i < ci->pos; i++)
				vline (context, &sheet->default_col_style);
		}
		vline (context, ci);
		last_col_gen = ci->pos;
	}

	for (rows = sheet->rows_info; rows; rows = rows->next){
		ColRowInfo *ri;

		ri = rows->data;
		if (ri->pos < start_row)
			continue;
		if (ri->pos > end_row)
			break;

		if ((last_row_gen > 0) && (ri->pos != last_row_gen+1)){
			int i;
			
			for (i = last_row_gen; i < ri->pos; i++)
				hline (context, &sheet->default_row_style);
		}
		hline (context, ri);
		last_row_gen = ri->pos;
	}
}



