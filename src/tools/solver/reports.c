/*
 * reports.c:  Solver report generation.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 1999, 2000, 2002 by Jukka-Pekka Iivonen
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

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "numbers.h"

#include "format.h"
#include "parse-util.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "sheet-style.h"
#include "eval.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "mathfunc.h"
#include "analysis-tools.h"
#include "reports-write.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <libgnome/gnome-i18n.h>
#include <sys/utsname.h>
#include <string.h>

/* ------------------------------------------------------------------------- */


/* FIXME: Remove when done */
extern SolverConstraint*
get_solver_constraint (SolverResults *res, int n);

static char *
find_name (Sheet *sheet, int col, int row)
{
        static char *str = NULL;
	const char  *col_str = "";
	const char  *row_str = "";
        int         col_n, row_n;

	for (col_n = col - 1; col_n >= 0; col_n--) {
	        Cell *cell = sheet_cell_get (sheet, col_n, row);
		if (cell && !VALUE_IS_NUMBER (cell->value)) {
			col_str = value_peek_string (cell->value);
		        break;
		}
	}

	for (row_n = row - 1; row_n >= 0; row_n--) {
	        Cell *cell = sheet_cell_get (sheet, col, row_n);
		if (cell && !VALUE_IS_NUMBER (cell->value)) {
			row_str = value_peek_string (cell->value);
		        break;
		}
	}

	str = g_new (char, strlen (col_str) + strlen (row_str) + 2);

	if (*col_str)
	        sprintf (str, "%s %s", col_str, row_str);
	else
	        sprintf (str, "%s", row_str);

	return str;
}


/*
 * Fetches the CPU model and the speed of it.  Returns TRUE if succeeded,
 * FALSE otherwise.  FIXME: Currently Linux only.
 */
gboolean
get_cpu_info (gchar *model_name, gchar *cpu_mhz, unsigned int size)
{
        FILE     *in;
	gchar    buf[256], *p;
	gboolean model = FALSE, cpu = FALSE;
	unsigned len;

	in = fopen ("/proc/cpuinfo", "r");
	if (in == NULL)
	        return FALSE;
	while (fgets (buf, 255, in) != NULL) {
	        if (strncmp (buf, "model name", 10) == 0) {
		        p = g_strrstr (buf, ":");
			len = strlen (p);
			if (p != NULL && p[1] != '\0' && p[len - 1] == '\n') {
			        p[len - 1] = '\0';
				strncpy (model_name, p + 2, MIN (size, len - 2));
				model_name [len - 2] = '\0';
				model = TRUE;
			}
		}
	        if (strncmp (buf, "cpu MHz", 7) == 0) {
		        p = g_strrstr (buf, ":");
			len = strlen (p);
			if (p != NULL && p[1] != '\0' && p[len - 1] == '\n') {
			        p[len - 1] = '\0';
				strncpy (cpu_mhz, p + 2, MIN (size, len - 2));
				cpu_mhz [len - 2] = '\0';
				cpu = TRUE;
			}
		}
	}

	return model & cpu;
}

static void
get_input_variable_names (SolverResults *res, Sheet *sheet)
{
        Cell *cell;
	int  i;

	for (i = 0; i < res->param->n_variables; i++) {
	        cell = get_solver_input_var (res, i);
		res->variable_names[i] = find_name (sheet, cell->pos.col,
						    cell->pos.row);
	}
}

static void
get_constraint_names (SolverResults *res, Sheet *sheet)
{
        Cell *cell;
	int  i;

	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = get_solver_constraint (res, i);
		res->constraint_names[i] = find_name (sheet, c->lhs.col,
						      c->lhs.row);
	}
}


gboolean
solver_prepare_reports (SolverProgram *program, SolverResults *res,
			Sheet *sheet)
{
        res->target_name = find_name (sheet,
				      res->param->target_cell->pos.col,
				      res->param->target_cell->pos.row);
        get_input_variable_names (res, sheet);
        get_constraint_names (res, sheet);
}

void
solver_lp_reports (WorkbookControl *wbc, Sheet *sheet, SolverResults *res,
		   gboolean answer, gboolean sensitivity, gboolean limits,
		   gboolean performance, gboolean program, gboolean dual)
{
        if (answer)
	        solver_answer_report (wbc, sheet, res);
	if (sensitivity && ! res->ilp_flag)
	        solver_sensitivity_report (wbc, sheet, res);
	if (limits && ! res->ilp_flag)
	        solver_limits_report (wbc, sheet, res);
	if (performance)
	        solver_performance_report (wbc, sheet, res);
	if (program)
	        solver_program_report (wbc, sheet, res);
	if (dual)
	        solver_dual_program_report (wbc, sheet, res);
}

void
solver_qp_reports (WorkbookControl *wbc, Sheet *sheet, SolverResults *res,
		   gboolean answer, gboolean sensitivity, gboolean limits,
		   gboolean performance, gboolean program, gboolean dual)
{
}
