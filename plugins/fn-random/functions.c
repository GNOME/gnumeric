/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-random.c:  Built in random number generation functions and functions
 *               registration
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@diku.dk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>

#include <func.h>
#include <mathfunc.h>
#include <value.h>
#include <auto-format.h>

#include <libgnome/gnome-i18n.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "gsl-pdf.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static const char *help_rand = {
	N_("@FUNCTION=RAND\n"
	   "@SYNTAX=RAND()\n"

	   "@DESCRIPTION="
	   "RAND returns a random number between zero and one ([0..1]).\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RAND() returns a random number greater than zero but less "
	   "than one.\n"
	   "\n"
	   "@SEEALSO=RANDBETWEEN")
};

static Value *
gnumeric_rand (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (random_01 ());
}

/***************************************************************************/

static const char *help_randuniform = {
	N_("@FUNCTION=RANDUNIFORM\n"
	   "@SYNTAX=RANDUNIFORM(a,b)\n"

	   "@DESCRIPTION="
	   "RANDUNIFORM returns a random variate from the uniform (flat) "
	   "distribution from a to b. The distribution is, p(x) dx = "
	   "{1 over (b-a)} dx.  If a <= x < b and 0 otherwise. a random "
	   "number between zero and one ([0..1]).\n"
	   "\n"
           "If @a > @b RANDUNIFORM returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RANDUNIFORM(1.4,4.2) returns a random number greater than or "
	   "equal to 1.4 but less than 4.2.\n"
	   "\n"
	   "@SEEALSO=RANDBETWEEN,RAND")
};

static Value *
gnumeric_randuniform (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);

	if (a > b)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (a  +  ( random_01 ()  *  (b - a) ) );
}

/***************************************************************************/

static const char *help_randexp = {
        N_("@FUNCTION=RANDEXP\n"
           "@SYNTAX=RANDEXP(b)\n"

           "@DESCRIPTION="
           "RANDEXP returns a exponentially-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDEXP(0.5).\n"
           "\n"
           "@SEEALSO=RAND,RANDBETWEEN")
};

static Value *
gnumeric_randexp (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);

        return value_new_float (random_exponential (x));
}

/***************************************************************************/

static const char *help_pdfexp = {
        N_("@FUNCTION=PDFEXP\n"
           "@SYNTAX=PDFEXP(x,mu)\n"

           "@DESCRIPTION="
           "PDFEXP returns the probability density p(x) at @x for "
	   "Exponential distribution with mean @mu. "
           "\n"
           "@EXAMPLES=\n"
           "PDFEXP(0.4,1).\n"
           "\n"
           "@SEEALSO=RANDEXP")
};

static Value *
gnumeric_pdfexp (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x  = value_get_as_float (argv[0]);
	gnum_float mu = value_get_as_float (argv[1]);

        return value_new_float (random_exponential_pdf (x, mu));
}

/***************************************************************************/

static const char *help_randpoisson = {
        N_("@FUNCTION=RANDPOISSON\n"
           "@SYNTAX=RANDPOISSON(lambda)\n"

           "@DESCRIPTION="
           "RANDPOISSON returns a poisson-distributed random number. "
           "\n"
           "If @lambda < 0 RANDPOISSON returns #NUM! error. "
           "\n"
           "@EXAMPLES=\n"
           "RANDPOISSON(3).\n"
           "\n"
           "@SEEALSO=RAND,RANDBETWEEN")
};

static Value *
gnumeric_randpoisson (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);

	if (x < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_poisson (x));
}

/***************************************************************************/

static const char *help_pdfpoisson = {
        N_("@FUNCTION=PDFPOISSON\n"
           "@SYNTAX=PDFPOISSON(k,lambda)\n"

           "@DESCRIPTION="
           "PDFPOISSON returns the probability p(k) of obtaining @k from "
	   "a Poisson distribution with mean @mu."
           "\n"
           "If @k < 0 PDFPOISSON returns #NUM! error. "
           "If @lambda < 0 PDFPOISSON returns #NUM! error. "
           "\n"
           "@EXAMPLES=\n"
           "RANDPOISSON(1,3).\n"
           "\n"
           "@SEEALSO=RANDPOISSON")
};

static Value *
gnumeric_pdfpoisson (FunctionEvalInfo *ei, Value **argv)
{
	int        k = value_get_as_int (argv[0]);
	gnum_float x = value_get_as_float (argv[1]);

	if (x < 0 || k < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_poisson_pdf (k, x));
}

/***************************************************************************/

static const char *help_randbinom = {
        N_("@FUNCTION=RANDBINOM\n"
           "@SYNTAX=RANDBINOM(p,trials)\n"

           "@DESCRIPTION="
           "RANDBINOM returns a binomially-distributed random number. "
           "\n"
           "If @p < 0 or @p > 1 RANDBINOM returns #NUM! error. "
           "If @trials < 0 RANDBINOM returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDBINOM(0.5,2).\n"
           "\n"
           "@SEEALSO=RAND,RANDBETWEEN")
};

