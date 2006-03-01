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
#include "gnm-format.h"
#include "style-color.h"
#include "style-font.h"
#include "parse-util.h"
#include "cell.h"
#include "value.h"
#include "style-border.h"
#include "style-conditions.h"
#include "gnm-style-impl.h"
#include "pattern.h"
#include "cellspan.h"
#include "ranges.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "rendered-value.h"
#include "str.h"
#include "cell-draw.h"
#include <libgnomeprint/gnome-print-pango.h>

#include <string.h>
#include <locale.h>

#if 0
#define MERGE_DEBUG(range, str) do { range_dump (range, str); } while (0)
#else
#define MERGE_DEBUG(range, str)
#endif

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
print_cell (GnmCell const *cell, GnmStyle const *mstyle,
	    GnomePrintContext *context, PangoContext *pcontext,
	    double x1, double y1, double width, double height, double h_center)
{
	RenderedValue *rv, *cell_rv = cell->rendered_value, *cell_rv100 = NULL;
	GOColor fore_color; 
	gint x, y;
	ColRowInfo const * const ci = cell->col_info;
	ColRowInfo const * const ri = cell->row_info;
	Sheet *sheet;

	/* Get the sizes exclusive of margins and grids */
	/* FIXME : all callers will eventually pass in their cell size */
	/* Note: +1 because size_pixels includes right gridline.  */
	if (width < 0) /* DEPRECATED */
		width  = ci->size_pts - (ci->margin_b + ci->margin_a + 1);
	if (height < 0) /* DEPRECATED */
		height = ri->size_pts - (ri->margin_b + ri->margin_a + 1);

	/* Create a rendered value for printing */
	sheet = cell->base.sheet;
	if (sheet->last_zoom_factor_used != 1) {
		/*
		 * We're zoomed and we don't want printing to reflect that.
		 * Simply create a new RenderedValue at zoom 100% for the
		 * _screen_ context.
		 */		   
		cell_rv100 =
			rendered_value_new ((GnmCell *)cell, mstyle,
					    cell_rv->variable_width,
					    pango_layout_get_context (cell_rv->layout),
					    1.0);
		/*
		 * Ah, but not so fast!
		 *
		 * If the old layout was modified by cell_calc_layout in a
		 * way that might affect the shape of the layout, we have
		 * to try that again before we recontext.
		 */
		if (pango_layout_get_width (cell_rv->layout) != -1) {
			gint dummy_x, dummy_y;
			cell_calc_layout (cell, cell_rv100, -1,
					  (int)(width * PANGO_SCALE),
					  (int)(height * PANGO_SCALE),
					  (int)h_center == -1 ? -1 : (int)(h_center * PANGO_SCALE),
					  &fore_color, &dummy_x, &dummy_y);
		}

		cell_rv = cell_rv100;
	}

	/* Now pretend it was made for printing.  */
	rv = rendered_value_recontext (cell_rv, pcontext);

	if (cell_rv100)
		rendered_value_destroy (cell_rv100);

	if (cell_calc_layout (cell, rv, -1,
			      (int)(width * PANGO_SCALE),
			      (int)(height * PANGO_SCALE),
			      (int)h_center == -1 ? -1 : (int)(h_center * PANGO_SCALE),
			      &fore_color, &x, &y)) {
		double x0 = x1 + 1 + ci->margin_a;
		double y0 = y1 - (1 + ri->margin_a);
		double px = x1 + x / (double)PANGO_SCALE;
		double py = y1 + y / (double)PANGO_SCALE;

		/* Clip the printed rectangle */
		gnome_print_gsave (context);

		if (!rv->rotation) {
			/* We do not clip rotated cells.  */
			print_make_rectangle_path (context,
						   x0 - 1, y0 - height,
						   x0 + width, y0 + 1);
			gnome_print_clip (context);
		}

		/* Set the font colour */
		gnome_print_setrgbcolor (context,
			UINT_RGBA_R (fore_color) / 255.,
			UINT_RGBA_G (fore_color) / 255.,
			UINT_RGBA_B (fore_color) / 255.);

		if (rv->rotation) {
			RenderedRotatedValue *rrv = (RenderedRotatedValue *)rv;
			const struct RenderedRotatedValueInfo *li = rrv->lines;
			GSList *lines;

			for (lines = pango_layout_get_lines (rv->layout);
			     lines;
			     lines = lines->next, li++) {
				gnome_print_gsave (context);
				gnome_print_moveto (context,
						    px + li->dx / (double)PANGO_SCALE,
						    py - li->dy / (double)PANGO_SCALE);
				gnome_print_rotate (context, rv->rotation);
				gnome_print_pango_layout_line (context, lines->data);
				gnome_print_grestore (context);
			}
		} else {
			gnome_print_moveto (context, px, py);
			gnome_print_pango_layout (context, rv->layout);
		}

		gnome_print_grestore (context);
	}

	rendered_value_destroy (rv);
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
print_merged_range (GnomePrintContext *context, PangoContext *pcontext,
		    Sheet const *sheet,
		    double start_x, double start_y,
		    GnmRange const *view, GnmRange const *range)
{
	float l, r, t, b;
	int last;
	GnmCell  const *cell  = sheet_cell_get (sheet, range->start.col, range->start.row);
	int const dir = sheet->text_is_rtl ? -1 : 1;

	/* load style from corner which may not be visible */
	GnmStyle const *style = sheet_style_get (sheet, range->start.col, range->start.row);

	l = r = start_x;
	if (view->start.col < range->start.col)
		l += dir * sheet_col_get_distance_pts (sheet,
			view->start.col, range->start.col);
	if (range->end.col <= (last = view->end.col))
		last = range->end.col;
	r += dir * sheet_col_get_distance_pts (sheet, view->start.col, last+1);

	t = b = start_y;
	if (view->start.row < range->start.row)
		t -= sheet_row_get_distance_pts (sheet,
			view->start.row, range->start.row);
	if (range->end.row <= (last = view->end.row))
		last = range->end.row;
	b -= sheet_row_get_distance_pts (sheet, view->start.row, last+1);

	if (l == r || t == b)
		return;

	if (style->conditions) {
		GnmEvalPos ep;
		int res;
		eval_pos_init (&ep, (Sheet *)sheet, range->start.col, range->start.row);
		if ((res = gnm_style_conditions_eval (style->conditions, &ep)) >= 0)
			style = g_ptr_array_index (style->cond_styles, res);
	}

	if (gnumeric_background_set_pc (style, context))
		/* Remember api excludes the far pixels */
		print_rectangle (context, l, t, r-l+1, t-b+1);

	if (range->start.col < view->start.col)
		l -= dir * sheet_col_get_distance_pts (sheet,
			range->start.col, view->start.col);
	if (view->end.col < range->end.col)
		r += dir * sheet_col_get_distance_pts (sheet,
			view->end.col+1, range->end.col+1);
	if (range->start.row < view->start.row)
		t += sheet_row_get_distance_pts (sheet,
			range->start.row, view->start.row);
	if (view->end.row < range->end.row)
		b -= sheet_row_get_distance_pts (sheet,
			view->end.row+1, range->end.row+1);

	if (cell != NULL) {
		ColRowInfo const * const ri = cell->row_info;
		ColRowInfo const * const ci = cell->col_info;

		if (ri->needs_respan)
			row_calc_spans ((ColRowInfo *)ri, sheet);

		/* FIXME : get the margins from the far col/row too */
		print_cell (cell, style, context, pcontext,
			    l, t,
			    r - l - ci->margin_b - ci->margin_a,
			    t - b - ri->margin_b - ri->margin_a, -1.);
	}
	style_border_print_diag (style, context, l, t, r, b);
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
	ColRowInfo const *ri = NULL, *next_ri = NULL;
	int const dir = sheet->text_is_rtl ? -1 : 1;
	int start_row, start_col, end_col, end_row;
	PangoContext *pcontext;

	GnmStyleRow sr, next_sr;
	GnmStyle const **styles;
	GnmBorder const **borders, **prev_vert;
	GnmBorder const *none =
		hide_grid ? NULL : style_border_none ();

	int n, col, row;
	double x, y, offset;
	GnmRange     view;
	GSList	 *merged_active, *merged_active_seen,
		 *merged_used, *merged_unused, *ptr, **lag;

	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (context));
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);
	g_return_if_fail (range->start.col <= range->end.col);
	g_return_if_fail (range->start.row <= range->end.row);

	pcontext = gnome_print_pango_create_context
		(gnome_print_pango_get_default_font_map ());

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
					print_merged_range (context, pcontext, sheet,
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

			if (dir < 0)
				x -= ci->size_pts;
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
					print_cell (cell, style, context, pcontext,
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
				double center_offset = cell->col_info->size_pts / 2;
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
					center_offset += sheet_col_get_distance_pts (
						sheet, start_span_col, cell->pos.col);

				if (start_span_col != col) {
					offset = sheet_col_get_distance_pts (
						sheet, start_span_col, col);
					tmp_width += offset;
					if (dir > 0)
						real_x -= offset;
					sr.vertical [col] = NULL;
				}
				if (end_span_col != col) {
					offset = sheet_col_get_distance_pts (
						sheet, col+1, end_span_col + 1);
					tmp_width += offset;
					if (dir < 0)
						real_x -= offset;
				}

				print_cell (cell, style, context, pcontext,
					    real_x, y, tmp_width, -1, center_offset);
			} else if (col != span->left)
				sr.vertical [col] = NULL;

			if (dir > 0)
			x += ci->size_pts;
		}
		style_borders_row_print (prev_vert, &sr,
					 context, base_x, y, y-ri->size_pts,
					 sheet, TRUE, dir);

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
				 context, base_x, y, y, sheet, FALSE, dir);

	g_slist_free (merged_used);	   /* merges with bottom in view */
	g_slist_free (merged_active_seen); /* merges with bottom the view */
	g_slist_free (merged_unused);	   /* merges in hidden rows */
	g_object_unref (pcontext);
	g_return_if_fail (merged_active == NULL);
}
