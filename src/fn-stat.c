/*
 * fn-stat.c:  Built in statistical functions and functions registration
 *
 * Authors:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *  Michael Meeks <michael@imaginator.com>
 *  Morten Welinder <terra@diku.dk>
 */
#include <config.h>
#include <gnome.h>
#include <math.h>
#include "mathfunc.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static guint
float_hash (const float_t *d)
{
        return (guint) ((*d)*100);
}

static gint
float_equal (const float_t *a, const float_t *b)
{
	if (*a == *b)
	        return 1;
	return 0;
}

static gint
float_compare (const float_t *a, const float_t *b)
{
        if (*a < *b)
                return -1;
	else if (*a == *b)
	        return 0;
	else
	        return 1;
}

static gint
float_compare_d (const float_t *a, const float_t *b)
{
        if (*a > *b)
                return -1;
	else if (*a == *b)
	        return 0;
	else
	        return 1;
}


/* Take a deep breath.

   We want to compute the variance of a population, i.e., we want to
   compute

     VAR(x1,...,xn) = (1/n) sum ((x_i - m)^2)

   where m is the average of the population.  The obvious way is to
   run twice through the data: once to calculate m and once to calculate
   the variance.  From a numerical point of view, that is not the worst
   thing to do, but it is wasteful to have to look at the data twice.

   Mathematically, we might use the identity

     VAR(x1,...,xn) = (sum x_i^2 - (sum x_i)^2 / n) / n

   but this method has very bad numerical properties.  The subtraction
   loses a lot of precision and we can get negative results.  Bad.

   It is much better to keep track of the two values of

     M_j = (1/j) sum x_i                 i = 1,...,j
     Q_j = sum (x_i - M_j)^2             i = 1,...,j

   as we encounter each new observation.  If we set M_0 = Q_0 = 0, then
   we have

     M_j = M_{j-1} + (x - M_{j-1}) / j
     Q_j = Q_{j-1} + (x - M_{j-1})^2 * (j-1) / j

   Note, that we keep adding non-negative numbers to get Q_j.  That gives
   us good numerical properties.  Finally, we get the variance by

     VAR(x1,...,xn) = Q_n / n

   March 30, 1999.

   Morten Welinder
   terra@gnu.org

   PS: For fun, here is a comparison of the two methods.

     robbie:~> ./a.out 40000.00000001 40000.00000002
     N-variance according to method 1: -4.76837e-07      <--- wrong sign!
     N-variance according to method 2: 2.50222e-17

     robbie:~> ./a.out 40000.0000001 40000.0000002
     N-variance according to method 1: 0                 <--- zero!
     N-variance according to method 2: 2.50004e-15

     robbie:~> ./a.out 40000.000001 40000.000002
     N-variance according to method 1: 2.38419e-07       <--- six ord. mag. off!
     N-variance according to method 2: 2.5e-13

     robbie:~> ./a.out 40000.00001 40000.00002
     N-variance according to method 1: 2.38419e-07
     N-variance according to method 2: 2.5e-11

     robbie:~> ./a.out 40000.0001 40000.0002
     N-variance according to method 1: 0                 <--- zero again???
     N-variance according to method 2: 2.5e-09

     robbie:~> ./a.out 40000.001 40000.002
     N-variance according to method 1: 2.38419e-07
     N-variance according to method 2: 2.5e-07

     robbie:~> ./a.out 40000.01 40000.02
     N-variance according to method 1: 2.47955e-05
     N-variance according to method 2: 2.5e-05

     robbie:~> ./a.out 40000.1 40000.2
     N-variance according to method 1: 0.0025003
     N-variance according to method 2: 0.0025

   */



void
setup_stat_closure (stat_closure_t *cl)
{
	cl->N = 0;
	cl->M = 0.0;
	cl->Q = 0.0;
	cl->afun_flag = 0;
}

int
callback_function_stat (Sheet *sheet, Value *value, ErrorMessage *error,
			void *closure)
{
	stat_closure_t *mm = closure;
	float_t x, dx, dm;

	if (VALUE_IS_NUMBER (value))
		x = value_get_as_float (value);
	else {
	        if (mm->afun_flag)
		        x = 0;
		else
		        return TRUE;
	}

	dx = x - mm->M;
	dm = dx / (mm->N + 1);
	mm->M += dm;
	mm->Q += mm->N * dx * dm;
	mm->N++;

	return TRUE;
}

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
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=STDEV,VAR,MEAN")
};

FuncReturn *
gnumeric_varp (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_closure_t cl;

	setup_stat_closure (&cl);

	function_iterate_argument_values (&ei->pos, callback_function_stat,
					  &cl, expr_node_list, ei->error);

	if (cl.N <= 0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (cl.Q / cl.N));
}

static char *help_var = {
	N_("@FUNCTION=VAR\n"
	   "@SYNTAX=VAR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VAR estimates the variance of a sample of a population. "
	   "To get the true variance of a complete population use @VARP"
	   "\n"
	   "(VAR is also known as the N-1-variance.  Under reasonable "
	   "conditions, it is the maximum-likelihood estimator for the "
	   "true variance.)"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=VARP,STDEV")
};

FuncReturn *
gnumeric_var (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_closure_t cl;

	setup_stat_closure (&cl);

	function_iterate_argument_values (&ei->pos, callback_function_stat,
					  &cl, expr_node_list, ei->error);

	if (cl.N <= 1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (cl.Q / (cl.N - 1)));
}

static char *help_stdev = {
	N_("@FUNCTION=STDEV\n"
	   "@SYNTAX=STDEV(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEV returns standard deviation of a set of numbers "
	   "treating these numbers as members of a population"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=VAR,MEAN")
};

FuncReturn *
gnumeric_stdev (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value *var, *ans;

	var = (Value *)gnumeric_var (ei, expr_node_list);
	if (!var)
		FUNC_RETURN_VAL (NULL);
	ans = value_new_float (sqrt (value_get_as_float (var)));
	value_release (var);
	FUNC_RETURN_VAL (ans);
}

static char *help_stdevp = {
	N_("@FUNCTION=STDEVP\n"
	   "@SYNTAX=STDEVP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEVP returns standard deviation of a set of numbers "
	   "treating these numbers as members of a complete population"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=STDEV,VAR,MEAN")
};

FuncReturn *
gnumeric_stdevp (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value *var, *ans;

	var = (Value *)gnumeric_varp (ei, expr_node_list);
	if (!var)
		FUNC_RETURN_VAL (NULL);
	ans = value_new_float (sqrt (value_get_as_float (var)));
	value_release (var);
	FUNC_RETURN_VAL (ans);
}

static char *help_harmean = {
	N_("@FUNCTION=HARMEAN\n"
	   "@SYNTAX=HARMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "HARMEAN returns the harmonic mean of the N data points."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=GEOMEAN,MEDIAN,MEAN,MODE")
};

typedef struct {
	guint32 num;
	float_t sum;
} stat_inv_sum_t;

static int
callback_function_stat_inv_sum (Sheet *sheet, Value *value,
				ErrorMessage *error, void *closure)
{
	stat_inv_sum_t *mm = closure;
	float_t x;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	x = value_get_as_float (value);

	if (x == 0) {
		error_message_set (error, _("#DIV/0"));
		return FALSE;
	} else {
		mm->num++;
		mm->sum += 1.0 / x;
		return TRUE;
	}
}

static char *help_rank = {
	N_("@FUNCTION=RANK\n"
	   "@SYNTAX=RANK(x,ref,order)\n"

	   "@DESCRIPTION="
	   "RANK returns the rank of a number in a list of numbers. @x is the "
	   "number whose rank you want to find, @ref is the list of numbers, "
	   "and @order specifies how to rank numbers. If order is 0 numbers "
	   "are rank in descending order, otherwise numbers are rank in "
	   "ascending order."
	   "\n"
	   "@SEEALSO=PERCENTRANK")
};

typedef struct {
	int     first;
	guint32 num;
        float_t x;
        float_t last;
        GSList  *list;
} stat_rank_t;

static int
callback_function_rank (Sheet *sheet, Value *value,
			ErrorMessage *error, void *closure)
{
	stat_rank_t *mm = closure;
	float_t x;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	x = value_get_as_float (value);
	if (mm->first == TRUE)
		mm->x = x;
	else if (mm->num == 1)
		mm->last = x;
	else {
		float_t *p = g_new (float_t, 1);
		*p = mm->last;
		mm->list = g_slist_append (mm->list, p);
		mm->last = x;
	}
	mm->num++;
	mm->first = FALSE;
	return TRUE;
}

static FuncReturn *
gnumeric_rank (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_rank_t p;
	GSList      *list;
	int         order, rank;

	p.first = TRUE;
	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_rank,
					  &p, expr_node_list, ei->error);
					  
	list  = p.list;
	order = (int) p.last;
	rank  = 1;

	while (list != NULL){
	        float_t *x;
		x = list->data;
		if (order){
		        if (*x < p.x)
			        rank++;
		} else {
		        if (*x > p.x)
			        rank++;
		}
		g_free(x);
		list = list->next;
	}

	g_slist_free(p.list);

	FUNC_RETURN_VAL (value_new_int (rank));
}

static char *help_trimmean = {
	N_("@FUNCTION=TRIMMEAN\n"
	   "@SYNTAX=TRIMMEAN(ref,percent)\n"

	   "@DESCRIPTION="
	   "TRIMMEAN returns the mean of the interior of a data set. @ref "
	   "is the list of numbers whose mean you want to calculate and "
	   "@percent is the percentage of number excluded from the mean. "
	   "For example, if percent=0.2 and the data set contans 40 numbers, "
	   "8 numbers are trimmed from the data set (40 x 0.2), 4 from the "
	   "top and 4 from the bottom of the set."
	   "\n"
	   "@SEEALSO=AVERAGE,GEOMEAN,HARMEAN,MEDIAN,MODE")
};

typedef struct {
	guint32 num;
        float_t last;
        GSList  *list;
} stat_trimmean_t;

static int
callback_function_trimmean (Sheet *sheet, Value *value,
			    ErrorMessage *error, void *closure)
{
	stat_trimmean_t *mm = closure;
	float_t x;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	x = value_get_as_float (value);
	if (mm->num > 0) {
		float_t *p = g_new(float_t, 1);
		*p = mm->last;
		mm->list = g_slist_append (mm->list, p);
	}
	mm->last = x;
	mm->num++;
	return TRUE;
}

static FuncReturn *
gnumeric_trimmean (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_trimmean_t p;
	GSList      *list;
	int         trim_count, n, count;
	float_t     sum;

	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_trimmean,
					  &p, expr_node_list, ei->error);

	p.num--;
	trim_count = (p.num * p.last) / 2;
	count = p.num - 2 * trim_count;
	p.list = g_slist_sort (p.list, (GCompareFunc) float_compare);
	list  = p.list;

	/* Skip the trimmed numbers in the beginning of the list */
	for (n = 0; n < trim_count; n++){
	        g_free (list->data);
		list = list->next;
	}

	/* Count the sum for mean */
	for (n = sum = 0; n < count; n++){
	        float_t *x;

		x = list->data;
		sum += *x;
		g_free (x);
		list = list->next;
	}

	/* Free the rest of the number on the list */
	for (n = 0; n < trim_count; n++){
	        g_free (list->data);
		list = list->next;
	}

	g_slist_free (p.list);

	FUNC_RETURN_VAL (value_new_float (sum / count));
}

static char *help_covar = {
	N_("@FUNCTION=COVAR\n"
	   "@SYNTAX=COVAR(array1,array2)\n"

	   "@DESCRIPTION="
	   "COVAR returns the covariance of two data sets."
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=CORREL,FISHER,FISHERINV")
};

typedef struct {
	guint32 num;
	int     count;
        GSList  *array1;
        GSList  *array2;
        float_t sum1;
        float_t sum2;
} stat_covar_t;

static int
callback_function_covar (Sheet *sheet, Value *value,
			 ErrorMessage *error, void *closure)
{
	stat_covar_t *mm = closure;
	float_t x, *p;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

        p = g_new (float_t, 1);
	*p = x = value_get_as_float (value);

	if (mm->num < mm->count){
		mm->array1 = g_slist_append (mm->array1, p);
		mm->sum1 += x;
	} else {
		mm->array2 = g_slist_append (mm->array2, p);
		mm->sum2 += x;
	}
	mm->num++;
	return TRUE;
}

static FuncReturn *
gnumeric_covar (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_covar_t pr;
	float_t sum, mean1, mean2;
	int     count;
	GSList  *list1, *list2;
	Value *vtmp;

	vtmp = (Value *)gnumeric_count (ei, expr_node_list);
			       
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	count = value_get_as_int (vtmp);
	value_release (vtmp);

	/* FIXME: what about count == 0?  */
	if (count % 2 > 0)
		return function_error (ei, gnumeric_err_NUM);

	pr.count  = count / 2;
	pr.num    = 0;
	pr.sum1   = 0.0;
	pr.sum2   = 0.0;
	pr.array1 = NULL;
	pr.array2 = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_covar,
					  &pr, expr_node_list, ei->error);
					  
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;
	mean1 = pr.sum1 / pr.count;
	mean2 = pr.sum2 / pr.count;
	while (list1 != NULL && list2 != NULL){
	        float_t *x, *y;

		x = list1->data;
		y = list2->data;
	        sum += (*x - mean1) * (*y - mean2);

		g_free (x);
		g_free (y);

		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free (pr.array1);
	g_slist_free (pr.array2);

	FUNC_RETURN_VAL (value_new_float (sum / pr.count));
}

static char *help_correl = {
	N_("@FUNCTION=CORREL\n"
	   "@SYNTAX=CORREL(array1,array2)\n"

	   "@DESCRIPTION="
	   "CORREL returns the correllation coefficient of two data sets."
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=COVAR,FISHER,FISHERINV")
};

typedef struct {
	guint32 num;
	int     count;
        GSList  *array1;
        GSList  *array2;
        float_t sum1;
        float_t sum2;
        float_t sqrsum1;
        float_t sqrsum2;
} stat_correl_t;

static int
callback_function_correl (Sheet *sheet, Value *value,
			  ErrorMessage *error, void *closure)
{
	stat_correl_t *mm = closure;
	float_t x, *p;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	p = g_new (float_t, 1);
	*p = x = value_get_as_float (value);

	if (mm->num < mm->count){
		mm->array1 = g_slist_append (mm->array1, p);
		mm->sum1 += x;
		mm->sqrsum1 += x * x;
	} else {
		mm->array2 = g_slist_append (mm->array2, p);
		mm->sum2 += x;
		mm->sqrsum2 += x * x;
	}
	mm->num++;
	return TRUE;
}

static FuncReturn *
gnumeric_correl (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_correl_t pr;
	float_t sum, tmp;
	int     count;
	GSList  *list1, *list2;
	Value *vtmp;

	vtmp = (Value *)gnumeric_count (ei, expr_node_list);
			       
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	count = value_get_as_int (vtmp);
	value_release (vtmp);
	if (count % 2 > 0)
		return function_error (ei, gnumeric_err_NUM);

	pr.count   = count / 2;
	pr.num     = 0;
	pr.sum1    = 0.0;
	pr.sum2    = 0.0;
	pr.sqrsum1 = 0.0;
	pr.sqrsum2 = 0.0;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_correl,
					  &pr, expr_node_list, ei->error);
					  
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL){
	        float_t *x, *y;

		x = list1->data;
		y = list2->data;
	        sum += *x * *y;
		g_free (x);
		g_free (y);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free (pr.array1);
	g_slist_free (pr.array2);

	tmp = (pr.sqrsum1-(pr.sum1*pr.sum1)/pr.count) *
	        (pr.sqrsum2-(pr.sum2*pr.sum2)/pr.count);
	if (tmp == 0)
	        FUNC_RETURN_VAL (value_new_float (0));
	else
	        FUNC_RETURN_VAL (value_new_float ((sum - (pr.sum1*pr.sum2/pr.count)) /
						  sqrt(tmp)));
}

static char *help_negbinomdist = {
	N_("@FUNCTION=NEGBINOMDIST\n"
	   "@SYNTAX=NEGBINOMDIST(f,t,p)\n"

	   "@DESCRIPTION="
	   "The NEGBINOMDIST function returns the negative binomial "
	   "distribution. @f is the number of failures, @t is the threshold "
	   "number of successes, and @p is the probability of a success."
	   "\n"
	   "if f or t is a non-integer it is truncated. "
	   "if (f + t -1) <= 0 NEGBINOMDIST returns #NUM! error. "
	   "if p < 0 or p > 1 NEGBINOMDIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=BINOMDIST,COMBIN,FACT,HYPGEOMDIST,PERMUT")
};

static FuncReturn *
gnumeric_negbinomdist (FunctionEvalInfo *ei, Value **argv)
{
	int x, r;
	float_t p;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2]))
		return function_error (ei, gnumeric_err_VALUE);

	x = value_get_as_int (argv [0]);
	r = value_get_as_int (argv [1]);
	p = value_get_as_float (argv[2]);

	if ((x + r -1) <= 0 || p < 0 || p > 1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (combin (x+r-1, r-1) * pow (p, r) * pow (1-p, x)));
}

