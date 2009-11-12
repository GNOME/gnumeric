/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * solver.c:  The Solver's core system.
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
#include <glib/gi18n-lib.h>
#include "reports.h"

#include "parse-util.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "sheet-style.h"
#include "dependent.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "ranges.h"
#include "expr.h"
#include "clipboard.h"
#include "commands.h"
#include "mathfunc.h"
#include "analysis-tools.h"
#include "api.h"
#include "gutils.h"
#include <goffice/goffice.h>
#include "xml-sax.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_TIMES
#include <sys/times.h>
#endif

#define GNM 100
#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

/* ------------------------------------------------------------------------- */


SolverParameters *
gnm_solver_param_new (Sheet *sheet)
{
	SolverParameters *res = g_new0 (SolverParameters, 1);

	dependent_managed_init (&res->target, sheet);
	dependent_managed_init (&res->input, sheet);

	res->options.model_type          = SolverLPModel;
	res->sheet                       = sheet;
	res->options.assume_non_negative = TRUE;
	res->options.algorithm           = NULL;
	res->options.scenario_name       = g_strdup ("Optimal");
	res->problem_type                = SolverMaximize;
	res->constraints                 = NULL;
	res->constraints		 = NULL;

	return res;
}

void
gnm_solver_param_free (SolverParameters *sp)
{
	dependent_managed_set_expr (&sp->target, NULL);
	dependent_managed_set_expr (&sp->input, NULL);
	go_slist_free_custom (sp->constraints,
			      (GFreeFunc)gnm_solver_constraint_free);
	g_free (sp->options.scenario_name);
	g_free (sp);
}

GnmValue const *
gnm_solver_param_get_input (SolverParameters const *sp)
{
	return sp->input.texpr
		? gnm_expr_top_get_constant (sp->input.texpr)
		: NULL;
}

void
gnm_solver_param_set_input (SolverParameters *sp, GnmValue *v)
{
	/* Takes ownership.  */
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&sp->input, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

static GnmValue *
cb_grab_cells (GnmCellIter const *iter, gpointer user)
{
	GSList **the_list = user;
	GnmCell *cell;

	if (NULL == (cell = iter->cell))
		cell = sheet_cell_create (iter->pp.sheet,
			iter->pp.eval.col, iter->pp.eval.row);
	*the_list = g_slist_append (*the_list, cell);
	return NULL;
}

GSList *
gnm_solver_param_get_input_cells (SolverParameters const *sp)
{
	GnmValue const *vr = gnm_solver_param_get_input (sp);
	GSList *input_cells = NULL;
	GnmEvalPos ep;

	if (!vr)
		return NULL;

	eval_pos_init_sheet (&ep, sp->sheet);
	workbook_foreach_cell_in_range (&ep, vr, CELL_ITER_ALL,
					cb_grab_cells,
					&input_cells);
	return input_cells;
}

void
gnm_solver_param_set_target (SolverParameters *sp, GnmCellRef const *cr)
{
	GnmCellRef cr2 = *cr;
	GnmExprTop const *texpr;

	/* Make reference absolute to avoid tracking problems on row/col
	   insert.  */
	cr2.row_relative = FALSE;
	cr2.col_relative = FALSE;

	texpr = gnm_expr_top_new (gnm_expr_new_cellref (&cr2));
	dependent_managed_set_expr (&sp->target, texpr);
	gnm_expr_top_unref (texpr);
}

const GnmCellRef *
gnm_solver_param_get_target (SolverParameters const *sp)
{
	return sp->target.texpr
		? gnm_expr_top_get_cellref (sp->target.texpr)
		: NULL;
}

GnmCell *
gnm_solver_param_get_target_cell (SolverParameters const *sp)
{
	const GnmCellRef *cr = gnm_solver_param_get_target (sp);
	if (!cr)
		return NULL;

        return sheet_cell_get (eval_sheet (cr->sheet, sp->sheet),
			       cr->col, cr->row);
}

gboolean
gnm_solver_param_valid (SolverParameters const *sp, GError **err)
{
	GSList *l;
	int i;
	GnmCell *target_cell;
	GSList *input_cells;

	target_cell = gnm_solver_param_get_target_cell (sp);
	if (!target_cell) {
		g_set_error (err,
			     go_error_invalid (),
			     0,
			     _("Invalid solver target"));
		return FALSE;
	}

	if (!gnm_cell_has_expr (target_cell) ||
	    target_cell->value == NULL ||
	    !VALUE_IS_FLOAT (target_cell->value)) {
		g_set_error (err,
			     go_error_invalid (),
			     0,
			     _("Target cell, %s, must contain a formula that evaluates to a number"),
			     cell_name (target_cell));
		return FALSE;
	}

	if (!gnm_solver_param_get_input (sp)) {
		g_set_error (err,
			     go_error_invalid (),
			     0,
			     _("Invalid solver input range"));
		return FALSE;
	}
	input_cells = gnm_solver_param_get_input_cells (sp);
	for (l = input_cells; l; l = l->next) {
		GnmCell *cell = l->data;
		if (gnm_cell_has_expr (cell)) {
			g_set_error (err,
				     go_error_invalid (),
				     0,
				     _("Input cell %s contains a formula"),
				     cell_name (cell));
			g_slist_free (input_cells);
			return FALSE;
		}
	}
	g_slist_free (input_cells);

	for (i = 1, l = sp->constraints; l; i++, l = l->next) {
		SolverConstraint *c = l->data;
		if (!gnm_solver_constraint_valid (c, sp)) {
			g_set_error (err,
				     go_error_invalid (),
				     0,
				     _("Solver constraint #%d is invalid"),
				     i);
			return FALSE;
		}
	}

	return TRUE;
}

/* ------------------------------------------------------------------------- */

static void
solver_constr_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	int type = 0;
	SolverConstraint *c;
	Sheet *sheet = gnm_xml_in_cur_sheet (xin);
	SolverParameters *sp = sheet->solver_parameters;
	int lhs_col = 0, lhs_row = 0, rhs_col = 0, rhs_row = 0;
	int cols = 1, rows = 1;
	gboolean old = FALSE;
	GnmParsePos pp;
	GnmExprParseFlags flags = GNM_EXPR_PARSE_DEFAULT;

	c = gnm_solver_constraint_new (sheet);

	parse_pos_init_sheet (&pp, sheet);

	for (; attrs && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "Lcol", &lhs_col) ||
		    gnm_xml_attr_int (attrs, "Lrow", &lhs_row) ||
		    gnm_xml_attr_int (attrs, "Rcol", &rhs_col) ||
		    gnm_xml_attr_int (attrs, "Rrow", &rhs_row) ||
		    gnm_xml_attr_int (attrs, "Cols", &cols) ||
		    gnm_xml_attr_int (attrs, "Rows", &rows))
			old = TRUE;
		else if (gnm_xml_attr_int (attrs, "Type", &type))
			; /* Nothing */
		else if (attr_eq (attrs[0], "lhs")) {
			GnmValue *v = value_new_cellrange_parsepos_str
				(&pp, CXML2C (attrs[1]), flags);
			gnm_solver_constraint_set_lhs (c, v);
		} else if (attr_eq (attrs[0], "rhs")) {
			GnmValue *v = value_new_cellrange_parsepos_str
				(&pp, CXML2C (attrs[1]), flags);
			gnm_solver_constraint_set_rhs (c, v);
		}
	}

	switch (type) {
	case 1: c->type = SolverLE; break;
	case 2: c->type = SolverGE; break;
	case 4: c->type = SolverEQ; break;
	case 8: c->type = SolverINT; break;
	case 16: c->type = SolverBOOL; break;
	default: c->type = SolverLE; break;
	}

	if (old)
		gnm_solver_constraint_set_old (c, c->type,
					       lhs_col, lhs_row,
					       rhs_col, rhs_row,
					       cols, rows);

	sp->constraints = g_slist_append (sp->constraints, c);
}

