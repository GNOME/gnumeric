#include <gnumeric-config.h>
#include "gnumeric.h"
#include <tools/gnm-solver.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "regression.h"
#include "rangefunc.h"
#include "workbook.h"
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

/*
 * This is based on the algorithm from "An Automatic Method for finding
 * the Greatest or Least Value of a Function" by H. H. Rosenbrock
 * published in _The Computer Journal_ (1960) 3(3): 175-184.
 *
 * It is thus 50+ years old.  You would think that advances in computer
 * science would have produced improvements that would run circles
 * around this, but that is not obviously true.
 *
 * There are a couple of attrictive features of the Rosenbrock method:
 * 1. It's monotonic.  Unlike Newton-style methods it cannot suddenly
 *    warp far away.
 * 2. We don't need the Hessian.
 *
 * Note, that in order to speed convergence we occasionally perform
 * a tentative Newton iteration step.  (It's tentative because we will
 * discard it if it doesn't lead to an immediate improvement.)
 */

#define PRIVATE_KEY "::nlsolve::"

/*
 * Note: the solver code assumes the problem is a minimization problem.
 * When used for a maximization problem, we flip the objective function
 * sign.
 */


typedef struct {
	/* The solver object in two forms.  */
	GnmSolver *sol;
	GnmIterSolver *isol;

	/* Number of vars.  */
	int n;

	/* Rosenbrock state */
	gnm_float **xi;
	int smallsteps;
	int tentative;
	gnm_float *tentative_xk, tentative_yk;

	/* Parameters: */
	gboolean debug;
	gnm_float min_factor;
} GnmNlsolve;

static gboolean
check_program (GnmSolver *sol, GError **err)
{
	unsigned ui;
	const GnmSolverParameters *params = sol->params;
	GSList *l;

	for (l = params->constraints; l; l = l->next) {
		GnmSolverConstraint *c  = l->data;
		switch (c->type) {
		case GNM_SOLVER_EQ:
			/*
			 * This catches also equalities where the sides are not
			 * input variables.
			 */
			goto no_equal;
		default:
			break;
		}
	}

	for (ui = 0; ui < sol->input_cells->len; ui++) {
		if (sol->discrete[ui])
			goto no_discrete;

		/*
		 * This also catches using two inequality constraints used
		 * to emulate equality.
		 */
		if (sol->min[ui] == sol->max[ui])
			goto no_equal;
	}

	return TRUE;

no_discrete:
	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("This solver does not handle discrete variables."));
	return FALSE;

no_equal:
	g_set_error (err,
		     go_error_invalid (),
		     0,
		     _("This solver does not handle equality constraints."));
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
	gnm_solver_set_var (nl->sol, i, x);
}

static void
set_vector (GnmNlsolve *nl, const gnm_float *xs)
{
	gnm_solver_set_vars (nl->sol, xs);
}

/* Get the target value as-if we were minimizing.  */
static gnm_float
get_value (GnmNlsolve *nl)
{
	/* nl->sol has been taught to flip sign if needed.  */
	return gnm_solver_get_target_value (nl->sol);
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
set_solution (GnmNlsolve *nl)
{
	/* nl->isol has been taught to flip sign if needed.  */
	gnm_iter_solver_set_solution (nl->isol);
}

static gboolean
gnm_nlsolve_prepare (GnmSolver *sol, WorkbookControl *wbc, GError **err,
		     GnmNlsolve *nl)
{
	gboolean ok;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY, FALSE);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARING);

	ok = check_program (sol, err);
	if (ok)
		ok = gnm_iter_solver_get_initial_solution (nl->isol, err);

	if (ok) {
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARED);
	} else {
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
	}

	return ok;
}

static gnm_float *
compute_gradient (GnmNlsolve *nl, const gnm_float *xs)
{
	return gnm_solver_compute_gradient (nl->sol, xs);
}

