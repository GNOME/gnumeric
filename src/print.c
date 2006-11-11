/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * print.c: Printing routines for Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Morten Welinder (terra@gnome.org)
 *
 * Handles printing of Sheets.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "print.h"

#include "gui-util.h"
#include "sheet-object.h"
#include "sheet-object-impl.h"
#include "selection.h"
#include "workbook.h"
#include "workbook-control.h"
#include "workbook-edit.h"
#include "command-context.h"
#include "dialogs.h"
#include "gnumeric-gconf.h"
#include "libgnumeric.h"
#include "sheet.h"
#include "value.h"
#include "cellspan.h"
#include "print-info.h"
#include "print-cell.h"
#include "application.h"
#include "sheet-style.h"
#include "ranges.h"
#include "style-font.h"
#include "gnumeric-gconf.h"
#include <goffice/utils/go-font.h>

#include <libgnomeprint/gnome-print-job.h>
#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/gnome-print-pango.h>
#include <libgnomeprintui/gnome-print-job-preview.h>
#include <libgnomeprintui/gnome-print-dialog.h>

#define PRINT_DIALOG_KEY "Gnumeric_Print_Dialog"

/*
 * Margins
 *
 * In the internal format, top and bottom margins, header and footer have
 * the same semantics as in Excel.  That is:
 *
 * +----------------------------------------+
 * |   ^            ^                       |
 * |   | top        |header                 |
 * |   |margin      v                       |
 * |-- |-------------                       |
 * |   v           header text              |
 * |------+--------------------------+------|
 * |      |                          |      |
 * |<---->|                          |<---->| ^
 * | left |           Cell           |right | |Increasing
 * |margin|                          |margin| |y
 * |      |           Grid           |      | |
 *
 * ~      ~                          ~      ~
 *
 * |      |                          |      |
 * |------+--------------------------+------|
 * |   ^           footer text              |
 * |-- |------------------------------------|
 * |   |bottom      ^                       |
 * |   |margin      |footer                 |
 * |   v            v                       |
 * +----------------------------------------+
 *
 * In the GUI on the other hand, top margin means the empty space above the
 * header text, and header area means the area from the top of the header
 * text to the top of the cell grid.  */

typedef struct {
	/*
	 * Part 1: The information the user configures on the Print Dialog
	 */
	PrintRange range;
	int start_page, end_page; /* Interval */
	gboolean sorted_print;
	gboolean is_preview;

	int current_output_sheet;		/* current sheet number during output */

	/*
	 * Part 2: Handy pre-computed information passed around
	 */
	double width, height;	/* total dimensions */
	double x_points;	/* real usable X (ie, width - margins) */
	double y_points;	/* real usable Y (ie, height - margins) */
	double titles_used_x;	/* points used by the X titles */
	double titles_used_y;	/* points used by the Y titles */

	/* The repeat columns/rows ranges used space */
	double repeat_cols_used_x;
	double repeat_rows_used_y;

	/*
	 * Part 3: Handy pointers
	 */
	GnomePrintContext *print_context;

	/*
	 * Part 5: Headers and footers
	 */
	HFRenderInfo *render_info;
	PangoLayout *decoration_layout;

	/* 6: The config */
	GnomePrintConfig *gp_config;
} PrintJobInfo;

static void
print_titles (PrintJobInfo const *pj, Sheet const *sheet, GnmRange *range,
	      double base_x, double base_y)
{
#warning TODO
}

static void
print_sheet_objects (PrintJobInfo const *pj, Sheet const *sheet, GnmRange *range,
		     double base_x, double base_y)
{
	GSList *ptr;
	double end_x, end_y, len;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (pj != NULL);
	g_return_if_fail (range != NULL);

	gnome_print_gsave (pj->print_context);

	/* Make sure the object doesn't go beyond the specified cells. */
	end_y = base_y - sheet_row_get_distance_pts (sheet, range->start.row,
						     range->end.row + 1);
	len = sheet_col_get_distance_pts (sheet,
		range->start.col, range->end.col + 1);
	if (sheet->text_is_rtl) {
		end_x = base_x - len;
		gnm_print_make_rect_path (pj->print_context,
			end_x, end_y, base_x, base_y);
	} else {
		end_x = base_x + len;
		gnm_print_make_rect_path (pj->print_context,
			base_x, base_y, end_x, end_y);
	}
#ifndef NO_DEBUG_PRINT
	if (print_debugging > 0) {
		gnome_print_gsave (pj->print_context);
		gnome_print_stroke (pj->print_context);
		gnome_print_moveto (pj->print_context, base_x, base_y);
		gnome_print_lineto (pj->print_context, end_x, end_y);
		gnome_print_stroke (pj->print_context);
		gnome_print_grestore (pj->print_context);
	}
#endif
	gnome_print_clip (pj->print_context);

	for (ptr = sheet->sheet_objects; ptr; ptr = ptr->next) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		double coords [4];

		if (!sheet_object_can_print (so) ||
		    !range_overlap (range, &so->anchor.cell_bound))
			continue;

		sheet_object_position_pts_get (so, coords);
		gnome_print_gsave (pj->print_context);
		/* move to top left */
		if (sheet->text_is_rtl)
			gnome_print_translate (pj->print_context,
				base_x - (MAX (coords [0], coords [2])
				    - sheet_col_get_distance_pts (sheet, 0, range->start.col)),
				base_y - (MIN (coords [3], coords [1])
				    - sheet_row_get_distance_pts (sheet, 0, range->start.row)));
		else
			gnome_print_translate (pj->print_context,
				base_x + (MIN (coords [0], coords [2])
				    - sheet_col_get_distance_pts (sheet, 0, range->start.col)),
				base_y - (MIN (coords [3], coords [1])
				    - sheet_row_get_distance_pts (sheet, 0, range->start.row)));

		sheet_object_print (so, pj->print_context,
			fabs (coords[2] - coords[0]),
			fabs (coords[3] - coords[1]));
		gnome_print_grestore (pj->print_context);
	}

	gnome_print_grestore (pj->print_context);
}

