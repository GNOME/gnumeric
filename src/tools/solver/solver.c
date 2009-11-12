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