static char *help_normsdist = {
       N_("@FUNCTION=NORMSDIST\n"
          "@SYNTAX=NORMSDIST(x)\n"

          "@DESCRIPTION="
          "The NORMSDIST function returns the standard normal cumulative "
	  "distribution. @x is the value for which you want the distribution. "
          "\n"
          "Performing this function on a string or empty cell simply does nothing. "
          "\n"
          "@SEEALSO=NOMRDIST")
};

static FuncReturn *
gnumeric_normsdist (FunctionEvalInfo *ei, Value **argv)
{
        float_t x;

        x = value_get_as_float (argv [0]);

	FUNC_RETURN_VAL (value_new_float (pnorm (x, 0, 1)));
}

static char *help_normsinv = {
       N_("@FUNCTION=NORMSINV\n"
          "@SYNTAX=NORMSINV(p)\n"

          "@DESCRIPTION="
          "The NORMSINV function returns the inverse of the standard normal "
	  "cumulative distribution. @p is the given probability corresponding "
	  "to the normal distribution."
          "\n"
	  "If @p < 0 or @p > 1 NORMSINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=NORMDIST,NORMINV,NORMSDIST,STANDARDIZE,ZTEST")
};


static FuncReturn *
gnumeric_normsinv (FunctionEvalInfo *ei, Value **argv)
{
        float_t p, x;

        p = value_get_as_float (argv [0]);
	if (p < 0 || p > 1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (qnorm (p, 0, 1)));
}

static char *help_lognormdist = {
       N_("@FUNCTION=LOGNORMDIST\n"
          "@SYNTAX=LOGNORMDIST(x,mean,stdev)\n"

          "@DESCRIPTION="
          "The LOGNORMDIST function returns the lognormal distribution. "
	  "@x is the value for which you want the distribution, @mean is "
	  "the mean of the distribution, and @stdev is the standard deviation "
	  "of the distribution. "
          "\n"
          "Performing this function on a string or empty cell simply does nothing. "
          "if @stdev = 0 LOGNORMDIST returns #DIV/0! error. "
	  "if @x<=0, @mean<0 or @stdev<0 LOGNORMDIST returns #NUM! error. "
          "\n"
          "@SEEALSO=NORMDIST")
};

static FuncReturn *
gnumeric_lognormdist (FunctionEvalInfo *ei, Value **argv)
{
        float_t x, mean, stdev;

        x = value_get_as_float (argv [0]);
        mean = value_get_as_float (argv [1]);
        stdev = value_get_as_float (argv [2]);

        if (stdev==0)
                return function_error (ei, _("#DIV/0!"));

        if (x<=0 || mean<0 || stdev<0)
                return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (plnorm (x, mean, stdev)));
}

static char *help_loginv = {
       N_("@FUNCTION=LOGINV\n"
          "@SYNTAX=LOGINV(p,mean,stdev)\n"

          "@DESCRIPTION="
          "The LOGINV function returns the inverse of the lognormal "
	  "cumulative distribution. @p is the given probability corresponding "
	  "to the normal distribution, @mean is the arithmetic mean of the "
	  "distribution, and @stdev is the standard deviation of the "
	  "distribution."
          "\n"
	  "If @p < 0 or @p > 1 or @stdev <= 0 LOGINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=EXP,LN,LOG,LOG10,LOGNORMDIST")
};

static FuncReturn *
gnumeric_loginv (FunctionEvalInfo *ei, Value **argv)
{
        float_t p, mean, stdev;

        p = value_get_as_float (argv [0]);
        mean = value_get_as_float (argv [1]);
        stdev = value_get_as_float (argv [2]);
	if (p < 0 || p > 1 || stdev <= 0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (qlnorm (p, mean, stdev)));
}

static char *help_fisherinv = {
       N_("@FUNCTION=FISHERINV\n"
          "@SYNTAX=FISHERINV(y)\n"

          "@DESCRIPTION="
          "The FISHERINV function returns the inverse of the Fisher "
	  "transformation at x. "
          "\n"
          "If x is non-number FISHER returns #VALUE! error."
          "\n"
          "@SEEALSO=FISHER")
};

static FuncReturn *
gnumeric_fisherinv (FunctionEvalInfo *ei, Value **argv)
{
       float_t y;

       if (!VALUE_IS_NUMBER(argv [0]))
               return function_error (ei, gnumeric_err_VALUE);

       y = value_get_as_float (argv [0]);
       FUNC_RETURN_VAL (value_new_float ((exp (2*y)-1.0) / (exp (2*y)+1.0)));
}

static char *help_mode = {
       N_("@FUNCTION=MODE\n"
          "@SYNTAX=MODE(n1, n2, ...)\n"

          "@DESCRIPTION="
          "MODE returns the most common number of the data set. If the data "
	  "set has many most common numbers MODE returns the first one of "
	  "them. "
          "\n"
          "Strings and empty cells are simply ignored."
	  "If the data set does not contain any duplicates MODE returns #N/A! error."
          "\n"
          "@SEEALSO=AVERAGE,MEDIAN")
};

typedef struct {
       GHashTable *hash_table;
       GSList     *items;
       float_t    mode;
       int        count;
} stat_mode_t;

static int
callback_function_mode (Sheet *sheet, Value *value,
			ErrorMessage *error, void *closure)
{
       stat_mode_t *mm = closure;
       float_t  key;
       int      *p, count;

       if (!VALUE_IS_NUMBER (value))
	       return TRUE;

       key = value_get_as_float (value);
       p = g_hash_table_lookup (mm->hash_table, &key);

       if (p == NULL){
	       p = g_new (int, 1);
	       mm->items = g_slist_append (mm->items, p);
	       *p = count = 1;
	       g_hash_table_insert (mm->hash_table, &key, p);
       } else
	       *p = count = *p + 1;

       if (count > mm->count) {
	       mm->count = count;
	       mm->mode = key;
       }
       return TRUE;
}

static FuncReturn *
gnumeric_mode (FunctionEvalInfo *ei, GList *expr_node_list)
{
       GSList *tmp;
       stat_mode_t pr;

       pr.hash_table = g_hash_table_new((GHashFunc) float_hash,
					(GCompareFunc) float_equal);
       pr.items      = NULL;
       pr.mode       = 0.0;
       pr.count      = 0;

       function_iterate_argument_values (&ei->pos, callback_function_mode,
                                         &pr, expr_node_list, ei->error);

       g_hash_table_destroy (pr.hash_table);
       tmp = pr.items;
       while (tmp != NULL){
	       g_free (tmp->data);
	       tmp = tmp->next;
       }
       g_slist_free (pr.items);
       if (pr.count < 2)
		return function_error (ei, _("#N/A!"));

       FUNC_RETURN_VAL (value_new_float (pr.mode));
}

static FuncReturn *
gnumeric_harmean (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_inv_sum_t pr;

	pr.num   = 0;
	pr.sum   = 0.0;

	if (function_iterate_argument_values (&ei->pos, callback_function_stat_inv_sum,
					      &pr, expr_node_list, ei->error)) {
		float_t num;
		num = (float_t)pr.num;
		FUNC_RETURN_VAL (value_new_float (1.0 / (1.0/num * pr.sum)));
	} else
		FUNC_RETURN_VAL (NULL);
}

static char *help_geomean = {
	N_("@FUNCTION=GEOMEAN\n"
	   "@SYNTAX=GEOMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "GEOMEAN returns the geometric mean of the given arguments. "
	   "This is equal to the Nth root of the product of the terms"
	   "\n"
	   "Performing this function on a string or empty cell simply "
	   "does nothing."
	   "\n"
	   "@SEEALSO=HARMEAN,MEDIAN,MEAN,MODE")
};

typedef struct {
	guint32 num;
	float_t product;
} stat_prod_t;

static int
callback_function_stat_prod (Sheet *sheet, Value *value,
			     ErrorMessage *error, void *closure)
{
	stat_prod_t *mm = closure;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	mm->num++;
	mm->product *= value_get_as_float (value);
	return TRUE;
}

static FuncReturn *
gnumeric_geomean (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_prod_t pr;
	float_t num;

	pr.num     = 0;
	pr.product = 1.0;

	function_iterate_argument_values (&ei->pos, callback_function_stat_prod,
					  &pr, expr_node_list, ei->error);
					  
	/* FIXME: What should happen with negative numbers?  */
	FUNC_RETURN_VAL (value_new_float (pow (pr.product, 1.0 / pr.num)));
}