void
solver_param_read_sax (GsfXMLIn *xin, xmlChar const **attrs)
{
	Sheet *sheet = gnm_xml_in_cur_sheet (xin);
	SolverParameters *sp = sheet->solver_parameters;
	int col = -1, row = -1;
	int ptype;
	GnmParsePos pp;
	gboolean old = FALSE;

	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (SHEET_SOLVER_CONSTR, SHEET_SOLVER_CONSTR, GNM, "Constr", GSF_XML_NO_CONTENT, &solver_constr_start, NULL),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc;

	parse_pos_init_sheet (&pp, sheet);

	for (; attrs && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "ProblemType", &ptype)) {
			sp->problem_type = (SolverProblemType)ptype;
		} else if (attr_eq (attrs[0], "Inputs")) {
			GnmValue *v = value_new_cellrange_parsepos_str
				(&pp,
				 CXML2C (attrs[1]),
				 GNM_EXPR_PARSE_DEFAULT);
			gnm_solver_param_set_input (sp, v);
		} else if (gnm_xml_attr_int (attrs, "TargetCol", &col) ||
			   gnm_xml_attr_int (attrs, "TargetRow", &row)) {
			old = TRUE;
		} else if (attr_eq (attrs[0], "Target")) {
			GnmValue *v = value_new_cellrange_parsepos_str
				(&pp,
				 CXML2C (attrs[1]),
				 GNM_EXPR_PARSE_DEFAULT);
			GnmSheetRange sr;
			GnmCellRef cr;

			if (!v ||
			    (gnm_sheet_range_from_value (&sr, v), !range_is_singleton (&sr.range)))
				continue;
			gnm_cellref_init (&cr, sr.sheet,
					  sr.range.start.col,
					  sr.range.start.row,
					  TRUE);
			gnm_solver_param_set_target (sp, &cr);
		} else if (gnm_xml_attr_int (attrs, "MaxTime", &(sp->options.max_time_sec)) ||
			   gnm_xml_attr_int (attrs, "MaxIter", &(sp->options.max_iter)) ||
			   gnm_xml_attr_bool (attrs, "NonNeg", &(sp->options.assume_non_negative)) ||
			   gnm_xml_attr_bool (attrs, "Discr", &(sp->options.assume_discrete)) ||
			   gnm_xml_attr_bool (attrs, "AutoScale", &(sp->options.automatic_scaling)) ||
			   gnm_xml_attr_bool (attrs, "ShowIter", &(sp->options.show_iter_results)) ||
			   gnm_xml_attr_bool (attrs, "AnswerR", &(sp->options.answer_report)) ||
			   gnm_xml_attr_bool (attrs, "SensitivityR", &(sp->options.sensitivity_report)) ||
			   gnm_xml_attr_bool (attrs, "LimitsR", &(sp->options.limits_report)) ||
			   gnm_xml_attr_bool (attrs, "PerformR", &(sp->options.performance_report)) ||
			   gnm_xml_attr_bool (attrs, "ProgramR", &(sp->options.program_report)))
			; /* Nothing */
	}

	if (old &&
	    col >= 0 && col < gnm_sheet_get_max_cols (sheet) &&
	    row >= 0 && row < gnm_sheet_get_max_rows (sheet)) {
		GnmCellRef cr;
		gnm_cellref_init (&cr, NULL, col, row, TRUE);
		gnm_solver_param_set_target (sp, &cr);
	}

	if (!doc)
		doc = gsf_xml_in_doc_new (dtd, NULL);
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);
}



