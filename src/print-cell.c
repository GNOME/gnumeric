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
#include "utils.h"
#include <libgnomeprint/gnome-print.h>
#include "print-cell.h"

#define CELL_DIM(cell,p) (cell->p->units + cell->p->margin_a_pt + cell->p->margin_b_pt)
#define CELL_HEIGHT(cell) CELL_DIM(cell,row)
#define CELL_WIDTH(cell)  CELL_DIM(cell,col)

static void
print_border (GnomePrintContext *pc, double x1, double y1, double x2, double y2, StyleBorder *b, int idx)
{
	double width;
	int dash_mode, dupit;

	width = 0.5;
	dash_mode = dupit = 0;
	
	switch (b->type [idx]){
	case BORDER_NONE:
		return;

	case BORDER_THIN:
		break;

	case BORDER_MEDIUM:
		width = 1.0;
		break;

	case BORDER_DASHED:
		dash_mode = 1;
		break;

	case BORDER_DOTTED:
		dash_mode = 2;
		break;

	case BORDER_THICK:
		width = 1.5;
		break;

	case BORDER_DOUBLE:
		width = 0.5;
		dupit = 2;
		break;

	case BORDER_HAIR:
		g_warning ("FIXME: What is BORDER_HAIR?");
		return;
	}
	gnome_print_setlinewidth (pc, width);
	gnome_print_moveto (pc, x1, y1);
	gnome_print_lineto (pc, x2, y2);
	gnome_print_stroke (pc);

	if (!dupit)
		return;

	if (x1 == x2){
		gnome_print_moveto (pc, x1 + 1, y1);
		gnome_print_lineto (pc, x2 + 1, y2);
		gnome_print_stroke (pc);
	} else {
		gnome_print_moveto (pc, x1, y1 + 1);
		gnome_print_lineto (pc, x2, y2 + 1);
		gnome_print_stroke (pc);
	}
}

static void
print_cell_border (GnomePrintContext *context, Cell *cell, double x1, double y1)
{
	gdouble cell_width = CELL_WIDTH (cell);
	gdouble cell_height = CELL_HEIGHT (cell);
	StyleBorder *border = cell->style->border;
	double x2, y2;

	x2 = x1 + cell_width;
	y2 = y1 - cell_height;
	
	print_border (context, x1, y1, x1, y2, border, 0);
	print_border (context, x1, y2, x2, y2, border, 1);
	print_border (context, x2, y2, x2, y1, border, 2);
	print_border (context, x2, y1, x1, y1, border, 3);
}

static void
print_overflow (GnomePrintContext *context, Cell *cell)
{
}

