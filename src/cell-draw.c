/*
 * cell-draw.c: Cell drawing on screen
 *
 * Author:
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 */
#include <config.h>
#include <gdk/gdk.h>
#include "cell-draw.h"
#include "style.h"
#include "cell.h"
#include "workbook.h"
#include "sheet-view.h" /* FIXME : Only for sheet_view_get_style_font */
#include "utils.h"	/* FIXME : Only for cell_name */

static inline void
draw_text (GdkDrawable *drawable, GdkFont *font, GdkGC *gc,
	   int x1, int text_base, char const * text, int n, int len_pixels,
	   int const * const line_offset, int num_lines)
{
	gdk_draw_text (drawable, font, gc, x1, text_base, text, n);

	/* FIXME how to handle small fonts ?
	 * the text_base should be at least 2 pixels above the bottom */
	while (--num_lines >= 0) {
		int y = text_base + line_offset[num_lines];
		gdk_draw_line (drawable, gc, x1, y, x1+len_pixels, y);
	}
}

static void
draw_overflow (GdkDrawable *drawable, GdkGC *gc, GdkFont *font, 
	       int x1, int text_base, int width,
	       int const * const line_offset, int num_lines)
{
	int const len = gdk_string_width (font, "#");
	int count = (len != 0) ? (width / len) : 0;

	/* Center */
	for (x1 += (width - count*len) / 2; --count >= 0 ; x1 += len )
		draw_text (drawable, font, gc, x1, text_base, "#", 1, len,
			   line_offset, num_lines);
}

/*
 * WARNING : This code is an almost exact duplicate of
 *          print-cell.c:cell_split_text
 * and is very similar to
 *          cell.c:calc_text_dimensions
 *
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

/*
 *             G      G
 *             r      r
 *             i      i
 *             d      d
 *
 *  Grid line  a------+
 *             |mmmmmm|
 *             |m    m|
 *             |mmmmmm|
 *  Grid line  +------+
 *
 *  'm' == margin
 *  ' ' == space for contents
 *
 * @x1 : The pixel coord within the drawable of the upper left corner
 *       of the gridlines (marked a).
 * @y1 : The pixel coord within the drawable of the upper left corner
 *       of the gridlines (marked a).
 */
