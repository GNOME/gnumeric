/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Options pricing
 *
 * Authors:
 *   Elliot Lee <sopwith@redhat.com> All initial work.
 *   Morten Welinder <terra@diku.dk> Port to new plugin framework.
 *                                         Cleanup.
 *   Hal Ashburner <hal_ashburner@yahoo.co.uk>
 *   Black Scholes Code re-structure, optional asset leakage paramaters,
 *   American approximations, alternative models to Black-Scholes
 *   and All exotic Options Functions.
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
#include "numbers.h"
#include "mathfunc.h"
#include "plugin.h"
#include "value.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

#include <glib/gi18n.h>
#include <math.h>
#include <string.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

typedef enum {
	OS_Call,
	OS_Put,
	OS_Error
} OptionSide;

typedef enum{
	OT_Euro,
	OT_Amer,
	OT_Error
} OptionType;

static gnm_float opt_baw_call	   (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float b, gnm_float v);
static gnm_float opt_baw_put	   (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float b, gnm_float v);
static gnm_float NRA_c		   (gnm_float x, gnm_float  t, gnm_float r, gnm_float b, gnm_float v);
static gnm_float NRA_p		   (gnm_float x, gnm_float t, gnm_float r, gnm_float b, gnm_float v);
static gnm_float opt_bjer_stens1_c  (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float b, gnm_float v);
/* static gnm_float opt_bjer_stens1_p (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float b, gnm_float v); */
static gnm_float phi		   (gnm_float s, gnm_float t, gnm_float gamma, gnm_float H, gnm_float I, gnm_float r, gnm_float b, gnm_float v);
static gnm_float CriticalValueOptionsOnOptions (OptionSide side, gnm_float x1, gnm_float x2, gnm_float t,
						gnm_float r, gnm_float b, gnm_float v);
static gnm_float opt_crit_val_chooser (gnm_float s,gnm_float xc,gnm_float xp,gnm_float t,
				       gnm_float tc, gnm_float tp, gnm_float r, gnm_float b, gnm_float v);


static OptionSide
option_side (const char *s)
{
	if (s[0] == 'p' || s[0] == 'P')
		return OS_Put;
	else if (s[0] == 'c' || s[0] == 'C')
		return OS_Call;
	else
		return OS_Error;
}

static OptionType
option_type (const char *s)
{
	if (s[0] == 'a' || s[0] == 'A')
		return OT_Amer;
	else if (s[0] == 'e' || s[0] == 'E')
		return OT_Euro;
	else
		return OT_Error;
}
/* The normal distribution function */
#define cum_norm_dist(x) pnorm (x, 0.0, 1.0, TRUE, FALSE)
#define n_d(x) dnorm (x, 0.0, 1.0, FALSE)
static int
Sgn (gnm_float a)
{
	if (a>0) return 1;
	else if (a<0) return -1;
	else return 0;
}

/* The cumulative bivariate normal distribution function */
static gnm_float
cum_biv_norm_dist1 (gnm_float a, gnm_float b, gnm_float rho)
{
	gnm_float rho1, rho2 , delta;
	gnm_float a1, b1, sum = 0.0;
	int i, j;

	static const gnm_float x[] = {0.24840615, 0.39233107, 0.21141819, 0.03324666, 0.00082485334};
	static const gnm_float y[] = {0.10024215, 0.48281397, 1.0609498, 1.7797294, 2.6697604};
	a1 = a / sqrtgnum (2.0 * (1 - (rho * rho)));
	b1 = b / sqrtgnum (2.0 * (1 - (rho * rho)));

	if (a <= 0 && b <= 0 && rho <= 0) {
		for (i = 0;i!=5;++i){
			for (j = 0; j!=5; ++j){
				sum = sum + x[i] * x[j] * expgnum (a1 * (2.0 * y[i] - a1) + b1 * (2.0 *
y[j] - b1) + 2.0 * rho * (y[i] - a1) * (y[j] - b1));
			}
		}
		return (sqrtgnum (1.0 - (rho * rho)) / M_PIgnum * sum);
	} else if (a <= 0 && b >= 0 && rho >= 0)
		return (cum_norm_dist (a) - cum_biv_norm_dist1 (a,-b,-rho));
	else if (a >= 0 && b <= 0 && rho >= 0)
		return (cum_norm_dist (b) - cum_biv_norm_dist1 (-a,b,-rho));
	else if (a >= 0 && b >= 0 && rho <= 0)
		return (cum_norm_dist (a) + cum_norm_dist (b) - 1.0 + cum_biv_norm_dist1 (-a,-b,rho));
	else if ((a * b * rho) > 0) {
		rho1 = (rho * a - b) * Sgn (a) / sqrtgnum ((a * a) - 2.0 * rho * a
							   * b + (b * b));
		rho2 = (rho * b - a) * Sgn (b) / sqrtgnum ((a * a) - 2.0 * rho * a
							   * b + (b * b));
		delta = (1.0 - Sgn (a) * Sgn (b)) / 4.0;
		return (cum_biv_norm_dist1 (a,0.0,rho1) + cum_biv_norm_dist1
			(b,0.0,rho2) - delta);
	}
	return -123;

}

static GnmValue *
cum_biv_norm_dist(FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);
	gnm_float rho = value_get_as_float (argv[2]);
	gnm_float result;

	result = cum_biv_norm_dist1 (a,b,rho);
	if (result == -123)
		return value_new_error_NUM (ei->pos);
	else
		return value_new_float (result);
}

static char const *help_cum_biv_norm_dist = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=CUM_BIV_NORM_DIST\n"

	   "@SYNTAX=CUM_BIV_NORM_DIST(a,b,rho)\n"
	   "@DESCRIPTION="
	   "CUM_BIV_NORM_DIST calculates the cumulative bivariate "
	   "normal distribution from parameters a, b & rho.\n"
	   "The return value is the probability that two random variables "
	   "with correlation @rho are respectively each less than @a and "
	   "@b.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=NORMDIST,NORMSDIST,NORMSINV")
};



