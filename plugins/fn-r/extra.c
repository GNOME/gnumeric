#include <gnumeric-config.h>
#include "gnumeric.h"
#include <mathfunc.h>
#include "extra.h"

#define ML_ERR_return_NAN { return gnm_nan; }

/* ------------------------------------------------------------------------- */
/* --- BEGIN MAGIC R SOURCE MARKER --- */

#define R_Q_P01_check(p)			\
    if ((log_p	&& p > 0) ||			\
	(!log_p && (p < 0 || p > 1)) )		\
	ML_ERR_return_NAN


/* ------------------------------------------------------------------------ */
/* --- END MAGIC R SOURCE MARKER --- */

gnm_float
qcauchy (gnm_float p, gnm_float location, gnm_float scale,
	 gboolean lower_tail, gboolean log_p)
{
	if (gnm_isnan(p) || gnm_isnan(location) || gnm_isnan(scale))
		return p + location + scale;

	R_Q_P01_check(p);
	if (scale < 0 || !gnm_finite(scale)) ML_ERR_return_NAN;

	if (log_p) {
		if (p > -1)
			/* The "0" here is important for the p=0 case:  */
			lower_tail = !lower_tail, p = 0 - gnm_expm1 (p);
		else
			p = gnm_exp (p);
	}
	if (lower_tail) scale = -scale;
	return location + scale / gnm_tan(M_PIgnum * p);
}


/* ------------------------------------------------------------------------- */

/* This implementation of Owen's T function is based on code licensed under GPL v.2: */

/*  GNU General Public License Agreement */
/*  Copyright (C) 2004-2007 CodeCogs, Zyba Ltd, Broadwood, Holford, TA5 1DU, England. */

/*  This program is free software; you can redistribute it and/or modify it under */
/*  the terms of the GNU General Public License as published by CodeCogs.  */
/*  You must retain a copy of this licence in all copies.  */

/*  This program is distributed in the hope that it will be useful, but WITHOUT ANY */
/*  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A */
/*  PARTICULAR PURPOSE. See the GNU General Public License for more details. */
/*  --------------------------------------------------------------------------------- */
/* ! Evaluates the Owen&#039;s T function. */

#define LIM1 1E-35
#define LIM2 15.0
#define LIM3 15.0
#define LIM4 1E-5

#define TWOPI_INVERSE 1/(2*M_PIgnum)

static gnm_float
gnm_owent (gnm_float h, gnm_float a)
{
	gnm_float weight[10] = { GNM_const(0.0666713443086881375935688098933),
				 GNM_const(0.149451349150580593145776339658),
				 GNM_const(0.219086362515982043995534934228),
				 GNM_const(0.269266719309996355091226921569),
				 GNM_const(0.295524224714752870173892994651),
				 GNM_const(0.295524224714752870173892994651),
				 GNM_const(0.269266719309996355091226921569),
				 GNM_const(0.219086362515982043995534934228),
				 GNM_const(0.149451349150580593145776339658),
				 GNM_const(0.0666713443086881375935688098933)};
	gnm_float xtab[10] = {GNM_const(0.026093471482828279922035987916),
			      GNM_const(0.134936633311015489267903311577),
			      GNM_const(0.320590431700975593765672634885),
			      GNM_const(0.566604605870752809200734056834),
			      GNM_const(0.85112566101836878911517399887),
			      GNM_const(1.148874338981631210884826001130),
			      GNM_const(1.433395394129247190799265943166),
			      GNM_const(1.679409568299024406234327365115),
			      GNM_const(1.865063366688984510732096688423),
			      GNM_const(1.973906528517171720077964012084)};
	gnm_float hs, h2, as, rt;
	int i;

	if (fabs(h) < LIM1) return atan(a) * TWOPI_INVERSE;
	if (fabs(h) > LIM2 || fabs(a) < LIM1) return 0.0;

	hs = -0.5 * h * h;
	h2 = a;
	as = a * a;

	if (log(1.0 + as) - hs * as >= LIM3)
	{
		gnm_float h1 = 0.5 * a;
		as *= 0.25;
		while (1)
		{
			gnm_float rt = as + 1.0;
			h2 = h1 + (hs * as + LIM3 - log(rt))
				/ (2.0 * h1 * (1.0 / rt - hs));
			as = h2 * h2;
			if (fabs(h2 - h1) < LIM4) break;
			h1 = h2;
		}
	}

	rt = 0.0;
	for (i = 0; i < 10; i++)
	{
		gnm_float x = 0.5 * h2 * xtab[i], tmp = 1.0 + x * x;
		rt += weight[i] * gnm_exp (hs * tmp) / tmp;
	}
	return 0.5 * rt * h2 * TWOPI_INVERSE;
}


