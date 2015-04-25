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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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


static gboolean
gnm_solver_get_lp_coeff (GnmCell *target, GnmCell *cell,
			 gnm_float *x, GError **err)
{
        gnm_float x0, x1;
	gboolean res = FALSE;

	gnm_cell_eval (target);
	if (!VALUE_IS_NUMBER (target->value))
		goto fail;
	x0 = value_get_as_float (target->value);

	gnm_cell_set_value (cell, value_new_float (1));
	cell_queue_recalc (cell);
	gnm_cell_eval (target);
	if (!VALUE_IS_NUMBER (target->value))
		goto fail;
	x1 = value_get_as_float (target->value);

	*x = x1 - x0;
	res = TRUE;
	goto out;

fail:
	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("Target cell did not evaluate to a number."));
	*x = 0;

out:
	gnm_cell_set_value (cell, value_new_int (0));
	cell_queue_recalc (cell);
	gnm_cell_eval (target);

	return res;
}

static const char *
glpk_var_name (GnmSubSolver *ssol, GnmCell const *cell)
{
	if (ssol)
		return gnm_sub_solver_get_cell_name (ssol, cell);
	return cell_name (cell);
}

static gboolean
glpk_affine_func (GString *dst, GnmCell *target, GnmSubSolver *ssol,
		  gboolean zero_too,
		  gnm_float cst, GError **err)
{
	GnmSolver *sol = GNM_SOLVER (ssol);
	unsigned ui;
	gboolean any = FALSE;
	gnm_float y;
	GPtrArray *old_values;
	gboolean ok = TRUE;
	GPtrArray *input_cells = sol->input_cells;

	if (!target) {
		gnm_string_add_number (dst, cst);
		return TRUE;
	}

	old_values = g_ptr_array_new ();
	for (ui = 0; ui < input_cells->len; ui++) {
	        GnmCell *cell = g_ptr_array_index (input_cells, ui);
		g_ptr_array_add (old_values, value_dup (cell->value));
		gnm_cell_set_value (cell, value_new_int (0));
		cell_queue_recalc (cell);
	}

	gnm_cell_eval (target);
	y = cst + value_get_as_float (target->value);

	for (ui = 0; ui < input_cells->len; ui++) {
	        GnmCell *cell = g_ptr_array_index (input_cells, ui);
		gnm_float x;
		ok = gnm_solver_get_lp_coeff (target, cell, &x, err);
		if (!ok)
			goto fail;
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
	for (ui = 0; ui < input_cells->len; ui++) {
	        GnmCell *cell = g_ptr_array_index (input_cells, ui);
		GnmValue *old = g_ptr_array_index (old_values, ui);
		gnm_cell_set_value (cell, old);
		cell_queue_recalc (cell);
	}
	g_ptr_array_free (old_values, TRUE);

	return ok;
}

static GString *
glpk_create_program (Sheet *sheet, GOIOContext *io_context,
		     GnmSubSolver *ssol, GError **err)
{
	GnmSolver *sol = GNM_SOLVER (ssol);
	GnmSolverParameters *sp = sheet->solver_parameters;
	GString *prg = NULL;
	GString *constraints = g_string_new (NULL);
	GString *binaries = g_string_new (NULL);
	GString *integers = g_string_new (NULL);
	GString *objfunc = g_string_new (NULL);
	GSList *l;
	GnmCell *target_cell = gnm_solver_param_get_target_cell (sp);
	GPtrArray *input_cells = sol->input_cells;
	gsize progress;

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

	progress = 2;
	if (sp->options.assume_non_negative) progress++;
	if (sp->options.assume_discrete) progress++;
	progress += g_slist_length (sp->constraints);

	go_io_count_progress_set (io_context, progress, 1);

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
	if (!glpk_affine_func (objfunc, target_cell, ssol,
			       TRUE, 0, err))
		goto fail;
	g_string_append (objfunc, "\n");
	go_io_count_progress_update (io_context, 1);

	/* ---------------------------------------- */

	if (sp->options.assume_non_negative) {
		unsigned ui;
		for (ui = 0; ui < input_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (input_cells, ui);
			g_string_append_printf (constraints, " %s >= 0\n",
						glpk_var_name (ssol, cell));
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
		     i++) {
			if (type) {
				g_string_append_printf
					(type, " %s\n",
					 glpk_var_name (ssol, lhs));
			} else {
				gboolean ok;

				g_string_append_c (constraints, ' ');

				ok = glpk_affine_func
					(constraints, lhs, ssol,
					 FALSE, cl, err);
				if (!ok)
					goto fail;

				g_string_append_c (constraints, ' ');
				g_string_append (constraints, op);
				g_string_append_c (constraints, ' ');

				ok = glpk_affine_func
					(constraints, rhs, ssol,
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
	g_string_free (integers, TRUE);
	g_string_free (binaries, TRUE);

	return prg;
}

void
glpk_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		WorkbookView const *wb_view, GsfOutput *output)
{
	Sheet *sheet = wb_view_cur_sheet (wb_view);
	GError *err = NULL;
	GString *prg;
	GnmLocale *locale;
	GnmSubSolver *ssol = g_object_get_data (G_OBJECT (fs), "solver");

	go_io_progress_message (io_context,
				_("Writing glpk file..."));

	locale = gnm_push_C_locale ();
	prg = glpk_create_program (sheet, io_context, ssol, &err);
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
}
