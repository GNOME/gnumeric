#ifndef __GSL_COMPLEX_H__
#define __GSL_COMPLEX_H__

void gsl_complex_inverse  (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arcsin   (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arccos   (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arctan   (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arcsec   (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arccsc   (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arccot   (gnm_complex const *a, gnm_complex *res);
void gsl_complex_sinh     (gnm_complex const *a, gnm_complex *res);
void gsl_complex_cosh     (gnm_complex const *a, gnm_complex *res);
void gsl_complex_tanh     (gnm_complex const *a, gnm_complex *res);
void gsl_complex_sech     (gnm_complex const *a, gnm_complex *res);
void gsl_complex_csch     (gnm_complex const *a, gnm_complex *res);
void gsl_complex_coth     (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arcsinh  (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arccosh  (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arctanh  (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arcsech  (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arccsch  (gnm_complex const *a, gnm_complex *res);
void gsl_complex_arccoth  (gnm_complex const *a, gnm_complex *res);

static inline gnm_complex
gnm_complex_f1r_ (void (*f) (gnm_complex const *, gnm_complex *),
		  gnm_complex a1)
{
	gnm_complex res;
	f (&a1, &res);
	return res;
}

#define GNM_CARCSIN(c1) (gnm_complex_f1r_ (gsl_complex_arcsin, (c1)))
#define GNM_CARCCOS(c1) (gnm_complex_f1r_ (gsl_complex_arccos, (c1)))
#define GNM_CARCTAN(c1) (gnm_complex_f1r_ (gsl_complex_arctan, (c1)))
#define GNM_CARCSEC(c1) (gnm_complex_f1r_ (gsl_complex_arcsec, (c1)))
#define GNM_CARCCSC(c1) (gnm_complex_f1r_ (gsl_complex_arccsc, (c1)))
#define GNM_CARCCOT(c1) (gnm_complex_f1r_ (gsl_complex_arccot, (c1)))
#define GNM_CSINH(c1) (gnm_complex_f1r_ (gsl_complex_sinh, (c1)))
#define GNM_CCOSH(c1) (gnm_complex_f1r_ (gsl_complex_cosh, (c1)))
#define GNM_CTANH(c1) (gnm_complex_f1r_ (gsl_complex_tanh, (c1)))
#define GNM_CSECH(c1) (gnm_complex_f1r_ (gsl_complex_sech, (c1)))
#define GNM_CCSCH(c1) (gnm_complex_f1r_ (gsl_complex_csch, (c1)))
#define GNM_CCOTH(c1) (gnm_complex_f1r_ (gsl_complex_coth, (c1)))
#define GNM_CARCSINH(c1) (gnm_complex_f1r_ (gsl_complex_arcsinh, (c1)))
#define GNM_CARCCOSH(c1) (gnm_complex_f1r_ (gsl_complex_arccosh, (c1)))
#define GNM_CARCTANH(c1) (gnm_complex_f1r_ (gsl_complex_arctanh, (c1)))
#define GNM_CARCSECH(c1) (gnm_complex_f1r_ (gsl_complex_arcsech, (c1)))
#define GNM_CARCCSCH(c1) (gnm_complex_f1r_ (gsl_complex_arccsch, (c1)))
#define GNM_CARCCOTH(c1) (gnm_complex_f1r_ (gsl_complex_arccoth, (c1)))

#endif