static void
print_page_cells (PrintJobInfo const *pj, Sheet const *sheet, GnmRange *range,
		  double base_x, double base_y)
{
	PrintInformation const *pi = sheet->print_info;

	/* Invert PostScript Y coordinates to make X&Y cases the same */
	base_y = (pj->height / (pi->scaling.percentage.y / 100.)) - base_y;

	if (sheet->text_is_rtl)
		base_x += (pj->width / (pi->scaling.percentage.x / 100.));
	gnm_print_cell_range (pj->print_context, sheet, range,
			  base_x, base_y, !pi->print_grid_lines);
	print_sheet_objects (pj, sheet, range, base_x, base_y);
}

/*
 * print_page_repeated_rows
 *
 * It is up to the caller to determine if repeated rows should be printed on
 * the page.
 */
static void
print_page_repeated_rows (PrintJobInfo const *pj, Sheet const *sheet,
			  int start_col, int end_col,
			  double base_x, double base_y)
{
	PrintInformation const *pi = sheet->print_info;
	GnmRange const *r = &pi->repeat_top.range;
	GnmRange range;

	range_init (&range, start_col, MIN (r->start.row, r->end.row),
			    end_col,   MAX (r->start.row, r->end.row));
	print_page_cells (pj, sheet, &range, base_x, base_y);
}

/*
 * print_page_repeated_cols
 *
 * It is up to the caller to determine if repeated columns should be printed
 * on the page.
 */
static void
print_page_repeated_cols (PrintJobInfo const *pj, Sheet const *sheet,
			  int start_row, int end_row,
			  double base_x, double base_y)
{
	PrintInformation const *pi = sheet->print_info;
	GnmRange const *r = &pi->repeat_left.range;
	GnmRange range;

	range_init (&range, MIN (r->start.col, r->end.col), start_row,
			    MAX (r->start.col, r->end.col), end_row);
	print_page_cells (pj, sheet, &range, base_x, base_y);
}

/*
 * print_page_repeated_intersect
 *
 * Print the corner where repeated rows and columns intersect.
 *
 * It is impossible to print both from rows and columns. XL prints the cells
 * from the rows, whether order is row and column major. We do the same.
 */
static void
print_page_repeated_intersect (PrintJobInfo const *pj, Sheet const *sheet,
			       double base_x, double base_y,
			       double print_width, double print_height)
{
	PrintInformation const *pi = sheet->print_info;
	print_page_repeated_rows (pj, sheet,
				  pi->repeat_left.range.start.col,
				  pi->repeat_left.range.end.col,
				  base_x, base_y);
}

static PangoLayout *
ensure_decoration_layout (PrintJobInfo *pj)
{
	if (!pj->decoration_layout) {
		PangoLayout *layout = gnome_print_pango_create_layout (pj->print_context);
		/*
		 * Copy the style so we don't leave a cached GnmFont in the
		 * prefs object.
		 */
		GnmStyle *style = gnm_style_dup (gnm_app_prefs->printer_decoration_font);
		GnmFont *font = gnm_style_get_font
			(style,
			 pango_layout_get_context (layout),
			 1.);

		pj->decoration_layout = layout;
		pango_layout_set_font_description (layout, font->go.font->desc);
		gnm_style_unref (style);
	}
	return pj->decoration_layout;
}


/*
 * print_hf_element
 * @pj: printing context
 * @format:
 * @side:
 * @y:
 *
 * Print a header/footer line.
 *
 * Position at y, and clip to rectangle. If print_debugging is > 0, display
 * the rectangle.
 */
static void
print_hf_element (PrintJobInfo const *pj, Sheet const *sheet, char const *format,
		  PangoAlignment side, double y, gboolean align_bottom)
{
	PrintInformation const *pi = sheet->print_info;
	char *text;

	g_return_if_fail (pj != NULL);
	g_return_if_fail (pj->render_info != NULL);

	if (format == NULL)
		return;
	text = hf_format_render (format, pj->render_info, HF_RENDER_PRINT);

	if (text && text[0]) {
		double header = 0, footer = 0, left = 0, right = 0;
		PangoLayout *layout = ensure_decoration_layout ((PrintJobInfo *)pj);

		print_info_get_margins (pi, &header, &footer, &left, &right);
		pango_layout_set_alignment (layout, side);
		pango_layout_set_width (layout, (pj->width - left - right) * PANGO_SCALE);
		pango_layout_set_text (layout, text, -1);

		if (align_bottom) {
			int height;
			pango_layout_get_size (layout, NULL, &height);
			y += height / (double)PANGO_SCALE;
		}

		gnome_print_moveto (pj->print_context, left, y);
		gnome_print_pango_layout (pj->print_context, layout);
	}
	g_free (text);
}

