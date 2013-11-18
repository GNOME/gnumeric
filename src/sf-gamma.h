#ifndef GNM_SF_GAMMA_H_
#define GNM_SF_GAMMA_H_

#include <numbers.h>

gnm_float lgamma1p (gnm_float a);
gnm_float stirlerr(gnm_float n);

gnm_float gnm_gamma (gnm_float x);
gnm_float gnm_fact (gnm_float x);
int       qfactf (gnm_float x, GnmQuad *mant, int *exp2);

gnm_float gnm_lbeta (gnm_float a, gnm_float b);
gnm_float beta (gnm_float a, gnm_float b);
gnm_float lbeta3 (gnm_float a, gnm_float b, int *sign);

gnm_float pochhammer (gnm_float x, gnm_float n, gboolean give_log);
gnm_float combin (gnm_float n, gnm_float k);
gnm_float permut (gnm_float n, gnm_float k);

#endif
