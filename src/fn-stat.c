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

#define M_LN_SQRT_2PI   0.918938533204672741780329736406  /* log(sqrt(2*pi)) */


/* The cumulative distribution function for the 0-1 normal distribution.  */
static float_t
phi (float_t x)
{
	return (1.0 + erf (x / M_SQRT2)) / 2.0;
}

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

static inline float_t
fmin2 (float_t x, float_t y)
{
        return (x < y) ? x : y;
}

static inline float_t
fmax2 (float_t x, float_t y)
{
        return (x > y) ? x : y;
}

/* This function is originally taken from R package
 * (src/nmath/pgamma.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t
pgamma(double x, double p, double scale)
{
        const float_t third = 1.0 / 3.0;
	const float_t xbig = 1.0e+8;
	const float_t oflo = 1.0e+37;
	const float_t plimit = 1000.0e0;
	const float_t elimit = -88.0e0;
        float_t       pn1, pn2, pn3, pn4, pn5, pn6, arg, c, rn, a, b, an;
	float_t       sum;
 
	x = x / scale;
	if (x <= 0)
	        return 0.0;
 
	/* use a normal approximation if p > plimit */
 
	if (p > plimit) {
	        pn1 = sqrt(p) * 3.0 * (pow(x/p, third) + 1.0 /
					 (p * 9.0) - 1.0);
		return phi(pn1);
	}
 
	/* if x is extremely large compared to p then return 1 */
 
	if (x > xbig)
	        return 1.0;
 
	if (x <= 1.0 || x < p) {
 
	        /* use pearson's series expansion. */
 
	        arg = p * log(x) - x - lgamma(p + 1.0);
		c = 1.0;
		sum = 1.0;
		a = p;
		do {
		        a = a + 1.0;
			c = c * x / a;
			sum = sum + c;
		} while (c > DBL_EPSILON);
		arg = arg + log(sum);
		sum = 0;
		if (arg >= elimit)
		        sum = exp(arg);
	} else {
 
	        /* use a continued fraction expansion */
 
	        arg = p * log(x) - x - lgamma(p);
		a = 1.0 - p;
		b = a + x + 1.0;
		c = 0;
		pn1 = 1.0;
		pn2 = x;
		pn3 = x + 1.0;
		pn4 = x * b;
		sum = pn3 / pn4;
		for (;;) {
		        a = a + 1.0;
			b = b + 2.0;
			c = c + 1.0;
			an = a * c;
			pn5 = b * pn3 - an * pn1;
			pn6 = b * pn4 - an * pn2;
			if (fabs(pn6) > 0) {
			        rn = pn5 / pn6;
				if (fabs(sum - rn) <= 
				    fmin2(DBL_EPSILON, DBL_EPSILON * rn))
				        break;
				sum = rn;
			}
			pn1 = pn3;
			pn2 = pn4;
			pn3 = pn5;
			pn4 = pn6;
			if (fabs(pn5) >= oflo) {
 
			        /* re-scale the terms in continued fraction */
			        /* if they are large */
 
			        pn1 = pn1 / oflo;
				pn2 = pn2 / oflo;
				pn3 = pn3 / oflo;
				pn4 = pn4 / oflo;
			}
		}
		arg = arg + log(sum);
		sum = 1.0;
		if (arg >= elimit)
		        sum = 1.0 - exp(arg);
	}
	return sum;
}

/* This function is originally taken from R package
 * (src/nmath/i1match.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static int
i1mach (int i)
{
    switch(i) {
    case  1:
            return 5;
    case  2:
            return 6;
    case  3:
            return 0;
    case  4:
            return 0;
    case  5:
            return CHAR_BIT * sizeof(int);
    case  6:
            return sizeof(int)/sizeof(char);
    case  7:
            return 2;
    case  8:
            return CHAR_BIT * sizeof(int) - 1;
    case  9:
            return INT_MAX;
    case 10:
            return FLT_RADIX;
    case 11:
            return FLT_MANT_DIG;
    case 12:
            return FLT_MIN_EXP;
    case 13:
            return FLT_MAX_EXP;
    case 14:
            return DBL_MANT_DIG;
    case 15:
            return DBL_MIN_EXP;
    case 16:
            return DBL_MAX_EXP;
    default:
            return 0;
    }
}

/* This function is originally taken from R package
 * (src/nmath/d1match.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t
d1mach(int i)
{
        switch(i) {
	case 1:
	        return DBL_MIN;
	case 2:
	        return DBL_MAX;
	case 3:
	        return pow((double)i1mach(10), -(double)i1mach(14));
	case 4:
	        return pow((double)i1mach(10), 1-(double)i1mach(14));
	case 5:
	        return log10(2.0);
	default:
	        return 0.0;
	}
}

/* This function is originally taken from R package
 * (src/nmath/chebyshev.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static int
chebyshev_init(float_t *dos, int nos, float_t eta)
{
        int i, ii;
	float_t err;

	if (nos < 1)
	        return 0;

	err = 0.0;
	i = 0;                      /* just to avoid compiler warnings */
	for (ii=1; ii<=nos; ii++) {
	        i = nos - ii;
		err += fabs(dos[i]);
		if (err > eta) {
		        return i;
		}
	}
	return i;
}

/* This function is originally taken from R package
 * (src/nmath/chebyshev.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t
chebyshev_eval(float_t x, float_t *a, int n)
{
        float_t b0, b1, b2, twox;
	int i;

	twox = x * 2;
	b2 = b1 = 0;
	b0 = 0;
	for (i = 1; i <= n; i++) {
	        b2 = b1;
		b1 = b0;
		b0 = twox * b1 - b2 + a[n - i];
	}
	return (b0 - b2) * 0.5;
}

/* This function is originally taken from R package
 * (src/nmath/lgammacor.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t
lgammacor(float_t x)
{
        static double algmcs[15] = {
	        +.1666389480451863247205729650822e+0,
		-.1384948176067563840732986059135e-4,
		+.9810825646924729426157171547487e-8,
		-.1809129475572494194263306266719e-10,
		+.6221098041892605227126015543416e-13,
		-.3399615005417721944303330599666e-15,
		+.2683181998482698748957538846666e-17,
		-.2868042435334643284144622399999e-19,
		+.3962837061046434803679306666666e-21,
		-.6831888753985766870111999999999e-23,
		+.1429227355942498147573333333333e-24,
		-.3547598158101070547199999999999e-26,
		+.1025680058010470912000000000000e-27,
		-.3401102254316748799999999999999e-29,
		+.1276642195630062933333333333333e-30
	};
	static int     nalgm = 0;
	static float_t xbig = 0;
	static float_t xmax = 0;
	float_t        tmp;

	if (nalgm == 0) {
	        nalgm = chebyshev_init(algmcs, 15, d1mach(3));
		xbig = 1 / sqrt(d1mach(3));
		xmax = exp(fmin2(log(d1mach(2) / 12), -log(12 * d1mach(1))));
	}

	if (x < xbig) {
	        tmp = 10 / x;
		return chebyshev_eval(tmp * tmp * 2 - 1, algmcs, nalgm) / x;
	}
	else
	        return (1 / (x * 12));
}

/* This function is originally taken from R package
 * (src/nmath/lbeta.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t
lbeta(float_t a, float_t b)
{
        static float_t corr, p, q;

	p = q = a;
	if (b < p)
	        p = b; /* := min(a,b) */
	if (b > q)
                q = b; /* := max(a,b) */

	/* both arguments must be >= 0 */

	if (p >= 10) {
	        /* p and q are big. */
	        corr = lgammacor(p) + lgammacor(q) - lgammacor(p + q);
		return log(q) * -0.5 + M_LN_SQRT_2PI + corr
		  + (p - 0.5) * log(p / (p + q)) + q * log(1+(-p / (p + q)));
	}
	else if (q >= 10) {
	        /* p is small, but q is big. */
	        corr = lgammacor(q) - lgammacor(p + q);
		return lgamma(p) + corr + p - p * log(p + q)
		  + (q - 0.5) * log(1+(-p / (p + q)));
	}
	else
                /* p and q are small: p <= q > 10. */
	        return log(exp(lgamma(p)) *
			   (exp(lgamma(q)) / exp(lgamma(p + q))));
}

