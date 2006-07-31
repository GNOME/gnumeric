#ifndef __GSL_COMPLEX_H__
#define __GSL_COMPLEX_H__

void gsl_complex_inverse  (complex_t const *a, complex_t *res);
void gsl_complex_negative (complex_t const *a, complex_t *res);
void gsl_complex_arcsin   (complex_t const *a, complex_t *res);
void gsl_complex_arccos   (complex_t const *a, complex_t *res);
void gsl_complex_arctan   (complex_t const *a, complex_t *res);
void gsl_complex_arcsec   (complex_t const *a, complex_t *res);
void gsl_complex_arccsc   (complex_t const *a, complex_t *res);
void gsl_complex_arccot   (complex_t const *a, complex_t *res);
void gsl_complex_sinh     (complex_t const *a, complex_t *res);
void gsl_complex_cosh     (complex_t const *a, complex_t *res);
void gsl_complex_tanh     (complex_t const *a, complex_t *res);
void gsl_complex_sech     (complex_t const *a, complex_t *res);
void gsl_complex_csch     (complex_t const *a, complex_t *res);
void gsl_complex_coth     (complex_t const *a, complex_t *res);
void gsl_complex_arcsinh  (complex_t const *a, complex_t *res);
void gsl_complex_arccosh  (complex_t const *a, complex_t *res);
void gsl_complex_arctanh  (complex_t const *a, complex_t *res);
void gsl_complex_arcsech  (complex_t const *a, complex_t *res);
void gsl_complex_arccsch  (complex_t const *a, complex_t *res);
void gsl_complex_arccoth  (complex_t const *a, complex_t *res);

#endif