static char *help_count = {
	N_("@FUNCTION=COUNT\n"
	   "@SYNTAX=COUNT(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "Returns the total number of integer or floating point "
	   "arguments passed."
	   "\n"
	   "Performing this function on a string or empty cell simply does "
	   "nothing."
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static int
callback_function_count (Sheet *sheet, Value *value,
			 ErrorMessage *error, void *closure)
{
	Value *result = (Value *) closure;

	if (VALUE_IS_NUMBER (value))
		result->v.v_int++;
	return TRUE;
}

FuncReturn *
gnumeric_count (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value *result;

	result = value_new_int (0);
	function_iterate_argument_values (&ei->pos, callback_function_count,
					  result, expr_node_list, ei->error);
					  
	FUNC_RETURN_VAL (result);
}

static char *help_counta = {
        N_("@FUNCTION=COUNTA\n"
           "@SYNTAX=COUNTA(b1, b2, ...)\n"

           "@DESCRIPTION="
           "Returns the number of arguments passed not including empty cells."
           "\n"
           "@SEEALSO=AVERAGE,COUNT,DCOUNT,DCOUNTA,PRODUCT,SUM")
};

static int
callback_function_counta (Sheet *sheet, Value *value,
			  ErrorMessage *error, void *
closure)
{
        Value *result = (Value *) closure;

        switch (value->type){
        case VALUE_INTEGER:
        case VALUE_FLOAT:
	case VALUE_STRING:
	case VALUE_CELLRANGE:
	case VALUE_ARRAY:
                result->v.v_int++;
                break;
        default:
                break;
        }
	return TRUE;
}

FuncReturn *
gnumeric_counta (FunctionEvalInfo *ei, GList *expr_node_list)
{
        Value *result;

        result = value_new_int (0);
        function_iterate_argument_values (&ei->pos, callback_function_counta,
					  result, expr_node_list, ei->error);
                                          
        FUNC_RETURN_VAL (result);
}

static char *help_average = {
	N_("@FUNCTION=AVERAGE\n"
	   "@SYNTAX=AVERAGE(value1, value2,...)\n"

	   "@DESCRIPTION="
	   "Computes the average of all the values and cells referenced in "
	   "the argument list.  This is equivalent to the sum of the "
	   "arguments divided by the count of the arguments."
	   "\n"
	   "@SEEALSO=SUM, COUNT")
};

FuncReturn *
gnumeric_average (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value *vtmp;
	float_t sum, count;

	vtmp = (Value *)gnumeric_sum (ei, expr_node_list);
			     
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	sum = value_get_as_float (vtmp);
	value_release (vtmp);

	vtmp = (Value *)gnumeric_count (ei, expr_node_list);
			       
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	count = value_get_as_float (vtmp);
	value_release (vtmp);

	if (count == 0)
		return function_error (ei, _("#DIV/0!"));

	FUNC_RETURN_VAL (value_new_float (sum / count));
}

static char *help_min = {
	N_("@FUNCTION=MIN\n"
	   "@SYNTAX=MIN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MIN returns the value of the element of the values passed "
	   "that has the smallest value. With negative numbers considered "
	   "smaller than positive numbers."
	   "\n"
	   "Performing this function on a string or empty cell simply does "
	   "nothing."
	   "\n"
	   "@SEEALSO=MAX,ABS")
};

static char *help_max = {
	N_("@FUNCTION=MAX\n"
	   "@SYNTAX=MAX(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MAX returns the value of the element of the values passed "
	   "that has the largest value. With negative numbers considered "
	   "smaller than positive numbers."
	   "\n"
	   "Performing this function on a string or empty cell simply does "
	   "nothing."
	   "\n"
	   "@SEEALSO=MIN,ABS")
};

enum {
	OPER_MIN,
	OPER_MAX
};

typedef struct {
	int   operation;
	int   found;
	Value *result;
} min_max_closure_t;

static int
callback_function_min_max (Sheet *sheet, Value *value,
			   ErrorMessage *error, void *closure)
{
	min_max_closure_t *mm = closure;

	switch (value->type){
	case VALUE_INTEGER:
		if (mm->found){
			if (mm->operation == OPER_MIN){
				if (value->v.v_int < mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_int;
			} else {
				if (value->v.v_int > mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_int;
			}
		} else {
			mm->found = 1;
			mm->result->v.v_float = value->v.v_int;
		}
		break;

	case VALUE_FLOAT:
		if (mm->found){
			if (mm->operation == OPER_MIN){
				if (value->v.v_float < mm->result->v.v_float)
					mm->result->v.v_float =
					  value->v.v_float;
			} else {
				if (value->v.v_float > mm->result->v.v_float)
					mm->result->v.v_float =
					  value->v.v_float;
			}
		} else {
			mm->found = 1;
			mm->result->v.v_float = value->v.v_float;
		}
		break;

	default:
		/* ignore strings */
		break;
	}

	return TRUE;
}

FuncReturn *
gnumeric_min (FunctionEvalInfo *ei, GList *expr_node_list)
{
	min_max_closure_t closure;

	closure.operation = OPER_MIN;
	closure.found  = 0;
	closure.result = value_new_float (0);

	function_iterate_argument_values (&ei->pos, callback_function_min_max,
					  &closure, expr_node_list, ei->error);

	FUNC_RETURN_VAL (	closure.result);
}

FuncReturn *
gnumeric_max (FunctionEvalInfo *ei, GList *expr_node_list)
{
	min_max_closure_t closure;

	closure.operation = OPER_MAX;
	closure.found  = 0;
	closure.result = value_new_float (0);

	function_iterate_argument_values (&ei->pos, callback_function_min_max,
					  &closure, expr_node_list, ei->error);
					  
	FUNC_RETURN_VAL (	closure.result);
}

static char *help_skew = {
	N_("@FUNCTION=SKEW\n"
	   "@SYNTAX=SKEW(n1, n2, ...)\n"

	   "@DESCRIPTION="
	   "SKEW returns the skewness of a distribution."
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "If less than three numbers are given SKEW returns #DIV/0! error."
	   "\n"
	   "@SEEALSO=VAR")
};

typedef struct {
	guint32 num;
        float_t mean;
        float_t stddev;
	float_t sum;
} stat_skew_sum_t;

static int
callback_function_skew_sum (Sheet *sheet, Value *value,
			    ErrorMessage *error, void *closure)
{
	stat_skew_sum_t *mm = closure;
	float_t tmp;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	tmp = (value_get_as_float (value) - mm->mean) / mm->stddev;
	mm->num++;
	mm->sum += tmp * tmp * tmp;
	return TRUE;
}

static FuncReturn *
gnumeric_skew (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_skew_sum_t pr;
	Value *vtmp;

	pr.num  = 0;
	pr.sum  = 0.0;

	vtmp = (Value *)gnumeric_average (ei, expr_node_list);
				 
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	pr.mean = value_get_as_float (vtmp);
	value_release (vtmp);

	vtmp = (Value *)gnumeric_stdev (ei, expr_node_list);

	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	pr.stddev = value_get_as_float (vtmp);
	value_release (vtmp);

	/* FIXME: Check this.  */
	if (pr.stddev == 0)
		FUNC_RETURN_VAL (value_new_float (0));

	function_iterate_argument_values (&ei->pos, callback_function_skew_sum,
					  &pr, expr_node_list, ei->error);

	if (pr.num < 3)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (pr.sum * pr.num / (pr.num - 1) / (pr.num - 2)));
}

static char *help_expondist = {
	N_("@FUNCTION=EXPONDIST\n"
	   "@SYNTAX=EXPONDIST(x,y,cumulative)\n"

	   "@DESCRIPTION="
	   "The EXPONDIST function returns the exponential distribution. "
	   "If the cumulative boolean is false it will return: "
	   "y * exp (-y*x), otherwise it will return 1 - exp (-y*x)."
	   "\n"
	   "If x<0 or y<=0 this will return an error"
	   "Performing this function on a string or empty cell simply "
	   "does nothing."
	   "\n"
	   "@SEEALSO=POISSON")
};

static FuncReturn *
gnumeric_expondist (FunctionEvalInfo *ei, Value **argv)
{
	float_t x, y;
	int cuml, err=0;

	x = value_get_as_float (argv [0]);
	y = value_get_as_float (argv [1]);
	if (x < 0.0 || y <= 0.0)
		return function_error (ei, gnumeric_err_NUM);

	cuml = value_get_as_bool (argv[2], &err);
	if (err)
		return function_error (ei, gnumeric_err_VALUE);

	if (cuml){
		FUNC_RETURN_VAL (value_new_float (-expm1(-y*x)));
	} else {
		FUNC_RETURN_VAL (value_new_float (y*exp(-y*x)));
	}
}

static char *help_gammaln = {
	N_("@FUNCTION=GAMMALN\n"
	   "@SYNTAX=GAMMALN(x)\n"

	   "@DESCRIPTION="
	   "The GAMMALN function returns the natural logarithm of the "
	   "gamma function."
	   "\n"
	   "If @x is non-number then GAMMALN returns #VALUE! error. "
	   "If @x <= 0 then GAMMALN returns #NUM! error."
	   "\n"
	   "@SEEALSO=POISSON")
};

static FuncReturn *
gnumeric_gammaln (FunctionEvalInfo *ei, Value **argv)
{
	float_t x;

	/* FIXME: the gamma function is defined for all real numbers except
	 * the integers 0, -1, -2, ...  It is positive (and log(gamma(x)) is
	 * thus defined) when x>0 or -2<x<-1 or -4<x<-3 ...  */

	x = value_get_as_float (argv [0]);
	if (x<=0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (lgamma(x)));
}

static char *help_gammadist = {
	N_("@FUNCTION=GAMMADIST\n"
	   "@SYNTAX=GAMMADIST(x,alpha,beta,cum)\n"

	   "@DESCRIPTION="
	   "GAMMADIST function returns the gamma distribution. If @cum "
	   "is TRUE GAMMADIST returns the incomplete gamma function, "
	   "otherwise it returns the probability mass function."
	   "\n"
	   "If @x < 0 GAMMADIST returns #NUM! error. "
	   "If @alpha <= 0 or beta <= 0, GAMMADIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=GAMMAINV")
};

static FuncReturn *
gnumeric_gammadist (FunctionEvalInfo *ei, Value **argv)
{
	float_t x, alpha, beta;
	int     cum;

	x = value_get_as_float (argv [0]);
	alpha = value_get_as_float (argv [1]);
	beta = value_get_as_float (argv [2]);

	if (x<0 || alpha<=0 || beta<=0)
		return function_error (ei, gnumeric_err_NUM);

	cum = value_get_as_int (argv [3]);
	if (cum)
	        FUNC_RETURN_VAL (value_new_float (pgamma(x, alpha, beta)));
	else
	        FUNC_RETURN_VAL (value_new_float (dgamma(x, alpha, beta)));
}

static char *help_gammainv = {
       N_("@FUNCTION=GAMMAINV\n"
          "@SYNTAX=GAMMAINV(p,alpha,beta)\n"

          "@DESCRIPTION="
          "The GAMMAINV function returns the inverse of the cumulative "
	  "gamma distribution."
          "\n"
	  "If @p < 0 or @p > 1 GAMMAINV returns #NUM! error. "
	  "If @alpha <= 0 or @beta <= 0 GAMMAINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=GAMMADIST")
};

static FuncReturn *
gnumeric_gammainv (FunctionEvalInfo *ei, Value **argv)
{
        float_t p;
	int alpha, beta;

        p = value_get_as_float (argv [0]);
	alpha = value_get_as_float (argv [1]);
	beta = value_get_as_float (argv [2]);

	if (p<0 || p>1 || alpha<=0 || beta<=0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (qgamma (p, alpha, beta)));
}

static char *help_chidist = {
	N_("@FUNCTION=CHIDIST\n"
	   "@SYNTAX=CHIDIST(x,dof)\n"

	   "@DESCRIPTION="
	   "CHIDIST function returns the one-tailed probability of the "
	   "chi-squared distribution. @dof is the number of degrees of "
	   "freedom."
	   "\n"
	   "If @dof is non-integer it is truncated. "
	   "If @dof < 1 CHIDIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=CHIINV,CHITEST")
};

static FuncReturn *
gnumeric_chidist (FunctionEvalInfo *ei, Value **argv)
{
	float_t x;
	int     dof;

	x = value_get_as_float (argv [0]);
	dof = value_get_as_int (argv [1]);

	if (dof<1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (1.0 - pchisq (x, dof)));
}

static char *help_chiinv = {
       N_("@FUNCTION=CHIINV\n"
          "@SYNTAX=CHIINV(p,dof)\n"

          "@DESCRIPTION="
          "The CHIINV function returns the inverse of the one-tailed "
	  "probability of the chi-squared distribution."
          "\n"
	  "If @p < 0 or @p > 1 or @dof < 1 CHIINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=CHIDIST,CHITEST")
};

static FuncReturn *
gnumeric_chiinv (FunctionEvalInfo *ei, Value **argv)
{
        float_t p;
	int dof;

        p = value_get_as_float (argv [0]);
	dof = value_get_as_int (argv [1]);

	if (p<0 || p>1 || dof<1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (qchisq (1.0 - p, dof)));
}

static char *help_chitest = {
       N_("@FUNCTION=CHITEST\n"
          "@SYNTAX=CHITEST(actual_range,theoretical_range)\n"

          "@DESCRIPTION="
          "The CHITEST function returns the test for independence of "
	  "chi-squared distribution. "
          "\n"
	  "@actual_range is a range that contains the observed data points. "
	  "@theoretical_range is a range that contains the expected values "
	  "of the data points. "
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

static int
callback_function_chitest_actual (Sheet *sheet, Value *value,
				  ErrorMessage *error, void *closure)
{
	stat_chitest_t *mm = closure;
	float_t        *p;

	if (!VALUE_IS_NUMBER (value))
		return FALSE;

	p = g_new (float_t, 1);
	*p = value_get_as_float (value);
	mm->column = g_slist_append(mm->column, p);

	mm->row++;
	if (mm->row == mm->rows) {
	        mm->row = 0;
		mm->col++;
		mm->columns = g_slist_append(mm->columns, mm->column);
		mm->column = NULL;
	}

	return TRUE;
}

typedef struct {
        GSList *current_cell;
        GSList *next_col;
        int    cols;
        int    rows;
        float_t sum;
} stat_chitest_t_t;

static int
callback_function_chitest_theoretical (Sheet *sheet, Value *value,
				       ErrorMessage *error, void *closure)
{
	stat_chitest_t_t *mm = closure;
	float_t          a, e, *p;

	if (!VALUE_IS_NUMBER (value))
		return FALSE;

	e = value_get_as_float (value);

	if (mm->current_cell == NULL) {
	        mm->current_cell = mm->next_col->data;
		mm->next_col = mm->next_col->next;
	}
	p = mm->current_cell->data;
	a = *p;

	mm->sum += ((a-e) * (a-e)) / e;
	g_free (p);
	mm->current_cell = mm->current_cell->next;

	return TRUE;
}

static FuncReturn *
gnumeric_chitest (FunctionEvalInfo *ei, Value **argv)
{
        Sheet            *sheet;
	stat_chitest_t   p1;
	stat_chitest_t_t p2;
	GSList           *tmp;
	int              col, row, ret, dof;

	sheet = argv[0]->v.cell.sheet;
	col = argv[0]->v.cell.col;
	row = argv[0]->v.cell.row;
	p1.cols = argv[0]->v.cell_range.cell_b.col -
	  argv[0]->v.cell_range.cell_a.col;
	p2.cols = argv[1]->v.cell_range.cell_b.col -
	  argv[1]->v.cell_range.cell_a.col;
	p1.rows = argv[0]->v.cell_range.cell_b.row -
	  argv[0]->v.cell_range.cell_a.row;
	p2.rows = argv[1]->v.cell_range.cell_b.row -
	  argv[1]->v.cell_range.cell_a.row;
	p1.row = p1.col = 0;
	p1.columns = p1.column = NULL;

	if (p1.cols != p2.cols || p1.rows != p2.rows)
		return function_error (ei, gnumeric_err_NUM);

	ret = function_iterate_do_value (sheet, (FunctionIterateCallback)
					 callback_function_chitest_actual,
					 &p1, col, row, argv[0],
					 ei->error);
	if (ret == FALSE)
		return function_error (ei, gnumeric_err_NUM);

	p2.sum = 0;
	p2.current_cell = p1.columns->data;
	p2.next_col = p1.columns->next;

	ret = function_iterate_do_value (sheet, (FunctionIterateCallback)
					 callback_function_chitest_theoretical,
					 &p2, col, row, argv[1],
					 ei->error);
	if (ret == FALSE)
		return function_error (ei, gnumeric_err_NUM);

	tmp = p1.columns;
	while (tmp != NULL) {
	        g_slist_free(tmp->data);
		tmp = tmp->next;
	}
	g_slist_free(p1.columns),
	dof = p1.rows;

	FUNC_RETURN_VAL (value_new_float (1.0 - pchisq (p2.sum, dof)));
}

static char *help_betadist = {
	N_("@FUNCTION=BETADIST\n"
	   "@SYNTAX=BETADIST(x,alpha,beta[,a,b])\n"

	   "@DESCRIPTION="
	   "BETADIST function returns the cumulative beta distribution. @a "
	   "is the optional lower bound of @x and @b is the optinal upper "
	   "bound of @x.  If @a is not given, BETADIST uses 0.  If @b is "
	   "not given, BETADIST uses 1."
	   "\n"
	   "If @x < @a or @x > @b BETADIST returns #NUM! error. "
	   "If @alpha <= 0 or beta <= 0, BETADIST returns #NUM! error. "
	   "If @a >= @b BETADIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=BETAINV")
};

static FuncReturn *
gnumeric_betadist (FunctionEvalInfo *ei, Value **argv)
{
	float_t x, alpha, beta, a, b;

	x = value_get_as_float (argv [0]);
	alpha = value_get_as_float (argv [1]);
	beta = value_get_as_float (argv [2]);
	if (argv[3] == NULL)
	        a = 0;
	else
	        a = value_get_as_float (argv [3]);
	if (argv[4] == NULL)
	        b = 1;
	else
	        b = value_get_as_float (argv [4]);

	if (x<a || x>b || a>=b || alpha<=0 || beta<=0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (pbeta((x-a) / (b-a), alpha, beta)));
}

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
	   "If @alpha <= 0 or beta <= 0, BETAINV returns #NUM! error. "
	   "If @a >= @b BETAINV returns #NUM! error."
	   "\n"
	   "@SEEALSO=BETADIST")
};

static FuncReturn *
gnumeric_betainv (FunctionEvalInfo *ei, Value **argv)
{
	float_t p, alpha, beta, a, b;

	p = value_get_as_float (argv [0]);
	alpha = value_get_as_float (argv [1]);
	beta = value_get_as_float (argv [2]);
	if (argv[3] == NULL)
	        a = 0;
	else
	        a = value_get_as_float (argv [3]);
	if (argv[4] == NULL)
	        b = 1;
	else
	        b = value_get_as_float (argv [4]);

	if (p<0 || p>1 || a>=b || alpha<=0 || beta<=0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float ((b-a) * qbeta(p, alpha, beta) + a));
}

static char *help_tdist = {
	N_("@FUNCTION=TDIST\n"
	   "@SYNTAX=TDIST(x,dof,tails)\n"

	   "@DESCRIPTION="
	   "TDIST function returns the Student's t-distribution. @dof is "
	   "the degree of freedom and @tails is 1 or 2 depending on whether "
	   "you want one-tailed or two-tailed distribution."
	   "\n"
	   "If @dof < 1 TDIST returns #NUM! error. "
	   "If @tails is neither 1 or 2 TDIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=TINV,TTEST")
};

static FuncReturn *
gnumeric_tdist (FunctionEvalInfo *ei, Value **argv)
{
	float_t x;
	int     dof, tails;

	x = value_get_as_float (argv [0]);
	dof = value_get_as_int (argv [1]);
	tails = value_get_as_int (argv [2]);

	if (dof<1 || (tails!=1 && tails!=2))
		return function_error (ei, gnumeric_err_NUM);

	if (tails == 1)
	        FUNC_RETURN_VAL (value_new_float (1.0 - pt(x, dof)));
	else
	        FUNC_RETURN_VAL (value_new_float ((1.0 - pt(x, dof))*2));
}

static char *help_tinv = {
       N_("@FUNCTION=TINV\n"
          "@SYNTAX=TINV(p,dof)\n"

          "@DESCRIPTION="
          "The TINV function returns the inverse of the two-tailed Student's "
	  "t-distribution."
          "\n"
	  "If @p < 0 or @p > 1 or @dof < 1 TINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=TDIST,TTEST")
};

static FuncReturn *
gnumeric_tinv (FunctionEvalInfo *ei, Value **argv)
{
        float_t p;
	int dof;

        p = value_get_as_float (argv [0]);
	dof = value_get_as_int (argv [1]);

	if (p<0 || p>1 || dof<1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (qt (1 - p / 2, dof)));
}

static char *help_fdist = {
	N_("@FUNCTION=FDIST\n"
	   "@SYNTAX=FDIST(x,dof1,dof2)\n"

	   "@DESCRIPTION="
	   "FDIST function returns the F probability distribution. @dof1 "
	   "is the numerator degrees of freedom and @dof2 is the denominator "
	   "degrees of freedom."
	   "\n"
	   "If @x < 0 FDIST returns #NUM! error. "
	   "If @dof1 < 1 or @dof2 < 1, GAMMADIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=FINV")
};

static FuncReturn *
gnumeric_fdist (FunctionEvalInfo *ei, Value **argv)
{
	float_t x;
	int     dof1, dof2;

	x = value_get_as_float (argv [0]);
	dof1 = value_get_as_int (argv [1]);
	dof2 = value_get_as_int (argv [2]);

	if (x<0 || dof1<1 || dof2<1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (1.0 - pf(x, dof1, dof2)));
}

static char *help_finv = {
       N_("@FUNCTION=FINV\n"
          "@SYNTAX=TINV(p,dof)\n"

          "@DESCRIPTION="
          "The FINV function returns the inverse of the F probability "
	  "distribution."
          "\n"
	  "If @p < 0 or @p > 1 FINV returns #NUM! error. "
	  "If @dof1 < 1 or @dof2 < 1 FINV returns #NUM! error."
	  "\n"
          "@SEEALSO=FDIST")
};

static FuncReturn *
gnumeric_finv (FunctionEvalInfo *ei, Value **argv)
{
        float_t p;
	int dof1, dof2;

        p = value_get_as_float (argv [0]);
	dof1 = value_get_as_int (argv [1]);
	dof2 = value_get_as_int (argv [2]);

	if (p<0 || p>1 || dof1<1 || dof2<1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (qf (1.0 - p, dof1, dof2)));
}

static char *help_binomdist = {
	N_("@FUNCTION=BINOMDIST\n"
	   "@SYNTAX=BINOMDIST(n,trials,p,cumulative)\n"

	   "@DESCRIPTION="
	   "The BINOMDIST function returns the binomial distribution. "
	   "@n is the number of successes, @trials is the total number of "
           "independent trials, @p is the probability of success in trials, "
           "and @cumulative describes whether to return the sum of the"
           "binomial function from 0 to n."
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "if n or trials is a non-integer it is truncated. "
	   "if n < 0 or trials < 0 BINOMDIST returns #NUM! error. "
	   "if n > trials BINOMDIST returns #NUM! error. "
	   "if p < 0 or p > 1 BINOMDIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=POISSON")
};

static FuncReturn *
gnumeric_binomdist (FunctionEvalInfo *ei, Value **argv)
{
	int n, trials;
	float_t p;
	int cuml, err, x;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2]))
		return function_error (ei, gnumeric_err_VALUE);

	n = value_get_as_int (argv [0]);
	trials = value_get_as_int (argv [1]);
	p = value_get_as_float (argv[2]);
	if (n<0 || trials<0 || p<0 || p>1 || n>trials)
		return function_error (ei, gnumeric_err_NUM);

	cuml = value_get_as_bool (argv[3], &err);

	if (cuml){
		float_t v=0;
		for (x=0; x<=n; x++)
		        v += (combin(trials, x) * pow(p, x) *
				    pow(1-p, trials-x));
		FUNC_RETURN_VAL (value_new_float (v));
	} else
		FUNC_RETURN_VAL (value_new_float (combin(trials, n) * pow(p, n) *
						  pow(1-p, trials-n)));
}

