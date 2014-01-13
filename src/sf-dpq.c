#include <gnumeric-config.h>
#include <sf-dpq.h>
#include <mathfunc.h>

#define give_log log_p
#define R_D__0	(log_p ? gnm_ninf : 0.0)
#define R_D__1	(log_p ? 0.0 : 1.0)
#define R_DT_0	(lower_tail ? R_D__0 : R_D__1)
#define R_DT_1	(lower_tail ? R_D__1 : R_D__0)
#define M_1_SQRT_2PI    GNM_const(0.398942280401432677939946059934)  /* 1/sqrt(2pi) */

/* ------------------------------------------------------------------------- */

#undef DEBUG_pfuncinverter

/**
 * pfuncinverter:
 * @p:
 * @shape:
 * @lower_tail:
 * @log_p:
 * @xlow:
 * @xhigh:
 * @x0:
 * @pfunc: (scope call):
 * @dpfunc_dx: (scope call):
 *
 * Returns:
 **/
gnm_float
pfuncinverter (gnm_float p, const gnm_float shape[],
	       gboolean lower_tail, gboolean log_p,
	       gnm_float xlow, gnm_float xhigh, gnm_float x0,
	       GnmPFunc pfunc, GnmDPFunc dpfunc_dx)
{
	gboolean have_xlow = gnm_finite (xlow);
	gboolean have_xhigh = gnm_finite (xhigh);
	gnm_float exlow, exhigh;
	gnm_float x = 0, e = 0, px;
	int i;

	g_return_val_if_fail (pfunc != NULL, gnm_nan);

	if (log_p ? (p > 0) : (p < 0 || p > 1))
		return gnm_nan;

	if (p == R_DT_0) return xlow;
	if (p == R_DT_1) return xhigh;

	exlow = R_DT_0 - p;
	exhigh = R_DT_1 - p;
	if (!lower_tail) {
		exlow = -exlow;
		exhigh = -exhigh;
	}

#ifdef DEBUG_pfuncinverter
	g_printerr ("p=%.15g\n", p);
#endif

	for (i = 0; i < 100; i++) {
		if (i == 0) {
			if (x0 > xlow && x0 < xhigh)
				/* Use supplied guess.  */
				x = x0;
			else if (have_xlow && x0 <= xlow)
				x = xlow + have_xhigh ? (xhigh - xlow) / 100 : 1;
			else if (have_xhigh && x0 >= xhigh)
				x = xhigh - have_xlow ? (xhigh - xlow) / 100 : 1;
			else
				x = 0;  /* Whatever */
		} else if (i == 1) {
			/*
			 * Under the assumption that the initial guess was
			 * good, pick a nearby point that is hopefully on
			 * the other side.  If we already have both sides,
			 * just bisect.
			 */
			if (have_xlow && have_xhigh)
				x = (xlow + xhigh) / 2;
			else if (have_xlow)
				x = xlow * 1.1;
			else
				x = xhigh / 1.1;
		} else if (have_xlow && have_xhigh) {
			switch (i % 8) {
			case 0:
				x = xhigh - (xhigh - xlow) *
					(exhigh / (exhigh - exlow));
				break;
			case 4:
				/* Half-way in log-space.  */
				if (xlow >= 0 && xhigh >= 0)
					x = gnm_sqrt (MAX (GNM_MIN, xlow)) * gnm_sqrt (xhigh);
				else if (xlow <= 0 && xhigh <= 0)
					x = -gnm_sqrt (-xlow) * gnm_sqrt (MAX (GNM_MIN, -xhigh));
				else
					x = 0;
				break;
			case 2:
				x = (xhigh + 1000 * xlow) / 1001;
				break;
			case 6:
				x = (1000 * xhigh + xlow) / 1001;
				break;
			default:
				x = (xhigh + xlow) / 2;
			}
		} else if (have_xlow) {
			/* Agressively seek right in search of xhigh.  */
			x = (xlow < 1) ? 1 : (2 * i) * xlow;
		} else {
			/* Agressively seek left in search of xlow.  */
			x = (xhigh > -1) ? -1 : (2 * i) * xhigh;
		}

	newton_retry:
		if ((have_xlow && x <= xlow) || (have_xhigh && x >= xhigh))
			continue;

		px = pfunc (x, shape, lower_tail, log_p);
		e = px - p;
		if (!lower_tail) e = -e;

#ifdef DEBUG_pfuncinverter
		g_printerr ("%3d:  x=%.15g  e=%.15g  l=%.15g  h=%.15g\n",
			    i, x, e, xlow, xhigh);
#endif

		if (e == 0)
			goto done;
		else if (e > 0) {
			xhigh = x;
			exhigh = e;
			have_xhigh = TRUE;
		} else if (e < 0) {
			xlow = x;
			exlow = e;
			have_xlow = TRUE;
		} else {
			/* We got a NaN.  */
		}

		if (have_xlow && have_xhigh) {
			gnm_float prec = (xhigh - xlow) /
				(gnm_abs (xlow) + gnm_abs (xhigh));
			if (prec < GNM_EPSILON * 4) {
				x = (xhigh + xlow) / 2;
				e = pfunc (x, shape, lower_tail, log_p) - p;
				if (!lower_tail) e = -e;
				goto done;
			}

			if (dpfunc_dx && i % 3 < 2 && (i == 0 || prec < 0.05)) {
				gnm_float d = dpfunc_dx (x, shape, log_p);
				if (log_p) d = gnm_exp (d - px);
#ifdef DEBUG_pfuncinverter
				g_printerr ("Newton: d=%-.14g\n", d);
#endif
				if (d) {
					/*
					 * Deliberately overshoot a bit to help
					 * with getting good points on both
					 * sides of the root.
					 */
					x = x - e / d * 1.000001;
					if (x > xlow && x < xhigh) {
#ifdef DEBUG_pfuncinverter
						g_printerr ("Newton ok\n");
#endif
						i++;
						goto newton_retry;
					}
				} else {
#ifdef DEBUG_pfuncinverter
						g_printerr ("Newton d=0\n");
#endif
				}
			}
		}
	}

#ifdef DEBUG_pfuncinverter
	g_printerr ("Failed to converge\n");
#endif

 done:
	/* Make sure to keep a lucky near-hit.  */

	if (have_xhigh && gnm_abs (e) > exhigh)
		e = exhigh, x = xhigh;
	if (have_xlow && gnm_abs (e) > -exlow)
		e = exlow, x = xlow;

#ifdef DEBUG_pfuncinverter
	g_printerr ("--> %.15g\n\n", x);
#endif
	return x;
}

