/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * print-cell.c: Printing of cell regions and cells.
 *
 * Author:
 *    Jody Goldberg 2000-2006	(jody@gnome.org)
 *    Miguel de Icaza 1999 (miguel@kernel.org)
 *    Andreas J. Guelzow 2007   (aguelzow@pyrshep.ca)
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

#include <string.h>
#include <locale.h>

#if 0
#define MERGE_DEBUG(range, str) do { range_dump (range, str); } while (0)
#else
#define MERGE_DEBUG(range, str)
#endif

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
print_cell_gtk (GnmCell const *cell, GnmStyle const *mstyle,
	    cairo_t *context, PangoContext *pcontext,
	    double x1, double y1, double width, double height, double h_center)
{
	GnmRenderedValue *rv, *cell_rv = cell->rendered_value, *cell_rv100 = NULL;
	GOColor fore_color;
	gint x, y;
	Sheet *sheet = cell->base.sheet;

	/* Get the sizes exclusive of margins and grids */
	/* Note: +1 because size_pixels includes leading gridline.  */
	height -= GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
	width  -= GNM_COL_MARGIN + GNM_COL_MARGIN + 1;

	/* Create a rendered value for printing */
	if (sheet->last_zoom_factor_used != 1 || cell_rv == NULL) {
		/*
		 * We're zoomed and we don't want printing to reflect that.
		 * Simply create a new GnmRenderedValue at zoom 100% for the
		 * _screen_ context.
		 */

		if (!cell_rv) {
			gnm_cell_render_value ((GnmCell *)cell, TRUE);
			cell_rv = cell->rendered_value;
		}

		cell_rv100 = gnm_rendered_value_new ((GnmCell *)cell, mstyle,
			cell_rv->variable_width,
			pango_layout_get_context (cell_rv->layout),
			1.0);
		cell_rv = cell_rv100;
	}

	/*
	 * Since some layout decisions are taken during cell_calc_layout
	 * we need to make sure that has been called.
	 */
	cell_finish_layout ((GnmCell*)cell, cell_rv, width, FALSE);
	if (!cell_rv100) {
		/* We might have made a new cell->rendered_value.  */
		cell_rv = cell->rendered_value;
	}

	/* Now pretend it was made for printing.  */
	rv = gnm_rendered_value_recontext (cell_rv, pcontext);

	/* Make sure we don't get overflow in print unless we had it in
	   display.  */
	rv->might_overflow = rv->numeric_overflow;

	if (cell_rv100)
		gnm_rendered_value_destroy (cell_rv100);

	if (cell_calc_layout (cell, rv, -1,
			      (int)(width * PANGO_SCALE),
			      (int)(height * PANGO_SCALE),
			      (int)h_center == -1 ? -1 : (int)(h_center * PANGO_SCALE),
			      &fore_color, &x, &y)) {
		double x0 = x1 + (1 + GNM_COL_MARGIN);
		double y0 = y1 + (1 + GNM_ROW_MARGIN);
		double px = x1 + x / (double)PANGO_SCALE;
		double py = y1 - y / (double)PANGO_SCALE;

		/* Clip the printed rectangle */
		cairo_save (context);

		if (!rv->rotation) {
			/* We do not clip rotated cells.  */
			cairo_new_path (context);
			cairo_rectangle (context, x0 - 1, y0 -1,
					 width + 1, height + 1);
			cairo_clip (context);
		}

		/* Set the font colour */
		cairo_set_source_rgb (context,
			 UINT_RGBA_R (fore_color) / 255.,
			 UINT_RGBA_G (fore_color) / 255.,
			 UINT_RGBA_B (fore_color) / 255.);

		if (rv->rotation) {
			cairo_save (context);
			cairo_translate (context, x1, y1);
			cairo_rotate (context, rv->rotation * (-M_PI / 180));
			cairo_move_to (context, 0.,0.);
			pango_cairo_show_layout (context, rv->layout);
			cairo_restore (context);
		} else {
			cairo_move_to (context, px, py);
			pango_cairo_show_layout (context, rv->layout);
		}
		cairo_restore(context);
	}

	gnm_rendered_value_destroy (rv);
}

static void
print_rectangle_gtk (cairo_t *context,
		 double x, double y, double w, double h)
{
	cairo_new_path (context);
	cairo_rectangle (context, x, y, w, h);
	cairo_fill (context);
}

static void
print_cell_background_gtk (cairo_t *context,
		       GnmStyle const *style, int col, int row,
		       float x, float y, float w, float h)
{
	if (gnumeric_background_set_gtk (style, context))
		print_rectangle_gtk (context, x, y, w, h);
	gnm_style_border_print_diag_gtk (style, context, x, y, x+w, y+h);
}


/**
 * print_merged_range:
 *
 * Handle the special drawing requirements for a 'merged cell'.
 * First draw the entire range (clipped to the visible region) then redraw any
 * segments that are selected.
 */
