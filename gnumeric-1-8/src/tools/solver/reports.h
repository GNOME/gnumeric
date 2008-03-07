#ifndef GNUMERIC_TOOLS_SOLVER_REPORTS_H
#define GNUMERIC_TOOLS_SOLVER_REPORTS_H

#include <solver.h>

void	 solver_prepare_reports		(SolverProgram *program,
					 SolverResults *res,
					 Sheet *sheet);
gboolean solver_prepare_reports_success (SolverProgram *program,
					 SolverResults *res,
					 Sheet *sheet);

#endif /* GNUMERIC_TOOLS_SOLVER_REPORTS_H */
