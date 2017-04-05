#ifndef GNM_SF_TRIG_H_
#define GNM_SF_TRIG_H_

#include <numbers.h>

gnm_float gnm_cot (gnm_float x);
gnm_float gnm_acot (gnm_float x);
gnm_float gnm_coth (gnm_float x);
gnm_float gnm_acoth (gnm_float x);

gnm_float gnm_reduce_pi (gnm_float x, int e, int *k);

#ifdef GNM_REDUCES_TRIG_RANGE
/* gnm_sin, gnm_cos, gnm_tan prototyped in numbers.h */
#endif

#endif
