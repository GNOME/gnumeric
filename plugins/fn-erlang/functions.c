/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-erlang.c:  Built in date functions.
 *
 * Authors:
 *   Arief Mulya Utama <arief_m_utama@telkomsel.co.id>
 *                     <arief.utama@gmail.com>
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
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <str.h>
#include <cell.h>
#include <value.h>
#include <mathfunc.h>
#include <format.h>
#include <workbook.h>
#include <sheet.h>

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

#define ERLANG_LIMIT	100000
#define MOVING_FACTOR	5E-4

static gnm_float
guess_carried_traffic (gnm_float traffic, gnm_float gos)
{
	return traffic * (1.0 - gos);
}

static gnm_float
calculate_gos (gnm_float traffic, gnm_float circuits)
{
	gnm_float gos;

	/* extra guards wont hurt, right? */
	if (circuits < 1 || traffic < 0 || circuits < traffic)
		return -1;

	if (traffic == 0)
		return 0;

	if (circuits < 25) {
		gnm_float cir_iter = 1;
		gos = 1;
		for (cir_iter = 1; cir_iter <= circuits; cir_iter++)
			gos = (traffic * gos) / (cir_iter + (traffic * gos));
	} else {
		/* FIXME: What about cancellation?  */
		gos = expgnum (circuits * loggnum (traffic) - lgamma1p (circuits) - traffic -
			       pgamma (traffic, circuits + 1, 1, FALSE, TRUE));
	}

	return gos;
}

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/
static char const *help_probblock = {
	N_("@FUNCTION=PROBBLOCK\n"
	   "@SYNTAX=PROBBLOCK(traffic,circuits)\n"

	   "@DESCRIPTION="
	   "PROBBLOCK returns probability of blocking when a number "
	   "of @traffic loads into a number of @circuits (servers).\n"
	   "\n"
	   "* @traffic cannot exceed @circuits\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "PROBBLOCK(24, 30) returns '0.4012'.\n"
	   "\n"
	   "@SEEALSO=OFFTRAF, DIMCIRC, OFFCAP")
};

static GnmValue *
gnumeric_probblock (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float traffic, circuits;

	traffic  = value_get_as_float(argv[0]);
	circuits = value_get_as_float(argv[1]);

	if (circuits < 1 || traffic < 0 || circuits < traffic)
		return value_new_error_VALUE (ei->pos);

	return value_new_float (calculate_gos (traffic, circuits));
}

static char const *help_offtraf = {
	N_("@FUNCTION=OFFTRAF\n"
	   "@SYNTAX=OFFTRAF(traffic,circuits)\n"

	   "@DESCRIPTION="
	   "OFFTRAF returns a predicted number of offered traffic "
	   "from a number of carried @traffic (taken from measurements) "
	   "on a number of @circuits.\n"
	   "\n"
	   "* @traffic cannot exceed @circuits\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OFFTRAF(24, 30) returns '25.526'.\n"
	   "\n"
	   "@SEEALSO=PROBBLOCK, DIMCIRC, OFFCAP")
};

static GnmValue *
gnumeric_offtraf (FunctionEvalInfo *ei, GnmValue **argv)
{
	int i;
	gnm_float guessed_offtraf, guessed_cartraf;
	gnm_float offtraf_lower, offtraf_upper;
	gnm_float cartraf_diff;

	gnm_float traffic  = value_get_as_float(argv[0]);
	gnm_float circuits = value_get_as_float(argv[1]);

	if (circuits < 1 || traffic < 0 || circuits < traffic)
		return value_new_error_VALUE (ei->pos);

	for (offtraf_upper = offtraf_lower = traffic;
		offtraf_upper < ERLANG_LIMIT /* MAX TRAF */ &&
		guess_carried_traffic(
			offtraf_upper, 
			calculate_gos (offtraf_upper, circuits)
			) < traffic;) {
		offtraf_lower  = offtraf_upper;
		offtraf_upper *= 2.0;
	}

	/* who is the poor boy that will design more than ERLANG_LIMIT? 
	 * I sure am hoping it will never happened. */
	if (offtraf_upper > ERLANG_LIMIT) return value_new_float (-1);

	for (i=0; i<500 /* MAX LOOP */; i++) {
		guessed_offtraf = (offtraf_upper + offtraf_lower)/2.0;
		guessed_cartraf = guess_carried_traffic(
					guessed_offtraf, 
					calculate_gos (guessed_offtraf, 
							circuits));
		cartraf_diff = guessed_cartraf - traffic;
		if (cartraf_diff > MOVING_FACTOR) 
			offtraf_upper = guessed_offtraf;
		else if (cartraf_diff < -(MOVING_FACTOR))
			offtraf_lower = guessed_offtraf;
		else
			return value_new_float (guessed_offtraf);
	}

	/* should not be reached */
	return value_new_float (-1);
}

