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
#include "selection.h"
#include "workbook.h"
#include "dialogs.h"
#include "main.h"
#include "print-info.h"
#include "print.h"
#include "print-cell.h"
#include "application.h"

/* If TRUE, we print empty pages */
int print_empty_pages = FALSE;

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
print_titles (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
	      double base_x, double base_y, PrintJobInfo *pj)
{
}

static void
print_page_repeated_rows (Sheet *sheet,
			  int start_col, int start_row, int end_col, int end_row,
			  double base_x, double base_y,
			  double print_width, double print_height,
			  PrintJobInfo *pj)
{
	Value *value = &pj->pi->repeat_top.range;
	CellRef *cell_a = &value->v_range.cell.a;
	CellRef *cell_b = &value->v_range.cell.b;

	base_y = pj->height - base_y;

	if (pj->pi->print_line_divisions){
		print_cell_grid (
			pj->print_context,
			sheet,
			start_col, cell_a->row,
			end_col,   cell_b->row,
			base_x, base_y,
			print_width, print_height);
	}
	print_cell_range (
		pj->print_context, sheet,
		start_col, cell_a->row,
		end_col,   cell_b->row,
		base_x, base_y, TRUE);
}

static void
print_page_repeated_cols (Sheet *sheet,
			  int start_col, int start_row, int end_col, int end_row,
			  double base_x, double base_y,
			  double print_width, double print_height,
			  PrintJobInfo *pj)
{
	Value *value = &pj->pi->repeat_top.range;
	CellRef *cell_a = &value->v_range.cell.a;
	CellRef *cell_b = &value->v_range.cell.b;

	base_y = pj->height - base_y;

	if (pj->pi->print_line_divisions){
		print_cell_grid (
			pj->print_context,
			sheet,
			cell_a->col, start_row,
			cell_b->col, end_row,
			base_x, base_y,
			print_width, print_height);
	}
	print_cell_range (
		pj->print_context, sheet,
		cell_a->col, start_row,
		cell_b->col, end_row,
		base_x, base_y, TRUE);
}

static void
print_page_cells (Sheet *sheet,
		  int start_col, int start_row, int end_col, int end_row,
		  double base_x, double base_y, double print_width, double print_height,
		  PrintJobInfo *pj)
{
	base_y = pj->height - base_y;

	if (pj->pi->print_line_divisions){
		print_cell_grid (
			pj->print_context,
			sheet, start_col, start_row,
			end_col, end_row,
			base_x, base_y,
			print_width, print_height);
	}
	
	print_cell_range (
		pj->print_context, sheet,
		start_col, start_row,
		end_col, end_row,
		base_x, base_y, TRUE);
}

#ifdef ENABLE_BONOBO

static void
print_page_object (Sheet *sheet, SheetObjectPrintInfo *pi,
		   int start_col, int start_row, int end_col, int end_row,
		   double base_x, double base_y, double print_width, double print_height,
		   PrintJobInfo *pj)
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

static void
print_hf (PrintJobInfo *pj, const char *format, HFSide side, double y)
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
	
	len = gnome_font_get_width_string (pj->decoration_font, text);
	
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
	gnome_print_show (pj->print_context, text);
	g_free (text);
}

static void
print_headers (PrintJobInfo *pj)
{
	PrintMargins *pm = &pj->pi->margins;
	double y;
	
	gnome_print_setfont (pj->print_context, pj->decoration_font);
	gnome_print_setrgbcolor (pj->print_context, 0, 0, 0);
	
	y = pj->height - pm->header.points - pj->decoration_font->size;
	print_hf (pj, pj->pi->header->left_format,   LEFT_HEADER, y);
	print_hf (pj, pj->pi->header->middle_format, MIDDLE_HEADER, y);
	print_hf (pj, pj->pi->header->right_format,  RIGHT_HEADER, y);
}

