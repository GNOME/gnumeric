/*
 * Options pricing
 *
 * Authors:
 *   Elliot Lee <sopwith@redhat.com>     All initial work.
 *   Morten Welinder <terra@diku.dk>     Port to new plugin framework.
 *                                         Cleanup.
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <glib.h>
#include <math.h>

#include "func.h"
#include "numbers.h"
#include "mathfunc.h"
#include "plugin.h"
#include "value.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/* Equity option pricing using the Black-Scholes model (European-style options) */
static gnum_float
calc_Np (gnum_float x)
{
	return dnorm (x, 0, 1, FALSE);
}

static gnum_float
calc_N (gnum_float x)
{
	return pnorm (x, 0, 1, TRUE, FALSE);
}

/*
 * Requires.
 *   t > 0.
 *   stddev > 0.
 *   X > 0.
 *   S > 0.
 */
static gnum_float
calc_d1 (gnum_float X, gnum_float S, gnum_float stddev, gnum_float t, gnum_float r)
{
	return (loggnum (S / X) + r * t) / (stddev * sqrtgnum (t))
		+ stddev * sqrtgnum (t) / 2.0;
}

static gnum_float
calc_d2 (gnum_float X, gnum_float S, gnum_float stddev, gnum_float t, gnum_float r)
{
	return calc_d1 (X, S, stddev, t, r) - stddev * sqrtgnum (t);
}

static Value *
func_opt_bs_call (FunctionEvalInfo *ei, Value *argv [])
{
	gnum_float S, X, stddev, t, r;

	gnum_float d1, d2, C;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d1 = calc_d1 (X, S, stddev, t, r);
	d2 = calc_d2 (X, S, stddev, t, r);
	C = S * calc_N (d1) - X * expgnum (-r * t) * calc_N (d2);

	return value_new_float (C);
}

static Value *
func_opt_bs_put (FunctionEvalInfo *ei, Value *argv [])
{
	gnum_float X, S, stddev, t, r;

	gnum_float d1, d2, P;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d1 = calc_d1 (X, S, stddev, t, r);
	d2 = calc_d2 (X, S, stddev, t, r);
	P = -S * calc_N (-d1) + X * expgnum (-r * t) * calc_N (-d2);

	return value_new_float (P);
}

static Value *
func_opt_bs_call_delta (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (calc_N (calc_d1 (X, S, stddev, t, r)));
}

static Value *
func_opt_bs_call_theta (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r;
	gnum_float d1, d2, theta;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d1 = calc_d1 (X, S, stddev, t, r);
	d2 = calc_d2 (X, S, stddev, t, r);
	theta = -(S * stddev * calc_Np (d1)) / (2.0 * sqrtgnum (t))
		-(r * X * calc_N (d2) * expgnum (-r * t));

	return value_new_float (theta);
}

static Value *
func_opt_bs_call_rho (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r, d2;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d2 = calc_d2 (X, S, stddev, t, r);
	return value_new_float (t * X * expgnum (-r * t) * calc_N (d2));
}

static Value *
func_opt_bs_put_delta (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (calc_N (calc_d1 (X, S, stddev, t, r)) - 1);
}

static Value *
func_opt_bs_put_theta (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r;

	gnum_float d1, d2, theta;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d1 = calc_d1 (X, S, stddev, t, r);
	d2 = calc_d2 (X, S, stddev, t, r);
	theta = - (S * stddev * calc_Np (d1)) / (2.0 * sqrtgnum (t)) + r * X * expgnum (-r * t) * calc_N (-d2);
	return value_new_float (theta);
}

static Value *
func_opt_bs_put_rho (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r;

	gnum_float d2;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d2 = calc_d2 (X, S, stddev, t, r);
	return value_new_float (-t * X * expgnum (-r * t) * calc_N (-d2));
}

static Value *
func_opt_bs_gamma (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r;

	gnum_float d1;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d1 = calc_d1 (X, S, stddev, t, r);
	return value_new_float (calc_Np (d1) / (S * stddev * sqrtgnum (t)));
}

