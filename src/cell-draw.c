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
#include "utils.h"
#include "pattern.h"

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

/*
 * WARNING : This code is an almost exact duplicate of
 *          print-cell.c:cell_split_text
 * Try to keep it that way.
 */
static GList *
cell_split_text (GdkFont *font, char const *text, int const width)
{
	char const *p, *line_begin;
	char const *first_whitespace = NULL;
	char const *last_whitespace = NULL;
	gboolean prev_was_space = FALSE;
	GList *list = NULL;
	int used = 0, used_last_space = 0;

	for (line_begin = p = text; *p; p++){
		int const len_current = gdk_text_width (font, p, 1);

		/* Wrap if there is an embeded newline, or we have overflowed */
		if (*p == '\n' || used + len_current > width){
			char const *begin = line_begin;
			int len;

			if (*p == '\n'){
				/* start after newline, preserve whitespace */
				line_begin = p+1;
				len = p - begin;
				used = 0;
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

static inline StyleFont *
sheet_get_style_font (const Sheet *sheet, MStyle *mstyle)
{
	double     zoom   = sheet->last_zoom_factor_used;
	StyleFont *font   = mstyle_get_font (mstyle, zoom);

	return font;
}

/*
 * Returns the number of columns used for the draw
 */
int 
cell_draw (Cell *cell, SheetView *sheet_view, GdkGC *gc,
	   GdkDrawable *drawable, int x1, int y1)
{
	MStyle       *mstyle = cell_get_mstyle (cell);
	GdkFont      *font;
	StyleFont    *style_font;
	GdkColor     *col;
	GdkRectangle  rect;
	GnumericSheet *gsheet;
	GnomeCanvas *canvas;
	
	int start_col, end_col;
	int width, height;
	int text_base;
	int font_height;
	int halign;
	int do_multi_line;
	char *text;

	gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	g_return_val_if_fail (GNUMERIC_IS_SHEET (gsheet), 1);
	canvas = GNOME_CANVAS (gsheet);

	style_font = sheet_get_style_font (cell->sheet, mstyle);
	font = style_font_gdk_font (style_font);

	text_base = y1 + cell->row->pixels - font->descent;
	
	cell_get_span (cell, &start_col, &end_col);

	width  = COL_INTERNAL_WIDTH (cell->col);
	height = ROW_INTERNAL_HEIGHT (cell->row);

	font_height = style_font_get_height (style_font);
	
	halign = cell_get_horizontal_align (cell, mstyle_get_align_h (mstyle));

	/* if a number overflows, do special drawing */
	if (width < cell->width && cell_is_number (cell)) {
		draw_overflow (drawable, gc, font,
			       x1 + cell->col->margin_a,
			       y1, text_base,
			       width, height);
		style_font_unref (style_font);
		mstyle_unref (mstyle);
		return 1;
	}

	if (halign == HALIGN_JUSTIFY ||
	    mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY ||
	    mstyle_get_fit_in_cell (mstyle))
		do_multi_line = TRUE;
	else
		do_multi_line = FALSE;

	if (cell && cell->text && cell->text->str)
		text = cell->text->str;
	else {
		printf ("Serious cell error at '%s'\n", cell_name (cell->col->pos,
								   cell->row->pos));
		text = "FATAL ERROR";
	} 
	
	if (do_multi_line) {
		GList *lines, *l;
		int cell_pixel_height = ROW_INTERNAL_HEIGHT (cell->row);
		int line_count, x_offset, y_offset, inter_space;
		
		lines = cell_split_text (font, text, width);
		line_count = g_list_length (lines);

		rect.x = x1 + 1;
		rect.y = y1 + 1;
		rect.width = cell->col->pixels  - 1;
		rect.height = cell->row->pixels - 1;
		/* Set the clip rectangle */
		gdk_gc_set_clip_rectangle (gc, &rect);
		
		if (gnumeric_background_set_gc (mstyle, gc, canvas) || end_col != start_col)
			gdk_draw_rectangle (drawable, gc, TRUE,
					    rect.x, rect.y, rect.width, rect.height);

		/* And now reset the previous foreground color */
		gdk_gc_set_fill (gc, GDK_SOLID);
		if (cell->render_color)
			gdk_gc_set_foreground (gc, &cell->render_color->color);
		else {
			col = &mstyle_get_color (mstyle, MSTYLE_COLOR_FORE)->color;
			gdk_gc_set_foreground (gc, col);
		}

		switch (mstyle_get_align_v (mstyle)) {
		case VALIGN_TOP:
			y_offset = 0;
			inter_space = font_height;
			break;
			
		case VALIGN_CENTER:
			y_offset = (cell_pixel_height -
				    (line_count * font_height)) / 2;
			inter_space = font_height;
			break;
			
		case VALIGN_JUSTIFY:
			if (line_count > 1) {
				y_offset = 0;
				inter_space = font_height + 
					(cell_pixel_height - (line_count * font_height))
					/ (line_count - 1);
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
		for (l = lines; l; l = l->next) {
			char const * const str = l->data;

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
				x_offset = (cell->col->pixels -
					    gdk_string_width (font, str)) / 2;
				break;
			default:
				g_warning ("Multi-line justification style not supported\n");
				x_offset = cell->col->margin_a;
			}
			/* Advance one pixel for the border */
			x_offset++;
			gc = GTK_WIDGET (sheet_view->sheet_view)->style->black_gc;
			gdk_draw_text (drawable, font, gc, x1 + x_offset,
				       y1 + y_offset, str, strlen (str));
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
			diff = -sheet_col_get_distance (cell->sheet,
							start_col, cell->col->pos);
		else
			diff = 0;

		/* This rectangle has the whole area used by this cell */
		rect.x = x1 + 1 + diff;
		rect.y = y1 + 1;
		rect.width  = sheet_col_get_distance (cell->sheet,
						      start_col, end_col + 1) - 1;
		rect.height = cell->row->pixels - 1;
		
		/* Set the clip rectangle */
		gdk_gc_set_clip_rectangle (gc, &rect);

		if (gnumeric_background_set_gc (mstyle, gc, canvas) || end_col != start_col)
			gdk_draw_rectangle (drawable, gc, TRUE,
					    rect.x, rect.y, rect.width, rect.height);

		/* And now reset the previous foreground color */
		gdk_gc_set_fill (gc, GDK_SOLID);
		if (cell->render_color)
			gdk_gc_set_foreground (gc, &cell->render_color->color);
		else {
			col = &mstyle_get_color (mstyle, MSTYLE_COLOR_FORE)->color;
			gdk_gc_set_foreground (gc, col);
		}

		len = 0;
		switch (halign) {
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
			x = (cell->col->pixels - cell->width) / 2;
			break;
			
		default:
			g_warning ("Single-line justification style not supported\n");
			x = cell->col->margin_a;
			break;
		}

		total = 0;
		do {
			gdk_draw_text (drawable, font, gc, 1 + x1 + x,
				       text_base, text, strlen (text));
			x1 += len;
			total += len;
		} while (halign == HALIGN_FILL && total < rect.width && len > 0);
	}

	style_font_unref (style_font);
	mstyle_unref (mstyle);

	return end_col - start_col + 1;
}
