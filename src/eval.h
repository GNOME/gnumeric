#ifndef EVAL_H
#define EVAL_H

/*
 * A DependencyRange defines a range of cells whose values
 * are used by another Cell in the spreadsheet.
 *
 * A change in those cells will trigger a recomputation on the
 * cells listed in cell_list.
 */
typedef DependencyRange {
	int ref_count;

	Sheet *sheet;
	int   start_col, start_row;
	int   end_col, end_row;

	/* The list of cells that depend on this range */
	GList *cell_list;
};

/* Registers all of the dependencies this cell has */
void    cell_add_dependencies    (Cell *cell, Sheet *sheet);

/* Removes this cell from the list of dependencies */
void    cell_drop_dependencies   (Cell *cell);

/*
 * Returns a newly allocated list with Cells inside that
 * depend on the value at col, row
 */
GList   *cell_get_dependencies   (Sheet *shet, int col, int row);

#endif

