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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <libgnome/gnome-i18n.h>
#include <sys/utsname.h>
#include <string.h>

/* ------------------------------------------------------------------------- */

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
fill_header_titles (data_analysis_output_t *dao, gchar *title, Sheet *sheet)
{
	GString   *buf;
	GDate     date;
	GTimeVal  t;
	struct tm tm_s;
	gchar     *tmp;

	buf = g_string_new ("");
	g_string_sprintfa (buf, "%s %s %s", 
			   _("Gnumeric Solver"), VERSION, title);
	dao_set_cell (dao, 0, 0, buf->str);
	g_string_free (buf, FALSE);

	buf = g_string_new ("");
	g_string_sprintfa (buf, "%s [%s]%s",
			   _("Worksheet:"),
			   workbook_get_filename (sheet->workbook),
			   sheet->name_quoted);
	dao_set_cell (dao, 0, 1, buf->str);
	g_string_free (buf, FALSE);

	buf = g_string_new ("");
	g_string_append (buf, _("Report Created: "));
	g_get_current_time (&t);
	g_date_set_time (&date, t.tv_sec);
	g_date_to_struct_tm (&date, &tm_s);
	tm_s.tm_sec  = t.tv_sec % 60;
	tm_s.tm_min  = (t.tv_sec / 60) % 60;
	tm_s.tm_hour = (t.tv_sec / 3600) % 24;
	tmp = asctime (&tm_s);
	g_string_append (buf, tmp);
	dao_set_cell (dao, 0, 2, buf->str);
	g_string_free (buf, FALSE);

	dao_set_bold (dao, 0, 0, 0, 2);
}


/*
 * Generates the Solver's answer report.
 */
void
solver_answer_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    i, vars;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Answer Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `Target Cell' section.
	 */
	dao_set_cell (&dao, 1, 6, _("Cell"));
	dao_set_cell (&dao, 2, 6, _("Name"));
	dao_set_cell (&dao, 3, 6, _("Original Value"));
	dao_set_cell (&dao, 4, 6, _("Final Value"));
	dao_set_bold (&dao, 0, 6, 4, 6);

	/* Set `Cell' field (cell reference to the target cell). */
	dao_set_cell (&dao, 1, 7, cell_name (res->param->target_cell));

	/* Set `Name' field */
	dao_set_cell (&dao, 2, 7, res->target_name);

	/* Set `Original Value' field */
	dao_set_cell_float (&dao, 3, 7, res->original_value_of_obj_fn);

	/* Set `Final Value' field */
	dao_set_cell_float (&dao, 4, 7, res->value_of_obj_fn);


	/*
	 * Fill in the labels of `Adjustable Cells' section.
	 */
	dao_set_cell (&dao, 1, 11,   _("Cell"));
	dao_set_cell (&dao, 2, 11,   _("Name"));
	dao_set_cell (&dao, 3, 11,   _("Original Value"));
	dao_set_cell (&dao, 4, 11,   _("Final Value"));
	dao_set_bold (&dao, 0, 11, 4, 11);

	for (i = 0; i < vars; i++) {
		/* Set `Cell' column */
	        cell = solver_get_input_var (res, i);
		dao_set_cell (&dao, 1, 12 + i, cell_name (cell));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 12 + i, res->variable_names [i]);

		/* Set `Original Value' column */
		dao_set_cell_value (&dao, 3, 12 + i,
				value_new_float (res->original_values[i]));

		/* Set `Final Value' column */
		dao_set_cell_value (&dao, 4, 12 + i,
				value_duplicate (cell->value));
	}


	/*
	 * Fill in the labels of `Constraints' section.
	 */
	dao_set_cell (&dao, 1, 15 + vars, _("Cell"));
	dao_set_cell (&dao, 2, 15 + vars, _("Name"));
	dao_set_cell (&dao, 3, 15 + vars, _("Cell Value"));
	dao_set_cell (&dao, 4, 15 + vars, _("Formula"));
	dao_set_cell (&dao, 5, 15 + vars, _("Status"));
	dao_set_cell (&dao, 6, 15 + vars, _("Slack"));
	dao_set_bold (&dao, 0, 15 + vars, 6, 15 + vars);

	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];

		/* Set `Cell' column */
		dao_set_cell (&dao, 1, 16 + vars + i,
			  cell_coord_name (c->lhs.col, c->lhs.row));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 16 + vars + i,
			      res->constraint_names [i]);

		/* Set `Cell Value' column */
		dao_set_cell_float (&dao, 3, 16 + vars + i, res->lhs[i]);

	        /* Set `Formula' column */
	        dao_set_cell (&dao, 4, 16 + vars + i, c->str);

		if (c->type == SolverINT || c->type == SolverBOOL) {
		        dao_set_cell (&dao, 5, 16 + vars + i, _("Binding"));
		        continue;
		}

		/* Set `Status' column */
		if (res->slack[i] < 0.001  /* FIXME */)
		        dao_set_cell (&dao, 5, 16 + vars + i, _("Binding"));
		else
		        dao_set_cell (&dao, 5, 16 + vars + i, _("Not Binding"));

		/* Set `Slack' column */
		dao_set_cell_float (&dao, 6, 16 + vars + i, res->slack [i]);
	}

	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 5);

	/*
	 * Fill in the titles.
	 */

	/* Fill in the column A labels into the answer report sheet. */
	if (res->param->problem_type == SolverMaximize)
	        dao_set_cell (&dao, 0, 5, _("Target Cell (Maximize)"));
	else
	        dao_set_cell (&dao, 0, 5, _("Target Cell (Minimize)"));

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Answer Report"), sheet);

	/* Fill in other titles. */
	dao_set_cell (&dao, 0, 10, _("Adjustable Cells"));
	dao_set_cell (&dao, 0, 14 + vars, _("Constraints"));
}


