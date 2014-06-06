#include <gnumeric-config.h>
#include "gnumeric.h"
#include <tools/gnm-solver.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "regression.h"
#include "rangefunc.h"
#include "workbook.h"
#include "application.h"
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define PRIVATE_KEY "::nlsolve::"

/*
 * Note: the solver code assumes the problem is a minimization problem.
 * When used for a maximization problem, we flip the objective function
 * sign.  This is done in functions get_value and gnm_nlsolve_set_solution.
 */


typedef struct {
	GnmSolver *parent;

	/* Input/output cells.  */
	GPtrArray *vars;
	GnmCell *target;
	GnmCellPos origin;
	int input_width, input_height;
	gboolean maximize; /* See note above */

	/* Initial point.  */
	gnm_float *x0;

	/* Current point.  */
	gnm_float *xk, yk;
	int k;

	/* Rosenbrock state */
	gnm_float **xi;
	int smallsteps;
	int tentative;
	gnm_float *tentative_xk, tentative_yk;

	/* Parameters: */
	gboolean debug;
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
}

static void
gnm_nlsolve_final (GnmNlsolve *nl)
{
	gnm_nlsolve_cleanup (nl);
	if (nl->vars)
		g_ptr_array_free (nl->vars, TRUE);
	g_free (nl->xk);
	g_free (nl->x0);
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

	return TRUE;

no_discrete:
	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("This solver does not handle discrete variables."));
	return FALSE;
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

	if (VALUE_IS_NUMBER (v) || VALUE_IS_EMPTY (v)) {
		gnm_float y = value_get_as_float (v);
		return nl->maximize ? 0 - y : y;
	} else
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
	const int n = nl->vars->len;
	int i;

	result->quality = GNM_SOLVER_RESULT_FEASIBLE;
	result->value = nl->maximize ? 0 - nl->yk : nl->yk;
	result->solution = value_new_array_empty (nl->input_width,
						  nl->input_height);
	for (i = 0; i < n; i++) {
		GnmCell *cell = g_ptr_array_index (nl->vars, i);
		value_array_set (result->solution,
				 cell->pos.col - nl->origin.col,
				 cell->pos.row - nl->origin.row,
				 value_new_float (nl->xk[i]));
	}

	g_object_set (sol, "result", result, NULL);
	g_object_unref (result);

	if (!gnm_solver_check_constraints (sol)) {
		g_printerr ("Infeasible solution set\n");
	}
}

static gboolean
gnm_nlsolve_get_initial_solution (GnmNlsolve *nl, GError **err)
{
	GnmSolver *sol = nl->parent;
	const int n = nl->vars->len;
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
		GnmCell *cell = g_ptr_array_index (nl->vars, i);
		nl->xk[i] = nl->x0[i] = value_get_as_float (cell->value);
	}
	nl->yk = get_value (nl);
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
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARED);
	} else {
		gnm_nlsolve_cleanup (nl);
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
	}

	return ok;
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
newton_improve (GnmNlsolve *nl, gnm_float *xs, gnm_float *y, gnm_float ymax)
{
	GnmSolver *sol = nl->parent;
	const int n = nl->vars->len;
	gnm_float *g, **H, *d;
	gboolean ok;

	g = compute_gradient (nl, xs);
	H = compute_hessian (nl, xs, g);
	d = g_new (gnm_float, n);
	ok = (gnm_linear_solve (H, g, n, d) == 0);

	if (ok) {
		int i;
		gnm_float y2, *xs2 = g_new (gnm_float, n);
		gnm_float f, best_f = -1;

		ok = FALSE;
		for (f = 1; f > 1e-4; f /= 2) {
			int i;
			for (i = 0; i < n; i++)
				xs2[i] = xs[i] - f * d[i];
			set_vector (nl, xs2);
			y2 = get_value (nl);
			if (nl->debug) {
				print_vector ("xs2", xs2, n);
				g_printerr ("Obj value %.15" GNM_FORMAT_g "\n",
					    y2);
			}

			if (y2 < ymax && gnm_solver_check_constraints (sol)) {
				best_f = f;
				ymax = y2;
				break;
			}
		}

		if (best_f > 0) {
			for (i = 0; i < n; i++)
				xs[i] = xs[i] - best_f * d[i];
			*y = ymax;
			ok = TRUE;
		}

		g_free (xs2);
	} else {
		if (nl->debug)
			g_printerr ("Failed to solve Newton step.\n");
	}

	g_free (d);
	g_free (g);
	free_matrix (H, n);

	return ok;
}

