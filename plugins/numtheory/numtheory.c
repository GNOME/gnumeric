/*
 * Number Theory Plugin
 *
 * Author:
 *    Marko R. Riedel (mriedel@neuearbeit.de)    [Functions]
 *    Morten Welinder (terra@diku.dk)            [Plugin framework]
 *    Brian J. Murrell (brian@interlinx.bc.ca)	 [Bitwise operators]
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnome.h>
#include <glib.h>

#include "func.h"
#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "value.h"
#include <limits.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

#ifndef WORD_BIT
#define WORD_BIT ((int)(sizeof (int) * CHAR_BIT))
#endif

#define OUT_OF_BOUNDS "#LIMIT!"


/* ------------------------------------------------------------------------- */

static int
intpow (int p, int v)
{
	int temp;

	if (v == 0) return 1;
	if (v == 1) return p;

	temp = intpow (p, v / 2);
	temp *= temp;
	return (v % 2) ? temp * p : temp;
}

#define PTABLE_CHUNK 64
#define ITHPRIME_LIMIT 1000000
static gint *prime_table = NULL;

/* Calculate the i-th prime.  Returns TRUE on error.  */
static gboolean
ithprime (int i, int *res)
{
	static int computed = 0;
	static int allocated = 0;

	if (i < 1 || i > ITHPRIME_LIMIT)
		return TRUE;

	if (i > computed) {
		int candidate;

		if (i > allocated) {
			g_assert (PTABLE_CHUNK >= 2);
			allocated = MAX (i, allocated + PTABLE_CHUNK);
			prime_table = g_renew (int, prime_table, allocated);
			if (computed == 0) {
				prime_table[computed++] = 2;
				prime_table[computed++] = 3;
			}
		}

		candidate = prime_table[computed - 1];
		/*
		 * Note, that the candidate is odd since we filled in the first
		 * two prime numbers.
		 */
		while (i > computed) {
			gboolean prime = TRUE;
			int j;
			candidate += 2;  /* Skip even candidates.  */

			for (j = 1; prime_table[j] * prime_table[j] <= candidate; j++)
				if (candidate % prime_table[j] == 0) {
					prime = FALSE;
					break;
				}

			if (prime)
				prime_table[computed++] = candidate;
		}
	}

	*res = prime_table[i - 1];
	return FALSE;
}

/*
 * A function useful for computing multiplicative aritmethic functions.
 * Returns TRUE on error.
 */
static gboolean
walk_factorization (int n, void *data,
		    void (*walk_term) (int p, int v, void *data))
{
	int index = 1, p = 2, v;

	while (n > 1 && p * p <= n) {
		if (ithprime (index, &p))
			return TRUE;

		v = 0;
		while (n % p == 0) {
			v++;
			n /= p;
		}

		if (v) {
			/* We found a prime factor, p, with arity v.  */
			walk_term (p, v, data);
		}

		index++;
	}

	if (n > 1) {
		/*
		 * A number, n, with no factors from 2 to sqrt (n) is a
		 * prime number.  The arity is 1.
		 */
		walk_term (n, 1, data);
	}

	return FALSE;
}

/*
 * Returns -1 (out of bounds), or #primes <= n
 */
static int
compute_nt_pi (int n)
{
	int lower = 2, upper = 4, mid, p = 7;

	if (n <= 1)
		return 0;

	if (n < 4)
		return n - 1;

	while (p < n) {
		lower = upper;
		upper *= 2;
		if (ithprime (upper, &p))
			return -1;
	}

	while (upper - lower > 1) {
		mid = (lower + upper) / 2;
		ithprime (mid, &p);

		if (p < n)
			lower = mid;
		else if (p > n)
			upper = mid;
		else
			return mid;
	}

	ithprime (upper, &p);
	return (p == n) ? lower + 1 : lower;
}


/*
 * Returns -1 (out of bounds), 0 (non-prime), or 1 (prime).
 */
static int
isprime (int n)
{
	int i = 1, p = 2;

	if (n <= 1)
		return 0;

	for (i = 1; p * p <= n; i++) {
		if (ithprime (i, &p))
			return -1;
		if (n % p == 0)
			return 0;
	}

	return 1;
}

/* ------------------------------------------------------------------------- */

