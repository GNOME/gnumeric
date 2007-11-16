/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_NUMBERS_H_
# define _GNM_NUMBERS_H_

#include <gnumeric-features.h>
#include <goffice/math/go-math.h>

G_BEGIN_DECLS

/* WARNING : Any preprocessor conditionals in here must also be placed in libspreadsheet-config.h */
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif
#ifdef HAVE_IEEE754_H
#include <ieee754.h>
#endif

#ifndef HAVE_LGAMMA
/* Defined in mathfunc.c  */
GO_VAR_DECL int signgam;
double lgamma (double x);
#endif
#ifndef HAVE_LGAMMA_R
/* Defined in mathfunc.c  */
double lgamma_r (double x, int *signp);
#endif

#ifdef WITH_LONG_DOUBLE

#ifdef HAVE_SUNMATH_H
#include <sunmath.h>
#endif

typedef long double gnm_float;

#ifdef HAVE_ERFL
#define gnm_erf erfl
#else
#define NEED_FAKE_ERFGNUM
/* Defined in mathfunc.c  */
gnm_float gnm_erf (gnm_float x);
#endif

#ifdef HAVE_ERFCL
#define gnm_erfc erfcl
#else
#define NEED_FAKE_ERFCGNUM
/* Defined in mathfunc.c  */
gnm_float gnm_erfc (gnm_float x);
#endif

#ifdef HAVE_YNL
#define gnm_yn ynl
#else
#define NEED_FAKE_YNGNUM
/* Defined in mathfunc.c  */
gnm_float gnm_yn (int n, gnm_float x);
#endif

#define gnm_abs fabsl
#define gnm_acos acosl
#define gnm_acosh acoshl
#define gnm_add_epsilon go_add_epsilonl
#define gnm_asin asinl
#define gnm_asinh asinhl
#define gnm_atan atanl
#define gnm_atan2 atan2l
#define gnm_atanh atanhl
#define gnm_ceil ceill
#define gnm_cos cosl
#define gnm_cosh coshl
#define gnm_exp expl
#define gnm_expm1 expm1l
#define gnm_fake_ceil go_fake_ceill
#define gnm_fake_floor go_fake_floorl
#define gnm_fake_round go_fake_roundl
#define gnm_fake_trunc go_fake_truncl
#define gnm_finite finitel
#define gnm_floor floorl
#define gnm_fmod fmodl
#define gnm_format_number go_format_numberl
#define gnm_format_value_gstring go_format_value_gstringl
#define gnm_frexp frexpl
#define gnm_hypot hypotl
#define gnm_isnan isnanl
#define gnm_ldexp ldexpl
#define gnm_lgamma lgammal
#define gnm_lgamma_r lgammal_r
#define gnm_log logl
#define gnm_log10 log10l
#define gnm_log1p log1pl
#define gnm_modf modfl
#define gnm_nan go_nanl
#define gnm_ninf go_ninfl
#define gnm_pinf go_pinfl
#define gnm_pow powl
#define gnm_pow10 go_pow10l
#define gnm_pow2 go_pow2l
#define gnm_render_general go_render_generall
#define gnm_sin sinl
#define gnm_sinh sinhl
#define gnm_sqrt sqrtl
#define gnm_strto go_strtold
#define gnm_sub_epsilon go_sub_epsilonl
#define gnm_tan tanl
#define gnm_tanh tanhl

#define GNM_FORMAT_e	"Le"
#define GNM_FORMAT_E	"LE"
#define GNM_FORMAT_f	"Lf"
#define GNM_FORMAT_g	"Lg"
#define GNM_FORMAT_G	"LG"
#define GNM_DIG		LDBL_DIG
#define GNM_MANT_DIG	LDBL_MANT_DIG
#define GNM_MIN_EXP	LDBL_MIN_EXP
#define GNM_MAX_EXP	LDBL_MAX_EXP
#define GNM_MIN		LDBL_MIN
#define GNM_MAX		LDBL_MAX
#define GNM_EPSILON	LDBL_EPSILON
#define GNM_const(_c)	_c ## L

#else /* !WITH_LONG_DOUBLE */

typedef double gnm_float;

#define gnm_abs fabs
#define gnm_acos acos
#define gnm_acosh acosh
#define gnm_add_epsilon go_add_epsilon
#define gnm_asin asin
#define gnm_asinh asinh
#define gnm_atan atan
#define gnm_atan2 atan2
#define gnm_atanh atanh
#define gnm_ceil ceil
#define gnm_cos cos
#define gnm_cosh cosh
#define gnm_erf erf
#define gnm_erfc erfc
#define gnm_exp exp
#define gnm_expm1 expm1
#define gnm_fake_ceil go_fake_ceil
#define gnm_fake_floor go_fake_floor
#define gnm_fake_round go_fake_round
#define gnm_fake_trunc go_fake_trunc
#define gnm_finite go_finite
#define gnm_floor floor
#define gnm_fmod fmod
#define gnm_format_number go_format_number
#define gnm_format_value_gstring go_format_value_gstring
#define gnm_frexp frexp
#define gnm_hypot hypot
#define gnm_isnan isnan
#define gnm_ldexp ldexp
#define gnm_lgamma lgamma
#define gnm_lgamma_r lgamma_r
#define gnm_log log
#define gnm_log10 log10
#define gnm_log1p log1p
#define gnm_modf modf
#define gnm_nan go_nan
#define gnm_ninf go_ninf
#define gnm_pinf go_pinf
#define gnm_pow pow
#define gnm_pow10 go_pow10
#define gnm_pow2 go_pow2
#define gnm_render_general go_render_general
#define gnm_sin sin
#define gnm_sinh sinh
#define gnm_sqrt sqrt
#define gnm_strto go_strtod
#define gnm_sub_epsilon go_sub_epsilon
#define gnm_tan tan
#define gnm_tanh tanh
#define gnm_yn yn

#define GNM_FORMAT_e	"e"
#define GNM_FORMAT_E	"E"
#define GNM_FORMAT_f	"f"
#define GNM_FORMAT_g	"g"
#define GNM_FORMAT_G	"G"
#define GNM_DIG		DBL_DIG
#define GNM_MANT_DIG	DBL_MANT_DIG
#define GNM_MIN_EXP	DBL_MIN_EXP
#define GNM_MAX_EXP	DBL_MAX_EXP
#define GNM_MIN		DBL_MIN
#define GNM_MAX		DBL_MAX
#define GNM_EPSILON	DBL_EPSILON
#define GNM_const(_c)	_c

#endif

G_END_DECLS

#endif /* _GNM_NUMBERS_H_ */
