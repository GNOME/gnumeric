#ifndef GNM_RANDOM_H_
#define GNM_RANDOM_H_

#include <numbers.h>

G_BEGIN_DECLS

gnm_float gnm_random_01             (void);

guint32   gnm_random_uniform_int (guint32 n);
gnm_float gnm_random_uniform_integer (gnm_float l, gnm_float h);

gnm_float gnm_random_poisson        (gnm_float lambda);
gnm_float gnm_random_binomial       (gnm_float p, gnm_float trials);
gnm_float gnm_random_negbinom       (gnm_float p, gnm_float f);
gnm_float gnm_random_exponential    (gnm_float b);
gnm_float gnm_random_bernoulli      (gnm_float p);
gnm_float random_normal         (void);
gnm_float gnm_random_cauchy         (gnm_float a);
gnm_float gnm_random_lognormal      (gnm_float zeta, gnm_float sigma);
gnm_float gnm_random_weibull        (gnm_float a, gnm_float b);
gnm_float gnm_random_laplace        (gnm_float a);
gnm_float gnm_random_rayleigh       (gnm_float sigma);
gnm_float gnm_random_rayleigh_tail  (gnm_float a, gnm_float sigma);
gnm_float gnm_random_gamma          (gnm_float a, gnm_float b);
gnm_float gnm_random_pareto         (gnm_float a, gnm_float b);
gnm_float gnm_random_fdist          (gnm_float nu1, gnm_float nu2);
gnm_float gnm_random_beta           (gnm_float a, gnm_float b);
gnm_float gnm_random_logistic       (gnm_float a);
gnm_float gnm_random_geometric      (gnm_float p);
gnm_float gnm_random_hypergeometric (gnm_float n1, gnm_float n2, gnm_float t);
gnm_float gnm_random_logarithmic    (gnm_float p);
gnm_float gnm_random_chisq          (gnm_float nu);
gnm_float gnm_random_tdist          (gnm_float nu);
gnm_float gnm_random_gumbel1        (gnm_float a, gnm_float b);
gnm_float gnm_random_gumbel2        (gnm_float a, gnm_float b);
gnm_float gnm_random_levy           (gnm_float c, gnm_float alpha);
gnm_float gnm_random_levy_skew      (gnm_float c, gnm_float alpha,
				 gnm_float beta);
gnm_float gnm_random_exppow         (gnm_float a, gnm_float b);
gnm_float gnm_random_landau         (void);
gnm_float gnm_random_gaussian_tail  (gnm_float a, gnm_float sigma);
gnm_float gnm_random_skew_normal    (gnm_float a);
gnm_float gnm_random_skew_tdist     (gnm_float nu, gnm_float a);

G_END_DECLS

#endif