static char *help_phi = {
	N_("@FUNCTION=NT_PHI\n"
	   "@SYNTAX=NT_PHI(n)\n"
	   "@DESCRIPTION="
	   "The NT_PHI function calculates the number of integers less "
	   "than or equal to @n that are relatively prime to @n.\n"
	   "This function only takes one argument."
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, ITHPRIME, NT_SIGMA")
};

static void
walk_for_phi (int p, int v, void *data)
{
	*((int *)data) *= intpow (p, v - 1) * (p - 1);
}

static Value *
gnumeric_phi (FunctionEvalInfo *ei, Value **args)
{
	int n, phi = 1;

	n = value_get_as_int (args [0]);
	if (n < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (walk_factorization (n, &phi, walk_for_phi))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (phi);
}

/* ------------------------------------------------------------------------- */

static char *help_nt_mu = {
	N_("@FUNCTION=NT_MU\n"
	   "@SYNTAX=NT_MU(n)\n"
	   "@DESCRIPTION="
	   "The NT_MU function (Möbius mu function) returns \n"
	   "0  if @n is divisible by the square of a prime .\n"
	   "Otherwise it returns: \n"
	   "-1 if @n has an odd  number of different prime factors .\n"
	   "1  if @n has an even number of different prime factors .\n"
	   "If @n=1 it returns 1\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, ITHPRIME, NT_PHI")
};

static void
walk_for_mu (int p, int v, void *data)
{
	*((int *)data) = (v >= 2) ?  0 : - *((int *)data) ;
}

static Value *
gnumeric_nt_mu (FunctionEvalInfo *ei, Value **args)
{
	int n, mu = 1;

	n = value_get_as_int (args [0]);
	if (n < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (walk_factorization (n, &mu, walk_for_mu))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (mu);
}


/* ------------------------------------------------------------------------- */

static char *help_d = {
	N_("@FUNCTION=NT_D\n"
	   "@SYNTAX=NT_D(n)\n"
	   "@DESCRIPTION="
	   "The NT_D function calculates the number of divisors of @n.\n"
	   "This function only takes one argument."
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=ITHPRIME, NT_PHI, NT_SIGMA")
};

static void
walk_for_d (int p, int v, void *data)
{
	* (int *) data *= (v + 1);
}

static Value *
gnumeric_d (FunctionEvalInfo *ei, Value **args)
{
	int n, d = 1;

	n = value_get_as_int (args [0]);
	if (n < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (walk_factorization (n, &d, walk_for_d))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (d);
}

/* ------------------------------------------------------------------------- */

static char *help_sigma = {
	N_("@FUNCTION=NT_SIGMA\n"
	   "@SYNTAX=NT_SIGMA(n)\n"
	   "@DESCRIPTION="
	   "The NT_SIGMA function calculates the sum of the divisors of @n.\n"
	   "This function only takes one argument."
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, ITHPRIME, NT_PHI")
};

static void
walk_for_sigma (int p, int v, void *data)
{
	* (int *) data *=
		  ( v == 1 ? p + 1 : (intpow (p, v + 1) - 1) / (p - 1) );
}

static Value *
gnumeric_sigma (FunctionEvalInfo *ei, Value **args)
{
	int n, sigma = 1;

	n = value_get_as_int (args [0]);
	if (n < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (walk_factorization (n, &sigma, walk_for_sigma))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (sigma);
}

/* ------------------------------------------------------------------------- */

static char *help_ithprime = {
	N_("@FUNCTION=ITHPRIME\n"
	   "@SYNTAX=ITHPRIME(i)\n"
	   "@DESCRIPTION="
	   "The ITHPRIME function returns the @ith prime.\n"
	   "This function only takes one argument."
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, NT_SIGMA")
};

static Value *
gnumeric_ithprime (FunctionEvalInfo *ei, Value **args)
{
	int i, p;

	i = value_get_as_int (args [0]);
	if (i < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (ithprime (i, &p))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (p);
}

/* ------------------------------------------------------------------------- */

static char *help_isprime = {
	N_("@FUNCTION=ISPRIME\n"
	   "@SYNTAX=ISPRIME(i)\n"
	   "@DESCRIPTION="
	   "The ISPRIME function returns TRUE if @i is prime "
	   "and FALSE otherwise.\n"
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=ITHPRIME, NT_D, NT_SIGMA")
};

static Value *
gnumeric_isprime (FunctionEvalInfo *ei, Value **args)
{
	int i, yesno;

	i = value_get_as_int (args [0]);
	yesno = isprime (i);
	if (yesno == -1)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);
	else
		return value_new_bool (yesno);
}

/* ------------------------------------------------------------------------- */

static char *help_nt_pi = {
	N_("@FUNCTION=NT_PI\n"
	   "@SYNTAX=NT_PI(n)\n"
	   "@DESCRIPTION="
	   "The NT_PI function returns the number of primes "
	   "less than or equal to @n.\n"
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=ITHPRIME, NT_PHI, NT_D, NT_SIGMA")
};

static Value *
gnumeric_nt_pi (FunctionEvalInfo *ei, Value **args)
{
	int n, pi;

	n = value_get_as_int (args [0]);
	pi = compute_nt_pi (n);

	if (pi == -1)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);
	else
		return value_new_int (pi);
}

/* ------------------------------------------------------------------------- */

static char *help_bitor = {
	N_("@FUNCTION=BITOR\n"
	   "@SYNTAX=BITOR(a,b)\n"
	   "@DESCRIPTION="
	   "The BITOR function returns bitwise or-ing of its arguments.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITXOR,BITAND")
};

static Value *
func_bitor (FunctionEvalInfo *ei, Value *argv [])
{
        return value_new_int (value_get_as_int (argv [0]) |
			      value_get_as_int (argv [1]));
}

/* ------------------------------------------------------------------------- */

static char *help_bitxor = {
	N_("@FUNCTION=BITXOR\n"
	   "@SYNTAX=BITXOR(a,b)\n"
	   "@DESCRIPTION="
	   "The BITXOR function returns bitwise exclusive or-ing of its arguments.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITOR,BITAND")
};

static Value *
func_bitxor (FunctionEvalInfo *ei, Value *argv [])
{
        return value_new_int (value_get_as_int (argv [0]) ^
			      value_get_as_int (argv [1]));
}

/* ------------------------------------------------------------------------- */

static char *help_bitand = {
	N_("@FUNCTION=BITAND\n"
	   "@SYNTAX=BITAND(a,b)\n"
	   "@DESCRIPTION="
	   "The BITAND function returns bitwise and-ing of its arguments.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITOR,BITXOR")
};


static Value *
func_bitand (FunctionEvalInfo *ei, Value *argv [])
{
        return value_new_int (value_get_as_int (argv [0]) &
			      value_get_as_int (argv [1]));
}

/* ------------------------------------------------------------------------- */

static char *help_bitlshift = {
	N_("@FUNCTION=BITLSHIFT\n"
	   "@SYNTAX=BITLSHIFT(x,n)\n"
	   "@DESCRIPTION="
	   "The BITLSHIFT function returns @x bit-shifted left by @n bits.\n"
	   "If @n is negative, a right shift will in effect be performed.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITRSHIFT")
};

static Value *
func_bitlshift (FunctionEvalInfo *ei, Value *argv [])
{
	int l = value_get_as_int (argv [0]);
	int r = value_get_as_int (argv [1]);

	if (r >= WORD_BIT || r <= -WORD_BIT)
		return value_new_int (0);  /* All bits shifted away.  */
	else if (r < 0)
		return value_new_int (l >> (-r));
	else
		return value_new_int (l << r);
}

/* ------------------------------------------------------------------------- */

static char *help_bitrshift = {
	N_("@FUNCTION=BITRSHIFT\n"
	   "@SYNTAX=BITRSHIFT(x,n)\n"
	   "@DESCRIPTION="
	   "The BITRSHIFT function returns @x bit-shifted right by @n bits.\n"
	   "If @n is negative, a left shift will in effect be performed.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITRSHIFT")
};

static Value *
func_bitrshift (FunctionEvalInfo *ei, Value *argv [])
{
	int l = value_get_as_int (argv [0]);
	int r = value_get_as_int (argv [1]);

	if (r >= WORD_BIT || r <= -WORD_BIT)
		return value_new_int (0);  /* All bits shifted away.  */
	else if (r < 0)
		return value_new_int (l << (-r));
	else
		return value_new_int (l >> r);
}

/* ------------------------------------------------------------------------- */

void
plugin_cleanup (void)
{
	g_free (prime_table);
	prime_table = NULL;
}

ModulePluginFunctionInfo num_theory_functions[] = {
	{"ithprime", "f", "number", &help_ithprime, &gnumeric_ithprime, NULL},
	{"nt_phi",   "f", "number", &help_phi,      &gnumeric_phi,      NULL},
	{"nt_d",     "f", "number", &help_d,        &gnumeric_d,        NULL},
	{"nt_sigma", "f", "number", &help_sigma,    &gnumeric_sigma,    NULL},
	{"isprime",  "f", "number", &help_isprime,  &gnumeric_isprime,  NULL},
	{"nt_pi",    "f", "number", &help_nt_pi,    &gnumeric_nt_pi,    NULL},
	{"nt_mu",    "f", "number", &help_nt_mu,    &gnumeric_nt_mu,    NULL},
	{NULL}
};

ModulePluginFunctionInfo bitwise_functions[] = {
	{"bitor",     "ff", "A,B", &help_bitor,     &func_bitor,     NULL},
	{"bitxor",    "ff", "A,B", &help_bitxor,    &func_bitxor,    NULL},
	{"bitand",    "ff", "A,B", &help_bitand,    &func_bitand,    NULL},
	{"bitlshift", "ff", "X,N", &help_bitlshift, &func_bitlshift, NULL},
	{"bitrshift", "ff", "N,N", &help_bitrshift, &func_bitrshift, NULL},
	{NULL}
};
