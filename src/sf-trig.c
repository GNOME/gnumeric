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


static gnm_float
reduce_pi_simple (gnm_float x, int *pk, int kbits)
{
	static const gnm_float two_over_pi = GNM_const(0.63661977236758134307553505349);
	static const gnm_float pi_over_two[] = {
		+0x1.921fb5p+0,
		+0x1.110b46p-26,
		+0x1.1a6263p-54,
		+0x1.8a2e03p-81,
		+0x1.c1cd12p-107
	};
	int i;
	gnm_float k = gnm_round (x * gnm_ldexp (two_over_pi, kbits - 2));
	gnm_float xx = 0;

	g_assert (k < (1 << 26));
	*pk = (int)k;

	if (k == 0)
		return x;

	x -= k * gnm_ldexp (pi_over_two[0], 2 - kbits);
	for (i = 1; i < 5; i++) {
		gnm_float dx = k * gnm_ldexp (pi_over_two[i], 2 - kbits);
		gnm_float s = x - dx;
		xx += x - s - dx;
		x = s;
	}

	return x + xx;
}

/*
 * Add the 64-bit number p at *dst and backwards.  This would be very
 * simple and fast in assembler.  In C it's a bit of a mess.
 */
static inline void
add_at (guint32 *dst, guint64 p)
{
	unsigned h = p >> 63;

	p += *dst;
	*dst-- = p & 0xffffffffu;
	p >>= 32;
	if (p) {
		p += *dst;
		*dst-- = p & 0xffffffffu;
		if (p >> 32)
			while (++*dst == 0)
				dst--;
	} else if (h) {
		/* p overflowed, pass carry on. */
		dst--;
		while (++*dst == 0)
			dst--;
	}
}

