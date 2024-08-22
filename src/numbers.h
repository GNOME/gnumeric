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

#if defined(GNM_SUPPLIES_LGAMMA_R) || defined(NEEDS_LGAMMA_R_PROTOTYPE)
double lgamma_r (double x, int *signp);
#endif


#ifdef GNM_WITH_LONG_DOUBLE

#ifdef GNM_SUPPLIES_ERFL
long double erfl (long double x);
#endif

#ifdef GNM_SUPPLIES_ERFCL
long double erfcl (long double x);
#endif

typedef long double gnm_float;

#define GNM_FORMAT_e	"Le"
#define GNM_FORMAT_E	"LE"
#define GNM_FORMAT_f	"Lf"
#define GNM_FORMAT_g	"Lg"
#define GNM_FORMAT_G	"LG"
#define GNM_SCANF_g	"Lg"
#define gnm_sscanf      sscanf
#define GNM_DIG		LDBL_DIG
#define GNM_MANT_DIG	LDBL_MANT_DIG
#define GNM_MIN_EXP	LDBL_MIN_EXP
#define GNM_MAX_EXP	LDBL_MAX_EXP
#define GNM_MIN		LDBL_MIN
#define GNM_MAX		LDBL_MAX
#define GNM_EPSILON	LDBL_EPSILON
#define GNM_const(_c)	_c ## L
#define GNM_SUFFIX(x) x ## l
#define GNM_RADIX       FLT_RADIX
#define GNM_MAX_10_EXP	LDBL_MAX_10_EXP
#define GNM_MIN_10_EXP	LDBL_MIN_10_EXP
#define GNM_MIN_DEN_10_EXP	-4950

#define gnm_lgamma_r lgammal_r
#define gnm_strto go_strtold
#define gnm_ascii_strto go_ascii_strtold
#define gnm_xml_out_add_gnm_float go_xml_out_add_long_double
#define gnm_unscalbn frexpl

#elif GNM_WITH_DECIMAL64

typedef _Decimal64 gnm_float;

#define GNM_FORMAT_e	"We"
#define GNM_FORMAT_E	"WE"
#define GNM_FORMAT_f	"Wf"
#define GNM_FORMAT_g	"Wg"
#define GNM_FORMAT_G	"WG"
#define GNM_SCANF_g	"Wg"
#undef gnm_sscanf       // No support for _Decimal64 in libc.  Defined in gutils.c
#define GNM_SUPPLIES_GNM_SSCANF
#define GNM_DIG		DECIMAL64_DIG
#define GNM_MANT_DIG	DECIMAL64_MANT_DIG
#define GNM_MIN_EXP	DECIMAL64_MIN_EXP
#define GNM_MAX_EXP	DECIMAL64_MAX_EXP
#define GNM_MIN		DECIMAL64_MIN
#define GNM_MAX		DECIMAL64_MAX
#define GNM_EPSILON	DECIMAL64_EPSILON
#define GNM_const(_c)	_c ## dd
#define GNM_SUFFIX(x) x ## D
#define GNM_RADIX       10
#define GNM_MAX_10_EXP	DECIMAL64_MAX_EXP
#define GNM_MIN_10_EXP	DECIMAL64_MIN_EXP
#define GNM_MIN_DEN_10_EXP	-398

#define gnm_lgamma_r lgammaD_r
#define gnm_strto go_strtoDd
#define gnm_ascii_strto go_ascii_strtoDd
#define gnm_xml_out_add_gnm_float go_xml_out_add_decimal64
#define gnm_unscalbn unscalbnD

#else

typedef double gnm_float;

#define GNM_FORMAT_e	"e"
#define GNM_FORMAT_E	"E"
#define GNM_FORMAT_f	"f"
#define GNM_FORMAT_g	"g"
#define GNM_FORMAT_G	"G"
#define GNM_SCANF_g	"lg"
#define gnm_sscanf      sscanf
#define GNM_DIG		DBL_DIG
#define GNM_MANT_DIG	DBL_MANT_DIG
#define GNM_MIN_EXP	DBL_MIN_EXP
#define GNM_MAX_EXP	DBL_MAX_EXP
#define GNM_MIN		DBL_MIN
#define GNM_MAX		DBL_MAX
#define GNM_EPSILON	DBL_EPSILON
#define GNM_const(_c)	_c
#define GNM_SUFFIX(x) x
#define GNM_RADIX       FLT_RADIX
#define GNM_MAX_10_EXP	DBL_MAX_10_EXP
#define GNM_MIN_10_EXP	DBL_MIN_10_EXP
#define GNM_MIN_DEN_10_EXP	-323

