#ifndef GNUMERIC_EVAL_H
#define GNUMERIC_EVAL_H

#include "sheet.h"
#include "cell.h"

DependencyData *dependency_data_new     (void);
void            dependency_data_destroy (DependencyData *deps);

void            sheet_dump_dependencies (const Sheet *sheet);

/* Registers all of the dependencies this cell has */
void    cell_add_dependencies       (Cell *cell);

/* Explicitly add a dependency */
void    cell_add_explicit_dependency (Cell *cell, CellRef const *a);

/* Removes this cell from the list of dependencies */
void    cell_drop_dependencies   (Cell *cell);

/*
 * Returns a newly allocated list with Cells inside that
 * depend on the value at Sheet, col, row
 */
GList   *cell_get_dependencies     (Cell *cell);

GList   *sheet_get_intersheet_deps (Sheet *sheet);

GList   *sheet_region_get_deps     (Sheet *sheet, int start_col, int start_row,
				    int end_col, int end_row);

void     sheet_recalc_dependencies (Sheet *sheet);

/*
 * Queue a cell or a list of cells for computation
 */
void cell_queue_recalc           (Cell *cell);

void cell_queue_recalc_list      (GList *list, gboolean freelist);

void cell_unqueue_from_recalc    (Cell *cell);

/*
 * Evaluate a cell
 */
void cell_eval                   (Cell *cell);
void cell_eval_content		 (Cell *cell);

#endif /* GNUMERIC_EVAL_H */