static void
print_footers (PrintJobInfo *pj)
{
	PrintMargins *pm = &pj->pi->margins;
	double y;
	
	gnome_print_setfont (pj->print_context, pj->decoration_font);

	y = pm->footer.points;
	print_hf (pj, pj->pi->footer->left_format,   LEFT_HEADER, y);
	print_hf (pj, pj->pi->footer->middle_format, MIDDLE_HEADER, y);
	print_hf (pj, pj->pi->footer->right_format,  RIGHT_HEADER, y);
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
 */
static int
print_page (Sheet *sheet, int start_col, int start_row, int end_col, int end_row,
	    PrintJobInfo *pj, gboolean output)
{
	PrintMargins *margins = &pj->pi->margins;
	double print_height, print_width;
	double base_x, base_y;
	int i;

	if (output == FALSE || (print_empty_pages == FALSE)){
		gboolean printed;
		
		printed = print_cell_range (
			pj->print_context, sheet,
			start_col, start_row,
			end_col, end_row, 0, 0, FALSE);

		if (!output)
			return printed;
		
		if (!printed)
			return 0;
	}
	
	base_x = 0;
	base_y = 0;

	print_height = sheet_row_get_distance_pts (sheet, start_row, end_row+1);

	if (pj->pi->center_vertically){
		if (pj->pi->print_titles)
			print_height += sheet->rows.default_style.size_pts;
		print_height += pj->repeat_rows_used_y;
		base_y = (pj->y_points - print_height)/2;
	}

	print_width = sheet_col_get_distance_pts (sheet, start_col, end_col+1);
	if (pj->pi->center_horizontally){
		if (pj->pi->print_titles)
			print_width += sheet->cols.default_style.size_pts;
		print_width += pj->repeat_cols_used_x;
		base_x = (pj->x_points - print_width)/2;
	}

	/* Margins */
	base_x += margins->left.points; 
	base_y += margins->top.points + margins->header.points;

	for (i = 0; i < pj->n_copies; i++){
		double x = base_x;
		double y = base_y;
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
		gnome_print_newpath   (pj->print_context);
		gnome_print_moveto    (pj->print_context,                x, y);
		gnome_print_lineto    (pj->print_context, pj->x_points + x, y);
		gnome_print_lineto    (pj->print_context, pj->x_points + x, y + pj->y_points);
		gnome_print_lineto    (pj->print_context,                x, y + pj->y_points);
		gnome_print_closepath (pj->print_context);
		gnome_print_clip      (pj->print_context);

		/* Start a new patch because the background fill function does not */
		gnome_print_newpath  (pj->print_context);

		/*
		 * Print the repeated rows and columns
		 */
		if (pj->pi->repeat_left.use){
			print_page_repeated_rows (
				sheet, start_col, start_row, end_col, end_row,
				x + pj->repeat_cols_used_x, y, print_width, print_height, pj);
			y += pj->repeat_rows_used_y;
		}

		if (pj->pi->repeat_top.use){
			print_page_repeated_cols (
				sheet, start_col, start_row, end_col, end_row,
				x, y, print_width, print_height, pj);
			
			x += pj->repeat_cols_used_x;
		}
		
		/*
		 * Print the body of the data
		 */
		print_page_cells (
			sheet, start_col, start_row, end_col, end_row,
			x, y, print_width, print_height, pj);

#ifdef ENABLE_BONOBO
		/*
		 * Print objects
		 */
		{
			GList *l;

			for (l = pj->sheet_objects; l; l = l->next)
				print_page_object (sheet, l->data, start_col, start_row, end_col, end_row,
						   x, y, print_width, print_height, pj);
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
 * Computes a list of indexes of ColRowInfos that fit in @usable starting
 * at index @start and finishing at index @end
 *
 * The list contains the number of items per group.
 */
static GList *
compute_groups (Sheet *sheet, int start, int end, int usable,
		ColRowInfo *(get_info)(Sheet const *sheet, int const p))
{
	GList *result = NULL;
	float size_pts = 1.; /* The initial grid line */
	int idx, count = 0;

	for (idx = start; idx < end; ){
		ColRowInfo *info;
		
		info = (*get_info) (sheet, idx);

		/* Hidden ColRows are ignored */
		if (info->visible) {
			size_pts += info->size_pts;

			if (size_pts > usable){
				if (count == 0){
					result = g_list_prepend (result, GINT_TO_POINTER (1));
					size_pts = 0;
					count = 0;
				} else {
					result = g_list_prepend (result, GINT_TO_POINTER (count));
					count = 0;
					size_pts = 0;
					continue;
				}
			} 
		}
		idx++;
		count++;
	}

	if (count)
		result = g_list_prepend (result, GINT_TO_POINTER (count));
			
	result = g_list_reverse (result);

	return result;
}

#define COL_FIT(col) (col >= SHEET_MAX_COLS ? (SHEET_MAX_COLS-1) : col)
#define ROW_FIT(row) (row >= SHEET_MAX_ROWS ? (SHEET_MAX_ROWS-1) : row)

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
print_sheet_range (Sheet *sheet, Range r, PrintJobInfo *pj, gboolean output)
{
	int usable_x, usable_y;
	GList *l, *m;
	GList *cols, *rows;
	int pages = 0;
	gboolean printed;

	usable_x = pj->x_points - pj->titles_used_x - pj->repeat_cols_used_x;
	usable_y = pj->y_points - pj->titles_used_y - pj->repeat_rows_used_y;

	cols = compute_groups (sheet, r.start.col, r.end.col, usable_x, sheet_col_get_info);
	rows = compute_groups (sheet, r.start.row, r.end.row, usable_y, sheet_row_get_info);

	/*
	 * Render Sheet objects.
	 */
#ifdef ENABLE_BONOBO
	{
		pj->sheet_objects = NULL;
		for (l = sheet->objects; l; l = l->next) {
			SheetObjectPrintInfo *pi;
			double tlx, tly, brx, bry;

			pi = g_new0 (SheetObjectPrintInfo, 1);

			pi->so = l->data;
			pi->scale_x = sheet->last_zoom_factor_used *
				application_display_dpi_get (TRUE) / 72.0;
			pi->scale_y = sheet->last_zoom_factor_used *
				application_display_dpi_get (FALSE) / 72.0;

			sheet_object_get_bounds (pi->so, &tlx, &tly, &brx, &bry);
			pi->x_pos_pts = tlx / pi->scale_x;
			pi->y_pos_pts = tly / pi->scale_y;

			pi->pd = bonobo_print_data_new ((brx - tlx) / pi->scale_x,
							(bry - tly) / pi->scale_y);
			sheet_object_print (pi->so, pi);

			pj->sheet_objects = g_list_prepend (pj->sheet_objects, pi);
		}
	}
#endif

	if (pj->pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT) {
		int col = r.start.col;

		for (l = cols; l; l = l->next) {
			int col_count = GPOINTER_TO_INT (l->data);
			int row = r.start.row;

			for (m = rows; m; m = m->next) {
				int row_count = GPOINTER_TO_INT (m->data);

				printed = print_page (
					sheet,
					COL_FIT (col), ROW_FIT (row),
					COL_FIT (col + col_count - 1),
					ROW_FIT (row + row_count - 1),
					pj, output);
				
				row += row_count;

				/* Only update page count if we are actually printing */
				if (printed && output)
					pj->render_info->page++;

				pages += printed ? 1 : 0;
			}
			col += col_count;
		}
	} else {
		int row = r.start.row;

		for (l = rows; l; l = l->next) {
			int row_count = GPOINTER_TO_INT (l->data);
			int col = r.start.col;
			
			for (m = cols; m; m = m->next) {
				int col_count = GPOINTER_TO_INT (m->data);

				printed = print_page (
					sheet,
					COL_FIT (col), ROW_FIT (row),
					COL_FIT (col + col_count - 1),
					ROW_FIT (row + row_count - 1),
					pj, output);

				col += col_count;

				/* Only update page count if we are actually printing */
				if (printed && output)
					pj->render_info->page++;

				pages += printed ? 1 : 0;
			}
			row += row_count;
		}
	}


#ifdef ENABLE_BONOBO
	for (l = pj->sheet_objects; l; l = l->next) {
		SheetObjectPrintInfo *pi = l->data;

		bonobo_print_data_free (pi->pd);
		pi->pd = NULL;
		g_free (pi);
	}
	g_list_free (pj->sheet_objects);
	pj->sheet_objects = NULL;
#endif

	g_list_free (cols);
	g_list_free (rows);

	return pages;
}

static double
print_range_used_units (Sheet *sheet, gboolean compute_rows, PrintRepeatRange *range)
{
	Value *cell_range = &range->range;
	CellRef *cell_a = &cell_range->v_range.cell.a;
	CellRef *cell_b = &cell_range->v_range.cell.b;

	if (compute_rows)
		return sheet_row_get_distance_pts
			(sheet, cell_a->row, cell_b->row+1);
	else
		return sheet_col_get_distance_pts
			(sheet, cell_a->col, cell_b->col+1);
}

static void
print_job_info_init_sheet (Sheet *sheet, PrintJobInfo *pj)
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
compute_sheet_pages (gpointer key, gpointer value, gpointer user_data)
{
	PageCountInfo *pc = user_data;
	PrintJobInfo *pj = pc->pj;
	Sheet *sheet = value;
	Range r;
	int usable_x, usable_y;
	GList *cols, *rows;

	/* only count pages we are printing */
	if (pj->range == PRINT_SHEET_RANGE) {
		pc->current_output_sheet++;
		if (!(pc->current_output_sheet-1 >= pj->start_page
		      && pc->current_output_sheet-1 <= pj->end_page))
			return;
	}

	print_job_info_init_sheet (sheet, pj);
	if (pj->range == PRINT_SHEET_SELECTION) {
		r = pc->r;
	} else {
		r = sheet_get_extent (sheet);
		r.end.col++;
		r.end.row++;
	}
	
	usable_x = pj->x_points - pj->titles_used_x - pj->repeat_cols_used_x;
	usable_y = pj->y_points - pj->titles_used_y - pj->repeat_rows_used_y;

	if (print_empty_pages){
		/*
		 * The total number of columns and rows that wants to be printed
		 */
		cols = compute_groups (sheet, r.start.col, r.end.col, usable_x, sheet_col_get_info);
		rows = compute_groups (sheet, r.start.row, r.end.row, usable_y, sheet_row_get_info);

		pc->pages = g_list_length (cols) * g_list_length (rows);
		
		g_list_free (cols);
		g_list_free (rows);
	} else {
		/*
		 * Find out how many pages actually contain data
		 */
		pc->pages = print_sheet_range (sheet, r, pj, FALSE);
	}		       
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
	if (wb!=NULL) {
		g_hash_table_foreach (wb->sheets, compute_sheet_pages, pc);
	} else {
		compute_sheet_pages (NULL, sheet, pc);
	}
	pages = pc->pages;
	g_free (pc);
	return pages;
}

static void
print_sheet (gpointer key, gpointer value, gpointer user_data)
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
	extent.end.col++;
	extent.end.row++;

	print_sheet_range (sheet, extent, pj, TRUE);
}

/* should this print a selection over any range of pages? */
static void
sheet_print_selection (Sheet *sheet, PrintJobInfo *pj)
{
	Range const *sel;
	Range extent;

	if ((sel = selection_first_range (sheet, FALSE)) == NULL) {
		gnumeric_notice (
			sheet->workbook, GNOME_MESSAGE_BOX_ERROR,
			_("Selection must be a single range"));
		return;
	}
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
	g_return_if_fail (wb != NULL);

	pj->render_info->pages = compute_pages (wb, NULL, NULL, pj);

	g_hash_table_foreach (wb->sheets, print_sheet, pj);
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
		(pm->top.points + pm->bottom.points + pm->header.points + pm->footer.points);

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
sheet_print (Sheet *sheet, gboolean preview, PrintRange default_range)
{
	GnomePrinter *printer = NULL;
	PrintJobInfo *pj;
	GnomePrintDialog *gpd;
	GnomePrintMaster *gpm;
	GnomePrintMasterPreview *pmp;
	int copies = 1;
	int collate = FALSE;
 	int first = 1;
	int end = workbook_sheet_count (sheet->workbook);
	int range;

  	g_return_if_fail (sheet != NULL);

  	if (!preview) {
		gpd = (GnomePrintDialog *)gnome_print_dialog_new (
			_("Print Sheets"),
			GNOME_PRINT_DIALOG_RANGE|GNOME_PRINT_DIALOG_COPIES);
		gnome_print_dialog_construct_range_page (
			gpd,
			GNOME_PRINT_RANGE_CURRENT|GNOME_PRINT_RANGE_ALL|
			GNOME_PRINT_RANGE_SELECTION|GNOME_PRINT_RANGE_RANGE,
			1, workbook_sheet_count(sheet->workbook),
			_("Act_ive sheet"), _("S_heets"));
		gnome_dialog_set_default (GNOME_DIALOG (gpd),
					  GNOME_PRINT_PRINT);
		switch (gnumeric_dialog_run (sheet->workbook,
					     GNOME_DIALOG(gpd))) {
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
		print_sheet (NULL, sheet, pj);
		break;
		
	case PRINT_ALL_SHEETS:
	case PRINT_SHEET_RANGE:
		workbook_print_all (sheet->workbook, pj);
		break;
				
	case PRINT_SHEET_SELECTION:
		sheet_print_selection (sheet, pj);
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
		gnome_print_master_print (gpm);
	}
	gtk_object_unref (GTK_OBJECT (gpm));
  	print_job_info_destroy (pj);
}

