#include <gnumeric-config.h>
#include <gnumeric.h>
#include <mathfunc.h>
#include <sf-dpq.h>
#include <sf-trig.h>
#include <sf-gamma.h>
#include "extra.h"

/* ------------------------------------------------------------------------- */

/* The skew-normal distribution.  */

gnm_float
dsnorm (gnm_float x, gnm_float shape, gnm_float location, gnm_float scale, gboolean give_log)
{
	if (gnm_isnan (x) || gnm_isnan (shape) || gnm_isnan (location) || gnm_isnan (scale))
		return gnm_nan;

	if (shape == 0)
		return dnorm (x, location, scale, give_log);
	else if (give_log)
		return M_LN2gnum + dnorm (x, location, scale, TRUE) + pnorm (shape * x, shape * location, scale, TRUE, TRUE);
	else
		return 2 * dnorm (x, location, scale, FALSE) * pnorm (shape * x, location/shape, scale, TRUE, FALSE);
}

gnm_float
psnorm (gnm_float x, gnm_float shape, gnm_float location, gnm_float scale, gboolean lower_tail, gboolean log_p)
{
	gnm_float result, h;

	if (gnm_isnan (x) || gnm_isnan (shape) ||
	    gnm_isnan (location) || gnm_isnan (scale))
		return gnm_nan;

	if (shape == 0)
		return pnorm (x, location, scale, lower_tail, log_p);

	/* Normalize */
	h = (x - location) / scale;

	/* Flip to a lower-tail problem.  */
	if (!lower_tail) {
		h = -h;
		shape = -shape;
		lower_tail = !lower_tail;
	}

	if (gnm_abs (shape) < 10) {
		gnm_float s = pnorm (h, 0, 1, lower_tail, FALSE);
		gnm_float t = 2 * gnm_owent (h, shape);
		result = s - t;
	} else {
		/*
		 * Make use of this result for Owen's T:
		 *
		 * T(h,a) = .5N(h) + .5N(ha) - N(h)N(ha) - T(ha,1/a)
		 */
		gnm_float s = pnorm (h * shape, 0, 1, TRUE, FALSE);
		gnm_float u = gnm_erf (h / M_SQRT2gnum);
		gnm_float t = 2 * gnm_owent (h * shape, 1 / shape);
		result = s * u + t;
	}

	/*
	 * Negatives can occur due to rounding errors and hopefully for no
	 * other reason.
	 */
	result = CLAMP (result, 0, 1);

	if (log_p)
		return gnm_log (result);
	else
		return result;
}

static gnm_float
dsnorm1 (gnm_float x, const gnm_float params[], gboolean log_p)
{
	return dsnorm (x, params[0], params[1], params[2], log_p);
}

static gnm_float
psnorm1 (gnm_float x, const gnm_float params[],
	 gboolean lower_tail, gboolean log_p)
{
	return psnorm (x, params[0], params[1], params[2], lower_tail, log_p);
}

gnm_float
qsnorm (gnm_float p, gnm_float shape, gnm_float location, gnm_float scale,
	gboolean lower_tail, gboolean log_p)
{
	gnm_float x0;
	gnm_float params[3];

	if (gnm_isnan (p) || gnm_isnan (shape) || gnm_isnan (location) || gnm_isnan (scale))
		return gnm_nan;

	if (shape == 0)
		return qnorm (p, location, scale, lower_tail, log_p);

	if (!log_p && p > GNM_const(0.9)) {
		/* We're far into the tail.  Flip.  */
		p = 1 - p;
		lower_tail = !lower_tail;
	}

	x0 = 0.0;
	params[0] = shape;
	params[1] = location;
	params[2] = scale;
	return pfuncinverter (p, params, lower_tail, log_p,
			      gnm_ninf, gnm_pinf, x0,
			      psnorm1, dsnorm1);
}

/* ------------------------------------------------------------------------- */

/* The skew-t distribution.  */

gnm_float
dst (gnm_float x, gnm_float n, gnm_float shape, gboolean give_log)
{
	if (n <= 0 || gnm_isnan (x) || gnm_isnan (n) || gnm_isnan (shape))
		return gnm_nan;

	if (shape == 0)
		return dt (x, n, give_log);
	else {
		gnm_float pdf = dt (x, n, give_log);
		gnm_float cdf = pt (shape * x * gnm_sqrt ((n + 1)/(x * x + n)),
				    n + 1, TRUE, give_log);
		return give_log ? (M_LN2gnum + pdf + cdf) : (2 * pdf * cdf);
	}
}

static gnm_float
gnm_atan_mpihalf (gnm_float x)
{
	if (x > 0)
		return gnm_acot (-x);
	else
		return gnm_atan (x) - (M_PIgnum / 2);
}

