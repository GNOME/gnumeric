#ifndef __GSL_PDF_H__
#define __GSL_PDF_H__

gnum_float random_beta_pdf              (gnum_float x, gnum_float a,
				         gnum_float b);
gnum_float random_exponential_pdf       (gnum_float x, gnum_float mu);
gnum_float random_gamma_pdf             (gnum_float x, gnum_float a,
				         gnum_float b);
gnum_float random_bernoulli_pdf         (int k, gnum_float p);
gnum_float random_binomial_pdf          (int k, gnum_float p, int n);
gnum_float random_cauchy_pdf            (gnum_float x, gnum_float a);
gnum_float random_chisq_pdf             (gnum_float x, gnum_float nu);
gnum_float random_hypergeometric_pdf    (int k, int n1, int n2, int t);
gnum_float random_landau_pdf            (gnum_float x);
gnum_float random_logistic_pdf          (gnum_float x, gnum_float a);
gnum_float random_lognormal_pdf         (gnum_float x, gnum_float zeta,
				         gnum_float sigma);
gnum_float random_negative_binomial_pdf (int k, gnum_float p, gnum_float n);
gnum_float random_pareto_pdf            (gnum_float x, gnum_float a,
					 gnum_float b);
gnum_float random_poisson_pdf           (int k, gnum_float mu);
gnum_float random_rayleigh_pdf          (gnum_float x, gnum_float sigma);
gnum_float random_rayleigh_tail_pdf     (gnum_float x, gnum_float a,
					 gnum_float sigma);
gnum_float random_tdist_pdf             (gnum_float x, gnum_float nu);
gnum_float random_weibull_pdf           (gnum_float x, gnum_float a,
					 gnum_float b);

#endif
