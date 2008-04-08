/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_MATHFUNC_H_
# define _GNM_MATHFUNC_H_

#include "numbers.h"
#include <math.h>
#include <glib.h>

G_BEGIN_DECLS

#ifdef qgamma
/* It was reported that mips-sgi-irix6.5 has a weird and conflicting define
   for qgamma.  See bug 1689.  */
#warning "Your <math.h> is somewhat broken; we'll work around that."
#undef qgamma
#endif

#define M_PIgnum    GNM_const(3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117)
#define M_PI_2gnum (M_PIgnum / 2)
/* The following are very good given a good compiler.  */
#define M_LN2gnum   GNM_const(0.693147180559945309417232121458176568075500134360255254120680009493393621969694715605863326996419)
#define M_LN10gnum  GNM_const(2.302585092994045684017991454684364207601101488628772976033327900967572609677352480235997205089598)
#define M_SQRT2gnum GNM_const(1.414213562373095048801688724209698078569671875376948073176679737990732478462107038850387534327642)

/* ------------------------------------------------------------------------- */

gnm_float log1pmx (gnm_float x);
gnm_float swap_log_tail (gnm_float lp);
gnm_float lgamma1p (gnm_float a);
gnm_float pow1p (gnm_float x, gnm_float y);
gnm_float pow1pm1 (gnm_float x, gnm_float y);
gnm_float gnm_trunc (gnm_float x);
gnm_float logfbit (gnm_float x);
gnm_float logspace_add (gnm_float logx, gnm_float logy);
gnm_float logspace_sub (gnm_float logx, gnm_float logy);

gnm_float beta (gnm_float a, gnm_float b);
gnm_float lbeta3 (gnm_float a, gnm_float b, int *sign);

gnm_float bessel_i (gnm_float x, gnm_float alpha, gnm_float expo);
gnm_float bessel_k (gnm_float x, gnm_float alpha, gnm_float expo);

/* "d": density.  */
/* "p": distribution function.  */
/* "q": inverse distribution function.  */

/* The normal distribution.  */
gnm_float dnorm (gnm_float x, gnm_float mu, gnm_float sigma, gboolean give_log);
gnm_float pnorm (gnm_float x, gnm_float mu, gnm_float sigma, gboolean lower_tail, gboolean log_p);
gnm_float qnorm (gnm_float p, gnm_float mu, gnm_float sigma, gboolean lower_tail, gboolean log_p);

/* The log-normal distribution.  */
gnm_float dlnorm (gnm_float x, gnm_float logmean, gnm_float logsd, gboolean give_log);
gnm_float plnorm (gnm_float x, gnm_float logmean, gnm_float logsd, gboolean lower_tail, gboolean log_p);
gnm_float qlnorm (gnm_float x, gnm_float logmean, gnm_float logsd, gboolean lower_tail, gboolean log_p);

