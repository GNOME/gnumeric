/*
 * solver-lp:  Linear programming methods.
 *
 * Authors:
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "solver.h"
#include "func.h"


int solver_simplex(Workbook *wb, Sheet *sheet)
{
        int i;
        CellList *cell_list = sheet->solver_parameters.input_cells;
	GSList   *constraints = sheet->solver_parameters.constraints;

        printf("Target cell: col=%d, row=%d\n", 
	       sheet->solver_parameters.target_cell->col->pos, 
	       sheet->solver_parameters.target_cell->row->pos);
	printf("Input cells:\n");
	for (i=0; cell_list != NULL; cell_list = cell_list->next) {
	        Cell *cell = (Cell *) cell_list->data;

	        printf("  %2d: col=%d, row=%d\n", i,
		       cell->col->pos, cell->row->pos);
	}
	printf("Constraints:\n");
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *) constraints->data;

	        printf("%-30s (col=%d, row=%d  %s  col=%d, row=%d\n",
		       c->str, c->lhs->col->pos, c->lhs->row->pos,
		       c->type, c->rhs->col->pos, c->rhs->row->pos);
		constraints = constraints->next;
	}
        return 0;
}
