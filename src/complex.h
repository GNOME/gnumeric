#ifndef GNUMERIC_COMPLEX_H
#define GNUMERIC_COMPLEX_H

#include "numbers.h"
#include <math.h>

typedef struct {
	gnum_float re, im;
} complex_t;

#ifdef GNUMERIC_COMPLEX_IMPLEMENTATION

/* The actual definitions.  */
#define GNUMERIC_COMPLEX_PROTO(p) p; p
#define GNUMERIC_COMPLEX_BODY

#else

#ifdef __GNUC__

/* Have gcc -- inline functions.  */
#define GNUMERIC_COMPLEX_PROTO(p) p; extern __inline__ p
#define GNUMERIC_COMPLEX_BODY

#else

/* No gcc -- no inline functions.  */
#define GNUMERIC_COMPLEX_PROTO(p) extern p;
#undef GNUMERIC_COMPLEX_BODY

#endif
#endif

/* ------------------------------------------------------------------------- */

char *complex_to_string (const complex_t *src, const char *reformat,
			 const char *imformat, char imunit);

int complex_from_string (complex_t *dst, const char *src, char *imunit);

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_init (complex_t *dst, gnum_float re, gnum_float im))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = re;
	dst->im = im;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_real (complex_t *dst, gnum_float re))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = re;
	dst->im = 0;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (int complex_real_p (const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return src->im == 0;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (int complex_zero_p (const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return src->re == 0 && src->im == 0;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (gnum_float complex_mod (const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return hypot (src->re, src->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (gnum_float complex_angle (const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return atan2 (src->im, src->re);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_to_polar (gnum_float *mod, gnum_float *angle, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	*mod = complex_mod (src);
	*angle = complex_angle (src);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_from_polar (complex_t *dst, gnum_float mod, gnum_float angle))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst, mod * cos (angle), mod * sin (angle));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_conj (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst, src->re, -src->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_add (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst, a->re + b->re, a->im + b->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_sub (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst, a->re - b->re, a->im - b->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_mul (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      a->re * b->re - a->im * b->im,
		      a->re * b->im + a->im * b->re);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_div (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	gnum_float modsqr;

	modsqr = b->re * b->re + b->im * b->im;
	complex_init (dst,
		      (a->re * b->re + a->im * b->im) / modsqr,
		      (a->im * b->re - a->re * b->im) / modsqr);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_exp (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      exp (src->re) * cos (src->im),
		      exp (src->re) * sin (src->im));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_ln (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      log (complex_mod (src)),
		      complex_angle (src));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_pow (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_t lna, b_lna;

	/* ln is not defined for reals less than or equal to zero.  */
	if (complex_real_p (a) && complex_real_p (b))
		complex_init (dst, pow (a->re, b->re), 0);
	else {
		complex_ln (&lna, a);
		complex_mul (&b_lna, b, &lna);
		complex_exp (dst, &b_lna);
	}
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_sin (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      sin (src->re) * cosh (src->im),
		      cos (src->re) * sinh (src->im));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_cos (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      cos (src->re) * cosh (src->im),
		      -sin (src->re) * sinh (src->im));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_tan (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_t s, c;

	complex_sin (&s, src);
	complex_cos (&c, src);
	complex_div (dst, &s, &c);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_sqrt (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_from_polar (dst,
			    sqrt (complex_mod (src)),
			    complex_angle (src) / 2);
}
#endif

/* ------------------------------------------------------------------------- */

#undef GNUMERIC_COMPLEX_PROTO
#undef GNUMERIC_COMPLEX_BODY

#endif