static SolverResults *
solver_results_init (const SolverParameters *sp)
{
        SolverResults *res     = g_new (SolverResults, 1);

	res->optimal_values    = g_new (gnm_float,  sp->n_variables);
	res->original_values   = g_new (gnm_float,  sp->n_variables);
	res->variable_names    = g_new0 (gchar *,   sp->n_variables);
	res->constraint_names  = g_new0 (gchar *,   sp->n_total_constraints);
	res->shadow_prizes     = g_new0 (gnm_float, sp->n_total_constraints);
	res->slack             = g_new0 (gnm_float, sp->n_total_constraints);
	res->lhs               = g_new0 (gnm_float, sp->n_total_constraints);
	res->rhs               = g_new0 (gnm_float, sp->n_total_constraints);
	res->n_variables       = sp->n_variables;
	res->n_constraints     = sp->n_constraints;
	res->n_nonzeros_in_obj = 0;
	res->n_nonzeros_in_mat = 0;
	res->n_iterations      = 0;
	res->time_user         = 0;
	res->time_system       = 0;
	res->time_real         = 0;
	res->ilp_flag          = FALSE;
	res->target_name       = NULL;
	res->input_cells_array = NULL;
	res->constraints_array = NULL;
	res->obj_coeff         = NULL;
	res->constr_coeff      = NULL;
	res->limits            = NULL;
	res->constr_allowable_increase  =
	        g_new0 (gnm_float, sp->n_total_constraints);
	res->constr_allowable_decrease =
	        g_new0 (gnm_float, sp->n_total_constraints);

	return res;
}

void
solver_results_free (SolverResults *res)
{
        int i;

	for (i = 0; i < res->n_variables; i++)
	        g_free (res->variable_names[i]);
	for (i = 0; i < res->n_constraints; i++)
	        g_free (res->constraint_names[i]);

        g_free (res->optimal_values);
	g_free (res->original_values);
	g_free (res->target_name);
	g_free (res->variable_names);
	g_free (res->constraint_names);
	g_free (res->shadow_prizes);
	g_free (res->input_cells_array);
	if (res->constraints_array)
	        for (i = 0; i < res->n_constraints; i++)
		        gnm_solver_constraint_free (res->constraints_array[i]);
	g_free (res->constraints_array);
	g_free (res->obj_coeff);
	if (res->constr_coeff != NULL)
	        for (i = 0; i < res->n_constraints; i++)
		        g_free (res->constr_coeff[i]);
	g_free (res->constr_coeff);
	g_free (res->limits);
	g_free (res->constr_allowable_increase);
	g_free (res->constr_allowable_decrease);
	g_free (res->slack);
	g_free (res->lhs);
	g_free (res->rhs);

	memset (res, 0xff, sizeof (*res));
	g_free (res);
}

/* ------------------------------------------------------------------------- */

GnmCell *
solver_get_input_var (SolverResults *res, int n)
{
        return res->input_cells_array[n];
}

SolverConstraint*
solver_get_constraint (SolverResults *res, int n)
{
        return res->constraints_array[n];
}

SolverConstraint *
gnm_solver_constraint_new (Sheet *sheet)
{
	SolverConstraint *res = g_new0 (SolverConstraint, 1);
	dependent_managed_init (&res->lhs, sheet);
	dependent_managed_init (&res->rhs, sheet);
	return res;
}

void
gnm_solver_constraint_free (SolverConstraint *c)
{
	gnm_solver_constraint_set_lhs (c, NULL);
	gnm_solver_constraint_set_rhs (c, NULL);
	g_free (c);
}

