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
#include "style-color.h"
#include "cell.h"
#include "sheet.h"
#include "str.h"
#include "value.h"
#include "workbook.h"
#include "rendered-value.h"
#include "sheet-control-gui.h" /* FIXME : Only for scg_get_style_font */
#include "parse-util.h"

#include <ctype.h>

static inline void
draw_text (GdkDrawable *drawable, GdkFont *font, GdkGC *gc,
	   int x1, int text_base, char const * text, int n, int len_pixels,
	   int const * const line_offset, int num_lines)
{
	/* Some Xservers crash when asked to draw strings that are too long
	 * add an arbitrary limit to keep things simple
	 */
	if (n > 1024)
		n = 1024;

	gdk_draw_text (drawable, font, gc, x1, text_base, text, n);

	/* FIXME how to handle small fonts ?
	 * the text_base should be at least 2 pixels above the bottom */
	while (--num_lines >= 0) {
		int y = text_base + line_offset [num_lines];
		gdk_draw_line (drawable, gc, x1, y, x1+len_pixels, y);
	}
}

static void
draw_overflow (GdkDrawable *drawable, GdkGC *gc, GdkFont *font,
	       int x1, int text_base, int width,
	       int const * const line_offset, int num_lines)
{
	int const len = gdk_string_width (font, "#");
	int count = 0;

	if (len != 0)  {
		count = width / len;
		if (count == 0)
			count = 1;
	}

	/* Center */
	for (x1 += (width - count*len) / 2; --count >= 0 ; x1 += len )
		draw_text (drawable, font, gc, x1, text_base, "#", 1, len,
			   line_offset, num_lines);
}

