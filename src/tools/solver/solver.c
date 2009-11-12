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

static void
solver_constr_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	int type = 0;
	GnmSolverConstraint *c;
	Sheet *sheet = gnm_xml_in_cur_sheet (xin);
	GnmSolverParameters *sp = sheet->solver_parameters;
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
	case 1: c->type = GNM_SOLVER_LE; break;
	case 2: c->type = GNM_SOLVER_GE; break;
	case 4: c->type = GNM_SOLVER_EQ; break;
	case 8: c->type = GNM_SOLVER_INTEGER; break;
	case 16: c->type = GNM_SOLVER_BOOLEAN; break;
	default: c->type = GNM_SOLVER_LE; break;
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
	GnmSolverParameters *sp = sheet->solver_parameters;
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
			sp->problem_type = (GnmSolverProblemType)ptype;
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

gchar *
solver_reports (WorkbookControl *wbc, Sheet *sheet, SolverResults *res,
		gboolean answer, gboolean sensitivity, gboolean limits,
		gboolean performance, gboolean program, gboolean dual)
{
	return NULL;
}

/* ------------------------------------------------------------------------- */

GnmCell *
solver_get_input_var (SolverResults *res, int n)
{
        return res->input_cells_array[n];
}

GnmSolverConstraint*
solver_get_constraint (SolverResults *res, int n)
{
        return res->constraints_array[n];
}

/* ------------------------------------------------------------------------- */