/*
 * print_hf_line
 * @pj:     printing context
 * @hf:     header/footer descriptor
 * @y:      vertical position
 * @left:   left coordinate of clip rectangle
 * @bottom: bottom coordinate of clip rectangle
 * @right:  right coordinate of clip rectangel
 * @top:    top coordinate of clip rectangle
 *
 * Print a header/footer line.
 *
 * Position at y, and clip to rectangle. If print_debugging is > 0, display
 * the rectangle.
 */
static void
print_hf_line (PrintJobInfo const *pj, Sheet const *sheet,
	       PrintHF const *hf,
	       double y,
	       double left, double bottom, double right, double top,
	       gboolean align_bottom)
{
	/* Check if there's room to print. top and bottom are on the clip
	 * path, so we are actually requiring room for a 6x4 pt
	 * character. */
	if (ABS (top - bottom) < 8)
		return;
	if (ABS (left - right) < 6)
		return;

	gnome_print_gsave (pj->print_context);

	gnome_print_setrgbcolor (pj->print_context, 0, 0, 0);

	gnm_print_make_rect_path (pj->print_context,
				   left, bottom, right, top);

#ifndef NO_DEBUG_PRINT
	if (print_debugging > 0) {
		static const double dash[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
		static gint n_dash = G_N_ELEMENTS (dash);

		gnome_print_gsave (pj->print_context);
		gnome_print_setdash (pj->print_context, n_dash, dash, 0.0);
		gnome_print_stroke  (pj->print_context);
		gnome_print_grestore (pj->print_context);
	}
#endif
	/* Clip the header or footer */
	gnome_print_clip      (pj->print_context);

	print_hf_element (pj, sheet, hf->left_format, PANGO_ALIGN_LEFT, y, align_bottom);
	print_hf_element (pj, sheet, hf->middle_format, PANGO_ALIGN_CENTER, y, align_bottom);
	print_hf_element (pj, sheet, hf->right_format, PANGO_ALIGN_RIGHT, y, align_bottom);

	gnome_print_grestore (pj->print_context);
}

/*
 * print_headers
 * @pj: printing context
 * Print headers
 *
 * Align ascenders flush with inside of top margin.
 */
static void
print_headers (PrintJobInfo const *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	PrintMargins const *pm = &pi->margin;
	double top, bottom, y;
	double header = 0, footer = 0, left = 0, right = 0;

	print_info_get_margins (pi, &header, &footer, &left, &right);

	y = pj->height - header;
	top    =  1 + pj->height - MIN (header, pm->top.points);
	bottom = -1 + pj->height - MAX (header, pm->top.points);

	print_hf_line (pj, sheet, pi->header, y,
		       -1 + left, bottom,
		       pj->width - right, top,
		       FALSE);
}

/*
 * print_footers
 * @pj: printing context
 * Print footers
 *
 * Align descenders flush with inside of bottom margin.
 */
static void
print_footers (PrintJobInfo const *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	PrintMargins const *pm = &pi->margin;
	double top, bottom, y;
	double header = 0, footer = 0, left = 0, right = 0;

	print_info_get_margins (pi, &header, &footer, &left, &right);

	y = footer;
	top    =  1 + MAX (footer, pm->bottom.points);
	bottom = -1 + MIN (footer, pm->bottom.points);

	/* Clip path for the header
	 * NOTE : postscript clip paths exclude the border, gdk includes it.
	 */
	print_hf_line (pj, sheet, pi->footer, y,
		       -1 + left, bottom,
		       pj->width - right, top,
		       TRUE);
}

static void
setup_scale (PrintJobInfo const *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	double affine [6];
	double x_scale = pi->scaling.percentage.x / 100.;
	double y_scale = pi->scaling.percentage.y / 100.;

	art_affine_scale (affine, x_scale, y_scale);
	gnome_print_concat (pj->print_context, affine);
}

static GnmValue *
cb_range_empty (GnmCellIter const *iter, gpointer flags)
{
	return (iter->ci->visible && iter->ri->visible) ? VALUE_TERMINATE : NULL;
}

/**
 * print_page:
 * @pj:        printing context
 * @sheet:     the sheet to print
 * @range:     a range
 *
 * Excel prints repeated rows like this: Pages up to and including the page
 * where the first of the repeated rows "naturally" occurs are printed in
 * the normal way. On subsequent pages, repated rows are printed before the
 * regular flow.
 */
static int
print_page (PrintJobInfo const *pj, Sheet const *sheet, GnmRange *range,
	    gboolean output)
{
	PrintInformation const *pi = sheet->print_info;
	PrintMargins const *pm = &pi->margin;
	/* print_height/width are sizes of the regular grid,
	 * not including repeating rows and columns */
	double print_height, print_width;
	double repeat_cols_used_x = 0., repeat_rows_used_y = 0.;
	double x, y, clip_y;
	char *pagenotxt;
	gboolean printed;
	GSList *ptr;
	double header = 0, footer = 0, left = 0, right = 0;

	/* FIXME: Can col / row space calculation be factored out? */

	/* Space for repeated rows depends on whether we print them or not */
	if (pi->repeat_top.use &&
	    range->start.row > pi->repeat_top.range.start.row) {
		repeat_rows_used_y = pj->repeat_rows_used_y;
		/* Make sure start_row never is inside the repeated range */
		range->start.row = MAX (range->start.row,
				        pi->repeat_top.range.end.row + 1);
	} else
		repeat_rows_used_y = 0;

	/* Space for repeated cols depends on whether we print them or not */
	if (pi->repeat_left.use &&
	    range->start.col > pi->repeat_left.range.start.col) {
		repeat_cols_used_x = pj->repeat_cols_used_x;
		/* Make sure start_col never is inside the repeated range */
		range->start.col = MAX (range->start.col,
				        pi->repeat_left.range.end.col + 1);
	} else
		repeat_cols_used_x = 0;

	/* If there are no cells in the area check for spans */
	printed = (NULL != sheet_foreach_cell_in_range ((Sheet *)sheet,
							CELL_ITER_IGNORE_NONEXISTENT,
							range->start.col,
							range->start.row,
							range->end.col,
							range->end.row,
							cb_range_empty, NULL));
	if (!printed) {
		int i = range->start.row;
		for (; i <= range->end.row ; ++i) {
			ColRowInfo const *ri = sheet_row_get_info (sheet, i);
			if (ri->visible &&
			    (NULL != row_span_get (ri, range->start.col) ||
			     NULL != row_span_get (ri, range->end.col))) {
				printed = TRUE;
				break;
			}
		}
	}
	if (!printed && pi->print_even_if_only_styles)
		printed = sheet_style_has_visible_content (sheet, range);

	/* Check for sheet objects if nothing has been found so far */
	if (!printed)
		for (ptr = sheet->sheet_objects; ptr && !printed; ptr = ptr->next) {
			SheetObject *so = SHEET_OBJECT (ptr->data);
			printed = range_overlap (range, &so->anchor.cell_bound);
		}

	if (!output)
		return printed;

	if (!printed)
		return 0;

	x = 0.;
	y = 0.;

	print_height = sheet_row_get_distance_pts (sheet, range->start.row,
						   range->end.row + 1);
	if (pi->center_vertically){
		double h = print_height;

		if (pi->print_titles)
			h += sheet->rows.default_style.size_pts;
		h += repeat_rows_used_y;
		h *= pi->scaling.percentage.y / 100.;
		y = (pj->y_points - h)/2;
	}

	print_width = sheet_col_get_distance_pts (sheet, range->start.col,
						  range->end.col + 1);
	if (pi->center_horizontally){
		double w = print_width;

		if (pi->print_titles)
			w += sheet->cols.default_style.size_pts;
		w += repeat_cols_used_x;
		w *= pi->scaling.percentage.x / 100.;
		x = (pj->x_points - w)/2;
	}

	print_info_get_margins (pi, &header, &footer, &left, &right);
	/* Margins */
	x += left;
	y += MAX (pm->top.points, header);
	if (pi->print_grid_lines) {
		/* the initial grid lines */
		x += 1.;
		y += 1.;
	} else {
		/* If there are no grids alias back to avoid a penalty for the
		 * margins of the leading gridline */
		x -= GNM_COL_MARGIN;
		y -= GNM_ROW_MARGIN;
	}

	/* Note: we cannot have spaces in page numbers.  */
	pagenotxt = hf_format_render (_("&[PAGE]"),
				      pj->render_info, HF_RENDER_PRINT);
	if (!pagenotxt)
		pagenotxt = g_strdup_printf ("%d", pj->render_info->page);
	gnome_print_beginpage (pj->print_context, pagenotxt);
	g_free (pagenotxt);

	print_headers (pj, sheet);
	print_footers (pj, sheet);

	/*
	 * Print any titles that might be used
	 */
	if (pi->print_titles) {
		print_titles (pj, sheet, range, x, y);
		x += sheet->cols.default_style.size_pts;
		y += sheet->rows.default_style.size_pts;
	}

	/* Clip the page */
	/* Gnome-print coordinates are lower left based,
	 * like Postscript */
	clip_y = 1 + pj->height - y;
	gnm_print_make_rect_path (
		pj->print_context,
		x - 1, clip_y,
		x + pj->x_points + 1,
		clip_y - pj->y_points - 2);
#ifndef NO_DEBUG_PRINT
	if (print_debugging > 0) {
		gnome_print_gsave (pj->print_context);
		gnome_print_stroke  (pj->print_context);
		gnome_print_grestore (pj->print_context);
	}
#endif
	gnome_print_clip (pj->print_context);

	/* Start a new path because the background fill function does not */
	gnome_print_newpath (pj->print_context);

	setup_scale (pj, sheet);
	x /= pi->scaling.percentage.x / 100.;
	y /= pi->scaling.percentage.y / 100.;

	if (pi->repeat_top.use && repeat_rows_used_y > 0.) {
		/* Intersection of repeated rows and columns */
		if (pi->repeat_left.use && repeat_cols_used_x > 0.)
			print_page_repeated_intersect (pj, sheet,
				x, y, repeat_cols_used_x, repeat_rows_used_y);

		print_page_repeated_rows (pj, sheet, range->start.col,
					  range->end.col,
					  x + repeat_cols_used_x, y);
		y += repeat_rows_used_y;
	}

	if (pi->repeat_left.use && repeat_cols_used_x > 0. ) {
		print_page_repeated_cols (pj, sheet, range->start.row,
					  range->end.row, x, y);
		x += repeat_cols_used_x;
	}

	print_page_cells (pj, sheet, range, x, y);

	gnome_print_showpage (pj->print_context);

	return 1;
}

/*
 * Computes number of rows or columns that fit in @usable starting
 * at index @start and limited to index @end
 */
static int
compute_group (PrintJobInfo const *pj, Sheet const *sheet,
	       int start, int end, double usable,
	       ColRowInfo const *(get_info)(Sheet const *sheet, int const p))
{
	double size_pts = 1.; /* The initial grid line */
	int idx, count = 0;

	for (idx = start; idx <= end; idx++, count++) {
		ColRowInfo const *info = (*get_info) (sheet, idx);
		if (info->visible) {
			size_pts += info->size_pts;
			if (size_pts > usable)
				break;
		}
	}

	/* FIXME : Find a way to inform the user that one of the rows/cols does
	 * not fit on a page
	 */
	g_return_val_if_fail (count > 0, 1);

	return count;
}

/* computer_scale_fit_to
 * Computes the scaling needed to fit all the rows or columns into the @usable
 * area.
 * This function is called when printing, and the user has selected the 'fit-to'
 * printing option. It will adjust the internal x and y scaling values to
 * make the sheet fit the desired number of pages, as suggested by the user.
 * It will only reduce the scaling to fit inside a page, not enlarge.
 */
static double
compute_scale_fit_to (PrintJobInfo const *pj, Sheet const *sheet,
		      int start, int end, double usable,
		      ColRowInfo const *(get_info)(Sheet const *sheet, int const p),
		      gint pages)
{
	double size_pts; /* The initial grid line on each page*/
	int idx;
	double scale;
	double max_unit = 0;

	/* This means to take whatever space is needed.  */
	if (pages <= 0)
		return 100;

	size_pts =  1. * pages;

	/* Work how much space the sheet requires. */
	for (idx = start; idx <= end; idx++) {
		ColRowInfo const *info = (*get_info) (sheet, idx);
		if (info->visible) {
			size_pts += info->size_pts;
			if (info->size_pts > max_unit)
				max_unit = (info->size_pts > usable) ?
					usable : info->size_pts;
		}
	}

	usable *= pages; /* Our usable area is this big. */

	/* What scale is required to fit the sheet onto this usable area? */
	/* Note that on each page but the last we may loose space that can */
	/* be nearly as large as the largest unit. */
	scale = usable / (size_pts + (pages - 1) * max_unit) * 100.;

	/* If the sheet needs to be shrunk, we update the scale.
	 * If it already fits, we simply leave the scale at 100.
	 * Another feature might be to enlarge a sheet so that it fills
	 * the page. But this is not a requested feature yet.
	 */
	return (scale < 100.) ? scale : 100.;
}

#define COL_FIT(col) (col >= SHEET_MAX_COLS ? (SHEET_MAX_COLS-1) : col)
#define ROW_FIT(row) (row >= SHEET_MAX_ROWS ? (SHEET_MAX_ROWS-1) : row)

static int
print_range_down_then_across (PrintJobInfo const *pj, Sheet const *sheet,
			      GnmRange const *r, gboolean output)
{
	PrintInformation *pi = sheet->print_info;
	double usable_x, usable_x_initial, usable_x_repeating;
	double usable_y, usable_y_initial, usable_y_repeating;
	int pages = 0;
	int col = r->start.col;
	gboolean printed;

	usable_x_initial   = pj->x_points - pj->titles_used_x;
	usable_x_repeating = usable_x_initial - pj->repeat_cols_used_x;
	usable_y_initial   = pj->y_points - pj->titles_used_y;
	usable_y_repeating = usable_y_initial - pj->repeat_rows_used_y;

	if (pi->scaling.type == PRINT_SCALE_FIT_PAGES) {
		int col = r->start.col;
		int row = r->start.row;

		if (col < pi->repeat_left.range.end.col) {
			usable_x = usable_x_initial;
			col = MIN (col, pi->repeat_left.range.end.col);
		} else
			usable_x = usable_x_repeating;
		pi->scaling.percentage.x = compute_scale_fit_to (pj, sheet, col, r->end.col,
			usable_x, sheet_col_get_info, pi->scaling.dim.cols);

		if (row < pi->repeat_top.range.end.row) {
			usable_y = usable_y_initial;
			row = MIN (row, pi->repeat_top.range.end.row);
		} else
			usable_y = usable_y_repeating;
		pi->scaling.percentage.y = compute_scale_fit_to (pj, sheet, row, r->end.row,
			usable_y, sheet_row_get_info, pi->scaling.dim.rows);

		/* Making sure that the x and y scales are identical */
		if (pi->scaling.percentage.y > pi->scaling.percentage.x)
			pi->scaling.percentage.y = pi->scaling.percentage.x;
		else
			pi->scaling.percentage.x = pi->scaling.percentage.y;
	}

	while (col <= r->end.col) {
		int col_count;
		int row = r->start.row;

		if (col <= pi->repeat_left.range.end.col) {
			usable_x = usable_x_initial;
			col = MIN (col,
				   pi->repeat_left.range.end.col);
		} else
			usable_x = usable_x_repeating;

		usable_x /= pi->scaling.percentage.x / 100.;
		col_count = compute_group (pj, sheet, col, r->end.col,
					   usable_x, sheet_col_get_info);

		while (row <= r->end.row) {
			GnmRange range;
			int row_count;

			if (row <= pi->repeat_top.range.end.row) {
				usable_y = usable_y_initial;
				row = MIN (row,
					   pi->repeat_top.range.end.row);
			} else
				usable_y = usable_y_repeating;

			usable_y /= pi->scaling.percentage.y / 100.;
			row_count = compute_group (pj, sheet, row, r->end.row,
						   usable_y, sheet_row_get_info);
			range_init (&range, COL_FIT (col), ROW_FIT (row),
				    COL_FIT (col + col_count - 1),
				    ROW_FIT (row + row_count - 1));
			printed = print_page (pj, sheet, &range, output);

			row += row_count;

			if (printed) {
				pages++;
				/* Only update page count when actually printing */
				if (output)
					pj->render_info->page++;
			}
		}
		col += col_count;
	}

	return pages;
}

static int
print_range_across_then_down (PrintJobInfo const *pj, Sheet const *sheet,
			      GnmRange const *r, gboolean output)
{
	PrintInformation *pi = sheet->print_info;
	double usable_x, usable_x_initial, usable_x_repeating;
	double usable_y, usable_y_initial, usable_y_repeating;
	int pages = 0;
	int row = r->start.row;
	gboolean printed;

	usable_x_initial   = pj->x_points - pj->titles_used_x;
	usable_x_repeating = usable_x_initial - pj->repeat_cols_used_x;
	usable_y_initial   = pj->y_points - pj->titles_used_y;
	usable_y_repeating = usable_y_initial - pj->repeat_rows_used_y;

	if (pi->scaling.type == PRINT_SCALE_FIT_PAGES) {
		int col = r->start.col;
		int row = r->start.row;

		if (col < pi->repeat_left.range.end.col) {
			usable_x = usable_x_initial;
			col = MIN (col, pi->repeat_left.range.end.col);
		} else
			usable_x = usable_x_repeating;
		pi->scaling.percentage.x = compute_scale_fit_to (pj, sheet, col, r->end.col,
			usable_x, sheet_col_get_info, pi->scaling.dim.cols);

		if (row < pi->repeat_top.range.end.row) {
			usable_y = usable_y_initial;
			row = MIN (row, pi->repeat_top.range.end.row);
		} else
			usable_y = usable_y_repeating;
		pi->scaling.percentage.y = compute_scale_fit_to (pj, sheet, row, r->end.row,
			usable_y, sheet_row_get_info, pi->scaling.dim.rows);

		if (pi->scaling.percentage.y > pi->scaling.percentage.x)
			pi->scaling.percentage.y = pi->scaling.percentage.x;
		else
			pi->scaling.percentage.x = pi->scaling.percentage.y;
	}

	while (row <= r->end.row) {
		int row_count;
		int col = r->start.col;

		if (row <= pi->repeat_top.range.end.row) {
			usable_y = usable_y_initial;
			row = MIN (row, pi->repeat_top.range.end.row);
		} else
			usable_y = usable_y_repeating;

		usable_y /= pi->scaling.percentage.y / 100.;
		row_count = compute_group (pj, sheet, row, r->end.row,
					   usable_y, sheet_row_get_info);

		while (col <= r->end.col) {
			GnmRange range;
			int col_count;

			if (col <= pi->repeat_left.range.end.col) {
				usable_x = usable_x_initial;
				col = MIN (col,
					   pi->repeat_left.range.end.col);
			} else
				usable_x = usable_x_repeating;

			usable_x /= pi->scaling.percentage.x / 100.;
			col_count = compute_group (pj, sheet, col, r->end.col,
						   usable_x, sheet_col_get_info);

			range_init (&range, COL_FIT (col), ROW_FIT (row),
				    COL_FIT (col + col_count - 1),
				    ROW_FIT (row + row_count - 1));
			printed = print_page (pj, sheet, &range, output);

			col += col_count;

			if (printed) {
				pages++;
				/* Only update page count when actually printing */
				if (output)
					pj->render_info->page++;
			}
		}
		row += row_count;
	}

	return pages;
}

static double
print_range_used_units (Sheet const *sheet, gboolean compute_rows,
			PrintRepeatRange const *range)
{
	GnmRange const *r = &range->range;
	if (compute_rows)
		return sheet_row_get_distance_pts
			(sheet, r->start.row, r->end.row+1);
	else
		return sheet_col_get_distance_pts
			(sheet, r->start.col, r->end.col+1);
}

static void
print_job_info_init_sheet (PrintJobInfo *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	PrintMargins const *pm = &sheet->print_info->margin;
	double header = 0, footer = 0, left = 0, right = 0;

	if (!gnome_print_config_get_page_size (pj->gp_config, &pj->width, &pj->height))
		pj->width = pj->height = 1.;

	print_info_get_margins (pi, &header, &footer, &left, &right);
	pj->x_points = pj->width - (left + right);
	pj->y_points = pj->height -
		(MAX (pm->top.points, header) +
		 MAX (pm->bottom.points, footer));

	if (pi->print_titles) {
		pj->titles_used_x = sheet->cols.default_style.size_pts;
		pj->titles_used_y = sheet->rows.default_style.size_pts;
	} else {
		pj->titles_used_x = 0;
		pj->titles_used_y = 0;
	}

	pj->repeat_rows_used_y = (pi->repeat_top.use)
	    ? print_range_used_units (sheet, TRUE, &pi->repeat_top)
	    : 0.;
	pj->repeat_cols_used_x = (pi->repeat_left.use)
	    ? print_range_used_units (sheet, FALSE, &pi->repeat_left)
	    : 0.;

	pj->render_info->sheet = sheet;
}

/*
 * @print_sheet_range:
 * @sheet: the sheet being printed
 * @r: the requested range of cells to be printed.
 * @pj: print context
 * @output: %TRUE if we actually want to print, %FALSE if we want to just
 *          use print_sheet_range() to probe whether the range contains data
 *
 * This routine is used for both printing as well as computing the number
 * of pages with actual content to print
 *
 * Return value: the number of pages printed.
 */
static int
print_sheet_range (PrintJobInfo *pj, Sheet const *sheet,
		   GnmRange const *r, gboolean output)
{
	print_job_info_init_sheet (pj, sheet);
 	if (sheet->print_info->print_across_then_down)
		return print_range_across_then_down (pj, sheet, r, output);
	else
		return print_range_down_then_across (pj, sheet, r, output);
}

/*
 * code to count the number of pages that will be printed.
 * Unfortuantely a lot of data here is calculated again when you
 * actually print the page ...
 */

typedef struct _PageCountInfo {
	int pages;
	PrintJobInfo *pj;
	GnmRange r;
	int current_output_sheet;
} PageCountInfo;

static void
compute_sheet_pages (PageCountInfo *pc, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	PrintJobInfo *pj = pc->pj;
	GnmRange r;

	/* only count pages we are printing */
	if (pj->range == PRINT_SHEET_RANGE) {
		pc->current_output_sheet++;
		if (!(pc->current_output_sheet-1 >= pj->start_page &&
		      pc->current_output_sheet-1 <= pj->end_page))
			return;
	}

	if (pj->range != PRINT_SHEET_SELECTION) {
		r = sheet_get_printarea	(sheet,
					 pi->print_even_if_only_styles);
	} else
		r = pc->r;

	/* Find out how many pages actually contain data */
	pc->pages += print_sheet_range (pj, sheet, &r, FALSE);
}

/*
 * Computes the number of pages that will be output by a specific
 * print request.
 */
static int
compute_pages (PrintJobInfo *pj,
	       Workbook const *wb, Sheet const *sheet, GnmRange const *r)
{
	PageCountInfo *pc = g_new0 (PageCountInfo, 1);
	int pages;

	pc->pj = pj;
	if (r)
		pc->r = *r;

	if (wb != NULL) {
		int i;
		for (i = 0; i < workbook_sheet_count (wb); i++)
			compute_sheet_pages (pc, workbook_sheet_by_index (wb, i));
	} else
		compute_sheet_pages (pc, sheet);

	pages = pc->pages;
	g_free (pc);
	return MAX (pages, 1);
}

static void
print_sheet (PrintJobInfo *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	GnmRange extent;

	g_return_if_fail (pj != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* if printing a range, check we are in range, otherwise iterate */
	if (pj->range == PRINT_SHEET_RANGE) {
		pj->current_output_sheet++;
		if (!(pj->current_output_sheet-1 >= pj->start_page
		      && pj->current_output_sheet-1 <= pj->end_page))
			return;
	}

	extent = sheet_get_printarea (sheet,
				      pi->print_even_if_only_styles);
	print_sheet_range (pj, sheet, &extent, TRUE);
}

/* should this print a selection over any range of pages? */
static void
sheet_print_selection (PrintJobInfo *pj, Sheet const *sheet,
		       WorkbookControl *wbc)
{
	GnmRange const *sel;
	GnmRange extent;

	if (!(sel = selection_first_range (sheet_get_view (sheet, wb_control_view (wbc)),
					   GO_CMD_CONTEXT (wbc), _("Print Region"))))
		return;

	extent = *sel;
	pj->render_info->pages = compute_pages (pj, sheet->workbook, NULL, &extent);

	print_sheet_range (pj, sheet, &extent, TRUE);
}

static PrintJobInfo *
print_job_info_get (Sheet *sheet, PrintRange range, gboolean const preview)
{
	PrintJobInfo *pj = g_new0 (PrintJobInfo, 1);

	pj->gp_config = print_info_make_config (sheet->print_info);

	/* Values that should be entered in a dialog box */
	pj->start_page = 0;
	pj->end_page = workbook_sheet_count (sheet->workbook) - 1;
	pj->range = range;
	pj->sorted_print = TRUE;
	pj->is_preview = preview;
	pj->current_output_sheet = 0;

	/*
	 * Setup render info
	 */
	pj->render_info = hf_render_info_new ();
	pj->render_info->sheet = sheet;
	pj->render_info->page = 1;

	return pj;
}

static void
print_job_info_destroy (PrintJobInfo *pj)
{
	g_object_unref (pj->gp_config);
	hf_render_info_destroy (pj->render_info);
	if (pj->decoration_layout)
		g_object_unref (pj->decoration_layout);
	if (pj->print_context)
		g_object_unref (pj->print_context);
	g_free (pj);
}

typedef struct {
	GtkWidget *dialog;
	WorkbookControlGUI  *wbcg;
	PrintJobInfo *pj;
	Sheet *sheet;
} PrintDialogState;


/**
 * dialog_destroy:
 * @window:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
dialog_destroy (GtkObject *w, PrintDialogState  *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;

	print_job_info_destroy (state->pj);
	g_free (state);

	return FALSE;
}

static void
sheet_print_real (WorkbookControlGUI *wbcg, Sheet *sheet,
		  gboolean preview, PrintJobInfo *pj, 
		  PrintRange default_range)
{
	int i;
	Workbook const *wb = sheet->workbook;
	GnomePrintJob *gpm = gnome_print_job_new (pj->gp_config);
	pj->print_context = gnome_print_job_get_context (gpm);
	pj->range = default_range;
	
	/* perform actual printing */
	switch (pj->range) {
		
	default:
		g_error ("mis-enumerated print type");
		/* Falling through */
	case PRINT_ACTIVE_SHEET:
		pj->render_info->pages = compute_pages (pj, NULL, sheet, NULL);
		print_sheet (pj, sheet);
		break;

	case PRINT_ALL_SHEETS:
	case PRINT_SHEET_RANGE:
		pj->render_info->pages = compute_pages (pj, wb, NULL, NULL);
		for (i = 0; i < workbook_sheet_count (wb); i++)
			print_sheet (pj, workbook_sheet_by_index (wb, i));
		break;
		
	case PRINT_SHEET_SELECTION:
		sheet_print_selection (pj, sheet, WORKBOOK_CONTROL (wbcg));
		break;
	}
	
	gnome_print_job_close (gpm);
	
	if (preview) {
		GtkWidget *w = gnome_print_job_preview_new (gpm, _("Print preview"));
		GdkScreen *screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
		gtk_window_set_screen (GTK_WINDOW (gtk_widget_get_toplevel (w)), screen);
		gtk_widget_show (w);
	} else {
		int result = gnome_print_job_print (gpm);
		if (result == -1) {
			/*
			 * FIXME: not a great message, but at this point we don't
			 * know *what* went wrong.
			 */
			go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				_("Printing failed"));
		}
	}
	
	if (gpm)
		g_object_unref (G_OBJECT (gpm));	
}

