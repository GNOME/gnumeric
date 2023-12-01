/*
 * goal-seek.c:  A generic root finder.
 *
 * Author:
 *   Morten Welinder (terra@gnome.org)
 *
 */

#undef DEBUG_GOAL_SEEK
#ifdef STANDALONE
#define DEBUG_GOAL_SEEK
#endif

#include <gnumeric-config.h>
#include <numbers.h>
#include <gnumeric.h>
#include <tools/goal-seek.h>
#include <gnm-random.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>

#include <stdlib.h>
#include <math.h>
#include <limits.h>


static gboolean
update_data (gnm_float x, gnm_float y, GnmGoalSeekData *data)
{
	if (!gnm_finite (y))
		return FALSE;

	if (y > 0) {
		if (data->havexpos) {
			if (data->havexneg) {
				/*
				 * When we have pos and neg, prefer the new point only
				 * if it makes the pos-neg x-interval smaller.
				 */
				if (gnm_abs (x - data->xneg) < gnm_abs (data->xpos - data->xneg)) {
					data->xpos = x;
					data->ypos = y;
				}
			} else if (y < data->ypos) {
				/* We have pos only and our neg y is closer to zero.  */
				data->xpos = x;
				data->ypos = y;
			}
		} else {
			data->xpos = x;
			data->ypos = y;
			data->havexpos = TRUE;
		}
		return FALSE;
	} else if (y < 0) {
		if (data->havexneg) {
			if (data->havexpos) {
				/*
				 * When we have pos and neg, prefer the new point only
				 * if it makes the pos-neg x-interval smaller.
				 */
				if (gnm_abs (x - data->xpos) < gnm_abs (data->xpos - data->xneg)) {
					data->xneg = x;
					data->yneg = y;
				}
			} else if (-y < -data->yneg) {
				/* We have neg only and our neg y is closer to zero.  */
				data->xneg = x;
				data->yneg = y;
			}

		} else {
			data->xneg = x;
			data->yneg = y;
			data->havexneg = TRUE;
		}
		return FALSE;
	} else {
		/* Lucky guess...  */
		data->have_root = TRUE;
		data->root = x;
#ifdef DEBUG_GOAL_SEEK
		g_print ("update_data: got root %.20" GNM_FORMAT_g "\n", x);
#endif
		return TRUE;
	}
}


/*
 * Calculate a reasonable approximation to the derivative of a function
 * in a single point.
 */
static GnmGoalSeekStatus
fake_df (GnmGoalSeekFunction f, gnm_float x, gnm_float *dfx, gnm_float xstep,
	 GnmGoalSeekData *data, void *user_data)
{
	gnm_float xl, xr, yl, yr;
	GnmGoalSeekStatus status;

#ifdef DEBUG_GOAL_SEEK
	g_print ("fake_df (x=%.20" GNM_FORMAT_g ", xstep=%.20" GNM_FORMAT_g ")\n",
		x, xstep);
#endif

	xl = x - xstep;
	if (xl < data->xmin)
		xl = x;

	xr = x + xstep;
	if (xr > data->xmax)
		xr = x;

	if (xl == xr) {
#ifdef DEBUG_GOAL_SEEK
		g_print ("==> xl == xr\n");
#endif
		return GOAL_SEEK_ERROR;
	}

	status = f (xl, &yl, user_data);
	if (status != GOAL_SEEK_OK) {
#ifdef DEBUG_GOAL_SEEK
		g_print ("==> failure at xl\n");
#endif
		return status;
	}
#ifdef DEBUG_GOAL_SEEK
	g_print ("==> xl=%.20" GNM_FORMAT_g "; yl=%.20" GNM_FORMAT_g "\n",
		 xl, yl);
#endif

	status = f (xr, &yr, user_data);
	if (status != GOAL_SEEK_OK) {
#ifdef DEBUG_GOAL_SEEK
		g_print ("==> failure at xr\n");
#endif
		return status;
	}
#ifdef DEBUG_GOAL_SEEK
	g_print ("==> xr=%.20" GNM_FORMAT_g "; yr=%.20" GNM_FORMAT_g "\n",
		 xr, yr);
#endif

	*dfx = (yr - yl) / (xr - xl);
#ifdef DEBUG_GOAL_SEEK
	g_print ("==> %.20" GNM_FORMAT_g "\n", *dfx);
#endif
	return gnm_finite (*dfx) ? GOAL_SEEK_OK : GOAL_SEEK_ERROR;
}