static GList *
cell_split_text (GnomeFont *font, char const *text, int const width)
{
	GList *list;
	char const *p, *line_begin, *ideal_cut_spot = NULL;
	int  line_len, used;
	gboolean last_was_cut_point = FALSE;

	list = NULL;
	used = 0;
	for (line_begin = p = text; *p; p++){
		int len;

		/* If there is an embeded return honour it */
		if (*p == '\n'){
			int const line_len = p - line_begin;
			char *line = g_malloc (line_len + 1);
			memcpy (line, line_begin, line_len);
			line [line_len] = '\0';
			list = g_list_append (list, line);

			used = 0;
			line_begin = p+1; /* skip the newline */
			ideal_cut_spot = NULL;
			last_was_cut_point = FALSE;
			continue;
		}

		if (last_was_cut_point && *p != ' ')
			ideal_cut_spot = p;

		len = gnome_font_get_width_string_n (font, p, 1);

		/* If we have overflowed, do the wrap */
		if (used + len > width){
			char const *begin = line_begin;
			char *line;
			
			if (ideal_cut_spot){
				int const n = p - ideal_cut_spot + 1;

				line_len = ideal_cut_spot - line_begin;
				used = gnome_font_get_width_string_n (font, ideal_cut_spot, n);
				line_begin = ideal_cut_spot;
			} else {
				/* Split BEFORE this character */
				used = len;
				line_len = p - line_begin;
				line_begin = p;
			}
			
			line = g_malloc (line_len + 1);
			memcpy (line, begin, line_len);
			line [line_len] = 0;
			list = g_list_append (list, line);

			ideal_cut_spot = NULL;
		} else
			used += len;

		if (*p == ' ')
			last_was_cut_point = TRUE;
		else
			last_was_cut_point = FALSE;
	}
	if (*line_begin){
		char *line;

		line_len = p - line_begin;
		line = g_malloc (line_len+1);
		memcpy (line, line_begin, line_len);
		line [line_len] = 0;
		list = g_list_append (list, line);
	}

	return list;
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
	font_height = cell->style->font->size;
		
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
		GList *lines, *l;
		int line_count, x_offset, y_offset;
		double inter_space;
		
		lines = cell_split_text (print_font, cell->text->str, cell->col->units);
		line_count = g_list_length (lines);
		
		{
			static int warn_shown;

			if (!warn_shown){
				g_warning ("Set clipping, multi-line");
				warn_shown = 1;
			}
		}

		switch (style->valign){
		case VALIGN_TOP:
			y_offset = 0;
			inter_space = font_height;
			break;
			
		case VALIGN_CENTER:
			y_offset = - ((cell->row->units - (line_count * font_height))/2);
			inter_space = font_height;
			break;
			
		case VALIGN_JUSTIFY:
			if (line_count > 1){
				y_offset = 0;
				inter_space = font_height + 
					(cell->row->units - (line_count * font_height))
					/ (line_count-1);
				break;
			} 
			/* Else, we become a VALIGN_BOTTOM line */
			
		case VALIGN_BOTTOM:
			y_offset = - (cell->row->units - (line_count * font_height));
			inter_space = font_height;
			break;
			
		default:
			g_warning ("Unhandled cell vertical alignment\n");
			y_offset = 0;
			inter_space = font_height;
		}

		gnome_print_setfont (context, print_font);

		y_offset -= font_height;
		for (l = lines; l; l = l->next){
			char *str = l->data;

			/* Why do we need this. it breaks multi-line indents */
			str = str_trim_spaces (str);

			switch (halign){
			case HALIGN_LEFT:
			case HALIGN_JUSTIFY:
				x_offset = cell->col->margin_a_pt;
				break;
				
			case HALIGN_RIGHT:
				x_offset = cell->col->units - cell->col->margin_b_pt -
					gnome_font_get_width_string (print_font, str);
				break;

			case HALIGN_CENTER:
				x_offset = (cell->col->units -
					    gnome_font_get_width_string (print_font, str)) / 2;
				break;
			default:
				g_warning ("Multi-line justification style not supported\n");
				x_offset = cell->col->margin_a_pt;
			}

			gnome_print_moveto (context, base_x + x_offset, base_y + y_offset);
			gnome_print_show (context, str);
			gnome_print_stroke (context);
			
			y_offset -= inter_space;
			g_free (l->data);
		}
		g_list_free (lines);
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

		{
			static int warn_shown;

			if (!warn_shown){
				g_warning ("Set clipping");
				warn_shown = 1;
			}
		}
		
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
	GdkColor *fore;
	
	g_assert (cell != NULL);

	gnome_print_setrgbcolor (context, 0, 0, 0);
	print_cell_border (context, cell, x, y);

	fore = &cell->style->fore_color->color;
	
	gnome_print_setrgbcolor (context, fore->red, fore->green, fore->blue);

	print_cell_text (context, cell,
			 x + cell->col->margin_a_pt,
			 y + cell->row->margin_b_pt);
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
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	y = 0;
	ci = NULL;
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
			
			x += ci->units + ci->margin_a_pt + ci->margin_b_pt;
		}
		y -= ri->units + ci->margin_a_pt + ci->margin_b_pt;
	}
}

static void
vline (GnomePrintContext *context, double x, double y1, double y2)
{
	gnome_print_moveto (context, x, y1);
	gnome_print_lineto (context, x, y2);
	gnome_print_stroke (context);
}

static void
hline (GnomePrintContext *context, double x1, double x2, double y)
{
	gnome_print_moveto (context, x1, y);
	gnome_print_lineto (context, x2, y);
	gnome_print_stroke (context);
}

void
print_cell_grid (GnomePrintContext *context,
		 Sheet *sheet, 
		 int start_col, int start_row,
		 int end_col, int end_row,
		 double base_x, double base_y,
		 double width, double height)
{
	int col, row;
	double x, y;
	
	g_return_if_fail (context != NULL);
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (context));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	gnome_print_setlinewidth (context, 0.5);

	end_col++;
	end_row++;
	
	x = base_x;
	for (col = start_col; col <= end_col; col++){
		ColRowInfo *ci = sheet_col_get_info (sheet, col);

		vline (context, x, base_y, base_y - height);
		x += ci->units + ci->margin_a_pt + ci->margin_b_pt;
	}

	y = base_y;
	for (row = start_row; row <= end_row; row++){
		ColRowInfo *ri = sheet_row_get_info (sheet, row);

		hline (context, base_x, base_x + width, y);
		y -= ri->units + ri->margin_a_pt + ri->margin_b_pt;
	}
}



