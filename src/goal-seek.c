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

#include <config.h>
#include <gnome.h>
#include <stdlib.h>
#include <math.h>
#include "goal-seek.h"
#include "gnumeric.h"
#include "utils.h"

/*
 * This value should be comfortably within the relative precision of a float_t.
 * For doubles, that means something like 10^-DBL_DIG.
 */
#define XRELSTEP_MIN 1e-12

#ifdef DEBUG_GOAL_SEEK
#include <stdio.h>
#endif

static gboolean
updata_data (float_t x, float_t y, GoalSeekData *data)
{
	if (y > 0) {
		if (data->havexpos) {
			if (data->havexneg &&
			    fabs (x - data->xneg) < fabs (data->xpos - data->xneg)) {
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
			if (data->havexpos &&
			    fabs (x - data->xpos) < fabs (data->xpos - data->xneg)) {
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
fake_df (GoalSeekFunction f, float_t x, float_t *dfx, float_t xrelstep,
	 GoalSeekData *data, void *user_data)
{
	float_t xstep, xl, xr, yl, yr;
	GoalSeekStatus status;

	xstep = fabs (x) * xrelstep;

	xl = x - xstep;
	if (xl < data->xmin)
		xl = x;

	xr = x + xstep;
	if (xr > data->xmax)
		xr = x;

	if (xl == xr)
		return GOAL_SEEK_ERROR;

	status = f (xl, &yl, user_data);
	if (status != GOAL_SEEK_OK)
		return status;

	status = f (xr, &yr, user_data);
	if (status != GOAL_SEEK_OK)
		return status;

	*dfx = (yr - yl) / (xr - xl);
	return GOAL_SEEK_OK;
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
		  GoalSeekData *data, void *user_data, float_t x0)
{
	int iterations;
	float_t xrelstep = 1e-3;
	float_t precision = data->precision / 2;

	for (iterations = 0; iterations < 20; iterations++) {
		float_t x1, y0, df0, stepsize;
		GoalSeekStatus status;

		/* Check whether we have left the valid interval.  */
		if (x0 < data->xmin || x0 > data->xmax)
			return GOAL_SEEK_ERROR;

		status = f (x0, &y0, user_data);
		if (status != GOAL_SEEK_OK)
			return status;

		if (updata_data (x0, y0, data))
			return GOAL_SEEK_OK;

		if (df)
			status = df (x0, &df0, user_data);
		else
			status = fake_df (f, x0, &df0, xrelstep, data, user_data);
		if (status != GOAL_SEEK_OK)
			return status;

		/* If we hit a flat spot, we are in trouble.  */
		if (df0 == 0)
			return GOAL_SEEK_ERROR;

		x1 = x0 - y0 / df0;
		if (x1 == x0) {
			data->root = x0;
			return GOAL_SEEK_OK;
		}

		stepsize = fabs (x1 - x0) / (fabs (x0) + fabs (x1));

#ifdef DEBUG_GOAL_SEEK
		printf ("x0 = %.20g\n", x0);
		printf ("                                        y0 = %.20g\n", y0);
		printf ("                                        ss = %.20g\n", stepsize);
#endif

		if (stepsize < precision) {
			data->root = x0;
			return GOAL_SEEK_OK;
		}

		/* As we get closer to the root, improve the derivation.  */
		if (stepsize * 1000 < xrelstep)
			xrelstep = MAX (xrelstep / 1000, XRELSTEP_MIN);

		x0 = x1;
	}

	return GOAL_SEEK_ERROR;
}

/*
 * Seek a goal (root) using bisection methods.
 *
 * The supplied function must (should) be continously over the interval.
 *
 * Caller must have located a positive and a negative point.
 *
 * This method will find a root steadily using bisection to narrow the
 * interval in which a root lies.
 *
 * It alternates between mid-point-bisection (semi-slow, but guaranteed
 * progress) and secant-bisection (usually quite fast, but sometimes gets
 * nowhere).
 */

#define SECANT_P(i) ((i) % 3 != 0)

GoalSeekStatus
goal_seek_bisection (GoalSeekFunction f, GoalSeekData *data, void *user_data)
{
	int iterations;

	if (!data->havexpos || !data->havexneg)
		return GOAL_SEEK_ERROR;

	for (iterations = 0; iterations < 60; iterations++) {
		float_t xmid, ymid, stepsize;
		GoalSeekStatus status;

		if (SECANT_P (iterations)) {
			/* Use secant method.  */
			xmid = data->xpos - data->ypos *
				((data->xneg - data->xpos) /
				 (data->yneg - data->ypos));
		} else {
			/* Use plain midpoint.  */
			xmid = (data->xpos + data->xneg) / 2;
		}

		status = f (xmid, &ymid, user_data);
		if (status != GOAL_SEEK_OK)
			return status;

		if (updata_data (xmid, ymid, data))
			return GOAL_SEEK_OK;

		stepsize = fabs (data->xpos - data->xneg)\
			/ (fabs (data->xpos) + fabs (data->xneg));

#ifdef DEBUG_GOAL_SEEK
		printf ("xmid = %.20g (%s)\n", xmid,
			SECANT_P (iterations) ? "secant" : "mid-point");
		printf ("                                        ymid = %.20g\n", ymid);
		printf ("                                          ss = %.20g\n", stepsize);
#endif

		if (stepsize < data->precision) {
			data->root = xmid;
			return GOAL_SEEK_OK;
		}
	}
	return GOAL_SEEK_ERROR;
}

#undef SECANT_P

GoalSeekStatus
goal_seek_trawl_uniformly (GoalSeekFunction f,
			   GoalSeekData *data, void *user_data,
			   float_t xmin, float_t xmax,
			   int points)
{
	int i;

	if (xmin > xmax || xmin < data->xmin || xmax > data->xmax)
		return GOAL_SEEK_ERROR;

	for (i = 0; i < points; i++) {
		float_t x, y;
		GoalSeekStatus status;

		if (data->havexpos && data->havexneg)
			break;

		x = xmin + (xmax - xmin) * random_01 ();
		status = f (x, &y, user_data);
		if (status != GOAL_SEEK_OK)
			/* We are not depending on the result, so go on.  */
			continue;

#ifdef DEBUG_GOAL_SEEK
		printf ("x = %.20g\n", x);
		printf ("                                        y = %.20g\n", y);
#endif

		if (updata_data (x, y, data))
			return GOAL_SEEK_OK;
	}

	/* We were not (extremely) lucky, so we did not actually hit the
	   root.  We report this as an error.  */
	return GOAL_SEEK_ERROR;
}

GoalSeekStatus
goal_seek_trawl_normally (GoalSeekFunction f,
			  GoalSeekData *data, void *user_data,
			  float_t mu, float_t sigma,
			  int points)
{
	int i;

	if (sigma <= 0 || mu < data->xmin || mu > data->xmax)
		return GOAL_SEEK_ERROR;

	for (i = 0; i < points; i++) {
		float_t x, y;
		GoalSeekStatus status;

		if (data->havexpos && data->havexneg)
			break;

		x = mu + sigma * random_normal ();
		status = f (x, &y, user_data);
		if (status != GOAL_SEEK_OK)
			/* We are not depending on the result, so go on.  */
			continue;

#ifdef DEBUG_GOAL_SEEK
		printf ("x = %.20g\n", x);
		printf ("                                        y = %.20g\n", y);
#endif

		if (updata_data (x, y, data))
			return GOAL_SEEK_OK;
	}

	/* We were not (extremely) lucky, so we did not actually hit the
	   root.  We report this as an error.  */
	return GOAL_SEEK_ERROR;
}

#ifdef STANDALONE
static GoalSeekStatus
f (float_t x, float_t *y, void *user_data)
{
	*y = x * x - 2;
	return GOAL_SEEK_OK;
}

static GoalSeekStatus
df (float_t x, float_t *y, void *user_data)
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