/*
 * Generates the Solver's sensitivity report.
 */
void
solver_sensitivity_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    i, vars;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Sensitivity Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `Adjustable Cells' section.
	 */

	dao_set_cell (&dao, 3, 6, _("Final"));
	dao_set_cell (&dao, 4, 6, _("Reduced"));
	dao_set_cell (&dao, 5, 6, _("Objective"));
	dao_set_cell (&dao, 6, 6, _("Allowable"));
	dao_set_cell (&dao, 7, 6, _("Allowable"));

	dao_set_cell (&dao, 1, 7, _("Cell"));
	dao_set_cell (&dao, 2, 7, _("Name"));
	dao_set_cell (&dao, 3, 7, _("Value"));
	dao_set_cell (&dao, 4, 7, _("Cost"));
	dao_set_cell (&dao, 5, 7, _("Coefficient"));
	dao_set_cell (&dao, 6, 7, _("Increase"));
	dao_set_cell (&dao, 7, 7, _("Decrease"));
	dao_set_bold (&dao, 0, 6, 7, 7);

	for (i = 0; i < vars; i++) {
		/* Set `Cell' column */
	        cell = solver_get_input_var (res, i);
		dao_set_cell (&dao, 1, 8 + i, cell_name (cell));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 8 + i, res->variable_names[i]);

		/* Set `Final Value' column */
		dao_set_cell_value (&dao, 3, 8 + i,
				value_duplicate (cell->value));

		/* Set `Reduced Cost' column */
		/* FIXME: Set this also?? */

		/* Set `Objective Coefficient' column */
		dao_set_cell_float (&dao, 5, 8 + i, res->obj_coeff[i]);

		/* FIXME: Set this also?? */

		/* Set `Allowable Increase' column */
		/* FIXME: Set this also?? */

		/* Set `Allowable Decrease' column */
		/* FIXME: Set this also?? */
	}


	/*
	 * Fill in the labels of `Constraints' section.
	 */
	dao_set_cell (&dao, 3, 10 + vars, _("Final"));
	dao_set_cell (&dao, 4, 10 + vars, _("Shadow"));
	dao_set_cell (&dao, 5, 10 + vars, _("Constraint"));
	dao_set_cell (&dao, 6, 10 + vars, _("Allowable"));
	dao_set_cell (&dao, 7, 10 + vars, _("Allowable"));

	dao_set_cell (&dao, 1, 11 + vars, _("Cell"));
	dao_set_cell (&dao, 2, 11 + vars, _("Name"));
	dao_set_cell (&dao, 3, 11 + vars, _("Value"));
	dao_set_cell (&dao, 4, 11 + vars, _("Price"));
	dao_set_cell (&dao, 5, 11 + vars, _("R.H. Side"));
	dao_set_cell (&dao, 6, 11 + vars, _("Increase"));
	dao_set_cell (&dao, 7, 11 + vars, _("Decrease"));
	dao_set_bold (&dao, 0, 10 + vars, 7, 11 + vars);

	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];

		/* Set `Cell' column */
		dao_set_cell (&dao, 1, 12 + vars + i,
			  cell_coord_name (c->lhs.col, c->lhs.row));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 12 + vars + i,
			      res->constraint_names [i]);

		/* Set `Final Value' column */
		cell = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		dao_set_cell_value (&dao, 3, 12 + vars + i,
				    value_duplicate (cell->value));

		/* Set `Shadow Price' */
		dao_set_cell_value (&dao, 4, 12 + vars + i,
				value_new_float (res->shadow_prizes[i]));

		/* Set `Constraint R.H. Side' column */
		dao_set_cell_float (&dao, 5, 12 + vars + i, res->rhs[i]);

		/* Set `Allowable Increase/Decrease' columns */
		if (res->slack[i] < 0.001  /* FIXME */) {
		        dao_set_cell_float (&dao, 6, 12 + vars + i,
				      res->constr_allowable_increase[i]);
		        /* FIXME */
		} else {
		        switch (c->type) {
		        case SolverLE:
			        dao_set_cell (&dao, 6, 12 + vars + i,
					      _("Infinity"));
			        dao_set_cell_float (&dao, 7, 12 + vars + i,
						    res->slack[i]);
				break;
			case SolverGE:
			        dao_set_cell_float (&dao, 6, 12 + vars + i,
						    res->slack[i]);
			        dao_set_cell (&dao, 7, 12 + vars + i,
					      _("Infinity"));
				break;
			case SolverEQ:
			        dao_set_cell_float (&dao, 6, 12 + vars + i, 0);
			        dao_set_cell_float (&dao, 7, 12 + vars + i, 0);
				break;
			default:
			        break;
			}
		}
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 4);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Sensitivity Report"), sheet);

	/* Fill in other titles. */
	dao_set_cell (&dao, 0, 5, _("Adjustable Cells"));
	dao_set_cell (&dao, 0, 9 + vars, _("Constraints"));
}


