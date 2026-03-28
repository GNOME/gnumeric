#include <gnumeric-config.h>
#include <sf-trig.h>
#include <mathfunc.h>

/* ------------------------------------------------------------------------- */

static gnm_float
gnm_cot_helper (volatile gnm_float *x)
{
	gnm_float s = gnm_sin (*x);
	gnm_float c = gnm_cos (*x);

	if (s == 0)
		return gnm_nan;
	else
		return c / s;
}

/**
 * gnm_cot:
 * @x: an angle in radians
 *
 * Returns: The co-tangent of the given angle.
 */
gnm_float
gnm_cot (gnm_float x)
{
	/* See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=59089 */
	return gnm_cot_helper (&x);
}

/**
 * gnm_acot:
 * @x: a number
 *
 * Returns: The inverse co-tangent of the given number.
 */
gnm_float
gnm_acot (gnm_float x)
{
	if (gnm_finite (x)) {
		if (x == 0)
			return M_PIgnum / 2;
		return gnm_atan (1 / x);
	} else {
		/* +inf -> +0 */
		/* -Inf -> -0 */
		/* +-NaN -> +-NaN */
		return 1 / x;
	}
}

/**
 * gnm_coth:
 * @x: a number.
 *
 * Returns: The hyperbolic co-tangent of the given number.
 */
gnm_float
gnm_coth (gnm_float x)
{
	return 1 / gnm_tanh (x);
}

/**
 * gnm_acoth:
 * @x: a number
 *
 * Returns: The inverse hyperbolic co-tangent of the given number.
 */
gnm_float
gnm_acoth (gnm_float x)
{
	return (gnm_abs (x) > 2)
		? gnm_log1p (2 / (x - 1)) / 2
		: gnm_log ((x - 1) / (x + 1)) / -2;
}

/* ------------------------------------------------------------------------- */

#ifdef GNM_REDUCES_TRIG_RANGE

gnm_float
gnm_sin (gnm_float x)
{
	int km4;
	gnm_float xr = gnm_reduce_pi (x, 1, &km4);

	switch (km4) {
	default:
	case 0: return +sin (xr);
	case 1: return +cos (xr);
	case 2: return -sin (xr);
	case 3: return -cos (xr);
	}
}

gnm_float
gnm_cos (gnm_float x)
{
	int km4;
	gnm_float xr = gnm_reduce_pi (x, 1, &km4);

	switch (km4) {
	default:
	case 0: return +cos (xr);
	case 1: return -sin (xr);
	case 2: return -cos (xr);
	case 3: return +sin (xr);
	}
}

gnm_float
gnm_tan (gnm_float x)
{
	int km4;
	gnm_float xr = gnm_reduce_pi (x, 1, &km4);

	switch (km4) {
	default:
	case 0: case 2: return +tan (xr);
	case 1: case 3: return -cos (xr) / sin (xr);
	}
}

#endif

/* ------------------------------------------------------------------------- */
