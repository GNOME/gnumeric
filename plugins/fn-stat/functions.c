/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-stat.c:  Built in statistical functions and functions registration
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Michael Meeks <micheal@ximian.com>
 *   Morten Welinder <terra@diku.dk>
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
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <mathfunc.h>
#include <rangefunc.h>
#include <regression.h>
#include <sheet.h>
#include <cell.h>
#include <collect.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <func-builtin.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static gint
float_compare (const gnm_float *a, const gnm_float *b)
{
        if (*a < *b)
                return -1;
	else if (*a == *b)
	        return 0;
	else
	        return 1;
}

typedef struct {
	int N;
	gnm_float M, Q, sum;
        gboolean afun_flag;
} stat_closure_t;

static void
setup_stat_closure (stat_closure_t *cl)
{
	cl->N = 0;
	cl->M = 0.0;
	cl->Q = 0.0;
	cl->afun_flag = 0;
	cl->sum = 0.0;
}

static GnmValue *
callback_function_stat (GnmEvalPos const *ep, GnmValue *value, void *closure)
{
	stat_closure_t *mm = closure;
	gnm_float x, dx, dm;

	if (value != NULL && VALUE_IS_NUMBER (value))
		x = value_get_as_float (value);
	else {
	        if (mm->afun_flag)
		        x = 0;
		else
		        return NULL;
	}

	/* I'm paranoid - if mm->N == -1, mm->N + 1 is 0 and the next line blows out */
	if (mm->N == - 1)
	        return value_new_error_NUM (ep);

	dx = x - mm->M;
	dm = dx / (mm->N + 1);
	mm->M += dm;
	mm->Q += mm->N * dx * dm;
	mm->N++;
	mm->sum += x;

	return NULL;
}

/**
 * FIXME : this is also a kludge, but at least it is more localized
 * than before.  We should not have to do this
 */
static GnmValue *
stat_helper (stat_closure_t *cl, GnmEvalPos const *ep, GnmValue *val)
{
	GnmExprConstant expr;
	GnmExprList *expr_node_list;
	GnmValue *err;

	setup_stat_closure (cl);

	gnm_expr_constant_init (&expr, val);
	expr_node_list = gnm_expr_list_append (NULL, &expr);
	err = function_iterate_argument_values (ep,
		&callback_function_stat, cl, expr_node_list,
		TRUE, CELL_ITER_ALL);
	gnm_expr_list_free (expr_node_list);

	if (err != NULL)
		return err;
	if (cl->N <= 1)
		return value_new_error_VALUE (ep);
	return NULL;
}

/***************************************************************************/