/*
 * WARNING : This code is an almost exact duplicate of
 *          print-cell.c:cell_split_text
 * and is very similar to
 *          rendered-value.c:rendered_value_calc_size_ext
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

	for (line_begin = p = text; *p; p++) {
		int const len_current = gdk_text_width (font, p, 1);

		/* Wrap if there is an embeded newline, or we have overflowed */
		if (*p == '\n' || used + len_current > width) {
			char const *begin = line_begin;
			int len;

			if (*p == '\n') {
				/* start after newline, preserve whitespace */
				line_begin = p+1;
				len = p - begin;
				used = 0;
			} else if (last_whitespace != NULL) {
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
		if (*p == '-') {
			used_last_space = used;
			last_whitespace = p;
			first_whitespace = p+1;
			prev_was_space = TRUE;
		} else if (isspace (*(unsigned char *)p)) {
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
 * @h_center : The number of pixels from x1 marking the logical center
 *             of the cell.  NOTE This can be asymetric.  Passing
 *             <= 0 will use width / 2
 */
void
cell_draw (Cell const *cell, MStyle const *mstyle,
	   GdkGC *gc, GdkDrawable *drawable,
	   int x1, int y1, int width, int height, int h_center)
{
	StyleFont    *style_font;
	GdkFont      *font;
	GdkRectangle  rect;

	Sheet const * const sheet = cell->base.sheet;
	ColRowInfo const * const ci = cell->col_info; /* DEPRECATED */
	ColRowInfo const * const ri = cell->row_info; /* DEPRECATED */
	int text_base;
	int font_height;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	int num_lines = 0;
	int line_offset [3]; /* There are up to 3 lines, double underlined strikethroughs */
	char const *text;
	StyleColor   *fore;
	int cell_width_pixel, indent;

	/* Don't print zeros if they should be ignored. */
	if (sheet && sheet->hide_zero && cell_is_zero (cell) &&
	    (!sheet->display_formulas || !cell_has_expr (cell)))
		return;

	if (cell->rendered_value == NULL)
		cell_render_value ((Cell *)cell, TRUE);

	g_return_if_fail (cell->rendered_value->rendered_text);

	if (cell->rendered_value->rendered_text->str == NULL) {
		g_warning ("Serious cell error at '%s'\n", cell_name (cell));
		/* This can occur when eg. a plugin function fires up a dialog */
		text = "Pending";
	} else
		text = cell->rendered_value->rendered_text->str;

	/* Get the sizes exclusive of margins and grids */
	/* FIXME : all callers will eventually pass in their cell size */
	if (width < 0) /* DEPRECATED */
		width  = ci->size_pixels - (ci->margin_b + ci->margin_a + 1);
	if (height < 0) /* DEPRECATED */
		height = ri->size_pixels - (ri->margin_b + ri->margin_a + 1);

	/* This rectangle has the whole area used by this cell
	 * excluding the surrounding grid lines and margins */
	if (width <= 0 || height <= 0)
		return;

	rect.x = x1 + 1 + ci->margin_a;
	rect.y = y1 + 1 + ri->margin_a;
	rect.width = width + 1;
	rect.height = height + 1;

	style_font = scg_get_style_font (sheet, mstyle);
	font = style_font_gdk_font (style_font);
	font_height = style_font_get_height (style_font);
	valign = mstyle_get_align_v (mstyle);

	switch (valign) {
	default:
		g_warning ("Unhandled cell vertical alignment\n");

	case VALIGN_JUSTIFY:
	case VALIGN_TOP:
		/*
		 * rect.y == first pixel past margin
		 * add font ascent
		 */
		text_base = rect.y + font->ascent;
		break;

	case VALIGN_CENTER:
		text_base = rect.y + font->ascent +
		    (height - font_height) / 2;
		break;

	case VALIGN_BOTTOM:
		/*
		 * rect.y == first pixel past margin
		 * add height == first pixel in lower margin
		 * subtract font descent
		 */
		text_base = rect.y + height - font->descent;
		break;
	}

	gdk_gc_set_clip_rectangle (gc, &rect);

	/* Set the font colour */
	gdk_gc_set_fill (gc, GDK_SOLID);
	fore = cell->rendered_value->render_color;
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

	cell_width_pixel = cell_rendered_width (cell);
	indent = cell_rendered_offset (cell);

	/* if a number overflows, do special drawing */
	if ((cell_width_pixel + indent) > width && cell_is_number (cell) &&
	    sheet && !sheet->display_formulas) {
		draw_overflow (drawable, gc, font, rect.x,
			       text_base, width, line_offset, num_lines);
		style_font_unref (style_font);
		return;
	}

	halign = style_default_halign (mstyle, cell);
	if (halign == HALIGN_CENTER_ACROSS_SELECTION || h_center <= 0)
		h_center = width / 2;

	if (halign != HALIGN_JUSTIFY && valign != VALIGN_JUSTIFY &&
	    !mstyle_get_wrap_text (mstyle)) {
		int x, total, len = cell_width_pixel;

		switch (halign) {
		case HALIGN_FILL: /* fall through */
		case HALIGN_LEFT:
			x = rect.x + indent;
			break;

		case HALIGN_RIGHT:
			x = rect.x + rect.width - 1 - cell_width_pixel - indent;
			break;

		case HALIGN_CENTER:
		case HALIGN_CENTER_ACROSS_SELECTION:
			x = rect.x + h_center - cell_width_pixel / 2;
			break;

		default:
			g_warning ("Single-line justification style not supported\n");
			x = rect.x;
		}

		total = len; /* don't include partial copies after the first */
		do {
			draw_text (drawable, font, gc, x, text_base,
				   text, strlen (text), len, line_offset, num_lines);
			x += len;
			total += len;
		} while (halign == HALIGN_FILL && total < rect.width && len > 0);
	} else {
		GList *lines, *l;
		int line_count;
		int x, y_offset, inter_space;

	       	lines = cell_split_text (font, text, width);
	       	line_count = g_list_length (lines);

		switch (valign) {
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

				/* lines should not overlap */
				if (inter_space < font_height)
					inter_space = font_height;
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

			switch (halign) {
			default:
				g_warning ("Multi-line justification style not supported\n");
			case HALIGN_JUSTIFY:
				/* fall through */
			case HALIGN_LEFT:
				x = rect.x + indent;

				/* Be cheap, only calculate the width of the
				 * string if we need to. */
				if (num_lines > 0)
					len = gdk_string_width (font, text);
				break;

			case HALIGN_RIGHT:
				len = gdk_string_width (font, str);
				x = rect.x + rect.width - 1 - len - indent;
				break;

			case HALIGN_CENTER:
			case HALIGN_CENTER_ACROSS_SELECTION:
				len = gdk_string_width (font, str);
				x = rect.x + h_center - len / 2;
			}

			draw_text (drawable, font, gc,
				   x, y1 + y_offset,
				   str, strlen (str), len, line_offset, num_lines);
			y_offset += inter_space;

			g_free (l->data);
		}
		g_list_free (lines);
	}

	style_font_unref (style_font);
}
