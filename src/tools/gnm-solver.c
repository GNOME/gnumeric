#include <gnumeric-config.h>
#include "gnumeric.h"
#include "value.h"
#include "cell.h"
#include "expr.h"
#include "sheet.h"
#include "workbook.h"
#include "ranges.h"
#include "gutils.h"
#include "gnm-solver.h"
#include "workbook-view.h"
#include "workbook-control.h"
#include "gnm-marshalers.h"
#include "dao.h"
#include "gnm-i18n.h"
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef G_OS_WIN32
#include <windows.h>
#ifndef WIFEXITED
#define WIFEXITED(x) ((x) != STILL_ACTIVE)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(x) (x)
#endif
#endif

/* ------------------------------------------------------------------------- */

gboolean
gnm_solver_debug (void)
{
	static int debug = -1;
	if (debug == -1)
		debug = gnm_debug_flag ("solver");
	return debug;
}

static char *
gnm_solver_cell_name (GnmCell const *cell, Sheet *origin)
{
	GnmConventionsOut out;
	GnmCellRef cr;
	GnmParsePos pp;

	gnm_cellref_init (&cr, cell->base.sheet,
			  cell->pos.col, cell->pos.row,
			  TRUE);
	out.accum = g_string_new (NULL);
	out.pp = parse_pos_init_sheet (&pp, origin);
	out.convs = origin->convs;
	cellref_as_string (&out, &cr, cell->base.sheet == origin);
	return g_string_free (out.accum, FALSE);
}

/* ------------------------------------------------------------------------- */

GType
gnm_solver_status_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_SOLVER_STATUS_READY,
			  "GNM_SOLVER_STATUS_READY",
			  "ready"
			},
			{ GNM_SOLVER_STATUS_PREPARING,
			  "GNM_SOLVER_STATUS_PREPARING",
			  "preparing"
			},
			{ GNM_SOLVER_STATUS_PREPARED,
			  "GNM_SOLVER_STATUS_PREPARED",
			  "prepared"
			},
			{ GNM_SOLVER_STATUS_RUNNING,
			  "GNM_SOLVER_STATUS_RUNNING",
			  "running"
			},
			{ GNM_SOLVER_STATUS_DONE,
			  "GNM_SOLVER_STATUS_DONE",
			  "done"
			},
			{ GNM_SOLVER_STATUS_ERROR,
			  "GNM_SOLVER_STATUS_ERROR",
			  "error"
			},
			{ GNM_SOLVER_STATUS_CANCELLED,
			  "GNM_SOLVER_STATUS_CANCELLED",
			  "cancelled"
			},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmSolverStatus", values);
	}
	return etype;
}

GType
gnm_solver_problem_type_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_SOLVER_MINIMIZE,
			  "GNM_SOLVER_MINIMIZE",
			  "minimize"
			},
			{ GNM_SOLVER_MAXIMIZE,
			  "GNM_SOLVER_MAXIMIZE",
			  "maximize"
			},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmSolverProblemType", values);
	}
	return etype;
}

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_solver_parent_class;

GnmSolverConstraint *
gnm_solver_constraint_new (Sheet *sheet)
{
	GnmSolverConstraint *res = g_new0 (GnmSolverConstraint, 1);
	dependent_managed_init (&res->lhs, sheet);
	dependent_managed_init (&res->rhs, sheet);
	return res;
}

void
gnm_solver_constraint_free (GnmSolverConstraint *c)
{
	gnm_solver_constraint_set_lhs (c, NULL);
	gnm_solver_constraint_set_rhs (c, NULL);
	g_free (c);
}

GnmSolverConstraint *
gnm_solver_constraint_dup (GnmSolverConstraint *c, Sheet *sheet)
{
	GnmSolverConstraint *res = gnm_solver_constraint_new (sheet);
	res->type = c->type;
	dependent_managed_set_expr (&res->lhs, c->lhs.texpr);
	dependent_managed_set_expr (&res->rhs, c->rhs.texpr);
	return res;
}

gboolean
gnm_solver_constraint_equal (GnmSolverConstraint const *a,
			     GnmSolverConstraint const *b)
{
	return (a->type == b->type &&
		gnm_expr_top_equal (a->lhs.texpr, b->lhs.texpr) &&
		(!gnm_solver_constraint_has_rhs (a) ||
		 gnm_expr_top_equal (a->rhs.texpr, b->rhs.texpr)));
}

gboolean
gnm_solver_constraint_has_rhs (GnmSolverConstraint const *c)
{
	g_return_val_if_fail (c != NULL, FALSE);

	switch (c->type) {
	case GNM_SOLVER_LE:
	case GNM_SOLVER_GE:
	case GNM_SOLVER_EQ:
		return TRUE;
	case GNM_SOLVER_INTEGER:
	case GNM_SOLVER_BOOLEAN:
	default:
		return FALSE;
	}
}