void
goal_seek_initialize (GnmGoalSeekData *data)
{
	data->havexpos = data->havexneg = data->have_root = FALSE;
	data->xpos = data->xneg = data->root = gnm_nan;
	data->ypos = data->yneg = gnm_nan;
	data->xmin = -1e10;
	data->xmax = +1e10;
	data->precision = 1e-10;
}


/**
 * goal_seek_point:
 * @f: (scope call): object function
 * @data: #GnmGoalSeekData state
 * @user_data: user data for @f
 * @x0: root guess
 *
 * Seek a goal using a single point.
 *
 * Returns:
 */
GnmGoalSeekStatus
goal_seek_point (GnmGoalSeekFunction f, GnmGoalSeekData *data,
		 void *user_data, gnm_float x0)
{
	GnmGoalSeekStatus status;
	gnm_float y0;

	if (data->have_root)
		return GOAL_SEEK_OK;

#ifdef DEBUG_GOAL_SEEK
	g_print ("goal_seek_point\n");
#endif

	if (x0 < data->xmin || x0 > data->xmax)
		return GOAL_SEEK_ERROR;

	status = f (x0, &y0, user_data);
	if (status != GOAL_SEEK_OK)
		return status;

	if (update_data (x0, y0, data))
		return GOAL_SEEK_OK;

	return GOAL_SEEK_ERROR;
}


static GnmGoalSeekStatus
goal_seek_newton_polish (GnmGoalSeekFunction f, GnmGoalSeekFunction df,
			 GnmGoalSeekData *data, void *user_data,
			 gnm_float x0, gnm_float y0)
{
	int iterations;
	gnm_float last_df0 = 1;
	gboolean try_newton = TRUE;
	gboolean try_square = x0 != 0 && gnm_abs (x0) < GNM_const(1e10);

#ifdef DEBUG_GOAL_SEEK
	g_print ("goal_seek_newton_polish\n");
#endif

	for (iterations = 0; iterations < 20; iterations++) {
		if (try_square) {
			gnm_float x1 = x0 * gnm_abs (x0);
			gnm_float y1, r;
			GnmGoalSeekStatus status = f (x1, &y1, user_data);
			if (status != GOAL_SEEK_OK)
				goto nomore_square;

			if (update_data (x1, y1, data))
				return GOAL_SEEK_OK;

			r = gnm_abs (y1 / y0);
			if (r >= 1)
				goto nomore_square;

			x0 = x1;
#ifdef DEBUG_GOAL_SEEK
			g_print ("polish square: x0=%.20" GNM_FORMAT_g "\n",
				 x0);
#endif
			if (r > GNM_const(0.5))
				goto nomore_square;

			continue;

		nomore_square:
			try_square = FALSE;
		}

		if (try_newton) {
			gnm_float df0, r, x1, y1;
			GnmGoalSeekStatus status = df
				? df (x0, &df0, user_data)
				: fake_df (f, x0, &df0, gnm_abs (x0) / 1000000, data, user_data);
			if (status != GOAL_SEEK_OK || df0 == 0)
				df0 = last_df0;  /* Bogus */
			else
				last_df0 = df0;

			x1 = x0 - y0 / df0;
			if (x1 < data->xmin || x1 > data->xmax)
				goto nomore_newton;

			status = f (x1, &y1, user_data);
			if (status != GOAL_SEEK_OK)
				goto nomore_newton;

			if (update_data (x1, y1, data))
				return GOAL_SEEK_OK;

			r = gnm_abs (y1 / y0);
			if (r >= 1)
				goto nomore_newton;

			x0 = x1;
#ifdef DEBUG_GOAL_SEEK
			g_print ("polish Newton: x0=%.20" GNM_FORMAT_g "\n",
				 x0);
#endif
			if (r > GNM_const(0.5))
				goto nomore_newton;

			continue;

		nomore_newton:
			try_newton = FALSE;
		}

		/* Nothing left to try.  */
		break;
	}

	if (goal_seek_bisection (f, data, user_data) == GOAL_SEEK_OK)
		return GOAL_SEEK_OK;

	data->root = x0;
	data->have_root = TRUE;
	return GOAL_SEEK_OK;
}


