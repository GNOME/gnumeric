#ifndef GNUMERIC_NUMBERS_H
#define GNUMERIC_NUMBERS_H

#include <math.h>
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif
#ifdef HAVE_IEEE754_H
#include <ieee754.h>
#endif

#ifdef WITH_LONG_DOUBLE

#ifdef HAVE_SUNMATH_H
#include <sunmath.h>
#endif

typedef long double gnm_float;
#ifdef HAVE_STRTOLD
#ifdef MUST_PROTOTYPE_STRTOLD
long double strtold (const char *, char **);
#endif
#define gnm_strto strtold
#else
#define NEED_FAKE_STRTOGNUM
/* Defined in gutils.c  */
gnm_float gnm_strto (const char *str, char **end);
#endif

#ifdef HAVE_MODFL
#define gnm_modf modfl
#else
#define NEED_FAKE_MODFGNUM
/* Defined in gutils.c  */
gnm_float gnm_modf (gnm_float x, gnm_float *iptr);
#endif

#ifdef HAVE_LDEXPL
#define gnm_ldexp ldexpl
#else
#define NEED_FAKE_LDEXPGNUM
/* Defined in gutils.c  */
gnm_float gnm_ldexp (gnm_float x, int exp);
#endif

#ifdef HAVE_FREXPL
#define gnm_frexp frexpl
#else
#define NEED_FAKE_FREXPGNUM
/* Defined in gutils.c  */
gnm_float gnm_frexp (gnm_float x, int *exp);
#endif

#ifdef HAVE_ERF
#define gnm_erf erfl
#else
#define NEED_FAKE_ERFGNUM
/* Defined in gutils.c  */
gnm_float gnm_erf (gnm_float x);
#endif

#ifdef HAVE_ERFC
#define gnm_erfc erfcl
#else
#define NEED_FAKE_ERFCGNUM
/* Defined in gutils.c  */
gnm_float gnm_erfc (gnm_float x);
#endif

#ifdef HAVE_YNL
#define gnm_yn ynl
#else
#define NEED_FAKE_YNGNUM
/* Defined in gutils.c  */
gnm_float gnm_yn (int n, gnm_float x);
#endif

#define gnm_acos acosl
#define gnm_acosh acoshl
#define gnm_asin asinl
#define gnm_asinh asinhl
#define gnm_atan2 atan2l
#define gnm_atan atanl
#define gnm_atanh atanhl
#define gnm_ceil ceill
#define gnm_cos cosl
#define gnm_cosh coshl
#define gnm_exp expl
#define gnm_expm1 expm1l
#define gnm_finite finitel
#define gnm_floor floorl
#define gnm_fmod fmodl
#define gnumabs fabsl
#define gnm_hypot hypotl
#define gnm_isnan isnanl
#define gnm_lgamma lgammal
#define lgamma_rgnum lgammal_r
#define gnm_log10 log10l
#define gnm_log1p log1pl
#define gnm_log logl
#define gnm_pow powl
#define gnm_sin sinl
#define gnm_sinh sinhl
#define gnm_sqrt sqrtl
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

#define gnm_acos acos
#define gnm_acosh acosh
#define gnm_asin asin
#define gnm_asinh asinh
#define gnm_atan2 atan2
#define gnm_atan atan
#define gnm_atanh atanh
#define gnm_ceil ceil
#define gnm_cos cos
#define gnm_cosh cosh
#define gnm_erfc erfc
#define gnm_erf erf
#define gnm_exp exp
#define gnm_expm1 expm1
#define gnm_floor floor
#define gnm_fmod fmod
#define gnm_frexp frexp
#define gnumabs fabs
#define gnm_hypot hypot
#define gnm_isnan isnan
#define gnm_ldexp ldexp
#define gnm_lgamma lgamma
#define lgamma_rgnum lgamma_r
#define gnm_log10 log10
#define gnm_log1p log1p
#define gnm_log log
#define gnm_modf modf
#define gnm_pow pow
#define gnm_sin sin
#define gnm_sinh sinh
#define gnm_sqrt sqrt
#define gnm_strto strtod
#define gnm_tan tan
#define gnm_tanh tanh
#define gnm_yn yn

/* What a circus!  */
#ifdef HAVE_FINITE
#define gnm_finite finite
#elif defined(HAVE_ISFINITE)
#define gnm_finite isfinite
#elif defined(FINITE)
#define gnm_finite FINITE
#error "I don't know an equivalent of finite for your system; you lose"
#endif

#ifndef HAVE_LGAMMA_R
#define NEED_FAKE_LGAMMA_R
/* Defined in gutils.c  */
gnm_float lgamma_rgnum (gnm_float x, int *signp);
#endif

#ifndef HAVE_EXPM1
#define NEED_FAKE_EXPM1
/* Defined in gutils.c  */
gnm_float expm1 (gnm_float x);
#endif

#ifndef HAVE_ASINH
#define NEED_FAKE_ASINH
/* Defined in gutils.c  */
gnm_float asinh (gnm_float x);
#endif

#ifndef HAVE_ACOSH
#define NEED_FAKE_ACOSH
/* Defined in gutils.c  */
gnm_float acosh (gnm_float x);
#endif

#ifndef HAVE_ATANH
#define NEED_FAKE_ATANH
/* Defined in gutils.c  */
gnm_float atanh (gnm_float x);
#endif

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

#endif /* GNUMERIC_NUMBERS_H */