/**
 * discpfuncinverter:
 * @p:
 * @shape:
 * @lower_tail:
 * @log_p:
 * @xlow:
 * @xhigh:
 * @x0:
 * @pfunc: (scope call):
 *
 * Discrete pfuncs only.  (Specifically: only integer x are allowed).
 * Returns:
 */
gnm_float
discpfuncinverter (gnm_float p, const gnm_float shape[],
		   gboolean lower_tail, gboolean log_p,
		   gnm_float xlow, gnm_float xhigh, gnm_float x0,
		   GnmPFunc pfunc)
{
	gboolean have_xlow = gnm_finite (xlow);
	gboolean have_xhigh = gnm_finite (xhigh);
	gnm_float step;
	int i;

	if (log_p ? (p > 0) : (p < 0 || p > 1))
		return gnm_nan;

	if (p == R_DT_0) return xlow;
	if (p == R_DT_1) return xhigh;

	if (gnm_finite (x0) && x0 >= xlow && x0 <= xhigh)
		; /* Nothing -- guess is good.  */
	else if (have_xlow && have_xhigh)
		x0 = (xlow + xhigh) / 2;
	else if (have_xhigh)
		x0 = xhigh;
	else if (have_xlow)
		x0 = xlow;
	else
		x0 = 0;
	x0 = gnm_floor (x0 + 0.5);
	step = 1 + gnm_floor (gnm_abs (x0) * GNM_EPSILON);

#if 0
	g_printerr ("step=%.20g\n", step);
#endif

	for (i = 1; 1; i++) {
		gnm_float ex0 = pfunc (x0, shape, lower_tail, log_p) - p;
#if 0
		g_printerr ("x=%.20g  e=%.20g\n", x0, ex0);
#endif
		if (!lower_tail) ex0 = -ex0;
		if (ex0 <= 0)
			xlow = x0, have_xlow = TRUE;
		if (ex0 >= 0)
			xhigh = x0, have_xhigh = TRUE, step = -gnm_abs (step);

		if (i > 1 && have_xlow && have_xhigh) {
			gnm_float xmid = gnm_floor ((xlow + xhigh) / 2);
			if (xmid - xlow < 0.5 ||
			    xmid - xlow < gnm_abs (xlow) * GNM_EPSILON)
				return xhigh;
			x0 = xmid;
		} else {
			gnm_float x1 = x0 + step;

			if (x1 == x0) {
				/* Probably infinite.  */
				return gnm_nan;
			} else if (x1 >= xlow && x1 <= xhigh) {
				x0 = x1;
				step *= 2 * i;
			} else {
				/* We went off the edge by walking too fast.  */
				gnm_float newstep = 1 + gnm_floor (gnm_abs (x0) * GNM_EPSILON);
				step = (step > 0) ? newstep : -newstep;
				x1 = x0 + step;
				if (x1 >= xlow && x1 <= xhigh)
					continue;
				/*
				 * We don't seem to find a finite x on the
				 * other side of the root.
				 */
				return (step > 0) ? xhigh : xlow;
			}
		}
	}

	g_assert_not_reached ();
}