static char *help_critbinom = {
       N_("@FUNCTION=CRITBINOM\n"
          "@SYNTAX=CRITBINOM(trials,p,alpha)\n"

          "@DESCRIPTION="
          "The CRITBINOM function returns the smallest value for which the"
          "cumulative is creater than or equal to a given value. "
          "@n is the number of trials, @p is the probability of success in "
          "trials, and @alpha is the criterion value. "
          "\n"
          "if trials is a non-integer it is truncated. "
          "if trials < 0 CRITBINOM returns #NUM! error. "
          "if p < 0 or p > 1 CRITBINOM returns #NUM! error. "
          "if alpha < 0 or alpha > 1 CRITBINOM returns #NUM! error. "
          "\n"
          "@SEEALSO=BINOMDIST")
};

static FuncReturn *
gnumeric_critbinom (FunctionEvalInfo *ei, Value **argv)
{
        int trials;
        float_t p, alpha, sum;
        int x;

        if (!VALUE_IS_NUMBER(argv[0]) ||
            !VALUE_IS_NUMBER(argv[1]) ||
            !VALUE_IS_NUMBER(argv[2]))
                return function_error (ei, gnumeric_err_VALUE);

        trials = value_get_as_int (argv [0]);
        p = value_get_as_float (argv[1]);
        alpha = value_get_as_float (argv[2]);

        if (trials<0 || p<0 || p>1 || alpha<0 || alpha>1)
	        return function_error (ei, gnumeric_err_NUM);

	sum = 0;
	for (x=0; sum<alpha; x++)
                sum += (combin(trials, x) * pow(p, x) * pow(1-p, trials-x));
	FUNC_RETURN_VAL (value_new_int (x-1));
}

static char *help_permut = {
	N_("@FUNCTION=PERMUT\n"
	   "@SYNTAX=PERMUT(n,k)\n"

	   "@DESCRIPTION="
	   "The PERMUT function returns the number of permutations. "
           "@n is the number of objects, @k is the number of objects in each "
           "permutation."
	   "\n"
	   "if n or k is non-integer PERMUT returns #VALUE! error. "
	   "if n = 0 PERMUT returns #NUM! error. "
	   "if n < k PERMUT returns #NUM! error."
	   "\n"
	   "@SEEALSO=COMBIN")
};

static FuncReturn *
gnumeric_permut (FunctionEvalInfo *ei, Value **argv)
{
	int n, k;

	if (argv[0]->type != VALUE_INTEGER ||
	    argv[1]->type != VALUE_INTEGER)
		return function_error (ei, gnumeric_err_VALUE);

	n = value_get_as_int (argv [0]);
	k = value_get_as_int (argv [1]);

	if (n<k || n==0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (fact(n) / fact(n-k)));
}

