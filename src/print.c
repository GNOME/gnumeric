/*
 * print.c: Printing routines for Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *
 * Handles printing of Sheets.
 *
 */
#include <config.h>
#include <gnome.h>
#include <libgnomeprint/gnome-printer.h>
#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-printer-dialog.h>
#include <libgnomeprint/gnome-print-copies.h>

#include <libgnomeprint/gnome-print-master.h>
#include <libgnomeprint/gnome-print-master-preview.h>
#include <libgnomeprint/gnome-print-dialog.h>

#ifdef ENABLE_BONOBO
#	include <bonobo/bonobo-print-client.h>
#endif

#include "gnumeric.h"
#include "gnumeric-util.h"
#include "sheet-object.h"
#include "sheet-object-impl.h"
#include "selection.h"
#include "workbook.h"
#include "dialogs.h"
#include "main.h"
#include "sheet.h"
#include "value.h"
#include "cellspan.h"
#include "print-info.h"
#include "print.h"
#include "print-cell.h"
#include "application.h"
#include "sheet-style.h"
#include "ranges.h"

extern int print_debugging;

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
	int n_copies;
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
	Sheet *sheet;
	PrintInformation *pi;
	GnomePrintContext *print_context;

	/*
	 * Part 4: Sheet objects as BonoboPrintData.
	 */
	GList *sheet_objects;

	/*
	 * Part 5: Headers and footers
	 */
	HFRenderInfo *render_info;
	GnomeFont    *decoration_font;
} PrintJobInfo;

static void
print_titles (Sheet const *sheet, int start_col, int start_row, int end_col, int end_row,
	      double base_x, double base_y, PrintJobInfo *pj)
{
}

static void
print_page_cells (Sheet const *sheet,
		  int start_col, int start_row, int end_col, int end_row,
		  double base_x, double base_y,
		  PrintJobInfo *pj)
{
	base_y = pj->height - base_y;
	print_cell_range (pj->print_context, sheet,
		start_col, start_row,
		end_col, end_row,
		base_x, base_y, !pj->pi->print_grid_lines);
}

/*
 * print_page_repeated_rows
 *
 * It is up to the caller to determine if repeated rows should be printed on
 * the page.
 */
static void
print_page_repeated_rows (Sheet const *sheet,
			  int start_col, int end_col,
			  double base_x, double base_y,
			  PrintJobInfo *pj)
{
	Range const *r = &pj->pi->repeat_top.range;
	print_page_cells (sheet,
		start_col, MIN (r->start.row, r->end.row),
		end_col,   MAX (r->start.row, r->end.row),
		base_x, base_y, pj);
}

/*
 * print_page_repeated_cols
 *
 * It is up to the caller to determine if repeated columns should be printed
 * on the page.
 */
