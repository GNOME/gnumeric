/*
 * fn-random.c:  Built in random number generation functions and functions
 *               registration
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@gnome.org)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>
#include <mathfunc.h>
#include <gnm-random.h>
#include <rangefunc.h>
#include <value.h>
#include <expr.h>
#include <sheet.h>
#include <cell.h>
#include <collect.h>
#include <gnm-i18n.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GnmFuncHelp const help_rand[] = {
        { GNM_FUNC_HELP_NAME, F_("RAND:a random number between zero and one")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=RAND()" },
        { GNM_FUNC_HELP_EXAMPLES, "=RAND()" },
        { GNM_FUNC_HELP_SEEALSO, "RANDBETWEEN" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_rand (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (random_01 ());
}

/***************************************************************************/

static GnmFuncHelp const help_randuniform[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDUNIFORM:random variate from the uniform distribution from @{a} to @{b}") },
        { GNM_FUNC_HELP_ARG, F_("a:lower limit of the uniform distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:upper limit of the uniform distribution") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{a} > @{b} RANDUNIFORM returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDUNIFORM(1.4,4.2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDUNIFORM(1.4,4.2)" },
        { GNM_FUNC_HELP_SEEALSO, "RANDBETWEEN,RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randuniform (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

	if (a > b)
		return value_new_error_NUM (ei->pos);

	return value_new_float (a  +  ( random_01 ()  *  (b - a) ) );
}

/***************************************************************************/

static GnmFuncHelp const help_randdiscrete[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDDISCRETE:random variate from a finite discrete distribution") },
        { GNM_FUNC_HELP_ARG, F_("val_range:possible values of the random variable") },
        { GNM_FUNC_HELP_ARG, F_("prob_range:probabilities of the corresponding values in @{val_range},"
				" defaults to equal probabilities") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("RANDDISCRETE returns one of the values in the @{val_range}. "
					"The probabilities for each value are given in the @{prob_range}.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the sum of all values in @{prob_range} is not one, RANDDISCRETE returns #NUM!") },
 	{ GNM_FUNC_HELP_NOTE, F_("If @{val_range} and @{prob_range} are not the same size, RANDDISCRETE returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{val_range} or @{prob_range} is not a range, RANDDISCRETE returns #VALUE!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=RANDDISCRETE({1;3;5;7})" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDDISCRETE({1;3;5;7})" },
        { GNM_FUNC_HELP_SEEALSO, "RANDBETWEEN,RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randdiscrete (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue *res = NULL;
	gnm_float *values = NULL;
	gnm_float *probs = NULL;
	int nv, np, i;
	gnm_float p;

	values = collect_floats_value (argv[0], ei->pos,
				       COLLECT_IGNORE_STRINGS |
				       COLLECT_IGNORE_BOOLS |
				       COLLECT_IGNORE_BLANKS,
				       &nv, &res);
	if (res)
		goto out;

	if (argv[1]) {
		probs = collect_floats_value (argv[1], ei->pos,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS,
					      &np, &res);
		if (res)
			goto out;
	} else
		np = nv;

	if (nv < 1 || nv != np)
		goto error;

	if (probs) {
		gnm_float pmin, psum;

		gnm_range_min (probs, np, &pmin);
		if (pmin < 0)
			goto error;

		gnm_range_sum (probs, np, &psum);
		if (gnm_abs (psum - 1) > GNM_const(1e-10))
			goto error;
	}

	if (probs) {
		p = random_01 ();
		for (i = 0; i < np; i++) {
			p -= probs[i];
			if (p < 0)
				break;
		}
	} else {
		/* Uniform.  */
		i = gnm_random_uniform_int (nv);
	}

	/* MIN is needed because of the sum grace.  */
	res = value_new_float (values[MIN (i, nv - 1)]);

out:
	g_free (values);
	g_free (probs);
	return res;

error:
	res = value_new_error_NUM (ei->pos);
	goto out;
}

/***************************************************************************/

static GnmFuncHelp const help_randexp[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDEXP:random variate from an exponential distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:parameter of the exponential distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDEXP(0.5)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDEXP(0.5)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDBETWEEN" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randexp (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);

        return value_new_float (random_exponential (x));
}

/***************************************************************************/

static GnmFuncHelp const help_randpoisson[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDPOISSON:random variate from a Poisson distribution") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xbb:parameter of the Poisson distribution") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{\xce\xbb} < 0 RANDPOISSON returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDPOISSON(30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDPOISSON(30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDPOISSON(2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDBETWEEN" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randpoisson (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);

	if (x < 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_poisson (x));
}

/***************************************************************************/

static GnmFuncHelp const help_randbinom[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDBINOM:random variate from a binomial distribution") },
        { GNM_FUNC_HELP_ARG, F_("p:probability of success in a single trial") },
        { GNM_FUNC_HELP_ARG, F_("n:number of trials") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 RANDBINOM returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} < 0 RANDBINOM returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBINOM(0.5,10)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBINOM(0.5,10)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDBETWEEN" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randbinom (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p      = value_get_as_float (argv[0]);
	gnm_float trials = value_get_as_float (argv[1]);

	if (p < 0 || p > 1 || trials < 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_binomial (p, gnm_floor (trials)));
}

/***************************************************************************/

static GnmFuncHelp const help_randbetween[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDBETWEEN:a random integer number between and "
				 "including @{bottom} and @{top}") },
        { GNM_FUNC_HELP_ARG, F_("bottom:lower limit") },
        { GNM_FUNC_HELP_ARG, F_("top:upper limit") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{bottom} > @{top}, RANDBETWEEN returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBETWEEN(3,7)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBETWEEN(3,7)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDUNIFORM" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randbetween (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float bottom = value_get_as_float (argv[0]);
	gnm_float top = value_get_as_float (argv[1]);

	if (bottom > top)
		return value_new_error_NUM (ei->pos);

	// Bottom is ceiled, top floored.  We do this on the limits, not the
	// resulting random number, in order to ensure all integers in the
	// range have the same probability.  Tests show that Excel 2013 does
	// the same thing.
	bottom = gnm_ceil (bottom);
	top = gnm_floor (top);
	return value_new_float (gnm_random_uniform_integer (bottom, top));
}

/***************************************************************************/

static GnmFuncHelp const help_randnegbinom[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDNEGBINOM:random variate from a negative binomial distribution") },
        { GNM_FUNC_HELP_ARG, F_("p:probability of success in a single trial") },
        { GNM_FUNC_HELP_ARG, F_("n:number of failures") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 RANDNEGBINOM returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} < 1 RANDNEGBINOM returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDNEGBINOM(0.5,5)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDBETWEEN" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randnegbinom (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);
	gnm_float failures = value_get_as_float (argv[1]);

	if (p < 0 || p > 1 || failures < 1)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_negbinom (p, gnm_floor (failures)));
}

/***************************************************************************/

static GnmFuncHelp const help_randbernoulli[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDBERNOULLI:random variate from a Bernoulli distribution") },
        { GNM_FUNC_HELP_ARG, F_("p:probability of success") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 RANDBERNOULLI returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBERNOULLI(0.5)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBERNOULLI(0.5)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBERNOULLI(0.5)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDBETWEEN" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randbernoulli (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);

	if (p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_bernoulli (p));
}

/***************************************************************************/

static GnmFuncHelp const help_randnorm[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDNORM:random variate from a normal distribution") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xbc:mean of the distribution") },
        { GNM_FUNC_HELP_ARG, F_("\xcf\x83:standard deviation of the distribution") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{\xcf\x83} < 0, RANDNORM returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDNORM(0,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDNORM(0,1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randnorm (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float mean  = value_get_as_float (argv[0]);
	gnm_float stdev = value_get_as_float (argv[1]);

	if (stdev < 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (stdev * random_normal () + mean);
}

/***************************************************************************/

static GnmFuncHelp const help_randcauchy[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDCAUCHY:random variate from a Cauchy or Lorentz distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:scale parameter of the distribution") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{a} < 0 RANDCAUCHY returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDCAUCHY(1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDCAUCHY(1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randcauchy (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);

	if (a < 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_cauchy (a));
}

/***************************************************************************/

static GnmFuncHelp const help_randlognorm[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDLOGNORM:random variate from a lognormal distribution") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xb6:parameter of the lognormal distribution") },
	{ GNM_FUNC_HELP_ARG, F_("\xcf\x83:standard deviation of the distribution") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{\xcf\x83} < 0, RANDLOGNORM returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLOGNORM(1,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLOGNORM(1,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randlognorm (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float zeta  = value_get_as_float (argv[0]);
	gnm_float sigma = value_get_as_float (argv[1]);

        return value_new_float (random_lognormal (zeta, sigma));
}

/***************************************************************************/

static GnmFuncHelp const help_randweibull[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDWEIBULL:random variate from a Weibull distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:scale parameter of the Weibull distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:shape parameter of the Weibull distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDWEIBULL(1,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDWEIBULL(1,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randweibull (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

        return value_new_float (random_weibull (a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_randlaplace[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDLAPLACE:random variate from a Laplace distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:parameter of the Laplace distribution") },
	{ GNM_FUNC_HELP_EXAMPLES, "=RANDLAPLACE(1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLAPLACE(1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randlaplace (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);

        return value_new_float (random_laplace (a));
}

/***************************************************************************/

static GnmFuncHelp const help_randrayleigh[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDRAYLEIGH:random variate from a Rayleigh distribution") },
	{ GNM_FUNC_HELP_ARG, F_("\xcf\x83:scale parameter of the Rayleigh distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDRAYLEIGH(1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDRAYLEIGH(1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randrayleigh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float sigma = value_get_as_float (argv[0]);

        return value_new_float (random_rayleigh (sigma));
}

/***************************************************************************/

static GnmFuncHelp const help_randrayleightail[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDRAYLEIGHTAIL:random variate from the tail of a Rayleigh distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:lower limit of the tail") },
	{ GNM_FUNC_HELP_ARG, F_("\xcf\x83:scale parameter of the Rayleigh distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDRAYLEIGHTAIL(0.3,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDRAYLEIGHTAIL(0.3,1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDRAYLEIGH" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randrayleightail (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a     = value_get_as_float (argv[0]);
	gnm_float sigma = value_get_as_float (argv[1]);

        return value_new_float (random_rayleigh_tail (a, sigma));
}

/***************************************************************************/

static GnmFuncHelp const help_randgamma[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDGAMMA:random variate from a Gamma distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:shape parameter of the Gamma distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:scale parameter of the Gamma distribution") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{a} \xe2\x89\xa4 0, RANDGAMMA returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDGAMMA(1,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDGAMMA(1,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randgamma (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

	if (a <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_gamma (a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_randpareto[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDPARETO:random variate from a Pareto distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:parameter of the Pareto distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:parameter of the Pareto distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDPARETO(1,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDPARETO(1,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randpareto (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

        return value_new_float (random_pareto (a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_randfdist[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDFDIST:random variate from an F distribution") },
	{ GNM_FUNC_HELP_ARG, F_("df1:numerator degrees of freedom") },
        { GNM_FUNC_HELP_ARG, F_("df2:denominator degrees of freedom") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDFDIST(1,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDFDIST(1,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDGAMMA" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randfdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float nu1 = value_get_as_float (argv[0]);
	gnm_float nu2 = value_get_as_float (argv[1]);

        return value_new_float (random_fdist (nu1, nu2));
}

/***************************************************************************/

static GnmFuncHelp const help_randbeta[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDBETA:random variate from a Beta distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:parameter of the Beta distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:parameter of the Beta distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBETA(1,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDBETA(1,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDGAMMA" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randbeta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

        return value_new_float (random_beta (a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_randlogistic[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDLOGISTIC:random variate from a logistic distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:parameter of the logistic distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLOGISTIC(1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLOGISTIC(1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randlogistic (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);

        return value_new_float (random_logistic (a));
}

/***************************************************************************/

static GnmFuncHelp const help_randgeom[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDGEOM:random variate from a geometric distribution") },
        { GNM_FUNC_HELP_ARG, F_("p:probability of success in a single trial") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 RANDGEOM returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDGEOM(0.4)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDGEOM(0.4)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randgeom (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);

	if (p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_geometric (p));
}

/***************************************************************************/

static GnmFuncHelp const help_randhyperg[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDHYPERG:random variate from a hypergeometric distribution") },
        { GNM_FUNC_HELP_ARG, F_("n1:number of objects of type 1") },
        { GNM_FUNC_HELP_ARG, F_("n2:number of objects of type 2") },
        { GNM_FUNC_HELP_ARG, F_("t:total number of objects selected") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDHYPERG(21,1,9)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDHYPERG(21,1,9)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randhyperg (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n1 = value_get_as_float (argv[0]);
	gnm_float n2 = value_get_as_float (argv[1]);
	gnm_float t = value_get_as_float (argv[2]);

        return value_new_float (random_hypergeometric (gnm_floor (n1),
						       gnm_floor (n2),
						       gnm_floor (t)));
}

/***************************************************************************/

static GnmFuncHelp const help_randlog[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDLOG:random variate from a logarithmic distribution") },
        { GNM_FUNC_HELP_ARG, F_("p:probability") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 RANDLOG returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLOG(0.72)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLOG(0.72)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randlog (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);

	if (p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_logarithmic (p));
}

/***************************************************************************/

static GnmFuncHelp const help_randchisq[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDCHISQ:random variate from a Chi-square distribution") },
        { GNM_FUNC_HELP_ARG, F_("df:degrees of freedom") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDCHISQ(0.5)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDCHISQ(0.5)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND,RANDGAMMA" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randchisq (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float nu = value_get_as_float (argv[0]);

        return value_new_float (random_chisq (nu));
}

/***************************************************************************/

static GnmFuncHelp const help_randtdist[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDTDIST:random variate from a Student t distribution") },
        { GNM_FUNC_HELP_ARG, F_("df:degrees of freedom") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDTDIST(5)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDTDIST(5)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randtdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float nu = value_get_as_float (argv[0]);

        return value_new_float (random_tdist (nu));
}

/***************************************************************************/

static GnmFuncHelp const help_randgumbel[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDGUMBEL:random variate from a Gumbel distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:parameter of the Gumbel distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:parameter of the Gumbel distribution") },
        { GNM_FUNC_HELP_ARG, F_("type:type of the Gumbel distribution, defaults to 1") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{type} is neither 1 nor 2, RANDGUMBEL returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDGUMBEL(0.5,1,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDGUMBEL(0.5,1,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randgumbel (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);
	gnm_float type = argv[2] ? value_get_as_float (argv[2]) : 1;

	if (type == 1)
		return value_new_float (random_gumbel1 (a, b));
	else if (type == 2)
		return value_new_float (random_gumbel2 (a, b));
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_randlevy[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDLEVY:random variate from a L\xc3\xa9vy distribution") },
        { GNM_FUNC_HELP_ARG, F_("c:parameter of the L\xc3\xa9vy distribution") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xb1:parameter of the L\xc3\xa9vy distribution") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xb2:parameter of the L\xc3\xa9vy distribution, defaults to 0") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("For @{\xce\xb1} = 1, @{\xce\xb2}=0, the L\xc3\xa9vy distribution "
					"reduces to the Cauchy (or Lorentzian) distribution.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("For @{\xce\xb1} = 2, @{\xce\xb2}=0, the L\xc3\xa9vy distribution "
					"reduces to the normal distribution.") },
 	{ GNM_FUNC_HELP_NOTE, F_("If @{\xce\xb1} \xe2\x89\xa4 0 or @{\xce\xb1} > 2, RANDLEVY returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{\xce\xb2} < -1 or @{\xce\xb2} > 1, RANDLEVY returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=RANDLEVY(0.5,0.1,1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=RANDLEVY(0.5,0.1,1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randlevy (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float c     = value_get_as_float (argv[0]);
	gnm_float alpha = value_get_as_float (argv[1]);
	gnm_float beta  = argv[2] ? value_get_as_float (argv[2]): 0;

	if (alpha <= 0 || alpha > 2 || beta < -1 || beta > 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (random_levy_skew (c, alpha, beta));
}

/***************************************************************************/

static GnmFuncHelp const help_randexppow[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDEXPPOW:random variate from an exponential power distribution") },
        { GNM_FUNC_HELP_ARG, F_("a:scale parameter of the exponential power distribution") },
        { GNM_FUNC_HELP_ARG, F_("b:exponent of the exponential power distribution") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("For @{b} = 1 the exponential power distribution "
					"reduces to the Laplace distribution.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("For @{b} = 2 the exponential power distribution "
					"reduces to the normal distribution with \xcf\x83 = a/sqrt(2)") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDEXPPOW(0.5,0.1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDEXPPOW(0.5,0.1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randexppow (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

	return value_new_float (random_exppow (a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_randlandau[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDLANDAU:random variate from the Landau distribution") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLANDAU()" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDLANDAU()" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randlandau (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (random_landau ());
}

/***************************************************************************/

static GnmFuncHelp const help_randnormtail[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDNORMTAIL:random variate from the upper tail of a normal distribution with mean 0") },
        { GNM_FUNC_HELP_ARG, F_("a:lower limit of the tail") },
        { GNM_FUNC_HELP_ARG, F_("\xcf\x83:standard deviation of the normal distribution") },
	{ GNM_FUNC_HELP_NOTE, F_("The method is based on Marsaglia's famous "
				 "rectangle-wedge-tail algorithm (Ann Math Stat 32, 894-899 "
				 "(1961)), with this aspect explained in Knuth, v2, 3rd ed, p139, "
				 "586 (exercise 11).") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDNORMTAIL(0.5,0.1)" },
        { GNM_FUNC_HELP_SEEALSO, "RAND" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randnormtail (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a     = value_get_as_float (argv[0]);
	gnm_float sigma = value_get_as_float (argv[1]);

	return value_new_float (random_gaussian_tail (a, sigma));
}

/***************************************************************************/

static GnmFuncHelp const help_simtable[] = {
        { GNM_FUNC_HELP_NAME, F_("SIMTABLE:one of the values in the given argument list "
				 "depending on the round number of the simulation tool") },
        { GNM_FUNC_HELP_ARG, F_("d1:first value") },
        { GNM_FUNC_HELP_ARG, F_("d2:second value") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("SIMTABLE returns one of the values in the given argument list "
					"depending on the round number of the simulation tool. When the "
					"simulation tool is not activated, SIMTABLE returns @{d1}.\n"
					"With the simulation tool and the SIMTABLE function you can "
					"test given decision variables. Each SIMTABLE function contains "
					"the possible values of a simulation variable. In most "
					"valid simulation models you should have the same number of "
					"values @{dN} for all decision variables.  If the simulation is run "
					"more rounds than there are values defined, SIMTABLE returns "
					"#N/A error (e.g. if A1 contains `=SIMTABLE(1)' and A2 "
					"`=SIMTABLE(1,2)', A1 yields #N/A error on the second round).\n"
					"The successive use of the simulation tool also requires that you "
					"give to the tool at least one input variable having RAND() or "
					"any other RAND<distribution name>() function in it. "
					"On each round, the simulation tool iterates for the given number "
					"of rounds over all the input variables to reevaluate them. "
					"On each iteration, "
					"the values of the output variables are stored, and when "
					"the round is completed, descriptive statistical information is "
					"created according to the values.") },
        { GNM_FUNC_HELP_EXAMPLES, "=SIMTABLE(223,225,227,229)" },
        { GNM_FUNC_HELP_END}
};

typedef struct {
	int   index;
	GnmValue *value;
} simtable_t;

static GnmValue *
callback_function_simtable (GnmEvalPos const *ep, GnmValue const *value, void *closure)
{
	simtable_t *p = closure;

	if (p->index == ep->sheet->simulation_round)
		p->value = value_dup (value);
	++(p->index);

	return NULL;
}

static GnmValue *
gnumeric_simtable (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	simtable_t p;

	p.index = 0;
	p.value = NULL;

	function_iterate_argument_values
		(ei->pos, callback_function_simtable, &p,
		 argc, argv, FALSE, CELL_ITER_IGNORE_BLANK);

	/* See if there was any value worth using. */
	if (p.value == NULL)
		return value_new_error_NA (ei->pos);

	return p.value;
}

/***************************************************************************/
/*gettext: For translations of the term skew-normal distribution see */
/* http://isi.cbs.nl/glossary/term3051.htm */
static GnmFuncHelp const help_randsnorm[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDSNORM:random variate from a skew-normal distribution") },
        { GNM_FUNC_HELP_ARG, F_("\360\235\233\274:shape parameter of the skew-normal distribution, "
				"defaults to 0") },
        { GNM_FUNC_HELP_ARG, F_("\360\235\234\211:location parameter of the skew-normal distribution, "
				"defaults to 0") },
        { GNM_FUNC_HELP_ARG, F_("\360\235\234\224:scale parameter of the skew-normal distribution, "
				"defaults to 1") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The random variates are drawn from a skew-normal "
					"distribution with shape parameter @{\360\235\233\274}. "
					"When @{\360\235\233\274}=0, the skewness vanishes, and "
					"we obtain the standard normal density; as \360\235\233\274"
					" increases (in absolute value), the skewness of the "
					"distribution increases; when @{\360\235\233\274} approaches "
					"infinity  the density converges to the so-called "
					"half-normal (or folded normal) density function; "
					"if the sign of @{\360\235\233\274} changes, the density "
					"is reflected on the opposite side of the vertical axis.") },
	{ GNM_FUNC_HELP_NOTE, F_("The mean of a skew-normal distribution with location parameter @{\360\235\234\211}=0 "
				 "is not 0.") },
	{ GNM_FUNC_HELP_NOTE, F_("The standard deviation of a skew-normal distribution with scale parameter "
				 "@{\360\235\234\224}=1 is not 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("The skewness of a skew-normal distribution is in general not @{\360\235\233\274}.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{\360\235\234\224} < 0, RANDSNORM returns #NUM!") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDSNORM(-3,0,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDSNORM(-3,0,1)" },
        { GNM_FUNC_HELP_SEEALSO, "RANDNORM,RANDSTDIST" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randsnorm (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float alpha  = 0.;
	gnm_float mean  = 0.;
	gnm_float stdev = 1.;
	gnm_float result;

	if (argv[0]) {
		alpha  = value_get_as_float (argv[0]);
		if (argv[1]) {
			mean  = value_get_as_float (argv[1]);
			if (argv[2])
				stdev = value_get_as_float (argv[2]);
		}
	}

       	if (stdev < 0)
		return value_new_error_NUM (ei->pos);

	result = ((alpha == 0) ? random_normal () : random_skew_normal (alpha));

        return value_new_float (stdev * result + mean);
}

/***************************************************************************/

static GnmFuncHelp const help_randstdist[] = {
        { GNM_FUNC_HELP_NAME, F_("RANDSTDIST:random variate from a skew-t distribution") },
        { GNM_FUNC_HELP_ARG, F_("df:degrees of freedom") },
        { GNM_FUNC_HELP_ARG, F_("\360\235\233\274:shape parameter of the skew-t distribution, defaults to 0") },
	{ GNM_FUNC_HELP_NOTE, F_("The mean of a skew-t distribution is not 0.") },
	{ GNM_FUNC_HELP_NOTE, F_("The standard deviation of a skew-t distribution is not 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("The skewness of a skew-t distribution is in general not @{\360\235\233\274}.") },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDSTDIST(5,-2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=RANDSTDIST(5,2)" },
        { GNM_FUNC_HELP_SEEALSO, "RANDTDIST,RANDSNORM" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_randstdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float nu = value_get_as_float (argv[0]);
	gnm_float alpha = argv[1] ? value_get_as_float (argv[1]) : 0;

	return ((alpha == 0) ? value_new_float (random_tdist (nu))
		: value_new_float (random_skew_tdist (nu, alpha)));
}

/***************************************************************************/

// Note: Most of the tests for these reside in sstest

GnmFuncDescriptor const random_functions[] = {
	{ "rand",    "", help_rand,
	  gnumeric_rand, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randbernoulli", "f",    help_randbernoulli,
	  gnumeric_randbernoulli, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randbeta", "ff",    help_randbeta,
	  gnumeric_randbeta, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randbetween", "ff",  help_randbetween,
	  gnumeric_randbetween, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randbinom", "ff",  help_randbinom,
	  gnumeric_randbinom, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randcauchy", "f",    help_randcauchy,
	  gnumeric_randcauchy, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randchisq", "f",    help_randchisq,
	  gnumeric_randchisq, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "randdiscrete", "r|r",
	  help_randdiscrete, gnumeric_randdiscrete, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "randexp", "f",          help_randexp,
	  gnumeric_randexp, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randexppow", "ff",          help_randexppow,
	  gnumeric_randexppow, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randfdist", "ff",       help_randfdist,
	  gnumeric_randfdist, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randgamma", "ff",     help_randgamma,
	  gnumeric_randgamma, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randnormtail", "ff",     help_randnormtail,
	  gnumeric_randnormtail, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randgeom", "f",     help_randgeom,
	  gnumeric_randgeom, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randgumbel", "ff|f",     help_randgumbel,
	  gnumeric_randgumbel, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randhyperg", "fff",     help_randhyperg,
	  gnumeric_randhyperg, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randlandau", "", help_randlandau,
	  gnumeric_randlandau, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randlaplace", "f",  help_randlaplace,
	  gnumeric_randlaplace, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randlevy", "ff|f",  help_randlevy,
	  gnumeric_randlevy, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randlog", "f",  help_randlog,
	  gnumeric_randlog, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randlogistic", "f",  help_randlogistic,
	  gnumeric_randlogistic, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randlognorm", "ff",  help_randlognorm,
	  gnumeric_randlognorm, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randnegbinom", "ff",  help_randnegbinom,
	  gnumeric_randnegbinom, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randnorm", "ff",  help_randnorm,
	  gnumeric_randnorm, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randpareto", "ff",  help_randpareto,
	  gnumeric_randpareto, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randpoisson", "f",  help_randpoisson,
	  gnumeric_randpoisson, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randrayleigh", "f",  help_randrayleigh,
	  gnumeric_randrayleigh, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randrayleightail", "ff",  help_randrayleightail,
	  gnumeric_randrayleightail, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randsnorm", "|fff",  help_randsnorm,
	  gnumeric_randsnorm, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randstdist", "ff",  help_randstdist,
	  gnumeric_randstdist, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "randtdist", "f",  help_randtdist,
	  gnumeric_randtdist, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randuniform", "ff",  help_randuniform,
	  gnumeric_randuniform, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
        { "randweibull", "ff",  help_randweibull,
	  gnumeric_randweibull, NULL,
	  GNM_FUNC_SIMPLE | GNM_FUNC_VOLATILE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "simtable", NULL,  help_simtable,
	  NULL,	gnumeric_simtable,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        {NULL}
};