/* This function is originally taken from R package
 * (src/nmath/pbeta.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t
pbeta_raw(float_t x, float_t pin, float_t qin)
{
        float_t ans, c, finsum, p, ps, p1, q, term, xb, xi, y;
	int n, i, ib;

	static float_t eps = 0;
	static float_t alneps = 0;
	static float_t sml = 0;
	static float_t alnsml = 0;

	if (eps == 0) { /* initialize machine constants ONCE */
	        eps = d1mach(3);
		alneps = log(eps);
		sml = d1mach(1);
		alnsml = log(sml);
	}

	y = x;
	p = pin;
	q = qin;

	/* swap tails if x is greater than the mean */

	if (p / (p + q) < x) {
	        y = 1 - y;
		p = qin;
		q = pin;
	}

	if ((p + q) * y / (p + 1) < eps) {

	        /* tail approximation */

	        ans = 0;
		xb = p * log(fmax2(y, sml)) - log(p) - lbeta(p, q);
		if (xb > alnsml && y != 0)
		        ans = exp(xb);
		if (y != x || p != pin)
		        ans = 1 - ans;
	}
	else {
	        /*___ FIXME ___:  This takes forever (or ends wrongly) 
		 * when (one or) both p & q  are huge 
		 */

	        /* evaluate the infinite sum first.  term will equal */
	        /* y^p / beta(ps, p) * (1 - ps)-sub-i * y^i / fac(i) */
	        ps = q - floor(q);
		if (ps == 0)
		        ps = 1;
		xb = p * log(y) - lbeta(ps, p) - log(p);
		ans = 0;
		if (xb >= alnsml) {
		        ans = exp(xb);
			term = ans * p;
			if (ps != 1) {
			        n = fmax2(alneps/log(y), 4.0);
				for(i=1 ; i<= n ; i++) {
				        xi = i;
					term = term * (xi - ps) * y / xi;
					ans = ans + term / (p + xi);
				}
			}
		}

		/* now evaluate the finite sum, maybe. */

		if (q > 1) {
		        xb = p * log(y) + q * log(1 - y) - lbeta(p, q) - log(q);
			ib = fmax2(xb / alnsml, 0.0);
			term = exp(xb - ib * alnsml);
			c = 1 / (1 - y);
			p1 = q * c / (p + q - 1);

			finsum = 0;
			n = q;
			if (q == n)
			        n = n - 1;
			for(i=1 ; i<=n ; i++) {
			        if (p1 <= 1 && term / eps <= finsum)
				  break;
				xi = i;
				term = (q - xi + 1) * c * term / (p + q - xi);
				if (term > 1) {
				        ib = ib - 1;
					term = term * sml;
				}
				if (ib == 0)
				        finsum = finsum + term;
			}
			ans = ans + finsum;
		}
		if (y != x || p != pin)
		        ans = 1 - ans;
		ans = fmax2(fmin2(ans, 1.0), 0.0);
	}
	return ans;
}

static float_t
pbeta(float_t x, float_t pin, float_t qin)
{
        if (x <= 0)
	        return 0;
	if (x >= 1)
	        return 1;
	return pbeta_raw(x, pin, qin);
}

/* This function is originally taken from R package
 * (src/nmath/pt.c) written and copyrighted (1995, 1996) by Robert Gentleman
 * and Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t 
pt(float_t x, float_t n)
{
        /* return  P[ T <= x ]  where  
	 * T ~ t_{n}  (t distrib. with n degrees of freedom).   
	 *      --> ./pnt.c for NON-central
	 */
        float_t val;

	if (n > 4e5) { /*-- Fixme(?): test should depend on `n' AND `x' ! */
	               /* Approx. from  Abramowitz & Stegun 26.7.8 (p.949) */
	        val = 1./(4.*n);
		return phi(x*(1. - val)/sqrt(1. + x*x*2.*val));
	}
	val = 0.5 * pbeta(n / (n + x * x), n / 2.0, 0.5);
	return val;
}

/* This function is originally taken from R package
 * (src/nmath/pf.c) written and copyrighted (1998) by Ross Ihaka.
 * Modified for Gnumeric by Jukka-Pekka Iivonen
 */
static float_t
pf(float_t x, float_t n1, float_t n2)
{
        if (x <= 0.0)
	        return 0.0;
	return pbeta(n2 / (n2 + n1 * x), n2 / 2.0, n1 / 2.0);
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
}

