#ifndef GNUMERIC_NUMBERS_H
#define GNUMERIC_NUMBERS_H

typedef int    gnum_int;


#ifdef WITH_LONG_DOUBLE

typedef long double gnum_float;
#ifdef HAVE_STRTOLD
#define strtognum strtold
#else
/* Defined in gutils.h  */
#endif

#ifdef HAVE_MODFL
#define modfgnum modfl
#else
#define NEED_FAKE_MODFL
#define modfgnum fake_modfl
#endif

#define GNUM_FORMAT_e "Le"
#define GNUM_FORMAT_E "LE"
#define GNUM_FORMAT_f "Lf"
#define GNUM_FORMAT_g "Lg"
#define GNUM_DIG LDBL_DIG

#else /* !WITH_LONG_DOUBLE */

typedef double gnum_float;
#define strtognum strtod
#define modfgnum modf

#define GNUM_FORMAT_e "e"
#define GNUM_FORMAT_E "E"
#define GNUM_FORMAT_f "f"
#define GNUM_FORMAT_g "g"
#define GNUM_DIG DBL_DIG

#endif

#endif /* GNUMERIC_NUMBERS_H */