static void
rosenbrock_init (GnmNlsolve *nl)
{
	const int n = nl->vars->len;
	int i, j;

	nl->xi = g_new (gnm_float *, n);
	for (i = 0; i < n; i++) {
		nl->xi[i] = g_new (gnm_float, n);
		for (j = 0; j < n; j++)
			nl->xi[i][j] = (i == j);
	}

	nl->smallsteps = 0;

	nl->tentative = 0;
	nl->tentative_xk = NULL;
}

static void
rosenbrock_tentative_end (GnmNlsolve *nl, gboolean accept)
{
	const int n = nl->vars->len;

	if (!accept && nl->tentative_xk) {
		nl->yk = nl->tentative_yk;
		memcpy (nl->xk, nl->tentative_xk, n * sizeof (gnm_float));
	}

	nl->tentative = 0;
	g_free (nl->tentative_xk);
	nl->tentative_xk = NULL;

	nl->smallsteps = 0;
}

static gboolean
rosenbrock_iter (GnmNlsolve *nl)
{
	GnmSolver *sol = nl->parent;
	const int n = nl->vars->len;
	int i, j;
	const gnm_float alpha = 3;
	const gnm_float beta = 0.5;
	gboolean any_at_all = FALSE;
	gnm_float *d, **A, *x, *dx, *t;
	char *state;
	int dones = 0;
	gnm_float ykm1 = nl->yk, *xkm1;
	gnm_float eps = gnm_pow2 (-16);
	int safety = 0;

	if (nl->tentative) {
		nl->tentative--;
		if (nl->tentative == 0) {
			if (nl->debug)
				g_printerr ("Tentative move rejected\n");
			rosenbrock_tentative_end (nl, FALSE);
		}
	}

	if (nl->k % 20 == 0) {
		for (i = 0; i < n; i++)
			for (j = 0; j < n; j++)
				nl->xi[i][j] = (i == j);
	}

	A = g_new (gnm_float *, n);
	for (i = 0; i < n; i++)
		A[i] = g_new (gnm_float, n);

	dx = g_new (gnm_float, n);
	for (i = 0; i < n; i++)
		dx[i] = 0;

	x = g_new (gnm_float, n);
	t = g_new (gnm_float, n);

	d = g_new (gnm_float, n);
	for (i = 0; i < n; i++) {
		d[i] = (nl->xk[i] == 0)
			? eps
			: gnm_abs (nl->xk[i]) * eps;
	}

	xkm1 = g_memdup (nl->xk, n * sizeof (gnm_float));

	state = g_new0 (char, n);

	while (dones < n) {
		/*
		 * A safety that shouldn't get hit, but might if the function
		 * being optimized is non-deterministic.
		 */
		if (safety++ > n * GNM_MANT_DIG)
			break;

		for (i = 0; i < n; i++) {
			gnm_float y;

			if (state[i] == 2)
				continue;

			/* x = xk + (d[i] * xi[i])  */
			for (j = 0; j < n; j++)
				x[j] = nl->xk[j] + d[i] * nl->xi[i][j];

			set_vector (nl, x);
			y = get_value (nl);

			if (y <= nl->yk && gnm_solver_check_constraints (sol)) {
				if (y < nl->yk) {
					nl->yk = y;
					memcpy (nl->xk, x, n * sizeof (gnm_float));
					dx[i] += d[i];
					any_at_all = TRUE;
				}
				switch (state[i]) {
				case 0:
					state[i] = 1;
					/* Fall through */
				case 1:
					d[i] *= alpha;
					break;
				default:
				case 2:
					break;
				}
			} else {
				switch (state[i]) {
				case 1:
					state[i] = 2;
					dones++;
					/* Fall through */
				case 0:
					d[i] *= -beta;
					break;
				default:
				case 2:
					/* No sign change. */
					d[i] *= 0.5;
					break;
				}
			}
		}
	}

	if (any_at_all) {
		gnm_float div, sum;

                for (j = n - 1; j >= 0; j--)
			for (i = 0; i < n; i++)
				A[j][i] = (j == n - 1 ? 0 : A[j + 1][i]) + dx[j] * nl->xi[j][i];

		sum = 0;
                for (i = n - 1; i >= 0; i--) {
			sum += dx[i] * dx[i];
			t[i] = sum;
		}

                for (i = n - 1; i > 0; i--) {
			div = gnm_sqrt (t[i - 1] * t[i]);
			if (div != 0)
				for (j = 0; j < n; j++) {
					nl->xi[i][j] = (dx[i - 1] * A[i][j] -
							nl->xi[i - 1][j] * t[i]) / div;
					g_assert (gnm_finite (nl->xi[i][j]));
				}
                }

		gnm_range_hypot (dx, n, &div);
		if (div != 0) {
			for (i = 0; i < n; i++) {
				nl->xi[0][i] = A[0][i] / div;
				if (!gnm_finite (nl->xi[0][i])) {
					g_printerr ("%g %g %g\n",
						    div, A[0][i], nl->xi[0][i]);
					g_assert (gnm_finite (nl->xi[0][i]));
				}
			}
		}

		/* ---------------------------------------- */

		if (!nl->tentative) {
			set_vector (nl, nl->xk);
			gnm_nlsolve_set_solution (nl);
		}

		if (nl->tentative) {
			if (nl->yk < nl->tentative_yk) {
				if (nl->debug)
					g_printerr ("Tentative move accepted!\n");
				rosenbrock_tentative_end (nl, TRUE);
			}
		} else if (gnm_abs (nl->yk - ykm1) > gnm_abs (ykm1) * 0.01) {
			/* A big step.  */
			nl->smallsteps = 0;
		} else {
			nl->smallsteps++;
		}

		if (!nl->tentative && nl->smallsteps > 50) {
			gnm_float yk = nl->yk;

			nl->tentative = 10;
			nl->tentative_xk = g_memdup (nl->xk, n * sizeof (gnm_float));
			nl->tentative_yk = yk;

			for (i = 0; i < 4; i++) {
				gnm_float ymax = yk +
					gnm_abs (yk) * (0.10 / (i + 1));
				if (i > 0)
					ymax = MIN (ymax, nl->yk);
				if (!newton_improve (nl, nl->xk, &nl->yk, ymax))
					break;
			}

			if (nl->debug)
				print_vector ("Tentative move to", nl->xk, n);
		}
	}

	g_free (x);
	g_free (xkm1);
	g_free (dx);
	g_free (t);
	g_free (d);
	free_matrix (A, n);
	g_free (state);

	return any_at_all;
}

