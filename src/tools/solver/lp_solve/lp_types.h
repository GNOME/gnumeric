#ifndef HEADER_lp_types
#define HEADER_lp_types

/* Define data types                                                         */
/* ------------------------------------------------------------------------- */
#ifndef REAL
  #define REAL  double
#endif

/*#define LREAL long double*/    /* Defines a global update variable long double */
#define LREAL REAL      /* Defines a global update variable regular variable */
#define RREAL long double        /* Defines a local accumulation long double */
/*#define RREAL REAL*/          /* Defines a local accumulation regular variable */

#ifndef DEF_STRBUFSIZE
  #define DEF_STRBUFSIZE   512
#endif
#ifndef MAXINT32
  #define MAXINT32  2147483647
#endif
#ifndef MAXUINT32
  #define MAXUINT32 4294967295
#endif
#ifndef MAXINT64
  #define MAXINT64   9223372036854775807
#endif
#ifndef MAXUINT64
  #define MAXUINT64 18446744073709551616
#endif
#ifndef CHAR_BIT
  #define CHAR_BIT  8
#endif
#ifndef MYBOOL
/*  #define MYBOOL  unsigned int */          /* May be faster on some processors */
  #define MYBOOL  unsigned char                         /* Conserves memory */
#endif


/* Constants                                                                 */
/* ------------------------------------------------------------------------- */
#ifndef NULL
  #define NULL                   0
#endif

#define FALSE                    0
#define TRUE                     1
#define AUTOMATIC                2
#define DYNAMIC                  4

/* Result ranges */
#define XRESULT_FREE             0
#define XRESULT_PLUS             1
#define XRESULT_MINUS           -1
#define XRESULT_RC               XRESULT_FREE


/* Compiler/target settings                                                  */
/* ------------------------------------------------------------------------- */
#ifdef WIN32
# define __WINAPI WINAPI
#else
# define __WINAPI
#endif

#ifndef __BORLANDC__                            /* Level 0 BEGIN */

#ifdef _USRDLL                                  /* Level 1 BEGIN */

#if 1
#define __EXPORT_TYPE __declspec(dllexport)
#else
/* Set up for the Microsoft compiler */
#ifdef LP_SOLVE_EXPORTS                         /* Level 2 BEGIN */
  #define __EXPORT_TYPE __declspec(dllexport)
#else
  #define __EXPORT_TYPE __declspec(dllimport)
#endif                                          /* Level 2 END */
#endif

#else

#define __EXPORT_TYPE

#endif                                          /* Level 1 END */

#ifdef __cplusplus
  #define __EXTERN_C extern "C"
#else
  #define __EXTERN_C
#endif

#else                                           /* Level 0 ELSE */

/* Otherwise set up for the Borland compiler */
#ifdef __DLL__                                  /* Level 1 BEGIN */

#define _USRDLL
#define __EXTERN_C extern "C"

#ifdef __READING_THE_DLL                        /* Level 2 BEGIN */
  #define __EXPORT_TYPE __import
#else
  #define __EXPORT_TYPE __export
#endif                                          /* Level 2 END */

#else

#define __EXPORT_TYPE
#define __EXTERN_C extern "C"

#endif                                          /* Level 1 END */

#endif                                          /* Level 0 END */


#if 0
  #define STATIC static
#else
  #define STATIC
#endif

#ifdef __cplusplus
  #define __EXTERN_C extern "C"
#else
  #define __EXTERN_C
#endif


/* Define macros                                                             */
/* ------------------------------------------------------------------------- */
#define my_min(x, y)            ((x) < (y) ? (x) : (y))
#define my_max(x, y)            ((x) > (y) ? (x) : (y))
#define my_range(x, lo, hi)     ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#ifndef my_mod
  #define my_mod(n, m)          ((n) % (m))
#endif
#define my_if(t, x, y)          ((t) ? (x) : (y))
#define my_sign(x)              ((x) < 0 ? -1 : 1)
#define my_chsign(t, x)         ( ((t) && ((x) != 0)) ? -(x) : (x))
#define my_flipsign(x)          ( fabs((REAL) (x)) == 0 ? 0 : -(x) )
#define my_roundzero(val, eps)  if (fabs((REAL) (val)) < eps) val = 0
#define my_inflimit(val)        (fabs((REAL) (val)) < lp->infinite ? (val) : lp->infinite * my_sign(val) )
#if 0
  #define my_precision(val, eps) ((fabs((REAL) (val))) < (eps) ? 0 : (val))
#else
  #define my_precision(val, eps) restoreINT(val, eps)
#endif
#define my_reldiff(x, y)       (((x) - (y)) / (1.0 + fabs((REAL) (y))))
#define my_boundstr(x)         (fabs(x) < lp->infinite ? sprintf("%g",x) : ((x) < 0 ? "-Inf" : "Inf") )
#ifndef my_boolstr
  #define my_boolstr(x)          (!(x) ? "FALSE" : "TRUE")
#endif
#define my_basisstr(x)         ((x) ? "BASIC" : "NON-BASIC")
#define my_lowbound(x)         ((FULLYBOUNDEDSIMPLEX) ? (x) : 0)


/* Forward declarations                                                      */
/* ------------------------------------------------------------------------- */
typedef struct _lprec    lprec;
typedef struct _INVrec   INVrec;
/* typedef struct _pricerec pricerec; */

typedef struct _pricerec
{
  REAL    theta;
  REAL    pivot;
  int     varno;
  lprec   *lp;
  MYBOOL  isdual;
} pricerec;

#endif /* HEADER_lp_types */
