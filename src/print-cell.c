/*
 * print-cell.c: Printing of cell regions and cells.
 *
 * Author:
 *    Jody Goldberg 2000-2006	(jody@gnome.org)
 *    Miguel de Icaza 1999 (miguel@kernel.org)
 *    Andreas J. Guelzow 2007   (aguelzow@pyrshep.ca)
 *  Copyright (C) 2007-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <print-cell.h>

#include <application.h>
#include <dependent.h>
#include <gnm-format.h>
#include <style-color.h>
#include <style-font.h>
#include <parse-util.h>
#include <cell.h>
#include <value.h>
#include <style-border.h>
#include <style-conditions.h>
#include <pattern.h>
#include <cellspan.h>
#include <ranges.h>
#include <sheet.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <rendered-value.h>
#include <cell-draw.h>
#include <print-info.h>

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
print_cell_gtk (GnmCell const *cell,
		cairo_t *context,
		double x1, double y1,
		double width, double height, double h_center,
		GnmPrintInformation const *pinfo)
{
	GnmRenderedValue *rv, *rv100 = NULL;
	GOColor fore_color;
	gint x, y;
	Sheet *sheet = cell->base.sheet;
	double const scale_h = 72. / gnm_app_display_dpi_get (TRUE);
	double const scale_v = 72. / gnm_app_display_dpi_get (FALSE);

	gboolean cell_shows_error;

	if (cell->base.flags & GNM_CELL_HAS_NEW_EXPR)
		gnm_cell_eval ((GnmCell *)cell);

	cell_shows_error = (gnm_cell_is_error (cell) != NULL)
		&& !(gnm_cell_has_expr (cell) && sheet->display_formulas);

	if (cell_shows_error && pinfo->error_display ==
	    GNM_PRINT_ERRORS_AS_BLANK)
		return;

	/* Get the sizes exclusive of margins and grids */
	/* Note: +1 because size_pixels includes leading gridline.  */
	height -= GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
	width  -= GNM_COL_MARGIN + GNM_COL_MARGIN + 1;

	rv = gnm_cell_fetch_rendered_value (cell, TRUE);

	/* Create a rendered value for printing */
	if (cell_shows_error &&
	    (pinfo->error_display == GNM_PRINT_ERRORS_AS_NA
	     || pinfo->error_display == GNM_PRINT_ERRORS_AS_DASHES)) {
		GnmCell *t_cell = (GnmCell *)cell;
		GnmValue *old = t_cell->value;
		if (pinfo->error_display == GNM_PRINT_ERRORS_AS_NA)
			t_cell->value = value_new_error_NA (NULL);
		else
			t_cell->value = value_new_error
				(NULL,
				 /* U+2014 U+200A U+2014 */
				 "\342\200\224\342\200\212\342\200\224");
		rv100 = gnm_rendered_value_new (t_cell,
						pango_layout_get_context (rv->layout),
						rv->variable_width,
						1.0);
		rv = rv100;
		value_release (t_cell->value);
		t_cell->value = old;
	} else if (sheet->last_zoom_factor_used != 1) {
		/*
		 * We're zoomed and we don't want printing to reflect that.
		 */

		rv100 = gnm_rendered_value_new ((GnmCell *)cell,
						pango_layout_get_context (rv->layout),
						rv->variable_width,
						1.0);
		rv = rv100;
	}

	/* Make sure we don't get overflow in print unless we had it in
	   display.  */
	rv->might_overflow = rv->numeric_overflow;

	if (cell_calc_layout (cell, rv, -1,
			      (int)(width * PANGO_SCALE / scale_h),
			      (int)(height * PANGO_SCALE / scale_v),
			      (int)h_center == -1 ? -1 : (int)(h_center * PANGO_SCALE),
			      &fore_color, &x, &y)) {

		/* Clip the printed rectangle */
		cairo_save (context);
#ifndef G_OS_WIN32
		if (!rv->rotation) {
			/* We do not clip rotated cells.  */
			cairo_new_path (context);
			cairo_rectangle (context, x1 + GNM_COL_MARGIN, y1 + GNM_ROW_MARGIN,
					 width + 1, height + 1);
			cairo_clip (context);
		}
#endif
		/* Set the font colour */
		cairo_set_source_rgba (context,
				       GO_COLOR_TO_CAIRO (fore_color));

		cairo_translate (context, x1+0.5, y1);

		if (rv->rotation) {
			GnmRenderedRotatedValue *rrv = (GnmRenderedRotatedValue *)rv;
			struct GnmRenderedRotatedValueInfo const *li = rrv->lines;
			GSList *lines;

			cairo_scale (context, scale_h, scale_v);
			cairo_move_to (context, 0.,0.);
			for (lines = pango_layout_get_lines (rv->layout);
			     lines;
			     lines = lines->next, li++) {
				cairo_save (context);
				cairo_move_to (context,
					       PANGO_PIXELS (x + li->dx),
					       PANGO_PIXELS (- y + li->dy));
				cairo_rotate (context, rv->rotation * (-M_PI / 180));
				pango_cairo_show_layout_line (context, lines->data);
				cairo_restore (context);
			}


		} else {
			cairo_scale (context, scale_h, scale_v);
			cairo_move_to (context, x / (double)PANGO_SCALE , - y / (double)PANGO_SCALE);
			pango_cairo_show_layout (context, rv->layout);
		}
		cairo_restore(context);
	}

	if (rv100)
		gnm_rendered_value_destroy (rv100);
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
			   GnmStyle const *style,
			   G_GNUC_UNUSED int col, G_GNUC_UNUSED int row,
			   double x, double y, double w, double h)
{
	if (gnm_pattern_background_set (style, context, FALSE, NULL))
		/* Remember api excludes the far pixels */
		print_rectangle_gtk (context, x, y, w+0.2, h+0.2);
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
print_merged_range_gtk (cairo_t *context,
			Sheet const *sheet,
			double start_x, double start_y,
			GnmRange const *view, GnmRange const *range,
			GnmPrintInformation const *pinfo)
{
	double l, r, t, b;
	int last;
	GnmCell  const *cell = sheet_cell_get (sheet, range->start.col, range->start.row);
	int const dir = sheet->text_is_rtl ? -1 : 1;
	GnmStyleConditions *conds;

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

	conds = gnm_style_get_conditions (style);
	if (style) {
		GnmEvalPos ep;
		int res;
		eval_pos_init (&ep, (Sheet *)sheet, range->start.col, range->start.row);
		if ((res = gnm_style_conditions_eval (conds, &ep)) >= 0)
			style = gnm_style_get_cond_style (style, res);
	}

	if (gnm_pattern_background_set (style, context, FALSE, NULL))
		print_rectangle_gtk (context, l, t, r-l+0.2, b-t+0.2);

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
		ColRowInfo *ri = sheet_row_get (sheet, range->start.row);

		if (ri->needs_respan)
			row_calc_spans (ri, cell->pos.row, sheet);

		if (sheet->text_is_rtl)
			print_cell_gtk (cell, context,
					r, t, l - r, b - t, -1., pinfo);
		else
			print_cell_gtk (cell, context,
					l, t, r - l, b - t, -1., pinfo);
	}
	gnm_style_border_print_diag_gtk (style, context, l, t, r, b);
}