static void        
dialog_response (GtkDialog *dialog, gint id,
		 PrintDialogState *state)
{
	PrintRange default_range = PRINT_ACTIVE_SHEET;
	int range = GNOME_PRINT_RANGE_CURRENT;
 	int first = 1;
	int end = workbook_sheet_count (state->sheet->workbook);

	switch (id) {
	case GNOME_PRINT_DIALOG_RESPONSE_PRINT:
	case GNOME_PRINT_DIALOG_RESPONSE_PREVIEW:
		state->pj->render_info->page = 1;
		state->sheet = wbcg_cur_sheet (state->wbcg);
		range = gnome_print_dialog_get_range_page (
			GNOME_PRINT_DIALOG (state->dialog), &first, &end);
		
		switch (range) {
		case GNOME_PRINT_RANGE_CURRENT:
			default_range = PRINT_ACTIVE_SHEET;
			break;
		case GNOME_PRINT_RANGE_ALL:
			default_range = PRINT_ALL_SHEETS;
			break;
		case GNOME_PRINT_RANGE_SELECTION:
			default_range = PRINT_SHEET_SELECTION;
			break;
		case GNOME_PRINT_RANGE_RANGE:
			default_range = PRINT_SHEET_RANGE;
			break;
		}

		if (default_range == PRINT_SHEET_RANGE) {
			state->pj->start_page = first-1;
			state->pj->end_page = end-1;
		}
		
		sheet_print_real (state->wbcg, state->sheet, 
				  GNOME_PRINT_DIALOG_RESPONSE_PREVIEW == id, 
				  state->pj, default_range);

		if (id == GNOME_PRINT_DIALOG_RESPONSE_PRINT)
			gtk_widget_destroy (state->dialog);
		break;
	case GNOME_PRINT_DIALOG_RESPONSE_CANCEL:
		gtk_widget_destroy (state->dialog);
		break;
	case GTK_RESPONSE_DELETE_EVENT:
		break;
	default:
		break;
	}
}

