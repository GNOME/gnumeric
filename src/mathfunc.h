#ifndef GNUMERIC_MATHFUNC_H
#define GNUMERIC_MATHFUNC_H

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

#endif
