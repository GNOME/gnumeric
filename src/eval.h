#ifndef GNUMERIC_EVAL_H
#define GNUMERIC_EVAL_H

#include "sheet.h"
#include "cell.h"

/*
 * A DependencyRange defines a range of cells whose values
 * are used by another Cell in the spreadsheet.
 *
 * A change in those cells will trigger a recomputation on the
 * cells listed in cell_list.
 */
typedef struct {
	int ref_count;

	Range range;

	Sheet *sheet;
	/* The list of cells that depend on this range */
	GList *cell_list;
} DependencyRange;

/* Registers all of the dependencies this cell has */
void    cell_add_dependencies    (Cell *cell);

/* Removes this cell from the list of dependencies */
void    cell_drop_dependencies   (Cell *cell);

/*
 * Returns a newly allocated list with Cells inside that
 * depend on the value at Sheet, col, row
 */
GList   *cell_get_dependencies   (Sheet *sheet, int col, int row);

/*
 * Returns a newly allocated list with Cells inside that
 * depends on any values in the range specified
 */
GList   *region_get_dependencies (Sheet *sheet,
				  int start_col, int start_row,
				  int end_col,   int end_row);

/*
 * Queue a cell or a list of cells for computation
 */
void cell_queue_recalc           (Cell *cell);

void cell_queue_recalc_list      (GList *list);

void cell_unqueue_from_recalc    (Cell *cell);

/*
 * Evaluate a cell
 */
void cell_eval                   (Cell *cell);

#endif /* GNUMERIC_EVAL_H */
