/*
 * fn-stat.c:  Built in statistical functions and functions registration
 *
 * Authors:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *  Morten Welinder <terra@diku.dk>
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static float_t get_positive_normdist(float_t x);

static guint
float_hash (const float_t *d)
{
        return (guint) ((*d)*100) ;
}

static gint
float_equal (const float_t *a, const float_t *b)
{
	if (*a==*b)
	        return 1 ;
	return 0 ;
}

static gint
float_compare (const float_t *a, const float_t *b)
{
        if (*a<*b)
                return -1;
	else if (*a==*b)
	        return 0;
	else
	        return 1;
}

static gint
float_compare_d (const float_t *a, const float_t *b)
{
        if (*a>*b)
                return -1;
	else if (*a==*b)
	        return 0;
	else
	        return 1;
}

#if 0
/* help template */
static char *help_ = {
	N_("@FUNCTION=NAME\n"
	   "@SYNTAX=(b1, b2, ...)\n"

	   "@DESCRIPTION"
	   ""
	   "\n"

	   ""
	   ""
	   "\n"
	   
	   ""
	   ""
	   ""
	   ""
	   "@SEEALSO=")
};

#endif

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



typedef struct {
	int N;
	float_t M, Q;
} stat_closure_t;

static void
setup_stat_closure (stat_closure_t *cl)
{
	cl->N = 0 ;
	cl->M = 0.0 ;
	cl->Q = 0.0 ;
}