/*
 * Generates the Solver's limits report.
 */
void
solver_limits_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    vars, i;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Limits Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set thise to fool the autofit_column function.  (They will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");
	dao_set_cell (&dao, 4, 3, "A");
	dao_set_cell (&dao, 7, 3, "A");


	/*
	 * Fill in the labels.
	 */

	dao_set_cell (&dao, 2, 5, _("Target"));
	dao_set_cell (&dao, 1, 6, _("Cell"));
	dao_set_cell (&dao, 2, 6, _("Name"));
	dao_set_cell (&dao, 3, 6, _("Value"));
	dao_set_bold (&dao, 2, 5, 2, 5);
	dao_set_bold (&dao, 0, 6, 3, 6);

	dao_set_cell (&dao, 2, 10, _("Adjustable"));
	dao_set_cell (&dao, 1, 11, _("Cell"));
	dao_set_cell (&dao, 2, 11, _("Name"));
	dao_set_cell (&dao, 3, 11, _("Value"));

	dao_set_cell (&dao, 5, 10, _("Lower"));
	dao_set_cell (&dao, 6, 10, _("Target"));
	dao_set_cell (&dao, 5, 11, _("Limit"));
	dao_set_cell (&dao, 6, 11, _("Result"));

	dao_set_cell (&dao, 8, 10, _("Upper"));
	dao_set_cell (&dao, 9, 10, _("Target"));
	dao_set_cell (&dao, 8, 11, _("Limit"));
	dao_set_cell (&dao, 9, 11, _("Result"));

	dao_set_bold (&dao, 2, 10, 9, 10);
	dao_set_bold (&dao, 0, 11, 9, 11);


	/*
	 * Fill in the target cell section.
	 */

	/* Set `Target Cell' field (cell reference to the target cell). */
	dao_set_cell (&dao, 1, 7, cell_name (res->param->target_cell));

	/* Set `Target Name' field */
	dao_set_cell (&dao, 2, 7, res->target_name);

	/* Set `Target Value' field */
        cell = sheet_cell_get (sheet, res->param->target_cell->pos.col,
                               res->param->target_cell->pos.row);
        dao_set_cell_float (&dao, 3, 7, res->value_of_obj_fn);


	/*
	 * Fill in the adjustable cells and limits section.
	 */

	for (i = 0; i < vars; i++) {
		/* Set `Adjustable Cell' column */
	        cell = solver_get_input_var (res, i);
		dao_set_cell (&dao, 1, 12 + i, cell_name (cell));

		/* Set `Adjustable Name' column */
		dao_set_cell (&dao, 2, 12 + i, res->variable_names[i]);

		/* Set `Adjustable Value' column */
		dao_set_cell_value (&dao, 3, 12 + i,
				value_duplicate (cell->value));


		/* Set `Lower Limit' column */
		dao_set_cell_float (&dao, 5, 12 + i, res->limits[i].lower_limit);

		/* Set `Target Result' column */
		dao_set_cell_float (&dao, 6, 12 + i, res->limits[i].lower_result);


		/* Set `Upper Limit' column */
		dao_set_cell_float (&dao, 8, 12 + i, res->limits[i].upper_limit);

		/* Set `Target Result' column */
		dao_set_cell_float (&dao, 9, 12 + i, res->limits[i].upper_result);
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 9);

	/* Clear these after autofit calls */
	dao_set_cell (&dao, 4, 3, "");
	dao_set_cell (&dao, 7, 3, "");

	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Limits Report"), sheet);
}


