/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-erlang.c:  Teletraffic functions.
 *
 * Authors:
 *   Arief Mulya Utama <arief_m_utama@telkomsel.co.id>
 *                     <arief.utama@gmail.com>
 *   [Initial plugin]
 *
 * Morten Welinder <terra@gnome.org>
 *   [calculate_loggos]
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
#include <parse-util.h>
#include <str.h>
#include <cell.h>
#include <value.h>
#include <mathfunc.h>
#include <gnm-format.h>
#include <workbook.h>
#include <sheet.h>
#include <tools/goal-seek.h>
#include <gnm-i18n.h>

#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

#include <math.h>
#include <string.h>
#include <stdlib.h>

/*
 * comp_gos == 1 - gos
 */
static gnm_float
guess_carried_traffic (gnm_float traffic, gnm_float comp_gos)
{
	return traffic * comp_gos;
}

static gnm_float
calculate_loggos (gnm_float traffic, gnm_float circuits)
{
	if (traffic < 0 || circuits < 1)
		return gnm_nan;

	return (dgamma (traffic, circuits + 1, 1, TRUE) -
		pgamma (traffic, circuits + 1, 1, FALSE, TRUE));
}

static gnm_float
calculate_gos (gnm_float traffic, gnm_float circuits, gboolean comp)
{
	gnm_float gos;

	/* extra guards wont hurt, right? */
	if (circuits < 1 || traffic < 0)
		return -1;

	if (traffic == 0)
		gos = comp ? 1 : 0;
	else if (circuits < 100) {
		gnm_float cir_iter = 1;
		gos = 1;
		for (cir_iter = 1; cir_iter <= circuits; cir_iter++)
			gos = (traffic * gos) / (cir_iter + (traffic * gos));
		if (comp) gos = 1 - gos;
	} else if (circuits / traffic < 0.9) {
		gnm_float sum = 0, term = 1, n = circuits;
		while (n > 1) {
			term *= n / traffic;
			if (term < GNM_EPSILON * sum)
				break;
			sum += term;
			n--;
		}
		gos = comp ? sum / (1 + sum) : 1 / (1 + sum);
	} else {
		gnm_float loggos = calculate_loggos (traffic, circuits);
		gos = comp ? -gnm_expm1 (loggos) : gnm_exp (loggos);
	}

	return gos;
}

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/
static GnmFuncHelp const help_probblock[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PROBBLOCK\n"
	   "@SYNTAX=PROBBLOCK(traffic,circuits)\n"

	   "@DESCRIPTION="
	   "PROBBLOCK returns probability of blocking when a number "
	   "of @traffic loads into a number of @circuits (servers).\n"
	   "\n"
	   "* @traffic cannot exceed @circuits\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "PROBBLOCK(24,30) returns 0.4012.\n"
	   "\n"
	   "@SEEALSO=OFFTRAF, DIMCIRC, OFFCAP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_probblock (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float traffic  = value_get_as_float (argv[0]);
	gnm_float circuits = value_get_as_float (argv[1]);
	gnm_float gos = calculate_gos (traffic, circuits, FALSE);

	if (gos >= 0)
		return value_new_float (gos);
	else
		return value_new_error_VALUE (ei->pos);
}

static GnmFuncHelp const help_offtraf[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=OFFTRAF\n"
	   "@SYNTAX=OFFTRAF(traffic,circuits)\n"

	   "@DESCRIPTION="
	   "OFFTRAF returns a predicted number of offered traffic "
	   "from a number of carried @traffic (taken from measurements) "
	   "on a number of @circuits.\n"
	   "\n"
	   "* @traffic cannot exceed @circuits\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OFFTRAF(24,30) returns 25.527.\n"
	   "\n"
	   "@SEEALSO=PROBBLOCK, DIMCIRC, OFFCAP")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	gnm_float traffic, circuits;
} gnumeric_offtraf_t;

static GoalSeekStatus
gnumeric_offtraf_f (gnm_float off_traffic, gnm_float *y, void *user_data)
{
	gnumeric_offtraf_t *pudata = user_data;
	gnm_float comp_gos = calculate_gos (off_traffic, pudata->circuits, TRUE);
	if (comp_gos < 0)
		return GOAL_SEEK_ERROR;
	*y = guess_carried_traffic (off_traffic, comp_gos) - pudata->traffic;
	return GOAL_SEEK_OK;
}