void 
cell_draw (Cell *cell, MStyle *mstyle, CellSpanInfo const * const spaninfo,
	   GdkGC *gc, GdkDrawable *drawable, int x1, int y1)
{
	StyleFont    *style_font;
	GdkFont      *font;
	GdkRectangle  rect;
	
	Sheet        *sheet = cell->sheet;
	int start_col, end_col;
	int width, height;
	int text_base;
	int font_height;
	int halign;
	int num_lines = 0;
	int line_offset[3]; /* There are up to 3 lines, double underlined strikethroughs */
	gboolean is_single_line;
	char const *text;
	StyleColor   *fore;

	/*
	 * If it is being edited pretend it is empty to avoid problems with the
	 * a long cells contents extending past the edge of the edit box.
	 * Don't print zeros if they should be ignored.
	 */
	if (cell == sheet->workbook->editing_cell ||
	    (!sheet->display_zero && cell_is_zero (cell)))
		return;

	g_return_if_fail (cell->text);

	if (cell->text->str == NULL) {
		g_warning ("Serious cell error at '%s'\n",
			   cell_name (cell->col->pos, cell->row->pos));
		/* This can occur when eg. a plugin function fires up a dialog */
		text = "Pending";
	} else
		text = cell->text->str;
	
	if (spaninfo != NULL) {
		start_col = spaninfo->left;
		end_col = spaninfo->right;
	} else
		start_col = end_col = cell->col->pos;

	/* Get the sizes exclusive of margins and grids */
	width  = COL_INTERNAL_WIDTH (cell->col);
	height = ROW_INTERNAL_HEIGHT (cell->row);

	style_font = sheet_view_get_style_font (sheet, mstyle);
	font = style_font_gdk_font (style_font);
	font_height = style_font_get_height (style_font);
	
	switch (mstyle_get_align_v (mstyle)) {
	default:
		g_warning ("Unhandled cell vertical alignment\n");

	case VALIGN_JUSTIFY:
	case VALIGN_TOP:
		/*
		 * y1 == top grid line.
		 * add top margin
		 * add font ascent
		 */
		text_base = y1 + font->ascent + cell->row->margin_a;
		break;
		
	case VALIGN_CENTER:
		text_base = y1 + font->ascent + cell->row->margin_a +
		    (height - font_height) / 2;
		break;
		
	case VALIGN_BOTTOM:
		/*
		 * y1+row->size_pixels == bottom grid line.
		 * subtract bottom margin
		 * subtract font descent
		 */
		text_base = y1 + cell->row->size_pixels - font->descent - cell->row->margin_b;
		break;
	}
	
	halign = cell_get_horizontal_align (cell, mstyle_get_align_h (mstyle));

	is_single_line = (halign != HALIGN_JUSTIFY &&
			  mstyle_get_align_v (mstyle) != VALIGN_JUSTIFY &&
			  !mstyle_get_fit_in_cell (mstyle));

	/* This rectangle has the whole area used by this cell
	 * including the surrounding grid lines */
	rect.x = x1;
	rect.y = y1;
	rect.width  = cell->col->size_pixels + 1;
	rect.height = cell->row->size_pixels + 1;

	/*
	 * x1, y1 are relative to this cell origin, but the cell might be using
	 * columns to the left (if it is set to right justify or center justify)
	 * compute the pixel difference 
	 */
	if (start_col != cell->col->pos) {
		int const offset =
		    sheet_col_get_distance_pixels (sheet,
						   start_col, cell->col->pos);
		rect.x     -= offset;
		rect.width += offset;
	}
	if (end_col != cell->col->pos) {
		int const offset =
		    sheet_col_get_distance_pixels (sheet,
						   cell->col->pos+1, end_col+1);
		rect.width += offset;
	}

	/* Do not allow text to impinge upon the grid lines or margins
	 * FIXME : Should use margins from start_col and end_col
	 */
	rect.x += 1 + cell->col->margin_a;
	rect.y += 1 + cell->row->margin_a;
	rect.width -= 2 + cell->col->margin_a + cell->col->margin_b;
	rect.height -= 2 + cell->row->margin_a + cell->row->margin_b;
	gdk_gc_set_clip_rectangle (gc, &rect);

	/* Set the font colour */
	gdk_gc_set_fill (gc, GDK_SOLID);
	fore = cell->render_color;
	if (fore == NULL)
		fore = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE);
	g_return_if_fail (fore != NULL); /* Be extra careful */
	gdk_gc_set_foreground (gc, &fore->color);

	/* Handle underlining and strikethrough */
	switch (mstyle_get_font_uline (mstyle)) {
	case UNDERLINE_SINGLE : num_lines = 1;
				line_offset[0] = 1;
				break;

	case UNDERLINE_DOUBLE : num_lines = 2;
				line_offset[0] = 0;
				line_offset[1] = 2;

	default :
				break;
	};
	if (mstyle_get_font_strike (mstyle))
		line_offset[num_lines++] = font->ascent/-2;

	/* if a number overflows, do special drawing */
	if (width < cell->width_pixel && cell_is_number (cell)) {
		draw_overflow (drawable, gc, font,
			       x1 + cell->col->margin_a + 1,
			       text_base, width, line_offset, num_lines);
		style_font_unref (style_font);
		return;
	}

	if (is_single_line) {
		int total, len = cell->width_pixel;

		switch (halign) {
		case HALIGN_FILL:
			printf ("FILL!\n");
			/* fall through */
			
		case HALIGN_LEFT:
			x1 += 1 + cell->col->margin_a;
			break;

		case HALIGN_RIGHT:
			x1 += cell->col->size_pixels - cell->col->margin_b - cell->width_pixel;
			break;

		case HALIGN_CENTER:
			x1 += 1 + cell->col->margin_a + (width - cell->width_pixel) / 2; 
			break;
			
		case HALIGN_CENTER_ACROSS_SELECTION:
			x1 = rect.x + (rect.width - cell->width_pixel) / 2; 
			break;

		default:
			g_warning ("Single-line justification style not supported\n");
			x1 += 1 + cell->col->margin_a;
		}

		total = 0;
		do {
			draw_text (drawable, font, gc, x1, text_base,
				   text, strlen (text), len, line_offset, num_lines);
			x1 += len;
			total += len;
		} while (halign == HALIGN_FILL && total < rect.width && len > 0);
	} else {
		GList *l, *lines = cell_split_text (font, text, width);
		int line_count = g_list_length (lines);
		int x_offset, y_offset, inter_space;

		switch (mstyle_get_align_v (mstyle)) {
		case VALIGN_TOP:
			y_offset = 0;
			inter_space = font_height;
			break;
			
		case VALIGN_CENTER:
			y_offset = (height -
				    (line_count * font_height)) / 2;
			inter_space = font_height;
			break;
			
		case VALIGN_JUSTIFY:
			if (line_count > 1) {
				y_offset = 0;
				inter_space = font_height + 
					(height - (line_count * font_height))
					/ (line_count - 1);
				break;
			} 
			/* Else, we become a VALIGN_BOTTOM line */
			
		case VALIGN_BOTTOM:
			y_offset = height - (line_count * font_height);
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
			int len = 0;

			switch (halign){
			default:
				g_warning ("Multi-line justification style not supported\n");
				/* fall through */

			case HALIGN_LEFT:
			case HALIGN_JUSTIFY:
				x_offset = cell->col->margin_a;
				if (num_lines > 0)
					len = gdk_string_width (font, text);
				break;
				
			case HALIGN_RIGHT:
				len = gdk_string_width (font, str);
				x_offset = cell->col->size_pixels - cell->col->margin_b - len;
				break;

			case HALIGN_CENTER:
			case HALIGN_CENTER_ACROSS_SELECTION:
				len = gdk_string_width (font, str);
				x_offset = (cell->col->size_pixels - len) / 2;
			}

			/* Advance one pixel for the border */
			x_offset++;
			draw_text (drawable, font, gc,
				   x1 + x_offset, y1 + y_offset,
				   str, strlen (str), len, line_offset, num_lines);
			y_offset += inter_space;
			
			g_free (l->data);
		}
		g_list_free (lines);
		
	}

	style_font_unref (style_font);
}