/**
 * goal_seek_newton:
 * @f: (scope call): object function
 * @df: (scope call) (nullable): object function derivative
 * @data: #GnmGoalSeekData state
 * @user_data: user data for @f and @df
 * @x0: root guess
 *
 * Seek a goal (root) using Newton's iterative method.
 *
 * The supplied function must (should) be continuously differentiable in
 * the supplied interval.  If @df is %NULL, this function will
 * estimate the derivative.
 *
 * This method will find a root rapidly provided the initial guess, x0,
 * is sufficiently close to the root.  (The number of significant digits
 * (asymptotically) goes like i^2 unless the root is a multiple root in
 * which case it is only like c*i.)
 */
GnmGoalSeekStatus
goal_seek_newton (GnmGoalSeekFunction f, GnmGoalSeekFunction df,
		  GnmGoalSeekData *data, void *user_data, gnm_float x0)
{
	int iterations;
	gnm_float precision = data->precision / 2;
	gnm_float last_df0 = 1;
	gnm_float step_factor = 1e-6;

	if (data->have_root)
		return GOAL_SEEK_OK;

#ifdef DEBUG_GOAL_SEEK
	g_print ("goal_seek_newton\n");
#endif

	for (iterations = 0; iterations < 100; iterations++) {
		gnm_float x1, y0, df0, stepsize;
		GnmGoalSeekStatus status;
		gboolean flat;

#ifdef DEBUG_GOAL_SEEK
		g_print ("x0 = %.20" GNM_FORMAT_g "   (i=%d)\n", x0, iterations);
#endif

		/* Check whether we have left the valid interval.  */
		if (x0 < data->xmin || x0 > data->xmax)
			return GOAL_SEEK_ERROR;

		status = f (x0, &y0, user_data);
		if (status != GOAL_SEEK_OK)
			return status;

#ifdef DEBUG_GOAL_SEEK
		g_print ("                                        y0 = %.20" GNM_FORMAT_g "\n", y0);
#endif

		if (update_data (x0, y0, data))
			return GOAL_SEEK_OK;

		if (df)
			status = df (x0, &df0, user_data);
		else {
			gnm_float xstep;

			if (gnm_abs (x0) < GNM_const(1e-10))
				if (data->havexneg && data->havexpos)
					xstep = gnm_abs (data->xpos - data->xneg) / 1000000;
				else
					xstep = (data->xmax - data->xmin) / 1000000;
			else
				xstep = step_factor * gnm_abs (x0);

			status = fake_df (f, x0, &df0, xstep, data, user_data);
		}
		if (status != GOAL_SEEK_OK)
			return status;

		/* If we hit a flat spot, we are in trouble.  */
		flat = (df0 == 0);
		if (flat) {
			last_df0 /= 2;
			if (gnm_abs (last_df0) <= GNM_MIN)
				return GOAL_SEEK_ERROR;
			df0 = last_df0;  /* Might be utterly bogus.  */
		} else
			last_df0 = df0;

		if (data->havexpos && data->havexneg)
			x1 = x0 - y0 / df0;
		else
			/*
			 * Overshoot slightly to prevent us from staying on
			 * just one side of the root.
			 */
			x1 = x0 - GNM_const(1.000001) * y0 / df0;

		stepsize = gnm_abs (x1 - x0) / (gnm_abs (x0) + gnm_abs (x1));

#ifdef DEBUG_GOAL_SEEK
		g_print ("                                        df0 = %.20" GNM_FORMAT_g "\n", df0);
		g_print ("                                        ss = %.20" GNM_FORMAT_g "\n", stepsize);
#endif

		if (stepsize < precision) {
			goal_seek_newton_polish (f, df, data, user_data, x0, y0);
			return GOAL_SEEK_OK;
		}

		if (flat && iterations > 0) {
			/*
			 * Verify that we made progress using our
			 * potentially bogus df0.
			 */
			gnm_float y1;

			if (x1 < data->xmin || x1 > data->xmax)
				return GOAL_SEEK_ERROR;

			status = f (x1, &y1, user_data);
			if (status != GOAL_SEEK_OK)
				return status;

#ifdef DEBUG_GOAL_SEEK
			g_print ("                                        y1 = %.20" GNM_FORMAT_g "\n", y1);
#endif
			if (gnm_abs (y1) >= GNM_const(0.9) * gnm_abs (y0))
				return GOAL_SEEK_ERROR;
		}

		if (stepsize < step_factor)
			step_factor = stepsize;

		x0 = x1;
	}

	return GOAL_SEEK_ERROR;
}