static void
rosenbrock_shutdown (GnmNlsolve *nl)
{
	const int n = nl->vars->len;

	rosenbrock_tentative_end (nl, FALSE);

	free_matrix (nl->xi, n);
	nl->xi = NULL;
}

static gboolean
polish_iter (GnmNlsolve *nl)
{
	GnmSolver *sol = nl->parent;
	const int n = nl->vars->len;
	gnm_float *x;
	gnm_float step;
	gboolean any_at_all = FALSE;

	x = g_new (gnm_float, n);
	for (step = gnm_pow2 (-10); step > GNM_EPSILON; step *= 0.75) {
		int c, s;

		for (c = 0; c < n; c++) {
			for (s = 0; s <= 1; s++) {
				gnm_float y;
				gnm_float dx = step * gnm_abs (nl->xk[c]);

				if (dx == 0) dx = step;
				if (s) dx = -dx;

				memcpy (x, nl->xk, n * sizeof (gnm_float));
				x[c] += dx;
				set_vector (nl, x);
				y = get_value (nl);

				if (y < nl->yk && gnm_solver_check_constraints (sol))  {
					nl->yk = y;
					memcpy (nl->xk, x, n * sizeof (gnm_float));
					any_at_all = TRUE;
					if (nl->debug)
						g_printerr ("Polish step %.15" GNM_FORMAT_g
							    " in direction %d\n",
							    dx, c);
					break;
				}
			}
		}
	}

	g_free (x);

	if (any_at_all)
		gnm_nlsolve_set_solution (nl);

	return any_at_all;
}