gboolean
gnm_solver_constraint_valid (GnmSolverConstraint const *c,
			     GnmSolverParameters const *sp)
{
	GnmValue const *lhs;

	g_return_val_if_fail (c != NULL, FALSE);

	lhs = gnm_solver_constraint_get_lhs (c);
	if (lhs == NULL || lhs->type != VALUE_CELLRANGE)
		return FALSE;

	if (gnm_solver_constraint_has_rhs (c)) {
		GnmValue const *rhs = gnm_solver_constraint_get_rhs (c);
		if (rhs == NULL)
			return FALSE;
		if (rhs->type == VALUE_CELLRANGE) {
			GnmSheetRange srl, srr;

			gnm_sheet_range_from_value (&srl, lhs);
			gnm_sheet_range_from_value (&srr, rhs);

			if (range_width (&srl.range) != range_width (&srr.range) ||
			    range_height (&srl.range) != range_height (&srr.range))
				return FALSE;
		} else if (VALUE_IS_FLOAT (rhs)) {
			/* Nothing */
		} else
			return FALSE;
	}

	switch (c->type) {
	case GNM_SOLVER_INTEGER:
	case GNM_SOLVER_BOOLEAN: {
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
gnm_solver_constraint_get_lhs (GnmSolverConstraint const *c)
{
	GnmExprTop const *texpr = c->lhs.texpr;
	return texpr ? gnm_expr_top_get_constant (texpr) : NULL;
}

void
gnm_solver_constraint_set_lhs (GnmSolverConstraint *c, GnmValue *v)
{
	/* Takes ownership.  */
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&c->lhs, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

GnmValue const *
gnm_solver_constraint_get_rhs (GnmSolverConstraint const *c)
{
	GnmExprTop const *texpr = c->rhs.texpr;
	return texpr ? gnm_expr_top_get_constant (texpr) : NULL;
}

void
gnm_solver_constraint_set_rhs (GnmSolverConstraint *c, GnmValue *v)
{
	/* Takes ownership.  */
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&c->rhs, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

gboolean
gnm_solver_constraint_get_part (GnmSolverConstraint const *c,
				GnmSolverParameters const *sp, int i,
				GnmCell **lhs, gnm_float *cl,
				GnmCell **rhs, gnm_float *cr)
{
	GnmSheetRange sr;
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

	gnm_sheet_range_from_value (&sr, vl);
	w = range_width (&sr.range);
	h = range_height (&sr.range);

	dy = i / w;
	dx = i % w;
	if (dy >= h)
		return FALSE;

	if (lhs)
		*lhs = sheet_cell_get (eval_sheet (sr.sheet, sp->sheet),
				       sr.range.start.col + dx,
				       sr.range.start.row + dy);

	if (!gnm_solver_constraint_has_rhs (c)) {
		/* Nothing */
	} else if (VALUE_IS_FLOAT (vr)) {
		if (cr)
			*cr = value_get_as_float (vr);
	} else {
		gnm_sheet_range_from_value (&sr, vr);
		if (rhs)
			*rhs = sheet_cell_get (eval_sheet (sr.sheet, sp->sheet),
					       sr.range.start.col + dx,
					       sr.range.start.row + dy);
	}

	return TRUE;
}

void
gnm_solver_constraint_set_old (GnmSolverConstraint *c,
			       GnmSolverConstraintType type,
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

void
gnm_solver_constraint_side_as_str (GnmSolverConstraint const *c,
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
gnm_solver_constraint_as_str (GnmSolverConstraint const *c, Sheet *sheet)
{
	const char * const type_str[] =	{
		"\xe2\x89\xa4" /* "<=" */,
		"\xe2\x89\xa5" /* ">=" */,
		"=",
		N_("Int"),
		N_("Bool")
	};
	const char *type = type_str[c->type];
	gboolean translate = (c->type >= GNM_SOLVER_INTEGER);
	GString *buf = g_string_new (NULL);

	gnm_solver_constraint_side_as_str (c, sheet, buf, TRUE);
	g_string_append_c (buf, ' ');
	g_string_append (buf, translate ? _(type) : type);
	if (gnm_solver_constraint_has_rhs (c)) {
		g_string_append_c (buf, ' ');
		gnm_solver_constraint_side_as_str (c, sheet, buf, FALSE);
	}

	return g_string_free (buf, FALSE);
}

/* ------------------------------------------------------------------------- */

enum {
	SOLP_PROP_0,
	SOLP_PROP_SHEET,
	SOLP_PROP_PROBLEM_TYPE
};

static GObjectClass *gnm_solver_param_parent_class;

GnmSolverParameters *
gnm_solver_param_new (Sheet *sheet)
{
	return g_object_new (GNM_SOLVER_PARAMETERS_TYPE,
			     "sheet", sheet,
			     NULL);
}

GnmSolverParameters *
gnm_solver_param_dup (GnmSolverParameters *src, Sheet *new_sheet)
{
	GnmSolverParameters *dst = gnm_solver_param_new (new_sheet);
	GSList *l;

	dst->problem_type = src->problem_type;
	dependent_managed_set_expr (&dst->target, src->target.texpr);
	dependent_managed_set_expr (&dst->input, src->input.texpr);

	dst->options.max_time_sec = src->options.max_time_sec;
	dst->options.max_iter = src->options.max_iter;
	dst->options.model_type = src->options.model_type;
	dst->options.assume_non_negative = src->options.assume_non_negative;
	dst->options.assume_discrete = src->options.assume_discrete;
	dst->options.automatic_scaling = src->options.automatic_scaling;
	dst->options.program_report = src->options.program_report;
	dst->options.add_scenario = src->options.add_scenario;

	g_free (dst->options.scenario_name);
	dst->options.scenario_name = g_strdup (src->options.scenario_name);

	gnm_solver_param_set_algorithm (dst, src->options.algorithm);

	/* Copy the constraints */
	for (l = src->constraints; l; l = l->next) {
		GnmSolverConstraint *old = l->data;
		GnmSolverConstraint *new =
			gnm_solver_constraint_dup (old, new_sheet);

		dst->constraints = g_slist_prepend (dst->constraints, new);
	}
	dst->constraints = g_slist_reverse (dst->constraints);

	return dst;
}

gboolean
gnm_solver_param_equal (GnmSolverParameters const *a,
			GnmSolverParameters const *b)
{
	GSList *la, *lb;

	if (a->sheet != b->sheet ||
	    a->problem_type != b->problem_type ||
	    !gnm_expr_top_equal (a->target.texpr, b->target.texpr) ||
	    !gnm_expr_top_equal (a->input.texpr, b->input.texpr) ||
	    a->options.max_time_sec != b->options.max_time_sec ||
	    a->options.max_iter != b->options.max_iter ||
	    a->options.algorithm != b->options.algorithm ||
	    a->options.model_type != b->options.model_type ||
            a->options.assume_non_negative != b->options.assume_non_negative ||
            a->options.assume_discrete != b->options.assume_discrete ||
            a->options.automatic_scaling != b->options.automatic_scaling ||
            a->options.program_report != b->options.program_report ||
            a->options.add_scenario != b->options.add_scenario ||
	    strcmp (a->options.scenario_name, b->options.scenario_name))
		return FALSE;

	for (la = a->constraints, lb = b->constraints;
	     la && lb;
	     la = la->next, lb = lb->next) {
		GnmSolverConstraint *ca = la->data;
		GnmSolverConstraint *cb = lb->data;
		if (!gnm_solver_constraint_equal (ca, cb))
			return FALSE;
	}
	return la == lb;
}

GnmValue const *
gnm_solver_param_get_input (GnmSolverParameters const *sp)
{
	return sp->input.texpr
		? gnm_expr_top_get_constant (sp->input.texpr)
		: NULL;
}

void
gnm_solver_param_set_input (GnmSolverParameters *sp, GnmValue *v)
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
gnm_solver_param_get_input_cells (GnmSolverParameters const *sp)
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
gnm_solver_param_set_target (GnmSolverParameters *sp, GnmCellRef const *cr)
{
	if (cr) {
		GnmExprTop const *texpr;
		GnmCellRef cr2 = *cr;
		/* Make reference absolute to avoid tracking problems on row/col
		   insert.  */
		cr2.row_relative = FALSE;
		cr2.col_relative = FALSE;

		texpr = gnm_expr_top_new (gnm_expr_new_cellref (&cr2));
		dependent_managed_set_expr (&sp->target, texpr);
		gnm_expr_top_unref (texpr);
	} else
		dependent_managed_set_expr (&sp->target, NULL);
}

const GnmCellRef *
gnm_solver_param_get_target (GnmSolverParameters const *sp)
{
	return sp->target.texpr
		? gnm_expr_top_get_cellref (sp->target.texpr)
		: NULL;
}

GnmCell *
gnm_solver_param_get_target_cell (GnmSolverParameters const *sp)
{
	const GnmCellRef *cr = gnm_solver_param_get_target (sp);
	if (!cr)
		return NULL;

        return sheet_cell_get (eval_sheet (cr->sheet, sp->sheet),
			       cr->col, cr->row);
}

void
gnm_solver_param_set_algorithm (GnmSolverParameters *sp,
				GnmSolverFactory *algo)
{
	sp->options.algorithm = algo;
}

gboolean
gnm_solver_param_valid (GnmSolverParameters const *sp, GError **err)
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
	gnm_cell_eval (target_cell);

	if (!gnm_cell_has_expr (target_cell) ||
	    target_cell->value == NULL ||
	    !VALUE_IS_FLOAT (target_cell->value)) {
		char *tcname = gnm_solver_cell_name (target_cell, sp->sheet);
		g_set_error (err,
			     go_error_invalid (),
			     0,
			     _("Target cell, %s, must contain a formula that evaluates to a number"),
			     tcname);
		g_free (tcname);
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
			char *cname = gnm_solver_cell_name (cell, sp->sheet);
			g_set_error (err,
				     go_error_invalid (),
				     0,
				     _("Input cell %s contains a formula"),
				     cname);
			g_free (cname);
			g_slist_free (input_cells);
			return FALSE;
		}
	}
	g_slist_free (input_cells);

	for (i = 1, l = sp->constraints; l; i++, l = l->next) {
		GnmSolverConstraint *c = l->data;
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

static GObject *
gnm_solver_param_constructor (GType type,
			      guint n_construct_properties,
			      GObjectConstructParam *construct_params)
{
	GObject *obj;
	GnmSolverParameters *sp;

	obj = gnm_solver_param_parent_class->constructor
		(type, n_construct_properties, construct_params);
	sp = GNM_SOLVER_PARAMETERS (obj);

	dependent_managed_init (&sp->target, sp->sheet);
	dependent_managed_init (&sp->input, sp->sheet);

	sp->options.model_type = GNM_SOLVER_LP;
	sp->options.max_iter = 1000;
	sp->options.max_time_sec = 60;
	sp->options.assume_non_negative = TRUE;
	sp->options.scenario_name = g_strdup ("Optimal");

	return obj;
}

static void
gnm_solver_param_finalize (GObject *obj)
{
	GnmSolverParameters *sp = GNM_SOLVER_PARAMETERS (obj);

	dependent_managed_set_expr (&sp->target, NULL);
	dependent_managed_set_expr (&sp->input, NULL);
	g_slist_free_full (sp->constraints,
			      (GFreeFunc)gnm_solver_constraint_free);
	g_free (sp->options.scenario_name);

	gnm_solver_param_parent_class->finalize (obj);
}

static void
gnm_solver_param_get_property (GObject *object, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
	GnmSolverParameters *sp = (GnmSolverParameters *)object;

	switch (property_id) {
	case SOLP_PROP_SHEET:
		g_value_set_object (value, sp->sheet);
		break;

	case SOLP_PROP_PROBLEM_TYPE:
		g_value_set_enum (value, sp->problem_type);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_param_set_property (GObject *object, guint property_id,
			       GValue const *value, GParamSpec *pspec)
{
	GnmSolverParameters *sp = (GnmSolverParameters *)object;

	switch (property_id) {
	case SOLP_PROP_SHEET:
		/* We hold no ref.  */
		sp->sheet = g_value_get_object (value);
		break;

	case SOLP_PROP_PROBLEM_TYPE:
		sp->problem_type = g_value_get_enum (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_param_class_init (GObjectClass *object_class)
{
	gnm_solver_param_parent_class = g_type_class_peek_parent (object_class);

	object_class->constructor = gnm_solver_param_constructor;
	object_class->finalize = gnm_solver_param_finalize;
	object_class->set_property = gnm_solver_param_set_property;
	object_class->get_property = gnm_solver_param_get_property;

	g_object_class_install_property (object_class, SOLP_PROP_SHEET,
		g_param_spec_object ("sheet",
				      P_("Sheet"),
				      P_("Sheet"),
				      GNM_SHEET_TYPE,
				      GSF_PARAM_STATIC |
				      G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, SOLP_PROP_PROBLEM_TYPE,
		g_param_spec_enum ("problem-type",
				    P_("Problem Type"),
				    P_("Problem Type"),
				    GNM_SOLVER_PROBLEM_TYPE_TYPE,
				    GNM_SOLVER_MAXIMIZE,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
}

GSF_CLASS (GnmSolverParameters, gnm_solver_param,
	   gnm_solver_param_class_init, NULL, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

enum {
	SOL_SIG_PREPARE,
	SOL_SIG_START,
	SOL_SIG_STOP,
	SOL_SIG_CHILD_EXIT,
	SOL_SIG_LAST
};

static guint solver_signals[SOL_SIG_LAST] = { 0 };

enum {
	SOL_PROP_0,
	SOL_PROP_STATUS,
	SOL_PROP_REASON,
	SOL_PROP_PARAMS,
	SOL_PROP_RESULT,
	SOL_PROP_STARTTIME,
	SOL_PROP_ENDTIME
};

static GObjectClass *gnm_solver_parent_class;

static void
gnm_solver_dispose (GObject *obj)
{
	GnmSolver *sol = GNM_SOLVER (obj);

	if (sol->status == GNM_SOLVER_STATUS_RUNNING) {
		gboolean ok = gnm_solver_stop (sol, NULL);
		if (ok) {
			g_warning ("Failed to stop solver -- now what?");
		}
	}

	g_free (sol->reason);
	sol->reason = NULL;

	if (sol->result) {
		g_object_unref (sol->result);
		sol->result = NULL;
	}

	if (sol->params) {
		g_object_unref (sol->params);
		sol->params = NULL;
	}

	gnm_solver_parent_class->dispose (obj);
}

static void
gnm_solver_get_property (GObject *object, guint property_id,
			 GValue *value, GParamSpec *pspec)
{
	GnmSolver *sol = (GnmSolver *)object;

	switch (property_id) {
	case SOL_PROP_STATUS:
		g_value_set_enum (value, sol->status);
		break;

	case SOL_PROP_REASON:
		g_value_set_string (value, sol->reason);
		break;

	case SOL_PROP_PARAMS:
		g_value_set_object (value, sol->params);
		break;

	case SOL_PROP_RESULT:
		g_value_set_object (value, sol->result);
		break;

	case SOL_PROP_STARTTIME:
		g_value_set_double (value, sol->starttime);
		break;

	case SOL_PROP_ENDTIME:
		g_value_set_double (value, sol->endtime);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_set_property (GObject *object, guint property_id,
			 GValue const *value, GParamSpec *pspec)
{
	GnmSolver *sol = (GnmSolver *)object;

	switch (property_id) {
	case SOL_PROP_STATUS:
		gnm_solver_set_status (sol, g_value_get_enum (value));
		break;

	case SOL_PROP_REASON:
		gnm_solver_set_reason (sol, g_value_get_string (value));
		break;

	case SOL_PROP_PARAMS:
		if (sol->params) g_object_unref (sol->params);
		sol->params = g_value_dup_object (value);
		break;

	case SOL_PROP_RESULT:
		if (sol->result) g_object_unref (sol->result);
		sol->result = g_value_dup_object (value);
		break;

	case SOL_PROP_STARTTIME:
		sol->starttime = g_value_get_double (value);
		break;

	case SOL_PROP_ENDTIME:
		sol->endtime = g_value_get_double (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

gboolean
gnm_solver_prepare (GnmSolver *sol, WorkbookControl *wbc, GError **err)
{
	gboolean res;

	g_return_val_if_fail (GNM_IS_SOLVER (sol), FALSE);
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY, FALSE);

	g_signal_emit (sol, solver_signals[SOL_SIG_PREPARE], 0, wbc, err, &res);
	return res;
}

gboolean
gnm_solver_start (GnmSolver *sol, WorkbookControl *wbc, GError **err)
{
	gboolean res;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY ||
			      sol->status == GNM_SOLVER_STATUS_PREPARED,
			      FALSE);

	if (sol->status == GNM_SOLVER_STATUS_READY) {
		res = gnm_solver_prepare (sol, wbc, err);
		if (!res)
			return FALSE;
	}

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_PREPARED, FALSE);

	g_signal_emit (sol, solver_signals[SOL_SIG_START], 0, wbc, err, &res);
	return res;
}

gboolean
gnm_solver_stop (GnmSolver *sol, GError **err)
{
	gboolean res;

	g_return_val_if_fail (GNM_IS_SOLVER (sol), FALSE);

	g_signal_emit (sol, solver_signals[SOL_SIG_STOP], 0, err, &res);
	return res;
}

static double
current_time (void)
{
	GTimeVal now;
	g_get_current_time (&now);
	return now.tv_sec + (now.tv_usec / 1e6);
}


double
gnm_solver_elapsed (GnmSolver *solver)
{
	double endtime;

	g_return_val_if_fail (GNM_IS_SOLVER (solver), 0);

	if (solver->starttime < 0)
		return 0;

	endtime = (solver->endtime < 0)
		? current_time ()
		: solver->endtime;

	return endtime - solver->starttime;
}

gboolean
gnm_solver_check_timeout (GnmSolver *solver)
{
	GnmSolverParameters *sp;

	g_return_val_if_fail (GNM_IS_SOLVER (solver), FALSE);

	sp = solver->params;

	if (solver->status != GNM_SOLVER_STATUS_RUNNING)
		return FALSE;

	if (gnm_solver_elapsed (solver) <= sp->options.max_time_sec)
		return FALSE;

	gnm_solver_stop (solver, NULL);
	gnm_solver_set_reason (solver, _("Timeout"));

	return TRUE;
}

void
gnm_solver_store_result (GnmSolver *sol)
{
	GnmValue const *vinput;
	GnmSheetRange sr;
	int h, w, x, y;
	GnmValue const *solution;

	g_return_if_fail (GNM_IS_SOLVER (sol));
	g_return_if_fail (sol->result != NULL);
	g_return_if_fail (sol->result->solution);

	vinput = gnm_solver_param_get_input (sol->params);
	gnm_sheet_range_from_value (&sr, vinput);
	if (!sr.sheet) sr.sheet = sol->params->sheet;
	h = range_height (&sr.range);
	w = range_width (&sr.range);

	solution = gnm_solver_has_solution (sol)
		? sol->result->solution
		: NULL;

	for (x = 0; x < w; x++) {
		for (y = 0; y < h; y++) {
			GnmValue *v = solution
				? value_dup (value_area_fetch_x_y (solution, x, y, NULL))
				: value_new_error_NA (NULL);
			GnmCell *cell =
				sheet_cell_fetch (sr.sheet,
						  sr.range.start.col + x,
						  sr.range.start.row + y);
			gnm_cell_set_value (cell, v);
			cell_queue_recalc (cell);
		}
	}
}

gboolean
gnm_solver_finished (GnmSolver *sol)
{
	g_return_val_if_fail (GNM_IS_SOLVER (sol), TRUE);

	switch (sol->status) {

	case GNM_SOLVER_STATUS_READY:
	case GNM_SOLVER_STATUS_PREPARING:
	case GNM_SOLVER_STATUS_PREPARED:
	case GNM_SOLVER_STATUS_RUNNING:
		return FALSE;
	case GNM_SOLVER_STATUS_DONE:
	default:
	case GNM_SOLVER_STATUS_ERROR:
	case GNM_SOLVER_STATUS_CANCELLED:
		return TRUE;
	}
}

void
gnm_solver_set_status (GnmSolver *solver, GnmSolverStatus status)
{
	GnmSolverStatus old_status;

	g_return_if_fail (GNM_IS_SOLVER (solver));

	if (status == solver->status)
		return;

	gnm_solver_set_reason (solver, NULL);

	old_status = solver->status;
	solver->status = status;
	g_object_notify (G_OBJECT (solver), "status");

	if (status == GNM_SOLVER_STATUS_RUNNING)
		g_object_set (G_OBJECT (solver),
			      "starttime", current_time (),
			      "endtime", (double)-1,
			      NULL);
	else if (old_status == GNM_SOLVER_STATUS_RUNNING)
		g_object_set (G_OBJECT (solver),
			      "endtime", current_time (),
			      NULL);
}

void
gnm_solver_set_reason (GnmSolver *solver, const char *reason)
{
	g_return_if_fail (GNM_IS_SOLVER (solver));

	if (g_strcmp0 (reason, solver->reason) == 0)
		return;

	g_free (solver->reason);
	solver->reason = g_strdup (reason);

	g_object_notify (G_OBJECT (solver), "reason");
}


gboolean
gnm_solver_has_solution (GnmSolver *solver)
{
	if (solver->result == NULL)
		return FALSE;

	switch (solver->result->quality) {
	case GNM_SOLVER_RESULT_NONE:
	case GNM_SOLVER_RESULT_INFEASIBLE:
	case GNM_SOLVER_RESULT_UNBOUNDED:
	default:
		return FALSE;
	case GNM_SOLVER_RESULT_FEASIBLE:
	case GNM_SOLVER_RESULT_OPTIMAL:
		return TRUE;
	}
}

gboolean
gnm_solver_check_constraints (GnmSolver *solver)
{
	GSList *l;
	GnmSolverParameters *sp = solver->params;
	GnmCell *target_cell;

	if (sp->options.assume_non_negative ||
	    sp->options.assume_discrete) {
		GSList *input_cells = gnm_solver_param_get_input_cells (sp);
		GSList *l;

		for (l = input_cells; l; l = l->next) {
			GnmCell *cell = l->data;
			gnm_float val;

			gnm_cell_eval (cell);
			val = value_get_as_float (cell->value);
			if (sp->options.assume_non_negative && val < 0)
				break;
			if (sp->options.assume_discrete &&
			    val != gnm_floor (val))
				break;
		}
		g_slist_free (input_cells);

		if (l)
			return FALSE;
	}

	for (l = sp->constraints; l; l = l->next) {
		GnmSolverConstraint *c = l->data;
		int i;
		gnm_float cl, cr;
		GnmCell *lhs, *rhs;

		for (i = 0;
		     gnm_solver_constraint_get_part (c, sp, i,
						     &lhs, &cl,
						     &rhs, &cr);
		     i++) {
			if (lhs) {
				gnm_cell_eval (lhs);
				cl = value_get_as_float (lhs->value);
			}
			if (rhs) {
				gnm_cell_eval (rhs);
				cr = value_get_as_float (rhs->value);
			}

			switch (c->type) {
			case GNM_SOLVER_INTEGER:
				if (cl == gnm_floor (cl))
					continue;
				return FALSE;
			case GNM_SOLVER_BOOLEAN:
				if (cl == 0 || cl == 1)
					continue;
				return FALSE;
			case GNM_SOLVER_LE:
				if (cl <= cr)
					continue;
				return FALSE;
			case GNM_SOLVER_GE:
				if (cl >= cr)
					continue;
				return FALSE;
			case GNM_SOLVER_EQ:
				if (cl == cr)
					continue;
				return FALSE;
			default:
				g_assert_not_reached ();
				return FALSE;
			}
		}
	}

	target_cell = gnm_solver_param_get_target_cell (sp);
	gnm_cell_eval (target_cell);
	if (!target_cell || !VALUE_IS_NUMBER (target_cell->value))
		return FALSE;

	return TRUE;
}

static GnmValue *
cb_get_value (GnmValueIter const *iter, gpointer user_data)
{
	GnmValue *res = user_data;

	value_array_set (res, iter->x, iter->y,
			 iter->v
			 ? value_dup (iter->v)
			 : value_new_int (0));

	return NULL;
}

GnmValue *
gnm_solver_get_current_values (GnmSolver *solver)
{
	int w, h;
	GnmValue *res;
	GnmSolverParameters const *sp = solver->params;
	GnmValue const *vinput = gnm_solver_param_get_input (sp);
	GnmEvalPos ep;

	eval_pos_init_sheet (&ep, sp->sheet);

	w = value_area_get_width (vinput, &ep);
	h = value_area_get_height (vinput, &ep);
	res = value_new_array_empty (w, h);

	value_area_foreach (vinput, &ep, CELL_ITER_ALL, cb_get_value, res);

	return res;
}

gboolean
gnm_solver_saveas (GnmSolver *solver, WorkbookControl *wbc,
		   GOFileSaver *fs,
		   const char *templ, char **filename,
		   GError **err)
{
	int fd;
	GsfOutput *output;
	FILE *file;
	GOIOContext *io_context;
	gboolean ok;
	WorkbookView *wbv = wb_control_view (wbc);

	fd = g_file_open_tmp (templ, filename, err);
	if (fd == -1) {
		g_set_error (err, G_FILE_ERROR, 0,
			     _("Failed to create file for linear program"));
		return FALSE;
	}

	file = fdopen (fd, "wb");
	if (!file) {
		/* This shouldn't really happen.  */
		close (fd);
		g_set_error (err, G_FILE_ERROR, 0,
			     _("Failed to create linear program file"));
		return FALSE;
	}

	/* Give the saver a way to talk to the solver.  */
	g_object_set_data_full (G_OBJECT (fs),
				"solver", g_object_ref (solver),
				(GDestroyNotify)g_object_unref);

	output = gsf_output_stdio_new_FILE (*filename, file, TRUE);
	io_context = go_io_context_new (GO_CMD_CONTEXT (wbc));
	wbv_save_to_output (wbv, fs, output, io_context);
	ok = !go_io_error_occurred (io_context);
	g_object_unref (io_context);
	g_object_unref (output);

	g_object_set_data (G_OBJECT (fs), "solver", NULL);

	if (!ok) {
		g_set_error (err, G_FILE_ERROR, 0,
			     _("Failed to save linear program"));
		return FALSE;
	}

	return TRUE;
}

static gboolean
cell_in_cr (GnmCell const *cell, GnmSheetRange *sr, gboolean follow,
	    int *px, int *py)
{
	if (!cell)
		return FALSE;

	if (sr->sheet != cell->base.sheet ||
	    !range_contains (&sr->range, cell->pos.col, cell->pos.row)) {
		/* If the expression is just =X42 thenm look at X42 instead.
		   This is because the mps loader uses such a level of
		   indirection.  Note: we follow only one such step.  */
		GnmCellRef const *cr = gnm_expr_top_get_cellref (cell->base.texpr);
		GnmCellRef cr2;
		GnmCell const *new_cell;
		GnmEvalPos ep;

		if (!cr)
			return FALSE;

		eval_pos_init_cell (&ep, cell);
		gnm_cellref_make_abs (&cr2, cr, &ep);
		new_cell = sheet_cell_get (eval_sheet (cr2.sheet, cell->base.sheet),
					   cr2.col, cr2.row);
		return cell_in_cr (new_cell, sr, FALSE, px, py);

	}

	*px = cell->pos.col - sr->range.start.col;
	*py = cell->pos.row - sr->range.start.row;
	return TRUE;
}

static gboolean
cell_is_constant (GnmCell *cell, gnm_float *pc)
{
	if (!cell)
		return TRUE;

	if (cell->base.texpr)
		return FALSE;

	gnm_cell_eval (cell);
	*pc = value_get_as_float (cell->value);
	return TRUE;
}

#define SET_LOWER(l_)						\
  do {								\
	  (*pmin)[y * w + x] = MAX ((*pmin)[y * w + x], (l_));	\
  } while (0)

#define SET_UPPER(l_)						\
  do {								\
	  (*pmax)[y * w + x] = MIN ((*pmax)[y * w + x], (l_));	\
  } while (0)



static void
gnm_solver_get_limits (GnmSolver *solver, gnm_float **pmin, gnm_float **pmax)
{
	GnmValue const *vinput;
	GnmSolverParameters *params = solver->params;
	int x, y, w, h;
	GnmSheetRange sr;
	GSList *l;

	*pmin = *pmax = NULL;

	vinput = gnm_solver_param_get_input (params);
	if (!vinput) return;

	gnm_sheet_range_from_value (&sr, vinput);
	if (!sr.sheet) sr.sheet = params->sheet;
	h = range_height (&sr.range);
	w = range_width (&sr.range);

	*pmin = g_new (gnm_float, h * w);
	*pmax = g_new (gnm_float, h * w);

	for (x = 0; x < w; x++) {
		for (y = 0; y < h; y++) {
			(*pmin)[y * w + x] = params->options.assume_non_negative ? 0 : gnm_ninf;
			(*pmax)[y * w + x] = gnm_pinf;
		}
	}

	for (l = params->constraints; l; l = l->next) {
		GnmSolverConstraint *c = l->data;
		int i;
		gnm_float cl, cr;
		GnmCell *lhs, *rhs;

		for (i = 0;
		     gnm_solver_constraint_get_part (c, params, i,
						     &lhs, &cl,
						     &rhs, &cr);
		     i++) {
			if (!cell_in_cr (lhs, &sr, TRUE, &x, &y))
				continue;
			if (!cell_is_constant (rhs, &cr))
				continue;

			switch (c->type) {
			case GNM_SOLVER_INTEGER:
				break;
			case GNM_SOLVER_BOOLEAN:
				SET_LOWER (0.0);
				SET_UPPER (1.0);
				break;
			case GNM_SOLVER_LE:
				SET_UPPER (cr);
				break;
			case GNM_SOLVER_GE:
				SET_LOWER (cr);
				break;
			case GNM_SOLVER_EQ:
				SET_LOWER (cr);
				SET_UPPER (cr);
				break;

			default:
				g_assert_not_reached ();
				break;
			}
		}
	}

}

#undef SET_LOWER
#undef SET_UPPER

#define ADD_HEADER(txt_) do {			\
	dao_set_bold (dao, 0, R, 0, R);		\
	dao_set_cell (dao, 0, R, (txt_));	\
	R++;					\
} while (0)

#define AT_LIMIT(s_,l_) \
  (gnm_finite (l_) ? gnm_abs ((s_) - (l_)) <= (gnm_abs ((s_)) + gnm_abs ((l_))) / 1e10 : (s_) == (l_))

#define MARK_BAD(col_)						\
  do {								\
	  int c = (col_);					\
	  dao_set_colors (dao, c, R, c, R,			\
			  gnm_color_new_rgb8 (255, 0, 0),	\
			  NULL);				\
  } while (0)


static void
add_value_or_special (data_analysis_output_t *dao, int col, int row,
		      gnm_float x)
{
	if (gnm_finite (x))
		dao_set_cell_float (dao, col, row, x);
	else {
		dao_set_cell (dao, col, row, "-");
		dao_set_align (dao, col, row, col, row,
			       GNM_HALIGN_CENTER, GNM_VALIGN_TOP);
	}
}

void
gnm_solver_create_report (GnmSolver *solver, const char *name)
{
	GnmSolverParameters *params = solver->params;
	int R = 0;
	GnmValue const *vinput;
	data_analysis_output_t *dao;
	GSList *l;

	dao = dao_init_new_sheet (NULL);
	dao->sheet = params->sheet;
	dao_prepare_output (NULL, dao, name);

	/* ---------------------------------------- */

	{
		char *tmp;

		ADD_HEADER (_("Target"));
		dao_set_cell (dao, 1, R, _("Cell"));
		dao_set_cell (dao, 2, R, _("Value"));
		dao_set_cell (dao, 3, R, _("Type"));
		dao_set_cell (dao, 4, R, _("Status"));
		R++;

		tmp = gnm_solver_cell_name
			(gnm_solver_param_get_target_cell (params),
			 params->sheet);
		dao_set_cell (dao, 1, R, tmp);
		g_free (tmp);

		dao_set_cell_float (dao, 2, R, solver->result->value);

		switch (params->problem_type) {
		case GNM_SOLVER_MINIMIZE:
			dao_set_cell (dao, 3, R, _("Minimize"));
			break;
		case GNM_SOLVER_MAXIMIZE:
			dao_set_cell (dao, 3, R, _("Maximize"));
			break;
		}

		switch (solver->result->quality) {
		default:
			break;
		case GNM_SOLVER_RESULT_FEASIBLE:
			dao_set_cell (dao, 4, R, _("Feasible"));
			break;
		case GNM_SOLVER_RESULT_OPTIMAL:
			dao_set_cell (dao, 4, R, _("Optimal"));
			break;
		}

		R++;
		R++;
	}

	/* ---------------------------------------- */

	vinput = gnm_solver_param_get_input (params);
	if (vinput) {
		int x, y, w, h;
		GnmSheetRange sr;
		gnm_float *pmin, *pmax;

		ADD_HEADER (_("Variables"));

		dao_set_cell (dao, 1, R, _("Cell"));
		dao_set_cell (dao, 2, R, _("Value"));
		dao_set_cell (dao, 3, R, _("Lower"));
		dao_set_cell (dao, 4, R, _("Upper"));
		dao_set_cell (dao, 5, R, _("Slack"));
		R++;

		gnm_sheet_range_from_value (&sr, vinput);
		if (!sr.sheet) sr.sheet = params->sheet;
		h = range_height (&sr.range);
		w = range_width (&sr.range);

		gnm_solver_get_limits (solver, &pmin, &pmax);

		for (x = 0; x < w; x++) {
			for (y = 0; y < h; y++) {
				GnmCell *cell = sheet_cell_fetch
					(sr.sheet,
					 sr.range.start.col + x,
					 sr.range.start.row + y);
				gnm_float m = pmin[y * w + x];
				gnm_float M = pmax[y * w + x];
				GnmValue const *vs = value_area_fetch_x_y (solver->result->solution, x, y, NULL);
				gnm_float s = value_get_as_float (vs);
				gnm_float slack = MIN (s - m, M - s);

				char *cname = gnm_solver_cell_name (cell, params->sheet);
				dao_set_cell (dao, 1, R, cname);
				g_free (cname);
				dao_set_cell_value (dao, 2, R, value_dup (vs));
				add_value_or_special (dao, 3, R, m);
				add_value_or_special (dao, 4, R, M);

				add_value_or_special (dao, 5, R, slack);
				if (slack < 0)
					MARK_BAD (5);

				if (AT_LIMIT (s, m) || AT_LIMIT (s, M))
					dao_set_cell (dao, 6, R, _("At limit"));


				if (s < m || s > M) {
					dao_set_cell (dao, 7, R, _("Outside bounds"));
					MARK_BAD (7);
				}

				R++;
			}
		}

		g_free (pmin);
		g_free (pmax);
		R++;
	}

	/* ---------------------------------------- */

	ADD_HEADER (_("Constraints"));

	if (params->constraints) {
		dao_set_cell (dao, 1, R, _("Condition"));
		dao_set_cell (dao, 2, R, _("Value"));
		dao_set_cell (dao, 3, R, _("Limit"));
		dao_set_cell (dao, 4, R, _("Slack"));
	} else {
		dao_set_cell (dao, 1, R, _("No constraints"));
	}
	R++;

	for (l = params->constraints; l; l = l->next) {
		GnmSolverConstraint *c = l->data;
		int i;
		gnm_float cl, cr;
		GnmCell *lhs, *rhs;

		for (i = 0;
		     gnm_solver_constraint_get_part (c, params, i,
						     &lhs, &cl,
						     &rhs, &cr);
		     i++) {
			gnm_float slack = 0;
			char *ctxt = gnm_solver_constraint_as_str (c, params->sheet);
			dao_set_cell (dao, 1, R, ctxt);
			g_free (ctxt);

			if (lhs) {
				gnm_cell_eval (lhs);
				cl = value_get_as_float (lhs->value);
			}
			if (rhs) {
				gnm_cell_eval (rhs);
				cr = value_get_as_float (rhs->value);
			}

			switch (c->type) {
			case GNM_SOLVER_INTEGER: {
				gnm_float c = gnm_fake_round (cl);
				slack = 0 - gnm_abs (c - cl);
				break;
			}
			case GNM_SOLVER_BOOLEAN: {
				gnm_float c = (cl > 0.5 ? 1 : 0);
				slack = 0 - gnm_abs (c - cl);
				break;
			}
			case GNM_SOLVER_LE:
				slack = cr - cl;
				break;
			case GNM_SOLVER_GE:
				slack = cl - cr;
				break;
			case GNM_SOLVER_EQ:
				slack = 0 - gnm_abs (cl - cr);
				break;
			default:
				g_assert_not_reached ();
			}

			add_value_or_special (dao, 2, R, cl);
			if (rhs)
				add_value_or_special (dao, 3, R, cr);

			add_value_or_special (dao, 4, R, slack);
			if (slack < 0)
				MARK_BAD (4);

			R++;
		}
	}

	/* ---------------------------------------- */

	dao_redraw_respan (dao);

	dao_free (dao);
}

#undef AT_LIMIT
#undef ADD_HEADER
#undef MARK_BAD

static void
gnm_solver_class_init (GObjectClass *object_class)
{
	gnm_solver_parent_class = g_type_class_peek_parent (object_class);

	object_class->dispose = gnm_solver_dispose;
	object_class->set_property = gnm_solver_set_property;
	object_class->get_property = gnm_solver_get_property;

        g_object_class_install_property (object_class, SOL_PROP_STATUS,
		g_param_spec_enum ("status",
				    P_("status"),
				    P_("The solver's current status"),
				    GNM_SOLVER_STATUS_TYPE,
				    GNM_SOLVER_STATUS_READY,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));

        g_object_class_install_property (object_class, SOL_PROP_REASON,
		g_param_spec_string ("reason",
				     P_("reason"),
				     P_("The reason behind the solver's status"),
				     NULL,
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));

	g_object_class_install_property (object_class, SOL_PROP_PARAMS,
		g_param_spec_object ("params",
				     P_("Parameters"),
				     P_("Solver parameters"),
				     GNM_SOLVER_PARAMETERS_TYPE,
				     GSF_PARAM_STATIC |
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_READWRITE));

	g_object_class_install_property (object_class, SOL_PROP_RESULT,
		g_param_spec_object ("result",
				     P_("Result"),
				     P_("Current best feasible result"),
				     GNM_SOLVER_RESULT_TYPE,
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));

	g_object_class_install_property (object_class, SOL_PROP_STARTTIME,
		g_param_spec_double ("starttime",
				     P_("Start Time"),
				     P_("Time the solver was started"),
				     -1, 1e10, -1,
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));

	g_object_class_install_property (object_class, SOL_PROP_ENDTIME,
		g_param_spec_double ("endtime",
				     P_("End Time"),
				     P_("Time the solver finished"),
				     -1, 1e10, -1,
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));

	solver_signals[SOL_SIG_PREPARE] =
		g_signal_new ("prepare",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverClass, prepare),
			      NULL, NULL,
			      gnm__BOOLEAN__OBJECT_POINTER,
			      G_TYPE_BOOLEAN, 2,
			      G_TYPE_OBJECT,
			      G_TYPE_POINTER);

	solver_signals[SOL_SIG_START] =
		g_signal_new ("start",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverClass, start),
			      NULL, NULL,
			      gnm__BOOLEAN__OBJECT_POINTER,
			      G_TYPE_BOOLEAN, 2,
			      G_TYPE_OBJECT,
			      G_TYPE_POINTER);

	solver_signals[SOL_SIG_STOP] =
		g_signal_new ("stop",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverClass, stop),
			      NULL, NULL,
			      gnm__BOOLEAN__POINTER,
			      G_TYPE_BOOLEAN, 1,
			      G_TYPE_POINTER);

	solver_signals[SOL_SIG_CHILD_EXIT] =
		g_signal_new ("child-exit",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverClass, child_exit),
			      NULL, NULL,
			      gnm__VOID__BOOLEAN_INT,
			      G_TYPE_NONE, 2,
			      G_TYPE_BOOLEAN, G_TYPE_INT);
}

GSF_CLASS (GnmSolver, gnm_solver,
	   &gnm_solver_class_init, NULL, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_solver_result_parent_class;

static void
gnm_solver_result_finalize (GObject *obj)
{
	GnmSolverResult *r = GNM_SOLVER_RESULT (obj);
	value_release (r->solution);
	gnm_solver_result_parent_class->finalize (obj);
}

static void
gnm_solver_result_class_init (GObjectClass *object_class)
{
	gnm_solver_result_parent_class =
		g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_solver_result_finalize;
}

GSF_CLASS (GnmSolverResult, gnm_solver_result,
	   &gnm_solver_result_class_init, NULL, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_sub_solver_parent_class;

void
gnm_sub_solver_clear (GnmSubSolver *subsol)
{
	int i;

	if (subsol->child_watch) {
		g_source_remove (subsol->child_watch);
		subsol->child_watch = 0;
	}

	if (subsol->child_pid) {
#ifdef G_OS_WIN32
		TerminateProcess (subsol->child_pid, 127);
#else
		kill (subsol->child_pid, SIGKILL);
#endif
		g_spawn_close_pid (subsol->child_pid);
		subsol->child_pid = (GPid)0;
	}

	for (i = 0; i <= 2; i++) {
		if (subsol->channel_watches[i]) {
			g_source_remove (subsol->channel_watches[i]);
			subsol->channel_watches[i] = 0;
		}
		if (subsol->channels[i]) {
			g_io_channel_unref (subsol->channels[i]);
			subsol->channels[i] = NULL;
		}
		if (subsol->fd[i] != -1) {
			close (subsol->fd[i]);
			subsol->fd[i] = -1;
		}
	}

	if (subsol->program_filename) {
		g_unlink (subsol->program_filename);
		g_free (subsol->program_filename);
		subsol->program_filename = NULL;
	}

	g_hash_table_remove_all (subsol->cell_from_name);
	g_hash_table_remove_all (subsol->name_from_cell);
}

static void
gnm_sub_solver_dispose (GObject *obj)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (obj);

	gnm_sub_solver_clear (subsol);

	gnm_sub_solver_parent_class->dispose (obj);
}

static void
gnm_sub_solver_init (GnmSubSolver *subsol)
{
	int i;

	for (i = 0; i <= 2; i++)
		subsol->fd[i] = -1;

	subsol->cell_from_name =
		g_hash_table_new_full (g_str_hash, g_str_equal,
				       g_free, NULL);
	subsol->name_from_cell =
		g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
cb_child_exit (G_GNUC_UNUSED GPid pid, gint status, GnmSubSolver *subsol)
{
	gboolean normal = WIFEXITED (status);
	int code;

	subsol->child_watch = 0;

	if (normal) {
		code = WEXITSTATUS (status);
		if (gnm_solver_debug ())
			g_printerr ("Solver process exited with code %d\n",
				    code);
#ifndef G_OS_WIN32
	} else if (WIFSIGNALED (status)) {
		code = WTERMSIG (status);
		if (gnm_solver_debug ())
			g_printerr ("Solver process received signal %d\n",
				    code);
#endif
	} else {
		code = -1;
		g_printerr ("Solver process exited with status 0x%x\n",
			    status);
	}

	g_signal_emit (subsol, solver_signals[SOL_SIG_CHILD_EXIT], 0,
		       normal, code);

	if (subsol->child_pid) {
		g_spawn_close_pid (subsol->child_pid);
		subsol->child_pid = (GPid)0;
	}
}

gboolean
gnm_sub_solver_spawn (GnmSubSolver *subsol,
		      char **argv,
		      GSpawnChildSetupFunc child_setup, gpointer setup_data,
		      GIOFunc io_stdout, gpointer stdout_data,
		      GIOFunc io_stderr, gpointer stderr_data,
		      GError **err)
{
	GnmSolver *sol = GNM_SOLVER (subsol);
	gboolean ok;
	GSpawnFlags spflags = G_SPAWN_DO_NOT_REAP_CHILD;
	int fd;

	g_return_val_if_fail (subsol->child_watch == 0, FALSE);
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_PREPARED, FALSE);

	if (!g_path_is_absolute (argv[0]))
		spflags |= G_SPAWN_SEARCH_PATH;

	if (io_stdout == NULL && !gnm_solver_debug ())
		spflags |= G_SPAWN_STDOUT_TO_DEV_NULL;

	if (gnm_solver_debug ()) {
		GString *msg = g_string_new ("Spawning");
		int i;
		for (i = 0; argv[i]; i++) {
			g_string_append_c (msg, ' ');
			g_string_append (msg, argv[i]);
		}
		g_printerr ("%s\n", msg->str);
		g_string_free (msg, TRUE);
	}

#ifdef G_OS_WIN32
	/* Hope for the best... */
	child_setup = NULL;
	setup_data = NULL;
#endif

	ok = g_spawn_async_with_pipes
		(g_get_home_dir (),  /* PWD */
		 argv,
		 NULL, /* environment */
		 spflags,
		 child_setup, setup_data,
		 &subsol->child_pid,
		 NULL,			/* stdin */
		 io_stdout ? &subsol->fd[1] : NULL,	/* stdout */
		 io_stdout ? &subsol->fd[2] : NULL,	/* stderr */
		 err);
	if (!ok)
		goto fail;

	subsol->child_watch =
		g_child_watch_add (subsol->child_pid,
				   (GChildWatchFunc)cb_child_exit, subsol);

	subsol->io_funcs[1] = io_stdout;
	subsol->io_funcs_data[1] = stdout_data;
	subsol->io_funcs[2] = io_stderr;
	subsol->io_funcs_data[2] = stderr_data;

	for (fd = 1; fd <= 2; fd++) {
		GIOFlags ioflags;

		if (subsol->io_funcs[fd] == NULL)
			continue;

		/*
		 * Despite the name these are documented to work on Win32.
		 * Let us hope that is actually true.
		 */
		subsol->channels[fd] = g_io_channel_unix_new (subsol->fd[fd]);
		ioflags = g_io_channel_get_flags (subsol->channels[fd]);
		g_io_channel_set_flags (subsol->channels[fd],
					ioflags | G_IO_FLAG_NONBLOCK,
					NULL);
		subsol->channel_watches[fd] =
			g_io_add_watch (subsol->channels[fd],
					G_IO_IN,
					subsol->io_funcs[fd],
					subsol->io_funcs_data[fd]);
	}

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_RUNNING);
	return TRUE;

fail:
	gnm_sub_solver_clear (subsol);
	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
	return FALSE;
}

const char *
gnm_sub_solver_name_cell (GnmSubSolver *subsol, GnmCell const *cell,
			  const char *name)
{
	char *name_copy = g_strdup (name);

	g_hash_table_insert (subsol->cell_from_name,
			     name_copy,
			     (gpointer)cell);
	g_hash_table_insert (subsol->name_from_cell,
			     (gpointer)cell,
			     name_copy);

	return name_copy;
}

GnmCell *
gnm_sub_solver_find_cell (GnmSubSolver *subsol, const char *name)
{
	return g_hash_table_lookup (subsol->cell_from_name, name);
}

const char *
gnm_sub_solver_get_cell_name (GnmSubSolver *subsol,
			      GnmCell const *cell)
{
	return g_hash_table_lookup (subsol->name_from_cell, (gpointer)cell);
}

char *
gnm_sub_solver_locate_binary (const char *binary, const char *solver,
			      const char *url,
			      WBCGtk *wbcg)
{
	GtkWindow *parent;
	GtkWidget *dialog;
	char *path = NULL;
	int res;
	GtkFileChooser *fsel;
	char *title;

	parent = wbcg ? wbcg_toplevel (wbcg) : NULL;
	dialog = gtk_message_dialog_new_with_markup
		(parent,
		 GTK_DIALOG_DESTROY_WITH_PARENT,
		 GTK_MESSAGE_QUESTION,
		 GTK_BUTTONS_YES_NO,
		 _("Gnumeric is unable to locate the program <i>%s</i> needed "
		   "for the <i>%s</i> solver.  For more information see %s.\n\n"
		   "Would you like to locate it yourself?"),
		 binary, solver, url);
	title = g_strdup_printf (_("Unable to locate %s"), binary);
	g_object_set (G_OBJECT (dialog),
		      "title", title,
		      NULL);
	g_free (title);

	res = go_gtk_dialog_run (GTK_DIALOG (dialog), parent);
	switch (res) {
	case GTK_RESPONSE_NO:
	case GTK_RESPONSE_DELETE_EVENT:
	default:
		return NULL;
	case GTK_RESPONSE_YES:
		break;
	}

	title = g_strdup_printf (_("Locate the %s program"), binary);
	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_OPEN,
			       "local-only", TRUE,
			       "title", title,
			       NULL));
	g_free (title);
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_EXECUTE, GTK_RESPONSE_OK,
				NULL);
	g_object_ref (fsel);
	if (go_gtk_file_sel_dialog (parent, GTK_WIDGET (fsel))) {
		path = gtk_file_chooser_get_filename (fsel);
		if (!g_file_test (path, G_FILE_TEST_IS_EXECUTABLE)) {
			g_free (path);
			path = NULL;
		}
	}

	gtk_widget_destroy (GTK_WIDGET (fsel));
	g_object_unref (fsel);

	return path;
}


void
gnm_sub_solver_flush (GnmSubSolver *subsol)
{
	int fd;

	for (fd = 1; fd <= 2; fd++) {
		if (subsol->io_funcs[fd] == NULL)
			continue;

		subsol->io_funcs[fd] (subsol->channels[fd],
				      G_IO_IN,
				      subsol->io_funcs_data[fd]);
	}
}

static void
gnm_sub_solver_class_init (GObjectClass *object_class)
{
	gnm_sub_solver_parent_class = g_type_class_peek_parent (object_class);

	object_class->dispose = gnm_sub_solver_dispose;
}

GSF_CLASS (GnmSubSolver, gnm_sub_solver,
	   gnm_sub_solver_class_init, gnm_sub_solver_init, GNM_SOLVER_TYPE)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_solver_factory_parent_class;

static void
gnm_solver_factory_dispose (GObject *obj)
{
	GnmSolverFactory *factory = GNM_SOLVER_FACTORY (obj);

	g_free (factory->id);
	g_free (factory->name);

	gnm_solver_factory_parent_class->dispose (obj);
}

static void
gnm_solver_factory_class_init (GObjectClass *object_class)
{
	gnm_solver_factory_parent_class =
		g_type_class_peek_parent (object_class);

	object_class->dispose = gnm_solver_factory_dispose;
}

GSF_CLASS (GnmSolverFactory, gnm_solver_factory,
	   gnm_solver_factory_class_init, NULL, G_TYPE_OBJECT)


static GSList *solvers;

GSList *
gnm_solver_db_get (void)
{
	return solvers;
}

GnmSolverFactory *
gnm_solver_factory_new (const char *id,
			const char *name,
			GnmSolverModelType type,
			GnmSolverCreator creator,
			GnmSolverFactoryFunctional functional)
{
	GnmSolverFactory *res;

	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (creator != NULL, NULL);

	res = g_object_new (GNM_SOLVER_FACTORY_TYPE, NULL);
	res->id = g_strdup (id);
	res->name = g_strdup (name);
	res->type = type;
	res->creator = creator;
	res->functional = functional;
	return res;
}

GnmSolver *
gnm_solver_factory_create (GnmSolverFactory *factory,
			   GnmSolverParameters *param)
{
	g_return_val_if_fail (GNM_IS_SOLVER_FACTORY (factory), NULL);
	return factory->creator (factory, param);
}

gboolean
gnm_solver_factory_functional (GnmSolverFactory *factory,
			       WBCGtk *wbcg)
{
	if (factory == NULL)
		return FALSE;

	return (factory->functional == NULL ||
		factory->functional (factory, wbcg));
}

static int
cb_compare_factories (GnmSolverFactory *a, GnmSolverFactory *b)
{
	return go_utf8_collate_casefold (a->name, b->name);
}

void
gnm_solver_db_register (GnmSolverFactory *factory)
{
	if (gnm_solver_debug ())
		g_printerr ("Registering %s\n", factory->id);
	g_object_ref (factory);
	solvers = g_slist_insert_sorted (solvers, factory,
					 (GCompareFunc)cb_compare_factories);
}

void
gnm_solver_db_unregister (GnmSolverFactory *factory)
{
	if (gnm_solver_debug ())
		g_printerr ("Unregistering %s\n", factory->id);
	solvers = g_slist_remove (solvers, factory);
	g_object_unref (factory);
}

/* ------------------------------------------------------------------------- */
