/*
 * Options pricing
 *
 * Authors:
 *   Elliot Lee <sopwith@redhat.com>     All initial work.
 *   Morten Welinder <terra@diku.dk>     Port to new plugin framework.
 *                                         Cleanup.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>
#include <math.h>

#include "func.h"
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
	return dnorm (x, 0, 1);
}

static gnum_float
calc_N (gnum_float x)
{
	return pnorm (x, 0, 1);
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
	return (log (S / X) + r * t) / (stddev * sqrt (t))
		+ stddev * sqrt (t) / 2.0;
}

static gnum_float
calc_d2 (gnum_float X, gnum_float S, gnum_float stddev, gnum_float t, gnum_float r)
{
	return calc_d1 (X, S, stddev, t, r) - stddev * sqrt (t);
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
	C = S * calc_N (d1) - X * exp (-r * t) * calc_N (d2);

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
	P = -S * calc_N (-d1) + X * exp (-r * t) * calc_N (-d2);

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
	theta = -(S * stddev * calc_Np (d1)) / (2.0 * sqrt (t))
		-(r * X * calc_N (d2) * exp (-r * t));
  
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
	return value_new_float (t * X * exp (-r * t) * calc_N (d2));
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
	theta = - (S * stddev * calc_Np (d1)) / (2.0 * sqrt (t)) + r * X * exp (-r * t) * calc_N (-d2);
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
	return value_new_float (-t * X * exp (-r * t) * calc_N (-d2));
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
	return value_new_float (calc_Np (d1) / (S * stddev * sqrt (t)));
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
	return value_new_float (S * sqrt (t) * calc_Np (d1));
}


ModulePluginFunctionInfo derivatives_functions[] = {
	{"opt_bs_call",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_call},

	{"opt_bs_call_delta",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_call_delta},

	{"opt_bs_call_rho",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_call_rho},

	{"opt_bs_call_theta",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_call_theta},

	{"opt_bs_put",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_put},

	{"opt_bs_put_delta",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_put_delta},

	{"opt_bs_put_rho",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_put_rho},

	{"opt_bs_put_theta",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_put_theta},

	{"opt_bs_gamma",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_gamma},

	{"opt_bs_vega",
	 "fffff", "strike_price, market_price, stddev, days_to_maturity, riskfree",
	 NULL, func_opt_bs_vega},

	{NULL}
};
