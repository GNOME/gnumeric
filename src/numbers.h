/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_NUMBERS_H_
# define _GNM_NUMBERS_H_

#include <gnumeric-features.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

/*
 * WARNING: Any preprocessor conditionals in here must also be placed
 * in gnumeric-features.h.in
 */

#ifdef GNM_SUPPLIES_LGAMMA
GO_VAR_DECL int signgam;
double lgamma (double x);
#endif

#ifdef GNM_SUPPLIES_LGAMMA_R
double lgamma_r (double x, int *signp);
#endif

#ifdef GNM_WITH_LONG_DOUBLE

#ifdef GNM_SUPPLIES_ERFL
long double erfl (long double x);
#endif

#ifdef GNM_SUPPLIES_ERFCL
long double erfl (long double x);
#endif

typedef long double gnm_float;

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
#define gnm_cosh coshl
#define gnm_cospi go_cospil
#define gnm_erf erfl
#define gnm_erfc erfcl
#define gnm_exp expl
#define gnm_expm1 expm1l
#define gnm_fake_ceil go_fake_ceill
#define gnm_fake_floor go_fake_floorl
#define gnm_fake_round go_fake_roundl
#define gnm_fake_trunc go_fake_truncl
#define gnm_finite finitel
#define gnm_floor floorl
#define gnm_fmod fmodl
#define gnm_format_value go_format_valuel
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
#define gnm_log2 log2l
#define gnm_modf modfl
#define gnm_nan go_nanl
#define gnm_ninf go_ninfl
#define gnm_pinf go_pinfl
#define gnm_pow powl
#define gnm_pow10 go_pow10l
#define gnm_pow2 go_pow2l
#define gnm_render_general go_render_generall
#define gnm_sinh sinhl
#define gnm_sinpi go_sinpil
#define gnm_sqrt sqrtl
#define gnm_strto go_strtold
#define gnm_sub_epsilon go_sub_epsilonl
#define gnm_tanh tanhl
#define gnm_tanpi go_tanpil
#ifndef GNM_REDUCES_TRIG_RANGE
#define gnm_sin sinl
#define gnm_cos cosl
#define gnm_tan tanl
#endif

#define GNM_FORMAT_e	"Le"
#define GNM_FORMAT_E	"LE"
#define GNM_FORMAT_f	"Lf"
#define GNM_FORMAT_g	"Lg"
#define GNM_FORMAT_G	"LG"
#define GNM_SCANF_g	"Lg"
#define GNM_DIG		LDBL_DIG
#define GNM_MANT_DIG	LDBL_MANT_DIG
#define GNM_MIN_EXP	LDBL_MIN_EXP
#define GNM_MAX_EXP	LDBL_MAX_EXP
#define GNM_MIN		LDBL_MIN
#define GNM_MAX		LDBL_MAX
#define GNM_EPSILON	LDBL_EPSILON
#define GNM_const(_c)	_c ## L

#define GnmQuad GOQuadl
#define gnm_quad_acos go_quad_acosl
#define gnm_quad_add go_quad_addl
#define gnm_quad_asin go_quad_asinl
#define gnm_quad_cos go_quad_cosl
#define gnm_quad_cospi go_quad_cospil
#define gnm_quad_div go_quad_divl
#define gnm_quad_e go_quad_el
#define gnm_quad_end go_quad_endl
#define gnm_quad_exp go_quad_expl
#define gnm_quad_expm1 go_quad_expm1l
#define gnm_quad_init go_quad_initl
#define gnm_quad_ln2 go_quad_ln2l
#define gnm_quad_log go_quad_logl
#define gnm_quad_mul go_quad_mull
#define gnm_quad_mul12 go_quad_mul12l
#define gnm_quad_one go_quad_onel
#define gnm_quad_pi go_quad_pil
#define gnm_quad_2pi go_quad_2pil
#define gnm_quad_pow go_quad_powl
#define gnm_quad_sin go_quad_sinl
#define gnm_quad_sinpi go_quad_sinpil
#define gnm_quad_sqrt go_quad_sqrtl
#define gnm_quad_sqrt2 go_quad_sqrt2l
#define gnm_quad_start go_quad_startl
#define gnm_quad_sub go_quad_subl
#define gnm_quad_value go_quad_valuel
#define gnm_quad_zero go_quad_zerol
#define GnmAccumulator GOAccumulatorl
#define gnm_accumulator_start go_accumulator_startl
#define gnm_accumulator_end go_accumulator_endl
#define gnm_accumulator_new go_accumulator_newl
#define gnm_accumulator_free go_accumulator_freel
#define gnm_accumulator_add go_accumulator_addl
#define gnm_accumulator_add_quad go_accumulator_add_quadl
#define gnm_accumulator_value go_accumulator_valuel

