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
#include <gnumeric.h>
#include <numbers.h>
#include <workbook-view.h>
#include <sheet.h>
#include <workbook.h>
#include <value.h>
#include <cell.h>
#include <solver.h>
#include <ranges.h>
#include <parse-util.h>
#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>

#include <gsf/gsf-output.h>
#include <string.h>


static gboolean
gnm_solver_get_lp_coeff (GnmCell *target, GnmCell *cell,
			 gnm_float *x, GError **err)
{
        gnm_float x0, x1;
	GnmValue *old = value_dup (cell->value);
	gboolean res = FALSE;

	gnm_cell_set_value (cell, value_new_float (1));
	cell_queue_recalc (cell);
	gnm_cell_eval (target);
	if (!VALUE_IS_NUMBER (target->value))
		goto fail;
	x1 = value_get_as_float (target->value);

	gnm_cell_set_value (cell, value_new_float (0));
	cell_queue_recalc (cell);
	gnm_cell_eval (target);
	if (!VALUE_IS_NUMBER (target->value))
		goto fail;
	x0 = value_get_as_float (target->value);

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
	gnm_cell_set_value (cell, old);
	cell_queue_recalc (cell);
	gnm_cell_eval (target);

	return res;
}

/*
 * FIXME: we need to handle the situation where cells from more than one
 * sheet are involved.
 */
static const char *
lpsolve_var_name (GnmCell const *cell)
{
	return cell_name (cell);
}

static double
force_round (double d)
{
	volatile double *pd = &d;
	return *pd;
}

static void
lpsolve_add_number (GString *buf, double d)
{
	char tmp[G_ASCII_DTOSTR_BUF_SIZE + 1];
	double d1 = force_round (d);
	double d2;

	g_ascii_formatd (tmp, G_ASCII_DTOSTR_BUF_SIZE, "%.15g", d1);

	d2 = force_round (g_ascii_strtod (tmp, NULL));
	if (d1 != d2) {
		g_ascii_formatd (tmp, G_ASCII_DTOSTR_BUF_SIZE, "%.16g", d1);
	}

	g_string_append (buf, tmp);
}

static gboolean
lpsolve_affine_func (GString *dst, GnmCell *target,
		     GSList *input_cells, GError **err)
{
	GSList *l;
	gboolean any = FALSE;
	gnm_float y = value_get_as_float (target->value);

 	for (l = input_cells; l; l = l->next) {
	        GnmCell *cell = l->data;
		gnm_float x;
		gboolean ok = gnm_solver_get_lp_coeff (target, cell, &x, err);
		if (!ok)
			return FALSE;
		if (x == 0)
			continue;

		y -= x * value_get_as_float (cell->value);

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
			lpsolve_add_number (dst, x);
			g_string_append_c (dst, ' ');
		}

		g_string_append (dst, lpsolve_var_name (cell));

		any = TRUE;
	}

	if (!any || y)
		lpsolve_add_number (dst, y);

	return TRUE;
}

static GnmValue *
cb_grab_cells (GnmCellIter const *iter, gpointer user)
{
	GList **the_list = user;
	GnmCell *cell;

	if (NULL == (cell = iter->cell))
		cell = sheet_cell_create (iter->pp.sheet,
			iter->pp.eval.col, iter->pp.eval.row);
	*the_list = g_list_append (*the_list, cell);
	return NULL;
}

