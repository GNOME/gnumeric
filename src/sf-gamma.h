#ifndef GNM_SF_GAMMA_H_
#define GNM_SF_GAMMA_H_

#include <numbers.h>
#include <complex.h>

gnm_float lgamma1p (gnm_float a);
gnm_float stirlerr(gnm_float n);

gnm_float gnm_gamma (gnm_float x);
gnm_float gnm_fact (gnm_float x);
int       qfactf (gnm_float x, GnmQuad *mant, int *exp2);
void complex_gamma (complex_t *dst, complex_t const *src);
void complex_fact (complex_t *dst, complex_t const *src);
void complex_igamma (complex_t *dst, complex_t const  *a, complex_t const *z,
		     gboolean lower, gboolean regularized);

gnm_float gnm_lbeta (gnm_float a, gnm_float b);
gnm_float gnm_beta (gnm_float a, gnm_float b);
gnm_float gnm_lbeta3 (gnm_float a, gnm_float b, int *sign);

gnm_float pochhammer (gnm_float x, gnm_float n);
gnm_float combin (gnm_float n, gnm_float k);
gnm_float permut (gnm_float n, gnm_float k);

#endif
