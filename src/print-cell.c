/*
 * cell.c: Printing of cell regions and cells.
 *
 * Author:
 *    Miguel de Icaza 1999 (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include <libgnomeprint/gnome-print.h>
#include "gnumeric.h"
#include "eval.h"
#include "format.h"
#include "color.h"
#include "parse-util.h"
#include "cell.h"
#include "value.h"
#include "border.h"
#include "pattern.h"
#include "cellspan.h"
#include "sheet.h"
#include "print-cell.h"
#include "rendered-value.h"

static void
print_vline (GnomePrintContext *context,
	     double x, double y1, double y2)
{
	gnome_print_moveto (context, x, y1);
	gnome_print_lineto (context, x, y2);
	gnome_print_stroke (context);
}

static void
print_hline (GnomePrintContext *context,
	     double x1, double x2, double y)
{
	gnome_print_moveto (context, x1, y);
	gnome_print_lineto (context, x2, y);
	gnome_print_stroke (context);
}

/*
 * print_show_iso8859_1
 *
 * Like gnome_print_show, but expects an ISO 8859.1 string.
 *
 * NOTE: This function got introduced when gnome-print switched to UTF-8,
 * and will disappear again once Gnumeric makes the switch. Deprecated at
 * birth! 
 */
int
print_show_iso8859_1 (GnomePrintContext *pc, char const *text)
{
	guint32 u4text[128], *u4p;
	guint32 *dynp = NULL;
	size_t len;
	int ret;
	char const *p;
	char *outp;

	g_return_val_if_fail (pc && text, -1);

	if (!*text)
		return 0;

	/* Dynamic allocation for long strings */
	if ((len = strlen (text)) > sizeof u4text / sizeof u4text[0]) {
		dynp = g_new0 (guint32, len);
		u4p  = dynp;
	} else {
		memset (u4text, '\0', sizeof u4text);
		u4p  = u4text;
	}
	outp =  (char *) u4p; 	/* Munging types on purpose */

	/* Convert to big endian UCS-4 */
	for (p = text, outp += 3; *p; p++, outp += 4)
		*outp = *p;

	ret = gnome_print_show_ucs4 (pc, u4p, (gint) len);

	if (dynp)
		g_free (dynp);

	return ret;
}

/***********************************************************/

/*
 * WARNING : This code is an almost exact duplicate of
 *          cell-draw.c
 * Try to keep it that way.
 */

static inline void
print_text (GnomePrintContext *context,
	    double x, double text_base, char const * text, double len_pts,
	    double const * const line_offset, int num_lines)
{
	gnome_print_moveto (context, x, text_base);
	/* FIXME:
	 * Switch this back to gnome_print_show once we use UTF-8 internally */
	print_show_iso8859_1 (context, text);

	/* FIXME how to handle small fonts ?
	 * the text_base should be at least 2 pixels above the bottom */
	while (--num_lines >= 0) {
		double y = text_base - line_offset[num_lines];
		gnome_print_newpath (context);
		gnome_print_setlinewidth (context, 0);
		print_hline (context, x, x+len_pts, y);
	}
}

static void
print_overflow (GnomePrintContext *context, GnomeFont *font,
		double x1, double text_base, double width,
		double const * const line_offset, int num_lines)
{
	double const len = gnome_font_get_width_string_n (font, "#", 1);
	int count = (len != 0) ? (width / len) : 0;

	/* Center */
	for (x1 += (width - count*len) / 2; --count >= 0 ; x1 += len )
		print_text (context, x1, text_base, "#", len,
			   line_offset, num_lines);
}

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

/*
 * base_[xy] : Coordinates of the upper left corner of the cell.
 *             INCLUSIVE of the near grid line
 *
 *      /--- (x1, y1)
 *      v
 *      g------\
 *      |      |
 *      \------/
 */
