#ifndef GNUMERIC_NUMBERS_H
#define GNUMERIC_NUMBERS_H

typedef int    gnum_int;


#ifdef WITH_LONG_DOUBLE

#ifdef HAVE_SUNMATH_H
#include <sunmath.h>
#endif

typedef long double gnum_float;
#ifdef HAVE_STRTOLD
#ifdef MUST_PROTOTYPE_STRTOLD
long double strtold (const char *, char **);
#endif
#define strtognum strtold
#else
#define NEED_FAKE_STRTOGNUM
/* Defined in gutils.c  */
gnum_float strtognum (const char *str, char **end);
#endif

#ifdef HAVE_MODFL
#define modfgnum modfl
#else
#define NEED_FAKE_MODFGNUM
/* Defined in gutils.c  */
gnum_float modfgnum (gnum_float x, gnum_float *iptr);
#endif

#ifdef HAVE_LDEXPL
#define ldexpgnum ldexpl
#else
#define NEED_FAKE_LDEXPGNUM
/* Defined in gutils.c  */
gnum_float ldexpgnum (gnum_float x, int exp);
#endif

#ifdef HAVE_FREXPL
#define frexpgnum frexpl
#else
#define NEED_FAKE_FREXPGNUM
/* Defined in gutils.c  */
gnum_float frexpgnum (gnum_float x, int *exp);
#endif

#ifdef HAVE_ERF
#define erfgnum erfl
#else
#define NEED_FAKE_ERFGNUM
/* Defined in gutils.c  */
gnum_float erfgnum (gnum_float x);
#endif

#ifdef HAVE_ERFC
#define erfcgnum erfcl
#else
#define NEED_FAKE_ERFCGNUM
/* Defined in gutils.c  */
gnum_float erfcgnum (gnum_float x);
#endif

#ifdef HAVE_YNL
#define yngnum ynl
#else
#define NEED_FAKE_YNGNUM
/* Defined in gutils.c  */
gnum_float yn (int n, gnum_float x);
#endif

#define sqrtgnum sqrtl
#define gnumabs fabsl
#define floorgnum floorl
#define ceilgnum ceill
#define powgnum powl
#define hypotgnum hypotl
#define expgnum expl
#define expm1gnum expm1l
#define loggnum logl
#define log10gnum log10l
#define log1pgnum log1pl
#define singnum sinl
#define cosgnum cosl
#define tangnum tanl
#define asingnum asinl
#define acosgnum acosl
#define atangnum atanl
#define atan2gnum atan2l
#define isnangnum isnanl
#define finitegnum finitel
#define sinhgnum sinhl
#define coshgnum coshl
#define tanhgnum tanhl
#define asinhgnum asinhl
#define acoshgnum acoshl
#define atanhgnum atanhl

#define GNUM_FORMAT_e "Le"
#define GNUM_FORMAT_E "LE"
#define GNUM_FORMAT_f "Lf"
#define GNUM_FORMAT_g "Lg"
#define GNUM_DIG LDBL_DIG
#define GNUM_MANT_DIG LDBL_MANT_DIG
#define GNUM_MIN_EXP LDBL_MIN_EXP
#define GNUM_MAX_EXP LDBL_MAX_EXP
#define GNUM_MIN LDBL_MIN
#define GNUM_MAX LDBL_MAX
#define GNUM_EPSILON LDBL_EPSILON
#define GNUM_const(_c) _c ## L

#else /* !WITH_LONG_DOUBLE */

typedef double gnum_float;
#define strtognum strtod
#define modfgnum modf
#define gnumabs fabs
#define ldexpgnum ldexp
#define frexpgnum frexp
#define sqrtgnum sqrt
#define floorgnum floor
#define ceilgnum ceil
#define powgnum pow
#define hypotgnum hypot
#define expgnum exp
#define expm1gnum expm1
#define loggnum log
#define log10gnum log10
#define log1pgnum log1p
#define singnum sin
#define cosgnum cos
#define tangnum tan
#define asingnum asin
#define acosgnum acos
#define atangnum atan
#define atan2gnum atan2
#define erfgnum erf
#define erfcgnum erfc
#define yngnum yn
#define isnangnum isnan
#define finitegnum finite
#define sinhgnum sinh
#define coshgnum cosh
#define tanhgnum tanh
#define asinhgnum asinh
#define acoshgnum acosh
#define atanhgnum atanh

#define GNUM_FORMAT_e "e"
#define GNUM_FORMAT_E "E"
#define GNUM_FORMAT_f "f"
#define GNUM_FORMAT_g "g"
#define GNUM_DIG DBL_DIG
#define GNUM_MANT_DIG DBL_MANT_DIG
#define GNUM_MIN_EXP DBL_MIN_EXP
#define GNUM_MAX_EXP DBL_MAX_EXP
#define GNUM_MIN DBL_MIN
#define GNUM_MAX DBL_MAX
#define GNUM_EPSILON DBL_EPSILON
#define GNUM_const(_c) _c

#endif

#endif /* GNUMERIC_NUMBERS_H */
