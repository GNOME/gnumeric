/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-logical.c:  Built in logical functions and functions registration
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
#include <value.h>
#include <auto-format.h>

#include <libgnome/gnome-i18n.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static const char *help_rand = {
	N_("@FUNCTION=RAND\n"
	   "@SYNTAX=RAND()\n"

	   "@DESCRIPTION="
	   "RAND returns a random number between zero and one ([0..1]).\n"
	   "This function is Excel compatible. "
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

static const char *help_randexp = {
        N_("@FUNCTION=RANDEXP\n"
           "@SYNTAX=RANDEXP(b)\n"

           "@DESCRIPTION="
           "RANDEXP returns a exponentially-distributed random number. "
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
           "RANDPOISSON returns a poisson-distributed random number. "
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
           "RANDBINOM returns a binomially-distributed random number. "
           "\n"
           "If @p < 0 or @p > 1 RANDBINOM returns #NUM! error. "
           "If @trials < 0 RANDBINOM returns #NUM! error. "
	   "\n"
           "@EXAMPLES=\n"
           "RANDBINOM(0.5,2).\n"
           "\n"
           "@SEEALSO=RAND,RANDBETWEEN")
};

static Value *
gnumeric_randbinom (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float p = value_get_as_float (argv[0]);
	int     trials = value_get_as_int (argv[1]);

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
	   "If @bottom or @top is non-integer, they are truncated. "
	   "If @bottom > @top, RANDBETWEEN returns #NUM! error.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RANDBETWEEN(3,7).\n"
	   "\n"
	   "@SEEALSO=RAND")
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
           "number. "
           "\n"
           "If @p < 0 or @p > 1, RANDNEGBINOM returns #NUM! error. "
           "If @failures RANDNEGBINOM returns #NUM! error. "
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
           "RANDBERNOULLI returns a Bernoulli-distributed random number. "
           "\n"
           "If @p < 0 or @p > 1 RANDBERNOULLI returns #NUM! error. "
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

const ModulePluginFunctionInfo random_functions[] = {
	{ "rand",    "", "",           &help_rand,
	  gnumeric_rand, NULL, NULL, NULL },
        { "randbernoulli", "f", N_("p"),   &help_randbernoulli,
	  gnumeric_randbernoulli, NULL, NULL, NULL },
        { "randbetween", "ff", N_("bottom,top"), &help_randbetween,
	  gnumeric_randbetween, NULL, NULL, NULL },
        { "randbinom", "ff", N_("p,trials"), &help_randbinom,
	  gnumeric_randbinom, NULL, NULL, NULL },
        { "randexp", "f", N_("b"),         &help_randexp,
	  gnumeric_randexp, NULL, NULL, NULL },
        { "randnegbinom", "ff", N_("p,failures"), &help_randnegbinom,
	  gnumeric_randnegbinom, NULL, NULL, NULL },
        { "randpoisson", "f", N_("lambda"), &help_randpoisson,
	  gnumeric_randpoisson, NULL, NULL, NULL },
        {NULL}
};

/* FIXME: Should be merged into the above.  */
static const struct {
	const char *func;
	AutoFormatTypes typ;
} af_info[] = {
	{ NULL, AF_UNKNOWN }
};

void
plugin_init (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_by_name (af_info[i].func,
						     af_info[i].typ);
}

void
plugin_cleanup (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_remove (af_info[i].func);
}
