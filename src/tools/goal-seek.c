/*
 * goal-seek.c:  A generic root finder.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 *
 */

#undef DEBUG_GOAL_SEEK
#ifdef STANDALONE
#define DEBUG_GOAL_SEEK
#endif

#include <gnumeric-config.h>
#include "numbers.h"
#include "gnumeric.h"
#include "goal-seek.h"

#include "mathfunc.h"
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#ifdef DEBUG_GOAL_SEEK
#include <stdio.h>
#endif

static gboolean
update_data (gnm_float x, gnm_float y, GoalSeekData *data)
{
	if (y > 0) {
		if (data->havexpos) {
			if (data->havexneg) {
				/*
				 * When we have pos and neg, prefer the new point only
				 * if it makes the pos-neg x-internal smaller.
				 */
				if (gnumabs (x - data->xneg) < gnumabs (data->xpos - data->xneg)) {
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
			data->havexpos = 1;
		}
		return FALSE;
	} else if (y < 0) {
		if (data->havexneg) {
			if (data->havexpos) {
				/*
				 * When we have pos and neg, prefer the new point only
				 * if it makes the pos-neg x-internal smaller.
				 */
				if (gnumabs (x - data->xpos) < gnumabs (data->xpos - data->xneg)) {
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
			data->havexneg = 1;
		}
		return FALSE;
	} else {
		/* Lucky guess...  */
		data->root = x;
		return TRUE;
	}
}


/*
 * Calculate a reasonable approximation to the derivative of a function
 * in a single point.
 */
static GoalSeekStatus
fake_df (GoalSeekFunction f, gnm_float x, gnm_float *dfx, gnm_float xstep,
	 GoalSeekData *data, void *user_data)
{
	gnm_float xl, xr, yl, yr;
	GoalSeekStatus status;

#ifdef DEBUG_GOAL_SEEK
	printf ("fake_df (x=%.20" GNUM_FORMAT_g ", xstep=%.20" GNUM_FORMAT_g ")\n",
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
		printf ("==> xl == xr\n");
#endif
		return GOAL_SEEK_ERROR;
	}

	status = f (xl, &yl, user_data);
	if (status != GOAL_SEEK_OK) {
#ifdef DEBUG_GOAL_SEEK
		printf ("==> failure at xl\n");
#endif
		return status;
	}
#ifdef DEBUG_GOAL_SEEK
		printf ("==> xl=%.20" GNUM_FORMAT_g "; yl=%.20" GNUM_FORMAT_g "\n",
			xl, yl);
#endif

	status = f (xr, &yr, user_data);
	if (status != GOAL_SEEK_OK) {
#ifdef DEBUG_GOAL_SEEK
		printf ("==> failure at xr\n");
#endif
		return status;
	}
#ifdef DEBUG_GOAL_SEEK
		printf ("==> xr=%.20" GNUM_FORMAT_g "; yr=%.20" GNUM_FORMAT_g "\n",
			xr, yr);
#endif

	*dfx = (yr - yl) / (xr - xl);
#ifdef DEBUG_GOAL_SEEK
		printf ("==> %.20" GNUM_FORMAT_g "\n", *dfx);
#endif
	return finitegnum (*dfx) ? GOAL_SEEK_OK : GOAL_SEEK_ERROR;
}

void
goal_seek_initialise (GoalSeekData *data)
{
	data->havexpos = data->havexneg = FALSE;
	data->xmin = -1e10;
	data->xmax = +1e10;
	data->precision = 1e-10;
}


/*
 * Seek a goal using a single point.
 */
GoalSeekStatus
goal_seek_point (GoalSeekFunction f, GoalSeekData *data,
		 void *user_data, gnm_float x0)
{
	GoalSeekStatus status;
	gnm_float y0;

	if (x0 < data->xmin || x0 > data->xmax)
		return GOAL_SEEK_ERROR;

	status = f (x0, &y0, user_data);
	if (status != GOAL_SEEK_OK)
		return status;

	if (update_data (x0, y0, data))
		return GOAL_SEEK_OK;

	return GOAL_SEEK_ERROR;
}


/*
 * Seek a goal (root) using Newton's iterative method.
 *
 * The supplied function must (should) be continously differentiable in
 * the supplied interval.  If NULL is used for `df', this function will
 * estimate the derivative.
 *
 * This method will find a root rapidly provided the initial guess, x0,
 * is sufficiently close to the root.  (The number of significant digits
 * (asympotically) goes like i^2 unless the root is a multiple root in
 * which case it is only like c*i.)
 */
GoalSeekStatus
goal_seek_newton (GoalSeekFunction f, GoalSeekFunction df,
		  GoalSeekData *data, void *user_data, gnm_float x0)
{
	int iterations;
	gnm_float precision = data->precision / 2;

#ifdef DEBUG_GOAL_SEEK
	printf ("goal_seek_newton\n");
#endif

	for (iterations = 0; iterations < 20; iterations++) {
		gnm_float x1, y0, df0, stepsize;
		GoalSeekStatus status;

		/* Check whether we have left the valid interval.  */
		if (x0 < data->xmin || x0 > data->xmax)
			return GOAL_SEEK_ERROR;

		status = f (x0, &y0, user_data);
		if (status != GOAL_SEEK_OK)
			return status;

#ifdef DEBUG_GOAL_SEEK
		printf ("x0 = %.20" GNUM_FORMAT_g "\n", x0);
		printf ("                                        y0 = %.20" GNUM_FORMAT_g "\n", y0);
#endif

		if (update_data (x0, y0, data))
			return GOAL_SEEK_OK;

		if (df)
			status = df (x0, &df0, user_data);
		else {
			gnm_float xstep;

			if (gnumabs (x0) < 1e-10)
				if (data->havexneg && data->havexpos)
					xstep = gnumabs (data->xpos - data->xneg) / 1e6;
				else
					xstep = (data->xmax - data->xmin) / 1e6;
			else
				xstep = gnumabs (x0) / 1e6;

			status = fake_df (f, x0, &df0, xstep, data, user_data);
		}
		if (status != GOAL_SEEK_OK)
			return status;

		/* If we hit a flat spot, we are in trouble.  */
		if (df0 == 0)
			return GOAL_SEEK_ERROR;

		/*
		 * Overshoot slightly to prevent us from staying on
		 * just one side of the root.
		 */
		x1 = x0 - 1.000001 * y0 / df0;
		if (x1 == x0) {
			data->root = x0;
			return GOAL_SEEK_OK;
		}

		stepsize = gnumabs (x1 - x0) / (gnumabs (x0) + gnumabs (x1));

#ifdef DEBUG_GOAL_SEEK
		printf ("                                        df0 = %.20" GNUM_FORMAT_g "\n", df0);
		printf ("                                        ss = %.20" GNUM_FORMAT_g "\n", stepsize);
#endif

		x0 = x1;

		if (stepsize < precision) {
			data->root = x0;
			return GOAL_SEEK_OK;
		}
	}

	return GOAL_SEEK_ERROR;
}

/*
 * Seek a goal (root) using bisection methods.
 *
 * The supplied function must (should) be continous over the interval.
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

GoalSeekStatus
goal_seek_bisection (GoalSeekFunction f, GoalSeekData *data, void *user_data)
{
	int iterations;
	gnm_float stepsize;
	int newton_submethod = 0;

#ifdef DEBUG_GOAL_SEEK
	printf ("goal_seek_bisection\n");
#endif

	if (!data->havexpos || !data->havexneg)
		return GOAL_SEEK_ERROR;

	stepsize = gnumabs (data->xpos - data->xneg)
		/ (gnumabs (data->xpos) + gnumabs (data->xneg));

	/* log_2 (10) = 3.3219 < 4.  */
	for (iterations = 0; iterations < 100 + GNUM_DIG * 4; iterations++) {
		gnm_float xmid, ymid;
		GoalSeekStatus status;
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

			det = sqrtgnum (ymid * ymid - data->ypos * data->yneg);
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
			if (stepsize > 0.1) {
				method = M_MIDPOINT;
				goto again;
			}

			switch (newton_submethod++ % 4) {
			case 0:	x0 = data->xpos; x0 = data->ypos; break;
			case 2: x0 = data->xneg; y0 = data->yneg; break;
			default:
			case 3:
			case 1:
				x0 = (data->xpos + data->xneg) / 2;

				status = f (x0, &y0, user_data);
				if (status != GOAL_SEEK_OK)
					continue;
			}

			xstep = gnumabs (data->xpos - data->xneg) / 1e6;
			status = fake_df (f, x0, &df0, xstep, data, user_data);
			if (status != GOAL_SEEK_OK)
				continue;

			if (df0 == 0)
				continue;

			/*
			 * Overshoot by 1% to prevent us from staying on
			 * just one side of the root.
			 */
			xmid = x0 - 1.01 * y0 / df0;
			if ((xmid < data->xpos && xmid < data->xneg) ||
			    (xmid > data->xpos && xmid > data->xneg))
				/* We left the interval.  */
				continue;
		}
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

			printf ("xmid = %.20" GNUM_FORMAT_g " (%s)\n", xmid, themethod);
			printf ("                                        ymid = %.20" GNUM_FORMAT_g "\n", ymid);
		}
#endif

		if (update_data (xmid, ymid, data)) {
			return GOAL_SEEK_OK;
		}

		stepsize = gnumabs (data->xpos - data->xneg)
			/ (gnumabs (data->xpos) + gnumabs (data->xneg));

#ifdef DEBUG_GOAL_SEEK
		printf ("                                          ss = %.20" GNUM_FORMAT_g "\n", stepsize);
#endif

		if (stepsize < data->precision) {
			if (data->yneg < ymid)
				ymid = data->yneg, xmid = data->xneg;

			if (data->ypos < ymid)
				ymid = data->ypos, xmid = data->xpos;

			data->root = xmid;
			return GOAL_SEEK_OK;
		}
	}
	return GOAL_SEEK_ERROR;
}

#undef SECANT_P
#undef RIDDER_P

GoalSeekStatus
goal_seek_trawl_uniformly (GoalSeekFunction f,
			   GoalSeekData *data, void *user_data,
			   gnm_float xmin, gnm_float xmax,
			   int points)
{
	int i;

	if (xmin > xmax || xmin < data->xmin || xmax > data->xmax)
		return GOAL_SEEK_ERROR;

	for (i = 0; i < points; i++) {
		gnm_float x, y;
		GoalSeekStatus status;

		if (data->havexpos && data->havexneg)
			break;

		x = xmin + (xmax - xmin) * random_01 ();
		status = f (x, &y, user_data);
		if (status != GOAL_SEEK_OK)
			/* We are not depending on the result, so go on.  */
			continue;

#ifdef DEBUG_GOAL_SEEK
		printf ("x = %.20" GNUM_FORMAT_g "\n", x);
		printf ("                                        y = %.20" GNUM_FORMAT_g "\n", y);
#endif

		if (update_data (x, y, data))
			return GOAL_SEEK_OK;
	}

	/* We were not (extremely) lucky, so we did not actually hit the
	   root.  We report this as an error.  */
	return GOAL_SEEK_ERROR;
}

GoalSeekStatus
goal_seek_trawl_normally (GoalSeekFunction f,
			  GoalSeekData *data, void *user_data,
			  gnm_float mu, gnm_float sigma,
			  int points)
{
	int i;

	if (sigma <= 0 || mu < data->xmin || mu > data->xmax)
		return GOAL_SEEK_ERROR;

	for (i = 0; i < points; i++) {
		gnm_float x, y;
		GoalSeekStatus status;

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
		printf ("x = %.20" GNUM_FORMAT_g "\n", x);
		printf ("                                        y = %.20" GNUM_FORMAT_g "\n", y);
#endif

		if (update_data (x, y, data))
			return GOAL_SEEK_OK;
	}

	/* We were not (extremely) lucky, so we did not actually hit the
	   root.  We report this as an error.  */
	return GOAL_SEEK_ERROR;
}

#ifdef STANDALONE
static GoalSeekStatus
f (gnm_float x, gnm_float *y, void *user_data)
{
	*y = x * x - 2;
	return GOAL_SEEK_OK;
}

static GoalSeekStatus
df (gnm_float x, gnm_float *y, void *user_data)
{
	*y = 2 * x;
	return GOAL_SEEK_OK;
}


int
main ()
{
	GoalSeekData data;

	goal_seek_initialise (&data);
	data.xmin = -100;
	data.xmax = 100;

	goal_seek_newton (f, NULL, &data, NULL, 50.0);

	goal_seek_newton (f, df, &data, NULL, 50.0);

	return 0;
}
#endif