#else /* !GNM_WITH_LONG_DOUBLE */

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
#define gnm_cosh cosh
#define gnm_cospi go_cospi
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
#define gnm_format_value go_format_value
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
#define gnm_log2 log2
#define gnm_modf modf
#define gnm_nan go_nan
#define gnm_ninf go_ninf
#define gnm_pinf go_pinf
#define gnm_pow pow
#define gnm_pow10 go_pow10
#define gnm_pow2 go_pow2
#define gnm_render_general go_render_general
#define gnm_sinh sinh
#define gnm_sinpi go_sinpi
#define gnm_sqrt sqrt
#define gnm_strto go_strtod
#define gnm_sub_epsilon go_sub_epsilon
#define gnm_tanh tanh
#define gnm_tanpi go_tanpi
#ifndef GNM_REDUCES_TRIG_RANGE
#define gnm_sin sin
#define gnm_cos cos
#define gnm_tan tan
#endif

#define GNM_FORMAT_e	"e"
#define GNM_FORMAT_E	"E"
#define GNM_FORMAT_f	"f"
#define GNM_FORMAT_g	"g"
#define GNM_FORMAT_G	"G"
#define GNM_SCANF_g	"lg"
#define GNM_DIG		DBL_DIG
#define GNM_MANT_DIG	DBL_MANT_DIG
#define GNM_MIN_EXP	DBL_MIN_EXP
#define GNM_MAX_EXP	DBL_MAX_EXP
#define GNM_MIN		DBL_MIN
#define GNM_MAX		DBL_MAX
#define GNM_EPSILON	DBL_EPSILON
#define GNM_const(_c)	_c

#define GnmQuad GOQuad
#define gnm_quad_acos go_quad_acos
#define gnm_quad_add go_quad_add
#define gnm_quad_asin go_quad_asin
#define gnm_quad_cos go_quad_cos
#define gnm_quad_cospi go_quad_cospi
#define gnm_quad_div go_quad_div
#define gnm_quad_e go_quad_e
#define gnm_quad_end go_quad_end
#define gnm_quad_exp go_quad_exp
#define gnm_quad_expm1 go_quad_expm1
#define gnm_quad_init go_quad_init
#define gnm_quad_ln2 go_quad_ln2
#define gnm_quad_log go_quad_log
#define gnm_quad_mul go_quad_mul
#define gnm_quad_mul12 go_quad_mul12
#define gnm_quad_one go_quad_one
#define gnm_quad_pi go_quad_pi
#define gnm_quad_2pi go_quad_2pi
#define gnm_quad_pow go_quad_pow
#define gnm_quad_sin go_quad_sin
#define gnm_quad_sinpi go_quad_sinpi
#define gnm_quad_sqrt go_quad_sqrt
#define gnm_quad_sqrt2 go_quad_sqrt2
#define gnm_quad_start go_quad_start
#define gnm_quad_sub go_quad_sub
#define gnm_quad_value go_quad_value
#define gnm_quad_zero go_quad_zero
#define GnmAccumulator GOAccumulator
#define gnm_accumulator_start go_accumulator_start
#define gnm_accumulator_end go_accumulator_end
#define gnm_accumulator_new go_accumulator_new
#define gnm_accumulator_free go_accumulator_free
#define gnm_accumulator_add go_accumulator_add
#define gnm_accumulator_add_quad go_accumulator_add_quad
#define gnm_accumulator_value go_accumulator_value

#endif

#ifdef GNM_REDUCES_TRIG_RANGE
gnm_float gnm_sin (gnm_float x);
gnm_float gnm_cos (gnm_float x);
gnm_float gnm_tan (gnm_float x);
#endif


G_END_DECLS

#endif /* _GNM_NUMBERS_H_ */
