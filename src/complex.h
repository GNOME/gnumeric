#ifndef _GNM_COMPLEX_H_
# define _GNM_COMPLEX_H_

#include <numbers.h>
#include <goffice/goffice.h>
#include <math.h>

G_BEGIN_DECLS

#define gnm_complex GNM_SUFFIX(GOComplex)
#define gnm_complex_init GNM_SUFFIX(go_complex_init)
#define gnm_complex_add GNM_SUFFIX(go_complex_add)
#define gnm_complex_sub GNM_SUFFIX(go_complex_sub)
#define gnm_complex_mul GNM_SUFFIX(go_complex_mul)
#define gnm_complex_div GNM_SUFFIX(go_complex_div)
#define gnm_complex_mod GNM_SUFFIX(go_complex_mod)
#define gnm_complex_angle GNM_SUFFIX(go_complex_angle)
#define gnm_complex_angle_pi GNM_SUFFIX(go_complex_angle_pi)
#define gnm_complex_real GNM_SUFFIX(go_complex_real)
#define gnm_complex_real_p GNM_SUFFIX(go_complex_real_p)
#define gnm_complex_zero_p GNM_SUFFIX(go_complex_zero_p)
#define gnm_complex_conj GNM_SUFFIX(go_complex_conj)
#define gnm_complex_exp GNM_SUFFIX(go_complex_exp)
#define gnm_complex_ln GNM_SUFFIX(go_complex_ln)
#define gnm_complex_sqrt GNM_SUFFIX(go_complex_sqrt)
#define gnm_complex_sin GNM_SUFFIX(go_complex_sin)
#define gnm_complex_cos GNM_SUFFIX(go_complex_cos)
#define gnm_complex_tan GNM_SUFFIX(go_complex_tan)
#define gnm_complex_pow GNM_SUFFIX(go_complex_pow)
#define gnm_complex_powx GNM_SUFFIX(go_complex_powx)
#define gnm_complex_scale_real GNM_SUFFIX(go_complex_scale_real)
#define gnm_complex_to_polar GNM_SUFFIX(go_complex_to_polar)
#define gnm_complex_from_polar GNM_SUFFIX(go_complex_from_polar)
#define gnm_complex_from_polar_pi GNM_SUFFIX(go_complex_from_polar_pi)

/* ------------------------------------------------------------------------- */

char *gnm_complex_to_string (gnm_complex const *src, char imunit);

int gnm_complex_from_string (gnm_complex *dst, char const *src, char *imunit);

int gnm_complex_invalid_p (gnm_complex const *src);

/* ------------------------------------------------------------------------- */
// Value interface

static inline gnm_complex
gnm_complex_f1_ (void (*f) (gnm_complex *, gnm_complex const *),
		 gnm_complex a1)
{
	gnm_complex res;
	f (&res, &a1);
	return res;
}

static inline gnm_complex
gnm_complex_f2_ (void (*f) (gnm_complex *, gnm_complex const *, gnm_complex const *),
		 gnm_complex a1, gnm_complex a2)
{
	gnm_complex res;
	f (&res, &a1, &a2);
	return res;
}

#define GNM_CRE(c) (+(c).re)
#define GNM_CIM(c) (+(c).im)
static inline gnm_complex GNM_CMAKE (gnm_float re, gnm_float im)
{
	gnm_complex res;
	res.re = re;
	res.im = im;
	return res;
}
#define GNM_CREAL(r) (GNM_CMAKE((r),0))
#define GNM_CREALP(c) (GNM_CIM((c)) == 0)
#define GNM_CZEROP(c) (GNM_CEQ((c),GNM_C0))
#define GNM_C0 (GNM_CREAL (0))
#define GNM_C1 (GNM_CREAL (1))
#define GNM_CI (GNM_CMAKE (0, 1))
#define GNM_CNAN (GNM_CMAKE (gnm_nan, gnm_nan))

static inline gboolean GNM_CEQ(gnm_complex c1, gnm_complex c2)
{
	return c1.re == c2.re && c1.im == c2.im;
}

static inline gnm_complex GNM_CPOLAR (gnm_float mod, gnm_float angle)
{
	gnm_complex res;
	gnm_complex_from_polar (&res, mod, angle);
	return res;
}
static inline gnm_complex GNM_CPOLARPI (gnm_float mod, gnm_float angle)
{
	gnm_complex res;
	gnm_complex_from_polar_pi (&res, mod, angle);
	return res;
}
static inline gnm_float GNM_CARG (gnm_complex c) { return gnm_complex_angle (&c); }
static inline gnm_float GNM_CARGPI (gnm_complex c) { return gnm_complex_angle_pi (&c); }
static inline gnm_float GNM_CABS (gnm_complex c) { return gnm_complex_mod (&c); }

#define GNM_CADD(c1,c2) (gnm_complex_f2_ (gnm_complex_add, (c1), (c2)))
#define GNM_CSUB(c1,c2) (gnm_complex_f2_ (gnm_complex_sub, (c1), (c2)))
#define GNM_CMUL(c1,c2) (gnm_complex_f2_ (gnm_complex_mul, (c1), (c2)))
#define GNM_CMUL3(c1,c2,c3) GNM_CMUL(GNM_CMUL(c1,c2),c3)
#define GNM_CMUL4(c1,c2,c3,c4) GNM_CMUL(GNM_CMUL(GNM_CMUL(c1,c2),c3),c4)
#define GNM_CDIV(c1,c2) (gnm_complex_f2_ (gnm_complex_div, (c1), (c2)))
#define GNM_CPOW(c1,c2) (gnm_complex_f2_ (gnm_complex_pow, (c1), (c2)))
static inline gnm_complex GNM_CPOWX(gnm_complex c1, gnm_complex c2, gnm_float *e)
{
	gnm_complex res;
	gnm_complex_powx (&res, e, &c1, &c2);
	return res;
}

#define GNM_CCONJ(c1) (gnm_complex_f1_ (gnm_complex_conj, (c1)))
#define GNM_CSQRT(c1) (gnm_complex_f1_ (gnm_complex_sqrt, (c1)))
#define GNM_CEXP(c1) (gnm_complex_f1_ (gnm_complex_exp, (c1)))
#define GNM_CLN(c1)  (gnm_complex_f1_ (gnm_complex_ln, (c1)))
#define GNM_CSIN(c1) (gnm_complex_f1_ (gnm_complex_sin, (c1)))
#define GNM_CCOS(c1) (gnm_complex_f1_ (gnm_complex_cos, (c1)))
#define GNM_CTAN(c1) (gnm_complex_f1_ (gnm_complex_tan, (c1)))
#define GNM_CINV(c1) (GNM_CDIV (GNM_C1, (c1)))
static inline gnm_complex GNM_CNEG(gnm_complex c)
{
	return GNM_CMAKE (-c.re, -c.im);
}

static inline gnm_complex GNM_CSCALE(gnm_complex c, gnm_float s)
{
	return GNM_CMAKE (c.re * s, c.im * s);
}

static inline gnm_complex GNM_CEXPPI(gnm_complex c)
{
	return GNM_CPOLARPI (gnm_exp (c.re), c.im);
}

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif /* _GNM_COMPLEX_H_ */
