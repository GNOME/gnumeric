/*
 * eval.c:  Cell recomputation routines.
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 * Recomputations:

 update_cell (cell)
{
  cells_referenced = eval_expression (cell->formula);
  if (cells_referenced){
      push_refs (cells_referenced);
      return;
  }
  cell->cycle = current_cycle;
  return eval (cell);
}

push_refs (list)
{
	foreach i in (list){
		cell = findcell (i);
		if (cell->cell_cycle = current_cycle)
			loop;
		else
			push_cell(cell);
	}
}

int
eval_next_cell()
{
	cell = pop_cell (cell);
	if (cell){
		if (cell->cell_cycle == current_cycle)
			--loop_counter;
		else
			loop_counter = 40;
		update_cell (cell);
		return loop_counter;
	} else
		return 0;
}

 */

#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"

void
sheet_compute_cell (Sheet *sheet, Cell *cell)
{
	char *error_msg;
	Value *v;

	if (cell->text)
		string_unref_ptr (&cell->text);
	
	v = eval_expr (sheet, cell->parsed_node,
		       cell->col->pos,
		       cell->row->pos,
		       &error_msg);

	if (cell->value)
		value_release (cell->value);
	
	if (v == NULL){
		cell->text = string_get (error_msg);
		cell->value = NULL;
	} else {
		/* FIXME: Use the format stuff */
		char *str = value_string (v);
		
		cell->value = v;
		cell->text  = string_get (str);
		g_free (str);
	}
}

static void
sheet_recompute_one_cell (Sheet *sheet, int col, int row, Cell *cell)
{
	if (cell->parsed_node == NULL)
		return;

	printf ("recomputing %d %d\n", col, row);
	sheet_compute_cell (sheet, cell);
	sheet_redraw_cell_region (sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
}