int
callback_function_stat (Sheet *sheet, Value *value, char **error_string,
			void *closure)
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
gnumeric_varp (Sheet *sheet, GList *expr_node_list, int eval_col,
	       int eval_row, char **error_string)
{
	stat_closure_t cl;
	float_t ans, num;

	setup_stat_closure (&cl);

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
gnumeric_var (Sheet *sheet, GList *expr_node_list, int eval_col,
	      int eval_row, char **error_string)
{
	stat_closure_t cl;
	float_t ans, num;

	setup_stat_closure (&cl);

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
gnumeric_stdev (Sheet *sheet, GList *expr_node_list, int eval_col,
		int eval_row, char **error_string)
{
	Value *ans = gnumeric_var (sheet, expr_node_list, eval_col, eval_row, error_string);

	if (ans && (ans->type == VALUE_FLOAT))
		ans->v.v_float = sqrt (ans->v.v_float);
	return ans;
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
gnumeric_stdevp (Sheet *sheet, GList *expr_node_list, int eval_col,
		 int eval_row, char **error_string)
{
	Value *ans = gnumeric_varp (sheet, expr_node_list, eval_col, eval_row, error_string);

	if (ans && (ans->type == VALUE_FLOAT))
		ans->v.v_float = sqrt (ans->v.v_float);

	return ans;
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
	int first;
	guint32 num;
	float_t sum;
} stat_inv_sum_t;

static int
callback_function_stat_inv_sum (Sheet *sheet, Value *value,
				char **error_string, void *closure)
{
	stat_inv_sum_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++;
		if (mm->first)
			mm->sum = 1.0 / value->v.v_int;
		else
			mm->sum = mm->sum + 1.0 / value->v.v_int;
		break;

	case VALUE_FLOAT:
		mm->num++;
		if (mm->first)
			mm->sum = 1.0 / value->v.v_float;
		else
			mm->sum = mm->sum + 1.0 / value->v.v_float;
		break;
		
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
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
	int     first;
	guint32 num;
        float_t x;
        float_t last;
        GSList  *list;
} stat_rank_t;

static int
callback_function_rank (Sheet *sheet, Value *value,
			char **error_string, void *closure)
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
		mm->num++;
		break;

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
		mm->num++;
		break;

	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_rank (Sheet *sheet, GList *expr_node_list,
	       int eval_col, int eval_row, char **error_string)
{
	stat_rank_t p;
	GSList      *list;
	int         order, rank;

	p.first = TRUE;
	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_rank,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);
	list  = p.list;
	order = (int) p.last;
	rank  = 1;

	while (list != NULL){
	        gpointer x;
		x = list->data;
		if (order){
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

	return value_int (rank);
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
	int     first;
	guint32 num;
        float_t last;
        GSList  *list;
} stat_trimmean_t;

static int
callback_function_trimmean (Sheet *sheet, Value *value,
			    char **error_string, void *closure)
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
		mm->num++;
		break;
	case VALUE_FLOAT:
	        if (mm->first == TRUE)
		        mm->last = value->v.v_float;
		else {
	                p = g_new(float_t, 1);
			*((float_t *) p) = mm->last;
			mm->list = g_slist_append(mm->list, p);
			mm->last = value->v.v_float;
		}
		mm->num++;
		break;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_trimmean (Sheet *sheet, GList *expr_node_list,
		   int eval_col, int eval_row, char **error_string)
{
	stat_trimmean_t p;
	GSList      *list;
	int         trim_count, n, count;
	float_t     sum;

	p.first = TRUE;
	p.num   = 0;
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
	for (n = 0; n < trim_count; n++){
	        g_free (list->data);
		list = list->next;
	}

	/* Count the sum for mean */
	for (n = sum = 0; n < count; n++){
	        gpointer x;

		x = list->data;
		sum += *((float_t *) x);
		g_free (x);
		list = list->next;
	}

	/* Free the rest of the number on the list */
	for (n = 0; n < trim_count; n++){
	        g_free (list->data);
		list = list->next;
	}

	g_slist_free (p.list);

	return value_float (sum / count);
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
	int     first;
	guint32 num;
	int     count;
        GSList  *array1;
        GSList  *array2;
        float_t sum1;
        float_t sum2;
} stat_covar_t;

static int
callback_function_covar (Sheet *sheet, Value *value,
			 char **error_string, void *closure)
{
	stat_covar_t *mm = closure;
	gpointer     p;

	switch (value->type){
	case VALUE_INTEGER:
	        p = g_new(float_t, 1);

		*((float_t *) p) = (float_t) value->v.v_int;

	        if (mm->num < mm->count){
		        mm->array1 = g_slist_append (mm->array1, p);
			mm->sum1 += value->v.v_int;
		} else {
		        mm->array2 = g_slist_append (mm->array2, p);
			mm->sum2 += value->v.v_int;
		}
		mm->num++;
		break;

	case VALUE_FLOAT:
	        p = g_new(float_t, 1);

		*((float_t *) p) = value->v.v_float;

	        if (mm->num < mm->count){
		        mm->array1 = g_slist_append(mm->array1, p);
			mm->sum1 += value->v.v_float;
		} else {
		        mm->array2 = g_slist_append(mm->array2, p);
			mm->sum2 += value->v.v_float;
		}
		mm->num++;
		break;

	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_covar (Sheet *sheet, GList *expr_node_list,
		int eval_col, int eval_row, char **error_string)
{
	stat_covar_t pr;
	float_t sum, mean1, mean2;
	int     count;
	GSList  *list1, *list2;

	pr.first   = TRUE;
	count = value_get_as_int (gnumeric_count
	        (sheet, expr_node_list, eval_col, eval_row, error_string));

	if (count % 2 > 0){
		*error_string = _("#NUM!");
		return NULL;
	}
	pr.count  = count / 2;
	pr.num    = 0;
	pr.sum1   = 0.0;
	pr.sum2   = 0.0;
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
	while (list1 != NULL && list2 != NULL){
	        gpointer x, y;

		x = list1->data;
		y = list2->data;
	        sum += (*((float_t *) x) - mean1) * (*((float_t *) y) - mean2);

		g_free (x);
		g_free (y);

		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free (pr.array1);
	g_slist_free (pr.array2);

	return value_float (sum / pr.count);
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
	int     first;
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
			  char **error_string, void *closure)
{
	stat_correl_t *mm = closure;
	gpointer     p;

	switch (value->type){
	case VALUE_INTEGER:
	        p = g_new (float_t, 1);

		*((float_t *) p) = (float_t) value->v.v_int;

	        if (mm->num < mm->count){
		        mm->array1 = g_slist_append (mm->array1, p);
			mm->sum1 += value->v.v_int;
			mm->sqrsum1 += value->v.v_int * value->v.v_int;
		} else {
		        mm->array2 = g_slist_append (mm->array2, p);
			mm->sum2 += value->v.v_int;
			mm->sqrsum2 += value->v.v_int * value->v.v_int;
		}
		mm->num++;
		break;

	case VALUE_FLOAT:
	        p = g_new (float_t, 1);
		*((float_t *) p) = value->v.v_float;

	        if (mm->num < mm->count){
		        mm->array1 = g_slist_append (mm->array1, p);
			mm->sum1 += value->v.v_float;
			mm->sqrsum1 += value->v.v_float * value->v.v_float;
		} else {
		        mm->array2 = g_slist_append (mm->array2, p);
			mm->sum2 += value->v.v_float;
			mm->sqrsum2 += value->v.v_float * value->v.v_float;
		}
		mm->num++;
		break;

	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_correl (Sheet *sheet, GList *expr_node_list,
		 int eval_col, int eval_row, char **error_string)
{
	stat_correl_t pr;
	float_t sum, tmp;
	int     count;
	GSList  *list1, *list2;

	pr.first   = TRUE;
	count = value_get_as_int(gnumeric_count
	        (sheet, expr_node_list, eval_col, eval_row, error_string));
	if (count % 2 > 0){
		*error_string = _("#NUM!");
		return NULL;
	}
	pr.count   = count / 2;
	pr.num     = 0;
	pr.sum1    = 0.0;
	pr.sum2    = 0.0;
	pr.sqrsum1 = 0.0;
	pr.sqrsum2 = 0.0;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (sheet, callback_function_correl,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL){
	        gpointer x, y;

		x = list1->data;
		y = list2->data;
	        sum += (*((float_t *) x)) * (*((float_t *) y));
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
	        return value_float (0);
	else
	        return value_float ((sum - (pr.sum1*pr.sum2/pr.count)) /
				    sqrt(tmp));
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
gnumeric_negbinomdist (struct FunctionDefinition *i,
		       Value *argv [], char **error_string)
{
	int x, r;
	float_t p;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!");
		return NULL;
	}

	x = value_get_as_int (argv [0]);
	r = value_get_as_int (argv [1]);
	p = value_get_as_double (argv[2]);

	if ((x + r -1) <= 0 || p < 0 || p > 1){
		*error_string = _("#NUM!");
		return NULL;
	}

	return value_float (combin (x+r-1, r-1) * pow (p, r) * pow (1-p, x));
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
gnumeric_normsdist (struct FunctionDefinition *i,
		    Value *argv [], char **error_string)
{
        float_t x;

        x = value_get_as_double (argv [0]);

	return value_float (phi (x));
}

static char *help_normsinv = {
       N_("@FUNCTION=NORMSINV\n"
          "@SYNTAX=NORMSINV(p)\n"

          "@DESCRIPTION="
          "The NORMSINV function returns the inverse of the standard normal "
	  "cumulative distribution. @p is the given probability corresponding "
	  "to the normal distribution. NORMSINV uses an iterative algorithm "
	  "for calculating the result. If NORMSINV does not converge "
	  "(accuracy within +/- 3x10^7) after 100 iterations, the function "
	  "returns #N/A! error. "
          "\n"
	  "If @p < 0 or @p > 1 NORMSINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=NORMDIST,NORMINV,NORMSDIST,STANDARDIZE,ZTEST")
};

static float_t
normsinv (float_t p)
{
        const int left = 1;
	const int right = 2;
        const int iterations = 200;
	const float_t accuracy_limit = 0.00000003;
        float_t p_test, x = 0, step = 1;
	int     n, dir = 0;

	for (n=0; n<iterations; n++) {
	        p_test = phi(x);
		if (fabs(p - p_test) < accuracy_limit)
		        return fabs(x);
		if (p < p_test) {
		        if (dir == right)
			        step /= 2;
		        x -= step;
			dir = left;
		} else {
		        if (dir == left)
			        step /= 2;
		        x += step;
			dir = right;
		}
	}
	return -1;
}

static Value *
gnumeric_normsinv (struct FunctionDefinition *i,
		   Value *argv [], char **error_string)
{
        float_t p, x;

        p = value_get_as_double (argv [0]);
	if (p < 0 || p > 1) {
		*error_string = _("#NUM!");
		return NULL;
	}
	x = normsinv(p);
	if (x < 0) {
		*error_string = _("#N/A!");
		return NULL;
	}
	return value_float(x);
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
gnumeric_lognormdist (struct FunctionDefinition *i,
		      Value *argv [], char **error_string)
{
        float_t x, mean, stdev;

        x = value_get_as_double (argv [0]);
        mean = value_get_as_double (argv [1]);
        stdev = value_get_as_double (argv [2]);

        if (stdev==0){
                *error_string = _("#DIV/0!");
                return NULL;
        }
        if (x<0 || mean<0 || stdev<0){
                *error_string = _("#NUM!");
                return NULL;
        }

	x = ((log (x)-mean) / stdev);
	return value_float (phi (x));
}

static char *help_loginv = {
       N_("@FUNCTION=LOGINV\n"
          "@SYNTAX=LOGINV(p,mean,stdev)\n"

          "@DESCRIPTION="
          "The LOGINV function returns the inverse of the lognormal "
	  "cumulative distribution. @p is the given probability corresponding "
	  "to the normal distribution, @mean is the arithmetic mean of the "
	  "distribution, and @stdev is the standard deviation of the "
	  "distribution. LOGINV uses an iterative algorithm "
	  "for calculating the result. If LOGINV does not converge "
	  "(accuracy within +/- 3x10^7) after 100 iterations, the function "
	  "returns #N/A! error. "
          "\n"
	  "If @p < 0 or @p > 1 or @stdev <= 0 LOGINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=EXP,LN,LOG,LOG10,LOGNORMDIST")
};

static Value *
gnumeric_loginv (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
        const int left = 1;
	const int right = 2;
        const int iterations = 100;
	const float_t accuracy_limit = 0.00000003;
        float_t p, p_test, x, step;
	float_t mean, stdev;
	int     n, dir = 0;

        p = value_get_as_double (argv [0]);
        mean = value_get_as_double (argv [1]);
        stdev = value_get_as_double (argv [2]);
	if (p < 0 || p > 1 || stdev <= 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	x = 0;
	step = 1;
	for (n=0; n<iterations; n++) {
	        p_test = phi(x);
		if (fabs(p - p_test) < accuracy_limit)
		        return value_float (exp(mean+stdev*x));
		if (p < p_test) {
		        if (dir == right)
			        step /= 2;
		        x -= step;
			dir = left;
		} else {
		        if (dir == left)
			        step /= 2;
		        x += step;
			dir = right;
		}
	}
	*error_string = _("#N/A!");
	return NULL;
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
gnumeric_fisherinv (struct FunctionDefinition *i,
		    Value *argv [], char **error_string)
{
       float_t y;

       if (!VALUE_IS_NUMBER(argv [0])){
               *error_string = _("#VALUE!");
               return NULL;
       }
       y = value_get_as_double (argv [0]);
       return value_float ((exp (2*y)-1.0) / (exp (2*y)+1.0));
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
       int        first;
       float_t    mode;
       int        count;
} stat_mode_t;

static int
callback_function_mode (Sheet *sheet, Value *value,
			char **error_string, void *closure)
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

       p = g_hash_table_lookup (mm->hash_table, &key);

       if (p == NULL){
	       p = g_new (int, 1);
	       mm->items = g_slist_append (mm->items, p);
	       *((int *) p) = 1;
	       g_hash_table_insert (mm->hash_table, &key, p);
	       count = 1;
       } else {
	       *((int *) p) += 1;
	       count = *((int *) p);
       }

       if (count > mm->count){
	       mm->count = count;
	       mm->mode = key;
       }
       mm->first = FALSE;
       return TRUE;
}

static Value *
gnumeric_mode (Sheet *sheet, GList *expr_node_list,
	       int eval_col, int eval_row, char **error_string)
{
       GSList *tmp;
       stat_mode_t pr;

       pr.first = TRUE;

       pr.hash_table = g_hash_table_new((GHashFunc) float_hash,
					(GCompareFunc) float_equal);
       pr.items      = NULL;
       pr.mode       = 0.0;
       pr.count      = 0;

       function_iterate_argument_values (sheet, callback_function_mode,
                                         &pr, expr_node_list,
                                         eval_col, eval_row, error_string);

       g_hash_table_destroy (pr.hash_table);
       tmp = pr.items;
       while (tmp != NULL){
	       g_free (tmp->data);
	       tmp = tmp->next;
       }
       g_slist_free (pr.items);
       if (pr.count < 2){
		*error_string = _("#N/A!");
		return NULL;
       }
       return value_float (pr.mode);
}

static Value *
gnumeric_harmean (Sheet *sheet, GList *expr_node_list,
		  int eval_col, int eval_row, char **error_string)
{
	stat_inv_sum_t pr;
	float_t ans, num;

	pr.first = TRUE;
	pr.num   = 0;
	pr.sum   = 0.0;

	function_iterate_argument_values (sheet, callback_function_stat_inv_sum,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	num = (float_t)pr.num;
	return value_float (1.0 / (1.0/num * pr.sum));
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
	int first;
	guint32 num;
	float_t product;
} stat_prod_t;

static int
callback_function_stat_prod (Sheet *sheet, Value *value,
			     char **error_string, void *closure)
{
	stat_prod_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++;
		mm->product = mm->first?value->v.v_int:mm->product*value->v.v_int;
		break;

	case VALUE_FLOAT:
		mm->num++;
		mm->product = mm->first?value->v.v_float:mm->product*value->v.v_float;
		break;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_geomean (Sheet *sheet, GList *expr_node_list,
		  int eval_col, int eval_row, char **error_string)
{
	stat_prod_t pr;
	float_t ans, num;

	pr.first   = TRUE;
	pr.num     = 0;
	pr.product = 0.0;

	function_iterate_argument_values (sheet, callback_function_stat_prod,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	num = (float_t)pr.num;
	return value_float (pow (pr.product, 1.0/num));
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
			 char **error_string, void *closure)
{
	Value *result = (Value *) closure;

	switch (value->type){
	case VALUE_INTEGER:
		result->v.v_int++;
		break;
		
	case VALUE_FLOAT:
		result->v.v_int++;
		break;
		
	default:
		break;
	}		
	return TRUE;
}

Value *
gnumeric_count (Sheet *sheet, GList *expr_node_list,
		int eval_col, int eval_row, char **error_string)
{
	Value *result;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 0;
	
	function_iterate_argument_values (sheet, callback_function_count,
					  result, expr_node_list,
					  eval_col, eval_row, error_string);

	return result;
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
			  char **error_string, void *
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

static Value *
gnumeric_counta (Sheet *sheet, GList *expr_node_list,
		 int eval_col, int eval_row, char **error_string)
{
        Value *result;

        result = g_new (Value, 1);
        result->type = VALUE_INTEGER;
        result->v.v_int = 0;

        function_iterate_argument_values (sheet, callback_function_counta,
					  result, expr_node_list,
                                          eval_col, eval_row, error_string);

        return result;
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

Value *
gnumeric_average (Sheet *sheet, GList *expr_node_list,
		  int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Value *sum, *count;
	double c;
	
	sum = gnumeric_sum (sheet, expr_node_list,
			    eval_col, eval_row, error_string);
	if (!sum)
		return NULL;
	
	count = gnumeric_count (sheet, expr_node_list,
				eval_col, eval_row, error_string);
	if (!count){
		value_release (sum);
		return NULL;
	}

	c = value_get_as_double (count);
	
	if (c == 0.0){
		*error_string = _("#DIV/0!");
		value_release (sum);
		return NULL;
	}
	
	result = value_float (value_get_as_double (sum) / c);

	value_release (count);
	value_release (sum);
	
	return result;
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
			   char **error_string, void *closure)
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

	default:
		/* ignore strings */
		break;
	}
	
	return TRUE;
}

static Value *
gnumeric_min (Sheet *sheet, GList *expr_node_list,
	      int eval_col, int eval_row, char **error_string)
{
	min_max_closure_t closure;

	closure.operation = OPER_MIN;
	closure.found  = 0;
	closure.result = g_new (Value, 1);
	closure.result->type = VALUE_FLOAT;
	closure.result->v.v_float = 0;

	function_iterate_argument_values (sheet, callback_function_min_max,
					  &closure, expr_node_list,
					  eval_col, eval_row, error_string);

	return 	closure.result;
}

static Value *
gnumeric_max (Sheet *sheet, GList *expr_node_list,
	      int eval_col, int eval_row, char **error_string)
{
	min_max_closure_t closure;

	closure.operation = OPER_MAX;
	closure.found  = 0;
	closure.result = g_new (Value, 1);
	closure.result->type = VALUE_FLOAT;
	closure.result->v.v_float = 0;

	function_iterate_argument_values (sheet, callback_function_min_max,
					  &closure, expr_node_list,
					  eval_col, eval_row, error_string);
	return 	closure.result;
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
	int first;
	guint32 num;
        float_t mean;
        float_t stddev;
	float_t sum;
} stat_skew_sum_t;

static int
callback_function_skew_sum (Sheet *sheet, Value *value,
			    char **error_string, void *closure)
{
	stat_skew_sum_t *mm = closure;
	float tmp;

	switch (value->type){
	case VALUE_INTEGER:
	        tmp = (value->v.v_int - mm->mean) / mm->stddev;
		mm->num++;
		mm->sum += tmp * tmp * tmp;
		break;

	case VALUE_FLOAT:
	        tmp = (value->v.v_float - mm->mean) / mm->stddev;
		mm->num++;
		mm->sum += tmp * tmp * tmp;
		break;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_skew (Sheet *sheet, GList *expr_node_list,
	       int eval_col, int eval_row, char **error_string)
{
	stat_skew_sum_t pr;
	float_t num;

	pr.first   = TRUE;

	pr.mean = value_get_as_double(gnumeric_average
	        (sheet, expr_node_list, eval_col, eval_row, error_string));
	pr.stddev = value_get_as_double(gnumeric_stdev
	        (sheet, expr_node_list, eval_col, eval_row, error_string));
	pr.num  = 0;
	pr.sum  = 0.0;

	function_iterate_argument_values (sheet, callback_function_skew_sum,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	if (pr.num < 3){
		*error_string = _("#NUM!");
		return NULL;
	}

	num = (float_t)pr.num;
	return value_float ((pr.num / ((pr.num-1.0) * (pr.num-2.0))) * pr.sum);
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
gnumeric_expondist (struct FunctionDefinition *i,
		    Value *argv [], char **error_string)
{
	float_t x, y;
	int cuml, err=0;

	x = value_get_as_double (argv [0]);
	y = value_get_as_double (argv [1]);
	if (x < 0.0 || y <= 0.0){
		*error_string = _("#NUM!");
		return NULL;
	}
	cuml = value_get_bool (argv[2], &err);
	if (err){
		*error_string = _("#VALUE!");
		return NULL;
	}
	
	if (cuml){
		return value_float (-expm1(-y*x));
	} else {
		return value_float (y*exp(-y*x));
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
gnumeric_gammaln (struct FunctionDefinition *i, 
		  Value *argv [], char **error_string)
{
	float_t x;

	if (argv[0]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT){
		*error_string = _("#VALUE!");
		return NULL;
	}

	x = value_get_as_double (argv [0]);
	if (x<=0){
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_float (lgamma(x));
}

static char *help_gammadist = {
	N_("@FUNCTION=GAMMADIST\n"
	   "@SYNTAX=GAMMADIST(x,alpha,beta,cum)\n"

	   "@DESCRIPTION="
	   "GAMMADIST function returns the gamma distribution. If @cum "
	   "is TRUE GAMMADIST returns the incomplete gamma function, "
	   "otherwise it returns the probability mass function. "
	   "\n"
	   "If @x < 0 GAMMADIST returns #NUM! error. "
	   "If @alpha <= 0 or beta <= 0, GAMMADIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=GAMMAINV")
};

static Value *
gnumeric_gammadist (struct FunctionDefinition *i, Value *argv [],
		    char **error_string)
{
	float_t x, alpha, beta;
	int     cum;

	x = value_get_as_double (argv [0]);
	alpha = value_get_as_double (argv [1]);
	beta = value_get_as_double (argv [2]);

	if (x<0 || alpha<=0 || beta<=0){
		*error_string = _("#NUM!");
		return NULL;
	}
	cum = value_get_as_int (argv [3]);
	if (cum)
	        return value_float (pgamma(x, alpha, beta));
	else
	        return value_float ((pow(x, alpha-1) * exp(-x/beta)) /
				    (pow(beta, alpha) * exp(lgamma(alpha))));
}

static char *help_gammainv = {
       N_("@FUNCTION=GAMMAINV\n"
          "@SYNTAX=GAMMAINV(p,alpha,beta)\n"

          "@DESCRIPTION="
          "The GAMMAINV function returns the inverse of the cumulative "
	  "gamma distribution. If GAMMAINV does not converge (accuracy "
	  "within +/- 3x10^7) after 100 iterations, the function returns "
	  "#N/A! error. "
          "\n"
	  "If @p < 0 or @p > 1 GAMMAINV returns #NUM! error. "
	  "If @alpha <= 0 or @beta <= 0 GAMMAINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=GAMMADIST")
};

static Value *
gnumeric_gammainv (struct FunctionDefinition *i, Value *argv [],
		   char **error_string)
{
        const int left = 1;
	const int right = 2;
        const int iterations = 200;
	const float_t accuracy_limit = 0.000003;
        float_t p, p_test, x, step;
	int     alpha, beta, n, dir = 0;

        p = value_get_as_double (argv [0]);
	alpha = value_get_as_double (argv [1]);
	beta = value_get_as_double (argv [2]);

	if (p<0 || p>1 || alpha<=0 || beta<=0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	x = 10;
	step = 1;
	for (n=0; n<iterations; n++) {
	        if (x < 0)
		        p_test = 0.0;
		else
		        p_test = pgamma(x, alpha, beta);
		if (fabs(p - p_test) < accuracy_limit)
		        return value_float (x);
		if (p < p_test) {
		        if (dir == right)
			        step /= 2;
		        x += step;
			dir = left;
		} else {
		        if (dir == left)
			        step /= 2;
		        x -= step;
			dir = right;
		}
	}
	*error_string = _("#N/A!");
	return NULL;
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
	   "If @dof < 1 CHIDIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=CHIINV,CHITEST")
};

static Value *
gnumeric_chidist (struct FunctionDefinition *i, Value *argv [],
		  char **error_string)
{
	float_t x;
	int     dof;

	x = value_get_as_double (argv [0]);
	dof = value_get_as_int (argv [1]);

	if (dof<1) {
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_float (1.0 - pgamma(x, dof / 2.0, 2.0));
}

static char *help_chiinv = {
       N_("@FUNCTION=CHIINV\n"
          "@SYNTAX=CHIINV(p,dof)\n"

          "@DESCRIPTION="
          "The CHIINV function returns the inverse of the one-tailed "
	  "probability of the chi-squared distribution. CHIINV uses an "
	  "iterative algorithm for calculating the result. If CHIINV "
	  "does not converge (accuracy within +/- 3x10^7) after 100 "
	  "iterations, the function returns #N/A! error. "
          "\n"
	  "If @p < 0 or @p > 1 or @dof < 1 CHIINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=CHIDIST,CHITEST")
};

static Value *
gnumeric_chiinv (struct FunctionDefinition *i, Value *argv [],
		 char **error_string)
{
        const int left = 1;
	const int right = 2;
        const int iterations = 200;
	const float_t accuracy_limit = 0.00000003;
        float_t p, p_test, x, step;
	int     dof, n, dir = 0;

        p = value_get_as_double (argv [0]);
	dof = value_get_as_int (argv [1]);

	if (p<0 || p>1 || dof<1) {
		*error_string = _("#NUM!");
		return NULL;
	}
	x = 10;
	step = 1;
	for (n=0; n<iterations; n++) {
	        if (x < 0)
		        p_test = 1.0;
		else
		        p_test = 1.0 - pgamma(x, dof / 2.0, 2.0);
		if (fabs(p - p_test) < accuracy_limit)
		        return value_float (x);
		if (p < p_test) {
		        if (dir == right)
			        step /= 2;
		        x += step;
			dir = left;
		} else {
		        if (dir == left)
			        step /= 2;
		        x -= step;
			dir = right;
		}
	}
	*error_string = _("#N/A!");
	return NULL;
}

static char *help_betadist = {
	N_("@FUNCTION=BETADIST\n"
	   "@SYNTAX=BETADIST(x,alpha,beta)\n"

	   "@DESCRIPTION="
	   "BETADIST function returns the cumulative beta distribution. "
	   "\n"
	   "If @x < 0 or @x > 1 BETADIST returns #NUM! error. "
	   "If @alpha <= 0 or beta <= 0, BETADIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=BETAINV")
};

static Value *
gnumeric_betadist (struct FunctionDefinition *i, Value *argv [],
		   char **error_string)
{
	float_t x, alpha, beta;

	x = value_get_as_double (argv [0]);
	alpha = value_get_as_double (argv [1]);
	beta = value_get_as_double (argv [2]);

	if (x<0 || x>1 || alpha<=0 || beta<=0){
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_float (pbeta(x, alpha, beta));
}

static char *help_tdist = {
	N_("@FUNCTION=TDIST\n"
	   "@SYNTAX=TDIST(x,dof,tails)\n"

	   "@DESCRIPTION="
	   "TDIST function returns the Student's t-distribution. @dof is "
	   "the degree of freedom and @tails is 1 or 2 depending on wheater "
	   "you want one-tailed or two-tailed distribution. "
	   "\n"
	   "If @dof < 1 TDIST returns #NUM! error. "
	   "If @tails is neither 1 or 2 TDIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=TINV,TTEST")
};

static Value *
gnumeric_tdist (struct FunctionDefinition *i, Value *argv [],
		char **error_string)
{
	float_t x;
	int     dof, tails;

	x = value_get_as_double (argv [0]);
	dof = value_get_as_int (argv [1]);
	tails = value_get_as_int (argv [2]);

	if (dof<1 || (tails!=1 && tails!=2)){
		*error_string = _("#NUM!");
		return NULL;
	}
	if (tails == 1)
	        return value_float (pt(x, dof));
	else
	        return value_float (pt(x, dof)*2);
}

static char *help_tinv = {
       N_("@FUNCTION=TINV\n"
          "@SYNTAX=TINV(p,dof)\n"

          "@DESCRIPTION="
          "The TINV function returns the inverse of the two-tailed Student's "
	  "t-distribution. TINV uses an iterative algorithm for calculating "
	  "the result. If TINV does not converge (accuracy within +/- 3x10^7) "
	  "after 100 iterations, the function returns #N/A! error. "
          "\n"
	  "If @p < 0 or @p > 1 or @dof < 1 TINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=TDIST,TTEST")
};

static Value *
gnumeric_tinv (struct FunctionDefinition *i, Value *argv [],
		 char **error_string)
{
        const int left = 1;
	const int right = 2;
        const int iterations = 200;
	const float_t accuracy_limit = 0.00000003;
        float_t p, p_test, x, step;
	int     dof, n, dir = 0;

        p = value_get_as_double (argv [0]);
	dof = value_get_as_int (argv [1]);

	if (p<0 || p>1 || dof<1) {
		*error_string = _("#NUM!");
		return NULL;
	}
	x = 10;
	step = 1;
	for (n=0; n<iterations; n++) {
	        p_test = pt(x, dof)*2;
		if (fabs(p - p_test) < accuracy_limit)
		        return value_float (x);
		if (p < p_test) {
		        if (dir == right)
			        step /= 2;
		        x += step;
			dir = left;
		} else {
		        if (dir == left)
			        step /= 2;
		        x -= step;
			dir = right;
		}
	}
	*error_string = _("#N/A!");
	return NULL;
}

static char *help_fdist = {
	N_("@FUNCTION=FDIST\n"
	   "@SYNTAX=FDIST(x,dof1,dof2)\n"

	   "@DESCRIPTION="
	   "FDIST function returns the F probability distribution. @dof1 "
	   "is the numerator degrees of freedom and @dof2 is the denominator "
	   "degrees of freedom. "
	   "\n"
	   "If @x < 0 FDIST returns #NUM! error. "
	   "If @dof1 < 1 or @dof2 < 1, GAMMADIST returns #NUM! error. "
	   "\n"
	   "@SEEALSO=FINV")
};

static Value *
gnumeric_fdist (struct FunctionDefinition *i, Value *argv [],
		char **error_string)
{
	float_t x;
	int     dof1, dof2;

	x = value_get_as_double (argv [0]);
	dof1 = value_get_as_int (argv [1]);
	dof2 = value_get_as_int (argv [2]);

	if (x<0 || dof1<1 || dof2<1){
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_float (pf(x, dof1, dof2));
}

static char *help_finv = {
       N_("@FUNCTION=FINV\n"
          "@SYNTAX=TINV(p,dof)\n"

          "@DESCRIPTION="
          "The FINV function returns the inverse of the F probability "
	  "distribution. FINV uses an iterative algorithm for calculating "
	  "the result. If FINV does not converge (accuracy within +/- 3x10^7) "
	  "after 100 iterations, the function returns #N/A! error. "
          "\n"
	  "If @p < 0 or @p > 1 FINV returns #NUM! error. "
	  "If @dof1 < 0 or @dof2 > 1 FINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=FDIST")
};

static Value *
gnumeric_finv (struct FunctionDefinition *i, Value *argv [],
		 char **error_string)
{
        const int left = 1;
	const int right = 2;
        const int iterations = 200;
	const float_t accuracy_limit = 0.00000003;
        float_t p, p_test, x, step;
	int     dof1, dof2, n, dir = 0;

        p = value_get_as_double (argv [0]);
	dof1 = value_get_as_int (argv [1]);
	dof2 = value_get_as_int (argv [2]);

	if (p<0 || p>1 || dof1<1 || dof2<1) {
		*error_string = _("#NUM!");
		return NULL;
	}
	x = 10;
	step = 1;
	for (n=0; n<iterations; n++) {
	        p_test = pf(x, dof1, dof2);
		if (fabs(p - p_test) < accuracy_limit)
		        return value_float (x);
		if (p < p_test) {
		        if (dir == right)
			        step /= 2;
		        x += step;
			dir = left;
		} else {
		        if (dir == left)
			        step /= 2;
		        x -= step;
			dir = right;
		}
	}
	*error_string = _("#N/A!");
	return NULL;
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
gnumeric_binomdist (struct FunctionDefinition *i,
		    Value *argv [], char **error_string)
{
	int n, trials;
	float_t p;
	int cuml, err, x;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!");
		return NULL;
	}
	n = value_get_as_int (argv [0]);
	trials = value_get_as_int (argv [1]);
	p = value_get_as_double (argv[2]);
	if (n<0 || trials<0 || p<0 || p>1 || n>trials){
		*error_string = _("#NUM!");
		return NULL;
	}

	cuml = value_get_bool (argv[3], &err);

	if (cuml){
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
gnumeric_critbinom (struct FunctionDefinition *i,
		    Value *argv [], char **error_string)
{
        int trials;
        float_t p, alpha, sum;
        int x;

        if (!VALUE_IS_NUMBER(argv[0]) ||
            !VALUE_IS_NUMBER(argv[1]) ||
            !VALUE_IS_NUMBER(argv[2])){
                *error_string = _("#VALUE!");
                return NULL;
        }
        trials = value_get_as_int (argv [0]);
        p = value_get_as_double (argv[1]);
        alpha = value_get_as_double (argv[2]);

        if (trials<0 || p<0 || p>1 || alpha<0 || alpha>1){
	        *error_string = _("#NUM!");
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
gnumeric_permut (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
	int n, k;

	if (argv[0]->type != VALUE_INTEGER ||
	    argv[1]->type != VALUE_INTEGER){
		*error_string = _("#VALUE!");
		return NULL;
	}
	n = value_get_as_int (argv [0]);
	k = value_get_as_int (argv [1]);

	if (n<k || n==0){
		*error_string = _("#NUM!");
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
gnumeric_hypgeomdist (struct FunctionDefinition *i,
		      Value *argv [], char **error_string)
{
	int x, n, M, N;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!");
		return NULL;
	}
	x = value_get_as_int (argv [0]);
	n = value_get_as_int (argv [1]);
	M = value_get_as_int (argv [2]);
	N = value_get_as_int (argv [3]);

	if (x<0 || n<0 || M<0 || N<0 || x>M || n>N){
		*error_string = _("#NUM!");
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
gnumeric_confidence (struct FunctionDefinition *i,
		     Value *argv [], char **error_string)
{
	float_t x, stddev;
	int size;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!");
		return NULL;
	}
	x = value_get_as_double (argv [0]);
	stddev = value_get_as_double (argv [1]);
	size = value_get_as_int (argv [2]);
	if (size == 0){
		*error_string = _("#DIV/0!");
		return NULL;
	}
	if (size < 0){
		*error_string = _("#NUM!");
		return NULL;
	}

	return value_float (normsinv(x/2) * (stddev/sqrt(size)));
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
gnumeric_standardize (struct FunctionDefinition *i,
		      Value *argv [], char **error_string)
{
	float_t x, mean, stddev;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1]) ||
	    !VALUE_IS_NUMBER(argv[2])){
		*error_string = _("#VALUE!");
		return NULL;
	}
	x = value_get_as_double (argv [0]);
	mean = value_get_as_double (argv [1]);
	stddev = value_get_as_double (argv [2]);
	if (stddev == 0){
		*error_string = _("#DIV/0!");
		return NULL;
	}

	return value_float ((x-mean) / stddev);
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
gnumeric_weibull (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
        float_t x, alpha, beta;
        int cuml, err=0;

        x = value_get_as_double (argv [0]);
        alpha = value_get_as_double (argv [1]);
        beta = value_get_as_double (argv [2]);
        if (x<0 || alpha<=0 || beta<=0){
                *error_string = _("#NUM!");
                return NULL;
        }
        cuml = value_get_bool (argv[3], &err);
        if (err){
                *error_string = _("#VALUE!");
                return NULL;
        }

        if (cuml)
                return value_float (1.0 - exp(-pow(x/beta, alpha)));
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


static Value *
gnumeric_normdist (struct FunctionDefinition *i,
		   Value *argv [], char **error_string)
{
        float_t x, mean, stdev;
        int cuml, err=0;

        x = value_get_as_double (argv [0]);
        mean = value_get_as_double (argv [1]);
        stdev = value_get_as_double (argv [2]);
        if (stdev==0){
                *error_string = _("#DIV/0!");
                return NULL;
        }
        cuml = value_get_bool (argv[3], &err);
        if (err){
                *error_string = _("#VALUE!");
                return NULL;
        }

        if (cuml){
        	x = ((x-mean) / stdev);
		return value_float (phi (x));
        } else {
		*error_string = _("Unimplemented");
		return NULL;
        }
}

static char *help_norminv = {
       N_("@FUNCTION=NORMINV\n"
          "@SYNTAX=NORMINV(p,mean,stdev)\n"

          "@DESCRIPTION="
          "The NORMINV function returns the inverse of the normal "
	  "cumulative distribution. @p is the given probability corresponding "
	  "to the normal distribution, @mean is the arithmetic mean of the "
	  "distribution, and @stdev is the standard deviation of the "
	  "distribution. NORMINV uses an iterative algorithm "
	  "for calculating the result. If NORMINV does not converge "
	  "(accuracy within +/- 3x10^7) after 100 iterations, the function "
	  "returns #N/A! error. "
          "\n"
	  "If @p < 0 or @p > 1 or @stdev <= 0 NORMINV returns #NUM! error. "
	  "\n"
          "@SEEALSO=NORMDIST,NORMSDIST,NORMSINV,STANDARDIZE,ZTEST")
};

static Value *
gnumeric_norminv (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
        const int left = 1;
	const int right = 2;
        const int iterations = 100;
	const float_t accuracy_limit = 0.00000003;
        float_t p, p_test, x, step;
	float_t mean, stdev;
	int     n, dir = 0;

        p = value_get_as_double (argv [0]);
	mean = value_get_as_double (argv [1]);
	stdev = value_get_as_double (argv [2]);
	if (p < 0 || p > 1 || stdev <= 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	x = mean;
	step = stdev;
	for (n=0; n<iterations; n++) {
	        p_test = phi((x-mean) / stdev);
		if (fabs(p - p_test) < accuracy_limit)
		        return value_float (x);
		if (p < p_test) {
		        if (dir == right)
			        step /= 2;
		        x -= step;
			dir = left;
		} else {
		        if (dir == left)
			        step /= 2;
		        x += step;
			dir = right;
		}
	}
	*error_string = _("#N/A!");
	return NULL;
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
        int first;
        guint32 num;
        float_t mean;
        float_t stddev;
        float_t sum;
} stat_kurt_sum_t;

static int
callback_function_kurt_sum (Sheet *sheet, Value *value,
			    char **error_string, void *closure)
{
        stat_kurt_sum_t *mm = closure;
        float_t tmp;

        switch (value->type){
        case VALUE_INTEGER:
                tmp = (value->v.v_int - mm->mean) / mm->stddev;
                tmp *= tmp;
                mm->num++;
                mm->sum += tmp * tmp;
                break;

        case VALUE_FLOAT:
                tmp = (value->v.v_float - mm->mean) / mm->stddev;
                tmp *= tmp;
                mm->num++;
                mm->sum += tmp * tmp;
                break;
        default:
                /* ignore strings */
                break;
        }
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_kurt (Sheet *sheet, GList *expr_node_list,
	       int eval_col, int eval_row, char **error_string)
{
        stat_kurt_sum_t pr;
	float_t n, d, num, dem;

	pr.first = TRUE;
	pr.mean = value_get_as_double(gnumeric_average
                (sheet, expr_node_list, eval_col, eval_row, error_string));
	pr.stddev = value_get_as_double(gnumeric_stdev
                (sheet, expr_node_list, eval_col, eval_row, error_string));
	pr.num  = 0;
	pr.sum  = 0.0;

	if (pr.stddev == 0.0){
	        *error_string = _("#NUM!");
                return NULL;
	}
	function_iterate_argument_values (sheet, callback_function_kurt_sum,
                                          &pr, expr_node_list,
                                          eval_col, eval_row, error_string);

	if (pr.num < 4){
                *error_string = _("#NUM!");
                return NULL;
	}

	n = (float_t) pr.num;
	num = (n*(n+1.0));
	dem = ((n-1.0) * (n-2.0) * (n-3.0));
	d = (3*(n-1)*(n-1)) / ((n-2) * (n-3));

	return value_float ((pr.sum) * (num/dem) - d);
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
        int first;
        guint32 num;
        float_t mean;
        float_t sum;
} stat_avedev_sum_t;

static int
callback_function_stat_avedev_sum (Sheet *sheet, Value *value,
				   char **error_string, void *closure)
{
        stat_avedev_sum_t *mm = closure;

	switch (value->type){
	case VALUE_INTEGER:
	        mm->num++;
                mm->sum += fabs(value->v.v_int - mm->mean);
                break;
        case VALUE_FLOAT:
                mm->num++;
                mm->sum += fabs(value->v.v_float - mm->mean);
                break;
        default:
                /* ignore strings */
                break;
        }
        mm->first = FALSE;
        return TRUE;
}

static Value *
gnumeric_avedev (Sheet *sheet, GList *expr_node_list,
		 int eval_col, int eval_row, char **error_string)
{
        stat_avedev_sum_t pr;
        float_t ans, num;

        pr.first = TRUE;
        pr.num   = 0;
        pr.sum   = 0.0;
        pr.mean  = value_get_as_double(gnumeric_average
	 			       (sheet, expr_node_list,
				        eval_col, eval_row, error_string));

	function_iterate_argument_values (sheet, callback_function_stat_avedev_sum,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);

	num = (float_t)pr.num;
	return value_float ((1.0/num) * pr.sum);
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
        int first;
        guint32 num;
        float_t mean;
        float_t sum;
} stat_devsq_sum_t;

static int
callback_function_devsq_sum (Sheet *sheet, Value *value, 
			     char **error_string, void *closure)
{
        stat_devsq_sum_t *mm = closure;
        float_t tmp;

        switch (value->type){
        case VALUE_INTEGER:
                tmp = value->v.v_int - mm->mean;
                mm->num++;
                mm->sum += tmp * tmp;
                break;
        case VALUE_FLOAT:
                tmp = value->v.v_int - mm->mean;
                mm->num++;
                mm->sum += tmp * tmp;
                break;
        default:
                /* ignore strings */
                break;
        }
        mm->first = FALSE;
        return TRUE;
}

static Value *
gnumeric_devsq (Sheet *sheet, GList *expr_node_list,
		int eval_col, int eval_row, char **error_string)
{
        stat_devsq_sum_t pr;

        pr.first = TRUE;

        pr.mean = value_get_as_double(gnumeric_average
                (sheet, expr_node_list, eval_col, eval_row, error_string));
        pr.num  = 0;
        pr.sum  = 0.0;

        function_iterate_argument_values (sheet, callback_function_devsq_sum,
                                          &pr, expr_node_list,
                                          eval_col, eval_row, error_string);

        return value_float (pr.sum);
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
gnumeric_fisher (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
        float_t x;

        if (!VALUE_IS_NUMBER(argv [0])){
                *error_string = _("#VALUE!");
                return NULL;
        }
        x = value_get_as_double (argv [0]);
        if (x <= -1.0 || x >= 1.0){
                *error_string = _("#NUM!");
                return NULL;
        }
        return value_float (0.5 * log((1.0+x) / (1.0-x)));
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
gnumeric_poisson (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	float_t x, mean;
	int cuml, err;

	if (argv[0]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_FLOAT){
		*error_string = _("#VALUE!");
		return NULL;
	}
	x = value_get_as_int (argv [0]);
	mean = value_get_as_double (argv [1]);
	if (x<=0  || mean <=0){
		*error_string = _("#NUM!");
		return NULL;
	}

	cuml = value_get_bool (argv[2], &err);

	if (cuml){
		*error_string = _("Unimplemented");
		return NULL;
	} else
		return value_float (exp(-mean)*pow(mean,x) / 
				    exp (lgamma (x + 1)));
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
gnumeric_pearson (Sheet *sheet, GList *expr_node_list,
		  int eval_col, int eval_row, char **error_string)
{
	stat_correl_t pr;
	float_t sum;
	int     count;
	GSList  *list1, *list2;

	pr.first   = TRUE;
	count = value_get_as_int(gnumeric_count
	        (sheet, expr_node_list, eval_col, eval_row, error_string));
	if (count % 2 > 0){
		*error_string = _("#NUM!");
		return NULL;
	}
	pr.count   = count / 2;
	pr.num     = 0;
	pr.sum1    = 0.0;
	pr.sum2    = 0.0;
	pr.sqrsum1 = 0.0;
	pr.sqrsum2 = 0.0;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (sheet, callback_function_correl,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL){
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

static char *help_rsq = {
	N_("@FUNCTION=RSQ\n"
	   "@SYNTAX=RSQ(array1,array2)\n"

	   "@DESCRIPTION="
	   "RSQ returns the square of the Pearson correllation coefficient "
	   "of two data sets. "
	   "\n"
	   "Strings and empty cells are simply ignored."
	   "\n"
	   "@SEEALSO=CORREL,COVAR,INTERPCEPT,LINEST,LOGEST,PEARSON,SLOPE,"
	   "STEYX,TREND")
};

static Value *
gnumeric_rsq (Sheet *sheet, GList *expr_node_list,
	      int eval_col, int eval_row, char **error_string)
{
	stat_correl_t pr;
	float_t sum, r;
	int     count;
	GSList  *list1, *list2;

	pr.first   = TRUE;
	count = value_get_as_int(gnumeric_count
	        (sheet, expr_node_list, eval_col, eval_row, error_string));
	if (count % 2 > 0){
		*error_string = _("#NUM!");
		return NULL;
	}
	pr.count   = count / 2;
	pr.num     = 0;
	pr.sum1    = 0.0;
	pr.sum2    = 0.0;
	pr.sqrsum1 = 0.0;
	pr.sqrsum2 = 0.0;
	pr.array1  = NULL;
	pr.array2  = NULL;

	function_iterate_argument_values (sheet, callback_function_correl,
					  &pr, expr_node_list,
					  eval_col, eval_row, error_string);
	list1 = pr.array1;
	list2 = pr.array2;
	sum = 0.0;

	while (list1 != NULL && list2 != NULL){
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

	r = (((pr.count*sum - pr.sum1*pr.sum2)) /
	     sqrt((pr.count*pr.sqrsum1 - pr.sum1*pr.sum1) *
		  (pr.count*pr.sqrsum2 - pr.sum2*pr.sum2)));
	return value_float (r * r);
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
	int     first;
	guint32 num;
        GSList  *list;
} stat_median_t;

static int
callback_function_median (Sheet *sheet, Value *value,
			  char **error_string, void *closure)
{
	stat_median_t *mm = closure;
	gpointer        p;

	switch (value->type){
	case VALUE_INTEGER:
	        p = g_new(float_t, 1);
		*((float_t *) p) = value->v.v_int;
		mm->list = g_slist_append(mm->list, p);
		mm->num++;
		break;
	case VALUE_FLOAT:
	        p = g_new(float_t, 1);
		*((float_t *) p) = value->v.v_float;
		mm->list = g_slist_append(mm->list, p);
		mm->num++;
		break;
	default:
		/* ignore strings */
		break;
	}
	mm->first = FALSE;
	return TRUE;
}

static Value *
gnumeric_median (Sheet *sheet, GList *expr_node_list,
		 int eval_col, int eval_row, char **error_string)
{
	stat_median_t   p;
	GSList          *list;
	int             median_ind, n;
	float_t         median;

	p.first = TRUE;
	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_median,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	median_ind = (p.num-1) / 2;
	p.list = g_slist_sort (p.list, (GCompareFunc) float_compare);
	list  = p.list;

	/* Skip half of the list */
	for (n=0; n<median_ind; n++){
	        g_free(list->data);
		list = list->next;
	}

	if ((p.num-1) % 2 == 0)
	        median = *((float_t *) list->data);
	else
	        median = (*((float_t *) list->data) +
			  *((float_t *) list->next->data)) / 2.0;

	while (list != NULL){
	        g_free(list->data);
		list = list->next;
	}

	g_slist_free(p.list);

	return value_float (median);
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
gnumeric_large (Sheet *sheet, GList *expr_node_list,
		int eval_col, int eval_row, char **error_string)
{
	stat_trimmean_t p;
	GSList          *list;
	int             n, count, k;
	float_t         r;

	p.first = TRUE;
	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_trimmean,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	p.num--;
	k = ((int) p.last);

	if (p.num == 0 || k<=0 || k >= p.num){
		*error_string = _("#NUM!");
		list  = p.list;
		while (list != NULL){
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
	return value_float (r);
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
gnumeric_small (Sheet *sheet, GList *expr_node_list, 
		int eval_col, int eval_row, char **error_string)
{
	stat_trimmean_t p;
	GSList          *list;
	int             n, count, k;
	float_t         r;

	p.first = TRUE;
	p.num   = 0;
	p.list  = NULL;

	function_iterate_argument_values (sheet, callback_function_trimmean,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	p.num--;
	k = ((int) p.last);

	if (p.num == 0 || k<=0 || k >= p.num){
		*error_string = _("#NUM!");
		list  = p.list;
		while (list != NULL){
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
	return value_float (r);
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
        float_t     x;
	gpointer    p;

	if (cell == NULL || cell->value == NULL)
	        return TRUE;

        switch (cell->value->type) {
	case VALUE_INTEGER:
	        x = cell->value->v.v_int;
		break;
	case VALUE_FLOAT:
	        x = cell->value->v.v_float;
		break;
	default:
	        return TRUE;
	}

	p = g_new(float_t, 1);
	*((float_t *) p) = x;
	mm->list = g_slist_append(mm->list, p);
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
	   "entries, PROB returns #N/A! error. "
	   "\n"
	   "@SEEALSO=BINOMDIST,CRITBINOM")
};

static Value *
gnumeric_prob (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
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
		        *error_string = _("#VALUE!");

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

			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}
	
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
		        *error_string = _("#VALUE!");

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

			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}

	if (items_x.num != items_prob.num) {
	        *error_string = _("#N/A!");

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

		return NULL;
	}

	lower_limit = value_get_as_double (argv[2]);
	if (argv[3] == NULL)
	        upper_limit = lower_limit;
	else
	        upper_limit = value_get_as_double (argv[3]);

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

	if (total_sum != 1) {
	        *error_string = _("#NUM!");
		return NULL;
	}

	return value_float (sum);
}

static char *help_ztest = {
	N_("@FUNCTION=ZTEST\n"
	   "@SYNTAX=ZTEST(ref,x)\n"

	   "@DESCRIPTION="
	   "ZTEST returns the two-tailed probability of a z-test. "
	   "\n"
	   "@ref is the data set and @x is the value to be tested. "
	   "\n"
	   "If ref contains less than two data items ZTEST "
	   "returns #DIV/0! error. "
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
			 char **error_string, void *closure)
{
	stat_ztest_t *mm = closure;
	float_t last;

	switch (value->type){
	case VALUE_INTEGER:
	        last = value->v.v_int;
		break;
	case VALUE_FLOAT:
	        last = value->v.v_float;
		break;
	default:
		return FALSE;
	}
	if (mm->num == 0)
	        mm->x = last;
	else {
	        mm->sum += mm->x;
		mm->sqrsum += mm->x * mm->x;
		mm->x = last;
	}
	mm->num++;
	return TRUE;
}

static Value *
gnumeric_ztest (Sheet *sheet, GList *expr_node_list,
		int eval_col, int eval_row, char **error_string)
{
	stat_ztest_t p;
	int          status;
	float_t      stdev;

	p.num    = 0;
	p.sum    = 0;
	p.sqrsum = 0;

	status = function_iterate_argument_values (sheet,
						   callback_function_ztest,
						   &p, expr_node_list,
						   eval_col, eval_row,
						   error_string);

	if (status == FALSE) {
	        *error_string = _("#VALUE!");
		return NULL;
	}
	p.num--;
	if (p.num < 2) {
	        *error_string = _("#DIV/0!");
		return NULL;
	}
	stdev = sqrt((p.sqrsum - p.sum*p.sum/p.num) / (p.num - 1));
	if (stdev == 0) {
	        *error_string = _("#DIV/0!");
		return NULL;
	}

	return value_float (1 - phi ((p.sum/p.num - p.x) /
				     (stdev / sqrt(p.num))));
}


FunctionDefinition stat_functions [] = {
        { "avedev",    0,      "",          &help_avedev,    gnumeric_avedev, NULL },
	{ "average", 0,      "",            &help_average, gnumeric_average, NULL },
	{ "betadist", "fff", "",            &help_betadist, NULL, gnumeric_betadist },
	{ "binomdist", "fffb", "n,t,p,c",   &help_binomdist, NULL, gnumeric_binomdist },
	{ "chidist",   "ff",  "",           &help_chidist, NULL, gnumeric_chidist },
	{ "chiinv",    "ff",  "",           &help_chiinv, NULL, gnumeric_chiinv },
	{ "confidence", "fff",  "x,stddev,size", &help_confidence, NULL, gnumeric_confidence },
	{ "count",     0,      "",          &help_count,   gnumeric_count, NULL },
	{ "counta",    0,      "",          &help_counta,    gnumeric_counta, NULL },
	{ "critbinom",  "fff",  "trials,p,alpha", &help_critbinom, NULL, gnumeric_critbinom },
        { "correl",     0,      "",         &help_correl,    gnumeric_correl, NULL },
        { "covar",      0,      "",         &help_covar,     gnumeric_covar, NULL },
        { "devsq",      0,      "",         &help_devsq,     gnumeric_devsq, NULL },
	{ "permut",    "ff",  "n,k",        &help_permut,    NULL, gnumeric_permut },
	{ "poisson",   "ffb",  "",          &help_poisson,   NULL, gnumeric_poisson },
	{ "expondist", "ffb",  "",          &help_expondist, NULL, gnumeric_expondist },
	{ "fdist",   "fff",  "",            &help_fdist,  NULL, gnumeric_fdist },
	{ "finv",   "fff",  "",             &help_finv,  NULL, gnumeric_finv },
        { "fisher",    "f",    "",          &help_fisher,    NULL, gnumeric_fisher },
        { "fisherinv", "f",    "",          &help_fisherinv, NULL, gnumeric_fisherinv },
	{ "gammaln",   "f",    "number",    &help_gammaln,   NULL, gnumeric_gammaln },
	{ "gammadist", "fffb", "number,alpha,beta,cum",    &help_gammadist,   NULL, gnumeric_gammadist },
	{ "gammainv", "fff",   "number,alpha,beta",        &help_gammainv,   NULL, gnumeric_gammainv },
	{ "geomean",   0,      "",          &help_geomean,   gnumeric_geomean, NULL },
	{ "harmean",   0,      "",          &help_harmean,   gnumeric_harmean, NULL },
	{ "hypgeomdist", "ffff", "x,n,M,N", &help_hypgeomdist, NULL, gnumeric_hypgeomdist },
        { "kurt",      0,      "",          &help_kurt,      gnumeric_kurt, NULL },
	{ "large",  0,      "",             &help_large,  gnumeric_large, NULL },
	{ "loginv",  "fff",  "",            &help_loginv, NULL, gnumeric_loginv },
	{ "lognormdist",  "fff",  "",       &help_lognormdist, NULL, gnumeric_lognormdist },
	{ "max",     0,      "",            &help_max,     gnumeric_max, NULL },
	{ "median",    0,      "",          &help_median,    gnumeric_median, NULL },
	{ "min",     0,      "",            &help_min,     gnumeric_min, NULL },
	{ "mode",      0,      "",          &help_mode,   gnumeric_mode, NULL },
	{ "negbinomdist", "fff", "f,t,p",   &help_negbinomdist, NULL, gnumeric_negbinomdist },
	{ "normdist",   "fffb",  "",        &help_normdist,  NULL, gnumeric_normdist },
	{ "norminv",    "fff",  "",         &help_norminv,  NULL, gnumeric_norminv },
	{ "normsdist",  "f",  "",           &help_normsdist,  NULL, gnumeric_normsdist },
	{ "normsinv",  "f",  "",            &help_normsinv,  NULL, gnumeric_normsinv },
	{ "pearson",   0,      "",          &help_pearson,   gnumeric_pearson, NULL },
	{ "prob", "AAf|f", "x_range,prob_range,lower_limit,upper_limit",
	  &help_prob,   NULL, gnumeric_prob },
	{ "rank",      0,      "",          &help_rank,      gnumeric_rank, NULL },
	{ "rsq",       0,      "",          &help_rsq,      gnumeric_rsq, NULL },
	{ "skew",      0,      "",          &help_skew,      gnumeric_skew, NULL },
	{ "small",  0,      "",             &help_small,  gnumeric_small, NULL },
	{ "standardize", "fff",  "x,mean,stddev", &help_standardize, NULL, gnumeric_standardize },
	{ "stdev",     0,      "",          &help_stdev,     gnumeric_stdev, NULL },
	{ "stdevp",    0,      "",          &help_stdevp,    gnumeric_stdevp, NULL },
	{ "tdist",   "fff",  "",            &help_tdist,  NULL, gnumeric_tdist },
	{ "tinv",    "ff",  "",             &help_tinv,  NULL, gnumeric_tinv },
	{ "trimmean",  0,      "",          &help_trimmean,  gnumeric_trimmean, NULL },
	{ "var",       0,      "",          &help_var,       gnumeric_var, NULL },
	{ "varp",      0,      "",          &help_varp,      gnumeric_varp, NULL },
        { "weibull", "fffb",  "",           &help_weibull, NULL, gnumeric_weibull },
	{ "ztest",  0,      "",             &help_ztest,  gnumeric_ztest, NULL },
	{ NULL, NULL },
};