static void
print_cell (Cell const *cell, MStyle *mstyle, CellSpanInfo const * const spaninfo,
	    GnomePrintContext *context, double x1, double y1)
{
	StyleFont *style_font = mstyle_get_font (mstyle, 1.0);
	GnomeFont *print_font = style_font->font;
	double const font_descent = gnome_font_get_descender (print_font);
	double const font_ascent = gnome_font_get_ascender (print_font);
	double clip_x, clip_width, clip_y, clip_height;

	Sheet const *sheet = cell->base.sheet;
	ColRowInfo const *ci = cell->col_info;
	ColRowInfo const *ri = cell->row_info;
	int start_col, end_col;
	double width, height;
	double text_base;
	double font_height;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	int num_lines = 0;
	double line_offset[3]; /* There are up to 3 lines, double underlined strikethroughs */
	gboolean is_single_line;
	char const *text;
	StyleColor *fore;
	double cell_width_pts;

	/* Don't print zeros if they should be ignored. */
	if (/* No need to check for the edit cell */
	    !sheet->display_zero && cell_is_zero (cell))
		return;

	g_return_if_fail (cell->rendered_value);
	g_return_if_fail (cell->rendered_value->rendered_text);

	if (cell->rendered_value->rendered_text->str == NULL) {
		g_warning ("Serious cell error at '%s'\n", cell_name (cell));
		/* This can occur when eg. a plugin function fires up a dialog */
		text = "Pending";
	} else
		text = cell->rendered_value->rendered_text->str;

	if (spaninfo != NULL) {
		start_col = spaninfo->left;
		end_col = spaninfo->right;
	} else
		start_col = end_col = cell->pos.col;

	/* Get the sizes exclusive of margins and grids */
	width  = ci->size_pts - (ci->margin_b + ci->margin_a + 1.);
	height = ri->size_pts - (ri->margin_b + ri->margin_a + 1.);

	font_height = style_font->size;
	valign = mstyle_get_align_v (mstyle);

	switch (valign) {
	default:
		g_warning ("Unhandled cell vertical alignment\n");

	case VALIGN_JUSTIFY:
	case VALIGN_TOP:
		/*
		 * y1 == top grid line.
		 * add top margin
		 * add font ascent
		 */
		text_base = y1 - font_ascent - ri->margin_a;
		break;
		
	case VALIGN_CENTER:
		text_base = y1 - font_ascent - ri->margin_a -
		    (height - font_height) / 2;
		break;
		
	case VALIGN_BOTTOM:
		/*
		 * y1 + row->size_pts == bottom grid line.
		 * subtract bottom margin
		 * subtract font descent
		 */
		text_base = y1 - ri->size_pts + font_descent + ri->margin_b;
		break;
	}
	
	halign = value_get_default_halign (cell->value, mstyle);

	is_single_line = (halign != HALIGN_JUSTIFY &&
			  valign != VALIGN_JUSTIFY &&
			  !mstyle_get_fit_in_cell (mstyle));

	/* This rectangle has the whole area used by this cell
	 * including the surrounding grid lines */
	clip_x = x1;
	clip_y = y1;
	clip_width  = ci->size_pts + 1;
	clip_height = ri->size_pts + 1;

	/*
	 * x1, y1 are relative to this cell origin, but the cell might be using
	 * columns to the left (if it is set to right justify or center justify)
	 * compute the difference in pts.
	 */
	if (start_col != cell->pos.col) {
		int const offset =
		    sheet_col_get_distance_pts (sheet, start_col, cell->pos.col);
		clip_x     -= offset;
		clip_width += offset;
	}
	if (end_col != cell->pos.col) {
		int const offset =
		    sheet_col_get_distance_pts (sheet, cell->pos.col+1, end_col+1);
		clip_width += offset;
	}

	/* Do not allow text to impinge upon the grid lines or margins
	 * FIXME : Should use margins from start_col and end_col
	 *
	 * NOTE : postscript clip paths exclude the border, gdk includes it.
	 */
	clip_x += ci->margin_a;
	clip_y -= ri->margin_a;
	clip_width -= ci->margin_a + ci->margin_b;
	clip_height -= ri->margin_a + ri->margin_b;
	gnome_print_gsave (context);
	gnome_print_newpath (context);
	gnome_print_moveto (context, clip_x, clip_y);
	gnome_print_lineto (context, clip_x + clip_width, clip_y);
	gnome_print_lineto (context, clip_x + clip_width, clip_y - clip_height);
	gnome_print_lineto (context, clip_x, clip_y - clip_height);
	gnome_print_closepath (context);
	gnome_print_clip (context);

	/* Set the font colour */
	fore = cell->rendered_value->render_color;
	if (fore == NULL)
		fore = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE);
	g_return_if_fail (fore != NULL); /* Be extra careful */
	gnome_print_setrgbcolor (context,
				 fore->red   / (double) 0xffff,
				 fore->green / (double) 0xffff,
				 fore->blue  / (double) 0xffff);

	/* Handle underlining and strikethrough */
	switch (mstyle_get_font_uline (mstyle)) {
	case UNDERLINE_SINGLE : num_lines = 1;
				line_offset[0] = 1.;
				break;

	case UNDERLINE_DOUBLE : num_lines = 2;
				line_offset[0] = 0.;
				line_offset[1] = 2.;

	default :
				break;
	};
	if (mstyle_get_font_strike (mstyle))
		line_offset[num_lines++] = font_ascent/-2;

	cell_width_pts = gnome_font_get_width_string (print_font, text);

	/* if a number overflows, do special drawing */
	if (width < cell_width_pts && cell_is_number (cell)) {
		print_overflow (context, print_font,
				x1 + ci->margin_a + 1,
				text_base, width, line_offset, num_lines);
		style_font_unref (style_font);
		gnome_print_grestore (context);
		return;
	}

	if (is_single_line) {
		double total, len = cell_width_pts;

		switch (halign) {
		case HALIGN_FILL:
			g_warning ("FILL!");
			/* fall through */

		case HALIGN_LEFT:
			x1 += 1 + ci->margin_a;
			break;

		case HALIGN_RIGHT:
			x1 += ci->size_pts - ci->margin_b - cell_width_pts;
			break;

		case HALIGN_CENTER:
			x1 += 1 + ci->margin_a + (width - cell_width_pts) / 2; 
			break;

		case HALIGN_CENTER_ACROSS_SELECTION:
			x1 = clip_x + (clip_width - cell_width_pts) / 2; 
			break;

		default:
			g_warning ("Single-line justitfication style not supported\n");
			x1 += 1 + ci->margin_a;
			break;
		}

		gnome_print_setfont (context, print_font);
		total = 0;
		do {
			print_text (context, x1, text_base, text, len,
				    line_offset, num_lines);

			x1 += len;
			total += len;
		} while (halign == HALIGN_FILL && total < ci->size_pts && len > 0);
	} else {
		GList *lines, *l;
		int line_count;
		double x_offset, y_offset, inter_space;

		lines = cell_split_text (print_font, text, ci->size_pts);
		line_count = g_list_length (lines);

		{
			static int warn_shown;

			if (!warn_shown){
				g_warning ("Set clipping, multi-line");
				warn_shown = 1;
			}
		}

		switch (valign) {
		case VALIGN_TOP:
			y_offset = 0.;
			inter_space = font_height;
			break;

		case VALIGN_CENTER:
			y_offset = ((height -
				       (line_count * font_height)) / 2);
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
			y_offset = (height - (line_count * font_height));
			inter_space = font_height;
			break;

		default:
			g_warning ("Unhandled cell vertical alignment\n");
			y_offset = 0;
			inter_space = font_height;
		}

		gnome_print_setfont (context, print_font);

		y_offset += font_height - 1;
		for (l = lines; l; l = l->next) {
			char const * const str = l->data;
			double len = 0.;

			switch (halign) {
			default:
				g_warning ("Multi-line justification style not supported\n");
				/* fall through */

			case HALIGN_LEFT:
			case HALIGN_JUSTIFY:
				x_offset = ci->margin_a;
				if (num_lines > 0)
					len = gnome_font_get_width_string (print_font, str);
				break;

			case HALIGN_RIGHT:
				len = gnome_font_get_width_string (print_font, str);
				x_offset = ci->size_pts - ci->margin_b - len;
				break;

			case HALIGN_CENTER:
			case HALIGN_CENTER_ACROSS_SELECTION:
				len = gnome_font_get_width_string (print_font, str);
				x_offset = (ci->size_pts - len) / 2;
			}

			/* Advance one pixel for the border */
			x_offset++;
			print_text (context,
				    x1 + x_offset, y1 - y_offset, str,
				    len, line_offset, num_lines);

			y_offset += inter_space;

			g_free (l->data);
		}
		g_list_free (lines);
	}
	style_font_unref (style_font);

	gnome_print_grestore (context);
}