static void
print_merged_range_gtk (cairo_t *context, PangoContext *pcontext,
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
	b += sheet_row_get_distance_pts (sheet, view->start.row, last+1);

	if (l == r || t == b)
		return;

	if (style->conditions) {
		GnmEvalPos ep;
		int res;
		eval_pos_init (&ep, (Sheet *)sheet, range->start.col, range->start.row);
		if ((res = gnm_style_conditions_eval (style->conditions, &ep)) >= 0)
			style = g_ptr_array_index (style->cond_styles, res);
	}

	if (gnumeric_background_set_gtk (style, context))
		/* Remember api excludes the far pixels */
		print_rectangle_gtk (context, l, t, r-l+1, b-t+1);

	if (range->start.col < view->start.col)
		l -= dir * sheet_col_get_distance_pts (sheet,
			range->start.col, view->start.col);
	if (view->end.col < range->end.col)
		r += dir * sheet_col_get_distance_pts (sheet,
			view->end.col+1, range->end.col+1);
	if (range->start.row < view->start.row)
		t -= sheet_row_get_distance_pts (sheet,
			range->start.row, view->start.row);
	if (view->end.row < range->end.row)
		b += sheet_row_get_distance_pts (sheet,
			view->end.row+1, range->end.row+1);

	if (cell != NULL) {
		ColRowInfo const * const ri = cell->row_info;

		if (ri->needs_respan)
			row_calc_spans ((ColRowInfo *)ri, cell->pos.row, sheet);

		if (sheet->text_is_rtl)
			print_cell_gtk (cell, style, context, pcontext,
				r, t, l - r, b - t, -1.);
		else
			print_cell_gtk (cell, style, context, pcontext,
				l, t, r - l, b - t, -1.);
	}
	gnm_style_border_print_diag_gtk (style, context, l, t, r, b);
}

static gint
merged_col_cmp (GnmRange const *a, GnmRange const *b)
{
	return a->start.col - b->start.col;
}


void
gnm_gtk_print_cell_range (GtkPrintContext *print_context, cairo_t *context,
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
		hide_grid ? NULL : gnm_style_border_none ();

	int n, col, row;
	double x, y, offset;
	GnmRange     view;
	GSList	 *merged_active, *merged_active_seen,
		 *merged_used, *merged_unused, *ptr, **lag;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);
	g_return_if_fail (range->start.col <= range->end.col);
	g_return_if_fail (range->start.row <= range->end.row);

	pcontext = gtk_print_context_create_pango_context (print_context);

	start_col = range->start.col;
	start_row = range->start.row;
	end_col = range->end.col;
	end_row = range->end.row;

	/* Skip any hidden cols/rows at the start */
	for (; start_col <= end_col ; ++start_col) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, start_col);
		if (ci->visible)
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
	merged_unused = gnm_sheet_merge_get_overlap (sheet,
		range_init (&view, start_col, start_row, end_col, end_row));

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 *	- 6 arrays of n GnmBorder const *
	 *	- 2 arrays of n GnmStyle const *
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

	for (y = base_y;
	     row <= end_row;
	     row = sr.row = next_sr.row, ri = next_ri) {
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
			row_calc_spans ((ColRowInfo *)ri, row, sheet);

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
					print_merged_range_gtk (context, pcontext, sheet,
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
			ColRowInfo const *ri = sheet_row_get_info (sheet, row);

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
			print_cell_background_gtk (context, style, col, row, x, y,
					       ci->size_pts, ri->size_pts);

			/* Is this part of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->spans != NULL)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (NULL == ri->spans || NULL == (span = row_span_get (ri, col)))
				{

				/* no need to draw blanks */
				GnmCell const *cell = sheet_cell_get (sheet, col, row);
				if (!gnm_cell_is_empty (cell))
					print_cell_gtk (cell, style,
							context, pcontext,
							x, y,
							ci->size_pts, ri->size_pts, -1.);

			/* Only draw spaning cells after all the backgrounds
			 * that we are going to draw have been drawn.  No need
			 * to draw the edit cell, or blanks.
			 */
				}
				else if (col == span->right || col == end_col) {
				GnmCell const *cell = span->cell;
				int const start_span_col = span->left;
				int const end_span_col = span->right;
				double real_x = x;
				ColRowInfo const *cell_col =
					sheet_col_get_info (sheet, cell->pos.col);
				double center_offset = cell_col->size_pts / 2;
				double tmp_width = ci->size_pts;

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

				print_cell_gtk (cell, style, context, pcontext,
						real_x, y, tmp_width, ri->size_pts,
						center_offset);
			} else if (col != span->left)
				sr.vertical [col] = NULL;

			if (dir > 0)
			x += ci->size_pts;
		}
		gnm_style_borders_row_print_gtk (prev_vert, &sr,
					 context, base_x, y, y+ri->size_pts,
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

		y += ri->size_pts;
	}
	gnm_style_borders_row_print_gtk (prev_vert, &sr,
				 context, base_x, y, y, sheet, FALSE, dir);

	g_slist_free (merged_used);	   /* merges with bottom in view */
	g_slist_free (merged_active_seen); /* merges with bottom the view */
	g_slist_free (merged_unused);	   /* merges in hidden rows */
	g_object_unref (pcontext);
	g_return_if_fail (merged_active == NULL);
}