/* The gamma distribution.  */
gnm_float dgamma (gnm_float x, gnm_float shape, gnm_float scale, gboolean give_log);
gnm_float pgamma (gnm_float x, gnm_float p, gnm_float scale, gboolean lower_tail, gboolean log_p);
gnm_float qgamma (gnm_float p, gnm_float alpha, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* The beta distribution.  */
gnm_float dbeta (gnm_float x, gnm_float a, gnm_float b, gboolean give_log);
gnm_float pbeta (gnm_float x, gnm_float pin, gnm_float qin, gboolean lower_tail, gboolean log_p);
gnm_float qbeta (gnm_float alpha, gnm_float p, gnm_float q, gboolean lower_tail, gboolean log_p);

/* The t distribution.  */
gnm_float dt (gnm_float x, gnm_float n, gboolean give_log);
gnm_float pt (gnm_float x, gnm_float n, gboolean lower_tail, gboolean log_p);
gnm_float qt (gnm_float p, gnm_float ndf, gboolean lower_tail, gboolean log_p);

/* The F distribution.  */
gnm_float df (gnm_float x, gnm_float m, gnm_float n, gboolean give_log);
gnm_float pf (gnm_float x, gnm_float n1, gnm_float n2, gboolean lower_tail, gboolean log_p);
gnm_float qf (gnm_float x, gnm_float n1, gnm_float n2, gboolean lower_tail, gboolean log_p);

/* The chi-squared distribution.  */
gnm_float dchisq (gnm_float x, gnm_float df, gboolean give_log);
gnm_float pchisq (gnm_float x, gnm_float df, gboolean lower_tail, gboolean log_p);
gnm_float qchisq (gnm_float p, gnm_float df, gboolean lower_tail, gboolean log_p);

/* The Weibull distribution.  */
gnm_float dweibull (gnm_float x, gnm_float shape, gnm_float scale, gboolean give_log);
gnm_float pweibull (gnm_float x, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p);
gnm_float qweibull (gnm_float p, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* The Poisson distribution.  */
gnm_float dpois (gnm_float x, gnm_float lambda, gboolean give_log);
gnm_float ppois (gnm_float x, gnm_float lambda, gboolean lower_tail, gboolean log_p);
gnm_float qpois (gnm_float p, gnm_float lambda, gboolean lower_tail, gboolean log_p);

/* The exponential distribution.  */
gnm_float dexp (gnm_float x, gnm_float scale, gboolean give_log);
gnm_float pexp (gnm_float x, gnm_float scale, gboolean lower_tail, gboolean log_p);
gnm_float qexp (gnm_float p, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* Binomial distribution.  */
gnm_float dbinom (gnm_float x, gnm_float n, gnm_float p, gboolean give_log);
gnm_float pbinom (gnm_float x, gnm_float n, gnm_float p, gboolean lower_tail, gboolean log_p);
gnm_float qbinom (gnm_float x, gnm_float n, gnm_float p, gboolean lower_tail, gboolean log_p);

/* Negative binomial distribution.  */
gnm_float dnbinom (gnm_float x, gnm_float n, gnm_float p, gboolean give_log);
gnm_float pnbinom (gnm_float x, gnm_float n, gnm_float p, gboolean lower_tail, gboolean log_p);
gnm_float qnbinom (gnm_float p, gnm_float n, gnm_float pr, gboolean lower_tail, gboolean log_p);

/* Hyper-geometrical distribution.  */
gnm_float dhyper (gnm_float x, gnm_float r, gnm_float b, gnm_float n, gboolean give_log);
gnm_float phyper (gnm_float x, gnm_float NR, gnm_float NB, gnm_float n, gboolean lower_tail, gboolean log_p);
gnm_float qhyper (gnm_float p, gnm_float NR, gnm_float NB, gnm_float n, gboolean lower_tail, gboolean log_p);

/* Geometrical distribution.  */
gnm_float dgeom (gnm_float x, gnm_float p, gboolean give_log);
gnm_float pgeom (gnm_float x, gnm_float p, gboolean lower_tail, gboolean log_p);
gnm_float qgeom (gnm_float p, gnm_float prob, gboolean lower_tail, gboolean log_p);

/* Cauchy distribution.  */
gnm_float dcauchy (gnm_float x, gnm_float location, gnm_float scale, gboolean give_log);
gnm_float pcauchy (gnm_float x, gnm_float location, gnm_float scale, gboolean lower_tail, gboolean log_p);

/* Random number generation. */
gnm_float random_01             (void);
gnm_float random_poisson        (gnm_float lambda);
gnm_float random_binomial       (gnm_float p, gnm_float trials);
gnm_float random_negbinom       (gnm_float p, gnm_float f);
gnm_float random_exponential    (gnm_float b);
gnm_float random_bernoulli      (gnm_float p);
gnm_float random_normal         (void);
gnm_float random_cauchy         (gnm_float a);
gnm_float random_lognormal      (gnm_float zeta, gnm_float sigma);
gnm_float random_weibull        (gnm_float a, gnm_float b);
gnm_float random_laplace        (gnm_float a);
gnm_float random_rayleigh       (gnm_float sigma);
gnm_float random_rayleigh_tail  (gnm_float a, gnm_float sigma);
gnm_float random_gamma          (gnm_float a, gnm_float b);
gnm_float random_pareto         (gnm_float a, gnm_float b);
gnm_float random_fdist          (gnm_float nu1, gnm_float nu2);
gnm_float random_beta           (gnm_float a, gnm_float b);
gnm_float random_logistic       (gnm_float a);
gnm_float random_geometric      (gnm_float p);
gnm_float random_hypergeometric (gnm_float n1, gnm_float n2, gnm_float t);
gnm_float random_logarithmic    (gnm_float p);
gnm_float random_chisq          (gnm_float nu);
gnm_float random_tdist          (gnm_float nu);
gnm_float random_gumbel1        (gnm_float a, gnm_float b);
gnm_float random_gumbel2        (gnm_float a, gnm_float b);
gnm_float random_levy           (gnm_float c, gnm_float alpha);
gnm_float random_levy_skew      (gnm_float c, gnm_float alpha,
				 gnm_float beta);
gnm_float random_exppow         (gnm_float a, gnm_float b);
gnm_float random_landau         (void);
gnm_float random_gaussian_tail  (gnm_float a, gnm_float sigma);

/* The probability density functions. */
gnm_float random_exppow_pdf     (gnm_float x, gnm_float a, gnm_float b);
gnm_float random_laplace_pdf    (gnm_float x, gnm_float a);

/* ------------------------------------------------------------------------- */

typedef gnm_float (*GnmPFunc) (gnm_float x, const gnm_float shape[],
			       gboolean lower_tail, gboolean log_p);
typedef gnm_float (*GnmDPFunc) (gnm_float x, const gnm_float shape[],
				gboolean log_p);

gnm_float pfuncinverter (gnm_float p, const gnm_float shape[],
			 gboolean lower_tail, gboolean log_p,
			 gnm_float xlow, gnm_float xhigh, gnm_float x0,
			 GnmPFunc pfunc, GnmDPFunc dpfunc_dx);
gnm_float discpfuncinverter (gnm_float p, const gnm_float shape[],
			     gboolean lower_tail, gboolean log_p,
			     gnm_float xlow, gnm_float xhigh, gnm_float x0,
			     GnmPFunc pfunc);

/* ------------------------------------------------------------------------- */

/* Matrix functions. */
void    mmult (gnm_float *A, gnm_float *B, int cols_a, int rows_a, int cols_b,
	       gnm_float *product);

/* ------------------------------------------------------------------------- */

gnm_float combin (gnm_float n, gnm_float k);
gnm_float permut (gnm_float n, gnm_float k);
gnm_float fact   (int n);

gint gnm_float_equal (gnm_float const *a, const gnm_float *b);
guint gnm_float_hash (gnm_float const *d);

void mathfunc_init (void);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif /* _GNM_MATHFUNC_H_ */
