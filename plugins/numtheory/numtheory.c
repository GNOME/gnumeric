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
/* number-theoretic functions by       */

#define PTABLE_CHUNK 64

static int
intpow (int p, int v)
{
	int temp;
	
	if (v == 0) return 1;
	if (v == 1) return p;

	temp = intpow (p, v/2);

	return temp * temp * (v % 2 ? p : 1);
}

static int *table = NULL;

static int 
ithprime (int i, int *res)
{
	static int computed = 0;
	static int allocated = 0;

	int candidate, index;

	if (i < 1 || i > 32768)
		return 1;


	if (i <= computed) {
		*res = table[i-1];
		return 0;
	}

	if (!allocated) {
		allocated = PTABLE_CHUNK;
		table = g_malloc (allocated * sizeof(int));
	}

	if (!computed) {
		table[0] = 2;
		table[1] = 3;

		computed = 2;
	}

	candidate = table[computed - 1];
	while (i > computed) {
		candidate++;
		for (index = 0; 
		     candidate % table[index] != 0 && 
			     table[index] * table[index] < candidate;
		     index++);

		if (candidate % table[index] != 0) {
			computed++;
			if (computed > allocated) {
				allocated += PTABLE_CHUNK;
				table = g_realloc (table, 
						   allocated * sizeof(int));
			}

			table[computed-1] = candidate;
		}
	}

	*res = table[i-1];
	return 0;
}

static void
walk_factorization (int n, void *data,
		    void (*walk_term) (int p, int v, void *data))
{
	int index = 1, p = 1, v;

	while (n > 1 && p * p <= n) {
		ithprime (index, &p);

		v = 0;
		while (!(n % p )) {
			v ++;
			n /= p;
		}

		if (v) {
			walk_term (p, v, data);
		}

		index++;
	}

	if (n > 1) {
		walk_term (n, 1, data);
	}
}

static void
walk_for_phi (int p, int v, void *data)
{
	* (int *) data *= intpow (p, v - 1) * (p - 1);
}

static void
walk_for_d (int p, int v, void *data)
{
	* (int *) data *= (v + 1);
}

static void
walk_for_sigma (int p, int v, void *data)
{
	* (int *) data *= 
		  ( v == 1 ? p + 1 : (intpow (p, v + 1) - 1) / (p - 1) );
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
	   "@SEEALSO=NT_D, NT_ITHPRIME, NT_SIGMA")
};

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


static char *help_d = {
	N_("@FUNCTION=NT_D\n"
	   "@SYNTAX=NT_D(n)\n"
	   "@DESCRIPTION="
	   "The NT_D function calculates the number of divisors of @n.\n" 
	   "This function only takes one argument."
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_ITHPRIME, NT_PHI, NT_SIGMA")
};

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


static char *help_sigma = {
	N_("@FUNCTION=NT_SIGMA\n"
	   "@SYNTAX=NT_SIGMA(n)\n"
	   "@DESCRIPTION="
	   "The NT_SIGMA function calculates the sum of the divisors of @n.\n" 
	   "This function only takes one argument."
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=NT_D, NT_ITHPRIME, NT_PHI")
};

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
	N_("@FUNCTION=NT_ITHPRIME\n"
	   "@SYNTAX=NT_ITHPRIME(i)\n"
	   "@DESCRIPTION="
	   "The NT_ITHPRIME function returns the @ith prime.\n" 
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
	else if (i > 32768)
		return value_new_error (ei->pos, OUT_OF_BOUNDS);

	ithprime (i, &p);
	return value_new_int (p);
}

/* ------------------------------------------------------------------------- */

static const char *function_names[] = {
	"nt_phi", "nt_d", "nt_sigma", "nt_ithprime"
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

	g_free (table);
	table = 0;
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

	function_add_args  (cat, "nt_ithprime","f",    
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
