/*
 * Number Theory Plugin
 *
 * Author:
 *    Marko R. Riedel (mriedel@neuearbeit.de)    [Functions]
 *    Morten Welinder (terra@gnome.org)          [Plugin framework]
 *    Brian J. Murrell (brian@interlinx.bc.ca)	 [Bitwise operators]
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
#include "func.h"
#include "value.h"
#include <gnm-i18n.h>
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>
#include <limits.h>

GNM_PLUGIN_MODULE_HEADER;

#define OUT_OF_BOUNDS "#LIMIT!"

static const double bit_max = MIN (1 / GNM_EPSILON, (gnm_float)G_MAXUINT64);

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
#define ITHPRIME_LIMIT (1 << 20)
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
isprime (guint64 n)
{
	int i = 1, p = 2;

	if (n <= 1)
		return 0;

	for (i = 1; (guint64)p * (guint64)p <= n; i++) {
		if (ithprime (i, &p))
			return -1;
		if (n % p == 0)
			return 0;
	}

	return 1;
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_phi[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NT_PHI\n"
	   "@SYNTAX=NT_PHI(n)\n"
	   "@DESCRIPTION="
	   "NT_PHI function calculates the number of integers less "
	   "than or equal to @n that are relatively prime to @n.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, ITHPRIME, NT_SIGMA")
    },
    { GNM_FUNC_HELP_END }
};

static void
walk_for_phi (int p, int v, void *data)
{
	*((int *)data) *= intpow (p, v - 1) * (p - 1);
}

static GnmValue *
gnumeric_phi (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int phi = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > INT_MAX)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((int)n, &phi, walk_for_phi))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (phi);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_nt_mu[] = {
    { GNM_FUNC_HELP_OLD,
	/* xgettext: you can translate the funny character as an 'o' if unicode is not available. */
	F_("@FUNCTION=NT_MU\n"
	   "@SYNTAX=NT_MU(n)\n"
	   "@DESCRIPTION="
	   "NT_MU function (Möbius mu function) returns \n"
	   "0  if @n is divisible by the square of a prime .\n"
	   "Otherwise it returns: \n\n"
	   "  -1 if @n has an odd  number of different prime factors .\n"
	   "   1  if @n has an even number of different prime factors .\n\n"
	   "* If @n = 1 NT_MU returns 1.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, ITHPRIME, NT_PHI")
    },
    { GNM_FUNC_HELP_END }
};

static void
walk_for_mu (int p, int v, void *data)
{
	*((int *)data) = (v >= 2) ?  0 : - *((int *)data) ;
}

static GnmValue *
gnumeric_nt_mu (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int mu = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > INT_MAX)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((int)n, &mu, walk_for_mu))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (mu);
}


/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_d[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NT_D\n"
	   "@SYNTAX=NT_D(n)\n"
	   "@DESCRIPTION="
	   "NT_D function calculates the number of divisors of @n.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=ITHPRIME, NT_PHI, NT_SIGMA")
    },
    { GNM_FUNC_HELP_END }
};

static void
walk_for_d (int p, int v, void *data)
{
	* (int *) data *= (v + 1);
}

static GnmValue *
gnumeric_d (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int d = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > INT_MAX)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((int)n, &d, walk_for_d))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (d);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_sigma[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NT_SIGMA\n"
	   "@SYNTAX=NT_SIGMA(n)\n"
	   "@DESCRIPTION="
	   "NT_SIGMA function calculates the sum of the divisors of @n.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, ITHPRIME, NT_PHI")
    },
    { GNM_FUNC_HELP_END }
};

static void
walk_for_sigma (int p, int v, void *data)
{
	* (int *) data *=
		  ( v == 1 ? p + 1 : (intpow (p, v + 1) - 1) / (p - 1) );
}

static GnmValue *
gnumeric_sigma (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int sigma = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > INT_MAX)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((int)n, &sigma, walk_for_sigma))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (sigma);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_ithprime[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ITHPRIME\n"
	   "@SYNTAX=ITHPRIME(i)\n"
	   "@DESCRIPTION="
	   "ITHPRIME function returns the @ith prime.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, NT_SIGMA")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ithprime (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int p;
	gnm_float i = gnm_floor (value_get_as_float (args[0]));

	if (i < 1 || i > INT_MAX)
		return value_new_error_NUM (ei->pos);

	if (ithprime ((int)i, &p))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (p);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_isprime[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISPRIME\n"
	   "@SYNTAX=ISPRIME(i)\n"
	   "@DESCRIPTION="
	   "ISPRIME function returns TRUE if @i is prime and FALSE otherwise.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=ITHPRIME, NT_D, NT_SIGMA")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isprime (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int yesno;
	gnm_float i = gnm_floor (value_get_as_float (args[0]));

	if (i < 0)
		yesno = 0;
	else if (i > bit_max)
		yesno = -1;
	else
		yesno = isprime ((guint64)i);

	if (yesno == -1)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);
	else
		return value_new_bool (yesno);
}

/* ------------------------------------------------------------------------- */

/*
 * Returns
 *   -1 (out of bounds)
 *    0 (n <= 1)
 *    smallest prime facter
 */
static int
prime_factor (guint64 n)
{
	int i = 1, p = 2;

	if (n <= 1)
		return 0;

	for (i = 1; (guint64)p * (guint64)p <= n; i++) {
		if (ithprime (i, &p))
			return -1;
		if (n % p == 0)
			return p;
	}

	return n;
}

