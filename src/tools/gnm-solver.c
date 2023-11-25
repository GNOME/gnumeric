#include <gnumeric-config.h>
#include <gnumeric.h>
#include <value.h>
#include <cell.h>
#include <expr.h>
#include <expr-deriv.h>
#include <sheet.h>
#include <workbook.h>
#include <rangefunc.h>
#include <ranges.h>
#include <gutils.h>
#include <mathfunc.h>
#include <tools/gnm-solver.h>
#include <workbook-view.h>
#include <workbook-control.h>
#include <application.h>
#include <gnm-marshalers.h>
#include <tools/dao.h>
#include <gui-util.h>
#include <gnm-i18n.h>
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

static gboolean debug_factory;

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
	dependent_managed_set_expr (&res->lhs, c->lhs.base.texpr);
	dependent_managed_set_expr (&res->rhs, c->rhs.base.texpr);
	return res;
}

static GnmSolverConstraint *
gnm_solver_constraint_dup1 (GnmSolverConstraint *c)
{
	return gnm_solver_constraint_dup (c, c->lhs.base.sheet);
}

GType
gnm_solver_constraint_get_type (void)
{
	static GType t = 0;

	if (t == 0)
		t = g_boxed_type_register_static ("GnmSolverConstraint",
			 (GBoxedCopyFunc)gnm_solver_constraint_dup1,
			 (GBoxedFreeFunc)gnm_solver_constraint_free);
	return t;
}

