#ifndef GNUMERIC_MATHFUNC_H
#define GNUMERIC_MATHFUNC_H

#include "numbers.h"

/* ------------------------------------------------------------------------- */

int range_sum (const float_t *xs, int n, float_t *res);
int range_product (const float_t *xs, int n, float_t *res);

int range_sumsq (const float_t *xs, int n, float_t *res);
int range_avedev (const float_t *xs, int n, float_t *res);

int range_average (const float_t *xs, int n, float_t *res);
int range_harmonic_mean (const float_t *xs, int n, float_t *res);
int range_geometric_mean (const float_t *xs, int n, float_t *res);

int range_min (const float_t *xs, int n, float_t *res);
int range_max (const float_t *xs, int n, float_t *res);

int range_devsq (const float_t *xs, int n, float_t *res);
int range_var_pop (const float_t *xs, int n, float_t *res);
int range_var_est (const float_t *xs, int n, float_t *res);
int range_stddev_pop (const float_t *xs, int n, float_t *res);
int range_stddev_est (const float_t *xs, int n, float_t *res);
int range_skew_pop (const float_t *xs, int n, float_t *res);
int range_skew_est (const float_t *xs, int n, float_t *res);
int range_kurtosis_m3_pop (const float_t *xs, int n, float_t *res);
int range_kurtosis_m3_est (const float_t *xs, int n, float_t *res);

/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */

#endif