/**
 * goal_seek_bisection:
 * @f: (scope call): object function
 * @data: #GnmGoalSeekData state.
 * @user_data: user data for @f.
 *
 * Seek a goal (root) using bisection methods.
 *
 * The supplied function must (should) be continuous over the interval.
 *
 * Caller must have located a positive and a negative point.
 *
 * This method will find a root steadily using bisection to narrow the
 * interval in which a root lies.
 *
 * It alternates between mid-point-bisection (semi-slow, but guaranteed
 * progress), secant-bisection (usually quite fast, but sometimes gets
 * nowhere), and Ridder's Method (usually fast, harder to fool than
 * the secant method).
 */
GnmGoalSeekStatus
goal_seek_bisection (GnmGoalSeekFunction f, GnmGoalSeekData *data,
		     void *user_data)
{
	int iterations;
	gnm_float stepsize;
	int newton_submethod = 0;

	if (data->have_root)
		return GOAL_SEEK_OK;

#ifdef DEBUG_GOAL_SEEK
	g_print ("goal_seek_bisection\n");
#endif

	if (!data->havexpos || !data->havexneg)
		return GOAL_SEEK_ERROR;

	stepsize = gnm_abs (data->xpos - data->xneg)
		/ (gnm_abs (data->xpos) + gnm_abs (data->xneg));

	/* log_2 (10) = 3.3219 < 4.  */
	for (iterations = 0; iterations < 100 + GNM_DIG * 4; iterations++) {
		gnm_float xmid, ymid;
		GnmGoalSeekStatus status;
		enum { M_SECANT, M_RIDDER, M_NEWTON, M_MIDPOINT } method;

		method = (iterations % 4 == 0)
			? M_RIDDER
			: ((iterations % 4 == 2)
			   ? M_NEWTON
			   : M_MIDPOINT);

	again:
		switch (method) {
		default:
			abort ();

		case M_SECANT:
			xmid = data->xpos - data->ypos *
				((data->xneg - data->xpos) /
				 (data->yneg - data->ypos));
			break;

		case M_RIDDER: {
			gnm_float det;

			xmid = (data->xpos + data->xneg) / 2;
			status = f (xmid, &ymid, user_data);
			if (status != GOAL_SEEK_OK)
				continue;
			if (ymid == 0) {
				update_data (xmid, ymid, data);
				return GOAL_SEEK_OK;
			}

			det = gnm_sqrt (ymid * ymid - data->ypos * data->yneg);
			if (det == 0)
				 /* This might happen with underflow, I guess. */
				continue;

			xmid += (xmid - data->xpos) * ymid / det;
			break;
		}

		case M_MIDPOINT:
			xmid = (data->xpos + data->xneg) / 2;
			break;

		case M_NEWTON: {
			gnm_float x0, y0, xstep, df0;

			/* This method is only effective close-in.  */
			if (stepsize > GNM_const(0.1)) {
				method = M_MIDPOINT;
				goto again;
			}

			switch (newton_submethod++ % 4) {
			case 0:	x0 = data->xpos; y0 = data->ypos; break;
			case 2: x0 = data->xneg; y0 = data->yneg; break;
			default:
			case 3:
			case 1:
				x0 = (data->xpos + data->xneg) / 2;

				status = f (x0, &y0, user_data);
				if (status != GOAL_SEEK_OK)
					continue;
			}

			xstep = gnm_abs (data->xpos - data->xneg) / 1000000;
			status = fake_df (f, x0, &df0, xstep, data, user_data);
			if (status != GOAL_SEEK_OK)
				continue;

			if (df0 == 0)
				continue;

			/*
			 * Overshoot by 1% to prevent us from staying on
			 * just one side of the root.
			 */
			xmid = x0 - GNM_const(1.01) * y0 / df0;
		}
		}

		if ((xmid < data->xpos && xmid < data->xneg) ||
		    (xmid > data->xpos && xmid > data->xneg)) {
			/* We left the interval.  */
			xmid = (data->xpos + data->xneg) / 2;
			method = M_MIDPOINT;
		}

		status = f (xmid, &ymid, user_data);
		if (status != GOAL_SEEK_OK)
			continue;

#ifdef DEBUG_GOAL_SEEK
		{
			const char *themethod;
			switch (method) {
			case M_MIDPOINT: themethod = "midpoint"; break;
			case M_RIDDER: themethod = "Ridder"; break;
			case M_SECANT: themethod = "secant"; break;
			case M_NEWTON: themethod = "Newton"; break;
			default: themethod = "?";
			}

			g_print ("xmid = %.20" GNM_FORMAT_g " (%s)\n", xmid, themethod);
			g_print ("                                        ymid = %.20" GNM_FORMAT_g "\n", ymid);
		}
#endif

		if (update_data (xmid, ymid, data)) {
			return GOAL_SEEK_OK;
		}

		stepsize = gnm_abs (data->xpos - data->xneg)
			/ (gnm_abs (data->xpos) + gnm_abs (data->xneg));

#ifdef DEBUG_GOAL_SEEK
		g_print ("                                          ss = %.20" GNM_FORMAT_g "\n", stepsize);
#endif

		if (stepsize < GNM_EPSILON) {
			if (data->yneg < ymid)
				ymid = data->yneg, xmid = data->xneg;

			if (data->ypos < ymid)
				ymid = data->ypos, xmid = data->xpos;

			data->have_root = TRUE;
			data->root = xmid;
			return GOAL_SEEK_OK;
		}
	}
	return GOAL_SEEK_ERROR;
}