/* Generates the Solver's performance report.  Contains some statistical
 * information regarding the program, information on how long it took
 * to be solved, and what kind of a system did the processing.
 */
void
solver_performance_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res)
{
        data_analysis_output_t dao;
	int                    mat_size, zeros;
	struct                 utsname unamedata;
	Value                  *v;
	gchar                  model_name [256], cpu_mhz [256];

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Performance Report"));

	dao.sheet->hide_grid = TRUE;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `General Statistics' section.
	 */

	dao_set_cell (&dao, 2, 6, _("Variables"));
	dao_set_cell (&dao, 3, 6, _("Constraints"));
	dao_set_cell (&dao, 4, 6, _("Int Constraints"));
	dao_set_cell (&dao, 5, 6, _("Bool Constraints"));
	dao_set_cell (&dao, 1, 7, _("Number of"));
	dao_set_bold (&dao, 0, 6, 5, 6);
	dao_set_bold (&dao, 1, 7, 1, 7);

	/* Set the `Nbr of Variables'. */
	dao_set_cell_value (&dao, 2, 7, value_new_float (res->param->n_variables));

	/* Set the `Nbr of Constraints'. */
	dao_set_cell_value (&dao, 3, 7, value_new_float (res->param->n_constraints));

	/* Set the `Nbr of Int Constraints'. */
	dao_set_cell_value (&dao, 4, 7, 
			    value_new_float (res->param->n_int_constraints));

	/* Set the `Nbr of Bool Constraints'. */
	dao_set_cell_value (&dao, 5, 7,
			    value_new_float (res->param->n_bool_constraints));


	/*
	 * Fill in the labels of `Data Sparsity' section.
	 */

	dao_set_cell (&dao, 2, 11, _("Matrix"));
	dao_set_cell (&dao, 2, 12, _("Elements"));
	dao_set_cell (&dao, 3, 11, _("Non-zeros in"));
	dao_set_cell (&dao, 3, 12, _("Constraints"));
	dao_set_cell (&dao, 4, 11, _("Zeros in"));
	dao_set_cell (&dao, 4, 12, _("Constraints"));
	dao_set_cell (&dao, 5, 11, _("Non-zeros in"));
	dao_set_cell (&dao, 5, 12, _("Obj. fn"));
	dao_set_cell (&dao, 6, 11, _("Zeros in"));
	dao_set_cell (&dao, 6, 12, _("Obj. fn"));
	dao_set_cell (&dao, 1, 13, _("Number of"));
	dao_set_cell (&dao, 1, 14, _("Ratio"));
	dao_set_bold (&dao, 0, 11, 6, 11);
	dao_set_bold (&dao, 0, 12, 6, 12);
	dao_set_bold (&dao, 1, 13, 1, 14);

	/* Set the `Nbr of Matrix Elements'. */
	mat_size = res->param->n_variables * res->param->n_constraints;
	dao_set_cell_value (&dao, 2, 13, value_new_float (mat_size));

	/* Set the `Ratio of Matrix Elements'. */
	v = value_new_float (1);
	value_set_fmt (v, style_format_default_percentage ());
	dao_set_cell_value (&dao, 2, 14, v);

	/* Set the `Nbr of Non-zeros (constr.)'. */
	dao_set_cell_value (&dao, 3, 13, value_new_float (res->n_nonzeros_in_mat));

	/* Set the `Nbr of Zeros (constr.)'. */
	zeros = mat_size - res->n_nonzeros_in_mat;
	dao_set_cell_value (&dao, 4, 13, value_new_float (zeros));

	/* Set the `Ratio of Non-zeros (constr.)'. */
	v = value_new_float ((gnum_float) res->n_nonzeros_in_mat / mat_size);
	value_set_fmt (v, style_format_default_percentage ());
	dao_set_cell_value (&dao, 3, 14, v);

	/* Set the `Ratio of Zeros (constr.)'. */
	v = value_new_float ((gnum_float) zeros / mat_size);
	value_set_fmt (v, style_format_default_percentage ());
	dao_set_cell_value (&dao, 4, 14, v);


	/* Set the `Nbr of Non-zeros (obj. fn)'. */
	dao_set_cell_value (&dao, 5, 13, value_new_float (res->n_nonzeros_in_obj));

	/* Set the `Nbr of Zeros (obj. fn)'. */
	zeros = res->param->n_variables - res->n_nonzeros_in_obj;
	dao_set_cell_value (&dao, 6, 13, value_new_float (zeros));

	/* Set the `Ratio of Non-zeros (obj. fn)'. */
	v = value_new_float ((gnum_float) res->n_nonzeros_in_obj /
			     res->param->n_variables);
	value_set_fmt (v, style_format_default_percentage ());
	dao_set_cell_value (&dao, 5, 14, v);
			
	/* Set the `Ratio of Zeros (obj. fn)'. */
	v = value_new_float ((gnum_float) zeros / res->param->n_variables);
	value_set_fmt (v, style_format_default_percentage ());
	dao_set_cell_value (&dao, 6, 14, v);


	/*
	 * Fill in the labels of `Computing Time' section.
	 */

	dao_set_cell (&dao, 2, 18, _("User"));
	dao_set_cell (&dao, 3, 18, _("System"));
	dao_set_cell (&dao, 4, 18, _("Real"));
	dao_set_cell (&dao, 1, 19, _("Time (sec.)"));
	dao_set_bold (&dao, 0, 18, 4, 18);
	dao_set_bold (&dao, 1, 18, 1, 19);

	/* Set the `User Time'. */
	dao_set_cell_value (&dao, 2, 19, value_new_float (res->time_user));

	/* Set the `System Time'. */
	dao_set_cell_value (&dao, 3, 19, value_new_float (res->time_system));

	/* Set the `Real Time'. */
	dao_set_cell_value (&dao, 4, 19, value_new_float (res->time_real));


	/*
	 * Fill in the labels of `System Information' section.
	 */

	dao_set_cell (&dao, 2, 23, _("CPU Model"));
	dao_set_cell (&dao, 3, 23, _("CPU MHz"));
	dao_set_cell (&dao, 4, 23, _("OS"));
	dao_set_cell (&dao, 1, 24, _("Name"));
	dao_set_bold (&dao, 0, 23, 4, 23);
	dao_set_bold (&dao, 1, 24, 1, 24);

	if (get_cpu_info (model_name, cpu_mhz, 255)) {
	        /* Set the `CPU Model'. */
	        dao_set_cell (&dao, 2, 24, model_name);

	        /* Set the `CPU Mhz'. */
	        dao_set_cell (&dao, 3, 24, cpu_mhz);
	} else {
	        /* Set the `CPU Model'. */
	        dao_set_cell (&dao, 2, 24, _("Unknown"));

	        /* Set the `CPU Mhz'. */
	        dao_set_cell (&dao, 3, 24, _("Unknown"));
	}

	/* Set the `OS Name'. */
	if (uname (&unamedata) == -1) {
	        char  *tmp = g_strdup_printf (_("Unknown"));
		Value *r = value_new_string (tmp);
		g_free (tmp);
		dao_set_cell_value (&dao, 4, 24, r);
	} else {
	        char  *tmp = g_strdup_printf (_("%s (%s)"),
					      unamedata.sysname,
					      unamedata.release);
		Value *r = value_new_string (tmp);
		g_free (tmp);
		dao_set_cell_value (&dao, 4, 24, r);
	}


	/*
	 * Fill in the labels of `Options' section.
	 */
	/* Set the labels. */
	dao_set_cell (&dao, 1, 27, _("Algorithm:"));
	dao_set_bold (&dao, 1, 27, 1, 27);

	/* Set the `Algorithm'. */
	dao_set_cell (&dao, 2, 27, _("LP Solve 3.2"));


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 6);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Performance Report"), sheet);

	/* Fill in other titles. */
	dao_set_cell (&dao, 0, 5, _("General Statistics"));
	dao_set_cell (&dao, 0, 10, _("Data Sparsity"));
	dao_set_cell (&dao, 0, 17, _("Computing Time"));
	dao_set_cell (&dao, 0, 22, _("System Information"));
	dao_set_cell (&dao, 0, 26, _("Options"));
}


