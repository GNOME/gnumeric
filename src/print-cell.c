/*
 * print-cell.c: Printing of cell regions and cells.
 *
 * Author:
 *    Miguel de Icaza 1999 (miguel@kernel.org)
 *
 * g_unichar_to_utf8: Copyright Red Hat, Inc
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include <libgnomeprint/gnome-print.h>
#include "gnumeric.h"
#include "eval.h"
#include "format.h"
#include "style-color.h"
#include "parse-util.h"
#include "cell.h"
#include "value.h"
#include "style-border.h"
#include "pattern.h"
#include "cellspan.h"
#include "ranges.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "print-cell.h"
#include "rendered-value.h"

#if 0
#define MERGE_DEBUG(range, str) do { range_dump (range, str); } while (0)
#else
#define MERGE_DEBUG(range, str)
#endif

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
 * This is cut & pasted from glib 1.3
 *
 * We need it only for iso-8859-1 converter and it will be
 * abandoned, if glib 2.0 or any other unicode library will
 * be introduced.
 */

static int
g_unichar_to_utf8 (gint c, gchar *outbuf)
{
  size_t len = 0;
  int first;
  int i;

  if (c < 0x80)
    {
      first = 0;
      len = 1;
    }
  else if (c < 0x800)
    {
      first = 0xc0;
      len = 2;
    }
  else if (c < 0x10000)
    {
      first = 0xe0;
      len = 3;
    }
   else if (c < 0x200000)
    {
      first = 0xf0;
      len = 4;
    }
  else if (c < 0x4000000)
    {
      first = 0xf8;
      len = 5;
    }
  else
    {
      first = 0xfc;
      len = 6;
    }

  if (outbuf)
    {
      for (i = len - 1; i > 0; --i)
	{
	  outbuf[i] = (c & 0x3f) | 0x80;
	  c >>= 6;
	}
      outbuf[0] = c | first;
    }

  return len;
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
	gchar *p, *utf, *udyn, ubuf[4096];
	gint len, ret, i;

	g_return_val_if_fail (pc && text, -1);

	if (!*text)
		return 0;

	/* We need only length * 2, because iso-8859-1 is encoded in 1-2 bytes */
	len = strlen (text);
	if (len * 2 > sizeof (ubuf)) {
		udyn = g_new (gchar, len * 2);
		utf = udyn;
	} else {
		udyn = NULL;
		utf = ubuf;
	}
	p = utf;

	for (i = 0; i < len; i++) {
		p += g_unichar_to_utf8 (((guchar *) text)[i], p);
	}

	ret = gnome_print_show_sized (pc, utf, p - utf);

	if (udyn)
		g_free (udyn);

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
	int count = 0;

	if (len != 0)  {
		count = width / len;
		if (count == 0)
			count = 1;
	}

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

	for (line_begin = p = text; *p; p++) {
		double const len_current =
			gnome_font_get_width_string_n (font, p, 1);

		/* Wrap if there is an embeded newline, or we have overflowed */
		if (*p == '\n' || used + len_current > width) {
			char const *begin = line_begin;
			int len;

			if (*p == '\n') {
				/* start after newline, preserve whitespace */
				line_begin = p+1;
				len = p - begin;
				used = 0.;
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
		if (*p == ' ') {
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
 * print_make_rectangle_path
 * @pc      print context
 * @left    left side x coordinate
 * @bottom  bottom side y coordinate
 * @right   right side x coordinate
 * @top     top side y coordinate
 *
 * Make a rectangular path.
 */
void
print_make_rectangle_path (GnomePrintContext *pc,
			   double left, double bottom,
			   double right, double top)
{
	g_return_if_fail (pc != NULL);

	gnome_print_newpath   (pc);
	gnome_print_moveto    (pc, left, bottom);
	gnome_print_lineto    (pc, left, top);
	gnome_print_lineto    (pc, right, top);
	gnome_print_lineto    (pc, right, bottom);
	gnome_print_closepath (pc);
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
print_cell (Cell const *cell, MStyle *mstyle,
	    GnomePrintContext *context, double x1, double y1, double width, double height)
{
	StyleFont *style_font = mstyle_get_font (mstyle, 1.0);
	GnomeFont *print_font = style_font->font;
	double const font_descent = gnome_font_get_descender (print_font);
	double const font_ascent = gnome_font_get_ascender (print_font);
	double rect_x, rect_width, rect_y, rect_height;

	Sheet const * const sheet = cell->base.sheet;
	ColRowInfo const * const ci = cell->col_info; /* DEPRECATED */
	ColRowInfo const * const ri = cell->row_info; /* DEPRECATED */
	double text_base;
	double font_height;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	int num_lines = 0;
	double line_offset [3]; /* There are up to 3 lines, double underlined strikethroughs */
	char const *text;
	StyleColor *fore;
	double cell_width_pts;

	/* Don't print zeros if they should be ignored. */
	if (/* No need to check for the edit cell */
	    !sheet->display_formulas &&
	    !sheet->display_zero &&
	    cell_is_zero (cell))
		return;

	g_return_if_fail (cell->rendered_value);
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
		width  = ci->size_pts - (ci->margin_b + ci->margin_a + 1.);
	if (height < 0) /* DEPRECATED */
		height = ri->size_pts - (ri->margin_b + ri->margin_a + 1.);

	/* This rectangle has the whole area used by this cell
	 * excluding the surrounding grid lines and margins */
	rect_x = x1 + 1 + ci->margin_a;
	rect_y = y1 - 1 - ri->margin_a;
	rect_width = width;
	rect_height = height;

	font_height = style_font->size;
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
		text_base = rect_y - font_ascent;
		break;

	case VALIGN_CENTER:
		text_base = rect_y - font_ascent -
		    (height - font_height) / 2;
		break;

	case VALIGN_BOTTOM:
		/*
		 * rect.y == first pixel past margin
		 * add height == first pixel in lower margin
		 * subtract font descent
		 */
		text_base = rect_y - height + font_descent;
		break;
	}

	/* Do not allow text to impinge upon the grid lines or margins
	 * FIXME : Should use margins from spaninfo->left and spaninfo->right
	 *
	 * NOTE : postscript clip paths exclude the border, gdk includes it.
	 */
	gnome_print_gsave (context);
	print_make_rectangle_path (context,
				   rect_x - 1.,
				   rect_y - rect_height - 1.,
				   rect_x + rect_width + 1.,
				   rect_y + 1.);
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
	if (width < cell_width_pts && cell_is_number (cell) &&
	    sheet && !sheet->display_formulas) {
		print_overflow (context, print_font, rect_x,
				text_base, width, line_offset, num_lines);
		style_font_unref (style_font);
		gnome_print_grestore (context);
		return;
	}

	halign = cell_default_halign (cell, mstyle);
	if (halign != HALIGN_JUSTIFY && valign != VALIGN_JUSTIFY &&
	    !mstyle_get_fit_in_cell (mstyle)) {
		double x, total, len = cell_width_pts;

		switch (halign) {
		case HALIGN_FILL:
			g_warning ("FILL!");
			/* fall through */

		case HALIGN_LEFT:
			x = rect_x;
			break;

		case HALIGN_RIGHT:
			x = rect_x + rect_width - cell_width_pts;
			break;

		case HALIGN_CENTER:
		case HALIGN_CENTER_ACROSS_SELECTION:
			x = rect_x + (rect_width - cell_width_pts) / 2;
			break;

		default:
			g_warning ("Single-line justitfication style not supported\n");
			x = rect_x;
			break;
		}

		gnome_print_setfont (context, print_font);
		total = 0;
		do {
			print_text (context, x, text_base, text, len,
				    line_offset, num_lines);

			x += len;
			total += len;
		} while (halign == HALIGN_FILL && total < ci->size_pts && len > 0);
	} else {
		GList *lines, *l;
		int line_count;
		double x, y_offset, inter_space;

		lines = cell_split_text (print_font, text, ci->size_pts);
		line_count = g_list_length (lines);

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
				x = rect_x;

				/* Be cheap, only calculate the width of the
				 * string if we need to. */
				if (num_lines > 0)
					len = gnome_font_get_width_string (print_font, str);
				break;

			case HALIGN_RIGHT:
				len = gnome_font_get_width_string (print_font, str);
				x = rect_x + rect_width - len;
				break;

			case HALIGN_CENTER:
			case HALIGN_CENTER_ACROSS_SELECTION:
				len = gnome_font_get_width_string (print_font, str);
				x = rect_x + (rect_width - len) / 2;
			}

			print_text (context,
				    x, y1 - y_offset, str,
				    len, line_offset, num_lines);

			y_offset += inter_space;

			g_free (l->data);
		}
		g_list_free (lines);
	}
	style_font_unref (style_font);

	gnome_print_grestore (context);
}

/* We do not use print_make_rectangle_path here - because we do not want a
 * new path.  */
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
	StyleBorder const * const top =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_TOP);
	StyleBorder const * const left = extended_left ? NULL :
	    mstyle_get_border (mstyle, MSTYLE_BORDER_LEFT);
	StyleBorder const * const diag =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_DIAGONAL);
	StyleBorder const * const rev_diag =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_REV_DIAGONAL);

	if (top)
		style_border_print (top, STYLE_BORDER_TOP, context,
				   x, y, x + w, y, left, NULL);
	if (left)
		style_border_print (left, STYLE_BORDER_LEFT, context,
				   x, y, x, y - h, top, NULL);

	if (diag)
		style_border_print (diag, STYLE_BORDER_DIAG, context,
				   x, y - h, x + w, y, NULL, NULL);
	if (rev_diag)
		style_border_print (rev_diag, STYLE_BORDER_REV_DIAG, context,
				   x, y, x + w, y - h, NULL, NULL);
}