static char const *help_dimcirc = {
	N_("@FUNCTION=DIMCIRC\n"
	   "@SYNTAX=DIMCIRC(traffic,gos)\n"

	   "@DESCRIPTION="
	   "DIMCIRC returns a number of circuits required from "
	   "a number of @traffic loads with @gos grade of service.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DIMCIRC(24, 1%) returns '35'.\n"
	   "\n"
	   "@SEEALSO=OFFCAP, OFFTRAF, PROBBLOCK")
};

static GnmValue *
gnumeric_dimcirc (FunctionEvalInfo *ei, GnmValue **argv)
{
	gnm_float circuits = 1.0;
	gnm_float gos      = 1.0;

	
	gnm_float traffic, des_gos;

	traffic = value_get_as_float(argv[0]);
	des_gos = value_get_as_float(argv[1]);

	/* What about <0 ? */
	if (des_gos > 1)
		return value_new_error_VALUE (ei->pos);

	/* insanity should not pass thru */
	if (traffic > ERLANG_LIMIT)
		return value_new_float (-1);

	while (gos > des_gos) {
		circuits++;
		gos = (traffic * gos) / (circuits + (traffic * gos));
	}

	return value_new_float (circuits);
}

static char const *help_offcap = {
	N_("@FUNCTION=OFFCAP\n"
	   "@SYNTAX=OFFCAP(circuits,gos)\n"

	   "@DESCRIPTION="
	   "OFFCAP returns a number of traffic capacity given by "
	   "a number of @circuits with @gos grade of service.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OFFCAP(30, 1%) returns '20.337'.\n"
	   "\n"
	   "@SEEALSO=DIMCIRC, OFFTRAF, PROBBLOCK")
};

static GnmValue *
gnumeric_offcap(FunctionEvalInfo *ei, GnmValue **argv)
{
	int    first_fac = 0;
	gnm_float inc_fac = 0.0;
	gnm_float traffic = 0.0;
	gnm_float oldtraf = 0.0;
	gnm_float circuits, des_gos;

	circuits = value_get_as_float(argv[0]);
	des_gos  = value_get_as_float(argv[1]);

	/* What about <0 ? */
	if (des_gos > 1)
		return value_new_error_VALUE (ei->pos);

	/* again, checks for crazy values here */
	if (circuits > ERLANG_LIMIT)
		return value_new_float (-1);
	
	first_fac = circuits/2;
	if (first_fac % 2) first_fac+=1.0;
	inc_fac = (gnm_float) first_fac;

	while (inc_fac > MOVING_FACTOR) {
		traffic+=inc_fac;
		if(calculate_gos (traffic, circuits) > des_gos)
			traffic = oldtraf;
		inc_fac /= 2;
		oldtraf  = traffic;
	}
	return value_new_float (traffic);
}

GnmFuncDescriptor const erlang_functions[] = {
	{ "probblock",        "ff",  N_("traffic,circuits"), &help_probblock,
	  gnumeric_probblock, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "offtraf",        "ff",  N_("traffic,circuits"), &help_offtraf,
	  gnumeric_offtraf, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "dimcirc",        "ff",  N_("traffic,gos"), &help_dimcirc,
	  gnumeric_dimcirc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "offcap",        "ff",  N_("circuits,gos"), &help_offcap,
	  gnumeric_offcap, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        {NULL}
};
