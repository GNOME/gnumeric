/*
 * fn-stat.c:  Built in statistical functions and functions registration
 *
 * Authors:
 *  Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *  Michael Meeks <michael@imaginator.com>
 *  Morten Welinder <terra@diku.dk>
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>

#include <func-util.h>
#include <parse-util.h>
#include <mathfunc.h>
#include <rangefunc.h>
#include <regression.h>
#include <sheet.h>
#include <cell.h>
#include <collect.h>
#include <auto-format.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static gint
float_compare (const gnum_float *a, const gnum_float *b)
{
        if (*a < *b)
                return -1;
	else if (*a == *b)
	        return 0;
	else
	        return 1;
}

void
setup_stat_closure (stat_closure_t *cl)
{
	cl->N = 0;
	cl->M = 0.0;
	cl->Q = 0.0;
	cl->afun_flag = 0;
	cl->sum = 0.0;
}

/**
 * FIXME : this is also a kludge, but at least it is more localized
 * than before.  We should not have to do this
 */
static Value *
stat_helper (stat_closure_t *cl, EvalPos const *ep, Value *val)
{
	ExprTree *tree;
	ExprList *expr_node_list;
	Value *err;

	setup_stat_closure (cl);

	tree = expr_tree_new_constant (val);
	expr_node_list = expr_list_append (NULL, tree);
	err = function_iterate_argument_values (ep,
		&callback_function_stat, cl, expr_node_list,
		TRUE, FALSE);
	expr_list_free (expr_node_list);
	g_free (tree); /* ICK!  this is necessary to avoid freeing the value */

	if (err != NULL)
		return err;
	if (cl->N <= 1)
		return value_new_error (ep, gnumeric_err_VALUE);
	return NULL;
}

Value *
callback_function_stat (EvalPos const *ep, Value *value, void *closure)
{
	stat_closure_t *mm = closure;
	gnum_float x, dx, dm;

	if (value != NULL && VALUE_IS_NUMBER (value))
		x = value_get_as_float (value);
	else {
	        if (mm->afun_flag)
		        x = 0;
		else
		        return NULL;
	}

	/* i'm paranoid - if mm->N == -1, mm->N + 1 is 0 and the next line blows out */
	if (mm->N == - 1)
	        return value_new_error (ep, gnumeric_err_NUM);

	dx = x - mm->M;
	dm = dx / (mm->N + 1);
	mm->M += dm;
	mm->Q += mm->N * dx * dm;
	mm->N++;
	mm->sum += x;

	return NULL;
}

typedef struct {
        GSList    *entries;
        int       n;
} make_list_t;

static Value *
callback_function_make_list (EvalPos const *ep, Value *value,
			     void *closure)
{
	make_list_t *mm = closure;
	gnum_float  x;
	gpointer    p;

	if (value != NULL && VALUE_IS_NUMBER (value))
		x = value_get_as_float (value);
	else
	        x = 0.;

	p = g_new (gnum_float, 1);
	*((gnum_float *) p) = x;
	mm->entries = g_slist_append (mm->entries, p);
	mm->n++;

	return NULL;
}

/**
 * FIXME : this is a kludge.
 * I've trimmed the code to avoid replicating it, but this is a stupid way to
 * do this.
 */
static Value *
make_list (make_list_t *p, EvalPos const *ep, Value *val)
{
	ExprTree *tree;
	ExprList *expr_node_list;
	Value *err;

        p->n = 0;
	p->entries = NULL;

	tree = expr_tree_new_constant (val);
	expr_node_list = expr_list_append (NULL, tree);
	err = function_iterate_argument_values (ep,
		&callback_function_make_list, p, expr_node_list,
		TRUE, FALSE);
	expr_list_free (expr_node_list);
	g_free (tree); /* ICK!  this is necessary to avoid freeing the value */
	return err;
}

/***************************************************************************/

static char *help_varp = {
	N_("@FUNCTION=VARP\n"
	   "@SYNTAX=VARP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VARP calculates the variance of a set of numbers "
	   "where each number is a member of a population "
	   "and the set is the entire population."
	   "\n"
	   "(VARP is also known as the N-variance.)"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "VARP(A1:A5) equals 94.112.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,DVAR,DVARP,STDEV,VAR")
};