/* Generates the Solver's program report.
 */
void
solver_program_report (WorkbookControl *wbc,
		       Sheet           *sheet,
		       SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    i, col, row, max_col, n, vars;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Program Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;


	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");
	dao_set_cell (&dao, 1, 3, "A");


	/* Print the objective function. */
	col = max_col = 0;
        for (i = 0; i < vars; i++) {
	        if (res->obj_coeff[i] != 0) {
		        /* Print the sign. */
		        if (res->obj_coeff[i] < 0)
			        dao_set_cell (&dao, 1 + col*3, 6, "-");
			else if (col > 0)
			        dao_set_cell (&dao, 1 + col*3, 6, "+");

			/* Print the coefficent. */
			if (gnumabs (res->obj_coeff[i]) != 1)
			        dao_set_cell_float (&dao, 2 + col*3, 6,
						gnumabs (res->obj_coeff[i]));

			/* Print the name of the variable. */
			dao_set_cell (&dao, 3 + col*3, 6,
				      res->variable_names [i]);
			col++;
			if (col > max_col)
			        max_col = col;
		}
	}


	/* Print the constraints. */
	row = 10;
	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];

		/* Print the constraint function. */
		col = 0;
		if (c->type == SolverINT || c->type == SolverBOOL)
		        continue;
		for (n = 0; n < res->param->n_variables; n++) {
		        if (res->constr_coeff[i][n] != 0) {
			        /* Print the sign. */
			        if (res->constr_coeff[i][n] < 0)
				        dao_set_cell (&dao, 1 + col*3, row, "-");
				else if (col > 0)
				        dao_set_cell (&dao, 1 + col*3, row, "+");

				/* Print the coefficent. */
				if (gnumabs (res->constr_coeff[i][n]) != 1)
				        dao_set_cell_float (&dao, 2 + col*3, row,
					     gnumabs (res->constr_coeff[i][n]));

				/* Print the name of the variable. */
				dao_set_cell (&dao, 3 + col*3, row,
					      res->variable_names [n]);
				col++;
				if (col > max_col)
				        max_col = col;
			}
		}


		/* Print the type. */
		switch (c->type) {
		case SolverLE:
		        dao_set_cell (&dao, col*3 + 1, row, "<");
			dao_set_underlined (&dao, col*3 + 1, row,
					    col*3 + 1, row);
		        break;
		case SolverGE:
		        dao_set_cell (&dao, col*3 + 1, row, ">");
			dao_set_underlined (&dao, col*3 + 1, row,
					    col*3 + 1, row);
		        break;
		case SolverEQ:
		        dao_set_cell (&dao, col*3 + 1, row, "=");
		        break;
		case SolverINT:
		case SolverBOOL:
		case SolverOF:
		        break;
		}

		/* Set RHS column. */
		dao_set_cell_float (&dao, col*3 + 2, row, res->rhs[i]);
		row++;
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, max_col*3 + 2);

	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	dao_set_cell (&dao, 1, 3, "");
	fill_header_titles (&dao, _("Program Report"), sheet);

	/* Print the type of the program. */
	switch (res->param->problem_type) {
	case SolverMinimize:
	        dao_set_cell (&dao, 0, 5, _("Minimize"));
		break;
	case SolverMaximize:
	        dao_set_cell (&dao, 0, 5, _("Maximize"));
		break;
	case SolverEqualTo:
	        dao_set_cell (&dao, 0, 5, _("Equal to"));
		break;
	}
	dao_set_bold (&dao, 0, 5, 0, 5);

	/* Print `Subject to' title. */
	dao_set_cell (&dao, 0, 9, _("Subject to"));
	dao_set_bold (&dao, 0, 9, 0, 9);
}


/* Generates the Solver's dual program report.
 */
void
solver_dual_program_report (WorkbookControl *wbc,
			    Sheet           *sheet,
			    SolverResults   *res)
{
        data_analysis_output_t dao;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Dual Program Report"));

	dao.sheet->hide_grid = TRUE;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");

	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Dual Program Report"), sheet);
}
