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

#define DIM(cri) (cri->units + cri->margin_a_pt + cri->margin_b_pt)

#define CELL_DIM(cell,p) DIM (cell->p)
#define CELL_HEIGHT(cell) CELL_DIM(cell,row)
#define CELL_WIDTH(cell)  CELL_DIM(cell,col)

#if 0
/* FIXME: need border support. */
static void
print_border (GnomePrintContext *pc, double x1, double y1, double x2, double y2,
	      StyleBorder *b, int idx)
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

	default :
		return;
	}
	gnome_print_setlinewidth (pc, width);
	gnome_print_moveto (pc, x1, y1);
	gnome_print_lineto (pc, x2, y2);
	gnome_print_stroke (pc);

	if (!dupit)
		return;

	if (x1 == x2) {
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
print_cell_border (GnomePrintContext *context, StyleBorder *border,
		   double x1, double y1, double x2, double y2)
{
	int i;
	
	for (i = 0; i < STYLE_ORIENT_MAX; ++i)
		print_border (context, x1, y1, x1, y2, border, i);
}
#endif

static void
print_overflow (GnomePrintContext *context, Cell *cell)
{
}

/*
 * WARNING : This code is an almost exact duplicate of
 *          cell-draw.c:cell_split_text
 * Try to keep it that way.
 */
static GList *
cell_split_text (GnomeFont *font, char const *text, int const width)
{
	char const *p, *line_begin;
	char const *first_whitespace = NULL;
	char const *last_whitespace = NULL;
	gboolean prev_was_space = FALSE;
	GList *list = NULL;
	double used = 0., used_last_space = 0.;

	for (line_begin = p = text; *p; p++){
		double const len_current =
			gnome_font_get_width_string_n (font, p, 1);

		/* Wrap if there is an embeded newline, or we have overflowed */
		if (*p == '\n' || used + len_current > width){
			char const *begin = line_begin;
			int len;

			if (*p == '\n'){
				/* start after newline, preserve whitespace */
				line_begin = p+1;
				len = p - begin;
				used = 0.;
			} else if (last_whitespace != NULL){
				/* Split at the run of whitespace */
				line_begin = last_whitespace + 1;
				len = first_whitespace - begin;
				used = len_current + used - used_last_space;
			} else {
				/* Split before the current character */
				line_begin = p; /* next line starts here */
				len = p - begin;
				used = len_current;
			}

			list = g_list_append (list, g_strndup (begin, len));
			first_whitespace = last_whitespace = NULL;
			prev_was_space = FALSE;
			continue;
		}

		used += len_current;
		if (*p == ' '){
			used_last_space = used;
			last_whitespace = p;
			if (!prev_was_space)
				first_whitespace = p;
			prev_was_space = TRUE;
		} else
			prev_was_space = FALSE;
	}

	/* Catch the final bit that did not wrap */
	if (*line_begin)
		list = g_list_append (list,
				      g_strndup (line_begin, p - line_begin));

	return list;
}

static void
print_cell_text (GnomePrintContext *context, Cell *cell, double base_x, double base_y)
{
	Style *style = cell_get_style (cell);
	GnomeFont *print_font = style->font->font;
	double text_width;
	double font_height;
	gboolean do_multi_line;
	int start_col, end_col;
	int halign;
	
	cell_get_span (cell, &start_col, &end_col);
	
	text_width = gnome_font_get_width_string (print_font, cell->text->str);
	font_height = style->font->size;
		
	if (text_width > cell->col->units && cell_is_number (cell)) {
		print_overflow (context, cell);
		style_unref (style);
		return;
	}

	halign = cell_get_horizontal_align (cell, style->halign);
	if (halign == HALIGN_JUSTIFY || style->valign == VALIGN_JUSTIFY || style->fit_in_cell)
		do_multi_line = TRUE;
	else
		do_multi_line = FALSE;

	if (do_multi_line) {
		GList *lines, *l;
		int line_count, x_offset, y_offset;
		double inter_space;
		
		lines = cell_split_text (print_font, cell->text->str,
					 cell->col->units);
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
			char const * const str = l->data;

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
		} while (halign == HALIGN_FILL && total < cell->col->units && len > 0);
	}
	style_unref (style);
}

static void
print_cell_background (GnomePrintContext *context, StyleColor *back,
		       double x1, double y1, double x2, double y2)
{
	gnome_print_setrgbcolor (
		context,
		back->red   / (double) 0xffff,
		back->green / (double) 0xffff,
		back->blue  / (double) 0xffff);

	gnome_print_moveto (context, x1, y1);
	gnome_print_lineto (context, x1, y2);
	gnome_print_lineto (context, x2, y2);
	gnome_print_lineto (context, x2, y1);
	gnome_print_lineto (context, x1, y1);
	gnome_print_fill (context);
}

static void
print_cell (GnomePrintContext *context, Cell *cell,
	    double x1, double y1, double x2, double y2)
{
	Style *style = cell_get_style (cell);
	StyleColor *fore = style->fore_color;
	
	g_assert (cell != NULL);

	gnome_print_setrgbcolor (context, 0, 0, 0);
/*	print_cell_border (context, style->border, x1, y1, x2, y2); */
	print_cell_background (context, style->back_color, x1, y1, x2, y2);
	
	gnome_print_setrgbcolor (context,
				 fore->red   / (double) 0xfff,
				 fore->green / (double) 0xffff,
				 fore->blue  / (double) 0xffff);

	print_cell_text (context, cell,
			 x1 + cell->col->margin_a_pt,
			 y1 + cell->row->margin_b_pt);
	style_unref (style);
}

static void
print_empty_cell (GnomePrintContext *context, Sheet *sheet, int col, int row,
		  double x1, double y1, double x2, double y2)
{
	MStyle *mstyle;

	mstyle = sheet_style_compute (sheet, col, row);
	print_cell_background (context,
			       mstyle_get_color (mstyle, MSTYLE_COLOR_BACK),
			       x1, y1, x2, y2);
	
	mstyle_unref (mstyle);
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
	for (row = start_row; row <= end_row; row++){
		ri = sheet_row_get_info (sheet, row);

		x = 0;
		for (col = start_col; col <= end_col; col++){
			double x1 = base_x + x;
			double y1 = base_y + y;
			Cell *cell;

			cell = sheet_cell_get (sheet, col, row);
			if (cell){
				double x2 = x1 + CELL_WIDTH (cell);
				double y2 = y1 - CELL_HEIGHT (cell);

				ci = cell->col;
				print_cell (context, cell, x1, y1, x2, y2);
			} else {
				double x2, y2;
					
				ci = sheet_col_get_info (sheet, col);
				x2 = x1 + DIM (ci);
				y2 = y1 - DIM (ri);

				print_empty_cell (context, sheet, col, row, x1, y1, x2, y2);
			}
			
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