static char *help_hypgeomdist = {
	N_("@FUNCTION=HYPGEOMDIST\n"
	   "@SYNTAX=HYPGEOMDIST(x,n,M,N)\n"

	   "@DESCRIPTION="
	   "The HYPGEOMDIST function returns the hypergeometric distribution "
	   "@x is the number of successes in the sample, @n is the number "
           "of trials, @M is the number of successes overall, and @N is the"
           "population size."
	   "\n"
	   "if x,n,M or N is a non-integer it is truncated. "
	   "if x,n,M or N < 0 HYPGEOMDIST returns #NUM! error. "
	   "if x > M or n > N HYPGEOMDIST returns #NUM! error."
	   "\n"
	   "@SEEALSO=BINOMDIST,POISSON")
};

static FuncReturn *
gnumeric_hypgeomdist (FunctionEvalInfo *ei, Value **argv)
{
	int x, n, M, N;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2]))
		return function_error (ei, gnumeric_err_VALUE);

	x = value_get_as_int (argv [0]);
	n = value_get_as_int (argv [1]);
	M = value_get_as_int (argv [2]);
	N = value_get_as_int (argv [3]);

	if (x<0 || n<0 || M<0 || N<0 || x>M || n>N)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float ((combin(M, x) * combin(N-M, n-x)) / combin(N,n)));
}

static char *help_confidence = {
	N_("@FUNCTION=CONFIDENCE\n"
	   "@SYNTAX=CONFIDENCE(x,stddev,size)\n"

	   "@DESCRIPTION="
	   "The CONFIDENCE function returns the confidence interval for a mean. "
	   "@x is the significance level, @stddev is the standard deviation, "
	   "and @size is the size of the sample."
	   "\n"
	   "if size is non-integer it is truncated. "
	   "if size < 0 CONFIDENCE returns #NUM! error. "
	   "if size is 0 CONFIDENCE returns #DIV/0! error."
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static FuncReturn *
gnumeric_confidence (FunctionEvalInfo *ei, Value **argv)
{
	float_t x, stddev;
	int size;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2]))
		return function_error (ei, gnumeric_err_VALUE);

	x = value_get_as_float (argv [0]);
	stddev = value_get_as_float (argv [1]);
	size = value_get_as_int (argv [2]);
	if (size == 0)
		return function_error (ei, _("#DIV/0!"));

	if (size < 0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (-qnorm (x/2, 0, 1) * (stddev/sqrt(size))));
}

static char *help_standardize = {
	N_("@FUNCTION=STANDARDIZE\n"
	   "@SYNTAX=STANDARDIZE(x,mean,stdev)\n"

	   "@DESCRIPTION="
	   "The STANDARDIZE function returns a normalized value. "
	   "@x is the number to be normalized, @mean is the mean of the "
	   "distribution, @stdev is the standard deviation of the distribution."
	   "\n"
	   "If stddev is 0 STANDARDIZE returns #DIV/0! error."
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static FuncReturn *
gnumeric_standardize (FunctionEvalInfo *ei, Value **argv)
{
	float_t x, mean, stddev;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2]))
		return function_error (ei, gnumeric_err_VALUE);

	x = value_get_as_float (argv [0]);
	mean = value_get_as_float (argv [1]);
	stddev = value_get_as_float (argv [2]);
	if (stddev == 0)
		return function_error (ei, _("#DIV/0!"));

	FUNC_RETURN_VAL (value_new_float ((x-mean) / stddev));
}

static char *help_weibull = {
        N_("@FUNCTION=WEIBULL\n"
           "@SYNTAX=WEIBULL(x,alpha,beta,cumulative)\n"

           "@DESCRIPTION="
           "The WEIBULL function returns the Weibull distribution. "
           "If the cumulative boolean is true it will return: "
           "1 - exp (-(x/beta)^alpha), otherwise it will return "
           "(alpha/beta^alpha) * x^(alpha-1) * exp(-(x/beta^alpha)). "
           "\n"
           "if x < 0 WEIBULL returns #NUM! error. "
           "if alpha <= 0 or beta <= 0 WEIBULL returns #NUM! error. "
           "\n"
           "@SEEALSO=POISSON")
};

static FuncReturn *
gnumeric_weibull (FunctionEvalInfo *ei, Value **argv)
{
        float_t x, alpha, beta;
        int cuml, err=0;

        x = value_get_as_float (argv [0]);
        alpha = value_get_as_float (argv [1]);
        beta = value_get_as_float (argv [2]);
        if (x<0 || alpha<=0 || beta<=0)
                return function_error (ei, gnumeric_err_NUM);

        cuml = value_get_as_bool (argv[3], &err);
        if (err)
                return function_error (ei, gnumeric_err_VALUE);

        if (cuml)
                FUNC_RETURN_VAL (value_new_float (1.0 - exp(-pow(x/beta, alpha))));
        else
                FUNC_RETURN_VAL (value_new_float ((alpha/pow(beta, alpha))*
						  pow(x, alpha-1)*exp(-pow(x/beta, alpha))));
}

static char *help_normdist = {
        N_("@FUNCTION=NORMDIST\n"
           "@SYNTAX=NORMDIST(x,mean,stdev,cumulative)\n"

           "@DESCRIPTION="
           "The NORMDIST function returns the normal cumulative distribution. "
	   "@x is the value for which you want the distribution, @mean is "
	   "the mean of the distribution, @stdev is the standard deviation. "
           "\n"
           "If stdev is 0 NORMDIST returns #DIV/0! error. "
           "\n"
           "@SEEALSO=POISSON")
};


static FuncReturn *
gnumeric_normdist (FunctionEvalInfo *ei, Value **argv)
{
        float_t x, mean, stdev;
        int cuml, err=0;

        x = value_get_as_float (argv [0]);
        mean = value_get_as_float (argv [1]);
        stdev = value_get_as_float (argv [2]);

        if (stdev <= 0)
                return function_error (ei, _("#DIV/0!"));

        cuml = value_get_as_bool (argv[3], &err);
        if (err)
                return function_error (ei, gnumeric_err_VALUE);

        if (cuml)
		FUNC_RETURN_VAL (value_new_float (pnorm (x, mean, stdev)));
        else
		FUNC_RETURN_VAL (value_new_float (dnorm (x, mean, stdev)));
}

static char *help_norminv = {
       N_("@FUNCTION=NORMINV\n"
          "@SYNTAX=NORMINV(p,mean,stdev)\n"

          "@DESCRIPTION="
          "The NORMINV function returns the inverse of the normal "
	  "cumulative distribution. @p is the given probability corresponding "
	  "to the normal distribution, @mean is the arithmetic mean of the "
	  "distribution, and @stdev is the standard deviation of the "
	  "distribution."
          "\n"
	  "If @p < 0 or @p > 1 or @stdev <= 0 NORMINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=NORMDIST,NORMSDIST,NORMSINV,STANDARDIZE,ZTEST")
};

static FuncReturn *
gnumeric_norminv (FunctionEvalInfo *ei, Value **argv)
{
        float_t p, mean, stdev;

        p = value_get_as_float (argv [0]);
	mean = value_get_as_float (argv [1]);
	stdev = value_get_as_float (argv [2]);
	if (p < 0 || p > 1 || stdev <= 0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (qnorm (p, mean, stdev)));
}


static char *help_kurt = {
        N_("@FUNCTION=KURT\n"
           "@SYNTAX=KURT(n1, n2, ...)\n"

           "@DESCRIPTION="
           "KURT returns the kurtosis of a data set."
           "\n"
           "Strings and empty cells are simply ignored."
           "\n"
           "If fewer than four numbers are given or all of them are equal "
           "KURT returns #DIV/0! error."
	   "\n"
           "@SEEALSO=VAR")
};

typedef struct {
        guint32 num;
        float_t mean;
        float_t stddev;
        float_t sum;
} stat_kurt_sum_t;

static int
callback_function_kurt_sum (Sheet *sheet, Value *value,
			    ErrorMessage *error, void *closure)
{
        stat_kurt_sum_t *mm = closure;
        float_t x;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	x = (value_get_as_float (value) - mm->mean) / mm->stddev;
	mm->num++;
	mm->sum += (x * x) * (x * x);
	return TRUE;
}

static FuncReturn *
gnumeric_kurt (FunctionEvalInfo *ei, GList *expr_node_list)
{
        stat_kurt_sum_t pr;
	Value *vtmp;

	pr.num  = 0;
	pr.sum  = 0.0;

	vtmp = (Value *)gnumeric_average (ei, expr_node_list);

	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	pr.mean = value_get_as_float (vtmp);
	value_release (vtmp);

	vtmp = (Value *)gnumeric_stdev (ei, expr_node_list);
			       
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	pr.stddev = value_get_as_float (vtmp);
	value_release (vtmp);

	if (pr.stddev == 0.0)
	        return function_error (ei, gnumeric_err_NUM);

	function_iterate_argument_values (&ei->pos, callback_function_kurt_sum,
                                          &pr, expr_node_list, ei->error);

	if (pr.num < 4)
                return function_error (ei, gnumeric_err_NUM);
	else {
		float_t n, d, num, dem;

		n = pr.num;
		num = n * (n + 1);
		dem = (n - 1) * (n - 2) * (n - 3);
		d = (3 * (n - 1) * (n - 1)) / ((n - 2) * (n - 3));

		FUNC_RETURN_VAL (value_new_float (pr.sum * (num / dem) - d));
	}
}

static char *help_avedev = {
        N_("@FUNCTION=AVEDEV\n"
           "@SYNTAX=AVEDEV(n1, n2, ...)\n"

           "@DESCRIPTION="
           "AVEDEV returns the average of the absolute deviations of a data "
	   "set from their mean. "
           "\n"
           "Performing this function on a string or empty cell simply does nothing."
           "\n"
           "@SEEALSO=STDEV")
};

typedef struct {
        guint32 num;
        float_t mean;
        float_t sum;
} stat_avedev_sum_t;

static int
callback_function_stat_avedev_sum (Sheet *sheet, Value *value,
				   ErrorMessage *error, void *closure)
{
        stat_avedev_sum_t *mm = closure;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	mm->num++;
	mm->sum += fabs (value_get_as_float (value) - mm->mean);
        return TRUE;
}

static FuncReturn *
gnumeric_avedev (FunctionEvalInfo *ei, GList *expr_node_list)
{
        stat_avedev_sum_t pr;
	Value *vtmp;

        pr.num   = 0;
        pr.sum   = 0.0;

	vtmp = (Value *)gnumeric_average (ei, expr_node_list);
				 
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	pr.mean = value_get_as_float (vtmp);
	value_release (vtmp);

	function_iterate_argument_values (&ei->pos,
					  callback_function_stat_avedev_sum,
					  &pr, expr_node_list, ei->error);

	FUNC_RETURN_VAL (value_new_float (pr.sum / pr.num));
}

static char *help_devsq = {
        N_("@FUNCTION=DEVSQ\n"
           "@SYNTAX=DEVSQ(n1, n2, ...)\n"

           "@DESCRIPTION="
           "DEVSQ returns the sum of squares of deviations of a data set from "
           "the sample mean."
           "\n"
           "Strings and empty cells are simply ignored."
           "\n"
           "@SEEALSO=STDEV")
};


static FuncReturn *
gnumeric_devsq (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_closure_t cl;

	setup_stat_closure (&cl);

	function_iterate_argument_values (&ei->pos, callback_function_stat,
					  &cl, expr_node_list, ei->error);

	FUNC_RETURN_VAL (value_new_float (cl.Q));
}

static char *help_fisher = {
        N_("@FUNCTION=FISHER\n"
           "@SYNTAX=FISHER(x)\n"

           "@DESCRIPTION="
           "The FISHER function returns the Fisher transformation at x."
           "\n"
           "If x is not-number FISHER returns #VALUE! error."
           "If x<=-1 or x>=1 FISHER returns #NUM! error"
           "\n"
           "@SEEALSO=SKEW")
};

static FuncReturn *
gnumeric_fisher (FunctionEvalInfo *ei, Value **argv)
{
        float_t x;

        if (!VALUE_IS_NUMBER(argv [0]))
                return function_error (ei, gnumeric_err_VALUE);

        x = value_get_as_float (argv [0]);
        if (x <= -1.0 || x >= 1.0)
                return function_error (ei, gnumeric_err_NUM);

        FUNC_RETURN_VAL (value_new_float (0.5 * log((1.0+x) / (1.0-x))));
}

static char *help_poisson = {
	N_("@FUNCTION=POISSON\n"
	   "@SYNTAX=POISSON(x,mean,cumulative)\n"

	   "@DESCRIPTION="
	   "The POISSON function returns the Poisson distribution "
	   "@x is the number of events, @mean is the expected numeric value "
	   "@cumulative describes whether to return the sum of the "
	   "poisson function from 0 to x."
	   "\n"
	   "if x is a non-integer it is truncated. "
	   "if x <= 0 POISSON returns #NUM! error. "
	   "if mean <= 0 POISSON returns the #NUM! error."
	   "\n"
	   "@SEEALSO=POISSON")
};