static void
print_page_repeated_cols (Sheet const *sheet,
			  int start_row, int end_row,
			  double base_x, double base_y,
			  PrintJobInfo *pj)
{
	Range const *r = &pj->pi->repeat_left.range;
	print_page_cells (sheet,
		MIN (r->start.col, r->end.col), start_row,
		MAX (r->start.col, r->end.col), end_row,
		base_x, base_y, pj);
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
print_page_repeated_intersect (Sheet const *sheet,
			       double base_x, double base_y,
			       double print_width, double print_height,
			       PrintJobInfo *pj)
{
	print_page_repeated_rows (sheet,
				  pj->pi->repeat_left.range.start.col,
				  pj->pi->repeat_left.range.end.col,
				  base_x, base_y, pj);
}

#ifdef ENABLE_BONOBO

static void
print_page_object (Sheet const *sheet, SheetObjectPrintInfo *pi,
		   int start_col, int start_row,
		   double base_x, double base_y, PrintJobInfo *pj)
{
	double       x, y;

	g_return_if_fail (pj != NULL);
	g_return_if_fail (pi != NULL);
	g_return_if_fail (pi->so != NULL);

	x = sheet_col_get_distance_pts (sheet, 0, start_col);
	y = sheet_row_get_distance_pts (sheet, 0, start_row);

	/* FIXME: we need to calc meta_x & meta_y for scissoring */

	bonobo_print_data_render (pj->print_context,
				  base_x + pi->x_pos_pts - x,
				  pj->height - base_y - pi->y_pos_pts + y - pi->pd->height,
				  pi->pd, 0, 0/*meta_x, meta_y*/);
/*	printf ("PosX: %g + %g  - %g = %g\n", base_x, pi->x_pos_pts, x,
		base_x + pi->x_pos_pts - x);
	printf ("PosY: %g - %g - %g + %g - %g = %g\n", pj->height, base_y,
		pi->y_pos_pts, y, pi->pd->height,
		pj->height - base_y - pi->y_pos_pts + y - pi->pd->height);*/
}
#endif

typedef enum {
	LEFT_HEADER,
	RIGHT_HEADER,
	MIDDLE_HEADER,
} HFSide;

/*
 * print_hf_line
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
print_hf_element (PrintJobInfo *pj, const char *format, HFSide side, double y)
{
	PrintMargins *pm;
	char *text;
	double x;
	double len;

	/* Be really really anal in case Bug
	 * http://bugs.gnome.org/db/82/8200.html exists
	 */
	g_return_if_fail (pj != NULL);
	g_return_if_fail (pj->decoration_font != NULL);
	g_return_if_fail (pj->render_info != NULL);

	text = hf_format_render (format, pj->render_info, HF_RENDER_PRINT);

	g_return_if_fail (text != NULL);

	if (text [0] == 0) {
		g_free (text);
		return;
	}

	len = get_width_string (pj->decoration_font, text);

	pm = &pj->pi->margins;
	switch (side){
	case LEFT_HEADER:
		x = pm->left.points;
		break;

	case RIGHT_HEADER:
		x = pj->width - pm->right.points - len;
		break;

	case MIDDLE_HEADER:
		x = (pj->x_points - len)/2 + pm->left.points;
		break;

	default:
		x = 0;
	}
	gnome_print_moveto (pj->print_context, x, y);
	print_show (pj->print_context, text);
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
print_hf_line (PrintJobInfo *pj, PrintHF *hf, double y,
	       double left, double bottom, double right, double top)
{
	gnome_print_setfont (pj->print_context, pj->decoration_font);
	gnome_print_setrgbcolor (pj->print_context, 0, 0, 0);

	/* Check if there's room to print. top and bottom are on the clip
	 * path, so we are actually requiring room for a 6x4 pt
	 * character. */
	if (ABS (top - bottom) < 8)
		return;
	if (ABS (left - right) < 6)
		return;

	gnome_print_gsave (pj->print_context);

	print_make_rectangle_path (pj->print_context,
				   left, bottom, right, top);

#ifndef NO_DEBUG_PRINT
	if (print_debugging > 0) {
		static double dash[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
		static gint n_dash = 6;

		gnome_print_gsave (pj->print_context);
		gnome_print_setdash (pj->print_context, n_dash, dash, 0.0);
		gnome_print_stroke  (pj->print_context);
		gnome_print_grestore (pj->print_context);
	}
#endif
	/* Clip the header or footer */
	gnome_print_clip      (pj->print_context);

	print_hf_element (pj, hf->left_format,   LEFT_HEADER, y);
	print_hf_element (pj, hf->middle_format, MIDDLE_HEADER, y);
	print_hf_element (pj, hf->right_format,  RIGHT_HEADER, y);
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
print_headers (PrintJobInfo *pj)
{
	PrintMargins *pm = &pj->pi->margins;
	double top, bottom, y, ascender;

	ascender = gnome_font_get_ascender (pj->decoration_font);
	y = pj->height - pm->header.points - ascender;
	top    =  1 + pj->height - MIN (pm->header.points, pm->top.points);
	bottom = -1 + pj->height - MAX (pm->header.points, pm->top.points);

	print_hf_line (pj, pj->pi->header, y,
		       -1 + pm->left.points, bottom,
		       pj->width - pm->right.points, top);
}

/*
 * print_footers
 * @pj: printing context
 * Print footers
 *
 * Align descenders flush with inside of bottom margin.
 */
static void
print_footers (PrintJobInfo *pj)
{
	PrintMargins *pm = &pj->pi->margins;
	double top, bottom, y;

	y = pm->footer.points
		+ gnome_font_get_descender (pj->decoration_font);
	top    =  1 + MAX (pm->footer.points, pm->bottom.points);
	bottom = -1 + MIN (pm->footer.points, pm->bottom.points);

	/* Clip path for the header
	 * NOTE : postscript clip paths exclude the border, gdk includes it.
	 */
	print_hf_line (pj, pj->pi->footer, y,
		       -1 + pm->left.points, bottom,
		       pj->width - pm->right.points, top);
}

static void
setup_rotation (PrintJobInfo *pj)
{
	double affine [6];

	if (pj->pi->orientation == PRINT_ORIENT_VERTICAL)
		return;

	art_affine_rotate (affine, 90.0);
	gnome_print_concat (pj->print_context, affine);

	art_affine_translate (affine, 0, -pj->height);
	gnome_print_concat (pj->print_context, affine);
}

static Value *
cb_range_empty (Sheet *sheet, int col, int row, Cell *cell, gpointer flags)
{
	ColRowInfo const *cri = sheet_col_get_info (sheet, col);
	if (!cri->visible)
		return NULL;
	cri = sheet_row_get_info (sheet, row);
	if (!cri->visible)
		return NULL;
	return value_terminate ();
}

/**
 * print_page:
 * @sheet:     the sheet to print
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 * @pj:        printing context
 *
 * Return value: always 0
 *
 * Excel prints repeated rows like this: Pages up to and including the page
 * where the first of the repeated rows "naturally" occurs are printed in
 * the normal way. On subsequent pages, repated rows are printed before the
 * regular flow.
 */
static int
print_page (Sheet const *sheet, int start_col, int start_row, int end_col, int end_row,
	    PrintJobInfo *pj, gboolean output)
{
	PrintMargins *margins = &pj->pi->margins;
	/* print_height/width are sizes of the regular grid,
	 * not including repeating rows and columns */
	double print_height, print_width;
	double repeat_cols_used_x = 0., repeat_rows_used_y = 0.;
	double base_x, base_y;
	gboolean printed;
	int i;

	/* FIXME: Can col / row space calculation be factored out? */

	/* Space for repeated rows depends on whether we print them or not */
	if (pj->pi->repeat_top.use &&
	    start_row > pj->pi->repeat_top.range.start.row) {
		repeat_rows_used_y = pj->repeat_rows_used_y;
		/* Make sure start_row never is inside the repeated range */
		start_row = MAX (start_row,
				 pj->pi->repeat_top.range.end.row + 1);
	} else
		repeat_rows_used_y = 0;

	/* Space for repeated cols depends on whether we print them or not */
	if (pj->pi->repeat_left.use &&
	    start_col > pj->pi->repeat_left.range.start.col) {
		repeat_cols_used_x = pj->repeat_cols_used_x;
		/* Make sure start_col never is inside the repeated range */
		start_col = MAX (start_col,
				 pj->pi->repeat_left.range.end.col + 1);
	} else
		repeat_cols_used_x = 0;

	/* If there are no cells in the area check for spans */
	printed = (NULL != sheet_foreach_cell_in_range ((Sheet *)sheet, TRUE,
							start_col, start_row,
							end_col, end_row,
							cb_range_empty, NULL));
	if (!printed) {
		int i = start_row;
		for (; i <= end_row ; ++i) {
			ColRowInfo const *ri = sheet_row_get_info (sheet, i);
			if (ri->visible &&
			    (NULL != row_span_get (ri, start_col) ||
			     NULL != row_span_get (ri, end_col))) {
				printed = TRUE;
				break;
			}
		}
	}
	if (!printed && pj->pi->print_even_if_only_styles) {
		Range r;
		printed = sheet_style_has_visible_content (sheet,
			range_init (&r, start_col, start_row,
				    end_col, end_row));
	}

	if (!output)
		return printed;

	if (!printed)
		return 0;

	base_x = 0;
	base_y = 0;

	print_height = sheet_row_get_distance_pts (sheet,
						   start_row, end_row + 1);
	if (pj->pi->center_vertically){
		double h = print_height;

		if (pj->pi->print_titles)
			h += sheet->rows.default_style.size_pts;
		h += repeat_rows_used_y;
		base_y = (pj->y_points - h)/2;
	}

	print_width = sheet_col_get_distance_pts (sheet, start_col, end_col+1);
	if (pj->pi->center_horizontally){
		double w = print_width;

		if (pj->pi->print_titles)
			w += sheet->cols.default_style.size_pts;
		w += repeat_cols_used_x;
		base_x = (pj->x_points - w)/2;
	}

	/* Margins */
	base_x += margins->left.points;
	base_y += MAX (margins->top.points, margins->header.points);
	if (pj->pi->print_grid_lines) {
		/* the initial grid lines */
		base_x += 1.;
		base_y += 1.;
	} else {
		/* If there are no grids ignore the leading cell margins */
		base_x -= sheet->cols.default_style.margin_a;
		base_y -= sheet->rows.default_style.margin_a;
	}

	for (i = 0; i < pj->n_copies; i++){
		double x = base_x;
		double y = base_y;
		double clip_y;
		char *pagenotxt;

		/* Note: we cannot have spaces in page numbers.  */
		pagenotxt = hf_format_render (_("&[PAGE]"),
					      pj->render_info, HF_RENDER_PRINT);
		if (!pagenotxt)
			pagenotxt = g_strdup_printf ("%d", pj->render_info->page);
		gnome_print_beginpage (pj->print_context, pagenotxt);
		g_free (pagenotxt);

		setup_rotation (pj);
		print_headers (pj);
		print_footers (pj);

		/*
		 * Print any titles that might be used
		 */
		if (pj->pi->print_titles){
			print_titles (
				sheet, start_col, start_row, end_col, end_row,
				x, y, pj);
			x += sheet->cols.default_style.size_pts;
			y += sheet->rows.default_style.size_pts;
		}

		/* Clip the page */
		/* Gnome-print coordinates are lower left based,
		 * like Postscript */
		clip_y = 1 + pj->height - y;
		print_make_rectangle_path (
			pj->print_context,
			x - 1, clip_y,
			x + pj->x_points + 1, clip_y - pj->y_points - 2);
#ifndef NO_DEBUG_PRINT
		if (print_debugging > 0) {
			gnome_print_gsave (pj->print_context);
			gnome_print_stroke  (pj->print_context);
			gnome_print_grestore (pj->print_context);
		}
#endif
		gnome_print_clip      (pj->print_context);

		/* Start a new path because the background fill function does not */
		gnome_print_newpath  (pj->print_context);

		/*
		 * Print the repeated rows and columns
		 */
		if (pj->pi->repeat_top.use && repeat_rows_used_y > 0. ){
			if (pj->pi->repeat_left.use &&
			    repeat_cols_used_x > 0. ){
				/* Intersection of repeated rows and columns */
				print_page_repeated_intersect (
					sheet, x, y, repeat_cols_used_x,
					repeat_rows_used_y, pj);
			}
			print_page_repeated_rows (sheet,
				start_col, end_col,
				x + repeat_cols_used_x,	y,
				pj);
			y += repeat_rows_used_y;
		}

		if (pj->pi->repeat_left.use && repeat_cols_used_x > 0. ) {
			print_page_repeated_cols (sheet,
				start_row, end_row, x, y, pj);

			x += repeat_cols_used_x;
		}

		/*
		 * Print the body of the data
		 */
		print_page_cells (sheet,
			start_col, start_row, end_col, end_row, x, y, pj);

#ifdef ENABLE_BONOBO
		/*
		 * Print objects
		 */
		{
			GList *l;

			for (l = pj->sheet_objects; l; l = l->next)
				print_page_object (sheet, l->data,
						   start_col, start_row,
						   x, y, pj);
		}
#endif

		/*
		 * Output page
		 */
		gnome_print_showpage (pj->print_context);
	}

	return 1;
}

/*
 * Computes number of rows or columns that fit in @usable starting
 * at index @start and limited to index @end
 */
static int
compute_group (Sheet const *sheet, int start, int end, int usable,
	       ColRowInfo *(get_info)(Sheet const *sheet, int const p))
{
	float size_pts = 1.; /* The initial grid line */
	int idx, count = 0;

	for (idx = start; idx < end; idx++, count++) {
		ColRowInfo const *info;

		info = (*get_info) (sheet, idx);

		/* Hidden ColRows are ignored */
		if (info->visible) {
			size_pts += info->size_pts;

			if (size_pts > usable){
				if (count == 0)
					return 1;
				else
					return count;
			}
		}
	}

	return count;
}

#ifdef ENABLE_BONOBO
/*
 * Render Sheet objects.
 *
 * FIXME : JEG Sep/10/00
 * Michael : Why is this bonobo specific ?
 * Shouldn't the printing be part of sheet object with special
 * handlers for bonobo objects ?
 *
 * FIXME: JKH Sep/11/00
 * Hmm. This appears to fail when sheet is split over several pages.
 */
static void
render_sheet_objects (Sheet const *sheet, PrintJobInfo *pj)
{
	GList *l;

	pj->sheet_objects = NULL;
	for (l = sheet->sheet_objects; l; l = l->next) {
		SheetObjectPrintInfo *pi;
		double coords [4];

		pi = g_new0 (SheetObjectPrintInfo, 1);

		pi->so = l->data;
		pi->scale_x = pi->scale_y = 1.;

		sheet_object_position_pts (pi->so, coords);
		pi->x_pos_pts = coords [0];
		pi->y_pos_pts = coords [1];

		pi->pd = bonobo_print_data_new ((coords [2] - coords [0]),
						(coords [3] - coords [1]));
		sheet_object_print (pi->so, pi);

		pj->sheet_objects = g_list_prepend (pj->sheet_objects, pi);
	}
}
#endif

#define COL_FIT(col) (col >= SHEET_MAX_COLS ? (SHEET_MAX_COLS-1) : col)
#define ROW_FIT(row) (row >= SHEET_MAX_ROWS ? (SHEET_MAX_ROWS-1) : row)

static int
print_range_down_then_right (Sheet const *sheet, Range r, PrintJobInfo *pj,
			     gboolean output)
{
	int usable_x, usable_x_initial, usable_x_repeating;
	int usable_y, usable_y_initial, usable_y_repeating;
	int pages = 0;
	int col = r.start.col;
	gboolean printed;

	usable_x_initial   = pj->x_points - pj->titles_used_x;
	usable_x_repeating = usable_x_initial - pj->repeat_cols_used_x;
	usable_y_initial   = pj->y_points - pj->titles_used_y;
	usable_y_repeating = usable_y_initial - pj->repeat_rows_used_y;


	while (col < r.end.col) {
		int col_count;
		int row = r.start.row;

		if (col < pj->pi->repeat_left.range.end.col) {
			usable_x = usable_x_initial;
			col = MIN (col,
				   pj->pi->repeat_left.range.end.col);
		} else
			usable_x = usable_x_repeating;

		col_count = compute_group (sheet, col, r.end.col,
					   usable_x, sheet_col_get_info);

		while (row < r.end.row) {
			int row_count;

			if (row < pj->pi->repeat_top.range.end.row) {
				usable_y = usable_y_initial;
				row = MIN (row,
					   pj->pi->repeat_top.range.end.row);
			} else
				usable_y = usable_y_repeating;

			row_count = compute_group (sheet, row, r.end.row,
						       usable_y,
						       sheet_row_get_info);
			printed = print_page (
				sheet,
				COL_FIT (col), ROW_FIT (row),
				COL_FIT (col + col_count - 1),
				ROW_FIT (row + row_count - 1),
				pj, output);

			row += row_count;

			/* Only update page count when actually printing */
			if (printed && output)
				pj->render_info->page++;

			pages += printed ? 1 : 0;
		}
		col += col_count;
	}

	return pages;
}

static int
print_range_right_then_down (Sheet const *sheet, Range r, PrintJobInfo *pj,
			     gboolean output)
{
	int usable_x, usable_x_initial, usable_x_repeating;
	int usable_y, usable_y_initial, usable_y_repeating;
	int pages = 0;
	int row = r.start.row;
	gboolean printed;

	usable_x_initial   = pj->x_points - pj->titles_used_x;
	usable_x_repeating = usable_x_initial - pj->repeat_cols_used_x;
	usable_y_initial   = pj->y_points - pj->titles_used_y;
	usable_y_repeating = usable_y_initial - pj->repeat_rows_used_y;

	while (row < r.end.row) {
		int row_count;
		int col = r.start.col;

		if (row < pj->pi->repeat_top.range.end.row) {
			usable_y = usable_y_initial;
			row = MIN (row,
				   pj->pi->repeat_top.range.end.row);
		} else
			usable_y = usable_y_repeating;

		row_count = compute_group (sheet, row, r.end.row,
					   usable_y, sheet_row_get_info);

		while (col < r.end.col) {
			int col_count;

			if (col < pj->pi->repeat_left.range.end.col) {
				usable_x = usable_x_initial;
				col = MIN (col,
					   pj->pi->repeat_left.range.end.col);
			} else
				usable_x = usable_x_repeating;

			col_count = compute_group (sheet, col, r.end.col,
						   usable_x,
						   sheet_col_get_info);

			printed = print_page (
				sheet,
				COL_FIT (col), ROW_FIT (row),
				COL_FIT (col + col_count - 1),
				ROW_FIT (row + row_count - 1),
				pj, output);

			col += col_count;

			/* Only update page count when actually printing */
			if (printed && output)
				pj->render_info->page++;

			pages += printed ? 1 : 0;
		}
		row += row_count;
	}

	return pages;
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
print_sheet_range (Sheet const *sheet, Range r, PrintJobInfo *pj, gboolean output)
{
	int pages;

#ifdef ENABLE_BONOBO
	render_sheet_objects (sheet, pj);
#endif

 	if (pj->pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT)
		pages = print_range_down_then_right (sheet, r, pj, output);
	else
		pages = print_range_right_then_down (sheet, r, pj, output);

#ifdef ENABLE_BONOBO
	{
	GList  *l;
	for (l = pj->sheet_objects; l; l = l->next) {
		SheetObjectPrintInfo *pi = l->data;

		bonobo_print_data_free (pi->pd);
		pi->pd = NULL;
		g_free (pi);
	}
	g_list_free (pj->sheet_objects);
	pj->sheet_objects = NULL;
	}
#endif

	return pages;
}

static double
print_range_used_units (Sheet const *sheet, gboolean compute_rows,
			PrintRepeatRange const *range)
{
	Range const *r = &range->range;
	if (compute_rows)
		return sheet_row_get_distance_pts
			(sheet, r->start.row, r->end.row+1);
	else
		return sheet_col_get_distance_pts
			(sheet, r->start.col, r->end.col+1);
}

static void
print_job_info_init_sheet (Sheet const *sheet, PrintJobInfo *pj)
{
	/*
	 * This should be set in print_job_info_get, but we need
	 * to get access to one of the sheets
	 */
	if (pj->pi->print_titles) {
		pj->titles_used_x = sheet->cols.default_style.size_pts;
		pj->titles_used_y = sheet->rows.default_style.size_pts;
	} else {
		pj->titles_used_x = 0;
		pj->titles_used_y = 0;
	}

	pj->repeat_rows_used_y = (pj->pi->repeat_top.use)
	    ? print_range_used_units (sheet, TRUE, &pj->pi->repeat_top)
	    : 0.;
	pj->repeat_cols_used_x = (pj->pi->repeat_left.use)
	    ? print_range_used_units (sheet, FALSE, &pj->pi->repeat_left)
	    : 0.;

	pj->render_info->sheet = sheet;
}

/*
 * code to count the number of pages that will be printed.
 * Unfortuantely a lot of data here is caclualted again when you
 * actually print the page ...
 */

typedef struct _PageCountInfo {
	int pages;
	PrintJobInfo *pj;
	Range r;
	int current_output_sheet;
} PageCountInfo;

static void
compute_sheet_pages (gpointer value, gpointer user_data)
{
	PageCountInfo *pc = user_data;
	PrintJobInfo *pj = pc->pj;
	Sheet const *sheet = value;
	Range r;

	/* only count pages we are printing */
	if (pj->range == PRINT_SHEET_RANGE) {
		pc->current_output_sheet++;
		if (!(pc->current_output_sheet-1 >= pj->start_page
		      && pc->current_output_sheet-1 <= pj->end_page))
			return;
	}

	print_job_info_init_sheet (sheet, pj);
	if (pj->range != PRINT_SHEET_SELECTION) {
		r = sheet_get_extent (sheet);
		if (pj->pi->print_even_if_only_styles)
			sheet_style_get_extent (sheet, &r);

		/* FIXME : print_range should be inclusive */
		r.end.col++;
		r.end.row++;
	} else
		r = pc->r;

	/* Find out how many pages actually contain data */
	pc->pages = print_sheet_range (sheet, r, pj, FALSE);
}

/*
 * Computes the number of pages that will be output by a specific
 * print request.
 */
static int
compute_pages (Workbook *wb, Sheet *sheet, Range *r, PrintJobInfo *pj)
{
	PageCountInfo *pc = g_new0(PageCountInfo, 1);
	int pages;

	pc->pj = pj;
	if (r)
		pc->r = *r;
	if (wb != NULL) {
		GList *sheets = workbook_sheets (wb);
		g_list_foreach (sheets, compute_sheet_pages, pc);
		g_list_free (sheets);
	} else {
		compute_sheet_pages (sheet, pc);
	}
	pages = pc->pages;
	g_free (pc);
	return pages;
}

static void
print_sheet (gpointer value, gpointer user_data)
{
	PrintJobInfo *pj    = user_data;
	Range         extent;
	Sheet        *sheet = value;

	g_return_if_fail (pj != NULL);
	g_return_if_fail (sheet != NULL);

	/* if printing a range, check we are in range, otherwise iterate */
	if (pj->range == PRINT_SHEET_RANGE) {
		pj->current_output_sheet++;
		if (!(pj->current_output_sheet-1 >= pj->start_page
		      && pj->current_output_sheet-1 <= pj->end_page))
			return;
	}

	print_job_info_init_sheet (sheet, pj);
	extent = sheet_get_extent (sheet);
	if (pj->pi->print_even_if_only_styles)
		sheet_style_get_extent (sheet, &extent);

	/* FIXME : print_range should be inclusive */
	extent.end.col++;
	extent.end.row++;

	print_sheet_range (sheet, extent, pj, TRUE);
}

/* should this print a selection over any range of pages? */
static void
sheet_print_selection (WorkbookControlGUI *wbcg, Sheet *sheet, PrintJobInfo *pj)
{
	Range const *sel;
	Range extent;

	if (!(sel = selection_first_range (sheet, WORKBOOK_CONTROL (wbcg), _("Print Region"))))
		return;

	extent = *sel;
	extent.end.col++;
	extent.end.row++;

	pj->render_info->pages = compute_pages (sheet->workbook, NULL, &extent, pj);

	print_job_info_init_sheet (sheet, pj);
	print_sheet_range (sheet, extent, pj, TRUE);
}

static void
workbook_print_all (Workbook *wb, PrintJobInfo *pj)
{
	GList *sheets;

	g_return_if_fail (wb != NULL);

	pj->render_info->pages = compute_pages (wb, NULL, NULL, pj);

	sheets = workbook_sheets (wb);
	g_list_foreach (sheets , print_sheet, pj);
	g_list_free (sheets);
}

static PrintJobInfo *
print_job_info_get (Sheet *sheet, PrintRange range, gboolean const preview)
{
	PrintJobInfo *pj;
	PrintMargins *pm = &sheet->print_info->margins;
	int width, height;

	pj = g_new0 (PrintJobInfo, 1);

	/*
	 * Handy pointers
	 */
	pj->sheet = sheet;
	pj->pi    = sheet->print_info;

	/*
	 * Values that should be entered in a dialog box
	 */
	pj->start_page = 0;
	pj->end_page = workbook_sheet_count (sheet->workbook)-1;
	pj->range = range;
	pj->sorted_print = TRUE;
	pj->is_preview = preview;
	pj->n_copies = 1;
	pj->current_output_sheet = 0;

	/* Precompute information */
	width  = gnome_paper_pswidth  (pj->pi->paper);
	height = gnome_paper_psheight (pj->pi->paper);

	if (pj->pi->orientation == PRINT_ORIENT_HORIZONTAL) {
		pj->width = height;
		pj->height = width;
	} else {
		pj->width = width;
		pj->height = height;
	}

	pj->x_points = pj->width - (pm->left.points + pm->right.points);
	pj->y_points = pj->height -
		(MAX (pm->top.points, pm->header.points) +
		 MAX (pm->bottom.points, pm->footer.points));

	/*
	 * Setup render info
	 */
	pj->render_info = hf_render_info_new ();
	pj->render_info->sheet = sheet;
	pj->render_info->page = 1;

	pj->sheet_objects = NULL;

	pj->decoration_font = gnome_font_new ("Helvetica", 12);

	return pj;
}

static void
print_job_info_destroy (PrintJobInfo *pj)
{
	hf_render_info_destroy (pj->render_info);
	gtk_object_unref (GTK_OBJECT (pj->decoration_font));

	if (pj->sheet_objects)
		g_warning ("Leaking sheet object print data");

	g_free (pj);
}

void
sheet_print (WorkbookControlGUI *wbcg, Sheet *sheet,
	     gboolean preview, PrintRange default_range)
{
	GnomePrinter *printer = NULL;
	PrintJobInfo *pj;
	GnomePrintDialog *gpd;
	GnomePrintMaster *gpm;
	GnomePrintMasterPreview *pmp;
	int copies = 1;
	int collate = FALSE;
 	int first = 1;
	int end;
	int range;

  	g_return_if_fail (IS_SHEET (sheet));

	end  = workbook_sheet_count (sheet->workbook);

  	if (!preview) {
		gpd = (GnomePrintDialog *)gnome_print_dialog_new (
			_("Print Sheets"),
			GNOME_PRINT_DIALOG_RANGE|GNOME_PRINT_DIALOG_COPIES);

		g_return_if_fail (gpd != NULL);

		gnome_print_dialog_construct_range_page (
			gpd,
			GNOME_PRINT_RANGE_CURRENT|GNOME_PRINT_RANGE_ALL|
			GNOME_PRINT_RANGE_SELECTION|GNOME_PRINT_RANGE_RANGE,
			1, workbook_sheet_count(sheet->workbook),
			_("Act_ive sheet"), _("S_heets"));
		gnome_dialog_set_default (GNOME_DIALOG (gpd),
					  GNOME_PRINT_PRINT);
		switch (gnumeric_dialog_run (wbcg, GNOME_DIALOG(gpd))) {
		case GNOME_PRINT_PRINT:
			break;
		case GNOME_PRINT_PREVIEW:
			preview = TRUE;
			break;
		case -1:
  			return;
		default:
			gnome_dialog_close (GNOME_DIALOG (gpd));
			return;
		}
		gnome_print_dialog_get_copies (gpd, &copies, &collate);
		printer = gnome_print_dialog_get_printer (gpd);
		range = gnome_print_dialog_get_range_page (gpd, &first, &end);
		gnome_dialog_close (GNOME_DIALOG (gpd));

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
  	}
	pj = print_job_info_get (sheet, default_range, preview);
	pj->n_copies = 1;
	pj->sorted_print = FALSE;
	if (default_range == PRINT_SHEET_RANGE) {
		pj->start_page = first-1;
		pj->end_page = end-1;
	}

	gpm = gnome_print_master_new ();
	gnome_print_master_set_paper (gpm, pj->pi->paper);
	if (printer)
		gnome_print_master_set_printer(gpm, printer);
	gnome_print_master_set_copies(gpm, copies, collate);
	pj->print_context = gnome_print_master_get_context(gpm);

	/* perform actual printing */
	switch (pj->range) {

	case PRINT_ACTIVE_SHEET:
		pj->render_info->pages = compute_pages (NULL, sheet, NULL, pj);
		print_sheet (sheet, pj);
		break;

	case PRINT_ALL_SHEETS:
	case PRINT_SHEET_RANGE:
		workbook_print_all (sheet->workbook, pj);
		break;

	case PRINT_SHEET_SELECTION:
		sheet_print_selection (wbcg, sheet, pj);
		break;

	default:
		g_error ("mis-enumerated print type");
		break;
  	}

	gnome_print_master_close (gpm);

	if (preview) {
		gboolean landscape = pj->pi->orientation == PRINT_ORIENT_HORIZONTAL;
		pmp = gnome_print_master_preview_new_with_orientation
			(gpm, _("Print preview"), landscape);
		gtk_widget_show (GTK_WIDGET (pmp));
	} else {
		int result = gnome_print_master_print (gpm);
		if (result == -1) {
			/*
			 * FIXME: not a great message, but at this point we don't
			 * know *what* went wrong.
			 */
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("Printing failed"));
		}
	}
	gtk_object_unref (GTK_OBJECT (gpm));
  	print_job_info_destroy (pj);
}

