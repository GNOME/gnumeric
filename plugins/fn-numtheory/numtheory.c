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
#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <limits.h>

GNM_PLUGIN_MODULE_HEADER;

#define OUT_OF_BOUNDS "#LIMIT!"

/*
 * The largest integer i, such at all integers {0,...,i} can be accurately
 * represented in a gnm_float _and_ in a guint64.  (For regular "double",
 * the latter part is irrelevant.)
 */
static const double bit_max = MIN (1 / GNM_EPSILON, (gnm_float)G_MAXUINT64);

/* ------------------------------------------------------------------------- */

static GnmValue *
value_new_guint64 (guint64 n)
{
	return value_new_float (n);
}


static guint64
intpow (int p, int v)
{
	guint64 temp;

	if (v == 0) return 1;
	if (v == 1) return p;

	temp = intpow (p, v / 2);
	temp *= temp;
	return (v % 2) ? temp * p : temp;
}

#define ITHPRIME_LIMIT 10000000
static guint *prime_table = NULL;

/* Calculate the i-th prime.  Returns TRUE on error.  */
static gboolean
ithprime (int i, guint64 *res)
{
	static guint computed = 0;
	static guint allocated = 0;

	if (i < 1 || (guint)i > ITHPRIME_LIMIT)
		return TRUE;

	if ((guint)i > computed) {
		static guint candidate = 3;
		static guint jlim = 1;

		if ((guint)i > allocated) {
			allocated = MAX ((guint)i, 2 * allocated + 100);
			allocated = MIN (allocated, ITHPRIME_LIMIT);
			prime_table = g_renew (guint, prime_table, allocated);
			if (computed == 0) {
				prime_table[computed++] = 2;
				prime_table[computed++] = 3;
			}
		}

		while ((guint)i > computed) {
			gboolean prime = TRUE;
			guint j;

			candidate += 2;  /* Skip even candidates.  */

			while (candidate >= prime_table[jlim] * prime_table[jlim])
				jlim++;

			for (j = 1; j < jlim; j++) {
				if (candidate % prime_table[j] == 0) {
					prime = FALSE;
					break;
				}
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
walk_factorization (guint64 n, void *data,
		    void (*walk_term) (guint64 p, int v, void *data))
{
	int index = 1, v;
	guint64 p = 2;

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
static gint64
compute_nt_pi (guint64 n)
{
	guint64 lower = 2, upper = 4, mid, p = 7;

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
	int i = 1;
	guint64 p = 2;

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

static GnmFuncHelp const help_nt_omega[] = {
 	{ GNM_FUNC_HELP_NAME, F_("NT_OMEGA:Number of distinct prime factors")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_NOTE, F_("Returns the number of distinct prime factors without multiplicity.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_PHI(9)" },
	{ GNM_FUNC_HELP_SEEALSO, "NT_D,ITHPRIME,NT_SIGMA"},
	{ GNM_FUNC_HELP_END }
};

static void
walk_for_omega (guint64 p, int v, void *data_)
{
	int *data = data_;
	(*data)++;
}

static GnmValue *
gnumeric_nt_omega (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int omega = 0;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > bit_max)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((guint64)n, &omega, walk_for_omega))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (omega);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_phi[] = {
 	{ GNM_FUNC_HELP_NAME, F_("NT_PHI:Euler's totient function")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_NOTE, F_("Euler's totient function gives the number of integers less than or equal to @{n} that are relatively prime (coprime) to @{n}.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_PHI(9)" },
	{ GNM_FUNC_HELP_SEEALSO, "NT_D,ITHPRIME,NT_SIGMA"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Euler's_totient_function") },
	{ GNM_FUNC_HELP_END }
};

static void
walk_for_phi (guint64 p, int v, void *data_)
{
	guint64 *data = data_;
	*data *= intpow (p, v - 1) * (p - 1);
}

static GnmValue *
gnumeric_phi (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	guint64 phi = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > bit_max)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((guint64)n, &phi, walk_for_phi))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_guint64 (phi);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_nt_mu[] = {
 	{ GNM_FUNC_HELP_NAME, F_("NT_MU:Möbius mu function")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("NT_MU function (Möbius mu function) returns 0  if @{n} is "
	     "divisible by the square of a prime. Otherwise, if @{n} has"
	     " an odd  number of different prime factors, NT_MU returns "
	     "-1, and if @{n} has an even number of different prime factors,"
	     " it returns 1. If @{n} = 1, NT_MU returns 1.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_MU(45)" },
	{ GNM_FUNC_HELP_SEEALSO, "ITHPRIME,NT_PHI,NT_SIGMA,NT_D"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Möbius_function") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:MoebiusFunction.html") },
	{ GNM_FUNC_HELP_END }
};

static void
walk_for_mu (guint64 p, int v, void *data_)
{
	int *data = data_;
	*data = (v >= 2) ?  0 : -*data;
}

static GnmValue *
gnumeric_nt_mu (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int mu = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > bit_max)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((guint64)n, &mu, walk_for_mu))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (mu);
}


/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_d[] = {
	{ GNM_FUNC_HELP_NAME, F_("NT_D:number of divisors")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NT_D calculates the number of divisors of @{n}.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_D(4096)" },
	{ GNM_FUNC_HELP_SEEALSO, "ITHPRIME,NT_PHI,NT_SIGMA"},
	{ GNM_FUNC_HELP_END }
};
static void
walk_for_d (guint64 p, int v, void *data_)
{
	int *data = data_;
	*data *= (v + 1);
}

static GnmValue *
gnumeric_d (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int d = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > bit_max)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((guint64)n, &d, walk_for_d))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_int (d);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_sigma[] = {
	{ GNM_FUNC_HELP_NAME, F_("NT_SIGMA:sigma function") },
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NT_SIGMA calculates the sum of the divisors of @{n}.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_SIGMA(4)" },
	{ GNM_FUNC_HELP_SEEALSO, "NT_D,ITHPRIME,NT_PHI" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Divisor_function") },
	{ GNM_FUNC_HELP_END }
};

static void
walk_for_sigma (guint64 p, int v, void *data_)
{
	guint64 *data = data_;
	*data *= ( v == 1 ? p + 1 : (intpow (p, v + 1) - 1) / (p - 1) );
}

static GnmValue *
gnumeric_sigma (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	guint64 sigma = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > bit_max)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((guint64)n, &sigma, walk_for_sigma))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_guint64 (sigma);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_ithprime[] = {
 	{ GNM_FUNC_HELP_NAME, F_("ITHPRIME:@{i}th prime")},
	{ GNM_FUNC_HELP_ARG, F_("i:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ITHPRIME finds the @{i}th prime.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=ITHPRIME(7)" },
	{ GNM_FUNC_HELP_SEEALSO, "NT_D,NT_SIGMA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ithprime (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	guint64 p;
	gnm_float i = gnm_floor (value_get_as_float (args[0]));

	if (i < 1 || i > INT_MAX)
		return value_new_error_NUM (ei->pos);

	if (ithprime ((int)i, &p))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_guint64 (p);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_isprime[] = {
	{ GNM_FUNC_HELP_NAME, F_("ISPRIME:whether @{n} is prime")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ISPRIME returns TRUE if @{n} is prime and FALSE otherwise.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=ISPRIME(57)" },
	{ GNM_FUNC_HELP_SEEALSO, "NT_D, NT_SIGMA"},
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:PrimeNumber.html") },
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
 *    0 (n <= 1) or (out of bounds)
 *    smallest prime facter
 */
static guint64
prime_factor (guint64 n)
{
	int i = 1;
	guint64 p = 2;

	if (n <= 1)
		return 0;

	for (i = 1; p * p <= n; i++) {
		if (ithprime (i, &p))
			return 0;
		if (n % p == 0)
			return p;

	}

	return n;
}

static GnmFuncHelp const help_pfactor[] = {
	{ GNM_FUNC_HELP_NAME, F_("PFACTOR:smallest prime factor")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PFACTOR finds the smallest prime factor of its argument.")},
	{ GNM_FUNC_HELP_NOTE, F_("The argument @{n} must be at least 2. Otherwise a #VALUE! error is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=PFACTOR(57)" },
	{ GNM_FUNC_HELP_SEEALSO, "ITHPRIME"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pfactor (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	gnm_float n = gnm_floor (value_get_as_float (args[0]));
	gint64 p;

	if (n < 2)
		return value_new_error_VALUE (ei->pos);
	if (n > bit_max)
		p = 0;
	else
		p = prime_factor ((guint64)n);

	if (p == 0)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_float (p);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_nt_pi[] = {
	{ GNM_FUNC_HELP_NAME, F_("NT_PI:number of primes upto @{n}")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NT_PI returns the number of primes less than or equal to @{n}.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_PI(11)" },
	{ GNM_FUNC_HELP_SEEALSO, "ITHPRIME,NT_PHI,NT_D,NT_SIGMA"},
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:PrimeCountingFunction.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_nt_pi (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	gnm_float n = gnm_floor (value_get_as_float (args[0]));
	gint64 pi;

	if (n < 0)
		pi = 0;
	else if (n > bit_max)
		pi = -1;
	else
		pi = compute_nt_pi ((guint64)n);

	if (pi == -1)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);
	else
		return value_new_int (pi);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitor[] = {
	{ GNM_FUNC_HELP_NAME, F_("BITOR:bitwise or")},
	{ GNM_FUNC_HELP_ARG, F_("a:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("b:non-negative integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITOR returns the bitwise or of the binary representations of its arguments.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=BITOR(9,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITXOR,BITAND"},
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
	{ GNM_FUNC_HELP_NAME, F_("BITXOR:bitwise exclusive or")},
	{ GNM_FUNC_HELP_ARG, F_("a:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("b:non-negative integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITXOR returns the bitwise exclusive or of the binary representations of its arguments.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=BITXOR(9,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITOR,BITAND"},
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
	{ GNM_FUNC_HELP_NAME, F_("BITAND:bitwise and")},
	{ GNM_FUNC_HELP_ARG, F_("a:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("b:non-negative integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITAND returns the bitwise and of the binary representations of its arguments.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=BITAND(9,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITOR,BITXOR"},
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
	{ GNM_FUNC_HELP_NAME, F_("BITLSHIFT:bit-shift to the left")},
	{ GNM_FUNC_HELP_ARG, F_("a:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("n:integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITLSHIFT returns the binary representations of @{a} shifted @{n} positions to the left.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} is negative, BITLSHIFT shifts the bits to the right by ABS(@{n}) positions.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BITLSHIFT(9,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITRSHIFT"},
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
 	{ GNM_FUNC_HELP_NAME, F_("BITRSHIFT:bit-shift to the right")},
	{ GNM_FUNC_HELP_ARG, F_("a:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("n:integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITRSHIFT returns the binary representations of @{a} shifted @{n} positions to the right.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} is negative, BITRSHIFT shifts the bits to the left by ABS(@{n}) positions.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BITRSHIFT(137,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITLSHIFT"},
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
	{"ithprime", "f", help_ithprime,
	 &gnumeric_ithprime, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"pfactor", "f", help_pfactor,
	 &gnumeric_pfactor, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_omega",   "f", help_nt_omega,
	 &gnumeric_nt_omega,  NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_phi",   "f", help_phi,
	 &gnumeric_phi,      NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_d",     "f", help_d,
	 &gnumeric_d,        NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_sigma", "f", help_sigma,
	 &gnumeric_sigma,    NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"isprime",  "f", help_isprime,
	 &gnumeric_isprime,  NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_pi",    "f", help_nt_pi,
	 &gnumeric_nt_pi,    NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{"nt_mu",    "f", help_nt_mu,
	 &gnumeric_nt_mu,    NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{NULL}
};

const GnmFuncDescriptor bitwise_functions[] = {
	{"bitor",     "ff", help_bitor,
	 &func_bitor,     NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitxor",    "ff", help_bitxor,
	 &func_bitxor,    NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitand",    "ff", help_bitand,
	 &func_bitand,    NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitlshift", "ff", help_bitlshift,
	 &func_bitlshift, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitrshift", "ff", help_bitrshift,
	 &func_bitrshift, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{NULL}
};