static FuncReturn *
gnumeric_poisson (FunctionEvalInfo *ei, Value **argv)
{
	float_t x, mean;
	int cuml, err;

	x = value_get_as_int (argv [0]);
	mean = value_get_as_float (argv [1]);
	if (x<=0  || mean <=0)
		return function_error (ei, gnumeric_err_NUM);

	cuml = value_get_as_bool (argv[2], &err);

	if (cuml) {
	        int k;
		float_t sum = 0;

	        for (k=0; k<=x; k++)
		        sum += exp(-mean)*pow(mean,k) / exp (lgamma (k + 1));

		FUNC_RETURN_VAL (value_new_float (sum));
	} else
		FUNC_RETURN_VAL (value_new_float (exp(-mean)*pow(mean,x) /
						  exp (lgamma (x + 1))));
}

static char *help_pearson = {
	N_("@FUNCTION=PEARSON\n"
	   "@SYNTAX=PEARSON(array1,array2)\n"

	   "@DESCRIPTION="
	   "PEARSON returns the Pearson correllation coefficient of two data "
	   "sets."
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=INTERCEPT,LINEST,RSQ,SLOPE,STEYX")
};

static FuncReturn *
gnumeric_pearson (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_correl_t pr;
	float_t sum;
	int     count;
	GSList  *list1, *list2;
	Value *vtmp;

	vtmp = (Value *)gnumeric_count (ei, expr_node_list);
			       
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	count = value_get_as_int (vtmp);
	value_release (vtmp);

	if (count % 2 > 0)
		return function_error (ei, gnumeric_err_NUM);

	pr.count   = count / 2;
	pr.num     = 0;
	pr.sum1    = 0.0;
	pr.sum2    = 0.0;
	pr.sqrsum1 = 0.0;
	pr.sqrsum2 = 0.0;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_correl,
					  &pr, expr_node_list, ei->error);
					  
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL){
	        float_t *x, *y;
		x = list1->data;
		y = list2->data;
	        sum += *x * *y;
		g_free(x);
		g_free(y);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(pr.array1);
	g_slist_free(pr.array2);

	FUNC_RETURN_VAL (value_new_float (((pr.count*sum - pr.sum1*pr.sum2)) /
					  sqrt((pr.count*pr.sqrsum1 - pr.sum1*pr.sum1) *
					       (pr.count*pr.sqrsum2 - pr.sum2*pr.sum2))));
}

static char *help_rsq = {
	N_("@FUNCTION=RSQ\n"
	   "@SYNTAX=RSQ(array1,array2)\n"

	   "@DESCRIPTION="
	   "RSQ returns the square of the Pearson correllation coefficient "
	   "of two data sets."
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=CORREL,COVAR,INTERPCEPT,LINEST,LOGEST,PEARSON,SLOPE,"
	   "STEYX,TREND")
};

static FuncReturn *
gnumeric_rsq (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_correl_t pr;
	float_t sum, r;
	int     count;
	GSList  *list1, *list2;
	Value *vtmp;

	vtmp = (Value *)gnumeric_count (ei, expr_node_list);
			       
	if (!vtmp)
		FUNC_RETURN_VAL (NULL);
	count = value_get_as_int (vtmp);
	value_release (vtmp);
	if (count % 2 > 0)
		return function_error (ei, gnumeric_err_NUM);

	/* FIXME: what about count == 0?  */

	pr.count   = count / 2;
	pr.num     = 0;
	pr.sum1    = 0.0;
	pr.sum2    = 0.0;
	pr.sqrsum1 = 0.0;
	pr.sqrsum2 = 0.0;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_correl,
					  &pr, expr_node_list, ei->error);
					  
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL){
	        float_t *x, *y;
		x = list1->data;
		y = list2->data;
	        sum += *x * *y;
		g_free(x);
		g_free(y);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(pr.array1);
	g_slist_free(pr.array2);

	r = (((pr.count*sum - pr.sum1*pr.sum2)) /
	     sqrt((pr.count*pr.sqrsum1 - pr.sum1*pr.sum1) *
		  (pr.count*pr.sqrsum2 - pr.sum2*pr.sum2)));
	FUNC_RETURN_VAL (value_new_float (r * r));
}

static char *help_median = {
       N_("@FUNCTION=MEDIAN\n"
          "@SYNTAX=MEDIAN(n1, n2, ...)\n"

          "@DESCRIPTION="
          "MEDIAN returns the median of the given data set."
          "\n"
          "Strings and empty cells are simply ignored."
	  "If even numbers are given MEDIAN returns the average of the two "
	  "numbers in the middle."
          "\n"
          "@SEEALSO=AVERAGE,COUNT,COUNTA,DAVERAGE,MODE,SUM")
};

typedef struct {
	guint32 num;
        GSList  *list;
} stat_median_t;

static int
callback_function_median (Sheet *sheet, Value *value,
			  ErrorMessage *error, void *closure)
{
	stat_median_t *mm = closure;
	float_t *p;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	p = g_new (float_t, 1);
	*p = value_get_as_float (value);
	mm->list = g_slist_append (mm->list, p);
	mm->num++;
	return TRUE;
}

static FuncReturn *
gnumeric_median (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_median_t   p;
	GSList          *list;
	int             median_ind, n;
	float_t         median;

	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_median,
					  &p, expr_node_list, ei->error);

	median_ind = (p.num - 1) / 2;
	p.list = g_slist_sort (p.list, (GCompareFunc) float_compare);
	list  = p.list;

	/* FIXME: what about zero elements?  */

	/* Skip half of the list */
	for (n=0; n<median_ind; n++){
	        g_free(list->data);
		list = list->next;
	}

	if (p.num % 2)
	        median = *((float_t *) list->data);
	else
		/* FIXME: is this really right?  */
	        median = (*((float_t *) list->data) +
			  *((float_t *) list->next->data)) / 2.0;

	while (list != NULL){
	        g_free(list->data);
		list = list->next;
	}

	g_slist_free(p.list);

	FUNC_RETURN_VAL (value_new_float (median));
}

static char *help_large = {
	N_("@FUNCTION=LARGE\n"
	   "@SYNTAX=LARGE(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "LARGE returns the k-th largest value in a data set."
	   "\n"
	   "If data set is empty LARGE returns #NUM! error. "
	   "If k<=0 or k is greater than the number of data items given "
	   "LARGE returns #NUM! error."
	   "\n"
	   "@SEEALSO=PERCENTILE,PERCENTRANK,QUARTILE,SMALL")
};

static FuncReturn *
gnumeric_large (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_trimmean_t p;
	GSList          *list;
	int             n, k;
	float_t         r;

	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_trimmean,
					  &p, expr_node_list, ei->error);

	p.num--;
	k = ((int) p.last);

	if (p.num == 0 || k<=0 || k > p.num) {
		list  = p.list;
		while (list != NULL){
		        g_free(list->data);
			list = list->next;
		}
		g_slist_free(p.list);
		return function_error (ei, gnumeric_err_NUM);
	} else {
	        p.list = g_slist_sort (p.list, (GCompareFunc) float_compare_d);
		list  = p.list;
		--k;

		/* Skip the k largest values */
		for (n=0; n<k; n++){
		        g_free(list->data);
			list = list->next;
		}

		r = *((float_t *) list->data);

		while (list != NULL){
		        g_free(list->data);
			list = list->next;
		}

		g_slist_free(p.list);
	}
	FUNC_RETURN_VAL (value_new_float (r));
}

static char *help_small = {
	N_("@FUNCTION=SMALL\n"
	   "@SYNTAX=SMALL(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "SMALL returns the k-th smallest value in a data set."
	   "\n"
	   "If data set is empty SMALL returns #NUM! error. "
	   "If k<=0 or k is greater than the number of data items given "
	   "SMALL returns #NUM! error."
	   "\n"
	   "@SEEALSO=PERCENTILE,PERCENTRANK,QUARTILE,LARGE")
};

static FuncReturn *
gnumeric_small (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_trimmean_t p;
	GSList          *list;
	int             n, k;
	float_t         r;

	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (&ei->pos, callback_function_trimmean,
					  &p, expr_node_list, ei->error);

	p.num--;
	k = ((int) p.last);

	if (p.num == 0 || k<=0 || k > p.num){
		list  = p.list;
		while (list != NULL){
		        g_free(list->data);
			list = list->next;
		}
		g_slist_free(p.list);
		return function_error (ei, gnumeric_err_NUM);
	} else {
	        p.list = g_slist_sort (p.list, (GCompareFunc) float_compare);
		list  = p.list;
		--k;

		/* Skip the k largest values */
		for (n=0; n<k; n++){
		        g_free(list->data);
			list = list->next;
		}

		r = *((float_t *) list->data);

		while (list != NULL){
		        g_free(list->data);
			list = list->next;
		}

		g_slist_free(p.list);
	}
	FUNC_RETURN_VAL (value_new_float (r));
}

typedef struct {
        GSList *list;
        int    num;
} stat_list_t;

static int
callback_function_list (Sheet *sheet, int col, int row,
			Cell *cell, void *user_data)
{
        stat_list_t *mm = user_data;
	float_t *p;

	if (cell == NULL || cell->value == NULL)
	        return TRUE;
	if (!VALUE_IS_NUMBER (cell->value))
		return TRUE;

	p = g_new (float_t, 1);
	*p = value_get_as_float (cell->value);
	mm->list = g_slist_append (mm->list, p);
	mm->num++;
	return TRUE;
}

static char *help_prob = {
	N_("@FUNCTION=PROB\n"
	   "@SYNTAX=PROB(range_x,prob_range,lower_limit[,upper_limit])\n"

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
	   "entries, PROB returns #N/A! error."
	   "\n"
	   "@SEEALSO=BINOMDIST,CRITBINOM")
};