Value *
gnumeric_varp (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_var_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_var = {
	N_("@FUNCTION=VAR\n"
	   "@SYNTAX=VAR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VAR estimates the variance of a sample of a population. "
	   "To get the true variance of a complete population use @VARP."
	   "\n"
	   "(VAR is also known as the N-1-variance.  Under reasonable "
	   "conditions, it is the maximum-likelihood estimator for the "
	   "true variance.)\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "VAR(A1:A5) equals 117.64.\n"
	   "\n"
	   "@SEEALSO=VARP,STDEV")
};

Value *
gnumeric_var (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_var_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_stdev = {
	N_("@FUNCTION=STDEV\n"
	   "@SYNTAX=STDEV(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEV returns standard deviation of a set of numbers "
	   "treating these numbers as members of a population.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "STDEV(A1:A5) equals 10.84619749.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,DSTDEV,DSTDEVP,STDEVA,STDEVPA,VAR")
};

Value *
gnumeric_stdev (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_stddev_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_stdevp = {
	N_("@FUNCTION=STDEVP\n"
	   "@SYNTAX=STDEVP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEVP returns standard deviation of a set of numbers "
	   "treating these numbers as members of a complete population.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "STDEVP(A1:A5) equals 9.701133954.\n"
	   "\n"
	   "@SEEALSO=STDEV,STDEVA,STDEVPA")
};

Value *
gnumeric_stdevp (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_stddev_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_rank = {
	N_("@FUNCTION=RANK\n"
	   "@SYNTAX=RANK(x,ref[,order])\n"

	   "@DESCRIPTION="
	   "RANK returns the rank of a number in a list of numbers.  @x is "
	   "the number whose rank you want to find, @ref is the list of "
	   "numbers, and @order specifies how to rank numbers.  If @order is "
	   "0, numbers are ranked in descending order, otherwise numbers are "
	   "ranked in ascending order.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "RANK(17.3,A1:A5) equals 4.\n"
	   "\n"
	   "@SEEALSO=PERCENTRANK")
};

typedef struct {
        gnum_float x;
        int     order;
        int     rank;
} stat_rank_t;

static Value *
callback_function_rank (Sheet *sheet, int col, int row,
			Cell *cell, void *user_data)
{
        stat_rank_t *p = user_data;
	gnum_float  x;

	if (cell == NULL || cell->value == NULL)
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
	case VALUE_EMPTY:
        default:
                return value_terminate ();
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

static Value *
gnumeric_rank (FunctionEvalInfo *ei, Value **argv)
{
	stat_rank_t p;
	Value      *ret;

	p.x = value_get_as_float (argv[0]);
	if (argv[2])
	        p.order = value_get_as_int (argv[2]);
	else
	        p.order = 0;
	p.rank = 1;
	ret = sheet_foreach_cell_in_range (
	        eval_sheet (argv[1]->v_range.cell.a.sheet,
			    ei->pos->sheet), TRUE,
		argv[1]->v_range.cell.a.col,
		argv[1]->v_range.cell.a.row,
		argv[1]->v_range.cell.b.col,
		argv[1]->v_range.cell.b.row,
		callback_function_rank,
		&p);

	if (ret != NULL)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_int (p.rank);
}

/***************************************************************************/

static char *help_trimmean = {
	N_("@FUNCTION=TRIMMEAN\n"
	   "@SYNTAX=TRIMMEAN(ref,fraction)\n"

	   "@DESCRIPTION="
	   "TRIMMEAN returns the mean of the interior of a data set. @ref "
	   "is the list of numbers whose mean you want to calculate and "
	   "@fraction is the fraction of the data set excluded from the mean. "
	   "For example, if @fraction=0.2 and the data set contains 40 "
	   "numbers, 8 numbers are trimmed from the data set (40 x 0.2), 4 "
	   "from the top and 4 from the bottom of the set.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "TRIMMEAN(A1:A5,0.2) equals 23.2.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,GEOMEAN,HARMEAN,MEDIAN,MODE")
};

static int
range_trimmean (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float p, sum = 0;
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
	qsort ((gnum_float *) xs, n, sizeof (xs[0]), (void *) &float_compare);

	for (i = tc; i < n - tc; i++)
		sum += xs[i];

	*res = sum / c;
	return 0;
}

static Value *
gnumeric_trimmean (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list, ei,
				     range_trimmean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_covar = {
	N_("@FUNCTION=COVAR\n"
	   "@SYNTAX=COVAR(array1,array2)\n"

	   "@DESCRIPTION="
	   "COVAR returns the covariance of two data sets."
	   "\n"
	   "Strings and empty cells are simply ignored.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "COVAR(A1:A5,B1:B5) equals 65.858.\n"
	   "\n"
	   "@SEEALSO=CORREL,FISHER,FISHERINV")
};

static Value *
gnumeric_covar (FunctionEvalInfo *ei, Value **argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      range_covar,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_correl = {
	N_("@FUNCTION=CORREL\n"
	   "@SYNTAX=CORREL(array1,array2)\n"

	   "@DESCRIPTION="
	   "CORREL returns the correlation coefficient of two data sets."
	   "\n"
	   "Strings and empty cells are simply ignored.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "CORREL(A1:A5,B1:B5) equals 0.996124788.\n"
	   "\n"
	   "@SEEALSO=COVAR,FISHER,FISHERINV")
};

static Value *
gnumeric_correl (FunctionEvalInfo *ei, Value **argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      range_correl_pop,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_negbinomdist = {
	N_("@FUNCTION=NEGBINOMDIST\n"
	   "@SYNTAX=NEGBINOMDIST(f,t,p)\n"

	   "@DESCRIPTION="
	   "NEGBINOMDIST function returns the negative binomial "
	   "distribution. @f is the number of failures, @t is the threshold "
	   "number of successes, and @p is the probability of a success."
	   "\n"
	   "If @f or @t is a non-integer it is truncated. "
	   "If (@f + @t -1) <= 0 NEGBINOMDIST returns #NUM! error. "
	   "If @p < 0 or @p > 1 NEGBINOMDIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "NEGBINOMDIST(2,5,0.55) equals 0.152872629.\n"
	   "\n"
	   "@SEEALSO=BINOMDIST,COMBIN,FACT,HYPGEOMDIST,PERMUT")
};

static Value *
gnumeric_negbinomdist (FunctionEvalInfo *ei, Value **argv)
{
	int x, r;
	gnum_float p;

	x = value_get_as_int (argv[0]);
	r = value_get_as_int (argv[1]);
	p = value_get_as_float (argv[2]);

	if ((x + r - 1) <= 0 || p < 0 || p > 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (combin (x + r - 1, r - 1) *
				pow (p, r) * pow (1 - p, x));
}

/***************************************************************************/

static char *help_normsdist = {
        N_("@FUNCTION=NORMSDIST\n"
           "@SYNTAX=NORMSDIST(x)\n"

           "@DESCRIPTION="
           "NORMSDIST function returns the standard normal cumulative "
	   "distribution. @x is the value for which you want the distribution.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "NORMSDIST(2) equals 0.977249868.\n"
	   "\n"
           "@SEEALSO=NORMDIST")
};

static Value *
gnumeric_normsdist (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float x;

        x = value_get_as_float (argv[0]);

	return value_new_float (pnorm (x, 0, 1));
}

/***************************************************************************/

static char *help_normsinv = {
        N_("@FUNCTION=NORMSINV\n"
           "@SYNTAX=NORMSINV(p)\n"

           "@DESCRIPTION="
           "NORMSINV function returns the inverse of the standard normal "
	   "cumulative distribution. @p is the given probability "
	   "corresponding to the normal distribution.\n"
	   "If @p < 0 or @p > 1 NORMSINV returns #NUM! error. "
	   "\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "NORMSINV(0.2) equals -0.841621234.\n"
	   "\n"
           "@SEEALSO=NORMDIST,NORMINV,NORMSDIST,STANDARDIZE,ZTEST")
};


static Value *
gnumeric_normsinv (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float p;

        p = value_get_as_float (argv[0]);
	if (p < 0 || p > 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qnorm (p, 0, 1));
}

/***************************************************************************/

static char *help_lognormdist = {
        N_("@FUNCTION=LOGNORMDIST\n"
           "@SYNTAX=LOGNORMDIST(x,mean,stdev)\n"

           "@DESCRIPTION="
           "LOGNORMDIST function returns the lognormal distribution. "
	   "@x is the value for which you want the distribution, @mean is "
	   "the mean of the distribution, and @stdev is the standard "
	   "deviation of the distribution.\n"
	   "If @stdev = 0 LOGNORMDIST returns #DIV/0! error. "
	   "If @x <= 0, @mean < 0 or @stdev < 0 LOGNORMDIST returns #NUM! "
	   "error.\n"
           "This function is Excel compatible. "
           "\n"
	   "@EXAMPLES=\n"
	   "LOGNORMDIST(3,1,2) equals 0.519662338.\n"
	   "\n"
           "@SEEALSO=NORMDIST")
};

static Value *
gnumeric_lognormdist (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float x, mean, stdev;

        x = value_get_as_float (argv[0]);
        mean = value_get_as_float (argv[1]);
        stdev = value_get_as_float (argv[2]);

        if (stdev == 0)
                return value_new_error (ei->pos, gnumeric_err_DIV0);

        if (x <= 0 || mean < 0 || stdev < 0)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (plnorm (x, mean, stdev));
}

/***************************************************************************/

static char *help_loginv = {
        N_("@FUNCTION=LOGINV\n"
           "@SYNTAX=LOGINV(p,mean,stdev)\n"

           "@DESCRIPTION="
           "LOGINV function returns the inverse of the lognormal "
	   "cumulative distribution. @p is the given probability "
	   "corresponding to the normal distribution, @mean is the "
	   "arithmetic mean of the distribution, and @stdev is the "
	   "standard deviation of the distribution."
           "\n"
	   "If @p < 0 or @p > 1 or @stdev <= 0 LOGINV returns #NUM! error.\n"
	   "This function is Excel compatible."
 	   "\n"
	   "@EXAMPLES=\n"
	   "LOGINV(0.5,2,3) equals 7.389056099.\n"
	   "\n"
           "@SEEALSO=EXP,LN,LOG,LOG10,LOGNORMDIST")
};

static Value *
gnumeric_loginv (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float p, mean, stdev;

        p = value_get_as_float (argv[0]);
        mean = value_get_as_float (argv[1]);
        stdev = value_get_as_float (argv[2]);

	if (p < 0 || p > 1 || stdev <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qlnorm (p, mean, stdev));
}

/***************************************************************************/

static char *help_fisherinv = {
        N_("@FUNCTION=FISHERINV\n"
           "@SYNTAX=FISHERINV(x)\n"

           "@DESCRIPTION="
           "FISHERINV function returns the inverse of the Fisher "
	   "transformation at @x. "
           "\n"
           "If @x is non-number FISHERINV returns #VALUE! error.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "FISHERINV(2) equals 0.96402758.\n"
	   "\n"
           "@SEEALSO=FISHER")
};

static Value *
gnumeric_fisherinv (FunctionEvalInfo *ei, Value **argv)
{
       gnum_float y;

       y = value_get_as_float (argv[0]);
       return value_new_float ((exp (2 * y) - 1.0) / (exp (2 * y) + 1.0));
}

/***************************************************************************/

static char *help_mode = {
        N_("@FUNCTION=MODE\n"
           "@SYNTAX=MODE(n1, n2, ...)\n"

           "@DESCRIPTION="
           "MODE returns the most common number of the data set. If the data "
	   "set has many most common numbers MODE returns the first one of "
	   "them. "
           "\n"
           "Strings and empty cells are simply ignored. "
	   "If the data set does not contain any duplicates MODE returns "
           "#N/A error.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 11.4, 25.9, and 40.1.  Then\n"
	   "MODE(A1:A5) equals 11.4.\n"
	   "\n"
           "@SEEALSO=AVERAGE,MEDIAN")
};

static Value *
gnumeric_mode (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_mode,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_NA);
}

/***************************************************************************/

static char *help_harmean = {
	N_("@FUNCTION=HARMEAN\n"
	   "@SYNTAX=HARMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "HARMEAN returns the harmonic mean of the N data points "
	   "(that is, N divided by the sum of the inverses of "
	   "the data points).\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "HARMEAN(A1:A5) equals 19.529814427.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,GEOMEAN,MEDIAN,MODE,TRIMMEAN")
};

static Value *
gnumeric_harmean (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_harmonic_mean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_geomean = {
	N_("@FUNCTION=GEOMEAN\n"
	   "@SYNTAX=GEOMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "GEOMEAN returns the geometric mean of the given arguments. "
	   "This is equal to the Nth root of the product of the terms.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "GEOMEAN(A1:A5) equals 21.279182482.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,HARMEAN,MEDIAN,MODE,TRIMMEAN")
};

static Value *
gnumeric_geomean (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_geometric_mean,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_count = {
	N_("@FUNCTION=COUNT\n"
	   "@SYNTAX=COUNT(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "COUNT returns the total number of integer or floating point "
	   "arguments passed.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "COUNT(A1:A5) equals 5.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static Value *
callback_function_count (EvalPos const *ep, Value *value, void *closure)
{
	Value *result = (Value *) closure;

	if (value &&
	    (value->type == VALUE_INTEGER || value->type == VALUE_FLOAT))
		result->v_int.val++;
	return NULL;
}

Value *
gnumeric_count (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	Value *result = value_new_int (0);

	/* no need to check for error, this is not strict */
	function_iterate_argument_values (ei->pos,
		callback_function_count, result, expr_node_list,
		FALSE, TRUE);

	return result;
}

/***************************************************************************/

static char *help_counta = {
        N_("@FUNCTION=COUNTA\n"
           "@SYNTAX=COUNTA(b1, b2, ...)\n"

           "@DESCRIPTION="
           "COUNTA returns the number of arguments passed not including "
	   "empty cells.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, \"missing\", \"missing\", 25.9, and 40.1.  "
	   "Then\n"
	   "COUNTA(A1:A5) equals 5.\n"
	   "\n"
           "@SEEALSO=AVERAGE,COUNT,DCOUNT,DCOUNTA,PRODUCT,SUM")
};

static Value *
callback_function_counta (EvalPos const *ep, Value *value, void *closure)
{
        Value *result = (Value *) closure;

	result->v_int.val++;
	return NULL;
}

Value *
gnumeric_counta (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
        Value *result = value_new_int (0);

	/* no need to check for error, this is not strict */
        function_iterate_argument_values (ei->pos,
		callback_function_counta, result, expr_node_list,
		FALSE, TRUE);

        return result;
}

/***************************************************************************/

static char *help_average = {
	N_("@FUNCTION=AVERAGE\n"
	   "@SYNTAX=AVERAGE(value1, value2,...)\n"

	   "@DESCRIPTION="
	   "AVERAGE computes the average of all the values and cells "
	   "referenced in the argument list.  This is equivalent to the "
	   "sum of the arguments divided by the count of the arguments.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "AVERAGE(A1:A5) equals 23.2.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT")
};

Value *
gnumeric_average (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_average,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_min = {
	N_("@FUNCTION=MIN\n"
	   "@SYNTAX=MIN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MIN returns the value of the element of the values passed "
	   "that has the smallest value. With negative numbers considered "
	   "smaller than positive numbers.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "MIN(A1:A5) equals 11.4.\n"
	   "\n"
	   "@SEEALSO=MAX,ABS")
};

static int
range_min0 (const gnum_float *xs, int n, gnum_float *res)
{
	if (n == 0) {
		*res = 0;
		return 0;
	} else
		return range_min (xs, n, res);
}

Value *
gnumeric_min (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_min0,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_max = {
	N_("@FUNCTION=MAX\n"
	   "@SYNTAX=MAX(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MAX returns the value of the element of the values passed "
	   "that has the largest value. With negative numbers considered "
	   "smaller than positive numbers.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "MAX(A1:A5) equals 40.1.\n"
	   "\n"
	   "@SEEALSO=MIN,ABS")
};

static int
range_max0 (const gnum_float *xs, int n, gnum_float *res)
{
	if (n == 0) {
		*res = 0;
		return 0;
	} else
		return range_max (xs, n, res);
}

Value *
gnumeric_max (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_max0,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_skew = {
	N_("@FUNCTION=SKEW\n"
	   "@SYNTAX=SKEW(n1, n2, ...)\n"

	   "@DESCRIPTION="
	   "SKEW returns an unbiased estimate for skewness of a distribution."
	   "\n"
	   "Note, that this is only meaningful if the underlying "
	   "distribution really has a third moment.  The skewness of a "
	   "symmetric (e.g., normal) distribution is zero."
           "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "If less than three numbers are given, SKEW returns #DIV/0! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "SKEW(A1:A5) equals 0.976798268.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,VAR,SKEWP,KURT")
};

static Value *
gnumeric_skew (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_skew_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_skewp = {
	N_("@FUNCTION=SKEWP\n"
	   "@SYNTAX=SKEWP(n1, n2, ...)\n"

	   "@DESCRIPTION="
	   "SKEWP returns the population skewness of a data set."
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "If less than two numbers are given, SKEWP returns #DIV/0! error."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "SKEWP(A1:A5) equals 0.655256198.\n"
	   "\n"
	   "@SEEALSO=AVERAGE,VARP,SKEW,KURTP")
};

static Value *
gnumeric_skewp (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_skew_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_expondist = {
	N_("@FUNCTION=EXPONDIST\n"
	   "@SYNTAX=EXPONDIST(x,y,cumulative)\n"

	   "@DESCRIPTION="
	   "EXPONDIST function returns the exponential distribution. "
	   "If the @cumulative boolean is false it will return: "
	   "@y * exp (-@y*@x), otherwise it will return 1 - exp (-@y*@x)."
	   "\n"
	   "If @x < 0 or @y <= 0 this will return an error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "EXPONDIST(2,4,0) equals 0.001341851.\n"
	   "\n"
	   "@SEEALSO=POISSON")
};

static Value *
gnumeric_expondist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x, y;
	int cuml;
	gboolean err;

	x = value_get_as_float (argv[0]);
	y = value_get_as_float (argv[1]);

	if (x < 0.0 || y <= 0.0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	cuml = value_get_as_bool (argv[2], &err);
	if (err)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (cuml)
		return value_new_float (pexp (x, 1 / y));
	else
		return value_new_float (dexp (x, 1 / y));
}

/***************************************************************************/

static char *help_gammaln = {
	N_("@FUNCTION=GAMMALN\n"
	   "@SYNTAX=GAMMALN(x)\n"

	   "@DESCRIPTION="
	   "GAMMALN function returns the natural logarithm of the "
	   "gamma function."
	   "\n"
	   "If @x is non-number then GAMMALN returns #VALUE! error. "
	   "If @x <= 0 then GAMMALN returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "GAMMALN(23) equals 48.471181352.\n"
	   "\n"
	   "@SEEALSO=POISSON")
};

static Value *
gnumeric_gammaln (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x;

	/* FIXME: the gamma function is defined for all real numbers except
	 * the integers 0, -1, -2, ...  It is positive (and log(gamma(x)) is
	 * thus defined) when x>0 or -2<x<-1 or -4<x<-3 ...  */

	x = value_get_as_float (argv[0]);

	if (x <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (lgamma (x));
}

/***************************************************************************/

static char *help_gammadist = {
	N_("@FUNCTION=GAMMADIST\n"
	   "@SYNTAX=GAMMADIST(x,alpha,beta,cum)\n"

	   "@DESCRIPTION="
	   "GAMMADIST function returns the gamma distribution. If @cum "
	   "is TRUE, GAMMADIST returns the incomplete gamma function, "
	   "otherwise it returns the probability mass function."
	   "\n"
	   "If @x < 0 GAMMADIST returns #NUM! error. "
	   "If @alpha <= 0 or @beta <= 0, GAMMADIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "GAMMADIST(1,2,3,0) equals 0.07961459.\n"
	   "\n"
	   "@SEEALSO=GAMMAINV")
};

static Value *
gnumeric_gammadist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x, alpha, beta;
	int     cum;

	x = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);

	if (x < 0 || alpha <= 0 || beta <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	cum = value_get_as_int (argv[3]);
	if (cum)
	        return value_new_float (pgamma (x, alpha, beta));
	else
	        return value_new_float (dgamma (x, alpha, beta));
}

/***************************************************************************/

static char *help_gammainv = {
        N_("@FUNCTION=GAMMAINV\n"
           "@SYNTAX=GAMMAINV(p,alpha,beta)\n"

           "@DESCRIPTION="
           "GAMMAINV function returns the inverse of the cumulative "
	   "gamma distribution."
           "\n"
	   "If @p < 0 or @p > 1 GAMMAINV returns #NUM! error. "
	   "If @alpha <= 0 or @beta <= 0 GAMMAINV returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "GAMMAINV(0.34,2,4) equals 4.829093908.\n"
	   "\n"
           "@SEEALSO=GAMMADIST")
};

static Value *
gnumeric_gammainv (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float p, alpha, beta;

        p = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);

	if (p < 0 || p > 1 || alpha <= 0 || beta <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qgamma (p, alpha, beta));
}

/***************************************************************************/

static char *help_chidist = {
	N_("@FUNCTION=CHIDIST\n"
	   "@SYNTAX=CHIDIST(x,dof)\n"

	   "@DESCRIPTION="
	   "CHIDIST function returns the one-tailed probability of the "
	   "chi-squared distribution. @dof is the number of degrees of "
	   "freedom."
	   "\n"
	   "If @dof is non-integer it is truncated.  "
	   "If @dof < 1 CHIDIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "CHIDIST(5.3,2) equals 0.070651213.\n"
	   "\n"
	   "@SEEALSO=CHIINV,CHITEST")
};

static Value *
gnumeric_chidist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x;
	int     dof;

	x = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);

	if (dof < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (1.0 - pchisq (x, dof));
}

/***************************************************************************/

static char *help_chiinv = {
        N_("@FUNCTION=CHIINV\n"
           "@SYNTAX=CHIINV(p,dof)\n"

           "@DESCRIPTION="
           "CHIINV function returns the inverse of the one-tailed "
	   "probability of the chi-squared distribution."
           "\n"
	   "If @p < 0 or @p > 1 or @dof < 1 CHIINV returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "CHIINV(0.98,7) equals 1.564293004.\n"
	   "\n"
           "@SEEALSO=CHIDIST,CHITEST")
};

static Value *
gnumeric_chiinv (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float p;
	int dof;

        p = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);

	if (p < 0 || p > 1 || dof < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qchisq (1.0 - p, dof));
}

/***************************************************************************/

static char *help_chitest = {
        N_("@FUNCTION=CHITEST\n"
           "@SYNTAX=CHITEST(actual_range,theoretical_range)\n"

           "@DESCRIPTION="
           "CHITEST function returns the test for independence of "
	   "chi-squared distribution."
           "\n"
	   "@actual_range is a range that contains the observed data points. "
	   "@theoretical_range is a range that contains the expected values "
	   "of the data points.\n"
	   "This function is Excel compatible."
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

static Value *
callback_function_chitest_actual (EvalPos const *ep, Value *value,
				  void *closure)
{
	stat_chitest_t *mm = closure;
	gnum_float     *p;

	if (!VALUE_IS_NUMBER (value))
		return value_terminate ();

	p = g_new (gnum_float, 1);
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
        gnum_float sum;
} stat_chitest_t_t;

static Value *
callback_function_chitest_theoretical (EvalPos const *ep, Value *value,
				       void *closure)
{
	stat_chitest_t_t *mm = closure;
	gnum_float a, e, *p;

	if (!VALUE_IS_NUMBER (value))
		return value_terminate ();

	e = value_get_as_float (value);

	if (mm->current_cell == NULL) {
	        mm->current_cell = mm->next_col->data;
		mm->next_col = mm->next_col->next;
	}
	p = mm->current_cell->data;
	a = *p;

	if (e == 0)
	        return value_new_error (ep, gnumeric_err_NUM);

	mm->sum += ((a-e) * (a-e)) / e;
	g_free (p);
	mm->current_cell = mm->current_cell->next;

	return NULL;
}

static Value *
gnumeric_chitest (FunctionEvalInfo *ei, Value **argv)
{
	stat_chitest_t   p1;
	stat_chitest_t_t p2;
	GSList           *tmp;
	Value           *ret;

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
		return value_new_error (ei->pos, gnumeric_err_NUM);

	ret = function_iterate_do_value (ei->pos,
		(FunctionIterateCB) callback_function_chitest_actual,
		&p1, argv[0], TRUE, TRUE);
	if (ret != NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	p2.sum = 0;
	p2.current_cell = p1.columns->data;
	p2.next_col = p1.columns->next;
	ret = function_iterate_do_value (ei->pos,
		(FunctionIterateCB) callback_function_chitest_theoretical,
		&p2, argv[1], TRUE, TRUE);
	if (ret != NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	for (tmp = p1.columns; tmp != NULL ; tmp = tmp->next)
	        g_slist_free (tmp->data);
	g_slist_free (p1.columns);

	/* FIXME : XL docs claim df = (r-1)(c-1) not (r-1),
	 * However, that makes no sense.
	 */
	return value_new_float (1. - pchisq (p2.sum, (p1.rows - 1)));
}

/***************************************************************************/

static char *help_betadist = {
	N_("@FUNCTION=BETADIST\n"
	   "@SYNTAX=BETADIST(x,alpha,beta[,a,b])\n"

	   "@DESCRIPTION="
	   "BETADIST function returns the cumulative beta distribution. @a "
	   "is the optional lower bound of @x and @b is the optional upper "
	   "bound of @x.  If @a is not given, BETADIST uses 0.  If @b is "
	   "not given, BETADIST uses 1."
	   "\n"
	   "If @x < @a or @x > @b BETADIST returns #NUM! error. "
	   "If @alpha <= 0 or @beta <= 0, BETADIST returns #NUM! error. "
	   "If @a >= @b BETADIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "BETADIST(0.12,2,3) equals 0.07319808.\n"
	   "\n"
	   "@SEEALSO=BETAINV")
};

static Value *
gnumeric_betadist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x, alpha, beta, a, b;

	x = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);
	a = argv[3] ? value_get_as_float (argv[3]) : 0;
	b = argv[4] ? value_get_as_float (argv[4]) : 1;

	if (x < a || x > b || a >= b || alpha <= 0 || beta <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (pbeta ((x - a) / (b - a), alpha, beta));
}

/***************************************************************************/

static char *help_betainv = {
	N_("@FUNCTION=BETAINV\n"
	   "@SYNTAX=BETAINV(p,alpha,beta[,a,b])\n"

	   "@DESCRIPTION="
	   "BETAINV function returns the inverse of cumulative beta "
	   "distribution.  @a is the optional lower bound of @x and @b "
	   "is the optinal upper bound of @x.  If @a is not given, "
	   "BETAINV uses 0.  If @b is not given, BETAINV uses 1."
	   "\n"
	   "If @p < 0 or @p > 1 BETAINV returns #NUM! error. "
	   "If @alpha <= 0 or @beta <= 0, BETAINV returns #NUM! error. "
	   "If @a >= @b BETAINV returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "BETAINV(0.45,1.6,1) equals 0.607096629.\n"
	   "\n"
	   "@SEEALSO=BETADIST")
};

static Value *
gnumeric_betainv (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float p, alpha, beta, a, b;

	p = value_get_as_float (argv[0]);
	alpha = value_get_as_float (argv[1]);
	beta = value_get_as_float (argv[2]);
	a = argv[3] ? value_get_as_float (argv[3]) : 0;
	b = argv[4] ? value_get_as_float (argv[4]) : 1;

	if (p < 0 || p > 1 || a >= b || alpha <= 0 || beta <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float ((b - a) * qbeta (p, alpha, beta) + a);
}

/***************************************************************************/

static char *help_tdist = {
	N_("@FUNCTION=TDIST\n"
	   "@SYNTAX=TDIST(x,dof,tails)\n"

	   "@DESCRIPTION="
	   "TDIST function returns the Student's t-distribution. @dof is "
	   "the degree of freedom and @tails is 1 or 2 depending on whether "
	   "you want one-tailed or two-tailed distribution."
	   "\n"
	   "If @dof < 1 TDIST returns #NUM! error.  "
	   "If @tails is neither 1 or 2 TDIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "TDIST(2,5,1) equals 0.050969739.\n"
	   "\n"
	   "@SEEALSO=TINV,TTEST")
};

static Value *
gnumeric_tdist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x;
	int     dof, tails;

	x = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);
	tails = value_get_as_int (argv[2]);

	if (dof < 1 || (tails != 1 && tails != 2))
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (tails == 1)
	        return value_new_float (1.0 - pt (x, dof));
	else
	        return value_new_float ((1.0 - pt (x, dof)) * 2);
}

/***************************************************************************/

static char *help_tinv = {
        N_("@FUNCTION=TINV\n"
           "@SYNTAX=TINV(p,dof)\n"

           "@DESCRIPTION="
           "TINV function returns the inverse of the two-tailed Student's "
	   "t-distribution."
           "\n"
	   "If @p < 0 or @p > 1 or @dof < 1 TINV returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "TINV(0.4,32) equals 0.852998454.\n"
	   "\n"
           "@SEEALSO=TDIST,TTEST")
};

static Value *
gnumeric_tinv (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float p;
	int dof;

        p = value_get_as_float (argv[0]);
	dof = value_get_as_int (argv[1]);

	if (p < 0 || p > 1 || dof < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qt (1 - p / 2, dof));
}

/***************************************************************************/

static char *help_fdist = {
	N_("@FUNCTION=FDIST\n"
	   "@SYNTAX=FDIST(x,dof1,dof2)\n"

	   "@DESCRIPTION="
	   "FDIST function returns the F probability distribution. @dof1 "
	   "is the numerator degrees of freedom and @dof2 is the denominator "
	   "degrees of freedom."
	   "\n"
	   "If @x < 0 FDIST returns #NUM! error.  "
	   "If @dof1 < 1 or @dof2 < 1, FDIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "FDIST(2,5,5) equals 0.232511319.\n"
	   "\n"
	   "@SEEALSO=FINV")
};

static Value *
gnumeric_fdist (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x;
	int     dof1, dof2;

	x = value_get_as_float (argv[0]);
	dof1 = value_get_as_int (argv[1]);
	dof2 = value_get_as_int (argv[2]);

	if (x < 0 || dof1 < 1 || dof2 < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (1.0 - pf (x, dof1, dof2));
}

/***************************************************************************/

static char *help_finv = {
        N_("@FUNCTION=FINV\n"
           "@SYNTAX=FINV(p,dof1,dof2)\n"

           "@DESCRIPTION="
           "FINV function returns the inverse of the F probability "
	   "distribution."
           "\n"
	   "If @p < 0 or @p > 1 FINV returns #NUM! error. "
	   "If @dof1 < 1 or @dof2 < 1 FINV returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "FINV(0.2,2,4) equals 2.472135955.\n"
	   "\n"
           "@SEEALSO=FDIST")
};

static Value *
gnumeric_finv (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float p;
	int dof1, dof2;

        p = value_get_as_float (argv[0]);
	dof1 = value_get_as_int (argv[1]);
	dof2 = value_get_as_int (argv[2]);

	if (p < 0 || p > 1 || dof1 < 1 || dof2 < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qf (1.0 - p, dof1, dof2));
}

/***************************************************************************/

static char *help_binomdist = {
	N_("@FUNCTION=BINOMDIST\n"
	   "@SYNTAX=BINOMDIST(n,trials,p,cumulative)\n"

	   "@DESCRIPTION="
	   "BINOMDIST function returns the binomial distribution. "
	   "@n is the number of successes, @trials is the total number of "
           "independent trials, @p is the probability of success in trials, "
           "and @cumulative describes whether to return the sum of the"
           "binomial function from 0 to @n."
	   "\n"
	   "If @n or @trials are non-integer they are truncated. "
	   "If @n < 0 or @trials < 0 BINOMDIST returns #NUM! error. "
	   "If @n > trials BINOMDIST returns #NUM! error. "
	   "If @p < 0 or @p > 1 BINOMDIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "BINOMDIST(3,5,0.8,0) equals 0.2048.\n"
	   "\n"
	   "@SEEALSO=POISSON")
};

static Value *
gnumeric_binomdist (FunctionEvalInfo *ei, Value **argv)
{
	int n, trials, cuml;
	gboolean err;
	gnum_float p;

	n = value_get_as_int (argv[0]);
	trials = value_get_as_int (argv[1]);
	p = value_get_as_float (argv[2]);
	cuml = value_get_as_bool (argv[3], &err);

	if (n < 0 || trials < 0 || p < 0 || p > 1 || n > trials || err)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (cuml)
		return value_new_float (pbinom (n, trials, p));
	else
		return value_new_float (dbinom (n, trials, p));
}

/***************************************************************************/

static char *help_critbinom = {
        N_("@FUNCTION=CRITBINOM\n"
           "@SYNTAX=CRITBINOM(trials,p,alpha)\n"

           "@DESCRIPTION="
           "CRITBINOM function returns the smallest value for which the"
           "cumulative is greater than or equal to a given value. "
           "@n is the number of trials, @p is the probability of success in "
           "trials, and @alpha is the criterion value. "
           "\n"
           "If @trials is a non-integer it is truncated. "
           "If @trials < 0 CRITBINOM returns #NUM! error. "
           "If @p < 0 or @p > 1 CRITBINOM returns #NUM! error. "
           "If @alpha < 0 or @alpha > 1 CRITBINOM returns #NUM! error.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "CRITBINOM(10,0.5,0.75) equals 6.\n"
	   "\n"
           "@SEEALSO=BINOMDIST")
};

static Value *
gnumeric_critbinom (FunctionEvalInfo *ei, Value **argv)
{
        int trials;
        gnum_float p, alpha;

        trials = value_get_as_int (argv[0]);
        p = value_get_as_float (argv[1]);
        alpha = value_get_as_float (argv[2]);

        if (trials < 0 || p < 0 || p > 1 || alpha < 0 || alpha > 1)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qbinom (alpha, trials, p));
}

/***************************************************************************/

static char *help_permut = {
	N_("@FUNCTION=PERMUT\n"
	   "@SYNTAX=PERMUT(n,k)\n"

	   "@DESCRIPTION="
	   "PERMUT function returns the number of permutations. "
           "@n is the number of objects, @k is the number of objects in each "
           "permutation."
	   "\n"
	   "If @n = 0 PERMUT returns #NUM! error.  "
	   "If @n < @k PERMUT returns #NUM! error.\n "
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "PERMUT(7,3) equals 210.\n"
	   "\n"
	   "@SEEALSO=COMBIN")
};

static Value *
gnumeric_permut (FunctionEvalInfo *ei, Value **argv)
{
	int n, k;

	n = value_get_as_int (argv[0]);
	k = value_get_as_int (argv[1]);

	if (0 <= k && k <= n)
		return value_new_float (fact (n) / fact (n - k));
	else
		return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_hypgeomdist = {
	N_("@FUNCTION=HYPGEOMDIST\n"
	   "@SYNTAX=HYPGEOMDIST(x,n,M,N)\n"

	   "@DESCRIPTION="
	   "HYPGEOMDIST function returns the hypergeometric distribution. "
	   "@x is the number of successes in the sample, @n is the number "
           "of trials, @M is the number of successes overall, and @N is the"
           "population size."
	   "\n"
	   "If @x,@n,@M or @N is a non-integer it is truncated.  "
	   "If @x,@n,@M or @N < 0 HYPGEOMDIST returns #NUM! error.  "
	   "If @x > @M or @n > @N HYPGEOMDIST returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "HYPGEOMDIST(1,2,3,10) equals 0.4666667.\n"
	   "\n"
	   "@SEEALSO=BINOMDIST,POISSON")
};

static Value *
gnumeric_hypgeomdist (FunctionEvalInfo *ei, Value **argv)
{
	int x, n, M, N;

	x = value_get_as_int (argv[0]);
	n = value_get_as_int (argv[1]);
	M = value_get_as_int (argv[2]);
	N = value_get_as_int (argv[3]);

	if (x < 0 || n < 0 || M < 0 || N < 0 || x > M || n > N)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float ((combin (M, x) * combin (N - M, n - x)) /
				combin (N, n));
}

/***************************************************************************/

static char *help_confidence = {
	N_("@FUNCTION=CONFIDENCE\n"
	   "@SYNTAX=CONFIDENCE(x,stddev,size)\n"

	   "@DESCRIPTION="
	   "CONFIDENCE function returns the confidence interval for a "
	   "mean. @x is the significance level, @stddev is the population "
	   "standard deviation, and @size is the size of the sample."
	   "\n"
	   "If @size is non-integer it is truncated.  "
	   "If @size < 0 CONFIDENCE returns #NUM! error.  "
	   "If @size is 0 CONFIDENCE returns #DIV/0! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "CONFIDENCE(0.05,1,33) equals 0.341185936.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static Value *
gnumeric_confidence (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x, stddev;
	int size;

	x = value_get_as_float (argv[0]);
	stddev = value_get_as_float (argv[1]);
	size = value_get_as_int (argv[2]);

	if (size == 0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);

	if (size < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (-qnorm (x / 2, 0, 1) * (stddev / sqrt (size)));
}

/***************************************************************************/

static char *help_standardize = {
	N_("@FUNCTION=STANDARDIZE\n"
	   "@SYNTAX=STANDARDIZE(x,mean,stdev)\n"

	   "@DESCRIPTION="
	   "STANDARDIZE function returns a normalized value. "
	   "@x is the number to be normalized, @mean is the mean of the "
	   "distribution, @stdev is the standard deviation of the "
	   "distribution."
	   "\n"
	   "If @stddev is 0 STANDARDIZE returns #DIV/0! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "STANDARDIZE(3,2,4) equals 0.25.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static Value *
gnumeric_standardize (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x, mean, stddev;

	x = value_get_as_float (argv[0]);
	mean = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);

	if (stddev == 0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);
	else if (stddev < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float ((x - mean) / stddev);
}

/***************************************************************************/

static char *help_weibull = {
        N_("@FUNCTION=WEIBULL\n"
           "@SYNTAX=WEIBULL(x,alpha,beta,cumulative)\n"

           "@DESCRIPTION="
           "WEIBULL function returns the Weibull distribution. "
           "If the @cumulative boolean is true it will return: "
           "1 - exp (-(@x/@beta)^@alpha), otherwise it will return "
           "(@alpha/@beta^@alpha) * @x^(@alpha-1) * exp(-(@x/@beta^@alpha)). "
           "\n"
           "If @x < 0 WEIBULL returns #NUM! error. "
           "If @alpha <= 0 or @beta <= 0 WEIBULL returns #NUM! error.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "WEIBULL(3,2,4,0) equals 0.213668559.\n"
	   "\n"
           "@SEEALSO=POISSON")
};

static Value *
gnumeric_weibull (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float x, alpha, beta;
        int cuml;
	gboolean err;

        x = value_get_as_float (argv[0]);
        alpha = value_get_as_float (argv[1]);
        beta = value_get_as_float (argv[2]);

        if (x < 0 || alpha <= 0 || beta <= 0)
                return value_new_error (ei->pos, gnumeric_err_NUM);

        cuml = value_get_as_bool (argv[3], &err);
        if (err)
                return value_new_error (ei->pos, gnumeric_err_VALUE);

        if (cuml)
                return value_new_float (pweibull (x, alpha, beta));
        else
		return value_new_float (dweibull (x, alpha, beta));
}

/***************************************************************************/

static char *help_normdist = {
        N_("@FUNCTION=NORMDIST\n"
           "@SYNTAX=NORMDIST(x,mean,stdev,cumulative)\n"

           "@DESCRIPTION="
           "NORMDIST function returns the normal cumulative distribution. "
	   "@x is the value for which you want the distribution, @mean is "
	   "the mean of the distribution, @stdev is the standard deviation. "
           "\n"
           "If @stdev is 0 NORMDIST returns #DIV/0! error.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "NORMDIST(2,1,2,0) equals 0.176032663.\n"
	   "\n"
           "@SEEALSO=POISSON")
};


static Value *
gnumeric_normdist (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float x, mean, stdev;
        int cuml;
	gboolean err;

        x = value_get_as_float (argv[0]);
        mean = value_get_as_float (argv[1]);
        stdev = value_get_as_float (argv[2]);

        if (stdev <= 0)
                return value_new_error (ei->pos, gnumeric_err_DIV0);

        cuml = value_get_as_bool (argv[3], &err);
        if (err)
                return value_new_error (ei->pos, gnumeric_err_VALUE);

        if (cuml)
		return value_new_float (pnorm (x, mean, stdev));
        else
		return value_new_float (dnorm (x, mean, stdev));
}

/***************************************************************************/

static char *help_norminv = {
        N_("@FUNCTION=NORMINV\n"
           "@SYNTAX=NORMINV(p,mean,stdev)\n"

           "@DESCRIPTION="
           "NORMINV function returns the inverse of the normal "
	   "cumulative distribution. @p is the given probability "
	   "corresponding to the normal distribution, @mean is the "
	   "arithmetic mean of the distribution, and @stdev is the "
	   "standard deviation of the distribution."
           "\n"
	   "If @p < 0 or @p > 1 or @stdev <= 0 NORMINV returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "NORMINV(0.76,2,3) equals 4.118907689.\n"
	   "\n"
           "@SEEALSO=NORMDIST,NORMSDIST,NORMSINV,STANDARDIZE,ZTEST")
};

static Value *
gnumeric_norminv (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float p, mean, stdev;

        p = value_get_as_float (argv[0]);
	mean = value_get_as_float (argv[1]);
	stdev = value_get_as_float (argv[2]);

	if (p < 0 || p > 1 || stdev <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (qnorm (p, mean, stdev));
}


/***************************************************************************/

static char *help_kurt = {
        N_("@FUNCTION=KURT\n"
           "@SYNTAX=KURT(n1, n2, ...)\n"

           "@DESCRIPTION="
           "KURT returns an unbiased estimate of the kurtosis of a data set."
           "\n"
	   "Note, that this is only meaningful is the underlying "
	   "distribution really has a fourth moment.  The kurtosis is "
	   "offset by three such that a normal distribution will have zero "
	   "kurtosis."
           "\n"
           "Strings and empty cells are simply ignored."
           "\n"
           "If fewer than four numbers are given or all of them are equal "
           "KURT returns #DIV/0! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "KURT(A1:A5) equals 1.234546305.\n"
	   "\n"
           "@SEEALSO=AVERAGE,VAR,SKEW,KURTP")
};

static Value *
gnumeric_kurt (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_kurtosis_m3_est,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_kurtp = {
        N_("@FUNCTION=KURTP\n"
           "@SYNTAX=KURTP(n1, n2, ...)\n"

           "@DESCRIPTION="
           "KURTP returns the population kurtosis of a data set."
           "\n"
           "Strings and empty cells are simply ignored."
           "\n"
           "If fewer than two numbers are given or all of them are equal "
           "KURTP returns #DIV/0! error."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "KURTP(A1:A5) equals -0.691363424.\n"
	   "\n"
           "@SEEALSO=AVERAGE,VARP,SKEWP,KURT")
};

static Value *
gnumeric_kurtp (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_kurtosis_m3_pop,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_avedev = {
        N_("@FUNCTION=AVEDEV\n"
           "@SYNTAX=AVEDEV(n1, n2, ...)\n"

           "@DESCRIPTION="
           "AVEDEV returns the average of the absolute deviations of a data "
	   "set from their mean.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "AVEDEV(A1:A5) equals 7.84.\n"
	   "\n"
           "@SEEALSO=STDEV")
};

static Value *
gnumeric_avedev (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_avedev,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_devsq = {
        N_("@FUNCTION=DEVSQ\n"
           "@SYNTAX=DEVSQ(n1, n2, ...)\n"

           "@DESCRIPTION="
           "DEVSQ returns the sum of squares of deviations of a data set from "
           "the sample mean."
           "\n"
           "Strings and empty cells are simply ignored.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "DEVSQ(A1:A5) equals 470.56.\n"
	   "\n"
           "@SEEALSO=STDEV")
};

static Value *
gnumeric_devsq (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_devsq,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_fisher = {
        N_("@FUNCTION=FISHER\n"
           "@SYNTAX=FISHER(x)\n"

           "@DESCRIPTION="
           "FISHER function returns the Fisher transformation at @x."
           "\n"
           "If @x is not-number FISHER returns #VALUE! error.  "
           "If @x <= -1 or @x >= 1 FISHER returns #NUM! error.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "FISHER(0.332) equals 0.345074339.\n"
	   "\n"
           "@SEEALSO=SKEW")
};

static Value *
gnumeric_fisher (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float x;

        if (!VALUE_IS_NUMBER (argv[0]))
                return value_new_error (ei->pos, gnumeric_err_VALUE);

        x = value_get_as_float (argv[0]);

        if (x <= -1.0 || x >= 1.0)
                return value_new_error (ei->pos, gnumeric_err_NUM);

        return value_new_float (0.5 * log ((1.0 + x) / (1.0 - x)));
}

/***************************************************************************/

static char *help_poisson = {
	N_("@FUNCTION=POISSON\n"
	   "@SYNTAX=POISSON(x,mean,cumulative)\n"

	   "@DESCRIPTION="
	   "POISSON function returns the Poisson distribution. "
	   "@x is the number of events, @mean is the expected numeric value "
	   "@cumulative describes whether to return the sum of the "
	   "poisson function from 0 to @x."
	   "\n"
	   "If @x is a non-integer it is truncated. "
	   "If @x <= 0 POISSON returns #NUM! error. "
	   "If @mean <= 0 POISSON returns the #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "POISSON(3,6,0) equals 0.089235078.\n"
	   "\n"
	   "@SEEALSO=NORMDIST, WEIBULL")
};

static Value *
gnumeric_poisson (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float mean;
	int x, cuml;
	gboolean err;

	x = value_get_as_int (argv[0]);
	mean = value_get_as_float (argv[1]);
	cuml = value_get_as_bool (argv[2], &err);

	if (x <= 0 || mean <= 0 || err)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (cuml)
		return value_new_float (ppois (x, mean));
	else
		return value_new_float (dpois (x, mean));
}

/***************************************************************************/

static char *help_pearson = {
	N_("@FUNCTION=PEARSON\n"
	   "@SYNTAX=PEARSON(array1,array2)\n"

	   "@DESCRIPTION="
	   "PEARSON returns the Pearson correlation coefficient of two data "
	   "sets."
	   "\n"
	   "Strings and empty cells are simply ignored.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=INTERCEPT,LINEST,RSQ,SLOPE,STEYX")
};

static Value *
gnumeric_pearson (FunctionEvalInfo *ei, Value **argv)
{
	return gnumeric_correl (ei, argv);
}


/***************************************************************************/

static char *help_rsq = {
	N_("@FUNCTION=RSQ\n"
	   "@SYNTAX=RSQ(array1,array2)\n"

	   "@DESCRIPTION="
	   "RSQ returns the square of the Pearson correlation coefficient "
	   "of two data sets."
	   "\n"
	   "Strings and empty cells are simply ignored.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=CORREL,COVAR,INTERCEPT,LINEST,LOGEST,PEARSON,SLOPE,"
	   "STEYX,TREND")
};

static Value *
gnumeric_rsq (FunctionEvalInfo *ei, Value **argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      range_rsq_pop,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_median = {
        N_("@FUNCTION=MEDIAN\n"
           "@SYNTAX=MEDIAN(n1, n2, ...)\n"

           "@DESCRIPTION="
           "MEDIAN returns the median of the given data set."
           "\n"
           "Strings and empty cells are simply ignored. "
	   "If even numbers are given MEDIAN returns the average of the two "
	   "numbers in the middle.\n"
	   "This function is Excel compatible."
           "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "MEDIAN(A1:A5) equals 21.3.\n"
	   "\n"
           "@SEEALSO=AVERAGE,COUNT,COUNTA,DAVERAGE,MODE,SUM")
};

static Value *
gnumeric_median (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     (float_range_function_t)range_median_inter_nonconst,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_large = {
	N_("@FUNCTION=LARGE\n"
	   "@SYNTAX=LARGE(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "LARGE returns the k-th largest value in a data set."
	   "\n"
	   "If data set is empty LARGE returns #NUM! error. "
	   "If @k <= 0 or @k is greater than the number of data items given "
	   "LARGE returns #NUM! error.\n"
	   "This function is Excel compatible."
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
range_large (gnum_float *xs, int n, gnum_float *res)
{
	int k;

	if (n < 2)
		return 1;

	k = (int)xs[--n];
	return range_min_k_nonconst (xs, n, res, n - k);
}

static Value *
gnumeric_large (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     (float_range_function_t)range_large,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_small = {
	N_("@FUNCTION=SMALL\n"
	   "@SYNTAX=SMALL(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "SMALL returns the k-th smallest value in a data set."
	   "\n"
	   "If data set is empty SMALL returns #NUM! error. "
	   "If @k <= 0 or @k is greater than the number of data items given "
	   "SMALL returns #NUM! error.\n"
	   "This function is Excel compatible."
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
range_small (gnum_float *xs, int n, gnum_float *res)
{
	int k;

	if (n < 2)
		return 1;

	k = (int)xs[--n];
	return range_min_k_nonconst (xs, n, res, k - 1);
}

static Value *
gnumeric_small (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     (float_range_function_t)range_small,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_NUM);
}

typedef struct {
        GSList *list;
        int    num;
} stat_list_t;

static Value *
callback_function_list (Sheet *sheet, int col, int row,
			Cell *cell, void *user_data)
{
        stat_list_t *mm = user_data;
	gnum_float *p;

	if (cell == NULL || cell->value == NULL)
	        return NULL;
	if (!VALUE_IS_NUMBER (cell->value))
		return NULL;

	p = g_new (gnum_float, 1);
	*p = value_get_as_float (cell->value);
	mm->list = g_slist_append (mm->list, p);
	mm->num++;
	return NULL;
}

/***************************************************************************/

static char *help_prob = {
	N_("@FUNCTION=PROB\n"
	   "@SYNTAX=PROB(x_range,prob_range,lower_limit[,upper_limit])\n"

	   "@DESCRIPTION="
	   "PROB function returns the probability that values in a range or "
	   "an array are between two limits. If @upper_limit is not "
	   "given, PROB returns the probability that values in @x_range "
	   "are equal to @lower_limit."
	   "\n"
	   "If the sum of the probabilities in @prob_range is not equal to 1 "
	   "PROB returns #NUM! error. "
	   "If any value in @prob_range is <=0 or > 1, PROB returns #NUM! "
	   "error. "
	   "If @x_range and @prob_range contain a different number of data "
	   "entries, PROB returns #N/A error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=BINOMDIST,CRITBINOM")
};

static Value *
gnumeric_prob (FunctionEvalInfo *ei, Value **argv)
{
	make_list_t  x_cl, prob_cl;
	Value        *err;
	gnum_float   sum, total_sum;
	gnum_float   lower_limit, upper_limit;
	GSList       *list1, *list2;

	if ((err = make_list (&x_cl, ei->pos, argv [0])) ||
	    (err = make_list (&prob_cl, ei->pos, argv [1])))
		return err;

	if (x_cl.n != prob_cl.n) {
		list1 = x_cl.entries;
		list2 = prob_cl.entries;
		while (list1 != NULL) {
		        g_free (list1->data);
			list1 = list1->next;
		}
		while (list2 != NULL) {
		        g_free (list2->data);
			list2 = list2->next;
		}
		g_slist_free (x_cl.entries);
		g_slist_free (prob_cl.entries);

	        return value_new_error (ei->pos, gnumeric_err_NA);
	}

	lower_limit = value_get_as_float (argv[2]);
	if (argv[3] == NULL)
	        upper_limit = lower_limit;
	else
	        upper_limit = value_get_as_float (argv[3]);

	list1 = x_cl.entries;
	list2 = prob_cl.entries;
	sum = total_sum = 0;

	while (list1 != NULL) {
	        gnum_float  x, prob;

		x = *((gnum_float *) list1->data);
		prob = *((gnum_float *) list2->data);

		if (prob <= 0 || prob > 1)
		        return value_new_error (ei->pos, gnumeric_err_NUM);

		total_sum += prob;

		if (x >= lower_limit && x <= upper_limit)
		        sum += prob;

		g_free (list1->data);
		g_free (list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free (x_cl.entries);
	g_slist_free (prob_cl.entries);

	if (total_sum != 1)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (sum);
}

/***************************************************************************/

static char *help_steyx = {
	N_("@FUNCTION=STEYX\n"
	   "@SYNTAX=STEYX(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "STEYX function returns the standard error of the predicted "
	   "y-value for each x in the regression."
	   "\n"
	   "If @known_y's and @known_x's are empty or have a different number "
	   "of arguments then STEYX returns #N/A error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "STEYX(A1:A5,B1:B5) equals 1.101509979.\n"
	   "\n"
	   "@SEEALSO=PEARSON,RSQ,SLOPE")
};

static Value *
gnumeric_steyx (FunctionEvalInfo *ei, Value **argv)
{
        Value       *known_y = argv[0];
        Value       *known_x = argv[1];
	stat_list_t items_x, items_y;
	gnum_float  sum_x, sum_y, sum_xy, sqrsum_x, sqrsum_y;
	gnum_float  num, den, k, n;
	GSList      *list1, *list2;
	Value       *ret;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (known_x->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (known_x->v_range.cell.a.sheet,
				    ei->pos->sheet), TRUE,
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

		        return value_new_error (ei->pos, gnumeric_err_VALUE);
		}
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

        if (known_y->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (known_y->v_range.cell.a.sheet,
				    ei->pos->sheet),TRUE,
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

		        return value_new_error (ei->pos, gnumeric_err_VALUE);
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

	        return value_new_error (ei->pos, gnumeric_err_NA);
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum_x = sum_y = 0;
	sqrsum_x = sqrsum_y = 0;
	sum_xy = 0;

	while (list1 != NULL) {
	        gnum_float  x, y;

		x = *((gnum_float *) list1->data);
		y = *((gnum_float *) list2->data);

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
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (sqrt (k * (n * sqrsum_y - sum_y * sum_y - num / den)));
}

/***************************************************************************/

static char *help_ztest = {
	N_("@FUNCTION=ZTEST\n"
	   "@SYNTAX=ZTEST(ref,x)\n"

	   "@DESCRIPTION="
	   "ZTEST returns the two-tailed probability of a z-test."
	   "\n"
	   "@ref is the data set and @x is the value to be tested."
	   "\n"
	   "If @ref contains less than two data items ZTEST "
	   "returns #DIV/0! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "ZTEST(A1:A5,20) equals 0.254717826.\n"
	   "\n"
	   "@SEEALSO=CONFIDENCE,NORMDIST,NORMINV,NORMSDIST,NORMSINV,"
	   "STANDARDIZE")
};

typedef struct {
	guint32 num;
        gnum_float x;
        gnum_float sum;
        gnum_float sqrsum;
} stat_ztest_t;

static Value *
callback_function_ztest (EvalPos const *ep, Value *value, void *closure)
{
	stat_ztest_t *mm = closure;
	gnum_float last;

	if (!VALUE_IS_NUMBER (value))
		return value_new_error (ep, gnumeric_err_VALUE);

	last = value_get_as_float (value);
	if (mm->num > 0) {
	        mm->sum += mm->x;
		mm->sqrsum += mm->x * mm->x;
	}
	mm->x = last;
	mm->num++;
	return NULL;
}

static Value *
gnumeric_ztest (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	stat_ztest_t p;
	Value       *status;
	gnum_float   stdev;

	p.num    = 0;
	p.sum    = 0;
	p.sqrsum = 0;

	status = function_iterate_argument_values (ei->pos,
		callback_function_ztest, &p, expr_node_list,
		TRUE, TRUE);
	if (status != NULL)
		return status;

	p.num--;
	if (p.num < 2)
	        return value_new_error (ei->pos, gnumeric_err_DIV0);

	stdev = sqrt ((p.sqrsum - p.sum * p.sum / p.num) / (p.num - 1));

	if (stdev == 0)
	        return value_new_error (ei->pos, gnumeric_err_DIV0);

	return value_new_float (1 - pnorm ((p.sum / p.num - p.x) /
					   (stdev / sqrt (p.num)),
					   0, 1));
}

/***************************************************************************/

static char *help_averagea = {
	N_("@FUNCTION=AVERAGEA\n"
	   "@SYNTAX=AVERAGEA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "AVERAGEA returns the average of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "AVERAGEA(A1:A5) equals 18.94.\n"
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static Value *
gnumeric_averagea (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_average,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_DIV0);
}

/***************************************************************************/

static char *help_maxa = {
	N_("@FUNCTION=MAXA\n"
	   "@SYNTAX=MAXA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "MAXA returns the largest value of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "MAXA(A1:A5) equals 40.1.\n"
	   "\n"
	   "@SEEALSO=MAX,MINA")
};

static Value *
gnumeric_maxa (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_max,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_mina = {
	N_("@FUNCTION=MINA\n"
	   "@SYNTAX=MINA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "MINA returns the smallest value of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "MINA(A1:A5) equals 0.\n"
	   "\n"
	   "@SEEALSO=MIN,MAXA")
};

static Value *
gnumeric_mina (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_min,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_vara = {
	N_("@FUNCTION=VARA\n"
	   "@SYNTAX=VARA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "VARA returns the variance based on a sample.  Numbers, text "
	   "and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates "
	   "to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "VARA(A1:A5) equals 228.613.\n"
	   "\n"
	   "@SEEALSO=VAR,VARPA")
};

static Value *
gnumeric_vara (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_var_est,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_varpa = {
	N_("@FUNCTION=VARPA\n"
	   "@SYNTAX=VARPA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "VARPA returns the variance based on the entire population.  "
	   "Numbers, text and logical values are included in the "
	   "calculation too.  If the cell contains text or the argument "
	   "evaluates to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "VARPA(A1:A5) equals 182.8904.\n"
	   "\n"
	   "@SEEALSO=VARP,VARP")
};

static Value *
gnumeric_varpa (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_var_pop,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_stdeva = {
	N_("@FUNCTION=STDEVA\n"
	   "@SYNTAX=STDEVA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "STDEVA returns the standard deviation based on a sample. "
	   "Numbers, text and logical values are included in the calculation "
	   "too.  If the cell contains text or the argument evaluates to "
	   "FALSE, it is counted as value zero (0).  If the argument "
	   "evaluates to TRUE, it is counted as one (1).  Note that empty "
	   "cells are not counted.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "STDEVA(A1:A5) equals 15.119953704.\n"
	   "\n"
	   "@SEEALSO=STDEV,STDEVPA")
};

static Value *
gnumeric_stdeva (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_stddev_est,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_stdevpa = {
	N_("@FUNCTION=STDEVPA\n"
	   "@SYNTAX=STDEVPA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "STDEVPA returns the standard deviation based on the entire "
	   "population.  Numbers, text and logical values are included in "
	   "the calculation too.  If the cell contains text or the argument "
	   "evaluates to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "and strings 11.4, 17.3, \"missing\", 25.9, and 40.1.  Then\n"
	   "STDEVPA(A1:A5) equals 13.523697719.\n"
	   "\n"
	   "@SEEALSO=STDEVA,STDEVP")
};

static Value *
gnumeric_stdevpa (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return float_range_function (expr_node_list,
				     ei,
				     range_stddev_pop,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_percentrank = {
	N_("@FUNCTION=PERCENTRANK\n"
	   "@SYNTAX=PERCENTRANK(array,x[,significance])\n"

	   "@DESCRIPTION="
	   "PERCENTRANK function returns the rank of a data point in a data "
	   "set.  @array is the range of numeric values, @x is the data "
	   "point which you want to rank, and the optional @significance "
	   "indentifies the number of significant digits for the returned "
	   "value, truncating the remainder.  If @significance is omitted, "
	   "PERCENTRANK uses three digits."
	   "\n"
	   "If @array contains no data points, PERCENTRANK returns #NUM! "
	   "error.\n"
	   "If @significance is less than one, PERCENTRANK returns #NUM! "
	   "error.\n"
	   "If @x exceeds the largest value or is less than the smallest "
	   "value in @array, PERCENTRANK returns #NUM! error.\n"
	   "If @x does not match any of the values in @array or @x matches "
	   "more than once, PERCENTRANK interpolates the returned value."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LARGE,MAX,MEDIAN,MIN,PERCENTILE,QUARTILE,SMALL")
};

typedef struct {
        gnum_float x;
        gnum_float smaller_x;
        gnum_float greater_x;
        int     smaller;
        int     greater;
        int     equal;
} stat_percentrank_t;

static Value *
callback_function_percentrank (EvalPos const *ep, Value *value,
			       void *user_data)
{
        stat_percentrank_t *p = user_data;
	gnum_float y;

	if (!VALUE_IS_NUMBER (value))
		return value_terminate ();

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

static Value *
gnumeric_percentrank (FunctionEvalInfo *ei, Value **argv)
{
        stat_percentrank_t p;
        gnum_float         x, k, pr;
        int                significance;
	Value		   *ret;

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
		        return value_new_error (ei->pos, gnumeric_err_NUM);
	}

	ret = function_iterate_do_value (ei->pos, (FunctionIterateCB)
					 callback_function_percentrank,
					 &p, argv[0],
					 TRUE, TRUE);

	if (ret != NULL || (p.smaller + p.equal == 0) ||
		(p.greater + p.equal == 0))
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	if (p.equal == 1)
	        pr = (gnum_float)p.smaller / (p.smaller + p.greater);
	else if (p.equal == 0) {
	        gnum_float a = (x - p.smaller_x) / (p.greater_x - p.smaller_x);
	        pr = (gnum_float)(p.smaller + a - 1) / (p.greater + p.smaller - 1.0);
	} else
	        pr = (p.smaller + 0.5 * p.equal) /
		  (p.smaller + p.equal + p.greater);

	k = gpow10 (significance);
	return value_new_float (gnumeric_fake_trunc (pr * k) / k);
}

/***************************************************************************/

static char *help_percentile = {
	N_("@FUNCTION=PERCENTILE\n"
	   "@SYNTAX=PERCENTILE(array,k)\n"

	   "@DESCRIPTION="
	   "PERCENTILE function returns the 100*@k-th percentile "
	   "of the given data points (that is, a number x such "
	   "that a fraction @k of the data points are less than x).  "
	   "\n"
	   "If @array is empty, PERCENTILE returns #NUM! error.  "
	   "If @k < 0 or @k > 1, PERCENTILE returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "PERCENTILE(A1:A5,0.42) equals 20.02.\n"
	   "\n"
	   "@SEEALSO=QUARTILE")
};

static Value *
gnumeric_percentile (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float *data;
	Value *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     &n, &result);
	if (!result) {
		gnum_float p = value_get_as_float (argv[1]);
		gnum_float res;

		if (range_fractile_inter_nonconst (data, n, &res, p))
			result = value_new_error (ei->pos, gnumeric_err_NUM);
		else
			result = value_new_float (res);
	}

	g_free (data);
	return result;
}

/***************************************************************************/

static char *help_quartile = {
	N_("@FUNCTION=QUARTILE\n"
	   "@SYNTAX=QUARTILE(array,quart)\n"

	   "@DESCRIPTION="
	   "QUARTILE function returns the quartile of the given data "
	   "points.  "
	   "\n"
	   "If @quart is equal to: QUARTILE returns:\n"
	   "0                      the smallest value of @array.\n"
	   "1                      the first quartile\n"
	   "2                      the second quartile\n"
	   "3                      the third quartile\n"
	   "4                      the largest value of @array.\n"
	   "\n"
	   "If @array is empty, QUARTILE returns #NUM! error. "
	   "If @quart < 0 or @quart > 4, QUARTILE returns #NUM! error. "
	   "If @quart is not an integer, it is truncated.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1.  Then\n"
	   "QUARTILE(A1:A5,1) equals 17.3.\n"
	   "\n"
	   "@SEEALSO=LARGE,MAX,MEDIAN,MIN,PERCENTILE,SMALL")
};

static Value *
gnumeric_quartile (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float *data;
	Value *result = NULL;
	int n;

	data = collect_floats_value (argv[0], ei->pos,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     &n, &result);
	if (!result) {
		int q = value_get_as_int (argv[1]);
		gnum_float res;

		if (range_fractile_inter_nonconst (data, n, &res, q / 4.0))
			result = value_new_error (ei->pos, gnumeric_err_NUM);
		else
			result = value_new_float (res);
	}

	g_free (data);
	return result;
}

/***************************************************************************/

static char *help_ftest = {
	N_("@FUNCTION=FTEST\n"
	   "@SYNTAX=FTEST(array1,array2)\n"

	   "@DESCRIPTION="
	   "FTEST function returns the two-tailed probability that the "
	   "variances in the given two data sets are not significantly "
	   "different.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "FTEST(A1:A5,B1:B5) equals 0.510815017.\n"
	   "\n"
	   "@SEEALSO=FDIST,FINV")
};

static Value *
gnumeric_ftest (FunctionEvalInfo *ei, Value *argv[])
{
	stat_closure_t cl;
	gnum_float     var1, var2, p;
	int            dof1, dof2;
	Value         *err;

	if (NULL != (err = stat_helper (&cl, ei->pos, argv [0])))
		return err;
	dof1 = cl.N - 1;
	var1 = cl.Q / (cl.N - 1);

	if (NULL != (err = stat_helper (&cl, ei->pos, argv [1])))
		return err;
	dof2 = cl.N - 1;
	var2 = cl.Q / (cl.N - 1);

	if (var2 == 0)
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

	p = (1.0 - pf (var1 / var2, dof1, dof2)) * 2;

	if (p > 1)
	        p = 2 - p;

	return value_new_float (p);
}

/***************************************************************************/

static char *help_ttest = {
	N_("@FUNCTION=TTEST\n"
	   "@SYNTAX=TTEST(array1,array2,tails,type)\n"

	   "@DESCRIPTION="
	   "TTEST function returns the probability of a Student's t-Test. "
	   "\n"
	   "@array1 is the first data set and @array2 is the second data "
	   "set.  If @tails is one, TTEST uses the one-tailed distribution "
	   "and if @tails is two, TTEST uses the two-tailed distribution.  "
	   "@type determines the kind of the test:\n"
	   "1  Paired test\n"
	   "2  Two-sample equal variance\n"
	   "3  Two-sample unequal variance\n"
	   "\n"
	   "If the data sets contain a different number of data points and "
	   "the test is paired (@type one), TTEST returns the #N/A error. "
	   "@tails and @type are truncated to integers. "
	   "If @tails is not one or two, TTEST returns #NUM! error. "
	   "If @type is any other than one, two, or three, TTEST returns "
	   "#NUM! error.\n"
	   "This function is Excel compatible."
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

static Value *
callback_function_ttest (EvalPos const *ep, Value *value, void *closure)
{
	stat_ttest_t *mm = closure;
	gnum_float   x;

	if (value != NULL && VALUE_IS_NUMBER (value))
		x = value_get_as_float (value);
	else
	        x = 0;

	if (mm->first) {
	        gpointer p = g_new (gnum_float, 1);
		*((gnum_float *) p) = x;
		mm->entries = g_slist_append (mm->entries, p);
	} else {
	        if (mm->current == NULL)
			return value_terminate ();

	        *((gnum_float *) mm->current->data) -= x;
		mm->current = mm->current->next;
	}

	return NULL;
}

static Value *
gnumeric_ttest (FunctionEvalInfo *ei, Value *argv[])
{
	stat_closure_t cl;
	stat_ttest_t   t_cl;
	ExprTree      *tree;
	ExprList      *expr_node_list;
        int            tails, type;
	gnum_float     mean1, mean2, x, p;
	gnum_float     s, var1, var2, dof;
	int            n1, n2;
	Value         *err;

	tails = value_get_as_int (argv[2]);
	type = value_get_as_int (argv[3]);

	if ((tails != 1 && tails != 2) ||
	    (type < 1 || type > 3))
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (type == 1) {
	        GSList  *current;
	        gnum_float sum, dx, dm, M, Q, N;

	        t_cl.first = TRUE;
		t_cl.entries = NULL;

		tree = expr_tree_new_constant (argv[0]);
		expr_node_list = expr_list_append (NULL, tree);
		err = function_iterate_argument_values (ei->pos,
			 &callback_function_ttest, &t_cl, expr_node_list,
			 TRUE, FALSE);
		expr_list_free (expr_node_list);
		g_free (tree);
		if (err != NULL)
		        return err;

	        t_cl.first = FALSE;
		t_cl.current = t_cl.entries;

		tree = expr_tree_new_constant (argv[1]);
		expr_node_list = expr_list_append (NULL, tree);
		err = function_iterate_argument_values (ei->pos,
			 &callback_function_ttest, &t_cl, expr_node_list,
			 TRUE, FALSE);
		expr_list_free (expr_node_list);
		g_free (tree);
		if (err != NULL)
		        return err;

		current = t_cl.entries;
		dx = dm = M = Q = N = sum = 0;

		while (current != NULL) {
		        x = *((gnum_float *) current->data);

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
		        return value_new_error (ei->pos, gnumeric_err_NUM);

		s = sqrt (Q / (N - 1));
		mean1 = sum / N;
		x = mean1 / (s / sqrt (N));
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
		        gnum_float c = (var1 / n1) / (var1 / n1 + var2 / n2);
			dof = 1.0 / ((c * c) / (n1 - 1) + ((1 - c) * (1 - c)) /
				     (n2 - 1));
		} else
		        dof = n1 + n2 - 2;

		x = (mean1 - mean2) / sqrt (var1 / n1 + var2 / n2);
	}

	if (tails == 1)
		p = 1.0 - pt (fabs (x), dof);
	else
		p = (1.0 - pt (fabs (x), dof)) * 2;
	return value_new_float (p);
}

/***************************************************************************/

static char *help_frequency = {
	N_("@FUNCTION=FREQUENCY\n"
	   "@SYNTAX=FREQUENCY(data_array,bins_array)\n"

	   "@DESCRIPTION="
	   "FREQUENCY function counts how often given values occur within a "
	   "range of values.  The results are given as an array. "
	   "\n"
	   "@data_array is a data array for which you want to count the "
	   "frequencies.  @bin_array is an array containing the intervals "
	   "into which you want to group the values in data_array.  If the "
	   "@bin_array is empty, FREQUENCY returns the number of data points "
	   "in @data_array.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_frequency (FunctionEvalInfo *ei, Value *argv[])
{
	GSList       *current;
	make_list_t  data_cl, bin_cl;
	Value        *err, *res;
	gnum_float   *bin_array;
	int          *count, i;

	if ((err = make_list (&data_cl, ei->pos, argv [0])) ||
	    (err = make_list (&bin_cl, ei->pos, argv [1])))
	        return err;

	if (bin_cl.n == 0)
	        return value_new_int (data_cl.n);

	bin_array = g_new (gnum_float, bin_cl.n);
	i = 0;
	for (current = bin_cl.entries; current != NULL; current=current->next) {
		gnum_float *xp = current->data;
	        bin_array[i++] = *xp;
		g_free (xp);
	}
	qsort (bin_array, bin_cl.n, sizeof (gnum_float),
	       (void *) &float_compare);

	count = g_new (int, bin_cl.n + 1);
	for (i = 0; i < bin_cl.n + 1; i++)
	        count[i] = 0;

	for (current = data_cl.entries; current != NULL; current=current->next) {
		gnum_float *xp = current->data;
		for (i = 0; i < bin_cl.n; i++)
		        if (*xp <= bin_array[i])
				break;
		g_free (xp);
		count[i]++;
	}

	res = value_new_array_non_init (1, bin_cl.n + 1);
	res->v_array.vals[0] = g_new (Value *, bin_cl.n + 1);

	for (i = 0; i < bin_cl.n + 1; i++)
		res->v_array.vals[0][i] = value_new_float (count[i]);

	g_free (bin_array);
	g_free (count);
	g_slist_free (data_cl.entries);
	g_slist_free (bin_cl.entries);

	return res;
}

/***************************************************************************/

static char *help_linest = {
	N_("@FUNCTION=LINEST\n"
	   "@SYNTAX=LINEST(known_y's[,known_x's[,const[,stat]]])\n"

	   "@DESCRIPTION="
	   "LINEST function calculates the ``least squares'' line that best "
	   "fit to your data in @known_y's.  @known_x's contains the "
	   "corresponding x's where y=mx+b."
	   "\n"
	   "If @known_x's is omitted, an array {1, 2, 3, ...} is used. "
           "LINEST returns an array having two columns and one row.  The "
           "slope (m) of the regression line y=mx+b is given in the first "
           "column and the y-intercept (b) in the second."
	   "\n"
	   "If @known_y's and @known_x's have unequal number of data points, "
	   "LINEST returns #NUM! error."
	   "\n"
	   "If @const is FALSE, the line will be forced to go through the "
	   "origin, i.e., b will be zero. The default is TRUE."
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
	   "of squares. "
	   "\n"
	   "The default of @stat is FALSE.\n"
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

static Value *
gnumeric_linest (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float        **xss = NULL, *ys = NULL;
	Value             *result = NULL;
	int               nx, ny, dim, i;
	int               xarg = 0;
	gnum_float        *linres = NULL;
	gboolean          affine, stat, err;
	enum {
		ARRAY      = 1,
		SINGLE_COL = 2,
		SINGLE_ROW = 3,
		OTHER      = 4
	}                 ytype;
	regression_stat_t extra_stat;

	memset (&extra_stat, 0, sizeof (extra_stat));
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
		xss = g_new (gnum_float *, 1);
	        xss[0] = g_new (gnum_float, ny);
	        for (nx = 0; nx < ny; nx++)
		        xss[0][nx] = nx + 1;
	}
	else if (ytype == ARRAY){
			xarg = 1;
			/* Get xss from array argument argv[1] */
	}
	else if (ytype == SINGLE_COL){
		int firstcol, lastcol;
		Value *copy;
		xarg = 1;
		firstcol = argv[1]->v_range.cell.a.col;
		lastcol  = argv[1]->v_range.cell.b.col;

		if (firstcol < lastcol) {
			int tmp = firstcol;
			firstcol = lastcol;
			lastcol = tmp;
		}

		dim = lastcol - firstcol + 1;
		copy = value_duplicate (argv[1]);
		xss = g_new (gnum_float *, dim);
		for (i = firstcol; i <= lastcol; i++){
			copy->v_range.cell.a.col = i;
			copy->v_range.cell.b.col = i;
			xss[i - firstcol] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				g_free (copy);
				dim = i - firstcol; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
				g_free (copy);
				dim = i - firstcol + 1;
				result = value_new_error (ei->pos, gnumeric_err_NUM);
				goto out;
			}
		}
		g_free (copy);
	}
	else if (ytype == SINGLE_ROW){
		int firstrow, lastrow;
		Value *copy;
		xarg = 1;
		firstrow = argv[1]->v_range.cell.a.row;
		lastrow  = argv[1]->v_range.cell.b.row;

		if (firstrow < lastrow) {
			int tmp = firstrow;
			firstrow = lastrow;
			lastrow = tmp;
		}

		dim = lastrow - firstrow + 1;
		copy = value_duplicate (argv[1]);
		xss = g_new (gnum_float *, dim);
		for (i = firstrow; i <= lastrow; i++){
			copy->v_range.cell.a.row = i;
			copy->v_range.cell.b.row = i;
			xss[i - firstrow] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				g_free (copy);
				dim = i - firstrow; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
					g_free (copy);
					dim = i - firstrow + 1;
					result = value_new_error (ei->pos, gnumeric_err_NUM);
					goto out;
			}
		}
		g_free (copy);
	}
	else { /*Y is none of the above */
		xarg = 1;
		dim = 1;
		xss = g_new (gnum_float *, dim);
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
			result = value_new_error (ei->pos, gnumeric_err_NUM);
			goto out;
		}
	}

	if (argv[1 + xarg]) {
		affine = value_get_as_bool (argv[1 + xarg], &err);
		if (err) {
			result = value_new_error (ei->pos, gnumeric_err_VALUE);
			goto out;
		}
	} else
		affine = TRUE;

	if (argv[2 + xarg]) {
		stat = value_get_as_bool (argv[2 + xarg], &err);
		if (err) {
			result = value_new_error (ei->pos,
						  gnumeric_err_VALUE);
			goto out;
		}
	} else
		stat = FALSE;

	linres = g_new (gnum_float, dim + 1);

	if (linear_regression (xss, dim, ys, nx, affine,
			       linres, &extra_stat)) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	if (stat) {
		result = value_new_array (dim + 1, 5);

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat.sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (sqrt (extra_stat.var)));
		value_array_set (result, 0, 3,
				 value_new_float (extra_stat.F));
		value_array_set (result, 1, 3,
				 value_new_float (extra_stat.df));
		value_array_set (result, 0, 4,
				 value_new_float (extra_stat.ss_reg));
		value_array_set (result, 1, 4,
				 value_new_float (extra_stat.ss_resid));
		for (i = 0; i < dim; i++)
			value_array_set (result, dim - i - 1, 1,
					 value_new_float (extra_stat.se[i+affine]));
		value_array_set (result, dim, 1,
				 value_new_float (extra_stat.se[0]));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (linres[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0, value_new_float (linres[i + 1]));

 out:
	for (i = 0; i < dim; i++)
		g_free (xss[i]);
	g_free (xss);
	g_free (ys);
	g_free (linres);
	g_free (extra_stat.se);
	g_free (extra_stat.xbar);
	g_free (extra_stat.t);
	return result;
}

/***************************************************************************/

static char *help_trend = {
	N_("@FUNCTION=TREND\n"
	   "@SYNTAX=TREND(known_y's[,known_x's],new_x's])\n"

	   "@DESCRIPTION="
	   "TREND function estimates future values of a given data set "
	   "using the ``least squares'' line that best fit to your data. "
	   "@known_y's is the y-values where y=mx+b and @known_x's contains "
	   "the corresponding x-values.  @new_x's contains the x-values for "
	   "which you want to estimate the y-values. "
	   "\n"
	   "If @known_x's is omitted, an array {1, 2, 3, ...} is used. "
	   "If @new_x's is omitted, it is assumed to be the same as "
	   "@known_x's. "
	   "If @known_y's and @known_x's have unequal number of data points, "
	   "TREND returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "TREND(A1:A5,B1:B5) equals 156.52.\n"
	   "\n"
	   "@SEEALSO=LINEST")
};

static Value *
gnumeric_trend (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float  *xs = NULL, *ys = NULL, *nxs = NULL;
	Value    *result = NULL;
	int      nx, ny, nnx, i, dim;
	gboolean affine, err;
	gnum_float  linres[2];

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
			        result = value_new_error (ei->pos,
							  gnumeric_err_VALUE);
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
		        xs = g_new (gnum_float, ny);
			for (nx = 0; nx < ny; nx++)
			        xs[nx] = nx + 1;
		        nxs = g_new (gnum_float, ny);
			for (nnx = 0; nnx < ny; nnx++)
			        xs[nnx] = nnx + 1;
		}
	}

	if (result)
		goto out;

	if (nx != ny) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	dim = 1;

	if (linear_regression (&xs, dim, ys, nx, affine, linres, NULL)) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
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

static char *help_logest = {
	N_("@FUNCTION=LOGEST\n"
	   "@SYNTAX=LOGEST(known_y's[,known_x's,const,stat])\n"

	   "@DESCRIPTION="
	   "The LOGEST function applies the ``least squares'' method to fit "
	   "an exponential curve of the form "
	   "y = b * m{1}^x{1} * m{2}^x{2}... to your data."
	   "\n"
	   "If @known_x's is omitted, an array {1, 2, 3, ...} is used. "
	   "LOGEST returns an array { m{n},m{n-1}, ...,m{1},b }."
	   "\n"
	   "If @known_y's and @known_x's have unequal number of data points, "
	   "LOGEST returns #NUM! error."
	   "\n"
	   "If @const is FALSE, the line will be forced to go through (0,1),"
	   "i.e., b will be one.  The default is TRUE."
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
	   "of squares. "
	   "\n"
	   "The default of @stat is FALSE.\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOGEST,GROWTH,TREND")
};

static Value *
gnumeric_logest (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float        **xss = NULL, *ys = NULL;
	Value             *result = NULL;
	int               affine, nx, ny, dim, i;
	int               xarg = 0;
	gnum_float        *expres = NULL;
	gboolean          stat, err;
	regression_stat_t extra_stat;
	enum {
		ARRAY      = 1,
		SINGLE_COL = 2,
		SINGLE_ROW = 3,
		OTHER      = 4
	}                 ytype;

	memset (&extra_stat, 0, sizeof (extra_stat));
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
		xss = g_new (gnum_float *, 1);
	        xss[0] = g_new (gnum_float, ny);
	        for (nx = 0; nx < ny; nx++)
		        xss[0][nx] = nx + 1;
	}
	else if (ytype == ARRAY){
			xarg = 1;
			/* Get xss from array argument argv[1] */
	}
	else if (ytype == SINGLE_COL){
		int firstcol, lastcol;
		Value *copy;
		xarg = 1;
		firstcol = argv[1]->v_range.cell.a.col;
		lastcol  = argv[1]->v_range.cell.b.col;
		if (firstcol < lastcol) {
			int tmp = firstcol;
			firstcol = lastcol;
			lastcol = tmp;
		}

		dim = lastcol - firstcol + 1;
		copy = value_duplicate (argv[1]);
		xss = g_new (gnum_float *, dim);
		for (i = firstcol; i <= lastcol; i++){
			copy->v_range.cell.a.col = i;
			copy->v_range.cell.b.col = i;
			xss[i - firstcol] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				g_free (copy);
				dim = i - firstcol; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
				g_free (copy);
				dim = i - firstcol + 1;
				result = value_new_error (ei->pos, gnumeric_err_NUM);
				goto out;
			}
		}
		g_free (copy);
	}
	else if (ytype == SINGLE_ROW){
		int firstrow, lastrow;
		Value *copy;
		xarg = 1;
		firstrow = argv[1]->v_range.cell.a.row;
		lastrow  = argv[1]->v_range.cell.b.row;

		if (firstrow < lastrow) {
			int tmp = firstrow;
			firstrow = lastrow;
			lastrow = tmp;
		}

		dim = lastrow - firstrow + 1;
		copy = value_duplicate (argv[1]);
		xss = g_new (gnum_float *, dim);
		for (i = firstrow; i <= lastrow; i++){
			copy->v_range.cell.a.row = i;
			copy->v_range.cell.b.row = i;
			xss[i - firstrow] = collect_floats_value (copy, ei->pos,
						       COLLECT_IGNORE_STRINGS |
						       COLLECT_IGNORE_BOOLS,
						       &nx, &result);
			if (result){
				g_free (copy);
				dim = i - firstrow; /*How many got allocated before failure*/
				goto out;
			}
			if (nx != ny){
					g_free (copy);
					dim = i - firstrow + 1;
					result = value_new_error (ei->pos, gnumeric_err_NUM);
					goto out;
			}
		}
		g_free (copy);
	}
	else { /*Y is none of the above */
		xarg = 1;
		dim = 1;
		xss = g_new (gnum_float *, dim);
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
			result = value_new_error (ei->pos, gnumeric_err_NUM);
			goto out;
		}
	}

	if (argv[1 + xarg]) {
		affine = value_get_as_bool (argv[1 + xarg], &err) ? 1 : 0;
		if (err) {
			result = value_new_error (ei->pos, gnumeric_err_VALUE);
			goto out;
		}
	} else
		affine = 1;

	if (argv[2 + xarg]) {
		stat = value_get_as_bool (argv[2 + xarg], &err);
		if (err) {
			result = value_new_error (ei->pos,
						  gnumeric_err_VALUE);
			goto out;
		}
	} else
		stat = FALSE;

	expres = g_new (gnum_float, dim + 1);
	if (exponential_regression (xss, dim, ys, nx, affine,
				    expres, &extra_stat)) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	if (stat) {
		result = value_new_array (dim + 1, 5);

		value_array_set (result, 0, 2,
				 value_new_float (extra_stat.sqr_r));
		value_array_set (result, 1, 2,
				 value_new_float (sqrt (extra_stat.var))); /* Still wrong ! */
		value_array_set (result, 0, 3,
				 value_new_float (extra_stat.F));
		value_array_set (result, 1, 3,
				 value_new_float (extra_stat.df));
		value_array_set (result, 0, 4,
				 value_new_float (extra_stat.ss_reg));
		value_array_set (result, 1, 4,
				 value_new_float (extra_stat.ss_resid));
		for (i = 0; i < dim; i++)
			value_array_set (result, dim - i - 1, 1,
					 value_new_float (extra_stat.se[i+affine]));
		value_array_set (result, dim, 1,
				 value_new_float (extra_stat.se[0]));
	} else
		result = value_new_array (dim + 1, 1);

	value_array_set (result, dim, 0, value_new_float (expres[0]));
	for (i = 0; i < dim; i++)
		value_array_set (result, dim - i - 1, 0, value_new_float (expres[i + 1]));

 out:
	for (i = 0; i < dim; i++)
		g_free (xss[i]);
	g_free (xss);
	g_free (ys);
	g_free (expres);
	g_free (extra_stat.se);
	g_free (extra_stat.xbar);
	g_free (extra_stat.t);

	return result;
}

/***************************************************************************/

static char *help_growth = {
	N_("@FUNCTION=GROWTH\n"
	   "@SYNTAX=GROWTH(known_y's[,known_x's,new_x's,const])\n"

	   "@DESCRIPTION="
	   "GROWTH function applies the ``least squares'' method to fit an "
	   "exponential curve to your data and predicts the exponential "
	   "growth by using this curve. "
	   "\n"
	   "If @known_x's is omitted, an array {1, 2, 3, ...} is used. "
	   "If @new_x's is omitted, it is assumed to be the same as "
	   "@known_x's. "
	   "\n"
	   "GROWTH returns an array having one column and a row for each "
	   "data point in @new_x."
	   "\n"
	   "If @known_y's and @known_x's have unequal number of data points, "
	   "GROWTH returns #NUM! error."
	   "\n"
	   "If @const is FALSE, the line will be forced to go through the "
	   "origin, i.e., b will be zero. The default is TRUE."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOGEST,GROWTH,TREND")
};

static Value *
gnumeric_growth (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float  *xs = NULL, *ys = NULL, *nxs = NULL;
	Value    *result = NULL;
	gboolean affine, err;
	int      nx, ny, nnx, i, dim;
	gnum_float  expres[2];

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
			        result = value_new_error (ei->pos,
							  gnumeric_err_VALUE);
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
		        xs = g_new (gnum_float, ny);
			for (nx = 0; nx < ny; nx++)
			        xs[nx] = nx + 1;
		        nxs = g_new (gnum_float, ny);
			for (nnx = 0; nnx < ny; nnx++)
			        nxs[nnx] = nnx + 1;
		}
	}

	if (result)
		goto out;

	if (nx != ny) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	dim = 1;

	if (exponential_regression (&xs, dim, ys, nx, affine, expres, NULL)) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	result = value_new_array (1, nnx);
	for (i = 0; i < nnx; i++)
	        value_array_set (result, 0, i,
				 value_new_float (pow (expres[1], nxs[i]) *
						  expres[0]));

 out:
	g_free (xs);
	g_free (ys);
	g_free (nxs);
	return result;
}

/***************************************************************************/

static char *help_forecast = {
	N_("@FUNCTION=FORECAST\n"
	   "@SYNTAX=FORECAST(x,known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "FORECAST function estimates a future value according to "
	   "existing values using simple linear regression.  The estimated "
	   "future value is a y-value for a given x-value (@x). "
	   "\n"
	   "If @known_x or @known_y contains no data entries or different "
	   "number of data entries, FORECAST returns #N/A error. "
	   "If the variance of the @known_x is zero, FORECAST returns #DIV/0 "
	   "error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11.4, 17.3, 21.3, 25.9, and 40.1, and the cells B1, B2, ... "
	   "B5 23.2, 25.8, 29.9, 33.5, and 42.7.  Then\n"
	   "FORECAST(7,A1:A5,B1:B5) equals -10.859397661.\n"
	   "\n"
	   "@SEEALSO=INTERCEPT,TREND")
};


static Value *
gnumeric_forecast (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float x, *xs = NULL, *ys = NULL;
	Value *result = NULL;
	int nx, ny, dim;
	gnum_float linres[2];

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
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	dim = 1;

	if (linear_regression (&xs, dim, ys, nx, 1, linres, NULL)) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	result = value_new_float (linres[0] + x * linres[1]);

 out:
	g_free (xs);
	g_free (ys);
	return result;
}

/***************************************************************************/

static char *help_intercept = {
	N_("@FUNCTION=INTERCEPT\n"
	   "@SYNTAX=INTERCEPT(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "INTERCEPT function calculates the point where the linear "
	   "regression line intersects the y-axis.  "
	   "\n"
	   "If @known_x or @known_y contains no data entries or different "
	   "number of data entries, INTERCEPT returns #N/A error. "
	   "If the variance of the @known_x is zero, INTERCEPT returns #DIV/0 "
	   "error.\n"
	   "This function is Excel compatible."
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
range_intercept (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res)
{
	gnum_float linres[2];
	int dim = 1;

	if (linear_regression ((gnum_float **)&xs, dim, ys, n, 1, linres, NULL))
		return 1;

	*res = linres[0];
	return 0;
}

static Value *
gnumeric_intercept (FunctionEvalInfo *ei, Value **argv)
{
	return float_range_function2 (argv[1], argv[0],
				      ei,
				      range_intercept,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_slope = {
	N_("@FUNCTION=SLOPE\n"
	   "@SYNTAX=SLOPE(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "SLOPE returns the slope of the linear regression line.\n"
	   "This function is Excel compatible."
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
range_slope (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res)
{
	gnum_float linres[2];
	int dim = 1;

	if (linear_regression ((gnum_float **)&xs, dim, ys, n, 1, linres, NULL))
		return 1;

	*res = linres[1];
	return 0;
}

static Value *
gnumeric_slope (FunctionEvalInfo *ei, Value **argv)
{
	return float_range_function2 (argv[1], argv[0],
				      ei,
				      range_slope,
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      gnumeric_err_VALUE);
}

/***************************************************************************/

void stat_functions_init (void);
void
stat_functions_init (void)
{
	FunctionDefinition *def;
	FunctionCategory *cat = function_get_category_with_translation
	  ("Statistics", _("Statistics"));

        def = function_add_nodes (cat, "avedev",    0,      "",
				  &help_avedev, gnumeric_avedev);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "average", 0,      "",
				  &help_average, gnumeric_average);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "averagea", 0,      "",
				  &help_averagea, gnumeric_averagea);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "betadist", "fff|ff", "",
				  &help_betadist, gnumeric_betadist);

	def = function_add_args  (cat, "betainv", "fff|ff", "",
				  &help_betainv, gnumeric_betainv);

	def = function_add_args  (cat, "binomdist", "fffb", "n,t,p,c",
				  &help_binomdist, gnumeric_binomdist);

	def = function_add_args  (cat, "chidist",   "ff",  "",
				  &help_chidist, gnumeric_chidist);

	def = function_add_args  (cat, "chiinv",    "ff",  "",
				  &help_chiinv, gnumeric_chiinv);

	def = function_add_args  (cat, "chitest",   "rr",  "",
				  &help_chitest, gnumeric_chitest);

	def = function_add_args  (cat, "confidence", "fff",  "x,stddev,size",
				  &help_confidence, gnumeric_confidence);

	def = function_add_nodes (cat, "count",     0,      "",
				  &help_count, gnumeric_count);

	def = function_add_nodes (cat, "counta",    0,      "",
				  &help_counta, gnumeric_counta);

	def = function_add_args  (cat, "critbinom",  "fff",  "trials,p,alpha",
				  &help_critbinom, gnumeric_critbinom);

        def = function_add_args  (cat, "correl",     "AA",   "array1,array2",
				  &help_correl, gnumeric_correl);

        def = function_add_args  (cat, "covar",      "AA",   "array1,array2",
				  &help_covar, gnumeric_covar);

        def = function_add_nodes (cat, "devsq",      0,      "",
				  &help_devsq, gnumeric_devsq);

	def = function_add_args  (cat, "permut",    "ff",  "n,k",
				  &help_permut, gnumeric_permut);

	def = function_add_args  (cat, "poisson",   "ffb",  "",
				  &help_poisson, gnumeric_poisson);

	def = function_add_args  (cat, "expondist", "ffb",  "",
				  &help_expondist, gnumeric_expondist);

	def = function_add_args  (cat, "fdist",   "fff",  "",
				  &help_fdist, gnumeric_fdist);

	def = function_add_args  (cat, "finv",   "fff",  "",
				  &help_finv, gnumeric_finv);

        def = function_add_args  (cat, "fisher",    "f",    "",
				  &help_fisher, gnumeric_fisher);

        def = function_add_args  (cat, "fisherinv", "f",    "",
				  &help_fisherinv, gnumeric_fisherinv);

        def = function_add_args  (cat, "forecast", "frr",   "",
				  &help_forecast, gnumeric_forecast);

	def = function_add_args  (cat, "frequency", "AA", "data_array,bins_array",
				  &help_frequency, gnumeric_frequency);

	def = function_add_args  (cat, "ftest",     "rr",   "arr1,arr2",
				  &help_ftest, gnumeric_ftest);

	def = function_add_args  (cat, "gammaln",   "f",    "number",
				  &help_gammaln, gnumeric_gammaln);

	def = function_add_args  (cat, "gammadist", "fffb", "number,alpha,beta,cum",
				  &help_gammadist, gnumeric_gammadist);

	def = function_add_args  (cat, "gammainv", "fff",   "number,alpha,beta",
				  &help_gammainv, gnumeric_gammainv);

	def = function_add_nodes (cat, "geomean",   0,      "",
				  &help_geomean, gnumeric_geomean);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "growth",  "A|AAb",
				  "known_y's[,known_x's,new_x's,const]",
				  &help_growth, gnumeric_growth);

	def = function_add_nodes (cat, "harmean",   0,      "",
				  &help_harmean, gnumeric_harmean);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "hypgeomdist", "ffff", "x,n,M,N",
				  &help_hypgeomdist, gnumeric_hypgeomdist);

        def = function_add_args  (cat, "intercept", "AA",   "",
				  &help_intercept, gnumeric_intercept);

        def = function_add_nodes (cat, "kurt",      0,      "",
				  &help_kurt, gnumeric_kurt);

        def = function_add_nodes (cat, "kurtp",     0,      "",
				  &help_kurtp, gnumeric_kurtp);

	def = function_add_nodes (cat, "large",  0,      "",
				  &help_large, gnumeric_large);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "linest",  "A|Abb",
				  "known_y's[,known_x's,const,stat]",
				  &help_linest, gnumeric_linest);

	def = function_add_args  (cat, "logest",  "A|Abb",
				  "known_y's[,known_x's,const,stat]",
				  &help_logest, gnumeric_logest);

	def = function_add_args  (cat, "loginv",  "fff",  "",
				  &help_loginv, gnumeric_loginv);

	def = function_add_args  (cat, "lognormdist",  "fff",  "",
				  &help_lognormdist, gnumeric_lognormdist);

	def = function_add_nodes (cat, "max",     0,      "",
				  &help_max, gnumeric_max);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "maxa",    0,      "",
				  &help_maxa, gnumeric_maxa);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "median",    0,      "",
				  &help_median, gnumeric_median);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "min",     0,      "",
				  &help_min, gnumeric_min);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "mina",    0,      "",
				  &help_mina, gnumeric_mina);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "mode",      0,      "",
				  &help_mode, gnumeric_mode);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "negbinomdist", "fff", "f,t,p",
				  &help_negbinomdist, gnumeric_negbinomdist);

	def = function_add_args  (cat, "normdist",   "fffb",  "",
				  &help_normdist, gnumeric_normdist);

	def = function_add_args  (cat, "norminv",    "fff",  "",
				  &help_norminv, gnumeric_norminv);

	def = function_add_args  (cat, "normsdist",  "f",  "",
				  &help_normsdist, gnumeric_normsdist);

	def = function_add_args  (cat, "normsinv",  "f",  "",
				  &help_normsinv, gnumeric_normsinv);

        def = function_add_args  (cat, "percentile",   "Af",  "array,k",
				  &help_percentile, gnumeric_percentile);

	def = function_add_args  (cat, "percentrank", "Af|f", "array,x,significance",
				  &help_percentrank, gnumeric_percentrank);

        def = function_add_args  (cat, "pearson",     "AA",   "array1,array2",
				  &help_pearson, gnumeric_pearson);

	def = function_add_args  (cat, "prob", "AAf|f",
				  "x_range,prob_range,lower_limit,upper_limit",
				  &help_prob, gnumeric_prob);

        def = function_add_args  (cat, "quartile",    "Af",   "array,quart",
				  &help_quartile, gnumeric_quartile);

	def = function_add_args  (cat, "rank", "fr|f",      "",
				  &help_rank, gnumeric_rank);

        def = function_add_args  (cat, "rsq",         "AA",   "array1,array2",
				  &help_rsq, gnumeric_rsq);

	def = function_add_nodes (cat, "skew",      0,      "",
				  &help_skew, gnumeric_skew);

	def = function_add_nodes (cat, "skewp",     0,      "",
				  &help_skewp, gnumeric_skewp);

	def = function_add_args  (cat, "slope", "AA", "known_y's,known_x's",
				  &help_slope, gnumeric_slope);

	def = function_add_nodes (cat, "small",  0,      "",
				  &help_small, gnumeric_small);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "standardize", "fff",  "x,mean,stddev",
				  &help_standardize, gnumeric_standardize);

	def = function_add_nodes (cat, "stdev",     0,      "",
				  &help_stdev, gnumeric_stdev);
	/*
	 * Note: since stdev is sort-of a difference, we should actually guess
	 * "number" when the arguments are dates.  We guess "date".  Tough.
	 */
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "stdeva",    0,      "",
				  &help_stdeva, gnumeric_stdeva);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "stdevp",    0,      "",
				  &help_stdevp, gnumeric_stdevp);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "stdevpa",   0,      "",
				  &help_stdevpa, gnumeric_stdevpa);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "steyx", "AA", "known_y's,known_x's",
				  &help_steyx, gnumeric_steyx);

	def = function_add_args  (cat, "tdist",   "fff",    "",
				  &help_tdist, gnumeric_tdist);

	def = function_add_args  (cat, "tinv",    "ff",     "",
				  &help_tinv, gnumeric_tinv);

	def = function_add_args  (cat, "trend",  "A|AAb",
				  "known_y's[,known_x's,new_x's,const]",
				  &help_trend, gnumeric_trend);

	def = function_add_nodes (cat, "trimmean",  0,      "",
				  &help_trimmean, gnumeric_trimmean);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "ttest",   "rrff",   "",
				  &help_ttest, gnumeric_ttest);

	def = function_add_nodes (cat, "var",       0,      "",
				  &help_var, gnumeric_var);

	def = function_add_nodes (cat, "vara",      0,      "",
				  &help_vara, gnumeric_vara);

	def = function_add_nodes (cat, "varp",      0,      "",
				  &help_varp, gnumeric_varp);

	def = function_add_nodes (cat, "varpa",     0,      "",
				  &help_varpa, gnumeric_varpa);

        def = function_add_args  (cat, "weibull", "fffb",  "",
				  &help_weibull, gnumeric_weibull);

	def = function_add_nodes (cat, "ztest",  0,      "",
				  &help_ztest, gnumeric_ztest);
}