static gnm_float
reduce_pi_full (gnm_float x, int *pk, int kbits)
{
	/* FIXME?  This table isn't big enough for long double's range */
	static const guint32 one_over_two_pi[] = {
		0x28be60dbu,
		0x9391054au,
		0x7f09d5f4u,
		0x7d4d3770u,
		0x36d8a566u,
		0x4f10e410u,
		0x7f9458eau,
		0xf7aef158u,
		0x6dc91b8eu,
		0x909374b8u,
		0x01924bbau,
		0x82746487u,
		0x3f877ac7u,
		0x2c4a69cfu,
		0xba208d7du,
		0x4baed121u,
		0x3a671c09u,
		0xad17df90u,
		0x4e64758eu,
		0x60d4ce7du,
		0x272117e2u,
		0xef7e4a0eu,
		0xc7fe25ffu,
		0xf7816603u,
		0xfbcbc462u,
		0xd6829b47u,
		0xdb4d9fb3u,
		0xc9f2c26du,
		0xd3d18fd9u,
		0xa797fa8bu,
		0x5d49eeb1u,
		0xfaf97c5eu,
		0xcf41ce7du,
		0xe294a4bau,
		0x9afed7ecu,
		0x47e35742u
	};
	static const guint32 pi_over_four[] = {
		0xc90fdaa2u,
		0x2168c234u,
		0xc4c6628bu,
		0x80dc1cd1u
	};

	gnm_float m;
	guint32 w2, w1, w0, wm1, wm2;
	int e, neg;
	unsigned di, i, j;
	guint32 r[6], r4[4];
	gnm_float rh, rl, l48, h48;

	m = gnm_frexp (x, &e);
	if (e >= GNM_MANT_DIG) {
		di = ((unsigned)e - GNM_MANT_DIG) / 32u;
		e -= di * 32;
	} else
		di = 0;
	m = gnm_ldexp (m, e - 64);
	w2  = (guint32)gnm_floor (m); m = gnm_ldexp (m - w2, 32);
	w1  = (guint32)gnm_floor (m); m = gnm_ldexp (m - w1, 32);
	w0  = (guint32)gnm_floor (m); m = gnm_ldexp (m - w0, 32);
	wm1 = (guint32)gnm_floor (m); m = gnm_ldexp (m - wm1, 32);
	wm2 = (guint32)gnm_floor (m);

	/*
	 * r[0] is an integer overflow area to be ignored.  We will not create
	 * any carry into r[-1] because 5/(2pi) < 1.
	 */
	r[0] = 0;

	for (i = 0; i < 5; i++) {
		g_assert (i + 2 + di < G_N_ELEMENTS (one_over_two_pi));
		r[i + 1] = 0;
		if (wm2 && i > 1)
			add_at (&r[i + 1], (guint64)wm2 * one_over_two_pi[i - 2]);
		if (wm1 && i > 0)
			add_at (&r[i + 1], (guint64)wm1 * one_over_two_pi[i - 1]);
		if (w0)
			add_at (&r[i + 1], (guint64)w0  * one_over_two_pi[i     + di]);
		if (w1)
			add_at (&r[i + 1], (guint64)w1  * one_over_two_pi[i + 1 + di]);
		if (w2)
			add_at (&r[i + 1], (guint64)w2  * one_over_two_pi[i + 2 + di]);

		/*
		 * We're done at i==3 unless the first 31-kbits bits, not counting
		 * those ending up in sign and *pk, are all zeros or ones.
		 */
		if (i == 3 && ((r[1] + 1u) & (0x7fffffffu >> kbits)) > 1)
			break;
	}

	*pk = kbits ? (r[1] >> (32 - kbits)) : 0;
	if ((neg = ((r[1] >> (31 - kbits)) & 1))) {
		(*pk)++;
		/* Two-complement negation */
		for (j = 1; j <= i; j++) r[j] ^= 0xffffffffu;
		add_at (&r[i], 1);
	}
	r[1] &= (0xffffffffu >> kbits);

	j = 1;
	if (r[j] == 0) j++;
	r4[0] = r4[1] = r4[2] = r4[3] = 0;
	add_at (&r4[1], (guint64)r[j    ] * pi_over_four[0]);
	add_at (&r4[2], (guint64)r[j    ] * pi_over_four[1]);
	add_at (&r4[2], (guint64)r[j + 1] * pi_over_four[0]);
	add_at (&r4[3], (guint64)r[j    ] * pi_over_four[2]);
	add_at (&r4[3], (guint64)r[j + 1] * pi_over_four[1]);
	add_at (&r4[3], (guint64)r[j + 2] * pi_over_four[0]);

	h48 = gnm_ldexp (((guint64)r4[0] << 16) | (r4[1] >> 16),
			 -32 * j + (kbits + 1) - 16);
	l48 = gnm_ldexp (((guint64)(r4[1] & 0xffff) << 32) | r4[2],
			 -32 * j + (kbits + 1) - 64);

	rh = h48 + l48;
	rl = h48 - rh + l48;

	if (neg) {
		rh = -rh;
		rl = -rl;
	}

	return gnm_ldexp (rh + rl, 2 - kbits);
}

/**
  * gnm_reduce_pi:
  * @x: number of reduce
  * @e: scale between -1 and 8, inclusive.
  * @k: (out): location to return lower @e+1 bits of reduction count
  *
  * This function performs range reduction for trigonometric functions.
  *
  * Returns: a value, xr, such that x = xr + j * Pi/2^@e for some integer
  * number j and |xr| <=  Pi/2^(@e+1).  The lower @e+1 bits of j will be
  * returned in @k.
  */
gnm_float
gnm_reduce_pi (gnm_float x, int e, int *k)
{
	gnm_float xr;
	void *state;

	g_return_val_if_fail (e >= -1 && e <= 8, x);
	g_return_val_if_fail (k != NULL, x);

	if (!gnm_finite (x)) {
		*k = 0;
		return x * gnm_nan;
	}

	/*
	 * We aren't actually using quads, but we rely somewhat on
	 * proper ieee double semantics.
	 */
	state = gnm_quad_start ();

	if (gnm_abs (x) < (1u << (27 - e)))
		xr = reduce_pi_simple (gnm_abs (x), k, e + 1);
	else
		xr = reduce_pi_full (gnm_abs (x), k, e + 1);

	if (x < 0) {
		xr = -xr;
		*k = -*k;
	}
	*k &= ((1 << (e + 1)) - 1);

	gnm_quad_end (state);

	return xr;
}




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