#define gnm_lgamma_r lgamma_r
#define gnm_strto go_strtod
#define gnm_ascii_strto go_ascii_strtod
#define gnm_xml_out_add_gnm_float go_xml_out_add_double
#define gnm_unscalbn frexp

#endif


#define gnm_abs GNM_SUFFIX(fabs)
#define gnm_acos GNM_SUFFIX(acos)
#define gnm_acosh GNM_SUFFIX(acosh)
#define gnm_add_epsilon GNM_SUFFIX(go_add_epsilon)
#define gnm_asin GNM_SUFFIX(asin)
#define gnm_asinh GNM_SUFFIX(asinh)
#define gnm_atan GNM_SUFFIX(atan)
#define gnm_atan2 GNM_SUFFIX(atan2)
#define gnm_atanpi GNM_SUFFIX(go_atanpi)
#define gnm_atan2pi GNM_SUFFIX(go_atan2pi)
#define gnm_atanh GNM_SUFFIX(atanh)
#define gnm_cbrt GNM_SUFFIX(cbrt)
#define gnm_ceil GNM_SUFFIX(ceil)
#define gnm_cosh GNM_SUFFIX(cosh)
#define gnm_cospi GNM_SUFFIX(go_cospi)
#define gnm_cotpi GNM_SUFFIX(go_cotpi)
#define gnm_erf GNM_SUFFIX(erf)
#define gnm_erfc GNM_SUFFIX(erfc)
#define gnm_exp GNM_SUFFIX(exp)
#define gnm_expm1 GNM_SUFFIX(expm1)
#define gnm_fake_ceil GNM_SUFFIX(go_fake_ceil)
#define gnm_fake_floor GNM_SUFFIX(go_fake_floor)
#define gnm_fake_round GNM_SUFFIX(go_fake_round)
#define gnm_fake_trunc GNM_SUFFIX(go_fake_trunc)
#define gnm_finite GNM_SUFFIX(go_finite)
#define gnm_floor GNM_SUFFIX(floor)
#define gnm_fmod GNM_SUFFIX(fmod)
#define gnm_format_value GNM_SUFFIX(go_format_value)
#define gnm_format_value_gstring GNM_SUFFIX(go_format_value_gstring)
#define gnm_frexp GNM_SUFFIX(frexp)
#define gnm_hypot GNM_SUFFIX(hypot)
#define gnm_isnan GNM_SUFFIX(isnan)
#define gnm_jn GNM_SUFFIX(jn)
#define gnm_ldexp GNM_SUFFIX(ldexp)
#define gnm_lgamma GNM_SUFFIX(lgamma)
#define gnm_log GNM_SUFFIX(log)
#define gnm_log10 GNM_SUFFIX(log10)
#define gnm_log1p GNM_SUFFIX(log1p)
#define gnm_log2 GNM_SUFFIX(log2)
#define gnm_modf GNM_SUFFIX(modf)
#define gnm_nan GNM_SUFFIX(go_nan)
#define gnm_nextafter GNM_SUFFIX(nextafter)
#define gnm_ninf GNM_SUFFIX(go_ninf)
#define gnm_pinf GNM_SUFFIX(go_pinf)
#define gnm_pow GNM_SUFFIX(go_pow)
#define gnm_pow10 GNM_SUFFIX(go_pow10)
#define gnm_pow2 GNM_SUFFIX(go_pow2)
#define gnm_render_general GNM_SUFFIX(go_render_general)
#define gnm_round GNM_SUFFIX(round)
#define gnm_scalbn GNM_SUFFIX(scalbn)
#define gnm_sinh GNM_SUFFIX(sinh)
#define gnm_sinpi GNM_SUFFIX(go_sinpi)
#define gnm_sqrt GNM_SUFFIX(sqrt)
#define gnm_sub_epsilon GNM_SUFFIX(go_sub_epsilon)
#define gnm_tanh GNM_SUFFIX(tanh)
#define gnm_tanpi GNM_SUFFIX(go_tanpi)
#ifndef GNM_REDUCES_TRIG_RANGE
#define gnm_sin GNM_SUFFIX(sin)
#define gnm_cos GNM_SUFFIX(cos)
#define gnm_tan GNM_SUFFIX(tan)
#endif
#define gnm_trunc GNM_SUFFIX(trunc)
#define gnm_yn GNM_SUFFIX(yn)

