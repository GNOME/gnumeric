/*
 * print.c: Printing routines for Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
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
#include "dialogs.h"
#include "main.h"
#include "file.h"
#include "utils.h"
#include "print-info.h"
#include "print.h"
#include "print-cell.h"

#define MARGIN_X 1
#define MARGIN_Y 1

typedef enum {
	PRINT_ALL,
	PRINT_SELECTION,
	PRINT_ACTIVE_SHEETS
} PrintRange;

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
	int width, height;	/* total dimensions */
	int x_points;		/* real usable X (ie, width - margins) */
	int y_points;		/* real usable Y (ie, height - margins) */
	int titles_used_x;	/* points used by the X titles */
	int titles_used_y;	/* points used by the Y titles */
	
	/*
	 * Part 3: Handy pointers
	 */
	Workbook *wb;
	PrintInformation *pi;
	GnomePrintContext *print_context;
} PrintJobInfo;

static void
print_titles (Sheet *sheet, int start_col, int start_row, int end_col, int end_row, PrintJobInfo *pj)
{
}

static void
print_page (Sheet *sheet, int start_col, int start_row, int end_col, int end_row, PrintJobInfo *pj)
{
	PrintMargins *margins = &pj->pi->margins;
	double print_height, print_width;
	int base_x, base_y;
	int size;
	int i;
	
	base_x = 0;
	base_y = 0;

	print_height = sheet_row_get_unit_distance (sheet, start_row, end_row);

	if (pj->pi->center_vertically){
		if (pj->pi->print_titles)
			print_height += sheet->default_row_style.units;
		base_y = (pj->y_points - print_height)/2;
	}

	print_width = sheet_col_get_unit_distance (sheet, start_col, end_col);
	if (pj->pi->center_horizontally){
		if (pj->pi->print_titles)
			print_width += sheet->default_col_style.units;
		base_x = (pj->x_points - print_width)/2;
	}

	if (pj->pi->print_titles){
		print_titles (sheet, start_col, start_row, end_col, end_row, pj);
		base_x += sheet->default_col_style.units;
		base_y += sheet->default_row_style.units;
	}

	/* Margins */
	base_x += margins->left.points; 
	base_y += margins->top.points + margins->header.points;
	
	base_y = pj->height - base_y;
	for (i = 0; i < pj->n_copies; i++){

		if (pj->pi->print_line_divisions){
			print_cell_grid (
				pj->print_context,
				sheet, start_col, start_row,
				end_col, end_row,
				base_x, base_y,
				print_width, print_height);
		}

		print_cell_range (pj->print_context, sheet,
				  start_col, start_row,
				  end_col, end_row,
				  base_x, base_y);

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
compute_groups (Sheet *sheet, int start, int end, int usable, ColRowInfo *(get_info)(Sheet *sheet, int p))
{
	GList *result;
	int units, count, idx;

	result = NULL;
	units = 0;
	count = 0;
	for (idx = start; idx < end; ){
		ColRowInfo *info;
		
		info = (*get_info) (sheet, idx);

		units += info->units;
		
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
	int width, height;
	int last_col, last_row;
	GList *l, *m;
	GList *cols, *rows;
	
	usable_x = pj->x_points - pj->titles_used_x;
	usable_y = pj->y_points - pj->titles_used_y;

	cols = compute_groups (sheet, start_col, end_col, usable_x, sheet_col_get_info);
	rows = compute_groups (sheet, start_row, end_row, usable_y, sheet_row_get_info);

	if (pj->pi->print_order == 0){
		int col = start_col;
		
		for (l = cols; l; l = l->next){
			int col_count = GPOINTER_TO_INT (l->data);
			int row = start_row;

			for (m = rows; m; m = m->next){
				int row_count = GPOINTER_TO_INT (m->data);
				
				print_page (sheet, col, row, col + col_count, row + row_count, pj);

				row += row_count;
			}
			col += col_count;
		}
	} else {
		int row = start_row;

		for (l = rows; l; l = l->next){
			int row_count = GPOINTER_TO_INT (l->data);
			int col = start_col;
			
			for (m = cols; m; m = m->next){
				int col_count = GPOINTER_TO_INT (m->data);

				print_page (sheet, col, row, col + col_count, row + row_count, pj);

				col += col_count;
			}
			row += row_count;
		}
	}

	g_list_free (cols);
	g_list_free (rows);
}

static void
workbook_print_selection (Workbook *wb, PrintJobInfo *pj)
{
	
}

static void
print_sheet (gpointer key, gpointer value, gpointer user_data)
{
	PrintJobInfo *pj = user_data;
	Sheet *sheet = value;

	/*
	 * This should be set in print_job_info_get, but we need
	 * to get access to one of the sheets
	 */
	if (pj->pi->print_titles){
		pj->titles_used_x = sheet->default_col_style.units;
		pj->titles_used_y = sheet->default_row_style.units;
	} else {
		pj->titles_used_x = 0;
		pj->titles_used_y = 0;
	}

	print_sheet_range (sheet, 0, 0, sheet->max_col_used+1, sheet->max_row_used+1, pj);
}

static void
workbook_print_all (Workbook *wb, PrintJobInfo *pj)
{
	g_hash_table_foreach (wb->sheets, print_sheet, pj);
}

static PrintJobInfo *
print_job_info_get (Workbook *wb)
{
	PrintJobInfo *pj;
	PrintMargins *pm = &wb->print_info->margins;
	int width, height;
	
	pj = g_new0 (PrintJobInfo, 1);

	/*
	 * Handy pointers
	 */
	pj->wb = wb;
	pj->pi = wb->print_info;

	/*
	 * Values that should be entered in a dialog box
	 */
	pj->start_page = 0;
	pj->end_page = -1;
	pj->range = PRINT_ALL;
	pj->sorted_print = TRUE;
	pj->n_copies = 1;

	/* Precompute information */
	width  = gnome_paper_pswidth (pj->pi->paper);
	height = gnome_paper_psheight (pj->pi->paper);

	if (pj->pi->orientation == PRINT_ORIENT_HORIZONTAL){
		pj->width = height;
		pj->height = width;
	} else {
		pj->width = width;
		pj->height = height;
	}

	pj->x_points = pj->width - (pm->left.points + pm->right.points);
	pj->y_points = pj->height -
		(pm->top.points + pm->bottom.points + pm->header.points + pm->footer.points);

	return pj;
}

static void
print_job_info_destroy (PrintJobInfo *pj)
{
	g_free (pj);
}

static void
setup_rotation (GnomePrintContext *pc)
{
	g_warning ("Should rotate here");
}

void
workbook_print (Workbook *wb)
{
	GnomePrinter *printer;
	GnomePrintContext *pc;
	PrintJobInfo *pj;
	Sheet *sheet;
	int loop, i;
	
	g_return_if_fail (wb != NULL);
	
	sheet = workbook_get_current_sheet (wb);

	printer = gnome_printer_dialog_new_modal ();
	if (!printer)
		return;

	pj = print_job_info_get (wb);

	if (pj->sorted_print){
		loop = pj->n_copies;
		pj->n_copies = 1;
	} else {
		loop = 1;
	}
	
	pj->print_context = gnome_print_context_new (printer);
	
	if (pj->pi->orientation == PRINT_ORIENT_HORIZONTAL){
		setup_rotation (pj->print_context);
	}
	
	for (i = 0; i < loop; i++){
		switch (pj->range){
		case PRINT_SELECTION:
			workbook_print_selection (wb, pj);
			break;
			
		case PRINT_ALL:
			workbook_print_all (wb, pj);
			break;
			
		case PRINT_ACTIVE_SHEETS:
			break;
		}
	}

	gnome_print_context_close_file (pj->print_context);

	gtk_object_unref (GTK_OBJECT (pj->print_context));

	print_job_info_destroy (pj);
	gtk_object_unref (GTK_OBJECT (printer));
}