static void
print_rectangle (GnomePrintContext *context,
		 double x, double y, double w, double h)
{
	/* Mirror gdk which excludes the far point */
	w -= 1.;
	h -= 1.;
	gnome_print_moveto (context, x, y);
	gnome_print_lineto (context, x+w, y);
	gnome_print_lineto (context, x+w, y-h);
	gnome_print_lineto (context, x, y-h);
	gnome_print_lineto (context, x, y);
	gnome_print_fill (context);
}

/*
 * TODO TODO TODO
 * Correctly support extended cells. Multi-line, or extending to the left
 * are incorrect currently.
 */
static void
print_border (GnomePrintContext *context, MStyle *mstyle,
	      double x, double y, double w, double h,
	      gboolean const extended_left)
{
	MStyleBorder const * const top =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_TOP);
	MStyleBorder const * const left = extended_left ? NULL :
	    mstyle_get_border (mstyle, MSTYLE_BORDER_LEFT);
	MStyleBorder const * const diag =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_DIAGONAL);
	MStyleBorder const * const rev_diag =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_REV_DIAGONAL);

	if (top)
		style_border_print (top, MSTYLE_BORDER_TOP, context,
				   x, y, x + w, y, left, NULL);
	if (left)
		style_border_print (left, MSTYLE_BORDER_LEFT, context,
				   x, y, x, y - h, top, NULL);

	if (diag)
		style_border_print (diag, MSTYLE_BORDER_DIAGONAL, context,
				   x, y - h, x + w, y, NULL, NULL);
	if (rev_diag)
		style_border_print (rev_diag, MSTYLE_BORDER_REV_DIAGONAL, context,
				   x, y, x + w, y - h, NULL, NULL);
}

