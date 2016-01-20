#ifndef __GSL_COMPLEX_H__
#define __GSL_COMPLEX_H__

void gsl_complex_inverse  (gnm_complex const *a, gnm_complex *res);
void gsl_complex_negative (gnm_complex const *a, gnm_complex *res);
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

#endif
