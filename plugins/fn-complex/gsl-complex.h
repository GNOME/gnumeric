#ifndef __GSL_COMPLEX_H__
#define __GSL_COMPLEX_H__

void gsl_complex_inverse  (const complex_t *a, complex_t *res);
void gsl_complex_negative (const complex_t *a, complex_t *res);
void gsl_complex_arcsin   (const complex_t *a, complex_t *res);
void gsl_complex_arccos   (const complex_t *a, complex_t *res);
void gsl_complex_arctan   (const complex_t *a, complex_t *res);
void gsl_complex_arcsec   (const complex_t *a, complex_t *res);
void gsl_complex_arccsc   (const complex_t *a, complex_t *res);
void gsl_complex_arccot   (const complex_t *a, complex_t *res);
void gsl_complex_sinh     (const complex_t *a, complex_t *res);
void gsl_complex_cosh     (const complex_t *a, complex_t *res);
void gsl_complex_tanh     (const complex_t *a, complex_t *res);
void gsl_complex_sech     (const complex_t *a, complex_t *res);
void gsl_complex_csch     (const complex_t *a, complex_t *res);
void gsl_complex_coth     (const complex_t *a, complex_t *res);
void gsl_complex_arcsinh  (const complex_t *a, complex_t *res);
void gsl_complex_arccosh  (const complex_t *a, complex_t *res);
void gsl_complex_arctanh  (const complex_t *a, complex_t *res);
void gsl_complex_arcsech  (const complex_t *a, complex_t *res);
void gsl_complex_arccsch  (const complex_t *a, complex_t *res);
void gsl_complex_arccoth  (const complex_t *a, complex_t *res);

#endif
