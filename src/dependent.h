#ifndef EVAL_H
#define EVAL_H

/*
 * A DependencyRange defines a range of cells whose values
 * are used by another Cell in the spreadsheet.
 *
 * A change in those cells will trigger a recomputation on the
 * cells listed in cell_list.
 */
typedef struct {
	int ref_count;

	Sheet *sheet;
	int   start_col, start_row;
	int   end_col, end_row;

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
GList   *cell_get_dependencies   (Sheet *shet, int col, int row);

/*
 * Queue a cell or a list of cells for computation
 */
void cell_queue_recalc           (Cell *cell);

void cell_queue_recalc_list      (GList *list);

/*
 * Evaluate a cell
 */
void cell_eval                   (Cell *cell);

#endif

