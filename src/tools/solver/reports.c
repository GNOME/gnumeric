/*
 * reports.c:  Solver report generation.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 1999, 2000, 2002 by Jukka-Pekka Iivonen
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
#include "sheet-style.h"
#include "eval.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "mathfunc.h"
#include "analysis-tools.h"

#include <math.h>
#include <stdlib.h>
#include <libgnome/gnome-i18n.h>
#include <sys/utsname.h>

/* ------------------------------------------------------------------------- */


static void
set_bold (Sheet *sheet, int col1, int row1, int col2, int row2)
{
	MStyle *mstyle = mstyle_new ();
	Range  range;

	range.start.col = col1;
	range.start.row = row1;
	range.end.col   = col2;
	range.end.row   = row2;

	mstyle_set_font_bold (mstyle, TRUE);
	sheet_style_apply_range (sheet, &range, mstyle);
}

static void
set_underlined (Sheet *sheet, int col1, int row1, int col2, int row2)
{
	MStyle *mstyle = mstyle_new ();
	Range  range;

	range.start.col = col1;
	range.start.row = row1;
	range.end.col   = col2;
	range.end.row   = row2;

	mstyle_set_font_uline (mstyle, TRUE);
	sheet_style_apply_range (sheet, &range, mstyle);
}

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

	if (str)
	        g_free (str);
	str = g_new (char, strlen (col_str) + strlen (row_str) + 2);

	if (*col_str)
	        sprintf (str, "%s %s", col_str, row_str);
	else
	        sprintf (str, "%s", row_str);

	return str;
}

static void
fill_header_titles (data_analysis_output_t *dao, gchar *title, Sheet *sheet)
{
	GString *buf;
	GDate   date;
	gchar   str[256];

	buf = g_string_new ("");
	g_string_sprintfa (buf, "%s %s %s", 
			   _("Gnumeric Solver"), VERSION, title);
	set_cell (dao, 0, 0, buf->str);
	g_string_free (buf, FALSE);

	buf = g_string_new ("");
	g_string_sprintfa (buf, "%s [%s]%s",
			   _("Worksheet:"),
			   workbook_get_filename (sheet->workbook),
			   sheet->name_quoted);
	set_cell (dao, 0, 1, buf->str);
	g_string_free (buf, FALSE);

	buf = g_string_new ("");
	g_date_set_time (&date, time (NULL));
	g_date_strftime (str, 255, "%D", &date);
	g_string_sprintfa (buf, "%s %s", _("Report Created:"), str);
	set_cell (dao, 0, 2, buf->str);
	g_string_free (buf, FALSE);

	set_bold (dao->sheet, 0, 0, 0, 2);
}


/*
 * Generates the Solver's answer report.
 */