static int
callback_function_stat (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_closure_t *mm = closure;
	float_t x, dx, dm;

	switch (value->type){
	case VALUE_INTEGER:
		x = value->v.v_int;
		break;
	case VALUE_FLOAT:
		x = value->v.v_float;
		break;
	default:
		/* ignore strings */
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

static Value *
gnumeric_varp (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_closure_t cl;
	Sheet *sheet = (Sheet *) tsheet;
	float_t ans, num ;

	setup_stat_closure (&cl) ;

	function_iterate_argument_values (sheet, callback_function_stat,
					  &cl, expr_node_list,
					  eval_col, eval_row, error_string);

	/* FIXME -- what about no arguments?  */
	return value_float (cl.Q / cl.N);
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

static Value *
gnumeric_var (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_closure_t cl;
	Sheet *sheet = (Sheet *) tsheet;
	float_t ans, num ;

	setup_stat_closure (&cl) ;

	function_iterate_argument_values (sheet, callback_function_stat,
					  &cl, expr_node_list,
					  eval_col, eval_row, error_string);

	/* FIXME -- what about no arguments or just one argument?  */
	return value_float (cl.Q / (cl.N - 1));
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

static Value *
gnumeric_stdev (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *ans = gnumeric_var (tsheet, expr_node_list, eval_col, eval_row, error_string) ;
	if(ans && ans->type==VALUE_FLOAT)
		ans->v.v_float = sqrt(ans->v.v_float) ;
	return ans ;
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

static Value *
gnumeric_stdevp (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *ans = gnumeric_varp (tsheet, expr_node_list, eval_col, eval_row, error_string) ;
	if(ans && ans->type==VALUE_FLOAT)
		ans->v.v_float = sqrt(ans->v.v_float) ;
	return ans ;
}

static char *help_harmean = {
	N_("@FUNCTION=HARMEAN\n"
	   "@SYNTAX=HARMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "HARMEAN returns the harmonic mean of the N data"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=GEOMEAN,MEDIAN,MEAN,MODE")
};

typedef struct {
	int first ;
	guint32 num ;
	float_t sum;
} stat_inv_sum_t;

static int
callback_function_stat_inv_sum (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_inv_sum_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++ ;
		mm->sum = mm->first?1.0/value->v.v_int:mm->sum+1.0/value->v.v_int ;
		break;

	case VALUE_FLOAT:
		mm->num++ ;
		mm->sum = mm->first?1.0/value->v.v_float:mm->sum+1.0/value->v.v_float ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static char *help_rank = {
	N_("@FUNCTION=RANK\n"
	   "@SYNTAX=RANK(x,ref,order)\n"

	   "@DESCRIPTION="
	   "RANK returns the rank of a number in a list of numbers. @x is the "
	   "number whose rank you want to find, @ref is the list of numbers, "
	   "and @order specifies how to rank numbers. If order is 0 numbers "
	   "are rank in descending order, otherwise numbers are rank in "
	   "ascending order. "
	   "\n"
	   "@SEEALSO=PERCENTRANK")
};

typedef struct {
	int     first ;
	guint32 num ;
        float_t x ;
        float_t last ;
        GSList  *list;
} stat_rank_t;

static int
callback_function_rank (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_rank_t *mm = closure;
	gpointer     p;

	switch (value->type){
	case VALUE_INTEGER:
	        if (mm->first == TRUE)
		        mm->x = value->v.v_int;
		else if (mm->num == 1)
		        mm->last = value->v.v_int;
		else {
	                p = g_new(float_t, 1);
			*((float_t *) p) = mm->last;
			mm->list = g_slist_append(mm->list, p);
			mm->last = value->v.v_int;
		}
		mm->num++ ;
		break ;
	case VALUE_FLOAT:
	        if (mm->first == TRUE)
		        mm->x = value->v.v_float;
		else if (mm->num == 1)
		        mm->last = value->v.v_float;
		else {
	                p = g_new(float_t, 1);
			*((float_t *) p) = mm->last;
			mm->list = g_slist_append(mm->list, p);
			mm->last = value->v.v_float;
		}
		mm->num++ ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_rank (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_rank_t p;
	Sheet       *sheet = (Sheet *) tsheet;
	GSList      *list;
	int         order, rank;

	p.first = TRUE ;
	p.num   = 0 ;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_rank,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);
	list  = p.list;
	order = (int) p.last;
	rank  = 1;

	while (list != NULL) {
	        gpointer x;
		x = list->data;
		if (order) {
		        if (*((float_t *) x) < p.x)
			        rank++;
		} else {
		        if (*((float_t *) x) > p.x)
			        rank++;
		}
		g_free(x);
		list = list->next;
	}

	g_slist_free(p.list);

	return value_int (rank) ;
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
	   "top and 4 from the bottom of the set. "
	   "\n"
	   "@SEEALSO=AVERAGE,GEOMEAN,HARMEAN,MEDIAN,MODE")
};

typedef struct {
	int     first ;
	guint32 num ;
        float_t last ;
        GSList  *list;
} stat_trimmean_t;

static int
callback_function_trimmean (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_trimmean_t *mm = closure;
	gpointer        p;

	switch (value->type){
	case VALUE_INTEGER:
		if (mm->first == TRUE)
		        mm->last = value->v.v_int;
		else {
	                p = g_new(float_t, 1);
			*((float_t *) p) = mm->last;
			mm->list = g_slist_append(mm->list, p);
			mm->last = value->v.v_int;
		}
		mm->num++ ;
		break ;
	case VALUE_FLOAT:
	        if (mm->first == TRUE)
		        mm->last = value->v.v_float;
		else {
	                p = g_new(float_t, 1);
			*((float_t *) p) = mm->last;
			mm->list = g_slist_append(mm->list, p);
			mm->last = value->v.v_float;
		}
		mm->num++ ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_trimmean (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_trimmean_t p;
	Sheet       *sheet = (Sheet *) tsheet;
	GSList      *list;
	int         trim_count, n, count;
	float_t     sum;

	p.first = TRUE ;
	p.num   = 0 ;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_trimmean,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	p.num--;
	trim_count = (p.num * p.last) / 2;
	count = p.num - 2 * trim_count;
	p.list = g_slist_sort (p.list, (GCompareFunc) float_compare);
	list  = p.list;

	/* Skip the trimmed numbers in the beginning of the list */
	for (n=0; n<trim_count; n++) {
	        g_free(list->data);
		list = list->next;
	}

	/* Count the sum for mean */
	for (n=sum=0; n<count; n++) {
	        gpointer x;
		x = list->data;
		sum += *((float_t *) x);
		g_free(x);
		list = list->next;
	}

	/* Free the rest of the number on the list */
	for (n=0; n<trim_count; n++) {
	        g_free(list->data);
		list = list->next;
	}

	g_slist_free(p.list);

	return value_float (sum / count) ;
}

static char *help_covar = {
	N_("@FUNCTION=COVAR\n"
	   "@SYNTAX=COVAR(array1,array2)\n"

	   "@DESCRIPTION="
	   "COVAR returns the covariance of two data sets. "
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=CORREL,FISHER,FISHERINV")
};

typedef struct {
	int     first ;
	guint32 num ;
	int     count ;
        GSList  *array1;
        GSList  *array2;
        float_t sum1;
        float_t sum2;
} stat_covar_t;

static int
callback_function_covar (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_covar_t *mm = closure;
	gpointer     p;

	switch (value->type){
	case VALUE_INTEGER:
	        p = g_new(float_t, 1);
		*((float_t *) p) = (float_t) value->v.v_int;
	        if (mm->num < mm->count) {
		        mm->array1 = g_slist_append(mm->array1, p);
			mm->sum1 += value->v.v_int;
		} else {
		        mm->array2 = g_slist_append(mm->array2, p);
			mm->sum2 += value->v.v_int;
		}
		mm->num++ ;
		break ;
	case VALUE_FLOAT:
	        p = g_new(float_t, 1);
		*((float_t *) p) = value->v.v_float;
	        if (mm->num < mm->count) {
		        mm->array1 = g_slist_append(mm->array1, p);
			mm->sum1 += value->v.v_float;
		} else {
		        mm->array2 = g_slist_append(mm->array2, p);
			mm->sum2 += value->v.v_float;
		}
		mm->num++ ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_covar (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_covar_t pr;
	Sheet *sheet = (Sheet *) tsheet;
	float_t sum, mean1, mean2 ;
	int     count;
	GSList  *list1, *list2;

	pr.first   = TRUE ;
	count = value_get_as_int(gnumeric_count
	        (tsheet, expr_node_list, eval_col, eval_row, error_string));
	if (count % 2 > 0) {
		*error_string = _("#NUM!") ;
		return NULL;
	}
	pr.count  = count / 2;
	pr.num    = 0 ;
	pr.sum1   = 0.0 ;
	pr.sum2   = 0.0 ;
	pr.array1 = NULL;
	pr.array2 = NULL;

	function_iterate_argument_values (sheet, callback_function_covar,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;
	mean1 = pr.sum1 / pr.count;
	mean2 = pr.sum2 / pr.count;
	while (list1 != NULL && list2 != NULL) {
	        gpointer x, y;
		x = list1->data;
		y = list2->data;
	        sum += (*((float_t *) x) - mean1) * (*((float_t *) y) - mean2);
		g_free(x);
		g_free(y);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(pr.array1);
	g_slist_free(pr.array2);

	return value_float (sum / pr.count) ;
}

static char *help_correl = {
	N_("@FUNCTION=CORREL\n"
	   "@SYNTAX=CORREL(array1,array2)\n"

	   "@DESCRIPTION="
	   "CORREL returns the correllation coefficient of two data sets. "
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=COVAR,FISHER,FISHERINV")
};

typedef struct {
	int     first ;
	guint32 num ;
	int     count ;
        GSList  *array1;
        GSList  *array2;
        float_t sum1;
        float_t sum2;
        float_t sqrsum1;
        float_t sqrsum2;
} stat_correl_t;

static int
callback_function_correl (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_correl_t *mm = closure;
	gpointer     p;

	switch (value->type){
	case VALUE_INTEGER:
	        p = g_new(float_t, 1);
		*((float_t *) p) = (float_t) value->v.v_int;
	        if (mm->num < mm->count) {
		        mm->array1 = g_slist_append(mm->array1, p);
			mm->sum1 += value->v.v_int;
			mm->sqrsum1 += value->v.v_int * value->v.v_int;
		} else {
		        mm->array2 = g_slist_append(mm->array2, p);
			mm->sum2 += value->v.v_int;
			mm->sqrsum2 += value->v.v_int * value->v.v_int;
		}
		mm->num++ ;
		break ;
	case VALUE_FLOAT:
	        p = g_new(float_t, 1);
		*((float_t *) p) = value->v.v_float;
	        if (mm->num < mm->count) {
		        mm->array1 = g_slist_append(mm->array1, p);
			mm->sum1 += value->v.v_float;
			mm->sqrsum1 += value->v.v_float * value->v.v_float;
		} else {
		        mm->array2 = g_slist_append(mm->array2, p);
			mm->sum2 += value->v.v_float;
			mm->sqrsum2 += value->v.v_float * value->v.v_float;
		}
		mm->num++ ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_correl (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_correl_t pr;
	Sheet *sheet = (Sheet *) tsheet;
	float_t sum ;
	int     count;
	GSList  *list1, *list2;

	pr.first   = TRUE ;
	count = value_get_as_int(gnumeric_count
	        (tsheet, expr_node_list, eval_col, eval_row, error_string));
	if (count % 2 > 0) {
		*error_string = _("#NUM!") ;
		return NULL;
	}
	pr.count   = count / 2;
	pr.num     = 0 ;
	pr.sum1    = 0.0 ;
	pr.sum2    = 0.0 ;
	pr.sqrsum1 = 0.0 ;
	pr.sqrsum2 = 0.0 ;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (sheet, callback_function_correl,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL) {
	        gpointer x, y;
		x = list1->data;
		y = list2->data;
	        sum += (*((float_t *) x)) * (*((float_t *) y));
		g_free(x);
		g_free(y);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(pr.array1);
	g_slist_free(pr.array2);

	return value_float ((sum - (pr.sum1*pr.sum2/pr.count)) /
			    sqrt((pr.sqrsum1-(pr.sum1*pr.sum1)/pr.count) *
				 (pr.sqrsum2-(pr.sum2*pr.sum2)/pr.count))) ;
}

static char *help_negbinomdist = {
	N_("@FUNCTION=NEGBINOMDIST\n"
	   "@SYNTAX=NEGBINOMDIST(f,t,p)\n"

	   "@DESCRIPTION="
	   "The NEGBINOMDIST function returns the negative binomial "
	   "distribution. @f is the number of failures, @t is the threshold "
	   "number of successes, and @p is the probability of a success. "
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "if f or t is a non-integer it is truncated. "
	   "if (f + t -1) <= 0 NEGBINOMDIST returns #NUM! error. "
	   "if p < 0 or p > 1 NEGBINOMDIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=BINOMDIST,COMBIN,FACT,HYPGEOMDIST,PERMUT")
};

static Value *
gnumeric_negbinomdist (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int x, r;
	float_t p;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!") ;
		return NULL;
	}

	x = value_get_as_int (argv [0]);
	r = value_get_as_int (argv [1]);
	p = value_get_as_double (argv[2]);
	if ((x + r -1)<=0 || p<0 || p>1) {
		*error_string = _("#NUM!") ;
		return NULL;
	}

	return value_float (combin(x+r-1,r-1) * pow(p, r) * pow(1-p, x));
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

static Value *
gnumeric_normsdist (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t x;

        x = value_get_as_double (argv [0]);

	if (x < 0)
	        return value_float (1.0 - get_positive_normdist(fabs(x))) ;
	else
		return value_float (get_positive_normdist(x)) ;
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
          "if stdev = 0 LOGNORMDIST returns #DIV/0! error. "
	  "if x<0, mean<0 or stdev<0 LOGNORMDIST returns #NUM! error. "
          "\n"
          "@SEEALSO=NORMDIST")
};

static Value *
gnumeric_lognormdist (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t x, mean, stdev ;

        x = value_get_as_double (argv [0]);
        mean = value_get_as_double (argv [1]);
        stdev = value_get_as_double (argv [2]);

        if (stdev==0) {
                *error_string = _("#DIV/0!") ;
                return NULL;
        }
        if (x<0 || mean<0 || stdev<0) {
                *error_string = _("#NUM!") ;
                return NULL;
        }

	x = ((log(x)-mean) / stdev) ;
	return value_float (get_positive_normdist(x)) ;
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

static Value *
gnumeric_fisherinv (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
       float_t y;

       if (!VALUE_IS_NUMBER(argv [0])) {
               *error_string = _("#VALUE!") ;
               return NULL;
       }
       y = value_get_as_double (argv [0]);
       return value_float ((exp(2*y)-1.0) / (exp(2*y)+1.0)) ;
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
       int        first ;
       float_t    mode ;
       int        count ;
} stat_mode_t;

static int
callback_function_mode (Sheet *sheet, Value *value, char **error_string, void *closure)
{
       stat_mode_t *mm = closure;
       gpointer p;
       float_t  key;
       int      count;

       switch (value->type){
       case VALUE_INTEGER:
	       key = (float_t) value->v.v_int;
	       break;
       case VALUE_FLOAT:
	       key = value->v.v_float;
	       break;
       default:
               /* ignore strings */
               break;
       }
       p = g_hash_table_lookup(mm->hash_table, &key);
       if (p == NULL) {
	       p = g_new(int, 1);
	       mm->items = g_slist_append(mm->items, p);
	       *((int *) p) = 1;
	       g_hash_table_insert(mm->hash_table, &key, p);
	       count = 1;
       } else {
	       *((int *) p) += 1;
	       count = *((int *) p);
       }
       if (count > mm->count) {
	       mm->count = count;
	       mm->mode = key;
       }
       mm->first = FALSE ;
       return TRUE;
}

static Value *
gnumeric_mode (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
       GSList *tmp;
       stat_mode_t pr;
       Sheet *sheet = (Sheet *) tsheet;

       pr.first = TRUE ;

       pr.hash_table = g_hash_table_new((GHashFunc) float_hash,
					(GCompareFunc) float_equal);
       pr.items      = NULL;
       pr.mode       = 0.0 ;
       pr.count      = 0;

       function_iterate_argument_values (sheet, callback_function_mode,
                                         &pr, expr_node_list,
                                         eval_col, eval_row, error_string);

       g_hash_table_destroy(pr.hash_table);
       tmp = pr.items;
       while (tmp != NULL) {
	       g_free(tmp->data);
	       tmp = tmp->next;
       }
       g_slist_free(pr.items);
       if (pr.count < 2) {
		*error_string = _("#N/A!") ;
		return NULL;
       }
       return value_float (pr.mode) ;
}

static Value *
gnumeric_harmean (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_inv_sum_t pr;
	Sheet *sheet = (Sheet *) tsheet;
	float_t ans, num ;

	pr.first = TRUE ;
	pr.num   = 0 ;
	pr.sum   = 0.0 ;

	function_iterate_argument_values (sheet, callback_function_stat_inv_sum,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	num = (float_t)pr.num ;
	return value_float (1.0 / (1.0/num * pr.sum)) ;
}

static char *help_geomean = {
	N_("@FUNCTION=GEOMEAN\n"
	   "@SYNTAX=GEOMEAN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "GEOMEAN returns the geometric mean of the N data"
	   "This is equal to the Nth root of the product of the terms"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=HARMEAN,MEDIAN,MEAN,MODE")
};

typedef struct {
	int first ;
	guint32 num ;
	float_t product ;
} stat_prod_t;

static int
callback_function_stat_prod (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_prod_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++ ;
		mm->product = mm->first?value->v.v_int:mm->product*value->v.v_int ;
		break;

	case VALUE_FLOAT:
		mm->num++ ;
		mm->product = mm->first?value->v.v_float:mm->product*value->v.v_float ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_geomean (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_prod_t pr;
	Sheet *sheet = (Sheet *) tsheet;
	float_t ans, num ;

	pr.first   = TRUE ;
	pr.num     = 0 ;
	pr.product = 0.0 ;

	function_iterate_argument_values (sheet, callback_function_stat_prod,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	num = (float_t)pr.num ;
	return value_float (pow (pr.product, 1.0/num)) ;
}

static char *help_skew = {
	N_("@FUNCTION=SKEW\n"
	   "@SYNTAX=SKEW(n1, n2, ...)\n"

	   "@DESCRIPTION="
	   "SKEW returns the skewness of a distribution. "
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "If less than three numbers are given SKEW returns #DIV/0! error. "
	   "@SEEALSO=VAR")
};

typedef struct {
	int first ;
	guint32 num ;
        float_t mean ;
        float_t stddev ;
	float_t sum ;
} stat_skew_sum_t;

static int
callback_function_skew_sum (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_skew_sum_t *mm = closure;
	float tmp;

	switch (value->type){
	case VALUE_INTEGER:
	        tmp = (value->v.v_int - mm->mean) / mm->stddev;
		mm->num++ ;
		mm->sum += tmp * tmp * tmp;
		break;

	case VALUE_FLOAT:
	        tmp = (value->v.v_float - mm->mean) / mm->stddev;
		mm->num++ ;
		mm->sum += tmp * tmp * tmp;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_skew (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_skew_sum_t pr;
	Sheet *sheet = (Sheet *) tsheet;
	float_t num ;

	pr.first   = TRUE ;

	pr.mean = value_get_as_double(gnumeric_average
	        (tsheet, expr_node_list, eval_col, eval_row, error_string));
	pr.stddev = value_get_as_double(gnumeric_stdev
	        (tsheet, expr_node_list, eval_col, eval_row, error_string));
	pr.num  = 0 ;
	pr.sum  = 0.0 ;

	function_iterate_argument_values (sheet, callback_function_skew_sum,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	if (pr.num < 3) {
		*error_string = _("#NUM!") ;
		return NULL;
	}

	num = (float_t)pr.num ;
	return value_float ((pr.num / ((pr.num-1.0) * (pr.num-2.0))) * pr.sum) ;
}

static char *help_expondist = {
	N_("@FUNCTION=EXPONDIST\n"
	   "@SYNTAX=EXPONDIST(x,y,cumulative)\n"

	   "@DESCRIPTION="
	   "The EXPONDIST function returns the exponential distribution "
	   "If the cumulative boolean is false it will return: "
	   "y * exp (-y*x), otherwise it will return 1 - exp (-y*x). "
	   "\n"
	   "If x<0 or y<=0 this will return an error"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "\n"
	   "@SEEALSO=POISSON")
};

static Value *
gnumeric_expondist (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t x, y;
	int cuml, err=0 ;

	x = value_get_as_double (argv [0]);
	y = value_get_as_double (argv [1]);
	if (x < 0.0 || y <= 0.0) {
		*error_string = _("#NUM!") ;
		return NULL;
	}
	cuml = value_get_bool (argv[2], &err) ;
	if (err) {
		*error_string = _("#VALUE!") ;
		return NULL;
	}
	
	if (cuml) {
		return value_float (-expm1(-y*x)) ;
	} else {
		return value_float (y*exp(-y*x)) ;
	}
}

static char *help_gammaln = {
	N_("@FUNCTION=GAMMALN\n"
	   "@SYNTAX=GAMMALN(x)\n"

	   "@DESCRIPTION="
	   "The GAMMALN function returns the ln of the gamma function"
	   "\n"
	   "Performing this function on a string or empty cell returns an error. "
	   "\n"
	   "@SEEALSO=POISSON")
};

static Value *
gnumeric_gammaln (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t x ;

	if (argv[0]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT) {
		*error_string = _("#VALUE!") ;
		return NULL;
	}

	x = value_get_as_double (argv [0]);
	if (x<=0) {
		*error_string = _("#NUM!") ;
		return NULL;
	}
	return value_float (lgamma(x)) ;
}

static char *help_binomdist = {
	N_("@FUNCTION=BINOMDIST\n"
	   "@SYNTAX=BINOMDIST(n,trials,p,cumulative)\n"

	   "@DESCRIPTION="
	   "The BINOMDIST function returns the binomial distribution "
	   "@n is the number of successes, @trials is the total number of "
           "independent trials, @p is the probability of success in trials, "
           "and @cumulative describes whether to return the sum of the"
           "binomial function from 0 to n."
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "if n or trials is a non-integer it is truncated. "
	   "if n < 0 or trials < 0 BINOMDIST returns #NUM! error. "
	   "if n > trials BINOMDIST returns #NUM! error. "
	   "if p < 0 or p > 1 BINOMDIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=POISSON")
};

static Value *
gnumeric_binomdist (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int n, trials;
	float_t p;
	int cuml, err, x;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!") ;
		return NULL;
	}
	n = value_get_as_int (argv [0]);
	trials = value_get_as_int (argv [1]);
	p = value_get_as_double (argv[2]);
	if (n<0 || trials<0 || p<0 || p>1 || n>trials) {
		*error_string = _("#NUM!") ;
		return NULL;
	}

	cuml = value_get_bool (argv[3], &err) ;

	if (cuml) {
		float_t v=0;
		for (x=0; x<=n; x++)
		        v += (combin(trials, x) * pow(p, x) *
				    pow(1-p, trials-x));
		return value_float (v);
	} else
		return value_float (combin(trials, n) * pow(p, n) *
				    pow(1-p, trials-n));
}

static char *help_critbinom = {
       N_("@FUNCTION=CRITBINOM\n"
          "@SYNTAX=CRITBINOM(trials,p,alpha)\n"

          "@DESCRIPTION="
          "The CRITBINOM function returns the smallest value for which the"
          "cumulative is creater than or equal to a given value. "
          "@n is the number of trials, @p is the probability of success in "
          "trials, and @alpha is the criterion value. "
	  "Performing this function on a string or empty cell simply does nothing. "
          "\n"
          "if trials is a non-integer it is truncated. "
          "if trials < 0 CRITBINOM returns #NUM! error. "
          "if p < 0 or p > 1 CRITBINOM returns #NUM! error. "
          "if alpha < 0 or alpha > 1 CRITBINOM returns #NUM! error. "
          "\n"
          "@SEEALSO=BINOMDIST")
};

static Value *
gnumeric_critbinom (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        int trials;
        float_t p, alpha, sum;
        int x;

        if (!VALUE_IS_NUMBER(argv[0]) ||
            !VALUE_IS_NUMBER(argv[1]) ||
            !VALUE_IS_NUMBER(argv[2])){
                *error_string = _("#VALUE!") ;
                return NULL;
        }
        trials = value_get_as_int (argv [0]);
        p = value_get_as_double (argv[1]);
        alpha = value_get_as_double (argv[2]);

        if (trials<0 || p<0 || p>1 || alpha<0 || alpha>1) {
	        *error_string = _("#NUM!") ;
		return NULL;
	}

	sum = 0;
	for (x=0; sum<alpha; x++)
                sum += (combin(trials, x) * pow(p, x) * pow(1-p, trials-x));
	return value_int (x-1);
}

static char *help_permut = {
	N_("@FUNCTION=PERMUT\n"
	   "@SYNTAX=PERMUT(n,k)\n"

	   "@DESCRIPTION="
	   "The PERMUT function returns the number of permutations. "
           "@n is the number of objects, @k is the number of objects in each "
           "permutation. "
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "if n or k is non-integer PERMUT returns #VALUE! error. "
	   "if n = 0 PERMUT returns #NUM! error. "
	   "if n < k PERMUT returns #NUM! error. "
	   "\n"
	   "@SEEALSO=COMBIN")
};

static Value *
gnumeric_permut (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int n, k;

	if (argv[0]->type != VALUE_INTEGER ||
	    argv[1]->type != VALUE_INTEGER){
		*error_string = _("#VALUE!") ;
		return NULL;
	}
	n = value_get_as_int (argv [0]);
	k = value_get_as_int (argv [1]);

	if (n<k || n==0){
		*error_string = _("#NUM!") ;
		return NULL;
	}

	return value_float (fact(n) / fact(n-k));
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
	   "Performing this function on a string or empty cell returns an error."
	   "if x,n,M or N is a non-integer it is truncated. "
	   "if x,n,M or N < 0 HYPGEOMDIST returns #NUM! error. "
	   "if x > M or n > N HYPGEOMDIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=BINOMDIST,POISSON")
};

static Value *
gnumeric_hypgeomdist (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int x, n, M, N;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!") ;
		return NULL;
	}
	x = value_get_as_int (argv [0]);
	n = value_get_as_int (argv [1]);
	M = value_get_as_int (argv [2]);
	N = value_get_as_int (argv [3]);

	if (x<0 || n<0 || M<0 || N<0 || x>M || n>N) {
		*error_string = _("#NUM!") ;
		return NULL;
	}

	return value_float ((combin(M, x) * combin(N-M, n-x)) / combin(N,n));
}

static char *help_confidence = {
	N_("@FUNCTION=CONFIDENCE\n"
	   "@SYNTAX=CONFIDENCE(x,stddev,size)\n"

	   "@DESCRIPTION="
	   "The CONFIDENCE function returns the confidence interval for a mean. "
	   "@x is the significance level, @stddev is the standard deviation, "
	   "and @size is the size of the sample."
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "if size is non-integer it is truncated. "
	   "if size < 0 CONFIDENCE returns #NUM! error. "
	   "if size is 0 CONFIDENCE returns #DIV/0! error. "
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static Value *
gnumeric_confidence (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t x, stddev;
	int size;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!") ;
		return NULL;
	}
	x = value_get_as_double (argv [0]);
	stddev = value_get_as_double (argv [1]);
	size = value_get_as_int (argv [2]);
	if (size == 0) {
		*error_string = _("#DIV/0!") ;
		return NULL;
	}
	if (size < 0) {
		*error_string = _("#NUM!") ;
		return NULL;
	}
	/* Only 95% confidence implemented */
	return value_float (1.96 * (stddev/sqrt(size)));
}

static char *help_standardize = {
	N_("@FUNCTION=STANDARDIZE\n"
	   "@SYNTAX=STANDARDIZE(x,mean,stdev)\n"

	   "@DESCRIPTION="
	   "The STANDARDIZE function returns a normalized value. "
	   "@x is the number to be normalized, @mean is the mean of the "
	   "distribution, @stdev is the standard deviation of the distribution."
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "if stddev is 0 STANDARDIZE returns #DIV/0! error. "
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static Value *
gnumeric_standardize (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t x, mean, stddev;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!") ;
		return NULL;
	}
	x = value_get_as_double (argv [0]);
	mean = value_get_as_double (argv [1]);
	stddev = value_get_as_double (argv [2]);
	if (stddev == 0) {
		*error_string = _("#DIV/0!") ;
		return NULL;
	}

	return value_float ((x-mean) / stddev) ;
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
           "Performing this function on a string or empty cell simply does nothing. "
           "if x < 0 WEIBULL returns #NUM! error. "
           "if alpha <= 0 or beta <= 0 WEIBULL returns #NUM! error. "
           "\n"
           "@SEEALSO=POISSON")
};

static Value *
gnumeric_weibull (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t x, alpha, beta;
        int cuml, err=0 ;

        x = value_get_as_double (argv [0]);
        alpha = value_get_as_double (argv [1]);
        beta = value_get_as_double (argv [2]);
        if (x<0 || alpha<=0 || beta<=0) {
                *error_string = _("#NUM!") ;
                return NULL;
        }
        cuml = value_get_bool (argv[3], &err) ;
        if (err) {
                *error_string = _("#VALUE!") ;
                return NULL;
        }

        if (cuml)
                return value_float (1.0 - exp(-pow(x/beta, alpha))) ;
        else
                return value_float ((alpha/pow(beta, alpha))* 
                                   pow(x, alpha-1)*exp(-pow(x/beta, alpha)));
}

static char *help_normdist = {
        N_("@FUNCTION=NORMDIST\n"
           "@SYNTAX=NORMDIST(x,mean,stdev,cumulative)\n"

           "@DESCRIPTION="
           "The NORMDIST function returns the normal cumulative distribution. "
	   "@x is the value for which you want the distribution, @mean is "
	   "the mean of the distribution, @stdev is the standard deviation. "
           "\n"
           "Performing this function on a string or empty cell simply does nothing. "
           "if stdev=0 NORMDIST returns #DIV/0! error. "
           "\n"
           "@SEEALSO=POISSON")
};

/* In the long run we will need better accuracy or a fast algorithm to
 * calculate these.
 */
static const float_t normdist_tbl[] = {
	.5000, .5040, .5080, .5120, .5160, .5199, .5239, .5279, .5319, .5359,
	.5398, .5438, .5478, .5517, .5557, .5596, .5636, .5675, .5714, .5753,
	.5793, .5832, .5871, .5910, .5948, .5987, .6026, .6064, .6103, .6141,
	.6179, .6217, .6255, .6293, .6331, .6368, .6406, .6443, .6480, .6517,
	.6554, .6591, .6628, .6664, .6700, .6736, .6772, .6808, .6844, .6879,
	
	.6915, .6950, .6985, .7019, .7054, .7088, .7123, .7157, .7190, .7224,
	.7257, .7291, .7324, .7357, .7389, .7422, .7454, .7486, .7517, .7549,
	.7580, .7611, .7642, .7673, .7704, .7734, .7764, .7794, .7823, .7852,
	.7881, .7910, .7939, .7967, .7995, .8023, .8051, .8078, .8106, .8133,
	.8159, .8186, .8212, .8238, .8264, .8289, .8315, .8340, .8365, .8389,
	
	.8413, .8438, .8461, .8485, .8508, .8531, .8554, .8577, .8599, .8621,
	.8643, .8665, .8686, .8708, .8729, .8749, .8770, .8790, .8810, .8830,
	.8849, .8869, .8888, .8907, .8925, .8944, .8962, .8980, .8997, .9015,
	.9032, .9049, .9066, .9082, .9099, .9115, .9131, .9147, .9162, .9177,
	.9192, .9207, .9222, .9236, .9251, .9265, .9279, .9292, .9306, .9319,
	
	.9332, .9345, .9357, .9370, .9382, .9394, .9406, .9418, .9429, .9441,
	.9452, .9463, .9474, .9484, .9495, .9505, .9515, .9525, .9535, .9545,
	.9554, .9564, .9573, .9582, .9591, .9599, .9608, .9616, .9625, .9633,
	.9641, .9649, .9656, .9664, .9671, .9678, .9686, .9693, .9699, .9706,
	.9713, .9719, .9726, .9732, .9738, .9744, .9750, .9756, .9761, .9767,
	
	.9772, .9778, .9783, .9788, .9793, .9798, .9803, .9808, .9812, .9817,
	.9821, .9826, .9830, .9834, .9838, .9842, .9846, .9850, .9854, .9857,
	.9861, .9864, .9868, .9871, .9875, .9878, .9881, .9884, .9887, .9890,
	.9893, .9896, .9898, .9901, .9904, .9906, .9909, .9911, .9913, .9916,
	.9918, .9920, .9922, .9925, .9927, .9929, .9931, .9932, .9934, .9936,
	
	.9938, .9940, .9941, .9943, .9945, .9946, .9948, .9949, .9951, .9952,
	.9953, .9955, .9956, .9957, .9959, .9960, .9961, .9962, .9963, .9964,
	.9965, .9966, .9967, .9968, .9969, .9970, .9971, .9972, .9973, .9974,
	.9974, .9975, .9976, .9977, .9977, .9978, .9979, .9979, .9980, .9981,
	.9981, .9982, .9982, .9983, .9984, .9984, .9985, .9985, .9986, .9986,
	
	.9987, .9987, .9987, .9988, .9988, .9989, .9989, .9989, .9990, .9990,
	.9990, .9991, .9991, .9991, .9992, .9992, .9992, .9992, .9993, .9993,
	.9993, .9993, .9994, .9994, .9994, .9994, .9994, .9995, .9995, .9995,
	.9995, .9995, .9995, .9996, .9996, .9996, .9996, .9996, .9996, .9997,
	.9997, .9997, .9997, .9997, .9997, .9997, .9997, .9997, .9997, .9998,
	
	.9998, .9998, .9998, .9998, .9998, .9998, .9998, .9998, .9998, .9998,
	.9998, .9998, .9999, .9999, .9999, .9999, .9999, .9999, .9999, .9999,
	.9999, .9999, .9999, .9999, .9999, .9999, .9999, .9999, .9999, .9999,
	.9999, .9999, .9999, .9999, .9999, .9999, .9999, .9999, .9999, .9999,
};

static float_t get_positive_normdist(float_t x)
{
        if (x >= 3.895)
	        return 1.0;
	return normdist_tbl[(int) ((x+0.005) * 100)]; 
}

static Value *
gnumeric_normdist (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t x, mean, stdev ;
        int cuml, err=0 ;

        x = value_get_as_double (argv [0]);
        mean = value_get_as_double (argv [1]);
        stdev = value_get_as_double (argv [2]);
        if (stdev==0) {
                *error_string = _("#DIV/0!") ;
                return NULL;
        }
        cuml = value_get_bool (argv[3], &err) ;
        if (err) {
                *error_string = _("#VALUE!") ;
                return NULL;
        }

        if (cuml) {
        	x = ((x-mean) / stdev) ;
	        if (x < 0)
		        return value_float (1.0 - 
					    get_positive_normdist(fabs(x))) ;
		else
		        return value_float (get_positive_normdist(x)) ;
        } else {
		*error_string = _("Unimplemented") ;
		return NULL;
        }
}

static char *help_kurt = {
        N_("@FUNCTION=KURT\n"
           "@SYNTAX=KURT(n1, n2, ...)\n"

           "@DESCRIPTION="
           "KURT returns the kurtosis of a data set. "
           "\n"
           "Strings and empty cells are simply ignored."
           "\n"
           "If fewer than four numbers are given or all of them are equal "
           "KURT returns #DIV/0! error. "
           "@SEEALSO=VAR")
};

typedef struct {
        int first ;
        guint32 num ;
        float_t mean ;
        float_t stddev ;
        float_t sum ;
} stat_kurt_sum_t;

static int
callback_function_kurt_sum (Sheet *sheet, Value *value, char **error_string, void *closure)
{
        stat_kurt_sum_t *mm = closure;
        float_t tmp;

        switch (value->type){
        case VALUE_INTEGER:
                tmp = (value->v.v_int - mm->mean) / mm->stddev;
                tmp *= tmp;
                mm->num++ ;
                mm->sum += tmp * tmp;
                break;

        case VALUE_FLOAT:
                tmp = (value->v.v_float - mm->mean) / mm->stddev;
                tmp *= tmp;
                mm->num++ ;
                mm->sum += tmp * tmp;
                break ;
        default:
                /* ignore strings */
                break;
        }
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_kurt (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
        stat_kurt_sum_t pr;
	Sheet *sheet = (Sheet *) tsheet;
	float_t n, d, num, dem ;

	pr.first = TRUE ;
	pr.mean = value_get_as_double(gnumeric_average
                (tsheet, expr_node_list, eval_col, eval_row, error_string));
	pr.stddev = value_get_as_double(gnumeric_stdev
                (tsheet, expr_node_list, eval_col, eval_row, error_string));
	pr.num  = 0 ;
	pr.sum  = 0.0 ;

	if (pr.stddev == 0.0) {
	        *error_string = _("#NUM!") ;
                return NULL;
	}
	function_iterate_argument_values (sheet, callback_function_kurt_sum,
                                          &pr, expr_node_list,
                                          eval_col, eval_row, error_string);

	if (pr.num < 4) {
                *error_string = _("#NUM!") ;
                return NULL;
	}

	n = (float_t) pr.num;
	num = (n*(n+1.0));
	dem = ((n-1.0) * (n-2.0) * (n-3.0));
	d = (3*(n-1)*(n-1)) / ((n-2) * (n-3));

	return value_float ((pr.sum) * (num/dem) - d) ;
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
        int first ;
        guint32 num ;
        float_t mean ;
        float_t sum ;
} stat_avedev_sum_t;

static int
callback_function_stat_avedev_sum (Sheet *sheet, Value *value, char **error_string, void *closure)
{
        stat_avedev_sum_t *mm = closure;

	switch (value->type){
	case VALUE_INTEGER:
	        mm->num++ ;
                mm->sum += fabs(value->v.v_int - mm->mean);
                break;
        case VALUE_FLOAT:
                mm->num++ ;
                mm->sum += fabs(value->v.v_float - mm->mean);
                break ;
        default:
                /* ignore strings */
                break;
        }
        mm->first = FALSE ;
        return TRUE;
}

static Value *
gnumeric_avedev (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
        stat_avedev_sum_t pr;
        Sheet *sheet = (Sheet *) tsheet;
        float_t ans, num ;

        pr.first = TRUE ;
        pr.num   = 0 ;
        pr.sum   = 0.0 ;
        pr.mean  = value_get_as_double(gnumeric_average
	 			       (tsheet, expr_node_list,
				        eval_col, eval_row, error_string));

	function_iterate_argument_values (sheet, callback_function_stat_avedev_sum,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	num = (float_t)pr.num ;
	return value_float ((1.0/num) * pr.sum) ;
}

static char *help_devsq = {
        N_("@FUNCTION=DEVSQ\n"
           "@SYNTAX=DEVSQ(n1, n2, ...)\n"

           "@DESCRIPTION="
           "DEVSQ returns the sum of squares of deviations of a data set from "
           "the sample mean. "
           "\n"
           "Strings and empty cells are simply ignored."
           "\n"
           "@SEEALSO=STDEV")
};

typedef struct {
        int first ;
        guint32 num ;
        float_t mean ;
        float_t sum ;
} stat_devsq_sum_t;

static int
callback_function_devsq_sum (Sheet *sheet, Value *value, char **error_string, void *closure)
{
        stat_devsq_sum_t *mm = closure;
        float_t tmp;

        switch (value->type){
        case VALUE_INTEGER:
                tmp = value->v.v_int - mm->mean;
                mm->num++ ;
                mm->sum += tmp * tmp;
                break;
        case VALUE_FLOAT:
                tmp = value->v.v_int - mm->mean;
                mm->num++ ;
                mm->sum += tmp * tmp;
                break ;
        default:
                /* ignore strings */
                break;
        }
        mm->first = FALSE ;
        return TRUE;
}

static Value *
gnumeric_devsq (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
        stat_devsq_sum_t pr;
        Sheet *sheet = (Sheet *) tsheet;

        pr.first = TRUE ;

        pr.mean = value_get_as_double(gnumeric_average
                (tsheet, expr_node_list, eval_col, eval_row, error_string));
        pr.num  = 0 ;
        pr.sum  = 0.0 ;

        function_iterate_argument_values (sheet, callback_function_devsq_sum,
                                          &pr, expr_node_list,
                                          eval_col, eval_row, error_string);

        return value_float (pr.sum) ;
}

static char *help_fisher = {
        N_("@FUNCTION=FISHER\n"
           "@SYNTAX=FISHER(x)\n"

           "@DESCRIPTION="
           "The FISHER function returns the Fisher transformation at x. "
           "\n"
           "If x is not-number FISHER returns #VALUE! error."
           "If x<=-1 or x>=1 FISHER returns #NUM! error"
           "\n"
           "@SEEALSO=SKEW")
};

static Value *
gnumeric_fisher (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t x;

        if (!VALUE_IS_NUMBER(argv [0])) {
                *error_string = _("#VALUE!") ;
                return NULL;
        }
        x = value_get_as_double (argv [0]);
        if (x <= -1.0 || x >= 1.0) {
                *error_string = _("#NUM!") ;
                return NULL;
        }
        return value_float (0.5 * log((1.0+x) / (1.0-x))) ;
}

static char *help_poisson = {
	N_("@FUNCTION=POISSON\n"
	   "@SYNTAX=POISSON(x,mean,cumulative)\n"

	   "@DESCRIPTION="
	   "The POISSON function returns the Poisson distribution "
	   "@x is the number of events, @mean is the expected numeric value "
	   "@cumulative describes whether to return the sum of the poisson function from 0 to x."
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "if x is a non-integer it is truncated. "
	   "if x <= 0 POISSON returns #NUM! error. "
	   "if mean <= 0 POISSON returns the #NUM! error. "
	   "\n"
	   "@SEEALSO=POISSON")
};

static Value *
gnumeric_poisson (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t x, mean ;
	int cuml, err ;

	if (argv[0]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_FLOAT) {
		*error_string = _("#VALUE!") ;
		return NULL;
	}
	x = value_get_as_int (argv [0]);
	mean = value_get_as_double (argv [1]);
	if (x<=0  || mean <=0) {
		*error_string = _("#NUM!") ;
		return NULL;
	}

	cuml = value_get_bool (argv[2], &err) ;

	if (cuml) {
		*error_string = _("Unimplemented") ;
		return NULL;
	} else
		return value_float (exp(-mean)*pow(mean,x)/exp (lgamma (x + 1))) ;
}

static char *help_pearson = {
	N_("@FUNCTION=PEARSON\n"
	   "@SYNTAX=PEARSON(array1,array2)\n"

	   "@DESCRIPTION="
	   "PEARSON returns the Pearson correllation coefficient of two data "
	   "sets. "
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=INTERCEPT,LINEST,RSQ,SLOPE,STEYX")
};

static Value *
gnumeric_pearson (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_correl_t pr;
	Sheet *sheet = (Sheet *) tsheet;
	float_t sum ;
	int     count;
	GSList  *list1, *list2;

	pr.first   = TRUE ;
	count = value_get_as_int(gnumeric_count
	        (tsheet, expr_node_list, eval_col, eval_row, error_string));
	if (count % 2 > 0) {
		*error_string = _("#NUM!") ;
		return NULL;
	}
	pr.count   = count / 2;
	pr.num     = 0 ;
	pr.sum1    = 0.0 ;
	pr.sum2    = 0.0 ;
	pr.sqrsum1 = 0.0 ;
	pr.sqrsum2 = 0.0 ;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (sheet, callback_function_correl,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL) {
	        gpointer x, y;
		x = list1->data;
		y = list2->data;
	        sum += (*((float_t *) x)) * (*((float_t *) y));
		g_free(x);
		g_free(y);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(pr.array1);
	g_slist_free(pr.array2);

	return value_float (((pr.count*sum - pr.sum1*pr.sum2)) /
			    sqrt((pr.count*pr.sqrsum1 - pr.sum1*pr.sum1) *
				 (pr.count*pr.sqrsum2 - pr.sum2*pr.sum2)));
}

static char *help_median = {
       N_("@FUNCTION=MEDIAN\n"
          "@SYNTAX=MEDIAN(n1, n2, ...)\n"

          "@DESCRIPTION="
          "MEDIAN returns the median of the given data set. "
          "\n"
          "Strings and empty cells are simply ignored."
	  "If even numbers are given MEDIAN returns the average of the two "
	  "numbers in the middle. "
          "\n"
          "@SEEALSO=AVERAGE,COUNT,COUNTA,DAVERAGE,MODE,SUM")
};

typedef struct {
	int     first ;
	guint32 num ;
        GSList  *list;
} stat_median_t;

static int
callback_function_median (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_median_t *mm = closure;
	gpointer        p;

	switch (value->type){
	case VALUE_INTEGER:
	        p = g_new(float_t, 1);
		*((float_t *) p) = value->v.v_int;
		mm->list = g_slist_append(mm->list, p);
		mm->num++ ;
		break ;
	case VALUE_FLOAT:
	        p = g_new(float_t, 1);
		*((float_t *) p) = value->v.v_float;
		mm->list = g_slist_append(mm->list, p);
		mm->num++ ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE ;
	return TRUE;
}

static Value *
gnumeric_median (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_median_t   p;
	Sheet           *sheet = (Sheet *) tsheet;
	GSList          *list;
	int             median_ind, n;
	float_t         median;

	p.first = TRUE ;
	p.num   = 0 ;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_median,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	median_ind = (p.num-1) / 2;
	p.list = g_slist_sort (p.list, (GCompareFunc) float_compare);
	list  = p.list;

	/* Skip half of the list */
	for (n=0; n<median_ind; n++) {
	        g_free(list->data);
		list = list->next;
	}

	if ((p.num-1) % 2 == 0)
	        median = *((float_t *) list->data);
	else
	        median = (*((float_t *) list->data) +
			  *((float_t *) list->next->data)) / 2.0;

	while (list != NULL) {
	        g_free(list->data);
		list = list->next;
	}

	g_slist_free(p.list);

	return value_float (median) ;
}

static char *help_large = {
	N_("@FUNCTION=LARGE\n"
	   "@SYNTAX=LARGE(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "LARGE returns the k-th largest value in a data set. "
	   "\n"
	   "If data set is empty LARGE returns #NUM! error. "
	   "If k<=0 or k is greater than the number of data items given "
	   "LARGE returns #NUM! error. "
	   "\n"
	   "@SEEALSO=PERCENTILE,PERCENTRANK,QUARTILE,SMALL")
};

static Value *
gnumeric_large (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_trimmean_t p;
	Sheet           *sheet = (Sheet *) tsheet;
	GSList          *list;
	int             n, count, k;
	float_t         r;

	p.first = TRUE ;
	p.num   = 0 ;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_trimmean,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	p.num--;
	k = ((int) p.last);

	if (p.num == 0 || k<=0 || k >= p.num) {
		*error_string = _("#NUM!") ;
		list  = p.list;
		while (list != NULL) {
		        g_free(list->data);
			list = list->next;
		}
		g_slist_free(p.list);
		return NULL;
	} else {
	        p.list = g_slist_sort (p.list, (GCompareFunc) float_compare_d);
		list  = p.list;
		--k;

		/* Skip the k largest values */
		for (n=0; n<k; n++) {
		        g_free(list->data);
			list = list->next;
		}

		r = *((float_t *) list->data);

		while (list != NULL) {
		        g_free(list->data);
			list = list->next;
		}

		g_slist_free(p.list);
	}
	return value_float (r) ;
}

static char *help_small = {
	N_("@FUNCTION=SMALL\n"
	   "@SYNTAX=SMALL(n1, n2, ..., k)\n"

	   "@DESCRIPTION="
	   "SMALL returns the k-th smallest value in a data set. "
	   "\n"
	   "If data set is empty SMALL returns #NUM! error. "
	   "If k<=0 or k is greater than the number of data items given "
	   "SMALL returns #NUM! error. "
	   "\n"
	   "@SEEALSO=PERCENTILE,PERCENTRANK,QUARTILE,LARGE")
};

static Value *
gnumeric_small (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_trimmean_t p;
	Sheet           *sheet = (Sheet *) tsheet;
	GSList          *list;
	int             n, count, k;
	float_t         r;

	p.first = TRUE ;
	p.num   = 0 ;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_trimmean,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	p.num--;
	k = ((int) p.last);

	if (p.num == 0 || k<=0 || k >= p.num) {
		*error_string = _("#NUM!") ;
		list  = p.list;
		while (list != NULL) {
		        g_free(list->data);
			list = list->next;
		}
		g_slist_free(p.list);
		return NULL;
	} else {
	        p.list = g_slist_sort (p.list, (GCompareFunc) float_compare);
		list  = p.list;
		--k;

		/* Skip the k largest values */
		for (n=0; n<k; n++) {
		        g_free(list->data);
			list = list->next;
		}

		r = *((float_t *) list->data);

		while (list != NULL) {
		        g_free(list->data);
			list = list->next;
		}

		g_slist_free(p.list);
	}
	return value_float (r) ;
}


FunctionDefinition stat_functions [] = {
        { "avedev",    0,      "",          &help_avedev,    gnumeric_avedev, NULL },
	{ "binomdist", "fffb", "n,t,p,c",   &help_binomdist, NULL, gnumeric_binomdist },
	{ "confidence", "fff",  "x,stddev,size", &help_confidence, NULL, gnumeric_confidence },
	{ "critbinom",  "fff",  "trials,p,alpha", &help_critbinom, NULL, gnumeric_critbinom },
        { "correl",     0,      "",         &help_correl,    gnumeric_correl, NULL },
        { "covar",      0,      "",         &help_covar,     gnumeric_covar, NULL },
        { "devsq",      0,      "",         &help_devsq,     gnumeric_devsq, NULL },
	{ "permut",    "ff",  "n,k",        &help_permut,    NULL, gnumeric_permut },
	{ "poisson",   "ffb",  "",          &help_poisson,   NULL, gnumeric_poisson },
	{ "expondist", "ffb",  "",          &help_expondist, NULL, gnumeric_expondist },
        { "fisher",    "f",    "",          &help_fisher,    NULL, gnumeric_fisher },
        { "fisherinv", "f",    "",          &help_fisherinv, NULL, gnumeric_fisherinv },
	{ "gammaln",   "f",    "number",    &help_gammaln,   NULL, gnumeric_gammaln },
	{ "geomean",   0,      "",          &help_geomean,   gnumeric_geomean, NULL },
	{ "harmean",   0,      "",          &help_harmean,   gnumeric_harmean, NULL },
	{ "hypgeomdist", "ffff", "x,n,M,N", &help_hypgeomdist, NULL, gnumeric_hypgeomdist },
        { "kurt",      0,      "",          &help_kurt,      gnumeric_kurt, NULL },
	{ "large",  0,      "",             &help_large,  gnumeric_large, NULL },
	{ "lognormdist",  "fff",  "",       &help_lognormdist, NULL, gnumeric_lognormdist },
	{ "median",    0,      "",          &help_median,    gnumeric_median, NULL },
	{ "mode",      0,      "",          &help_mode,   gnumeric_mode, NULL },
	{ "negbinomdist", "fff", "f,t,p",   &help_negbinomdist, NULL, gnumeric_negbinomdist },
	{ "normdist",   "fffb",  "",        &help_normdist,  NULL, gnumeric_normdist },
	{ "normsdist",  "f",  "",           &help_normsdist,  NULL, gnumeric_normsdist },
	{ "pearson",   0,      "",          &help_pearson,   gnumeric_pearson, NULL },
	{ "rank",      0,      "",          &help_rank,      gnumeric_rank, NULL },
	{ "skew",      0,      "",          &help_skew,      gnumeric_skew, NULL },
	{ "small",  0,      "",             &help_small,  gnumeric_small, NULL },
	{ "standardize", "fff",  "x,mean,stddev", &help_standardize, NULL, gnumeric_standardize },
	{ "stdev",     0,      "",          &help_stdev,     gnumeric_stdev, NULL },
	{ "stdevp",    0,      "",          &help_stdevp,    gnumeric_stdevp, NULL },
	{ "trimmean",  0,      "",          &help_trimmean,  gnumeric_trimmean, NULL },
	{ "var",       0,      "",          &help_var,       gnumeric_var, NULL },
	{ "varp",      0,      "",          &help_varp,      gnumeric_varp, NULL },
        { "weibull", "fffb",  "",           &help_weibull, NULL, gnumeric_weibull },
	{ NULL, NULL },
};
