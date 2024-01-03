#ifndef GNM_SF_GAMMA_H_
#define GNM_SF_GAMMA_H_

#include <numbers.h>
#include <complex.h>

gnm_float stirlerr(gnm_float n);

gnm_float gnm_gamma (gnm_float x);
gnm_float gnm_gammax (gnm_float x, int *expb);
gnm_float gnm_fact (gnm_float x);
gnm_float gnm_factx (gnm_float x, int *expb);
int       qfactf (gnm_float x, GnmQuad *mant, int *expb);
gnm_complex gnm_complex_gamma (gnm_complex z, int *expb);
gnm_complex gnm_complex_fact (gnm_complex z, int *expb);
gnm_complex gnm_complex_igamma (gnm_complex a, gnm_complex z,
				gboolean lower, gboolean regularized);

gnm_float gnm_fact2 (int x);

gnm_float gnm_digamma (gnm_float x);

gnm_float gnm_lbeta (gnm_float a, gnm_float b);
gnm_float gnm_beta (gnm_float a, gnm_float b);
gnm_float gnm_lbeta3 (gnm_float a, gnm_float b, int *sign);

gnm_float pochhammer (gnm_float x, gnm_float n);
gnm_float combin (gnm_float n, gnm_float k);
gnm_float permut (gnm_float n, gnm_float k);

#endif