static void
solver_answer_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    i, vars;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Answer Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `Target Cell' section.
	 */
	set_cell (&dao, 1, 6, _("Cell"));
	set_cell (&dao, 2, 6, _("Name"));
	set_cell (&dao, 3, 6, _("Original Value"));
	set_cell (&dao, 4, 6, _("Final Value"));
	set_bold (dao.sheet, 0, 6, 4, 6);

	/* Set `Cell' field (cell reference to the target cell). */
	set_cell (&dao, 1, 7, cell_name (res->param->target_cell));

	/* Set `Name' field */
	set_cell (&dao, 2, 7, find_name (sheet,
					 res->param->target_cell->pos.col,
					 res->param->target_cell->pos.row));

	/* Set `Original Value' field */
	set_cell_float (&dao, 3, 7, res->original_value_of_obj_fn);

	/* Set `Final Value' field */
	cell = sheet_cell_get (sheet, res->param->target_cell->pos.col,
			       res->param->target_cell->pos.row);
	set_cell_value (&dao, 4, 7, value_duplicate (cell->value));


	/*
	 * Fill in the labels of `Adjustable Cells' section.
	 */
	set_cell (&dao, 1, 11,   _("Cell"));
	set_cell (&dao, 2, 11,   _("Name"));
	set_cell (&dao, 3, 11,   _("Original Value"));
	set_cell (&dao, 4, 11,   _("Final Value"));
	set_bold (dao.sheet, 0, 11, 4, 11);

	for (i = 0; i < vars; i++) {
		/* Set `Cell' column */
	        cell = get_solver_input_var (res, i);
		set_cell (&dao, 1, 12 + i, cell_name (cell));

		/* Set `Name' column */
		set_cell (&dao, 2, 12 + i, find_name (sheet, cell->pos.col,
						      cell->pos.row));

		/* Set `Original Value' column */
		set_cell_value (&dao, 3, 12 + i,
				value_new_float (res->original_values[i]));

		/* Set `Final Value' column */
		set_cell_value (&dao, 4, 12 + i,
				value_duplicate (cell->value));
	}


	/*
	 * Fill in the labels of `Constraints' section.
	 */
	set_cell (&dao, 1, 15 + vars, _("Cell"));
	set_cell (&dao, 2, 15 + vars, _("Name"));
	set_cell (&dao, 3, 15 + vars, _("Cell Value"));
	set_cell (&dao, 4, 15 + vars, _("Formula"));
	set_cell (&dao, 5, 15 + vars, _("Status"));
	set_cell (&dao, 6, 15 + vars, _("Slack"));
	set_bold (dao.sheet, 0, 15 + vars, 6, 15 + vars);

	for (i = 0; i < res->param->n_constraints +
	       res->param->n_int_bool_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];
		gnum_float       lhs, rhs;

		/* Set `Cell' column */
		set_cell (&dao, 1, 16 + vars + i,
			  cell_coord_name (c->lhs.col, c->lhs.row));

		/* Set `Name' column */
		set_cell (&dao, 2, 16 + vars + i,
			  find_name (sheet, c->lhs.col, c->lhs.row));

		/* Set `Cell Value' column */
		cell = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		lhs = value_get_as_float (cell->value);
		set_cell_value (&dao, 3, 16 + vars + i,
				value_duplicate (cell->value));

	        /* Set `Formula' column */
	        set_cell (&dao, 4, 16 + vars + i, c->str);

		/* Set `Status' column */
		cell = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);
		rhs = value_get_as_float (cell->value);
		if (gnumabs (lhs - rhs) < 0.001)
		        set_cell (&dao, 5, 16 + vars + i, _("Binding"));
		else
		        set_cell (&dao, 5, 16 + vars + i, _("Not Binding"));

		/* Set `Slack' column */
		set_cell_float (&dao, 6, 16 + vars + i, gnumabs (lhs - rhs));
	}

	/*
	 * Autofit columns to make the sheet more readable.
	 */

	for (i = 0; i <= 5; i++)
	        autofit_column (&dao, i);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the column A labels into the answer report sheet. */
	if (res->param->problem_type == SolverMaximize)
	        set_cell (&dao, 0, 5, _("Target Cell (Maximize)"));
	else
	        set_cell (&dao, 0, 5, _("Target Cell (Minimize)"));

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Answer Report"), sheet);

	/* Fill in other titles. */
	set_cell (&dao, 0, 10, _("Adjustable Cells"));
	set_cell (&dao, 0, 14 + vars, _("Constraints"));
}


/*
 * Generates the Solver's sensitivity report.
 */