/* ------------------------------------------------------------------------ */

/**
 * dnorm:
 * @x: observation
 * @mu: mean of the distribution
 * @sigma: standard deviation of the distribution
 * @give_log: if %TRUE, log of the result will be returned instead
 *
 * Returns: density of the normal distribution.
 */
gnm_float
dnorm (gnm_float x, gnm_float mu, gnm_float sigma, gboolean give_log)
{
	if (gnm_isnan (x) || gnm_isnan (mu) || gnm_isnan (sigma))
		return x + mu + sigma;
	if (sigma < 0)
		return gnm_nan;

	x = gnm_abs ((x - mu) / sigma);

	if (x >= 2 * gnm_sqrt (GNM_MAX))
		return R_D__0;
	if (give_log)
		return -(M_LN_SQRT_2PI + 0.5 * x * x + gnm_log (sigma));
	else
		return M_1_SQRT_2PI * expmx2h (x) / sigma;
}

gnm_float
pnorm2 (gnm_float x1, gnm_float x2)
{
	if (gnm_isnan (x1) || gnm_isnan (x2))
		return gnm_nan;

	if (x1 > x2)
		return 0 - pnorm2 (x2, x1);

	/* A bunch of special cases:  */
	if (x1 == x2)
		return 0.0;
	if (x1 == gnm_ninf)
		return pnorm (x2, 0.0, 1.0, TRUE, FALSE);
	if (x2 == gnm_pinf)
		return pnorm (x1, 0.0, 1.0, FALSE, FALSE);
	if (x1 == 0)
		return gnm_erf (x2 / M_SQRT2gnum) / 2;
	if (x2 == 0)
		return gnm_erf (x1 / -M_SQRT2gnum) / 2;

	if (x1 <= 0 && x2 >= 0) {
		/* The interval spans 0.  */
		gnm_float p1 = pnorm2 (0, MIN (-x1, x2));
		gnm_float p2 = pnorm2 (MIN (-x1, x2), MAX (-x1, x2));
		return 2 * p1 + p2;
	} else if (x1 < 0) {
		/* Both < 0 -- use symmetry */
		return pnorm2 (-x2, -x1);
	} else {
		/* Both >= 0 */
		gnm_float p1C = pnorm (x1, 0.0, 1.0, FALSE, FALSE);
		gnm_float p2C = pnorm (x2, 0.0, 1.0, FALSE, FALSE);
		gnm_float raw = p1C - p2C;
		gnm_float dx, d1, d2, ub, lb;

		if (gnm_abs (p1C - p2C) * 32 > gnm_abs (p1C + p2C))
			return raw;

		/* dnorm is strictly decreasing in this area.  */
		dx = x2 - x1;
		d1 = dnorm (x1, 0.0, 1.0, FALSE);
		d2 = dnorm (x2, 0.0, 1.0, FALSE);
		ub = dx * d1;  /* upper bound */
		lb = dx * d2;  /* lower bound */

		raw = MAX (raw, lb);
		raw = MIN (raw, ub);
		return raw;
	}
}