static gint
merged_col_cmp (GnmRange const *a, GnmRange const *b)
{
	return a->start.col - b->start.col;
}


void
gnm_gtk_print_cell_range (cairo_t *context,
			  Sheet const *sheet, GnmRange *range,
			  double base_x, double base_y,
			  GnmPrintInformation const *pinfo)
{
	ColRowInfo const *ri = NULL, *next_ri = NULL;
	int const dir = sheet->text_is_rtl ? -1 : 1;
	double const hscale = sheet->display_formulas ? 2 : 1;
	int start_row, start_col, end_col, end_row;

	GnmStyleRow sr, next_sr;
	GnmStyle const **styles;
	GnmBorder const **borders, **prev_vert;
	GnmBorder const *none;
	gpointer *sr_array_data;

	int n, col, row;
	double x, y, offset;
	GnmRange     view;
	GSList	 *merged_active, *merged_active_seen,
		 *merged_used, *merged_unused, *ptr, **lag;
	gboolean hide_grid;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);
	g_return_if_fail (range->start.col <= range->end.col);
	g_return_if_fail (range->start.row <= range->end.row);
	g_return_if_fail (pinfo != NULL);

	hide_grid = !pinfo->print_grid_lines;
	none = hide_grid ? NULL : gnm_style_border_none ();

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

	sheet_style_update_grid_color (sheet, NULL);

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
	sr_array_data = g_new (gpointer, n * 8);
	style_row_init (&prev_vert, &sr, &next_sr, start_col, end_col,
			sr_array_data, hide_grid);

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

		/* it is safe to const_cast because only a non-default row
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
					print_merged_range_gtk (context, sheet,
								base_x, y, &view, r,
								pinfo);
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
				x -= ci->size_pts * hscale;
			style = sr.styles [col];
			print_cell_background_gtk (context, style, col, row, x, y,
						   ci->size_pts * hscale, ri->size_pts);

			/* Is this part of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->spans != NULL)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (NULL == ri->spans || NULL == (span = row_span_get (ri, col))) {
				/* no need to draw blanks */
				GnmCell const *cell = sheet_cell_get (sheet, col, row);
				if (!gnm_cell_is_empty (cell))
					print_cell_gtk (cell, context, x, y,
							ci->size_pts * hscale,
							ri->size_pts, -1., pinfo);

			/* Only draw spaning cells after all the backgrounds
			 * that we are going to draw have been drawn.  No need
			 * to draw the edit cell, or blanks.
			 */
			} else if (col == span->right || col == end_col) {
				GnmCell const *cell = span->cell;
				int const start_span_col = span->left;
				int const end_span_col = span->right;
				double real_x = x;
				ColRowInfo const *cell_col =
					sheet_col_get_info (sheet, cell->pos.col);
				double center_offset = cell_col->size_pts * hscale / 2;
				double tmp_width = ci->size_pts * hscale;

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

				print_cell_gtk (cell, context,
						real_x, y, tmp_width, ri->size_pts,
						center_offset, pinfo);
			} else if (col != span->left)
				sr.vertical [col] = NULL;

			if (dir > 0)
				x += ci->size_pts * hscale;
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
	g_free (sr_array_data);
	g_return_if_fail (merged_active == NULL);
}