#undef LIM1
#undef LIM2
#undef LIM3
#undef LIM4
#undef TWOPI_INVERSE

/* ------------------------------------------------------------------------- */

/* The skew-normal distribution.  */

gnm_float
dsnorm (gnm_float x, gnm_float shape, gnm_float location, gnm_float scale, gboolean give_log)
{
	if (shape == 0.)
		return dnorm (x, location, scale, give_log);
	else if (give_log)
		return M_LN2gnum + dnorm (x, location, scale, TRUE) + pnorm (shape * x, shape * location, scale, TRUE, TRUE);
	else
		return 2 * dnorm (x, location, scale, FALSE) * pnorm (shape * x, location/shape, scale, TRUE, FALSE);
}

gnm_float
psnorm (gnm_float x, gnm_float shape, gnm_float location, gnm_float scale, gboolean lower_tail, gboolean log_p)
{
	gnm_float result, a, b;

	if (shape == 0.)
		return pnorm (x, location, scale, lower_tail, log_p);

	a = pnorm (x, location, scale, lower_tail, FALSE);
	b = 2 * gnm_owent ((x - location) / scale, shape);
	result = lower_tail ? a - b : a + b;

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

	if (shape == 0.)
		return qnorm (p, location, scale, lower_tail, log_p);

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
	if (shape == 0.)
		return dt (x, n, give_log);
	else {
		gnm_float pdf = dt (x, n, give_log);
		gnm_float cdf = pt (shape * x * gnm_sqrt ((n + 1)/(x * x + n)),
				    n + 1, TRUE, give_log);
		return give_log ? (M_LN2gnum + pdf + cdf) : (2. * pdf * cdf);
	}
}

gnm_float
pst (gnm_float x, gnm_float n, gnm_float shape, gboolean lower_tail, gboolean log_p)
{
	gnm_float p;

	if (n <= 0)
		return gnm_nan;

	if (shape == 0.)
		return pt (x, n, lower_tail, log_p);

	if (n > 100) {
		/* Approximation */
		return psnorm (x, shape, 0.0, 1.0, lower_tail, log_p);
	}

	/* Generic fallback.  */
	if (!lower_tail)
		return log_p
			? swap_log_tail (pst (x, n, shape, TRUE, TRUE))
			: 1 - pst (x, n, shape, TRUE, FALSE);
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
		double a, lb, c, d, pv, v = n - 1;

		d = v == 2
			? M_LN2gnum - gnm_log (M_PIgnum) + gnm_log (3) / 2
			: (0.5 + M_LN2gnum / 2 - gnm_log (M_PIgnum) / 2 +
			   v / 2 * (gnm_log1p (-1 / (v - 1)) + gnm_log (v + 1)) -
			   0.5 * (gnm_log (v - 2) + gnm_log (v + 1)) +
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

		p2 = (0.5 - gnm_atan (shape) / M_PIgnum) +
			f * (0.5 + gnm_atan (shape * f) / M_PIgnum);

		p += p2;
	} else {
		return gnm_nan;
	}

	/*
	 * Negatives can occur due to rounding errors and hopefully for no
	 * other reason.
	 */
	p = CLAMP (p, 0.0, 1.0);

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

	if (shape == 0.)
		return qt (p, n, lower_tail, log_p);

	x0 = 0.0;
	params[0] = n;
	params[1] = shape;
	return pfuncinverter (p, params, lower_tail, log_p,
			      gnm_ninf, gnm_pinf, x0,
			      pst1, dst1);
}

/* ------------------------------------------------------------------------- */