#undef SECANT_P
#undef RIDDER_P

/**
 * goal_seek_trawl_uniformly:
 * @f: (scope call): object function
 * @data: #GnmGoalSeekData state
 * @user_data: user data for @f
 * @xmin: lower search bound
 * @xmax: upper search bound
 * @points: number of points to try.
 */
GnmGoalSeekStatus
goal_seek_trawl_uniformly (GnmGoalSeekFunction f,
			   GnmGoalSeekData *data, void *user_data,
			   gnm_float xmin, gnm_float xmax,
			   int points)
{
	int i;

	if (data->have_root)
		return GOAL_SEEK_OK;

#ifdef DEBUG_GOAL_SEEK
	g_print ("goal_seek_trawl_uniformly\n");
#endif

	if (xmin > xmax || xmin < data->xmin || xmax > data->xmax)
		return GOAL_SEEK_ERROR;

	for (i = 0; i < points; i++) {
		gnm_float x, y;
		GnmGoalSeekStatus status;

		if (data->havexpos && data->havexneg)
			break;

		x = xmin + (xmax - xmin) * random_01 ();
		status = f (x, &y, user_data);
		if (status != GOAL_SEEK_OK)
			/* We are not depending on the result, so go on.  */
			continue;

#ifdef DEBUG_GOAL_SEEK
		g_print ("x = %.20" GNM_FORMAT_g "\n", x);
		g_print ("                                        y = %.20" GNM_FORMAT_g "\n", y);
#endif

		if (update_data (x, y, data))
			return GOAL_SEEK_OK;
	}

	/* We were not (extremely) lucky, so we did not actually hit the
	   root.  We report this as an error.  */
	return GOAL_SEEK_ERROR;
}

/**
 * goal_seek_trawl_normally:
 * @f: (scope call): object function
 * @data: #GnmGoalSeekData state
 * @user_data: user data for @f
 * @mu: search mean
 * @sigma: search standard deviation
 * @points: number of points to try.
 */
GnmGoalSeekStatus
goal_seek_trawl_normally (GnmGoalSeekFunction f,
			  GnmGoalSeekData *data, void *user_data,
			  gnm_float mu, gnm_float sigma,
			  int points)
{
	int i;

	if (data->have_root)
		return GOAL_SEEK_OK;

#ifdef DEBUG_GOAL_SEEK
	g_print ("goal_seek_trawl_normally\n");
#endif

	if (sigma <= 0 || mu < data->xmin || mu > data->xmax)
		return GOAL_SEEK_ERROR;

	for (i = 0; i < points; i++) {
		gnm_float x, y;
		GnmGoalSeekStatus status;

		if (data->havexpos && data->havexneg)
			break;

		x = mu + sigma * random_normal ();
		if (x < data->xmin || x > data->xmax)
			continue;

		status = f (x, &y, user_data);
		if (status != GOAL_SEEK_OK)
			/* We are not depending on the result, so go on.  */
			continue;

#ifdef DEBUG_GOAL_SEEK
		g_print ("x = %.20" GNM_FORMAT_g "\n", x);
		g_print ("                                        y = %.20" GNM_FORMAT_g "\n", y);
#endif

		if (update_data (x, y, data))
			return GOAL_SEEK_OK;
	}

	/* We were not (extremely) lucky, so we did not actually hit the
	   root.  We report this as an error.  */
	return GOAL_SEEK_ERROR;
}