static void
solver_sensitivity_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    i, vars;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Sensitivity Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `Adjustable Cells' section.
	 */

	set_cell (&dao, 3, 6, _("Final"));
	set_cell (&dao, 4, 6, _("Reduced"));
	set_cell (&dao, 1, 7, _("Cell"));
	set_cell (&dao, 2, 7, _("Name"));
	set_cell (&dao, 3, 7, _("Value"));
	set_cell (&dao, 4, 7, _("Gradient"));
	set_bold (dao.sheet, 0, 6, 4, 7);

	for (i = 0; i < vars; i++) {
		/* Set `Cell' column */
	        cell = get_solver_input_var (res, i);
		set_cell (&dao, 1, 8 + i, cell_name (cell));

		/* Set `Name' column */
		set_cell (&dao, 2, 8 + i, find_name (sheet, cell->pos.col,
						     cell->pos.row));

		/* Set `Final Value' column */
		set_cell_value (&dao, 3, 8 + i,
				value_duplicate (cell->value));

		/* Set `Reduced Gradient' column */
		/* FIXME: Set this also?? */
	}


	/*
	 * Fill in the labels of `Constraints' section.
	 */
	set_cell (&dao, 3, 10 + vars, _("Final"));
	set_cell (&dao, 4, 10 + vars, _("Lagrange"));
	set_cell (&dao, 1, 11 + vars, _("Cell"));
	set_cell (&dao, 2, 11 + vars, _("Name"));
	set_cell (&dao, 3, 11 + vars, _("Value"));
	set_cell (&dao, 4, 11 + vars, _("Multiplier"));
	set_bold (dao.sheet, 0, 10 + vars, 4, 11 + vars);

	for (i = 0; i < res->param->n_constraints +
	       res->param->n_int_bool_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];

		/* Set `Cell' column */
		set_cell (&dao, 1, 12 + vars + i,
			  cell_coord_name (c->lhs.col, c->lhs.row));

		/* Set `Name' column */
		set_cell (&dao, 2, 12 + vars + i,
			  find_name (sheet, c->lhs.col, c->lhs.row));

		/* Set `Final Value' column */
		cell = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		set_cell_value (&dao, 3, 12 + vars + i,
				value_duplicate (cell->value));

		/* Set `Lagrange Multiplier' */
		set_cell_value (&dao, 4, 12 + vars + i,
				value_new_float (res->shadow_prizes[i]));
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	for (i = 0; i <= 4; i++)
	        autofit_column (&dao, i);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Sensitivity Report"), sheet);

	/* Fill in other titles. */
	set_cell (&dao, 0, 5, _("Adjustable Cells"));
	set_cell (&dao, 0, 9 + vars, _("Constraints"));
}


/*
 * Generates the Solver's limits report.
 */
