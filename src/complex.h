#ifndef GNUMERIC_COMPLEX_H
#define GNUMERIC_COMPLEX_H

#include "numbers.h"
#include <math.h>

typedef struct {
	float_t re, im;
} complex_t;

#ifdef GNUMERIC_COMPLEX_IMPLEMENTATION

/* The actual definitions.  */
#define GNUMERIC_COMPLEX_PROTO(p) p; p
#define GNUMERIC_COMPLEX_BODY

#else

#ifdef __GNUC__

/* Have gcc -- inline functions.  */
#define GNUMERIC_COMPLEX_PROTO(p) p; extern inline p
#define GNUMERIC_COMPLEX_BODY

#else

/* No gcc -- no inline functions.  */
#define GNUMERIC_COMPLEX_INLINE(p) extern p;
#undef GNUMERIC_COMPLEX_BODY

#endif
#endif

/* ------------------------------------------------------------------------- */

char *complex_to_string (const complex_t *src, const char *reformat,
			 const char *imformat, char imunit);

int complex_from_string (complex_t *dst, const char *src, char *imunit);

void complex_sqrt (complex_t *dst, const complex_t *src);
void complex_to_polar (float_t *mod, float_t *angle, const complex_t *src);
void complex_from_polar (complex_t *dst, float_t mod, float_t angle);

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_init (complex_t *dst, float_t re, float_t im))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = re;
	dst->im = im;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_real (complex_t *dst, float_t re))
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

GNUMERIC_COMPLEX_PROTO (float_t complex_mod (const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return sqrt (src->re * src->re + src->im * src->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (float_t complex_angle (const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return atan2 (src->im, src->re);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_conj (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = src->re;
	dst->im = -src->im;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_add (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = a->re + b->re;
	dst->im = a->im + b->im;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_sub (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = a->re - b->re;
	dst->im = a->im - b->im;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_mul (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = a->re * b->re - a->im * b->im;
	dst->im = a->re * b->im + a->im * b->re;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_div (complex_t *dst, const complex_t *a, const complex_t *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	float_t modsqr;

	modsqr = b->re * b->re + b->im * b->im;
	dst->re = (a->re * b->re + a->im * b->im) / modsqr;
	dst->im = (a->im * b->re - a->re * b->im) / modsqr;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_exp (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = exp (src->re) * cos (src->im);
	dst->im = exp (src->re) * sin (src->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_ln (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = log (complex_mod (src));
	dst->im = complex_angle (src);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_sin (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = sin (src->re) * cosh (src->im);
	dst->im = cos (src->re) * sinh (src->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_cos (complex_t *dst, const complex_t *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = cos (src->re) * cosh (src->im);
	dst->im = -sin (src->re) * sinh (src->im);
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

#undef GNUMERIC_COMPLEX_PROTO
#undef GNUMERIC_COMPLEX_BODY

#endif
