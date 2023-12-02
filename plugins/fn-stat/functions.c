/*
 * fn-stat.c:  Built in statistical functions and functions registration
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonenhutcs.cs.hut.fi>
 *   Michael Meeks <micheal@ximian.com>
 *   Morten Welinder <terra@gnome.org>
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
#include <sf-gamma.h>
#include <sf-dpq.h>
#include <rangefunc.h>
#include <regression.h>
#include <sheet.h>
#include <collect.h>
#include <gutils.h>
#include <value.h>
#include <expr.h>
#include <gnm-i18n.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <tools/analysis-tools.h>

GNM_PLUGIN_MODULE_HEADER;

#define HELP_DESCRIPTION_TEXT_INCLUSION { GNM_FUNC_HELP_DESCRIPTION, F_("Numbers, text and logical values are included in the calculation too. If the cell contains text or the argument evaluates to FALSE, it is counted as value zero (0). If the argument evaluates to TRUE, it is counted as one (1). Note that empty cells are not counted.")}

/***************************************************************************/

static GnmFuncHelp const help_varp[] = {
	{ GNM_FUNC_HELP_NAME, F_("VARP:variance of an entire population")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("VARP is also known as the N-variance.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=VARP(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, ("AVERAGE,DVAR,DVARP,STDEV,VAR")},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Variance") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Variance.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_varp (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_var_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_var[] = {
	{ GNM_FUNC_HELP_NAME, F_("VAR:sample variance of the given sample")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("VAR is also known as the N-1-variance.")},
	{ GNM_FUNC_HELP_NOTE, F_("Since the N-1-variance includes Bessel's correction, whereas the N-variance calculated by VARPA or VARP does not, "
				 "under reasonable conditions the N-1-variance is an unbiased estimator of the variance of the population "
				 "from which the sample is drawn.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=VAR(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, ("VARP,STDEV,VARA")},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Variance") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Variance.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_var (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_var_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_stdev[] = {
	{ GNM_FUNC_HELP_NAME, F_("STDEV:sample standard deviation of the given sample")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("STDEV is also known as the N-1-standard deviation.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("To obtain the population standard deviation of a whole population use STDEVP.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=STDEV(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, ("AVERAGE,DSTDEV,DSTDEVP,STDEVA,STDEVPA,VAR")},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Standard_deviation") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:StandardDeviation.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_stdev (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_stddev_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_stdevp[] = {
	{ GNM_FUNC_HELP_NAME, F_("STDEVP:population standard deviation of the given population")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This is also known as the N-standard deviation")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=STDEVP(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, ("STDEV,STDEVA,STDEVPA")},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Standard_deviation") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:StandardDeviation.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_stdevp (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_stddev_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_rank[] = {
	{ GNM_FUNC_HELP_NAME, F_("RANK:rank of a number in a list of numbers")},
	{ GNM_FUNC_HELP_ARG, F_("x:number whose rank you want to find")},
	{ GNM_FUNC_HELP_ARG, F_("ref:list of numbers")},
	{ GNM_FUNC_HELP_ARG, F_("order:0 (descending order) or non-zero (ascending order); defaults to 0")},
	{ GNM_FUNC_HELP_NOTE, F_("In case of a tie, RANK returns the largest possible rank.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, \xe2\x80\xa6, A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 25.9.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then RANK(17.3,A1:A5) equals 4.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then RANK(25.9,A1:A5) equals 1.")},
	{ GNM_FUNC_HELP_SEEALSO, ("PERCENTRANK,RANK.AVG")},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rank (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *xs;
	int i, r, n;
	GnmValue *result = NULL;
	gnm_float x;
	gboolean increasing;

	x = value_get_as_float (argv[0]);
	xs = collect_floats_value (argv[1], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_ORDER_IRRELEVANT,
				   &n, &result);
	increasing = argv[2] ? value_get_as_checked_bool (argv[2]) : FALSE;

	if (result)
		goto out;

	for (i = 0, r = 1; i < n; i++) {
		gnm_float y = xs[i];

		if (increasing ? y < x : y > x)
			r++;
	}

	result = value_new_int (r);

 out:
	g_free (xs);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_rank_avg[] = {
	{ GNM_FUNC_HELP_NAME, F_("RANK.AVG:rank of a number in a list of numbers")},
	{ GNM_FUNC_HELP_ARG, F_("x:number whose rank you want to find")},
	{ GNM_FUNC_HELP_ARG, F_("ref:list of numbers")},
	{ GNM_FUNC_HELP_ARG, F_("order:0 (descending order) or non-zero (ascending order); defaults to 0")},
	{ GNM_FUNC_HELP_NOTE, F_("In case of a tie, RANK.AVG returns the average rank.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel 2010 compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 25.9.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then RANK.AVG(17.3,A1:A5) equals 4.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then RANK.AVG(25.9,A1:A5) equals 1.5.")},
	{ GNM_FUNC_HELP_SEEALSO, ("PERCENTRANK,RANK")},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rank_avg (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *xs;
	int i, r, n, t;
	GnmValue *result = NULL;
	gnm_float x;
	gboolean increasing;

	x = value_get_as_float (argv[0]);
	xs = collect_floats_value (argv[1], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_ORDER_IRRELEVANT,
				   &n, &result);
	increasing = argv[2] ? value_get_as_checked_bool (argv[2]) : FALSE;

	if (result)
		goto out;

	for (i = 0, r = 1, t = 0; i < n; i++) {
		gnm_float y = xs[i];

		if (increasing ? y < x : y > x)
			r++;
		if (x == y)
			t++;
	}

	if (t > 1)
		result = value_new_float (r + (t - 1)/2.);
	else
		result = value_new_int (r);

 out:
	g_free (xs);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_trimmean[] = {

	{ GNM_FUNC_HELP_NAME, F_("TRIMMEAN:mean of the interior of a data set")},
	{ GNM_FUNC_HELP_ARG, F_("ref:list of numbers whose mean you want to calculate")},
	{ GNM_FUNC_HELP_ARG, F_("fraction:fraction of the data set excluded from the mean")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{fraction}=0.2 and the data set contains 40 numbers, 8 numbers are trimmed from the data set (40 x 0.2): "
					"the 4 largest and the 4 smallest. To avoid a bias, the number of points to be excluded is always rounded down to the nearest even number.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 40.1.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then TRIMMEAN(A1:A5,0.2) equals 23.2 and TRIMMEAN(A1:A5,0.4) equals 21.5.")},
	{ GNM_FUNC_HELP_SEEALSO, ("AVERAGE,GEOMEAN,HARMEAN,MEDIAN,MODE")},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_trimmean (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int c, tc, n;
	GnmValue *result = NULL;
	gnm_float *xs = collect_floats_value (argv[0], ei->pos,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS |
					      COLLECT_SORT,
					      &n, &result);
	gnm_float p = value_get_as_float (argv[1]);
	gnm_float res;

	if (result)
		goto out;

	if (p < 0 || p >= 1) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	tc = (int)gnm_fake_floor ((n * p) / 2);
	c = n - 2 * tc;
	if (gnm_range_average (xs + tc, c, &res))
		result = value_new_error_VALUE (ei->pos);
	else
		result = value_new_float (res);

	g_free (xs);
 out:
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_covar[] = {
	{ GNM_FUNC_HELP_NAME, F_("COVAR:covariance of two data sets")},
	{ GNM_FUNC_HELP_ARG, F_("array1:first data set")},
	{ GNM_FUNC_HELP_ARG, F_("array2:set data set")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then COVAR(A1:A5,B1:B5) equals 65.858.") },
	{ GNM_FUNC_HELP_SEEALSO, "CORREL,FISHER,FISHERINV"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Covariance") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Covariance.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_covar (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      gnm_range_covar_pop,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_covariance_s[] = {
	{ GNM_FUNC_HELP_NAME, F_("COVARIANCE.S:sample covariance of two data sets")},
	{ GNM_FUNC_HELP_ARG, F_("array1:first data set")},
	{ GNM_FUNC_HELP_ARG, F_("array2:set data set")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then COVAR(A1:A5,B1:B5) equals 65.858.") },
	{ GNM_FUNC_HELP_SEEALSO, "COVAR,CORREL"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Covariance") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Covariance.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_covariance_s (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      gnm_range_covar_est,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_correl[] = {
	{ GNM_FUNC_HELP_NAME, F_("CORREL:Pearson correlation coefficient of two data sets")},
	{ GNM_FUNC_HELP_ARG, F_("array1:first data set")},
	{ GNM_FUNC_HELP_ARG, F_("array2:second data set")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then CORREL(A1:A5,B1:B5) equals 0.996124788.") },
	{ GNM_FUNC_HELP_SEEALSO, "COVAR,FISHER,FISHERINV"},
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:CorrelationCoefficient.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Covariance.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_correl (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      gnm_range_correl_pop,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_negbinomdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("NEGBINOMDIST:probability mass function of the negative binomial distribution")},
	{ GNM_FUNC_HELP_ARG, F_("f:number of failures")},
	{ GNM_FUNC_HELP_ARG, F_("t:threshold number of successes")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability of a success")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{f} or @{t} is a non-integer it is truncated.")},
	{ GNM_FUNC_HELP_NOTE, F_("If (@{f} + @{t} -1) <= 0 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NEGBINOMDIST(2,5,0.55)" },
	{ GNM_FUNC_HELP_SEEALSO, "BINOMDIST,COMBIN,FACT,HYPGEOMDIST,PERMUT"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_negbinomdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float r = gnm_fake_floor (value_get_as_float (argv[1]));
	gnm_float p = value_get_as_float (argv[2]);

	if ((x + r - 1) <= 0 || p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (dnbinom (x, r, p, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_normsdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("NORMSDIST:cumulative distribution function of the standard normal distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("NORMSDIST is the OpenFormula function LEGACY.NORMSDIST.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NORMSDIST(2)" },
	{ GNM_FUNC_HELP_SEEALSO, "NORMDIST"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Normal_distribution") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:NormalDistribution.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_normsdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);

	return value_new_float (pnorm (x, 0, 1, TRUE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_snorm_dist_range[] = {
	{ GNM_FUNC_HELP_NAME, F_("SNORM.DIST.RANGE:probability of the standard normal distribution over an interval") },
	{ GNM_FUNC_HELP_ARG, F_("x1:start of the interval") },
	{ GNM_FUNC_HELP_ARG, F_("x2:end of the interval") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns the cumulative probability over a range of the standard normal distribution; that is the integral over the probability density function from @{x1} to @{x2}.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x1}>@{x2}, this function returns a negative value.")},
	{ GNM_FUNC_HELP_SEEALSO, "NORMSDIST,R.PNORM,R.QNORM,R.DNORM"},
	{ GNM_FUNC_HELP_EXAMPLES, "=SNORM.DIST.RANGE(0.6,0.6+1e-10)" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_snorm_dist_range (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	gnm_float x1 = value_get_as_float (args[0]);
	gnm_float x2 = value_get_as_float (args[1]);

	return value_new_float (pnorm2 (x1, x2));
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_normsinv[] = {
	{ GNM_FUNC_HELP_NAME, F_("NORMSINV:inverse of the cumulative distribution function of the standard normal distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:given probability")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("NORMSINV is the OpenFormula function LEGACY.NORMSINV.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NORMSINV(0.2)" },
	{ GNM_FUNC_HELP_SEEALSO, "NORMDIST,NORMINV,NORMSDIST,STANDARDIZE,ZTEST"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Normal_distribution") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:NormalDistribution.html") },
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_normsinv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);

	if (p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qnorm (p, 0, 1, TRUE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_owent[] = {
	{ GNM_FUNC_HELP_NAME, F_("OWENT:Owen's T function")},
	{ GNM_FUNC_HELP_ARG, F_("h:number")},
	{ GNM_FUNC_HELP_ARG, F_("a:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=OWENT(0.1,11)" },
	{ GNM_FUNC_HELP_SEEALSO, "R.PSNORM,R.PST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_owent (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float h = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);

	return value_new_float (gnm_owent (h, a));
}

/***************************************************************************/

static GnmFuncHelp const help_lognormdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOGNORMDIST:cumulative distribution function of the lognormal distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("mean:mean")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:standard deviation")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{stddev} = 0 LOGNORMDIST returns a #DIV/0! error.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} <= 0, @{mean} < 0 or @{stddev} <= 0 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LOGNORMDIST(3,1,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "NORMDIST"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Log-normal_distribution") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:LogNormalDistribution.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_lognormdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x	 = value_get_as_float (argv[0]);
	gnm_float mean   = value_get_as_float (argv[1]);
	gnm_float stddev = value_get_as_float (argv[2]);

	if (x <= 0 || mean < 0 || stddev <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (plnorm (x, mean, stddev, TRUE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_loginv[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOGINV:inverse of the cumulative distribution function of the lognormal distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability")},
	{ GNM_FUNC_HELP_ARG, F_("mean:mean")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:standard deviation")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 or @{stddev} <= 0 this function returns #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LOGINV(0.5,2,3)" },
	{ GNM_FUNC_HELP_SEEALSO, ("EXP,LN,LOG,LOG10,LOGNORMDIST")},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Log-normal_distribution") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:LogNormalDistribution.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_loginv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p, mean, stddev;

	p = value_get_as_float (argv[0]);
	mean = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);

	if (p < 0 || p > 1 || stddev <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qlnorm (p, mean, stddev, TRUE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_fisherinv[] = {
	{ GNM_FUNC_HELP_NAME, F_("FISHERINV:inverse of the Fisher transformation")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} is a non-number this function returns a #VALUE! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FISHERINV(2)" },
	{ GNM_FUNC_HELP_SEEALSO, "FISHER,TANH"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fisherinv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_tanh (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_mode[] = {
	{ GNM_FUNC_HELP_NAME, F_("MODE:first most common number in the dataset")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If the data set does not contain any duplicates this function returns a #N/A error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MODE(11.4,17.3,11.4,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,MEDIAN,MODE.MULT"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Mode_(statistics)") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Mode.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_mode (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_mode,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NA);
}

/***************************************************************************/

static GnmFuncHelp const help_mode_mult[] = {
	{ GNM_FUNC_HELP_NAME, F_("MODE.MULT:most common numbers in the dataset")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If the data set does not contain any duplicates this function returns a #N/A error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MODE.MULT(11.4,17.3,11.4,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,MEDIAN,MODE"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Mode_(statistics)") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Mode.html") },
	{ GNM_FUNC_HELP_END }
};

static gint
gnumeric_mode_mult_cmp (gconstpointer a, gconstpointer b)
{
	return (((*((gnm_float *)a))<(*((gnm_float *)b))) ? -1 : 1);
}

static gboolean
gnumeric_mode_mult_rm (gpointer key, gpointer value, gpointer user_data)
{
	return (*((int *)user_data) != *((int *)value));
}

static GnmValue *
gnumeric_mode_mult (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmValue *error = NULL;
	GnmValue *result;
	gnm_float *vals;
	int n;
	gboolean constp;

	vals = collect_floats (argc, argv, ei->pos,
			       COLLECT_IGNORE_STRINGS |
			       COLLECT_IGNORE_BOOLS |
			       COLLECT_IGNORE_BLANKS,
			       &n, &error,
			       NULL, &constp);
	if (!vals)
		return error;

	if (n <= 1)
		result = value_new_error_NA (ei->pos);
	else {
		GHashTable *h;
		int i;
		int dups = 0;

		h = g_hash_table_new_full ((GHashFunc)gnm_float_hash,
					   (GCompareFunc)gnm_float_equal,
					   NULL,
					   (GDestroyNotify)g_free);
		for (i = 0; i < n; i++) {
			gpointer rval;
			gboolean found = g_hash_table_lookup_extended (h, &vals[i], NULL, &rval);
			int *pdups;

			if (found) {
				pdups = (int *)rval;
				(*pdups)++;
			} else {
				pdups = g_new (int, 1);
				*pdups = 1;
				g_hash_table_insert (h, (gpointer)(vals + i), pdups);
			}

			if (*pdups > dups)
				dups = *pdups;
		}

		if (dups <= 1)
			result = value_new_error_NA (ei->pos);
		else {
			GList *keys, *l;

			g_hash_table_foreach_remove (h, gnumeric_mode_mult_rm, &dups);
			keys = g_hash_table_get_keys (h);

			keys = g_list_sort (keys, gnumeric_mode_mult_cmp);

			result = value_new_array (1,g_list_length (keys));
			i = 0;
			for (l = keys; l != NULL; l = l->next)
				value_array_set (result, 0, i++, value_new_float (*((gnm_float *)(l->data))));
			g_list_free (keys);
		}

		g_hash_table_destroy (h);
	}

	if (!constp) g_free (vals);

	return result;
}
/***************************************************************************/

static GnmFuncHelp const help_harmean[] = {
	{ GNM_FUNC_HELP_NAME, F_("HARMEAN:harmonic mean")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The harmonic mean of N data points is N divided by the sum of the reciprocals of the data points).")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=HARMEAN(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,GEOMEAN,MEDIAN,MODE,TRIMMEAN"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Harmonic_mean") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:HarmonicMean.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_harmean (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_harmonic_mean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_geomean[] = {
	{ GNM_FUNC_HELP_NAME, F_("GEOMEAN:geometric mean")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The geometric mean is equal to the Nth root of the product of the N values.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GEOMEAN(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,HARMEAN,MEDIAN,MODE,TRIMMEAN"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Geometric_mean") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:GeometricMean.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_geomean (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_geometric_mean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_count[] = {
	{ GNM_FUNC_HELP_NAME, F_("COUNT:total number of integer or floating point arguments passed")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then COUNT(A1:A5) equals 5.") },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_count (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_count,
				     COLLECT_IGNORE_ERRORS |
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_counta[] = {
	{ GNM_FUNC_HELP_NAME, F_("COUNTA:number of arguments passed not including empty cells")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers and strings 11.4, \"missing\", \"missing\", 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then COUNTA(A1:A5) equals 5.") },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,COUNT,DCOUNT,DCOUNTA,PRODUCT,SUM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_counta (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_count,
				     COLLECT_ZERO_ERRORS |
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_average[] = {
	{ GNM_FUNC_HELP_NAME, F_("AVERAGE:average of all the numeric values and cells")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=AVERAGE(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, ("SUM,COUNT")},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Arithmetic_mean") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:ArithmeticMean.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_average (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_average,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_min[] = {
	{ GNM_FUNC_HELP_NAME, F_("MIN:smallest value, with negative numbers considered smaller than positive numbers")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MIN(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "MAX,ABS"},
	{ GNM_FUNC_HELP_END }
};

static int
range_min0 (gnm_float const *xs, int n, gnm_float *res)
{
	if (n == 0) {
		*res = 0;
		return 0;
	} else
		return gnm_range_min (xs, n, res);
}

static GnmValue *
gnumeric_min (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_min0,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_ORDER_IRRELEVANT,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_max[] = {
	{ GNM_FUNC_HELP_NAME, F_("MAX:largest value, with negative numbers considered smaller than positive numbers")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MAX(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "MIN,ABS"},
	{ GNM_FUNC_HELP_END }
};

static int
range_max0 (gnm_float const *xs, int n, gnm_float *res)
{
	if (n == 0) {
		*res = 0;
		return 0;
	} else
		return gnm_range_max (xs, n, res);
}

static GnmValue *
gnumeric_max (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_max0,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_ORDER_IRRELEVANT,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_skew[] = {
	{ GNM_FUNC_HELP_NAME, F_("SKEW:unbiased estimate for skewness of a distribution")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_NOTE, F_("This is only meaningful if the underlying "
				 "distribution really has a third moment.  The skewness of a "
				 "symmetric (e.g., normal) distribution is zero.")},
	{ GNM_FUNC_HELP_NOTE, F_("If less than three numbers are given, this function returns a #DIV/0! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SKEW(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,VAR,SKEWP,KURT"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_skew (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_skew_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_skewp[] = {
	{ GNM_FUNC_HELP_NAME, F_("SKEWP:population skewness of a data set")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_NOTE, F_("If less than two numbers are given, SKEWP returns a #DIV/0! error.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SKEWP(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,VARP,SKEW,KURTP"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_skewp (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_skew_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_expondist[] = {
	{ GNM_FUNC_HELP_NAME, F_("EXPONDIST:probability density or cumulative distribution function of the exponential distribution")},
	   { GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("y:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the density function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{cumulative} is false it will return:\t"
	   "@{y} * exp (-@{y}*@{x}), otherwise it will return\t"
	   "1 - exp (-@{y}*@{x}).")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0 or @{y} <= 0 this will return an error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EXPONDIST(2,4,0)" },
	{ GNM_FUNC_HELP_SEEALSO, "POISSON"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_expondist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float y = value_get_as_float (argv[1]);
	gboolean cuml = value_get_as_checked_bool (argv[2]);

	if (x < 0 || y <= 0)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (pexp (x, 1 / y, TRUE, FALSE));
	else
		return value_new_float (dexp (x, 1 / y, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_bernoulli[] = {
	{ GNM_FUNC_HELP_NAME, F_("BERNOULLI:probability mass function of a Bernoulli distribution")},
	{ GNM_FUNC_HELP_ARG, F_("k:integer")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability of success")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{k} != 0 and @{k} != 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=BERNOULLI(0,0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDBERNOULLI"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
random_bernoulli_pdf (gnm_float k, gnm_float p)
{
	if (k == 0)
		return 1 - p;
	else if (k == 1)
		return p;
	else
		return 0;
}

static GnmValue *
gnumeric_bernoulli (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float k = value_get_as_float (argv[0]);
	gnm_float p = value_get_as_float (argv[1]);

	if (p < 0 || p > 1 || (k != 0 && k != 1))
		return value_new_error_NUM (ei->pos);

	return value_new_float (random_bernoulli_pdf (k, p));
}

/***************************************************************************/

static GnmFuncHelp const help_gammadist[] = {
	{ GNM_FUNC_HELP_NAME, F_("GAMMADIST:probability density or cumulative distribution function of the gamma distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("beta:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the density function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{alpha} <= 0 or @{beta} <= 0, this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GAMMADIST(1,2,3,0)" },
	{ GNM_FUNC_HELP_SEEALSO, "GAMMAINV"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_gammadist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float alpha = value_get_as_float (argv[1]);
	gnm_float beta = value_get_as_float (argv[2]);
	gboolean cum = value_get_as_checked_bool (argv[3]);

	if (x < 0 || alpha <= 0 || beta <= 0)
		return value_new_error_NUM (ei->pos);

	if (cum)
		return value_new_float (pgamma (x, alpha, beta, TRUE, FALSE));
	else
		return value_new_float (dgamma (x, alpha, beta, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_gammainv[] = {
	{ GNM_FUNC_HELP_NAME, F_("GAMMAINV:inverse of the cumulative gamma distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("beta:scale parameter")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{alpha} <= 0 or @{beta} <= 0 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GAMMAINV(0.34,2,4)" },
	{ GNM_FUNC_HELP_SEEALSO, "GAMMADIST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_gammainv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p, alpha, beta;

	p = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);

	if (p < 0 || p > 1 || alpha <= 0 || beta <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qgamma (p, alpha, beta, TRUE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_chidist[] = {
	{ GNM_FUNC_HELP_NAME, F_("CHIDIST:survival function of the chi-squared distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("dof:number of degrees of freedom")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The survival function is 1 minus the cumulative distribution function.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{dof} is non-integer it is truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{dof} < 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("CHIDIST(@{x},@{dof}) is the OpenFormula function LEGACY.CHIDIST(@{x},@{dof}).") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CHIDIST(5.3,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "CHIINV,CHITEST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_chidist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float dof = gnm_fake_floor (value_get_as_float (argv[1]));

	if (dof < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (pchisq (x, dof, FALSE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_chiinv[] = {
	{ GNM_FUNC_HELP_NAME, F_("CHIINV:inverse of the survival function of the chi-squared distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability")},
	{ GNM_FUNC_HELP_ARG, F_("dof:number of degrees of freedom")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The survival function is 1 minus the cumulative distribution function.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 or @{dof} < 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("CHIINV(@{p},@{dof}) is the OpenFormula function LEGACY.CHIDIST(@{p},@{dof}).") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CHIINV(0.98,7)" },
	{ GNM_FUNC_HELP_SEEALSO, "CHIDIST,CHITEST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_chiinv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);
	gnm_float dof = gnm_fake_floor (value_get_as_float (argv[1]));

	if (p < 0 || p > 1 || dof < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qchisq (p, dof, FALSE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_chitest[] = {
	{ GNM_FUNC_HELP_NAME, F_("CHITEST:p value of the Goodness of Fit Test")},
	{ GNM_FUNC_HELP_ARG, F_("actual_range:observed data")},
	{ GNM_FUNC_HELP_ARG, F_("theoretical_range:expected values")},
	{ GNM_FUNC_HELP_NOTE, F_("If the actual range is not an n by 1 or 1 by n range, "
				 "but an n by m range, then CHITEST uses (n-1) times (m-1) as "
				 "degrees of freedom. This is useful if the expected values "
				 "were calculated from the observed value in a test of "
				 "independence or test of homogeneity.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("CHITEST is the OpenFormula function LEGACY.CHITEST.") },
	{ GNM_FUNC_HELP_SEEALSO, "CHIDIST,CHIINV"},
	{ GNM_FUNC_HELP_END }
};

static int
calc_chisq (gnm_float const *xs, gnm_float const *ys, int n, gnm_float *res)
{
	gnm_float sum = 0;
	int i;
	gboolean has_neg = FALSE;

	if (n == 0)
		return 1;

	for (i = 0; i < n; i++) {
		gnm_float a = xs[i];
		gnm_float e = ys[i];

		if (e == 0)
			return 1;
		else if (e < 0)
			has_neg = TRUE;
		else
			sum += (a - e) / e * (a - e);
	}

	if (has_neg) {
		/* Hack: return -1 as a flag.  */
		*res = -1;
		return 0;
	}

	*res = sum;
	return 0;
}

static GnmValue *
gnumeric_chitest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int w0 = value_area_get_width (argv[0], ei->pos);
	int h0 = value_area_get_height (argv[0], ei->pos);
	int w1 = value_area_get_width (argv[1], ei->pos);
	int h1 = value_area_get_height (argv[1], ei->pos);
	GnmValue *v;
	gnm_float chisq;
	int df;

	/* Size error takes precedence over everything else.  */
	if (w0 * h0 != w1 * h1)
		return value_new_error_NA (ei->pos);

	v = float_range_function2 (argv[0], argv[1],
				   ei,
				   calc_chisq,
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS,
				   GNM_ERROR_DIV0);

	if (!VALUE_IS_NUMBER (v))
		return v;

	chisq = value_get_as_float (v);
	value_release (v);

	/* See calc_chisq.  */
	if (chisq == -1)
		return value_new_error_NUM (ei->pos);

	df = (w0-1) * (h0-1);
	df = (df == 0 ? w0 * h0 - 1 : df);
	return value_new_float (pchisq (chisq, df, FALSE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_betadist[] = {
	{ GNM_FUNC_HELP_NAME, F_("BETADIST:cumulative distribution function of the beta distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("beta:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("a:optional lower bound, defaults to 0")},
	{ GNM_FUNC_HELP_ARG, F_("b:optional upper bound, defaults to 1")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < @{a} or @{x} > @{b} this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{alpha} <= 0 or @{beta} <= 0, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{a} >= @{b} this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BETADIST(0.12,2,3)" },
	{ GNM_FUNC_HELP_SEEALSO, "BETAINV,BETA.DIST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_betadist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x, alpha, beta, a, b;

	x = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);
	a = argv[3] ? value_get_as_float (argv[3]) : 0;
	b = argv[4] ? value_get_as_float (argv[4]) : 1;

	if (x < a || x > b || a >= b || alpha <= 0 || beta <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (pbeta ((x - a) / (b - a), alpha, beta, TRUE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_beta_dist[] = {
	{ GNM_FUNC_HELP_NAME, F_("BETA.DIST:cumulative distribution function of the beta distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("beta:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the density function or the cumulative "
				"distribution function")},
	{ GNM_FUNC_HELP_ARG, F_("a:optional lower bound, defaults to 0")},
	{ GNM_FUNC_HELP_ARG, F_("b:optional upper bound, defaults to 1")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < @{a} or @{x} > @{b} this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{alpha} <= 0 or @{beta} <= 0, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{a} >= @{b} this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BETA.DIST(0.12,2,3,FALSE,0,4)" },
	{ GNM_FUNC_HELP_SEEALSO, "BETAINV,BETADIST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_beta_dist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x, alpha, beta, a, b;
	gboolean cuml;

	x = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);
	cuml = value_get_as_checked_bool (argv[3]);
	a = argv[4] ? value_get_as_float (argv[4]) : 0;
	b = argv[5] ? value_get_as_float (argv[5]) : 1;

	if (x < a || x > b || a >= b || alpha <= 0 || beta <= 0)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (pbeta ((x - a) / (b - a), alpha, beta, TRUE, FALSE));
	else
		return value_new_float (dbeta ((x - a) / (b - a), alpha, beta, FALSE) / (b - a));
}
/***************************************************************************/

/* Note: Excel's this function returns a #NUM! for various values where it
   simply gives up computing the result.  */
static GnmFuncHelp const help_betainv[] = {
	{ GNM_FUNC_HELP_NAME, F_("BETAINV:inverse of the cumulative distribution function of the beta distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("beta:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("a:optional lower bound, defaults to 0")},
	{ GNM_FUNC_HELP_ARG, F_("b:optional upper bound, defaults to 1")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{alpha} <= 0 or @{beta} <= 0, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{a} >= @{b} this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BETAINV(0.45,1.6,1)" },
	{ GNM_FUNC_HELP_SEEALSO, "BETADIST,BETA.DIST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_betainv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p, alpha, beta, a, b;

	p = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);
	a = argv[3] ? value_get_as_float (argv[3]) : 0;
	b = argv[4] ? value_get_as_float (argv[4]) : 1;

	if (p < 0 || p > 1 || a >= b || alpha <= 0 || beta <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float ((b - a) * qbeta (p, alpha, beta, TRUE, FALSE) + a);
}

/***************************************************************************/

static GnmFuncHelp const help_tdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("TDIST:survival function of the Student t-distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("dof:number of degrees of freedom")},
	{ GNM_FUNC_HELP_ARG, F_("tails:1 or 2")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The survival function is 1 minus the cumulative distribution function.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{dof} < 1 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{tails} is neither 1 or 2 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("The parameterization of this function is different from "
	   "what is used for, e.g., NORMSDIST.  This is a common source of "
	   "mistakes, but necessary for compatibility.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function is Excel compatible for non-negative @{x}.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=TDIST(2,5,1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=TDIST(-2,5,1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=TDIST(0,5,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "TINV,TTEST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float dof = value_get_as_float (argv[1]);
	gnm_float tails = value_get_as_float (argv[2]);
	gnm_float p;

	if (dof < 1)
		goto bad;

	if (tails == 1) {
		gboolean lower_tail = FALSE;
		if (x < 0) {
			lower_tail = TRUE;
			x = -x;
		}
		p = pt (x, dof, lower_tail, FALSE);
	} else if (tails == 2) {
		if (x < 0)
			goto bad;
		p = 2 * pt (x, dof, FALSE, FALSE);
	} else
		goto bad;

	return value_new_float (p);

 bad:
	return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_tinv[] = {
	{ GNM_FUNC_HELP_NAME, F_("TINV:two tailed inverse of the Student t-distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability in both tails")},
	{ GNM_FUNC_HELP_ARG, F_("dof:number of degrees of freedom")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns the non-negative value x such that the "
					"area under the Student t density with @{dof} degrees of freedom "
					"to the right of x is @{p}/2.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 or @{dof} < 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_NOTE, F_("The parameterization of this function is different from "
	   "what is used for, e.g., NORMSINV.  This is a common source of "
	   "mistakes, but necessary for compatibility.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=TINV(0.4,32)" },
	{ GNM_FUNC_HELP_SEEALSO, "TDIST,TTEST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tinv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);
	gnm_float dof = value_get_as_float (argv[1]);
	gnm_float result;

	if (p < 0 || p > 1 || dof < 1)
		return value_new_error_NUM (ei->pos);

	result = qt (p / 2, dof, FALSE, FALSE);

	if (result < 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (result);
}

/***************************************************************************/

static GnmFuncHelp const help_fdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("FDIST:survival function of the F distribution")},
	   { GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("dof_of_num:numerator degrees of freedom")},
	{ GNM_FUNC_HELP_ARG, F_("dof_of_denom:denominator degrees of freedom")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The survival function is 1 minus the cumulative distribution function.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{dof_of_num} < 1 or @{dof_of_denom} < 1, this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("FDIST is the OpenFormula function LEGACY.FDIST.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FDIST(2,5,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "FINV"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float dof1 = gnm_fake_floor (value_get_as_float (argv[1]));
	gnm_float dof2 = gnm_fake_floor (value_get_as_float (argv[2]));

	if (x < 0 || dof1 < 1 || dof2 < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (pf (x, dof1, dof2, FALSE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_landau[] = {
	{ GNM_FUNC_HELP_NAME, F_("LANDAU:approximate probability density function of the Landau distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=LANDAU(0.34)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDLANDAU"},
	{ GNM_FUNC_HELP_END }
};

/* From the GNU Scientific Library 1.1.1 (randist/landau.c)
 *
 * Copyright (C) 2001 David Morrison
 */
static gnm_float
random_landau_pdf (gnm_float x)
{
	static const gnm_float P1[5] = {
		GNM_const(0.4259894875E0),
		GNM_const(-0.1249762550E0),
		GNM_const(0.3984243700E-1),
		GNM_const(-0.6298287635E-2),
		GNM_const(0.1511162253E-2)
	};
	static const gnm_float P2[5] = {
		GNM_const(0.1788541609E0),
		GNM_const(0.1173957403E0),
		GNM_const(0.1488850518E-1),
		GNM_const(-0.1394989411E-2),
		GNM_const(0.1283617211E-3)
	};
	static const gnm_float P3[5] = {
		GNM_const(0.1788544503E0),
		GNM_const(0.9359161662E-1),
		GNM_const(0.6325387654E-2),
		GNM_const(0.6611667319E-4),
		GNM_const(-0.2031049101E-5)
	};
	static const gnm_float P4[5] = {
		GNM_const(0.9874054407E0),
		GNM_const(0.1186723273E3),
		GNM_const(0.8492794360E3),
		GNM_const(-0.7437792444E3),
		GNM_const(0.4270262186E3)
	};
	static const gnm_float P5[5] = {
		GNM_const(0.1003675074E1),
		GNM_const(0.1675702434E3),
		GNM_const(0.4789711289E4),
		GNM_const(0.2121786767E5),
		GNM_const(-0.2232494910E5)
	};
	static const gnm_float P6[5] = {
		GNM_const(0.1000827619E1),
		GNM_const(0.6649143136E3),
		GNM_const(0.6297292665E5),
		GNM_const(0.4755546998E6),
		GNM_const(-0.5743609109E7)
	};

	static const gnm_float Q1[5] = {
		GNM_const(1.0),
		GNM_const(-0.3388260629E0),
		GNM_const(0.9594393323E-1),
		GNM_const(-0.1608042283E-1),
		GNM_const(0.3778942063E-2)
	};
	static const gnm_float Q2[5] = {
		GNM_const(1.0),
		GNM_const(0.7428795082E0),
		GNM_const(0.3153932961E0),
		GNM_const(0.6694219548E-1),
		GNM_const(0.8790609714E-2)
	};
	static const gnm_float Q3[5] = {
		GNM_const(1.0),
		GNM_const(0.6097809921E0),
		GNM_const(0.2560616665E0),
		GNM_const(0.4746722384E-1),
		GNM_const(0.6957301675E-2)
	};
	static const gnm_float Q4[5] = {
		GNM_const(1.0),
		GNM_const(0.1068615961E3),
		GNM_const(0.3376496214E3),
		GNM_const(0.2016712389E4),
		GNM_const(0.1597063511E4)
	};
	static const gnm_float Q5[5] = {
		GNM_const(1.0),
		GNM_const(0.1569424537E3),
		GNM_const(0.3745310488E4),
		GNM_const(0.9834698876E4),
		GNM_const(0.6692428357E5)
	};
	static const gnm_float Q6[5] = {
		GNM_const(1.0),
		GNM_const(0.6514101098E3),
		GNM_const(0.5697473333E5),
		GNM_const(0.1659174725E6),
		GNM_const(-0.2815759939E7)
	};

	static const gnm_float A1[3] = {
		GNM_const(0.4166666667E-1),
		GNM_const(-0.1996527778E-1),
		GNM_const(0.2709538966E-1)
	};
	static const gnm_float A2[2] = {
		GNM_const(-0.1845568670E1),
		GNM_const(-0.4284640743E1)
	};

	gnm_float U, V, DENLAN;

	V = x;
	if (V < GNM_const(-5.5)) {
		U      = gnm_exp (V + 1);
		DENLAN = GNM_const(0.3989422803) * (gnm_exp ( -1 / U) / gnm_sqrt (U)) *
			(1 + (A1[0] + (A1[1] + A1[2] * U) * U) * U);
	} else if (V < -1) {
		U = gnm_exp (-V - 1);
		DENLAN = gnm_exp ( -U) * gnm_sqrt (U) *
			(P1[0] + (P1[1] + (P1[2] + (P1[3] + P1[4] * V) * V)
				  * V) * V) /
			(Q1[0] + (Q1[1] + (Q1[2] + (Q1[3] + Q1[4] * V) * V)
				  * V) * V);
	} else if (V < 1) {
		DENLAN = (P2[0] + (P2[1] + (P2[2] + (P2[3] + P2[4] * V) * V)
				   * V) * V) /
			(Q2[0] + (Q2[1] + (Q2[2] + (Q2[3] + Q2[4] * V) * V)
				  * V) * V);
	} else if (V < 5) {
		DENLAN = (P3[0] + (P3[1] + (P3[2] + (P3[3] + P3[4] * V) * V)
				   * V) * V) /
			(Q3[0] + (Q3[1] + (Q3[2] + (Q3[3] + Q3[4] * V) * V)
				  * V) * V);
	} else if (V < 12) {
		U = 1 / V;
		DENLAN = U * U *
			(P4[0] + (P4[1] + (P4[2] + (P4[3] + P4[4] * U) * U)
				  * U) * U) /
			(Q4[0] + (Q4[1] + (Q4[2] + (Q4[3] + Q4[4] * U) * U)
				  * U) * U);
	} else if (V < 50) {
		U = 1 / V;
		DENLAN = U * U *
			(P5[0] + (P5[1] + (P5[2] + (P5[3] + P5[4] * U) * U)
				  * U) * U) /
			(Q5[0] + (Q5[1] + (Q5[2] + (Q5[3] + Q5[4] * U) * U)
				  * U) * U);
	} else if (V < 300) {
		U = 1 / V;
		DENLAN = U * U *
			(P6[0] + (P6[1] + (P6[2] + (P6[3] + P6[4] * U) * U)
				  * U) * U) /
			(Q6[0] + (Q6[1] + (Q6[2] + (Q6[3] + Q6[4] * U) * U)
				  * U) * U);
	} else {
		U = 1 / (V - V * gnm_log(V) / (V + 1));
		DENLAN = U * U * (1 + (A2[0] + A2[1] * U) * U);
	}

	return DENLAN;
}

static GnmValue *
gnumeric_landau (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);

	return value_new_float (random_landau_pdf (x));
}


/***************************************************************************/

static GnmFuncHelp const help_finv[] = {
	{ GNM_FUNC_HELP_NAME, F_("FINV:inverse of the survival function of the F distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability")},
	{ GNM_FUNC_HELP_ARG, F_("dof_of_num:numerator degrees of freedom")},
	{ GNM_FUNC_HELP_ARG, F_("dof_of_denom:denominator degrees of freedom")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The survival function is 1 minus the cumulative distribution function.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{dof_of_num} < 1 or @{dof_of_denom} < 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("FINV is the OpenFormula function LEGACY.FINV.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FINV(0.2,2,4)" },
	{ GNM_FUNC_HELP_SEEALSO, "FDIST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_finv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);
	gnm_float dof1 = gnm_fake_floor (value_get_as_float (argv[1]));
	gnm_float dof2 = gnm_fake_floor (value_get_as_float (argv[2]));

	if (p < 0 || p > 1 || dof1 < 1 || dof2 < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qf (p, dof1, dof2, FALSE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_binomdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("BINOMDIST:probability mass or cumulative distribution function of the binomial distribution")},
	{ GNM_FUNC_HELP_ARG, F_("n:number of successes")},
	{ GNM_FUNC_HELP_ARG, F_("trials:number of trials")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability of success in each trial")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the mass function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} or @{trials} are non-integer they are truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} < 0 or @{trials} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} > @{trials} this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BINOMDIST(3,5,0.8,0)" },
	{ GNM_FUNC_HELP_SEEALSO, "POISSON"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_binomdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float trials = gnm_fake_floor (value_get_as_float (argv[1]));
	gnm_float p = value_get_as_float (argv[2]);
	gboolean cuml = value_get_as_checked_bool (argv[3]);

	if (n < 0 || trials < 0 || p < 0 || p > 1 || n > trials)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (pbinom (n, trials, p, TRUE, FALSE));
	else
		return value_new_float (dbinom (n, trials, p, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_binom_dist_range[] = {
	{ GNM_FUNC_HELP_NAME, F_("BINOM.DIST.RANGE:probability of the binomial distribution over an interval")},
	{ GNM_FUNC_HELP_ARG, F_("trials:number of trials")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability of success in each trial")},
	{ GNM_FUNC_HELP_ARG, F_("start:start of the interval")},
	{ GNM_FUNC_HELP_ARG, F_("end:end of the interval, defaults to @{start}")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{start}, @{end} or @{trials} are non-integer they are truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{trials} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{start} > @{end} this function returns 0.")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BINOM.DIST.RANGE(5,0.8,3,4)" },
	{ GNM_FUNC_HELP_SEEALSO, "BINOMDIST,R.PBINOM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_binom_dist_range (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float trials = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float p = value_get_as_float (argv[1]);
	gnm_float start = gnm_fake_floor (value_get_as_float (argv[2]));
	gnm_float end = argv[3] ? gnm_fake_floor (value_get_as_float (argv[3])) : start;

	if (trials < 0 || p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (pbinom2 (start, end, trials, p));
}

/***************************************************************************/
static GnmFuncHelp const help_cauchy[] = {
	{ GNM_FUNC_HELP_NAME, F_("CAUCHY:probability density or cumulative distribution function of the Cauchy, "
				 "Lorentz or Breit-Wigner distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("a:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the density function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{a} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{cumulative} is neither TRUE nor FALSE this function returns a #VALUE! error.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CAUCHY(0.43,1,TRUE)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDCAUCHY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cauchy (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);
	gboolean cuml = value_get_as_checked_bool (argv[2]);

	if (a < 0)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (pcauchy (x, 0, a, FALSE, FALSE));
	else
		return value_new_float (dcauchy (x, 0, a, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_critbinom[] = {
	{ GNM_FUNC_HELP_NAME, F_("CRITBINOM:right-tailed critical value of the binomial distribution")},
	{ GNM_FUNC_HELP_ARG, F_("trials:number of trials")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability of success in each trial")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:significance level (area of the tail)")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{trials} is a non-integer it is truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{trials} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{alpha} < 0 or @{alpha} > 1 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CRITBINOM(10,0.5,0.75)" },
	{ GNM_FUNC_HELP_SEEALSO, "BINOMDIST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_critbinom (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float trials = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float p = value_get_as_float (argv[1]);
	gnm_float alpha = value_get_as_float (argv[2]);

	if (trials < 0 || p < 0 || p > 1 || alpha < 0 || alpha > 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qbinom (alpha, trials, p, TRUE, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_permut[] = {
	{ GNM_FUNC_HELP_NAME, F_("PERMUT:number of @{k}-permutations of a @{n}-set")},
	{ GNM_FUNC_HELP_ARG, F_("n:size of the base set")},
	{ GNM_FUNC_HELP_ARG, F_("k:number of elements in each permutation")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} = 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} < @{k} this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=PERMUT(7,3)" },
	{ GNM_FUNC_HELP_SEEALSO, "COMBIN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_permut (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float k = gnm_fake_floor (value_get_as_float (argv[1]));

	if (0 <= k && k <= n)
		return value_new_float (permut (n, k));
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_hypgeomdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("HYPGEOMDIST:probability mass or cumulative distribution function of the hypergeometric distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number of successes")},
	{ GNM_FUNC_HELP_ARG, F_("n:sample size")},
	{ GNM_FUNC_HELP_ARG, F_("M:number of possible successes in the population")},
	{ GNM_FUNC_HELP_ARG, F_("N:population size")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the mass function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x},@{n},@{M} or @{N} is a non-integer it is truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x},@{n},@{M} or @{N} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} > @{M} or @{n} > @{N} this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=HYPGEOMDIST(1,2,3,10)" },
	{ GNM_FUNC_HELP_SEEALSO, "BINOMDIST,POISSON"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hypgeomdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float n = gnm_fake_floor (value_get_as_float (argv[1]));
	gnm_float M = gnm_fake_floor (value_get_as_float (argv[2]));
	gnm_float N = gnm_fake_floor (value_get_as_float (argv[3]));
	gboolean cum = argv[4] ? value_get_as_checked_bool (argv[4]) : FALSE;

	if (x < 0 || n < 0 || M < 0 || N < 0 || x > M || n > N)
		return value_new_error_NUM (ei->pos);

	if (cum)
		return value_new_float (phyper (x, M, N - M, n, TRUE, FALSE));
	else
		return value_new_float (dhyper (x, M, N - M, n, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_confidence[] = {
	{ GNM_FUNC_HELP_NAME, F_("CONFIDENCE:margin of error of a confidence interval for the population mean")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:significance level")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:population standard deviation")},
	{ GNM_FUNC_HELP_ARG, F_("size:sample size")},
	{ GNM_FUNC_HELP_NOTE, F_("This function requires the usually unknown population standard deviation.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{size} is non-integer it is truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{size} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{size} is 0 this function returns a #DIV/0! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CONFIDENCE(0.05,1,33)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,CONFIDENCE.T"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_confidence (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float stddev = value_get_as_float (argv[1]);
	gnm_float size = gnm_fake_floor (value_get_as_float (argv[2]));

	if (size == 0)
		return value_new_error_DIV0 (ei->pos);
	if (size <= 0 || stddev <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (-qnorm (x / 2, 0, 1, TRUE, FALSE) * (stddev / gnm_sqrt (size)));
}

/***************************************************************************/

static GnmFuncHelp const help_confidence_t[] = {
	{ GNM_FUNC_HELP_NAME, F_("CONFIDENCE.T:margin of error of a confidence interval for the "
				 "population mean using the Student's t-distribution")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:significance level")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:sample standard deviation")},
	{ GNM_FUNC_HELP_ARG, F_("size:sample size")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{stddev} < 0 or = 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{size} is non-integer it is truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{size} < 1 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{size} is 1 this function returns a #DIV/0! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CONFIDENCE.T(0.05,1,33)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,CONFIDENCE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_confidence_t (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float stddev = value_get_as_float (argv[1]);
	gnm_float size = gnm_fake_floor (value_get_as_float (argv[2]));

	if (size == 1)
		return value_new_error_DIV0 (ei->pos);
	if (size <= 1 || stddev <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (-qt (x / 2, size - 1, TRUE, FALSE) * (stddev / gnm_sqrt (size)));
}

/***************************************************************************/

static GnmFuncHelp const help_standardize[] = {
	{ GNM_FUNC_HELP_NAME, F_("STANDARDIZE:z-score of a value")},
	{ GNM_FUNC_HELP_ARG, F_("x:value")},
	{ GNM_FUNC_HELP_ARG, F_("mean:mean of the original distribution")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:standard deviation of the original distribution")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{stddev} is 0 this function returns a #DIV/0! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=STANDARDIZE(3,2,4)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_standardize (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x	 = value_get_as_float (argv[0]);
	gnm_float mean   = value_get_as_float (argv[1]);
	gnm_float stddev = value_get_as_float (argv[2]);

	if (stddev <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float ((x - mean) / stddev);
}

/***************************************************************************/

static GnmFuncHelp const help_weibull[] = {
	{ GNM_FUNC_HELP_NAME, F_("WEIBULL:probability density or cumulative distribution function of the Weibull distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("alpha:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("beta:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the density function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If the @{cumulative} boolean is true it will return: "
           "1 - exp (-(@{x}/@{beta})^@{alpha}), otherwise it will return "
           "(@{alpha}/@{beta}^@{alpha}) * @{x}^(@{alpha}-1) * exp(-(@{x}/@{beta}^@{alpha})).") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{alpha} <= 0 or @{beta} <= 0 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=WEIBULL(3,2,4,0)" },
	{ GNM_FUNC_HELP_SEEALSO, "POISSON"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_weibull (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float alpha = value_get_as_float (argv[1]);
	gnm_float beta = value_get_as_float (argv[2]);
	gboolean cuml = value_get_as_checked_bool (argv[3]);

	if (x < 0 || alpha <= 0 || beta <= 0)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (pweibull (x, alpha, beta, TRUE, FALSE));
	else
	 	return value_new_float (dweibull (x, alpha, beta, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_normdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("NORMDIST:probability density or cumulative distribution function of a normal distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("mean:mean of the distribution")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:standard deviation of the distribution")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the density function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{stddev} is 0 this function returns a #DIV/0! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NORMDIST(2,1,2,0)" },
	{ GNM_FUNC_HELP_SEEALSO, "POISSON"},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_normdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float mean = value_get_as_float (argv[1]);
	gnm_float stddev = value_get_as_float (argv[2]);
	gboolean cuml = value_get_as_checked_bool (argv[3]);

	if (stddev <= 0)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (pnorm (x, mean, stddev, TRUE, FALSE));
	else
	 	return value_new_float (dnorm (x, mean, stddev, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_norminv[] = {
	{ GNM_FUNC_HELP_NAME, F_("NORMINV:inverse of the cumulative distribution function of a normal distribution")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability")},
	{ GNM_FUNC_HELP_ARG, F_("mean:mean of the distribution")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:standard deviation of the distribution")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 or @{stddev} <= 0 this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NORMINV(0.76,2,3)" },
	{ GNM_FUNC_HELP_SEEALSO, "NORMDIST,NORMSDIST,NORMSINV,STANDARDIZE,ZTEST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_norminv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float p = value_get_as_float (argv[0]);
	gnm_float mean = value_get_as_float (argv[1]);
	gnm_float stddev = value_get_as_float (argv[2]);

	if (p < 0 || p > 1 || stddev <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qnorm (p, mean, stddev, TRUE, FALSE));
}


/***************************************************************************/

static GnmFuncHelp const help_kurt[] = {
	{ GNM_FUNC_HELP_NAME, F_("KURT:unbiased estimate of the kurtosis of a data set")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.") },
	{ GNM_FUNC_HELP_NOTE, F_("This is only meaningful if the underlying "
	   "distribution really has a fourth moment.  The kurtosis is "
	   "offset by three such that a normal distribution will have zero "
	   "kurtosis.") },
	{ GNM_FUNC_HELP_NOTE, F_("If fewer than four numbers are given or all of them are equal "
				 "this function returns a #DIV/0! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=KURT(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,VAR,SKEW,KURTP"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_kurt (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_kurtosis_m3_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_kurtp[] = {
	{ GNM_FUNC_HELP_NAME, F_("KURTP:population kurtosis of a data set")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.") },
	{ GNM_FUNC_HELP_NOTE, F_("If fewer than two numbers are given or all of them are equal "
           "this function returns a #DIV/0! error.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=KURTP(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,VARP,SKEWP,KURT"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_kurtp (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_kurtosis_m3_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_avedev[] = {
	{ GNM_FUNC_HELP_NAME, F_("AVEDEV:average of the absolute deviations of a data set")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=AVEDEV(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "STDEV"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_avedev (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_avedev,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_devsq[] = {
	{ GNM_FUNC_HELP_NAME, F_("DEVSQ:sum of squares of deviations of a data set")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=DEVSQ(11.4,17.3,21.3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "STDEV"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_devsq (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_devsq,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_fisher[] = {
	{ GNM_FUNC_HELP_NAME, F_("FISHER:Fisher transformation")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} is not a number, this function returns a #VALUE! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} <= -1 or @{x} >= 1, this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FISHER(0.332)" },
	{ GNM_FUNC_HELP_SEEALSO, "FISHERINV,ATANH"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fisher (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);

	if (x <= -1 || x >= 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_atanh (x));
}

/***************************************************************************/

static GnmFuncHelp const help_poisson[] = {
	{ GNM_FUNC_HELP_NAME, F_("POISSON:probability mass or cumulative distribution function of the Poisson distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number of events")},
	{ GNM_FUNC_HELP_ARG, F_("mean:mean of the distribution")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the mass function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} is a non-integer it is truncated.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{mean} <= 0 POISSON returns the #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=POISSON(3,6,0)"},
	{ GNM_FUNC_HELP_SEEALSO, ("NORMDIST,WEIBULL")},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_poisson (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float mean = value_get_as_float (argv[1]);
	gboolean cuml = value_get_as_checked_bool (argv[2]);

	if (x < 0 || mean <= 0)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (ppois (x, mean, TRUE, FALSE));
	else
		return value_new_float (dpois (x, mean, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_pearson[] = {
	{ GNM_FUNC_HELP_NAME, F_("PEARSON:Pearson correlation coefficient of the paired set of data")},
	{ GNM_FUNC_HELP_ARG, F_("array1:first component values")},
	{ GNM_FUNC_HELP_ARG, F_("array2:second component values")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=PEARSON({1,2,3},{3,4,4})" },
	{ GNM_FUNC_HELP_SEEALSO, "INTERCEPT,LINEST,RSQ,SLOPE,STEYX"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pearson (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return gnumeric_correl (ei, argv);
}


/***************************************************************************/

static GnmFuncHelp const help_rsq[] = {
	{ GNM_FUNC_HELP_NAME, F_("RSQ:square of the Pearson correlation coefficient of the paired set of data")},
	{ GNM_FUNC_HELP_ARG, F_("array1:first component values")},
	{ GNM_FUNC_HELP_ARG, F_("array2:second component values")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=RSQ({1,2,3},{3,4,4})" },
	{ GNM_FUNC_HELP_SEEALSO, ("CORREL,COVAR,INTERCEPT,LINEST,LOGEST,PEARSON,SLOPE,STEYX,TREND")},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rsq (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      gnm_range_rsq_pop,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_median[] = {
	{ GNM_FUNC_HELP_NAME, F_("MEDIAN:median of a data set")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Strings and empty cells are simply ignored.") },
	{ GNM_FUNC_HELP_NOTE, F_("If even numbers are given MEDIAN returns the average of the two "
	   "numbers in the center.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MEDIAN(11.4,17.3,11.4,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,COUNT,COUNTA,DAVERAGE,MODE,SSMEDIAN,SUM"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Median") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:StatisticalMedian.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_median (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_median_inter_sorted,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_SORT,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_ssmedian[] = {
	{ GNM_FUNC_HELP_NAME, F_("SSMEDIAN:median for grouped data")},
	{ GNM_FUNC_HELP_ARG, F_("array:data set")},
	{ GNM_FUNC_HELP_ARG, F_("interval:length of each grouping interval, defaults to 1")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The data are assumed to be grouped into intervals of width @{interval}. "
					"Each data point in @{array} is the midpoint of the interval containing the true value. "
					"The median is calculated by interpolation within the median interval "
					"(the interval containing the median value), "
					"assuming that the true values within that interval are distributed uniformly:\n"
					"median = L + @{interval}*(N/2 - CF)/F\n"
					"where:\n"
					"L = the lower limit of the median interval\n"
					"N = the total number of data points\n"
					"CF = the number of data points below the median interval\n"
					"F = the number of data points in the median interval") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{array} is empty, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{interval} <= 0, this function returns a #NUM! error. "
	   "SSMEDIAN does not check whether the data points are "
	   "at least @{interval} apart.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SSMEDIAN(ARRAY(7,7,8,9), 1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=SSMEDIAN(ARRAY(7,7,8,8,9), 1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=SSMEDIAN(ARRAY(7,7,8,8,8,9), 1)" },
	{ GNM_FUNC_HELP_SEEALSO, "MEDIAN"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
gnumeric_ssmedian_calc (gnm_float const *sorted_data, int len,
			gnm_float mid_val, gnm_float interval)
{
	gnm_float L_lower = mid_val - interval / 2;
	gnm_float L_upper = mid_val + interval / 2;
	int f_below = 0;
	int f_within = 0;
	int i;

	for (i = 0; i < len; i++) {
		if (*sorted_data < L_lower)
			f_below++;
		else if (*sorted_data <= L_upper)
			f_within++;
		else
			break;
		sorted_data++;
	}

	return L_lower + (len / GNM_const(2.) - f_below) * interval / f_within;
}

static GnmValue *
gnumeric_ssmedian (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	gnm_float interval;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_SORT,
				     &n, &result);
	if (result)
		goto done;
	interval = argv[1] ? value_get_as_float (argv[1]) : 1;

	if (interval <= 0 || n == 0) {
		result = value_new_error_NUM (ei->pos);
		goto done;
	}

	switch (n) {
	case (1):
		result = value_new_float (data[0]);
		break;
	case (2):
		result = value_new_float ((data[0] + data[1]) / 2);
		break;
	default:
		if (n % 2 == 0) {
			gnm_float m1 = data[n / 2];
			gnm_float m0 = data[n / 2 - 1];
			result = value_new_float
				(m0 == m1
				 ? gnumeric_ssmedian_calc (data, n,
							   m1, interval)
				 : (m0 + m1) / 2);
		} else {
			result = value_new_float
				(gnumeric_ssmedian_calc
				 (data, n, data[n / 2], interval));
		}
	}

 done:
	g_free (data);
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_large[] = {
	{ GNM_FUNC_HELP_NAME, F_("LARGE:@{k}-th largest value in a data set")},
	{ GNM_FUNC_HELP_ARG, F_("data:data set")},
	{ GNM_FUNC_HELP_ARG, F_("k:which value to find")},
	{ GNM_FUNC_HELP_NOTE, F_("If data set is empty this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{k} <= 0 or @{k} is greater than the number of data items given "
				 "this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then LARGE(A1:A5,2) equals 25.9. "
				     "LARGE(A1:A5,4) equals 17.3.") },
	{ GNM_FUNC_HELP_SEEALSO, "PERCENTILE,PERCENTRANK,QUARTILE,SMALL"},
	{ GNM_FUNC_HELP_END }
};

static int
gnm_kth (gnm_float k)
{
	if (k < 1)
		// This just makes us get an error unless we're very
		// close to 1.
		k = gnm_fake_floor (k);
	else
		k = gnm_fake_ceil (k);

	if (k < 1 || k >= INT_MAX)
		return 0;
	else
		return (int)k;
}

static GnmValue *
gnumeric_large (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int n;
	GnmValue *res = NULL;
	gnm_float *xs = collect_floats_value (argv[0], ei->pos,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS |
					      COLLECT_SORT,
					      &n, &res);
	int ki = gnm_kth (value_get_as_float (argv[1]));
	if (res)
		return res;

	if (ki >= 1 && ki <= n)
		res = value_new_float (xs[n - ki]);
	else
		res = value_new_error_NUM (ei->pos);

	g_free (xs);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_small[] = {
	{ GNM_FUNC_HELP_NAME, F_("SMALL:@{k}-th smallest value in a data set")},
	{ GNM_FUNC_HELP_ARG, F_("data:data set")},
	{ GNM_FUNC_HELP_ARG, F_("k:which value to find")},
	{ GNM_FUNC_HELP_NOTE, F_("If data set is empty this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{k} <= 0 or @{k} is greater than the number of data items given "
				 "this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then SMALL(A1:A5,2) equals 17.3. "
				     "SMALL(A1:A5,4) equals 25.9.") },
	{ GNM_FUNC_HELP_SEEALSO, "PERCENTILE,PERCENTRANK,QUARTILE,LARGE"},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_small (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int n;
	GnmValue *res = NULL;
	gnm_float *xs = collect_floats_value (argv[0], ei->pos,
					      COLLECT_IGNORE_STRINGS |
					      COLLECT_IGNORE_BOOLS |
					      COLLECT_IGNORE_BLANKS |
					      COLLECT_SORT,
					      &n, &res);
	int ki = gnm_kth (value_get_as_float (argv[1]));
	if (res)
		return res;

	if (ki >= 1 && ki <= n)
		res = value_new_float (xs[ki - 1]);
	else
		res = value_new_error_NUM (ei->pos);

	g_free (xs);
	return res;
}

/***********************************************************************/

static GnmFuncHelp const help_prob[] = {
	{ GNM_FUNC_HELP_NAME, F_("PROB:probability of an interval for a discrete (and finite) probability distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x_range:possible values")},
	{ GNM_FUNC_HELP_ARG, F_("prob_range:probabilities of the corresponding values")},
	{ GNM_FUNC_HELP_ARG, F_("lower_limit:lower interval limit")},
	{ GNM_FUNC_HELP_ARG, F_("upper_limit:upper interval limit, defaults to @{lower_limit}")},
	{ GNM_FUNC_HELP_NOTE, F_("If the sum of the probabilities in @{prob_range} is not equal to 1 "
				 "this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If any value in @{prob_range} is <=0 or > 1, this function returns a #NUM! "
				 "error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x_range} and @{prob_range} contain a different number of data "
				 "entries, this function returns a #N/A error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "BINOMDIST,CRITBINOM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_prob (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue *res;
	GnmValue *error = NULL;
	int i, prob_n, x_n;
	gnm_float *prob_vals = NULL, *x_vals = NULL;
	gnm_float lower_limit, upper_limit;
	gnm_float total_sum = 0, sum = 0;

	lower_limit = value_get_as_float (argv[2]);
	upper_limit = argv[3] ? value_get_as_float (argv[3]) : lower_limit;

	x_vals = collect_floats_value
		(argv[0], ei->pos,
		 COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS |
		 COLLECT_IGNORE_BLANKS,
		 &x_n, &error);
	if (error) {
		res = error;
		goto out;
	}

	prob_vals = collect_floats_value
		(argv[1], ei->pos,
		 COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS |
		 COLLECT_IGNORE_BLANKS,
		 &prob_n, &error);
	if (error) {
		res = error;
		goto out;
	}

	if (x_n != prob_n) {
		res = value_new_error_NA (ei->pos);
		goto out;
	}

	for (i = 0; i < x_n; i++) {
		gnm_float x = x_vals[i];
		gnm_float prob = prob_vals[i];

		/* FIXME: Check "0" behaviour with Excel and comment.  */
		if (prob <= 0 || prob > 1) {
			res = value_new_error_NUM (ei->pos);
			goto out;
		}

		total_sum += prob;

		if (x >= lower_limit && x <= upper_limit)
			sum += prob;
	}

	if (gnm_abs (total_sum - 1) > x_n * 2 * GNM_EPSILON) {
		res = value_new_error_NUM (ei->pos);
		goto out;
	}

	res = value_new_float (sum);

 out:
	g_free (x_vals);
	g_free (prob_vals);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_steyx[] = {
	{ GNM_FUNC_HELP_NAME, F_("STEYX:standard error of the predicted y-value in the regression")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{known_ys} and @{known_xs} are empty or have a different "
				 "number of arguments then this function returns a #N/A error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
				     "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then STEYX(A1:A5,B1:B5) equals 1.101509979.") },
	{ GNM_FUNC_HELP_SEEALSO, "PEARSON,RSQ,SLOPE"},
	{ GNM_FUNC_HELP_END }
};

static int
range_steyx (gnm_float const *xs, gnm_float const *ys, int n, gnm_float *res)
{
	gnm_float linres[2];
	int dim = 1;
	GORegressionResult regres;
	gnm_regression_stat_t *extra_stat;
	gboolean affine = TRUE;

	extra_stat = gnm_regression_stat_new ();
	regres = gnm_linear_regression ((gnm_float **)&xs, dim,
					ys, n, affine, linres, extra_stat);
	*res = gnm_sqrt (extra_stat->var);
	gnm_regression_stat_destroy (extra_stat);

	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		return 0;
	default:
		return 1;
	}
}

static GnmValue *
gnumeric_steyx (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[1], argv[0],
				      ei,
				      range_steyx,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_ztest[] = {
	{ GNM_FUNC_HELP_NAME, F_("ZTEST:the probability of observing a sample mean as large as "
				 "or larger than the mean of the given sample")},
	{ GNM_FUNC_HELP_ARG, F_("ref:data set (sample)")},
	{ GNM_FUNC_HELP_ARG, F_("x:population mean")},
	{ GNM_FUNC_HELP_ARG, F_("stddev:population standard deviation, defaults to the sample standard deviation")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ZTEST calculates the probability of observing a sample mean as large as "
				 "or larger than the mean of the given sample for samples drawn "
				 "from a normal distribution with mean @{x} and standard deviation @{stddev}.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{ref} contains less than two data items ZTEST "
				 "returns #DIV/0! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then ZTEST(A1:A5,20) equals 0.254717826.")},
	{ GNM_FUNC_HELP_SEEALSO, ("CONFIDENCE,NORMDIST,NORMINV,NORMSDIST,NORMSINV,STANDARDIZE")},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ztest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int n;
	gnm_float *xs;
	GnmValue *result = NULL;
	gnm_float x, s, m, p;

	xs = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS,
				   &n, &result);
	if (result)
		goto done;

	x = value_get_as_float (argv[1]);

	if (gnm_range_average (xs, n, &m)) {
		result = value_new_error_DIV0 (ei->pos);
		goto done;
	}

	if (argv[2])
		s = value_get_as_float (argv[2]);
	else if (gnm_range_stddev_est (xs, n, &s)) {
		result = value_new_error_DIV0 (ei->pos);
		goto done;
	}

	if (s <= 0) {
		result = value_new_error_DIV0 (ei->pos);
		goto done;
	}

	p = pnorm (x, m, s / gnm_sqrt (n), TRUE, FALSE);
	result = value_new_float (p);

done:
	g_free (xs);
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_averagea[] = {
	{ GNM_FUNC_HELP_NAME, F_("AVERAGEA:average of all the values and cells")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	HELP_DESCRIPTION_TEXT_INCLUSION,
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=AVERAGE(11.4,17.3,21.3,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_averagea (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_average,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_maxa[] = {
	{ GNM_FUNC_HELP_NAME, F_("MAXA:largest value, with negative numbers considered smaller than positive numbers")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	HELP_DESCRIPTION_TEXT_INCLUSION,
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MAXA(11.4,17.3,21.3,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "MAX,MINA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_maxa (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_max0,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_ORDER_IRRELEVANT,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_mina[] = {
	{ GNM_FUNC_HELP_NAME, F_("MINA:smallest value, with negative numbers considered smaller than positive numbers")},
	{ GNM_FUNC_HELP_ARG, F_("number1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("number2:second value")},
	HELP_DESCRIPTION_TEXT_INCLUSION,
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MINA(11.4,17.3,21.3,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "MIN,MAXA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_mina (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_min0,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_ORDER_IRRELEVANT,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_vara[] = {
	{ GNM_FUNC_HELP_NAME, F_("VARA:sample variance of the given sample")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("VARA is also known as the N-1-variance.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("To get the true variance of a complete population use VARPA.") },
	HELP_DESCRIPTION_TEXT_INCLUSION,
	{ GNM_FUNC_HELP_NOTE, F_("Since the N-1-variance includes Bessel's correction, whereas the N-variance calculated by VARPA or VARP does not, "
				 "under reasonable conditions the N-1-variance is an unbiased estimator of the variance of the population "
				 "from which the sample is drawn.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=VARA(11.4,17.3,21.3,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "VAR,VARPA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_vara (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_var_est,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_varpa[] = {
	{ GNM_FUNC_HELP_NAME, F_("VARPA:variance of an entire population")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("VARPA is also known as the N-variance.") },
	HELP_DESCRIPTION_TEXT_INCLUSION,
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=VARPA(11.4,17.3,21.3,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "VARA,VARP"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_varpa (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_var_pop,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_stdeva[] = {
	{ GNM_FUNC_HELP_NAME, F_("STDEVA:sample standard deviation of the given "
	   "sample")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("STDEVA is also known as the N-1-standard deviation.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("To obtain the population standard deviation of a whole population "
					"use STDEVPA.")},
	HELP_DESCRIPTION_TEXT_INCLUSION,
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=STDEVA(11.4,17.3,21.3,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "STDEV,STDEVPA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_stdeva (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_stddev_est,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_stdevpa[] = {
	{ GNM_FUNC_HELP_NAME, F_("STDEVPA:population standard deviation of an entire population")},
	{ GNM_FUNC_HELP_ARG, F_("area1:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area2:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This is also known as the N-standard deviation")},
	HELP_DESCRIPTION_TEXT_INCLUSION,
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=STDEVPA(11.4,17.3,21.3,3,25.9,40.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "STDEVA,STDEVP"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_stdevpa (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_stddev_pop,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_percentrank[] = {
	{ GNM_FUNC_HELP_NAME, F_("PERCENTRANK:rank of a data point in a data set (Hyndman-Fan method 7: N-1 basis)")},
	{ GNM_FUNC_HELP_ARG, F_("array:range of numeric values")},
	{ GNM_FUNC_HELP_ARG, F_("x:data point to be ranked")},
	{ GNM_FUNC_HELP_ARG, F_("significance:number of significant digits, defaults to 3")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array} contains no data points, this function returns a #NUM! "
				 "error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{significance} is less than one, this function returns a #NUM! "
				 "error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} exceeds the largest value or is less than the smallest "
				 "value in @{array}, this function returns an #N/A error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} does not match any of the values in @{array} or @{x} matches "
				 "more than once, this function interpolates the returned value.") },
	{ GNM_FUNC_HELP_SEEALSO, "LARGE,MAX,MEDIAN,MIN,PERCENTILE,QUARTILE,SMALL"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_percentrank (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *data, x, significance, r;
	GnmValue *result = NULL;
	int i, n;
	int n_equal, n_smaller, n_larger;
	gnm_float x_larger, x_smaller;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_ORDER_IRRELEVANT,
				     &n, &result);
	x = value_get_as_float (argv[1]);
	significance = argv[2] ? value_get_as_float (argv[2]) : 3;

	if (result)
		goto done;

	if (n == 0) {
		result = value_new_error_NUM (ei->pos);
		goto done;
	}

	n_equal = n_smaller = n_larger = 0;
	x_larger = x_smaller = 42;
	for (i = 0; i < n; i++) {
		gnm_float y = data[i];

		if (y < x) {
			if (n_smaller == 0 || x_smaller < y)
				x_smaller = y;
			n_smaller++;
		} else if (y > x) {
			if (n_larger == 0 || x_larger > y)
				x_larger = y;
			n_larger++;
		} else
			n_equal++;
	}

	if (n_smaller + n_equal == 0 || n_larger + n_equal == 0) {
		result = value_new_error_NA (ei->pos);
		goto done;
	}

	if (n == 1)
		r = 1;
	else {
		gnm_float s10;

		if (n_equal > 0)
			r = n_smaller / (gnm_float)(n - 1);
		else {
			gnm_float r1 = (n_smaller - 1) / (gnm_float)(n - 1);
			gnm_float r2 = n_smaller / (gnm_float)(n - 1);
			r = (r1 * (x_larger - x) +
			     r2 * (x - x_smaller)) / (x_larger - x_smaller);
		}

		/* A strange place to check, but n==1 is special.  */
		if (significance < 1) {
			result = value_new_error_NUM (ei->pos);
			goto done;
		}

		s10 = gnm_pow10 (-significance);
		if (s10 <= 0) {
			result = value_new_error_DIV0 (ei->pos);
			goto done;
		}

		r = gnm_fake_trunc (r / s10) * s10;
	}
	result = value_new_float (r);

 done:
	g_free (data);
	return result;
}

/***************************************************************************/



static GnmFuncHelp const help_percentrank_exc[] = {
	{ GNM_FUNC_HELP_NAME, F_("PERCENTRANK.EXC:rank of a data point in a data set (Hyndman-Fan method 6: N+1 basis)")},
	{ GNM_FUNC_HELP_ARG, F_("array:range of numeric values")},
	{ GNM_FUNC_HELP_ARG, F_("x:data point to be ranked")},
	{ GNM_FUNC_HELP_ARG, F_("significance:number of significant digits, defaults to 3")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array} contains no data points, this function returns a #NUM! "
				 "error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{significance} is less than one, this function returns a #NUM! "
				 "error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} exceeds the largest value or is less than the smallest "
				 "value in @{array}, this function returns an #N/A error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} does not match any of the values in @{array} or @{x} matches "
				 "more than once, this function interpolates the returned value.") },
	{ GNM_FUNC_HELP_SEEALSO, "LARGE,MAX,MEDIAN,MIN,PERCENTILE,PERCENTILE.EXC,QUARTILE,QUARTILE.EXC,SMALL"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_percentrank_exc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *data, x, significance, r;
	GnmValue *result = NULL;
	int i, n;
	int n_equal, n_smaller, n_larger;
	gnm_float x_larger, x_smaller;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_ORDER_IRRELEVANT,
				     &n, &result);
	x = value_get_as_float (argv[1]);
	significance = argv[2] ? value_get_as_float (argv[2]) : 3;

	if (result)
		goto done;

	if (n == 0) {
		result = value_new_error_NUM (ei->pos);
		goto done;
	}

	n_equal = n_smaller = n_larger = 0;
	x_larger = x_smaller = 42;
	for (i = 0; i < n; i++) {
		gnm_float y = data[i];

		if (y < x) {
			if (n_smaller == 0 || x_smaller < y)
				x_smaller = y;
			n_smaller++;
		} else if (y > x) {
			if (n_larger == 0 || x_larger > y)
				x_larger = y;
			n_larger++;
		} else
			n_equal++;
	}

	if (n_smaller + n_equal == 0 || n_larger + n_equal == 0) {
		result = value_new_error_NA (ei->pos);
		goto done;
	}

	if (n == 1)
		r = 1;
	else {
		gnm_float s10;

		/* A strange place to check, but n==1 is special.  */
		if (significance < 1) {
			result = value_new_error_NUM (ei->pos);
			goto done;
		}
		s10 = gnm_pow10 (-significance);
		if (s10 <= 0) {
			result = value_new_error_DIV0 (ei->pos);
			goto done;
		}

		if (n_equal > 0)
			r = (n_smaller + 1) / (gnm_float)(n + 1);
		else {
			gnm_float r1 = n_smaller / (gnm_float)(n + 1);
			gnm_float r2 = (n_smaller + 1) / (gnm_float)(n + 1);
			r = (r1 * (x_larger - x) +
			     r2 * (x - x_smaller)) / (x_larger - x_smaller);
		}

		r = gnm_fake_trunc (r / s10) * s10;
	}
	result = value_new_float (r);

 done:
	g_free (data);
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_percentile[] = {
	{ GNM_FUNC_HELP_NAME, F_("PERCENTILE:determines the 100*@{k}-th percentile of the given data points (Hyndman-Fan method 7: N-1 basis)")},
	{ GNM_FUNC_HELP_ARG, F_("array:data points")},
	{ GNM_FUNC_HELP_ARG, F_("k:which percentile to calculate")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array} is empty, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{k} < 0 or @{k} > 1, this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then PERCENTILE(A1:A5,0.42) equals 20.02.") },
	{ GNM_FUNC_HELP_SEEALSO, "QUARTILE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_percentile (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_SORT,
				     &n, &result);
	if (!result) {
		gnm_float p = value_get_as_float (argv[1]);
		gnm_float res;

		if (gnm_range_fractile_inter_sorted (data, n, &res, p))
			result = value_new_error_NUM (ei->pos);
		else
			result = value_new_float (res);
	}

	g_free (data);
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_percentile_exc[] = {
	{ GNM_FUNC_HELP_NAME, F_("PERCENTILE.EXC:determines the 100*@{k}-th percentile of the given data points (Hyndman-Fan method 6: N+1 basis)")},
	{ GNM_FUNC_HELP_ARG, F_("array:data points")},
	{ GNM_FUNC_HELP_ARG, F_("k:which percentile to calculate")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array} is empty, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{k} < 0 or @{k} > 1, this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then PERCENTILE.EXC(A1:A5,0.42) equals 20.02.") },
	{ GNM_FUNC_HELP_SEEALSO, "PERCENTILE,QUARTILE,QUARTILE.EXC"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_percentile_exc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_SORT,
				     &n, &result);
	if (!result) {
		if (n > 1) {
			gnm_float p = value_get_as_float (argv[1]);
			gnm_float res;
			gnm_float fr = (p * (n + 1) - 1)/(n-1);

			if (gnm_range_fractile_inter_sorted (data, n, &res, fr))
				result = value_new_error_NUM (ei->pos);
			else
				result = value_new_float (res);
		} else
			result = value_new_error_NUM (ei->pos);
	}

	g_free (data);
	return result;
}
/***************************************************************************/

static GnmFuncHelp const help_quartile[] = {
	{ GNM_FUNC_HELP_NAME, F_("QUARTILE:the @{k}-th quartile of the data points (Hyndman-Fan method 7: N-1 basis)")},
	{ GNM_FUNC_HELP_ARG, F_("array:data points")},
	{ GNM_FUNC_HELP_ARG, F_("quart:a number from 0 to 4, indicating which quartile to calculate")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array} is empty, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{quart} < 0 or @{quart} > 4, this function returns a #NUM! error. If @{quart} = 0, the smallest value of @{array} to be returned.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{quart} is not an integer, it is truncated.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then QUARTILE(A1:A5,1) equals 17.3.") },
	{ GNM_FUNC_HELP_SEEALSO, "LARGE,MAX,MEDIAN,MIN,PERCENTILE,QUARTILE.EXC,SMALL"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_quartile (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_SORT,
				     &n, &result);
	if (!result) {
		gnm_float q = gnm_fake_floor (value_get_as_float (argv[1]));
		gnm_float res;

		if (gnm_range_fractile_inter_sorted (data, n, &res, q / 4))
			result = value_new_error_NUM (ei->pos);
		else
			result = value_new_float (res);
	}

	g_free (data);
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_quartile_exc[] = {
	{ GNM_FUNC_HELP_NAME, F_("QUARTILE.EXC:the @{k}-th quartile of the data points (Hyndman-Fan method 6: N+1 basis)")},
	{ GNM_FUNC_HELP_ARG, F_("array:data points")},
	{ GNM_FUNC_HELP_ARG, F_("quart:a number from 1 to 3, indicating which quartile to calculate")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array} is empty, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{quart} < 0 or @{quart} > 4, this function returns a #NUM! error. If @{quart} = 0, the smallest value of @{array} to be returned.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{quart} is not an integer, it is truncated.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3, 21.3, 25.9, and 40.1.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then QUARTILE.EXC(A1:A5,1) equals 14.35.") },
	{ GNM_FUNC_HELP_SEEALSO, "LARGE,MAX,MEDIAN,MIN,PERCENTILE,PERCENTILE.EXC,QUARTILE,SMALL"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_quartile_exc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS |
				     COLLECT_SORT,
				     &n, &result);
	if (!result) {
		if (n > 1) {
			gnm_float q = gnm_fake_floor (value_get_as_float (argv[1]));
			gnm_float res;
			gnm_float fr = ((q / 4) * (n + 1) - 1)/(n-1);

			if (gnm_range_fractile_inter_sorted (data, n, &res, fr))
				result = value_new_error_NUM (ei->pos);
			else
				result = value_new_float (res);
		} else
			result = value_new_error_NUM (ei->pos);
	}

	g_free (data);
	return result;
}
/***************************************************************************/

static GnmFuncHelp const help_ftest[] = {
	{ GNM_FUNC_HELP_NAME, F_("FTEST:p-value for the two-tailed hypothesis test comparing the "
				 "variances of two populations")},
	{ GNM_FUNC_HELP_ARG, F_("array1:sample from the first population")},
	{ GNM_FUNC_HELP_ARG, F_("array2:sample from the second population")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
				     "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then FTEST(A1:A5,B1:B5) equals 0.510815017.") },
	{ GNM_FUNC_HELP_SEEALSO, "FDIST,FINV"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ftest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	CollectFlags flags = COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS |
		COLLECT_IGNORE_BLANKS;
	gnm_float *xs = NULL, *ys = NULL;
	int nx, ny;
	GnmValue *res = NULL;
	gnm_float p, varx, vary;

	xs = collect_floats_value (argv[0], ei->pos, flags, &nx, &res);
	if (res)
		goto out;

	ys = collect_floats_value (argv[1], ei->pos, flags, &ny, &res);
	if (res)
		goto out;

	if (gnm_range_var_est (xs, nx, &varx) ||
	    gnm_range_var_est (ys, ny, &vary) ||
	    vary == 0) {
		res = value_new_error_DIV0 (ei->pos);
		goto out;
	}

	p = pf (varx / vary, nx - 1, ny - 1, FALSE, FALSE);
	if (p > GNM_const(0.5)) {
		/*
		 * We need the other tail and 1-p might not be very accurate.
		 */
		p = pf (varx / vary, nx - 1, ny - 1, TRUE, FALSE);
	}

	res = value_new_float (2 * p);

out:
	g_free (xs);
	g_free (ys);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_ttest[] = {
	{ GNM_FUNC_HELP_NAME, F_("TTEST:p-value for a hypothesis test comparing the means of two populations using "
				 "the Student t-distribution")},
	{ GNM_FUNC_HELP_ARG, F_("array1:sample from the first population")},
	{ GNM_FUNC_HELP_ARG, F_("array2:sample from the second population")},
	{ GNM_FUNC_HELP_ARG, F_("tails:number of tails to consider")},
	{ GNM_FUNC_HELP_ARG, F_("type:Type of test to perform. 1 indicates a test for paired variables, "
				"2 a test of unpaired variables with equal variances, and "
				"3 a test of unpaired variables with unequal variances")},
	{ GNM_FUNC_HELP_NOTE, F_("If the data sets contain a different number of data points and "
				 "the test is paired (@{type} one), TTEST returns the #N/A error.") },
	{ GNM_FUNC_HELP_NOTE, F_("@{tails} and @{type} are truncated to integers.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{tails} is not one or two, this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{type} is any other than one, two, or three, this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
				     "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then TTEST(A1:A5,B1:B5,1,1) equals 0.003127619. "
				     "TTEST(A1:A5,B1:B5,2,1) equals 0.006255239. "
				     "TTEST(A1:A5,B1:B5,1,2) equals 0.111804322. "
				     "TTEST(A1:A5,B1:B5,1,3) equals 0.113821797.") },
	{ GNM_FUNC_HELP_SEEALSO, "FDIST,FINV"},
	{ GNM_FUNC_HELP_END }
};

static int barf_ttest_dof;

static int
calc_ttest_paired (gnm_float const *xs, gnm_float const *ys, int n,
		   gnm_float *res)
{
	gnm_float *zs, zm, zsig;
	gboolean err;
	int i;

	if (n == 0)
		return 1;

	/* zs = xs - ys */
	zs = go_memdup_n (xs, n, sizeof (*xs));
	for (i = 0; i < n; i++)
		zs[i] -= ys[i];

	err = (gnm_range_average (zs, n, &zm) ||
	       gnm_range_stddev_est (zs, n, &zsig) ||
	       zsig == 0);
	g_free (zs);

	if (err)
		return 1;
	else {
		*res = gnm_sqrt (n) * (zm / zsig);
		/* We need this n out of here.  For now, hack it.  */
		barf_ttest_dof = n - 1;
		return 0;
	}
}

static GnmValue *
ttest_paired (GnmFuncEvalInfo *ei,
	      GnmValue const *r0, GnmValue const *r1, int tails)
{
	int w0 = value_area_get_width (r0, ei->pos);
	int h0 = value_area_get_height (r0, ei->pos);
	int w1 = value_area_get_width (r1, ei->pos);
	int h1 = value_area_get_height (r1, ei->pos);
	GnmValue *v;
	gnm_float x;

	/* Size error takes precedence over everything else.  */
	if (w0 * h0 != w1 * h1)
		return value_new_error_NA (ei->pos);

	v = float_range_function2 (r0, r1, ei, calc_ttest_paired,
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS,
				   GNM_ERROR_DIV0);

	if (!VALUE_IS_NUMBER (v))
		return v;

	x = value_get_as_float (v);
	value_release (v);

	return value_new_float (tails * pt (gnm_abs (x), barf_ttest_dof,
					    FALSE, FALSE));
}

static GnmValue *
ttest_equal_unequal (GnmFuncEvalInfo *ei,
		     GnmValue const *rx, GnmValue const *ry, int tails,
		     gboolean unequal)
{
	CollectFlags flags = COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS |
		COLLECT_IGNORE_BLANKS;
	gnm_float *xs = NULL, *ys = NULL;
	int nx, ny;
	GnmValue *res = NULL;
	gnm_float mx, vx, my, vy, dof, t;

	xs = collect_floats_value (rx, ei->pos, flags, &nx, &res);
	if (res)
		goto out;

	ys = collect_floats_value (ry, ei->pos, flags, &ny, &res);
	if (res)
		goto out;

	if (gnm_range_average (xs, nx, &mx) ||
	    gnm_range_var_est (xs, nx, &vx) ||
	    gnm_range_average (ys, ny, &my) ||
	    gnm_range_var_est (ys, ny, &vy)) {
		res = value_new_error_DIV0 (ei->pos);
		goto out;
	}

	if (vx == 0 && vy == 0) {
		/* Note sure here.  */
		res = value_new_error_DIV0 (ei->pos);
		goto out;
	}

	if (unequal) {
		gnm_float S = (vx / nx + vy / ny);
		gnm_float c = (vx / nx) / S;
		gnm_float cC = (vy / ny) / S;
		dof = 1 / (c * c / (nx - 1) + cC * cC / (ny - 1));
		t = gnm_abs (mx - my) / gnm_sqrt (S);
	} else {
		dof = nx + ny - 2;
		t = (gnm_abs (mx - my) *
		     gnm_sqrt (dof * nx * ny /
			       ((nx + ny) * ((nx - 1) * vx + (ny - 1) * vy))));
	}
	res = value_new_float (tails * pt (t, dof, FALSE, FALSE));

out:
	g_free (xs);
	g_free (ys);
	return res;
}

static GnmValue *
gnumeric_ttest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float tails = value_get_as_float (argv[2]);
	gnm_float type = value_get_as_float (argv[3]);
	int itails;

	if (tails != 1 && tails != 2)
		return value_new_error_NUM (ei->pos);
	itails = (int)tails;

	if (type != 1 && type != 2 && type != 3)
		return value_new_error_NUM (ei->pos);

	switch ((int)type) {
	case 1:
		return ttest_paired (ei, argv[0], argv[1], itails);

	case 2:
		return ttest_equal_unequal (ei, argv[0], argv[1], itails, FALSE);

	case 3:
		return ttest_equal_unequal (ei, argv[0], argv[1], itails, TRUE);

	default:
		return value_new_error_NUM (ei->pos);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_frequency[] = {
	{ GNM_FUNC_HELP_NAME, F_("FREQUENCY:frequency table")},
	{ GNM_FUNC_HELP_ARG, F_("data_array:data values")},
	{ GNM_FUNC_HELP_ARG, F_("bins_array:array of cutoff values")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The results are given as an array.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If the @{bins_array} is empty, this function "
					"returns the number of data points "
					"in @{data_array}.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_frequency (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	CollectFlags flags = COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS |
		COLLECT_IGNORE_BLANKS;
	GnmValue *error = NULL, *res;
	int *counts;
	int i, nvalues, nbins;
	gnm_float *values = NULL, *bins = NULL;

	values = collect_floats_value (argv[0], ei->pos, flags,
				       &nvalues, &error);
	if (error) {
		res = error;
		goto out;
	}

	bins = collect_floats_value (argv[1], ei->pos, flags | COLLECT_SORT,
				     &nbins, &error);
	if (error) {
		res = error;
		goto out;
	}

	/* Special case.  */
	if (nbins == 0) {
		res = value_new_int (nvalues);
		goto out;
	}

	counts = g_new0 (int, nbins + 1);

	/* Stupid code.  */
	for (i = 0; i < nvalues; i++) {
		int j;
		for (j = 0; j < nbins; j++)
			if (values[i] <= bins[j])
				break;
		counts[j]++;
	}

	res = value_new_array_non_init (1, nbins + 1);
	res->v_array.vals[0] = g_new (GnmValue *, nbins + 1);
	for (i = 0; i < nbins + 1; i++)
		res->v_array.vals[0][i] = value_new_float (counts[i]);
	g_free (counts);

 out:
	g_free (values);
	g_free (bins);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_leverage[] = {
	{ GNM_FUNC_HELP_NAME, F_("LEVERAGE:calculate regression leverage")},
	{ GNM_FUNC_HELP_ARG, F_("A:a matrix")},
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("Returns the diagonal of @{A} (@{A}^T @{A})^-1 @{A}^T as a column vector.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the matrix is singular, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_leverage (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmValue *res = NULL;
	GORegressionResult regres;
	gnm_float *x;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	if (gnm_matrix_is_empty (A)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	x = g_new (gnm_float, A->rows);

	regres = gnm_linear_regression_leverage (A->data, x, A->rows, A->cols);

	if (regres != GO_REG_ok && regres != GO_REG_near_singular_good) {
		res = value_new_error_NUM (ei->pos);
	} else {
		int x_rows = A->rows, x_cols = 1;
		int c, r;

		res = value_new_array_non_init (x_cols, x_rows);
		for (c = 0; c < x_cols; c++) {
			res->v_array.vals[c] = g_new (GnmValue *, x_rows);
			for (r = 0; r < x_rows; r++)
				res->v_array.vals[c][r] =
					value_new_float (x[r]);
		}
	}

	g_free (x);

out:
	if (A) gnm_matrix_unref (A);
	return res;
}

/***************************************************************************/

/* Notes for now, to be incorporated into help when it actually works:
 *
 * Entered as linest(Yrange, [Xrange, [Intercept, [Stat]]]). Intercept and Stat
 * work as above. According to playing with Excel, if Yrange is an array, Xrange
 * must be an array. They claim that you can use semicolons to separate rows in
 * an array, but I've never gotten that to work. If Yrange is a single column,
 * every column in the Xrange (which must be a cell range) is interpreted as a
 * separate variable. Similar for a single row. If Y is a blob, X is interpreted
 * as a single variable blob. Experiments suggest that multivariable blobs don't work.
 * Currently everything should be implemented except for inputting arrays. X's must
 * be contiguous so far as I can tell.
 */

typedef enum {
	gnm_reg_type_rect = 0,
	gnm_reg_type_vertical,
	gnm_reg_type_horizontal
} GnmRegType_t;

typedef struct {
	gnm_float *ys;
	int n;
	gnm_float **xss;
	int dim;
	GnmRegType_t type;
} GnmRegData;

static void
gnm_reg_data_free (GnmRegData *data)
{
	int i;

	g_free (data->ys);
	for (i = 0; i < data->dim; i++)
		g_free (data->xss[i]);
	g_free (data->xss);

	memset (data, 0, sizeof (*data));
}

static gnm_float *
gnm_reg_get_var (GnmValue const *xval, int x, int y, int dx, int dy,
		 int n, GnmEvalPos const *ep)
{
	gnm_float *res = g_new (gnm_float, n);
	int i;

	for (i = 0; i < n; i++) {
		GnmValue const *v = value_area_fetch_x_y (xval, x, y, ep);

		if (!VALUE_IS_FLOAT (v)) {
			/* Anything else is an error.  */
			g_free (res);
			return NULL;
		}

		res[i] = value_get_as_float (v);
		x += dx;
		y += dy;
	}

	return res;
}



static GnmValue *
gnm_reg_data_collect (GnmValue const *yval, GnmValue const *xval,
		      GnmRegData *data, GnmEvalPos const *ep)
{
	int yh = value_area_get_height (yval, ep);
	int yw = value_area_get_width (yval, ep);
	int ny;
	GnmValue *result = NULL;

	memset (data, 0, sizeof (*data));

	/* Blanks, bools, strings, errors all are forbidden.  */
	data->ys = collect_floats_value (yval, ep, 0,
					 &ny, &result);
	if (result || ny <= 0)
		goto error;
	data->n = ny;

	if (VALUE_IS_EMPTY (xval)) {
		int i;

		data->dim = 1;
		data->xss = g_new (gnm_float *, data->dim);
		data->xss[0] = g_new (gnm_float, ny);
		data->type = gnm_reg_type_rect;
		for (i = 0; i < ny; i++)
			data->xss[0][i] = i + 1;
	} else {
		int xh = value_area_get_height (xval, ep);
		int xw = value_area_get_width (xval, ep);
		int i, nx;

		if (yw == 1) {
			/* X's columns are the variables.  */
			if (xh != yh)
				goto ref_error;
			data->dim = xw;
			data->xss = g_new0 (gnm_float *, data->dim);
			data->type = gnm_reg_type_vertical;
			for (i = 0; i < data->dim; i++) {
				data->xss[i] = gnm_reg_get_var
					(xval, i, 0, 0, +1, xh, ep);
				if (!data->xss[i])
					goto error;
			}
		} else if (yh == 1) {
			/* X's rows are the variables.  */
			if (xw != yw)
				goto ref_error;
			data->dim = xh;
			data->xss = g_new0 (gnm_float *, data->dim);
			data->type = gnm_reg_type_horizontal;
			for (i = 0; i < data->dim; i++) {
				data->xss[i] = gnm_reg_get_var
					(xval, 0, i, +1, 0, xw, ep);
				if (!data->xss[i])
					goto error;
			}
		} else {
			if (xh != yh || xw != yw)
				goto ref_error;
			data->dim = 1;
			data->xss = g_new0 (gnm_float *, data->dim);
			data->xss[0] = collect_floats_value (xval, ep, 0,
							     &nx, &result);
			data->type = gnm_reg_type_rect;
			if (result)
				goto error;
		}
	}

	return NULL;

 error:
	value_release (result);
	/* Always this kind of error.  */
	result = value_new_error_VALUE (ep);
	gnm_reg_data_free (data);
	return result;

 ref_error:
	/* If the areas have the wrong shape, we get #REF!  */
	gnm_reg_data_free (data);
	return value_new_error_REF (ep);
}



static GnmFuncHelp const help_linest[] = {
	{ GNM_FUNC_HELP_NAME, F_("LINEST:multiple linear regression coefficients and statistics") },
	{ GNM_FUNC_HELP_ARG, F_("known_ys:vector of values of dependent variable") },
	{ GNM_FUNC_HELP_ARG, F_("known_xs:array of values of independent variables, defaults to a single vector {1,\xe2\x80\xa6,n}") },
	{ GNM_FUNC_HELP_ARG, F_("affine:if true, the model contains a constant term, defaults to true") },
	{ GNM_FUNC_HELP_ARG, F_("stats:if true, some additional statistics are provided, defaults to false") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns an array with the first row giving the regression "
					"coefficients for the independent variables "
					"x_m, x_(m-1),\xe2\x80\xa6,x_2, x_1 followed by the y-intercept if @{affine} is true.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{stats} is true, the second row contains the corresponding standard "
					"errors of the regression coefficients. "
					"In this case, the third row contains the R^2 value and the standard error "
					"for the predicted value. "
					"The fourth row contains the observed F value and its degrees of freedom. "
					"Finally, the fifth row contains the regression sum of squares and the "
					"residual sum of squares.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{affine} is false, R^2 is the uncentered version of the coefficient "
					"of determination; "
					"that is the proportion of the sum of squares explained by the model.")},
	{ GNM_FUNC_HELP_NOTE, F_("If the length of @{known_ys} does not match the corresponding length of @{known_xs}, "
				 "this function returns a #NUM! error.")},
	{ GNM_FUNC_HELP_SEEALSO, "LOGEST,TREND" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_linest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmRegData data;
	gnm_regression_stat_t *extra_stat = NULL;
	GORegressionResult regres;
	GnmValue *result;
	gnm_float *linres = NULL;
	gboolean affine, withstat;
	int i, dim;

	result = gnm_reg_data_collect (argv[0], argv[1], &data, ei->pos);
	if (result)
		return result;
	dim = data.dim;

	affine = argv[2] ? value_get_as_checked_bool (argv[2]) : TRUE;
	withstat = argv[3] ? value_get_as_checked_bool (argv[3]) : FALSE;

	linres = g_new (gnm_float, dim + 1);
	extra_stat = gnm_regression_stat_new ();
	regres = gnm_linear_regression (data.xss, dim,
					data.ys, data.n, affine,
					linres, extra_stat);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	if (withstat) {
		result = value_new_array (dim + 1, 5);

		for (i = 2; i <= dim; i++) {
			value_array_set (result, i, 2,
					 value_new_error_NA (ei->pos));
			value_array_set (result, i, 3,
					 value_new_error_NA (ei->pos));
			value_array_set (result, i, 4,
					 value_new_error_NA (ei->pos));
		}

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat->sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (gnm_sqrt (extra_stat->var)));
		value_array_set (result, 0, 3,
				 value_new_float (extra_stat->F));
		value_array_set (result, 1, 3,
				 value_new_float (extra_stat->df_resid));
		value_array_set (result, 0, 4,
				 value_new_float (extra_stat->ss_reg));
		value_array_set (result, 1, 4,
				 value_new_float (extra_stat->ss_resid));
		for (i = 0; i < dim; i++)
			value_array_set (result, dim - i - 1, 1,
					 value_new_float (extra_stat->se[i+affine]));
		value_array_set (result, dim, 1,
				 affine ? value_new_float (extra_stat->se[0])
				 : value_new_error_NA (ei->pos));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (linres[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0,
				 value_new_float (linres[i + 1]));

 out:
	gnm_reg_data_free (&data);
	g_free (linres);
	gnm_regression_stat_destroy (extra_stat);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_logreg[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOGREG:the logarithmic regression")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values; defaults to the array {1, 2, 3, \xe2\x80\xa6}")},
	{ GNM_FUNC_HELP_ARG, F_("affine:if true, the model contains a constant term, defaults to true")},
	{ GNM_FUNC_HELP_ARG, F_("stat:if true, extra statistical information will be returned; defaults to FALSE")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("LOGREG function transforms your x's to z=ln(x) and "
					"applies the \xe2\x80\x9cleast squares\xe2\x80\x9d method to fit the linear equation "
					"y = m * z + b "
					"to your y's and z's --- equivalent to fitting the equation "
					"y = m * ln(x) + b "
					"to y's and x's. "
					"LOGREG returns an array having two columns and one row. "
					"m is given in the first column and b in the second.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Any extra statistical information is written below m and b in the "
					"result array.  This extra statistical information consists of four "
					"rows of data:  In the first row the standard error values for the "
					"coefficients m, b are given.  The second row "
					"contains the square of R and the standard error for the y "
					"estimate. The third row contains the F-observed value and the "
					"degrees of freedom.  The last row contains the regression sum "
					"of squares and the residual sum of squares. "
					"The default of @{stat} is FALSE.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{known_ys} and @{known_xs} have unequal number of data points, "
				 "this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_SEEALSO, "LOGFIT,LINEST,LOGEST"},
	{ GNM_FUNC_HELP_END }
};
/* The following is a copy of "gnumeric_linest"
 * with "linear_regression" replaced by "logarithmic_regression".
 *
 * In Excel, this functionality is not available as a function, but only
 * as a "trend curve" within graphs.
 *
 * The function "logarithmic_regression" transforms x's logarithmically
 * before calling "general_linear_regression" written by others.
 *
 * I do not know if in statistical praxis logarithmically transformed x-data
 * is useful for *multidimensional* regression, and also if extra statistical
 * data is useful in this case, but since "general_linear_regression" written
 * by others provides it I have passed this functionality to the user.
 * But see comment to "gnumeric_linest" for problem with reading more than
 * one x-range.
 */
static GnmValue *
gnumeric_logreg (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmRegData data;
	gnm_regression_stat_t *extra_stat = NULL;
	GORegressionResult regres;
	GnmValue *result;
	gnm_float *logres = NULL;
	gboolean affine, withstat;
	int i, dim;

	result = gnm_reg_data_collect (argv[0], argv[1], &data, ei->pos);
	if (result)
		return result;
	dim = data.dim;

	affine = argv[2] ? value_get_as_checked_bool (argv[2]) : TRUE;
	withstat = argv[3] ? value_get_as_checked_bool (argv[3]) : FALSE;

	logres = g_new (gnm_float, dim + 1);
	extra_stat = gnm_regression_stat_new ();
	regres = gnm_logarithmic_regression (data.xss, dim,
					     data.ys, data.n, affine,
					     logres, extra_stat);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	if (withstat) {
		result = value_new_array (dim + 1, 5);

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat->sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (gnm_sqrt (extra_stat->var)));
		value_array_set (result, 0, 3,
				 value_new_float (extra_stat->F));
		value_array_set (result, 1, 3,
				 value_new_float (extra_stat->df_resid));
		value_array_set (result, 0, 4,
				 value_new_float (extra_stat->ss_reg));
		value_array_set (result, 1, 4,
				 value_new_float (extra_stat->ss_resid));
		for (i = 0; i < dim; i++)
			value_array_set (result, dim - i - 1, 1,
					 value_new_float (extra_stat->se[i+affine]));
		value_array_set (result, dim, 1,
				 affine ? value_new_float (extra_stat->se[0])
				 : value_new_error_NA (ei->pos));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (logres[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0,
				 value_new_float (logres[i + 1]));

 out:
	gnm_reg_data_free (&data);
	g_free (logres);
	gnm_regression_stat_destroy (extra_stat);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_logfit[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOGFIT:logarithmic least square fit (using a trial and error method)")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
	   "LOGFIT function applies the \xe2\x80\x9cleast squares\xe2\x80\x9d method to fit "
	   "the logarithmic equation "
	   "y = a + b * ln(sign * (x - c)) ,   sign = +1 or -1 "
	   "to your data. The graph of the equation is a logarithmic curve "
	   "moved horizontally by c and possibly mirrored across the y-axis "
	   "(if sign = -1).")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("LOGFIT returns an array having five columns and one row. "
					"`Sign' is given in the first column, `a', `b', and `c' are "
					"given in columns 2 to 4. Column 5 holds the sum of squared "
					"residuals.")},
	{ GNM_FUNC_HELP_NOTE, F_("An error is returned when there are less than 3 different x's "
				 "or y's, or when the shape of the point cloud is too different "
				 "from a ``logarithmic'' one.")},
	{ GNM_FUNC_HELP_NOTE, F_("You can use the above formula "
				 "= a + b * ln(sign * (x - c)) "
				 "or rearrange it to "
				 "= (exp((y - a) / b)) / sign + c "
				 "to compute unknown y's or x's, respectively.")},
	{ GNM_FUNC_HELP_NOTE, F_("This is non-linear fitting by trial-and-error. "
				 "The accuracy of `c' is: width of x-range -> rounded to the "
				 "next smaller (10^integer), times 0.000001. There might be cases "
				 "in which the returned fit is not the best possible.") },
	{ GNM_FUNC_HELP_SEEALSO, "LOGREG,LINEST,LOGEST"},
	{ GNM_FUNC_HELP_END }
};

/* This function is not available in Excel.
 * It is intended for calculation of unknowns from a calibration curve.
 * It adapts well to some types of scientific data.
 * It does not do multidimensional regression or extra statistics.
 *
 * One could do this kind of non-linear fitting with a general solver, too,
 * but then the success depends on the choosing of suitable starting values.
 * Also, determination of `sign' would be complicated.
 */
static GnmValue *
gnumeric_logfit (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float         *xs = NULL, *ys = NULL;
	GnmValue              *result = NULL;
	int                nx, ny, i;
	gnm_float         *logfit_res = NULL;

	if (argv[0] == NULL || !VALUE_IS_CELLRANGE (argv[0]))
		goto out;
	ys = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_BLANKS | /* zeroing blanks
                                      is prone to produce unwanted results */
				   COLLECT_IGNORE_STRINGS | /* dangerous, user
should use validation tool to prevent erroneously inputting strings instead of
numbers */
				   COLLECT_IGNORE_BOOLS,
				   &ny, &result);
	if (result)
		goto out;
	if (argv[1] == NULL || !VALUE_IS_CELLRANGE (argv[1]))
		goto out;
	xs = collect_floats_value (argv[1], ei->pos,
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS,
				   &nx, &result);
	if (result)
		goto out;
	if (nx != ny || nx < 3) {
		result = value_new_error_VALUE (ei->pos);
		goto out;
	}

	logfit_res = g_new (gnm_float, 5);

	if (gnm_logarithmic_fit (xs, ys, nx, logfit_res) != GO_REG_ok) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	result = value_new_array (5, 1);
	for (i=0; i<5; i++)
		value_array_set (result, i, 0,
				 value_new_float (logfit_res[i]));

 out:
	g_free (xs);
	g_free (ys);
	g_free (logfit_res);
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_trend[] = {
	{ GNM_FUNC_HELP_NAME, F_("TREND:estimates future values of a given data set using a least squares approximation")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:vector of values of dependent variable")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:array of values of independent variables, defaults to a single vector {1,\xe2\x80\xa6,n}")},
	{ GNM_FUNC_HELP_ARG, F_("new_xs:array of x-values for which to estimate the y-values; defaults to @{known_xs}")},
	{ GNM_FUNC_HELP_ARG, F_("affine:if true, the model contains a constant term, defaults to true")},
	{ GNM_FUNC_HELP_NOTE, F_("If the length of @{known_ys} does not match the corresponding length of @{known_xs}, "
				 "this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, \xe2\x80\xa6, A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
				     "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then TREND(A1:A5,B1:B5) equals {12.1, 15.7, 21.6, 26.7, 39.7}.") },
	{ GNM_FUNC_HELP_SEEALSO, "LINEST"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_trend (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmRegData data;
	gnm_regression_stat_t *extra_stat = NULL;
	GORegressionResult regres;
	GnmValue *result;
	gnm_float *linres = NULL;
	gboolean affine;
	int i, j, dim, new_x_n, new_x_m;
	GnmValue const *new_x = NULL;
	gnm_float *new_x_val = NULL;

	result = gnm_reg_data_collect (argv[0], argv[1], &data, ei->pos);
	if (result)
		return result;
	dim = data.dim;

	affine = argv[3] ? value_get_as_checked_bool (argv[3]) : TRUE;

	linres = g_new (gnm_float, dim + 1);
	extra_stat = gnm_regression_stat_new ();
	regres = gnm_linear_regression (data.xss, dim,
					data.ys, data.n, affine,
					linres, extra_stat);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	if (argv[2] != NULL)
		new_x = argv[2];
	else if (argv[1] != NULL)
		new_x = argv[1];

	if (dim == 1)
		data.type = gnm_reg_type_rect;

	if (new_x == NULL) {
		result = value_new_array (1, data.n);
		for (i = 0; i < data.n; i++) {
			gnm_float res = linres[0];
			res += (i + 1) * linres[1];
			value_array_set (result, 0, i,
					 value_new_float (res));
		}
	} else
		switch (data.type) {
		case gnm_reg_type_rect:
			new_x_n = value_area_get_height (new_x, ei->pos);
			new_x_m = value_area_get_width (new_x, ei->pos);
			result = value_new_array (new_x_m, new_x_n);
			for (i = 0; i < new_x_n; i++) {
				for (j = 0; j < new_x_m; j++) {
					gnm_float res = linres[0];
					new_x_val = gnm_reg_get_var
							(new_x, j, i, 0, 0, 1, ei->pos);
					if (new_x_val != NULL) {
						res += new_x_val[0] * linres[1];
						value_array_set
							(result, j, i,
							 value_new_float (res));
						g_free (new_x_val);
					} else
						value_array_set
							(result, j, i,
							 value_new_error_NA (ei->pos));
				}
			}
			break;
		case gnm_reg_type_vertical:
			if (dim != value_area_get_width (new_x, ei->pos)) {
				result = value_new_error_NUM (ei->pos);
				goto out;
			}
			new_x_n = value_area_get_height (new_x, ei->pos);
			result = value_new_array (1, new_x_n);
			for (i = 0; i < new_x_n; i++) {
				gnm_float res = linres[0];
				new_x_val = gnm_reg_get_var
					(new_x, 0, i, +1, 0, dim, ei->pos);
				if (new_x_val != NULL) {
					for (j = 0; j < dim; j++)
						res += new_x_val[j] * linres[j+1];
					value_array_set (result, 0, i,
							 value_new_float (res));
					g_free (new_x_val);
				} else
					value_array_set (result, 0, i,
							 value_new_error_NA (ei->pos));
			}
			break;
		case gnm_reg_type_horizontal:
			if (dim != value_area_get_height (new_x, ei->pos)) {
				result = value_new_error_NUM (ei->pos);
				goto out;
			}
			new_x_n = value_area_get_width (new_x, ei->pos);
			result = value_new_array (new_x_n, 1);
			for (i = 0; i < new_x_n; i++) {
				gnm_float res = linres[0];
				new_x_val = gnm_reg_get_var
					(new_x, i, 0, 0, +1, dim, ei->pos);
				if (new_x_val != NULL) {
					for (j = 0; j < dim; j++)
						res += new_x_val[j] * linres[j+1];
					value_array_set (result, i, 0,
							 value_new_float (res));
					g_free (new_x_val);
				} else
					value_array_set (result, i, 0,
							 value_new_error_NA (ei->pos));
			}
			break;
		}


 out:
	gnm_reg_data_free (&data);
	g_free (linres);
	gnm_regression_stat_destroy (extra_stat);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_logest[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOGEST:exponential least square fit")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values; default to an array {1, 2, 3, \xe2\x80\xa6}")},
	{ GNM_FUNC_HELP_ARG, F_("affine:if true, the model contains a constant term, defaults to true")},
	{ GNM_FUNC_HELP_ARG, F_("stat:if true, extra statistical information will be returned; defaults to FALSE")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("LOGEST function applies the "
					"\xe2\x80\x9cleast squares\xe2\x80\x9d method to fit "
					"an exponential curve of the form\t"
					"y = b * m{1}^x{1} * m{2}^x{2}... to your data.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("LOGEST returns an array { m{n},m{n-1}, ...,m{1},b }.") },
	{ GNM_FUNC_HELP_NOTE, F_("Extra statistical information is written below the regression "
				 "line coefficients in the result array.  Extra statistical "
				 "information consists of four rows of data.  In the first row "
				 "the standard error values for the coefficients m1, (m2, ...), b "
				 "are represented.  The second row contains the square of R and "
				 "the standard error for the y estimate.  The third row contains "
				 "the F-observed value and the degrees of freedom.  The last row "
				 "contains the regression sum of squares and the residual sum "
				 "of squares.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{known_ys} and @{known_xs} have unequal number of data points, "
				 "this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_SEEALSO, "GROWTH,TREND"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_logest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmRegData data;
	gnm_regression_stat_t *extra_stat = NULL;
	GORegressionResult regres;
	GnmValue *result;
	gnm_float *expres = NULL;
	gboolean affine, withstat;
	int i, dim;

	result = gnm_reg_data_collect (argv[0], argv[1], &data, ei->pos);
	if (result)
		return result;
	dim = data.dim;

	affine = argv[2] ? value_get_as_checked_bool (argv[2]) : TRUE;
	withstat = argv[3] ? value_get_as_checked_bool (argv[3]) : FALSE;

	expres = g_new (gnm_float, dim + 1);
	extra_stat = gnm_regression_stat_new ();
	regres = gnm_exponential_regression (data.xss, dim,
					     data.ys, data.n, affine,
					     expres, extra_stat);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	if (withstat) {
		result = value_new_array (dim + 1, 5);

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat->sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (gnm_sqrt (extra_stat->var)));
		value_array_set (result, 0, 3,
				 value_new_float (extra_stat->F));
		value_array_set (result, 1, 3,
				 value_new_float (extra_stat->df_resid));
		value_array_set (result, 0, 4,
				 value_new_float (extra_stat->ss_reg));
		value_array_set (result, 1, 4,
				 value_new_float (extra_stat->ss_resid));
		for (i = 0; i < dim; i++)
			value_array_set (result, dim - i - 1, 1,
					 value_new_float (extra_stat->se[i+affine]));
		value_array_set (result, dim, 1,
				 affine ? value_new_float (extra_stat->se[0])
				 : value_new_error_NA (ei->pos));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (expres[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0,
				 value_new_float (expres[i + 1]));

 out:
	gnm_reg_data_free (&data);
	g_free (expres);
	gnm_regression_stat_destroy (extra_stat);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_growth[] = {
	{ GNM_FUNC_HELP_NAME, F_("GROWTH:exponential growth prediction")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values; defaults to the array {1, 2, 3, \xe2\x80\xa6}")},
	{ GNM_FUNC_HELP_ARG, F_("new_xs:x-values for which to estimate the y-values; defaults to @{known_xs}")},
	{ GNM_FUNC_HELP_ARG, F_("affine:if true, the model contains a constant term, defaults to true")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("GROWTH function applies the \xe2\x80\x9cleast "
					"squares\xe2\x80\x9d method to fit an "
					"exponential curve to your data and predicts "
					"the exponential "
					"growth by using this curve.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("GROWTH returns an array having one column and a row for each "
					"data point in @{new_xs}.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GROWTH({1,2,3},{3,4,4},{6})" },
	{ GNM_FUNC_HELP_NOTE, F_("If @{known_ys} and @{known_xs} have unequal number of data points, "
				 "this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_SEEALSO, "LOGEST,GROWTH,TREND"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_growth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *ys, *xs, *nxs;
	int i, n, nnx;
	GnmValue *res = NULL;
	int dim = 1;
	gboolean affine;
	GORegressionResult regres;
	gnm_float expres[2];
	gboolean constp = FALSE;

	if (argv[1]) {
		res = collect_float_pairs (argv[0], argv[1], ei->pos,
					   COLLECT_IGNORE_BLANKS |
					   COLLECT_IGNORE_STRINGS |
					   COLLECT_IGNORE_BOOLS,
					   &ys, &xs, &n, &constp);
		if (res)
			return res;
	} else {
		ys = collect_floats_value (argv[0], ei->pos,
					   COLLECT_IGNORE_BLANKS |
					   COLLECT_IGNORE_STRINGS |
					   COLLECT_IGNORE_BOOLS,
					   &n, &res);
		if (res)
			return res;
		xs = g_new (gnm_float, n);
		for (i = 0; i < n; i++)
			xs[i] = i + 1;
	}

	if (argv[2] != NULL) {
		nxs = collect_floats_value (argv[2], ei->pos,
					    COLLECT_IGNORE_BLANKS |
					    COLLECT_IGNORE_STRINGS |
					    COLLECT_IGNORE_BOOLS,
					    &nnx, &res);
		if (res)
			goto out;
	} else {
		/* @{new_x}'s is assumed to be the same as @{known_x}'s */
		nxs = go_memdup_n (xs, n, sizeof (gnm_float));
		nnx = n;
	}

	affine = argv[3] ? value_get_as_checked_bool (argv[3]) : TRUE;

	if (nnx <= 0) {
		res = value_new_error_NUM (ei->pos);
		goto out;
	}

	regres = gnm_exponential_regression (&xs, dim,
					     ys, n, affine, expres, NULL);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		res = value_new_error_NUM (ei->pos);
		goto out;
	}

	res = value_new_array (1, nnx);
	for (i = 0; i < nnx; i++)
		value_array_set (res, 0, i,
				 value_new_float (gnm_pow (expres[1], nxs[i]) *
						  expres[0]));

 out:
	if (!constp) {
		g_free (xs);
		g_free (ys);
	}
	g_free (nxs);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_forecast[] = {
	{ GNM_FUNC_HELP_NAME, F_("FORECAST:estimates a future value according to existing values "
				 "using simple linear regression")},
	{ GNM_FUNC_HELP_ARG, F_("x:x-value whose matching y-value should be forecast")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function estimates a future value according to "
					"existing values using simple linear regression.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{known_xs} or @{known_ys} contains no data entries or different "
				 "number of data entries, this function returns a #N/A error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the variance of the @{known_xs} is zero, this function returns a #DIV/0 "
				 "error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
				     "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then FORECAST(7,A1:A5,B1:B5) equals -10.859397661.") },
	{ GNM_FUNC_HELP_SEEALSO, "INTERCEPT,TREND"},
	{ GNM_FUNC_HELP_END }
};

static int
range_forecast (gnm_float const *xs, gnm_float const *ys, int n, gnm_float *res, gpointer user)
{
	gnm_float const *px = user;
	gnm_float linres[2];
	int dim = 1;
	gboolean affine = TRUE;
	GORegressionResult regres;

	regres = gnm_linear_regression ((gnm_float **)&xs, dim,
					ys, n, affine, linres, NULL);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		return 1;
	}

	*res = linres[0] + (*px) * linres[1];
	return 0;
}

static GnmValue *
gnumeric_forecast (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);

	return float_range_function2d (argv[2], argv[1],
				       ei,
				       range_forecast,
				       COLLECT_IGNORE_BLANKS |
				       COLLECT_IGNORE_STRINGS |
				       COLLECT_IGNORE_BOOLS,
				       GNM_ERROR_VALUE,
				       &x);
}

/***************************************************************************/

static GnmFuncHelp const help_intercept[] = {
	{ GNM_FUNC_HELP_NAME, F_("INTERCEPT:the intercept of a linear regression line")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{known_xs} or @{known_ys} contains no data entries or different "
				 "number of data entries, this function returns a #N/A error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the variance of the @{known_xs} is zero, this function returns "
				 "#DIV/0 error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
				     "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then INTERCEPT(A1:A5,B1:B5) equals -20.785117212.") },
	{ GNM_FUNC_HELP_SEEALSO, "FORECAST,TREND"},
	{ GNM_FUNC_HELP_END }
};

static int
range_intercept (gnm_float const *xs, gnm_float const *ys, int n, gnm_float *res)
{
	gnm_float linres[2];
	int dim = 1;
	GORegressionResult regres;

	regres = gnm_linear_regression ((gnm_float **)&xs, dim,
					ys, n, 1, linres, NULL);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		return 1;
	}

	*res = linres[0];
	return 0;
}

static GnmValue *
gnumeric_intercept (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[1], argv[0],
				      ei,
				      range_intercept,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_slope[] = {
	{ GNM_FUNC_HELP_NAME, F_("SLOPE:the slope of a linear regression line")},
	{ GNM_FUNC_HELP_ARG, F_("known_ys:known y-values")},
	{ GNM_FUNC_HELP_ARG, F_("known_xs:known x-values")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{known_xs} or @{known_ys} contains no data entries or different "
				 "number of data entries, this function returns a #N/A error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the variance of the @{known_xs} is zero, this function returns "
				 "#DIV/0 error.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers "
				     "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
				     "B5 23.2, 25.8, 29.9, 33.5, and 42.7.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then SLOPE(A1:A5,B1:B5) equals 1.417959936.") },
	{ GNM_FUNC_HELP_SEEALSO, "STDEV,STDEVPA"},
	{ GNM_FUNC_HELP_END }
};

static int
range_slope (gnm_float const *xs, gnm_float const *ys, int n, gnm_float *res)
{
	gnm_float linres[2];
	int dim = 1;
	GORegressionResult regres;

	regres = gnm_linear_regression ((gnm_float **)&xs, dim,
					ys, n, 1, linres, NULL);
	switch (regres) {
	case GO_REG_ok:
	case GO_REG_near_singular_good:
		break;
	default:
		return 1;
	}

	*res = linres[1];
	return 0;
}

static GnmValue *
gnumeric_slope (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[1], argv[0],
				      ei,
				      range_slope,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_subtotal[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUBTOTAL:the subtotal of the given list of arguments")},
	{ GNM_FUNC_HELP_ARG, F_("function_nbr:determines which function to use according to the following table:\n"
				"\t1   AVERAGE\n"
				"\t2   COUNT\n"
				"\t3   COUNTA\n"
				"\t4   MAX\n"
				"\t5   MIN\n"
				"\t6   PRODUCT\n"
				"\t7   STDEV\n"
				"\t8   STDEVP\n"
				"\t9   SUM\n"
				"\t10   VAR\n"
				"\t11   VARP"
			)},
	{ GNM_FUNC_HELP_ARG, F_("ref1:first value")},
	{ GNM_FUNC_HELP_ARG, F_("ref2:second value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 23, 27, 28, 33, and 39.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then SUBTOTAL(1,A1:A5) equals 30. "
				     "SUBTOTAL(6,A1:A5) equals 22378356. "
				     "SUBTOTAL(7,A1:A5) equals 6.164414003. "
				     "SUBTOTAL(9,A1:A5) equals 150. "
				     "SUBTOTAL(11,A1:A5) equals 30.4.") },
	{ GNM_FUNC_HELP_SEEALSO, "COUNT,SUM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_subtotal (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmExpr const *expr;
	GnmValue *val;
	int   fun_nbr;
	float_range_function_t func;
	GnmStdError err = GNM_ERROR_DIV0;
	CollectFlags flags_errors = 0;
	CollectFlags flags_strings = COLLECT_IGNORE_STRINGS;
	CollectFlags flags_bools = COLLECT_IGNORE_BOOLS;
	CollectFlags flags_other = COLLECT_IGNORE_BLANKS | COLLECT_IGNORE_SUBTOTAL;

	if (argc == 0)
		return value_new_error_NUM (ei->pos);

	expr = argv[0];
	if (expr == NULL)
		return value_new_error_NUM (ei->pos);

	val = gnm_expr_eval (expr, ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (VALUE_IS_ERROR (val))
		return val;
	fun_nbr = value_get_as_int (val);
	value_release (val);

	/* Skip the first node */
	argc--;
	argv++;

	switch (fun_nbr) {
	case  1: func = gnm_range_average;	break;
	case  2: flags_errors = COLLECT_IGNORE_ERRORS;
		 func = gnm_range_count;        break;
	case  3: flags_errors = COLLECT_ZERO_ERRORS;
		 flags_strings = COLLECT_ZERO_STRINGS;
		 flags_bools = COLLECT_ZEROONE_BOOLS;
		 func = gnm_range_count;        break;
	case  4: err = GNM_ERROR_VALUE;
		 func = range_max0;		break;
	case  5: err = GNM_ERROR_VALUE;
		 func = range_min0;		break;
	case  6: err = GNM_ERROR_VALUE;
		 func = gnm_range_product;	break;
	case  7: func = gnm_range_stddev_est;	break;
	case  8: func = gnm_range_stddev_pop;	break;
	case  9: err = GNM_ERROR_VALUE;
		 func = gnm_range_sum;		break;
	case 10: func = gnm_range_var_est;	break;
	case 11: func = gnm_range_var_pop;	break;

	default:
		return value_new_error_NUM (ei->pos);
	}

	return float_range_function (argc, argv, ei, func,
				     flags_errors | flags_strings | flags_bools | flags_other,
				     err);
}

/***************************************************************************/

static GnmFuncHelp const help_cronbach[] = {
	{ GNM_FUNC_HELP_NAME, F_("CRONBACH:Cronbach's alpha")},
	{ GNM_FUNC_HELP_ARG, F_("ref1:first data set")},
	{ GNM_FUNC_HELP_ARG, F_("ref2:second data set")},
	{ GNM_FUNC_HELP_SEEALSO, "VAR" },
	{ GNM_FUNC_HELP_END }
};

static void
free_values (GnmValue **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values [i])
			value_release (values [i]);
	g_free (values);
}

static GnmValue *
function_marshal_arg (GnmFuncEvalInfo *ei,
		      GnmExpr const *t,
		      GnmValue **type_mismatch)
{
	GnmValue *v = gnm_expr_eval (t, ei->pos,
				     GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
				     GNM_EXPR_EVAL_WANT_REF);

	if (!VALUE_IS_ARRAY (v) && !VALUE_IS_CELLRANGE (v))
		*type_mismatch = value_new_error_VALUE (ei->pos);
	else
		*type_mismatch = NULL;

	return v;
}

static GnmValue *
gnumeric_cronbach (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int         i, j;
	GnmValue **values;
	gnm_float sum_variance = 0.0;
	gnm_float sum_covariance = 0.0;

	if (argc < 2)
		return value_new_error_VALUE (ei->pos);

	for (i = 0; i < argc; i++) {
		GnmValue *fl_val =
			float_range_function (1, argv + i, ei,
					      gnm_range_var_pop, 0,
					      GNM_ERROR_VALUE);
		if (!VALUE_IS_NUMBER (fl_val))
			return fl_val;
		sum_variance += value_get_as_float (fl_val);
		value_release (fl_val);
	}

	values = g_new0 (GnmValue *, argc);

	for (i = 0; i < argc; i++) {
		GnmValue *type_mismatch;

		values[i] = function_marshal_arg (ei, argv[i], &type_mismatch);
		if (type_mismatch || values[i] == NULL) {
			free_values (values, i + 1);
			if (type_mismatch)
				return type_mismatch;
			else
				return value_new_error_VALUE (ei->pos);
		}
	}

	g_return_val_if_fail (i == argc, value_new_error_VALUE (ei->pos));

	/* We now have an array of array values and an expression list */
	for (i = 0; i < argc; ++i) {
		for (j = i + 1; j < argc; ++j) {
			GnmValue *fl_val;
			fl_val = float_range_function2 (values[i], values[j],
							ei,
							gnm_range_covar_pop, 0,
							GNM_ERROR_VALUE);
			if (!VALUE_IS_NUMBER (fl_val)) {
				free_values (values, argc);
				return fl_val;
			}
			sum_covariance += value_get_as_float (fl_val);
			value_release (fl_val);
		}
	}

	free_values (values, argc);
	return  value_new_float
		(argc * (1 - sum_variance / (sum_variance + 2 * sum_covariance)) / (argc - 1));
}

/***************************************************************************/

static GnmFuncHelp const help_geomdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("GEOMDIST:probability mass or cumulative distribution function of the geometric distribution")},
	{ GNM_FUNC_HELP_ARG, F_("k:number of trials")},
	{ GNM_FUNC_HELP_ARG, F_("p:probability of success in any trial")},
	{ GNM_FUNC_HELP_ARG, F_("cumulative:whether to evaluate the mass function or the cumulative distribution function")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{k} < 0 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{p} < 0 or @{p} > 1 this function returns a #NUM! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{cumulative} is neither TRUE nor FALSE this function returns a #VALUE! error.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GEOMDIST(2,0.4,TRUE)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDGEOM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_geomdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float k   = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float p   = value_get_as_float (argv[1]);
	gboolean  cum = value_get_as_checked_bool (argv[2]);

	if (p < 0 || p > 1 || k < 0)
		return value_new_error_NUM (ei->pos);

	if (cum)
		return value_new_float (pgeom (k, p, TRUE, FALSE));
	else
		return value_new_float (dgeom (k, p, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_logistic[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOGISTIC:probability density function of the logistic distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("a:scale parameter")},
	{ GNM_FUNC_HELP_EXAMPLES, "=LOGISTIC(0.4,1)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDLOGISTIC"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
random_logistic_pdf (gnm_float x, gnm_float a)
{
	gnm_float u = gnm_exp (-gnm_abs (x) / a);

	return u / (gnm_abs (a) * (1 + u) * (1 + u));
}

static GnmValue *
gnumeric_logistic (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);

	if (a <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (random_logistic_pdf (x, a));
}

/***************************************************************************/

static GnmFuncHelp const help_pareto[] = {
	{ GNM_FUNC_HELP_NAME, F_("PARETO:probability density function of the Pareto distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("a:exponent")},
	{ GNM_FUNC_HELP_ARG, F_("b:scale parameter")},
	{ GNM_FUNC_HELP_EXAMPLES, "=PARETO(0.6,1,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDPARETO"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
random_pareto_pdf (gnm_float x, gnm_float a, gnm_float b)
{
	if (x >= b)
		return (a / b) / gnm_pow (x / b, a + 1);
	else
		return 0;
}

static GnmValue *
gnumeric_pareto (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);
	gnm_float b = value_get_as_float (argv[2]);

	if (a <= 0 || b <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (random_pareto_pdf (x, a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_rayleigh[] = {
	{ GNM_FUNC_HELP_NAME, F_("RAYLEIGH:probability density function of the Rayleigh distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("sigma:scale parameter")},
	{ GNM_FUNC_HELP_EXAMPLES, "=RAYLEIGH(0.4,1)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDRAYLEIGH"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rayleigh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x     = value_get_as_float (argv[0]);
	gnm_float sigma = value_get_as_float (argv[1]);

	return value_new_float (drayleigh (x, sigma, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_rayleightail[] = {
	{ GNM_FUNC_HELP_NAME, F_("RAYLEIGHTAIL:probability density function of the Rayleigh tail distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("a:lower limit")},
	{ GNM_FUNC_HELP_ARG, F_("sigma:scale parameter")},
	{ GNM_FUNC_HELP_EXAMPLES, "=RAYLEIGHTAIL(0.6,0.3,1)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDRAYLEIGHTAIL"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
random_rayleigh_tail_pdf (gnm_float x, gnm_float a, gnm_float sigma)
{
	if (x < a)
		return 0;
	else {
		gnm_float u = x / sigma ;
		gnm_float v = a / sigma ;

		return (u / sigma) * gnm_exp ((v + u) * (v - u) / 2) ;
	}
}

static GnmValue *
gnumeric_rayleightail (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x     = value_get_as_float (argv[0]);
	gnm_float a     = value_get_as_float (argv[1]);
	gnm_float sigma = value_get_as_float (argv[2]);

	if (sigma <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (random_rayleigh_tail_pdf (x, a, sigma));
}

/***************************************************************************/

static GnmFuncHelp const help_exppowdist[] = {
	{ GNM_FUNC_HELP_NAME, F_("EXPPOWDIST:the probability density function of the "
				 "Exponential Power distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("a:scale parameter")},
	{ GNM_FUNC_HELP_ARG, F_("b:scale parameter")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
			"This distribution has been recommended for lifetime analysis "
			"when a U-shaped hazard function is desired. "
			"This corresponds to rapid failure once the product starts to "
			"wear out after a period of steady or even improving reliability.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EXPPOWDIST(0.4,1,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDEXPPOW"},
	{ GNM_FUNC_HELP_END }
};
/* Part of help text quoted from the PEXPDF manpage of the DATAPLOT program
 * by NIST. */

static GnmValue *
gnumeric_exppowdist (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);
	gnm_float b = value_get_as_float (argv[2]);

	/* FIXME: Check this condition.  */
	if (b <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (random_exppow_pdf (x, a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_laplace[] = {
	{ GNM_FUNC_HELP_NAME, F_("LAPLACE:probability density function of the Laplace distribution")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("a:mean")},
	{ GNM_FUNC_HELP_EXAMPLES, "=LAPLACE(0.4,1)" },
	{ GNM_FUNC_HELP_SEEALSO, "RANDLAPLACE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_laplace (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);

	if (a <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (random_laplace_pdf (x, a));
}

/***************************************************************************/

static GnmFuncHelp const help_permutationa[] = {
	{ GNM_FUNC_HELP_NAME, F_("PERMUTATIONA:the number of permutations of @{y} objects chosen from @{x} objects with repetition allowed")},
	{ GNM_FUNC_HELP_ARG, F_("x:total number of objects")},
	{ GNM_FUNC_HELP_ARG, F_("y:number of selected objects")},
	{ GNM_FUNC_HELP_NOTE, F_("If both @{x} and @{y} equal 0, PERMUTATIONA returns 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0 or @{y} < 0, PERMUTATIONA returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} or @{y} are not integers, they are truncated.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=PERMUTATIONA(2,7)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=PERMUTATIONA(2.3,7.6)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=PERMUTATIONA(0,0)" },
	{ GNM_FUNC_HELP_SEEALSO, "POWER"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_permutationa (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = gnm_fake_floor (value_get_as_float (argv[0]));
	gnm_float y = gnm_fake_floor (value_get_as_float (argv[1]));

	if (x < 0 || y < 0)
		return value_new_error_NUM (ei->pos);
	else if (y == 0)
		return value_new_float (1);
	else
		return value_new_float (gnm_pow (x, y));
}

/***************************************************************************/

static GnmFuncHelp const help_lkstest[] = {
	{ GNM_FUNC_HELP_NAME, F_("LKSTEST:Lilliefors (Kolmogorov-Smirnov) Test of Normality") },
	{ GNM_FUNC_HELP_ARG, F_("x:array of sample values") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns an array with the first row giving the p-value of the Lilliefors (Kolmogorov-Smirnov) Test,"
					" the second row the test statistic of the test, and the third the number of observations in the sample.")},
	{ GNM_FUNC_HELP_NOTE, F_("If there are less than 5 sample values, LKSTEST returns #VALUE!") },
	{ GNM_FUNC_HELP_SEEALSO, "CHITEST,ADTEST,SFTEST,CVMTEST" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Lilliefors_test") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_lkstest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *xs;
	int n;
	GnmValue *result = NULL;
	gnm_float mu = 0;
	gnm_float sigma = 1;

	xs = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_ORDER_IRRELEVANT,
				   &n, &result);

	if (result)
		goto out;

	result = value_new_array (1, 3);
	value_array_set (result, 0, 2,
			 value_new_int (n));

	if ((n < 5) || gnm_range_average (xs, n, &mu)
	    || gnm_range_stddev_est (xs, n, &sigma)) {
		value_array_set (result, 0, 0,
				 value_new_error_VALUE (ei->pos));
		value_array_set (result, 0, 1,
				 value_new_error_VALUE (ei->pos));
	} else {
		int i;
		gnm_float dplus, dminus;
		gnm_float p, nd;
		gnm_float *ys;
		gnm_float val;
		gnm_float stat;

		ys = gnm_range_sort (xs, n);

		val = pnorm (ys[0], mu, sigma, TRUE, FALSE);
		dplus = 1 / (gnm_float)n - val;
		dminus = val;

		for (i = 1; i < n; i++) {
			gnm_float one_dplus, one_dminus;
			val = pnorm (ys[i], mu, sigma, TRUE, FALSE);
			one_dplus = (i + 1)/(gnm_float)n - val;
			one_dminus = val - i/(gnm_float)n;

			if (one_dplus > dplus)
				dplus = one_dplus;
			if (one_dminus > dminus)
				dminus = one_dminus;
		}

		stat = ((dplus < dminus) ? dminus : dplus);

		value_array_set (result, 0, 1,
				 value_new_float (stat));

		g_free (ys);

		if (n > 100) {
			stat = stat * gnm_pow (n / GNM_const(100.), GNM_const(0.49));
			nd = 100;
		} else
			nd = n;

		p = gnm_exp (GNM_const(-7.01256) * stat * stat * (nd + GNM_const(2.78019)) +
			     GNM_const(2.99587) * stat * gnm_sqrt (nd + GNM_const(2.78019))
			     - GNM_const(0.122119) + GNM_const(0.974598) / gnm_sqrt(nd) + GNM_const(1.67997)/nd);

		if (p > GNM_const(0.1)) {
			stat = (gnm_sqrt (nd) - GNM_const(0.01) +
				GNM_const(0.85) / gnm_sqrt (nd)) * stat;
			if (stat <= GNM_const(0.302))
				p = 1;
			else if (stat <= GNM_const(0.5))
				p = GNM_const(2.76773) - GNM_const(19.828315) * stat
					+ GNM_const(80.709644) * stat * stat
					- GNM_const(138.55152) * stat * stat * stat
					+ GNM_const(81.218052) * stat * stat * stat * stat;
			else if (stat <= GNM_const(0.9))
				p = GNM_const(-4.901232) + GNM_const(40.662806) * stat
					- GNM_const(97.490286) * stat * stat
					+ GNM_const(94.029866) * stat * stat * stat
					- GNM_const(32.355711) * stat * stat * stat * stat;
			else if (stat <= GNM_const(1.31))
				p = GNM_const(6.198765) - GNM_const(19.558097) * stat
					+ GNM_const(23.186922) * stat * stat
					- GNM_const(12.234627) * stat * stat * stat
					+ GNM_const(2.423045) * stat * stat * stat * stat;
			else
				p = 0;
		}

		value_array_set (result, 0, 0, value_new_float (p));
	}

 out:
	g_free (xs);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_sftest[] = {
	{ GNM_FUNC_HELP_NAME, F_("SFTEST:Shapiro-Francia Test of Normality") },
	{ GNM_FUNC_HELP_ARG, F_("x:array of sample values") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns an array with the first row giving the p-value of the Shapiro-Francia Test,"
					" the second row the test statistic of the test, and the third the number of observations in the sample.")},
	{ GNM_FUNC_HELP_NOTE, F_("If there are less than 5 or more than 5000 sample values, SFTEST returns #VALUE!") },
	{ GNM_FUNC_HELP_SEEALSO, "CHITEST,ADTEST,LKSTEST,CVMTEST" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sftest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *xs;
	int n;
	GnmValue *result = NULL;

	xs = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_ORDER_IRRELEVANT,
				   &n, &result);

	if (result)
		goto out;

	result = value_new_array (1, 3);
	value_array_set (result, 0, 2,
			 value_new_int (n));

	if ((n < 5) || (n > 5000)) {
		value_array_set (result, 0, 0,
				 value_new_error_VALUE (ei->pos));
		value_array_set (result, 0, 1,
				 value_new_error_VALUE (ei->pos));
	} else {
		int i;
		gnm_float stat_;
		gnm_float *ys;
		gnm_float *zs;

		ys = gnm_range_sort (xs, n);
		zs = g_new (gnm_float, n);

		for (i = 0; i < n; i++)
			zs[i] = qnorm ((((gnm_float)(i + 1)) - GNM_const(3.) / 8) /
				       (n + GNM_const(0.25)), 0., 1., TRUE, FALSE);

		if (gnm_range_correl_pop (ys, zs, n, &stat_)) {
			value_array_set (result, 0, 0,
					 value_new_error_VALUE (ei->pos));
			value_array_set (result, 0, 1,
					 value_new_error_VALUE (ei->pos));
		} else {
			gnm_float p;
			gnm_float u, v, mu, sig;

			stat_ = stat_ * stat_;

			value_array_set (result, 0, 1,
					 value_new_float (stat_));

			u = gnm_log (n);
			v = gnm_log (u);
			mu = GNM_const(-1.2725) + GNM_const(1.0521) * (v - u);
			sig = GNM_const(1.0308) - GNM_const(0.26758) * (v + 2 / u);

			p = pnorm (gnm_log1p (-stat_), mu, sig, FALSE, FALSE);

			value_array_set (result, 0, 0,
					 value_new_float (p));
		}
		g_free (ys);
		g_free (zs);
	}

 out:
	g_free (xs);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_cvmtest[] = {
	{ GNM_FUNC_HELP_NAME, F_("CVMTEST:Cram\xc3\xa9r-von Mises Test of Normality") },
	{ GNM_FUNC_HELP_ARG, F_("x:array of sample values") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns an array with the first row giving the p-value of the Cram\xc3\xa9r-von Mises Test,"
					" the second row the test statistic of the test, and the third the number of observations in the sample.")},
	{ GNM_FUNC_HELP_NOTE, F_("If there are less than 8 sample values, CVMTEST returns #VALUE!") },
	{ GNM_FUNC_HELP_SEEALSO, "CHITEST,ADTEST,LKSTEST,SFTEST" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Cramér–von-Mises_criterion") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cvmtest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *xs;
	int n;
	GnmValue *result = NULL;
	gnm_float mu = 0;
	gnm_float sigma = 1;

	xs = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_ORDER_IRRELEVANT,
				   &n, &result);

	if (result)
		goto out;

	result = value_new_array (1, 3);
	value_array_set (result, 0, 2,
			 value_new_int (n));

	if ((n < 8) || gnm_range_average (xs, n, &mu)
	    || gnm_range_stddev_est (xs, n, &sigma)) {
		value_array_set (result, 0, 0,
				 value_new_error_VALUE (ei->pos));
		value_array_set (result, 0, 1,
				 value_new_error_VALUE (ei->pos));
	} else {
		int i;
		gnm_float total = 0;
		gnm_float p;
		gnm_float *ys;

		ys = gnm_range_sort (xs, n);

		for (i = 0; i < n; i++) {
			gnm_float val = pnorm (ys[i], mu, sigma, TRUE, FALSE);
			gnm_float delta;
			delta = val - (2 * i + 1) / (gnm_float)(2 * n);
			total += (delta * delta);
		}

		total += (1 / (12 * (gnm_float)n));
		value_array_set (result, 0, 1,
				 value_new_float (total));

		g_free (ys);

		total *= (1 + GNM_const(0.5) / n);
		if (total < GNM_const(0.0275))
			p = 1 - gnm_exp (GNM_const(-13.953) + GNM_const(775.5) * total - GNM_const(12542.61) * total * total);
		else if (total < GNM_const(0.051))
			p = 1 - gnm_exp (GNM_const(-5.903) + GNM_const(179.546) * total - GNM_const(1515.29) * total * total);
		else if (total < GNM_const(0.092))
			p = gnm_exp (GNM_const(0.886) - GNM_const(31.62)  * total - GNM_const(10.897) * total * total);
		else if (total < 1)
			p = gnm_exp (GNM_const(1.111) - GNM_const(34.242) * total + GNM_const(12.832) * total * total);
		else
			p = 0;

		value_array_set (result, 0, 0, value_new_float (p));
	}

 out:
	g_free (xs);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_adtest[] = {
	{ GNM_FUNC_HELP_NAME, F_("ADTEST:Anderson-Darling Test of Normality") },
	{ GNM_FUNC_HELP_ARG, F_("x:array of sample values") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns an array with the first row giving the p-value of the Anderson-Darling Test,"
					" the second row the test statistic of the test, and the third the number of observations in the sample.")},
	{ GNM_FUNC_HELP_NOTE,  F_("If there are less than 8 sample values, ADTEST returns #VALUE!") },
	{ GNM_FUNC_HELP_SEEALSO, "CHITEST,CVMTEST,LKSTEST,SFTEST" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Anderson–Darling_test") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_adtest (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *xs;
	int n;
	GnmValue *result = NULL;
	gnm_float statistics = 0;
	gnm_float p = 0;

	xs = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_ORDER_IRRELEVANT,
				   &n, &result);

	if (result)
		goto out;

	result = value_new_array (1, 3);
	value_array_set (result, 0, 2,
			 value_new_int (n));

	if ((n < 8) || gnm_range_adtest (xs, n, &p, &statistics)) {
		value_array_set (result, 0, 0,
				 value_new_error_VALUE (ei->pos));
		value_array_set (result, 0, 1,
				 value_new_error_VALUE (ei->pos));
	} else {
		value_array_set (result, 0, 0,
				 value_new_float (p));
		value_array_set (result, 0, 1,
				 value_new_float (statistics));
	}

 out:
	g_free (xs);

	return result;
}

/***************************************************************************/

GnmFuncDescriptor const stat_functions[] = {
	{ "adtest",       "A",
	  help_adtest, gnumeric_adtest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "sftest",       "A",
	  help_sftest, gnumeric_sftest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "cvmtest",       "A",
	  help_cvmtest, gnumeric_cvmtest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "lkstest",       "A",
	  help_lkstest, gnumeric_lkstest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "avedev", NULL,
	  help_avedev, NULL, gnumeric_avedev,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "average", NULL,
	  help_average, NULL, gnumeric_average,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "averagea", NULL,
	  help_averagea, NULL, gnumeric_averagea,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "bernoulli", "ff",    help_bernoulli,
	  gnumeric_bernoulli, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "betadist",     "fff|ff",
	  help_betadist, gnumeric_betadist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "beta.dist",     "fffb|ff",
	  help_beta_dist, gnumeric_beta_dist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "betainv",      "fff|ff",
	  help_betainv, gnumeric_betainv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "binomdist",    "fffb",
	  help_binomdist, gnumeric_binomdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "binom.dist.range",    "fff|f",
	  help_binom_dist_range, gnumeric_binom_dist_range, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "cauchy", "ffb",    help_cauchy,
	  gnumeric_cauchy, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "chidist",      "ff",
	  help_chidist, gnumeric_chidist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "chiinv",       "ff",
	  help_chiinv, gnumeric_chiinv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "chitest",      "AA",
	  help_chitest, gnumeric_chitest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "confidence",   "fff",
	  help_confidence, gnumeric_confidence, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "confidence.t",   "fff",
	  help_confidence_t, gnumeric_confidence_t, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE,  GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "correl",       "AA",
	  help_correl, gnumeric_correl, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "count", NULL,
	  help_count, NULL, gnumeric_count,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "counta", NULL,
	  help_counta, NULL, gnumeric_counta,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "covar",        "AA",
	  help_covar, gnumeric_covar, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "covariance.s", "AA",
	  help_covariance_s, gnumeric_covariance_s, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "critbinom",    "fff",
	  help_critbinom, gnumeric_critbinom, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "devsq", NULL,
	  help_devsq, NULL, gnumeric_devsq,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "expondist",    "ffb",
	  help_expondist, gnumeric_expondist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fdist",        "fff",
	  help_fdist, gnumeric_fdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "finv",         "fff",
	  help_finv, gnumeric_finv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fisher",       "f",
	  help_fisher, gnumeric_fisher, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fisherinv",    "f",
	  help_fisherinv, gnumeric_fisherinv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "forecast",     "frr",
	  help_forecast, gnumeric_forecast, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "frequency",    "AA",
	  help_frequency, gnumeric_frequency, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ftest",        "rr",
	  help_ftest, gnumeric_ftest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gammadist",    "fffb",
	  help_gammadist, gnumeric_gammadist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gammainv",     "fff",
	  help_gammainv, gnumeric_gammainv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "geomean", NULL,
	  help_geomean, NULL, gnumeric_geomean,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "growth",       "A|AAb",
	  help_growth, gnumeric_growth, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "harmean", NULL,
	  help_harmean, NULL, gnumeric_harmean,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hypgeomdist",  "ffff|b",
	  help_hypgeomdist, gnumeric_hypgeomdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "intercept",    "AA",
	  help_intercept, gnumeric_intercept, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "kurt", NULL,
	  help_kurt, NULL, gnumeric_kurt,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "large", "Af",
	  help_large, gnumeric_large, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "leverage", "A",  help_leverage,
	  gnumeric_leverage, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "linest",       "A|Abb",
	  help_linest, gnumeric_linest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "logest",       "A|Abb",
	  help_logest, gnumeric_logest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "logfit",       "rr",
	  help_logfit, gnumeric_logfit, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "loginv",       "fff",
	  help_loginv, gnumeric_loginv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "lognormdist",  "fff",
	  help_lognormdist, gnumeric_lognormdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "logreg",       "A|Abb",
	  help_logreg, gnumeric_logreg, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "max", NULL,
	  help_max, NULL, gnumeric_max,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "maxa", NULL,
	  help_maxa, NULL, gnumeric_maxa,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "median", NULL,
	  help_median, NULL, gnumeric_median,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "min", NULL,
	  help_min, NULL, gnumeric_min,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mina", NULL,
	  help_mina, NULL, gnumeric_mina,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mode", NULL,
	  help_mode, NULL, gnumeric_mode,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mode.mult", NULL,
	  help_mode_mult, NULL, gnumeric_mode_mult,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "negbinomdist", "fff",
	  help_negbinomdist, gnumeric_negbinomdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "normdist",     "fffb",
	  help_normdist, gnumeric_normdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "snorm.dist.range", "ff",
	  help_snorm_dist_range, gnumeric_snorm_dist_range, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE,
	},
	{ "norminv",      "fff",
	  help_norminv, gnumeric_norminv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "normsdist",    "f",
	  help_normsdist, gnumeric_normsdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "normsinv",     "f",
	  help_normsinv, gnumeric_normsinv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "owent",    "ff",
	  help_owent, gnumeric_owent, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "pearson",      "AA",
	  help_pearson, gnumeric_pearson, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "percentile",   "Af",
	  help_percentile, gnumeric_percentile, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "percentile.exc",   "Af",
	  help_percentile_exc, gnumeric_percentile_exc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "percentrank",  "Af|f",
	  help_percentrank, gnumeric_percentrank, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "percentrank.exc",  "Af|f",
	  help_percentrank_exc, gnumeric_percentrank_exc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "permut",       "ff",
	  help_permut, gnumeric_permut, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "poisson",      "ffb",
	  help_poisson, gnumeric_poisson, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "prob",         "AAf|f",
	  help_prob, gnumeric_prob, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "quartile",     "Af",
	  help_quartile, gnumeric_quartile, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "quartile.exc",     "Af",
	  help_quartile_exc, gnumeric_quartile_exc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "rank",         "fr|b",
	  help_rank, gnumeric_rank, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rank.avg",         "fr|b",
	  help_rank_avg, gnumeric_rank_avg, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "slope",        "AA",
	  help_slope, gnumeric_slope, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "small", "Af",
	  help_small, gnumeric_small, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "standardize",  "fff",
	  help_standardize, gnumeric_standardize, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ssmedian",   "A|f",
	  help_ssmedian, gnumeric_ssmedian, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "stdev", NULL,
	  help_stdev, NULL, gnumeric_stdev,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "stdeva", NULL,
	  help_stdeva, NULL, gnumeric_stdeva,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "stdevp", NULL,
	  help_stdevp, NULL, gnumeric_stdevp,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "stdevpa", NULL,
	  help_stdevpa, NULL, gnumeric_stdevpa,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "steyx",        "AA",
	  help_steyx, gnumeric_steyx, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rsq",          "AA",
	  help_rsq, gnumeric_rsq, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "skew", NULL,
	  help_skew, NULL, gnumeric_skew,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tdist",        "fff",
	  help_tdist, gnumeric_tdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tinv",         "ff",
	  help_tinv, gnumeric_tinv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "trend",        "A|AAb",
	  help_trend, gnumeric_trend, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "trimmean",     "rf",
	  help_trimmean, gnumeric_trimmean, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ttest",        "rrff",
	  help_ttest, gnumeric_ttest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "var", NULL,
	  help_var, NULL, gnumeric_var,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "vara", NULL,
	  help_vara, NULL, gnumeric_vara,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "varp", NULL,
	  help_varp, NULL, gnumeric_varp,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "varpa", NULL,
	  help_varpa, NULL, gnumeric_varpa,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "weibull",      "fffb",
	  help_weibull, gnumeric_weibull, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ztest", "Af|f",
	  help_ztest, gnumeric_ztest, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "exppowdist", "fff",          help_exppowdist,
	  gnumeric_exppowdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "geomdist", "ffb",     help_geomdist,
	  gnumeric_geomdist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "kurtp", NULL,
	  help_kurtp, NULL, gnumeric_kurtp,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "landau", "f",  help_landau,
	  gnumeric_landau, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "laplace", "ff",  help_laplace,
	  gnumeric_laplace, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "logistic", "ff",  help_logistic,
	  gnumeric_logistic, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "pareto", "fff",  help_pareto,
	  gnumeric_pareto, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "rayleigh", "ff",  help_rayleigh,
	  gnumeric_rayleigh, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "rayleightail", "fff",  help_rayleightail,
	  gnumeric_rayleightail, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "skewp", NULL,
	  help_skewp, NULL, gnumeric_skewp,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "subtotal", NULL,
	  help_subtotal,    NULL, gnumeric_subtotal,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "cronbach", NULL,
	  help_cronbach, NULL, gnumeric_cronbach,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "permutationa",   "ff",       help_permutationa,
	  gnumeric_permutationa, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{NULL}
};
