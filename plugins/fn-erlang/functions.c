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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>
#include <parse-util.h>
#include <cell.h>
#include <value.h>
#include <mathfunc.h>
#include <gnm-format.h>
#include <workbook.h>
#include <sheet.h>
#include <tools/goal-seek.h>
#include <gnm-i18n.h>

#include <goffice/goffice.h>
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

	circuits = gnm_floor (circuits);

	/* extra guards won't hurt, right? */
	if (circuits < 1 || traffic < 0)
		return -1;

	if (traffic == 0)
		gos = comp ? 1 : 0;
	else if (circuits < 100) {
		gnm_float cir_iter, ag, g = 1;

		// Run the recurrence B(n) = A*B(n-1) / (n + A*B(n-1)) up to
		// circuits-1 and then do the last step by hand.  Since
		// 1 - B(n) = n / (n + A*B(n-1)), that lets us get the
		// complement without the cancellation in 1 - B(n), which
		// is severe when the blocking probability is close to 1.
		for (cir_iter = 1; cir_iter < circuits; cir_iter++)
			g = (traffic * g) / (cir_iter + (traffic * g));
		ag = traffic * g;
		gos = (comp ? circuits : ag) / (circuits + ag);
	} else if (circuits / traffic < GNM_const(0.9)) {
		gnm_float sum = 0, term = 1, n = circuits;
		while (n > 0) {
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

/*
 * Average number of idle circuits, i.e., the number of circuits (rounded
 * down) minus the carried traffic A*(1-B).  Mathematically that is just a
 * difference, but when B is close to 1 the carried traffic is very close
 * to the number of circuits and the subtraction loses most of the
 * precision.  Here we compute the result without cancellation.
 *
 * Returns -1 for invalid arguments.
 */
static gnm_float
calculate_idle_circuits (gnm_float traffic, gnm_float circuits)
{
	circuits = gnm_floor (circuits);

	if (circuits < 1 || traffic < 0)
		return -1;

	if (traffic == 0)
		return circuits;

	if (circuits < 100) {
		// With I(n) = n - A*(1-B(n)) and B(n) = A*B(n-1) / (n + A*B(n-1))
		// we have I(n) = n * (I(n-1) + 1) / (n + A*B(n-1)), I(0) = 0.
		// All terms are positive and the recurrence is stable.
		gnm_float n, b = 1, idle = 0;
		for (n = 1; n <= circuits; n++) {
			gnm_float den = n + traffic * b;
			idle = n * (idle + 1) / den;
			b = traffic * b / den;
		}
		return idle;
	} else if (circuits / traffic < GNM_const(0.9)) {
		// With B = 1/(1+S) and S = sum_j term_j as in calculate_gos
		// we get I = (sum_j j * term_j) / (1+S).
		gnm_float s = 0, w = 0, term = 1, n = circuits, j = 0;
		while (n > 0) {
			term *= n / traffic;
			j++;
			if (j * term < GNM_EPSILON * w)
				break;
			s += term;
			w += j * term;
			n--;
		}
		return w / (1 + s);
	} else {
		gnm_float loggos = calculate_loggos (traffic, circuits);
		gnm_float carried = traffic * -gnm_expm1 (loggos);
		return MAX (0, circuits - carried);
	}
}

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/
static GnmFuncHelp const help_probblock[] = {
	{ GNM_FUNC_HELP_NAME, F_("PROBBLOCK:probability of blocking")},
	{ GNM_FUNC_HELP_ARG, F_("traffic:number of calls")},
	{ GNM_FUNC_HELP_ARG, F_("circuits:number of circuits")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PROBBLOCK returns probability of blocking when @{traffic}"
					" calls load into @{circuits} circuits.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=PROBBLOCK(24,30)" },
	{ GNM_FUNC_HELP_SEEALSO, "OFFTRAF,DIMCIRC,OFFCAP"},
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
	{ GNM_FUNC_HELP_NAME, F_("OFFTRAF:predicted number of offered calls")},
	{ GNM_FUNC_HELP_ARG, F_("traffic:number of carried calls")},
	{ GNM_FUNC_HELP_ARG, F_("circuits:number of circuits")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OFFTRAF returns the predicted number of offered calls given @{traffic} carried calls (taken from measurements) on @{circuits} circuits.")},
	{ GNM_FUNC_HELP_NOTE, F_("@{traffic} must be less than @{circuits}, rounded down to an integer.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=OFFTRAF(24,30)" },
	{ GNM_FUNC_HELP_SEEALSO, "PROBBLOCK,DIMCIRC,OFFCAP"},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	gnm_float traffic, circuits;
	// When the carried traffic is close to the number of circuits we
	// solve for the number of idle circuits instead.  This is the same
	// equation, but it is much better conditioned.
	gboolean use_idle;
	gnm_float target_idle;
} gnumeric_offtraf_t;

static GnmGoalSeekStatus
gnumeric_offtraf_f (gnm_float off_traffic, gnm_float *y, void *user_data)
{
	gnumeric_offtraf_t *pudata = user_data;

	if (pudata->use_idle) {
		gnm_float idle = calculate_idle_circuits (off_traffic, pudata->circuits);
		if (!(idle >= 0))
			return GOAL_SEEK_ERROR;
		// carried - traffic == (circuits - traffic) - idle
		*y = pudata->target_idle - idle;
	} else {
		gnm_float comp_gos = calculate_gos (off_traffic, pudata->circuits, TRUE);
		if (!(comp_gos >= 0))
			return GOAL_SEEK_ERROR;
		*y = guess_carried_traffic (off_traffic, comp_gos) - pudata->traffic;
	}
	return GOAL_SEEK_OK;
}

static GnmValue *
gnumeric_offtraf (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float traffic = value_get_as_float (argv[0]);
	gnm_float circuits = value_get_as_float (argv[1]);
	gnm_float upper;
	GnmGoalSeekData data;
	GnmGoalSeekStatus status;
	gnumeric_offtraf_t udata;

	/*
	 * Physical limit: Carried traffic cannot exceed or equal circuit
	 * count.  Fractional circuits are not used, see calculate_gos.
	 */
	if (!(circuits >= 1) || !(traffic >= 0) ||
	    !(traffic < gnm_floor (circuits)))
		return value_new_error_VALUE (ei->pos);

	goal_seek_initialize (&data);
	data.xmin = traffic;
	// Carried traffic approaches the circuit count only slowly as
	// offered traffic grows, so the default limit of 1e10 is too low.
	data.xmax = GNM_MAX / 2;
	udata.circuits = circuits;
	udata.traffic = traffic;
	udata.use_idle = (traffic >= gnm_floor (circuits) / 2);
	udata.target_idle = gnm_floor (circuits) - traffic;

	(void)goal_seek_point (&gnumeric_offtraf_f, &data, &udata, traffic);
	upper = MAX (circuits, traffic * 2);
	while (upper < data.xmax && !data.havexpos && !data.have_root) {
		(void)goal_seek_point (&gnumeric_offtraf_f, &data, &udata, upper);
		if (data.havexpos) break;
		upper *= 2;
		if (upper > GNM_MAX / 2) break;
	}

	status = goal_seek_bisection (&gnumeric_offtraf_f, &data, &udata);

	if (status == GOAL_SEEK_OK)
		return value_new_float (data.root);
	else
		return value_new_error_VALUE (ei->pos);
}

static GnmFuncHelp const help_dimcirc[] = {
	{ GNM_FUNC_HELP_NAME, F_("DIMCIRC:number of circuits required")},
	{ GNM_FUNC_HELP_ARG, F_("traffic:number of calls")},
	{ GNM_FUNC_HELP_ARG, F_("gos:grade of service")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DIMCIRC returns the number of circuits required given @{traffic} calls with grade of service @{gos}.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=DIMCIRC(24,0.01)" },
	{ GNM_FUNC_HELP_SEEALSO, "OFFCAP,OFFTRAF,PROBBLOCK"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dimcirc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float traffic  = value_get_as_float (argv[0]);
	gnm_float des_gos  = value_get_as_float (argv[1]);
	gnm_float low, high;

	if (!(des_gos > 0 && des_gos <= 1) || !(traffic >= 0))
		return value_new_error_VALUE (ei->pos);

	low = high = 1;
	while (calculate_gos (traffic, high, FALSE) > des_gos) {
		low = high;
		if (high >= GNM_MAX / 2)
			return value_new_error_NUM (ei->pos);
		high += high;
	}

	while (high - low > GNM_const(1.5)) {
		gnm_float mid = gnm_floor ((high + low) / 2 + GNM_const(0.1));
		gnm_float gos;

		// Beyond 2^53 we cannot bisect integers any more and the
		// midpoint will round to one of the end points.
		if (!(mid > low && mid < high))
			break;

		gos = calculate_gos (traffic, mid, FALSE);
		if (gos > des_gos)
			low = mid;
		else
			high = mid;
	}

	return value_new_float (high);
}

static GnmFuncHelp const help_offcap[] = {
	{ GNM_FUNC_HELP_NAME, F_("OFFCAP:traffic capacity")},
	{ GNM_FUNC_HELP_ARG, F_("circuits:number of circuits")},
	{ GNM_FUNC_HELP_ARG, F_("gos:grade of service")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OFFCAP returns the traffic capacity given @{circuits} circuits with grade of service @{gos}.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=OFFCAP(30,0.01)" },
	{ GNM_FUNC_HELP_SEEALSO, "DIMCIRC,OFFTRAF,PROBBLOCK"},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	gnm_float circuits, des_gos;
} gnumeric_offcap_t;

static GnmGoalSeekStatus
gnumeric_offcap_f (gnm_float traffic, gnm_float *y, void *user_data)
{
	gnumeric_offcap_t *pudata = user_data;
	gnm_float gos = calculate_gos (traffic, pudata->circuits, FALSE);
	if (!(gos >= 0))
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
	GnmGoalSeekData data;
	GnmGoalSeekStatus status;
	gnumeric_offcap_t udata;

	if (!(des_gos > 0 && des_gos < 1) || !(circuits >= 1))
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
	{ "probblock",        "ff",   help_probblock,
	  gnumeric_probblock, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "offtraf",        "ff",   help_offtraf,
	  gnumeric_offtraf, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "dimcirc",        "ff",   help_dimcirc,
	  gnumeric_dimcirc, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "offcap",        "ff",   help_offcap,
	  gnumeric_offcap, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        {NULL}
};