static char const *help_varp = {
	N_("@FUNCTION=VARP\n"
	   "@SYNTAX=VARP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VARP calculates the variance of an entire population.\n"
	   "VARP is also known as the N-variance.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "VARP(A1:A5) equals 94.112.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,DVAR,DVARP,STDEV,VAR")
};

static GnmValue *
gnumeric_varp (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_var_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_var = {
	N_("@FUNCTION=VAR\n"
	   "@SYNTAX=VAR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VAR calculates sample variance of the given sample. "
	   "To get the true variance of a complete population use VARP.\n"
	   "VAR is also known as the N-1-variance. Under reasonable "
	   "conditions, it is the maximum-likelihood estimator for the "
	   "true variance.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "VAR(A1:A5) equals 117.64.\n"
	   "\n"
	   "@SEEALSO=VARP,STDEV")
};

static GnmValue *
gnumeric_var (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_var_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_stdev = {
	N_("@FUNCTION=STDEV\n"
	   "@SYNTAX=STDEV(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEV returns the sample standard deviation of the given sample.\n"
	   "To obtain the population standard deviation of a whole population "
	   "use STDEVP.\n"
	   "STDEV is also known as the N-1-standard deviation.\n"
	   "Under reasonable "
	   "conditions, it is the maximum-likelihood estimator for the "
	   "true population standard deviation.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "STDEV(A1:A5) equals 10.84619749.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,DSTDEV,DSTDEVP,STDEVA,STDEVPA,VAR")
};

static GnmValue *
gnumeric_stdev (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_stddev_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_stdevp = {
	N_("@FUNCTION=STDEVP\n"
	   "@SYNTAX=STDEVP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEVP returns the population standard deviation of the given "
	   "population. \n"
	   "This is also known as the N-standard deviation\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "STDEVP(A1:A5) equals 9.701133954.\n"
	   "\n"
	   "@SEEALSO=STDEV,STDEVA,STDEVPA")
};

static GnmValue *
gnumeric_stdevp (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_stddev_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_rank = {
	N_("@FUNCTION=RANK\n"
	   "@SYNTAX=RANK(x,ref[,order])\n"

	   "@DESCRIPTION="
	   "RANK returns the rank of a number in a list of numbers.  @x is "
	   "the number whose rank you want to find, @ref is the list of "
	   "numbers, and @order specifies how to rank numbers.  If @order is "
	   "0, numbers are ranked in descending order, otherwise numbers are "
	   "ranked in ascending order.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "RANK(17.3,A1:A5) equals 4.\n"
	   "\n"
	   "@SEEALSO=PERCENTRANK")
};

typedef struct {
        gnm_float x;
        int     order;
        int     rank;
} stat_rank_t;

static GnmValue *
callback_function_rank (Sheet *sheet, int col, int row,
			GnmCell *cell, void *user_data)
{
        stat_rank_t *p = user_data;
	gnm_float  x;

	cell_eval (cell);
	if (cell->value == NULL)
	        return NULL;

	switch (cell->value->type) {
        case VALUE_BOOLEAN:
                x = cell->value->v_bool.val ? 1 : 0;
                break;
        case VALUE_INTEGER:
                x = cell->value->v_int.val;
                break;
        case VALUE_FLOAT:
                x = cell->value->v_float.val;
                break;
	case VALUE_STRING: /* XL does not appear to do implicit conversion */
	case VALUE_EMPTY:
        default:
                return NULL;	/* ignore them */
        }

	if (p->order) {
	        if (x < p->x)
		        p->rank++;
	} else {
	        if (x > p->x)
		        p->rank++;
	}

	return NULL;
}

static GnmValue *
gnumeric_rank (FunctionEvalInfo *ei, GnmValue **argv)
{
	stat_rank_t p;
	GnmValue      *ret;

	p.x = value_get_as_float (argv[0]);
	if (argv[2])
	        p.order = value_get_as_int (argv[2]);
	else
	        p.order = 0;
	p.rank = 1;
	ret = sheet_foreach_cell_in_range (
	        eval_sheet (argv[1]->v_range.cell.a.sheet, ei->pos->sheet),
		CELL_ITER_IGNORE_BLANK,
		argv[1]->v_range.cell.a.col,
		argv[1]->v_range.cell.a.row,
		argv[1]->v_range.cell.b.col,
		argv[1]->v_range.cell.b.row,
		callback_function_rank,
		&p);

	if (ret != NULL)
		return value_new_error_VALUE (ei->pos);

	return value_new_int (p.rank);
}

/***************************************************************************/

static char const *help_trimmean = {
	N_("@FUNCTION=TRIMMEAN\n"
	   "@SYNTAX=TRIMMEAN(ref,fraction)\n"

	   "@DESCRIPTION="
	   "TRIMMEAN returns the mean of the interior of a data set. @ref "
	   "is the list of numbers whose mean you want to calculate and "
	   "@fraction is the fraction of the data set excluded from the mean. "
	   "For example, if @fraction=0.2 and the data set contains 40 "
	   "numbers, 8 numbers are trimmed from the data set (40 x 0.2), 4 "
	   "from the top and 4 from the bottom of the set.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "TRIMMEAN(A1:A5,0.2) equals 23.2.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,GEOMEAN,HARMEAN,MEDIAN,MODE")
};

static int
range_trimmean (const gnm_float *xs, int n, gnm_float *res)
{
	gnm_float p, sum = 0;
	int tc, c, i;

	if (n < 2)
		return 1;

	p = xs[--n];
	if (p < 0 || p > 1)
		return 1;

	tc = (n * p) / 2;
	c = n - 2 * tc;
	if (c == 0)
		return 1;

	/* OK, so we ignore the constness here.  Tough.  */
	qsort ((gnm_float *) xs, n, sizeof (xs[0]), (void *) &float_compare);

	for (i = tc; i < n - tc; i++)
		sum += xs[i];

	*res = sum / c;
	return 0;
}

static GnmValue *
gnumeric_trimmean (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_trimmean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_covar = {
	N_("@FUNCTION=COVAR\n"
	   "@SYNTAX=COVAR(array1,array2)\n"

	   "@DESCRIPTION="
	   "COVAR returns the covariance of two data sets.\n"
	   "\n"
	   "* Strings and empty cells are simply ignored.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "COVAR(A1:A5,B1:B5) equals 65.858.\n"
	   "\n"
	   "@SEEALSO=CORREL,FISHER,FISHERINV")
};

static GnmValue *
gnumeric_covar (FunctionEvalInfo *ei, GnmValue **argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      range_covar,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_correl = {
	N_("@FUNCTION=CORREL\n"
	   "@SYNTAX=CORREL(array1,array2)\n"

	   "@DESCRIPTION="
	   "CORREL returns the correlation coefficient of two data sets.\n"
	   "\n"
	   "* Strings and empty cells are simply ignored.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "CORREL(A1:A5,B1:B5) equals 0.996124788.\n"
	   "\n"
	   "@SEEALSO=COVAR,FISHER,FISHERINV")
};

static GnmValue *
gnumeric_correl (FunctionEvalInfo *ei, GnmValue **argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      range_correl_pop,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_negbinomdist = {
	N_("@FUNCTION=NEGBINOMDIST\n"
	   "@SYNTAX=NEGBINOMDIST(f,t,p)\n"

	   "@DESCRIPTION="
	   "NEGBINOMDIST function returns the negative binomial "
	   "distribution. @f is the number of failures, @t is the threshold "
	   "number of successes, and @p is the probability of a success.\n"
	   "\n"
	   "* If @f or @t is a non-integer it is truncated.\n"
	   "* If (@f + @t -1) <= 0 NEGBINOMDIST returns #NUM! error.\n"
	   "* If @p < 0 or @p > 1 NEGBINOMDIST returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "NEGBINOMDIST(2,5,0.55) equals 0.152872629.\n"
	   "\n"
	   "@SEEALSO=BINOMDIST,COMBIN,FACT,HYPGEOMDIST,PERMUT")
};

static GnmValue *
gnumeric_negbinomdist (FunctionEvalInfo *ei, GnmValue **argv)
{
	int x, r;
	gnm_float p;

	x = value_get_as_int (argv[0]);
	r = value_get_as_int (argv[1]);
	p = value_get_as_float (argv[2]);

	if ((x + r - 1) <= 0 || p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (dnbinom (x, r, p, FALSE));
}

/***************************************************************************/

static char const *help_normsdist = {
        N_("@FUNCTION=NORMSDIST\n"
           "@SYNTAX=NORMSDIST(x)\n"

           "@DESCRIPTION="
           "NORMSDIST function returns the standard normal cumulative "
	   "distribution. @x is the value for which you want the "
	   "distribution.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "NORMSDIST(2) equals 0.977249868.\n"
	   "\n"
           "@SEEALSO=NORMDIST")
};

static GnmValue *
gnumeric_normsdist (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float x;

        x = value_get_as_float (argv[0]);

	return value_new_float (pnorm (x, 0, 1, TRUE, FALSE));
}

/***************************************************************************/

static char const *help_normsinv = {
        N_("@FUNCTION=NORMSINV\n"
           "@SYNTAX=NORMSINV(p)\n"

           "@DESCRIPTION="
           "NORMSINV function returns the inverse of the standard normal "
	   "cumulative distribution. @p is the given probability "
	   "corresponding to the normal distribution.\n\n"
	   "* If @p < 0 or @p > 1 NORMSINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "NORMSINV(0.2) equals -0.841621234.\n"
	   "\n"
           "@SEEALSO=NORMDIST,NORMINV,NORMSDIST,STANDARDIZE,ZTEST")
};


static GnmValue *
gnumeric_normsinv (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float p;

        p = value_get_as_float (argv[0]);
	if (p < 0 || p > 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qnorm (p, 0, 1, TRUE, FALSE));
}

/***************************************************************************/

static char const *help_lognormdist = {
        N_("@FUNCTION=LOGNORMDIST\n"
           "@SYNTAX=LOGNORMDIST(x,mean,stddev)\n"

           "@DESCRIPTION="
           "LOGNORMDIST function returns the lognormal distribution. "
	   "@x is the value for which you want the distribution, @mean is "
	   "the mean of the distribution, and @stddev is the standard "
	   "deviation of the distribution.\n\n"
	   "* If @stddev = 0 LOGNORMDIST returns #DIV/0! error.\n"
	   "* If @x <= 0, @mean < 0 or @stddev < 0 LOGNORMDIST returns #NUM! "
	   "error.\n"
           "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "LOGNORMDIST(3,1,2) equals 0.519662338.\n"
	   "\n"
           "@SEEALSO=NORMDIST")
};

static GnmValue *
gnumeric_lognormdist (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float x, mean, stddev;

        x = value_get_as_float (argv[0]);
        mean = value_get_as_float (argv[1]);
        stddev = value_get_as_float (argv[2]);

        if (stddev == 0)
                return value_new_error_DIV0 (ei->pos);

        if (x <= 0 || mean < 0 || stddev < 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float (plnorm (x, mean, stddev, TRUE, FALSE));
}

/***************************************************************************/

static char const *help_loginv = {
        N_("@FUNCTION=LOGINV\n"
           "@SYNTAX=LOGINV(p,mean,stddev)\n"

           "@DESCRIPTION="
           "LOGINV function returns the inverse of the lognormal "
	   "cumulative distribution. @p is the given probability "
	   "corresponding to the normal distribution, @mean is the "
	   "arithmetic mean of the distribution, and @stddev is the "
	   "standard deviation of the distribution.\n"
           "\n"
	   "* If @p < 0 or @p > 1 or @stddev <= 0 LOGINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
 	   "\n"
	   "@EXAMPLES=\n"
	   "LOGINV(0.5,2,3) equals 7.389056099.\n"
	   "\n"
           "@SEEALSO=EXP,LN,LOG,LOG10,LOGNORMDIST")
};

static GnmValue *
gnumeric_loginv (FunctionEvalInfo *ei, GnmValue **argv)
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

static char const *help_fisherinv = {
        N_("@FUNCTION=FISHERINV\n"
           "@SYNTAX=FISHERINV(x)\n"

           "@DESCRIPTION="
           "FISHERINV function returns the inverse of the Fisher "
	   "transformation at @x.\n"
           "\n"
           "* If @x is non-number FISHERINV returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "FISHERINV(2) equals 0.96402758.\n"
	   "\n"
           "@SEEALSO=FISHER")
};

static GnmValue *
gnumeric_fisherinv (FunctionEvalInfo *ei, GnmValue **argv)
{
       gnm_float y = value_get_as_float (argv[0]);

       return value_new_float (expm1gnum (2 * y) / (expgnum (2 * y) + 1.0));
}

/***************************************************************************/

static char const *help_mode = {
        N_("@FUNCTION=MODE\n"
           "@SYNTAX=MODE(n1, n2, ...)\n"

           "@DESCRIPTION="
           "MODE returns the most common number of the data set. If the data "
	   "set has many most common numbers MODE returns the first one of "
	   "them.\n"
           "\n"
           "* Strings and empty cells are simply ignored.\n"
	   "* If the data set does not contain any duplicates MODE returns "
           "#N/A error.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 11.4, 25.9, and 40.1.  Then\n"
	   "MODE(A1:A5) equals 11.4.\n"
	   "\n"
           "@SEEALSO=AVERAGE,MEDIAN")
};

static GnmValue *
gnumeric_mode (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_mode,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NA);
}

/***************************************************************************/

static char const *help_harmean = {
	N_("@FUNCTION=HARMEAN\n"
	   "@SYNTAX=HARMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "HARMEAN returns the harmonic mean of the N data points "
	   "(that is, N divided by the sum of the inverses of "
	   "the data points).\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "HARMEAN(A1:A5) equals 19.529814427.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,GEOMEAN,MEDIAN,MODE,TRIMMEAN")
};

static GnmValue *
gnumeric_harmean (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_harmonic_mean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static char const *help_geomean = {
	N_("@FUNCTION=GEOMEAN\n"
	   "@SYNTAX=GEOMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "GEOMEAN returns the geometric mean of the given arguments. "
	   "This is equal to the Nth root of the product of the terms.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "GEOMEAN(A1:A5) equals 21.279182482.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,HARMEAN,MEDIAN,MODE,TRIMMEAN")
};

static GnmValue *
gnumeric_geomean (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_geometric_mean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static char const *help_count = {
	N_("@FUNCTION=COUNT\n"
	   "@SYNTAX=COUNT(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "COUNT returns the total number of integer or floating point "
	   "arguments passed.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "COUNT(A1:A5) equals 5.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static GnmValue *
callback_function_count (GnmEvalPos const *ep, GnmValue *value, void *closure)
{
	GnmValue *result = (GnmValue *) closure;

	if (value && 
	    (value->type == VALUE_INTEGER || value->type == VALUE_FLOAT))
		result->v_int.val++;
	return NULL;
}

static GnmValue *
gnumeric_count (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	GnmValue *result = value_new_int (0);

	/* no need to check for error, this is not strict */
	function_iterate_argument_values (ei->pos,
		callback_function_count, result, expr_node_list,
		FALSE, CELL_ITER_IGNORE_BLANK);

	return result;
}

/***************************************************************************/

static char const *help_counta = {
        N_("@FUNCTION=COUNTA\n"
           "@SYNTAX=COUNTA(b1, b2, ...)\n"

           "@DESCRIPTION="
           "COUNTA returns the number of arguments passed not including "
	   "empty cells.\n\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, \"missing\", \"missing\", 25.9, and 40.1.  "
	   "Then\n"
	   "COUNTA(A1:A5) equals 5.\n"
	   "\n"
           "@SEEALSO=AVERAGE,COUNT,DCOUNT,DCOUNTA,PRODUCT,SUM")
};

static GnmValue *
callback_function_counta (GnmEvalPos const *ep, GnmValue *value, void *closure)
{
        GnmValue *result = (GnmValue *) closure;

	result->v_int.val++;
	return NULL;
}

static GnmValue *
gnumeric_counta (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
        GnmValue *result = value_new_int (0);

	/* no need to check for error, this is not strict */
        function_iterate_argument_values (ei->pos,
		callback_function_counta, result, expr_node_list,
		FALSE, CELL_ITER_IGNORE_BLANK);

        return result;
}

/***************************************************************************/

static char const *help_average = {
	N_("@FUNCTION=AVERAGE\n"
	   "@SYNTAX=AVERAGE(value1, value2,...)\n"

	   "@DESCRIPTION="
	   "AVERAGE computes the average of all the values and cells "
	   "referenced in the argument list.  This is equivalent to the "
	   "sum of the arguments divided by the count of the arguments.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "AVERAGE(A1:A5) equals 23.2.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT")
};

static GnmValue *
gnumeric_average (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_average,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_min = {
	N_("@FUNCTION=MIN\n"
	   "@SYNTAX=MIN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MIN returns the value of the element of the values passed "
	   "that has the smallest value, with negative numbers considered "
	   "smaller than positive numbers.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "MIN(A1:A5) equals 11.4.\n"
	   "\n"
	   "@SEEALSO=MAX,ABS")
};

static int
range_min0 (const gnm_float *xs, int n, gnm_float *res)
{
	if (n == 0) {
		*res = 0;
		return 0;
	} else
		return range_min (xs, n, res);
}

static GnmValue *
gnumeric_min (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_min0,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_max = {
	N_("@FUNCTION=MAX\n"
	   "@SYNTAX=MAX(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MAX returns the value of the element of the values passed "
	   "that has the largest value, with negative numbers considered "
	   "smaller than positive numbers.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "MAX(A1:A5) equals 40.1.\n"
	   "\n"
	   "@SEEALSO=MIN,ABS")
};

static int
range_max0 (const gnm_float *xs, int n, gnm_float *res)
{
	if (n == 0) {
		*res = 0;
		return 0;
	} else
		return range_max (xs, n, res);
}

static GnmValue *
gnumeric_max (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_max0,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_skew = {
	N_("@FUNCTION=SKEW\n"
	   "@SYNTAX=SKEW(n1, n2, ...)\n"

	   "@DESCRIPTION="
	   "SKEW returns an unbiased estimate for skewness of a distribution.\n"
	   "\n"
	   "Note, that this is only meaningful if the underlying "
	   "distribution really has a third moment.  The skewness of a "
	   "symmetric (e.g., normal) distribution is zero.\n"
           "\n"
	   "* Strings and empty cells are simply ignored.\n"
	   "* If less than three numbers are given, SKEW returns #DIV/0! "
	   "error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "SKEW(A1:A5) equals 0.976798268.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,VAR,SKEWP,KURT")
};

static GnmValue *
gnumeric_skew (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_skew_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_skewp = {
	N_("@FUNCTION=SKEWP\n"
	   "@SYNTAX=SKEWP(n1, n2, ...)\n"

	   "@DESCRIPTION="
	   "SKEWP returns the population skewness of a data set.\n"
	   "\n"
	   "* Strings and empty cells are simply ignored.\n"
	   "* If less than two numbers are given, SKEWP returns #DIV/0! "
	   "error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "SKEWP(A1:A5) equals 0.655256198.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,VARP,SKEW,KURTP")
};

static GnmValue *
gnumeric_skewp (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_skew_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_expondist = {
	N_("@FUNCTION=EXPONDIST\n"
	   "@SYNTAX=EXPONDIST(x,y,cumulative)\n"

	   "@DESCRIPTION="
	   "EXPONDIST function returns the exponential distribution. "
	   "If the @cumulative boolean is false it will return:\n\n\t"
	   "@y * exp (-@y*@x),\n\notherwise it will return\n\n\t"
	   "1 - exp (-@y*@x).\n"
	   "\n"
	   "* If @x < 0 or @y <= 0 this will return an error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "EXPONDIST(2,4,0) equals 0.001341851.\n"
	   "\n"
	   "@SEEALSO=POISSON")
};

static GnmValue *
gnumeric_expondist (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x, y;
	int cuml;
	gboolean err;

	x = value_get_as_float (argv[0]);
	y = value_get_as_float (argv[1]);

	if (x < 0.0 || y <= 0.0)
		return value_new_error_NUM (ei->pos);

	cuml = value_get_as_bool (argv[2], &err);
	if (err)
		return value_new_error_VALUE (ei->pos);

	if (cuml)
		return value_new_float (pexp (x, 1 / y, TRUE, FALSE));
	else
		return value_new_float (dexp (x, 1 / y, FALSE));
}

/***************************************************************************/

static char const *help_bernoulli = {
        N_("@FUNCTION=BERNOULLI\n"
           "@SYNTAX=BERNOULLI(k,p)\n"

           "@DESCRIPTION="
           "BERNOULLI returns the probability p(k) of obtaining @k "
	   "from a Bernoulli distribution with probability parameter @p.\n"
           "\n"
           "* If @k != 0 and @k != 1 BERNOULLI returns #NUM! error.\n"
           "* If @p < 0 or @p > 1 BERNOULLI returns #NUM! error.\n"
	   "\n"
           "@EXAMPLES=\n"
           "BERNOULLI(0,0.5).\n"
           "\n"
           "@SEEALSO=RANDBERNOULLI")
};

static gnm_float
random_bernoulli_pdf (int k, gnm_float p)
{
        if (k == 0)
	        return 1 - p;
	else if (k == 1)
	        return p;
	else
	        return 0;
}

static GnmValue *
gnumeric_bernoulli (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float k = value_get_as_int (argv[0]);
	gnm_float p = value_get_as_float (argv[1]);

	if (p < 0 || p > 1 || (k != 0 && k != 1))
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_bernoulli_pdf (k, p));
}

/***************************************************************************/

static char const *help_gammaln = {
	N_("@FUNCTION=GAMMALN\n"
	   "@SYNTAX=GAMMALN(x)\n"

	   "@DESCRIPTION="
	   "GAMMALN function returns the natural logarithm of the "
	   "gamma function.\n"
	   "\n"
	   "* If @x is non-number then GAMMALN returns #VALUE! error.\n"
	   "* If @x <= 0 then GAMMALN returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "GAMMALN(23) equals 48.471181352.\n"
	   "\n"
	   "@SEEALSO=POISSON")
};

static GnmValue *
gnumeric_gammaln (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x;

	/* FIXME: the gamma function is defined for all real numbers except
	 * the integers 0, -1, -2, ...  It is positive (and log(gamma(x)) is
	 * thus defined) when x>0 or -2<x<-1 or -4<x<-3 ...  */

	x = value_get_as_float (argv[0]);

	if (x <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (lgammagnum (x));
}

/***************************************************************************/

static char const *help_gammadist = {
	N_("@FUNCTION=GAMMADIST\n"
	   "@SYNTAX=GAMMADIST(x,alpha,beta,cum)\n"

	   "@DESCRIPTION="
	   "GAMMADIST function returns the gamma distribution. If @cum "
	   "is TRUE, GAMMADIST returns the incomplete gamma function, "
	   "otherwise it returns the probability mass function.\n"
	   "\n"
	   "* If @x < 0 GAMMADIST returns #NUM! error.\n"
	   "* If @alpha <= 0 or @beta <= 0, GAMMADIST returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "GAMMADIST(1,2,3,0) equals 0.07961459.\n"
	   "\n"
	   "@SEEALSO=GAMMAINV")
};

static GnmValue *
gnumeric_gammadist (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x, alpha, beta;
	int     cum;

	x = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);

	if (x < 0 || alpha <= 0 || beta <= 0)
		return value_new_error_NUM (ei->pos);

	cum = value_get_as_int (argv[3]);
	if (cum)
	        return value_new_float (pgamma (x, alpha, beta, TRUE, FALSE));
	else
	        return value_new_float (dgamma (x, alpha, beta, FALSE));
}

/***************************************************************************/

static char const *help_gammainv = {
        N_("@FUNCTION=GAMMAINV\n"
           "@SYNTAX=GAMMAINV(p,alpha,beta)\n"

           "@DESCRIPTION="
           "GAMMAINV function returns the inverse of the cumulative "
	   "gamma distribution.\n"
           "\n"
	   "* If @p < 0 or @p > 1 GAMMAINV returns #NUM! error.\n"
	   "* If @alpha <= 0 or @beta <= 0 GAMMAINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "GAMMAINV(0.34,2,4) equals 4.829093908.\n"
	   "\n"
           "@SEEALSO=GAMMADIST")
};

static GnmValue *
gnumeric_gammainv (FunctionEvalInfo *ei, GnmValue **argv)
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

static char const *help_chidist = {
	N_("@FUNCTION=CHIDIST\n"
	   "@SYNTAX=CHIDIST(x,dof)\n"

	   "@DESCRIPTION="
	   "CHIDIST function returns the one-tailed probability of the "
	   "chi-squared distribution. @dof is the number of degrees of "
	   "freedom.\n"
	   "\n"
	   "* If @dof is non-integer it is truncated.\n"
	   "* If @dof < 1 CHIDIST returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CHIDIST(5.3,2) equals 0.070651213.\n"
	   "\n"
	   "@SEEALSO=CHIINV,CHITEST")
};

static GnmValue *
gnumeric_chidist (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x;
	int     dof;

	x = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);

	if (dof < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (pchisq (x, dof, FALSE, FALSE));
}

/***************************************************************************/

static char const *help_chiinv = {
        N_("@FUNCTION=CHIINV\n"
           "@SYNTAX=CHIINV(p,dof)\n"

           "@DESCRIPTION="
           "CHIINV function returns the inverse of the one-tailed "
	   "probability of the chi-squared distribution.\n"
           "\n"
	   "* If @p < 0 or @p > 1 or @dof < 1 CHIINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CHIINV(0.98,7) equals 1.564293004.\n"
	   "\n"
           "@SEEALSO=CHIDIST,CHITEST")
};

static GnmValue *
gnumeric_chiinv (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float p;
	int dof;

        p = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);

	if (p < 0 || p > 1 || dof < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qchisq (p, dof, FALSE, FALSE));
}

/***************************************************************************/

static char const *help_chitest = {
        N_("@FUNCTION=CHITEST\n"
           "@SYNTAX=CHITEST(actual_range,theoretical_range)\n"

           "@DESCRIPTION="
           "CHITEST function returns the test for independence of "
	   "chi-squared distribution.\n"
           "\n"
	   "@actual_range is a range that contains the observed data points. "
	   "@theoretical_range is a range that contains the expected values "
	   "of the data points.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
           "@SEEALSO=CHIDIST,CHIINV")
};

typedef struct {
        GSList *columns;
        GSList *column;
        int col;
        int row;
        int cols;
        int rows;
} stat_chitest_t;

static GnmValue *
callback_function_chitest_actual (GnmEvalPos const *ep, GnmValue *value,
				  void *closure)
{
	stat_chitest_t *mm = closure;
	gnm_float     *p;

	if (!VALUE_IS_NUMBER (value))
		return VALUE_TERMINATE;

	p = g_new (gnm_float, 1);
	*p = value_get_as_float (value);
	mm->column = g_slist_append (mm->column, p);

	mm->row++;
	if (mm->row == mm->rows) {
	        mm->row = 0;
		mm->col++;
		mm->columns = g_slist_append (mm->columns, mm->column);
		mm->column = NULL;
	}

	return NULL;
}

typedef struct {
        GSList *current_cell;
        GSList *next_col;
        int    cols;
        int    rows;
        gnm_float sum;
} stat_chitest_t_t;

static GnmValue *
callback_function_chitest_theoretical (GnmEvalPos const *ep, GnmValue *value,
				       void *closure)
{
	stat_chitest_t_t *mm = closure;
	gnm_float a, e, *p;

	if (!VALUE_IS_NUMBER (value))
		return VALUE_TERMINATE;

	e = value_get_as_float (value);

	if (mm->current_cell == NULL) {
	        mm->current_cell = mm->next_col->data;
		mm->next_col = mm->next_col->next;
	}
	p = mm->current_cell->data;
	a = *p;

	if (e == 0)
	        return value_new_error_NUM (ep);

	mm->sum += ((a-e) * (a-e)) / e;
	g_free (p);
	mm->current_cell = mm->current_cell->next;

	return NULL;
}

static GnmValue *
gnumeric_chitest (FunctionEvalInfo *ei, GnmValue **argv)
{
	stat_chitest_t   p1;
	stat_chitest_t_t p2;
	GSList           *tmp;
	GnmValue           *ret;

	p1.row = p1.col = 0;
	p1.columns = p1.column = NULL;
	p1.cols = abs (argv[0]->v_range.cell.b.col -
		       argv[0]->v_range.cell.a.col) + 1;
	p1.rows = abs (argv[0]->v_range.cell.b.row -
		       argv[0]->v_range.cell.a.row) + 1;
	p2.rows = abs (argv[1]->v_range.cell.b.row -
		       argv[1]->v_range.cell.a.row) + 1;
	p2.cols = abs (argv[1]->v_range.cell.b.col -
		       argv[1]->v_range.cell.a.col) + 1;

	if (p1.cols != p2.cols || p1.rows != p2.rows)
		return value_new_error_NUM (ei->pos);

	ret = function_iterate_do_value (ei->pos,
		(FunctionIterateCB) callback_function_chitest_actual,
		&p1, argv[0], TRUE, CELL_ITER_IGNORE_BLANK);
	if (ret != NULL)
		return value_new_error_NUM (ei->pos);

	p2.sum = 0;
	p2.current_cell = p1.columns->data;
	p2.next_col = p1.columns->next;
	ret = function_iterate_do_value (ei->pos,
		(FunctionIterateCB) callback_function_chitest_theoretical,
		&p2, argv[1], TRUE, CELL_ITER_IGNORE_BLANK);
	if (ret != NULL)
		return value_new_error_NUM (ei->pos);

	for (tmp = p1.columns; tmp != NULL ; tmp = tmp->next)
	        g_slist_free (tmp->data);
	g_slist_free (p1.columns);

	/* FIXME : XL docs claim df = (r-1)(c-1) not (r-1),
	 * However, that makes no sense.
	 */
	return value_new_float (pchisq (p2.sum, (p1.rows - 1), FALSE, FALSE));
}

/***************************************************************************/

static char const *help_betadist = {
	N_("@FUNCTION=BETADIST\n"
	   "@SYNTAX=BETADIST(x,alpha,beta[,a,b])\n"

	   "@DESCRIPTION="
	   "BETADIST function returns the cumulative beta distribution. @a "
	   "is the optional lower bound of @x and @b is the optional upper "
	   "bound of @x."
	   "\n"
	   "* If @a is not given, BETADIST uses 0.\n"
	   "* If @b is not given, BETADIST uses 1.\n"
	   "* If @x < @a or @x > @b BETADIST returns #NUM! error.\n"
	   "* If @alpha <= 0 or @beta <= 0, BETADIST returns #NUM! error.\n"
	   "* If @a >= @b BETADIST returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BETADIST(0.12,2,3) equals 0.07319808.\n"
	   "\n"
	   "@SEEALSO=BETAINV")
};

static GnmValue *
gnumeric_betadist (FunctionEvalInfo *ei, GnmValue **argv)
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

static char const *help_betainv = {
	N_("@FUNCTION=BETAINV\n"
	   "@SYNTAX=BETAINV(p,alpha,beta[,a,b])\n"

	   "@DESCRIPTION="
	   "BETAINV function returns the inverse of cumulative beta "
	   "distribution.  @a is the optional lower bound of @x and @b "
	   "is the optional upper bound of @x.\n"
	   "\n"
	   "* If @a is not given, BETAINV uses 0.\n"
	   "* If @b is not given, BETAINV uses 1.\n"
	   "* If @p < 0 or @p > 1 BETAINV returns #NUM! error.\n"
	   "* If @alpha <= 0 or @beta <= 0, BETAINV returns #NUM! error.\n"
	   "* If @a >= @b BETAINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BETAINV(0.45,1.6,1) equals 0.607096629.\n"
	   "\n"
	   "@SEEALSO=BETADIST")
};

static GnmValue *
gnumeric_betainv (FunctionEvalInfo *ei, GnmValue **argv)
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

static char const *help_tdist = {
	N_("@FUNCTION=TDIST\n"
	   "@SYNTAX=TDIST(x,dof,tails)\n"

	   "@DESCRIPTION="
	   "TDIST function returns the Student's t-distribution. @dof is "
	   "the degree of freedom and @tails is 1 or 2 depending on whether "
	   "you want one-tailed or two-tailed distribution.\n"
	   "@tails = 1 returns the size of the right tail.\n"
	   "\n"
	   "* If @dof < 1 TDIST returns #NUM! error.\n"
	   "* If @tails is neither 1 or 2 TDIST returns #NUM! error.\n"
	   "* This function is Excel compatible for non-negative @x.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TDIST(2,5,1) equals 0.050969739.\n"
	   "TDIST(-2,5,1) equals 0.949030261.\n"
	   "TDIST(0,5,2) equals 1.\n"
	   "\n"
	   "@SEEALSO=TINV,TTEST")
};

static GnmValue *
gnumeric_tdist (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x;
	int     dof, tails;

	x = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);
	tails = value_get_as_int (argv[2]);

	if (dof < 1 || (tails != 1 && tails != 2))
		return value_new_error_NUM (ei->pos);

	return value_new_float (tails * pt ((tails == 1) ? x : gnumabs(x), 
					    dof, FALSE, FALSE));
}

/***************************************************************************/

static char const *help_tinv = {
        N_("@FUNCTION=TINV\n"
           "@SYNTAX=TINV(p,dof)\n"

           "@DESCRIPTION="
           "TINV function returns the inverse of the two-tailed Student's "
	   "t-distribution.\n"
           "\n"
	   "* If @p < 0 or @p > 1 or @dof < 1 TINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TINV(0.4,32) equals 0.852998454.\n"
	   "\n"
           "@SEEALSO=TDIST,TTEST")
};

static GnmValue *
gnumeric_tinv (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float p;
	int dof;

        p = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);

	if (p < 0 || p > 1 || dof < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qt (p / 2, dof, FALSE, FALSE));
}

/***************************************************************************/

static char const *help_fdist = {
	N_("@FUNCTION=FDIST\n"
	   "@SYNTAX=FDIST(x,dof1,dof2)\n"

	   "@DESCRIPTION="
	   "FDIST function returns the F probability distribution. @dof1 "
	   "is the numerator degrees of freedom and @dof2 is the denominator "
	   "degrees of freedom.\n"
	   "\n"
	   "* If @x < 0 FDIST returns #NUM! error.\n"
	   "* If @dof1 < 1 or @dof2 < 1, FDIST returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "FDIST(2,5,5) equals 0.232511319.\n"
	   "\n"
	   "@SEEALSO=FINV")
};

static GnmValue *
gnumeric_fdist (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x;
	int     dof1, dof2;

	x = value_get_as_float (argv[0]);
	dof1 = value_get_as_int (argv[1]);
	dof2 = value_get_as_int (argv[2]);

	if (x < 0 || dof1 < 1 || dof2 < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (pf (x, dof1, dof2, FALSE, FALSE));
}

/***************************************************************************/

static char const *help_landau = {
        N_("@FUNCTION=LANDAU\n"
           "@SYNTAX=LANDAU(x)\n"

           "@DESCRIPTION="
           "LANDAU returns the probability density p(x) at @x for the "
	   "Landau distribution using an approximation method. "
           "\n"
           "@EXAMPLES=\n"
           "LANDAU(0.34).\n"
           "\n"
           "@SEEALSO=RANDLANDAU")
};

/* From the GNU Scientific Library 1.1.1 (randist/landau.c)
 *
 * Copyright (C) 2001 David Morrison
 */
static gnm_float
random_landau_pdf (gnm_float x)
{
        static gnm_float P1[5] = {
	        0.4259894875E0, -0.1249762550E0, 0.3984243700E-1,
		-0.6298287635E-2, 0.1511162253E-2
	};
	static gnm_float P2[5] = {
	        0.1788541609E0, 0.1173957403E0, 0.1488850518E-1,
		-0.1394989411E-2, 0.1283617211E-3
	};
	static gnm_float P3[5] = {
	        0.1788544503E0, 0.9359161662E-1, 0.6325387654E-2,
		0.6611667319E-4, -0.2031049101E-5
	};
	static gnm_float P4[5] = {
	        0.9874054407E0, 0.1186723273E3, 0.8492794360E3,
		-0.7437792444E3, 0.4270262186E3
	};
	static gnm_float P5[5] = {
	        0.1003675074E1, 0.1675702434E3, 0.4789711289E4,
		0.2121786767E5, -0.2232494910E5
	};
	static gnm_float P6[5] = {
	        0.1000827619E1, 0.6649143136E3, 0.6297292665E5,
		0.4755546998E6, -0.5743609109E7
	};

	static gnm_float Q1[5] = {
	        1.0, -0.3388260629E0, 0.9594393323E-1,
		-0.1608042283E-1, 0.3778942063E-2
	};
	static gnm_float Q2[5] = {
	        1.0, 0.7428795082E0, 0.3153932961E0,
		0.6694219548E-1, 0.8790609714E-2
	};
	static gnm_float Q3[5] = {
	        1.0, 0.6097809921E0, 0.2560616665E0,
		0.4746722384E-1, 0.6957301675E-2
	};
	static gnm_float Q4[5] = {
	        1.0, 0.1068615961E3, 0.3376496214E3,
		0.2016712389E4, 0.1597063511E4
	};
	static gnm_float Q5[5] = {
	        1.0, 0.1569424537E3, 0.3745310488E4,
		0.9834698876E4, 0.6692428357E5
	};
	static gnm_float Q6[5] = {
	        1.0, 0.6514101098E3, 0.5697473333E5,
		0.1659174725E6, -0.2815759939E7
	};

	static gnm_float A1[3] = {
	        0.4166666667E-1, -0.1996527778E-1, 0.2709538966E-1
	};
	static gnm_float A2[2] = {
	        -0.1845568670E1, -0.4284640743E1
	};

	gnm_float U, V, DENLAN;

	V = x;
	if (V < -5.5) {
	        U      = expgnum (V + 1.0);
		DENLAN = 0.3989422803 * (expgnum ( -1 / U) / sqrtgnum (U)) *
		        (1 + (A1[0] + (A1[1] + A1[2] * U) * U) * U);
	} else if (V < -1) {
	        U = expgnum (-V - 1);
		DENLAN = expgnum ( -U) * sqrtgnum (U) *
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
	        U = 1 / (V - V * log(V) / (V + 1));
		DENLAN = U * U * (1 + (A2[0] + A2[1] * U) * U);
	}

	return DENLAN;
}

static GnmValue *
gnumeric_landau (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x = value_get_as_float (argv[0]);

	return value_new_float (random_landau_pdf (x));
}


/***************************************************************************/

static char const *help_finv = {
        N_("@FUNCTION=FINV\n"
           "@SYNTAX=FINV(p,dof1,dof2)\n"

           "@DESCRIPTION="
           "FINV function returns the inverse of the F probability "
	   "distribution.\n"
           "\n"
	   "* If @p < 0 or @p > 1 FINV returns #NUM! error.\n"
	   "* If @dof1 < 1 or @dof2 < 1 FINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "FINV(0.2,2,4) equals 2.472135955.\n"
	   "\n"
           "@SEEALSO=FDIST")
};

static GnmValue *
gnumeric_finv (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float p;
	int dof1, dof2;

        p = value_get_as_float (argv[0]);
	dof1 = value_get_as_int (argv[1]);
	dof2 = value_get_as_int (argv[2]);

	if (p < 0 || p > 1 || dof1 < 1 || dof2 < 1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qf (p, dof1, dof2, FALSE, FALSE));
}

/***************************************************************************/

static char const *help_binomdist = {
	N_("@FUNCTION=BINOMDIST\n"
	   "@SYNTAX=BINOMDIST(n,trials,p,cumulative)\n"

	   "@DESCRIPTION="
	   "BINOMDIST function returns the binomial distribution. "
	   "@n is the number of successes, @trials is the total number of "
           "independent trials, @p is the probability of success in trials, "
           "and @cumulative describes whether to return the sum of the "
           "binomial function from 0 to @n.\n"
	   "\n"
	   "* If @n or @trials are non-integer they are truncated.\n"
	   "* If @n < 0 or @trials < 0 BINOMDIST returns #NUM! error.\n"
	   "* If @n > @trials BINOMDIST returns #NUM! error.\n"
	   "* If @p < 0 or @p > 1 BINOMDIST returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BINOMDIST(3,5,0.8,0) equals 0.2048.\n"
	   "\n"
	   "@SEEALSO=POISSON")
};

static GnmValue *
gnumeric_binomdist (FunctionEvalInfo *ei, GnmValue **argv)
{
	int n, trials, cuml;
	gboolean err;
	gnm_float p;

	n = value_get_as_int (argv[0]);
	trials = value_get_as_int (argv[1]);
	p = value_get_as_float (argv[2]);
	cuml = value_get_as_bool (argv[3], &err);

	if (n < 0 || trials < 0 || p < 0 || p > 1 || n > trials || err)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (pbinom (n, trials, p, TRUE, FALSE));
	else
		return value_new_float (dbinom (n, trials, p, FALSE));
}

/***************************************************************************/

static char const *help_cauchy = {
        N_("@FUNCTION=CAUCHY\n"
           "@SYNTAX=CAUCHY(x,a,cum)\n"

           "@DESCRIPTION="
           "CAUCHY returns the Cauchy distribution with scale parameter @a. "
	   "If @cum is TRUE, CAUCHY returns the cumulative distribution.\n"
           "\n"
           "* If @a < 0 CAUCHY returns #NUM! error.\n"
           "* If @cum != TRUE and @cum != FALSE CAUCHY returns #VALUE! error.\n"
	   "\n"
           "@EXAMPLES=\n"
           "CAUCHY(0.43,1,TRUE) returns 0.370735.\n"
           "\n"
           "@SEEALSO=RANDCAUCHY")
};

static GnmValue *
gnumeric_cauchy (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);
	int        cuml;
	gboolean   err;

	if (a < 0)
		return value_new_error_NUM (ei->pos);

	cuml = value_get_as_bool (argv[2], &err);
	if (err)
		return value_new_error_VALUE (ei->pos);

	if (cuml)
		return value_new_float (pcauchy (x, 0, a, FALSE, FALSE));
	else
		return value_new_float (dcauchy (x, 0, a, FALSE));
}

/***************************************************************************/

static char const *help_critbinom = {
        N_("@FUNCTION=CRITBINOM\n"
           "@SYNTAX=CRITBINOM(trials,p,alpha)\n"

           "@DESCRIPTION="
           "CRITBINOM function returns the smallest value for which the "
           "cumulative is greater than or equal to a given value. "
           "@n is the number of trials, @p is the probability of success in "
           "trials, and @alpha is the criterion value.\n"
           "\n"
           "* If @trials is a non-integer it is truncated.\n"
           "* If @trials < 0 CRITBINOM returns #NUM! error.\n"
           "* If @p < 0 or @p > 1 CRITBINOM returns #NUM! error.\n"
           "* If @alpha < 0 or @alpha > 1 CRITBINOM returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "CRITBINOM(10,0.5,0.75) equals 6.\n"
	   "\n"
           "@SEEALSO=BINOMDIST")
};

static GnmValue *
gnumeric_critbinom (FunctionEvalInfo *ei, GnmValue **argv)
{
        int trials;
        gnm_float p, alpha;

        trials = value_get_as_int (argv[0]);
        p = value_get_as_float (argv[1]);
        alpha = value_get_as_float (argv[2]);

        if (trials < 0 || p < 0 || p > 1 || alpha < 0 || alpha > 1)
	        return value_new_error_NUM (ei->pos);

	return value_new_float (qbinom (alpha, trials, p, TRUE, FALSE));
}

/***************************************************************************/

static char const *help_permut = {
	N_("@FUNCTION=PERMUT\n"
	   "@SYNTAX=PERMUT(n,k)\n"

	   "@DESCRIPTION="
	   "PERMUT function returns the number of permutations. "
           "@n is the number of objects, @k is the number of objects in each "
           "permutation.\n"
	   "\n"
	   "* If @n = 0 PERMUT returns #NUM! error.\n"
	   "* If @n < @k PERMUT returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "PERMUT(7,3) equals 210.\n"
	   "\n"
	   "@SEEALSO=COMBIN")
};

static GnmValue *
gnumeric_permut (FunctionEvalInfo *ei, GnmValue **argv)
{
	int n, k;

	n = value_get_as_int (argv[0]);
	k = value_get_as_int (argv[1]);

	if (0 <= k && k <= n)
		return value_new_float (permut (n, k));
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static char const *help_hypgeomdist = {
	N_("@FUNCTION=HYPGEOMDIST\n"
	   "@SYNTAX=HYPGEOMDIST(x,n,M,N[,cumulative])\n"

	   "@DESCRIPTION="
	   "HYPGEOMDIST function returns the hypergeometric distribution. "
	   "@x is the number of successes in the sample, @n is the number "
           "of trials, @M is the number of successes overall, and @N is the "
           "population size.\n"
	   "\n"
	   "If the optional argument @cumulative is TRUE, the cumulative "
	   "left tail will be returned.\n"
	   "\n"
	   "* If @x,@n,@M or @N is a non-integer it is truncated.\n"
	   "* If @x,@n,@M or @N < 0 HYPGEOMDIST returns #NUM! error.\n"
	   "* If @x > @M or @n > @N HYPGEOMDIST returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "HYPGEOMDIST(1,2,3,10) equals 0.4666667.\n"
	   "\n"
	   "@SEEALSO=BINOMDIST,POISSON")
};

static GnmValue *
gnumeric_hypgeomdist (FunctionEvalInfo *ei, GnmValue **argv)
{
	int x = value_get_as_int (argv[0]);
	int n = value_get_as_int (argv[1]);
	int M = value_get_as_int (argv[2]);
	int N = value_get_as_int (argv[3]);
	gboolean cum = argv[4] ? value_get_as_int (argv[4]) : FALSE;

	if (x < 0 || n < 0 || M < 0 || N < 0 || x > M || n > N)
		return value_new_error_NUM (ei->pos);

	if (cum)
		return value_new_float (phyper (x, M, N - M, n, TRUE, FALSE));
	else
		return value_new_float (dhyper (x, M, N - M, n, FALSE));
}

/***************************************************************************/

static char const *help_confidence = {
	N_("@FUNCTION=CONFIDENCE\n"
	   "@SYNTAX=CONFIDENCE(x,stddev,size)\n"

	   "@DESCRIPTION="
	   "CONFIDENCE function returns the confidence interval for a "
	   "mean. @x is the significance level, @stddev is the population "
	   "standard deviation, and @size is the size of the sample.\n"
	   "\n"
	   "* If @size is non-integer it is truncated.\n"
	   "* If @size < 0 CONFIDENCE returns #NUM! error.\n"
	   "* If @size is 0 CONFIDENCE returns #DIV/0! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CONFIDENCE(0.05,1,33) equals 0.341185936.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static GnmValue *
gnumeric_confidence (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x, stddev;
	int size;

	x = value_get_as_float (argv[0]);
	stddev = value_get_as_float (argv[1]);
	size = value_get_as_int (argv[2]);

	if (size == 0)
		return value_new_error_DIV0 (ei->pos);

	if (size < 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (-qnorm (x / 2, 0, 1, TRUE, FALSE) * (stddev / sqrtgnum (size)));
}

/***************************************************************************/

static char const *help_standardize = {
	N_("@FUNCTION=STANDARDIZE\n"
	   "@SYNTAX=STANDARDIZE(x,mean,stddev)\n"

	   "@DESCRIPTION="
	   "STANDARDIZE function returns a normalized value. "
	   "@x is the number to be normalized, @mean is the mean of the "
	   "distribution, @stddev is the standard deviation of the "
	   "distribution.\n"
	   "\n"
	   "* If @stddev is 0 STANDARDIZE returns #DIV/0! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "STANDARDIZE(3,2,4) equals 0.25.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static GnmValue *
gnumeric_standardize (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x, mean, stddev;

	x = value_get_as_float (argv[0]);
	mean = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);

	if (stddev == 0)
		return value_new_error_DIV0 (ei->pos);
	else if (stddev < 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float ((x - mean) / stddev);
}

/***************************************************************************/

static char const *help_weibull = {
        N_("@FUNCTION=WEIBULL\n"
           "@SYNTAX=WEIBULL(x,alpha,beta,cumulative)\n"

           "@DESCRIPTION="
           "WEIBULL function returns the Weibull distribution. "
           "If the @cumulative boolean is true it will return:\n\n\t"
           "1 - exp (-(@x/@beta)^@alpha),\n\notherwise it will return\n\n\t"
           "(@alpha/@beta^@alpha) * @x^(@alpha-1) * exp(-(@x/@beta^@alpha)).\n"
           "\n"
           "* If @x < 0 WEIBULL returns #NUM! error.\n"
           "* If @alpha <= 0 or @beta <= 0 WEIBULL returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "WEIBULL(3,2,4,0) equals 0.213668559.\n"
	   "\n"
           "@SEEALSO=POISSON")
};

static GnmValue *
gnumeric_weibull (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float x, alpha, beta;
        int cuml;
	gboolean err;

        x = value_get_as_float (argv[0]);
        alpha = value_get_as_float (argv[1]);
        beta = value_get_as_float (argv[2]);

        if (x < 0 || alpha <= 0 || beta <= 0)
                return value_new_error_NUM (ei->pos);

        cuml = value_get_as_bool (argv[3], &err);
        if (err)
                return value_new_error_VALUE (ei->pos);

        if (cuml)
                return value_new_float (pweibull (x, alpha, beta, TRUE, FALSE));
        else
		return value_new_float (dweibull (x, alpha, beta, FALSE));
}

/***************************************************************************/

static char const *help_normdist = {
        N_("@FUNCTION=NORMDIST\n"
           "@SYNTAX=NORMDIST(x,mean,stddev,cumulative)\n"

           "@DESCRIPTION="
           "NORMDIST function returns the normal cumulative distribution. "
	   "@x is the value for which you want the distribution, @mean is "
	   "the mean of the distribution, @stddev is the standard deviation.\n"
           "\n"
           "* If @stddev is 0 NORMDIST returns #DIV/0! error.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "NORMDIST(2,1,2,0) equals 0.176032663.\n"
	   "\n"
           "@SEEALSO=POISSON")
};


static GnmValue *
gnumeric_normdist (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float x, mean, stddev;
        int cuml;
	gboolean err;

        x = value_get_as_float (argv[0]);
        mean = value_get_as_float (argv[1]);
        stddev = value_get_as_float (argv[2]);

        if (stddev <= 0)
                return value_new_error_DIV0 (ei->pos);

        cuml = value_get_as_bool (argv[3], &err);
        if (err)
                return value_new_error_VALUE (ei->pos);

        if (cuml)
		return value_new_float (pnorm (x, mean, stddev, TRUE, FALSE));
        else
		return value_new_float (dnorm (x, mean, stddev, FALSE));
}

/***************************************************************************/

static char const *help_norminv = {
        N_("@FUNCTION=NORMINV\n"
           "@SYNTAX=NORMINV(p,mean,stddev)\n"

           "@DESCRIPTION="
           "NORMINV function returns the inverse of the normal "
	   "cumulative distribution. @p is the given probability "
	   "corresponding to the normal distribution, @mean is the "
	   "arithmetic mean of the distribution, and @stddev is the "
	   "standard deviation of the distribution.\n"
           "\n"
	   "* If @p < 0 or @p > 1 or @stddev <= 0 NORMINV returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "NORMINV(0.76,2,3) equals 4.118907689.\n"
	   "\n"
           "@SEEALSO=NORMDIST,NORMSDIST,NORMSINV,STANDARDIZE,ZTEST")
};

static GnmValue *
gnumeric_norminv (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float p, mean, stddev;

        p = value_get_as_float (argv[0]);
	mean = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);

	if (p < 0 || p > 1 || stddev <= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (qnorm (p, mean, stddev, TRUE, FALSE));
}


/***************************************************************************/

static char const *help_kurt = {
        N_("@FUNCTION=KURT\n"
           "@SYNTAX=KURT(n1, n2, ...)\n"

           "@DESCRIPTION="
           "KURT returns an unbiased estimate of the kurtosis of a data set."
           "\n"
	   "Note, that this is only meaningful if the underlying "
	   "distribution really has a fourth moment.  The kurtosis is "
	   "offset by three such that a normal distribution will have zero "
	   "kurtosis.\n"
           "\n"
           "* Strings and empty cells are simply ignored.\n"
           "* If fewer than four numbers are given or all of them are equal "
           "KURT returns #DIV/0! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "KURT(A1:A5) equals 1.234546305.\n"
	   "\n"
           "@SEEALSO=AVERAGE,VAR,SKEW,KURTP")
};

static GnmValue *
gnumeric_kurt (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_kurtosis_m3_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_kurtp = {
        N_("@FUNCTION=KURTP\n"
           "@SYNTAX=KURTP(n1, n2, ...)\n"

           "@DESCRIPTION="
           "KURTP returns the population kurtosis of a data set.\n"
           "\n"
           "* Strings and empty cells are simply ignored.\n"
           "* If fewer than two numbers are given or all of them are equal "
           "KURTP returns #DIV/0! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "KURTP(A1:A5) equals -0.691363424.\n"
	   "\n"
           "@SEEALSO=AVERAGE,VARP,SKEWP,KURT")
};

static GnmValue *
gnumeric_kurtp (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_kurtosis_m3_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_avedev = {
        N_("@FUNCTION=AVEDEV\n"
           "@SYNTAX=AVEDEV(n1, n2, ...)\n"

           "@DESCRIPTION="
           "AVEDEV returns the average of the absolute deviations of a data "
	   "set from their mean.\n\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "AVEDEV(A1:A5) equals 7.84.\n"
	   "\n"
           "@SEEALSO=STDEV")
};

static GnmValue *
gnumeric_avedev (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_avedev,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static char const *help_devsq = {
        N_("@FUNCTION=DEVSQ\n"
           "@SYNTAX=DEVSQ(n1, n2, ...)\n"

           "@DESCRIPTION="
           "DEVSQ returns the sum of squares of deviations of a data set from "
           "the sample mean.\n"
           "\n"
           "* Strings and empty cells are simply ignored.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "DEVSQ(A1:A5) equals 470.56.\n"
	   "\n"
           "@SEEALSO=STDEV")
};

static GnmValue *
gnumeric_devsq (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_devsq,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_fisher = {
        N_("@FUNCTION=FISHER\n"
           "@SYNTAX=FISHER(x)\n"

           "@DESCRIPTION="
           "FISHER function returns the Fisher transformation at @x.\n"
           "\n"
           "* If @x is not a number, FISHER returns #VALUE! error.\n"
           "* If @x <= -1 or @x >= 1, FISHER returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "FISHER(0.332) equals 0.345074339.\n"
	   "\n"
           "@SEEALSO=SKEW")
};

static GnmValue *
gnumeric_fisher (FunctionEvalInfo *ei, GnmValue **argv)
{
        gnm_float x;

        if (!VALUE_IS_NUMBER (argv[0]))
                return value_new_error_VALUE (ei->pos);

        x = value_get_as_float (argv[0]);

        if (x <= -1.0 || x >= 1.0)
                return value_new_error_NUM (ei->pos);

        return value_new_float (0.5 * (log1pgnum (x) - log1pgnum (-x)));
}

/***************************************************************************/

static char const *help_poisson = {
	N_("@FUNCTION=POISSON\n"
	   "@SYNTAX=POISSON(x,mean,cumulative)\n"

	   "@DESCRIPTION="
	   "POISSON function returns the Poisson distribution. "
	   "@x is the number of events, @mean is the expected numeric value "
	   "@cumulative describes whether to return the sum of the "
	   "Poisson function from 0 to @x.\n"
	   "\n"
	   "* If @x is a non-integer it is truncated.\n"
	   "* If @x < 0 POISSON returns #NUM! error.\n"
	   "* If @mean <= 0 POISSON returns the #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "POISSON(3,6,0) equals 0.089235078.\n"
	   "\n"
	   "@SEEALSO=NORMDIST, WEIBULL")
};

static GnmValue *
gnumeric_poisson (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float mean;
	int x, cuml;
	gboolean err;

	x = value_get_as_int (argv[0]);
	mean = value_get_as_float (argv[1]);
	cuml = value_get_as_bool (argv[2], &err);

	if (x < 0 || mean <= 0 || err)
		return value_new_error_NUM (ei->pos);

	if (cuml)
		return value_new_float (ppois (x, mean, TRUE, FALSE));
	else
		return value_new_float (dpois (x, mean, FALSE));
}

/***************************************************************************/

static char const *help_pearson = {
	N_("@FUNCTION=PEARSON\n"
	   "@SYNTAX=PEARSON(array1,array2)\n"

	   "@DESCRIPTION="
	   "PEARSON returns the Pearson correlation coefficient of two data "
	   "sets.\n"
	   "\n"
	   "* Strings and empty cells are simply ignored.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=INTERCEPT,LINEST,RSQ,SLOPE,STEYX")
};

static GnmValue *
gnumeric_pearson (FunctionEvalInfo *ei, GnmValue **argv)
{
	return gnumeric_correl (ei, argv);
}


/***************************************************************************/

static char const *help_rsq = {
	N_("@FUNCTION=RSQ\n"
	   "@SYNTAX=RSQ(array1,array2)\n"

	   "@DESCRIPTION="
	   "RSQ returns the square of the Pearson correlation coefficient "
	   "of two data sets.\n"
	   "\n"
	   "* Strings and empty cells are simply ignored.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=CORREL,COVAR,INTERCEPT,LINEST,LOGEST,PEARSON,SLOPE,"
	   "STEYX,TREND")
};

static GnmValue *
gnumeric_rsq (FunctionEvalInfo *ei, GnmValue **argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      range_rsq_pop,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_median = {
        N_("@FUNCTION=MEDIAN\n"
           "@SYNTAX=MEDIAN(n1, n2, ...)\n"

           "@DESCRIPTION="
           "MEDIAN returns the median of the given data set.\n"
           "\n"
           "* Strings and empty cells are simply ignored.\n"
	   "* If even numbers are given MEDIAN returns the average of the two "
	   "numbers in the middle.\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "MEDIAN(A1:A5) equals 21.3.\n"
	   "\n"
           "@SEEALSO=AVERAGE,COUNT,COUNTA,DAVERAGE,MODE,SSMEDIAN,SUM")
};

static GnmValue *
gnumeric_median (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     (float_range_function_t)range_median_inter_nonconst,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static char const *help_ssmedian = {
	N_("@FUNCTION=SSMEDIAN\n"
	   "@SYNTAX=SSMEDIAN(array[,interval)]\n"

	   "@DESCRIPTION="
	   "The SSMEDIAN function returns the median "
	   "for grouped data as commonly determined in "
	   "the social sciences. "
	   "The data points given in @array are assumed to be the result of "
	   "grouping data "
	   "into intervals of length @interval\n"
	   "\n"
	   "* If @interval is not given, SSMEDIAN uses 1.\n"
	   "* If @array is empty, SSMEDIAN returns #NUM! error.\n"
	   "* If @interval <= 0, SSMEDIAN returns #NUM! error.\n"
	   "* SSMEDIAN does not check whether the data points are "
	   "at least @interval apart.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, A3 contain numbers "
	   "7, 8, 8.  Then\n"
	   "SSMEDIAN(A1:A3, 1) equals 7.75.\n"
	   "\n"
	   "@SEEALSO=MEDIAN")
};

static gnm_float
gnumeric_ssmedian_calc (gnm_float *sorted_data, int len, gnm_float mid_val, 
			 gnm_float interval)
{
	gnm_float L_lower = mid_val - interval / 2;
	gnm_float L_upper = mid_val + interval / 2;
	int f_below = 0;
	int f_within = 0;
	int i;

	for (i=0; i<len; i++) {
		if (*sorted_data < L_lower)
			f_below++;
		else if (*sorted_data <= L_upper)
			f_within++;
		else
			break;
		sorted_data++;
	}

	return L_lower + (len / 2e0 - f_below) * interval / f_within;
}

static GnmValue *
gnumeric_ssmedian (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     &n, &result);
	if (!result) {
		gnm_float interval = argv[1] ? 
			value_get_as_float (argv[1]) : 1.0;

		if (interval <= 0 || n == 0)
			result = value_new_error_NUM (ei->pos);
		else {
			switch (n) {
			case (1):
				result = value_new_float (*data);
				break;
			case (2):
				result = value_new_float 
					((*data + *(data+1))/2);
				break;
			default:
				qsort (data, n, sizeof (data[0]), (int (*) (const void *, const void *))&float_compare);
				if ((n%2) == 0) {
					result = (*(data + n/2) == *(data + n/2 - 1)) ?
						value_new_float 
						(gnumeric_ssmedian_calc 
						 (data, n, *(data + n/2), 
						  interval)) :
						value_new_float 
						((*(data + n/2) + *(data + n/2 - 1))
						 /2);
				} else {
					result = value_new_float 
						(gnumeric_ssmedian_calc 
						 (data, n, *(data + n/2), 
						  interval));
				}
			}
		}
	}

	g_free (data);
	return result;
}

/***************************************************************************/

static char const *help_large = {
	N_("@FUNCTION=LARGE\n"
	   "@SYNTAX=LARGE(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "LARGE returns the k-th largest value in a data set.\n"
	   "\n"
	   "* If data set is empty LARGE returns #NUM! error.\n"
	   "* If @k <= 0 or @k is greater than the number of data items given "
	   "LARGE returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "LARGE(A1:A5,2) equals 25.9.\n"
	   "LARGE(A1:A5,4) equals 17.3.\n"
	   "\n"
	   "@SEEALSO=PERCENTILE,PERCENTRANK,QUARTILE,SMALL")
};

static int
range_large (gnm_float *xs, int n, gnm_float *res)
{
	int k;

	if (n < 2)
		return 1;

	k = (int)xs[--n];
	return range_min_k_nonconst (xs, n, res, n - k);
}

static GnmValue *
gnumeric_large (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     (float_range_function_t)range_large,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static char const *help_small = {
	N_("@FUNCTION=SMALL\n"
	   "@SYNTAX=SMALL(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "SMALL returns the k-th smallest value in a data set.\n"
	   "\n"
	   "* If data set is empty SMALL returns #NUM! error.\n"
	   "* If @k <= 0 or @k is greater than the number of data items given "
	   "SMALL returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "SMALL(A1:A5,2) equals 17.3.\n"
	   "SMALL(A1:A5,4) equals 25.9.\n"
	   "\n"
	   "@SEEALSO=PERCENTILE,PERCENTRANK,QUARTILE,LARGE")
};

static int
range_small (gnm_float *xs, int n, gnm_float *res)
{
	int k;

	if (n < 2)
		return 1;

	k = (int)xs[--n];
	return range_min_k_nonconst (xs, n, res, k - 1);
}

static GnmValue *
gnumeric_small (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     (float_range_function_t)range_small,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

typedef struct {
        GSList *list;
        int    num;
} stat_list_t;

static GnmValue *
callback_function_list (Sheet *sheet, int col, int row,
			GnmCell *cell, void *user_data)
{
        stat_list_t *mm = user_data;
	gnm_float *p;

	cell_eval (cell);
	if (cell->value == NULL || !VALUE_IS_NUMBER (cell->value))
		return NULL;

	p = g_new (gnm_float, 1);
	*p = value_get_as_float (cell->value);
	mm->list = g_slist_append (mm->list, p);
	mm->num++;
	return NULL;
}

/***************************************************************************/

static char const *help_prob = {
	N_("@FUNCTION=PROB\n"
	   "@SYNTAX=PROB(x_range,prob_range,lower_limit[,upper_limit])\n"

	   "@DESCRIPTION="
	   "PROB function returns the probability that values in a range or "
	   "an array are between two limits. If @upper_limit is not "
	   "given, PROB returns the probability that values in @x_range "
	   "are equal to @lower_limit.\n"
	   "\n"
	   "* If the sum of the probabilities in @prob_range is not equal to 1 "
	   "PROB returns #NUM! error.\n"
	   "* If any value in @prob_range is <=0 or > 1, PROB returns #NUM! "
	   "error.\n"
	   "* If @x_range and @prob_range contain a different number of data "
	   "entries, PROB returns #N/A error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=BINOMDIST,CRITBINOM")
};

static GnmValue *
gnumeric_prob (FunctionEvalInfo *ei, GnmValue **argv)
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

	if (gnumabs (total_sum - 1) > x_n * 2 * GNUM_EPSILON) {
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

static char const *help_steyx = {
	N_("@FUNCTION=STEYX\n"
	   "@SYNTAX=STEYX(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "STEYX function returns the standard error of the predicted "
	   "y-value for each x in the regression.\n"
	   "\n"
	   "* If @known_y's and @known_x's are empty or have a different "
	   "number of arguments then STEYX returns #N/A error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "STEYX(A1:A5,B1:B5) equals 1.101509979.\n"
	   "\n"
	   "@SEEALSO=PEARSON,RSQ,SLOPE")
};

static GnmValue *
gnumeric_steyx (FunctionEvalInfo *ei, GnmValue **argv)
{
        GnmValue       *known_y = argv[0];
        GnmValue       *known_x = argv[1];
	stat_list_t items_x, items_y;
	gnm_float  sum_x, sum_y, sum_xy, sqrsum_x, sqrsum_y;
	gnm_float  num, den, k, n;
	GSList      *list1, *list2;
	GnmValue       *ret;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (known_x->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (known_x->v_range.cell.a.sheet, ei->pos->sheet),
			CELL_ITER_IGNORE_BLANK,
			known_x->v_range.cell.a.col,
			known_x->v_range.cell.a.row,
			known_x->v_range.cell.b.col,
			known_x->v_range.cell.b.row,
			callback_function_list,
			&items_x);
		if (ret != NULL) {
			list1 = items_x.list;
			list2 = items_y.list;
			while (list1 != NULL) {
			        g_free (list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free (list2->data);
				list2 = list2->next;
			}
			g_slist_free (items_x.list);
			g_slist_free (items_y.list);

		        return value_new_error_VALUE (ei->pos);
		}
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

        if (known_y->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (known_y->v_range.cell.a.sheet, ei->pos->sheet),
			CELL_ITER_IGNORE_BLANK,
			known_y->v_range.cell.a.col,
			known_y->v_range.cell.a.row,
			known_y->v_range.cell.b.col,
			known_y->v_range.cell.b.row,
			callback_function_list,
			&items_y);
		if (ret != NULL) {
			list1 = items_x.list;
			list2 = items_y.list;
			while (list1 != NULL) {
			        g_free (list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free (list2->data);
				list2 = list2->next;
			}
			g_slist_free (items_x.list);
			g_slist_free (items_y.list);

		        return value_new_error_VALUE (ei->pos);
		}
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

	if (items_x.num != items_y.num) {
		list1 = items_x.list;
		list2 = items_y.list;
		while (list1 != NULL) {
		        g_free (list1->data);
			list1 = list1->next;
		}
		while (list2 != NULL) {
		        g_free (list2->data);
			list2 = list2->next;
		}
		g_slist_free (items_x.list);
		g_slist_free (items_y.list);

	        return value_new_error_NA (ei->pos);
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum_x = sum_y = 0;
	sqrsum_x = sqrsum_y = 0;
	sum_xy = 0;

	while (list1 != NULL) {
	        gnm_float  x, y;

		x = *((gnm_float *) list1->data);
		y = *((gnm_float *) list2->data);

		sum_x += x;
		sum_y += y;
		sqrsum_x += x * x;
		sqrsum_y += y * y;
		sum_xy += x * y;

		g_free (list1->data);
		g_free (list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free (items_x.list);
	g_slist_free (items_y.list);

	n = items_x.num;
	k = 1.0 / (n * (n - 2));
	num = n * sum_xy - sum_x * sum_y;
	num *= num;
	den = n * sqrsum_x - sum_x * sum_x;

	if (den == 0)
	        return value_new_error_NUM (ei->pos);

	return value_new_float (sqrtgnum (k * (n * sqrsum_y - sum_y * sum_y - num / den)));
}

/***************************************************************************/

static char const *help_ztest = {
	N_("@FUNCTION=ZTEST\n"
	   "@SYNTAX=ZTEST(ref,x)\n"

	   "@DESCRIPTION="
	   "ZTEST returns the two-tailed probability of a z-test.\n"
	   "\n"
	   "@ref is the data set and @x is the value to be tested.\n"
	   "\n"
	   "* If @ref contains less than two data items ZTEST "
	   "returns #DIV/0! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "ZTEST(A1:A5,20) equals 0.254717826.\n"
	   "\n"
	   "@SEEALSO=CONFIDENCE,NORMDIST,NORMINV,NORMSDIST,NORMSINV,"
	   "STANDARDIZE")
};

static int
range_ztest (const gnm_float *xs, int n, gnm_float *res)
{
	gnm_float x, s, m;

	if (n < 3)
		return 1;

	x = xs[--n];
	if (range_average (xs, n, &m))
		return 1;
	if (range_stddev_est (xs, n, &s) || s == 0)
	        return 1;

	*res = pnorm (x, m, s / sqrtgnum (n), TRUE, FALSE);
	return 0;
}

static GnmValue *
gnumeric_ztest (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_ztest,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_averagea = {
	N_("@FUNCTION=AVERAGEA\n"
	   "@SYNTAX=AVERAGEA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "AVERAGEA returns the average of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "AVERAGEA(A1:A5) equals 18.94.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static GnmValue *
gnumeric_averagea (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_average,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static char const *help_maxa = {
	N_("@FUNCTION=MAXA\n"
	   "@SYNTAX=MAXA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "MAXA returns the largest value of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "MAXA(A1:A5) equals 40.1.\n"
	   "\n"
	   "@SEEALSO=MAX,MINA")
};

static GnmValue *
gnumeric_maxa (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_max,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_mina = {
	N_("@FUNCTION=MINA\n"
	   "@SYNTAX=MINA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "MINA returns the smallest value of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "MINA(A1:A5) equals 0.\n"
	   "\n"
	   "@SEEALSO=MIN,MAXA")
};

static GnmValue *
gnumeric_mina (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_min,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_vara = {
	N_("@FUNCTION=VARA\n"
	   "@SYNTAX=VARA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "VARA calculates sample variance of the given sample.\n"
	   "To get the true variance of a complete population use VARPA.\n"
	   "VARA is also known as the N-1-variance.\n"
	   "Under reasonable "
	   "conditions, it is the maximum-likelihood estimator for the "
	   "true variance.\n"
	   "Numbers, text "
	   "and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates "
	   "to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "VARA(A1:A5) equals 228.613.\n"
	   "\n"
	   "@SEEALSO=VAR,VARPA")
};

static GnmValue *
gnumeric_vara (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_var_est,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_varpa = {
	N_("@FUNCTION=VARPA\n"
	   "@SYNTAX=VARPA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "VARPA calculates the variance of an entire population.\n"
	   "VARPA is also known as the N-variance.\n"
	   "Numbers, text and logical values are included in the "
	   "calculation too.  If the cell contains text or the argument "
	   "evaluates to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "VARPA(A1:A5) equals 182.8904.\n"
	   "\n"
	   "@SEEALSO=VARA,VARP")
};

static GnmValue *
gnumeric_varpa (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_var_pop,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_stdeva = {
	N_("@FUNCTION=STDEVA\n"
	   "@SYNTAX=STDEVA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "STDEVA returns the sample standard deviation of the given "
	   "sample.\n"
	   "To obtain the population standard deviation of a whole population "
	   "use STDEVPA.\n"
	   "STDEVA is also known as the N-1-standard deviation.\n"
	   "Under reasonable "
	   "conditions, it is the maximum-likelihood estimator for the "
	   "true population standard deviation.\n"
	   "Numbers, text and logical values are included in the calculation "
	   "too.  If the cell contains text or the argument evaluates to "
	   "FALSE, it is counted as value zero (0).  If the argument "
	   "evaluates to TRUE, it is counted as one (1).  Note that empty "
	   "cells are not counted.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "STDEVA(A1:A5) equals 15.119953704.\n"
	   "\n"
	   "@SEEALSO=STDEV,STDEVPA")
};

static GnmValue *
gnumeric_stdeva (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_stddev_est,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_stdevpa = {
	N_("@FUNCTION=STDEVPA\n"
	   "@SYNTAX=STDEVPA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "STDEVPA returns the population standard deviation of an entire "
	   "population.\n"
	   "This is also known as the N-standard deviation\n"
	   "Numbers, text and logical values are included in "
	   "the calculation too.  If the cell contains text or the argument "
	   "evaluates to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "STDEVPA(A1:A5) equals 13.523697719.\n"
	   "\n"
	   "@SEEALSO=STDEVA,STDEVP")
};

static GnmValue *
gnumeric_stdevpa (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_stddev_pop,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_percentrank = {
	N_("@FUNCTION=PERCENTRANK\n"
	   "@SYNTAX=PERCENTRANK(array,x[,significance])\n"

	   "@DESCRIPTION="
	   "PERCENTRANK function returns the rank of a data point in a data "
	   "set.  @array is the range of numeric values, @x is the data "
	   "point which you want to rank, and the optional @significance "
	   "specifies the number of significant digits for the returned "
	   "value, truncating the remainder.  If @significance is omitted, "
	   "PERCENTRANK uses three digits.\n"
	   "\n"
	   "* If @array contains no data points, PERCENTRANK returns #NUM! "
	   "error.\n"
	   "* If @significance is less than one, PERCENTRANK returns #NUM! "
	   "error.\n"
	   "* If @x exceeds the largest value or is less than the smallest "
	   "value in @array, PERCENTRANK returns #NUM! error.\n"
	   "* If @x does not match any of the values in @array or @x matches "
	   "more than once, PERCENTRANK interpolates the returned value.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LARGE,MAX,MEDIAN,MIN,PERCENTILE,QUARTILE,SMALL")
};

typedef struct {
        gnm_float x;
        gnm_float smaller_x;
        gnm_float greater_x;
        int     smaller;
        int     greater;
        int     equal;
} stat_percentrank_t;

static GnmValue *
callback_function_percentrank (GnmEvalPos const *ep, GnmValue *value,
			       void *user_data)
{
        stat_percentrank_t *p = user_data;
	gnm_float y;

	if (!VALUE_IS_NUMBER (value))
		return VALUE_TERMINATE;

	y = value_get_as_float (value);

	if (y < p->x) {
	        p->smaller++;
		if (p->smaller_x == p->x || p->smaller_x < y)
		        p->smaller_x = y;
	} else if (y > p->x) {
	        p->greater++;
		if (p->greater_x == p->x || p->greater_x > y)
		        p->greater_x = y;
	} else
	        p->equal++;

	return NULL;
}

static GnmValue *
gnumeric_percentrank (FunctionEvalInfo *ei, GnmValue **argv)
{
        stat_percentrank_t p;
        gnm_float         x, k, pr;
        int                significance;
	GnmValue		   *ret;

	x = value_get_as_float (argv[1]);

	p.smaller = 0;
	p.greater = 0;
	p.equal = 0;
	p.smaller_x = x;
	p.greater_x = x;
	p.x = x;

        if (argv[2] == NULL)
	        significance = 3;
	else {
	        significance = value_get_as_int (argv[2]);
		if (significance < 1)
		        return value_new_error_NUM (ei->pos);
	}

	ret = function_iterate_do_value (ei->pos, (FunctionIterateCB)
					 callback_function_percentrank,
					 &p, argv[0],
					 TRUE, CELL_ITER_IGNORE_BLANK);

	if (ret != NULL || (p.smaller + p.equal == 0) ||
		(p.greater + p.equal == 0))
	        return value_new_error_NUM (ei->pos);

	if (p.equal == 1)
	        pr = (gnm_float)p.smaller / (p.smaller + p.greater);
	else if (p.equal == 0) {
	        gnm_float a = (x - p.smaller_x) / (p.greater_x - p.smaller_x);
	        pr = (gnm_float)(p.smaller + a - 1) / (p.greater + p.smaller - 1.0);
	} else
	        pr = (p.smaller + 0.5 * p.equal) /
		  (p.smaller + p.equal + p.greater);

	k = gpow10 (significance);
	return value_new_float (gnumeric_fake_trunc (pr * k) / k);
}

/***************************************************************************/

static char const *help_percentile = {
	N_("@FUNCTION=PERCENTILE\n"
	   "@SYNTAX=PERCENTILE(array,k)\n"

	   "@DESCRIPTION="
	   "PERCENTILE function returns the 100*@k-th percentile "
	   "of the given data points (that is, a number x such "
	   "that a fraction @k of the data points are less than x).\n"
	   "\n"
	   "* If @array is empty, PERCENTILE returns #NUM! error.\n"
	   "* If @k < 0 or @k > 1, PERCENTILE returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "PERCENTILE(A1:A5,0.42) equals 20.02.\n"
	   "\n"
	   "@SEEALSO=QUARTILE")
};

static GnmValue *
gnumeric_percentile (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     &n, &result);
	if (!result) {
		gnm_float p = value_get_as_float (argv[1]);
		gnm_float res;

		if (range_fractile_inter_nonconst (data, n, &res, p))
			result = value_new_error_NUM (ei->pos);
		else
			result = value_new_float (res);
	}

	g_free (data);
	return result;
}

/***************************************************************************/

static char const *help_quartile = {
	N_("@FUNCTION=QUARTILE\n"
	   "@SYNTAX=QUARTILE(array,quart)\n"

	   "@DESCRIPTION="
	   "QUARTILE function returns the quartile of the given data "
	   "points.\n"
	   "\n"
	   "If @quart is equal to: QUARTILE returns:\n"
	   "0                      the smallest value of @array.\n"
	   "1                      the first quartile\n"
	   "2                      the second quartile\n"
	   "3                      the third quartile\n"
	   "4                      the largest value of @array.\n"
	   "\n"
	   "* If @array is empty, QUARTILE returns #NUM! error.\n"
	   "* If @quart < 0 or @quart > 4, QUARTILE returns #NUM! error.\n"
	   "* If @quart is not an integer, it is truncated.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "QUARTILE(A1:A5,1) equals 17.3.\n"
	   "\n"
	   "@SEEALSO=LARGE,MAX,MEDIAN,MIN,PERCENTILE,SMALL")
};

static GnmValue *
gnumeric_quartile (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float *data;
	GnmValue *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     &n, &result);
	if (!result) {
		int q = value_get_as_int (argv[1]);
		gnm_float res;

		if (range_fractile_inter_nonconst (data, n, &res, q / 4.0))
			result = value_new_error_NUM (ei->pos);
		else
			result = value_new_float (res);
	}

	g_free (data);
	return result;
}

/***************************************************************************/

static char const *help_ftest = {
	N_("@FUNCTION=FTEST\n"
	   "@SYNTAX=FTEST(array1,array2)\n"

	   "@DESCRIPTION="
	   "FTEST function returns the two-tailed probability that the "
	   "variances in the given two data sets are not significantly "
	   "different.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "FTEST(A1:A5,B1:B5) equals 0.510815017.\n"
	   "\n"
	   "@SEEALSO=FDIST,FINV")
};

static GnmValue *
gnumeric_ftest (FunctionEvalInfo *ei, GnmValue *argv[])
{
	stat_closure_t cl;
	gnm_float     var1, var2, p;
	int            dof1, dof2;
	GnmValue         *err;

	if (NULL != (err = stat_helper (&cl, ei->pos, argv [0])))
		return err;
	dof1 = cl.N - 1;
	var1 = cl.Q / (cl.N - 1);

	if (NULL != (err = stat_helper (&cl, ei->pos, argv [1])))
		return err;
	dof2 = cl.N - 1;
	var2 = cl.Q / (cl.N - 1);

	if (var2 == 0)
	        return value_new_error_VALUE (ei->pos);

	p = pf (var1 / var2, dof1, dof2, FALSE, FALSE);
	if (p > 0.5) {
		/*
		 * We need the other tail and 1-p might not be very
		 * accurate.
		 */
		p = pf (var1 / var2, dof1, dof2, TRUE, FALSE);
	}

	return value_new_float (2 * p);
}

/***************************************************************************/

static char const *help_ttest = {
	N_("@FUNCTION=TTEST\n"
	   "@SYNTAX=TTEST(array1,array2,tails,type)\n"

	   "@DESCRIPTION="
	   "TTEST function returns the probability of a Student's t-Test. "
	   "\n"
	   "@array1 is the first data set and @array2 is the second data "
	   "set.  If @tails is one, TTEST uses the one-tailed distribution "
	   "and if @tails is two, TTEST uses the two-tailed distribution.  "
	   "@type determines the kind of the test:\n\n"
	   "\t1  Paired test\n"
	   "\t2  Two-sample equal variance\n"
	   "\t3  Two-sample unequal variance\n"
	   "\n"
	   "* If the data sets contain a different number of data points and "
	   "the test is paired (@type one), TTEST returns the #N/A error.\n"
	   "* @tails and @type are truncated to integers.\n"
	   "* If @tails is not one or two, TTEST returns #NUM! error.\n"
	   "* If @type is any other than one, two, or three, TTEST returns "
	   "#NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "TTEST(A1:A5,B1:B5,1,1) equals 0.003127619.\n"
	   "TTEST(A1:A5,B1:B5,2,1) equals 0.006255239.\n"
	   "TTEST(A1:A5,B1:B5,1,2) equals 0.111804322.\n"
	   "TTEST(A1:A5,B1:B5,1,3) equals 0.113821797.\n"
	   "\n"
	   "@SEEALSO=FDIST,FINV")
};

typedef struct {
        GSList   *entries;
        GSList   *current;
        gboolean first;
} stat_ttest_t;

static GnmValue *
callback_function_ttest (GnmEvalPos const *ep, GnmValue *value, void *closure)
{
	stat_ttest_t *mm = closure;
	gnm_float   x;

	if (value != NULL && VALUE_IS_NUMBER (value))
		x = value_get_as_float (value);
	else
	        x = 0;

	if (mm->first) {
	        gpointer p = g_new (gnm_float, 1);
		*((gnm_float *) p) = x;
		mm->entries = g_slist_append (mm->entries, p);
	} else {
	        if (mm->current == NULL)
			return VALUE_TERMINATE;

	        *((gnm_float *) mm->current->data) -= x;
		mm->current = mm->current->next;
	}

	return NULL;
}

static GnmValue *
gnumeric_ttest (FunctionEvalInfo *ei, GnmValue *argv[])
{
	stat_closure_t cl;
	stat_ttest_t   t_cl;
	GnmExprList   *expr_node_list;
        int            tails, type;
	gnm_float     mean1, mean2, x;
	gnm_float     s, var1, var2, dof;
	int            n1, n2;
	GnmValue         *err;

	tails = value_get_as_int (argv[2]);
	type = value_get_as_int (argv[3]);

	if ((tails != 1 && tails != 2) ||
	    (type < 1 || type > 3))
		return value_new_error_NUM (ei->pos);

	if (type == 1) {
	        GSList  *current;
		GnmExprConstant expr;
	        gnm_float sum, dx, dm, M, Q, N;

	        t_cl.first = TRUE;
		t_cl.entries = NULL;

		gnm_expr_constant_init (&expr, argv[0]);
		expr_node_list = gnm_expr_list_append (NULL, &expr);
		err = function_iterate_argument_values (ei->pos,
			 &callback_function_ttest, &t_cl, expr_node_list,
			 TRUE, CELL_ITER_ALL);
		gnm_expr_list_free (expr_node_list);
		if (err != NULL)
		        return err;

	        t_cl.first = FALSE;
		t_cl.current = t_cl.entries;

		gnm_expr_constant_init (&expr, argv[1]);
		expr_node_list = gnm_expr_list_append (NULL, &expr);
		err = function_iterate_argument_values (ei->pos,
			 &callback_function_ttest, &t_cl, expr_node_list,
			 TRUE, CELL_ITER_ALL);
		gnm_expr_list_free (expr_node_list);
		if (err != NULL)
		        return err;

		current = t_cl.entries;
		dx = dm = M = Q = N = sum = 0;

		while (current != NULL) {
		        x = *((gnm_float *) current->data);

			dx = x - M;
			dm = dx / (N + 1);
			M += dm;
			Q += N * dx * dm;
			N++;
			sum += x;

			g_free (current->data);
			current = current->next;
		}
		g_slist_free (t_cl.entries);

		if (N - 1 == 0 || N == 0)
		        return value_new_error_NUM (ei->pos);

		s = sqrtgnum (Q / (N - 1));
		mean1 = sum / N;
		x = mean1 / (s / sqrtgnum (N));
		dof = N - 1;
	} else {
	        if ((err = stat_helper (&cl, ei->pos, argv [0])))
			return err;
		var1 = cl.Q / (cl.N - 1);
		mean1 = cl.sum / cl.N;
		n1 = cl.N;

	        if ((err = stat_helper (&cl, ei->pos, argv [1])))
			return err;
		var2 = cl.Q / (cl.N - 1);
		mean2 = cl.sum / cl.N;
		n2 = cl.N;

		if (type != 2) {
		        gnm_float c = (var1 / n1) / (var1 / n1 + var2 / n2);
			dof = 1.0 / ((c * c) / (n1 - 1) + ((1 - c) * (1 - c)) /
				     (n2 - 1));
		} else
		        dof = n1 + n2 - 2;

		x = (mean1 - mean2) / sqrtgnum (var1 / n1 + var2 / n2);
	}

	return value_new_float (tails * pt (gnumabs (x), dof, FALSE, FALSE));
}

/***************************************************************************/

static char const *help_frequency = {
	N_("@FUNCTION=FREQUENCY\n"
	   "@SYNTAX=FREQUENCY(data_array,bins_array)\n"

	   "@DESCRIPTION="
	   "FREQUENCY function counts how often given values occur within a "
	   "range of values.  The results are given as an array.\n"
	   "\n"
	   "@data_array is a data array for which you want to count the "
	   "frequencies.  @bin_array is an array containing the intervals "
	   "into which you want to group the values in data_array.  If the "
	   "@bin_array is empty, FREQUENCY returns the number of data points "
	   "in @data_array.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};


static GnmValue *
gnumeric_frequency (FunctionEvalInfo *ei, GnmValue *argv[])
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

	bins = collect_floats_value (argv[1], ei->pos, flags,
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

	qsort (bins, nbins, sizeof (gnm_float), (void *) &float_compare);
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

static char const *help_linest = {
	N_("@FUNCTION=LINEST\n"
	   "@SYNTAX=LINEST(known_y's[,known_x's[,const[,stat]]])\n"

	   "@DESCRIPTION="
	   "LINEST function calculates the ``least squares'' line that best "
	   "fit to your data in @known_y's.  @known_x's contains the "
	   "corresponding x's where y=mx+b.\n"
	   "\n"
           "LINEST returns an array having two columns and one row.  The "
           "slope (m) of the regression line y=mx+b is given in the first "
           "column and the y-intercept (b) in the second.\n"
	   "\n"
	   "If @stat is TRUE, extra statistical information will be returned. "
	   "Extra statistical information is written bellow the regression "
	   "line coefficients in the result array.  Extra statistical "
	   "information consists of four rows of data.  In the first row "
	   "the standard error values for the coefficients m1, (m2, ...), b "
	   "are represented.  The second row contains the square of R and "
	   "the standard error for the y estimate.  The third row contains "
	   "the F-observed value and the degrees of freedom.  The last row "
	   "contains the regression sum of squares and the residual sum "
	   "of squares.\n"
	   "\n"
	   "* If @known_x's is omitted, an array {1, 2, 3, ...} is used.\n"
	   "* If @known_y's and @known_x's have unequal number of data "
	   "points, LINEST returns #NUM! error.\n"
	   "* If @const is FALSE, the line will be forced to go through the "
	   "origin, i.e., b will be zero. The default is TRUE.\n"
	   "* The default of @stat is FALSE.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOGEST,TREND")
};

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

static GnmValue *
gnumeric_linest (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float       **xss = NULL, *ys = NULL;
	GnmValue         *result = NULL;
	int               nx, ny, dim, i;
	int               xarg = 0;
	gnm_float        *linres = NULL;
	gboolean          affine, stat, err;
	enum {
		ARRAY      = 1,
		SINGLE_COL = 2,
		SINGLE_ROW = 3,
		OTHER      = 4
	}                 ytype;
	regression_stat_t *extra_stat;

	extra_stat = regression_stat_new ();
	dim = 0;

	if (argv[0] == NULL || (argv[0]->type != VALUE_ARRAY && argv[0]->type != VALUE_CELLRANGE)){
	        goto out; /* Not a valid input for ys */
	}

	if (argv[0]->type == VALUE_ARRAY)
		ytype = ARRAY;
	else if (argv[0]->v_range.cell.a.col == argv[0]->v_range.cell.b.col)
		ytype = SINGLE_COL;
	else if (argv[0]->v_range.cell.a.row == argv[0]->v_range.cell.b.row)
		ytype = SINGLE_ROW;
	else ytype = OTHER;

	if (argv[0]->type == VALUE_CELLRANGE)
		ys = collect_floats_value (argv[0], ei->pos,
					   COLLECT_IGNORE_STRINGS |
					   COLLECT_IGNORE_BOOLS,
					   &ny, &result);
	else if (argv[0]->type == VALUE_ARRAY){
	  /*
	   * Get ys from array argument argv[0]
	   */
	}

	if (result)
		goto out;

	/* TODO Better error-checking in next statement */

	if (argv[1] == NULL || (ytype == ARRAY && argv[1]->type != VALUE_ARRAY) ||
	    (ytype != ARRAY && argv[1]->type != VALUE_CELLRANGE)){
		dim = 1;
		xss = g_new (gnm_float *, 1);
	        xss[0] = g_new (gnm_float, ny);
	        for (nx = 0; nx < ny; nx++)
		        xss[0][nx] = nx + 1;
	}
	else if (ytype == ARRAY){
			xarg = 1;
			/* Get xss from array argument argv[1] */
	}
	else if (ytype == SINGLE_COL){
		int firstcol, lastcol;
		GnmValue *copy;
		xarg = 1;
		firstcol = argv[1]->v_range.cell.a.col;
		lastcol  = argv[1]->v_range.cell.b.col;

		if (firstcol > lastcol) {
			int tmp = firstcol;
			firstcol = lastcol;
			lastcol = tmp;
		}

		dim = lastcol - firstcol + 1;
		copy = value_dup (argv[1]);
		xss = g_new (gnm_float *, dim);
		for (i = firstcol; i <= lastcol; i++){
			copy->v_range.cell.a.col = i;
			copy->v_range.cell.b.col = i;
			xss[i - firstcol] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				value_release (copy);
				dim = i - firstcol; /* How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
				value_release (copy);
				dim = i - firstcol + 1;
				result = value_new_error_NUM (ei->pos);
				goto out;
			}
		}
		value_release (copy);
	}
	else if (ytype == SINGLE_ROW){
		int firstrow, lastrow;
		GnmValue *copy;
		xarg = 1;
		firstrow = argv[1]->v_range.cell.a.row;
		lastrow  = argv[1]->v_range.cell.b.row;

		if (firstrow < lastrow) {
			int tmp = firstrow;
			firstrow = lastrow;
			lastrow = tmp;
		}

		dim = lastrow - firstrow + 1;
		copy = value_dup (argv[1]);
		xss = g_new (gnm_float *, dim);
		for (i = firstrow; i <= lastrow; i++){
			copy->v_range.cell.a.row = i;
			copy->v_range.cell.b.row = i;
			xss[i - firstrow] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				value_release (copy);
				dim = i - firstrow; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
					value_release (copy);
					dim = i - firstrow + 1;
					result = value_new_error_NUM (ei->pos);
					goto out;
			}
		}
		value_release (copy);
	}
	else { /*Y is none of the above */
		xarg = 1;
		dim = 1;
		xss = g_new (gnm_float *, dim);
		xss[0] = collect_floats_value (argv[1], ei->pos,
					       COLLECT_IGNORE_STRINGS |
					       COLLECT_IGNORE_BOOLS,
					       &nx, &result);
		if (result){
			dim = 0;
			goto out;
		}
		if (nx != ny){
			dim = 1;
			result = value_new_error_NUM (ei->pos);
			goto out;
		}
	}

	if (argv[1 + xarg]) {
		affine = value_get_as_bool (argv[1 + xarg], &err);
		if (err) {
			result = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else
		affine = TRUE;

	if (argv[2 + xarg]) {
		stat = value_get_as_bool (argv[2 + xarg], &err);
		if (err) {
			result = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else
		stat = FALSE;

	linres = g_new (gnm_float, dim + 1);

	if (linear_regression (xss, dim, ys, nx, affine,
			       linres, extra_stat) != REG_ok) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	if (stat) {
		result = value_new_array (dim + 1, 5);

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat->sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (sqrtgnum (extra_stat->var)));
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
				 value_new_float (extra_stat->se[0]));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (linres[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0,
				 value_new_float (linres[i + 1]));

 out:
	if (xss) {
		for (i = 0; i < dim; i++)
			g_free (xss[i]);
		g_free (xss);
	}
	g_free (ys);
	g_free (linres);
	regression_stat_destroy (extra_stat);

	return result;
}

/***************************************************************************/

static char const *help_logreg = {
	N_("@FUNCTION=LOGREG\n"
	   "@SYNTAX=LOGREG(known_y's[,known_x's[,const[,stat]]])\n"

	   "@DESCRIPTION="
	   "LOGREG function transforms your x's to z=ln(x) and "
	   "applies the ``least squares'' method to fit the linear equation\n"
           "y = m * z + b \n"
	   "to your y's and z's --- equivalent to fitting the equation\n"
	   "y = m * ln(x) + b \n"
	   "to y's and x's. \n"
	   "\n"
	   "If @known_x's is omitted, an array {1, 2, 3, ...} is used. "
           "LOGREG returns an array having two columns and one row. "
           "m is given in the first column and b in the second. \n"
	   "\n"
	   "If @known_y's and @known_x's have unequal number of data points, "
	   "LOGREG returns #NUM! error.\n"
	   "\n"
	   "If @const is FALSE, the curve will be forced to go through "
	   "[1; 0], i.e., b will be zero. The default is TRUE.\n"
	   "\n"
	   "If @stat is TRUE, extra statistical information will be returned "
	   "which applies to the state AFTER transformation to z. "
	   "Extra statistical information is written below m and b in the "
	   "result array.  Extra statistical information consists of four "
	   "rows of data.  In the first row the standard error values for the "
	   "coefficients m, b are represented.  The second row "
	   "contains the square of R and the standard error for the y "
	   "estimate. The third row contains the F-observed value and the "
	   "degrees of freedom.  The last row contains the regression sum "
	   "of squares and the residual sum of squares."
	   "The default of @stat is FALSE.\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOGFIT,LINEST,LOGEST")
};
/* The following is a copy of "gnumeric_linest" of Gnumeric version 1.1.9 
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
gnumeric_logreg (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float        **xss = NULL, *ys = NULL;
	GnmValue             *result = NULL;
	int               nx, ny, dim, i;
	int               xarg = 0;
	gnm_float        *logreg_res = NULL;
	gboolean          affine, stat, err;
	enum {
		ARRAY      = 1,
		SINGLE_COL = 2,
		SINGLE_ROW = 3,
		OTHER      = 4
	}                 ytype;
	regression_stat_t *extra_stat;

	extra_stat = regression_stat_new ();
	dim = 0;

	if (argv[0] == NULL || (argv[0]->type != VALUE_ARRAY && argv[0]->type != VALUE_CELLRANGE)){
	        goto out; /* Not a valid input for ys */
	}

	if (argv[0]->type == VALUE_ARRAY)
		ytype = ARRAY;
	else if (argv[0]->v_range.cell.a.col == argv[0]->v_range.cell.b.col)
		ytype = SINGLE_COL;
	else if (argv[0]->v_range.cell.a.row == argv[0]->v_range.cell.b.row)
		ytype = SINGLE_ROW;
	else ytype = OTHER;

	if (argv[0]->type == VALUE_CELLRANGE)
		ys = collect_floats_value (argv[0], ei->pos,
					   COLLECT_IGNORE_STRINGS |
					   COLLECT_IGNORE_BOOLS,
					   &ny, &result);
	else if (argv[0]->type == VALUE_ARRAY){
	  /*
	   * Get ys from array argument argv[0]
	   */
	}

	if (result)
		goto out;

	/* TODO Better error-checking in next statement */

	if (argv[1] == NULL || (ytype == ARRAY && argv[1]->type != VALUE_ARRAY) ||
	    (ytype != ARRAY && argv[1]->type != VALUE_CELLRANGE)){
		dim = 1;
		xss = g_new (gnm_float *, 1);
	        xss[0] = g_new (gnm_float, ny);
	        for (nx = 0; nx < ny; nx++)
		        xss[0][nx] = nx + 1;
	}
	else if (ytype == ARRAY){
			xarg = 1;
			/* Get xss from array argument argv[1] */
	}
	else if (ytype == SINGLE_COL){
		int firstcol, lastcol;
		GnmValue *copy;
		xarg = 1;
		firstcol = argv[1]->v_range.cell.a.col;
		lastcol  = argv[1]->v_range.cell.b.col;

		if (firstcol < lastcol) {
			int tmp = firstcol;
			firstcol = lastcol;
			lastcol = tmp;
		}

		dim = lastcol - firstcol + 1;
		copy = value_dup (argv[1]);
		xss = g_new (gnm_float *, dim);
		for (i = firstcol; i <= lastcol; i++){
			copy->v_range.cell.a.col = i;
			copy->v_range.cell.b.col = i;
			xss[i - firstcol] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				value_release (copy);
				dim = i - firstcol; /* How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
				value_release (copy);
				dim = i - firstcol + 1;
				result = value_new_error_NUM (ei->pos);
				goto out;
			}
		}
		value_release (copy);
	}
	else if (ytype == SINGLE_ROW){
	int firstrow, lastrow;
		GnmValue *copy;
		xarg = 1;
		firstrow = argv[1]->v_range.cell.a.row;
		lastrow  = argv[1]->v_range.cell.b.row;

		if (firstrow < lastrow) {
			int tmp = firstrow;
			firstrow = lastrow;
			lastrow = tmp;
		}

		dim = lastrow - firstrow + 1;
		copy = value_dup (argv[1]);
		xss = g_new (gnm_float *, dim);
		for (i = firstrow; i <= lastrow; i++){
			copy->v_range.cell.a.row = i;
			copy->v_range.cell.b.row = i;
			xss[i - firstrow] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				value_release (copy);
				dim = i - firstrow; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
					value_release (copy);
					dim = i - firstrow + 1;
					result = value_new_error_NUM (ei->pos);
					goto out;
			}
		}
		value_release (copy);
	}
	else { /*Y is none of the above */
		xarg = 1;
		dim = 1;
		xss = g_new (gnm_float *, dim);
		xss[0] = collect_floats_value (argv[1], ei->pos,
					       COLLECT_IGNORE_STRINGS |
					       COLLECT_IGNORE_BOOLS,
					       &nx, &result);
		if (result){
			dim = 0;
			goto out;
		}
		if (nx != ny){
			dim = 1;
			result = value_new_error_NUM (ei->pos);
			goto out;
		}
	}

	if (argv[1 + xarg]) {
		affine = value_get_as_bool (argv[1 + xarg], &err);
		if (err) {
			result = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else
		affine = TRUE;

	if (argv[2 + xarg]) {
		stat = value_get_as_bool (argv[2 + xarg], &err);
		if (err) {
			result = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else
		stat = FALSE;

	logreg_res = g_new (gnm_float, dim + 1);

	if (logarithmic_regression (xss, dim, ys, nx, affine,
				    logreg_res, extra_stat) != REG_ok) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	if (stat) {
		result = value_new_array (dim + 1, 5);

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat->sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (sqrtgnum (extra_stat->var)));
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
				 value_new_float (extra_stat->se[0]));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (logreg_res[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0,
				 value_new_float (logreg_res[i + 1]));

 out:
	if (xss) {
		for (i = 0; i < dim; i++)
			g_free (xss[i]);
		g_free (xss);
	}
	g_free (ys);
	g_free (logreg_res);
	regression_stat_destroy (extra_stat);

	return result;
}

/***************************************************************************/

static char const *help_logfit = {
	N_("@FUNCTION=LOGFIT\n"
	   "@SYNTAX=LOGFIT(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "LOGFIT function applies the ``least squares'' method to fit "
	   "the logarithmic equation\n"
	   "y = a + b * ln(sign * (x - c)) ,   sign = +1 or -1 \n"
	   "to your data. The graph of the equation is a logarithmic curve "
	   "moved horizontally by c and possibly mirrored across the y-axis "
	   "(if sign = -1).\n"
	   "\n"
	   "LOGFIT returns an array having five columns and one row. "
	   "`Sign' is given in the first column, `a', `b', and `c' are "
	   "given in columns 2 to 4. Column 5 holds the sum of squared "
	   "residuals.\n"
	   "\n"
	   "An error is returned when there are less than 3 different x's "
	   "or y's, or when the shape of the point cloud is too different "
	   "from a ``logarithmic'' one.\n"
	   "\n"
	   "You can use the above formula \n"
	   "= a + b * ln(sign * (x - c)) \n"
	   "or rearrange it to \n"
	   "= (exp((y - a) / b)) / sign + c \n"
	   "to compute unknown y's or x's, respectively. \n"
	   "\n"
	   "Technically, this is non-linear fitting by trial-and-error. "
	   "The accuracy of `c' is: width of x-range -> rounded to the "
	   "next smaller (10^integer), times 0.000001. There might be cases "
	   "in which the returned fit is not the best possible.\n"
           "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOGREG,LINEST,LOGEST")
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
gnumeric_logfit (FunctionEvalInfo *ei, GnmValue *argv[])
{
        gnm_float         *xs = NULL, *ys = NULL;
	GnmValue              *result = NULL;
	int                nx, ny, i;
	gnm_float         *logfit_res = NULL;

        if (argv[0] == NULL || argv[0]->type != VALUE_CELLRANGE)
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
	if (argv[1] == NULL || argv[1]->type != VALUE_CELLRANGE)
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

	if (logarithmic_fit (xs, ys, nx, logfit_res) != REG_ok) {
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

static char const *help_trend = {
	N_("@FUNCTION=TREND\n"
	   "@SYNTAX=TREND(known_y's[,known_x's[,new_x's[,const]]])\n"

	   "@DESCRIPTION="
	   "TREND function estimates future values of a given data set "
	   "using the ``least squares'' line that best fit to your data. "
	   "@known_y's is the y-values where y=mx+b and @known_x's contains "
	   "the corresponding x-values.  @new_x's contains the x-values for "
	   "which you want to estimate the y-values. If @const is FALSE, "
	   "the line will be forced to go through the "
           "origin, i.e., b will be zero.\n"
	   "\n"
	   "* If @known_x's is omitted, an array {1, 2, 3, ...} is used.\n"
	   "* If @new_x's is omitted, it is assumed to be the same as "
	   "@known_x's.\n"
	   "* If @const is omitted, it is assumed to be TRUE.\n"
	   "* If @known_y's and @known_x's have unequal number of data points, "
	   "TREND returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "TREND(A1:A5,B1:B5) equals 156.52.\n"
	   "\n"
	   "@SEEALSO=LINEST")
};

static GnmValue *
gnumeric_trend (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float  *xs = NULL, *ys = NULL, *nxs = NULL;
	GnmValue    *result = NULL;
	int      nx, ny, nnx, i, dim;
	gboolean affine, err;
	gnm_float  linres[2];

	ys = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS,
				   &ny, &result);
	if (result)
		goto out;

	affine = TRUE;

	if (argv[2] != NULL) {
	        xs = collect_floats_value (argv[1], ei->pos,
					   COLLECT_IGNORE_STRINGS |
					   COLLECT_IGNORE_BOOLS,
					   &nx, &result);

		nxs = collect_floats_value (argv[2], ei->pos,
					    COLLECT_IGNORE_STRINGS |
					    COLLECT_IGNORE_BOOLS,
					    &nnx, &result);
		if (argv[3] != NULL) {
		        affine = value_get_as_bool (argv[3], &err);
			if (err) {
			        result = value_new_error_VALUE (ei->pos);
				goto out;
			}
		}
	} else {
	        /* @new_x's is assumed to be the same as @known_x's */
	        if (argv[1] != NULL) {
		        xs = collect_floats_value (argv[1], ei->pos,
						   COLLECT_IGNORE_STRINGS |
						   COLLECT_IGNORE_BOOLS,
						   &nx, &result);
			nxs = collect_floats_value (argv[1], ei->pos,
						    COLLECT_IGNORE_STRINGS |
						    COLLECT_IGNORE_BOOLS,
						    &nnx, &result);
		} else {
		        xs = g_new (gnm_float, ny);
			for (nx = 0; nx < ny; nx++)
			        xs[nx] = nx + 1;
		        nxs = g_new (gnm_float, ny);
			for (nnx = 0; nnx < ny; nnx++)
			        xs[nnx] = nnx + 1;
		}
	}

	if (result)
		goto out;

	if (nx != ny) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	dim = 1;

	if (linear_regression (&xs, dim, ys, nx, affine, linres, NULL) !=
	    REG_ok) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	result = value_new_array (1, nnx);
	for (i = 0; i < nnx; i++)
	        value_array_set (result, 0, i,
				 value_new_float (linres[1] * nxs[i] + linres[0]));

 out:
	g_free (xs);
	g_free (ys);
	g_free (nxs);
	return result;
}

/***************************************************************************/

static char const *help_logest = {
	N_("@FUNCTION=LOGEST\n"
	   "@SYNTAX=LOGEST(known_y's[,known_x's,const,stat])\n"

	   "@DESCRIPTION="
	   "LOGEST function applies the ``least squares'' method to fit "
	   "an exponential curve of the form\n\n\t"
	   "y = b * m{1}^x{1} * m{2}^x{2}... to your data.\n"
	   "\n"
	   "If @stat is TRUE, extra statistical information will be returned. "
	   "Extra statistical information is written bellow the regression "
	   "line coefficients in the result array.  Extra statistical "
	   "information consists of four rows of data.  In the first row "
	   "the standard error values for the coefficients m1, (m2, ...), b "
	   "are represented.  The second row contains the square of R and "
	   "the standard error for the y estimate.  The third row contains "
	   "the F-observed value and the degrees of freedom.  The last row "
	   "contains the regression sum of squares and the residual sum "
	   "of squares.\n"
	   "\n"
	   "* If @known_x's is omitted, an array {1, 2, 3, ...} is used. "
	   "LOGEST returns an array { m{n},m{n-1}, ...,m{1},b }.\n"
	   "* If @known_y's and @known_x's have unequal number of data points, "
	   "LOGEST returns #NUM! error.\n"
	   "* If @const is FALSE, the line will be forced to go through (0,1),"
	   "i.e., b will be one.  The default is TRUE.\n"
	   "* The default of @stat is FALSE.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=GROWTH,TREND")
};

static GnmValue *
gnumeric_logest (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float        **xss = NULL, *ys = NULL;
	GnmValue             *result = NULL;
	int               affine, nx, ny, dim, i;
	int               xarg = 0;
	gnm_float        *expres = NULL;
	gboolean          stat, err;
	regression_stat_t *extra_stat;
	enum {
		ARRAY      = 1,
		SINGLE_COL = 2,
		SINGLE_ROW = 3,
		OTHER      = 4
	}                 ytype;

	extra_stat = regression_stat_new ();
	dim = 0;

	if (argv[0] == NULL || (argv[0]->type != VALUE_ARRAY && argv[0]->type != VALUE_CELLRANGE)){
	        goto out; /* Not a valid input for ys */
	}

	if (argv[0]->type == VALUE_ARRAY)
		ytype = ARRAY;
	else if (argv[0]->v_range.cell.a.col == argv[0]->v_range.cell.b.col)
		ytype = SINGLE_COL;
	else if (argv[0]->v_range.cell.a.row == argv[0]->v_range.cell.b.row)
		ytype = SINGLE_ROW;
	else ytype = OTHER;

	if (argv[0]->type == VALUE_CELLRANGE)
		ys = collect_floats_value (argv[0], ei->pos,
					   COLLECT_IGNORE_STRINGS |
					   COLLECT_IGNORE_BOOLS,
					   &ny, &result);
	else if (argv[0]->type == VALUE_ARRAY){
		/*
		 * Get ys from array argument argv[0]
		 */
	}

	if (result)
		goto out;

	/* TODO Better error-checking in next statement */

	if (argv[1] == NULL || (ytype == ARRAY && argv[1]->type != VALUE_ARRAY) ||
	    (ytype != ARRAY && argv[1]->type != VALUE_CELLRANGE)){
		dim = 1;
		xss = g_new (gnm_float *, 1);
	        xss[0] = g_new (gnm_float, ny);
	        for (nx = 0; nx < ny; nx++)
		        xss[0][nx] = nx + 1;
	}
	else if (ytype == ARRAY){
			xarg = 1;
			/* Get xss from array argument argv[1] */
	}
	else if (ytype == SINGLE_COL){
		int firstcol, lastcol;
		GnmValue *copy;
		xarg = 1;
		firstcol = argv[1]->v_range.cell.a.col;
		lastcol  = argv[1]->v_range.cell.b.col;
		if (firstcol < lastcol) {
			int tmp = firstcol;
			firstcol = lastcol;
			lastcol = tmp;
		}

		dim = lastcol - firstcol + 1;
		copy = value_dup (argv[1]);
		xss = g_new (gnm_float *, dim);
		for (i = firstcol; i <= lastcol; i++){
			copy->v_range.cell.a.col = i;
			copy->v_range.cell.b.col = i;
			xss[i - firstcol] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				value_release (copy);
				dim = i - firstcol; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
				value_release (copy);
				dim = i - firstcol + 1;
				result = value_new_error_NUM (ei->pos);
				goto out;
			}
		}
		value_release (copy);
	}
	else if (ytype == SINGLE_ROW){
		int firstrow, lastrow;
		GnmValue *copy;
		xarg = 1;
		firstrow = argv[1]->v_range.cell.a.row;
		lastrow  = argv[1]->v_range.cell.b.row;

		if (firstrow < lastrow) {
			int tmp = firstrow;
			firstrow = lastrow;
			lastrow = tmp;
		}

		dim = lastrow - firstrow + 1;
		copy = value_dup (argv[1]);
		xss = g_new (gnm_float *, dim);
		for (i = firstrow; i <= lastrow; i++){
			copy->v_range.cell.a.row = i;
			copy->v_range.cell.b.row = i;
			xss[i - firstrow] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				value_release (copy);
				dim = i - firstrow; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
					value_release (copy);
					dim = i - firstrow + 1;
					result = value_new_error_NUM (ei->pos);
					goto out;
			}
		}
		value_release (copy);
	}
	else { /*Y is none of the above */
		xarg = 1;
		dim = 1;
		xss = g_new (gnm_float *, dim);
		xss[0] = collect_floats_value (argv[1], ei->pos,
					       COLLECT_IGNORE_STRINGS |
					       COLLECT_IGNORE_BOOLS,
					       &nx, &result);
		if (result){
			dim = 0;
			goto out;
		}
		if (nx != ny){
			dim = 1;
			result = value_new_error_NUM (ei->pos);
			goto out;
		}
	}

	if (argv[1 + xarg]) {
		affine = value_get_as_bool (argv[1 + xarg], &err) ? 1 : 0;
		if (err) {
			result = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else
		affine = 1;

	if (argv[2 + xarg]) {
		stat = value_get_as_bool (argv[2 + xarg], &err);
		if (err) {
			result = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else
		stat = FALSE;

	expres = g_new (gnm_float, dim + 1);
	if (exponential_regression (xss, dim, ys, nx, affine,
				    expres, extra_stat) != REG_ok) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	if (stat) {
		result = value_new_array (dim + 1, 5);

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat->sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (sqrtgnum (extra_stat->var))); /* Still wrong ! */
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
					 value_new_float (extra_stat->se[i + affine]));
		value_array_set (result, dim, 1,
				 value_new_float (extra_stat->se[0]));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (expres[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0, value_new_float (expres[i + 1]));

 out:
	if (xss) {
		for (i = 0; i < dim; i++)
			g_free (xss[i]);
		g_free (xss);
	}
	g_free (ys);
	g_free (expres);
	regression_stat_destroy (extra_stat);

	return result;
}

/***************************************************************************/

static char const *help_growth = {
	N_("@FUNCTION=GROWTH\n"
	   "@SYNTAX=GROWTH(known_y's[,known_x's,new_x's,const])\n"

	   "@DESCRIPTION="
	   "GROWTH function applies the ``least squares'' method to fit an "
	   "exponential curve to your data and predicts the exponential "
	   "growth by using this curve. "
	   "\n"
	   "GROWTH returns an array having one column and a row for each "
	   "data point in @new_x.\n"
	   "\n"
	   "* If @known_x's is omitted, an array {1, 2, 3, ...} is used.\n"
	   "* If @new_x's is omitted, it is assumed to be the same as "
	   "@known_x's.\n"
	   "* If @known_y's and @known_x's have unequal number of data points, "
	   "GROWTH returns #NUM! error.\n"
	   "* If @const is FALSE, the line will be forced to go through the "
	   "origin, i.e., b will be zero. The default is TRUE.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOGEST,GROWTH,TREND")
};

static GnmValue *
gnumeric_growth (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float  *xs = NULL, *ys = NULL, *nxs = NULL;
	GnmValue    *result = NULL;
	gboolean affine, err;
	int      nx, ny, nnx, i, dim;
	gnm_float  expres[2];

	affine = TRUE;

	ys = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS,
				   &ny, &result);
	if (result)
		goto out;

	if (argv[2] != NULL) {
	        xs = collect_floats_value (argv[1], ei->pos,
					   COLLECT_IGNORE_STRINGS |
					   COLLECT_IGNORE_BOOLS,
					   &nx, &result);

		nxs = collect_floats_value (argv[2], ei->pos,
					    COLLECT_IGNORE_STRINGS |
					    COLLECT_IGNORE_BOOLS,
					    &nnx, &result);
		if (argv[3] != NULL) {
		        affine = value_get_as_bool (argv[3], &err);
			if (err) {
			        result = value_new_error_VALUE (ei->pos);
				goto out;
			}
		}
	} else {
	        /* @new_x's is assumed to be the same as @known_x's */
	        if (argv[1] != NULL) {
		        xs = collect_floats_value (argv[1], ei->pos,
						   COLLECT_IGNORE_STRINGS |
						   COLLECT_IGNORE_BOOLS,
						   &nx, &result);
		        nxs = collect_floats_value (argv[1], ei->pos,
						    COLLECT_IGNORE_STRINGS |
						    COLLECT_IGNORE_BOOLS,
						    &nnx, &result);
		} else {
		        xs = g_new (gnm_float, ny);
			for (nx = 0; nx < ny; nx++)
			        xs[nx] = nx + 1;
		        nxs = g_new (gnm_float, ny);
			for (nnx = 0; nnx < ny; nnx++)
			        nxs[nnx] = nnx + 1;
		}
	}

	if (result)
		goto out;

	if (nx != ny) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	dim = 1;

	if (exponential_regression (&xs, dim,
				    ys, nx, affine, expres, NULL) != REG_ok) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	result = value_new_array (1, nnx);
	for (i = 0; i < nnx; i++)
	        value_array_set (result, 0, i,
				 value_new_float (powgnum (expres[1], nxs[i]) *
						  expres[0]));

 out:
	g_free (xs);
	g_free (ys);
	g_free (nxs);
	return result;
}

/***************************************************************************/

static char const *help_forecast = {
	N_("@FUNCTION=FORECAST\n"
	   "@SYNTAX=FORECAST(x,known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "FORECAST function estimates a future value according to "
	   "existing values using simple linear regression.  The estimated "
	   "future value is a y-value for a given x-value (@x).\n"
	   "\n"
	   "* If @known_x or @known_y contains no data entries or different "
	   "number of data entries, FORECAST returns #N/A error.\n"
	   "* If the variance of the @known_x is zero, FORECAST returns #DIV/0 "
	   "error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "FORECAST(7,A1:A5,B1:B5) equals -10.859397661.\n"
	   "\n"
	   "@SEEALSO=INTERCEPT,TREND")
};


static GnmValue *
gnumeric_forecast (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float x, *xs = NULL, *ys = NULL;
	GnmValue *result = NULL;
	int nx, ny, dim;
	gnm_float linres[2];

	x = value_get_as_float (argv[0]);

	ys = collect_floats_value (argv[1], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS,
				   &ny, &result);
	if (result)
		goto out;

	xs = collect_floats_value (argv[2], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS,
				   &nx, &result);
	if (result)
		goto out;

	if (nx != ny) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	dim = 1;

	if (linear_regression (&xs, dim, ys, nx, 1, linres, NULL) != REG_ok) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	result = value_new_float (linres[0] + x * linres[1]);

 out:
	g_free (xs);
	g_free (ys);
	return result;
}

/***************************************************************************/

static char const *help_intercept = {
	N_("@FUNCTION=INTERCEPT\n"
	   "@SYNTAX=INTERCEPT(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "INTERCEPT function calculates the point where the linear "
	   "regression line intersects the y-axis.\n"
	   "\n"
	   "* If @known_x or @known_y contains no data entries or different "
	   "number of data entries, INTERCEPT returns #N/A error.\n"
	   "* If the variance of the @known_x is zero, INTERCEPT returns "
	   "#DIV/0 error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "INTERCEPT(A1:A5,B1:B5) equals -20.785117212.\n"
	   "\n"
	   "@SEEALSO=FORECAST,TREND")
};

static int
range_intercept (const gnm_float *xs, const gnm_float *ys, int n, gnm_float *res)
{
	gnm_float linres[2];
	int dim = 1;

	if (linear_regression ((gnm_float **)&xs, dim,
			       ys, n, 1, linres, NULL) != REG_ok)
		return 1;

	*res = linres[0];
	return 0;
}

static GnmValue *
gnumeric_intercept (FunctionEvalInfo *ei, GnmValue **argv)
{
	return float_range_function2 (argv[1], argv[0],
				      ei,
				      range_intercept,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_slope = {
	N_("@FUNCTION=SLOPE\n"
	   "@SYNTAX=SLOPE(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "SLOPE returns the slope of the linear regression line.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "SLOPE(A1:A5,B1:B5) equals 1.417959936.\n"
	   "\n"
	   "@SEEALSO=STDEV,STDEVPA")
};

static int
range_slope (const gnm_float *xs, const gnm_float *ys, int n, gnm_float *res)
{
	gnm_float linres[2];
	int dim = 1;

	if (linear_regression ((gnm_float **)&xs, dim,
			       ys, n, 1, linres, NULL) != REG_ok)
		return 1;

	*res = linres[1];
	return 0;
}

static GnmValue *
gnumeric_slope (FunctionEvalInfo *ei, GnmValue **argv)
{
	return float_range_function2 (argv[1], argv[0],
				      ei,
				      range_slope,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static char const *help_subtotal = {
	N_("@FUNCTION=SUBTOTAL\n"
	   "@SYNTAX=SUBTOTAL(function_nbr,ref1,ref2,...)\n"

	   "@DESCRIPTION="
	   "SUBTOTAL function returns a subtotal of given list of arguments. "
	   "@function_nbr is the number that specifies which function to use "
	   "in calculating the subtotal.\n\n"
	   "The following functions are available:\n\n"
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
	   "\t11   VARP\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "23, 27, 28, 33, and 39.  Then\n"
	   "SUBTOTAL(1,A1:A5) equals 30.\n"
	   "SUBTOTAL(6,A1:A5) equals 22378356.\n"
	   "SUBTOTAL(7,A1:A5) equals 6.164414003.\n"
	   "SUBTOTAL(9,A1:A5) equals 150.\n"
	   "SUBTOTAL(11,A1:A5) equals 30.4.\n"
	   "\n"
	   "@SEEALSO=COUNT,SUM")
};

static GnmValue *
gnumeric_subtotal (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
        GnmExpr const *expr;
	GnmValue *val;
	int   fun_nbr;
	float_range_function_t func;
	GnmStdError err = GNM_ERROR_DIV0;

	if (expr_node_list == NULL)
		return value_new_error_NUM (ei->pos);

	expr = expr_node_list->data;
	if (expr == NULL)
		return value_new_error_NUM (ei->pos);

	val = gnm_expr_eval (expr, ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (val->type == VALUE_ERROR)
		return val;
	fun_nbr = value_get_as_int (val);
	value_release (val);

	/* Skip the first node */
	expr_node_list = expr_node_list->next;

	switch (fun_nbr) {
	case 2: val = value_new_int (0);
		/* no need to check for error, this is not strict */
		function_iterate_argument_values (ei->pos,
			callback_function_count, val, expr_node_list,
			FALSE, CELL_ITER_IGNORE_BLANK | CELL_ITER_IGNORE_SUBTOTAL);
		return val;

	case 3: val = value_new_int (0);
		/* no need to check for error, this is not strict */
		function_iterate_argument_values (ei->pos,
			callback_function_counta, val, expr_node_list,
			FALSE, CELL_ITER_IGNORE_BLANK | CELL_ITER_IGNORE_SUBTOTAL);
		return val;

	case  1: func = range_average;		break;
	case  4: err = GNM_ERROR_VALUE;
		 func = range_max0;		break;
	case  5: err = GNM_ERROR_VALUE;
		 func = range_min0;		break;
	case  6: err = GNM_ERROR_VALUE;
		 func = range_product;		break;
	case  7: func = range_stddev_est;	break;
	case  8: func = range_stddev_pop;	break;
	case  9: err = GNM_ERROR_VALUE;
		 func = range_sum;		break;
	case 10: func = range_var_est;		break;
	case 11: func = range_var_pop;		break;

	default:
		return value_new_error_NUM (ei->pos);
	}

	return float_range_function (expr_node_list, ei, func,
		COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS |
		COLLECT_IGNORE_BLANKS | COLLECT_IGNORE_SUBTOTAL,
		err);
}

/***************************************************************************/

static char const *help_cronbach = {
	N_("@FUNCTION=CRONBACH\n"
	   "@SYNTAX=CRONBACH(ref1,ref2,...)\n"

	   "@DESCRIPTION="
	   "CRONBACH returns Cronbach's alpha for the given cases."
	   "\n"
	   "@ref1 is a data set, @ref2 the second data set, etc.."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static inline void
free_values (GnmValue **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values [i])
			value_release (values [i]);
	g_free (values);
}

static inline GnmValue *
function_marshal_arg (FunctionEvalInfo *ei,
		      GnmExpr         *t,
		      GnmValue            **type_mismatch)
{
	GnmValue *v;

	*type_mismatch = NULL;

	if (t->any.oper == GNM_EXPR_OP_CELLREF)
		v = value_new_cellrange (&t->cellref.ref, &t->cellref.ref,
					 ei->pos->eval.col,
					 ei->pos->eval.row);
	else
		v = gnm_expr_eval (t, ei->pos, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);

	if (v->type != VALUE_ARRAY &&
	    v->type != VALUE_CELLRANGE) {
		*type_mismatch = value_new_error_VALUE (ei->pos);
	}

	if (v->type == VALUE_CELLRANGE) {
		cellref_make_abs (&v->v_range.cell.a,
				  &v->v_range.cell.a,
				  ei->pos);
		cellref_make_abs (&v->v_range.cell.b,
				  &v->v_range.cell.b,
				  ei->pos);
	}

	return v;
}

static GnmValue *
gnumeric_cronbach (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	int         k, i, j;
	GnmValue **values;
	gnm_float sum_variance = 0.0;
	gnm_float sum_covariance = 0.0;
	GnmExprList *list = expr_node_list;
	GnmExprList *short_list;

	k = gnm_expr_list_length (expr_node_list);
	if (k < 2)
	        return value_new_error_VALUE (ei->pos);

	for (i = 0; i < k && list; list = list->next, ++i) {
		GnmValue *fl_val;

		short_list = gnm_expr_list_prepend (NULL, list->data);
		fl_val = float_range_function (short_list, ei,
					       range_var_pop, 0,
					       GNM_ERROR_VALUE);
		gnm_expr_list_free (short_list);
		if (!VALUE_IS_NUMBER (fl_val))
			return fl_val;
		sum_variance += value_get_as_float (fl_val);
		value_release (fl_val);
	}


	values = g_new0 (GnmValue *, k);
	list = expr_node_list;

	for (i = 0; list; list = list->next, ++i) {
		GnmValue *type_mismatch;

		values[i] = function_marshal_arg (ei, list->data, &type_mismatch);
		if (type_mismatch || values[i] == NULL) {
			free_values (values, i + 1);
			if (type_mismatch)
				return type_mismatch;
			else
				return value_new_error_VALUE (ei->pos);
		}
	}

	g_return_val_if_fail (i == k, value_new_error_VALUE (ei->pos));

	/* We now have an array of array values and an expression list */
	for (i = 0; i < k; ++i) {
		for (j = i + 1; j < k; ++j) {
			GnmValue *fl_val;
			fl_val = float_range_function2 (values[i], values[j],
							ei,
							range_covar, 0,
							GNM_ERROR_VALUE);
			if (!VALUE_IS_NUMBER (fl_val)) {
				free_values (values, k);
				return fl_val;
			}
			sum_covariance += value_get_as_float (fl_val);
			value_release (fl_val);
		}
	}

	free_values (values, k);
	return  value_new_float 
		(k * (1 - sum_variance / (sum_variance + 2 * sum_covariance)) / (k - 1));
}

/***************************************************************************/

static char const *help_geomdist = {
        N_("@FUNCTION=GEOMDIST\n"
           "@SYNTAX=GEOMDIST(k,p,cum)\n"

           "@DESCRIPTION="
           "GEOMDIST returns the probability p(k) of obtaining @k from a "
	   "geometric distribution with probability parameter @p.\n"
           "\n"
           "* If @k < 0 GEOMDIST returns #NUM! error.\n"
           "* If @p < 0 or @p > 1 GEOMDIST returns #NUM! error.\n"
           "* If @cum != TRUE and @cum != FALSE GEOMDIST returns #NUM! error.\n"
	   "\n"
           "@EXAMPLES=\n"
           "GEOMDIST(2,10.4,TRUE).\n"
           "\n"
           "@SEEALSO=RANDGEOM")
};

static GnmValue *
gnumeric_geomdist (FunctionEvalInfo *ei, GnmValue **argv)
{
	int        k   = value_get_as_int (argv[0]);
	gnm_float p   = value_get_as_float (argv[1]);
	gboolean   cum = value_get_as_int (argv[2]);

	if (p < 0 || p > 1 || k < 0 || (cum != TRUE && cum != FALSE))
		return value_new_error_NUM (ei->pos);

	if (cum)
		return value_new_float (pgeom (k, p, TRUE, FALSE));
	else
		return value_new_float (dgeom (k, p, FALSE));
}

/***************************************************************************/

static char const *help_logistic = {
        N_("@FUNCTION=LOGISTIC\n"
           "@SYNTAX=LOGISTIC(x,a)\n"

           "@DESCRIPTION="
           "LOGISTIC returns the probability density p(x) at @x for a "
	   "logistic distribution with scale parameter @a.\n"
           "\n"
           "@EXAMPLES=\n"
           "LOGISTIC(0.4,1).\n"
           "\n"
           "@SEEALSO=RANDLOGISTIC")
};

static gnm_float
random_logistic_pdf (gnm_float x, gnm_float a)
{
        gnm_float u = expgnum (-gnumabs (x) / a);

	return u / (gnumabs (a) * (1 + u) * (1 + u));
}

static GnmValue *
gnumeric_logistic (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);

	if (a <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_logistic_pdf (x, a));
}

/***************************************************************************/

static char const *help_pareto = {
        N_("@FUNCTION=PARETO\n"
           "@SYNTAX=PARETO(x,a,b)\n"

           "@DESCRIPTION="
           "PARETO returns the probability density p(x) at @x for a "
	   "Pareto distribution with exponent @a and scale @b.\n"
           "\n"
           "@EXAMPLES=\n"
           "PARETO(0.6,1,2).\n"
           "\n"
           "@SEEALSO=RANDPARETO")
};

static gnm_float
random_pareto_pdf (gnm_float x, gnm_float a, gnm_float b)
{
        if (x >= b)
	        return (a / b) / powgnum (x / b, a + 1);
	else
	        return 0;
}

static GnmValue *
gnumeric_pareto (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);
	gnm_float b = value_get_as_float (argv[2]);

	if (a <= 0 || b <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_pareto_pdf (x, a, b));
}

/***************************************************************************/

static char const *help_rayleigh = {
        N_("@FUNCTION=RAYLEIGH\n"
           "@SYNTAX=RAYLEIGH(x,sigma)\n"

           "@DESCRIPTION="
           "RAYLEIGH returns the probability density p(x) at @x for a "
	   "Rayleigh distribution with scale parameter @sigma.\n"
           "\n"
           "@EXAMPLES=\n"
           "RAYLEIGH(0.4,1).\n"
           "\n"
           "@SEEALSO=RANDRAYLEIGH")
};

static gnm_float
random_rayleigh_pdf (gnm_float x, gnm_float sigma)
{
        if (x < 0)
	        return 0;
	else {
	        gnm_float u = x / sigma;

		return (u / sigma) * expgnum (-u * u / 2.0);
	}
}

static GnmValue *
gnumeric_rayleigh (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x     = value_get_as_float (argv[0]);
	gnm_float sigma = value_get_as_float (argv[1]);

	if (sigma <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_rayleigh_pdf (x, sigma));
}

/***************************************************************************/

static char const *help_rayleightail = {
        N_("@FUNCTION=RAYLEIGHTAIL\n"
           "@SYNTAX=RAYLEIGHTAIL(x,a,sigma)\n"

           "@DESCRIPTION="
           "RAYLEIGHTAIL returns the probability density p(x) at @x for "
	   "a Rayleigh tail distribution with scale parameter @sigma and "
	   "lower limit @a.\n"
           "\n"
           "@EXAMPLES=\n"
           "RAYLEIGHTAIL(0.6,0.3,1).\n"
           "\n"
           "@SEEALSO=RANDRAYLEIGHTAIL")
};

static gnm_float
random_rayleigh_tail_pdf (gnm_float x, gnm_float a, gnm_float sigma)
{
        if (x < a)
	        return 0;
	else {
	        gnm_float u = x / sigma ;
		gnm_float v = a / sigma ;

		return (u / sigma) * expgnum ((v + u) * (v - u) / 2.0) ;
	}
}

static GnmValue *
gnumeric_rayleightail (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x     = value_get_as_float (argv[0]);
	gnm_float a     = value_get_as_float (argv[1]);
	gnm_float sigma = value_get_as_float (argv[2]);

	if (sigma <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_rayleigh_tail_pdf (x, a, sigma));
}

/***************************************************************************/

static char const *help_exppowdist = {
        N_("@FUNCTION=EXPPOWDIST\n"
           "@SYNTAX=EXPPOWDIST(x,a,b)\n"

           "@DESCRIPTION="
           "EXPPOWDIST returns the probability density p(x) at @x for "
	   "Exponential Power distribution with scale parameter @a and "
	   "exponent @b.\n"
           "\n"
           "@EXAMPLES=\n"
           "EXPPOWDIST(0.4,1,2).\n"
           "\n"
           "@SEEALSO=RANDEXPPOW")
};

static GnmValue *
gnumeric_exppowdist (FunctionEvalInfo *ei, GnmValue **argv)
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

static char const *help_laplace = {
        N_("@FUNCTION=LAPLACE\n"
           "@SYNTAX=LAPLACE(x,a)\n"

           "@DESCRIPTION="
           "LAPLACE returns the probability density p(x) at @x for "
	   "Laplace distribution with mean @a. "
           "\n"
           "@EXAMPLES=\n"
           "LAPLACE(0.4,1).\n"
           "\n"
           "@SEEALSO=RANDLAPLACE")
};

static GnmValue *
gnumeric_laplace (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float a = value_get_as_float (argv[1]);

	if (a <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (random_laplace_pdf (x, a));
}

/***************************************************************************/

const GnmFuncDescriptor stat_functions[] = {
        { "avedev", NULL,      N_("number,number,"),
	  &help_avedev, NULL, gnumeric_avedev, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "average", NULL,      N_("number,number,"),
	  &help_average, NULL, gnumeric_average, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "averagea", NULL,      N_("number,number,"),
	  &help_averagea, NULL, gnumeric_averagea, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "bernoulli", "ff", N_("k,p"),   &help_bernoulli,
	  gnumeric_bernoulli, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "betadist",     "fff|ff", N_("x,alpha,beta,a,b"),
	  &help_betadist, gnumeric_betadist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "betainv",      "fff|ff", N_("p,alpha,beta,a,b"),
	  &help_betainv, gnumeric_betainv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "binomdist",    "fffb", N_("n,t,p,c"),
	  &help_binomdist, gnumeric_binomdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "cauchy", "fff", N_("x,a,cum"),   &help_cauchy,
	  gnumeric_cauchy, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "chidist",      "ff",  N_("x,dof"),
	  &help_chidist, gnumeric_chidist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "chiinv",       "ff",  N_("p,dof"),
	  &help_chiinv, gnumeric_chiinv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "chitest",      "rr",  N_("act_range,theo_range"),
	  &help_chitest, gnumeric_chitest, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "confidence",   "fff",  N_("x,stddev,size"),
	  &help_confidence, gnumeric_confidence, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "correl",       "AA",   N_("array1,array2"),
	  &help_correl, gnumeric_correl, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "count", NULL,      N_("number,number,"),
	  &help_count, NULL, gnumeric_count, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "counta", NULL,      N_("number,number,"),
	  &help_counta, NULL, gnumeric_counta, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "covar",        "AA",   N_("array1,array2"),
	  &help_covar, gnumeric_covar, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "critbinom",    "fff",  N_("trials,p,alpha"),
	  &help_critbinom, gnumeric_critbinom, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "devsq", NULL,      N_("number,number,"),
	  &help_devsq, NULL, gnumeric_devsq, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "expondist",    "ffb",  N_("x,y,cumulative"),
	  &help_expondist, gnumeric_expondist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fdist",        "fff",  N_("x,dof of num,dof of denom"),
	  &help_fdist, gnumeric_fdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "finv",         "fff",  N_("p,dof of num,dof of denom"),
	  &help_finv, gnumeric_finv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "fisher",       "f",    N_("number"),
	  &help_fisher, gnumeric_fisher, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "fisherinv",    "f",    N_("number"),
	  &help_fisherinv, gnumeric_fisherinv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "forecast",     "frr",   N_("x,known y's,known x's"),
	  &help_forecast, gnumeric_forecast, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "frequency",    "AA", N_("data_array,bins_array"),
	  &help_frequency, gnumeric_frequency, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ftest",        "rr",   N_("arr1,arr2"),
	  &help_ftest, gnumeric_ftest, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gammadist",    "fffb", N_("number,alpha,beta,cum"),
	  &help_gammadist, gnumeric_gammadist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gammainv",     "fff",   N_("number,alpha,beta"),
	  &help_gammainv, gnumeric_gammainv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gammaln",      "f",    N_("number"),
	  &help_gammaln, gnumeric_gammaln, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "geomean", NULL,      N_("number,number,"),
	  &help_geomean, NULL, gnumeric_geomean, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "growth",       "A|AAb", N_("known_y's,known_x's,new_x's,const"),
	  &help_growth, gnumeric_growth, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "harmean", NULL,      "",
	  &help_harmean, NULL, gnumeric_harmean, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hypgeomdist",  "ffff|b", N_("x,n,M,N,cumulative"),
	  &help_hypgeomdist, gnumeric_hypgeomdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "intercept",    "AA",   N_("number,number,"),
	  &help_intercept, gnumeric_intercept, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "kurt", NULL,      N_("number,number,"),
	  &help_kurt, NULL, gnumeric_kurt, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "large", NULL,      N_("number,number,"),
	  &help_large, NULL, gnumeric_large, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "linest",       "A|Abb",  N_("known_y's,known_x's,const,stat"),
	  &help_linest, gnumeric_linest, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "logest",       "A|Abb",  N_("known_y's,known_x's,const,stat"),
	  &help_logest, gnumeric_logest, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "logfit",       "rr",  N_("known_y's,known_x's"),
	  &help_logfit, gnumeric_logfit, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "loginv",       "fff",  N_("p,mean,stddev"),
	  &help_loginv, gnumeric_loginv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "lognormdist",  "fff",  N_("x,mean,stddev"),
	  &help_lognormdist, gnumeric_lognormdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "logreg",       "A|Abb",  N_("known_y's,known_x's,const,stat"),
	  &help_logreg, gnumeric_logreg, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "max", NULL,      N_("number,number,"),
	  &help_max, NULL, gnumeric_max, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "maxa", NULL,      N_("number,number,"),
	  &help_maxa, NULL, gnumeric_maxa, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "median", NULL,      N_("number,number,"),
	  &help_median, NULL, gnumeric_median, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "min", NULL,      N_("number,number,"),
	  &help_min, NULL, gnumeric_min, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mina", NULL,      N_("number,number,"),
	  &help_mina, NULL, gnumeric_mina, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mode", NULL,      N_("number,number,"),
	  &help_mode, NULL, gnumeric_mode, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "negbinomdist", "fff", N_("f,t,p"),
	  &help_negbinomdist, gnumeric_negbinomdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "normdist",     "fffb",  N_("x,mean,stddev,cumulative"),
	  &help_normdist, gnumeric_normdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "norminv",      "fff",  N_("p,mean,stddev"),
	  &help_norminv, gnumeric_norminv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "normsdist",    "f",  N_("number"),
	  &help_normsdist, gnumeric_normsdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "normsinv",     "f",  N_("p"),
	  &help_normsinv, gnumeric_normsinv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "pearson",      "AA",   N_("array1,array2"),
	  &help_pearson, gnumeric_pearson, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "percentile",   "Af",  N_("array,k"),
	  &help_percentile, gnumeric_percentile, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "percentrank",  "Af|f", N_("array,x,significance"),
	  &help_percentrank, gnumeric_percentrank, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "permut",       "ff",  N_("n,k"),
	  &help_permut, gnumeric_permut, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "poisson",      "ffb",  N_("x,mean,cumulative"),
	  &help_poisson, gnumeric_poisson, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "prob",         "AAf|f", N_("x_range,prob_range,lower_limit,upper_limit"),
	  &help_prob, gnumeric_prob, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "quartile",     "Af",   N_("array,quart"),
	  &help_quartile, gnumeric_quartile, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rank",         "fr|f",      "",
	  &help_rank, gnumeric_rank, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "slope",        "AA", N_("known_y's,known_x's"),
	  &help_slope, gnumeric_slope, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "small", NULL,      N_("number,number,"),
	  &help_small, NULL, gnumeric_small, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "standardize",  "fff",  N_("x,mean,stddev"),
	  &help_standardize, gnumeric_standardize, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "ssmedian",   "A|f",  N_("array,interval"),
	  &help_ssmedian, gnumeric_ssmedian, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "stdev", NULL,      N_("number,number,"),
	  &help_stdev, NULL, gnumeric_stdev, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "stdeva", NULL,      N_("number,number,"),
	  &help_stdeva, NULL, gnumeric_stdeva, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "stdevp", NULL,      N_("number,number,"),
	  &help_stdevp, NULL, gnumeric_stdevp, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "stdevpa", NULL,      N_("number,number,"),
	  &help_stdevpa, NULL, gnumeric_stdevpa, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "steyx",        "AA", N_("known_y's,known_x's"),
	  &help_steyx, gnumeric_steyx, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "rsq",          "AA",   N_("array1,array2"),
	  &help_rsq, gnumeric_rsq, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "skew", NULL,      "",
	  &help_skew, NULL, gnumeric_skew, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tdist",        "fff",    N_("x,dof,tails"),
	  &help_tdist, gnumeric_tdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tinv",         "ff",     N_("p,dof"),
	  &help_tinv, gnumeric_tinv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "trend",        "A|AAb", N_("known_y's,known_x's,new_x's,const"),
	  &help_trend, gnumeric_trend, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "trimmean", NULL,      N_("ref,fraction"),
	  &help_trimmean, NULL, gnumeric_trimmean, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ttest",        "rrff",   N_("array1,array2,tails,type"),
	  &help_ttest, gnumeric_ttest, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "var", NULL,      N_("number,number,"),
	  &help_var, NULL, gnumeric_var, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "vara", NULL,      N_("number,number,"),
	  &help_vara, NULL, gnumeric_vara, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "varp", NULL,      N_("number,number,"),
	  &help_varp, NULL, gnumeric_varp, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "varpa", NULL,      N_("number,number,"),
	  &help_varpa, NULL, gnumeric_varpa, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "weibull",      "fffb",  N_("x.alpha,beta,cumulative"),
	  &help_weibull, gnumeric_weibull, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ztest", NULL,      N_("ref,x"),
	  &help_ztest, NULL, gnumeric_ztest, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "exppowdist", "fff", N_("x,a,b"),         &help_exppowdist,
	  gnumeric_exppowdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "geomdist", "ffb", N_("k,p"),    &help_geomdist,
	  gnumeric_geomdist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "kurtp", NULL,      N_("number,number,"),
	  &help_kurtp, NULL, gnumeric_kurtp, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "landau", "f", N_("x"), &help_landau,
	  gnumeric_landau, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "laplace", "ff", N_("x,a"), &help_laplace,
	  gnumeric_laplace, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "logistic", "ff", N_("x,a"), &help_logistic,
	  gnumeric_logistic, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "pareto", "fff", N_("x,a,b"), &help_pareto,
	  gnumeric_pareto, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "rayleigh", "ff", N_("x,sigma"), &help_rayleigh,
	  gnumeric_rayleigh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "rayleightail", "fff", N_("x,a,sigma"), &help_rayleightail,
	  gnumeric_rayleightail, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "skewp", NULL,      "",
	  &help_skewp, NULL, gnumeric_skewp, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "subtotal", NULL,  N_("function_nbr,ref,ref,"),
	  &help_subtotal,    NULL, gnumeric_subtotal, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "cronbach", NULL,      N_("ref,ref,"),
	  &help_cronbach, NULL, gnumeric_cronbach, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        {NULL}
};
