/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COMPLEX_H_
# define _GNM_COMPLEX_H_

#include "numbers.h"
#include <math.h>

G_BEGIN_DECLS

typedef struct {
	gnm_float re, im;
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

char *complex_to_string (complex_t const *src, char const *reformat,
			 char const *imformat, char imunit);

int complex_from_string (complex_t *dst, char const *src, char *imunit);

void complex_to_polar (gnm_float *mod, gnm_float *angle, complex_t const *src);
void complex_from_polar (complex_t *dst, gnm_float mod, gnm_float angle);
void complex_mul  (complex_t *dst, complex_t const *a, complex_t const *b);
void complex_div  (complex_t *dst, complex_t const *a, complex_t const *b);
void complex_pow  (complex_t *dst, complex_t const *a, complex_t const *b);
void complex_sqrt (complex_t *dst, complex_t const *src);

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_init (complex_t *dst, gnm_float re, gnm_float im))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = re;
	dst->im = im;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_real (complex_t *dst, gnm_float re))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re = re;
	dst->im = 0;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (int complex_real_p (complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return src->im == 0;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (int complex_zero_p (complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return src->re == 0 && src->im == 0;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (gnm_float complex_mod (complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return gnm_hypot (src->re, src->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (gnm_float complex_angle (complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	return gnm_atan2 (src->im, src->re);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_conj (complex_t *dst, complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst, src->re, -src->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_scale_real (complex_t *dst, gnm_float f))
#ifdef GNUMERIC_COMPLEX_BODY
{
	dst->re *= f;
	dst->im *= f;
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_add (complex_t *dst, complex_t const *a, complex_t const *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst, a->re + b->re, a->im + b->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_sub (complex_t *dst, complex_t const *a, complex_t const *b))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst, a->re - b->re, a->im - b->im);
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_exp (complex_t *dst, complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      gnm_exp (src->re) * gnm_cos (src->im),
		      gnm_exp (src->re) * gnm_sin (src->im));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_ln (complex_t *dst, complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      gnm_log (complex_mod (src)),
		      complex_angle (src));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_sin (complex_t *dst, complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      gnm_sin (src->re) * gnm_cosh (src->im),
		      gnm_cos (src->re) * gnm_sinh (src->im));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_cos (complex_t *dst, complex_t const *src))
#ifdef GNUMERIC_COMPLEX_BODY
{
	complex_init (dst,
		      gnm_cos (src->re) * gnm_cosh (src->im),
		      -gnm_sin (src->re) * gnm_sinh (src->im));
}
#endif

/* ------------------------------------------------------------------------- */

GNUMERIC_COMPLEX_PROTO (void complex_tan (complex_t *dst, complex_t const *src))
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

G_END_DECLS

#endif /* _GNM_COMPLEX_H_ */