/* the generalized Black and Scholes formula*/
static gnm_float
opt_bs1 (OptionSide side,
	 gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v,
	 gnm_float b)
{
	gnm_float d1;
	gnm_float d2;

	d1 = (loggnum (s / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	d2 = d1 - v * sqrtgnum (t);

	switch (side) {
	case OS_Call:
		return (s * expgnum ((b - r) * t) * cum_norm_dist (d1) -
			x * expgnum (-r * t) * cum_norm_dist (d2));
	case OS_Put:
		return (x * expgnum (-r * t) * cum_norm_dist (-d2) -
			s * expgnum ((b - r) * t) * cum_norm_dist (-d1));
	default:
		return -1;
	}
}


static GnmValue *
opt_bs (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0;
	gnm_float gfresult = opt_bs1 (call_put, s, x, t, r, v, b);
 	if (gfresult == -1)
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}

static char const *help_opt_bs = {
	N_("@FUNCTION=OPT_BS\n"
	   "@SYNTAX=OPT_BS(call_put_flag,spot,strike,time,rate,volatility [,"
	   "cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BS uses the Black-Scholes model to calculate the price of "
	   "a European option using call_put_flag, @call_put_flag, 'c' or 'p' "
	   "struck at "
	   "@strike on an asset with spot price @spot.\n"
	   "@time is the time to maturity of the option expressed in years.\n"
	   "@rate is the risk-free interest rate."
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the asset "
	   "for the period through to the exercise date. "
	   "\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."
	   "\n"
	   "* The returned value will be expressed in the same units as "
	   "@strike and @spot.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

/* Delta for the generalized Black and Scholes formula */
static gnm_float
opt_bs_delta1 (OptionSide side,
	       gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float d1 =
		(loggnum (s / x) + (b + (v * v) / 2.0) * t) /
		(v * sqrtgnum (t));

	switch (side) {
	case OS_Call:
		return expgnum ((b - r) * t) * cum_norm_dist (d1);

	case OS_Put:
		return expgnum ((b - r) * t) * (cum_norm_dist (d1) - 1.0);

	default:
		g_assert_not_reached ();
		return -123;
	}
}


static GnmValue *
opt_bs_delta (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0.0;
	gnm_float gfresult = opt_bs_delta1 (call_put, s, x, t, r, v, b);

	if (gfresult == -123)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gfresult);
}

static char const *help_opt_bs_delta = {
	N_("@FUNCTION=OPT_BS_DELTA\n"

	   "@SYNTAX=OPT_BS_DELTA(call_put_flag,spot,strike,time,rate,"
	   "volatility[,cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BS_DELTA uses the Black-Scholes model to calculate the "
	   "'delta' of a European option with call_put_flag, @call_put_flag, 'c' or 'p' "
	   "struck at "
	   "@strike on an asset with spot price @spot.\n"
	   "Where @time is the time to maturity of the option expressed in years.\n"
	   "@rate is the risk-free interest rate."
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the asset "
	   "for the period through to the exercise date. "
	   "\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."
	   "\n"
	   "* The returned value will be expressed in the same units as "
	   "@strike and @spot.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};


/* Gamma for the generalized Black and Scholes formula */
static gnm_float
opt_bs_gamma1 (gnm_float s,gnm_float x,gnm_float t,gnm_float r,gnm_float v,gnm_float b)
{
	gnm_float d1;

	d1 = (loggnum (s / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	return (expgnum ((b - r) * t) * n_d (d1) / (s * v * sqrtgnum (t)));
}


static GnmValue *
opt_bs_gamma (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float s = value_get_as_float (argv[0]);
	gnm_float x = value_get_as_float (argv[1]);
	gnm_float t = value_get_as_float (argv[2]);
	gnm_float r = value_get_as_float (argv[3]);
	gnm_float v = value_get_as_float (argv[4]);
	gnm_float b = argv[5] ? value_get_as_float (argv[5]) : 0.0;
	gnm_float gfresult = opt_bs_gamma1 (s,x,t,r,v,b);
	return value_new_float (gfresult);
}

static char const *help_opt_bs_gamma = {
	N_("@FUNCTION=OPT_BS_GAMMA\n"

	   "@SYNTAX=OPT_BS_GAMMA(spot,strike,time,rate,volatility[,"
	   "cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BS_GAMMA uses the Black-Scholes model to calculate the "
	   "'gamma' of a European option struck at @strike on an asset "
	   "with spot price @spot.\n"
	   "\n"
	   "(The gamma of an option is the second derivative of its price "
	   "with respect to the price of the underlying asset, and is the "
	   "same for calls and puts.)\n"
	   "\n"
	   "@time is the time to maturity of the option expressed in years.\n" 
	   "@rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."
	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of delta per unit change in @spot.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_VEGA")
};

/* theta for the generalized Black and Scholes formula */
static gnm_float
opt_bs_theta1 (OptionSide side,
	       gnm_float s,gnm_float x,gnm_float t,gnm_float r,gnm_float v,gnm_float b)
{
	gnm_float d1 = (loggnum (s / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	gnm_float d2 = d1 - v * sqrtgnum (t);

	switch (side) {
	case OS_Call:
		return -s * expgnum ((b - r) * t) * n_d (d1) * v / (2.0 * sqrtgnum (t)) -
			(b - r) * s * expgnum ((b - r) * t) * cum_norm_dist (d1) - r * x * expgnum (-r * t) * cum_norm_dist (d2);
	case OS_Put:
		return -s * expgnum ((b - r) * t) * n_d (d1) * v / (2.0 * sqrtgnum (t)) +
			(b - r) * s * expgnum ((b - r) * t) * cum_norm_dist (-d1) + r * x * expgnum (-r * t) * cum_norm_dist (-d2);
	default:
		return -123;
	}
}

static GnmValue *
opt_bs_theta (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0.0;
	gnm_float gfresult =
		opt_bs_theta1 (call_put, s, x, t, r, v, b);
	if (gfresult == -123)
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}

static char const *help_opt_bs_theta = {
	N_("@FUNCTION=OPT_BS_THETA\n"

	   "@SYNTAX=OPT_BS_THETA(call_put_flag,spot,strike,time,rate,"
	   "volatility[,cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BS_THETA uses the Black-Scholes model to calculate the "
	   "'theta' of a European option with call_put_flag, @call_put_flag "
	   "struck at @strike on an asset with spot price @spot.\n"
	   "\n"
	   "(The theta of an option is the rate of change of its price with "
	   "respect to time to expiry.)\n"
	   "\n"
	   "@time is the time to maturity of the option expressed in years\n" 
	   "and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."
	   "\n"
	   "* The returned value will be expressed as minus the rate of change "
	   "of option value, per 365.25 days.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_VEGA, OPT_BS_GAMMA")
};


/* Vega for the generalized Black and Scholes formula */
static gnm_float
opt_bs_vega1 (gnm_float s,gnm_float x,gnm_float t,gnm_float r,gnm_float v,gnm_float b)
{
	gnm_float d1;

	d1 = (loggnum (s / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	return (s * expgnum ((b - r) * t) * n_d (d1) * sqrtgnum (t));
}

static GnmValue *
opt_bs_vega (FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float s = value_get_as_float (argv[0]);
	gnm_float x = value_get_as_float (argv[1]);
	gnm_float t = value_get_as_float (argv[2]);
	gnm_float r = value_get_as_float (argv[3]);
	gnm_float v = value_get_as_float (argv[4]);
	gnm_float b = argv[5] ? value_get_as_float (argv[5]) : 0.0;

	return value_new_float (opt_bs_vega1 (s,x,t,r,v,b));
}

static char const *help_opt_bs_vega = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_VEGA\n"

	   "@SYNTAX=OPT_BS_VEGA(spot,strike,time,rate,volatility[,"
	   "cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BS_VEGA uses the Black-Scholes model to calculate the "
	   "'vega' of a European option struck at @strike on an asset "
	   "with spot price @spot.\n"
	   "(The vega of an option is the rate of change of its price with "
	   "respect to volatility, and is the same for calls and puts.)\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.\n "
	   "@time is the time to maturity of the option expressed in years.\n" 
	   "@rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of option value, per 100% volatility.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Rho for the generalized Black and Scholes formula */
static gnm_float
opt_bs_rho1 (OptionSide side, gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float d1 = (loggnum (s / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	gnm_float d2 = d1 - v * sqrtgnum (t);
	switch (side) {
	case OS_Call:
		if (b != 0)
			return t * x * expgnum (-r * t) * cum_norm_dist (d2);
		else
			return -t *  opt_bs1 (side, s, x, t, r, v, b);

	case OS_Put:
		if (b != 0)
			return -t * x * expgnum (-r * t) * cum_norm_dist (-d2);
		else
			return -t * opt_bs1 (side, s, x, t, r, v, b);

	default:
		return -123;
	}
}


static GnmValue *
opt_bs_rho (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0.0;
	gnm_float gfresult = opt_bs_rho1 (call_put, s, x, t, r, v, b);
	if (gfresult == -123)
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}

static char const *help_opt_bs_rho = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_RHO\n"

	   "@SYNTAX=OPT_BS_RHO(call_put_flag,spot,strike,time,rate,volatility[,"
	   "cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BS_RHO uses the Black-Scholes model to calculate the "
	   "'rho' of a European option with call_put_flag, @call_put_flag "
	   "struck at @strike on an asset with spot price @spot.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "\n"
	   "(The rho of an option is the rate of change of its price with "
	   "respect to the risk free interest rate.)\n"
	   
	   "@time is the time to maturity of the option expressed in years.\n" 
	   "@rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."
	   
	   "\n"
	   "* The returned value will be expressed as the rate of change of "
	   "option value, per 100% change in @rate.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

/* Carry for the generalized Black and Scholes formula */
static gnm_float
opt_bs_carrycost1 (OptionSide side, gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float d1 = (loggnum (s / x) + (b + (v * v) / 2.0) * t) /
		(v * sqrtgnum (t));

	switch (side) {
	case OS_Call:
		return t * s * expgnum ((b - r) * t) * cum_norm_dist (d1);
	case OS_Put:
		return -t * s * expgnum ((b - r) * t) * cum_norm_dist (-d1);
	default:
		return -123; /*should never get to here*/
	}
}

static GnmValue *
opt_bs_carrycost (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0.0;
	gnm_float gfresult = opt_bs_carrycost1 (call_put, s,x,t,r,v,b);
	if (gfresult == -123)
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}


static char const *help_opt_bs_carrycost = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_CARRYCOST\n"

	   "@SYNTAX=OPT_BS_CARRYCOST(call_put_flag,spot,strike,time,rate,"
	   "volatility[,cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BS_CARRYCOST uses the Black-Scholes model to calculate the "
	   "'elasticity' of a European option struck at @strike on an asset "
	   "with spot price @spot.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   
	   "\n"
	   "(The elasticity of an option is the rate of change of its price "
	   "with respect to its cost of carry.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@time is the time to maturity of the option expressed in years.\n" 
	   "@rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of option value, per 100% volatility.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Currency Options - Garman and Kohlhagen */
static gnm_float
opt_garman_kohlhagen1 (OptionSide side,
		       gnm_float s, gnm_float x, gnm_float t,
		       gnm_float r, gnm_float rf, gnm_float v)
{
	gnm_float d1 = (loggnum (s / x) + (r - rf + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	gnm_float d2 = d1 - v * sqrtgnum (t);
	switch (side) {
	case OS_Call:
		return s * expgnum (-rf * t) * cum_norm_dist (d1) - x * expgnum (-r * t) * cum_norm_dist (d2);
	case OS_Put:
		return x * expgnum (-r * t) * cum_norm_dist (-d2) - s * expgnum (-rf * t) * cum_norm_dist (-d1);
	default:
		return -123; /*should never get to here*/
	}
}

static GnmValue *
opt_garman_kohlhagen (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float rf = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);
	gnm_float gfresult;

	gfresult = opt_garman_kohlhagen1 (call_put, s, x, t, r, rf, v);
	if (gfresult == -123)
		return value_new_error_NUM (ei->pos);
	else
		return value_new_float (gfresult);
}

static char const *help_opt_garman_kohlhagen = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_GARMAN_KOHLHAGEN\n"

	   "@SYNTAX=OPT_GARMAN_KOHLHAGEN(call_put_flag,spot,strike,time,"
	   "domestic_rate,foreign_rate,volatility[,cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_GARMAN_KOHLHAGEN values the theoretical price of a European "
	   "currency option struck at @strike on an asset with spot price @spot.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date. \n"
	   "@time the number of days to exercise.\n"
	   "@domestic_rate is the domestic risk-free interest rate to the "
	   "exercise date.\n"
	   "@foreign_rate is the foreign risk-free interest rate "
	   "to the exercise date, in percent.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."

	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of option value, per 100% volatility.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* French (1984) adjusted Black and scholes model for trading day volatility */
static gnm_float
opt_french1 (OptionSide side, gnm_float s, gnm_float  x, gnm_float trading_t, gnm_float calander_t,
	     gnm_float r, gnm_float v, gnm_float  b)
{
	gnm_float d1 = (loggnum (s / x) + b * calander_t + ((v * v) / 2.0) * trading_t) / (v * sqrtgnum (trading_t));
	gnm_float d2 = d1 - v * sqrtgnum (trading_t);

	switch (side) {
	case OS_Call:
		return s * expgnum ((b - r) * calander_t) * cum_norm_dist (d1) - x * expgnum (-r * calander_t) * cum_norm_dist (d2);
	case OS_Put:
		return x * expgnum (-r * calander_t) * cum_norm_dist (-d2) - s * expgnum ((b - r) * calander_t) * cum_norm_dist (-d1);
	default:
		return -123;
	}
}


static GnmValue *
opt_french (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float t1 = value_get_as_float (argv[4]);
	gnm_float r = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);
	gnm_float b = value_get_as_float (argv[7]);
	gnm_float gfresult;

	gfresult = opt_french1 (call_put, s, x, t, t1, r, v, b);
	if (gfresult == -123)
		return value_new_error_NUM (ei->pos);
	else
		return value_new_float (gfresult);
}

static char const *help_opt_french = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_FRENCH\n"

	   "@SYNTAX=OPT_FRENCH(call_put_flag,spot,strike,time,t2,rate,volatility[,cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_FRENCH values the theoretical price of a "
	   "European option adjusted for trading day volatility, struck at "
	   "@strike on an asset with spot price @spot.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.\n "
	   "@time the number of calendar days to exercise divided by calendar days in the year.\n"
	   "@t2 is the number of trading days to exercise divided by trading days in the year.\n"
	   "@rate is the risk-free interest rate.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "to the exercise date, in percent.\n"
	   "For common stocks, this would be the dividend yield."
	   "\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

/* Merton jump diffusion model*/
static gnm_float
opt_jump_diff1 (OptionSide side, gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v,
	       gnm_float lambda, gnm_float gamma)
{
	gnm_float delta, sum;
	gnm_float Z, vi;
	int i;

	delta = sqrtgnum (gamma * (v * v) / lambda);
	Z = sqrtgnum ((v * v) - lambda * (delta * delta));
	sum = 0.0;
	for(i = 0; i != 11; ++i)
	{
		vi = sqrtgnum ((Z * Z) + (delta * delta) * (i / t));
		sum = sum + expgnum (-lambda * t) * powgnum (lambda * t, i) / fact(i) *
			opt_bs1 (side, s, x, t, r, vi, r);
	}
	return sum;
}

static GnmValue *
opt_jump_diff (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float lambda = value_get_as_float (argv[6]);
	gnm_float gamma = value_get_as_float (argv[7]);
	gnm_float gfresult =
		opt_jump_diff1 (call_put, s, x, t, r, v, lambda, gamma);
	return value_new_float (gfresult);
}

static char const *help_opt_jump_diff = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_JUMP_DIFF\n"

	   "@SYNTAX=OPT_JUMP_DIFF(call_put_flag,spot,strike,time,rate,"
	   "volatility,lambda,gamma)\n"
	   "@DESCRIPTION="
	   "OPT_JUMP_DIFF models the theoretical price of an option according "
	   "to the Jump Diffusion process (Merton)."
	   "\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike is the strike price of the option.\n"
	   "@time is the time to maturity of the option expressed in years.\n" 
	   "@rate is the annualized rate of interest.\n"
	   "@volatility is the annualized volatility of the underlying asset.\n"
	   "@lambda is expected number of 'jumps' per year.\n"
	   "@gamma is proportion of volatility explained by the 'jumps.'\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};



/* Miltersen schwartz (1997) commodity option model */
static gnm_float
opt_miltersen_schwartz1 (OptionSide side, gnm_float p_t, gnm_float f_t, gnm_float x, gnm_float t1,
			 gnm_float t2, gnm_float v_s, gnm_float v_e, gnm_float v_f, gnm_float rho_se,
			 gnm_float rho_sf, gnm_float rho_ef, gnm_float kappa_e, gnm_float kappa_f)
{
	gnm_float vz, vxz;
	gnm_float d1, d2;

	vz = (v_s * v_s) * t1 + 2.0 * v_s * (v_f * rho_sf * 1.0/ kappa_f * (t1 - 1.0/ kappa_f * expgnum (-kappa_f * t2) * (expgnum (kappa_f * t1) - 1.0))
					    - v_e * rho_se * 1.0/ kappa_e * (t1 - 1.0/ kappa_e * expgnum (-kappa_e * t2) * (expgnum (kappa_e * t1) - 1.0)))
		+ (v_e * v_e) * 1.0/ (kappa_e * kappa_e) * (t1 + 1.0/ (2.0 * kappa_e) * expgnum (-2 * kappa_e * t2) * (expgnum (2.0 * kappa_e * t1) - 1.0)
							 - 2.0 * 1.0/ kappa_e * expgnum (-kappa_e * t2) * (expgnum (kappa_e * t1) - 1.0))
		+ (v_f * v_f) * 1.0/ (kappa_f * kappa_f) * (t1 + 1.0/ (2.0 * kappa_f) * expgnum (-2.0 * kappa_f * t2) * (expgnum (2.0 * kappa_f * t1) - 1.0)
							 - 2.0 * 1.0/ kappa_f * expgnum (-kappa_f * t2) * (expgnum (kappa_f * t1) - 1.0))
		- 2.0 * v_e * v_f * rho_ef * 1.0/ kappa_e * 1.0/ kappa_f * (t1 - 1.0/ kappa_e * expgnum (-kappa_e * t2) * (expgnum (kappa_e * t1) - 1.0)
									- 1.0/ kappa_f * expgnum (-kappa_f * t2) * (expgnum (kappa_f * t1) - 1.0)
									+ 1.0/ (kappa_e + kappa_f) * expgnum (-(kappa_e + kappa_f) * t2) * (expgnum ((kappa_e + kappa_f) * t1) - 1.0));

	vxz = v_f * 1.0/ kappa_f * (v_s * rho_sf * (t1 - 1.0/ kappa_f * (1.0 - expgnum (-kappa_f * t1)))
				   + v_f * 1.0/ kappa_f * (t1 - 1.0/ kappa_f * expgnum (-kappa_f * t2) * (expgnum (kappa_f * t1) - 1.0) - 1.0/ kappa_f * (1 - expgnum (-kappa_f * t1))
							  + 1.0/ (2.0 * kappa_f) * expgnum (-kappa_f * t2) * (expgnum (kappa_f * t1) - expgnum (-kappa_f * t1)))
				   - v_e * rho_ef * 1.0/ kappa_e * (t1 - 1.0/ kappa_e * expgnum (-kappa_e * t2) * (expgnum (kappa_e * t1) - 1.0) - 1.0/ kappa_f * (1.0 - expgnum (-kappa_f * t1))
								   + 1.0/ (kappa_e + kappa_f) * expgnum (-kappa_e * t2) * (expgnum (kappa_e * t1) - expgnum (-kappa_f * t1))));

	vz = sqrtgnum (vz);

	d1 = (loggnum (f_t / x) - vxz + (vz * vz) / 2.0) / vz;
	d2 = (loggnum (f_t / x) - vxz - (vz * vz) / 2.0) / vz;

	switch (side) {
	case OS_Call:
		return p_t * (f_t * expgnum (-vxz) * cum_norm_dist (d1) - x * cum_norm_dist (d2));
	case OS_Put:
		return p_t * (x * cum_norm_dist (-d2) - f_t * expgnum (-vxz) * cum_norm_dist (-d1));
	default:
		return -123;
	}
}

static GnmValue *
opt_miltersen_schwartz (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float p_t = value_get_as_float (argv[1]);
	gnm_float f_t = value_get_as_float (argv[2]);
	gnm_float x = value_get_as_float (argv[3]);
	gnm_float t1 = value_get_as_float (argv[4]);
	gnm_float t2 = value_get_as_float (argv[5]);
	gnm_float v_s = value_get_as_float (argv[6]);
	gnm_float v_e = value_get_as_float (argv[7]);
	gnm_float v_f = value_get_as_float (argv[8]);
	gnm_float rho_se = value_get_as_float (argv[9]);
	gnm_float rho_sf = value_get_as_float (argv[10]);
	gnm_float rho_ef = value_get_as_float (argv[11]);
	gnm_float kappa_e = value_get_as_float (argv[12]);
	gnm_float kappa_f = value_get_as_float (argv[13]);

	gnm_float gfresult = 0;

	gfresult = opt_miltersen_schwartz1 (call_put, p_t, f_t, x, t1, t2, v_s, v_e, v_f, rho_se, rho_sf, rho_ef, kappa_e, kappa_f);
	if (gfresult != -123)
		return value_new_float (gfresult);
	return value_new_error_NUM (ei->pos);
}


static char const *help_opt_miltersen_schwartz = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_MILTERSEN_SCHWARTZ\n"

	   "@SYNTAX=OPT_MILTERSEN_SCHWARTZ(call_put_flag,p_t,f_t,x,t1,t2,v_s,"
	   "v_e,v_f,rho_se,rho_sf,rho_ef,kappa_e,kappa_f)\n"
	   "@DESCRIPTION="
	   "OPT_MILTERSEN_SCHWARTZ models the theoretical price of options on "
	   "commodities futures "
	   "according to Miltersen & Schwartz. \n"
	   
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@p_t is a zero coupon bond with expiry at option maturity.\n"
	   "@f_t is is the futures price.\n"
	   "@x is is the strike price.\n"
	   "@t1 is the time to maturity of the option.\n"
	   "@t2 is the time to maturity of the underlying commodity futures contract.\n"
	   "@v_s is the volatility of the spot commodity price.\n"
	   "@v_e is the volatility of the future convenience yield.\n"
	   "@v_f is the volatility of the forward rate of interest.\n"
	   "@rho_se is correlation between the spot commodity price and the convenience yield.\n"
	   "@rho_sf is correlation between the spot commodity price and the forward interest rate.\n"
	   "@rho_ef is correlation between the forward interest rate and the convenience yield.\n"
	   "@kappa_e is the speed of mean reversion of the convenience yield.\n"
	   "@kappa_f is the speed of mean reversion of the forward interest rate.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};






/* American options */


/* American Calls on stocks with known dividends, Roll-Geske-Whaley */
static gnm_float opt_rgw1 (gnm_float s, gnm_float x, gnm_float t1, gnm_float t2, gnm_float r, gnm_float d, gnm_float v)
	/*t1 time to dividend payout
	  t2 time to option expiration */
{
	gnm_float sx, i;
	gnm_float a1, a2, b1, b2;
	gnm_float HighS, LowS, epsilon;
	gnm_float ci, infinity;
	gnm_float gfresult;

	infinity = 100000000;
	epsilon = 0.00001;
	sx = s - d * expgnum (-r * t1);
	if (d <= (x * (1.0 - expgnum (-r * (t2 - t1)))))
		/* Not optimal to exercise */
		return opt_bs1 (OS_Call, sx, x, t2, r, v,0.0);

	ci = opt_bs1 (OS_Call, s, x, t2 - t1, r, v,0.0);
	HighS = s;
	while ((ci - HighS - d + x) > 0.0 && HighS < infinity){

		HighS *= 2.0;
		ci = opt_bs1 (OS_Call, HighS, x, t2 - t1, r, v,0.0);
	}
	if (HighS > infinity)
		return opt_bs1 (OS_Call, sx, x, t2, r, v,0.0);

	LowS = 0.0;
	i = HighS * 0.5;
	ci = opt_bs1 (OS_Call, i, x, t2 - t1, r, v,0.0);

	/* search algorithm to find the critical stock price i */
	while (gnumabs(ci - i - d + x) > epsilon && HighS - LowS > epsilon) {
		if ((ci - i - d + x) < 0)
			HighS = i;
		else
			LowS = i;
		i = (HighS + LowS) / 2.0;
		ci = opt_bs1 (OS_Call, i, x, (t2 - t1), r, v,0.0);
	}

	a1 = (loggnum (sx / x) + (r + (v * v) / 2.0) * t2) / (v * sqrtgnum (t2));
	a2 = a1 - v * sqrtgnum (t2);
	b1 = (loggnum (sx / i) + (r + (v * v) / 2.0) * t1) / (v * sqrtgnum (t1));
	b2 = b1 - v * sqrtgnum (t1);

	gfresult = sx * cum_norm_dist (b1) + sx * cum_biv_norm_dist1 (a1, -b1, -sqrtgnum (t1 / t2))
		- x * expgnum (-r * t2) * cum_biv_norm_dist1 (a2, -b2, -sqrtgnum (t1 / t2)) - (x - d)
		* expgnum (-r * t1) * cum_norm_dist (b2);
	return gfresult;
}


static GnmValue *
opt_rgw(FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float s = value_get_as_float (argv[0]);
	gnm_float x = value_get_as_float (argv[1]);
	gnm_float t1 = value_get_as_float (argv[2]);
	gnm_float t2 = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float d = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);
	gnm_float gfresult = 0.0;

	gfresult = opt_rgw1 (s, x, t1, t2, r, d, v);

	return value_new_float (gfresult);
}

static char const *help_opt_rgw = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_RGW\n"

	   "@SYNTAX=OPT_RGW(call_put_flag,spot,strike,t1,t2,rate,d,volatility)"
	   "\n"
	   "@DESCRIPTION="
	   "OPT_RGW models the theoretical price of an american option according to "
	   "the Roll-Geske-Whaley approximation where: \n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike is the strike price at which the option is struck.\n"
	   "@t1 is the time to the dividend payout.\n"
	   "@t2 is the time to option expiration.\n"
	   "@rate is the annualized rate of interest.\n"
	   "@d is the amount of the dividend to be paid.\n"
	   "@volatility is the annualized rate of volatility of the underlying asset.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

/* the Barone-Adesi and Whaley (1987) American approximation */
static GnmValue *
opt_baw_amer (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = value_get_as_float (argv[6]);
	gnm_float gfresult;

	switch (call_put) {
	case OS_Call:
		gfresult = opt_baw_call (s, x, t, r, v, b);
		break;
	case OS_Put:
		gfresult = opt_baw_put (s, x, t, r, v, b);
		break;
	default:
		return value_new_error_NUM (ei->pos);
	}

	if (gfresult == -123)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gfresult);
}

static char const *help_opt_baw_amer = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BAW_AMER\n"

	   "@SYNTAX=OPT_BAW_AMER(call_put_flag,spot,strike,time,rate,"
	   "cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_BAW_AMER models the theoretical price of an option according "
	   "to the Barone Adesie & Whaley approximation. \n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike is the strike price at which the option is struck.\n"
	   "@time is the number of days to maturity of the option.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield.\n"
	   "@volatility is the annualized volatility in price of the underlying asset.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

/* American call */
static gnm_float
opt_baw_call (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float sk, n, k;
	gnm_float d1, q2, a2;
	gnm_float gfresult;
	if (b >= r)
		gfresult = opt_bs1 (OS_Call, s, x, t, r, v, b);
	else
	{
		sk = NRA_c (x, t, r, v, b);
		n = 2.0 * b / (v * v);
		k = 2.0 * r / ((v * v) * (1.0 - expgnum (-r * t)));
		d1 = (loggnum (sk / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
		q2 = (-(n - 1.0) + sqrtgnum ((n - 1.0) * (n - 1.0) + 4.0 * k)) / 2.0;
		a2 = (sk / q2) * (1.0 - expgnum ((b - r) * t) * cum_norm_dist (d1));
		if (s < sk)
			gfresult = opt_bs1 (OS_Call, s, x, t, r, v, b) + a2 * powgnum (s / sk, q2);
		else
			gfresult = s - x;

	} /*end if statement*/
	return gfresult;
}





/* Newton Raphson algorithm to solve for the critical commodity price for a Call */
static gnm_float
NRA_c (gnm_float x, gnm_float  t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float n, m;
	gnm_float su, si;
	gnm_float  h2, k;
	gnm_float d1, q2, q2u;
	gnm_float LHS, RHS;
	gnm_float bi, e;

	/* Calculation of seed value, si */
	n = 2.0 * b / (v * v);
	m = 2.0 * r / (v * v);
	q2u = (-(n - 1.0) + sqrtgnum (((n - 1.0) * (n - 1.0)) + 4.0 * m)) / 2.0;
	su = x / (1.0 - 1.0/ q2u);
	h2 = -(b * t + 2.0 * v * sqrtgnum (t)) * x / (su - x);
	si = x + (su - x) * (1.0 - expgnum (h2));

	k = 2.0 * r / ((v * v) * (1.0 - expgnum (-r * t)));
	d1 = (loggnum (si / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	q2 = (-(n - 1.0) + sqrtgnum (((n - 1.0) * (n - 1.0)) + 4.0 * k)) / 2.0;
	LHS = si - x;
	RHS = opt_bs1 (OS_Call, si, x, t, r, v, b) + (1.0 - expgnum ((b - r) * t) * cum_norm_dist (d1)) * si / q2;
	bi = expgnum ((b - r) * t) * cum_norm_dist (d1) * (1.0 - 1.0/ q2)
		+ (1.0 - expgnum ((b - r) * t) * cum_norm_dist (d1) / (v * sqrtgnum (t))) / q2;
	e = 0.000001;

	/* Newton Raphson algorithm for finding critical price si */
	while ((gnumabs(LHS - RHS) / x) > e)
	{
		si = (x + RHS - bi * si) / (1.0 - bi);
		d1 = (loggnum (si / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
		LHS = si - x;
		RHS = opt_bs1 (OS_Call, si, x, t, r, v, b) + (1.0 - expgnum ((b - r) * t) * cum_norm_dist (d1)) * si / q2;
		bi = expgnum ((b - r) * t) * cum_norm_dist (d1) * (1.0 - 1.0/ q2)
			+ (1.0 - expgnum ((b - r) * t) * n_d (d1) / (v * sqrtgnum (t))) / q2;
	}
	return si;
}

static gnm_float
opt_baw_put (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float sk = NRA_p (x, t, r, v, b);
	gnm_float n = 2.0 * b / (v * v);
	gnm_float k = 2.0 * r / ((v * v) * (1.0 - expgnum (-r * t)));
	gnm_float d1 = (loggnum (sk / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	gnm_float q1 = (-(n - 1.0) - sqrtgnum (((n - 1.0) * (n - 1.0)) + 4.0 * k)) / 2.0;
	gnm_float a1 = -(sk / q1) * (1.0 - expgnum ((b - r) * t) * cum_norm_dist (-d1));

	if (s > sk)
		return opt_bs1 (OS_Put, s, x, t, r, v, b) + a1 * powgnum (s/ sk, q1);
	else
		return x - s;
}

/* Newton Raphson algorithm to solve for the critical commodity price for a Put*/
static gnm_float
NRA_p (gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{

	gnm_float n, m;
	gnm_float su, si;
	gnm_float h1, k;
	gnm_float d1, q1u, q1;
	gnm_float LHS, RHS;
	gnm_float bi, e;

	/* Calculation of seed value, si */
	n = 2.0 * b / (v * v);
	m = 2.0 * r / (v * v);
	q1u = (-(n - 1.0) - sqrtgnum (((n - 1.0) * (n - 1.0)) + 4.0 * m)) / 2.0;
	su = x / (1.0 - 1.0/ q1u);
	h1 = (b * t - 2.0 * v * sqrtgnum (t)) * x / (x - su);
	si = su + (x - su) * expgnum (h1);

	k = 2.0 * r / ((v * v) * (1.0 - expgnum (-r * t)));
	d1 = (loggnum (si / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	q1 = (-(n - 1.0) - sqrtgnum (((n - 1.0) * (n - 1.0)) + 4.0 * k)) / 2.0;
	LHS = x - si;
	RHS = opt_bs1 (OS_Put, si, x, t, r, v, b) - (1.0 - expgnum ((b - r) * t) * cum_norm_dist (-d1)) * si / q1;
	bi = -expgnum ((b - r) * t) * cum_norm_dist (-d1) * (1.0 - 1.0/ q1)
		- (1.0 + expgnum ((b - r) * t) * n_d (-d1) / (v * sqrtgnum (t))) / q1;
	e = 0.000001;

	/* Newton Raphson algorithm for finding critical price si */
	while(gnumabs(LHS - RHS) / x > e) {
		si = (x - RHS + bi * si) / (1.0 + bi);
		d1 = (loggnum (si / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
		LHS = x - si;
		RHS = opt_bs1 (OS_Put, si, x, t, r, v, b) - (1.0 - expgnum ((b - r) * t) * cum_norm_dist (-d1)) * si / q1;
		bi = -expgnum ((b - r) * t) * cum_norm_dist (-d1) * (1.0 - 1.0/ q1)
			- (1.0 + expgnum ((b - r) * t) * cum_norm_dist (-d1) / (v * sqrtgnum (t))) / q1;
	}
	return si;
}

/* the Bjerksund and stensland (1993) American approximation */
static gnm_float
opt_bjer_stens1 (OptionSide side, gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	switch (side) {
	case OS_Call:
		return opt_bjer_stens1_c (s, x, t, r, v, b);
	case OS_Put:
		/* Use the Bjerksund and stensland put-call transformation */
		return opt_bjer_stens1_c (x, s, t, r - b, v, -b);
	default:
		return -123;
	}
}

static GnmValue *
opt_bjer_stens (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]):0;
	gnm_float gfresult =
		opt_bjer_stens1 (call_put, s, x, t, r, v, b);
	return value_new_float (gfresult);
}


static char const *help_opt_bjer_stens = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BJERSTENS\n"

	   "@SYNTAX=OPT_BJERSTENS(call_put_flag,spot,strike,time,rate,"
	   "volatility[,cost_of_carry])\n"
	   "@DESCRIPTION="
	   "OPT_BJERSTENS models the theoretical price of american options "
	   "according to the Bjerksund & Stensland approximation technique.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike is the strike price at which the option is struck.\n"
	   "@time is the number of days to maturity of the option.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@volatility is the annualized volatility in price of the underlying asset.\n" 
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

static gnm_float
opt_bjer_stens1_c (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	if (b >= r) /* Never optimal to exersice before maturity */
		return opt_bs1 (OS_Call, s, x, t, r, v, b);
	else {
		gnm_float Beta =
			(1.0/ 2.0 - b / (v * v)) +
			sqrtgnum (powgnum (b / (v * v) - 1.0/ 2.0, 2) + 2.0 * r / (v * v));
		gnm_float BInfinity = Beta / (Beta - 1.0) * x;
		gnm_float B0 = MAX (x, r / (r - b) * x);
		gnm_float ht = -(b * t + 2.0 * v * sqrtgnum (t)) * B0 / (BInfinity - B0);
		gnm_float I = B0 + (BInfinity - B0) * (1.0 - expgnum (ht));
		if (s >= I)
			return s - x;
		else {
			gnm_float alpha = (I - x) * powgnum (I ,-Beta);
			return alpha * powgnum (s ,Beta) -
				alpha * phi (s, t, Beta, I, I, r, v, b) +
				phi (s, t, 1.0, I, I, r, v, b) -
				phi (s, t, 1.0, x, I, r, v, b) -
				x * phi (s, t, 0.0, I, I, r, v, b) +
				x * phi (s, t, 0.0, x, I, r, v, b);
		}
	}
}

static gnm_float
phi (gnm_float s, gnm_float t, gnm_float gamma, gnm_float H, gnm_float I, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float lambda, kappa;
	gnm_float d;
	gnm_float gfresult;

	lambda = (-r + gamma * b + 0.5 * gamma * (gamma - 1.0) * (v * v)) * t;
	d = -(loggnum (s / H) + (b + (gamma - 0.5) * (v * v)) * t) / (v * sqrtgnum (t));
	kappa = 2.0 * b / (v * v) + (2.0 * gamma - 1.0);
	gfresult = expgnum (lambda) * powgnum (s, gamma) * (cum_norm_dist (d) - powgnum (I / s, kappa) * cum_norm_dist (d - 2.0 * loggnum (I / s) / (v * sqrtgnum (t))));

	return gfresult;
}


/* Executive stock options */
static GnmValue *
opt_exec (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = value_get_as_float (argv[6]);
	gnm_float lambda = value_get_as_float (argv[7]);
	gnm_float gfresult =
		expgnum (-lambda * t) * opt_bs1 (call_put, s, x, t, r, v, b);
	return value_new_float (gfresult);
}


static char const *help_opt_exec = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_EXEC\n"

	   "@SYNTAX=OPT_EXEC(call_put_flag,spot,strike,time,rate,volatility,"
	   "cost_of_carry,lambda)\n"
	   "@DESCRIPTION="
	   "OPT_EXEC models the theoretical price of executive stock options "
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "One would expect this to always be a call option.\n"
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike is the strike price at which the option is struck.\n"
	   "@time is the number of days to maturity of the option.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@volatility is the annualized volatility in price of the underlying asset.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield.\n"
	   "@lambda is the jump rate for executives."
	   " The model assumes executives forfeit their options if they leave the company.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};





/* Forward start options */
static GnmValue *
opt_forward_start(FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float alpha = value_get_as_float (argv[2]);
	gnm_float t1 = value_get_as_float (argv[3]);
	gnm_float t = value_get_as_float (argv[4]);
	gnm_float r = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);
	gnm_float b = value_get_as_float (argv[7]);
	gnm_float gfresult =
		s * expgnum ((b - r) * t1) * opt_bs1 (call_put, 1, alpha, t - t1, r, v, b);
	return value_new_float (gfresult);

}


static char const *help_opt_forward_start = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_FORWARD_START\n"

	   "@SYNTAX=OPT_FORWARD_START(call_put_flag,spot,alpha,time1,time,rate,"
	   "volatility,cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_FORWARD_START models the theoretical price of forward start options\n "
	   
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@spot is the spot price of the underlying asset.\n"
	   "@alpha is a fraction that set the strike price the future date @time1.\n"
	   "@time1 is the number of days until the option starts.\n"
	   "@time is the number of days to maturity of the option.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@volatility is the annualized volatility in price of the underlying asset.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* time switch options (discrete) */
static GnmValue *
opt_time_switch (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float a = value_get_as_float (argv[3]);
	gnm_float t = value_get_as_float (argv[4]);
	gnm_float m = value_get_as_float (argv[5]);
	gnm_float dt = value_get_as_float (argv[6]);
	gnm_float r = value_get_as_float (argv[7]);
	gnm_float b = value_get_as_float (argv[8]);
	gnm_float v = value_get_as_float (argv[9]);

	gnm_float gfresult;
	gnm_float sum, d;
	int i, n, Z = 0;

	switch (call_put) {
	case OS_Call: Z = +1; break;
	case OS_Put: Z = -1; break;
	default: return value_new_float (-123);
	}

	sum = 0.0;
	n = t / dt;
	for (i = 1; i < n; ++i) {
		d = (loggnum (s / x) + (b - (v * v) / 2.0) * i * dt) / (v * sqrtgnum (i * dt));
		sum = sum + cum_norm_dist (Z * d) * dt;
	}

	gfresult = a * expgnum (-r * t) * sum + dt * a * expgnum (-r * t) * m;
	return value_new_float (gfresult);

}

static char const *help_opt_time_switch = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_TIME_SWITCH\n"

	   "@SYNTAX=OPT_TIME_SWITCH(call_put_flag,spot,strike,a,time,m,dt,rate,"
	   "cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_TIME_SWITCH models the theoretical price of time switch "
	   "options. (Pechtl 1995)\n"
	   "The holder receives @a * @dt for each period dt that the asset price was "
	   "greater than the strike price (for a call) or below it (for a put). \n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike is the strike price at which the option is struck.\n"
	   "@a is the amount received for each time period as discussed above.\n"
	   "@time is the maturity of the option in years.\n"
	   "@m is the number of time units the option has already met the condition.\n"
	   "@dt is the agreed upon discrete time period (often a day) expressed as "
	   "a fraction of a year.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."
 
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* simple chooser options */
static GnmValue *
opt_simple_chooser(FunctionEvalInfo *ei, GnmValue *argv[])
{

	gnm_float s = value_get_as_float (argv[0]);
	gnm_float x = value_get_as_float (argv[1]);
	gnm_float t1 = value_get_as_float (argv[2]);
	gnm_float t2 = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float b = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);

	gnm_float gfresult;

	gnm_float d, y;

	d = (loggnum (s / x) + (b + (v * v) / 2.0) * t2) / (v * sqrtgnum (t2));
	y = (loggnum (s / x) + b * t2 + (v * v) * t1 / 2.0) / (v * sqrtgnum (t1));

	gfresult = s * expgnum ((b - r) * t2) * cum_norm_dist (d) - x * expgnum (-r * t2) * cum_norm_dist (d - v * sqrtgnum (t2))
		- s * expgnum ((b - r) * t2) * cum_norm_dist (-y) + x * expgnum (-r * t2) * cum_norm_dist (-y + v * sqrtgnum (t1));

	return value_new_float (gfresult);

}

static char const *help_opt_simple_chooser = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_SIMPLE_CHOOSER\n"

	   "@SYNTAX=OPT_SIMPLE_CHOOSER(call_put_flag,spot,strike,time1,time2,"
	   "rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_SIMPLE_CHOOSER models the theoretical price of simple chooser "
	   "options.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike is the strike price at which the option is struck.\n"
	   "@time1 is the time in years until the holder chooses a put or a call option.\n"
	   "@time2 is the time in years until the the chosen option expires.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield."

	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Complex chooser options */
static GnmValue *
opt_complex_chooser(FunctionEvalInfo *ei, GnmValue *argv[])
{

	gnm_float s = value_get_as_float (argv[0]);
	gnm_float xc = value_get_as_float (argv[1]);
	gnm_float xp = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float tc = value_get_as_float (argv[4]);
	gnm_float tp = value_get_as_float (argv[5]);
	gnm_float r = value_get_as_float (argv[6]);
	gnm_float b = value_get_as_float (argv[7]);
	gnm_float v = value_get_as_float (argv[8]);

	gnm_float gfresult;

	gnm_float d1, d2, y1, y2;
	gnm_float rho1, rho2, I;

	I = opt_crit_val_chooser (s, xc, xp, t, tc, tp, r, b, v);
	d1 = (loggnum (s / I) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum (t));
	d2 = d1 - v * sqrtgnum (t);
	y1 = (loggnum (s / xc) + (b + (v * v) / 2.0) * tc) / (v * sqrtgnum (tc));
	y2 = (loggnum (s / xp) + (b + (v * v) / 2.0) * tp) / (v * sqrtgnum (tp));
	rho1 = sqrtgnum (t / tc);
	rho2 = sqrtgnum (t / tp);

	gfresult = s * expgnum ((b - r) * tc) * cum_biv_norm_dist1 (d1, y1, rho1) - xc * expgnum (-r * tc)
		* cum_biv_norm_dist1 (d2, y1 - v * sqrtgnum (tc), rho1) - s * expgnum ((b - r) * tp)
		* cum_biv_norm_dist1 (-d1, -y2, rho2) + xp * expgnum (-r * tp) * cum_biv_norm_dist1 (-d2, -y2 + v * sqrtgnum (tp), rho2);

	return value_new_float (gfresult);

}

static char const *help_opt_complex_chooser = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_COMPLEX_CHOOSER\n"

	   "@SYNTAX=OPT_COMPLEX_CHOOSER(call_put_flag,spot,strike_call,"
	   "strike_put,time,time_call,time_put,rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_COMPLEX_CHOOSER models the theoretical price of complex "
	   "chooser options.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike_call is the strike price at which the option is struck, applicable if exercised as a call option.\n"
	   "@strike_put is the strike price at which the option is struck, applicable if exercised as a put option.\n"
	   
	   "@time is the time in years until the holder chooses a put or a call option. \n"
	   "@time_call is the time in years to maturity of the call option if chosen.\n"
	   "@time_put is the time in years  to maturity of the put option if chosen.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, " 
	   "for common stocks, this would be the dividend yield.\n"
	   "@volatility is the annualized volatility in price of the underlying asset.\n" 

	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};




/* Critical value complex chooser option */
static gnm_float
opt_crit_val_chooser (gnm_float s,gnm_float xc,gnm_float xp,gnm_float t,
		      gnm_float tc, gnm_float tp, gnm_float r, gnm_float b, gnm_float v)
{
	gnm_float sv, ci, Pi, epsilon;
	gnm_float dc, dp, yi, di;

	sv = s;
	ci = opt_bs1 (OS_Call, sv, xc, tc - t, r, v, b);
	Pi = opt_bs1 (OS_Put, sv, xp, tp - t, r, v, b);
	dc = opt_bs_delta1 (OS_Call, sv, xc, tc - t, r, v, b);
	dp = opt_bs_delta1 (OS_Put, sv, xp, tp - t, r, v, b);
	yi = ci - Pi;
	di = dc - dp;
	epsilon = 0.001;
	/* Newton-Raphson */
	while (gnumabs(yi) > epsilon)
	{
		sv = sv - (yi) / di;
		ci = opt_bs1 (OS_Call, sv, xc, tc - t, r, v, b);
		Pi = opt_bs1 (OS_Put, sv, xp, tp - t, r, v, b);
		dc = opt_bs_delta1 (OS_Call, sv, xc, tc - t, r, v, b);
		dp = opt_bs_delta1 (OS_Put, sv, xp, tp - t, r, v, b);
		yi = ci - Pi;
		di = dc - dp;
	}

	return sv;
}


/* Options on options */
static GnmValue *
opt_on_options (FunctionEvalInfo *ei, GnmValue *argv[])
{
	char *type_flag = value_get_as_string (argv[0]);
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x1 = value_get_as_float (argv[2]);
	gnm_float x2 = value_get_as_float (argv[3]);
	gnm_float t1 = value_get_as_float (argv[4]);
	gnm_float t2 = value_get_as_float (argv[5]);
	gnm_float r = value_get_as_float (argv[6]);
	gnm_float b = value_get_as_float (argv[7]);
	gnm_float v = value_get_as_float (argv[8]);

	gnm_float gfresult;

	gnm_float y1, y2, z1, z2;
	gnm_float I, rho;
	OptionSide call_put;

	if (!strcmp (type_flag , "cc") || !strcmp (type_flag , "pc"))
		call_put = OS_Call;
	else
		call_put = OS_Put;

	I = CriticalValueOptionsOnOptions (call_put, x1, x2, t2 - t1, r, b, v);

	rho = sqrtgnum (t1 / t2);
	y1 = (loggnum (s / I) + (b + (v * v) / 2.0) * t1) / (v * sqrtgnum (t1));
	y2 = y1 - v * sqrtgnum (t1);
	z1 = (loggnum (s / x1) + (b + (v * v) / 2.0) * t2) / (v * sqrtgnum (t2));
	z2 = z1 - v * sqrtgnum (t2);

	if (!strcmp (type_flag , "cc"))
		gfresult = s * expgnum ((b - r) * t2) * cum_biv_norm_dist1 (z1, y1, rho) -
			x1 * expgnum (-r * t2) * cum_biv_norm_dist1 (z2, y2, rho) - x2 * expgnum (-r * t1) * cum_norm_dist (y2);
	else if (!strcmp (type_flag , "pc"))
		gfresult = x1 * expgnum (-r * t2) * cum_biv_norm_dist1 (z2, -y2, -rho) -
			s * expgnum ((b - r) * t2) * cum_biv_norm_dist1 (z1, -y1, -rho) + x2 * expgnum (-r * t1) * cum_norm_dist (-y2);
	else if (!strcmp (type_flag , "cp"))
		gfresult = x1 * expgnum (-r * t2) * cum_biv_norm_dist1 (-z2, -y2, rho) -
			s * expgnum ((b - r) * t2) * cum_biv_norm_dist1 (-z1, -y1, rho) - x2 * expgnum (-r * t1) * cum_norm_dist (-y2);
	else if (!strcmp (type_flag , "pp"))
		gfresult = s * expgnum ((b - r) * t2) * cum_biv_norm_dist1 (-z1, y1, -rho) -
			x1 * expgnum (-r * t2) * cum_biv_norm_dist1 (-z2, y2, -rho) + expgnum (-r * t1) * x2 * cum_norm_dist (y2);
	else {
		g_free (type_flag);
		return value_new_error_VALUE (ei->pos);
	}
	return value_new_float (gfresult);
}

static char const *help_opt_on_options = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_ON_OPTIONS\n"

	   "@SYNTAX=OPT_ON_OPTIONS(type_flag,spot,strike1,strike2,time1,time2,"
	   "rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_ON_OPTIONS models the theoretical price of options on options.\n"
	   "@type_flag is 'cc' for calls on calls, 'cp' for calls on puts, and so on for 'pc', and 'pp'.\n"
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike1 is the strike price at which the option being valued is struck.\n"
	   "@strike2 is the strike price at which the underlying option is struck.\n"
	   "@time1 is the time in years to maturity of the option.\n"
	   "@time2 is the time in years to the maturity of the underlying option.\n"
	   "(@time2 >= @time1).\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset of the underlying option."
	   "for common stocks, this would be the dividend yield.\n"
	   "@volatility is the annualized volatility in price of the underlying asset of the underlying option.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Calculation of critical price options on options */
static gnm_float
CriticalValueOptionsOnOptions (OptionSide side, gnm_float x1, gnm_float x2, gnm_float t,
			       gnm_float r, gnm_float b, gnm_float v)
{
	gnm_float si, ci, di, epsilon;

	si = x1;
	ci = opt_bs1 (side, si, x1, t, r, v, b);
	di = opt_bs_delta1 (side, si, x1, t, r, v, b);

	/* Newton-Raphson algorithm */
	epsilon = 0.0001;
	while (gnumabs (ci - x2) > epsilon) {
		si = si - (ci - x2) / di;
		ci = opt_bs1 (side, si, x1, t, r, v, b);
		di = opt_bs_delta1 (side, si, x1, t, r, v, b);
	}
	return si;
}

/* Writer extendible options */
static GnmValue *
opt_extendible_writer (FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x1 = value_get_as_float (argv[2]);
	gnm_float x2 = value_get_as_float (argv[3]);
	gnm_float t1 = value_get_as_float (argv[4]);
	gnm_float t2 = value_get_as_float (argv[5]);
	gnm_float r = value_get_as_float (argv[6]);
	gnm_float b = value_get_as_float (argv[7]);
	gnm_float v = value_get_as_float (argv[8]);

	gnm_float rho = sqrtgnum (t1 / t2);
	gnm_float z1 = (loggnum (s / x2) + (b + (v * v) / 2.0) * t2) / (v * sqrtgnum (t2));
	gnm_float z2 = (loggnum (s / x1) + (b + (v * v) / 2.0) * t1) / (v * sqrtgnum (t1));

	gnm_float gfresult;

	switch (call_put) {
	case OS_Call:
		gfresult = opt_bs1 (call_put, s, x1, t1, r, v, b) +
			s * expgnum ((b - r) * t2) * cum_biv_norm_dist1 (z1, -z2, -rho) -
			x2 * expgnum (-r * t2) * cum_biv_norm_dist1 (z1 - sqrtgnum ((v * v) * t2), -z2 + sqrtgnum ((v * v) * t1), -rho);
	break;
	case OS_Put:
		gfresult = opt_bs1 (call_put, s, x1, t1, r, v, b) +
			x2 * expgnum (-r * t2) * cum_biv_norm_dist1 (-z1 + sqrtgnum ((v * v) * t2), z2 - sqrtgnum ((v * v) * t1), -rho) -
			s * expgnum ((b - r) * t2) * cum_biv_norm_dist1 (-z1, z2, -rho);
	break;
	default:
		gfresult = -123;
	}

	return value_new_float (gfresult);
}

static char const *help_opt_extendible_writer = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_EXTENDIBLE_WRITER\n"

	   "@SYNTAX=OPT_EXTENDIBLE_WRITER(call_put_flag,spot,strike1,strike2,"
	   "time1,time2,rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_EXTENDIBLE_WRITER models the theoretical price of extendible "
	   "writer options. These are options that can be exercised at an initial "
	   "period, @time1, or their maturity extended to @time2 if the option is "
	   "out of the money at @time1.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@spot is the spot price of the underlying asset.\n"
	   "@strike1 is the strike price at which the option is struck.\n"
	   "@strike2 is the strike price at which the option is re-struck if out of the money at @time1.\n"
	   "@time1 is the initial maturity of the option in years.\n"
	   "@time2 is the is the extended maturity in years if chosen.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@cost_of_carry is the leakage in value of the underlying asset, "
	   "for common stocks, this would be the dividend yield.\n"
	   "@volatility is the annualized volatility in price of the underlying asset.\n"

	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};



/* Two asset correlation options */
static GnmValue *
opt_2_asset_correlation(FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put = option_side(value_peek_string(argv[0]));
	gnm_float s1 = value_get_as_float(argv[1]);
	gnm_float s2 = value_get_as_float(argv[2]);
	gnm_float x1 = value_get_as_float(argv[3]);
	gnm_float x2 = value_get_as_float(argv[4]);
	gnm_float t = value_get_as_float(argv[5]);
	gnm_float b1 = value_get_as_float(argv[6]);
	gnm_float b2 = value_get_as_float(argv[7]);
	gnm_float r = value_get_as_float(argv[8]);
	gnm_float v1 = value_get_as_float(argv[9]);
	gnm_float v2 = value_get_as_float(argv[10]);
	gnm_float rho = value_get_as_float(argv[11]);
	gnm_float y1, y2;
   
    y1 = (loggnum(s1 / x1) + (b1 - (v1 * v1) / 2.0) * t) / (v1 * sqrtgnum(t));
    y2 = (loggnum(s2 / x2) + (b2 - (v2 * v2) / 2.0) * t) / (v2 * sqrtgnum(t));
    
    if (call_put == OS_Call){ 
         return value_new_float(s2 * expgnum((b2 - r) * t) 
			 * cum_biv_norm_dist1(y2 + v2 * sqrtgnum(t), y1 + rho * v2 * sqrtgnum(t), rho)
			 - x2 * expgnum(-r * t) * cum_biv_norm_dist1(y2, y1, rho));
	}else if (call_put == OS_Put){ 
        	return value_new_float(x2 * expgnum(-r * t) * cum_biv_norm_dist1(-y2, -y1, rho) 
         - s2 * expgnum((b2 - r) * t) * cum_biv_norm_dist1(-y2 - v2 * sqrtgnum(t), -y1 - rho * v2 * sqrtgnum(t), rho));
	 }else 
	 return value_new_error_NUM (ei->pos); 
}

static char const *help_opt_2_asset_correlation = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_2_ASSET_CORRELATION\n"

	   "@SYNTAX=OPT_2_ASSSET_CORRELATION(call_put_flag,spot1,spot2,strike1,strike2,"
	   "time,cost_of_carry1,cost_of_carry2,rate,volatility1,volatility2,rho)\n"
	   "@DESCRIPTION="
	   "OPT_2_ASSET_CORRELATION models the theoretical price of  options "
	   
	   "on 2 assets with correlation @rho.\nThe payoff for a call is "
	   "max(@spot2 - @strike2,0) if @spot1 > @strike1 or 0 otherwise.\n"
	   "The payoff for a put is max (@strike2 - @spot2, 0) if @spot1 < @strike1 or 0 otherwise.\n"
	   "@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	   "@spot1 & @spot2 are the spot prices of the underlying assets.\n"
	   "@strike1 & @strike2 are the strike prices at which the option"
	   " is struck.\n"
	   "@time is the initial maturity of the option in years.\n"
	   "@rate is the risk annualized free rate of interest.\n"
	   "@cost_of_carry1 & @cost_of_carry2 are the leakage in value of the underlying assets, "
	   "for common stocks, this would be the dividend yield.\n"
	   "@volatility1 & @volatility2 are the annualized volatility in price of the underlying assets.\n"

	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* European option to exchange one asset for another */
static GnmValue *
opt_euro_exchange(FunctionEvalInfo *ei, GnmValue *argv[])
{
	gnm_float s1 = value_get_as_float(argv[0]);
	gnm_float s2 = value_get_as_float(argv[1]);
	gnm_float q1 = value_get_as_float(argv[2]);
	gnm_float q2 = value_get_as_float(argv[3]);
	gnm_float t = value_get_as_float(argv[4]);
	gnm_float r = value_get_as_float(argv[5]);
	gnm_float b1 = value_get_as_float(argv[6]);
	gnm_float b2 = value_get_as_float(argv[7]);
	gnm_float v1 = value_get_as_float(argv[8]);
	gnm_float v2 = value_get_as_float(argv[9]);
	gnm_float rho = value_get_as_float(argv[10]);
	gnm_float v, d1, d2;

	v = sqrtgnum(v1 * v1 + v2 * v2 - 2.0 * rho * v1 * v2);
    d1 = (loggnum(q1 * s1 / (q2 * s2)) + (b1 - b2 + (v * v) / 2.0) * t) / (v * sqrtgnum(t));
    d2 = d1 - v * sqrtgnum(t);
 
    return value_new_float(q1 * s1 * expgnum((b1 - r) * t) * cum_norm_dist(d1) - q2 * s2 * expgnum((b2 - r) * t) * cum_norm_dist(d2));
}

static char const *help_opt_euro_exchange = { 
	 /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_EURO_EXCHANGE\n" 
	"@SYNTAX=OPT_EURO_EXCHANGE(spot1,spot2,qty1,qty2,"
	"time,rate,cost_of_carry1,cost_of_carry2,"
	"volatility1,volatility2,rho)\n"
	"@DESCRIPTION="
	"OPT_EURO_EXCHANGE models the theoretical price of a European "
	"option to exchange one asset with quantity @qty2 and spot "
	"price @spot2 for another, with quantity @qty1 and spot price "
	"@spot1.\n"
	"@time is the initial maturity of the option in years.\n"
	"@rate is the risk annualized free rate of interest.\n"
	"@cost_of_carry1 & @cost_of_carry2 are the leakage in value of the underlying assets, "
	"for common stocks, this would be the dividend yield.\n"  
	"@volatility1 & @volatility2 are the annualized volatility in price of the underlying assets.\n"
	"@rho is the correlation between the two assets.\n"
	"\n"
	"@EXAMPLES=\n"
	"\n"
	"@SEEALSO=OPT_AMER_EXCHANGE, OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
}; 
	 

/* American option to exchange one asset for another */
static GnmValue *
opt_amer_exchange(FunctionEvalInfo *ei, GnmValue *argv[])
{ 
	gnm_float s1 = value_get_as_float(argv[0]);
	gnm_float s2 = value_get_as_float(argv[1]);
	gnm_float q1 = value_get_as_float(argv[2]);
	gnm_float q2 = value_get_as_float(argv[3]);
	gnm_float t = value_get_as_float(argv[4]);
	gnm_float r = value_get_as_float(argv[5]);
	gnm_float b1 = value_get_as_float(argv[6]);
	gnm_float b2 = value_get_as_float(argv[7]);
	gnm_float v1 = value_get_as_float(argv[8]);
	gnm_float v2 = value_get_as_float(argv[9]);
	gnm_float rho = value_get_as_float(argv[10]);
	gnm_float v = sqrtgnum(v1 * v1 + v2 * v2 - 2.0 * rho * v1 * v2);
    	return value_new_float(opt_bjer_stens1(OS_Call, q1 * s1, q2 * s2, t, r - b2, v,b1 - b2));
}

static char const *help_opt_amer_exchange = { 
	 /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_AMER_EXCHANGE\n" 
	"@SYNTAX=OPT_AMER_EXCHANGE(spot1,spot2,qty1,qty2,time,rate,cost_of_carry1,cost_of_carry2,volatility1, volatility2, rho)\n"
	"@DESCRIPTION="
	"OPT_AMER_EXCHANGE models the theoretical price of an American "
	"option to exchange one asset with quantity @qty2 and spot "
	"price @spot2 for another, with quantity @qty1 and spot price "
	"@spot1.\n"
	"@time is the initial maturity of the option in years.\n"
	"@rate is the risk annualized free rate of interest.\n"
	"@cost_of_carry1 & @cost_of_carry2 are the leakage in value of the underlying assets, "
	"for common stocks, this would be the dividend yield.\n"  
	"@volatility1 & @volatility2 are the annualized volatility in price of the underlying assets.\n"
	"@rho is the correlation between the two assets.\n"
	"\n"
	"@EXAMPLES=\n"
	"\n"
	"@SEEALSO=OPT_EURO_EXCH, OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
}; 


/* Spread option approximation */
static GnmValue *
opt_spread_approx(FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put_flag = option_side(value_peek_string(argv[0]));
	gnm_float f1 = value_get_as_float(argv[1]);
	gnm_float f2 = value_get_as_float(argv[2]);
	gnm_float x = value_get_as_float(argv[3]);
	gnm_float t = value_get_as_float(argv[4]);
	gnm_float r = value_get_as_float(argv[5]);
	gnm_float v1 = value_get_as_float(argv[6]);
	gnm_float v2 = value_get_as_float(argv[7]);
	gnm_float rho = value_get_as_float(argv[8]);

	gnm_float v, F;
   
	v = sqrtgnum(v1 * v1 + powgnum((v2 * f2 / (f2 + x)) , 2) - 2.0 * rho * v1 * v2 * f2 / (f2 + x));
    F = f1 / (f2 + x);
    
    return value_new_float(opt_bs1(call_put_flag, F, 1.0, t, r, v,0.0) * (f2 + x));
}

static char const *help_opt_spread_approx = { 
	 /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_SPREAD_APPROX\n" 
	"@SYNTAX=OPT_SPREAD_APPROX(call_put_flag,fut_price1,fut_price2,strike,time, rate,volatility1,volatility2,rho)\n"
	"@DESCRIPTION="
	"OPT_SPREAD_APPROX models the theoretical price of a European option on the spread between two futures contracts.\n"
	"@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	"@fut_price1 & @fut_price2 are the prices of the two futures contracts.\n" 	
	"@strike is the strike price at which the option is struck \n"
	"@time is the initial maturity of the option in years.\n"
	"@rate is the risk annualized free rate of interest.\n"
	"@volatility1 & @volatility2 are the annualized volatility in price of the underlying futures contracts.\n"
	"@rho is the correlation between the two futures contracts.\n"
	"\n"
	"@EXAMPLES=\n"
	"\n"
	"@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
}; 

	
/* Floating strike lookback options */
static GnmValue * opt_float_strk_lkbk(FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put_flag = option_side(value_peek_string(argv[0]));
	gnm_float s = value_get_as_float(argv[1]);
	gnm_float s_min = value_get_as_float(argv[2]);
	gnm_float s_max = value_get_as_float(argv[3]);
	gnm_float t = value_get_as_float(argv[4]);
	gnm_float r = value_get_as_float(argv[5]);
	gnm_float b = value_get_as_float(argv[6]);
	gnm_float v = value_get_as_float(argv[7]);
	

	gnm_float a1, a2, m;
    
	if(OS_Call == call_put_flag)
        	m = s_min;
	else if(OS_Put == call_put_flag)
        	m = s_max;
	else return value_new_error_NUM(ei->pos);
    
	a1 = (loggnum(s / m) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum(t));
 	a2 = a1 - v * sqrtgnum(t);
	
	
	if(OS_Call == call_put_flag)
		return value_new_float(s * expgnum((b - r) * t) * cum_norm_dist(a1) - m * expgnum(-r * t) * cum_norm_dist(a2) +expgnum(-r * t) * (v * v) / (2.0 * b) * s * (powgnum((s / m) , (-2.0 * b / (v * v))) * cum_norm_dist(-a1 + 2.0 * b / v * sqrtgnum(t)) - expgnum(b * t) * cum_norm_dist(-a1)));
	else if(OS_Put == call_put_flag)
		return value_new_float(m * expgnum(-r * t) * cum_norm_dist(-a2) - s * expgnum((b - r) * t) * cum_norm_dist(-a1) + expgnum(-r * t) * (v * v) / (2.0 * b) * s * (-powgnum((s / m) , ((-2.0 * b) / (v * v))) * cum_norm_dist(a1 - 2.0 * b / v * sqrtgnum(t)) + expgnum(b * t) * cum_norm_dist(a1)));

	return value_new_error_VALUE (ei->pos);
}

static char const *help_opt_float_strk_lkbk = { 
	 /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_FLOAT_STRK_LKBK\n" 
	"@SYNTAX=OPT_FLOAT_STRK_LKBK(call_put_flag,spot,spot_min,spot_max,time,rate,cost_of_carry,volatility)\n"
	"@DESCRIPTION="
	"OPT_FLOAT_STRK_LKBK models the theoretical price of an option where the holder of the option may exercise on expiry at the most favourable price observed during the options life of the underlying asset.\n"
	"@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	"@spot is the spot price of the underlying asset.\n"
	"@spot_min is the minimum spot price of the underlying asset so far observed.\n"
	"@spot_max is the maximum spot price of the underlying asset so far observed.\n"
	"@time is the initial maturity of the option in years.\n"
	"@rate is the risk annualized free rate of interest.\n"
	"@cost_of_carry is the leakage in value of the underlying asset, "
	"for common stocks, this would be the dividend yield.\n"
	"@volatility is the annualized volatility in price of the underlying asset.\n"
	 "\n"
	 "@EXAMPLES=\n"
	 "\n"
	 "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

			   
/* Fixed strike lookback options */

static GnmValue *
opt_fixed_strk_lkbk(FunctionEvalInfo *ei, GnmValue *argv[])
{
	OptionSide call_put_flag = option_side(value_peek_string(argv[0]));
	gnm_float s = value_get_as_float(argv[1]);
	gnm_float s_min = value_get_as_float(argv[2]);
	gnm_float s_max = value_get_as_float(argv[3]);
	gnm_float x = value_get_as_float(argv[4]);
	gnm_float t = value_get_as_float(argv[5]);
	gnm_float r = value_get_as_float(argv[6]);
	gnm_float b = value_get_as_float(argv[7]);
	gnm_float v = value_get_as_float(argv[8]);
	    
	gnm_float d1, d2;
	gnm_float e1, e2, m;

	if(OS_Call == call_put_flag)
		m = s_max;
	else if(OS_Put == call_put_flag)
		m = s_min;

	d1 = (loggnum(s / x) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum(t));
	d2 = d1 - v * sqrtgnum(t);
	e1 = (loggnum(s / m) + (b + (v * v) / 2.0) * t) / (v * sqrtgnum(t));
	e2 = e1 - v * sqrtgnum(t);

	if (OS_Call == call_put_flag && x > m) 
		return value_new_float(s * expgnum((b - r) * t) * cum_norm_dist(d1) - x * expgnum(-r * t) * cum_norm_dist(d2) + s * expgnum(-r * t) * (v * v) / (2.0 * b) * (-powgnum((s / x) , (-2.0 * b / (v * v))) * cum_norm_dist(d1 - 2.0 * b / v * sqrtgnum(t)) + expgnum(b * t) * cum_norm_dist(d1)));
	
	else if (OS_Call == call_put_flag && x <= m)
		return value_new_float(expgnum(-r * t) * (m - x) + s * expgnum((b - r) * t) * cum_norm_dist(e1) - expgnum(-r * t) * m * cum_norm_dist(e2) + s * expgnum(-r * t) * (v * v) / (2.0 * b) * (-powgnum((s / m) , (-2.0 * b / (v * v))) * cum_norm_dist(e1 - 2.0 * b / v * sqrtgnum(t)) + expgnum(b * t) * cum_norm_dist(e1)));
	
	else if (OS_Put == call_put_flag && x < m)
		return value_new_float(-s * expgnum((b - r) * t) * cum_norm_dist(-d1) + x * expgnum(-r * t) * cum_norm_dist(-d1 + v * sqrtgnum(t)) + s * expgnum(-r * t) * (v * v) / (2.0 * b) * (powgnum((s / x) , (-2.0 * b / (v * v))) * cum_norm_dist(-d1 + 2.0 * b / v * sqrtgnum(t)) - expgnum(b * t) * cum_norm_dist(-d1)));
	
	else if (OS_Put == call_put_flag && x >= m)
		return value_new_float(expgnum(-r * t) * (x - m) - s * expgnum((b - r) * t) * cum_norm_dist(-e1) + expgnum(-r * t) * m * cum_norm_dist(-e1 + v * sqrtgnum(t)) + expgnum(-r * t) * (v * v) / (2.0 * b) * s * (powgnum((s / m) , (-2.0 * b / (v * v))) * cum_norm_dist(-e1 + 2 * b / v * sqrtgnum(t)) - expgnum(b * t) * cum_norm_dist(-e1)));
	
	return value_new_error_VALUE (ei->pos);
}

static char const *help_opt_fixed_strk_lkbk = { 
	 /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_FIXED_STRK_LKBK\n" 
	"@SYNTAX=OPT_FIXED_STRK_LKBK(call_put_flag,spot,spot_min,spot_max,strike,time,rate,cost_of_carry,volatility)\n"
	"@DESCRIPTION="
	"OPT_FIXED_STRK_LKBK models the theoretical price of an option where the holder of the option may exercise on expiry at the most favourable price observed during the options life of the underlying asset.\n"
	"@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	"@spot is the spot price of the underlying asset.\n"
	"@spot_min is the minimum spot price of the underlying asset so far observed.\n"
	"@spot_max is the maximum spot price of the underlying asset so far observed.\n"
	"@strike is the strike prices at which the option is struck.\n"
	"@time is the initial maturity of the option in years.\n"
	"@rate is the risk annualized free rate of interest.\n"
	"@cost_of_carry is the leakage in value of the underlying asset, "
	"for common stocks, this would be the dividend yield.\n"
	"@volatility is the annualized volatility in price of the underlying asset.\n"
	 "\n"
	 "@EXAMPLES=\n"
	 "\n"
	 "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

 

/* Binomial Tree valuation */
static GnmValue *
opt_binomial(FunctionEvalInfo *ei, GnmValue *argv[])
{ 
	OptionType amer_euro_flag = option_type(value_peek_string(argv[0]));
	OptionSide call_put_flag = option_side(value_peek_string(argv[1]));
	gint n = value_get_as_int(argv[2]);
	gnm_float s = value_get_as_float(argv[3]);
	gnm_float x = value_get_as_float(argv[4]);
	gnm_float t = value_get_as_float(argv[5]);
	gnm_float r = value_get_as_float(argv[6]);
	gnm_float v = value_get_as_float(argv[7]);
	gnm_float b = argv[8] ? value_get_as_float(argv[8]):0;
	
	gnm_float *value_array;
	gnm_float u, d, p, dt, Df, temp1, temp2, gf_result;
	gint i, j, z;
    
	value_array = (gnm_float *) g_try_malloc ((n + 2)* sizeof(gnm_float));
	if (value_array == NULL)
		return value_new_error_NUM (ei->pos);
    
	if (OS_Call == call_put_flag)
        	z = 1;
        else if (OS_Put == call_put_flag)
        	z = -1;
	else return value_new_error_NUM(ei->pos);
	if (OT_Error == amer_euro_flag)
		return value_new_error_NUM(ei->pos);
	dt = t / n;
	u = expgnum(v * sqrtgnum(dt));
	d = 1.0 / u;
	p = (expgnum(b * dt) - d) / (u - d);
	Df = expgnum(-r * dt);
	    
	for(i = 0; i <= n; ++i){
		temp1 = z * (s * powgnum(u , i) * powgnum(d , (n - i)) - x);
		value_array[i] = (temp1>0.0)?temp1:0.0;
	    }
	    
	for(j = n - 1; j > -1; --j){
		for(i = 0; i<=j; ++i){
			/*if (0==i)printf("secondloop %d\n",j);*/
			if (OT_Euro == amer_euro_flag)
				value_array[i] = (p * value_array[i + 1] + (1.0 - p) * value_array[i]) * Df;
			else if (OT_Amer == amer_euro_flag){
				temp1 = (z * (s * powgnum(u , i) * powgnum(d , (gnumabs(i - j))) - x));
				temp2 = (p * value_array[i + 1] + (1.0 - p) * value_array[i]) * Df;
				value_array[i] = (temp1>temp2)?temp1:temp2;
			}
		}
	}
	gf_result = value_array[0];
	g_free (value_array);
	return value_new_float(gf_result);
}

static char const *help_opt_binomial = { 
	 /* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BINOMIAL\n" 
	"@SYNTAX=OPT_BINOMIAL(amer_euro_flag,call_put_flag,num_time_steps, spot, strike, time, rate, volatility, cost_of_carry)\n"
	"@DESCRIPTION="
	"OPT_ models the theoretical price of either an American or European style option using a binomial tree.\n"
	"@amer_euro_flag is either 'a' or 'e' to indicate whether the option being valued is an American or European style option respectively.\n"
	"@call_put_flag is 'c' or 'p' to indicate whether the option is a call or a put.\n" 
	"@num_time_steps is the number of time steps used in the valuation, a greater number of time steps yields greater accuracy however is slower to calculate.\n"
	"@spot is the spot price of the underlying asset.\n"
	"@strike is the strike price at which the option is struck.\n"
	"@time is the initial maturity of the option in years.\n"
	"@rate is the risk annualized free rate of interest.\n"
	"@volatility is the annualized volatility in price of the underlying asset.\n"
	"@cost_of_carry is the leakage in value of the underlying asset.\n"
	"\n"
	"@EXAMPLES=\n"
	"\n"
	"@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
}; 
	


GnmFuncDescriptor const derivatives_functions [] = {
	{ "opt_bs",
	  "sfffff|f", N_("call_put_flag, spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_bs, opt_bs, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_delta",
	  "sffffff|f", N_("call_put_flag, spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_bs_delta, opt_bs_delta, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_rho",
	  "sffffff|f", N_("call_put_flag, spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_bs_rho, opt_bs_rho, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_theta",
	  "sffffff|f", N_("call_put_flag, spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_bs_theta, opt_bs_theta, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_gamma",
	  "fffff|f", N_("spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_bs_gamma, opt_bs_gamma, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_vega",
	  "fffff|f", N_("spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_bs_vega, opt_bs_vega, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_carrycost",
	  "sffffff|f", N_("call_put_flag, spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_bs_carrycost, opt_bs_carrycost, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "cum_biv_norm_dist",
	  "fff", N_("a, b, rho"),
	  &help_cum_biv_norm_dist, cum_biv_norm_dist, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "opt_garman_kohlhagen",
	  "sffffff", N_("call_put_flag, spot, strike, time, domestic_rate, foreign_rate, volatility"),
	  &help_opt_garman_kohlhagen, opt_garman_kohlhagen, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_french",
	  "sfffffff", N_("call_put_flag, spot, strike, time, t2, rate, volatility, cost of carry"),
	  &help_opt_french, opt_french, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_jump_diff",
	  "sfffffff", N_("call_put_flag, spot, strike, time, rate, volatility, lambda, gamma"),
	  &help_opt_jump_diff, opt_jump_diff, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_exec",
	  "sfffffff", N_("call_put_flag, spot, strike, time, rate, volatility, cost_of_carry, lambda"),
	  &help_opt_exec, opt_exec, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bjer_stens",
	  "sffffff", N_("call_put_flag, spot, strike, time, rate, cost_of_carry, volatility"),
	  &help_opt_bjer_stens, opt_bjer_stens, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_miltersen_schwartz",
	  "sfffffffffffff", N_("call_put_flag, p_t, f_t, x, t1, t2, v_s, v_e, v_f, rho_se, rho_sf, rho_ef, kappa_e, kappa_f)"),
	  &help_opt_miltersen_schwartz, opt_miltersen_schwartz, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_baw_amer",
	  "sffffff", N_("call_put_flag, spot, strike, time, rate, cost_of_carry, volatility"),
	  &help_opt_baw_amer, opt_baw_amer, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_rgw",
	  "fffffff", N_("call_put_flag, spot, strike, t1, t2, rate, d, volatility"),
	  &help_opt_rgw, opt_rgw, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_forward_start",
	  "sfffffff", N_("call_put_flag, spot, alpha, time1, time, rate, volatility, cost_of_carry"),
	  &help_opt_forward_start, opt_forward_start, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_time_switch",
	  "sfffffffff", N_("call_put_flag, spot, strike, a, time, m, dt, rate, cost_of_carry, volatility"),
	  &help_opt_time_switch, opt_time_switch, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_simple_chooser",
	  "fffffff", N_("spot, strike, time1, time2, rate, cost_of_carry, volatility"),
	  &help_opt_simple_chooser, opt_simple_chooser, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_complex_chooser",
	  "fffffffff", N_("spot, strike_call, strike_put, time, time_call, time_put, rate, cost_of_carry, volatility"),
	  &help_opt_complex_chooser, opt_complex_chooser, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_on_options",
	  "sffffffff", N_("type_flag, spot, strike1, strike2, time1, time2, rate, cost_of_carry, volatility"),
	  &help_opt_on_options, opt_on_options, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_extendible_writer",
	  "sffffffff", N_("type_flag, spot, strike1, strike2, time1, time2, rate, cost_of_carry, volatility"),
	  &help_opt_extendible_writer, opt_extendible_writer, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_2_asset_correlation",
	  "sfffffffffff", N_("type_flag, spot1, spot2, strike1, strike2, time, cost_of_carry1, cost_of_carry2, rate, volatility1, volatility2, rho"),
	  &help_opt_2_asset_correlation, opt_2_asset_correlation, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_euro_exchange",
	  "fffffffffff", N_("spot1,spot2,qty1,qty2,time,rate,cost_of_carry1,cost_of_carry2,volatility1,volatility2,rho"),
	  &help_opt_euro_exchange, opt_euro_exchange, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	
	{ "opt_amer_exchange",
	  "fffffffffff", N_("spot1,spot2,qty1,qty2,time,rate,cost_of_carry1,cost_of_carry2,volatility1,volatility2,rho"),
	  &help_opt_amer_exchange, opt_amer_exchange, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_spread_approx",
	  "sffffffff", N_("call_put_flag,fut_price1,fut_price2,strike,time, rate,volatility1,volatility2,rho"),
	  &help_opt_spread_approx, opt_spread_approx, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_float_strk_lkbk",
	  "sfffffff", N_("call_put_flag,spot,spot_min,spot_max,time,rate,cost_of_carry,volatility"),
	  &help_opt_float_strk_lkbk, opt_float_strk_lkbk, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_fixed_strk_lkbk",
	  "sffffffff", N_("call_put_flag,spot,spot_min,spot_max,strike,time,rate,cost_of_carry,volatility"),
	  &help_opt_fixed_strk_lkbk, opt_fixed_strk_lkbk, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },


	{ "opt_binomial",
	  "ssffffff|f", N_("amer_euro_flag,call_put_flag,num_time_steps, spot, strike, time, rate, volatility, cost_of_carry"),
	  &help_opt_binomial, opt_binomial, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },



	{ NULL}
};
