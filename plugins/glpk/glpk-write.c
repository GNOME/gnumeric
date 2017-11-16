/*
 * Copyright (C) 2009 Morten Welinder (terra@gnome.org)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <boot.h>
#include <numbers.h>
#include <workbook-view.h>
#include <sheet.h>
#include <workbook.h>
#include <application.h>
#include <value.h>
#include <cell.h>
#include <expr.h>
#include <tools/gnm-solver.h>
#include <ranges.h>
#include <parse-util.h>
#include <gutils.h>
#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>

#include <string.h>


static const char *
glpk_var_name (GnmSubSolver *ssol, GnmCell const *cell)
{
	if (ssol)
		return gnm_sub_solver_get_cell_name (ssol, cell);
	return cell_name (cell);
}

static gboolean
glpk_affine_func (GString *dst, GnmCell *target, GnmSubSolver *ssol,
		  gnm_float const *x1, gnm_float const *x2,
		  gboolean zero_too,
		  gnm_float cst, GError **err)
{
	GnmSolver *sol = GNM_SOLVER (ssol);
	unsigned ui;
	gboolean any = FALSE;
	gnm_float y;
	gboolean ok = TRUE;
	GPtrArray *input_cells = sol->input_cells;
	gnm_float *cs;

	if (!target) {
		gnm_string_add_number (dst, cst);
		return TRUE;
	}

	gnm_solver_set_vars (sol, x1);
	gnm_cell_eval (target);
	y = cst + value_get_as_float (target->value);

	cs = gnm_solver_get_lp_coeffs (sol, target, x1, x2, err);
	if (!cs)
		goto fail;

	/* Adjust constant for choice of x1.  */
	for (ui = 0; ui < input_cells->len; ui++)
		y -= x1[ui] * cs[ui];

	for (ui = 0; ui < input_cells->len; ui++) {
	        GnmCell *cell = g_ptr_array_index (input_cells, ui);
		gnm_float x = cs[ui];
		if (x == 0 && !zero_too)
			continue;

		if (any) {
			if (x < 0)
				g_string_append (dst, " - ");
			else
				g_string_append (dst, " + ");
		} else {
			if (x < 0)
				g_string_append_c (dst, '-');
		}
		x = gnm_abs (x);

		if (x != 1) {
			gnm_string_add_number (dst, x);
			g_string_append_c (dst, ' ');
		}

		g_string_append (dst, glpk_var_name (ssol, cell));

		any = TRUE;
	}

	if (!any || y) {
		if (any) {
			g_string_append_c (dst, ' ');
			if (y > 0)
				g_string_append_c (dst, '+');
		}
		gnm_string_add_number (dst, y);
	}

fail:
	g_free (cs);

	return ok;
}

