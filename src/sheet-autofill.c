/*
 * sheet-autofill.c: Provides the autofill features
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org), 1998
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "sheet-autofill.h"

static void
sheet_autofill_dir (Sheet *sheet,
		    int base_col,     int base_row,
		    int region_count,
		    int start_pos,    int end_pos,
		    int col_inc,      int row_inc)
{
	ValueType last_value_type = VALUE_UNKNOWN;
	int x = base_col;
	int y = base_row;
	int i, pos, fill_start;
	int region_types = 0;
	Value **values;

	values = g_new (Value *, region_count);
	
	printf ("region_count=%d\n", region_count);
	fill_start = start_pos;

	/*
	 * Count the number of different region types we have
	 *
	 * We need this to deal with regions that have numbers and strings
	 * and apply an autofill function depending on it.
	 */
	for (i = 0; i < region_count; i++){
		Cell *cell;
		
		fill_start++;

		cell = sheet_cell_get (sheet, x, y);
		if (cell)
			values [i] = cell->value;
		else
			values [i] = NULL;
		
		x += col_inc;
		y += row_inc;

		if (values [i]){
			if (last_value_type != VALUE_UNKNOWN)
				if (last_value_type != values [i]->type)
					region_types++;
			
			last_value_type = values [i]->type;

			/* Integers and floats are all numbers, count them together */
			if (last_value_type == VALUE_FLOAT)
				last_value_type = VALUE_INTEGER;
		}
	}

	printf ("we have %d different region types\n", region_types);
	for (pos = fill_start; pos < end_pos; pos++){
		Cell *cell;
		
		cell = sheet_cell_new (sheet, x, y);
		cell_set_text (cell, "Autofilled");
		
		x += col_inc;
		y += row_inc;
	}
	workbook_recalc (sheet->workbook);
}

void
sheet_autofill (Sheet *sheet, int base_col, int base_row, int w, int h, int end_col, int end_row)
{
	int n;			/* Number of different auto fill ranges */
	int end_pos;		/* Where to stop */
	int items;		/* Items provided per auto-fill range */
	int item_next_col;	/* increment col */
	int item_next_row;	/* increment row */

	int range;		/* range iterator */
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	printf ("base_col=%d, base_row=%d\n", base_col, base_row);
	printf ("width=%d     height=%d\n", w, h);
	printf ("end_col=%d   end_row=%d\n", end_col, end_row);
	
	if (end_col != base_col + w - 1){
		for (range = 0; range < h; range++)
			sheet_autofill_dir (sheet, base_col, base_row+range, w, base_col, end_col+1, 1, 0);
	} else {
		for (range = 0; range < w; range++)
			sheet_autofill_dir (sheet, base_col+range, base_row, h, base_row, end_row+1, 0, 1);
	}
}

void
autofill_init (void)
{

}
