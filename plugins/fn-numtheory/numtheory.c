/*
 * Number Theory Plugin
 *
 * Author:
 *    Marko R. Riedel (mriedel@neuearbeit.de)    [Functions]
 *    Morten Welinder (terra@gnome.org)          [Plugin framework and more]
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>
#include <value.h>
#include <gnm-i18n.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <limits.h>
#include <collect.h>
#include <mathfunc.h>

GNM_PLUGIN_MODULE_HEADER;

#define OUT_OF_BOUNDS "#LIMIT!"

/*
 * The largest integer i, such at all integers {0,...,i} can be accurately
 * represented in a gnm_float _and_ in a guint64.  (For regular "double",
 * the latter part is irrelevant.)
 */
static const gnm_float bit_max = MIN (GNM_RADIX / GNM_EPSILON, (gnm_float)G_MAXUINT64);

// The largest odd number.  (This probably assumes even radix.)
static const gnm_float last_odd = GNM_RADIX / GNM_EPSILON - 1;

/* ------------------------------------------------------------------------- */

static GnmValue *
value_new_guint64 (guint64 n)
{
	return value_new_float (n);
}

// Integer power function
static guint64
intpow (guint64 p, unsigned int v)
{
	guint64 res = 1;
	while (v > 0) {
		if (v & 1) res *= p;
		v >>= 1;
		if (v > 0) p *= p;
	}
	return res;
}

// Modular addition (a + b) % m without overflow.
static guint64
add_mod (guint64 a, guint64 b, guint64 m)
{
	if (a < m - b)
		return a + b;
	else
		return a - (m - b);
}

// Modular multiplication (a * b) % m without overflow.
static guint64
mul_mod (guint64 a, guint64 b, guint64 m)
{
	guint64 res = 0;
	a %= m;
	while (b > 0) {
		if (b & 1) res = add_mod (res, a, m);
		a = add_mod (a, a, m);
		b >>= 1;
	}
	return res;
}

// Modular exponentiation (a ^ b) % m without overflow.
static guint64
pow_mod (guint64 a, guint64 b, guint64 m)
{
	guint64 res = 1;
	a %= m;
	while (b > 0) {
		if (b & 1) res = mul_mod (res, a, m);
		a = mul_mod (a, a, m);
		b >>= 1;
	}
	return res;
}

#define ITHPRIME_LIMIT 100000000
static guint *prime_table = NULL;
static guint prime_table_size = 0;

// Bit-field macros for sieve.  Note that only odd indices are used.
#define SIEVE_ITEM(u_) sieve[((u_) - base) >> 4]
#define SIEVE_BIT(u_) (1u << ((((u_) - base) >> 1) & 7))

// Calculate the i-th prime.  Returns TRUE on too-big-to-handle error.
static gboolean
ithprime (int i, guint64 *res)
{
	gboolean debug = FALSE;
	static const guint chunk = 1000000;

	if (i < 1 || i > ITHPRIME_LIMIT)
		return TRUE;

	if ((guint)i > prime_table_size) {
		guint base, ub, L, c;
		guint newsize = MIN (ITHPRIME_LIMIT,
				     (i + chunk - 1) / chunk * chunk);
		guint N = prime_table_size;
		guint ui;
		guint8 *sieve;

		base = N ? prime_table[N - 1] + 1 : 0;
		// Compute an upper bound of the largest prime we need.
		// See https://en.wikipedia.org/wiki/Prime_number_theorem
		// This bound is valid for n >= 6.
		ub = (guint)
			(newsize * (log (newsize) + log (log (newsize))));
		// Largest candidate that needs sieving.  Note that this
		// number can be squared without overflow.
		L = (guint)sqrt (ub);

		// Extend the table; fill with 2 if empty
		prime_table = g_renew (guint, prime_table, newsize);
		if (!N)
			prime_table[N++] = 2;

		// Allocate the sieve
		sieve = g_new0 (guint8, ((ub - base) >> 4) + 1);

		// Tag odd multiples of primes already in prime_table
		for (ui = 1; ui < N; ui++) {
			guint c = prime_table[ui], d;
			if (c > L)
				break;

			d = c * c;
			if (d < base) {
				// Skip to first odd multiple larger than base
				d = base - base % c + c;
				if (d % 2 == 0)
					d += c;
			}

			for (; d <= ub; d += 2 * c)
				SIEVE_ITEM (d) |= SIEVE_BIT (d);
		}

		// Now look at new candidates until we have enough primes
		for (c = (base ? base + 1 : 3); N < newsize; c += 2) {
			if (SIEVE_ITEM (c) & SIEVE_BIT (c))
				continue;
			prime_table[N++] = c;
			if (c <= L) {
				// Tag odd multiples of c starting at c^2
				guint d = c * c;
				for (; d <= ub; d += 2 * c)
					SIEVE_ITEM (d) |= SIEVE_BIT (d);
			}
		}

		if (debug)
			g_printerr ("New size of prime table is %d\n", N);
		prime_table_size = N;
		g_free (sieve);
	}

	if (debug)
		g_printerr ("%dth prime is %d\n", i, prime_table[i - 1]);

	*res = prime_table[i - 1];
	return FALSE;
}

