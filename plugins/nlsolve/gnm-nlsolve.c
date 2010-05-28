#include <gnumeric-config.h>
#include "gnumeric.h"
#include <tools/gnm-solver.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "ranges.h"
#include "regression.h"
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define PRIVATE_KEY "::nlsolve::"

typedef struct {
	GnmSolver *parent;

	/* Input cells.  */
	GPtrArray *vars;

	/* Target cell. */
	GnmCell *target;

	gboolean debug;

	int iterations;

	/* Next axis direction to try.  */
	int dim;

	/* Parameters: */
	gnm_float eps;
	int max_iter;
	gnm_float min_factor;

	guint idle_tag;
} GnmNlsolve;

static void
gnm_nlsolve_cleanup (GnmNlsolve *nl)
{
	if (nl->idle_tag) {
		g_source_remove (nl->idle_tag);
		nl->idle_tag = 0;
	}

	if (nl->vars) {
		g_ptr_array_free (nl->vars, TRUE);
		nl->vars = NULL;
	}
}

static void
gnm_nlsolve_final (GnmNlsolve *nl)
{
	gnm_nlsolve_cleanup (nl);
	g_free (nl);
}

static gboolean
check_program (const GnmSolverParameters *params, GError **err)
{
	GSList *l;

	if (params->options.assume_discrete)
		goto no_discrete;

	for (l = params->constraints; l; l = l->next) {
		GnmSolverConstraint *c  = l->data;
		if (c->type == GNM_SOLVER_INTEGER ||
		    c->type == GNM_SOLVER_BOOLEAN)
			goto no_discrete;
	}

	if (params->problem_type != GNM_SOLVER_MINIMIZE) {
		/* Not a fundamental problem, just not done.  */
		g_set_error (err,
			     go_error_invalid (),
			     0,
			     _("This solver handles only minimization."));
		return FALSE;
	}

	return TRUE;

no_discrete:
	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("This solver does not handle discrete variables."));
	return FALSE;
}

static void
set_value (GnmNlsolve *nl, int i, gnm_float x)
{
	GnmCell *cell = g_ptr_array_index (nl->vars, i);
	if (cell->value &&
	    VALUE_IS_FLOAT (cell->value) &&
	    value_get_as_float (cell->value) == x)
		return;

	gnm_cell_set_value (cell, value_new_float (x));
	cell_queue_recalc (cell);
}

static void
set_vector (GnmNlsolve *nl, const gnm_float *xs)
{
	const int n = nl->vars->len;
	int i;

	for (i = 0; i < n; i++)
		set_value (nl, i, xs[i]);
}

static gnm_float
get_value (GnmNlsolve *nl)
{
	GnmValue const *v;

	gnm_cell_eval (nl->target);
	v = nl->target->value;

	if (VALUE_IS_NUMBER (v) || VALUE_IS_EMPTY (v))
		return value_get_as_float (v);
	else
		return gnm_nan;
}

static void
free_matrix (gnm_float **m, int n)
{
	int i;
	for (i = 0; i < n; i++)
		g_free (m[i]);
	g_free (m);
}

static void
gnm_nlsolve_set_solution (GnmNlsolve *nl)
{
	GnmSolver *sol = nl->parent;
	GnmSolverResult *result = g_object_new (GNM_SOLVER_RESULT_TYPE, NULL);

	result->solution = gnm_solver_get_current_values (sol);
	result->quality = GNM_SOLVER_RESULT_FEASIBLE;
	result->value = get_value (nl);

	g_object_set (sol, "result", result, NULL);
	g_object_unref (result);
}

static gboolean
gnm_nlsolve_get_initial_solution (GnmNlsolve *nl, GError **err)
{
	GnmSolver *sol = nl->parent;

	if (gnm_solver_check_constraints (sol))
		goto got_it;

	/* More? */

	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("The initial values do not satisfy the constraints."));
	return FALSE;

got_it:
	gnm_nlsolve_set_solution (nl);
	return TRUE;
}

