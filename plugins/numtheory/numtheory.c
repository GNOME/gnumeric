/*
 * Number Theory Plugin
 *
 * Author:
 *    Marko R. Riedel (mriedel@neuearbeit.de)    [Functions]
 *    Morten Welinder (terra@diku.dk)            [Plugin framework]
 */
#include <gnome.h>
#include <glib.h>

#include "../../src/gnumeric.h"
#include "../../src/func.h"
#include "../../src/plugin.h"
#include "../../src/value.h"

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
static int *prime_table = NULL;

static int 
ithprime (int i, int *res)
{
	static int computed = 0;
	static int allocated = 0;

	if (i < 1 || i > ITHPRIME_LIMIT)
		return 1;

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
		 * Note, that the dandidate is odd since we filled in the first
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
	return 0;
}

static void
walk_factorization (int n, void *data,
		    void (*walk_term) (int p, int v, void *data))
{
	int index = 1, p = 2, v;

	while (n > 1 && p * p <= n) {
		ithprime (index, &p);

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
	else if (n > 262144)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	walk_factorization (n, &phi, walk_for_phi);
	return value_new_int (phi);
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
	else if (n > 262144)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	walk_factorization (n, &d, walk_for_d);
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

/* ------------------------------------------------------------------------- */

static Value *
gnumeric_sigma (FunctionEvalInfo *ei, Value **args)
{
	int n, sigma = 1;

	n = value_get_as_int (args [0]);
	if (n < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);
	else if (n > 262144)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	walk_factorization (n, &sigma, walk_for_sigma);
	return value_new_int (sigma);
}

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

/* ------------------------------------------------------------------------- */

static Value *
gnumeric_ithprime (FunctionEvalInfo *ei, Value **args)
{
	int i, p;

	i = value_get_as_int (args [0]);
	if (i < 1)
		return value_new_error (ei->pos, gnumeric_err_NUM);
	else if (i > ITHPRIME_LIMIT)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	ithprime (i, &p);
	return value_new_int (p);
}

/* ------------------------------------------------------------------------- */

static const char *function_names[] = {
	"nt_phi", "nt_d", "nt_sigma", "ithprime"
};

static const int function_count =
	sizeof (function_names) / sizeof (function_names[0]);

static int
can_unload (PluginData *pd)
{
	int i, excess = 0;

	for (i = 0; i < function_count; i++) {
		Symbol *sym;
		sym = symbol_lookup (global_symbol_table,
				     function_names[i]);
		excess += sym ? sym->ref_count - 1 : 0;
	}

	return excess == 0;
}

static void
cleanup_plugin (PluginData *pd)
{
	int i;

	for (i = 0; i < function_count; i++) {
		Symbol *sym;
		sym = symbol_lookup (global_symbol_table,
				     function_names[i]);
		if (sym) symbol_unref (sym);
	}

	g_free (prime_table);
	prime_table = 0;
}

#define NUMTHEORY_TITLE _("Number Theory Plugin")
#define NUMTHEORY_DESCR _("Gnumeric Plugin For Number Theory")

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	FunctionCategory *cat;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	cat = function_get_category (_("Number Theory"));

	function_add_args  (cat, "ithprime","f",    
			    "number",    &help_ithprime, gnumeric_ithprime);
	function_add_args  (cat, "nt_phi",     "f",    
			    "number",    &help_phi,      gnumeric_phi);
	function_add_args  (cat, "nt_d",       "f",    
			    "number",    &help_d,        gnumeric_d);
	function_add_args  (cat, "nt_sigma",   "f",    
			    "number",    &help_sigma,    gnumeric_sigma);

	if (plugin_data_init (pd, can_unload, cleanup_plugin,
			      NUMTHEORY_TITLE, NUMTHEORY_DESCR))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;
}
