/*
 * reports-write.h:
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2002 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GNUMERIC_SOLVER_REPORTS_WRITE_H
#define GNUMERIC_SOLVER_REPORTS_WRITE_H

#include "gnumeric.h"
#include "numbers.h"
#include "solver.h"

void
solver_answer_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res);
void
solver_sensitivity_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res);
void
solver_limits_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res);
void
solver_performance_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res);
gboolean
solver_program_report (WorkbookControl *wbc,
		       Sheet           *sheet,
		       SolverResults   *res);
void
solver_dual_program_report (WorkbookControl *wbc,
			    Sheet           *sheet,
			    SolverResults   *res);





#endif