static GnmValue *
gnumeric_offtraf (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float traffic = value_get_as_float (argv[0]);
	gnm_float circuits = value_get_as_float (argv[1]);
	gnm_float traffic0;
	GoalSeekData data;
	GoalSeekStatus status;
	gnumeric_offtraf_t udata;

	if (circuits < 1 || traffic < 0)
		return value_new_error_VALUE (ei->pos);

	goal_seek_initialize (&data);
	data.xmin = traffic;
	data.xmax = circuits;
	udata.circuits = circuits;
	udata.traffic = traffic;
	traffic0 = (data.xmin + data.xmax) / 2;
	/* Newton search from guess.  */
	status = goal_seek_newton (&gnumeric_offtraf_f, NULL,
				   &data, &udata, traffic0);
	if (status != GOAL_SEEK_OK) {
		(void)goal_seek_point (&gnumeric_offtraf_f, &data, &udata, traffic);
		(void)goal_seek_point (&gnumeric_offtraf_f, &data, &udata, circuits);
		status = goal_seek_bisection (&gnumeric_offtraf_f, &data, &udata);
	}

	if (status == GOAL_SEEK_OK)
		return value_new_float (data.root);
	else
		return value_new_error_VALUE (ei->pos);
}

static GnmFuncHelp const help_dimcirc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DIMCIRC\n"
	   "@SYNTAX=DIMCIRC(traffic,gos)\n"

	   "@DESCRIPTION="
	   "DIMCIRC returns a number of circuits required from "
	   "a number of @traffic loads with @gos grade of service.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DIMCIRC(24,1%) returns 35.\n"
	   "\n"
	   "@SEEALSO=OFFCAP, OFFTRAF, PROBBLOCK")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dimcirc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float traffic  = value_get_as_float (argv[0]);
	gnm_float des_gos  = value_get_as_float (argv[1]);
	gnm_float low, high;

	if (des_gos > 1 || des_gos <= 0)
		return value_new_error_VALUE (ei->pos);

	low = high = 1;
	while (calculate_gos (traffic, high, FALSE) > des_gos) {
		low = high;
		high += high;
	}

	while (high - low > 1.5) {
		gnm_float mid = gnm_floor ((high + low) / 2 + 0.1);
		gnm_float gos = calculate_gos (traffic, mid, FALSE);
		if (gos > des_gos)
			low = mid;
		else
			high = mid;
	}

	return value_new_float (high);
}

static GnmFuncHelp const help_offcap[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=OFFCAP\n"
	   "@SYNTAX=OFFCAP(circuits,gos)\n"

	   "@DESCRIPTION="
	   "OFFCAP returns a number of traffic capacity given by "
	   "a number of @circuits with @gos grade of service.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OFFCAP(30,1%) returns 20.337.\n"
	   "\n"
	   "@SEEALSO=DIMCIRC, OFFTRAF, PROBBLOCK")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	gnm_float circuits, des_gos;
} gnumeric_offcap_t;

static GoalSeekStatus
gnumeric_offcap_f (gnm_float traffic, gnm_float *y, void *user_data)
{
	gnumeric_offcap_t *pudata = user_data;
	gnm_float gos = calculate_gos (traffic, pudata->circuits, FALSE);
	if (gos < 0)
		return GOAL_SEEK_ERROR;
	*y = gos - pudata->des_gos;
	return GOAL_SEEK_OK;
}

static GnmValue *
gnumeric_offcap (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float circuits = value_get_as_float (argv[0]);
	gnm_float des_gos  = value_get_as_float (argv[1]);
	gnm_float traffic0;
	GoalSeekData data;
	GoalSeekStatus status;
	gnumeric_offcap_t udata;

	if (des_gos >= 1 || des_gos <= 0)
		return value_new_error_VALUE (ei->pos);

	goal_seek_initialize (&data);
	data.xmin = 0;
	data.xmax = circuits / (1 - des_gos);
	udata.circuits = circuits;
	udata.des_gos = des_gos;

	traffic0 = data.xmax * (2 + des_gos * 10) / (3 + des_gos * 10);
	/* Newton search from guess.  */
	status = goal_seek_newton (&gnumeric_offcap_f, NULL,
				   &data, &udata, traffic0);
	if (status != GOAL_SEEK_OK) {
		(void)goal_seek_point (&gnumeric_offcap_f, &data, &udata, data.xmin);
		(void)goal_seek_point (&gnumeric_offcap_f, &data, &udata, data.xmax);
		status = goal_seek_bisection (&gnumeric_offcap_f, &data, &udata);
	}

	if (status == GOAL_SEEK_OK)
		return value_new_float (data.root);
	else
		return value_new_error_VALUE (ei->pos);
}

GnmFuncDescriptor const erlang_functions[] = {
	{ "probblock",        "ff",  N_("traffic,circuits"), help_probblock,
	  gnumeric_probblock, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "offtraf",        "ff",  N_("traffic,circuits"), help_offtraf,
	  gnumeric_offtraf, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "dimcirc",        "ff",  N_("traffic,gos"), help_dimcirc,
	  gnumeric_dimcirc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "offcap",        "ff",  N_("circuits,gos"), help_offcap,
	  gnumeric_offcap, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        {NULL}
};