static gboolean
gnm_nlsolve_prepare (GnmSolver *sol, WorkbookControl *wbc, GError **err,
		     GnmNlsolve *nl)
{
	gboolean ok;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY, FALSE);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARING);

	ok = check_program (sol->params, err);
	if (ok)
		ok = gnm_nlsolve_get_initial_solution (nl, err);

	if (ok) {
		if (nl->debug)
			g_printerr ("Initial value:\n%15.8" GNM_FORMAT_f "\n",
				    sol->result->value);
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARED);
	} else {
		gnm_nlsolve_cleanup (nl);
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
	}

	return ok;
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

static gnm_float *
compute_gradient (GnmNlsolve *nl, const gnm_float *xs)
{
	gnm_float *g;
	gnm_float y0;
	const int n = nl->vars->len;
	int i;

	set_vector (nl, xs);
	y0 = get_value (nl);
	
	g = g_new (gnm_float, n);
	for (i = 0; i < n; i++) {
		gnm_float x0 = xs[i];
		gnm_float dx;
		gnm_float y1;
		gnm_float eps = gnm_pow2 (-25);

		if (x0 == 0)
			dx = eps;
		else
			dx = gnm_abs (x0) * eps;

		set_value (nl, i, x0 + dx);
		y1 = get_value (nl);
#if 0
		g_printerr ("  y0=%g   y1=%g\n", y0, y1);
#endif
		g[i] = (y1 - y0) / dx;

		set_value (nl, i, x0);
	}

	return g;
}

static gnm_float **
compute_hessian (GnmNlsolve *nl, const gnm_float *xs, const gnm_float *g0)
{
	gnm_float **H, *xs2;
	const int n = nl->vars->len;
	int i, j;

	H = g_new (gnm_float *, n);

	xs2 = g_memdup (xs, n * sizeof (gnm_float));
	for (i = 0; i < n; i++) {
		gnm_float x0 = xs[i];
		gnm_float dx;
		const gnm_float *g;
		gnm_float eps = gnm_pow2 (-25);

		if (x0 == 0)
			dx = eps;
		else
			dx = gnm_abs (x0) * eps;

		xs2[i] = x0 + dx;
		g = H[i] = compute_gradient (nl, xs2);
		xs2[i] = x0;

		if (nl->debug) {
			int j;

			g_printerr ("  Gradient %d ", i);
			for (j = 0; j < n; j++)
				g_printerr ("%15.8" GNM_FORMAT_f " ", g[j]);
			g_printerr ("\n");
		}
		for (j = 0; j < n; j++)
			H[i][j] = (g[j] - g0[j]) / dx;

		set_value (nl, i, x0);
	}

	g_free (xs2);
	return H;
}

static gboolean
try_direction (GnmNlsolve *nl, const gnm_float *x0, const gnm_float *d)
{
	GnmSolver *sol = nl->parent;
	const int n = nl->vars->len;
	gnm_float factor;

	for (factor = 1; factor >= nl->min_factor; factor /= 2) {
		gnm_float y;
		int i;

		for (i = 0; i < n; i++) {
			gnm_float x = x0[i] - factor * d[i];
			set_value (nl, i, x);
		}

		y = get_value (nl);
		if (nl->debug)
			g_printerr ("Factor=%-10" GNM_FORMAT_g
				    "  value=%.8" GNM_FORMAT_f "\n",
				    factor, y);

		if (y < sol->result->value) {
			if (gnm_solver_check_constraints (sol)) {
				gnm_nlsolve_set_solution (nl);
				return TRUE;
			}
			if (nl->debug)
				g_printerr ("Would-be solution does not satisfy constraints.\n");
		}
	}

	return FALSE;
}

