#ifndef GNUMERIC_NUMBERS_H
#define GNUMERIC_NUMBERS_H

typedef int    gnum_int;


#ifdef WITH_LONG_DOUBLE

typedef long double gnum_float;
#ifdef HAVE_STRTOLD
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

#ifdef HAVE_FABSL
#define gnumabs fabsl
#else
#define NEED_FAKE_GNUMABS
/* Defined in gutils.c  */
gnum_float gnumabs (gnum_float x);
#endif

#define GNUM_FORMAT_e "Le"
#define GNUM_FORMAT_E "LE"
#define GNUM_FORMAT_f "Lf"
#define GNUM_FORMAT_g "Lg"
#define GNUM_DIG LDBL_DIG
#define GNUM_MANT_DIG LDBL_MANT_DIG
#define GNUM_MAX_EXP LDBL_MAX_EXP
#define GNUM_MAX LDBL_MAX

#else /* !WITH_LONG_DOUBLE */

typedef double gnum_float;
#define strtognum strtod
#define modfgnum modf
#define gnumabs fabs

#define GNUM_FORMAT_e "e"
#define GNUM_FORMAT_E "E"
#define GNUM_FORMAT_f "f"
#define GNUM_FORMAT_g "g"
#define GNUM_DIG DBL_DIG
#define GNUM_MANT_DIG DBL_MANT_DIG
#define GNUM_MAX_EXP DBL_MAX_EXP
#define GNUM_MAX DBL_MAX

#endif

#endif /* GNUMERIC_NUMBERS_H */
