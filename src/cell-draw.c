/*
 * cell-draw.c: Cell drawing on screen
 *
 * Author:
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "eval.h"
#include "format.h"
#include "color.h"

static void
draw_overflow (GdkDrawable *drawable, GdkGC *gc, GdkFont *font, int x1, int y1, int text_base, int width, int height)
{
	GdkRectangle rect;
	int len = gdk_string_width (font, "#");
	int total, offset;
	
	rect.x = x1;
	rect.y = y1;
	rect.width = width;
	rect.height = height;
	gdk_gc_set_clip_rectangle (gc, &rect);
	
	offset = x1 + width - len;
	for (total = len;  offset > len; total += len){
		gdk_draw_text (drawable, font, gc, x1 + offset, text_base, "#", 1);
		offset -= len;
	} 
}

static GList *
cell_split_text (GdkFont *font, char *text, int width)
{
	GList *list;
	char *p, *line, *line_begin, *ideal_cut_spot = NULL;
	int  line_len, used, last_was_cut_point;

	list = NULL;
	used = 0;
	last_was_cut_point = FALSE;
	for (line_begin = p = text; *p; p++){
		int len;

		if (last_was_cut_point && *p != ' ')
			ideal_cut_spot = p;

		len = gdk_text_width (font, p, 1);

		/* If we have overflowed, do the wrap */
		if (used + len > width){
			char *begin = line_begin;
			
			if (ideal_cut_spot){
				int n = p - ideal_cut_spot + 1;

				line_len = ideal_cut_spot - line_begin;
				used = gdk_text_width (font, ideal_cut_spot, n);
				line_begin = ideal_cut_spot;
			} else {
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
		line_len = p - line_begin;
		line = g_malloc (line_len+1);
		memcpy (line, line_begin, line_len);
		line [line_len] = 0;
		list = g_list_append (list, line);
	}

	return list;
}

static GdkFont *
sheet_view_font (SheetView *sheet_view, Cell *cell)
{
	return style_font_gdk_font (cell->style->font);
}

/*
 * Returns the number of columns used for the draw
 */
int 
cell_draw (Cell *cell, SheetView *sv, GdkGC *gc, GdkDrawable *drawable, int x1, int y1)
{
	Style        *style = cell->style;
	GdkFont      *font;
	SheetView    *sheet_view = sv;
	GdkGC        *white_gc = GTK_WIDGET (sheet_view->sheet_view)->style->white_gc;
	GdkRectangle rect;
	
	int start_col, end_col;
	int width, height;
	int text_base;
	int font_height;
	int halign;
	int do_multi_line;
	char *text;

	font = sheet_view_font (sheet_view, cell);
	text_base = y1 + cell->row->pixels - cell->row->margin_b - font->descent;
	
	cell_get_span (cell, &start_col, &end_col);

	width  = COL_INTERNAL_WIDTH (cell->col);
	height = ROW_INTERNAL_HEIGHT (cell->row);

	font_height = style_font_get_height (cell->style->font);
	
	halign = cell_get_horizontal_align (cell);

	/* if a number overflows, do special drawing */
	if (width < cell->width && CELL_IS_NUMBER (cell)){
		draw_overflow (drawable, gc, font, x1 + cell->col->margin_a, y1, text_base,
			       width, height);
		return 1;
	}

	if (halign == HALIGN_JUSTIFY || style->valign == VALIGN_JUSTIFY || style->fit_in_cell)
		do_multi_line = TRUE;
	else
		do_multi_line = FALSE;

	text = cell->text->str;
	
	if (do_multi_line){
		GList *lines, *l;
		int cell_pixel_height = ROW_INTERNAL_HEIGHT (cell->row);
		int line_count, x_offset, y_offset, inter_space;
		
		lines = cell_split_text (font, text, width);
		line_count = g_list_length (lines);

		rect.x = x1;
		rect.y = y1;
		rect.height = cell->row->pixels + 1;
		rect.width = cell->col->pixels  + 1;
		gdk_gc_set_clip_rectangle (gc, &rect);
		
		switch (style->valign){
		case VALIGN_TOP:
			y_offset = 0;
			inter_space = font_height;
			break;
			
		case VALIGN_CENTER:
			y_offset = (cell_pixel_height - (line_count * font_height))/2;
			inter_space = font_height;
			break;
			
		case VALIGN_JUSTIFY:
			if (line_count > 1){
				y_offset = 0;
				inter_space = font_height + 
					(cell_pixel_height - (line_count * font_height))
					/ (line_count-1);
				break;
			} 
			/* Else, we become a VALIGN_BOTTOM line */
			
		case VALIGN_BOTTOM:
			y_offset = cell_pixel_height - (line_count * font_height);
			inter_space = font_height;
			break;
			
		default:
			g_warning ("Unhandled cell vertical alignment\n");
			y_offset = 0;
			inter_space = font_height;
		}

		y_offset += font_height - 1;
		for (l = lines; l; l = l->next){
			char *str = l->data;

			str = str_trim_spaces (str);
			
			switch (halign){
			case HALIGN_LEFT:
			case HALIGN_JUSTIFY:
				x_offset = cell->col->margin_a;
				break;
				
			case HALIGN_RIGHT:
				x_offset = cell->col->pixels - cell->col->margin_b -
					gdk_string_width (font, str);
				break;

			case HALIGN_CENTER:
				x_offset = (cell->col->pixels - gdk_string_width (font, str)) / 2;
				break;
			default:
				g_warning ("Multi-line justification style not supported\n");
				x_offset = cell->col->margin_a;
			}
			/* Advance one pixel for the border */
			x_offset++;
			gc = GTK_WIDGET (sheet_view->sheet_view)->style->black_gc;
			gdk_draw_text (drawable, font, gc, x1 + x_offset, y1 + y_offset, str, strlen (str));
			y_offset += inter_space;
			
			g_free (l->data);
		}
		g_list_free (lines);
		
	} else {
		int x, diff, total, len;

		/*
		 * x1, y1 are relative to this cell origin, but the cell might be using
		 * columns to the left (if it is set to right justify or center justify)
		 * compute the pixel difference 
		 */
		if (start_col != cell->col->pos)
			diff = -sheet_col_get_distance (cell->sheet, start_col, cell->col->pos);
		else
			diff = 0;

		/* This rectangle has the whole area used by this cell */
		rect.x = x1 + 1 + diff;
		rect.y = y1 + 1;
		rect.width  = sheet_col_get_distance (cell->sheet, start_col, end_col+1) - 1;
		rect.height = cell->row->pixels - 1;
		
		/* Set the clip rectangle */
		gdk_gc_set_clip_rectangle (gc, &rect);
		gdk_draw_rectangle (drawable, white_gc, TRUE,
				    rect.x, rect.y, rect.width, rect.height);

		len = 0;
		switch (halign){
		case HALIGN_FILL:
			printf ("FILL!\n");
			len = gdk_string_width (font, text);
			/* fall down */
			
		case HALIGN_LEFT:
			x = cell->col->margin_a;
			break;

		case HALIGN_RIGHT:
			x = cell->col->pixels - cell->col->margin_b - cell->width;
			break;

		case HALIGN_CENTER:
			x = (cell->col->pixels - cell->width)/2;
			break;
			
		default:
			g_warning ("Single-line justification style not supported\n");
			x = cell->col->margin_a;
			break;
		}

		total = 0;
		do {
			gdk_draw_text (drawable, font, gc, 1 + x1 + x, text_base, text, strlen (text));
			x1 += len;
			total += len;
		} while (halign == HALIGN_FILL && total < rect.width);
	}

	return end_col - start_col + 1;
}