static FuncReturn *
gnumeric_prob (FunctionEvalInfo *ei, Value **argv)
{
        Value       *range_x = argv[0];
        Value       *prob_range = argv[1];
	stat_list_t items_x, items_prob;
	int         ret;
	float_t     sum, total_sum;
	float_t     lower_limit, upper_limit;
	GSList      *list1, *list2;

	items_x.num     = 0;
	items_x.list    = NULL;
	items_prob.num  = 0;
	items_prob.list = NULL;

        if (range_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  range_x->v.cell_range.cell_a.sheet, TRUE,
		  range_x->v.cell_range.cell_a.col,
		  range_x->v.cell_range.cell_a.row,
		  range_x->v.cell_range.cell_b.col,
		  range_x->v.cell_range.cell_b.row,
		  callback_function_list,
		  &items_x);
		if (ret == FALSE) {

			list1 = items_x.list;
			list2 = items_prob.list;
			while (list1 != NULL) {
			        g_free(list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free(list2->data);
				list2 = list2->next;
			}
			g_slist_free(items_x.list);
			g_slist_free(items_prob.list);

		        return function_error (ei, gnumeric_err_VALUE);
		}
	} else
		return function_error (ei, _("Array version not implemented!"));

        if (prob_range->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  prob_range->v.cell_range.cell_a.sheet, TRUE,
		  prob_range->v.cell_range.cell_a.col,
		  prob_range->v.cell_range.cell_a.row,
		  prob_range->v.cell_range.cell_b.col,
		  prob_range->v.cell_range.cell_b.row,
		  callback_function_list,
		  &items_prob);
		if (ret == FALSE) {

			list1 = items_x.list;
			list2 = items_prob.list;
			while (list1 != NULL) {
			        g_free(list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free(list2->data);
				list2 = list2->next;
			}
			g_slist_free(items_x.list);
			g_slist_free(items_prob.list);

		        return function_error (ei, gnumeric_err_VALUE);
		}
	} else
		return function_error (ei, _("Array version not implemented!"));

	if (items_x.num != items_prob.num) {
	        return function_error (ei, _("#N/A!"));

		list1 = items_x.list;
		list2 = items_prob.list;
		while (list1 != NULL) {
		        g_free(list1->data);
			list1 = list1->next;
		}
		while (list2 != NULL) {
		        g_free(list2->data);
			list2 = list2->next;
		}
		g_slist_free(items_x.list);
		g_slist_free(items_prob.list);

		FUNC_RETURN_VAL (NULL);
	}

	lower_limit = value_get_as_float (argv[2]);
	if (argv[3] == NULL)
	        upper_limit = lower_limit;
	else
	        upper_limit = value_get_as_float (argv[3]);

	list1 = items_x.list;
	list2 = items_prob.list;
	sum = total_sum = 0;

	while (list1 != NULL) {
	        float_t  x, prob;

		x = *((float_t *) list1->data);
		prob = *((float_t *) list2->data);

		if (prob <= 0 || prob > 1)
		        prob = 2; /* Force error in total sum check */

		total_sum += prob;

		if (x >= lower_limit && x <= upper_limit)
		        sum += prob;

		g_free(list1->data);
		g_free(list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(items_x.list);
	g_slist_free(items_prob.list);

	if (total_sum != 1)
	        return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (sum));
}

static char *help_steyx = {
	N_("@FUNCTION=PROB\n"
	   "@SYNTAX=PROB(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "STEYX function returns the standard error of the predicted "
	   "y-value for each x in the regression. "
	   "\n"
	   "If @known_y's and @known_x's are empty or have a different number "
	   "of arguments then STEYX returns #N/A! error."
	   "\n"
	   "@SEEALSO=PEARSON,RSQ,SLOPE")
};

static FuncReturn *
gnumeric_steyx (FunctionEvalInfo *ei, Value **argv)
{
        Value       *known_y = argv[0];
        Value       *known_x = argv[1];
	stat_list_t items_x, items_y;
	float_t     sum_x, sum_y, sum_xy, sqrsum_x, sqrsum_y;
	float_t     num, den, k, n;
	GSList      *list1, *list2;
	int         ret;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (known_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  known_x->v.cell_range.cell_a.sheet, TRUE,
		  known_x->v.cell_range.cell_a.col,
		  known_x->v.cell_range.cell_a.row,
		  known_x->v.cell_range.cell_b.col,
		  known_x->v.cell_range.cell_b.row,
		  callback_function_list,
		  &items_x);
		if (ret == FALSE) {
			list1 = items_x.list;
			list2 = items_y.list;
			while (list1 != NULL) {
			        g_free(list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free(list2->data);
				list2 = list2->next;
			}
			g_slist_free(items_x.list);
			g_slist_free(items_y.list);

		        return function_error (ei, gnumeric_err_VALUE);
		}
	} else
		return function_error (ei, _("Array version not implemented!"));

        if (known_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  known_y->v.cell_range.cell_a.sheet, TRUE,
		  known_y->v.cell_range.cell_a.col,
		  known_y->v.cell_range.cell_a.row,
		  known_y->v.cell_range.cell_b.col,
		  known_y->v.cell_range.cell_b.row,
		  callback_function_list,
		  &items_y);
		if (ret == FALSE) {
			list1 = items_x.list;
			list2 = items_y.list;
			while (list1 != NULL) {
			        g_free(list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free(list2->data);
				list2 = list2->next;
			}
			g_slist_free(items_x.list);
			g_slist_free(items_y.list);

		        return function_error (ei, gnumeric_err_VALUE);
		}
	} else
		return function_error (ei, _("Array version not implemented!"));

	if (items_x.num != items_y.num) {
		list1 = items_x.list;
		list2 = items_y.list;
		while (list1 != NULL) {
		        g_free(list1->data);
			list1 = list1->next;
		}
		while (list2 != NULL) {
		        g_free(list2->data);
			list2 = list2->next;
		}
		g_slist_free(items_x.list);
		g_slist_free(items_y.list);

	        return function_error (ei, _("#N/A!"));
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum_x = sum_y = 0;
	sqrsum_x = sqrsum_y = 0;
	sum_xy = 0;

	while (list1 != NULL) {
	        float_t  x, y;

		x = *((float_t *) list1->data);
		y = *((float_t *) list2->data);

		sum_x += x;
		sum_y += y;
		sqrsum_x += x*x;
		sqrsum_y += y*y;
		sum_xy += x*y;

		g_free(list1->data);
		g_free(list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(items_x.list);
	g_slist_free(items_y.list);

	n = items_x.num;
	k = 1.0 / (n*(n-2));
	num = n*sum_xy - sum_x*sum_y;
	num *= num;
	den = n*sqrsum_x - sum_x*sum_x;

	if (den == 0)
	        return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (sqrt(k * (n*sqrsum_y-sum_y*sum_y - num/den))));
}

static char *help_ztest = {
	N_("@FUNCTION=ZTEST\n"
	   "@SYNTAX=ZTEST(ref,x)\n"

	   "@DESCRIPTION="
	   "ZTEST returns the two-tailed probability of a z-test."
	   "\n"
	   "@ref is the data set and @x is the value to be tested."
	   "\n"
	   "If ref contains less than two data items ZTEST "
	   "returns #DIV/0! error."
	   "\n"
	   "@SEEALSO=CONFIDENCE,NORMDIST,NORMINV,NORMSDIST,NORMSINV,"
	   "STANDARDIZE")
};

typedef struct {
	guint32 num;
        float_t x;
        float_t sum;
        float_t sqrsum;
} stat_ztest_t;

static int
callback_function_ztest (Sheet *sheet, Value *value,
			 ErrorMessage *error, void *closure)
{
	stat_ztest_t *mm = closure;
	float_t last;

	if (!VALUE_IS_NUMBER (value))
		return FALSE;

	last = value_get_as_float (value);
	if (mm->num > 0) {
	        mm->sum += mm->x;
		mm->sqrsum += mm->x * mm->x;
	}
	mm->x = last;
	mm->num++;
	return TRUE;
}

static FuncReturn *
gnumeric_ztest (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_ztest_t p;
	int          status;
	float_t      stdev;

	p.num    = 0;
	p.sum    = 0;
	p.sqrsum = 0;

	status = function_iterate_argument_values (&ei->pos,
						   callback_function_ztest,
						   &p, expr_node_list, ei->error);

	if (status == FALSE)
	        return function_error (ei, gnumeric_err_VALUE);

	p.num--;
	if (p.num < 2)
	        return function_error (ei, _("#DIV/0!"));

	stdev = sqrt((p.sqrsum - p.sum*p.sum/p.num) / (p.num - 1));
	if (stdev == 0)
	        return function_error (ei, _("#DIV/0!"));

	FUNC_RETURN_VAL (value_new_float (1 - pnorm ((p.sum/p.num - p.x) /
						     (stdev / sqrt(p.num)), 0, 1)));
}

static char *help_averagea = {
	N_("@FUNCTION=AVERAGEA\n"
	   "@SYNTAX=AVERAGEA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "AVERAGEA returns the average of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted."
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static FuncReturn *
gnumeric_averagea (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value *result;
	Value *sum, *count;
	double c;

	sum = (Value *)gnumeric_suma (ei, expr_node_list);
			     
	if (!sum)
		FUNC_RETURN_VAL (NULL);

	count = (Value *)gnumeric_count (ei, expr_node_list);
				 
	if (!count){
		value_release (sum);
		FUNC_RETURN_VAL (NULL);
	}

	c = value_get_as_float (count);

	if (c == 0.0) {
		value_release (sum);
		return function_error (ei, _("#DIV/0!"));
	}

	result = value_new_float (value_get_as_float (sum) / c);

	value_release (count);
	value_release (sum);

	FUNC_RETURN_VAL (result);
}

static char *help_maxa = {
	N_("@FUNCTION=MAXA\n"
	   "@SYNTAX=MAXA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "MAXA returns the largest value of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted."
	   "\n"
	   "@SEEALSO=MAX,MINA")
};

static int
callback_function_mina_maxa (Sheet *sheet, Value *value,
			     ErrorMessage *error, void *closure)
{
	min_max_closure_t *mm = closure;

	switch (value->type){
	case VALUE_INTEGER:
		if (mm->found){
			if (mm->operation == OPER_MIN){
				if (value->v.v_int < mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_int;
			} else {
				if (value->v.v_int > mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_int;
			}
		} else {
			mm->found = 1;
			mm->result->v.v_float = value->v.v_int;
		}
		break;

	case VALUE_FLOAT:
		if (mm->found){
			if (mm->operation == OPER_MIN){
				if (value->v.v_float < mm->result->v.v_float)
					mm->result->v.v_float =
					  value->v.v_float;
			} else {
				if (value->v.v_float > mm->result->v.v_float)
					mm->result->v.v_float =
					  value->v.v_float;
			}
		} else {
			mm->found = 1;
			mm->result->v.v_float = value->v.v_float;
		}
		break;

	default:
		if (mm->found){
			if (mm->operation == OPER_MIN){
				if (0 < mm->result->v.v_float)
					mm->result->v.v_float = 0;
			} else {
				if (0 > mm->result->v.v_float)
					mm->result->v.v_float = 0;
			}
		} else {
			mm->found = 1;
			mm->result->v.v_float = 0;
		}
		break;
	}

	return TRUE;
}

static FuncReturn *
gnumeric_maxa (FunctionEvalInfo *ei, GList *expr_node_list)
{
	min_max_closure_t closure;

	closure.operation = OPER_MAX;
	closure.found  = 0;
	closure.result = value_new_float (0);

	function_iterate_argument_values (&ei->pos, callback_function_mina_maxa,
					  &closure, expr_node_list, ei->error);
					  
	FUNC_RETURN_VAL (	closure.result);
}

static char *help_mina = {
	N_("@FUNCTION=MINA\n"
	   "@SYNTAX=MINA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "MINA returns the smallest value of the given arguments.  Numbers, "
	   "text and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates to FALSE, "
	   "it is counted as value zero (0).  If the argument evaluates to "
	   "TRUE, it is counted as one (1).  Note that empty cells are not "
	   "counted."
	   "\n"
	   "@SEEALSO=MIN,MAXA")
};

static FuncReturn *
gnumeric_mina (FunctionEvalInfo *ei, GList *expr_node_list)
{
	min_max_closure_t closure;

	closure.operation = OPER_MIN;
	closure.found  = 0;
	closure.result = value_new_float (0);

	function_iterate_argument_values (&ei->pos, callback_function_mina_maxa,
					  &closure, expr_node_list, ei->error);
					  
	FUNC_RETURN_VAL (	closure.result);
}

static char *help_vara = {
	N_("@FUNCTION=VARA\n"
	   "@SYNTAX=VARA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "VARA returns the variance based on a sample.  Numbers, text "
	   "and logical values are included in the calculation too. "
	   "If the cell contains text or the argument evaluates"
	   "to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted."
	   "\n"
	   "@SEEALSO=VAR,VARPA")
};

static FuncReturn *
gnumeric_vara (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_closure_t cl;

	setup_stat_closure (&cl);
	cl.afun_flag = 1;

	function_iterate_argument_values (&ei->pos, callback_function_stat,
					  &cl, expr_node_list, ei->error);

	if (cl.N <= 1)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (cl.Q / (cl.N - 1)));
}

static char *help_varpa = {
	N_("@FUNCTION=VARPA\n"
	   "@SYNTAX=VARPA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "VARPA returns the variance based on the entire population.  "
	   "Numbers, text and logical values are included in the "
	   "calculation too.  If the cell contains text or the argument "
	   "evaluates to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted."
	   "\n"
	   "@SEEALSO=VARP,VARP")
};

static FuncReturn *
gnumeric_varpa (FunctionEvalInfo *ei, GList *expr_node_list)
{
	stat_closure_t cl;

	setup_stat_closure (&cl);
	cl.afun_flag = 1;

	function_iterate_argument_values (&ei->pos, callback_function_stat,
					  &cl, expr_node_list, ei->error);

	if (cl.N <= 0)
		return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (cl.Q / cl.N));
}

static char *help_stdeva = {
	N_("@FUNCTION=STDEVA\n"
	   "@SYNTAX=STDEVA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "STDEVA returns the standard deviation based on a sample. "
	   "Numbers, text and logical values are included in the calculation "
	   "too.  If the cell contains text or the argument evaluates to "
	   "FALSE, it is counted as value zero (0).  If the argument "
	   "evaluates to TRUE, it is counted as one (1).  Note that empty "
	   "cells are not counted."
	   "\n"
	   "@SEEALSO=STDEV,STDEVPA")
};

static FuncReturn *
gnumeric_stdeva (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value *var, *ans;

	var = (Value *)gnumeric_vara (ei, expr_node_list);
	if (!var)
		FUNC_RETURN_VAL (NULL);
	ans = value_new_float (sqrt (value_get_as_float (var)));
	value_release (var);
	FUNC_RETURN_VAL (ans);
}

static char *help_stdevpa = {
	N_("@FUNCTION=STDEVPA\n"
	   "@SYNTAX=STDEVPA(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "STDEVPA returns the standard deviation based on the entire "
	   "population.  Numbers, text and logical values are included in "
	   "the calculation too.  If the cell contains text or the argument "
	   "evaluates to FALSE, it is counted as value zero (0).  If the "
	   "argument evaluates to TRUE, it is counted as one (1).  Note "
	   "that empty cells are not counted."
	   "\n"
	   "@SEEALSO=STDEVA,STDEVP")
};

static FuncReturn *
gnumeric_stdevpa (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value *var, *ans;

	var = (Value *)gnumeric_varpa (ei, expr_node_list);
	if (!var)
		FUNC_RETURN_VAL (NULL);
	ans = value_new_float (sqrt (value_get_as_float (var)));
	value_release (var);
	FUNC_RETURN_VAL (ans);
}

static char *help_slope = {
	N_("@FUNCTION=SLOPE\n"
	   "@SYNTAX=SLOPE(known_y's,known_x's)\n"

	   "@DESCRIPTION="
	   "SLOPE returns the slope of the linear regression line."
	   "\n"
	   "@SEEALSO=STDEV,STDEVPA")
};

static FuncReturn *
gnumeric_slope (FunctionEvalInfo *ei, Value **argv)
{
        Value       *known_y = argv[0];
        Value       *known_x = argv[1];
	stat_list_t items_x, items_y;
	float_t     sum_x, sum_y, sum_xy, sqrsum_x, sqrsum_y;
	float_t     num, den, n;
	GSList      *list1, *list2;
	int         ret;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (known_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  known_x->v.cell_range.cell_a.sheet, TRUE,
		  known_x->v.cell_range.cell_a.col,
		  known_x->v.cell_range.cell_a.row,
		  known_x->v.cell_range.cell_b.col,
		  known_x->v.cell_range.cell_b.row,
		  callback_function_list,
		  &items_x);
		if (ret == FALSE) {
			list1 = items_x.list;
			list2 = items_y.list;
			while (list1 != NULL) {
			        g_free(list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free(list2->data);
				list2 = list2->next;
			}
			g_slist_free(items_x.list);
			g_slist_free(items_y.list);

		        return function_error (ei, gnumeric_err_VALUE);
		}
	} else
		return function_error (ei, _("Array version not implemented!"));

        if (known_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  known_y->v.cell_range.cell_a.sheet, TRUE,
		  known_y->v.cell_range.cell_a.col,
		  known_y->v.cell_range.cell_a.row,
		  known_y->v.cell_range.cell_b.col,
		  known_y->v.cell_range.cell_b.row,
		  callback_function_list,
		  &items_y);
		if (ret == FALSE) {
			list1 = items_x.list;
			list2 = items_y.list;
			while (list1 != NULL) {
			        g_free(list1->data);
				list1 = list1->next;
			}
			while (list2 != NULL) {
			        g_free(list2->data);
				list2 = list2->next;
			}
			g_slist_free(items_x.list);
			g_slist_free(items_y.list);

		        return function_error (ei, gnumeric_err_VALUE);
		}
	} else
		return function_error (ei, _("Array version not implemented!"));

	if (items_x.num != items_y.num) {
		list1 = items_x.list;
		list2 = items_y.list;
		while (list1 != NULL) {
		        g_free(list1->data);
			list1 = list1->next;
		}
		while (list2 != NULL) {
		        g_free(list2->data);
			list2 = list2->next;
		}
		g_slist_free(items_x.list);
		g_slist_free(items_y.list);

	        return function_error (ei, _("#N/A!"));
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum_x = sum_y = 0;
	sqrsum_x = sqrsum_y = 0;
	sum_xy = 0;

	while (list1 != NULL) {
	        float_t  x, y;

		x = *((float_t *) list1->data);
		y = *((float_t *) list2->data);

		sum_x += x;
		sum_y += y;
		sqrsum_x += x*x;
		sqrsum_y += y*y;
		sum_xy += x*y;

		g_free(list1->data);
		g_free(list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(items_x.list);
	g_slist_free(items_y.list);

	n = items_x.num;
	num = n*sum_xy - sum_x*sum_y;
	den = n*sqrsum_x - sum_x*sum_x;

	if (den == 0)
	        return function_error (ei, gnumeric_err_NUM);

	FUNC_RETURN_VAL (value_new_float (num / den));
}

static char *help_percentrank = {
	N_("@FUNCTION=PERCENTRANK\n"
	   "@SYNTAX=PERCENTRANK(array,x[,significance])\n"

	   "@DESCRIPTION="
	   "PERCENTRANK function returns the rank of a data point in a data "
	   "set.  @array is the range of numeric values, @x is the data "
	   "point which you want to rank, and the optional @significance "
	   "indentifies the number of significant digits for the returned "
	   "value.  If @significance is omitted, PERCENTRANK uses three "
	   "digits."
	   "\n"
	   "If @array contains not data points, PERCENTRANK returns #NUM! "
	   "error. "
	   "If @significance is less than one, PERCENTRANK returns #NUM! "
	   "error. "
	   "If @x does not match any of the values in @array or @x matches "
	   "more than once, PERCENTRANK interpolates the returned value."
	   "\n"
	   "@SEEALSO=LARGE,MAX,MEDIAN,MIN,PERCENTILE,QUARTILE,SMALL")
};

typedef struct {
        float_t x;
        int     smaller;
        int     greater;
        int     equal;
} stat_percentrank_t;

static int
callback_function_percentrank (Sheet *sheet, Value *value,
			       ErrorMessage *error, void *user_data)
{
        stat_percentrank_t *p = user_data;
	float_t y;

	if (!VALUE_IS_NUMBER (value))
		return FALSE;

	y = value_get_as_float (value);

	if (y < p->x)
	        p->smaller++;
	else if (y > p->x)
	        p->greater++;
	else
	        p->equal++;

	return TRUE;
}

static FuncReturn *
gnumeric_percentrank (FunctionEvalInfo *ei, Value **argv)
{
        stat_percentrank_t p;
	Sheet              *sheet;
        float_t            x, k, pr;
	int                col, row, n;
        int                significance, ret;

	x = value_get_as_float (argv[1]);

	p.smaller = 0;
	p.greater = 0;
	p.equal = 0;
	p.x = x;

        if (argv[2] == NULL)
	        significance = 3;
	else {
	        significance = value_get_as_int (argv[2]);
		if (significance < 1)
		        return function_error (ei, gnumeric_err_NUM);
	}

	sheet = argv[0]->v.cell.sheet;
	col = argv[0]->v.cell.col;
	row = argv[0]->v.cell.row;

	ret = function_iterate_do_value (sheet, (FunctionIterateCallback)
					 callback_function_percentrank,
					 &p, col, row, argv[0],
					 ei->error);

	if (ret == FALSE || (p.smaller+p.greater+p.equal==0))
	        return function_error (ei, gnumeric_err_NUM);

	if (p.equal == 1)
	        pr = (float_t) p.smaller / (p.smaller+p.greater);
	else
	        pr = (p.smaller + 0.5 * p.equal) /
		  (p.smaller + p.equal + p.greater);

	k = 1;
	for (n=0; n<significance; n++)
	        k *= 10;

	pr *= k;
	pr = floor(pr);
	pr /= k;

	FUNC_RETURN_VAL (value_new_float (pr));
}


void stat_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Statistics"));

        function_add_nodes (cat, "avedev",    0,      "",          &help_avedev,
			    gnumeric_avedev);
	function_add_nodes (cat, "average", 0,      "",            &help_average,
			    gnumeric_average);
	function_add_nodes (cat, "averagea", 0,      "",           &help_averagea,
			    gnumeric_averagea);
	function_add_args  (cat, "betadist", "fff|ff", "",         &help_betadist,
			    gnumeric_betadist);
	function_add_args  (cat, "betainv", "fff|ff", "",          &help_betainv,
			    gnumeric_betainv);
	function_add_args  (cat, "binomdist", "fffb", "n,t,p,c",   &help_binomdist,
			    gnumeric_binomdist);
	function_add_args  (cat, "chidist",   "ff",  "",           &help_chidist,
			    gnumeric_chidist);
	function_add_args  (cat, "chiinv",    "ff",  "",           &help_chiinv,
			    gnumeric_chiinv);
	function_add_args  (cat, "chitest",   "rr",  "",           &help_chitest,
			    gnumeric_chitest);
	function_add_args  (cat, "confidence", "fff",  "x,stddev,size", &help_confidence,
			    gnumeric_confidence);
	function_add_nodes (cat, "count",     0,      "",          &help_count,
			    gnumeric_count);
	function_add_nodes (cat, "counta",    0,      "",          &help_counta,
			    gnumeric_counta);
	function_add_args  (cat, "critbinom",  "fff",  "trials,p,alpha", &help_critbinom,
			    gnumeric_critbinom);
        function_add_nodes (cat, "correl",     0,      "",         &help_correl,
			    gnumeric_correl);
        function_add_nodes (cat, "covar",      0,      "",         &help_covar,
			    gnumeric_covar);
        function_add_nodes (cat, "devsq",      0,      "",         &help_devsq,
			    gnumeric_devsq);
	function_add_args  (cat, "permut",    "ff",  "n,k",        &help_permut,
			    gnumeric_permut);
	function_add_args  (cat, "poisson",   "ffb",  "",          &help_poisson,
			    gnumeric_poisson);
	function_add_args  (cat, "expondist", "ffb",  "",          &help_expondist,
			    gnumeric_expondist);
	function_add_args  (cat, "fdist",   "fff",  "",            &help_fdist,
			    gnumeric_fdist);
	function_add_args  (cat, "finv",   "fff",  "",             &help_finv,
			    gnumeric_finv);
        function_add_args  (cat, "fisher",    "f",    "",          &help_fisher,
			    gnumeric_fisher);
        function_add_args  (cat, "fisherinv", "f",    "",          &help_fisherinv,
			    gnumeric_fisherinv);
	function_add_args  (cat, "gammaln",   "f",    "number",    &help_gammaln,
			    gnumeric_gammaln);
	function_add_args  (cat, "gammadist", "fffb", "number,alpha,beta,cum",    &help_gammadist,
			    gnumeric_gammadist);
	function_add_args  (cat, "gammainv", "fff",   "number,alpha,beta",        &help_gammainv,
			    gnumeric_gammainv);
	function_add_nodes (cat, "geomean",   0,      "",          &help_geomean,
			    gnumeric_geomean);
	function_add_nodes (cat, "harmean",   0,      "",          &help_harmean,
			    gnumeric_harmean);
	function_add_args  (cat, "hypgeomdist", "ffff", "x,n,M,N", &help_hypgeomdist,
			    gnumeric_hypgeomdist);
        function_add_nodes (cat, "kurt",      0,      "",          &help_kurt,
			    gnumeric_kurt);
	function_add_nodes (cat, "large",  0,      "",             &help_large,
			    gnumeric_large);
	function_add_args  (cat, "loginv",  "fff",  "",            &help_loginv,
			    gnumeric_loginv);
	function_add_args  (cat, "lognormdist",  "fff",  "",       &help_lognormdist,
			    gnumeric_lognormdist);
	function_add_nodes (cat, "max",     0,      "",            &help_max,
			    gnumeric_max);
	function_add_nodes (cat, "maxa",    0,      "",            &help_maxa,
			    gnumeric_maxa);
	function_add_nodes (cat, "median",    0,      "",          &help_median,
			    gnumeric_median);
	function_add_nodes (cat, "min",     0,      "",            &help_min,
			    gnumeric_min);
	function_add_nodes (cat, "mina",    0,      "",            &help_mina,
			    gnumeric_mina);
	function_add_nodes (cat, "mode",      0,      "",          &help_mode,
			    gnumeric_mode);
	function_add_args  (cat, "negbinomdist", "fff", "f,t,p",   &help_negbinomdist,
			    gnumeric_negbinomdist);
	function_add_args  (cat, "normdist",   "fffb",  "",        &help_normdist,
			    gnumeric_normdist);
	function_add_args  (cat, "norminv",    "fff",  "",         &help_norminv,
			    gnumeric_norminv);
	function_add_args  (cat, "normsdist",  "f",  "",           &help_normsdist,
			    gnumeric_normsdist);
	function_add_args  (cat, "normsinv",  "f",  "",            &help_normsinv,
			    gnumeric_normsinv);
	function_add_args  (cat, "percentrank", "Af|f", "array,x,significance",
			    &help_percentrank,   gnumeric_percentrank);
	function_add_nodes (cat, "pearson",   0,      "",          &help_pearson,
			    gnumeric_pearson);
	function_add_args  (cat, "prob", "AAf|f", "x_range,prob_range,lower_limit,upper_limit",
			    &help_prob,   gnumeric_prob);
	function_add_nodes (cat, "rank",      0,      "",          &help_rank,
			    gnumeric_rank);
	function_add_nodes (cat, "rsq",       0,      "",          &help_rsq,
			    gnumeric_rsq);
	function_add_nodes (cat, "skew",      0,      "",          &help_skew,
			    gnumeric_skew);
	function_add_args  (cat, "slope", "AA", "known_y's,known_x's",
			    &help_slope,   gnumeric_slope);
	function_add_nodes (cat, "small",  0,      "",             &help_small,
			    gnumeric_small);
	function_add_args  (cat, "standardize", "fff",  "x,mean,stddev", &help_standardize,
			    gnumeric_standardize);
	function_add_nodes (cat, "stdev",     0,      "",          &help_stdev,
			    gnumeric_stdev);
	function_add_nodes (cat, "stdeva",    0,      "",          &help_stdeva,
			    gnumeric_stdeva);
	function_add_nodes (cat, "stdevp",    0,      "",          &help_stdevp,
			    gnumeric_stdevp);
	function_add_nodes (cat, "stdevpa",   0,      "",          &help_stdevpa,
			    gnumeric_stdevpa);
	function_add_args  (cat, "steyx", "AA", "known_y's,known_x's",
			    &help_steyx,   gnumeric_steyx);
	function_add_args  (cat, "tdist",   "fff",  "",            &help_tdist,
			    gnumeric_tdist);
	function_add_args  (cat, "tinv",    "ff",  "",             &help_tinv,
			    gnumeric_tinv);
	function_add_nodes (cat, "trimmean",  0,      "",          &help_trimmean,
			    gnumeric_trimmean);
	function_add_nodes (cat, "var",       0,      "",          &help_var,
			    gnumeric_var);
	function_add_nodes (cat, "vara",      0,      "",          &help_vara,
			    gnumeric_vara);
	function_add_nodes (cat, "varp",      0,      "",          &help_varp,
			    gnumeric_varp);
	function_add_nodes (cat, "varpa",     0,      "",          &help_varpa,
			    gnumeric_varpa);
        function_add_args  (cat, "weibull", "fffb",  "",           &help_weibull,
			    gnumeric_weibull);
	function_add_nodes (cat, "ztest",  0,      "",             &help_ztest,
			    gnumeric_ztest);
}