static gnm_float **
compute_hessian (GnmNlsolve *nl, const gnm_float *xs, const gnm_float *g0)
{
	gnm_float **H, *xs2;
	const int n = nl->n;
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
	GnmSolver *sol = nl->sol;
	const int n = nl->n;
	gnm_float *g, **H, *d;
	gboolean ok;

	g = compute_gradient (nl, xs);
	H = compute_hessian (nl, xs, g);
	d = g_new (gnm_float, n);
	ok = (gnm_linear_solve (H, g, n, d) == 0);

	if (ok) {
		int i;
		gnm_float y2, *xs2 = g_new (gnm_float, n);
		gnm_float best_f = 42;
		// We try these step sizes.  We really should not need
		// negative, but if H isn't positive definite it might
		// work.
		static const gnm_float fs[] = {
			1.0, 0.5, 1.0 / 16,
			-1.0, -1.0 / 16,
		};
		unsigned ui;

		if (nl->debug) {
			int i;
			for (i = 0; i < n; i++)
				print_vector (NULL, H[i], n);
			print_vector ("d", d, n);
			print_vector ("g", g, n);
		}

		ok = FALSE;
		for (ui = 0 ; ui < G_N_ELEMENTS (fs); ui++) {
			gnm_float f = fs[ui];
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

		if (best_f != 42) {
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
	const int n = nl->n;
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
	const int n = nl->n;
	GnmIterSolver *isol = nl->isol;

	if (!accept && nl->tentative_xk) {
		nl->isol->yk = nl->tentative_yk;
		memcpy (isol->xk, nl->tentative_xk, n * sizeof (gnm_float));
	}

	nl->tentative = 0;
	g_free (nl->tentative_xk);
	nl->tentative_xk = NULL;

	nl->smallsteps = 0;
}

static gboolean
rosenbrock_iter (GnmNlsolve *nl)
{
	GnmSolver *sol = nl->sol;
	GnmIterSolver *isol = nl->isol;
	const int n = nl->n;
	int i, j;
	const gnm_float alpha = 3;
	const gnm_float beta = 0.5;
	gboolean any_at_all = FALSE;
	gnm_float *d, **A, *x, *dx, *t;
	char *state;
	int dones = 0;
	gnm_float ykm1 = isol->yk, *xkm1;
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

	if (isol->iterations % 100 == 0 &&
	    gnm_solver_has_analytic_gradient (sol)) {
		if (newton_improve (nl, isol->xk, &isol->yk, isol->yk))
			return TRUE;
	}

	if (isol->iterations % 20 == 0) {
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
		d[i] = (isol->xk[i] == 0)
			? eps
			: gnm_abs (isol->xk[i]) * eps;
	}

	xkm1 = g_memdup (isol->xk, n * sizeof (gnm_float));

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
				x[j] = isol->xk[j] + d[i] * nl->xi[i][j];

			set_vector (nl, x);
			y = get_value (nl);

			if (y <= isol->yk && gnm_solver_check_constraints (sol)) {
				if (y < isol->yk) {
					isol->yk = y;
					memcpy (isol->xk, x, n * sizeof (gnm_float));
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
			set_vector (nl, isol->xk);
			set_solution (nl);
		}

		if (nl->tentative) {
			if (isol->yk < nl->tentative_yk) {
				if (nl->debug)
					g_printerr ("Tentative move accepted!\n");
				rosenbrock_tentative_end (nl, TRUE);
			}
		} else if (gnm_abs (isol->yk - ykm1) > gnm_abs (ykm1) * 0.01) {
			/* A big step.  */
			nl->smallsteps = 0;
		} else {
			nl->smallsteps++;
		}

		if (0 && !nl->tentative && nl->smallsteps > 50) {
			gnm_float yk = isol->yk;

			nl->tentative = 10;
			nl->tentative_xk = g_memdup (isol->xk, n * sizeof (gnm_float));
			nl->tentative_yk = yk;

			for (i = 0; i < 4; i++) {
				gnm_float ymax = yk +
					gnm_abs (yk) * (0.10 / (i + 1));
				if (i > 0)
					ymax = MIN (ymax, isol->yk);
				if (!newton_improve (nl, isol->xk, &isol->yk, ymax))
					break;
			}

			if (nl->debug)
				print_vector ("Tentative move to", isol->xk, n);
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

static gboolean
gnm_nlsolve_iterate (GnmSolverIterator *iter, GnmNlsolve *nl)
{
	GnmIterSolver *isol = nl->isol;
	const int n = nl->n;

	if (isol->iterations == 0)
		rosenbrock_init (nl);

	if (nl->debug) {
		g_printerr ("Iteration %ld at %.15" GNM_FORMAT_g "\n",
			    (long)(isol->iterations), isol->yk);
		print_vector ("Current point", isol->xk, n);
	}

	return rosenbrock_iter (nl);
}

static void
gnm_nlsolve_final (GnmNlsolve *nl)
{
	const int n = nl->n;

	/* Accept, i.e., don't try to restore.  */
	rosenbrock_tentative_end (nl, TRUE);

	if (nl->xi) {
		free_matrix (nl->xi, n);
		nl->xi = NULL;
	}

	g_free (nl);
}

/* ------------------------------------------------------------------------- */
/* Plug-in interface.  */

gboolean nlsolve_solver_factory_functional (GnmSolverFactory *factory);
GnmSolver *nlsolve_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params);

gboolean
nlsolve_solver_factory_functional (GnmSolverFactory *factory)
{
	return TRUE;
}

GnmSolver *
nlsolve_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params)
{
	GnmIterSolver *isol = g_object_new
		(GNM_ITER_SOLVER_TYPE,
		 "params", params,
		 "flip-sign", (params->problem_type == GNM_SOLVER_MAXIMIZE),
		 NULL);
	GnmSolver *sol = GNM_SOLVER (isol);
	GnmNlsolve *nl = g_new0 (GnmNlsolve, 1);
	GnmSolverIteratorCompound *citer;
	GnmSolverIterator *iter;

	citer = g_object_new (GNM_SOLVER_ITERATOR_COMPOUND_TYPE, NULL);

	iter = gnm_solver_iterator_new_func (G_CALLBACK (gnm_nlsolve_iterate), nl);
	gnm_solver_iterator_compound_add (citer, iter, 1);

	gnm_solver_iterator_compound_add (citer, gnm_solver_iterator_new_polish (isol), 0);

	gnm_iter_solver_set_iterator (isol, GNM_SOLVER_ITERATOR (citer));
	g_object_unref (citer);

	nl->sol = sol;
	nl->isol = isol;
	nl->debug = gnm_solver_debug ();
	nl->min_factor = 1e-10;
	nl->n = nl->sol->input_cells->len;

	g_signal_connect (isol, "prepare", G_CALLBACK (gnm_nlsolve_prepare), nl);

	g_object_set_data_full (G_OBJECT (isol), PRIVATE_KEY, nl,
				(GDestroyNotify)gnm_nlsolve_final);

	return sol;
}

/* ------------------------------------------------------------------------- */