static GString *
glpk_create_program (GnmSubSolver *ssol, GOIOContext *io_context, GError **err)
{
	GnmSolver *sol = GNM_SOLVER (ssol);
	GnmSolverParameters *sp = sol->params;
	GString *prg = NULL;
	GString *constraints = g_string_new (NULL);
	GString *bounds = g_string_new (NULL);
	GString *binaries = g_string_new (NULL);
	GString *integers = g_string_new (NULL);
	GString *objfunc = g_string_new (NULL);
	GSList *l;
	GnmCell *target_cell = gnm_solver_param_get_target_cell (sp);
	GPtrArray *input_cells = sol->input_cells;
	gsize progress;
	GPtrArray *old = NULL;
	gnm_float *x1 = NULL, *x2 = NULL;
	int cidx = 0;

	/* ---------------------------------------- */

	if (sp->options.model_type != GNM_SOLVER_LP) {
		g_set_error (err,
			     go_error_invalid (),
			     0,
			     _("Only linear programs are handled."));
		goto fail;
	}

	/* ---------------------------------------- */

	if (ssol) {
		unsigned ui;

		for (ui = 0; ui < input_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (input_cells, ui);
			char *name = g_strdup_printf ("X_%u", ui + 1);
			gnm_sub_solver_name_cell (ssol, cell, name);
			g_free (name);
		}
	}

	/* ---------------------------------------- */

	progress = 3;
	/* assume_non_negative */ progress++;
	if (sp->options.assume_discrete) progress++;
	progress += g_slist_length (sp->constraints);

	go_io_count_progress_set (io_context, progress, 1);

	/* ---------------------------------------- */

	old = gnm_solver_save_vars (sol);

	gnm_solver_pick_lp_coords (sol, &x1, &x2);
	go_io_count_progress_update (io_context, 1);

	/* ---------------------------------------- */

	switch (sp->problem_type) {
	case GNM_SOLVER_MINIMIZE:
		g_string_append (objfunc, "Minimize\n");
		break;
	case GNM_SOLVER_MAXIMIZE:
		g_string_append (objfunc, "Maximize\n");
		break;
	default:
		g_assert_not_reached ();
	}
	go_io_count_progress_update (io_context, 1);

	g_string_append (objfunc, " obj: ");
	if (!glpk_affine_func (objfunc, target_cell, ssol, x1, x2,
			       TRUE, 0, err))
		goto fail;
	g_string_append (objfunc, "\n");
	go_io_count_progress_update (io_context, 1);

	/* ---------------------------------------- */

	{
		unsigned ui;
		for (ui = 0; ui < input_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (input_cells, ui);
			const char *name = glpk_var_name (ssol, cell);
			if (sp->options.assume_non_negative)
				g_string_append_printf (bounds, " %s >= 0\n", name);
			else
				g_string_append_printf (bounds, " %s free\n", name);
		}
		go_io_count_progress_update (io_context, 1);
	}

	if (sp->options.assume_discrete) {
		unsigned ui;
		for (ui = 0; ui < input_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (input_cells, ui);
			g_string_append_printf (integers, " %s\n",
						glpk_var_name (ssol, cell));
		}
		go_io_count_progress_update (io_context, 1);
	}

 	for (l = sp->constraints; l; l = l->next) {
		GnmSolverConstraint *c = l->data;
		const char *op = NULL;
		int i;
		gnm_float cl, cr;
		GnmCell *lhs, *rhs;
		GString *type = NULL;

		switch (c->type) {
		case GNM_SOLVER_LE:
			op = "<=";
			break;
		case GNM_SOLVER_GE:
			op = ">=";
			break;
		case GNM_SOLVER_EQ:
			op = "=";
			break;
		case GNM_SOLVER_INTEGER:
			type = integers;
			break;
		case GNM_SOLVER_BOOLEAN:
			type = binaries;
			break;
		default:
			g_assert_not_reached ();
		}

		for (i = 0;
		     gnm_solver_constraint_get_part (c, sp, i,
						     &lhs, &cl,
						     &rhs, &cr);
		     i++, cidx++) {
			if (type) {
				g_string_append_printf
					(type, " %s\n",
					 glpk_var_name (ssol, lhs));
			} else {
				gboolean ok;
				char *name;

				g_string_append_c (constraints, ' ');

				name = g_strdup_printf ("C_%d", cidx);
				gnm_sub_solver_name_constraint (ssol, cidx, name);
				g_string_append (constraints, name);
				g_string_append (constraints, ": ");
				g_free (name);

				ok = glpk_affine_func
					(constraints, lhs, ssol,
					 x1, x2,
					 FALSE, cl, err);
				if (!ok)
					goto fail;

				g_string_append_c (constraints, ' ');
				g_string_append (constraints, op);
				g_string_append_c (constraints, ' ');

				ok = glpk_affine_func
					(constraints, rhs, ssol,
					 x1, x2,
					 FALSE, cr, err);
				if (!ok)
					goto fail;

				g_string_append (constraints, "\n");
			}
		}

		go_io_count_progress_update (io_context, 1);
	}

	/* ---------------------------------------- */

	prg = g_string_new (NULL);
	g_string_append_printf (prg,
				"\\ Created by Gnumeric %s\n\n",
				GNM_VERSION_FULL);
	go_string_append_gstring (prg, objfunc);

	g_string_append (prg, "\nSubject to\n");
	go_string_append_gstring (prg, constraints);

	g_string_append (prg, "\nBounds\n");
	go_string_append_gstring (prg, bounds);

	if (integers->len > 0) {
		g_string_append (prg, "\nGeneral\n");
		go_string_append_gstring (prg, integers);
	}
	if (binaries->len > 0) {
		g_string_append (prg, "\nBinary\n");
		go_string_append_gstring (prg, binaries);
	}
	g_string_append (prg, "\nEnd\n");

fail:
	g_string_free (objfunc, TRUE);
	g_string_free (constraints, TRUE);
	g_string_free (bounds, TRUE);
	g_string_free (integers, TRUE);
	g_string_free (binaries, TRUE);
	g_free (x1);
	g_free (x2);

	if (old)
		gnm_solver_restore_vars (sol, old);

	return prg;
}

void
glpk_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		WorkbookView const *wb_view, GsfOutput *output)
{
	GError *err = NULL;
	GString *prg;
	GnmLocale *locale;
	GnmSolver *sol = NULL;
	GnmSubSolver *ssol = g_object_get_data (G_OBJECT (fs), "solver");

	if (!ssol) {
		// Create a temporary solver just functional enough to
		// write the program
		Sheet *sheet = wb_view_cur_sheet (wb_view);
		sol = glpk_solver_create (sheet->solver_parameters);
		ssol = GNM_SUB_SOLVER (sol);
	}

	go_io_progress_message (io_context,
				_("Writing glpk file..."));

	locale = gnm_push_C_locale ();
	prg = glpk_create_program (ssol, io_context, &err);
	gnm_pop_C_locale (locale);

	gnm_app_recalc ();

	if (!prg) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
					     err ? err->message : "?");
		goto fail;
	}

	gsf_output_write (output, prg->len, prg->str);
	g_string_free (prg, TRUE);

fail:
	go_io_progress_unset (io_context);
	if (err)
		g_error_free (err);

	if (sol)
		g_object_unref (sol);
}
