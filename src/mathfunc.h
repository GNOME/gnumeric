#ifndef GNUMERIC_MATHFUNC_H
#define GNUMERIC_MATHFUNC_H

#include "numbers.h"
#include <math.h>
#include <glib.h>

#ifndef FINITE
#  if defined(HAVE_FINITE)
#    ifdef HAVE_IEEEFP_H
#      include <ieeefp.h>
#    endif
#    define FINITE finite
#  elif defined(HAVE_ISFINITE)
#    define FINITE isfinite
#  else
#    error FINITE undefined
#  endif
#endif

#ifdef qgamma
/* It was reported that mips-sgi-irix6.5 has a weird and conflicting define
   for qgamma.  See bug 1689.  */
#warning "Your <math.h> is somewhat broken; we'll work around that."
#undef qgamma
#endif

#define M_PIgnum GNUM_const(3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117)
#define M_PI_2gnum (M_PIgnum / 2)
/* The following are very good given a good compiler.  */
#define M_LN2gnum (loggnum (2))
#define M_LN10gnum (loggnum (10))
#define M_SQRT2gnum (sqrtgnum (2))

/* ------------------------------------------------------------------------- */

gnum_float gnumeric_add_epsilon (gnum_float x);
gnum_float gnumeric_sub_epsilon (gnum_float x);
gnum_float gnumeric_fake_floor (gnum_float x);
gnum_float gnumeric_fake_ceil (gnum_float x);
gnum_float gnumeric_fake_round (gnum_float x);
gnum_float gnumeric_fake_trunc (gnum_float x);

/* ------------------------------------------------------------------------- */

gnum_float bessel_i (gnum_float x, gnum_float alpha, gnum_float expo);
gnum_float bessel_k (gnum_float x, gnum_float alpha, gnum_float expo);

/* "d": density.  */
/* "p": distribution function.  */
/* "q": inverse distribution function.  */

/* The normal distribution.  */
gnum_float dnorm (gnum_float x, gnum_float mu, gnum_float sigma, gboolean give_log);
gnum_float pnorm (gnum_float x, gnum_float mu, gnum_float sigma, gboolean lower_tail, gboolean log_p);
gnum_float qnorm (gnum_float p, gnum_float mu, gnum_float sigma, gboolean lower_tail, gboolean log_p);

/* The log-normal distribution.  */
gnum_float plnorm (gnum_float x, gnum_float logmean, gnum_float logsd, gboolean lower_tail, gboolean log_p);
gnum_float qlnorm (gnum_float x, gnum_float logmean, gnum_float logsd, gboolean lower_tail, gboolean log_p);

/* The gamma distribution.  */
gnum_float dgamma (gnum_float x, gnum_float shape, gnum_float scale, gboolean give_log);
gnum_float pgamma (gnum_float x, gnum_float p, gnum_float scale, gboolean lower_tail, gboolean log_p);
gnum_float qgamma (gnum_float p, gnum_float alpha, gnum_float scale, gboolean lower_tail, gboolean log_p);

/* The beta distribution.  */
gnum_float pbeta (gnum_float x, gnum_float pin, gnum_float qin, gboolean lower_tail, gboolean log_p);
gnum_float qbeta (gnum_float alpha, gnum_float p, gnum_float q, gboolean lower_tail, gboolean log_p);

/* The t distribution.  */
gnum_float pt (gnum_float x, gnum_float n, gboolean lower_tail, gboolean log_p);
gnum_float qt (gnum_float p, gnum_float ndf, gboolean lower_tail, gboolean log_p);

/* The F distribution.  */
gnum_float pf (gnum_float x, gnum_float n1, gnum_float n2, gboolean lower_tail, gboolean log_p);
gnum_float qf (gnum_float x, gnum_float n1, gnum_float n2, gboolean lower_tail, gboolean log_p);

/* The chi-squared distribution.  */
gnum_float pchisq (gnum_float x, gnum_float df, gboolean lower_tail, gboolean log_p);
gnum_float qchisq (gnum_float p, gnum_float df, gboolean lower_tail, gboolean log_p);

/* The Weibull distribution.  */
gnum_float dweibull (gnum_float x, gnum_float shape, gnum_float scale, gboolean give_log);
gnum_float pweibull (gnum_float x, gnum_float shape, gnum_float scale, gboolean lower_tail, gboolean log_p);

/* The Poisson distribution.  */
gnum_float dpois (gnum_float x, gnum_float lambda, gboolean give_log);
gnum_float ppois (gnum_float x, gnum_float lambda, gboolean lower_tail, gboolean log_p);
gnum_float qpois (gnum_float p, gnum_float lambda, gboolean lower_tail, gboolean log_p);

/* The exponential distribution.  */
gnum_float dexp (gnum_float x, gnum_float scale, gboolean give_log);
gnum_float pexp (gnum_float x, gnum_float scale, gboolean lower_tail, gboolean log_p);

/* Binomial distribution.  */
gnum_float dbinom (gnum_float x, gnum_float n, gnum_float p, gboolean give_log);
gnum_float pbinom (gnum_float x, gnum_float n, gnum_float p, gboolean lower_tail, gboolean log_p);
gnum_float qbinom (gnum_float x, gnum_float n, gnum_float p, gboolean lower_tail, gboolean log_p);

/* Negative binomial distribution.  */
gnum_float dnbinom (gnum_float x, gnum_float n, gnum_float p, gboolean give_log);

/* Hyper-geometrical distribution.  */
gnum_float dhyper (gnum_float x, gnum_float r, gnum_float b, gnum_float n, gboolean give_log);

/* Random number generation. */
gnum_float random_01          (void);
gnum_float random_poisson     (gnum_float lambda);
gnum_float random_binomial    (gnum_float p, int trials);
gnum_float random_negbinom    (gnum_float p, int f);
gnum_float random_exponential (gnum_float b);
gnum_float random_bernoulli   (gnum_float p);
gnum_float random_normal      (void);

/* ------------------------------------------------------------------------- */

/* Matrix functions. */
gnum_float mdeterm (gnum_float *A, int dim);
int     minverse (gnum_float *A, int dim, gnum_float *res);
void    mmult (gnum_float *A, gnum_float *B, int cols_a, int rows_a, int cols_b,
	       gnum_float *product);

/* ------------------------------------------------------------------------- */

/* Misc. */
gnum_float     gpow10 (int n);
gnum_float     gpow2  (int n);
int            gcd    (int a, int b);
gnum_float     combin (int n, int k);
gnum_float     permut (int n, int k);
gnum_float     fact   (int n);

/* ------------------------------------------------------------------------- */

void stern_brocot (float val, int max_denom, int *res_num, int *res_denom);

#endif
