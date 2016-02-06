/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COMPLEX_H_
# define _GNM_COMPLEX_H_

#include "numbers.h"
#include <goffice/goffice.h>
#include <math.h>

G_BEGIN_DECLS

#ifdef GNM_WITH_LONG_DOUBLE
#define gnm_complex GOComplexl
#define gnm_complex_init go_complex_initl
#define gnm_complex_add go_complex_addl
#define gnm_complex_sub go_complex_subl
#define gnm_complex_mul go_complex_mull
#define gnm_complex_div go_complex_divl
#define gnm_complex_mod go_complex_modl
#define gnm_complex_angle go_complex_anglel
#define gnm_complex_angle_pi go_complex_angle_pil
#define gnm_complex_real go_complex_reall
#define gnm_complex_real_p go_complex_real_pl
#define gnm_complex_zero_p go_complex_zero_pl
#define gnm_complex_conj go_complex_conjl
#define gnm_complex_exp go_complex_expl
#define gnm_complex_ln go_complex_lnl
#define gnm_complex_sqrt go_complex_sqrtl
#define gnm_complex_sin go_complex_sinl
#define gnm_complex_cos go_complex_cosl
#define gnm_complex_tan go_complex_tanl
#define gnm_complex_pow go_complex_powl
#define gnm_complex_scale_real go_complex_scale_reall
#define gnm_complex_to_polar go_complex_to_polarl
#define gnm_complex_from_polar go_complex_from_polarl
#define gnm_complex_from_polar_pi go_complex_from_polar_pil
#else
#define gnm_complex GOComplex
#define gnm_complex_init go_complex_init
#define gnm_complex_add go_complex_add
#define gnm_complex_sub go_complex_sub
#define gnm_complex_mul go_complex_mul
#define gnm_complex_div go_complex_div
#define gnm_complex_mod go_complex_mod
#define gnm_complex_angle go_complex_angle
#define gnm_complex_angle_pi go_complex_angle_pi
#define gnm_complex_real go_complex_real
#define gnm_complex_real_p go_complex_real_p
#define gnm_complex_zero_p go_complex_zero_p
#define gnm_complex_conj go_complex_conj
#define gnm_complex_exp go_complex_exp
#define gnm_complex_ln go_complex_ln
#define gnm_complex_sqrt go_complex_sqrt
#define gnm_complex_sin go_complex_sin
#define gnm_complex_cos go_complex_cos
#define gnm_complex_tan go_complex_tan
#define gnm_complex_pow go_complex_pow
#define gnm_complex_scale_real go_complex_scale_real
#define gnm_complex_to_polar go_complex_to_polar
#define gnm_complex_from_polar go_complex_from_polar
#define gnm_complex_from_polar_pi go_complex_from_polar_pi
#endif

/* ------------------------------------------------------------------------- */

char *gnm_complex_to_string (gnm_complex const *src, char imunit);

int gnm_complex_from_string (gnm_complex *dst, char const *src, char *imunit);

int gnm_complex_invalid_p (gnm_complex const *src);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif /* _GNM_COMPLEX_H_ */