static MStyle *
print_cell_background (GnomePrintContext *context, Sheet *sheet,
		       ColRowInfo const * const ci, ColRowInfo const * const ri,
		       /* Pass the row, col because the ColRowInfos may be the default. */
		       int col, int row, double x, double y,
		       gboolean const extended_left)
{
	MStyle *mstyle = sheet_style_compute (sheet, col, row);
	float const w    = ci->size_pts;
	float const h    = ri->size_pts;

	if (gnumeric_background_set_pc (mstyle, context))
		/* Fill the entire cell including the right & left grid line */
		print_rectangle (context, x, y, w+1, h+1);
	else if (extended_left) {
		/* Fill the entire cell including left & excluding right grid line */
		gnome_print_setrgbcolor (context, 1., 1., 1.);
		print_rectangle (context, x, y-1, w, h-1);
	}

	print_border (context, mstyle, x, y, w, h, extended_left);

	return mstyle;

#if 0
	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	if (!mstyle_is_element_set (mstyle, MSTYLE_PATTERN))
		return;

	pattern = mstyle_get_pattern (mstyle);
	if (pattern > 0) {
		StyleColor *back_col =
			mstyle_get_color (mstyle, MSTYLE_COLOR_BACK);
		g_return_if_fail (back_col != NULL);

		gnome_print_setrgbcolor (context,
					 back_col->red   / (double) 0xffff,
					 back_col->green / (double) 0xffff,
					 back_col->blue  / (double) 0xffff);
	}
#endif
}