static SolverConstraint *
gnm_solver_constraint_dup (SolverConstraint *c, Sheet *sheet)
{
	SolverConstraint *res = gnm_solver_constraint_new (sheet);
	res->type = c->type;
	dependent_managed_set_expr (&res->lhs, res->lhs.texpr);
	dependent_managed_set_expr (&res->rhs, res->lhs.texpr);
	return res;
}

gboolean
gnm_solver_constraint_has_rhs (SolverConstraint const *c)
{
	g_return_val_if_fail (c != NULL, FALSE);

	switch (c->type) {
	case SolverLE:
	case SolverGE:
	case SolverEQ:
		return TRUE;
	case SolverINT:
	case SolverBOOL:
	default:
		return FALSE;
	}
}

gboolean
gnm_solver_constraint_valid (SolverConstraint const *c,
			     SolverParameters const *sp)
{
	GnmValue const *lhs;

	g_return_val_if_fail (c != NULL, FALSE);

	lhs = gnm_solver_constraint_get_lhs (c);
	if (lhs == NULL || lhs->type != VALUE_CELLRANGE)
		return FALSE;

	if (gnm_solver_constraint_has_rhs (c)) {
		GnmValue const *rhs = gnm_solver_constraint_get_lhs (c);
		if (rhs == NULL)
			return FALSE;
		if (rhs->type == VALUE_CELLRANGE) {
			GnmRange rl, rr;

			range_init_value (&rl, lhs);
			range_init_value (&rr, rhs);

			if (range_width (&rl) != range_width (&rr) ||
			    range_height (&rl) != range_height (&rr))
				return FALSE;
		} else if (VALUE_IS_FLOAT (rhs)) {
			/* Nothing */
		} else
			return FALSE;
	}

	switch (c->type) {
	case SolverINT:
	case SolverBOOL: {
		GnmValue const *vinput = gnm_solver_param_get_input (sp);
		GnmSheetRange sr_input, sr_c;

		if (!vinput)
			break; /* No need to blame contraint.  */

		gnm_sheet_range_from_value (&sr_input, vinput);
		gnm_sheet_range_from_value (&sr_c, lhs);

		if (eval_sheet (sr_input.sheet, sp->sheet) !=
		    eval_sheet (sr_c.sheet, sp->sheet) ||
		    !range_contained (&sr_c.range, &sr_input.range))
			return FALSE;
		break;
	}

	default:
		break;
	}

	return TRUE;
}

static int
gnm_solver_constraint_get_size (SolverConstraint const *c)
{
	GnmRange r;
	GnmValue const *lhs;

	g_return_val_if_fail (c != NULL, 0);

	lhs = gnm_solver_constraint_get_lhs (c);
	if (lhs) {
		if (VALUE_IS_FLOAT (lhs))
			return 1;
		if (lhs->type == VALUE_CELLRANGE) {
			range_init_value (&r, lhs);
			return range_width (&r) * range_height (&r);
		}
	}

	return 0;
}

GnmValue const *
gnm_solver_constraint_get_lhs (SolverConstraint const *c)
{
	GnmExprTop const *texpr = c->lhs.texpr;
	return texpr ? gnm_expr_top_get_constant (texpr) : NULL;
}

