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
#include <gdk/gdkkeysyms.h>
#include <libgnomeprint/gnome-printer.h>
#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-printer-dialog.h>

#include "gnumeric.h"
#include "eval.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-object.h"
#include "selection.h"
#include "dialogs.h"
#include "main.h"
#include "file.h"
#include "utils.h"
#include "print-info.h"
#include "print.h"
#include "print-cell.h"
#include "print-preview.h"
#include "dialog-printer.h"

#define MARGIN_X 1
#define MARGIN_Y 1

typedef struct {
	/*
	 * Part 1: The information the user configures on the Print Dialog
	 */
	PrintRange range;
	int start_page, end_page; /* Interval */
	int n_copies;
	gboolean sorted_print;

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
	 * Part 4: Print Preview
	 */
	PrintPreview *preview;
	
	/*
	 * For headers and footers
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
	CellRef *cell_a = &value->v.cell_range.cell_a;
	CellRef *cell_b = &value->v.cell_range.cell_b;

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
		base_x, base_y);
}

static void
print_page_repeated_cols (Sheet *sheet,
			  int start_col, int start_row, int end_col, int end_row,
			  double base_x, double base_y,
			  double print_width, double print_height,
			  PrintJobInfo *pj)
{
	Value *value = &pj->pi->repeat_top.range;
	CellRef *cell_a = &value->v.cell_range.cell_a;
	CellRef *cell_b = &value->v.cell_range.cell_b;

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
		base_x, base_y);
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
		base_x, base_y);
}

typedef enum {
	LEFT_HEADER,
	RIGHT_HEADER,
	MIDDLE_HEADER,
} HFSide;

static void
print_hf (PrintJobInfo *pj, const char *format, HFSide side, double y)
{
	PrintMargins *pm = &pj->pi->margins;
	char *text;
	double x;
	double len;
	
	text = hf_format_render (format, pj->render_info, HF_RENDER_PRINT);

	if (text [0] == 0){
		g_free (text);
		return;
	}
	
	len = gnome_font_get_width_string (pj->decoration_font, text);
	
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

	y = pj->height - pm->top.points - pj->decoration_font->size;
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

	y = pm->footer.points = pm->bottom.points;
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

static void
print_page (Sheet *sheet, int start_col, int start_row, int end_col, int end_row, PrintJobInfo *pj)
{
	PrintMargins *margins = &pj->pi->margins;
	double print_height, print_width;
	double base_x, base_y;
	int i;
	
	base_x = 0;
	base_y = 0;

	print_height = sheet_row_get_unit_distance (sheet, start_row, end_row+1);

	if (pj->pi->orientation == PRINT_ORIENT_HORIZONTAL)
		setup_rotation (pj);
	
	if (pj->pi->center_vertically){
		if (pj->pi->print_titles)
			print_height += sheet->rows.default_style.units;
		if (pj->pi->repeat_top.use)
			print_height += pj->repeat_rows_used_y;
		base_y = (pj->y_points - print_height)/2;
	}

	print_width = sheet_col_get_unit_distance (sheet, start_col, end_col+1);
	if (pj->pi->center_horizontally){
		if (pj->pi->print_titles)
			print_width += sheet->cols.default_style.units;
		if (pj->repeat_cols_used_x)
			print_width += pj->repeat_cols_used_x;
		base_x = (pj->x_points - print_width)/2;
	}

	/* Margins */
	base_x += margins->left.points; 
	base_y += margins->top.points + margins->header.points;

	for (i = 0; i < pj->n_copies; i++){
		double x = base_x;
		double y = base_y;

		print_headers (pj);
		print_footers (pj);
		
		/*
		 * Print any titles that might be used
		 */
		if (pj->pi->print_titles){
			print_titles (
				sheet, start_col, start_row, end_col, end_row,
				x, y, pj);
			x += sheet->cols.default_style.units;
			y += sheet->rows.default_style.units;
		}

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

		/*
		 * Output page
		 */
		gnome_print_showpage (pj->print_context);
	}
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
	GList *result;
	int units, count, idx;

	result = NULL;
	units = 0;
	count = 0;
	for (idx = start; idx < end; ){
		ColRowInfo *info;
		
		info = (*get_info) (sheet, idx);

		units += info->units + info->margin_a_pt + info->margin_b_pt;
		
		if (units > usable){
			if (count == 0){
				result = g_list_prepend (result, GINT_TO_POINTER (1));
				units = 0;
				count = 0;
			} else {
				result = g_list_prepend (result, GINT_TO_POINTER (count));
				count = 0;
				units = 0;
				continue;
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

static void
print_sheet_range (Sheet *sheet, int start_col, int start_row, int end_col, int end_row, PrintJobInfo *pj)
{
	int usable_x, usable_y;
	GList *l, *m;
	GList *cols, *rows;
	
	usable_x = pj->x_points - pj->titles_used_x - pj->repeat_cols_used_x;
	usable_y = pj->y_points - pj->titles_used_y - pj->repeat_rows_used_y;

	cols = compute_groups (sheet, start_col, end_col, usable_x, sheet_col_get_info);
	rows = compute_groups (sheet, start_row, end_row, usable_y, sheet_row_get_info);

	pj->render_info->pages = g_list_length (cols) * g_list_length (rows);
	
	if (pj->pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT){
		int col = start_col;

		pj->render_info->page = 1;
		for (l = cols; l; l = l->next){
			int col_count = GPOINTER_TO_INT (l->data);
			int row = start_row;

			for (m = rows; m; m = m->next){
				int row_count = GPOINTER_TO_INT (m->data);
				
				print_page (sheet, col, row, col + col_count - 1, row + row_count - 1, pj);

				row += row_count;
				pj->render_info->page++;
			}
			col += col_count;
		}
	} else {
		int row = start_row;

		pj->render_info->page = 1;
		for (l = rows; l; l = l->next){
			int row_count = GPOINTER_TO_INT (l->data);
			int col = start_col;
			
			for (m = cols; m; m = m->next){
				int col_count = GPOINTER_TO_INT (m->data);

				print_page (sheet, col, row, col + col_count - 1, row + row_count - 1, pj);

				col += col_count;
				pj->render_info->page++;
			}
			row += row_count;
		}
	}

	g_list_free (cols);
	g_list_free (rows);
}

static double
print_range_used_units (Sheet *sheet, gboolean compute_rows, PrintRepeatRange *range)
{
	Value *cell_range = &range->range;
	CellRef *cell_a = &cell_range->v.cell_range.cell_a;
	CellRef *cell_b = &cell_range->v.cell_range.cell_b;

	if (compute_rows)
		return sheet_row_get_unit_distance
			(sheet, cell_a->row, cell_b->row+1);
	else
		return sheet_col_get_unit_distance
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
		pj->titles_used_x = sheet->cols.default_style.units;
		pj->titles_used_y = sheet->rows.default_style.units;
	} else {
		pj->titles_used_x = 0;
		pj->titles_used_y = 0;
	}

	pj->repeat_rows_used_y = print_range_used_units (
		sheet, TRUE, &pj->pi->repeat_top);
	pj->repeat_cols_used_x = print_range_used_units (
		sheet, FALSE, &pj->pi->repeat_left);

	pj->render_info->sheet = sheet;
}

static void
print_sheet (gpointer key, gpointer value, gpointer user_data)
{
	PrintJobInfo *pj = user_data;
	Sheet *sheet = value;

	g_return_if_fail (pj != NULL);
	g_return_if_fail (sheet != NULL);

	print_job_info_init_sheet (sheet, pj);

	print_sheet_range (sheet, 0, 0, sheet->cols.max_used+1, sheet->rows.max_used+1, pj);
}

static void
sheet_print_selection (Sheet *sheet, PrintJobInfo *pj)
{
	Range const * sel;
	if ((sel = selection_first_range (sheet)) == NULL) {
		gnumeric_notice (
			sheet->workbook, GNOME_MESSAGE_BOX_ERROR,
			_("Selection must be a single range"));
		return;
	}

	print_job_info_init_sheet (sheet, pj);
	print_sheet_range (sheet,
			   sel->start.col, sel->start.row,
			   sel->end.col + 1, sel->end.row + 1, pj);
}

static void
workbook_print_all (Workbook *wb, PrintJobInfo *pj)
{
	g_return_if_fail (wb != NULL);

	g_hash_table_foreach (wb->sheets, print_sheet, pj);
}

static PrintJobInfo *
print_job_info_get (Sheet *sheet, PrintRange range)
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
	pj->end_page = -1;
	pj->range = range;
	pj->sorted_print = TRUE;
	pj->n_copies = 1;

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

	pj->decoration_font = gnome_font_new ("Helvetica", 12);
	
	return pj;
}

static void
print_job_info_destroy (PrintJobInfo *pj)
{
	hf_render_info_destroy (pj->render_info);
	gtk_object_unref (GTK_OBJECT (pj->decoration_font));
	
	g_free (pj);
}

void
sheet_print (Sheet *sheet, gboolean preview,
	     PrintRange default_range)
{
	GnomePrinter *printer = NULL;
	PrintJobInfo *pj;
	int loop, i;
	
	g_return_if_fail (sheet != NULL);
	
	if (!preview) {
		if (!(printer = gnumeric_printer_dialog_run 
		      (&default_range, sheet->workbook)))
			return;
	}
		
	pj = print_job_info_get (sheet, default_range);

	if (pj->sorted_print) {
		loop = pj->n_copies;
		pj->n_copies = 1;
	} else
		loop = 1;

	if (preview) {
		pj->n_copies = 1;
		loop = 1;
		pj->preview = print_preview_new (sheet);
		pj->print_context = print_preview_context (pj->preview);
	} else {
		pj->preview = NULL;
		pj->print_context = gnome_print_context_new_with_paper_size (
			printer, gnome_paper_name (pj->pi->paper));
	}

	for (i = 0; i < loop; i++) {
		switch (pj->range) {

		case PRINT_ACTIVE_SHEET:
			print_sheet (NULL, sheet, pj);
			break;

		case PRINT_ALL_SHEETS:
			workbook_print_all (sheet->workbook, pj);
			break;

		case PRINT_SHEET_SELECTION:
			sheet_print_selection (sheet, pj);
			break;
			
		default:
			g_error ("mis-enumerated print type");
			break;
		}
	}

	if (preview)
		print_preview_print_done (pj->preview);
	else {
		gnome_print_context_close_file (pj->print_context);
		gtk_object_unref (GTK_OBJECT (pj->print_context));
		gtk_object_unref (GTK_OBJECT (printer));
	}
	
	print_job_info_destroy (pj);
}