static GnmFuncHelp const help_pfactor[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PFACTOR\n"
	   "@SYNTAX=PFACTOR(n)\n"
	   "@DESCRIPTION="
	   "PFACTOR function returns the smallest prime factor of its argument.\n"
	   "\n"
	   "The argument must be at least 2, or else a #VALUE! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=ITHPRIME")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pfactor (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	gnm_float n = gnm_floor (value_get_as_float (args[0]));
	int p;

	if (n < 2)
		return value_new_error_VALUE (ei->pos);
	if (n > bit_max)
		p = -1;
	else
		p = prime_factor ((guint64)n);

	if (p < 0)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (p);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_nt_pi[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NT_PI\n"
	   "@SYNTAX=NT_PI(n)\n"
	   "@DESCRIPTION="
	   "NT_PI function returns the number of primes less than or equal "
	   "to @n.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=ITHPRIME, NT_PHI, NT_D, NT_SIGMA")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_nt_pi (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	gnm_float n = gnm_floor (value_get_as_float (args[0]));
	int pi;

	if (n < 0)
		pi = 0;
	else if (n > INT_MAX)
		pi = -1;
	else
		pi = compute_nt_pi (n);

	if (pi == -1)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);
	else
		return value_new_int (pi);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitor[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=BITOR\n"
	   "@SYNTAX=BITOR(a,b)\n"
	   "@DESCRIPTION="
	   "BITOR function returns bitwise or-ing of its arguments.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITXOR,BITAND")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
func_bitor (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float l = value_get_as_float (argv[0]);
	gnm_float r = value_get_as_float (argv[1]);

	if (l < 0 || l > bit_max || r < 0 || r > bit_max)
		return value_new_error_NUM (ei->pos);

        return value_new_float ((guint64)l | (guint64)r);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitxor[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=BITXOR\n"
	   "@SYNTAX=BITXOR(a,b)\n"
	   "@DESCRIPTION="
	   "BITXOR function returns bitwise exclusive or-ing of its "
	   "arguments.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITOR,BITAND")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
func_bitxor (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float l = value_get_as_float (argv[0]);
	gnm_float r = value_get_as_float (argv[1]);

	if (l < 0 || l > bit_max || r < 0 || r > bit_max)
		return value_new_error_NUM (ei->pos);

        return value_new_float ((guint64)l ^ (guint64)r);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitand[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=BITAND\n"
	   "@SYNTAX=BITAND(a,b)\n"
	   "@DESCRIPTION="
	   "BITAND function returns bitwise and-ing of its arguments.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITOR,BITXOR")
    },
    { GNM_FUNC_HELP_END }
};


static GnmValue *
func_bitand (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float l = value_get_as_float (argv[0]);
	gnm_float r = value_get_as_float (argv[1]);

	if (l < 0 || l > bit_max || r < 0 || r > bit_max)
		return value_new_error_NUM (ei->pos);

        return value_new_float ((guint64)l & (guint64)r);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitlshift[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=BITLSHIFT\n"
	   "@SYNTAX=BITLSHIFT(x,n)\n"
	   "@DESCRIPTION="
	   "BITLSHIFT function returns @x bit-shifted left by @n bits.\n\n"
	   "* If @n is negative, a right shift will in effect be performed.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITRSHIFT")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
func_bitlshift (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float l = value_get_as_float (argv[0]);
	gnm_float r = gnm_floor (value_get_as_float (argv[1]));

	if (l < 0 || l > bit_max)
		return value_new_error_NUM (ei->pos);

	if (r >= 64 || r <= -64)
		return value_new_int (0);  /* All bits shifted away.  */
	else if (r < 0)
		return value_new_float ((guint64)l >> (-(int)r));
	else
		return value_new_float ((guint64)l << (int)r);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitrshift[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=BITRSHIFT\n"
	   "@SYNTAX=BITRSHIFT(x,n)\n"
	   "@DESCRIPTION="
	   "BITRSHIFT function returns @x bit-shifted right by @n bits.\n\n"
	   "* If @n is negative, a left shift will in effect be performed.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=BITLSHIFT")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
func_bitrshift (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float l = value_get_as_float (argv[0]);
	gnm_float r = gnm_floor (value_get_as_float (argv[1]));

	if (l < 0 || l > bit_max)
		return value_new_error_NUM (ei->pos);

	if (r >= 64 || r <= -64)
		return value_new_int (0);  /* All bits shifted away.  */
	else if (r < 0)
		return value_new_float ((guint64)l << (-(int)r));
	else
		return value_new_float ((guint64)l >> (int)r);
}

/* ------------------------------------------------------------------------- */

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	g_free (prime_table);
	prime_table = NULL;
}

const GnmFuncDescriptor num_theory_functions[] = {
	{"ithprime", "f", "number", help_ithprime,
	 &gnumeric_ithprime, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"pfactor", "f", "number", help_pfactor,
	 &gnumeric_pfactor, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_phi",   "f", "number", help_phi,
	 &gnumeric_phi,      NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_d",     "f", "number", help_d,
	 &gnumeric_d,        NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_sigma", "f", "number", help_sigma,
	 &gnumeric_sigma,    NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"isprime",  "f", "number", help_isprime,
	 &gnumeric_isprime,  NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_pi",    "f", "number", help_nt_pi,
	 &gnumeric_nt_pi,    NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_mu",    "f", "number", help_nt_mu,
	 &gnumeric_nt_mu,    NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{NULL}
};

const GnmFuncDescriptor bitwise_functions[] = {
	{"bitor",     "ff", "A,B", help_bitor,
	 &func_bitor,     NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitxor",    "ff", "A,B", help_bitxor,
	 &func_bitxor,    NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitand",    "ff", "A,B", help_bitand,
	 &func_bitand,    NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitlshift", "ff", "X,N", help_bitlshift,
	 &func_bitlshift, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitrshift", "ff", "N,N", help_bitrshift,
	 &func_bitrshift, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{NULL}
};