/* ------------------------------------------------------------------------ */

/**
 * dlnorm:
 * @x: observation
 * @logmean: mean of the underlying normal distribution
 * @logsd: standard deviation of the underlying normal distribution
 * @give_log: if %TRUE, log of the result will be returned instead
 *
 * Returns: density of the log-normal distribution.
 */
gnm_float
dlnorm (gnm_float x, gnm_float logmean, gnm_float logsd, gboolean give_log)
{
	void *state;
	GnmQuad qx, qlx, qs, qy, qt;
	static GnmQuad qsqrt2pi;
	gnm_float r;

	if (gnm_isnan (x) || gnm_isnan (logmean) || gnm_isnan (logsd))
		return x + logmean + logsd;

	if (logsd <= 0)
		return gnm_nan;

	if (x <= 0)
		return R_D__0;

	state = gnm_quad_start ();
	if (qsqrt2pi.h == 0)
		gnm_quad_sqrt (&qsqrt2pi, &gnm_quad_2pi);
	gnm_quad_init (&qx, x);
	gnm_quad_log (&qlx, &qx);
	gnm_quad_init (&qt, logmean);
	gnm_quad_sub (&qy, &qlx, &qt);
	gnm_quad_init (&qs, logsd);
	gnm_quad_div (&qy, &qy, &qs);
	gnm_quad_mul (&qy, &qy, &qy);
	qy.h *= -0.5; qy.l *= -0.5;
	gnm_quad_mul (&qt, &qs, &qx);
	gnm_quad_mul (&qt, &qt, &qsqrt2pi);
	if (give_log) {
		gnm_quad_log (&qt, &qt);
		gnm_quad_sub (&qy, &qy, &qt);
	} else {
		gnm_quad_exp (&qy, NULL, &qy);
		gnm_quad_div (&qy, &qy, &qt);
	}
	r = gnm_quad_value (&qy);
	gnm_quad_end (state);

	return r;
}

/* ------------------------------------------------------------------------ */

/**
 * plnorm:
 * @x: observation
 * @logmean: mean of the underlying normal distribution
 * @logsd: standard deviation of the underlying normal distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, log of the result will be returned instead
 *
 * Returns: cumulative density of the log-normal distribution.
 */
gnm_float
plnorm (gnm_float x, gnm_float logmean, gnm_float logsd, gboolean lower_tail, gboolean log_p)
{
	if (gnm_isnan (x) || gnm_isnan (logmean) || gnm_isnan (logsd))
		return x + logmean + logsd;

	if (logsd <= 0)
		return gnm_nan;

	return (x > 0)
		? pnorm (gnm_log (x), logmean, logsd, lower_tail, log_p)
		: R_D__0;
}

/* ------------------------------------------------------------------------ */

/**
 * qlnorm:
 * @p: probability
 * @logmean: mean of the underlying normal distribution
 * @logsd: standard deviation of the underlying normal distribution
 * @lower_tail: if %TRUE, the lower tail of the distribution is considered.
 * @log_p: if %TRUE, @p is given as log probability
 *
 * Returns: the observation with cumulative probability @p for the
 * log-normal distribution.
 */
gnm_float
qlnorm (gnm_float p, gnm_float logmean, gnm_float logsd, gboolean lower_tail, gboolean log_p)
{
	if (gnm_isnan (p) || gnm_isnan (logmean) || gnm_isnan (logsd))
		return p + logmean + logsd;

	if (log_p ? (p > 0) : (p < 0 || p > 1))
		return gnm_nan;

	return gnm_exp (qnorm (p, logmean, logsd, lower_tail, log_p));
}