gnm_float
pst (gnm_float x, gnm_float n, gnm_float shape, gboolean lower_tail, gboolean log_p)
{
	gnm_float p;

	if (n <= 0 || gnm_isnan (x) || gnm_isnan (n) || gnm_isnan (shape))
		return gnm_nan;

	if (shape == 0)
		return pt (x, n, lower_tail, log_p);

	if (n > 100) {
		/* Approximation */
		return psnorm (x, shape, 0.0, 1.0, lower_tail, log_p);
	}

	/* Flip to a lower-tail problem.  */
	if (!lower_tail) {
		x = -x;
		shape = -shape;
		lower_tail = !lower_tail;
	}

	/* Generic fallback.  */
	if (log_p)
		gnm_log (pst (x, n, shape, TRUE, FALSE));

	if (n != gnm_floor (n)) {
		/* We would need numerical integration for this.  */
		return gnm_nan;
	}

	/*
	 * Use recurrence formula from "Recurrent relations for
	 * distributions of a skew-t and a linear combination of order
	 * statistics form a bivariate-t", Computational Statistics
	 * and Data Analysis volume 52, 2009 by Jamallizadeh,
	 * Khosravi, Balakrishnan.
	 *
	 * This brings us down to n==1 or n==2 for which explicit formulas
	 * are available.
	 */

	p = 0;
	while (n > 2) {
		gnm_float a, lb, c, d, pv, v = n - 1;

		d = v == 2
			? M_LN2gnum - gnm_log (M_PIgnum) + gnm_log (3) / 2
			: (GNM_const(0.5) + M_LN2gnum / 2 - gnm_log (M_PIgnum) / 2 +
			   v / 2 * (gnm_log1p (-1 / (v - 1)) + gnm_log (v + 1)) -
			   GNM_const(0.5) * (gnm_log (v - 2) + gnm_log (v + 1)) +
			   stirlerr (v / 2 - 1) -
			   stirlerr ((v - 1) / 2));

		a = v + 1 + x * x;
		lb = (d - gnm_log (a) * v / 2);
		c = pt (gnm_sqrt (v) * shape * x / gnm_sqrt (a), v, TRUE, FALSE);
		pv = x * gnm_exp (lb) * c;
		p += pv;

		n -= 2;
		x *= gnm_sqrt ((v - 1) / (v + 1));
	}

	g_return_val_if_fail (n == 1 || n == 2, gnm_nan);
	if (n == 1) {
		gnm_float p1;

		p1 = (gnm_atan (x) + gnm_acos (shape / gnm_sqrt ((1 + shape * shape) * (1 + x * x)))) / M_PIgnum;
		p += p1;
	} else if (n == 2) {
		gnm_float p2, f;

		f = x / gnm_sqrt (2 + x * x);

		p2 = (gnm_atan_mpihalf (shape) + f * gnm_atan_mpihalf (-shape * f)) / -M_PIgnum;

		p += p2;
	} else {
		return gnm_nan;
	}

	/*
	 * Negatives can occur due to rounding errors and hopefully for no
	 * other reason.
	 */
	p = CLAMP (p, 0, 1);

	return p;
}


static gnm_float
dst1 (gnm_float x, const gnm_float params[], gboolean log_p)
{
	return dst (x, params[0], params[1], log_p);
}

static gnm_float
pst1 (gnm_float x, const gnm_float params[],
      gboolean lower_tail, gboolean log_p)
{
	return pst (x, params[0], params[1], lower_tail, log_p);
}

gnm_float
qst (gnm_float p, gnm_float n, gnm_float shape,
     gboolean lower_tail, gboolean log_p)
{
	gnm_float x0;
	gnm_float params[2];

	if (n <= 0 || gnm_isnan (p) || gnm_isnan (n) || gnm_isnan (shape))
		return gnm_nan;

	if (shape == 0)
		return qt (p, n, lower_tail, log_p);

	if (!log_p && p > GNM_const(0.9)) {
		/* We're far into the tail.  Flip.  */
		p = 1 - p;
		lower_tail = !lower_tail;
	}

	x0 = 0.0;
	params[0] = n;
	params[1] = shape;
	return pfuncinverter (p, params, lower_tail, log_p,
			      gnm_ninf, gnm_pinf, x0,
			      pst1, dst1);
}

/* ------------------------------------------------------------------------- */

gnm_float
dgumbel (gnm_float x, gnm_float mu, gnm_float beta, gboolean give_log)
{
	gnm_float z, lp;

	if (!(beta > 0) || gnm_isnan (mu) || gnm_isnan (beta) || gnm_isnan (x))
		return gnm_nan;

	z = (x - mu) / beta;
	lp = -(z + gnm_exp (-z));
	return give_log ? lp - gnm_log (beta) : gnm_exp (lp) / beta;
}

gnm_float
pgumbel (gnm_float x, gnm_float mu, gnm_float beta, gboolean lower_tail, gboolean log_p)
{
	gnm_float z, lp;

	if (!(beta > 0) || gnm_isnan (mu) || gnm_isnan (beta) || gnm_isnan (x))
		return gnm_nan;

	z = (x - mu) / beta;
	lp = -gnm_exp (-z);
	if (lower_tail)
		return log_p ? lp : gnm_exp (lp);
	else
		return log_p ? swap_log_tail (lp) : 0 - gnm_expm1 (lp);
}

gnm_float
qgumbel (gnm_float p, gnm_float mu, gnm_float beta, gboolean lower_tail, gboolean log_p)
{
	if (!(beta > 0) ||
	    gnm_isnan (mu) || gnm_isnan (beta) || gnm_isnan (p) ||
	    (log_p ? p > 0 : (p < 0 || p > 1)))
		return gnm_nan;

	if (log_p) {
		if (!lower_tail)
			p = swap_log_tail (p);
	} else {
		if (lower_tail)
			p = gnm_log (p);
		else
			p = gnm_log1p (-p);
	}

	/* We're now in the log_p, lower_tail case.  */
	return mu - beta * gnm_log (-p);
}