static MStyle *
print_cell_background (GnomePrintContext *context, Sheet *sheet,
		       ColRowInfo const * const ci, ColRowInfo const * const ri,
		       /* Pass the row, col because the ColRowInfos may be the default. */
		       int col, int row, double x, double y,
		       gboolean const extended_left)
{
	MStyle *mstyle = sheet_style_get (sheet, col, row);
	float const w    = ci->size_pts;
	float const h    = ri->size_pts;

	if (gnumeric_background_set_pc (mstyle, context))
		/* Fill the entire cell including the right & left grid line */
		print_rectangle (context, x, y, w+1, h+1);
	else if (extended_left) {
		/* Fill the entire cell including left & excluding right grid line */
		gnome_print_setrgbcolor (context, 1., 1., 1.);
		/* FIXME : should not need the hack used in the drawing
		 * code of +-1 to */
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

/**
 * print_merged_range:
 *
 * Handle the special drawing requirements for a 'merged cell'.
 * First draw the entire range (clipped to the visible region) then redraw any
 * segments that are selected.
 */
static void
print_merged_range (GnomePrintContext *context, Sheet *sheet,
		    double start_x, double start_y,
		    Range const *view, Range const *range)
{
	int tmp;
	double l, r, t, b;
	Cell const *cell = sheet_cell_get (sheet, range->start.col, range->start.row);
	MStyle *mstyle = sheet_style_get (sheet, range->start.col, range->start.row);
	gboolean const no_background = !(gnumeric_background_set_pc (mstyle, context));

	/* FIXME : should not need the hack used in the drawing code of +-1 to
	 * remove the grid lines. */

	l = r = start_x;
	if (view->start.col <= range->start.col) {
		l += sheet_col_get_distance_pts (sheet,
			view->start.col, range->start.col);
		if (no_background)
			l++;
	}
	if (range->end.col <= (tmp = view->end.col)) {
		tmp = range->end.col;
		if (no_background)
			r--;
	}
	r += sheet_col_get_distance_pts (sheet, view->start.col, tmp+1);

	t = b = start_y;
	if (view->start.row <= range->start.row) {
		t -= sheet_row_get_distance_pts (sheet,
			view->start.row, range->start.row);
		if (no_background)
			t--;
	}
	if (range->end.row <= (tmp = view->end.row)) {
		tmp = range->end.row;
		if (no_background)
			b++;
	}
	b -= sheet_row_get_distance_pts (sheet, view->start.row, tmp+1);

	/* Remember PS includes the far pixels */
	if (no_background)
		gnome_print_setrgbcolor (context, 1., 1., 1.);
	print_rectangle (context, l, t, r-l, t-b);

	if (range->start.col < view->start.col) {
		l -= sheet_col_get_distance_pts (sheet,
			range->start.col, view->start.col);
	} else if (no_background)
		l--;

	if (view->end.col < range->end.col)
		r += sheet_col_get_distance_pts (sheet,
			view->end.col+1, range->end.col+1);
	else if (no_background)
		r++;
	if (range->start.row < view->start.row)
		t += sheet_row_get_distance_pts (sheet,
			range->start.row, view->start.row);
	else if (no_background)
		t++;
	if (view->end.row < range->end.row)
		b -= sheet_row_get_distance_pts (sheet,
			view->end.row+1, range->end.row+1);
	else if (no_background)
		b--;

	print_border (context, mstyle, l, t, r-l+1, t-b+1, FALSE);

	if (cell != NULL) {
		ColRowInfo const * const ri = cell->row_info;
		ColRowInfo const * const ci = cell->col_info;

		print_cell (cell, mstyle, context,
			   l, t,
			   r - l - ci->margin_b - ci->margin_a,
			   t - b - ri->margin_b - ri->margin_a);
	}
}

static gint
merged_col_cmp (Range const *a, Range const *b)
{
	return a->start.col - b->start.col;
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
	double x, y;
	gboolean printed = FALSE;

	int col, row;
	GSList *merged_active, *merged_active_seen, *merged_unused, *merged_used, *ptr, **lag;
	gboolean first_row;
	Range view;

	g_return_val_if_fail (GNOME_IS_PRINT_CONTEXT (context), FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (start_col <= end_col, FALSE);
	g_return_val_if_fail (start_row <= end_row, FALSE);

	first_row = TRUE;
	merged_active = merged_active_seen = merged_used = NULL;
	merged_unused = sheet_merge_get_overlap (sheet,
		range_init (&view, start_col, start_row, end_col, end_row));

	y = base_y;
	for (row = start_row; row <= end_row; row++) {
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (!ri->visible)
			continue;

		col = start_col;
		x = base_x;

		/* Restore the set of ranges seen, but still active ranges.
		 * Reinverting to maintain the original order */
		g_return_val_if_fail (merged_active == NULL, FALSE);

		while (merged_active_seen != NULL) {
			GSList *tmp = merged_active_seen->next;
			merged_active_seen->next = merged_active;
			merged_active = merged_active_seen;
			merged_active_seen = tmp;
			MERGE_DEBUG (merged_active->data, " : seen -> active\n");
		}

		/* look for merges that start on this row, on the first painted row
		 * also check for merges that start above. */
		lag = &merged_unused;
		for (ptr = merged_unused; ptr != NULL; ) {
			Range * const r = ptr->data;

			if ((r->start.row == row) ||
			    (first_row && r->start.row < row)) {
				GSList *tmp = ptr;
				ptr = *lag = tmp->next;
				g_slist_free_1 (tmp);
				merged_active = g_slist_insert_sorted (merged_active, r,
							(GCompareFunc)merged_col_cmp);
				MERGE_DEBUG (r, " : unused -> active\n");

				view.start.row = row;
				view.start.col = col;
				if (output)
					print_merged_range (context, sheet,
							    x, y, &view, r);
				printed = TRUE;
			} else {
				lag = &(ptr->next);
				ptr = ptr->next;
			}
		}

		first_row = FALSE;

		/* DO NOT increment the column here, spanning cols are different */
		while (col <= end_col) {
			CellSpanInfo const * span;
			ColRowInfo const * ci = sheet_col_get_info (sheet, col);

			if (!ci->visible) {
				++col;
				continue;
			}

			/* Skip any merged regions */
			if (merged_active) {
				Range const *r = merged_active->data;
				if (r->start.col <= col) {
					x += sheet_col_get_distance_pts (
						sheet, col, r->end.col+1);
					col = r->end.col + 1;

					ptr = merged_active;
					merged_active = merged_active->next;
					if (r->end.row == row) {
						ptr->next = merged_used;
						merged_used = ptr;
						MERGE_DEBUG (r, " : active -> used\n");
					} else {
						ptr->next = merged_active_seen;
						merged_active_seen = ptr;
						MERGE_DEBUG (r, " : active -> seen\n");
					}
					continue;
				}
			}

			/*
			 * Is this the start of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->pos != -1)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (ri->pos == -1 || NULL == (span = row_span_get (ri, col))) {
				Cell   *cell   = sheet_cell_get (sheet, col, row);
				MStyle *mstyle = (output)
					? print_cell_background (
						context, sheet, ci, ri,
						col, row, x, y, FALSE)
					: sheet_style_get (sheet, col, row);

				if (!cell_is_blank (cell)) {
					printed = TRUE;
					if (output)
						print_cell (cell, mstyle, context,
							    x, y, -1., -1.);
				} else {
					if (!output)
						printed |= mstyle_visible_in_blank (mstyle);
				}

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
						}

						x += ci->size_pts;
					}
				}

				/* The real cell is not visible, we have not painted it.
				 * Compute the style, and offset
				 */
				if (real_style == NULL) {
					real_style = sheet_style_get (sheet, real_col, ri->pos);
					real_x = x + sheet_col_get_distance_pts (cell->base.sheet,
										 col, cell->pos.col);
				}

				if (is_visible && output) {
					/* FIXME : use correct margins */
					double tmp_width  = ci->size_pts - (ci->margin_b + ci->margin_a + 1);
					double tmp_x = real_x;

					/* x1, y1 are relative to this cell origin, but the cell might
					 * be using columns to the left (if it is set to right justify
					 * or center justify) compute the difference in pts.
					 */
					if (start_span_col != cell->pos.col) {
						int offset = sheet_col_get_distance_pts (sheet,
							start_span_col, cell->pos.col);
						tmp_x     -= offset;
						tmp_width += offset;
					}
					if (end_span_col != cell->pos.col)
						tmp_width += sheet_col_get_distance_pts (sheet,
							cell->pos.col+1, end_span_col+1);

					print_cell (cell, real_style, context,
						    tmp_x, y, tmp_width, -1.);
				}

				printed = TRUE;
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
	for (row = start_row; row <= end_row; row++) {
		ColRowInfo const *ri = sheet_row_get_info (sheet, row);
		if (ri && ri->visible) {
			y -= ri->size_pts;
			print_hline (context, base_x, base_x + width, y);
		}
	}
}