gboolean
gnm_solver_constraint_equal (GnmSolverConstraint const *a,
			     GnmSolverConstraint const *b)
{
	return (a->type == b->type &&
		gnm_expr_top_equal (a->lhs.base.texpr, b->lhs.base.texpr) &&
		(!gnm_solver_constraint_has_rhs (a) ||
		 gnm_expr_top_equal (a->rhs.base.texpr, b->rhs.base.texpr)));
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
	if (lhs == NULL || !VALUE_IS_CELLRANGE (lhs))
		return FALSE;

	if (gnm_solver_constraint_has_rhs (c)) {
		GnmValue const *rhs = gnm_solver_constraint_get_rhs (c);
		if (rhs == NULL)
			return FALSE;
		if (VALUE_IS_CELLRANGE (rhs)) {
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
			break; /* No need to blame constraint.  */

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

/**
 * gnm_solver_constraint_get_lhs:
 * @c: GnmSolverConstraint
 *
 * Returns: (transfer none) (nullable): Get left-hand side of constraint @c.
 */
GnmValue const *
gnm_solver_constraint_get_lhs (GnmSolverConstraint const *c)
{
	GnmExprTop const *texpr = c->lhs.base.texpr;
	return texpr ? gnm_expr_top_get_constant (texpr) : NULL;
}

/**
 * gnm_solver_constraint_set_lhs:
 * @c: GnmSolverConstraint
 * @v: (transfer full) (nullable): new left-hand side
 */
void
gnm_solver_constraint_set_lhs (GnmSolverConstraint *c, GnmValue *v)
{
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&c->lhs, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

/**
 * gnm_solver_constraint_get_rhs:
 * @c: GnmSolverConstraint
 *
 * Returns: (transfer none) (nullable): Get right-hand side of constraint @c.
 */
GnmValue const *
gnm_solver_constraint_get_rhs (GnmSolverConstraint const *c)
{
	GnmExprTop const *texpr = c->rhs.base.texpr;
	return texpr ? gnm_expr_top_get_constant (texpr) : NULL;
}

/**
 * gnm_solver_constraint_set_rhs:
 * @c: GnmSolverConstraint
 * @v: (transfer full) (nullable): new right-hand side
 */
void
gnm_solver_constraint_set_rhs (GnmSolverConstraint *c, GnmValue *v)
{
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&c->rhs, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

/**
 * gnm_solver_constraint_get_part:
 * @c: GnmSolverConstraint
 * @sp: GnmSolverParameters
 * @i: part index
 * @lhs: (optional) (out): #GnmCell of left-hand side
 * @cl: (optional) (out): constant value of left-hand side
 * @rhs: (optional) (out): #GnmCell of right-hand side
 * @cr: (optional) (out): constant value of left-hand side
 *
 * This splits @c into parts and returns information about the @i'th part.
 * There will be multiple parts when the left-hand side is a cell range.
 *
 * Returns: %TRUE if the @i'th part exists.
 */
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

	texpr = lhs ? c->lhs.base.texpr : c->rhs.base.texpr;
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

char *
gnm_solver_constraint_part_as_str (GnmSolverConstraint const *c, int i,
				   GnmSolverParameters *sp)
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
	GString *buf;
	gnm_float cl, cr;
	GnmCell *lhs, *rhs;

	if (!gnm_solver_constraint_get_part (c, sp, i, &lhs, &cl, &rhs, &cr))
		return NULL;

	buf = g_string_new (NULL);

	g_string_append (buf, cell_name (lhs));
	g_string_append_c (buf, ' ');
	g_string_append (buf, translate ? _(type) : type);
	if (gnm_solver_constraint_has_rhs (c)) {
		g_string_append_c (buf, ' ');
		g_string_append (buf, cell_name (rhs));
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

/**
 * gnm_solver_param_dup:
 * @src: #GnmSolverParameters
 * @new_sheet: #Sheet
 *
 * Returns: (transfer full): duplicate @src, but for @new_sheet.
 */
GnmSolverParameters *
gnm_solver_param_dup (GnmSolverParameters *src, Sheet *new_sheet)
{
	GnmSolverParameters *dst = gnm_solver_param_new (new_sheet);
	GSList *l;

	dst->problem_type = src->problem_type;
	dependent_managed_set_expr (&dst->target, src->target.base.texpr);
	dependent_managed_set_expr (&dst->input, src->input.base.texpr);

	g_free (dst->options.scenario_name);
	dst->options = src->options;
	dst->options.algorithm = NULL;
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
	    !gnm_expr_top_equal (a->target.base.texpr, b->target.base.texpr) ||
	    !gnm_expr_top_equal (a->input.base.texpr, b->input.base.texpr) ||
	    a->options.max_time_sec != b->options.max_time_sec ||
	    a->options.max_iter != b->options.max_iter ||
	    a->options.algorithm != b->options.algorithm ||
	    a->options.model_type != b->options.model_type ||
            a->options.assume_non_negative != b->options.assume_non_negative ||
            a->options.assume_discrete != b->options.assume_discrete ||
            a->options.automatic_scaling != b->options.automatic_scaling ||
            a->options.program_report != b->options.program_report ||
            a->options.sensitivity_report != b->options.sensitivity_report ||
            a->options.add_scenario != b->options.add_scenario ||
	    strcmp (a->options.scenario_name, b->options.scenario_name) ||
	    a->options.gradient_order != b->options.gradient_order)
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

/**
 * gnm_solver_param_get_input:
 * @sp: #GnmSolverParameters
 *
 * Returns: (transfer none) (nullable): the input cell area.
 */
GnmValue const *
gnm_solver_param_get_input (GnmSolverParameters const *sp)
{
	return sp->input.base.texpr
		? gnm_expr_top_get_constant (sp->input.base.texpr)
		: NULL;
}

/**
 * gnm_solver_param_set_input:
 * @sp: #GnmSolverParameters
 * @v: (transfer full) (nullable): new input area
 */
void
gnm_solver_param_set_input (GnmSolverParameters *sp, GnmValue *v)
{
	GnmExprTop const *texpr = v ? gnm_expr_top_new_constant (v) : NULL;
	dependent_managed_set_expr (&sp->input, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

static GnmValue *
cb_grab_cells (GnmCellIter const *iter, gpointer user)
{
	GPtrArray *input_cells = user;
	GnmCell *cell;

	if (NULL == (cell = iter->cell))
		cell = sheet_cell_create (iter->pp.sheet,
			iter->pp.eval.col, iter->pp.eval.row);
	g_ptr_array_add (input_cells, cell);
	return NULL;
}

/**
 * gnm_solver_param_get_input_cells:
 * @sp: #GnmSolverParameters
 *
 * Returns: (element-type GnmCell) (transfer container):
 */
GPtrArray *
gnm_solver_param_get_input_cells (GnmSolverParameters const *sp)
{
	GnmValue const *vr = gnm_solver_param_get_input (sp);
	GPtrArray *input_cells = g_ptr_array_new ();

	if (vr) {
		GnmEvalPos ep;
		eval_pos_init_sheet (&ep, sp->sheet);
		workbook_foreach_cell_in_range (&ep, vr, CELL_ITER_ALL,
						cb_grab_cells,
						input_cells);
	}

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
	return sp->target.base.texpr
		? gnm_expr_top_get_cellref (sp->target.base.texpr)
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
	GPtrArray *input_cells;
	unsigned ui;

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
	for (ui = 0; ui < input_cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (input_cells, ui);
		if (gnm_cell_has_expr (cell)) {
			char *cname = gnm_solver_cell_name (cell, sp->sheet);
			g_set_error (err,
				     go_error_invalid (),
				     0,
				     _("Input cell %s contains a formula"),
				     cname);
			g_free (cname);
			g_ptr_array_free (input_cells, TRUE);
			return FALSE;
		}
	}
	g_ptr_array_free (input_cells, TRUE);

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
	sp->options.gradient_order = 10;

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
	SOL_SIG_LAST
};

static guint solver_signals[SOL_SIG_LAST] = { 0 };

enum {
	SOL_PROP_0,
	SOL_PROP_STATUS,
	SOL_PROP_REASON,
	SOL_PROP_PARAMS,
	SOL_PROP_RESULT,
	SOL_PROP_SENSITIVITY,
	SOL_PROP_STARTTIME,
	SOL_PROP_ENDTIME,
	SOL_PROP_FLIP_SIGN
};

static GObjectClass *gnm_solver_parent_class;

static void gnm_solver_update_derived (GnmSolver *sol);

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

	if (sol->sensitivity) {
		g_object_unref (sol->sensitivity);
		sol->sensitivity = NULL;
	}

	if (sol->params) {
		g_object_unref (sol->params);
		sol->params = NULL;
		gnm_solver_update_derived (sol);
	}

	sol->gradient_status = 0;
	if (sol->gradient) {
		g_ptr_array_unref (sol->gradient);
		sol->gradient = NULL;
	}

	sol->hessian_status = 0;
	if (sol->hessian) {
		g_ptr_array_unref (sol->hessian);
		sol->hessian = NULL;
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

	case SOL_PROP_SENSITIVITY:
		g_value_set_object (value, sol->sensitivity);
		break;

	case SOL_PROP_STARTTIME:
		g_value_set_double (value, sol->starttime);
		break;

	case SOL_PROP_ENDTIME:
		g_value_set_double (value, sol->endtime);
		break;

	case SOL_PROP_FLIP_SIGN:
		g_value_set_boolean (value, sol->flip_sign);
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

	case SOL_PROP_PARAMS: {
		GnmSolverParameters *p = g_value_dup_object (value);
		if (sol->params) g_object_unref (sol->params);
		sol->params = p;
		gnm_solver_update_derived (sol);
		break;
	}

	case SOL_PROP_RESULT: {
		GnmSolverResult *r = g_value_dup_object (value);
		if (sol->result) g_object_unref (sol->result);
		sol->result = r;
		break;
	}

	case SOL_PROP_SENSITIVITY: {
		GnmSolverSensitivity *s = g_value_dup_object (value);
		if (sol->sensitivity) g_object_unref (sol->sensitivity);
		sol->sensitivity = s;
		break;
	}

	case SOL_PROP_STARTTIME:
		sol->starttime = g_value_get_double (value);
		break;

	case SOL_PROP_ENDTIME:
		sol->endtime = g_value_get_double (value);
		break;

	case SOL_PROP_FLIP_SIGN:
		sol->flip_sign = g_value_get_boolean (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/**
 * gnm_solver_prepare: (virtual prepare)
 * @sol: solver
 * @wbc: control for user interaction
 * @err: location to store error
 *
 * Prepare for solving.  Preparation need not do anything, but may include
 * such tasks as checking that the model is valid for the solver and
 * locating necessary external programs.
 *
 * Returns: %TRUE ok success, %FALSE on error.
 */
gboolean
gnm_solver_prepare (GnmSolver *sol, WorkbookControl *wbc, GError **err)
{
	gboolean res;

	g_return_val_if_fail (GNM_IS_SOLVER (sol), FALSE);
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY, FALSE);

	if (gnm_solver_debug ())
		g_printerr ("Prepararing solver\n");

	gnm_solver_update_derived (sol);

	g_signal_emit (sol, solver_signals[SOL_SIG_PREPARE], 0, wbc, err, &res);
	return res;
}

/**
 * gnm_solver_start: (virtual start)
 * @sol: solver
 * @wbc: control for user interaction
 * @err: location to store error
 *
 * Start the solving process.  If needed, the solver will be prepared first.
 *
 * Returns: %TRUE ok success, %FALSE on error.
 */
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

	if (gnm_solver_debug ())
		g_printerr ("Starting solver\n");

	g_signal_emit (sol, solver_signals[SOL_SIG_START], 0, wbc, err, &res);
	return res;
}

/**
 * gnm_solver_stop: (virtual stop)
 * @sol: solver
 * @err: location to store error
 *
 * Terminate the currently-running solver.
 *
 * Returns: %TRUE ok success, %FALSE on error.
 */
gboolean
gnm_solver_stop (GnmSolver *sol, GError **err)
{
	gboolean res;

	g_return_val_if_fail (GNM_IS_SOLVER (sol), FALSE);

	if (gnm_solver_debug ())
		g_printerr ("Stopping solver\n");

	g_signal_emit (sol, solver_signals[SOL_SIG_STOP], 0, err, &res);
	return res;
}

static double
current_time (void)
{
	return g_get_monotonic_time () / 1e6;
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
	gnm_float const *solution;
	unsigned ui, n = sol->input_cells->len;

	g_return_if_fail (GNM_IS_SOLVER (sol));
	g_return_if_fail (sol->result != NULL);
	g_return_if_fail (sol->result->solution);

	solution = gnm_solver_has_solution (sol)
		? sol->result->solution
		: NULL;

	for (ui = 0; ui < n; ui++) {
		GnmCell *cell = g_ptr_array_index (sol->input_cells, ui);
		GnmValue *v = solution ? value_new_float (solution[ui])	: value_new_error_NA (NULL);
		gnm_cell_set_value (cell, v);
		cell_queue_recalc (cell);
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

	if (gnm_solver_debug ())
		g_printerr ("Reason: %s\n", reason ? reason : "-");

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

static gnm_float
get_cell_value (GnmCell *cell)
{
	GnmValue const *v;
	gnm_cell_eval (cell);
	v = cell->value;
	return VALUE_IS_NUMBER (v) || VALUE_IS_EMPTY (v)
		? value_get_as_float (v)
		: gnm_nan;
}

gboolean
gnm_solver_check_constraints (GnmSolver *solver)
{
	GSList *l;
	GnmSolverParameters *sp = solver->params;
	GnmCell *target_cell;

	if (sp->options.assume_non_negative ||
	    sp->options.assume_discrete) {
		unsigned ui;
		gboolean bad;

		for (ui = 0; ui < solver->input_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (solver->input_cells, ui);
			gnm_float val = get_cell_value (cell);

			if (!gnm_finite (val))
				break;
			if (sp->options.assume_non_negative && val < 0)
				break;
			if (sp->options.assume_discrete &&
			    val != gnm_floor (val))
				break;
		}
		bad = (ui < solver->input_cells->len);

		if (bad)
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
			if (lhs)
				cl = get_cell_value (lhs);
			if (rhs)
				cr = get_cell_value (rhs);

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
	workbook_view_save_to_output (wbv, fs, output, io_context);
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

int
gnm_solver_cell_index (GnmSolver *solver, GnmCell const *cell)
{
	gpointer idx;

	if (g_hash_table_lookup_extended (solver->index_from_cell,
					  (gpointer)cell,
					  NULL, &idx))
		return GPOINTER_TO_INT (idx);
	else
		return -1;
}

static int
cell_in_cr (GnmSolver *sol, GnmCell const *cell, gboolean follow)
{
	int idx;

	if (!cell)
		return -1;

	idx = gnm_solver_cell_index (sol, cell);
	if (idx < 0 && follow) {
		/* If the expression is just =X42 then look at X42 instead.
		   This is because the mps loader uses such a level of
		   indirection.  Note: we follow only one such step.  */
		GnmCellRef const *cr = gnm_expr_top_get_cellref (cell->base.texpr);
		GnmCellRef cr2;
		GnmCell const *new_cell;
		GnmEvalPos ep;

		if (!cr)
			return -1;

		eval_pos_init_cell (&ep, cell);
		gnm_cellref_make_abs (&cr2, cr, &ep);
		new_cell = sheet_cell_get (eval_sheet (cr2.sheet, cell->base.sheet),
					   cr2.col, cr2.row);
		return cell_in_cr (sol, new_cell, FALSE);
	}

	return idx;
}

static gboolean
cell_is_constant (GnmCell *cell, gnm_float *pc)
{
	if (!cell) {
		*pc = 0;
		return TRUE;
	}

	if (cell->base.texpr)
		return FALSE;

	*pc = get_cell_value (cell);
	return gnm_finite (*pc);
}

#define SET_LOWER(l_)							\
	do {								\
		sol->min[idx] = MAX (sol->min[idx], (gnm_float)(l_));	\
	} while (0)

#define SET_UPPER(l_)							\
	do {								\
		sol->max[idx] = MIN (sol->max[idx], (gnm_float)(l_));	\
	} while (0)



static void
gnm_solver_update_derived (GnmSolver *sol)
{
	GnmSolverParameters *params = sol->params;

	if (sol->input_cells) {
		g_ptr_array_free (sol->input_cells, TRUE);
		sol->input_cells = NULL;
	}

	if (sol->index_from_cell) {
		g_hash_table_destroy (sol->index_from_cell);
		sol->index_from_cell = NULL;
	}
	sol->target = NULL;

	g_free (sol->min);
	sol->min = NULL;

	g_free (sol->max);
	sol->max = NULL;

	g_free (sol->discrete);
	sol->discrete = NULL;

	if (params) {
		unsigned ui, n;
		GSList *l;

		sol->target = gnm_solver_param_get_target_cell (params);

		sol->input_cells = gnm_solver_param_get_input_cells (params);

		n = sol->input_cells->len;
		sol->index_from_cell = g_hash_table_new (g_direct_hash, g_direct_equal);
		for (ui = 0; ui < n; ui++) {
			GnmCell *cell = g_ptr_array_index (sol->input_cells, ui);
			g_hash_table_insert (sol->index_from_cell, cell, GUINT_TO_POINTER (ui));
		}

		sol->min = g_new (gnm_float, n);
		sol->max = g_new (gnm_float, n);
		sol->discrete = g_new (guint8, n);
		for (ui = 0; ui < n; ui++) {
			sol->min[ui] = params->options.assume_non_negative ? 0 : gnm_ninf;
			sol->max[ui] = gnm_pinf;
			sol->discrete[ui] = params->options.assume_discrete;
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
				int idx = cell_in_cr (sol, lhs, TRUE);

				if (idx < 0)
					continue;
				if (!cell_is_constant (rhs, &cr))
					continue;

				switch (c->type) {
				case GNM_SOLVER_INTEGER:
					sol->discrete[idx] = TRUE;
					break;
				case GNM_SOLVER_BOOLEAN:
					sol->discrete[idx] = TRUE;
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

		/*
		 * If parameters are discrete, narrow the range by eliminating
		 * the fractional part of the limits.
		 */
		for (ui = 0; ui < n; ui++) {
			if (sol->discrete[ui]) {
				sol->min[ui] = gnm_ceil (sol->min[ui]);
				sol->max[ui] = gnm_floor (sol->max[ui]);
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
	(gnm_finite (l_) ? gnm_abs ((s_) - (l_)) <= (gnm_abs ((s_)) + gnm_abs ((l_))) / GNM_const(1e10) : (s_) == (l_))

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

static void
print_vector (const char *name, const gnm_float *v, int n)
{
	int i;

	if (name)
		g_printerr ("%s:\n", name);
	for (i = 0; i < n; i++)
		g_printerr ("%15.8" GNM_FORMAT_f " ", v[i]);
	g_printerr ("\n");
}

static void
gnm_solver_create_program_report (GnmSolver *solver, const char *name)
{
	GnmSolverParameters *params = solver->params;
	int R = 0;
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

	if (solver->input_cells->len > 0) {
		unsigned ui;

		ADD_HEADER (_("Variables"));

		dao_set_cell (dao, 1, R, _("Cell"));
		dao_set_cell (dao, 2, R, _("Value"));
		dao_set_cell (dao, 3, R, _("Lower"));
		dao_set_cell (dao, 4, R, _("Upper"));
		dao_set_cell (dao, 5, R, _("Slack"));
		R++;

		for (ui = 0; ui < solver->input_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (solver->input_cells, ui);
			gnm_float L = solver->min[ui];
			gnm_float H = solver->max[ui];
			gnm_float s = solver->result->solution[ui];
			gnm_float slack = MIN (s - L, H - s);

			char *cname = gnm_solver_cell_name (cell, params->sheet);
			dao_set_cell (dao, 1, R, cname);
			g_free (cname);
			dao_set_cell_value (dao, 2, R, value_new_float (s));
			add_value_or_special (dao, 3, R, L);
			add_value_or_special (dao, 4, R, H);

			add_value_or_special (dao, 5, R, slack);
			if (slack < 0)
				MARK_BAD (5);

			if (AT_LIMIT (s, L) || AT_LIMIT (s, H))
				dao_set_cell (dao, 6, R, _("At limit"));

			if (s < L || s > H) {
				dao_set_cell (dao, 7, R, _("Outside bounds"));
				MARK_BAD (7);
			}

			R++;
		}

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
			char *ctxt = gnm_solver_constraint_part_as_str (c, i, params);
			dao_set_cell (dao, 1, R, ctxt);
			g_free (ctxt);

			if (lhs)
				cl = get_cell_value (lhs);
			if (rhs)
				cr = get_cell_value (rhs);

			switch (c->type) {
			case GNM_SOLVER_INTEGER: {
				gnm_float c = gnm_fake_round (cl);
				slack = 0 - gnm_abs (c - cl);
				break;
			}
			case GNM_SOLVER_BOOLEAN: {
				gnm_float c = (cl > GNM_const(0.5) ? 1 : 0);
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

	dao_autofit_columns (dao);
	dao_redraw_respan (dao);

	dao_free (dao);
}

static void
gnm_solver_create_sensitivity_report (GnmSolver *solver, const char *name)
{
	GnmSolverParameters *params = solver->params;
	GnmSolverSensitivity *sols = solver->sensitivity;
	int R = 0;
	data_analysis_output_t *dao;
	GSList *l;

	if (!sols)
		return;

	dao = dao_init_new_sheet (NULL);
	dao->sheet = params->sheet;
	dao_prepare_output (NULL, dao, name);

	/* ---------------------------------------- */

	if (solver->input_cells->len > 0) {
		unsigned ui;

		ADD_HEADER (_("Variables"));

		dao_set_cell (dao, 1, R, _("Cell"));
		dao_set_cell (dao, 2, R, _("Final\nValue"));
		dao_set_cell (dao, 3, R, _("Reduced\nCost"));
		dao_set_cell (dao, 4, R, _("Lower\nLimit"));
		dao_set_cell (dao, 5, R, _("Upper\nLimit"));
		dao_set_align (dao, 1, R, 5, R, GNM_HALIGN_CENTER, GNM_VALIGN_BOTTOM);
		dao_autofit_these_rows (dao, R, R);
		R++;

		for (ui = 0; ui < solver->input_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (solver->input_cells, ui);
			gnm_float L = sols->vars[ui].low;
			gnm_float H = sols->vars[ui].high;
			gnm_float red = sols->vars[ui].reduced_cost;
			gnm_float s = solver->result->solution[ui];

			char *cname = gnm_solver_cell_name (cell, params->sheet);
			dao_set_cell (dao, 1, R, cname);
			g_free (cname);
			dao_set_cell_value (dao, 2, R, value_new_float (s));
			add_value_or_special (dao, 3, R, red);
			add_value_or_special (dao, 4, R, L);
			add_value_or_special (dao, 5, R, H);

			R++;
		}

		R++;
	}

	/* ---------------------------------------- */

	ADD_HEADER (_("Constraints"));

	if (params->constraints) {
		dao_set_cell (dao, 1, R, _("Constraint"));
		dao_set_cell (dao, 2, R, _("Shadow\nPrice"));
		dao_set_cell (dao, 3, R, _("Constraint\nLHS"));
		dao_set_cell (dao, 4, R, _("Constraint\nRHS"));
		dao_set_cell (dao, 5, R, _("Lower\nLimit"));
		dao_set_cell (dao, 6, R, _("Upper\nLimit"));
		dao_set_align (dao, 1, R, 6, R, GNM_HALIGN_CENTER, GNM_VALIGN_BOTTOM);
		dao_autofit_these_rows (dao, R, R);
	} else {
		dao_set_cell (dao, 1, R, _("No constraints"));
	}
	R++;

	for (l = params->constraints; l; l = l->next) {
		GnmSolverConstraint *c = l->data;
		int i, cidx = 0;
		gnm_float cl, cr;
		GnmCell *lhs, *rhs;

		for (i = 0;
		     gnm_solver_constraint_get_part (c, params, i,
						     &lhs, &cl,
						     &rhs, &cr);
		     i++, cidx++) {
			char *ctxt;

			switch (c->type) {
			case GNM_SOLVER_INTEGER:
			case GNM_SOLVER_BOOLEAN:
				continue;
			default:
				; // Nothing
			}

			ctxt = gnm_solver_constraint_part_as_str (c, i, params);
			dao_set_cell (dao, 1, R, ctxt);
			g_free (ctxt);

			if (lhs)
				cl = get_cell_value (lhs);
			if (rhs)
				cr = get_cell_value (rhs);

			add_value_or_special (dao, 2, R, sols->constraints[cidx].shadow_price);
			add_value_or_special (dao, 3, R, cl);
			add_value_or_special (dao, 4, R, cr);
			add_value_or_special (dao, 5, R, sols->constraints[cidx].low);
			add_value_or_special (dao, 6, R, sols->constraints[cidx].high);

			R++;
		}
	}

	/* ---------------------------------------- */

	dao_autofit_columns (dao);
	dao_redraw_respan (dao);

	dao_free (dao);
}

void
gnm_solver_create_report (GnmSolver *solver, const char *base)
{
	GnmSolverParameters *params = solver->params;

	if (params->options.program_report) {
		char *name = g_strdup_printf (base, _("Program"));
		gnm_solver_create_program_report (solver, name);
		g_free (name);
	}

	if (params->options.sensitivity_report) {
		char *name = g_strdup_printf (base, _("Sensitivity"));
		gnm_solver_create_sensitivity_report (solver, name);
		g_free (name);
	}
}

#undef AT_LIMIT
#undef ADD_HEADER
#undef MARK_BAD

/**
 * gnm_solver_get_target_value:
 * @solver: solver
 *
 * Returns: the current value of the target cell, possibly with the sign
 * flipped.
 */
gnm_float
gnm_solver_get_target_value (GnmSolver *solver)
{
	gnm_float y = get_cell_value (solver->target);
	return solver->flip_sign ? 0 - y : y;
}

void
gnm_solver_set_var (GnmSolver *sol, int i, gnm_float x)
{
	GnmCell *cell = g_ptr_array_index (sol->input_cells, i);

	if (cell->value &&
	    VALUE_IS_FLOAT (cell->value) &&
	    value_get_as_float (cell->value) == x)
		return;

	gnm_cell_set_value (cell, value_new_float (x));
	cell_queue_recalc (cell);
}

void
gnm_solver_set_vars (GnmSolver *sol, gnm_float const *xs)
{
	const int n = sol->input_cells->len;
	int i;

	for (i = 0; i < n; i++)
		gnm_solver_set_var (sol, i, xs[i]);
}

/**
 * gnm_solver_save_vars:
 * @sol: #GnmSolver
 *
 * Returns: (transfer full) (element-type GnmValue):
 */
GPtrArray *
gnm_solver_save_vars (GnmSolver *sol)
{
	GPtrArray *vals = g_ptr_array_new ();
	unsigned ui;

	for (ui = 0; ui < sol->input_cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (sol->input_cells, ui);
		g_ptr_array_add (vals, value_dup (cell->value));
	}

	return vals;
}

/**
 * gnm_solver_restore_vars:
 * @sol: #GnmSolver
 * @vals: (transfer full) (element-type GnmValue): values to restore
 */
void
gnm_solver_restore_vars (GnmSolver *sol, GPtrArray *vals)
{
	unsigned ui;

	for (ui = 0; ui < sol->input_cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (sol->input_cells, ui);
		gnm_cell_set_value (cell, g_ptr_array_index (vals, ui));
		cell_queue_recalc (cell);
	}

	g_ptr_array_free (vals, TRUE);
}

/**
 * gnm_solver_has_analytic_gradient:
 * @sol: the solver
 *
 * Returns: %TRUE if the gradient can be computed analytically.
 */
gboolean
gnm_solver_has_analytic_gradient (GnmSolver *sol)
{
	const int n = sol->input_cells->len;

	if (sol->gradient_status == 0) {
		int i;

		sol->gradient_status++;

		sol->gradient = g_ptr_array_new_with_free_func ((GDestroyNotify)gnm_expr_top_unref);
		for (i = 0; i < n; i++) {
			GnmCell *cell = g_ptr_array_index (sol->input_cells, i);
			GnmExprTop const *te =
				gnm_expr_cell_deriv (sol->target, cell);
			if (te)
				g_ptr_array_add (sol->gradient, (gpointer)te);
			else {
				if (gnm_solver_debug ())
					g_printerr ("Unable to compute analytic gradient\n");
				g_ptr_array_unref (sol->gradient);
				sol->gradient = NULL;
				sol->gradient_status++;
				break;
			}
		}
	}

	return sol->gradient_status == 1;
}

static gnm_float *
gnm_solver_compute_gradient_analytically (GnmSolver *sol, gnm_float const *xs)
{
	const int n = sol->input_cells->len;
	int i;
	gnm_float *g = g_new (gnm_float, n);
	GnmEvalPos ep;

	eval_pos_init_cell (&ep, sol->target);
	for (i = 0; i < n; i++) {
		GnmExprTop const *te = g_ptr_array_index (sol->gradient, i);
		GnmValue *v = gnm_expr_top_eval
			(te, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		g[i] = VALUE_IS_NUMBER (v) ? value_get_as_float (v) : gnm_nan;
		if (sol->flip_sign)
			g[i] = 0 - g[i];
		value_release (v);
	}

	if (gnm_solver_debug ())
		print_vector ("Analytic gradient", g, n);

	return g;
}

/**
 * gnm_solver_compute_gradient: (skip)
 * @sol: Solver
 * @xs: Point to compute gradient at
 *
 * Returns: xxx(transfer full): A vector containing the gradient.  This
 * function takes the flip-sign property into account.  This will use
 * analytic gradient, if possible, and a numerical approximation otherwise.
 */
gnm_float *
gnm_solver_compute_gradient (GnmSolver *sol, gnm_float const *xs)
{
	gnm_float *g;
	gnm_float y0;
	const int n = sol->input_cells->len;
	int i;
	const int order = sol->params->options.gradient_order;

	gnm_solver_set_vars (sol, xs);
	y0 = gnm_solver_get_target_value (sol);

	if (gnm_solver_has_analytic_gradient (sol))
		return gnm_solver_compute_gradient_analytically (sol, xs);

	g = g_new (gnm_float, n);
	for (i = 0; i < n; i++) {
		gnm_float x0 = xs[i];
		gnm_float dx, dy;
		int j;

		/*
		 * This computes the least-squares fit of an affine function
		 * based on 2*order equidistant points symmetrically around
		 * the value we compute the derivative for.
		 *
		 * We use an even number of ULPs for the step size.  This
		 * ensures that the x values are computed without rounding
		 * error except, potentially, a single step that crosses an
		 * integer power of 2.
		 */
		dx = 16 * (gnm_add_epsilon (x0) - x0);
		dy = 0;
		for (j = -order; j <= order; j++) {
			gnm_float y;

			if (j == 0)
				continue;

			gnm_solver_set_var (sol, i, x0 + j * dx);
			y = gnm_solver_get_target_value (sol);
			dy += j * (y - y0);
		}
		dy /= 2 * (order * (2 * order * order + 3 * order + 1) / 6);
		g[i] = dy / dx;

		gnm_solver_set_var (sol, i, x0);
	}

	if (gnm_solver_debug ())
		print_vector ("Numerical gradient", g, n);

	return g;
}

/**
 * gnm_solver_has_analytic_hessian:
 * @sol: the solver
 *
 * Returns: %TRUE if the Hessian can be computed analytically.
 */
gboolean
gnm_solver_has_analytic_hessian (GnmSolver *sol)
{
	const int n = sol->input_cells->len;
	int i, j;
	GnmEvalPos ep;
	GnmExprDeriv *info;

	if (!gnm_solver_has_analytic_gradient (sol))
		sol->hessian_status = sol->gradient_status;

	if (sol->hessian_status)
		return sol->hessian_status == 1;

	sol->hessian_status++;
	sol->hessian = g_ptr_array_new_with_free_func ((GDestroyNotify)gnm_expr_top_unref);

	eval_pos_init_cell (&ep, sol->target);
	info = gnm_expr_deriv_info_new ();
	for (i = 0; i < n && sol->hessian_status == 1; i++) {
		GnmExprTop const *gi = g_ptr_array_index (sol->gradient, i);
		for (j = i; j < n; j++) {
			GnmCell *cell;
			GnmExprTop const *te;
			GnmEvalPos var;

			cell = g_ptr_array_index (sol->input_cells, j);
			eval_pos_init_cell (&var, cell);
			gnm_expr_deriv_info_set_var (info, &var);
			te = gnm_expr_top_deriv (gi, &ep, info);

			if (te)
				g_ptr_array_add (sol->hessian, (gpointer)te);
			else {
				if (gnm_solver_debug ())
					g_printerr ("Unable to compute analytic hessian\n");
				sol->hessian_status++;
				break;
			}
		}
	}

	gnm_expr_deriv_info_unref (info);

	return sol->hessian_status == 1;
}

/**
 * gnm_solver_compute_hessian:
 * @sol: Solver
 * @xs: Point to compute Hessian at
 *
 * Returns: (transfer full): A matrix containing the Hessian.  This
 * function takes the flip-sign property into account.
 */
GnmMatrix *
gnm_solver_compute_hessian (GnmSolver *sol, gnm_float const *xs)
{
	int i, j, k;
	GnmMatrix *H;
	GnmEvalPos ep;
	int const n = sol->input_cells->len;

	if (!gnm_solver_has_analytic_hessian (sol))
		return NULL;

	gnm_solver_set_vars (sol, xs);

	H = gnm_matrix_new (n, n);
	eval_pos_init_cell (&ep, sol->target);
	for (i = k = 0; i < n; i++) {
		for (j = i; j < n; j++, k++) {
			GnmExprTop const *te =
				g_ptr_array_index (sol->hessian, k);
			GnmValue *v = gnm_expr_top_eval
				(te, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
			gnm_float x = VALUE_IS_NUMBER (v)
				? value_get_as_float (v)
				: gnm_nan;
			if (sol->flip_sign)
				x = 0 - x;
			value_release (v);

			H->data[i][j] = x;
			H->data[j][i] = x;
		}
	}

	return H;
}


static gnm_float
try_step (GnmSolver *sol, gnm_float const *x0, gnm_float const *dir, gnm_float step)
{
	int const n = sol->input_cells->len;
	gnm_float *x = g_new (gnm_float, n);
	int i;
	gnm_float y;

	for (i = 0; i < n; i++)
		x[i] = x0[i] + step * dir[i];
	gnm_solver_set_vars (sol, x);
	y = gnm_solver_get_target_value (sol);
	g_free (x);
	return y;
}

/**
 * gnm_solver_line_search:
 * @sol: Solver
 * @x0: Starting point
 * @dir: direction
 * @try_reverse: whether to try reverse direction at all
 * @step: initial step size
 * @max_step: largest allowed step
 * @eps: tolerance for optimal step
 * @py: (out): location to store resulting objective function value
 *
 * Returns: optimal step size.
 */
gnm_float
gnm_solver_line_search (GnmSolver *sol,
			gnm_float const *x0, gnm_float const *dir,
			gboolean try_reverse,
			gnm_float step, gnm_float max_step, gnm_float eps,
			gnm_float *py)
{
	/*
	 * 0: Initial
	 * 1: Found improvement, but not far side of it
	 * 2: Have (s0,s1,s2) with s1 lowest
	 */
	int phase = 0;
	gnm_float s, s0, s1, s2;
	gnm_float y, y0, y1, y2;
	gnm_float const phi = (gnm_sqrt (5) + 1) / 2;
	gboolean rbig;
	gboolean debug = gnm_solver_debug ();

	g_return_val_if_fail (eps >= 0, gnm_nan);
	g_return_val_if_fail (step > 0, gnm_nan);
	g_return_val_if_fail (max_step >= step, gnm_nan);

	if (debug) {
		g_printerr ("LS: step=%" GNM_FORMAT_g ", max=%" GNM_FORMAT_g ", eps=%" GNM_FORMAT_g "\n", step, max_step, eps);
		print_vector (NULL, dir, sol->input_cells->len);
	}

	gnm_solver_set_vars (sol, x0);
	y0 = gnm_solver_get_target_value (sol);
	s0 = 0;

	s = step;
	while (phase == 0) {
		gboolean flat = TRUE;

		y = try_step (sol, x0, dir, s);
		if (0 && debug)
			g_printerr ("LS0: s:%.6" GNM_FORMAT_g "  y:%.6" GNM_FORMAT_g "\n",
				    s, y);
		if (y < y0 && gnm_solver_check_constraints (sol)) {
			y1 = y;
			s1 = s;
			phase = 1;
			break;
		} else if (y != y0)
			flat = FALSE;

		if (try_reverse) {
			y = try_step (sol, x0, dir, -s);
			if (0 && debug)
				g_printerr ("LS0: s:%.6" GNM_FORMAT_g "  y:%.6" GNM_FORMAT_g "\n",
					    -s, y);
			if (y < y0 && gnm_solver_check_constraints (sol)) {
				y1 = y;
				s1 = -s;
				phase = 1;
				break;
			} else if (y != y0)
				flat = FALSE;
		}

		s /= 32;

		if (s <= 0 || flat)
			return gnm_nan;
	}

	while (phase == 1) {
		s = s1 * (phi + 1);

		if (gnm_abs (s) >= max_step)
			goto bail;

		y = try_step (sol, x0, dir, s);
		if (!gnm_finite (y) || !gnm_solver_check_constraints (sol))
			goto bail;

		if (y < y1) {
			y1 = y;
			s1 = s;
			continue;
		}

		y2 = y;
		s2 = s;
		phase = 2;
	}

	/*
	 * Phase 2: we have three steps, s0/s1/s2, in order (descending or ascending) such
	 * that
	 *   1.  y1<=y0 (equality unlikely)
	 *   2.  y1<=y2 (equality unlikely)
	 *   3a. (s2-s1)=phi*(s1-s0) or
	 *   3b. (s2-s1)*phi=(s1-s0)
	 */
	rbig = TRUE;  /* Initially 3a holds. */
	while (phase == 2) {
		if (0 && debug) {
			gnm_float s01 = s1 - s0;
			gnm_float s12 = s2 - s1;
			g_printerr ("LS2: s0:%.6" GNM_FORMAT_g "  s01=%.6" GNM_FORMAT_g "  s12=%.6" GNM_FORMAT_g
				    "  r=%" GNM_FORMAT_g
				    "  y:%.10" GNM_FORMAT_g "/%.10" GNM_FORMAT_g "/%.10" GNM_FORMAT_g "\n",
				    s0, s01, s12, s12 / s01, y0, y1, y2);
		}

		s = rbig ? s1 + (s1 - s0) * (phi - 1) : s1 - (s2 - s1) * (phi - 1);
		if (s <= s0 || s >= s2 || gnm_abs (s - s1) <= eps)
			break;

		y = try_step (sol, x0, dir, s);
		if (!gnm_finite (y) || !gnm_solver_check_constraints (sol))
			goto bail;

		if (y < y1) {
			if (rbig) {
				y0 = y1;
				s0 = s1;
			} else {
				y2 = y1;
				s2 = s1;
			}
			y1 = y;
			s1 = s;
		} else {
			if (rbig) {
				y2 = y;
				s2 = s;
			} else {
				y0 = y;
				s0 = s;
			}
			rbig = !rbig;

			if (y0 == y1 && y1 == y2)
				break;
		}
	}

bail:
	if (debug)
		g_printerr ("LS: step %.6" GNM_FORMAT_g "\n", s1);

	*py = y1;
	return s1;
}

/**
 * gnm_solver_pick_lp_coords:
 * @sol: Solver
 * @px1: (out): first coordinate value
 * @px2: (out): second coordinate value
 *
 * Pick two good values for each coordinate.  We prefer 0 and 1
 * when they are valid.
 */
void
gnm_solver_pick_lp_coords (GnmSolver *sol,
			   gnm_float **px1, gnm_float **px2)
{
	const unsigned n = sol->input_cells->len;
	gnm_float *x1 = *px1 = g_new (gnm_float, n);
	gnm_float *x2 = *px2 = g_new (gnm_float, n);
	unsigned ui;

	for (ui = 0; ui < n; ui++) {
		const gnm_float L = sol->min[ui], H = sol->max[ui];

		if (L == H) {
			x1[ui] = x2[ui] = L;
		} else if (sol->discrete[ui] && H - L == 1) {
			x1[ui] = L;
			x2[ui] = H;
		} else {
			if (L <= 0 && H >= 0)
				x1[ui] = 0;
			else if (gnm_finite (L))
				x1[ui] = L;
			else
				x1[ui] = H;

			if (x1[ui] + 1 <= H)
				x2[ui] = x1[ui] + 1;
			else if (x1[ui] - 1 >= H)
				x2[ui] = x1[ui] - 1;
			else if (x1[ui] != H)
				x2[ui] = (x1[ui] + H) / 2;
			else
				x2[ui] = (x1[ui] + L) / 2;
		}
	}
}

/**
 * gnm_solver_get_lp_coeffs: (skip)
 * @sol: Solver
 * @ycell: Cell for which to compute coefficients
 * @x1: first coordinate value
 * @x2: second coordinate value
 * @err: error location
 *
 * Returns: xxx(transfer full) (nullable): coordinates, or %NULL in case of error.
 * Note: this function is not affected by the flip-sign property, even
 * if @ycell happens to coincide with the solver target cell.
 */
gnm_float *
gnm_solver_get_lp_coeffs (GnmSolver *sol, GnmCell *ycell,
			  gnm_float const *x1, gnm_float const *x2,
			  GError **err)
{
	const unsigned n = sol->input_cells->len;
	unsigned ui;
	gnm_float *res = g_new (gnm_float, n);
	gnm_float y0;

	gnm_solver_set_vars (sol, x1);
	y0 = get_cell_value (ycell);
	if (!gnm_finite (y0))
		goto fail_calc;

	for (ui = 0; ui < n; ui++) {
		gnm_float dx = x2[ui] - x1[ui], dy, y1;

		if (dx <= 0) {
			res[ui] = 0;
			continue;
		}

		gnm_solver_set_var (sol, ui, x2[ui]);
		y1 = get_cell_value (ycell);

		dy = y1 - y0;
		res[ui] = dy / dx;
		if (!gnm_finite (res[ui]))
			goto fail_calc;

		if (!sol->discrete[ui] || dx != 1) {
			gnm_float x01, y01, e, emax;

			x01 = (x1[ui] + x2[ui]) / 2;
			if (sol->discrete[ui]) x01 = gnm_floor (x01);
			gnm_solver_set_var (sol, ui, x01);
			y01 = get_cell_value (ycell);
			if (!gnm_finite (y01))
				goto fail_calc;

			emax = dy == 0
				? GNM_const(1e-10)
				: gnm_abs (dy) / GNM_const(1e-10);  // ????
			e = dy - 2 * (y01 - y0);
			if (gnm_abs (e) > emax)
				goto fail_linear;
		}

		gnm_solver_set_var (sol, ui, x1[ui]);
	}

	return res;

fail_calc:
	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("Target cell did not evaluate to a number."));
	g_free (res);
	return NULL;

fail_linear:
	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("Target cell does not appear to depend linearly on input cells."));
	g_free (res);
	return NULL;
}


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

	g_object_class_install_property (object_class, SOL_PROP_SENSITIVITY,
		g_param_spec_object ("sensitivity",
				     P_("Sensitivity"),
				     P_("Sensitivity results"),
				     GNM_SOLVER_SENSITIVITY_TYPE,
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

	g_object_class_install_property (object_class, SOL_PROP_FLIP_SIGN,
		g_param_spec_boolean ("flip-sign",
				      P_("Flip Sign"),
				      P_("Flip sign of target value"),
				      FALSE,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));

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
}

GSF_CLASS (GnmSolver, gnm_solver,
	   &gnm_solver_class_init, NULL, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_solver_result_parent_class;

static void
gnm_solver_result_finalize (GObject *obj)
{
	GnmSolverResult *r = GNM_SOLVER_RESULT (obj);
	g_free (r->solution);
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

static GObjectClass *gnm_solver_sensitivity_parent_class;

enum {
	SOLS_PROP_0,
	SOLS_PROP_SOLVER
};

static void
gnm_solver_sensitivity_constructed (GObject *obj)
{
	GnmSolverSensitivity *sols = GNM_SOLVER_SENSITIVITY (obj);
	GnmSolver *sol = sols->solver;
	GnmSolverParameters *sp = sol->params;
	const int n = sol->input_cells->len;
	int i, cn;
	GSList *l;

	/* Chain to parent first */
	gnm_solver_sensitivity_parent_class->constructed (obj);

	sols->vars = g_new (struct GnmSolverSensitivityVars_, n);
	for (i = 0; i < n; i++) {
		sols->vars[i].low = gnm_nan;
		sols->vars[i].high = gnm_nan;
		sols->vars[i].reduced_cost = gnm_nan;
	}

	cn = 0;
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
			cn++;
		}
	}
	sols->constraints = g_new (struct GnmSolverSensitivityConstraints_, cn);
	for (i = 0; i < cn; i++) {
		sols->constraints[i].low = gnm_nan;
		sols->constraints[i].high = gnm_nan;
		sols->constraints[i].shadow_price = gnm_nan;
	}
}

static void
gnm_solver_sensitivity_finalize (GObject *obj)
{
	GnmSolverSensitivity *r = GNM_SOLVER_SENSITIVITY (obj);
	g_free (r->vars);
	g_free (r->constraints);
	gnm_solver_sensitivity_parent_class->finalize (obj);
}

static void
gnm_solver_sensitivity_get_property (GObject *object, guint property_id,
				     GValue *value, GParamSpec *pspec)
{
	GnmSolverSensitivity *sols = (GnmSolverSensitivity *)object;

	switch (property_id) {
	case SOLS_PROP_SOLVER:
		g_value_set_object (value, sols->solver);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_sensitivity_set_property (GObject *object, guint property_id,
				     GValue const *value, GParamSpec *pspec)
{
	GnmSolverSensitivity *sols = (GnmSolverSensitivity *)object;

	switch (property_id) {
	case SOLS_PROP_SOLVER:
		/* We hold no ref.  */
		sols->solver = g_value_get_object (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_sensitivity_class_init (GObjectClass *object_class)
{
	gnm_solver_sensitivity_parent_class =
		g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_solver_sensitivity_finalize;
	object_class->constructed = gnm_solver_sensitivity_constructed;
	object_class->set_property = gnm_solver_sensitivity_set_property;
	object_class->get_property = gnm_solver_sensitivity_get_property;

	g_object_class_install_property
		(object_class, SOLS_PROP_SOLVER,
		 g_param_spec_object ("solver",
				      P_("Solver"),
				      P_("Solver"),
				      GNM_SOLVER_TYPE,
				      GSF_PARAM_STATIC |
				      G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_READWRITE));
}

GSF_CLASS (GnmSolverSensitivity, gnm_solver_sensitivity,
	   gnm_solver_sensitivity_class_init, NULL, G_TYPE_OBJECT)

GnmSolverSensitivity *
gnm_solver_sensitivity_new (GnmSolver *sol)
{
	return g_object_new (GNM_SOLVER_SENSITIVITY_TYPE, "solver", sol, NULL);
}

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_sub_solver_parent_class;

enum {
	SUB_SOL_SIG_CHILD_EXIT,
	SUB_SOL_SIG_LAST
};

static guint sub_solver_signals[SUB_SOL_SIG_LAST] = { 0 };

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

	if (subsol->cell_from_name)
		g_hash_table_remove_all (subsol->cell_from_name);

	if (subsol->name_from_cell)
		g_hash_table_remove_all (subsol->name_from_cell);

	if (subsol->constraint_from_name)
		g_hash_table_remove_all (subsol->constraint_from_name);
}

static void
gnm_sub_solver_dispose (GObject *obj)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (obj);

	gnm_sub_solver_clear (subsol);

	gnm_sub_solver_parent_class->dispose (obj);
}

static void
gnm_sub_solver_finalize (GObject *obj)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (obj);

	/*
	 * The weird finalization in gnm_lpsolve_final makes it important that
	 * we leave the object in a state that gnm_sub_solver_clear is happy
	 * with.
	 */

	g_hash_table_destroy (subsol->cell_from_name);
	subsol->cell_from_name = NULL;

	g_hash_table_destroy (subsol->name_from_cell);
	subsol->name_from_cell = NULL;

	g_hash_table_destroy (subsol->constraint_from_name);
	subsol->constraint_from_name = NULL;

	gnm_sub_solver_parent_class->finalize (obj);
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

	subsol->constraint_from_name =
		g_hash_table_new_full (g_str_hash, g_str_equal,
				       g_free, NULL);
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

	g_signal_emit (subsol, sub_solver_signals[SUB_SOL_SIG_CHILD_EXIT], 0,
		       normal, code);

	if (subsol->child_pid) {
		g_spawn_close_pid (subsol->child_pid);
		subsol->child_pid = (GPid)0;
	}
}

/**
 * gnm_sub_solver_spawn: (skip)
 */
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

const char *
gnm_sub_solver_name_constraint (GnmSubSolver *subsol,
				int cidx,
				const char *name)
{
	char *name_copy = g_strdup (name);

	g_hash_table_insert (subsol->constraint_from_name,
			     name_copy,
			     GINT_TO_POINTER (cidx));

	return name_copy;
}

int
gnm_sub_solver_find_constraint (GnmSubSolver *subsol, const char *name)
{
	gpointer idx;

	if (g_hash_table_lookup_extended (subsol->constraint_from_name,
					  (gpointer)name,
					  NULL, &idx))
		return GPOINTER_TO_INT (idx);
	else
		return -1;
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
	go_gtk_dialog_add_button (GTK_DIALOG (fsel), GNM_STOCK_CANCEL,
				  "gtk-cancel", GTK_RESPONSE_CANCEL);
	go_gtk_dialog_add_button (GTK_DIALOG (fsel), GNM_STOCK_OK,
				  "system-run", GTK_RESPONSE_OK);
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
	object_class->finalize = gnm_sub_solver_finalize;

	sub_solver_signals[SUB_SOL_SIG_CHILD_EXIT] =
		g_signal_new ("child-exit",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSubSolverClass, child_exit),
			      NULL, NULL,
			      gnm__VOID__BOOLEAN_INT,
			      G_TYPE_NONE, 2,
			      G_TYPE_BOOLEAN, G_TYPE_INT);
}

GSF_CLASS (GnmSubSolver, gnm_sub_solver,
	   gnm_sub_solver_class_init, gnm_sub_solver_init, GNM_SOLVER_TYPE)

/* ------------------------------------------------------------------------- */

enum {
	SOL_ITER_SIG_ITERATE,
	SOL_ITER_SIG_LAST
};

static guint solver_iterator_signals[SOL_ITER_SIG_LAST] = { 0 };

static void
gnm_solver_iterator_class_init (GObjectClass *object_class)
{
	solver_iterator_signals[SOL_ITER_SIG_ITERATE] =
		g_signal_new ("iterate",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverIteratorClass, iterate),
			      NULL, NULL,
			      gnm__BOOLEAN__VOID,
			      G_TYPE_BOOLEAN, 0);
}

gboolean
gnm_solver_iterator_iterate (GnmSolverIterator *iter)
{
	gboolean progress = FALSE;
	g_signal_emit (iter, solver_iterator_signals[SOL_ITER_SIG_ITERATE], 0, &progress);
	return progress;
}

/**
 * gnm_solver_iterator_new_func: (skip)
 */
GnmSolverIterator *
gnm_solver_iterator_new_func (GCallback iterate, gpointer user)
{
	GnmSolverIterator *iter;

	iter = g_object_new (GNM_SOLVER_ITERATOR_TYPE, NULL);
	g_signal_connect (iter, "iterate", G_CALLBACK (iterate), user);
	return iter;
}

static gboolean
cb_polish_iter (GnmSolverIterator *iter, GnmIterSolver *isol)
{
	GnmSolver *sol = GNM_SOLVER (isol);
	const int n = sol->input_cells->len;
	gnm_float *dir;
	gboolean progress = FALSE;
	int c;

	dir = g_new0 (gnm_float, n);
	for (c = 0; c < n; c++) {
		gnm_float s, y, s0, sm, xc = isol->xk[c];

		if (xc == 0) {
			s0 = 0.5;
			sm = 1;
		} else {
			int e;
			(void)gnm_frexp (xc, &e);
			s0 = gnm_ldexp (1, e - 10);
			if (s0 == 0) s0 = GNM_MIN;
			sm = gnm_abs (xc);
		}

		dir[c] = 1;
		s = gnm_solver_line_search (sol, isol->xk, dir, TRUE,
					    s0, sm, 0.0, &y);
		dir[c] = 0;

		if (gnm_finite (s) && s != 0) {
			isol->xk[c] += s;
			isol->yk = y;
			progress = TRUE;
		}
	}
	g_free (dir);

	if (progress)
		gnm_iter_solver_set_solution (isol);

	return progress;
}

/**
 * gnm_solver_iterator_new_polish:
 * @isol: the solver to operate on
 *
 * Returns: (transfer full): an iterator object that can be used to polish
 * a solution by simple axis-parallel movement.
 */
GnmSolverIterator *
gnm_solver_iterator_new_polish (GnmIterSolver *isol)
{
	return gnm_solver_iterator_new_func (G_CALLBACK (cb_polish_iter), isol);
}


static gboolean
cb_gradient_iter (GnmSolverIterator *iter, GnmIterSolver *isol)
{
	GnmSolver *sol = GNM_SOLVER (isol);
	const int n = sol->input_cells->len;
	gboolean progress = FALSE;
	gnm_float s, y;
	gnm_float *g;
	int i;

	/* Search in opposite direction of gradient.  */
	g = gnm_solver_compute_gradient (sol, isol->xk);
	for (i = 0; i < n; i++)
		g[i] = -g[i];

	s = gnm_solver_line_search (sol, isol->xk, g, FALSE,
				    1, gnm_pinf, 0.0, &y);
	if (s > 0) {
		for (i = 0; i < n; i++)
			isol->xk[i] += s * g[i];
		isol->yk = y;
		progress = TRUE;
	}

	g_free (g);

	if (progress)
		gnm_iter_solver_set_solution (isol);

	return progress;
}

/**
 * gnm_solver_iterator_new_gradient:
 * @isol: the solver to operate on
 *
 * Returns: (transfer full): an iterator object that can be used to perform
 * a gradient descent step.
 */
GnmSolverIterator *
gnm_solver_iterator_new_gradient (GnmIterSolver *isol)
{
	return gnm_solver_iterator_new_func (G_CALLBACK (cb_gradient_iter), isol);
}

GSF_CLASS (GnmSolverIterator, gnm_solver_iterator,
	   gnm_solver_iterator_class_init, NULL, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

enum {
	SOLIC_PROP_0,
	SOLIC_PROP_CYCLES
};

static GObjectClass *gnm_solver_iterator_compound_parent_class;

/**
 * gnm_solver_iterator_compound_add:
 * @ic: Compound iterator
 * @iter: (transfer full): sub-iterator
 * @count: repeat count
 *
 * Add an iterator to a compound iterator with a given repeat count.  As a
 * special case, a repeat count of zero means to try the iterator once
 * in a cycle, but only if no other sub-iterator has shown any progress so far.
 */
void
gnm_solver_iterator_compound_add (GnmSolverIteratorCompound *ic,
				  GnmSolverIterator *iter,
				  unsigned count)
{
	g_ptr_array_add (ic->iterators, iter);
	ic->counts = g_renew (unsigned, ic->counts, ic->iterators->len);
	ic->counts[ic->iterators->len - 1] = count;
}

static gboolean
gnm_solver_iterator_compound_iterate (GnmSolverIterator *iter)
{
	GnmSolverIteratorCompound *ic = (GnmSolverIteratorCompound *)iter;
	gboolean progress;

	while (TRUE) {
		if (ic->cycle >= ic->cycles)
			return FALSE;

		if (ic->next >= ic->iterators->len) {
			/* We've been through all iterators.  */
			if (!ic->cycle_progress)
				return FALSE;
			ic->cycle_progress = FALSE;
			ic->next = 0;
			ic->next_counter = 0;
			ic->cycle++;
			continue;
		}

		if (ic->next_counter < ic->counts[ic->next])
			break;

		/* Special case: when count==0, use only if no progress.  */
		if (!ic->cycle_progress && ic->next_counter == 0)
			break;

		ic->next++;
		ic->next_counter = 0;
	}

	progress = gnm_solver_iterator_iterate (g_ptr_array_index (ic->iterators, ic->next));
	if (progress) {
		ic->cycle_progress = TRUE;
		ic->next_counter++;
	} else {
		/* No progress, so don't retry.  */
		ic->next++;
		ic->next_counter = 0;
	}

	/* Report progress as long as we have stuff to try.  */
	return TRUE;
}

static void
gnm_solver_iterator_compound_init (GnmSolverIteratorCompound *ic)
{
	ic->iterators = g_ptr_array_new ();
	ic->cycles = G_MAXUINT;
}

static void
gnm_solver_iterator_compound_finalize (GObject *obj)
{
	GnmSolverIteratorCompound *ic = (GnmSolverIteratorCompound *)obj;
	g_ptr_array_foreach (ic->iterators, (GFunc)g_object_unref, NULL);
	g_ptr_array_free (ic->iterators, TRUE);
	g_free (ic->counts);
	gnm_solver_iterator_compound_parent_class->finalize (obj);
}

static void
gnm_solver_iterator_compound_get_property (GObject *object, guint property_id,
					   GValue *value, GParamSpec *pspec)
{
	GnmSolverIteratorCompound *it = (GnmSolverIteratorCompound *)object;

	switch (property_id) {
	case SOLIC_PROP_CYCLES:
		g_value_set_uint (value, it->cycles);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_iterator_compound_set_property (GObject *object, guint property_id,
					   GValue const *value, GParamSpec *pspec)
{
	GnmSolverIteratorCompound *it = (GnmSolverIteratorCompound *)object;

	switch (property_id) {
	case SOLIC_PROP_CYCLES:
		it->cycles = g_value_get_uint (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_iterator_compound_class_init (GObjectClass *object_class)
{
	GnmSolverIteratorClass *iclass = (GnmSolverIteratorClass *)object_class;

	gnm_solver_iterator_compound_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_solver_iterator_compound_finalize;
	object_class->set_property = gnm_solver_iterator_compound_set_property;
	object_class->get_property = gnm_solver_iterator_compound_get_property;
	iclass->iterate = gnm_solver_iterator_compound_iterate;

	g_object_class_install_property (object_class, SOLIC_PROP_CYCLES,
		g_param_spec_uint ("cycles",
				   P_("Cycles"),
				   P_("Maximum number of cycles"),
				   0, G_MAXUINT, G_MAXUINT,
				    GSF_PARAM_STATIC | G_PARAM_READWRITE));
}

GSF_CLASS (GnmSolverIteratorCompound, gnm_solver_iterator_compound,
	   gnm_solver_iterator_compound_class_init, gnm_solver_iterator_compound_init, GNM_SOLVER_ITERATOR_TYPE)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_iter_solver_parent_class;

void
gnm_iter_solver_set_iterator (GnmIterSolver *isol, GnmSolverIterator *iterator)
{
	GnmSolverIterator *old_iterator;

	g_return_if_fail (GNM_IS_ITER_SOLVER (isol));

	old_iterator = isol->iterator;
	isol->iterator = iterator ? g_object_ref (iterator) : NULL;
	if (old_iterator)
		g_object_unref (old_iterator);
}

gboolean
gnm_iter_solver_get_initial_solution (GnmIterSolver *isol, GError **err)
{
	GnmSolver *sol = GNM_SOLVER (isol);
	const int n = sol->input_cells->len;
	int i;

	if (gnm_solver_check_constraints (sol))
		goto got_it;

	/* More? */

	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("The initial values do not satisfy the constraints."));
	return FALSE;

got_it:
	for (i = 0; i < n; i++) {
		GnmCell *cell = g_ptr_array_index (sol->input_cells, i);
		isol->xk[i] = value_get_as_float (cell->value);
	}
	isol->yk = gnm_solver_get_target_value (sol);

	gnm_iter_solver_set_solution (isol);

	return TRUE;
}

void
gnm_iter_solver_set_solution (GnmIterSolver *isol)
{
	GnmSolver *sol = GNM_SOLVER (isol);
	GnmSolverResult *result = g_object_new (GNM_SOLVER_RESULT_TYPE, NULL);
	const int n = sol->input_cells->len;

	result->quality = GNM_SOLVER_RESULT_FEASIBLE;
	result->value = sol->flip_sign ? 0 - isol->yk : isol->yk;
	result->solution = go_memdup_n (isol->xk, n, sizeof (gnm_float));
	g_object_set (sol, "result", result, NULL);
	g_object_unref (result);

	if (!gnm_solver_check_constraints (sol)) {
		g_printerr ("Infeasible solution set\n");
	}
}

static void
gnm_iter_solver_clear (GnmIterSolver *isol)
{
	if (isol->idle_tag) {
		g_source_remove (isol->idle_tag);
		isol->idle_tag = 0;
	}
}

static void
gnm_iter_solver_dispose (GObject *obj)
{
	GnmIterSolver *isol = GNM_ITER_SOLVER (obj);
	gnm_iter_solver_clear (isol);
	gnm_iter_solver_parent_class->dispose (obj);
}

static void
gnm_iter_solver_finalize (GObject *obj)
{
	GnmIterSolver *isol = GNM_ITER_SOLVER (obj);
	g_free (isol->xk);
	gnm_iter_solver_parent_class->finalize (obj);
}

static void
gnm_iter_solver_constructed (GObject *obj)
{
	GnmIterSolver *isol = GNM_ITER_SOLVER (obj);
	GnmSolver *sol = GNM_SOLVER (obj);

	/* Chain to parent first */
	gnm_iter_solver_parent_class->constructed (obj);

	isol->xk = g_new0 (gnm_float, sol->input_cells->len);
}

static void
gnm_iter_solver_init (GnmIterSolver *isol)
{
}

static gint
gnm_iter_solver_idle (gpointer data)
{
	GnmIterSolver *isol = data;
	GnmSolver *sol = &isol->parent;
	GnmSolverParameters *params = sol->params;
	gboolean progress;

	progress = isol->iterator && gnm_solver_iterator_iterate (isol->iterator);
	isol->iterations++;

	if (!gnm_solver_finished (sol)) {
		if (!progress) {
			gnm_solver_set_status (sol, GNM_SOLVER_STATUS_DONE);
		} else if (isol->iterations >= params->options.max_iter) {
			gnm_solver_stop (sol, NULL);
			gnm_solver_set_reason (sol, _("Iteration limit exceeded"));
		}
	}

	if (gnm_solver_finished (sol)) {
		isol->idle_tag = 0;

		gnm_app_recalc ();

		return FALSE;
	} else {
		/* Call again.  */
		return TRUE;
	}
}

static gboolean
gnm_iter_solver_start (GnmSolver *solver, WorkbookControl *wbc, GError **err)
{
	GnmIterSolver *isol = GNM_ITER_SOLVER (solver);

	g_return_val_if_fail (isol->idle_tag == 0, FALSE);

	isol->idle_tag = g_idle_add (gnm_iter_solver_idle, solver);
	gnm_solver_set_status (solver, GNM_SOLVER_STATUS_RUNNING);

	return TRUE;
}

static gboolean
gnm_iter_solver_stop (GnmSolver *solver, GError **err)
{
	GnmIterSolver *isol = GNM_ITER_SOLVER (solver);
	GnmSolver *sol = &isol->parent;

	gnm_iter_solver_clear (isol);

	g_clear_object (&isol->iterator);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_CANCELLED);

	return TRUE;
}

static void
gnm_iter_solver_class_init (GObjectClass *object_class)
{
	GnmSolverClass *sclass = (GnmSolverClass *)object_class;

	gnm_iter_solver_parent_class = g_type_class_peek_parent (object_class);

	object_class->dispose = gnm_iter_solver_dispose;
	object_class->finalize = gnm_iter_solver_finalize;
	object_class->constructed = gnm_iter_solver_constructed;
	sclass->start = gnm_iter_solver_start;
	sclass->stop = gnm_iter_solver_stop;
}

GSF_CLASS (GnmIterSolver, gnm_iter_solver,
	   gnm_iter_solver_class_init, gnm_iter_solver_init, GNM_SOLVER_TYPE)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_solver_factory_parent_class;

static void
gnm_solver_factory_finalize (GObject *obj)
{
	GnmSolverFactory *factory = GNM_SOLVER_FACTORY (obj);

	if (factory->notify)
		factory->notify (factory->data);

	g_free (factory->id);
	g_free (factory->name);

	gnm_solver_factory_parent_class->finalize (obj);
}

static void
gnm_solver_factory_class_init (GObjectClass *object_class)
{
	debug_factory = gnm_debug_flag ("solver-factory");

	gnm_solver_factory_parent_class =
		g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_solver_factory_finalize;
}

GSF_CLASS (GnmSolverFactory, gnm_solver_factory,
	   gnm_solver_factory_class_init, NULL, G_TYPE_OBJECT)


static GSList *solvers;

/**
 * gnm_solver_db_get:
 *
 * Returns: (transfer none) (element-type GnmSolverFactory): list of
 * registered solver factories.
 */
GSList *
gnm_solver_db_get (void)
{
	return solvers;
}

/**
 * gnm_solver_factory_new:
 * @id: Unique identifier
 * @name: Translated name for UI purposes
 * @type: Model type created by factory
 * @creator: (scope notified): callback for creating a solver
 * @functional: (scope notified): callback for checking if factory is functional
 * @data: User pointer for @creator and @functional
 * @notify: Destroy notification for @data.
 *
 * Returns: (transfer full): a new #GnmSolverFactory
 */
GnmSolverFactory *
gnm_solver_factory_new (const char *id,
			const char *name,
			GnmSolverModelType type,
			GnmSolverCreator creator,
			GnmSolverFactoryFunctional functional,
			gpointer data,
			GDestroyNotify notify)
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
	res->data = data;
	res->notify = notify;
	return res;
}

/**
 * gnm_solver_factory_create:
 * @factory: #GnmSolverFactory
 * @param: #GnmSolverParameters
 *
 * Returns: (transfer full): a new #GnmSolver
 */
GnmSolver *
gnm_solver_factory_create (GnmSolverFactory *factory,
			   GnmSolverParameters *param)
{
	g_return_val_if_fail (GNM_IS_SOLVER_FACTORY (factory), NULL);

	if (debug_factory)
		g_printerr ("Creating solver instance from %s\n",
			    factory->id);
	return factory->creator (factory, param, factory->data);
}

gboolean
gnm_solver_factory_functional (GnmSolverFactory *factory,
			       WBCGtk *wbcg)
{
	if (factory == NULL)
		return FALSE;

	return (factory->functional == NULL ||
		factory->functional (factory, wbcg, factory->data));
}

static int
cb_compare_factories (GnmSolverFactory *a, GnmSolverFactory *b)
{
	return go_utf8_collate_casefold (a->name, b->name);
}

void
gnm_solver_db_register (GnmSolverFactory *factory)
{
	if (debug_factory)
		g_printerr ("Registering %s\n", factory->id);
	g_object_ref (factory);
	solvers = g_slist_insert_sorted (solvers, factory,
					 (GCompareFunc)cb_compare_factories);
}

void
gnm_solver_db_unregister (GnmSolverFactory *factory)
{
	if (debug_factory)
		g_printerr ("Unregistering %s\n", factory->id);
	solvers = g_slist_remove (solvers, factory);
	g_object_unref (factory);
}

/* ------------------------------------------------------------------------- */