#define GnmQuad GNM_SUFFIX(GOQuad)
#define gnm_quad_acos GNM_SUFFIX(go_quad_acos)
#define gnm_quad_add GNM_SUFFIX(go_quad_add)
#define gnm_quad_asin GNM_SUFFIX(go_quad_asin)
#define gnm_quad_cos GNM_SUFFIX(go_quad_cos)
#define gnm_quad_cospi GNM_SUFFIX(go_quad_cospi)
#define gnm_quad_div GNM_SUFFIX(go_quad_div)
#define gnm_quad_e GNM_SUFFIX(go_quad_e)
#define gnm_quad_end GNM_SUFFIX(go_quad_end)
#define gnm_quad_exp GNM_SUFFIX(go_quad_exp)
#define gnm_quad_expm1 GNM_SUFFIX(go_quad_expm1)
#define gnm_quad_floor GNM_SUFFIX(go_quad_floor)
#define gnm_quad_half GNM_SUFFIX(go_quad_half)
#define gnm_quad_init GNM_SUFFIX(go_quad_init)
#define gnm_quad_ln2 GNM_SUFFIX(go_quad_ln2)
#define gnm_quad_ln10 GNM_SUFFIX(go_quad_ln10)
#define gnm_quad_log GNM_SUFFIX(go_quad_log)
#define gnm_quad_negate GNM_SUFFIX(go_quad_negate)
#define gnm_quad_mul GNM_SUFFIX(go_quad_mul)
#define gnm_quad_mul12 GNM_SUFFIX(go_quad_mul12)
#define gnm_quad_one GNM_SUFFIX(go_quad_one)
#define gnm_quad_pi GNM_SUFFIX(go_quad_pi)
#define gnm_quad_2pi GNM_SUFFIX(go_quad_2pi)
#define gnm_quad_pow GNM_SUFFIX(go_quad_pow)
#define gnm_quad_scalbn GNM_SUFFIX(go_quad_scalbn)
#define gnm_quad_sin GNM_SUFFIX(go_quad_sin)
#define gnm_quad_sinpi GNM_SUFFIX(go_quad_sinpi)
#define gnm_quad_sqrt GNM_SUFFIX(go_quad_sqrt)
#define gnm_quad_sqrt2 GNM_SUFFIX(go_quad_sqrt2)
#define gnm_quad_start GNM_SUFFIX(go_quad_start)
#define gnm_quad_sub GNM_SUFFIX(go_quad_sub)
#define gnm_quad_value GNM_SUFFIX(go_quad_value)
#define gnm_quad_zero GNM_SUFFIX(go_quad_zero)

#define GnmAccumulator GNM_SUFFIX(GOAccumulator)
#define gnm_accumulator_start GNM_SUFFIX(go_accumulator_start)
#define gnm_accumulator_end GNM_SUFFIX(go_accumulator_end)
#define gnm_accumulator_clear GNM_SUFFIX(go_accumulator_clear)
#define gnm_accumulator_new GNM_SUFFIX(go_accumulator_new)
#define gnm_accumulator_free GNM_SUFFIX(go_accumulator_free)
#define gnm_accumulator_add GNM_SUFFIX(go_accumulator_add)
#define gnm_accumulator_add_quad GNM_SUFFIX(go_accumulator_add_quad)
#define gnm_accumulator_value GNM_SUFFIX(go_accumulator_value)


#ifdef GNM_REDUCES_TRIG_RANGE
gnm_float gnm_sin (gnm_float x);
gnm_float gnm_cos (gnm_float x);
gnm_float gnm_tan (gnm_float x);
#endif


G_END_DECLS

#endif /* _GNM_NUMBERS_H_ */