void
gnm_solver_constraint_set_lhs (SolverConstraint *c, GnmValue *v)
{
	/* Takes ownership.  */
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&c->lhs, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

GnmValue const *
gnm_solver_constraint_get_rhs (SolverConstraint const *c)
{
	GnmExprTop const *texpr = c->rhs.texpr;
	return texpr ? gnm_expr_top_get_constant (texpr) : NULL;
}

void
gnm_solver_constraint_set_rhs (SolverConstraint *c, GnmValue *v)
{
	/* Takes ownership.  */
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&c->rhs, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}


gboolean
gnm_solver_constraint_get_part (SolverConstraint const *c,
				SolverParameters const *sp, int i,
				GnmCell **lhs, gnm_float *cl,
				GnmCell **rhs, gnm_float *cr)
{
	GnmRange r;
	int h, w, dx, dy;
	GnmValue const *vl, *vr;

	if (cl)	*cl = 0;
	if (cr)	*cr = 0;
	if (lhs) *lhs = NULL;
	if (rhs) *rhs = NULL;

	if (!gnm_solver_constraint_valid (c, sp))
		return FALSE;

	vl = gnm_solver_constraint_get_lhs (c);
	vr = gnm_solver_constraint_get_rhs (c);

	range_init_value (&r, vl);
	w = range_width (&r);
	h = range_height (&r);

	dy = i / w;
	dx = i % w;
	if (dy >= h)
		return FALSE;

	if (lhs)
		*lhs = sheet_cell_get (sp->sheet,
				       r.start.col + dx, r.start.row + dy);

	if (gnm_solver_constraint_has_rhs (c)) {
		if (VALUE_IS_FLOAT (vr)) {
			if (cr)
				*cr = value_get_as_float (vr);
		} else {
			range_init_value (&r, vr);
			if (rhs)
				*rhs = sheet_cell_get (sp->sheet,
						       r.start.col + dx,
						       r.start.row + dy);
		}
	}

	return TRUE;
}

static gboolean
gnm_solver_constraint_get_part_val (SolverConstraint const *c,
				    SolverParameters const *sp, int i,
				    GnmValue **lhs, GnmValue **rhs)
{
	GnmRange r;
	int h, w, dx, dy;
	GnmValue const *vl, *vr;

	if (lhs) *lhs = NULL;
	if (rhs) *rhs = NULL;

	if (!gnm_solver_constraint_valid (c, sp))
		return FALSE;

	vl = gnm_solver_constraint_get_lhs (c);
	vr = gnm_solver_constraint_get_rhs (c);

	range_init_value (&r, vl);
	w = range_width (&r);
	h = range_height (&r);

	dy = i / w;
	dx = i % w;
	if (dy >= h)
		return FALSE;

	r.start.col += dx;
	r.start.row += dy;
	r.end = r.start;
	if (lhs) *lhs = value_new_cellrange_r (sp->sheet, &r);

	if (rhs && gnm_solver_constraint_has_rhs (c)) {
		if (VALUE_IS_FLOAT (vr)) {
			*rhs = value_dup (vr);
		} else {
			range_init_value (&r, vr);
			r.start.col += dx;
			r.start.row += dy;
			r.end = r.start;
			*rhs = value_new_cellrange_r (sp->sheet, &r);
		}
	}

	return TRUE;
}

void
gnm_solver_constraint_set_old (SolverConstraint *c,
			       SolverConstraintType type,
			       int lhs_col, int lhs_row,
			       int rhs_col, int rhs_row,
			       int cols, int rows)
{
	GnmRange r;

	c->type = type;

	range_init (&r,
		    lhs_col, lhs_row,
		    lhs_col + (cols - 1), lhs_row + (rows - 1));
	gnm_solver_constraint_set_lhs
		(c, value_new_cellrange_r (NULL, &r));

	if (gnm_solver_constraint_has_rhs (c)) {
		range_init (&r,
			    rhs_col, rhs_row,
			    rhs_col + (cols - 1), rhs_row + (rows - 1));
		gnm_solver_constraint_set_rhs
			(c, value_new_cellrange_r (NULL, &r));
	} else
		gnm_solver_constraint_set_rhs (c, NULL);
}

/* ------------------------------------------------------------------------- */

void
gnm_solver_constraint_side_as_str (SolverConstraint const *c,
				   Sheet const *sheet,
				   GString *buf, gboolean lhs)
{
	GnmExprTop const *texpr;

	texpr = lhs ? c->lhs.texpr : c->rhs.texpr;
	if (texpr) {
		GnmConventionsOut out;
		GnmParsePos pp;

		out.accum = buf;
		out.pp = parse_pos_init_sheet (&pp, sheet);
		out.convs = sheet->convs;
		gnm_expr_top_as_gstring (texpr, &out);
	} else
		g_string_append (buf,
				 value_error_name (GNM_ERROR_REF,
						   sheet->convs->output.translated));
}

char *
gnm_solver_constraint_as_str (SolverConstraint const *c, Sheet *sheet)
{
	const char * const type_str[] =	{
		"\xe2\x89\xa4" /* "<=" */,
		"\xe2\x89\xa5" /* ">=" */,
		"=", "Int", "Bool"
	};
	GString *buf = g_string_new (NULL);

	gnm_solver_constraint_side_as_str (c, sheet, buf, TRUE);
	g_string_append_c (buf, ' ');
	g_string_append (buf, type_str[c->type]);
	if (gnm_solver_constraint_has_rhs (c)) {
		g_string_append_c (buf, ' ');
		gnm_solver_constraint_side_as_str (c, sheet, buf, FALSE);
	}

	return g_string_free (buf, FALSE);
}

/* ------------------------------------------------------------------------- */

/*
 * This function implements a simple way to determine linear
 * coefficents.  For example, if we have the minization target cell in
 * `target' and the first input variable in `change', this function
 * returns the coefficent of the first variable of the objective
 * function.
 */
static gnm_float
get_lp_coeff (GnmCell *target, GnmCell *change)
{
        gnm_float x0, x1;

	gnm_cell_set_value (change, value_new_float (1));
	cell_queue_recalc (change);
	gnm_cell_eval (target);
	x1 = value_get_as_float (target->value);

	gnm_cell_set_value (change, value_new_float (0));
	cell_queue_recalc (change);
	gnm_cell_eval (target);
	x0 = value_get_as_float (target->value);

	return x1 - x0;
}

/*
 * Saves the original values of the input variables into a
 * SolverResults entry.
 */
static void
save_original_values (SolverResults          *res,
		      const SolverParameters *param,
		      Sheet                  *sheet)
{
	GSList *input_cells = gnm_solver_param_get_input_cells (param);
	GSList *l;
	GnmCell  *cell;
	int i;

	for (i = 0, l = input_cells; l; i++, l = l->next) {
	        GnmCell *cell = l->data;

		if (cell == NULL || cell->value == NULL)
		        res->original_values[i] = 0;
		else
		        res->original_values[i] =
			        value_get_as_float (cell->value);
	}

	g_slist_free (input_cells);

	cell = gnm_solver_param_get_target_cell (param);
	res->original_value_of_obj_fn = value_get_as_float (cell->value);
}

/************************************************************************
 */

static int
get_col_nbr (SolverResults *res, GnmValue const *v)
{
        int  i;
	GnmRange r;

	range_init_value (&r, v);

	for (i = 0; i < res->param->n_variables; i++) {
		GnmCell *cell = solver_get_input_var (res, i);
		if (gnm_cellpos_equal (&r.start, &cell->pos))
		        return i;
	}
	return -1;
}

/* ------------------------------------------------------------------------- */

static void
clear_input_vars (int n_variables, SolverResults *res)
{
        int i;

	for (i = 0; i < n_variables; i++)
		sheet_cell_set_value (solver_get_input_var (res, i),
				      value_new_float (0.0));
}

/*
 * Initializes the program according to the information given in the
 * solver dialog and the related sheet.  After the call, the LP
 * program is ready to run.
 */
static SolverProgram
lp_qp_solver_init (Sheet *sheet, const SolverParameters *param,
		   SolverResults *res, const SolverLPAlgorithm *alg,
		   gnm_float start_time, GTimeVal start, GError **err)
{
        SolverProgram     program;
	GnmCell          *target;
	gnm_float         x;
	int               i, n, ind;

	/* Initialize the SolverProgram structure. */
	program = alg->init_fn (param);

	/* Set up the objective function coefficients. */
	target = gnm_solver_param_get_target_cell (param);
	clear_input_vars (param->n_variables, res);

	gnm_cell_eval (target);

	if (param->options.model_type == SolverLPModel) {
	        for (i = 0; i < param->n_variables; i++) {
		        x = get_lp_coeff (target,
					  solver_get_input_var (res, i));
			if (x != 0) {
			        alg->set_obj_fn (program, i, x);
				res->n_nonzeros_in_obj += 1;
				res->obj_coeff[i] = x;
			}
		}
	} else {
	        /* FIXME: Init qp */
	}

	/* Add constraints. */
	for (i = ind = 0; i < param->n_total_constraints; i++) {
	        SolverConstraint const *c = solver_get_constraint (res, i);
		GTimeVal cur_time;
		const GnmValue *lval;
		const GnmValue *rval;
		gnm_float lx, rx;

		gnm_solver_constraint_get_part (c, param, 0,
						&target, NULL,
						NULL, NULL);
		if (target) {
			gnm_cell_eval (target);
			lval = target->value;
		} else
			lval = NULL;

		/* Check that LHS is a number type. */
		if (lval == NULL || !VALUE_IS_NUMBER (lval)) {
			g_set_error (err, go_error_invalid (), 0,
				     _("The LHS cells should contain formulas "
				       "that yield proper numerical values.  "
				       "Specify valid LHS entries."));
			return NULL;
		}
		lx = value_get_as_float (lval);

		if (c->type == SolverINT) {
		        n = get_col_nbr (res, gnm_solver_constraint_get_lhs (c));
			if (n == -1)
			        return NULL;
		        alg->set_int_fn (program, n);
			res->ilp_flag = TRUE;
		        continue;
		}
		if (c->type == SolverBOOL) {
		        n = get_col_nbr (res, gnm_solver_constraint_get_lhs (c));
			if (n == -1)
			        return NULL;
		        alg->set_bool_fn (program, n);
			res->ilp_flag = TRUE;
		        continue;
		}
		clear_input_vars (param->n_variables, res);
		for (n = 0; n < param->n_variables; n++) {
		        x = get_lp_coeff (target,
					  solver_get_input_var (res, n));
			if (x != 0) {
			        res->n_nonzeros_in_mat += 1;
				alg->set_constr_mat_fn (program, n, ind, x);
				res->constr_coeff[i][n] = x;
			}
		}

		gnm_solver_constraint_get_part (c, param, 0,
						NULL, NULL, &target, NULL);
		if (target) {
			gnm_cell_eval (target);
			rval = target->value;
		} else
			rval = NULL;

		/* Check that RHS is a number type. */
		if (rval == NULL || !VALUE_IS_NUMBER (rval)) {
			g_set_error (err, go_error_invalid (), 0,
				     _("The RHS cells should contain proper "
				       "numerical values only.  Specify valid "
				       "RHS entries."));
			return NULL;
		}
		rx = value_get_as_float (rval);

		x = rx - lx;
		alg->set_constr_fn (program, ind, c->type, x);
		res->rhs[i] = x;
		ind++;

		/* Check that max time has not elapsed. */
		g_get_current_time (&cur_time);
		if (cur_time.tv_sec - start.tv_sec >
		    param->options.max_time_sec) {
			g_set_error (err, go_error_invalid (), 0,
				     SOLVER_MAX_TIME_ERR);
			return NULL;
		}

	}

	/* Set up the problem type. */
	switch (param->problem_type) {
	case SolverMinimize:
	        alg->minim_fn (program);
	        break;
	case SolverMaximize:
	        alg->maxim_fn (program);
	        break;
	case SolverEqualTo:
		g_set_error (err, go_error_invalid (), 0,
			     _("EqualTo models are not supported yet.  "
			       "Please use Min or Max"));
	        return NULL; /* FIXME: Equal to feature not yet implemented. */
	default:
		g_warning ("unknown problem type %d", param->problem_type);
	        return NULL;
	}

	/* Set options. */
	if (alg->set_option_fn (program, SolverOptAutomaticScaling,
				&(param->options.automatic_scaling),
				NULL, NULL)) {
		g_set_error (err, go_error_invalid (), 0,
			     _("Failure setting automatic scaling with this solver, try a different algorithm."));
	        return NULL;
	}
	if (alg->set_option_fn (program, SolverOptMaxIter, NULL, NULL,
				&(param->options.max_iter))) {
		g_set_error (err, go_error_invalid (), 0,
			     _("Failure setting the maximum number of iterations with this solver, try a different algorithm."));
	        return NULL;
	}
	if (alg->set_option_fn (program, SolverOptMaxTimeSec, NULL, &start_time,
				&(param->options.max_time_sec))) {
		g_set_error (err, go_error_invalid (), 0,
			     _("Failure setting the maximum solving time with this solver, try a different algorithm."));
	        return NULL;
	}

	/* Assume Integer (Discrete) button. */
	if (param->options.assume_discrete) {
	        for (i = 0; i < param->n_variables; i++)
		        alg->set_int_fn (program, i);
		res->ilp_flag = TRUE;
	}

	alg->print_fn (program);

	return program;
}

/*
 * Returns TRUE if an error is found in the program definition.
 * Otherwise, return FALSE (Ok).
 */
static gboolean
check_program_definition_failures (Sheet            *sheet,
				   SolverParameters *param,
				   SolverResults    **res,
				   GError **err)
{
	GSList           *inputs;
	GSList           *c;
	int               i;
	GnmCell          **input_cells_array;
	SolverConstraint **constraints_array;
	GSList *input_cells = gnm_solver_param_get_input_cells (param);

	param->n_variables = 0;

	/*
	 * Checks for the Input cells.
	 */

	/* Count the nbr of the input cells and check that each cell
	 * is in the list only once. */
 	for (inputs = input_cells; inputs ; inputs = inputs->next) {
	        GnmCell *cell = inputs->data;

		/* Check that the cell contains a number or is empty. */
		if (! (cell->value == NULL || VALUE_IS_EMPTY (cell->value)
		       || VALUE_IS_NUMBER (cell->value))) {
			g_set_error (err, go_error_invalid (), 0,
				     _("Some of the input cells contain "
				       "non-numeric values.  Specify a valid "
				       "input range."));
			g_slist_free (input_cells);
			return TRUE;
		}

	        param->n_variables += 1;
	}
	input_cells_array = g_new (GnmCell *, param->n_variables);
	i = 0;
 	for (inputs = input_cells; inputs ; inputs = inputs->next)
	        input_cells_array[i++] = (GnmCell *) inputs->data;
	g_slist_free (input_cells);

	param->n_constraints      = 0;
	param->n_int_constraints  = 0;
	param->n_bool_constraints = 0;
	i = 0;
 	for (c = param->constraints; c ; c = c->next) {
	        SolverConstraint *sc = c->data;
		int N = gnm_solver_constraint_get_size (sc);

		if (sc->type == SolverINT)
		        param->n_int_constraints += N;
		else if (sc->type == SolverBOOL)
		        param->n_bool_constraints += N;
		else
		        param->n_constraints += N;
	}
	param->n_total_constraints = param->n_constraints +
	        param->n_int_constraints + param->n_bool_constraints;
	constraints_array = g_new (SolverConstraint *,
				   param->n_total_constraints);
	i = 0;
 	for (c = param->constraints; c ; c = c->next) {
	        SolverConstraint *sc = c->data;
		int j;
		GnmValue *lhs, *rhs;

		for (j = 0;
		     gnm_solver_constraint_get_part_val (sc, param, j,
							 &lhs, &rhs);
		     j++) {
			SolverConstraint *nc =
				gnm_solver_constraint_new (sheet);
			nc->type = sc->type;
			gnm_solver_constraint_set_lhs (nc, lhs);
			gnm_solver_constraint_set_rhs (nc, rhs);
		        constraints_array[i++] = nc;
		}
	}

	*res = solver_results_init (param);

	(*res)->param = param;
	(*res)->input_cells_array = input_cells_array;
	(*res)->constraints_array = constraints_array;
	(*res)->obj_coeff = g_new0 (gnm_float, param->n_variables);

	(*res)->constr_coeff = g_new0 (gnm_float *, param->n_total_constraints);
	for (i = 0; i < param->n_total_constraints; i++)
	        (*res)->constr_coeff[i] = g_new0 (gnm_float,
						  param->n_variables);
	(*res)->limits = g_new (SolverLimits, param->n_variables);

	return FALSE;  /* Everything Ok. */
}

static SolverResults *
solver_run (WorkbookControl *wbc, Sheet *sheet,
	    const SolverLPAlgorithm *alg, GError **err)
{
	SolverParameters  *param = sheet->solver_parameters;
	SolverProgram     program;
	SolverResults     *res;
	GnmValue const *  vinput = gnm_solver_param_get_input (param);
	GnmSheetRange sr;
	GOUndo *undo;
	GTimeVal          start, end;
#if defined(HAVE_TIMES) && defined(HAVE_SYSCONF)
	struct tms        buf;

	times (&buf);
#warning what is the equivalent of times for win32
#endif

	g_get_current_time (&start);

	if (!gnm_solver_param_valid (param, err))
		return NULL;

	if (check_program_definition_failures (sheet, param, &res, err))
	        return NULL;

#if defined(HAVE_TIMES) && defined(HAVE_SYSCONF)
	res->time_user   = - buf.tms_utime / (gnm_float) sysconf (_SC_CLK_TCK);
	res->time_system = - buf.tms_stime / (gnm_float) sysconf (_SC_CLK_TCK);
#else
	res->time_user = 0;
	res->time_system = 0;
#warning TODO
#endif
	res->time_real   = - (start.tv_sec +
			      start.tv_usec / (gnm_float) G_USEC_PER_SEC);

	gnm_sheet_range_from_value (&sr, vinput);
	if (!sr.sheet) sr.sheet = sheet;
	undo = clipboard_copy_range_undo (sr.sheet, &sr.range);

	save_original_values (res, param, sheet);

	program              = lp_qp_solver_init (sheet, param, res, alg,
						  -res->time_real, start,
						  err);
	if (program == NULL)
		goto fail;

        res->status = alg->solve_fn (program);
	g_get_current_time (&end);
#if defined(HAVE_TIMES) && defined(HAVE_SYSCONF)
	times (&buf);
	res->time_user   += buf.tms_utime / (gnm_float) sysconf (_SC_CLK_TCK);
	res->time_system += buf.tms_stime / (gnm_float) sysconf (_SC_CLK_TCK);
#else
#warning TODO
	res->time_user   = 0;
	res->time_system = 0;
#endif
	res->time_real   += end.tv_sec + end.tv_usec /
	        (gnm_float) G_USEC_PER_SEC;
	res->n_iterations = alg->get_iterations_fn (program);

	solver_prepare_reports (program, res, sheet);
	if (res->status == SolverOptimal) {
		GOUndo *redo;
	        if (solver_prepare_reports_success (program, res, sheet)) {
		        alg->remove_fn (program);
			goto fail;
		}
		redo = clipboard_copy_range_undo (sr.sheet, &sr.range);
		cmd_solver (wbc, undo, redo);
	} else {
		go_undo_undo (undo);
		g_object_unref (undo);
		undo = NULL;
	}

	alg->remove_fn (program);

	return res;

 fail:
	if (res)
		solver_results_free (res);
	if (undo)
		g_object_unref (undo);
	return NULL;
}

SolverResults *
solver (WorkbookControl *wbc, Sheet *sheet, GError **err)
{
	const SolverLPAlgorithm *alg = NULL;
	SolverParameters  *param = sheet->solver_parameters;

        switch (sheet->solver_parameters->options.model_type) {
	case SolverLPModel:
	        alg = &lp_algorithm [0 /* param->options.algorithm */];
		break;
	case SolverQPModel:
	        alg = &qp_algorithm [0 /* param->options.algorithm */];
		break;
	case SolverNLPModel:
	        return NULL;

	default :
		g_assert_not_reached ();
	}

	return solver_run (wbc, sheet, alg, err);
}


SolverParameters *
gnm_solver_param_dup (const SolverParameters *src_param, Sheet *new_sheet)
{
	SolverParameters *dst_param = gnm_solver_param_new (new_sheet);
	GSList           *constraints;

	dst_param->problem_type = src_param->problem_type;
	dependent_managed_set_expr (&dst_param->target,
				    src_param->target.texpr);
	dependent_managed_set_expr (&dst_param->input,
				    src_param->input.texpr);

	g_free (dst_param->options.scenario_name);
	dst_param->options = src_param->options;
	dst_param->options.scenario_name = g_strdup (src_param->options.scenario_name);
	/* Had there been any non-scalar options, we'd copy them here.  */

	/* Copy the constraints */
	for (constraints = src_param->constraints; constraints;
	     constraints = constraints->next) {
		SolverConstraint *old = constraints->data;
		SolverConstraint *new = gnm_solver_constraint_dup (old, new_sheet);

		dst_param->constraints =
		        g_slist_prepend (dst_param->constraints, new);
	}
	dst_param->constraints = g_slist_reverse (dst_param->constraints);

	dst_param->n_constraints       = src_param->n_constraints;
	dst_param->n_variables         = src_param->n_variables;
	dst_param->n_int_constraints   = src_param->n_int_constraints;
	dst_param->n_bool_constraints  = src_param->n_bool_constraints;
	dst_param->n_total_constraints = src_param->n_total_constraints;

	return dst_param;
}