static Value *
func_opt_bs_vega (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float X, S, stddev, t, r;
	gnum_float d1;

	X = value_get_as_float (argv[0]);
	S = value_get_as_float (argv[1]);
	stddev = value_get_as_float (argv[2]);
	t = value_get_as_float (argv[3]) / 365.25;
	r = value_get_as_float (argv[4]);

	if (t <= 0 ||
	    stddev <= 0 ||
	    X <= 0 ||
	    S <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	d1 = calc_d1 (X, S, stddev, t, r);
	return value_new_float (S * sqrtgnum (t) * calc_Np (d1));
}

static const char *help_opt_bs_call = {
	N_("@FUNCTION=OPT_BS_CALL\n"
	   "@SYNTAX=OPT_BS_CALL(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_CALL uses the Black-Scholes model to calculate the price of "
	   "a European call option struck at @strike on an asset with price "
	   "@price.\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of "
	   "the asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed in the same units as "
	   "@strike and @price.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_PUT, OPT_BS_CALL_DELTA, OPT_BS_PUT_DELTA, "
	   "OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, OPT_BS_CALL_THETA, "
	   "OPT_BS_PUT_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_put = {
	N_("@FUNCTION=OPT_BS_PUT\n"
	   "@SYNTAX=OPT_BS_PUT(strike,price,volatility,days_to_maturity,rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_PUT uses the Black-Scholes model to calculate the price of "
	   "a European put option struck at @strike on an asset with price "
	   "@price.\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of "
	   "the asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed in the same units as "
	   "@strike and @price.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_CALL_DELTA, OPT_BS_PUT_DELTA, "
	   "OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, OPT_BS_CALL_THETA, "
	   "OPT_BS_PUT_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_call_delta = {
	N_("@FUNCTION=OPT_BS_CALL_DELTA\n"
	   "@SYNTAX=OPT_BS_CALL_DELTA(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_CALL_DELTA uses the Black-Scholes model to calculate the "
	   "\"delta\" of a European call option struck at @strike on an "
	   "asset with price @price.\n"
	   "\n"
	   "(The delta of an option is the rate of change of its price with "
	   "respect to the price of the underlying asset.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change of "
	   "option value per unit change in @price."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_PUT_DELTA, "
	   "OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, OPT_BS_CALL_THETA, "
	   "OPT_BS_PUT_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_put_delta = {
	N_("@FUNCTION=OPT_BS_PUT_DELTA\n"
	   "@SYNTAX=OPT_BS_PUT_DELTA(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_PUT_DELTA uses the Black-Scholes model to calculate the "
	   "\"delta\" of a European put option struck at @strike on an asset "
	   "with price @price.\n"
	   "\n"
	   "(The delta of an option is the rate of change of its price with "
	   "respect to the price of the underlying asset.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change of "
	   "option value per unit change in @price."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_CALL_DELTA, "
	   "OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, OPT_BS_CALL_THETA, "
	   "OPT_BS_PUT_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_call_rho = {
        /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_CALL_RHO\n"
	   "@SYNTAX=OPT_BS_CALL_RHO(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_CALL_RHO uses the Black-Scholes model to calculate the "
	   "\"rho\" of a European call option struck at @strike on an asset "
	   "with price @price.\n"
	   "\n"
	   "(The rho of an option is the rate of change of its price with "
	   "respect to the risk free interest rate.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change of "
	   "option value, per 100% change in @rate."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_CALL_DELTA, "
	   "OPT_BS_PUT_DELTA, OPT_BS_PUT_RHO, OPT_BS_CALL_THETA, "
	   "OPT_BS_PUT_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_put_rho = {
        /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_PUT_RHO\n"
	   "@SYNTAX=OPT_BS_PUT_RHO(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_PUT_RHO uses the Black-Scholes model to calculate the "
	   "\"rho\" of a European put option struck at @strike on an asset "
	   "with price @price.\n"
	   "\n"
	   "(The rho of an option is the rate of change of its price with "
	   "respect to the risk free interest rate.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change of "
	   "option value, per 100% change in @rate."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_CALL_DELTA, "
	   "OPT_BS_PUT_DELTA, OPT_BS_CALL_RHO, OPT_BS_CALL_THETA, "
	   "OPT_BS_PUT_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_call_theta = {
	N_("@FUNCTION=OPT_BS_CALL_THETA\n"
	   "@SYNTAX=OPT_BS_CALL_THETA(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_CALL_THETA uses the Black-Scholes model to calculate the "
	   "\"theta\" of a European call option struck at @strike on an asset "
	   "with price @price.\n"
	   "\n"
	   "(The theta of an option is the rate of change of its price with "
	   "respect to time to expiry.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as minus the rate of change "
	   "of option value, per 365.25 days."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_CALL_DELTA, "
	   "OPT_BS_PUT_DELTA, OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, "
	   "OPT_BS_PUT_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_put_theta = {
	N_("@FUNCTION=OPT_BS_PUT_THETA\n"
	   "@SYNTAX=OPT_BS_PUT_THETA(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_PUT_THETA uses the Black-Scholes model to calculate the "
	   "\"theta\" of a European put option struck at @strike on an asset "
	   "with price @price.\n"
	   "\n"
	   "(The theta of an option is the rate of change of its price with "
	   "respect to time to expiry.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as minus the rate of change "
	   "of option value, per 365.25 days."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_CALL_DELTA, "
	   "OPT_BS_PUT_DELTA, OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, "
	   "OPT_BS_CALL_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

static const char *help_opt_bs_gamma = {
	N_("@FUNCTION=OPT_BS_GAMMA\n"
	   "@SYNTAX=OPT_BS_GAMMA(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_GAMMA uses the Black-Scholes model to calculate the "
	   "\"gamma\" of a European option struck at @strike on an asset "
	   "with price @price.\n"
	   "\n"
	   "(The gamma of an option is the second derivative of its price "
	   "with respect to the price of the underlying asset, and is the "
	   "same for calls and puts.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of delta per unit change in @price."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_CALL_DELTA, "
	   "OPT_BS_PUT_DELTA, OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, "
	   "OPT_BS_CALL_THETA, OPT_BS_PUT_RHO, OPT_BS_VEGA")
};

static const char *help_opt_bs_vega = {
        /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_VEGA\n"
	   "@SYNTAX=OPT_BS_VEGA(strike,price,volatility,days_to_maturity,"
	   "rate)\n"
	   "@DESCRIPTION="
	   "OPT_BS_VEGA uses the Black-Scholes model to calculate the "
	   "\"vega\" of a European option struck at @strike on an asset "
	   "with price @price.\n"
	   "\n"
	   "(The vega of an option is the rate of change of its price with "
	   "respect to volatility, and is the same for calls and puts.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of option value, per 100% volatilty."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_CALL, OPT_BS_PUT, OPT_BS_CALL_DELTA, "
	   "OPT_BS_PUT_DELTA, OPT_BS_CALL_RHO, OPT_BS_PUT_RHO, "
	   "OPT_BS_CALL_THETA, OPT_BS_PUT_RHO, OPT_BS_GAMMA")
};

const ModulePluginFunctionInfo derivatives_functions[] = {
	{"opt_bs_call",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_call, func_opt_bs_call, NULL, NULL, NULL },

	{"opt_bs_put",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_put, func_opt_bs_put, NULL, NULL, NULL },

	{"opt_bs_call_delta",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_call_delta, func_opt_bs_call_delta, NULL, NULL, NULL },

	{"opt_bs_put_delta",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_put_delta, func_opt_bs_put_delta, NULL, NULL, NULL },

	{"opt_bs_call_rho",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_call_rho, func_opt_bs_call_rho, NULL, NULL, NULL },

	{"opt_bs_put_rho",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_put_rho, func_opt_bs_put_rho, NULL, NULL, NULL },

	{"opt_bs_call_theta",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_call_theta, func_opt_bs_call_theta, NULL, NULL, NULL },

	{"opt_bs_put_theta",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_put_theta, func_opt_bs_put_theta, NULL, NULL, NULL },

	{"opt_bs_gamma",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_gamma, func_opt_bs_gamma, NULL, NULL, NULL },

	{"opt_bs_vega",
	 "fffff", "strike, price, volatility, days_to_maturity, rate",
	 &help_opt_bs_vega, func_opt_bs_vega, NULL, NULL, NULL },

	{NULL}
};