void
gnm_print_sheet (WorkbookControlGUI *wbcg, Sheet *sheet,
		 gboolean preview, PrintRange default_range)
{
	PrintJobInfo *pj;
	GtkWidget *gnome_print_dialog = NULL;
	PrintDialogState *state = NULL;

  	g_return_if_fail (IS_SHEET (sheet));

	pj = print_job_info_get (sheet, default_range, preview);

	pj->sorted_print = FALSE;
	if (default_range == PRINT_SHEET_RANGE) {
		pj->start_page = 0;
		pj->end_page = workbook_sheet_count (sheet->workbook);
	}

	if (preview) {
		sheet_print_real (wbcg, sheet, TRUE, pj, default_range);
		print_job_info_destroy (pj);
		return;
	}

	if (gnumeric_dialog_raise_if_exists (wbcg, PRINT_DIALOG_KEY)) {
		print_job_info_destroy (pj);
		return;
	}
	
	gnome_print_dialog = g_object_new (GNOME_TYPE_PRINT_DIALOG,
					   "print-config", pj->gp_config,
					   NULL);
	
	g_return_if_fail (gnome_print_dialog != NULL);
	
	state = g_new (PrintDialogState, 1);
	state->pj = pj;
	state->wbcg = wbcg;
	state->dialog = GTK_WIDGET (gnome_print_dialog);
	state->sheet = sheet;
	
	gnome_print_dialog_construct (GNOME_PRINT_DIALOG (gnome_print_dialog),
				      _("Print Sheets"),
				      GNOME_PRINT_DIALOG_RANGE |
				      GNOME_PRINT_DIALOG_COPIES);
	
	gnome_print_dialog_construct_range_page (
		GNOME_PRINT_DIALOG (gnome_print_dialog),
		GNOME_PRINT_RANGE_CURRENT | GNOME_PRINT_RANGE_ALL |
		GNOME_PRINT_RANGE_SELECTION | GNOME_PRINT_RANGE_RANGE,
		1, workbook_sheet_count (sheet->workbook), 
		_("Act_ive sheet"), _("S_heets"));
	
	g_signal_connect (G_OBJECT (gnome_print_dialog), "destroy",
			  G_CALLBACK (dialog_destroy), state);
	g_signal_connect (G_OBJECT (gnome_print_dialog), "response",
			  G_CALLBACK (dialog_response), state);
	
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (gnome_print_dialog), PRINT_DIALOG_KEY);
	wbcg_edit_attach_guru (wbcg, GTK_WIDGET (gnome_print_dialog));
	gtk_widget_show (GTK_WIDGET (gnome_print_dialog));	
}
