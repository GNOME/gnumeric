/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * print-cell.c: Printing of cell regions and cells.
 *
 * Author:
 *    Jody Goldberg 2000-2002	(jody@gnome.org)
 *    Miguel de Icaza 1999 (miguel@kernel.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "print-cell.h"

#include "dependent.h"
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
#include "rendered-value.h"
#include "str.h"

#include <string.h>
#include <locale.h>

#if 0
#define MERGE_DEBUG(range, str) do { range_dump (range, str); } while (0)
#else
#define MERGE_DEBUG(range, str)
#endif

static inline void
print_hline (GnomePrintContext *context,
	     float x1, float x2, float y)
{
	gnome_print_moveto (context, x1, y);
	gnome_print_lineto (context, x2, y);
	gnome_print_stroke (context);
}

/***********************************************************/

/*
 * WARNING : This code is an almost exact duplicate of
 *          cell-draw.c
 * Try to keep it that way.
 */

static inline void
print_text (GnomePrintContext *context,
	    double x, double text_base, char const *text, double len_pts,
	    double const * const line_offset, int num_lines)
{
	gnome_print_moveto (context, x, text_base);
	gnome_print_show (context, text);

	/* FIXME how to handle small fonts ?
	 * the text_base should be at least 2 pixels above the bottom */
	while (--num_lines >= 0) {
		double y = text_base - line_offset[num_lines];
		gnome_print_newpath (context);
		gnome_print_setlinewidth (context, 0);
		print_hline (context, x, x+len_pts, y);
	}
}

static char const hashes[] =
"################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################";

static void
print_overflow (GnomePrintContext *context, GnomeFont *font,
		double x1, double text_base, double width,
		double const * const line_offset, int num_lines)
{
	double const len = gnome_font_get_width_utf8_sized (font, "##########", 10) / 10;
	unsigned count = 1;

	if (len != 0)
		count = width / len;
	if (count == 0)
		count = 1;
	else if (count >= sizeof (hashes))
		count = sizeof (hashes) - 1;
	x1 += (width - count*len) / 2; /* Center */
	print_text (context, x1, text_base, hashes + sizeof (hashes) - count - 1,
		    count*len, line_offset, num_lines);
}

static GList *
cell_split_text_no_wrap (char const *text)
{
	GList *lines = NULL;
	gchar const *next = text;

	while (next != NULL) {
		gchar const *this = next;
		gchar *str;

		next = g_utf8_strchr (next, -1, '\n');
		if (next == NULL)
			str = g_strdup (this);
		else {
			str = g_strndup (this, next - this);
			next++;
		}
		lines = g_list_append (lines, str);
	}
	return lines;
}

static GList *
cell_split_text (GnomeFont *font, char const *text, int const width)
{
	char const *p, *next, *line_begin;
	char const *first_whitespace = NULL;
	char const *last_whitespace = NULL;
	gboolean prev_was_space = FALSE;
	GList *list = NULL;
	double used = 0., used_last_space = 0.;
	double len_current;

	for (line_begin = p = text; *p; p = next) {
		next = g_utf8_next_char (p);
		len_current = gnome_font_get_width_utf8_sized (font, p, next - p);

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
		if (*p == '-') {
			used_last_space = used;
			last_whitespace = p;
			first_whitespace = p+1;
			prev_was_space = TRUE;
		} else if (g_unichar_isspace (g_utf8_get_char (p))) {
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
print_cell (GnmCell const *cell, GnmStyle const *mstyle, GnomePrintContext *context,
	    double x1, double y1, double width, double height, double h_center)
{
	Sheet const * const sheet = cell->base.sheet;
	GnmFont *style_font = mstyle_get_font (mstyle, sheet->context, 1.0);
	GnomeFont *print_font = style_font->gnome_print_font;
	double const font_descent = gnome_font_get_descender (print_font);
	double const font_ascent = gnome_font_get_ascender (print_font);
	double rect_x, rect_width, rect_y, rect_height;

	ColRowInfo const * const ci = cell->col_info; /* DEPRECATED */
	ColRowInfo const * const ri = cell->row_info; /* DEPRECATED */
	double text_base;
	double font_height;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	int num_lines = 0;
	double line_offset [3]; /* There are up to 3 lines, double underlined strikethroughs */
	char const *text;
	const PangoColor *fore;
	double cell_width_pts, indent = 0.;

	gboolean is_multi_line_text;

	/* Don't print zeros if they should be ignored. */
	if (sheet && sheet->hide_zero && cell_is_zero (cell) &&
	    (!sheet->display_formulas || !cell_has_expr (cell)))
		return;

	if (cell->rendered_value == NULL) {
		g_warning ("Serious cell error at '%s'", cell_name (cell));
		/* This can occur when eg. a plugin function fires up a dialog */
		return;
	}

	text = rendered_value_get_text (cell->rendered_value);
	fore = cell_get_render_color (cell);
	g_return_if_fail (fore != NULL); /* Be extra careful */

	/* Get the sizes exclusive of margins and grids */
	/* FIXME : all callers will eventually pass in their cell size */
	if (width < 0) /* DEPRECATED */
		width  = ci->size_pts - (ci->margin_b + ci->margin_a + 1.);
	if (height < 0) /* DEPRECATED */
		height = ri->size_pts - (ri->margin_b + ri->margin_a + 1.);

	/* This rectangle has the whole area used by this cell
	 * excluding the surrounding grid lines and margins */
	if (width <= 0 || height <= 0)
		return;

	/* This rectangle has the whole area used by this cell
	 * excluding the surrounding grid lines and margins */
	rect_x = x1 + 1 + ci->margin_a;
	rect_y = y1 - 1 - ri->margin_a;
	rect_width = width + 1;
	rect_height = height + 1;

	font_height = style_font->size_pts;
	valign = mstyle_get_align_v (mstyle);

	switch (valign) {
	default:
		g_warning ("Unhandled cell vertical alignment.");

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
		 * subtract font descent (sign of descent seems reversed for
		 *	 gnome-print-2.0)
		 */
		text_base = rect_y - height - font_descent;
		break;
	}

	/* Do not allow text to impinge upon the grid lines or margins
	 * FIXME : Should use margins from spaninfo->left and spaninfo->right
	 *
	 * NOTE : postscript clip paths exclude near the border, gdk includes it.
	 */
	gnome_print_gsave (context);
	print_make_rectangle_path (context,
				   rect_x - 1.,
				   rect_y - rect_height - 1.,
				   rect_x + rect_width + 1.,
				   rect_y + 1.);
	gnome_print_clip (context);

	/* Set the font colour */
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

	/* FIXME : This will be wrong for JUSTIFIED halignments */
	halign = style_default_halign (mstyle, cell);
	cell_width_pts = gnome_font_get_width_utf8 (print_font, text);
	if (halign == HALIGN_LEFT || halign == HALIGN_RIGHT) {
		/* 2*width seems to be pretty close to XL's notion */
		/* FIXME: Why use digit?  */
		indent = mstyle_get_indent (mstyle) *
			2. * style_font->approx_width.pts.digit;
	}

	/* if a number overflows, do special drawing */
	if ((cell_width_pts + indent) > width && cell_is_number (cell) &&
	    sheet && !sheet->display_formulas) {
		print_overflow (context, print_font, rect_x,
				text_base, width, line_offset, num_lines);
		style_font_unref (style_font);
		gnome_print_grestore (context);
		return;
	}

	if (halign == HALIGN_CENTER_ACROSS_SELECTION || h_center <= 0.)
		h_center = width / 2.;

	is_multi_line_text = mstyle_get_wrap_text (mstyle) 
		|| (NULL != g_utf8_strchr (text, -1, '\n')); 
	
	if (halign != HALIGN_JUSTIFY && valign != VALIGN_JUSTIFY &&
	    !is_multi_line_text) {
		double x, total, len = cell_width_pts;

		switch (halign) {
		case HALIGN_FILL: /* fall through */
		case HALIGN_LEFT:
			x = rect_x + indent;
			break;

		case HALIGN_RIGHT:
			x = rect_x + rect_width - 1 - cell_width_pts -indent;
			break;

		case HALIGN_CENTER:
		case HALIGN_CENTER_ACROSS_SELECTION:
			x = rect_x + h_center - cell_width_pts / 2;
			break;

		default:
			g_warning ("Single-line justification style not supported.");
			x = rect_x;
			break;
		}

		gnome_print_setfont (context, print_font);
		total = len; /* don't include partial copies after the first */
		do {
			print_text (context, x, text_base, text, len,
				    line_offset, num_lines);

			x += len;
			total += len;
		} while (halign == HALIGN_FILL && total < rect_width && len > 0);
	} else {
		GList *lines, *l;
		int line_count;
		double x, y_offset, inter_space;

		lines = mstyle_get_wrap_text (mstyle) ? 
			cell_split_text (print_font, text, width) :
			cell_split_text_no_wrap (text);
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
			g_warning ("Unhandled cell vertical alignment.");
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
				g_warning ("Multi-line justification style not supported.");
			case HALIGN_JUSTIFY:
				/* fall through */
			case HALIGN_LEFT:
				x = rect_x + indent;

				/* Be cheap, only calculate the width of the
				 * string if we need to. */
				if (num_lines > 0)
					len = gnome_font_get_width_utf8 (print_font, str);
				break;

			case HALIGN_RIGHT:
				len = gnome_font_get_width_utf8 (print_font, str);
				x = rect_x + rect_width - 1 - len - indent;
				break;

			case HALIGN_CENTER:
			case HALIGN_CENTER_ACROSS_SELECTION:
				len = gnome_font_get_width_utf8 (print_font, str);
				x = rect_x + h_center - len / 2;
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

static void
print_cell_background (GnomePrintContext *context,
		       GnmStyle const *style, int col, int row,
		       float x, float y, float w, float h)
{
	if (gnumeric_background_set_pc (style, context))
		/* Fill the entire cell (API excludes far pixel) */
		print_rectangle (context, x, y, w+1, h+1);
	style_border_print_diag (style, context, x, y, x+w, y-h);
}

/**
 * print_merged_range:
 *
 * Handle the special drawing requirements for a 'merged cell'.
 * First draw the entire range (clipped to the visible region) then redraw any
 * segments that are selected.
 */
static void
print_merged_range (GnomePrintContext *context, Sheet const *sheet,
		    double start_x, double start_y,
		    GnmRange const *view, GnmRange const *range)
{
	float l, r, t, b;
	GnmCell  const *cell    = sheet_cell_get (sheet, range->start.col, range->start.row);
	GnmStyle const *mstyle = sheet_style_get (sheet, range->start.col, range->start.row);

	l = sheet_col_get_distance_pts (sheet,
		view->start.col, range->start.col) + start_x;
	r = sheet_col_get_distance_pts (sheet,
		view->start.col, range->end.col+1) + start_x;

	t = sheet_row_get_distance_pts (sheet,
		view->start.row, range->start.row) + start_y;
	b = sheet_row_get_distance_pts (sheet,
		view->start.row, range->end.row+1) + start_y;

	if (gnumeric_background_set_pc (mstyle, context))
		/* Remember api excludes the far pixels */
		print_rectangle (context, l, t, r-l+1, b-t+1);

	if (cell != NULL) {
		ColRowInfo const * const ri = cell->row_info;
		ColRowInfo const * const ci = cell->col_info;

		/* FIXME : get the margins from the far col/row too */
		print_cell (cell, mstyle, context,
			    l, t,
			    r - l - ci->margin_b - ci->margin_a,
			    b - t - ri->margin_b - ri->margin_a, -1.);
	}
}

static gint
merged_col_cmp (GnmRange const *a, GnmRange const *b)
{
	return a->start.col - b->start.col;
}

void
print_cell_range (GnomePrintContext *context,
		  Sheet const *sheet, GnmRange *range,
		  double base_x, double base_y,
		  gboolean hide_grid)
{
	int n, col, row;
	double x, y;
	ColRowInfo const *ri = NULL, *next_ri = NULL;
	int start_row, start_col, end_col, end_row;

	GnmRow sr, next_sr;
	GnmStyle const **styles;
	GnmBorder const **borders, **prev_vert;
	GnmBorder const *none =
		hide_grid ? NULL : style_border_none ();

	GnmRange     view;
	GSList	 *merged_active, *merged_active_seen,
		 *merged_used, *merged_unused, *ptr, **lag;

	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (context));
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);
	g_return_if_fail (range->start.col <= range->end.col);
	g_return_if_fail (range->start.row <= range->end.row);

	start_col = range->start.col;
	start_row = range->start.row;
	end_col = range->end.col;
	end_row = range->end.row;

	/* Skip any hidden cols/rows at the start */
	for (; start_col <= end_col ; ++start_col) {
		ri = sheet_col_get_info (sheet, start_col);
		if (ri->visible)
			break;
	}
	for (; start_row <= end_row ; ++start_row) {
		ri = sheet_row_get_info (sheet, start_row);
		if (ri->visible)
			break;
	}

	sheet_style_update_grid_color (sheet);

	/* Get ordered list of merged regions */
	merged_active = merged_active_seen = merged_used = NULL;
	merged_unused = sheet_merge_get_overlap (sheet,
		range_init (&view, start_col, start_row, end_col, end_row));

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 * 	- 6 arrays of n GnmBorder const *
	 * 	- 2 arrays of n GnmStyle const *
	 *
	 * then alias the arrays for easy access so that array [col] is valid
	 * for all elements start_col-1 .. end_col+1 inclusive.
	 * Note that this means that in some cases array [-1] is legal.
	 */
	n = end_col - start_col + 3; /* 1 before, 1 after, 1 fencepost */
	style_row_init (&prev_vert, &sr, &next_sr, start_col, end_col,
			g_alloca (n * 8 * sizeof (gpointer)), hide_grid);

	/* load up the styles for the first row */
	next_sr.row = sr.row = row = start_row;
	sheet_style_get_row (sheet, &sr);

	for (y = base_y; row <= end_row; row = sr.row = next_sr.row, ri = next_ri) {
		/* Restore the set of ranges seen, but still active.
		 * Reinverting list to maintain the original order */
		g_return_if_fail (merged_active == NULL);

		while (merged_active_seen != NULL) {
			GSList *tmp = merged_active_seen->next;
			merged_active_seen->next = merged_active;
			merged_active = merged_active_seen;
			merged_active_seen = tmp;
			MERGE_DEBUG (merged_active->data, " : seen -> active\n");
		}

		/* find the next visible row */
		while (1) {
			++next_sr.row;
			if (next_sr.row <= end_row) {
				next_ri = sheet_row_get_info (sheet, next_sr.row);
				if (next_ri->visible) {
					sheet_style_get_row (sheet, &next_sr);
					break;
				}
			} else {
				for (col = start_col ; col <= end_col; ++col)
					next_sr.vertical [col] =
					next_sr.bottom [col] = none;
				break;
			}
		}

		/* it is safe to const_cast because only the a non-default row
		 * will ever get flagged.
		 */
		if (ri->needs_respan)
			row_calc_spans ((ColRowInfo *)ri, sheet);

		/* look for merges that start on this row, on the first painted row
		 * also check for merges that start above. */
		view.start.row = row;
		lag = &merged_unused;
		for (ptr = merged_unused; ptr != NULL; ) {
			GnmRange * const r = ptr->data;

			if (r->start.row <= row) {
				GSList *tmp = ptr;
				ptr = *lag = tmp->next;
				if (r->end.row < row) {
					tmp->next = merged_used;
					merged_used = tmp;
					MERGE_DEBUG (r, " : unused -> used\n");
				} else {
					ColRowInfo const *ci =
						sheet_col_get_info (sheet, r->start.col);
				g_slist_free_1 (tmp);
				merged_active = g_slist_insert_sorted (merged_active, r,
							(GCompareFunc)merged_col_cmp);
				MERGE_DEBUG (r, " : unused -> active\n");

					if (ci->visible)
				print_merged_range (context, sheet,
						    base_x, y, &view, r);
				}
			} else {
				lag = &(ptr->next);
				ptr = ptr->next;
			}
		}

		for (col = start_col, x = base_x; col <= end_col ; col++) {
			GnmStyle const *style;
			CellSpanInfo const *span;
			ColRowInfo const *ci = sheet_col_get_info (sheet, col);

			if (!ci->visible) {
				if (merged_active != NULL) {
					GnmRange const *r = merged_active->data;
					if (r->end.col == col) {
						ptr = merged_active;
						merged_active = merged_active->next;
						if (r->end.row <= row) {
							ptr->next = merged_used;
							merged_used = ptr;
							MERGE_DEBUG (r, " : active2 -> used\n");
						} else {
							ptr->next = merged_active_seen;
							merged_active_seen = ptr;
							MERGE_DEBUG (r, " : active2 -> seen\n");
						}
					}
				}
				continue;
			}

			/* Skip any merged regions */
			if (merged_active) {
				GnmRange const *r = merged_active->data;
				if (r->start.col <= col) {
					gboolean clear_top, clear_bottom = TRUE;
					int i, first = r->start.col;
					int last  = r->end.col;

					x += sheet_col_get_distance_pts (sheet,
						col, last+1);
					col = last;

					if (first < start_col) {
						first = start_col;
						sr.vertical [first] = NULL;
					}
					if (last > end_col) {
						last = end_col;
						sr.vertical [last+1] = NULL;
					}
					clear_top = (r->start.row != row);

					ptr = merged_active;
					merged_active = merged_active->next;
					if (r->end.row <= row) {
						clear_bottom = FALSE;
						ptr->next = merged_used;
						merged_used = ptr;
						MERGE_DEBUG (r, " : active -> used\n");
					} else {
						ptr->next = merged_active_seen;
						merged_active_seen = ptr;
						MERGE_DEBUG (r, " : active -> seen\n");
					}

					/* Clear the borders */
					for (i = first ; i <= last ; i++) {
						if (clear_top)
							sr.top [i] = NULL;
						if (clear_bottom)
							sr.bottom [i] = NULL;
						if (i > first)
							sr.vertical [i] = NULL;
					}
					continue;
				}
			}

			style = sr.styles [col];
			print_cell_background (context, style, col, row, x, y,
					       ci->size_pts, ri->size_pts);

			/* Is this part of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->pos != -1)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (ri->pos == -1 || NULL == (span = row_span_get (ri, col))) {

				/* no need to draw blanks */
				GnmCell const *cell = sheet_cell_get (sheet, col, row);
				if (!cell_is_empty (cell))
					print_cell (cell, style, context,
						    x, y, -1., -1., -1.);

			/* Only draw spaning cells after all the backgrounds
			 * that we are goign to draw have been drawn.  No need
			 * to draw the edit cell, or blanks.
			 */
			} else if (col == span->right || col == end_col) {
				GnmCell const *cell = span->cell;
				int const start_span_col = span->left;
				int const end_span_col = span->right;
				double real_x = x;
				double h_center = cell->col_info->size_pts / 2;
				/* TODO : Use the spanning margins */
				double tmp_width = ci->size_pts -
					ci->margin_b - ci->margin_a;

				if (col != cell->pos.col)
					style = sheet_style_get (sheet,
						cell->pos.col, row);

				/* x, y are relative to this cell origin, but the cell
				 * might be using columns to the left (if it is set to right
				 * justify or center justify) compute the pixel difference
				 */
				if (start_span_col != cell->pos.col)
					h_center += sheet_col_get_distance_pts (
						sheet, start_span_col, cell->pos.col);

				if (start_span_col != col) {
					double offset = sheet_col_get_distance_pts (
						sheet, start_span_col, col);
					real_x -= offset;
					tmp_width += offset;
					sr.vertical [col] = NULL;
				}
				if (end_span_col != col)
					tmp_width += sheet_col_get_distance_pts (
						sheet, col+1, end_span_col + 1);

				print_cell (cell, style, context,
					    real_x, y, tmp_width, -1, h_center);
			} else if (col != span->left)
				sr.vertical [col] = NULL;

			x += ci->size_pts;
		}
		style_borders_row_print (prev_vert, &sr,
					 context, base_x, y, y-ri->size_pts,
					 sheet, TRUE);

		/* In case there were hidden merges that trailed off the end */
		while (merged_active != NULL) {
			GnmRange const *r = merged_active->data;
			ptr = merged_active;
			merged_active = merged_active->next;
			if (r->end.row <= row) {
				ptr->next = merged_used;
				merged_used = ptr;
				MERGE_DEBUG (r, " : active3 -> used\n");
			} else {
				ptr->next = merged_active_seen;
				merged_active_seen = ptr;
				MERGE_DEBUG (r, " : active3 -> seen\n");
			}
		}
		/* roll the pointers */
		borders = prev_vert; prev_vert = sr.vertical;
		sr.vertical = next_sr.vertical; next_sr.vertical = borders;
		borders = sr.top; sr.top = sr.bottom;
		sr.bottom = next_sr.top = next_sr.bottom; next_sr.bottom = borders;
		styles = sr.styles; sr.styles = next_sr.styles; next_sr.styles = styles;

		y -= ri->size_pts;
	}
	style_borders_row_print (prev_vert, &sr,
				 context, base_x, y, y, sheet, FALSE);

	g_slist_free (merged_used);	   /* merges with bottom in view */
	g_slist_free (merged_active_seen); /* merges with bottom the view */
	g_slist_free (merged_unused);	   /* merges in hidden rows */
	g_return_if_fail (merged_active == NULL);
}
