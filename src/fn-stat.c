/*
 * fn-stat.c:  Built in statistical functions and functions registration
 *
 * Authors:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

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

typedef struct {
	guint32 num ;
	float_t sum_x ;
	float_t sum_x_squared ;
} stat_closure_t;

static void
setup_stat_closure (stat_closure_t *cl)
{
	cl->num = 0 ;
	cl->sum_x = 0.0 ;
	cl->sum_x_squared = 0.0 ;
}

static int
callback_function_stat (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_closure_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++ ;
		mm->sum_x+=value->v.v_int ;
		mm->sum_x_squared+= ((float_t)value->v.v_int)*((float_t)value->v.v_int) ;
		break;

	case VALUE_FLOAT:
		mm->num++ ;
		mm->sum_x+=value->v.v_float ;
		mm->sum_x_squared+= ((float_t)value->v.v_float)*((float_t)value->v.v_float) ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	
	return TRUE;
}

static char *help_varp = {
	N_("@FUNCTION=VARP\n"
	   "@SYNTAX=VARP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VARP calculates the variance of a set of numbers "
	   "where each number is a member of a population "
	   "and the set is the entire population"
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

	num = (float_t)cl.num ;
	ans = (num*cl.sum_x_squared - cl.sum_x*cl.sum_x)
		/ (num * num) ;

	return 	value_float(ans) ;
}

static char *help_var = {
	N_("@FUNCTION=VAR\n"
	   "@SYNTAX=VAR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VAR estimates the variance of a sample of a population "
	   "To get the true variance of a complete population use @VARP"
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


	num = (float_t)cl.num ;
	ans = (num*cl.sum_x_squared - cl.sum_x*cl.sum_x)
		/ (num * (num - 1.0)) ;

	return value_float(ans) ;
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
	   "STDEV returns standard deviation of a set of numbers "
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

float_t combin (int n, int k);

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
	int err;

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

	x = value_get_as_int (argv [0]);      /* Hint, think */
	mean = value_get_as_double (argv [1]);
	cuml = value_get_bool (argv[2], &err) ;

	if (cuml) {
		*error_string = _("Unimplemented") ;
		return NULL;
	} else
		return value_float (exp(-mean)*pow(mean,x)/exp (lgamma (x + 1))) ;
}

FunctionDefinition stat_functions [] = {
	{ "binomdist", "fffb", "n,t,p,c",   &help_poisson,   NULL, gnumeric_binomdist },
	{ "poisson",   "ffb",  "",          &help_poisson,   NULL, gnumeric_poisson },
	{ "expondist", "ffb",  "",          &help_expondist, NULL, gnumeric_expondist },
	{ "gammaln",   "f",    "number",    &help_gammaln,   NULL, gnumeric_gammaln },
	{ "geomean",   0,      "",          &help_geomean,   gnumeric_geomean, NULL },
	{ "harmean",   0,      "",          &help_harmean,   gnumeric_harmean, NULL },
	{ "hypgeomdist", "ffff", "x,n,M,N", &help_hypgeomdist, NULL, gnumeric_hypgeomdist },
	{ "stdev",     0,      "",          &help_stdev,     gnumeric_stdev, NULL },
	{ "stdevp",    0,      "",          &help_stdevp,    gnumeric_stdevp, NULL },
	{ "var",       0,      "",          &help_var,       gnumeric_var, NULL },
	{ "varp",      0,      "",          &help_varp,      gnumeric_varp, NULL },
	{ NULL, NULL },
};


/*
 * Mode, Median: Use large hash table :-)
 *
 * Engineering: Bessel functions: use C fns: j0, y0 etc.
 */
