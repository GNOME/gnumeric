#ifndef GNUMERIC_MATHFUNC_H
#define GNUMERIC_MATHFUNC_H

#include "numbers.h"
#include <math.h>
#include <glib.h>

#ifndef FINITE
#  ifdef finite
#    define FINITE finite
#  elif defined(isfinite)
#    define FINITE isfinite
#  endif
#endif

#ifdef qgamma
/* It was reported that mips-sgi-irix6.5 has a weird and conflicting define
   for qgamma.  See bug 1689.  */
#warning "Your <math.h> is somewhat broken; we'll work around that."
#undef qgamma
#endif

/* Make up for a few deficient headers.  */
#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif
#ifndef M_LN10
#define M_LN10 2.30258509299404568402
#endif

/* ------------------------------------------------------------------------- */

double gnumeric_fake_floor (double x);
double gnumeric_fake_ceil (double x);
double gnumeric_fake_round (double x);
double gnumeric_fake_trunc (double x);

int range_sum (const gnum_float *xs, int n, gnum_float *res);
int range_product (const gnum_float *xs, int n, gnum_float *res);

int range_sumsq (const gnum_float *xs, int n, gnum_float *res);
int range_avedev (const gnum_float *xs, int n, gnum_float *res);

int range_average (const gnum_float *xs, int n, gnum_float *res);
int range_harmonic_mean (const gnum_float *xs, int n, gnum_float *res);
int range_geometric_mean (const gnum_float *xs, int n, gnum_float *res);

int range_min (const gnum_float *xs, int n, gnum_float *res);
int range_max (const gnum_float *xs, int n, gnum_float *res);

int range_devsq (const gnum_float *xs, int n, gnum_float *res);
int range_var_pop (const gnum_float *xs, int n, gnum_float *res);
int range_var_est (const gnum_float *xs, int n, gnum_float *res);
int range_stddev_pop (const gnum_float *xs, int n, gnum_float *res);
int range_stddev_est (const gnum_float *xs, int n, gnum_float *res);
int range_skew_pop (const gnum_float *xs, int n, gnum_float *res);
int range_skew_est (const gnum_float *xs, int n, gnum_float *res);
int range_kurtosis_m3_pop (const gnum_float *xs, int n, gnum_float *res);
int range_kurtosis_m3_est (const gnum_float *xs, int n, gnum_float *res);

int range_covar (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_correl_pop (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_correl_est (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_rsq_pop (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);
int range_rsq_est (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res);

/* ------------------------------------------------------------------------- */

double bessel_i (double x, double alpha, double expo);
double bessel_k (double x, double alpha, double expo);

/* "d": density.  */
/* "p": distribution function.  */
/* "q": inverse distribution function.  */

/* The normal distribution.  */
double dnorm (double x, double mu, double sigma);
double pnorm (double x, double mu, double sigma);
double qnorm (double p, double mu, double sigma);

/* The log-normal distribution.  */
double plnorm (double x, double logmean, double logsd);
double qlnorm (double x, double logmean, double logsd);

/* The gamma distribution.  */
double dgamma (double x, double shape, double scale);
double pgamma (double x, double p, double scale);
double qgamma (double p, double alpha, double scale);

/* The beta distribution.  */
double pbeta (double x, double pin, double qin);
double qbeta (double alpha, double p, double q);

/* The t distribution.  */
double pt (double x, double n);
double qt (double p, double ndf);

/* The F distribution.  */
double pf (double x, double n1, double n2);
double qf (double x, double n1, double n2);

/* The chi-squared distribution.  */
double pchisq (double x, double df);
double qchisq (double p, double df);

/* The Weibull distribution.  */
double dweibull (double x, double shape, double scale);
double pweibull (double x, double shape, double scale);

/* The Poisson distribution.  */
double dpois (double x, double lambda);
double ppois (double x, double lambda);

/* The exponential distribution.  */
double dexp (double x, double scale);
double pexp (double x, double scale);

/* Binomial distribution.  */
double dbinom (double x, double n, double p);
double pbinom (double x, double n, double p);
double qbinom (double x, double n, double p);

/* Random number generation. */
double random_01          (void);
double random_poisson     (double lambda);
double random_binomial    (double p, int trials);
double random_negbinom    (double p, int f);
double random_exponential (double b);
double random_bernoulli   (double p);
double random_normal      (void);

/* ------------------------------------------------------------------------- */

/* Matrix functions. */
gnum_float mdeterm (gnum_float *A, int dim);
int     minverse(gnum_float *A, int dim, gnum_float *res);
void    mmult   (gnum_float *A, gnum_float *B, int cols_a, int rows_a, int cols_b,
		 gnum_float *product);

/* ------------------------------------------------------------------------- */

/* Misc. */
gnum_float     gpow10 (int n);
int         gcd    (int a, int b);
gnum_float     combin (int n, int k);
gnum_float     fact   (int n);

/* ------------------------------------------------------------------------- */

/* Optimization methods for the Solver tool. */


/* Affine scaling */

typedef void (*affscale_callback_fun_t) (int iter, gnum_float *x,
					 gnum_float bv, gnum_float cx,
					 int n_variables, void *data);

gboolean affine_init (gnum_float *A, gnum_float *b, gnum_float *c, int n_constraints,
		      int n_variables, gnum_float *x);
gboolean affine_scale (gnum_float *A, gnum_float *b, gnum_float *c, gnum_float *x,
		       int n_constraints, int n_variables, gboolean max_flag,
		       gnum_float e, int max_iter,
		       affscale_callback_fun_t fun, void *data);

gboolean branch_and_bound (gnum_float *A, gnum_float *b, gnum_float *c, gnum_float *xx,
			   int n_constraints, int n_variables, int n_original,
			   gboolean max_flag, gnum_float e, int max_iter,
			   gboolean *int_r,
			   affscale_callback_fun_t fun, void *data,
			   gnum_float *best);

#endif
