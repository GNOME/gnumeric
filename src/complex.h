/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COMPLEX_H_
# define _GNM_COMPLEX_H_

#include "numbers.h"
#include <goffice/goffice.h>
#include <math.h>

G_BEGIN_DECLS

#ifdef GNM_WITH_LONG_DOUBLE
#define complex_t GOComplexl
#define complex_init go_complex_initl
#define complex_add go_complex_addl
#define complex_sub go_complex_subl
#define complex_mul go_complex_mull
#define complex_div go_complex_divl
#define complex_mod go_complex_modl
#define complex_angle go_complex_anglel
#define complex_real go_complex_reall
#define complex_real_p go_complex_real_pl
#define complex_zero_p go_complex_zero_pl
#define complex_conj go_complex_conjl
#define complex_exp go_complex_expl
#define complex_ln go_complex_lnl
#define complex_sqrt go_complex_sqrtl
#define complex_sin go_complex_sinl
#define complex_cos go_complex_cosl
#define complex_tan go_complex_tanl
#define complex_pow go_complex_powl
#define complex_scale_real go_complex_scale_reall
#define complex_to_polar go_complex_to_polarl
#define complex_from_polar go_complex_from_polarl
#else
#define complex_t GOComplex
#define complex_init go_complex_init
#define complex_add go_complex_add
#define complex_sub go_complex_sub
#define complex_mul go_complex_mul
#define complex_div go_complex_div
#define complex_mod go_complex_mod
#define complex_angle go_complex_angle
#define complex_real go_complex_real
#define complex_real_p go_complex_real_p
#define complex_zero_p go_complex_zero_p
#define complex_conj go_complex_conj
#define complex_exp go_complex_exp
#define complex_ln go_complex_ln
#define complex_sqrt go_complex_sqrt
#define complex_sin go_complex_sin
#define complex_cos go_complex_cos
#define complex_tan go_complex_tan
#define complex_pow go_complex_pow
#define complex_scale_real go_complex_scale_real
#define complex_to_polar go_complex_to_polar
#define complex_from_polar go_complex_from_polar
#endif

/* ------------------------------------------------------------------------- */

char *complex_to_string (complex_t const *src, char imunit);

int complex_from_string (complex_t *dst, char const *src, char *imunit);

int complex_invalid_p (complex_t const *src);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif /* _GNM_COMPLEX_H_ */