static gint
gnm_nlsolve_idle (gpointer data)
{
	GnmNlsolve *nl = data;
	GnmSolver *sol = nl->parent;
	gnm_float **H, *g, *d, *x0;
	int i;
	const int n = nl->vars->len;
	gboolean ok;
	gnm_float y0;
	gboolean call_again = TRUE;

	nl->iterations++;

	y0 = get_value (nl);

	x0 = g_new (gnm_float, n);
	for (i = 0; i < n; i++) {
		GnmCell *cell = g_ptr_array_index (nl->vars, i);
		x0[i] = value_get_as_float (cell->value);
	}
	g = compute_gradient (nl, x0);
	H = compute_hessian (nl, x0, g);

	if (nl->debug) {
		int i;

		print_vector ("x0", x0, n);
		print_vector ("Gradient", g, n);

		g_printerr ("Hessian:\n");
		for (i = 0; i < n; i++) {
			print_vector (NULL, H[i], n);
		}
	}

	d = g_new (gnm_float, n);
	ok = (gnm_linear_solve (H, g, n, d) == 0);
	if (ok) {
		if (nl->debug)
			print_vector ("Delta", d, n);
		ok = try_direction (nl, x0, d);
	}

	if (!ok) {
		int i, j;
		gnm_float *d1 = g_new (gnm_float, n);

		/*
		 * The Newton step failed.  This is likely because the
		 * surface is seriously non-linear.  Try one axis
		 * direction at a time.
		 */

		for (j = 0; !ok && j < n; j++) {
			int c = nl->dim++ % n;
			if (g[c] == 0)
				continue;

			for (i = 0; i < n; i++)
				d1[i] = (i == c)
					? y0 / g[c]
					: 0;

			ok = try_direction (nl, x0, d1);
		}

		g_free (d1);
	}

	if (!ok) {
		set_vector (nl, x0);
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_DONE);
		call_again = FALSE;
	}

	if (call_again && nl->iterations >= nl->max_iter) {
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_DONE);
		call_again = FALSE;
	}

	g_free (d);
	g_free (x0);
	g_free (g);
	free_matrix (H, n);

	return call_again;
}

static gboolean
gnm_nlsolve_start (GnmSolver *sol, WorkbookControl *wbc, GError **err,
		   GnmNlsolve *nl)
{
	gboolean ok = TRUE;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_PREPARED, FALSE);

	nl->idle_tag = g_idle_add (gnm_nlsolve_idle, nl);
	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_RUNNING);

	return ok;
}

static gboolean
gnm_nlsolve_stop (GnmSolver *sol, GError *err, GnmNlsolve *nl)
{
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_RUNNING, FALSE);

	gnm_nlsolve_cleanup (nl);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_CANCELLED);

	return TRUE;
}

gboolean
nlsolve_solver_factory_functional (GnmSolverFactory *factory);

gboolean
nlsolve_solver_factory_functional (GnmSolverFactory *factory)
{
	return TRUE;
}


GnmSolver *
nlsolve_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params);

GnmSolver *
nlsolve_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params)
{
	GnmSolver *res = g_object_new (GNM_SOLVER_TYPE,
				       "params", params,
				       NULL);
	GnmNlsolve *nl = g_new0 (GnmNlsolve, 1);
	GSList *input_cells, *l;

	nl->parent = GNM_SOLVER (res);

	nl->debug = gnm_solver_debug ();
	nl->eps = gnm_pow2 (-25);
	nl->max_iter = 1000;
	nl->min_factor = 1e-10;

	nl->target = gnm_solver_param_get_target_cell (params);

	nl->vars = g_ptr_array_new ();
	input_cells = gnm_solver_param_get_input_cells (params);
	for (l = input_cells; l; l = l->next)
		g_ptr_array_add (nl->vars, l->data);
	g_slist_free (input_cells);

	g_signal_connect (res, "prepare", G_CALLBACK (gnm_nlsolve_prepare), nl);
	g_signal_connect (res, "start", G_CALLBACK (gnm_nlsolve_start), nl);
	g_signal_connect (res, "stop", G_CALLBACK (gnm_nlsolve_stop), nl);

	g_object_set_data_full (G_OBJECT (res), PRIVATE_KEY, nl,
				(GDestroyNotify)gnm_nlsolve_final);

	return res;
}