static gint
gnm_nlsolve_idle (gpointer data)
{
	GnmNlsolve *nl = data;
	GnmSolver *sol = nl->parent;
	const int n = nl->vars->len;
	gboolean ok;
	gboolean call_again = TRUE;

	if (nl->k == 0)
		rosenbrock_init (nl);

	if (nl->debug) {
		g_printerr ("Iteration %d at %.15" GNM_FORMAT_g "\n",
			    nl->k, nl->yk);
		print_vector ("Current point", nl->xk, n);
	}

	nl->k++;
	ok = rosenbrock_iter (nl);

	if (!ok && !nl->tentative) {
		ok = polish_iter (nl);
	}

	if (!ok) {
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_DONE);
		call_again = FALSE;
	}

	if (call_again && nl->k >= nl->max_iter) {
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_DONE);
		call_again = FALSE;
	}

	if (!call_again) {
		set_vector (nl, nl->x0);
		gnm_app_recalc ();

		rosenbrock_shutdown (nl);
	}

	if (!call_again)
		nl->idle_tag = 0;

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
	int n;
	GnmValue const *vinput = gnm_solver_param_get_input (params);
	GnmEvalPos ep;
	GnmCellRef origin;

	nl->parent = GNM_SOLVER (res);

	nl->maximize = (params->problem_type == GNM_SOLVER_MAXIMIZE);

	eval_pos_init_sheet (&ep, params->sheet);
	if (vinput) {
		gnm_cellref_make_abs (&origin, &vinput->v_range.cell.a, &ep);
		nl->origin.col = origin.col;
		nl->origin.row = origin.row;
		nl->input_width = value_area_get_width (vinput, &ep);
		nl->input_height = value_area_get_height (vinput, &ep);
	}

	nl->debug = gnm_solver_debug ();
	nl->max_iter = params->options.max_iter;
	nl->min_factor = 1e-10;

	nl->target = gnm_solver_param_get_target_cell (params);

	nl->vars = g_ptr_array_new ();
	input_cells = gnm_solver_param_get_input_cells (params);
	for (l = input_cells; l; l = l->next)
		g_ptr_array_add (nl->vars, l->data);
	g_slist_free (input_cells);
	n = nl->vars->len;

	nl->x0 = g_new (gnm_float, n);
	nl->xk = g_new (gnm_float, n);

	g_signal_connect (res, "prepare", G_CALLBACK (gnm_nlsolve_prepare), nl);
	g_signal_connect (res, "start", G_CALLBACK (gnm_nlsolve_start), nl);
	g_signal_connect (res, "stop", G_CALLBACK (gnm_nlsolve_stop), nl);

	g_object_set_data_full (G_OBJECT (res), PRIVATE_KEY, nl,
				(GDestroyNotify)gnm_nlsolve_final);

	return res;
}