/**
 * gnm_goal_seek_eval_cell:
 * @x: x-value for which to evaluate
 * @y: (out): location to store result
 * @data: user data
 *
 * Returns: An status indicating whether evaluation went ok.
 */
GnmGoalSeekStatus
gnm_goal_seek_eval_cell (gnm_float x, gnm_float *y, gpointer data_)
{
	GnmGoalSeekCellData const *data = data_;
	GnmValue *v = value_new_float (x);

	gnm_cell_set_value (data->xcell, v);
	cell_queue_recalc (data->xcell);
	gnm_cell_eval (data->ycell);

	if (data->ycell->value &&
	    VALUE_IS_NUMBER (data->ycell->value)) {
		*y = value_get_as_float (data->ycell->value) - data->ytarget;
		if (gnm_finite (*y))
			return GOAL_SEEK_OK;
	}

	return GOAL_SEEK_ERROR;
}

GnmGoalSeekStatus
gnm_goal_seek_cell (GnmGoalSeekData *data,
		    GnmGoalSeekCellData *celldata)
{
	GnmGoalSeekStatus status;
	gboolean hadold;
	gnm_float oldx;
	GnmValue *v;

	hadold = !VALUE_IS_EMPTY_OR_ERROR (celldata->xcell->value);
	oldx = hadold ? value_get_as_float (celldata->xcell->value) : 0;

	/* PLAN A: Newton's iterative method from initial or midpoint.  */
	{
		gnm_float x0;

		if (hadold && oldx >= data->xmin && oldx <= data->xmax)
			x0 = oldx;
		else
			x0 = (data->xmin + data->xmax) / 2;

		status = goal_seek_newton (gnm_goal_seek_eval_cell, NULL,
					   data, celldata,
					   x0);
		if (status == GOAL_SEEK_OK)
			goto DONE;
	}

	/* PLAN B: Trawl uniformly.  */
	if (!data->havexpos || !data->havexneg) {
		status = goal_seek_trawl_uniformly (gnm_goal_seek_eval_cell,
						    data, celldata,
						    data->xmin, data->xmax,
						    100);
		if (status == GOAL_SEEK_OK)
			goto DONE;
	}

	/* PLAN C: Trawl normally from middle.  */
	if (!data->havexpos || !data->havexneg) {
		gnm_float sigma, mu;
		int i;

		sigma = MIN (data->xmax - data->xmin, GNM_const(1e6));
		mu = (data->xmax + data->xmin) / 2;

		for (i = 0; i < 5; i++) {
			sigma /= 10;
			status = goal_seek_trawl_normally (gnm_goal_seek_eval_cell,
							   data, celldata,
							   mu, sigma, 30);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN D: Trawl normally from left.  */
	if (!data->havexpos || !data->havexneg) {
		gnm_float sigma, mu;
		int i;

		sigma = MIN (data->xmax - data->xmin, GNM_const(1e6));
		mu = data->xmin;

		for (i = 0; i < 5; i++) {
			sigma /= 10;
			status = goal_seek_trawl_normally (gnm_goal_seek_eval_cell,
							   data, celldata,
							   mu, sigma, 20);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN E: Trawl normally from right.  */
	if (!data->havexpos || !data->havexneg) {
		gnm_float sigma, mu;
		int i;

		sigma = MIN (data->xmax - data->xmin, GNM_const(1e6));
		mu = data->xmax;

		for (i = 0; i < 5; i++) {
			sigma /= 10;
			status = goal_seek_trawl_normally (gnm_goal_seek_eval_cell,
							   data, celldata,
							   mu, sigma, 20);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN F: Newton iteration with uniform net of starting points.  */
	if (!data->havexpos || !data->havexneg) {
		int i;
		const int N = 10;

		for (i = 1; i <= N; i++) {
			gnm_float x0 =	data->xmin +
				(data->xmax - data->xmin) / (N + 1) * i;

			status = goal_seek_newton (gnm_goal_seek_eval_cell, NULL,
						   data, celldata,
						   x0);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN Z: Bisection.  */
	{
		status = goal_seek_bisection (gnm_goal_seek_eval_cell,
					      data, celldata);
		if (status == GOAL_SEEK_OK)
			goto DONE;
	}

 DONE:
	if (status == GOAL_SEEK_OK)
		v = value_new_float (data->root);
	else if (hadold)
		v = value_new_float (oldx);
	else
		v = value_new_empty ();
	sheet_cell_set_value (celldata->xcell, v);

	return status;
}