/*
 * print_cell_range:
 *
 * Prints the cell range.  If output if FALSE, then it does not actually print
 * but it only computes whether there is data to be printed at all.
 *
 * Return value: returns TRUE if at least one cell was printed
 */
gboolean 
print_cell_range (GnomePrintContext *context,
		  Sheet *sheet,
		  int start_col, int start_row,
		  int end_col, int end_row,
		  double base_x, double base_y,
		  gboolean output)
{
	int row, col;
	double x, y;
	gboolean printed = FALSE;
	
	g_return_val_if_fail (context != NULL, FALSE);
	g_return_val_if_fail (GNOME_IS_PRINT_CONTEXT (context), FALSE);
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (start_col <= end_col, FALSE);
	g_return_val_if_fail (start_row <= end_row, FALSE);

	y = base_y;
	for (row = start_row; row <= end_row; row++){
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (!ri->visible)
			continue;

		x = base_x;

		/* Do not increment the column here, spanning cols are different */
		for (col = start_col; col <= end_col; ){
			CellSpanInfo const * span;
			ColRowInfo const * ci = sheet_col_get_info (sheet, col);

			if (!ci->visible) {
				++col;
				continue;
			}

			/*
			 * Is this the start of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->pos != -1)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (ri->pos == -1 || NULL == (span = row_span_get (ri, col))){
				Cell *cell = sheet_cell_get (sheet, col, row);
				MStyle *mstyle = NULL;

				if (output)
					mstyle = print_cell_background (
						context, sheet, ci, ri,
						col, row, x, y, FALSE);

				if (!cell_is_blank (cell)){
					if (output)
						print_cell (cell, mstyle, NULL,
							    context, x, y);
					printed = TRUE;
				}
				if (mstyle)
					mstyle_unref (mstyle);

				/* Increment the column
				 * DO NOT move this outside the if, spanning
				 * columns increment themselves.
				 */
				x += ci->size_pts;
				++col;
			} else {
				Cell const *cell = span->cell;
				int const real_col = cell->pos.col;
				int const start_span_col = span->left;
				int const end_span_col = span->right;
				int real_x = -1;
				MStyle *real_style = NULL;
				gboolean const is_visible =
				    ri->visible && ci->visible;

				/* Paint the backgrounds & borders */
				for (; col <= MIN (end_col, end_span_col) ; ++col) {

					ci = sheet_col_get_info (sheet, col);
					if (ci->visible) {
						MStyle *mstyle = NULL;

						if (output)
							mstyle = print_cell_background (
								context, sheet, ci, ri,
								col, row, x, y,
								col != start_span_col && is_visible);
						
						if (col == real_col) {
							real_style = mstyle;
							real_x = x;
						} else if (mstyle)
							mstyle_unref (mstyle);

						x += ci->size_pts;
					}
				}

				/* The real cell is not visible, we have not painted it.
				 * Compute the style, and offset
				 */
				if (real_style == NULL) {
					real_style = sheet_style_compute (sheet, real_col, ri->pos);
					real_x = x + sheet_col_get_distance_pts (cell->base.sheet,
										 col, cell->pos.col);
				}

				if (is_visible && output)
					print_cell (cell, real_style, span,
						    context, real_x, y);

				printed = TRUE;
				mstyle_unref (real_style);
			}
		}
		y -= ri->size_pts;
	}

	return printed;
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

	/* thinest possible line */
	gnome_print_setlinewidth (context, 0);

	x = base_x;
	print_vline (context, x, base_y, base_y - height);
	for (col = start_col; col <= end_col; col++) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, col);
		if (ci && ci->visible) {
			x += ci->size_pts;
			print_vline (context, x, base_y, base_y - height);
		}
	}

	y = base_y;
	print_hline (context, base_x, base_x + width, y);
	for (row = start_row; row <= end_row; row++){
		ColRowInfo const *ri = sheet_row_get_info (sheet, row);
		if (ri && ri->visible) {
			y -= ri->size_pts;
			print_hline (context, base_x, base_x + width, y);
		}
	}
}