static Value *
gnumeric_randbinom (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float p      = value_get_as_float (argv[0]);
	int        trials = value_get_as_int (argv[1]);

	if (p < 0 || p > 1 || trials < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_binomial (p, trials));
}

/***************************************************************************/

static const char *help_pdfbinom = {
        N_("@FUNCTION=PDFBINOM\n"
           "@SYNTAX=PDFBINOM(k,p,trials)\n"

           "@DESCRIPTION="
           "PDFBINOM returns the probability p(k) of obtaining @k from a "
	   "binomial distribution with parameters @p and @n. "
           "\n"
           "If @k < 0 PDFBINOM returns #NUM! error. "
           "If @p < 0 or @p > 1 PDFBINOM returns #NUM! error. "
           "If @trials < 0 PDFBINOM returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "PDFBINOM(2,0.5,4).\n"
           "\n"
           "@SEEALSO=RANDBINOM")
};

static Value *
gnumeric_pdfbinom (FunctionEvalInfo *ei, Value **argv)
{
	int        k      = value_get_as_int (argv[0]);
	gnum_float p      = value_get_as_float (argv[1]);
	int        trials = value_get_as_int (argv[2]);

	if (p < 0 || p > 1 || trials < 0 || k < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_binomial_pdf (k, p, trials));
}

/***************************************************************************/

static const char *help_randbetween = {
	N_("@FUNCTION=RANDBETWEEN\n"
	   "@SYNTAX=RANDBETWEEN(bottom,top)\n"

	   "@DESCRIPTION="
	   "RANDBETWEEN function returns a random integer number "
	   "between and including @bottom and @top."
           "\n"
	   "If @bottom or @top is non-integer, they are truncated. "
	   "If @bottom > @top, RANDBETWEEN returns #NUM! error.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RANDBETWEEN(3,7).\n"
	   "\n"
	   "@SEEALSO=RAND,RANDUNIFORM")
};

static Value *
gnumeric_randbetween (FunctionEvalInfo *ei, Value **argv)
{
        int bottom, top;
	gnum_float r;

	bottom = value_get_as_int (argv[0]);
	top    = value_get_as_int (argv[1]);
	if (bottom > top)
		return value_new_error (ei->pos, gnumeric_err_NUM );

	r = bottom + floorgnum ((top + 1.0 - bottom) * random_01 ());
	return value_new_int ((int)r);
}

/***************************************************************************/

static const char *help_randnegbinom = {
        N_("@FUNCTION=RANDNEGBINOM\n"
           "@SYNTAX=RANDNEGBINOM(p,failures)\n"

           "@DESCRIPTION="
           "RANDNEGBINOM returns a negative binomially-distributed random "
           "number. "
           "\n"
           "If @p < 0 or @p > 1, RANDNEGBINOM returns #NUM! error. "
           "If @failures RANDNEGBINOM returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDNEGBINOM(0.5,2).\n"
           "\n"
           "@SEEALSO=RAND,RANDBETWEEN")
};

static Value *
gnumeric_randnegbinom (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float p = value_get_as_float (argv[0]);
	int failures = value_get_as_int (argv[1]);

	if (p < 0 || p > 1 || failures < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_negbinom (p, failures));
}

/***************************************************************************/

static const char *help_pdfnegbinom = {
        N_("@FUNCTION=PDFNEGBINOM\n"
           "@SYNTAX=PDFNEGBINOM(k,p,failures)\n"

           "@DESCRIPTION="
           "PDFNEGBINOM returns the probability p(k) of obtaining @k from "
	   "a negative binomial distribution with parameters @p and @n."
           "\n"
           "If @k < 0 PDFNEGBINOM returns #NUM! error. "
           "If @p < 0 or @p > 1, PDFNEGBINOM returns #NUM! error. "
           "If @failures PDFNEGBINOM returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "PDFNEGBINOM(1,0.5,2).\n"
           "\n"
           "@SEEALSO=RANDNEGBINOM")
};

static Value *
gnumeric_pdfnegbinom (FunctionEvalInfo *ei, Value **argv)
{
	int        k        = value_get_as_int (argv[0]);
	gnum_float p        = value_get_as_float (argv[1]);
	int        failures = value_get_as_int (argv[2]);

	if (p < 0 || p > 1 || failures < 0 || k < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_negative_binomial_pdf (k, p, failures));
}

/***************************************************************************/

static const char *help_randbernoulli = {
        N_("@FUNCTION=RANDBERNOULLI\n"
           "@SYNTAX=RANDBERNOULLI(p)\n"

           "@DESCRIPTION="
           "RANDBERNOULLI returns a Bernoulli-distributed random number. "
           "\n"
           "If @p < 0 or @p > 1 RANDBERNOULLI returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDBERNOULLI(0.5).\n"
           "\n"
           "@SEEALSO=RAND,RANDBETWEEN")
};