#undef SIEVE_ITEM
#undef SIEVE_BIT


static int isprime (guint64 n);

/*
 * A function useful for computing multiplicative arithmetic functions.
 * Returns TRUE on error.
 */
static gboolean
walk_factorization (guint64 n, void *data,
		    void (*walk_term) (guint64 p, int v, void *data))
{
	int index = 1, v;
	guint64 p = 2;

	if (n <= 1)
		return FALSE;

	if (isprime (n) == 1) {
		walk_term (n, 1, data);
		return FALSE;
	}

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
			if (isprime (n) == 1)
				break;
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
		upper = MAX (upper + 1, (MIN (upper * 2, ITHPRIME_LIMIT)));
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

// Miller-Rabin primality test for base a.
static gboolean
miller_rabin_test (guint64 n, guint64 a)
{
	guint64 d = n - 1;
	int s = 0;
	guint64 x;

	while (d % 2 == 0) {
		d /= 2;
		s++;
	}

	x = pow_mod (a, d, n);
	if (x == 1 || x == n - 1)
		return TRUE;

	while (s > 1) {
		x = mul_mod (x, x, n);
		if (x == n - 1)
			return TRUE;
		s--;
	}

	return FALSE;
}

/*
 * Returns -1 (out of bounds), 0 (non-prime), or 1 (prime).
 */
static int
isprime (guint64 n)
{
	// Deterministic up to 3,825,123,056,546,413,051
	static const guint8 bases[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23 };
	unsigned int i;

	if (n >= 3825123056546413051ull)
		return -1;

	if (n < 2) return 0;
	if (n == 2 || n == 3) return 1;
	if (n % 2 == 0 || n % 3 == 0) return 0;

	for (i = 0; i < G_N_ELEMENTS (bases); i++) {
		if (n == bases[i]) return 1;
		if (!miller_rabin_test (n, bases[i]))
			return 0;
	}

	return 1;
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_nt_omega[] = {
	{ GNM_FUNC_HELP_NAME, F_("NT_OMEGA:Number of distinct prime factors")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_NOTE, F_("Returns the number of distinct prime factors without multiplicity.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_OMEGA(9)" },
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

static GnmFuncHelp const help_radical[] = {
	{ GNM_FUNC_HELP_NAME, F_("NT_RADICAL:Radical function")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_NOTE, F_("The function computes the product of its distinct prime factors") },
	{ GNM_FUNC_HELP_EXAMPLES, "=NT_RADICAL(36)" },
	{ GNM_FUNC_HELP_SEEALSO, "NT_D,ITHPRIME,NT_SIGMA"},
	{ GNM_FUNC_HELP_END }
};

static void
walk_for_radical (guint64 p, int v, void *data_)
{
	guint64 *data = data_;
	*data *= p;
}

static GnmValue *
gnumeric_radical (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	guint64 rad = 1;
	gnm_float n = gnm_floor (value_get_as_float (args[0]));

	if (n < 1 || n > bit_max)
		return value_new_error_NUM (ei->pos);

	if (walk_factorization ((guint64)n, &rad, walk_for_radical))
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	return value_new_guint64 (rad);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_nt_mu[] = {
	{ GNM_FUNC_HELP_NAME, F_("NT_MU:Möbius mu function")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("NT_MU function (Möbius mu function) returns 0 if @{n} is "
	     "divisible by the square of a prime. Otherwise, if @{n} has"
	     " an odd number of different prime factors, NT_MU returns "
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
	guint64 s;
	if (v == 1)
		s = p + 1;
	else {
		// Compute the overflow-safe 1+...+p^(v-1) first
		s = (intpow (p, v) - 1) / (p - 1);
		s = s * p + 1;
	}
	*data *= s;
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
	{ GNM_FUNC_HELP_SEEALSO, "ISPRIME,NT_D,NT_SIGMA"},
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
	{ GNM_FUNC_HELP_ARG, F_("n:integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ISPRIME returns TRUE if @{n} is prime and FALSE otherwise.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=ISPRIME(57)" },
	{ GNM_FUNC_HELP_SEEALSO, "ITHPRIME,NT_D,NT_SIGMA"},
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:PrimeNumber.html") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isprime (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int yesno;
	gnm_float i = gnm_floor (value_get_as_float (args[0]));

	if (i < 2 || i > last_odd)
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
 *    smallest prime factor
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
	if (n > last_odd)
		p = 2;
	else if (n > bit_max)
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
	{ GNM_FUNC_HELP_ARG, F_("n:integer")},
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
	{ GNM_FUNC_HELP_ARG, F_("values:non-negative integers")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITOR returns the bitwise or of the binary representations of its arguments.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BITOR(9,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITXOR,BITAND"},
	{ GNM_FUNC_HELP_END }
};

static int
gnm_range_bitor (gnm_float const *xs, int n, gnm_float *res)
{
	int i;
	guint64 acc = 0;

	for (i = 0; i < n; i++) {
		gnm_float x = gnm_fake_floor (xs[i]);
		if (x < 0 || x > bit_max)
			return 1;
		acc |= (guint64)x;
	}

	*res = acc;
	return 0;
}

static GnmValue *
func_bitor (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_bitor,
				     COLLECT_COERCE_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitxor[] = {
	{ GNM_FUNC_HELP_NAME, F_("BITXOR:bitwise exclusive or")},
	{ GNM_FUNC_HELP_ARG, F_("values:non-negative integers")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITXOR returns the bitwise exclusive or of the binary representations of its arguments.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BITXOR(9,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITOR,BITAND"},
	{ GNM_FUNC_HELP_END }
};

static int
gnm_range_bitxor (gnm_float const *xs, int n, gnm_float *res)
{
	int i;
	guint64 acc = 0;

	for (i = 0; i < n; i++) {
		gnm_float x = gnm_fake_floor (xs[i]);
		if (x < 0 || x > bit_max)
			return 1;
		acc ^= (guint64)x;
	}

	*res = acc;
	return 0;
}

static GnmValue *
func_bitxor (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_bitxor,
				     COLLECT_COERCE_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitand[] = {
	{ GNM_FUNC_HELP_NAME, F_("BITAND:bitwise and")},
	{ GNM_FUNC_HELP_ARG, F_("values:non-negative integers")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITAND returns the bitwise and of the binary representations of its arguments.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BITAND(9,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BITOR,BITXOR"},
	{ GNM_FUNC_HELP_END }
};

static int
gnm_range_bitand (gnm_float const *xs, int n, gnm_float *res)
{
	int i;
	guint64 acc = (guint64)-1;

	if (n == 0)
		return 1;

	for (i = 0; i < n; i++) {
		gnm_float x = gnm_fake_floor (xs[i]);
		if (x < 0 || x > bit_max)
			return 1;
		acc &= (guint64)x;
	}

	*res = acc;
	return 0;
}

static GnmValue *
func_bitand (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_bitand,
				     COLLECT_COERCE_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/* ------------------------------------------------------------------------- */

static GnmFuncHelp const help_bitlshift[] = {
	{ GNM_FUNC_HELP_NAME, F_("BITLSHIFT:bit-shift to the left")},
	{ GNM_FUNC_HELP_ARG, F_("a:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("n:integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BITLSHIFT returns the binary representations of @{a} shifted @{n} positions to the left.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
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
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
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

static GnmFuncHelp const help_partitions[] = {
	{ GNM_FUNC_HELP_NAME, F_("PARTITIONS:number of partitions")},
	{ GNM_FUNC_HELP_ARG, F_("n:non-negative integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PARTITIONS calculates the number of ways of writing the integer @{n} as a sum of positive integers.")},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Partition_(number_theory)")},
	{ GNM_FUNC_HELP_EXAMPLES, "=PARTITIONS(5)" },
	{ GNM_FUNC_HELP_SEEALSO, "NT_PHI,NT_SIGMA"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float *partition_table = NULL;
static gnm_float partitions_cutoff;

// Rademacher's series for the partition function:
//
//   p(n) = 1/(pi*sqrt(2)) * sum_{k>=1} A_k(n) * sqrt(k) *
//          d/dn [ sinh (C/k * sqrt (n-1/24)) / sqrt (n-1/24) ]
//
// with C = pi*sqrt(2/3).  Term k is of size exp(C*sqrt(n)/k), so after
// three terms the relative error is about exp(-0.75*C*sqrt(n)).
// For binary64 that is below half an ulp from n=~375.  Use the series
// from there on; below that the recurrence is cheap.
static int
partitions_rademacher_min (void)
{
	// binary64: 392
	// binary80: 564
	// decimal64: 361

	static int nmin = 0;
	if (nmin == 0) {
		gnm_float C = M_PIgnum * gnm_sqrt (GNM_const (2.) / 3);
		gnm_float x = (-gnm_log (GNM_EPSILON) + 2) / (GNM_const (0.75) * C);
		nmin = (int)gnm_ceil (x * x);

		// An assert to ensure that everything smaller fits in 128 bits.
		g_assert (nmin < 1459);
	}
	return nmin;
}

static gnm_float
partitions_rademacher (int n)
{
	void *state = gnm_quad_start ();
	GnmQuad q3, np, u, invu, C, x1, F, sum;
	gnm_float e1 = 0;

	gnm_quad_init (&q3, 3);

	// np = n - 1/24; u = sqrt (np)
	gnm_quad_init (&np, 1);
	gnm_quad_init (&F, 24);
	gnm_quad_div (&np, &np, &F);
	gnm_quad_init (&F, n);
	gnm_quad_sub (&np, &F, &np);
	gnm_quad_sqrt (&u, &np);
	gnm_quad_div (&invu, &gnm_quad_one, &u);

	// C = pi * sqrt (2/3)
	gnm_quad_init (&C, 2);
	gnm_quad_div (&C, &C, &q3);
	gnm_quad_sqrt (&C, &C);
	gnm_quad_mul (&C, &C, &gnm_quad_pi);

	gnm_quad_mul (&x1, &C, &u);

	// F = 1 / (4 * sqrt(2) * pi * np).  This is the common factor of
	// all terms once sinh and cosh have been replaced by exp(x)/2; the
	// neglected exp(-x) parts are below exp(-2*x), which is under 1e-15
	// for the k=3 term and far smaller for the others.
	gnm_quad_mul (&F, &gnm_quad_sqrt2, &gnm_quad_pi);
	gnm_quad_mul (&F, &F, &np);
	gnm_quad_init (&sum, 4);
	gnm_quad_mul (&F, &F, &sum);
	gnm_quad_div (&F, &gnm_quad_one, &F);

	sum = gnm_quad_zero;
	for (int k = 1; k <= 3; k++) {
		GnmQuad qk, xk, r, t, f, a;
		gnm_float ek;

		gnm_quad_init (&qk, k);

		// exp(C*sqrt(np)/k) as r * 2^ek -- no overflow
		gnm_quad_div (&xk, &x1, &qk);
		gnm_quad_exp (&r, &ek, &xk);

		// (C/k - 1/sqrt(np))
		gnm_quad_div (&f, &C, &qk);
		gnm_quad_sub (&f, &f, &invu);

		// A_k(n) * sqrt(k)
		switch (k) {
		case 1:
			a = gnm_quad_one;
			break;
		case 2:
			// A_2(n) = (-1)^n
			gnm_quad_sqrt (&a, &qk);
			if (n & 1)
				gnm_quad_sub (&a, &gnm_quad_zero, &a);
			break;
		default:
		case 3: {
			// A_3(n) = 2 cos (pi * (12n-1) / 18), which takes one of
			// three values depending on n mod 3.  They are the roots
			// of x^3 - 3x - sqrt(3) = 0.  Start from a native
			// approximation and polish with Newton in quad precision;
			// that is cheaper and better behaved than quad trig.
			static const int m[3] = { 35, 11, 23 };  // (12n-1) mod 36
			GnmQuad s3, x2, d;
			gnm_quad_sqrt (&s3, &q3);
			gnm_quad_init (&a, 2 * gnm_cospi (m[n % 3] / GNM_const (18.)));
			for (int i = 0; i < 2; i++) {
				// f = x^3 - 3x - sqrt(3);  f' = 3 (x^2 - 1)
				gnm_quad_mul (&x2, &a, &a);
				gnm_quad_sub (&d, &x2, &q3);
				gnm_quad_mul (&d, &d, &a);
				gnm_quad_sub (&d, &d, &s3);
				gnm_quad_sub (&t, &x2, &gnm_quad_one);
				gnm_quad_mul (&t, &t, &q3);
				gnm_quad_div (&d, &d, &t);
				gnm_quad_sub (&a, &a, &d);
			}
			gnm_quad_sqrt (&t, &qk);
			gnm_quad_mul (&a, &a, &t);
			break;
		}
		}

		gnm_quad_mul (&t, &F, &a);
		gnm_quad_mul (&t, &t, &f);
		gnm_quad_mul (&t, &t, &r);

		if (k == 1) {
			e1 = ek;
			sum = t;
		} else {
			gnm_quad_scalbn (&t, &t, (int)(ek - e1));
			gnm_quad_add (&sum, &sum, &t);
		}
	}

	gnm_quad_end (state);

	return gnm_scalbn (gnm_quad_value (&sum), (int)e1);
}

// Exact 128-bit unsigned arithmetic, wrapping modulo 2^128.  We only need
// addition and subtraction.
typedef struct {
	guint64 lo, hi;
} Uint128;

static void
uint128_add (Uint128 *a, const Uint128 *b)
{
	a->lo += b->lo;
	a->hi += b->hi + (a->lo < b->lo);
}

static void
uint128_sub (Uint128 *a, const Uint128 *b)
{
	guint64 borrow = a->lo < b->lo;
	a->lo -= b->lo;
	a->hi -= b->hi + borrow;
}

// Convert to gnm_float with a single rounding.
static gnm_float
uint128_to_gnm_float (const Uint128 *a)
{
	if (a->hi == 0)
		return (gnm_float)a->lo;

#if GNM_RADIX == 2 && GNM_MANT_DIG < 64
	// Keep the top 62 bits and fold everything below into a sticky
	// bit.  Converting that to gnm_float rounds correctly, and the
	// final scaling by a power of 2 is exact.
	int hb = 0, s;
	guint64 v, sticky;
	for (guint64 h = a->hi; h; h >>= 1)
		hb++;
	s = 64 + hb - 62;
	if (s < 64) {
		v = (a->hi << (64 - s)) | (a->lo >> s);
		sticky = (a->lo & ((G_GUINT64_CONSTANT (1) << s) - 1)) != 0;
	} else {
		int t = s - 64;
		v = a->hi >> t;
		sticky = a->lo != 0 ||
			(a->hi & ((G_GUINT64_CONSTANT (1) << t) - 1)) != 0;
	}
	return gnm_ldexp ((gnm_float)(v | sticky), s);
#else
	// Either the hi part is exact (binary80) or we don't have a
	// better way (decimal).
	return (gnm_float)a->hi * GNM_const (18446744073709551616.0) +
		(gnm_float)a->lo;
#endif
}

// Euler's pentagonal number recurrence
//
//   p(n) = sum_{j>=1} (-1)^(j+1) * (p(n - j(3j-1)/2) + p(n - j(3j+1)/2))
//
// is hopeless in floating point because of cancellation, but it is exact in
// integers.  Everything below the Rademacher cutoff fits in 128 bits, so
// wrapping arithmetic gives the right answer even if intermediate sums do
// not.  We compute the whole table the first time any entry is needed.
static void
partitions_fill_table (void)
{
	int nmin = partitions_rademacher_min ();
	Uint128 *p = g_new (Uint128, nmin);

	p[0].lo = 1;
	p[0].hi = 0;
	for (int n = 1; n < nmin; n++) {
		Uint128 pos = { 0, 0 }, neg = { 0, 0 };
		for (int j = 1; ; j++) {
			int g1 = j * (3 * j - 1) / 2;
			int g2 = g1 + j;
			Uint128 *acc = (j & 1) ? &pos : &neg;
			if (g1 > n)
				break;
			uint128_add (acc, &p[n - g1]);
			if (g2 <= n)
				uint128_add (acc, &p[n - g2]);
		}
		uint128_sub (&pos, &neg);
		p[n] = pos;
	}

	partition_table = g_new (gnm_float, nmin);
	for (int n = 0; n < nmin; n++)
		partition_table[n] = uint128_to_gnm_float (&p[n]);
	g_free (p);
}

static gnm_float
partitions (int n)
{
	if (n < 0) return 0;

	if (n >= partitions_rademacher_min ())
		return partitions_rademacher (n);

	if (!partition_table)
		partitions_fill_table ();

	return partition_table[n];
}

static GnmValue *
gnumeric_partitions (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float fn = gnm_floor (value_get_as_float (argv[0]));

	if (fn < 0)
		return value_new_error_NUM (ei->pos);

	if (fn > partitions_cutoff)
		return value_new_error_VALUE (ei->pos);

	int n = (int)fn;
	gnm_float pn = partitions (n);
	return gnm_finite (pn)
		? value_new_float (pn)
		: value_new_error_VALUE (ei->pos);
}

/* ------------------------------------------------------------------------- */

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	// Compute the overflow point (with a margin) for partitions
	{
		// binary64: ~80k, decimal64: ~123k, binary80: ~20M
		gnm_float L = gnm_log (GNM_MAX) + gnm_log (48) / 2;
		gnm_float a = M_PIgnum * gnm_sqrt (GNM_const (2.) / 3);
		gnm_float sn = (L + 2 * gnm_log (L / a)) / a;
		partitions_cutoff = (sn * sn * GNM_const(1.01));

		// Current algorithm gets slow
		if (partitions_cutoff > 125000)
			partitions_cutoff = 125000;
	}
}



G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	g_free (prime_table);
	prime_table = NULL;
	prime_table_size = 0;

	g_free (partition_table);
	partition_table = NULL;
}

const GnmFuncDescriptor num_theory_functions[] = {
	{"ithprime", "f", help_ithprime,
	 &gnumeric_ithprime, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{"partitions", "f", help_partitions,
	 &gnumeric_partitions, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{"pfactor", "f", help_pfactor,
	 &gnumeric_pfactor, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{"nt_omega",   "f", help_nt_omega,
	 &gnumeric_nt_omega, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"nt_phi",   "f", help_phi,
	 &gnumeric_phi,      NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"nt_radical",   "f", help_radical,
	 &gnumeric_radical,      NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{"nt_d",     "f", help_d,
	 &gnumeric_d,        NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"nt_sigma", "f", help_sigma,
	 &gnumeric_sigma,    NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"isprime",  "f", help_isprime,
	 &gnumeric_isprime,  NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{"nt_pi",    "f", help_nt_pi,
	 &gnumeric_nt_pi,    NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{"nt_mu",    "f", help_nt_mu,
	 &gnumeric_nt_mu,    NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },

	{NULL}
};

const GnmFuncDescriptor bitwise_functions[] = {
	{"bitor", NULL, help_bitor,
	 NULL, &func_bitor,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitxor", NULL, help_bitxor,
	 NULL, &func_bitxor,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitand", NULL, help_bitand,
	 NULL,  &func_bitand,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitlshift", "ff", help_bitlshift,
	 &func_bitlshift, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{"bitrshift", "ff", help_bitrshift,
	 &func_bitrshift, NULL,
	 GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{NULL}
};
