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
#include <rangefunc.h>
#include <value.h>
#include <auto-format.h>

#include <libgnome/gnome-i18n.h>
#include <expr.h>
#include <sheet.h>
#include <cell.h>
#include <collect.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static const char *help_rand = {
	N_("@FUNCTION=RAND\n"
	   "@SYNTAX=RAND()\n"

	   "@DESCRIPTION="
	   "RAND returns a random number between zero and one ([0..1]).\n\n"
	   "* This function is Excel compatible.\n"
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
	   "distribution from a to b. The distribution is,\n\n\t"
	   "p(x) dx = {1 over (b-a)} dx.\n\n"
	   "* If a <= x < b and 0 otherwise.\n"
           "* If @a > @b RANDUNIFORM returns #NUM! error.\n"
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

static const char *help_randdiscrete = {
	N_("@FUNCTION=RANDDISCRETE\n"
	   "@SYNTAX=RANDDISCRETE(val_range[,prob_range])\n"

	   "@DESCRIPTION="
	   "RANDDISCRETE returns one of the values in the @val_range. The "
	   "probablilites for each value are given in the @prob_range.\n"
	   "\n"
	   "* If @prob_range is omitted, the uniform discrete distribution "
	   "is assumed.\n"
           "* If the sum of all values in @prob_range is other than one, "
	   "RANDDISCRETE returns #NUM! error.\n"
           "* If @val_range and @prob_range are not the same size, "
	   "RANDDISCRETE returns #NUM! error.\n"
	   "* If @val_range or @prob_range is not a range, RANDDISCRETE "
	   "returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "RANDDISCRETE(A1:A6) returns one of the values in the range A1:A6.\n"
	   "\n"
	   "@SEEALSO=RANDBETWEEN,RAND")
};

typedef struct {
	gnum_float *prob;
	int        ind;
	gnum_float x;
	gnum_float cum;
	int        x_ind;
	Value      *res;
} randdiscrete_t;

static Value *
cb_randdiscrete (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	randdiscrete_t *p = (randdiscrete_t *) user_data;

	if (p->res != NULL)
		return NULL;

	if (p->prob) {
		if (p->x <= p->prob [p->ind] + p->cum)
			if (cell != NULL)
				p->res = value_duplicate (cell->value);
			else
				p->res = value_new_empty ();
		else
			p->cum += p->prob [p->ind];
	} else if (p->ind == p->x_ind) {
		if (cell != NULL)
			p->res = value_duplicate (cell->value);
		else
			p->res = value_new_empty ();
	}
	(p->ind)++;
		    
	return NULL;
}

static Value *
gnumeric_randdiscrete (FunctionEvalInfo *ei, Value **argv)
{
        Value          *value_range = argv[0];
	Value          *prob_range  = argv[1];
	Value          *ret;
	int            cols, rows;
	randdiscrete_t rd;

	rd.prob  = NULL;
	rd.ind   = 0;
	rd.cum   = 0;
	rd.res   = NULL;
	rd.x_ind = 0;

	if (value_range->type != VALUE_CELLRANGE ||
	    (prob_range != NULL && prob_range->type != VALUE_CELLRANGE))
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

	cols = value_range->v_range.cell.b.col
		- value_range->v_range.cell.a.col + 1;
	rows = value_range->v_range.cell.b.row
		- value_range->v_range.cell.a.row + 1;

	rd.x = random_01 ();

	if (prob_range) {
		int        n;
		gnum_float sum;

		if (prob_range->v_range.cell.b.col
		    - prob_range->v_range.cell.a.col + 1 != cols
		    || prob_range->v_range.cell.b.row
		    - prob_range->v_range.cell.a.row + 1 != rows)
			return value_new_error (ei->pos, gnumeric_err_NUM);

		rd.prob = collect_floats_value (prob_range, ei->pos, 0, &n,
						&ret);

		/* Check that the cumulative probability equals to one. */
		range_sum (rd.prob, n, &sum);
		if (sum != 1) {
			g_free (rd.prob);
			return value_new_error (ei->pos, gnumeric_err_NUM);
		}
	} else
		rd.x_ind = rd.x * cols * rows;

	ret = sheet_foreach_cell_in_range
		(eval_sheet (value_range->v_range.cell.a.sheet, ei->pos->sheet),
		 CELL_ITER_ALL,
		 value_range->v_range.cell.a.col,
		 value_range->v_range.cell.a.row,
		 value_range->v_range.cell.b.col,
		 value_range->v_range.cell.b.row,
		 cb_randdiscrete,
		 &rd);

	g_free (rd.prob);

	if (ret != NULL) {
		g_free (rd.res);
	        return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	return rd.res;
	
}

/***************************************************************************/

static const char *help_randexp = {
        N_("@FUNCTION=RANDEXP\n"
           "@SYNTAX=RANDEXP(b)\n"

           "@DESCRIPTION="
           "RANDEXP returns a exponentially-distributed random number.\n"
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

static const char *help_randpoisson = {
        N_("@FUNCTION=RANDPOISSON\n"
           "@SYNTAX=RANDPOISSON(lambda)\n"

           "@DESCRIPTION="
           "RANDPOISSON returns a poisson-distributed random number.\n"
           "\n"
           "* If @lambda < 0 RANDPOISSON returns #NUM! error.\n"
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

static const char *help_randbinom = {
        N_("@FUNCTION=RANDBINOM\n"
           "@SYNTAX=RANDBINOM(p,trials)\n"

           "@DESCRIPTION="
           "RANDBINOM returns a binomially-distributed random number.\n"
           "\n"
           "* If @p < 0 or @p > 1 RANDBINOM returns #NUM! error.\n"
           "* If @trials < 0 RANDBINOM returns #NUM! error. "
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

static const char *help_randbetween = {
	N_("@FUNCTION=RANDBETWEEN\n"
	   "@SYNTAX=RANDBETWEEN(bottom,top)\n"

	   "@DESCRIPTION="
	   "RANDBETWEEN function returns a random integer number "
	   "between and including @bottom and @top.\n"
           "\n"
	   "* If @bottom or @top is non-integer, they are truncated.\n"
	   "* If @bottom > @top, RANDBETWEEN returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
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
           "number.\n"
           "\n"
           "* If @p < 0 or @p > 1, RANDNEGBINOM returns #NUM! error.\n"
           "* If @failures RANDNEGBINOM returns #NUM! error.\n"
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

static const char *help_randbernoulli = {
        N_("@FUNCTION=RANDBERNOULLI\n"
           "@SYNTAX=RANDBERNOULLI(p)\n"

           "@DESCRIPTION="
           "RANDBERNOULLI returns a Bernoulli-distributed random number.\n"
           "\n"
           "* If @p < 0 or @p > 1 RANDBERNOULLI returns #NUM! error.\n"
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

static const char *help_randgaussian = {
        N_("@FUNCTION=RANDGAUSSIAN\n"
           "@SYNTAX=RANDGAUSSIAN(mean,stdev)\n"

           "@DESCRIPTION="
           "RANDGAUSSIAN returns a gaussian-distributed random number.\n"
           "\n"
           "* If @stdev < 0 RANDGAUSSIAN returns #NUM! error.\n"
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
	   "Lorentz distribution.\n"
           "\n"
           "* If @a < 0 RANDCAUCHY returns #NUM! error.\n"
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

static const char *help_randlognorm = {
        N_("@FUNCTION=RANDLOGNORM\n"
           "@SYNTAX=RANDLOGNORM(zeta,sigma)\n"

           "@DESCRIPTION="
           "RANDLOGNORM returns a lognormal-distributed random number.\n"
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
           "RANDWEIBULL returns a Weibull-distributed random number.\n"
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

static const char *help_randlaplace = {
        N_("@FUNCTION=RANDLAPLACE\n"
           "@SYNTAX=RANDLAPLACE(a)\n"

           "@DESCRIPTION="
           "RANDLAPLACE returns a Laplace-distributed random number. Laplace "
	   "distribution is also known as two-sided exponential probability "
	   "distribution.\n"
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

static const char *help_randrayleigh = {
        N_("@FUNCTION=RANDRAYLEIGH\n"
           "@SYNTAX=RANDRAYLEIGH(sigma)\n"

           "@DESCRIPTION="
           "RANDRAYLEIGH returns a Rayleigh-distributed random number.\n"
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

static const char *help_randrayleightail = {
        N_("@FUNCTION=RANDRAYLEIGHTAIL\n"
           "@SYNTAX=RANDRAYLEIGHTAIL(a,sigma)\n"

           "@DESCRIPTION="
           "RANDRAYLEIGHTAIL returns  a random variate from the tail of the "
	   "Rayleigh distribution with scale parameter sigma and a lower limit "
	   "of a. The distribution is,\n\n\t"
	   "p(x) dx = {x over sigma^2} exp ((a^2 - x^2) /(2 sigma^2)) dx,\n\n"
	   "for x > a.\n"
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
           "RANDGAMMA returns a Gamma-distributed random number.\n"
           "\n"
           "* If @a <= 0 RANDGAMMA returns #NUM! error.\n"
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

static const char *help_randpareto = {
        N_("@FUNCTION=RANDPARETO\n"
           "@SYNTAX=RANDPARETO(a,b)\n"

           "@DESCRIPTION="
           "RANDPARETO returns a Pareto-distributed random number.\n"
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
           "RANDFDIST returns a F-distributed random number.\n"
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
           "RANDBETA returns a Beta-distributed random number.\n"
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

static const char *help_randlogistic = {
        N_("@FUNCTION=RANDLOGISTIC\n"
           "@SYNTAX=RANDLOGISTIC(a)\n"

           "@DESCRIPTION="
           "RANDLOGISTIC returns a logistic-distributed random number.  The "
	   "distribution function is,\n\n\t"
	   "p(x) dx = { exp(-x/a) over a (1 + exp(-x/a))^2 } dx for "
	   "-infty < x < +infty.\n"
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
	   "variates is, \n\n\tp(k) =  p (1-p)^(k-1), for k >= 1.\n"
           "\n"
           "* If @p < 0 or @p > 1 RANDGEOM returns #NUM! error. "
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
	   "is,\n\n\t"
	   "p(k) =  C(n_1,k) C(n_2, t-k) / C(n_1 + n_2,k), \n\nwhere C(a,b) "
	   "= a!/(b!(a-b)!). \n\n"
	   "The domain of k is max(0,t-n_2), ..., max(t,n_1)."
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
           "RANDLOG returns a logarithmic-distributed random number.\n"
           "\n"
           "* If @p < 0 or @p > 1 RANDLOG returns #NUM! error.\n"
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
           "RANDCHISQ returns a Chi-Square-distributed random number.\n"
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
           "RANDTDIST returns a T-distributed random number.\n"
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
	   "the distribution (Type I or Type II).\n"
           "\n"
	   "* If @type is either 1 or 2, RANDGUMBEL returns #NUM! error.\n"
	   "* If @type is omitted, Type I is assumed.\n"
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
	   "\n"
	   "* For alpha = 1, beta=0, we get the Lorentz distribution.\n"
	   "* For alpha = 2, beta=0, we get the Gaussian distribution.\n"
           "\n"
	   "* If @alpha <= 0 or @alpha > 2, RANDLEVY returns #NUM! error.\n"
	   "* If @beta < -1 or @beta > 1, RANDLEVY returns #NUM! error.\n"
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
           "RANDEXPPOW returns a random variate from the exponential power "
	   "distribution with scale parameter @a and exponent @b. The "
	   "distribution is,\n\n\t"
	   "p(x) dx = {1 over 2 a Gamma(1+1/b)} exp(-|x/a|^b) dx, "
	   "for x >= 0.\n\n"
	   "* For b = 1 this reduces to the Laplace distribution.\n"
	   "* For b = 2 it has the same form as a gaussian distribution, "
	   "but with a = sqrt{2} sigma.\n"
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

static const char *help_randlandau = {
        N_("@FUNCTION=RANDLANDAU\n"
           "@SYNTAX=RANDLANDAU()\n"

           "@DESCRIPTION="
           "RANDLANDAU returns a random variate from the Landau distribution. "
	   "The probability distribution for Landau random variates is "
	   "defined analytically by the complex integral,\n\n\t"
	   "p(x) = (1/(2 pi i)) int_{c-i infty}^{c+i infty} ds "
	   "exp(s log(s) + x s).\n\n"
	   "For numerical purposes it is more convenient to use the "
	   "following equivalent form of the integral,\n\n\t"
	   "p(x) = (1/pi) int_0^ infty dt exp(-t log(t) - x t) sin(pi t).\n"
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
	   "586 (exercise 11).\n"
	   "\n"
	   "The probability distribution for Gaussian tail random variates "
	   "is,\n\n\t"
	   "p(x) dx = {1 over N(a;sigma)} exp (- x^2/(2 sigma^2)) dx,\n\n"
	   "for x > a where N(a;sigma) is the normalization constant, "
	   "N(a;sigma) = (1/2) erfc(a / sqrt(2 sigma^2)).\n"
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

static const char *help_simtable = {
        N_("@FUNCTION=SIMTABLE\n"
           "@SYNTAX=SIMTABLE(d1, d2, ..., dN)\n"

           "@DESCRIPTION="
           "SIMTABLE returns one of the values in the given argument list "
	   "depending on the round number of the simulation tool. When the "
	   "simulation tool is not activated, SIMTABLE returns @d1.\n"
	   "\n"
	   "With the simulation tool and the SIMTABLE function you can "
	   "test given decision variables. Each SIMTABLE function contains "
	   "the possible values of a simulation variable. In most of "
	   "valid simulation models you should have the same number of "
	   "values @dN for all decision variables.  If the simulation is run "
	   "more rounds than there are values defined, SIMTABLE returns "
	   "#N/A! error (e.g. if A1 contains `=SIMTABLE(1)' and A2 "
	   "`=SIMTABLE(1,2)', A1 yields #N/A! error on the second round).\n"
	   "\n"
	   "The successive use of the simulation tool also requires that you "
	   "give to the tool at least one input variable having RAND() or "
	   "any other RAND<distribution name>() function in it. "
	   "On each round, the simulation tool iterates given number of "
	   "times all the input variables to revalue them. On each iteration, "
	   "the values of the of the output variables are stored, and when "
	   "the round is completed, descriptive statistical information is "
	   "created according to the values.\n"
	   "\n"
           "@EXAMPLES=\n"
	   "SIMTABLE(TRUE,FALSE) returns TRUE on the first simulation round "
	   "and FALSE on the second round.\n"
           "SIMTABLE(223,225,227,229) returns 227 on the simulation round "
           "#3.\n"
           "\n"
           "@SEEALSO=")
};

typedef struct {
	int   index;
	Value *value;
} simtable_t;

static Value *
callback_function_simtable (const EvalPos *ep, Value *value, void *closure)
{
	simtable_t *p = closure;

	if (p->index == ep->sheet->simulation_round)
		p->value = value_duplicate (value);
	++(p->index);

	return NULL;
}

static Value *
gnumeric_simtable (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	simtable_t p;

	p.index = 0;
	p.value = NULL;

	function_iterate_argument_values
		(ei->pos, callback_function_simtable, &p, nodes,
		 FALSE, CELL_ITER_IGNORE_BLANK);

	/* See if there was any value worth using. */
	if (p.value == NULL)
		return value_new_error (ei->pos, gnumeric_err_NA);

	return p.value;
}

/***************************************************************************/

const ModulePluginFunctionInfo random_functions[] = {
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
 	{ "randdiscrete", "r|r", N_("value_range,prob_range"),
	  &help_randdiscrete, gnumeric_randdiscrete, NULL, NULL, NULL },
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
        { "randlandau", "", "", &help_randlandau,
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
	{ "simtable", 0, N_("d1[,d2,...,dN]"), &help_simtable, NULL,
	  gnumeric_simtable, NULL, NULL },
        {NULL}
};