static Value *
gnumeric_randbernoulli (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float p = value_get_as_float (argv[0]);

	if (p < 0 || p > 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_bernoulli (p));
}

/***************************************************************************/

static const char *help_pdfbernoulli = {
        N_("@FUNCTION=PDFBERNOULLI\n"
           "@SYNTAX=PDFBERNOULLI(k,p)\n"

           "@DESCRIPTION="
           "PDFBERNOULLI returns the probability p(k) of obtaining @k "
	   "from a Bernoulli distribution with probability parameter @p. "
           "\n"
           "If @k != 0 and @k != 1 PDFBERNOULLI returns #NUM! error. "
           "If @p < 0 or @p > 1 PDFBERNOULLI returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "PDFBERNOULLI(0,0.5).\n"
           "\n"
           "@SEEALSO=RANDBERBOULLI")
};

static Value *
gnumeric_pdfbernoulli (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float k = value_get_as_int (argv[0]);
	gnum_float p = value_get_as_float (argv[1]);

	if (p < 0 || p > 1 || (k != 0 && k != 1))
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_bernoulli_pdf (k, p));
}

/***************************************************************************/

static const char *help_randgaussian = {
        N_("@FUNCTION=RANDGAUSSIAN\n"
           "@SYNTAX=RANDGAUSSIAN(mean,stdev)\n"

           "@DESCRIPTION="
           "RANDGAUSSIAN returns a gaussian-distributed random number. "
           "\n"
           "If @stdev < 0 RANDGAUSSIAN returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDGAUSSIAN(0,1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randgaussian (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float mean  = value_get_as_float (argv[0]);
	gnum_float stdev = value_get_as_float (argv[1]);

	if (stdev < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (stdev * random_normal () + mean);
}

/***************************************************************************/

static const char *help_randcauchy = {
        N_("@FUNCTION=RANDCAUCHY\n"
           "@SYNTAX=RANDCAUCHY(a)\n"

           "@DESCRIPTION="
           "RANDCAUCHY returns a cauchy-distributed random number with "
	   "scale parameter a. The Cauchy distribution is also known as the "
	   "Lorentz distribution. "
           "\n"
           "If @a < 0 RANDCAUCHY returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDCAUCHY(1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randcauchy (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);

	if (a < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_cauchy (a));
}

/***************************************************************************/

static const char *help_pdfcauchy = {
        N_("@FUNCTION=PDFCAUCHY\n"
           "@SYNTAX=PDFCAUCHY(x,a)\n"

           "@DESCRIPTION="
           "PDFCAUCHY returns the probability density p(x) at x for @a "
	   "Cauchy distribution with scale parameter @a. "
           "\n"
           "If @a < 0 PDFCAUCHY returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "PDFCAUCHY(0.43,1).\n"
           "\n"
           "@SEEALSO=RANDCAUCHY")
};

static Value *
gnumeric_pdfcauchy (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);
	gnum_float a = value_get_as_float (argv[1]);

	if (a < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_cauchy_pdf (x, a));
}

/***************************************************************************/

static const char *help_randlognorm = {
        N_("@FUNCTION=RANDLOGNORM\n"
           "@SYNTAX=RANDLOGNORM(zeta,sigma)\n"

           "@DESCRIPTION="
           "RANDLOGNORM returns a lognormal-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDLOGNORM(1,2).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randlognorm (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float zeta  = value_get_as_float (argv[0]);
	gnum_float sigma = value_get_as_float (argv[1]);

        return value_new_float (random_lognormal (zeta, sigma));
}

/***************************************************************************/

static const char *help_randweibull = {
        N_("@FUNCTION=RANDWEIBULL\n"
           "@SYNTAX=RANDWEIBULL(a,b)\n"

           "@DESCRIPTION="
           "RANDWEIBULL returns a Weibull-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDWEIBULL(1,2).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randweibull (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);

        return value_new_float (random_weibull (a, b));
}

/***************************************************************************/

static const char *help_pdfweibull = {
        N_("@FUNCTION=PDFWEIBULL\n"
           "@SYNTAX=PDFWEIBULL(x,a,b)\n"

           "@DESCRIPTION="
           "PDFWEIBULL returns the probability density p(x) at @x for a "
	   "Weibull distribution with scale @a and exponent @b. "
           "\n"
           "@EXAMPLES=\n"
           "PDFWEIBULL(0.7,1,2).\n"
           "\n"
           "@SEEALSO=RANDWEIBULL")
};

static Value *
gnumeric_pdfweibull (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);
	gnum_float a = value_get_as_float (argv[1]);
	gnum_float b = value_get_as_float (argv[2]);

        return value_new_float (random_weibull_pdf (x, a, b));
}

/***************************************************************************/

static const char *help_randlaplace = {
        N_("@FUNCTION=RANDLAPLACE\n"
           "@SYNTAX=RANDLAPLACE(a)\n"

           "@DESCRIPTION="
           "RANDLAPLACE returns a Laplace-distributed random number. Laplace "
	   "distribution is also known as two-sided exponential probability "
	   "distribution. "
           "\n"
           "@EXAMPLES=\n"
           "RANDLAPLACE(1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randlaplace (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);

        return value_new_float (random_laplace (a));
}

/***************************************************************************/

static const char *help_pdflaplace = {
        N_("@FUNCTION=PDFLAPLACE\n"
           "@SYNTAX=PDFLAPLACE(x,a)\n"

           "@DESCRIPTION="
           "PDFLAPLACE returns the probability density p(x) at @x for "
	   "Laplace distribution with mean @a. "
           "\n"
           "@EXAMPLES=\n"
           "PDFLAPLACE(0.4,1).\n"
           "\n"
           "@SEEALSO=RANDLAPLACE")
};

static Value *
gnumeric_pdflaplace (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);
	gnum_float a = value_get_as_float (argv[1]);

        return value_new_float (random_laplace_pdf (x, a));
}

/***************************************************************************/

static const char *help_randrayleigh = {
        N_("@FUNCTION=RANDRAYLEIGH\n"
           "@SYNTAX=RANDRAYLEIGH(sigma)\n"

           "@DESCRIPTION="
           "RANDRAYLEIGH returns a Rayleigh-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDRAYLEIGH(1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randrayleigh (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float sigma = value_get_as_float (argv[0]);

        return value_new_float (random_rayleigh (sigma));
}

/***************************************************************************/

static const char *help_pdfrayleigh = {
        N_("@FUNCTION=PDFRAYLEIGH\n"
           "@SYNTAX=PDFRAYLEIGH(x,sigma)\n"

           "@DESCRIPTION="
           "PDFRAYLEIGH returns the probability density p(x) at @x for a "
	   "Rayleigh distribution with scale parameter @sigma."
           "\n"
           "@EXAMPLES=\n"
           "PDFRAYLEIGH(0.4,1).\n"
           "\n"
           "@SEEALSO=RANDRAYLEIGH")
};

static Value *
gnumeric_pdfrayleigh (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x     = value_get_as_float (argv[0]);
	gnum_float sigma = value_get_as_float (argv[1]);

        return value_new_float (random_rayleigh_pdf (x, sigma));
}

/***************************************************************************/

static const char *help_randrayleightail = {
        N_("@FUNCTION=RANDRAYLEIGHTAIL\n"
           "@SYNTAX=RANDRAYLEIGHTAIL(a,sigma)\n"

           "@DESCRIPTION="
           "RANDRAYLEIGHTAIL returns  a random variate from the tail of the "
	   "Rayleigh distribution with scale parameter sigma and a lower limit "
	   "of a. The distribution is, p(x) dx = {x over sigma^2} exp "
	   "((a^2 - x^2) /(2 sigma^2)) dx, for x > a. "
           "\n"
           "@EXAMPLES=\n"
           "RANDRAYLEIGHTAIL(0.3,1).\n"
           "\n"
           "@SEEALSO=RAND,RANDRAYLEIGH")
};

static Value *
gnumeric_randrayleightail (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a     = value_get_as_float (argv[0]);
	gnum_float sigma = value_get_as_float (argv[1]);

        return value_new_float (random_rayleigh_tail (a, sigma));
}

/***************************************************************************/

static const char *help_randgamma = {
        N_("@FUNCTION=RANDGAMMA\n"
           "@SYNTAX=RANDGAMMA(a,b)\n"

           "@DESCRIPTION="
           "RANDGAMMA returns a Gamma-distributed random number. "
           "\n"
           "If @a <= 0 RANDGAMMA returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDGAMMA(1,2).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randgamma (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);

	if (a <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_gamma (a, b));
}

/***************************************************************************/

static const char *help_pdfgamma = {
        N_("@FUNCTION=PDFGAMMA\n"
           "@SYNTAX=PDFGAMMA(x,a,b)\n"

           "@DESCRIPTION="
           "PDFGAMMA returns the probability density p(x) at @x for "
	   "Gamma distribution with parameters @a and @b. "
           "\n"
           "If @a <= 0 PDFGAMMA returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "PDFGAMMA(0.7,0.4,1).\n"
           "\n"
           "@SEEALSO=RANDGAMMA")
};

static Value *
gnumeric_pdfgamma (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);
	gnum_float a = value_get_as_float (argv[1]);
	gnum_float b = value_get_as_float (argv[2]);

	if (a <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (random_gamma_pdf (x, a, b));
}

/***************************************************************************/

static const char *help_randpareto = {
        N_("@FUNCTION=RANDPARETO\n"
           "@SYNTAX=RANDPARETO(a,b)\n"

           "@DESCRIPTION="
           "RANDPARETO returns a Pareto-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDPARETO(1,2).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randpareto (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);

        return value_new_float (random_pareto (a, b));
}

/***************************************************************************/

static const char *help_randfdist = {
        N_("@FUNCTION=RANDFDIST\n"
           "@SYNTAX=RANDFDIST(nu1,nu2)\n"

           "@DESCRIPTION="
           "RANDFDIST returns a F-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDFDIST(1,2).\n"
           "\n"
           "@SEEALSO=RAND,RANDGAMMA")
};

static Value *
gnumeric_randfdist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float nu1 = value_get_as_float (argv[0]);
	gnum_float nu2 = value_get_as_float (argv[1]);

        return value_new_float (random_fdist (nu1, nu2));
}

/***************************************************************************/

static const char *help_randbeta = {
        N_("@FUNCTION=RANDBETA\n"
           "@SYNTAX=RANDBETA(a,b)\n"

           "@DESCRIPTION="
           "RANDBETA returns a Beta-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDBETA(1,2).\n"
           "\n"
           "@SEEALSO=RAND,RANDGAMMA")
};

static Value *
gnumeric_randbeta (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);

        return value_new_float (random_beta (a, b));
}

/***************************************************************************/

static const char *help_pdfbeta = {
        N_("@FUNCTION=PDFBETA\n"
           "@SYNTAX=PDFBETA(x,a,b)\n"

           "@DESCRIPTION="
           "PDFBETA returns the probability density p(x) at @x for "
	   "Beta distribution with parameters @a and @b. "
           "\n"
           "@EXAMPLES=\n"
           "PDFBETA(0.7,0.4,1).\n"
           "\n"
           "@SEEALSO=RANDBETA")
};

static Value *
gnumeric_pdfbeta (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);
	gnum_float a = value_get_as_float (argv[1]);
	gnum_float b = value_get_as_float (argv[2]);

        return value_new_float (random_beta_pdf (x, a, b));
}

/***************************************************************************/

static const char *help_randlogistic = {
        N_("@FUNCTION=RANDLOGISTIC\n"
           "@SYNTAX=RANDLOGISTIC(a)\n"

           "@DESCRIPTION="
           "RANDLOGISTIC returns a logistic-distributed random number.  The "
	   "distribution function is, p(x) dx = { exp(-x/a) over a "
	   "(1 + exp(-x/a))^2 } dx for -infty < x < +infty."
           "\n"
           "@EXAMPLES=\n"
           "RANDLOGISTIC(1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randlogistic (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);

        return value_new_float (random_logistic (a));
}

/***************************************************************************/

static const char *help_randgeom = {
        N_("@FUNCTION=RANDGEOM\n"
           "@SYNTAX=RANDGEOM(p)\n"

           "@DESCRIPTION="
           "RANDGEOM returns a geometric-distributed random number. The "
	   "number of independent trials with probability @p until the "
	   "first success. The probability distribution for geometric "
	   "variates is, p(k) =  p (1-p)^(k-1), for k >= 1. "
           "\n"
           "If @p < 0 or @p > 1 RANDGEOM returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDGEOM(0.4).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randgeom (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float p = value_get_as_float (argv[0]);

	if (p < 0 || p > 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_int (random_geometric (p));
}

/***************************************************************************/

static const char *help_randhyperg = {
        N_("@FUNCTION=RANDHYPERG\n"
           "@SYNTAX=RANDHYPERG(n1,n2,t)\n"

           "@DESCRIPTION="
           "RANDHYPERG returns a hypergeometric-distributed random number. "
	   "The probability distribution for hypergeometric random variates "
	   "is, p(k) =  C(n_1,k) C(n_2, t-k) / C(n_1 + n_2,k), where C(a,b) "
	   "= a!/(b!(a-b)!). The domain of k is max(0,t-n_2), ..., max(t,n_1)."
           "\n"
           "@EXAMPLES=\n"
           "RANDHYPERG(21,1,9).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randhyperg (FunctionEvalInfo *ei, Value **argv)
{
	unsigned int n1 = value_get_as_int (argv[0]);
	unsigned int n2 = value_get_as_int (argv[1]);
	unsigned int t = value_get_as_int (argv[2]);

        return value_new_int (random_hypergeometric (n1, n2, t));
}

/***************************************************************************/

static const char *help_randlog = {
        N_("@FUNCTION=RANDLOG\n"
           "@SYNTAX=RANDLOG(p)\n"

           "@DESCRIPTION="
           "RANDLOG returns a logarithmic-distributed random number. "
           "\n"
           "If @p < 0 or @p > 1 RANDLOG returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDHYPERG(0.72).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randlog (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float p = value_get_as_float (argv[0]);

	if (p < 0 || p > 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_int (random_logarithmic (p));
}

/***************************************************************************/

static const char *help_randchisq = {
        N_("@FUNCTION=RANDCHISQ\n"
           "@SYNTAX=RANDCHISQ(nu)\n"

           "@DESCRIPTION="
           "RANDCHISQ returns a Chi-Square-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDCHISQ(0.5).\n"
           "\n"
           "@SEEALSO=RAND,RANDGAMMA")
};

static Value *
gnumeric_randchisq (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float nu = value_get_as_float (argv[0]);

        return value_new_float (random_chisq (nu));
}

/***************************************************************************/

static const char *help_randtdist = {
        N_("@FUNCTION=RANDTDIST\n"
           "@SYNTAX=RANDTDIST(nu)\n"

           "@DESCRIPTION="
           "RANDTDIST returns a T-distributed random number. "
           "\n"
           "@EXAMPLES=\n"
           "RANDTDIST(0.5).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randtdist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float nu = value_get_as_float (argv[0]);

        return value_new_float (random_tdist (nu));
}

/***************************************************************************/

static const char *help_randgumbel = {
        N_("@FUNCTION=RANDGUMBEL\n"
           "@SYNTAX=RANDGUMBEL(a,b[,type])\n"

           "@DESCRIPTION="
           "RANDGUMBEL returns a Type I or Type II Gumbel-distributed "
	   "random number. @type is either 1 or 2 and specifies the type of "
	   "the distribution (Type I or Type II). "
           "\n"
	   "If @type is either 1 or 2, RANDGUMBEL returns #NUM! error. "
	   "If @type is omitted, Type I is assumed. "
           "\n"
           "@EXAMPLES=\n"
           "RANDGUMBEL(0.5,1,2).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randgumbel (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);
	int type     = (argv[2] == NULL) ? 1 : value_get_as_int (argv[2]);

	if (type != 1 && type != 2)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (type == 1)
		return value_new_float (random_gumbel1 (a, b));
	else
		return value_new_float (random_gumbel2 (a, b));
}

/***************************************************************************/

static const char *help_randlevy = {
        N_("@FUNCTION=RANDLEVY\n"
           "@SYNTAX=RANDLEVY(c,alpha[,beta])\n"

           "@DESCRIPTION="
           "RANDLEVY returns a Levy-distributed random number. If @beta is "
	   "ommitted, it is assumed to be 0.\n"
	   "For alpha = 1, beta=0, we get the Lorentz distribution. "
	   "For alpha = 2, beta=0, we get the Gaussian distribution. "
           "\n"
	   "If @alpha <= 0 or @alpha > 2, RANDLEVY returns #NUM! error. "
	   "If @beta < -1 or @beta > 1, RANDLEVY returns #NUM! error. "
           "\n"
           "@EXAMPLES=\n"
           "RANDLEVY(0.5,0.1,1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randlevy (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float c     = value_get_as_float (argv[0]);
	gnum_float alpha = value_get_as_float (argv[1]);
	gnum_float beta  = argv[2] == NULL ? 0 : value_get_as_float (argv[1]);

	if (alpha <= 0 || alpha > 2 || beta < -1 || beta > 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (random_levy_skew (c, alpha, beta));
}

/***************************************************************************/

static const char *help_randexppow = {
        N_("@FUNCTION=RANDEXPPOW\n"
           "@SYNTAX=RANDEXPPOW(a,b)\n"

           "@DESCRIPTION="
           "RANDLEVY returns a random variate from the exponential power "
	   "distribution with scale parameter @a and exponent @b. The "
	   "distribution is, p(x) dx = {1 over 2 a Gamma(1+1/b)} exp(-|x/a|^b)"
	   " dx, for x >= 0. For b = 1 this reduces to the Laplace "
	   "distribution. For b = 2 it has the same form as a gaussian "
	   "distribution, but with a = sqrt{2} sigma. "
           "\n"
           "@EXAMPLES=\n"
           "RANDEXPPOW(0.5,0.1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randexppow (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);

	return value_new_float (random_exppow (a, b));
}

/***************************************************************************/

static const char *help_pdfexppow = {
        N_("@FUNCTION=PDFEXPPOW\n"
           "@SYNTAX=PDFEXPPOW(x,a,b)\n"

           "@DESCRIPTION="
           "PDFEXPPOW returns the probability density p(x) at @x for "
	   "Exponential Power distribution with scale parameter @a and "
	   "exponent @b. "
           "\n"
           "@EXAMPLES=\n"
           "PDFEXPPOW(0.4,1,2).\n"
           "\n"
           "@SEEALSO=RANDEXPPOW")
};

static Value *
gnumeric_pdfexppow (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);
	gnum_float a = value_get_as_float (argv[1]);
	gnum_float b = value_get_as_float (argv[2]);

        return value_new_float (random_exppow_pdf (x, a, b));
}

/***************************************************************************/

static const char *help_randlandau = {
        N_("@FUNCTION=RANDLANDAU\n"
           "@SYNTAX=RANDLANDAU()\n"

           "@DESCRIPTION="
           "RANDLANDAU returns a random variate from the Landau distribution. "
	   "The probability distribution for Landau random variates is "
	   "defined analytically by the complex integral, p(x) = (1/(2 pi i)) "
	   "int_{c-i infty}^{c+i infty} ds exp(s log(s) + x s). For numerical "
	   "purposes it is more convenient to use the following equivalent "
	   "form of the integral, p(x) = (1/pi) int_0^ infty dt "
	   "exp(-t log(t) - x t) sin(pi t)."
           "\n"
           "@EXAMPLES=\n"
           "RANDLANDAU().\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randlandau (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (random_landau ());
}

/***************************************************************************/

static const char *help_pdflandau = {
        N_("@FUNCTION=PDFLANDAU\n"
           "@SYNTAX=PDFLANDAU(x)\n"

           "@DESCRIPTION="
           "PDFLANDAU returns the probability density p(x) at @x for the "
	   "Landau distribution using an approximation method. "
           "\n"
           "@EXAMPLES=\n"
           "PDFLANDAU(0.34).\n"
           "\n"
           "@SEEALSO=RANDLANDAU")
};

static Value *
gnumeric_pdflandau (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x = value_get_as_float (argv[0]);

	return value_new_float (random_landau_pdf (x));
}

/***************************************************************************/

static const char *help_randgaussiantail = {
        N_("@FUNCTION=RANDGAUSSIANTAIL\n"
           "@SYNTAX=RANDGAUSSIANTAIL(a,sigma)\n"

           "@DESCRIPTION="
           "RANDGAUSSIANTAIL returns a random variates from the upper tail "
	   "of a Gaussian distribution with standard deviation sigma. The "
	   "values returned are larger than the lower limit a, which must be "
	   "positive. The method is based on Marsaglia's famous "
	   "rectangle-wedge-tail algorithm (Ann Math Stat 32, 894-899 "
	   "(1961)), with this aspect explained in Knuth, v2, 3rd ed, p139, "
	   "586 (exercise 11)."
	   "\n"
	   "The probability distribution for Gaussian tail random variates "
	   "is, p(x) dx = {1 over N(a;sigma)} exp (- x^2/(2 sigma^2)) dx, "
	   "for x > a where N(a;sigma) is the normalization constant, "
	   "N(a;sigma) = (1/2) erfc(a / sqrt(2 sigma^2)). "
           "\n"
           "@EXAMPLES=\n"
           "RANDGAUSSIANTAIL(0.5,0.1).\n"
           "\n"
           "@SEEALSO=RAND")
};

static Value *
gnumeric_randgaussiantail (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float a     = value_get_as_float (argv[0]);
	gnum_float sigma = value_get_as_float (argv[1]);

	return value_new_float (random_gaussian_tail (a, sigma));
}

/***************************************************************************/

const ModulePluginFunctionInfo random_functions[] = {
        { "pdfbernoulli", "ff", N_("k,p"),   &help_pdfbernoulli,
	  gnumeric_pdfbernoulli, NULL, NULL, NULL },
        { "pdfbeta", "fff", N_("x,a,b"),         &help_pdfbeta,
	  gnumeric_pdfbeta, NULL, NULL, NULL },
        { "pdfbinom", "fff", N_("k,p,trials"), &help_pdfbinom,
	  gnumeric_pdfbinom, NULL, NULL, NULL },
        { "pdfcauchy", "ff", N_("x,a"),   &help_pdfcauchy,
	  gnumeric_pdfcauchy, NULL, NULL, NULL },
        { "pdfexp", "ff", N_("x,mu"),         &help_pdfexp,
	  gnumeric_pdfexp, NULL, NULL, NULL },
        { "pdfexppow", "fff", N_("x,a,b"),         &help_pdfexppow,
	  gnumeric_pdfexppow, NULL, NULL, NULL },
        { "pdfgamma", "fff", N_("x,a,b"),         &help_pdfgamma,
	  gnumeric_pdfgamma, NULL, NULL, NULL },
        { "pdflandau", "f", N_("x"), &help_pdflandau,
	  gnumeric_pdflandau, NULL, NULL, NULL },
        { "pdflaplace", "ff", N_("x,a"), &help_pdflaplace,
	  gnumeric_pdflaplace, NULL, NULL, NULL },
        { "pdfnegbinom", "fff", N_("k,p,failures"), &help_pdfnegbinom,
	  gnumeric_pdfnegbinom, NULL, NULL, NULL },
        { "pdfpoisson", "ff", N_("k,lambda"), &help_pdfpoisson,
	  gnumeric_pdfpoisson, NULL, NULL, NULL },
        { "pdfrayleigh", "ff", N_("x,sigma"), &help_pdfrayleigh,
	  gnumeric_pdfrayleigh, NULL, NULL, NULL },
        { "pdfweibull", "fff", N_("x,a,b"), &help_pdfweibull,
	  gnumeric_pdfweibull, NULL, NULL, NULL },
	{ "rand",    "", "",           &help_rand,
	  gnumeric_rand, NULL, NULL, NULL },
        { "randbernoulli", "f", N_("p"),   &help_randbernoulli,
	  gnumeric_randbernoulli, NULL, NULL, NULL },
        { "randbeta", "ff", N_("a,b"),   &help_randbeta,
	  gnumeric_randbeta, NULL, NULL, NULL },
        { "randbetween", "ff", N_("bottom,top"), &help_randbetween,
	  gnumeric_randbetween, NULL, NULL, NULL },
        { "randbinom", "ff", N_("p,trials"), &help_randbinom,
	  gnumeric_randbinom, NULL, NULL, NULL },
        { "randcauchy", "f", N_("a"),   &help_randcauchy,
	  gnumeric_randcauchy, NULL, NULL, NULL },
        { "randchisq", "f", N_("nu"),   &help_randchisq,
	  gnumeric_randchisq, NULL, NULL, NULL },
        { "randexp", "f", N_("b"),         &help_randexp,
	  gnumeric_randexp, NULL, NULL, NULL },
        { "randexppow", "ff", N_("a,b"),         &help_randexppow,
	  gnumeric_randexppow, NULL, NULL, NULL },
        { "randfdist", "ff", N_("nu1,nu2"),      &help_randfdist,
	  gnumeric_randfdist, NULL, NULL, NULL },
        { "randgamma", "ff", N_("a,b"),    &help_randgamma,
	  gnumeric_randgamma, NULL, NULL, NULL },
        { "randgaussiantail", "ff", N_("a,sigma"),    &help_randgaussiantail,
	  gnumeric_randgaussiantail, NULL, NULL, NULL },
        { "randgeom", "f", N_("p"),    &help_randgeom,
	  gnumeric_randgeom, NULL, NULL, NULL },
        { "randgumbel", "ff|f", N_("a,b[,type]"),    &help_randgumbel,
	  gnumeric_randgumbel, NULL, NULL, NULL },
        { "randhyperg", "fff", N_("n1,n2,t"),    &help_randhyperg,
	  gnumeric_randhyperg, NULL, NULL, NULL },
        { "randlandau", "", N_(""), &help_randlandau,
	  gnumeric_randlandau, NULL, NULL, NULL },
        { "randlaplace", "f", N_("a"), &help_randlaplace,
	  gnumeric_randlaplace, NULL, NULL, NULL },
        { "randlevy", "ff|f", N_("c,alpha[,beta]"), &help_randlevy,
	  gnumeric_randlevy, NULL, NULL, NULL },
        { "randlog", "f", N_("p"), &help_randlog,
	  gnumeric_randlog, NULL, NULL, NULL },
        { "randlogistic", "f", N_("a"), &help_randlogistic,
	  gnumeric_randlogistic, NULL, NULL, NULL },
        { "randlognorm", "ff", N_("zeta,sigma"), &help_randlognorm,
	  gnumeric_randlognorm, NULL, NULL, NULL },
        { "randnegbinom", "ff", N_("p,failures"), &help_randnegbinom,
	  gnumeric_randnegbinom, NULL, NULL, NULL },
        { "randgaussian", "ff", N_("mean,stdev"), &help_randgaussian,
	  gnumeric_randgaussian, NULL, NULL, NULL },
        { "randpareto", "ff", N_("a,b"), &help_randpareto,
	  gnumeric_randpareto, NULL, NULL, NULL },
        { "randpoisson", "f", N_("lambda"), &help_randpoisson,
	  gnumeric_randpoisson, NULL, NULL, NULL },
        { "randrayleigh", "f", N_("sigma"), &help_randrayleigh,
	  gnumeric_randrayleigh, NULL, NULL, NULL },
        { "randrayleightail", "ff", N_("a,sigma"), &help_randrayleightail,
	  gnumeric_randrayleightail, NULL, NULL, NULL },
        { "randtdist", "f", N_("nu"), &help_randtdist,
	  gnumeric_randtdist, NULL, NULL, NULL },
        { "randuniform", "ff", N_("a,b"), &help_randuniform,
	  gnumeric_randuniform, NULL, NULL, NULL },
        { "randweibull", "ff", N_("a,b"), &help_randweibull,
	  gnumeric_randweibull, NULL, NULL, NULL },
        {NULL}
};