static void
solver_limits_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    vars, i;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Limits Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	set_cell (&dao, 0, 0, "A");
	set_cell (&dao, 4, 0, "A");
	set_cell (&dao, 7, 0, "A");


	/*
	 * Fill in the labels.
	 */

	set_cell (&dao, 2, 5, _("Target"));
	set_cell (&dao, 1, 6, _("Cell"));
	set_cell (&dao, 2, 6, _("Name"));
	set_cell (&dao, 3, 6, _("Value"));
	set_bold (dao.sheet, 2, 5, 2, 5);
	set_bold (dao.sheet, 0, 6, 3, 6);

	set_cell (&dao, 2, 10, _("Adjustable"));
	set_cell (&dao, 1, 11, _("Cell"));
	set_cell (&dao, 2, 11, _("Name"));
	set_cell (&dao, 3, 11, _("Value"));

	set_cell (&dao, 5, 10, _("Lower"));
	set_cell (&dao, 6, 10, _("Target"));
	set_cell (&dao, 5, 11, _("Limit"));
	set_cell (&dao, 6, 11, _("Result"));

	set_cell (&dao, 8, 10, _("Upper"));
	set_cell (&dao, 9, 10, _("Target"));
	set_cell (&dao, 8, 11, _("Limit"));
	set_cell (&dao, 9, 11, _("Result"));

	set_bold (dao.sheet, 2, 10, 9, 10);
	set_bold (dao.sheet, 0, 11, 9, 11);


	/*
	 * Fill in the target cell section.
	 */

	/* Set `Target Cell' field (cell reference to the target cell). */
	set_cell (&dao, 1, 7, cell_name (res->param->target_cell));

	/* Set `Target Name' field */
	set_cell (&dao, 2, 7, find_name (sheet,
					 res->param->target_cell->pos.col,
					 res->param->target_cell->pos.row));

	/* Set `Target Value' field */
        cell = sheet_cell_get (sheet, res->param->target_cell->pos.col,
                               res->param->target_cell->pos.row);
        set_cell_value (&dao, 3, 7, value_duplicate (cell->value));


	/*
	 * Fill in the adjustable cells and limits section.
	 */

	for (i = 0; i < vars; i++) {
		/* Set `Adjustable Cell' column */
	        cell = get_solver_input_var (res, i);
		set_cell (&dao, 1, 12 + i, cell_name (cell));

		/* Set `Adjustable Name' column */
		set_cell (&dao, 2, 12 + i, find_name (sheet, cell->pos.col,
						      cell->pos.row));

		/* Set `Adjustable Value' column */
		set_cell_value (&dao, 3, 12 + i,
				value_duplicate (cell->value));


		/* Set `Lower Limit' column */
		set_cell_value (&dao, 5, 12 + i, value_new_float(0)); /* FIXME */

		/* Set `Target Result' column */
		set_cell_value (&dao, 6, 12 + i, value_new_float(0)); /* FIXME */


		/* Set `Upper Limit' column */
		set_cell_value (&dao, 8, 12 + i, value_new_float(0)); /* FIXME */

		/* Set `Target Result' column */
		set_cell_value (&dao, 9, 12 + i, value_new_float(0)); /* FIXME */
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	for (i = 0; i <= 9; i++)
	        autofit_column (&dao, i);

	/* Clear these after autofit calls */
	set_cell (&dao, 4, 0, "");
	set_cell (&dao, 7, 0, "");


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
static void
solver_performance_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res)
{
        data_analysis_output_t dao;
	int                    i, mat_size, zeros;
	struct                 utsname unamedata;
	Value                  *v;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Performance Report"));

	dao.sheet->hide_grid = TRUE;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `General Statistics' section.
	 */

	set_cell (&dao, 2, 6, _("Variables"));
	set_cell (&dao, 3, 6, _("Constraints"));
	set_cell (&dao, 4, 6, _("Int Constraints"));
	set_cell (&dao, 5, 6, _("Bool Constraints"));
	set_cell (&dao, 1, 7, _("Number of"));
	set_bold (dao.sheet, 0, 6, 5, 6);
	set_bold (dao.sheet, 1, 7, 1, 7);

	/* Set the `Nbr of Variables'. */
	set_cell_value (&dao, 2, 7, value_new_float (res->param->n_variables));

	/* Set the `Nbr of Constraints'. */
	set_cell_value (&dao, 3, 7, value_new_float (res->param->n_constraints));

	/* Set the `Nbr of Int Constraints'. */
	set_cell_value (&dao, 4, 7, /* FIXME: Bools */
			value_new_float (res->param->n_int_bool_constraints));

	/* Set the `Nbr of Bool Constraints'. */
	set_cell_value (&dao, 5, 7, value_new_float (0)); /* FIXME: Bools */


	/*
	 * Fill in the labels of `Data Sparsity' section.
	 */

	set_cell (&dao, 2, 11, _("Matrix Elements"));
	set_cell (&dao, 3, 11, _("Non-zeros (constr.)"));
	set_cell (&dao, 4, 11, _("Zeros (constr.)"));
	set_cell (&dao, 5, 11, _("Non-zeros (obj. fn)"));
	set_cell (&dao, 6, 11, _("Zeros (obj. fn)"));
	set_cell (&dao, 1, 12, _("Number of"));
	set_cell (&dao, 1, 13, _("Ratio"));
	set_bold (dao.sheet, 0, 11, 6, 11);
	set_bold (dao.sheet, 1, 12, 1, 13);

	/* Set the `Nbr of Matrix Elements'. */
	mat_size = res->param->n_variables * res->param->n_constraints;
	set_cell_value (&dao, 2, 12, value_new_float (mat_size));

	/* Set the `Ratio of Matrix Elements'. */
	v = value_new_float (1);
	value_set_fmt (v, style_format_default_percentage ());
	set_cell_value (&dao, 2, 13, v);

	/* Set the `Nbr of Non-zeros (constr.)'. */
	set_cell_value (&dao, 3, 12, value_new_float (res->n_nonzeros_in_mat));

	/* Set the `Nbr of Zeros (constr.)'. */
	zeros = mat_size - res->n_nonzeros_in_mat;
	set_cell_value (&dao, 4, 12, value_new_float (zeros));

	/* Set the `Ratio of Non-zeros (constr.)'. */
	v = value_new_float ((gnum_float) res->n_nonzeros_in_mat / mat_size);
	value_set_fmt (v, style_format_default_percentage ());
	set_cell_value (&dao, 3, 13, v);

	/* Set the `Ratio of Zeros (constr.)'. */
	v = value_new_float ((gnum_float) zeros / mat_size);
	value_set_fmt (v, style_format_default_percentage ());
	set_cell_value (&dao, 4, 13, v);


	/* Set the `Nbr of Non-zeros (obj. fn)'. */
	set_cell_value (&dao, 5, 12, value_new_float (res->n_nonzeros_in_obj));

	/* Set the `Nbr of Zeros (obj. fn)'. */
	zeros = res->param->n_variables - res->n_nonzeros_in_obj;
	set_cell_value (&dao, 6, 12, value_new_float (zeros));

	/* Set the `Ratio of Non-zeros (obj. fn)'. */
	v = value_new_float ((gnum_float) res->n_nonzeros_in_obj /
			     res->param->n_variables);
	value_set_fmt (v, style_format_default_percentage ());
	set_cell_value (&dao, 5, 13, v);
			
	/* Set the `Ratio of Zeros (obj. fn)'. */
	v = value_new_float ((gnum_float) zeros / res->param->n_variables);
	value_set_fmt (v, style_format_default_percentage ());
	set_cell_value (&dao, 6, 13, v);


	/*
	 * Fill in the labels of `Computing Time' section.
	 */

	set_cell (&dao, 2, 17, _("User"));
	set_cell (&dao, 3, 17, _("System"));
	set_cell (&dao, 4, 17, _("Real"));
	set_cell (&dao, 1, 18, _("Time (sec.)"));
	set_bold (dao.sheet, 0, 17, 4, 17);
	set_bold (dao.sheet, 1, 18, 1, 18);

	/* Set the `User Time'. */
	set_cell_value (&dao, 2, 18, value_new_float (res->time_user));

	/* Set the `System Time'. */
	set_cell_value (&dao, 3, 18, value_new_float (res->time_system));

	/* Set the `Real Time'. */
	set_cell_value (&dao, 4, 18, value_new_float (res->time_real));


	/*
	 * Fill in the labels of `System Information' section.
	 */

	set_cell (&dao, 2, 22, _("CPU"));
	set_cell (&dao, 3, 22, _("OS"));
	set_cell (&dao, 1, 23, _("Name"));
	set_bold (dao.sheet, 0, 22, 3, 22);
	set_bold (dao.sheet, 1, 23, 1, 23);

	/* Set the `CPU Name'. */
	set_cell (&dao, 2, 23, _("Unknown")); /* FIXME */

	/* Set the `OS Name'. */
	if (uname (&unamedata) == -1) {
	        char  *tmp = g_strdup_printf (_("Unknown"));
		Value *r = value_new_string (tmp);
		g_free (tmp);
		set_cell_value (&dao, 3, 23, r);
	} else {
	        char  *tmp = g_strdup_printf (_("%s (%s)"),
					      unamedata.sysname,
					      unamedata.release);
		Value *r = value_new_string (tmp);
		g_free (tmp);
		set_cell_value (&dao, 3, 23, r);
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	for (i = 0; i <= 6; i++)
	        autofit_column (&dao, i);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Performance Report"), sheet);

	/* Fill in other titles. */
	set_cell (&dao, 0, 5, _("General Statistics"));
	set_cell (&dao, 0, 10, _("Data Sparsity"));
	set_cell (&dao, 0, 16, _("Computing Time"));
	set_cell (&dao, 0, 21, _("System Information"));
}


/* Generates the Solver's program report.
 */
static void
solver_program_report (WorkbookControl *wbc,
		       Sheet           *sheet,
		       SolverResults   *res)
{
        data_analysis_output_t dao;
	Cell                   *cell;
	int                    i, col, max_col, n, vars;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Program Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;


	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	set_cell (&dao, 0, 0, "A");
	set_cell (&dao, 1, 3, "A");


	/* Print the objective function. */
	col = max_col = 0;
        for (i = 0; i < vars; i++) {
	        if (res->obj_coeff[i] != 0) {
		        /* Print the sign. */
		        if (res->obj_coeff[i] < 0)
			        set_cell (&dao, 1 + col*3, 6, "-");
			else if (col > 0)
			        set_cell (&dao, 1 + col*3, 6, "+");

			/* Print the coefficent. */
			set_cell_float (&dao, 2 + col*3, 6,
					fabs (res->obj_coeff[i]));

			/* Print the name of the variable. */
			cell = get_solver_input_var (res, i);
			set_cell (&dao, 3 + col*3, 6,
				  find_name (sheet, cell->pos.col,
					     cell->pos.row));
			col++;
			if (col > max_col)
			        max_col = col;
		}
	}


	/* Print the constraints. */
	for (i = 0; i < res->param->n_constraints +
	       res->param->n_int_bool_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];

		/* Print the constraint function. */
		col = 0;
		for (n = 0; n < res->param->n_variables; n++) {
		        if (res->constr_coeff[i][n] != 0) {
			        /* Print the sign. */
			        if (res->constr_coeff[i][n] < 0)
				        set_cell (&dao, 1 + col*3, 10 + i, "-");
				else if (col > 0)
				        set_cell (&dao, 1 + col*3, 10 + i, "+");

				/* Print the coefficent. */
				set_cell_float (&dao, 2 + col*3, 10 + i,
						fabs (res->constr_coeff[i][n]));

				/* Print the name of the variable. */
				cell = get_solver_input_var (res, n);
				set_cell (&dao, 3 + col*3, 10 + i,
					  find_name (sheet, cell->pos.col,
						     cell->pos.row));
				col++;
				if (col > max_col)
				        max_col = col;
			}
		}


		/* Print the type. */
		switch (c->type) {
		case SolverLE:
		        set_cell (&dao, col*3 + 1, 10 + i, "<");
			set_underlined (dao.sheet, col*3 + 1, 10 + i,
					col*3 + 1, 10 + i);
		        break;
		case SolverGE:
		        set_cell (&dao, col*3 + 1, 10 + i, ">");
			set_underlined (dao.sheet, col*3 + 1, 10 + i,
					col*3 + 1, 10 + i);
		        break;
		case SolverEQ:
		        set_cell (&dao, col*3 + 1, 10 + i, "=");
		        break;
		case SolverINT:
		        break;
		case SolverBOOL:
		        break;
		case SolverOF:
		        break;
		}

		/* Set RHS column. */
		cell = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);
		set_cell_value (&dao, col*3 + 2, 10 + i,
				value_duplicate (cell->value));
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	for (i = 0; i <= max_col*3 + 2; i++)
	        autofit_column (&dao, i);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	set_cell (&dao, 1, 3, "");
	fill_header_titles (&dao, _("Program Report"), sheet);

	/* Print the type of the program. */
	switch (res->param->problem_type) {
	case SolverMinimize:
	        set_cell (&dao, 0, 5, _("Minimize"));
		break;
	case SolverMaximize:
	        set_cell (&dao, 0, 5, _("Maximize"));
		break;
	case SolverEqualTo:
	        set_cell (&dao, 0, 5, _("Equal to"));
		break;
	}
	set_bold (dao.sheet, 0, 5, 0, 5);

	/* Print `Subject to' title. */
	set_cell (&dao, 0, 9, _("Subject to"));
	set_bold (dao.sheet, 0, 9, 0, 9);
}


/* Generates the Solver's dual program report.
 */
static void
solver_dual_program_report (WorkbookControl *wbc,
			    Sheet           *sheet,
			    SolverResults   *res)
{
        data_analysis_output_t dao;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Dual Program Report"));

	dao.sheet->hide_grid = TRUE;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	set_cell (&dao, 0, 0, "A");

	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	fill_header_titles (&dao, _("Dual Program Report"), sheet);
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