static GString *
lpsolve_create_program (Sheet *sheet, GError **err)
{
	SolverParameters const *sp = sheet->solver_parameters;
	GString *prg = NULL;
	GString *constraints = g_string_new (NULL);
	GString *declarations = g_string_new (NULL);
	GString *objfunc = g_string_new (NULL);
	GSList *l, *input_cells = NULL;

	/* This is insane -- why do we keep a string?  */
	{
		GnmEvalPos ep;
		GnmRange r;
		GnmValue *vr;

		if (!range_parse (&r, sp->input_entry_str,
				  gnm_sheet_get_size (sheet)))
			goto fail;

		vr = value_new_cellrange_r (sheet, &r);
		eval_pos_init_sheet (&ep, sheet);
		workbook_foreach_cell_in_range (&ep, vr, CELL_ITER_ALL,
						cb_grab_cells, &input_cells);
		value_release (vr);
	}

	/* ---------------------------------------- */

	switch (sp->problem_type) {
	case SolverEqualTo:
		if (!lpsolve_affine_func (constraints, sp->target_cell,
					  input_cells, err))
			goto fail;
		/* FIXME -- what value goes here?  */
		g_string_append (constraints, " = 42;\n");
		/* Fall through */
	case SolverMinimize:
		g_string_append (objfunc, "min: ");
		break;
	case SolverMaximize:
		g_string_append (objfunc, "max: ");
		break;
	default:
		g_assert_not_reached ();
	}

	if (!lpsolve_affine_func (objfunc, sp->target_cell,
				  input_cells, err))
		goto fail;
	g_string_append (objfunc, ";\n");

	/* ---------------------------------------- */

	if (sp->options.assume_non_negative) {
		GSList *l;
		for (l = input_cells; l; l = l->next) {
			GnmCell *cell = l->data;
			g_string_append (constraints,
					 lpsolve_var_name (cell));
			g_string_append (constraints, " >= 0;\n");
		}
	}

	if (sp->options.assume_discrete) {
		GSList *l;
		for (l = input_cells; l; l = l->next) {
			GnmCell *cell = l->data;
			g_string_append (declarations, "int ");
			g_string_append (declarations,
					 lpsolve_var_name (cell));
			g_string_append (declarations, ";\n");
		}
	}

 	for (l = sp->constraints; l; l = l->next) {
		SolverConstraint *c = l->data;
		const char *op = NULL;
		const char *type = NULL;
		int dx, dy;
		gboolean right_small = TRUE;

		switch (c->type) {
		case SolverLE:
			op = "<=";
			right_small = FALSE;
			break;
		case SolverGE:
			op = ">=";
			break;
		case SolverEQ:
			op = "=";
			break;
		case SolverINT:
			type = "int";
			break;
		case SolverBOOL:
			type = "binary";
			break;
		default:
			g_assert_not_reached ();
		}

		for (dy = 0; dy < c->rows; dy++) {
			for (dx = 0; dx < c->cols; dx++) {
				GnmCell *lhs =
					sheet_cell_get (sheet,
							c->lhs.col + dx,
							c->lhs.row + dy);
				GnmCell *rhs =
					sheet_cell_get (sheet,
							c->rhs.col + dx,
							c->rhs.row + dy);

				if (!lhs || (op && !rhs))
					continue;

				if (type) {
					g_string_append (declarations, type);
					g_string_append_c (declarations, ' ');
					g_string_append (declarations, lpsolve_var_name (lhs));
					g_string_append (declarations, ";\n");
				} else {
					gboolean ok;

					ok = lpsolve_affine_func
						(constraints, lhs, input_cells, err);
					if (!ok)
						goto fail;

					g_string_append_c (constraints, ' ');
					g_string_append (constraints, op);
					g_string_append_c (constraints, ' ');

					ok = lpsolve_affine_func
						(constraints, rhs, input_cells, err);
					if (!ok)
						goto fail;

					g_string_append (constraints, ";\n");
				}
			}
		}
	}

	/* ---------------------------------------- */

	prg = g_string_new (NULL);
	g_string_append_printf (prg,
				"/* Created by Gnumeric %s */\n",
				GNM_VERSION_FULL);
	g_string_append (prg, "\n/* Object function */\n");
	go_string_append_gstring (prg, objfunc);
	g_string_append (prg, "\n/* Constraints */\n");
	go_string_append_gstring (prg, constraints);
	g_string_append (prg, "\n/* Declarations */\n");
	go_string_append_gstring (prg, declarations);
	g_string_append (prg, "\n\n/* The End */\n");

fail:
	g_string_free (objfunc, TRUE);
	g_string_free (constraints, TRUE);
	g_string_free (declarations, TRUE);

	return prg;
}

void
lpsolve_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output);

void
lpsolve_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output)
{
	Sheet *sheet = wb_view_cur_sheet (wb_view);
	GError *err = NULL;
	GString *prg;

	workbook_recalc (sheet->workbook);

	prg = lpsolve_create_program (sheet, &err);
	if (!prg) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
					     err->message);
		g_error_free (err);
		return;
	}

	gsf_output_write (output, prg->len, prg->str);
	g_string_free (prg, TRUE);
}
