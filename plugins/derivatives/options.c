/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t;
c-basic-offset: 8 -*- */
/*
 * Options pricing
 *
 * Authors:
 *   Elliot Lee <sopwith@redhat.com> All initial work.
 *   Morten Welinder <terra@diku.dk> Port to new plugin framework.
 *                                         Cleanup.
 *   Hal Ashburner <hal_ashburner@yahoo.co.uk>
 *   Black Scholes Code re-structure, optional asset leakage paramaters,
 *   American approximations and All exotic Options Functions.
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

#include <libgnome/gnome-i18n.h>
#include <math.h>
#include <string.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static gnum_float n_d(gnum_float x);
static gnum_float gf_max(gnum_float x, gnum_float y);
static int Sgn(gnum_float a);
gnum_float cum_biv_norm_dist1 (gnum_float a, gnum_float b, gnum_float rho);
static gnum_float opt_bs1 (char const *call_put_flag, gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float v, gnum_float b);

static gnum_float opt_BAW_call	   (gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v);
static gnum_float opt_BAW_put	   (gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v);
static gnum_float NRA_c		   (gnum_float x, gnum_float  t, gnum_float r, gnum_float b, gnum_float v);
static gnum_float NRA_p		   (gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v);
static gnum_float opt_bjerStens1_c (gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v);
/* static gnum_float opt_bjerStens1_p (gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v); */
static gnum_float opt_bjerStens1   (char const *call_put_flag, gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v);
static gnum_float phi		   (gnum_float s, gnum_float t, gnum_float gamma, gnum_float H, gnum_float I, gnum_float r, gnum_float b, gnum_float v);
static gnum_float CriticalValueOptionsOnOptions (char const *call_put_flag, gnum_float x1, gnum_float x2, gnum_float t,
						 gnum_float r, gnum_float b, gnum_float v);
static gnum_float opt_crit_val_chooser (gnum_float s,gnum_float xc,gnum_float xp,gnum_float t,
					gnum_float tc, gnum_float tp, gnum_float r, gnum_float b, gnum_float v);


/* The normal distribution function */
#define calc_N(x) pnorm(x,0,1,TRUE,FALSE)

static gnum_float
n_d(gnum_float x)
{
	gnum_float gfresult;
	gfresult = 1 / sqrtgnum(2 * M_PIgnum) * expgnum(-pow(x , 2) / 2);
	return gfresult;
}

static gnum_float
gf_max(gnum_float x, gnum_float y)
{
	if (x >= y)
		return x;
	return y;
}

static int
Sgn(gnum_float a)
{
	if (a>0) return 1;
	else if (a<0) return -1;
	else return 0;
}

/* The cumulative bivariate normal distribution function */
gnum_float
cum_biv_norm_dist1 (gnum_float a, gnum_float b, gnum_float rho)
{
	gnum_float rho1, rho2 , delta;
	gnum_float a1, b1, sum = 0.0;
	int i, j;


	gnum_float x[] = {0.24840615, 0.39233107, 0.21141819, 0.03324666, 0.00082485334};
	gnum_float y[] = {0.10024215, 0.48281397, 1.0609498, 1.7797294, 2.6697604};
	a1 = a / sqrtgnum (2 * (1 - pow (rho , 2)));
	b1 = b / sqrtgnum (2 * (1 - pow (rho , 2)));

	if (a <= 0 && b <= 0 && rho <= 0) {
		for (i = 0;i!=5;++i)
			for (j = 0; j!=5; ++j)
				sum = sum + x[i] * x[j] * expgnum (a1 * (2 * y[i] - a1) + b1 * (2 *
y[j] - b1) + 2 * rho * (y[i] - a1) * (y[j] - b1));
		return (sqrtgnum (1 - pow (rho , 2)) / M_PIgnum * sum);
	} else if (a <= 0 && b >= 0 && rho >= 0)
		return (calc_N (a) - cum_biv_norm_dist1 (a,-b,-rho));
	else if (a >= 0 && b <= 0 && rho >= 0) 
		return (calc_N (b) - cum_biv_norm_dist1 (-a,b,-rho));
	else if (a >= 0 && b >= 0 && rho <= 0)
		return (calc_N (a) + calc_N (b) - 1 + cum_biv_norm_dist1 (-a,-b,rho));
	else if ((a * b * rho) > 0) {
		rho1 = (rho * a - b) * Sgn (a) / sqrtgnum (pow (a , 2) - 2 * rho * a
							   * b + pow (b , 2));
		rho2 = (rho * b - a) * Sgn (b) / sqrtgnum (pow (a , 2) - 2 * rho * a
							   * b + pow (b , 2));
		delta = (1 - Sgn (a) * Sgn (b)) / 4;
		return (cum_biv_norm_dist1 (a,0.0,rho1) + cum_biv_norm_dist1
			(b,0.0,rho2) - delta);
	}
	return -123;

}

static Value *
cum_biv_norm_dist(FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float a = value_get_as_float (argv[0]);
	gnum_float b = value_get_as_float (argv[1]);
	gnum_float rho = value_get_as_float (argv[2]);
	gnum_float result;

	result = cum_biv_norm_dist1(a,b,rho);
	if (result == -123)
		return value_new_error(ei->pos, gnumeric_err_NUM);
	else
		return value_new_float(result);
}

static const char *help_cum_biv_norm_dist = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=CUM_BIV_NORM_DIST\n"
	
	   "@SYNTAX=CUM_BIV_NORM_DIST(a,b,rho)\n"
	   "@DESCRIPTION="
	   "CUM_BIV_NORM_DIST calculates the cumulative bivariate"
	   "normal distribution from parameters a, b & rho")
};



/* the generalized Black and Scholes formula*/
static gnum_float
opt_bs1 (char const *call_put_flag,
	 gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float v,
	 gnum_float b)
{
	gnum_float d1;
	gnum_float d2;

	d1 = (loggnum (s / x) + (b + pow (v , 2) / 2) * t) / (v * sqrtgnum (t));
	d2 = d1 - v * sqrtgnum (t);

	if (!strcmp(call_put_flag , "c"))
		return (s * expgnum ((b - r) * t) * calc_N (d1) -
			x * expgnum (-r * t) * calc_N (d2));
	else if (!strcmp(call_put_flag , "p"))
		return (x * expgnum (-r * t) * calc_N (-d2) -
			s * expgnum ((b - r) * t) * calc_N (-d1));
	else
		return (-1);
}


static Value *
opt_bs (FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float (argv[1]);
	gnum_float x = value_get_as_float (argv[2]);
	gnum_float t = value_get_as_float (argv[3]);
	gnum_float r = value_get_as_float (argv[4]);
	gnum_float v = value_get_as_float (argv[5]);
	gnum_float b = 0;
	gnum_float gfresult;
	if (argv[6]) b = value_get_as_float (argv[6]);
		gfresult = opt_bs1 (call_put_flag,s,x,t,r,v,b);
	g_free (call_put_flag);
 	if (gfresult == -1)
		return value_new_error (ei->pos, gnumeric_err_NUM);
	return value_new_float (gfresult);
}

static const char *help_opt_bs = {
	N_("@FUNCTION=opt_bs\n"
	   "@SYNTAX=OPT_BS(call_put_flag,spot,strike,time,rate,volatility,"
	   "cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_BS uses the Black-Scholes model to calculate the price of "
	   "a European option using call_put_flag, @call_put_flag struck at "
	   "@strike on an asset with spot price @spot.\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the asset "
	   "or the period through to the exercise date. "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed in the same units as "
	   "@strike and @spot.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS_PUT, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, "
	   "OPT_BS_VEGA, OPT_BS_GAMMA")
};

/* Delta for the generalized Black and Scholes formula */
static gnum_float
opt_bs_delta1 (char const* call_put_flag,
	       gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float v, gnum_float b)
{
	gnum_float d1;
	gnum_float gfresult = 0;

	d1 = (loggnum (s / x) + (b + pow (v , 2) / 2) * t) / (v * sqrtgnum
							      (t));

	if (!strcmp(call_put_flag , "c"))
	{
		gfresult = (expgnum ((b - r) * t) * calc_N (d1));
	}	
	else if (!strcmp(call_put_flag , "p"))
		gfresult = (expgnum ((b - r) * t) * (calc_N (d1) - 1));
	else  gfresult = -123; /*should never get to*/
	return gfresult;
}


static Value *
opt_bs_delta (FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string (argv[0]);
	gnum_float s = value_get_as_float (argv[1]);
	gnum_float x = value_get_as_float (argv[2]);
	gnum_float t = value_get_as_float (argv[3]);
	gnum_float r = value_get_as_float (argv[4]);
	gnum_float v = value_get_as_float (argv[5]);
	gnum_float b = 0;
	gnum_float gfresult = 0;

	if (argv[6]) b = value_get_as_float (argv[6]);

	gfresult = opt_bs_delta1 (call_put_flag,s,x,t,r,v,b);
	g_free (call_put_flag);

	if (gfresult ==-123)
		return value_new_error(ei->pos, gnumeric_err_NUM);

	return value_new_float(gfresult);
}

static char const *help_opt_bs_delta = {
	N_("@FUNCTION=OPT_BS_DELTA\n"

	   "@SYNTAX=OPT_BS_DELTA(call_put_flag,spot,strike,time,rate,"
	   "volatility,cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_BS_DELTA uses the Black-Scholes model to calculate the "
	   "\"delta\" of a European option with call_put_flag, @call_put_flag"
	   "struck at @strike on an asset with spot price @spot.\n"
	   "\n"
	   "(The delta of an option is the rate of change of its price with "
	   "respect to the spot price of the underlying asset.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date.  "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change of "
	   "option value per unit change in @spot."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_RHO, OPT_BS_THETA, "
	   "OPT_BS_VEGA, OPT_BS_GAMMA")
};


/* Gamma for the generalized Black and Scholes formula */
static gnum_float
opt_bs_gamma1 (gnum_float s,gnum_float x,gnum_float t,gnum_float r,gnum_float v,gnum_float b)
{
	gnum_float d1;

	d1 = (loggnum (s / x) + (b + pow (v , 2) / 2) * t) / (v * sqrtgnum
							      (t));
	return (expgnum ((b - r) * t) * n_d (d1) / (s * v *
						    sqrtgnum (t)));
}


static Value*
opt_bs_gamma (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float s = value_get_as_float (argv[0]);
	gnum_float x = value_get_as_float (argv[1]);
	gnum_float t = value_get_as_float (argv[2]);
	gnum_float r = value_get_as_float (argv[3]);
	gnum_float v = value_get_as_float (argv[4]);
	gnum_float b = 0;
	gnum_float gfresult;
	if (argv[5]) b = value_get_as_float (argv[5]);

	gfresult = opt_bs_gamma1(s,x,t,r,v,b);
	return value_new_float(gfresult);
}

static char const *help_opt_bs_gamma = {
	N_("@FUNCTION=OPT_BS_GAMMA\n"

	   "@SYNTAX=OPT_BS_GAMMA(spot,strike,time,rate,volatility,"
	   "cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_BS_GAMMA uses the Black-Scholes model to calculate the "
	   "\"gamma\" of a European option struck at @strike on an asset "
	   "with spot price @spot.\n"
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
	   "of delta per unit change in @spot."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_VEGA")
};

/* theta for the generalized Black and Scholes formula */
static gnum_float
opt_bs_theta1 (char const *call_put_flag,
	       gnum_float s,gnum_float x,gnum_float t,gnum_float r,gnum_float v,gnum_float b)
{
	gnum_float d1, d2;
	gnum_float gfresult =0;

	d1 = (loggnum (s / x) + (b + pow (v , 2) / 2) * t) / (v * sqrtgnum
							      (t));
	d2 = d1 - v * sqrtgnum (t);

	if (!strcmp(call_put_flag , "c"))
	{
		gfresult = (-s * expgnum ((b - r) * t) * n_d (d1) * v / (2 * sqrtgnum (t)) - (b - r) * s * expgnum ((b - r) * t) * calc_N (d1) - r * x
			    * expgnum (-r * t) * calc_N (d2));
	}
	else	if (!strcmp(call_put_flag , "p"))
	{
		gfresult = (-s * expgnum ((b - r) * t) * n_d (d1) * v /
			    (2 * sqrtgnum (t))  + (b - r) * s * expgnum ((b - r) * t) * calc_N(-d1) + r * x * expgnum (-r * t) * calc_N (-d2));
	}
	else  gfresult = -123;
	return gfresult;
}

static Value*
opt_bs_theta (FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string (argv[0]);
	gnum_float s = value_get_as_float (argv[1]);
	gnum_float x = value_get_as_float (argv[2]);
	gnum_float t = value_get_as_float (argv[3]);
	gnum_float r = value_get_as_float (argv[4]);
	gnum_float v = value_get_as_float (argv[5]);
	gnum_float b = 0;
	gnum_float gfresult =0;

	if (argv[6])
		b = value_get_as_float (argv[6]);

	gfresult = opt_bs_theta1(call_put_flag,s,x,t,r,v,b);
	g_free (call_put_flag);
	if (gfresult == -123)
		return value_new_error(ei->pos, gnumeric_err_NUM);
	return value_new_float(gfresult);
}

static char const *help_opt_bs_theta = {
	N_("@FUNCTION=OPT_BS_THETA\n"

	   "@SYNTAX=OPT_BS_THETA(call_put_flag,spot,strike,time,rate,"
	   "volatility,cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_BS_THETA uses the Black-Scholes model to calculate the "
	   "\"theta\" of a European option with call_put_flag, @call_put_flag "
	   "struck at @strike on an asset with spot price @spot.\n"
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
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, "
	   "OPT_BS_VEGA, OPT_BS_GAMMA")
};


/* Vega for the generalized Black and Scholes formula */
gnum_float
opt_bs_vega1 (gnum_float s,gnum_float x,gnum_float t,gnum_float r,gnum_float v,gnum_float b)
{
	gnum_float d1;

	d1 = (loggnum (s / x) + (b + pow (v , 2) / 2) * t) / (v * sqrtgnum(t));
	return (s * expgnum ((b - r) * t) * n_d (d1) * sqrtgnum(t));
}

static Value*
opt_bs_vega (FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float s = value_get_as_float (argv[0]);
	gnum_float x = value_get_as_float (argv[1]);
	gnum_float t = value_get_as_float (argv[2]);
	gnum_float r = value_get_as_float (argv[3]);
	gnum_float v = value_get_as_float (argv[4]);
	gnum_float b = 0;
	gnum_float gfresult;

	if (argv[5]) b = value_get_as_float (argv[5]);

	gfresult=opt_bs_vega1(s,x,t,r,v,b);
	return value_new_float(gfresult);
}

static char const *help_opt_bs_vega = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_VEGA\n"

	   "@SYNTAX=OPT_BS_VEGA(call_put_flag,spot,strike,time,rate,volatility,"
	   "cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_BS_VEGA uses the Black-Scholes model to calculate the "
	   "\"vega\" of a European option struck at @strike on an asset "
	   "with spot price @spot.\n"
	   "\n"
	   "(The vega of an option is the rate of change of its price with "
	   "respect to volatility, and is the same for calls and puts.)\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date. "
	   "@days_to_maturity the number of days to exercise, and @rate is "
	   "the risk-free interest rate to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of option value, per 100% volatilty."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Rho for the generalized Black and Scholes formula */
static gnum_float
opt_bs_rho1 (char const *call_put_flag,gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float v, gnum_float b)
{

	gnum_float d1, d2;
	gnum_float gfresult =0;


	d1 = (loggnum (s / x) + (b + pow (v , 2) / 2) * t) / (v * sqrtgnum
							      (t));
	d2 = d1 - v * sqrtgnum (t);
	if (!strcmp(call_put_flag , "c")) {
		if (b != 0)
			gfresult =  (t * x * expgnum (-r * t) * calc_N (d2));
		else
			gfresult =  (-t *  opt_bs1 (call_put_flag, s, x, t, r, v,
						    b));
	}

	else if (!strcmp(call_put_flag , "p")) {
		if (b != 0)
			gfresult = (-t * x * expgnum (-r * t) * calc_N (-d2));
		else
			gfresult = (-t * opt_bs1 (call_put_flag, s, x, t, r, v,
						  b));
	}
	else gfresult = -123;
	return gfresult;
}


static Value*
opt_bs_rho (FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string (argv[0]);
	gnum_float s = value_get_as_float (argv[1]);
	gnum_float x = value_get_as_float (argv[2]);
	gnum_float t = value_get_as_float (argv[3]);
	gnum_float r = value_get_as_float (argv[4]);
	gnum_float v = value_get_as_float (argv[5]);
	gnum_float b = 0;
	gnum_float gfresult =0;
	if (argv[6]) b = value_get_as_float (argv[6]);

	gfresult= opt_bs_rho1(call_put_flag,s,x,t,r,v,b);
	g_free (call_put_flag);
	if (gfresult == -123)
		return value_new_error(ei->pos, gnumeric_err_NUM);
	return value_new_float(gfresult);
}

static char const *help_opt_bs_rho = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_RHO\n"

	   "@SYNTAX=OPT_BS_RHO(call_put_flag,spot,strike,time,rate,volatility,"
	   "cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_BS_RHO uses the Black-Scholes model to calculate the "
	   "\"rho\" of a European option with call_put_flag, @call_put_flag "
	   "struck at @strike on an asset with spot price @spot.\n"
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
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_THETA, OPT_BS_VEGA, OPT_BS_GAMMA")
};

/* Carry for the generalized Black and Scholes formula */
gnum_float
opt_bs_carrycost1 (char const *call_put_flag, gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float v, gnum_float b)
{
	gnum_float d1;
	gnum_float gfresult = 0;


	d1 = (loggnum (s / x) + (b + pow (v , 2) / 2) * t) / (v * sqrtgnum
							      (t));
	if (!strcmp(call_put_flag , "c"))
		gfresult =  (t * s * expgnum ((b - r) * t) * calc_N (d1));
	else if (!strcmp(call_put_flag , "p"))
		gfresult = (-t * s * expgnum ((b - r) * t) * calc_N
			    (-d1));
	else gfresult = -123; /*should never get to here*/

	return gfresult;
} /*end func*/

static Value*
opt_bs_carrycost (FunctionEvalInfo *ei, Value *argv[])
{
	char* call_put_flag = value_get_as_string (argv[0]);
	gnum_float s = value_get_as_float (argv[1]);
	gnum_float x = value_get_as_float (argv[2]);
	gnum_float t = value_get_as_float (argv[3]);
	gnum_float r = value_get_as_float (argv[4]);
	gnum_float v = value_get_as_float (argv[5]);
	gnum_float b = 0;
	gnum_float gfresult = 0;

	if (argv[6]) b = value_get_as_float (argv[6]);

	gfresult = opt_bs_carrycost1(call_put_flag, s,x,t,r,v,b);
	g_free (call_put_flag);
	if (gfresult == -123)
		return value_new_error(ei->pos, gnumeric_err_NUM);
	return value_new_float(gfresult);
}


static char const *help_opt_bs_carrycost = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BS_CARRYCOST\n"

	   "@SYNTAX=OPT_BS_CARRYCOST(call_put_flag,spot,strike,time,rate,"
	   "volatility,cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_BS_CARRYCOST uses the Black-Scholes model to calculate the "
	   "\"elasticity\" of a European option struck at @strike on an asset"
	   "with spot price @spot.\n"
	   "\n"
	   "(The elasticity of an option is the rate of change of its price"
	   "with respect to its cost of carry.)\n"
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
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Currency Options - Garman and Kohlhagen */
static gnum_float
opt_garman_kohlhagen1(char const *call_put_flag,
		      gnum_float s, gnum_float x, gnum_float t,
		      gnum_float r, gnum_float rf, gnum_float v)
{


	gnum_float d1, d2;

	d1 = (loggnum(s / x) + (r - rf + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
	d2 = d1 - v * sqrtgnum(t);
	if (strcmp(call_put_flag , "c"))
		return (s * expgnum(-rf * t) * calc_N(d1) - x * expgnum(-r * t) * calc_N(d2));
	else if (strcmp(call_put_flag , "p"))
		return (x * expgnum(-r * t) * calc_N(-d2) - s * expgnum(-rf * t) * calc_N(-d1));
	return -123; /*should never get to here*/


} /*end func*/

static Value*
opt_garman_kohlhagen(FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x = value_get_as_float(argv[2]);
	gnum_float t = value_get_as_float(argv[3]);
	gnum_float r = value_get_as_float(argv[4]);
	gnum_float rf = value_get_as_float(argv[5]);
	gnum_float v = value_get_as_float(argv[6]);
	gnum_float gfresult;

	gfresult = opt_garman_kohlhagen1(call_put_flag, s, x, t, r, rf, v);
	g_free (call_put_flag);
	if (gfresult ==-123)
		return value_new_error(ei->pos, gnumeric_err_NUM);
	else
		return value_new_float(gfresult);
}

static char const *help_opt_garman_kohlhagen = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_GARMAN_KOHLHAGEN\n"

	   "@SYNTAX=OPT_GARMAN_KOHLHAGEN(call_put_flag,spot,strike,time,"
	   "domesitc_rate,foreign_rate,volatility,cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_GARMAN_KOHLHAGEN values the theoretical price of a European "
	   "option struck at @strike on an asset with spot price @spot.\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date. "
	   "@days_to_maturity the number of days to exercise, and "
	   "@domestic_rate is the domestic risk-free interest rate to the "
	   "exercise date,@foreign_rate is the foreign risk-free interest rate "
	   "to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change "
	   "of option value, per 100% volatilty."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};


/* French (1984) adjusted Black and scholes model for trading day volatility */
static gnum_float
opt_french1 (char const *call_put_flag, gnum_float s, gnum_float  x, gnum_float t, gnum_float t1,
	    gnum_float r, gnum_float v, gnum_float  b)
{
	gnum_float d1, d2;

	d1 = (loggnum(s / x) + b * t + pow(v , 2) / 2 * t1) / (v * sqrtgnum(t1));
	d2 = d1 - v * sqrtgnum(t1);

	if (!strcmp(call_put_flag , "c"))
		return (s * expgnum((b - r) * t) * calc_N(d1) - x * expgnum(-r * t) * calc_N(d2));
	else if (!strcmp(call_put_flag , "p"))
		return (x * expgnum(-r * t) * calc_N(-d2) - s * expgnum((b - r) * t) * calc_N(-d1));
	else return -123;
} /*end func*/


static Value*
opt_french(FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x = value_get_as_float(argv[2]);
	gnum_float t = value_get_as_float(argv[3]);
	gnum_float t1 = value_get_as_float(argv[4]);
	gnum_float r = value_get_as_float(argv[5]);
	gnum_float v = value_get_as_float(argv[6]);
	gnum_float b = value_get_as_float(argv[7]);				
	gnum_float gfresult;

	gfresult = opt_french1(call_put_flag, s, x, t, t1, r, b, v);
	g_free (call_put_flag);
	if (gfresult ==-123)
		return value_new_error(ei->pos, gnumeric_err_NUM);
	else
		return value_new_float(gfresult);
}

static char const *help_opt_french = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_FRENCH\n"

	   "@SYNTAX=OPT_FRENCH(call_put_flag,spot,strike,time,t2,rate,"
	   "volatility,cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_FRENCH values the theoretical price of a "
	   "European option adjusted for trading day volatility, struck at "
	   "@strike on an asset with spot price @spot.\n"
	   "\n"
	   "@volatility is the annualized volatility, in percent, of the "
	   "asset for the period through to the exercise date. "
	   "@days_to_maturity the number of days to exercise, and "
	   "@domestic_rate is the domestic risk-free interest rate to the "
	   "exercise date,@foreign_rate is the foreign risk-free interest rate "
	   "to the exercise date, in percent.\n"
	   "\n"
	   "* The returned value will be expressed as the rate of change of "
	   "option value, per 100% volatilty."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};

/* Merton jump diffusion model*/
static gnum_float
opt_jump_diff1 (char const *call_put_flag, gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float v,
	       gnum_float lambda, gnum_float gamma)
{
	gnum_float delta, sum;
	gnum_float Z, vi;
	int i;

	delta = sqrtgnum(gamma * pow(v , 2) / lambda);
	Z = sqrtgnum(pow(v , 2) - lambda * pow(delta , 2));
	sum = 0;
	for(i = 0; i!=11; ++i)
	{
		vi = sqrtgnum(pow(Z , 2) + pow(delta , 2) * (i / t));
		sum = sum + expgnum(-lambda * t) * pow((lambda * t) , i) / fact(i) *
			opt_bs1(call_put_flag, s, x, t, r, r, vi);
	}
	return sum;
} /*end func*/

static Value*
opt_jump_diff(FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x = value_get_as_float(argv[2]);
	gnum_float t = value_get_as_float(argv[3]);
	gnum_float r = value_get_as_float(argv[4]);
	gnum_float v = value_get_as_float(argv[5]);
	gnum_float lambda = value_get_as_float(argv[6]);
	gnum_float gamma = value_get_as_float(argv[7]);
	gnum_float gfresult = 0;

	gfresult = opt_jump_diff1(call_put_flag, s, x, t, r, v, lambda, gamma);
	g_free (call_put_flag);
	return value_new_float(gfresult);
}

static const char *help_opt_jump_diff = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_JUMP_DIFF\n"

	   "@SYNTAX=OPT_JUMP_DIFF(call_put_flag,spot,strike,time,rate,"
	   "volatility,lambda,gamma)\n"
	   "@DESCRIPTION="
	   "OPT_JUMP_DIFF models the theoretical price of an option according "
	   "to the Jump Diffussion process (Merton)."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};



/* Miltersen schwartz (1997) commodity option model */
static gnum_float
opt_miltersen_schwartz1 (char const *call_put_flag, gnum_float p_t, gnum_float f_t, gnum_float x, gnum_float t1,
			 gnum_float t2, gnum_float v_s, gnum_float v_e, gnum_float v_f, gnum_float rho_se,
			 gnum_float rho_sf, gnum_float rho_ef, gnum_float kappa_e, gnum_float kappa_f)
{
	gnum_float vz, vxz;
	gnum_float d1, d2;
	gnum_float gfresult;

	vz = pow(v_s , 2) * t1 + 2 * v_s * (v_f * rho_sf * 1 / kappa_f * (t1 - 1 / kappa_f * expgnum(-kappa_f * t2) * (expgnum(kappa_f * t1) - 1))
					    - v_e * rho_se * 1 / kappa_e * (t1 - 1 / kappa_e * expgnum(-kappa_e * t2) * (expgnum(kappa_e * t1) - 1)))
		+ pow(v_e , 2) * 1 / pow(kappa_e , 2) * (t1 + 1 / (2 * kappa_e) * expgnum(-2 * kappa_e * t2) * (expgnum(2 * kappa_e * t1) - 1)
							 - 2 * 1 / kappa_e * expgnum(-kappa_e * t2) * (expgnum(kappa_e * t1) - 1))
		+ pow(v_f , 2) * 1 / pow(kappa_f , 2) * (t1 + 1 / (2 * kappa_f) * expgnum(-2 * kappa_f * t2) * (expgnum(2 * kappa_f * t1) - 1)
							 - 2 * 1 / kappa_f * expgnum(-kappa_f * t2) * (expgnum(kappa_f * t1) - 1))
		- 2 * v_e * v_f * rho_ef * 1 / kappa_e * 1 / kappa_f * (t1 - 1 / kappa_e * expgnum(-kappa_e * t2) * (expgnum(kappa_e * t1) - 1)
									- 1 / kappa_f * expgnum(-kappa_f * t2) * (expgnum(kappa_f * t1) - 1)
									+ 1 / (kappa_e + kappa_f) * expgnum(-(kappa_e + kappa_f) * t2) * (expgnum((kappa_e + kappa_f) * t1) - 1));

	vxz = v_f * 1 / kappa_f * (v_s * rho_sf * (t1 - 1 / kappa_f * (1 - expgnum(-kappa_f * t1)))
				   + v_f * 1 / kappa_f * (t1 - 1 / kappa_f * expgnum(-kappa_f * t2) * (expgnum(kappa_f * t1) - 1) - 1 / kappa_f * (1 - expgnum(-kappa_f * t1))
							  + 1 / (2 * kappa_f) * expgnum(-kappa_f * t2) * (expgnum(kappa_f * t1) - expgnum(-kappa_f * t1)))
				   - v_e * rho_ef * 1 / kappa_e * (t1 - 1 / kappa_e * expgnum(-kappa_e * t2) * (expgnum(kappa_e * t1) - 1) - 1 / kappa_f * (1 - expgnum(-kappa_f * t1))
								   + 1 / (kappa_e + kappa_f) * expgnum(-kappa_e * t2) * (expgnum(kappa_e * t1) - expgnum(-kappa_f * t1))));

	vz = sqrtgnum(vz);

	d1 = (loggnum(f_t / x) - vxz + pow(vz , 2) / 2) / vz;
	d2 = (loggnum(f_t / x) - vxz - pow(vz , 2) / 2) / vz;

	if (!strcmp(call_put_flag , "c"))
		gfresult = p_t * (f_t * expgnum(-vxz) * calc_N(d1) - x * calc_N(d2));
	else if(!strcmp(call_put_flag , "p"))
		gfresult = p_t * (x * calc_N(-d2) - f_t * expgnum(-vxz) * calc_N(-d1));
	else gfresult =-123;
	return gfresult;

} /*end func*/

static Value*
opt_miltersen_schwartz(FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float p_t = value_get_as_float(argv[1]);
	gnum_float f_t = value_get_as_float(argv[2]);
	gnum_float x = value_get_as_float(argv[3]);
	gnum_float t1 = value_get_as_float(argv[4]);
	gnum_float t2 = value_get_as_float(argv[5]);
	gnum_float v_s = value_get_as_float(argv[6]);
	gnum_float v_e = value_get_as_float(argv[7]);
	gnum_float v_f = value_get_as_float(argv[8]);
	gnum_float rho_se = value_get_as_float(argv[9]);
	gnum_float rho_sf = value_get_as_float(argv[10]);		
	gnum_float rho_ef = value_get_as_float(argv[11]);
	gnum_float kappa_e = value_get_as_float(argv[12]);
	gnum_float kappa_f = value_get_as_float(argv[13]);


	gnum_float gfresult = 0;

	gfresult = opt_miltersen_schwartz1(call_put_flag, p_t, f_t, x, t1, t2, v_s, v_e, v_f, rho_se, rho_sf, rho_ef, kappa_e, kappa_f);
	g_free (call_put_flag);
	if (gfresult != -123)
		return value_new_float(gfresult);
	return value_new_error(ei->pos, gnumeric_err_NUM);
}


static char const *help_opt_miltersen_schwartz = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_MILTERSEN_SCHWARTZ\n"

	   "@SYNTAX=OPT_MILTERSEN_SCHWARTZ(call_put_flag,p_t,f_t,x,t1,t2,v_s,"
	   "v_e,v_f,rho_se,rho_sf,rho_ef,kappa_e,kappa_f)\n"
	   "@DESCRIPTION="
	   "OPT_MILTERSEN_SCHWARTZ models the theoretical price of an option "
	   "according to Miltersen & Schwartz."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};






/* American options */


/* American Calls on stocks with known dividends, Roll-Geske-Whaley */
static gnum_float opt_rgw1(gnum_float s, gnum_float x, gnum_float t1, gnum_float t2, gnum_float r, gnum_float d, gnum_float v)
	/*t1 time to dividend payout
	  t2 time to option expiration */
{
	gnum_float sx, i;
	gnum_float a1, a2, b1, b2;
	gnum_float HighS, LowS, epsilon;
	gnum_float ci, infinity;
	gnum_float gfresult;

	infinity = 100000000;
	epsilon = 0.00001;
	sx = s - d * expgnum(-r * t1);
	if (d <= (x * (1 - expgnum(-r * (t2 - t1))))) /* Not optimal to exercise*/
	{
		return opt_bs1("c", sx, x, t2, r, v,0);
	} /*end if statement*/
	ci = opt_bs1("c", s, x, t2 - t1, r, v,0);
	HighS = s;
	while ((ci - HighS - d + x) > 0 && HighS < infinity)
	{
		HighS *= 2;
		ci = opt_bs1("c", HighS, x, t2 - t1, r, v,0);
	}
	if (HighS > infinity)
		return opt_bs1("c", sx, x, t2, r, v,0);

	/*end if statement*/

	LowS = 0;
	i = HighS * 0.5;
	ci = opt_bs1("c", i, x, t2 - t1, r, v,0);

	/* search algorithm to find the critical stock price i*/
	while (fabs(ci - i - d + x) > epsilon && HighS - LowS > epsilon)
	{
		if ((ci - i - d + x) < 0)
			HighS = i;
		else
			LowS = i;
		/*end if statement*/
		i = (HighS + LowS) / 2;
		ci = opt_bs1("c", i, x, (t2 - t1), r, v,0);
	} /* end while statement */



	a1 = (loggnum(sx / x) + (r + pow(v , 2) / 2) * t2) / (v * sqrtgnum(t2));
	a2 = a1 - v * sqrtgnum(t2);
	b1 = (loggnum(sx / i) + (r + pow(v , 2) / 2) * t1) / (v * sqrtgnum(t1));
	b2 = b1 - v * sqrtgnum(t1);

	gfresult = sx * calc_N(b1) + sx * cum_biv_norm_dist1(a1, -b1, -sqrtgnum(t1 / t2))
		- x * expgnum(-r * t2) * cum_biv_norm_dist1(a2, -b2, -sqrtgnum(t1 / t2)) - (x - d)
		* expgnum(-r * t1) * calc_N(b2);
	return gfresult;
} /*end func*/


static Value*
opt_rgw(FunctionEvalInfo *ei, Value *argv[])
{
	gnum_float s = value_get_as_float(argv[0]);
	gnum_float x = value_get_as_float(argv[1]);
	gnum_float t1 = value_get_as_float(argv[2]);
	gnum_float t2 = value_get_as_float(argv[3]);
	gnum_float r = value_get_as_float(argv[4]);
	gnum_float d = value_get_as_float(argv[5]);
	gnum_float v = value_get_as_float(argv[6]);
	gnum_float gfresult = 0;

	gfresult = opt_rgw1(s, x, t1, t2, r, d, v);

	return value_new_float(gfresult);
}

static char const *help_opt_rgw = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_RGW\n"

	   "@SYNTAX=OPT_RGW(call_put_flag,spot,strike,t1,t2,rate,d,volatility)"
	   "\n"
	   "@DESCRIPTION="
	   "OPT_RGW models the theoretical price of an option according to "
	   "the Roll Geske Whaley apporximation where t1 is the time to the "
	   "dividend payout and t2 is the time to option expiration."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};

/* the Barone-Adesi and Whaley (1987) American approximation */
static Value*
opt_BAW_amer (FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x = value_get_as_float(argv[2]);
	gnum_float t = value_get_as_float(argv[3]);
	gnum_float r = value_get_as_float(argv[4]);
	gnum_float b = value_get_as_float(argv[5]);
	gnum_float v = value_get_as_float(argv[6]);
	gnum_float gfresult = 0;

	if (!strcmp(call_put_flag , "c"))
		gfresult = opt_BAW_call(s, x, t, r, b, v);
	else if(!strcmp(call_put_flag , "p"))
		gfresult = opt_BAW_put(s, x, t, r, b, v);
	else gfresult = -123;

	g_free (call_put_flag);
	if (gfresult == -123)
		return value_new_error(ei->pos, gnumeric_err_NUM);
	return value_new_float(gfresult);
}

static char const *help_opt_BAW_amer = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BAW_AMER\n"

	   "@SYNTAX=OPT_BAW_AMER(call_put_flag,spot,strike,time,rate,"
	   "cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_BAW_AMER models the theoretical price of an option according "
	   "to the Barone Adesie & Whaley approximation."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};

/* American call */
static gnum_float
opt_BAW_call(gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float sk, n, k;
	gnum_float d1, q2, a2;
	gnum_float gfresult;
	if (b >= r)
		gfresult = opt_bs1("c", s, x, t, r, v,b);
	else
	{
		sk = NRA_c(x, t, r, b, v);
		n = 2 * b / pow(v , 2);
		k = 2 * r / (pow(v , 2) * (1 - expgnum(-r * t)));
		d1 = (loggnum(sk / x) + (b + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
		q2 = (-(n - 1) + sqrtgnum(pow((n - 1) , 2) + 4 * k)) / 2;
		a2 = (sk / q2) * (1 - expgnum((b - r) * t) * calc_N(d1));
		if (s < sk)
			gfresult = opt_bs1("c", s, x, t, r, v,b) + a2 * pow((s / sk) , q2);
		else
			gfresult = s - x;

	} /*end if statement*/
	return gfresult;
} /*end func*/





/* Newton Raphson algorithm to solve for the critical commodity price for a Call */
static gnum_float
NRA_c(gnum_float x, gnum_float  t, gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float n, m;
	gnum_float su, si;
	gnum_float  h2, k;
	gnum_float d1, q2, q2u;
	gnum_float LHS, RHS;
	gnum_float bi, e;

	/* Calculation of seed value, si */
	n = 2 * b / pow(v , 2);
	m = 2 * r / pow(v , 2);
	q2u = (-(n - 1) + sqrtgnum(pow((n - 1) , 2) + 4 * m)) / 2;
	su = x / (1 - 1 / q2u);
	h2 = -(b * t + 2 * v * sqrtgnum(t)) * x / (su - x);
	si = x + (su - x) * (1 - expgnum(h2));

	k = 2 * r / (pow(v , 2) * (1 - expgnum(-r * t)));
	d1 = (loggnum(si / x) + (b + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
	q2 = (-(n - 1) + sqrtgnum(pow((n - 1) , 2) + 4 * k)) / 2;
	LHS = si - x;
	RHS = opt_bs1("c", si, x, t, r, v, b) + (1 - expgnum((b - r) * t) * calc_N(d1)) * si / q2;
	bi = expgnum((b - r) * t) * calc_N(d1) * (1 - 1 / q2)
		+ (1 - expgnum((b - r) * t) * calc_N(d1) / (v * sqrtgnum(t))) / q2;
	e = 0.000001;

	/* Newton Raphson algorithm for finding critical price si */
	while ((fabs(LHS - RHS) / x) > e)
	{
		si = (x + RHS - bi * si) / (1 - bi);
		d1 = (loggnum(si / x) + (b + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
		LHS = si - x;
		RHS = opt_bs1("c", si, x, t, r, v, b) + (1 - expgnum((b - r) * t) * calc_N(d1)) * si / q2;
		bi = expgnum((b - r) * t) * calc_N(d1) * (1 - 1 / q2)
			+ (1 - expgnum((b - r) * t) * n_d(d1) / (v * sqrtgnum(t))) / q2;
	}
	return si;
}

static gnum_float
opt_BAW_put (gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float sk, n, k;
	gnum_float d1, q1, a1;
	gnum_float gfresult;

	sk = NRA_p(x, t, r, b, v);
	n = 2 * b / pow(v , 2);
	k = 2 * r / (pow(v , 2) * (1 - expgnum(-r * t)));
	d1 = (loggnum(sk / x) + (b + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
	q1 = (-(n - 1) - sqrtgnum(pow((n - 1) , 2) + 4 * k)) / 2;
	a1 = -(sk / q1) * (1 - expgnum((b - r) * t) * calc_N(-d1));

	if (s > sk)
		gfresult = opt_bs1("p", s, x, t, r, v, b) + a1 * pow((a1 / sk) , q1);
	else
		gfresult = x - s;

	return gfresult;
}

/* Newton Raphson algorithm to solve for the critical commodity price for a Put*/
static gnum_float
NRA_p(gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v)
{

	gnum_float n, m;
	gnum_float su, si;
	gnum_float h1, k;
	gnum_float d1, q1u, q1;
	gnum_float LHS, RHS;
	gnum_float bi, e;

	/* Calculation of seed value, si */
	n = 2 * b / pow(v , 2);
	m = 2 * r / pow(v , 2);
	q1u = (-(n - 1) - sqrtgnum(pow((n - 1) , 2) + 4 * m)) / 2;
	su = x / (1 - 1 / q1u);
	h1 = (b * t - 2 * v * sqrtgnum(t)) * x / (x - su);
	si = su + (x - su) * expgnum(h1);

	k = 2 * r / (pow(v , 2) * (1 - expgnum(-r * t)));
	d1 = (loggnum(si / x) + (b + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
	q1 = (-(n - 1) - sqrtgnum(pow((n - 1) , 2) + 4 * k)) / 2;
	LHS = x - si;
	RHS = opt_bs1("p", si, x, t, r, v, b) - (1 - expgnum((b - r) * t) * calc_N(-d1)) * si / q1;
	bi = -expgnum((b - r) * t) * calc_N(-d1) * (1 - 1 / q1)
		- (1 + expgnum((b - r) * t) * n_d(-d1) / (v * sqrtgnum(t))) / q1;
	e = 0.000001;

	/* Newton Raphson algorithm for finding critical price si */
	while((fabs(LHS - RHS) / x) > e) {
		si = (x - RHS + bi * si) / (1 + bi);
		d1 = (loggnum(si / x) + (b + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
		LHS = x - si;
		RHS = opt_bs1("p", si, x, t, r, v, b) - (1 - expgnum((b - r) * t) * calc_N(-d1)) * si / q1;
		bi = -expgnum((b - r) * t) * calc_N(-d1) * (1 - 1 / q1)
			- (1 + expgnum((b - r) * t) * calc_N(-d1) / (v * sqrtgnum(t))) / q1;
	}
	return si;
}

/* the Bjerksund and stensland (1993) American approximation */
static gnum_float
opt_bjerStens1 (char const *call_put_flag, gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float gfresult;

	if (!strcmp(call_put_flag , "c"))
		gfresult = opt_bjerStens1_c(s, x, t, r, b, v);
	else if (!strcmp(call_put_flag , "p")) /* Use the Bjerksund and stensland put-call transformation */
		gfresult = opt_bjerStens1_c(x, s, t, r - b, -b, v);
	else gfresult = -123;

	return gfresult;
}

static Value *
opt_bjerStens(FunctionEvalInfo *ei, Value *argv[])
{
	char* call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x = value_get_as_float(argv[2]);
	gnum_float t = value_get_as_float(argv[3]);
	gnum_float r = value_get_as_float(argv[4]);
	gnum_float v = value_get_as_float(argv[5]);
	gnum_float b = value_get_as_float(argv[6]);
	gnum_float gfresult;

	gfresult = opt_bjerStens1(call_put_flag, s, x, t, r, b, v);
	g_free(call_put_flag);
	return value_new_float(gfresult);
}


static char const *help_opt_bjerStens = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_BJERSTENS\n"

	   "@SYNTAX=OPT_BJERSTENS(call_put_flag,spot,strike,time,rate,"
	   "cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_BJERSTENS models the theoretical price of american options "
	   "according to the Bjerksund & Stensland approximation technique."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

static gnum_float
opt_bjerStens1_c(gnum_float s, gnum_float x, gnum_float t, gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float  BInfinity, B0;
	gnum_float  ht, I;
	gnum_float alpha, Beta;
	gnum_float gfresult;

	if (b >= r) /* Never optimal to exersice before maturity */
		gfresult = opt_bs1("c", s, x, t, r, v, b);
	else
	{
		Beta = (1 / 2 - b / pow(v , 2)) + sqrtgnum(pow((b / pow(v , 2) - 1 / 2) , 2) + 2 * r / pow(v , 2));
		BInfinity = Beta / (Beta - 1) * x;
		B0 = gf_max(x, r / (r - b) * x);
		ht = -(b * t + 2 * v * sqrtgnum(t)) * B0 / (BInfinity - B0);
		I = B0 + (BInfinity - B0) * (1 - expgnum(ht));
		alpha = (I - x) * pow(I , (-Beta));
		if (s >= I)
			gfresult = s - x;
		else
			gfresult = alpha * pow(s , Beta) - alpha * phi(s, t, Beta, I, I, r, b, v) + phi(s, t, 1, I, I, r, b, v) - phi(s, t, 1, x, I, r, b, v) - x * phi(s, t, 0, I, I, r, b, v) + x * phi(s, t, 0, x, I, r, b, v);
	} /*end if statement*/

	return gfresult;
} /*end func*/

static gnum_float
phi(gnum_float s, gnum_float t, gnum_float gamma, gnum_float H, gnum_float I, gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float lambda, kappa;
	gnum_float d;
	gnum_float gfresult;

	lambda = (-r + gamma * b + 0.5 * gamma * (gamma - 1) * pow(v , 2)) * t;
	d = -(loggnum(s / H) + (b + (gamma - 0.5) * pow(v , 2)) * t) / (v * sqrtgnum(t));
	kappa = 2 * b / (pow(v , 2)) + (2 * gamma - 1);
	gfresult = expgnum(lambda) * pow(s , gamma) * (calc_N(d) - pow((I / s) , kappa) * calc_N(d - 2 * loggnum(I / s) / (v * sqrtgnum(t))));

	return gfresult;
} /*end func*/


/* Executive stock options */
static Value *
opt_exec(FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x = value_get_as_float(argv[2]);
	gnum_float t = value_get_as_float(argv[3]);
	gnum_float r = value_get_as_float(argv[4]);
	gnum_float v = value_get_as_float(argv[5]);
	gnum_float b = value_get_as_float(argv[6]);
	gnum_float lambda = value_get_as_float(argv[7]);
	gnum_float gfresult;

	gfresult = expgnum(-lambda * t) * opt_bs1(call_put_flag, s, x, t, r, v, b);
	g_free (call_put_flag);
	return value_new_float(gfresult);
}


static char const *help_opt_exec = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_EXEC\n"

	   "@SYNTAX=OPT_EXEC(call_put_flag,spot,strike,time,rate,volatility,"
	   "cost_of_carry,lambda)\n"
	   "@DESCRIPTION="
	   "OPT_EXEC models the theoretical price of executive stock options "
	   "given lambda (jump rate for executives)."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, OPT_BS_RHO, "
	   "OPT_BS_THETA, OPT_BS_GAMMA")
};





/* Forward start options */
static Value*
opt_forward_start(FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float alpha = value_get_as_float(argv[2]);
	gnum_float t1 = value_get_as_float(argv[3]);
	gnum_float t = value_get_as_float(argv[4]);
	gnum_float r = value_get_as_float(argv[5]);
	gnum_float v = value_get_as_float(argv[6]);
	gnum_float b = value_get_as_float(argv[7]);
	gnum_float gfresult;

	gfresult = s * expgnum((b - r) * t1) * opt_bs1(call_put_flag, 1, alpha, t - t1, r, v, b);
	g_free (call_put_flag);
	return value_new_float(gfresult);

} /*end func*/


static char const *help_opt_forward_start = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_FORWARD_START\n"

	   "@SYNTAX=OPT_FORWARD_START(call_put_flag,spot,alpha,time1,time,rate,"
	   "volatility,cost_of_carry)\n"
	   "@DESCRIPTION="
	   "OPT_FORWARD_START models the theoretical price of forward start "
	   "options"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* time switch options (discrete) */
static Value*
opt_time_switch(FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x = value_get_as_float(argv[2]);
	gnum_float a = value_get_as_float(argv[3]);
	gnum_float t = value_get_as_float(argv[4]);
	gnum_float m = value_get_as_float(argv[5]);
	gnum_float dt = value_get_as_float(argv[6]);
	gnum_float r = value_get_as_float(argv[7]);
	gnum_float b = value_get_as_float(argv[8]);
	gnum_float v = value_get_as_float(argv[9]);

	gnum_float gfresult;
	gnum_float sum, d;
	int i, n, Z = 0;

	n = t / dt;
	sum = 0;
	if (!strcmp(call_put_flag , "c"))
		Z = 1;
	else if(!strcmp(call_put_flag , "p"))
		Z = -1;
	else
		gfresult = -123;

	if (Z != 0) {
		for (i = 1; i < n; ++i) {
			d = (loggnum(s / x) + (b - pow(v , 2) / 2) * i * dt) / (v * sqrtgnum(i * dt));
			sum = sum + calc_N(Z * d) * dt;
		}
		gfresult = a * expgnum (-r * t) * sum + dt * a * expgnum(-r * t) * m;
	}
	g_free (call_put_flag);
	return value_new_float (gfresult);

}

static char const *help_opt_time_switch = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_TIME_SWITCH\n"

	   "@SYNTAX=OPT_TIME_SWITCH(call_put_flag,spot,strike,a,time,m,dt,rate,"
	   "cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_TIME_SWITCH models the theoretical price of time switch "
	   "options."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* simple chooser options */
static Value*
opt_simple_chooser(FunctionEvalInfo *ei, Value *argv[])
{

	gnum_float s = value_get_as_float(argv[0]);
	gnum_float x = value_get_as_float(argv[1]);
	gnum_float t1 = value_get_as_float(argv[2]);
	gnum_float t2 = value_get_as_float(argv[3]);
	gnum_float r = value_get_as_float(argv[4]);
	gnum_float b = value_get_as_float(argv[5]);
	gnum_float v = value_get_as_float(argv[6]);

	gnum_float gfresult;

	gnum_float d, y;

	d = (loggnum(s / x) + (b + pow(v , 2) / 2) * t2) / (v * sqrtgnum(t2));
	y = (loggnum(s / x) + b * t2 + pow(v , 2) * t1 / 2) / (v * sqrtgnum(t1));

	gfresult = s * expgnum((b - r) * t2) * calc_N(d) - x * expgnum(-r * t2) * calc_N(d - v * sqrtgnum(t2))
		- s * expgnum((b - r) * t2) * calc_N(-y) + x * expgnum(-r * t2) * calc_N(-y + v * sqrtgnum(t1));

	return value_new_float(gfresult);

} /*end func*/

static char const *help_opt_simple_chooser = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_SIMPLE_CHOOSER\n"

	   "@SYNTAX=OPT_SIMPLE_CHOOSER(call_put_flag,spot,strike,time1,time2,"
	   "rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_SIMPLE_CHOOSER models the theoretical price of simple chooser "
	   "options."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Complex chooser options */
static Value*
opt_complex_chooser(FunctionEvalInfo *ei, Value *argv[])
{

	gnum_float s = value_get_as_float(argv[0]);
	gnum_float xc = value_get_as_float(argv[1]);
	gnum_float xp = value_get_as_float(argv[2]);
	gnum_float t = value_get_as_float(argv[3]);
	gnum_float tc = value_get_as_float(argv[4]);
	gnum_float tp = value_get_as_float(argv[5]);
	gnum_float r = value_get_as_float(argv[6]);
	gnum_float b = value_get_as_float(argv[7]);
	gnum_float v = value_get_as_float(argv[8]);

	gnum_float gfresult;

	gnum_float d1, d2, y1, y2;
	gnum_float rho1, rho2, I;

	I = opt_crit_val_chooser(s, xc, xp, t, tc, tp, r, b, v);
	d1 = (loggnum(s / I) + (b + pow(v , 2) / 2) * t) / (v * sqrtgnum(t));
	d2 = d1 - v * sqrtgnum(t);
	y1 = (loggnum(s / xc) + (b + pow(v , 2) / 2) * tc) / (v * sqrtgnum(tc));
	y2 = (loggnum(s / xp) + (b + pow(v , 2) / 2) * tp) / (v * sqrtgnum(tp));
	rho1 = sqrtgnum(t / tc);
	rho2 = sqrtgnum(t / tp);

	gfresult = s * expgnum((b - r) * tc) * cum_biv_norm_dist1(d1, y1, rho1) - xc * expgnum(-r * tc)
		* cum_biv_norm_dist1(d2, y1 - v * sqrtgnum(tc), rho1) - s * expgnum((b - r) * tp)
		* cum_biv_norm_dist1(-d1, -y2, rho2) + xp * expgnum(-r * tp) * cum_biv_norm_dist1(-d2, -y2 + v * sqrtgnum(tp), rho2);

	;
	return value_new_float(gfresult);

} /*end func*/

static char const *help_opt_complex_chooser = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_COMPLEX_CHOOSER\n"

	   "@SYNTAX=OPT_COMPLEX_CHOOSER(call_put_flag,spot,strike_call,"
	   "strike_put,time,time_call,time_put,rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_COMPLEX_CHOOSER models the theoretical price of complex "
	   "chooser options."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};




/* Critical value complex chooser option */
static gnum_float
opt_crit_val_chooser(gnum_float s,gnum_float xc,gnum_float xp,gnum_float t,
		     gnum_float tc, gnum_float tp, gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float sv, ci, Pi, epsilon;
	gnum_float dc, dp, yi, di;

	sv = s;


	ci = opt_bs1("c", sv, xc, tc - t, r, v, b);
	Pi = opt_bs1("p", sv, xp, tp - t, r, v, b);
	dc = opt_bs_delta1("c", sv, xc, tc - t, r, v, b);
	dp = opt_bs_delta1("p", sv, xp, tp - t, r, v, b);
	yi = ci - Pi;
	di = dc - dp;
	epsilon = 0.001;
	/* Newton-Raphson */
	while (fabs(yi) > epsilon)
	{
		sv = sv - (yi) / di;
		ci = opt_bs1("c", sv, xc, tc - t, r, v, b);
		Pi = opt_bs1("p", sv, xp, tp - t, r, v, b);
		dc = opt_bs_delta1("c", sv, xc, tc - t, r, v, b);
		dp = opt_bs_delta1("p", sv, xp, tp - t, r, v, b);
		yi = ci - Pi;
		di = dc - dp;
	}

	return sv;
} /*end func*/


/* Options on options */
static Value*
opt_on_options(FunctionEvalInfo *ei, Value *argv[])
{
	char *type_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x1 = value_get_as_float(argv[2]);
	gnum_float x2 = value_get_as_float(argv[3]);
	gnum_float t1 = value_get_as_float(argv[4]);
	gnum_float t2 = value_get_as_float(argv[5]);
	gnum_float r = value_get_as_float(argv[6]);
	gnum_float b = value_get_as_float(argv[7]);
	gnum_float v = value_get_as_float(argv[8]);

	gnum_float gfresult;

	gnum_float y1, y2, z1, z2;
	gnum_float I, rho;
	char const *call_put_flag;

	if (!strcmp (type_flag , "cc") ||
	    !strcmp (type_flag , "pc"))
		call_put_flag = "c";
	else
		call_put_flag = "p";

	I = CriticalValueOptionsOnOptions(call_put_flag, x1, x2, t2 - t1, r, b, v);

	rho = sqrtgnum(t1 / t2);
	y1 = (loggnum(s / I) + (b + pow(v , 2) / 2) * t1) / (v * sqrtgnum(t1));
	y2 = y1 - v * sqrtgnum(t1);
	z1 = (loggnum(s / x1) + (b + pow(v , 2) / 2) * t2) / (v * sqrtgnum(t2));
	z2 = z1 - v * sqrtgnum(t2);

	if (!strcmp (type_flag , "cc"))
		gfresult = s * expgnum((b - r) * t2) * cum_biv_norm_dist1(z1, y1, rho) -
			x1 * expgnum(-r * t2) * cum_biv_norm_dist1(z2, y2, rho) - x2 * expgnum(-r * t1) * calc_N(y2);
	else if (!strcmp (type_flag , "pc"))
		gfresult = x1 * expgnum(-r * t2) * cum_biv_norm_dist1(z2, -y2, -rho) -
			s * expgnum((b - r) * t2) * cum_biv_norm_dist1(z1, -y1, -rho) + x2 * expgnum(-r * t1) * calc_N(-y2);
	else if (!strcmp (type_flag , "cp"))
		gfresult = x1 * expgnum(-r * t2) * cum_biv_norm_dist1(-z2, -y2, rho) -
			s * expgnum((b - r) * t2) * cum_biv_norm_dist1(-z1, -y1, rho) - x2 * expgnum(-r * t1) * calc_N(-y2);
	else if (!strcmp (type_flag , "pp"))
		gfresult = s * expgnum((b - r) * t2) * cum_biv_norm_dist1(-z1, y1, -rho) -
			x1 * expgnum(-r * t2) * cum_biv_norm_dist1(-z2, y2, -rho) + expgnum(-r * t1) * x2 * calc_N(y2);
	else {
		g_free (type_flag);
		return value_new_error(ei->pos, gnumeric_err_VALUE);
	}
	return value_new_float(gfresult);
}

static char const *help_opt_on_options = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_ON_OPTIONS\n"

	   "@SYNTAX=OPT_ON_OPTIONS(type_flag,spot,strike1,strike1,time1,time2,"
	   "rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_ON_OPTIONS models the theoretical price of options on options"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};


/* Calculation of critical price options on options */
static gnum_float
CriticalValueOptionsOnOptions (char const *call_put_flag, gnum_float x1, gnum_float x2, gnum_float t,
			      gnum_float r, gnum_float b, gnum_float v)
{
	gnum_float si, ci, di, epsilon;

	si = x1;
	ci = opt_bs1(call_put_flag, si, x1, t, r, v, b);
	di = opt_bs_delta1(call_put_flag, si, x1, t, r, v, b);

	/* Newton-Raphson algorithm */
	epsilon = 0.0001;
	while (fabs(ci - x2) > epsilon) {
		si = si - (ci - x2) / di;
		ci = opt_bs1(call_put_flag, si, x1, t, r, v, b);
		di = opt_bs_delta1(call_put_flag, si, x1, t, r, v, b);
	}
	return si;
}

/* Writer extendible options */
static Value*
opt_extendible_writer (FunctionEvalInfo *ei, Value *argv[])
{
	char *call_put_flag = value_get_as_string(argv[0]);
	gnum_float s = value_get_as_float(argv[1]);
	gnum_float x1 = value_get_as_float(argv[2]);
	gnum_float x2 = value_get_as_float(argv[3]);
	gnum_float t1 = value_get_as_float(argv[4]);
	gnum_float t2 = value_get_as_float(argv[5]);
	gnum_float r = value_get_as_float(argv[6]);
	gnum_float b = value_get_as_float(argv[7]);
	gnum_float v = value_get_as_float(argv[8]);

	gnum_float gfresult;


	gnum_float rho, z1, z2;
	rho = sqrtgnum(t1 / t2);
	z1 = (loggnum(s / x2) + (b + pow(v , 2) / 2) * t2) / (v * sqrtgnum(t2));
	z2 = (loggnum(s / x1) + (b + pow(v , 2) / 2) * t1) / (v * sqrtgnum(t1));

	if (!strcmp(call_put_flag , "c"))
		gfresult = opt_bs1(call_put_flag, s, x1, t1, r, v, b) +
			s * expgnum((b - r) * t2) * cum_biv_norm_dist1(z1, -z2, -rho) -
			x2 * expgnum(-r * t2) * cum_biv_norm_dist1(z1 - sqrtgnum(pow(v , 2) * t2), -z2 + sqrtgnum(pow(v , 2) * t1), -rho);
	else if (!strcmp(call_put_flag , "p"))
		gfresult = opt_bs1(call_put_flag, s, x1, t1, r, v, b) +
			x2 * expgnum(-r * t2) * cum_biv_norm_dist1(-z1 + sqrtgnum(pow(v , 2) * t2), z2 - sqrtgnum(pow(v , 2) * t1), -rho) -
			s * expgnum((b - r) * t2) * cum_biv_norm_dist1(-z1, z2, -rho);
	else
		gfresult = -123;

	g_free (call_put_flag);
	return value_new_float(gfresult);
}

static char const *help_opt_extendible_writer = {
	/* xgettext:no-c-format */
	N_("@FUNCTION=OPT_EXTENDIBLE_WRITER\n"

	   "@SYNTAX=OPT_EXTENDIBLE_WRITER(call_put_flag,spot,strike1,strike2,"
	   "time1,time2,rate,cost_of_carry,volatility)\n"
	   "@DESCRIPTION="
	   "OPT_EXTENDIBLE_WRITER models the theoretical price of extendible "
	   "writer options."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=OPT_BS, OPT_BS_DELTA, "
	   "OPT_BS_RHO, OPT_BS_THETA, OPT_BS_GAMMA")
};

GnmFuncDescriptor const derivatives_functions [] = {
	{ "opt_bs",
	  "sfffff|f","call_put_flag, spot, strike, time, rate, volatility, "
	  "cost_of_carry",
	  &help_opt_bs, opt_bs, NULL, NULL, NULL },

	{ "opt_bs_delta",
	  "sffffff|f", "call_put_flag, spot, strike, time, rate, volatility, "
	  "cost_of_carry",
	  &help_opt_bs_delta, opt_bs_delta, NULL, NULL, NULL },

	{ "opt_bs_rho",
	  "sffffff|f", "call_put_flag, spot, strike, time, rate, volatility, "
	  "cost_of_carry",
	  &help_opt_bs_rho, opt_bs_rho, NULL, NULL, NULL },

	{ "opt_bs_theta",
	  "sffffff|f", "call_put_flag, spot, strike, time, rate, volatility, "
	  "cost_of_carry",
	  &help_opt_bs_theta, opt_bs_theta, NULL, NULL, NULL },

	{ "opt_bs_gamma",
	  "fffff|f", "spot, strike, time, rate, volatility, cost_of_carry",
	  &help_opt_bs_gamma, opt_bs_gamma, NULL, NULL, NULL },

	{ "opt_bs_vega",
	  "fffff|f", "spot, strike, time, rate, volatility, cost_of_carry",
	  &help_opt_bs_vega, opt_bs_vega, NULL, NULL, NULL },

	{ "opt_bs_carrycost",
	  "sffffff|f", "call_put_flag, spot, strike, time, rate, volatility, "
	  "cost_of_carry",
	  &help_opt_bs_carrycost, opt_bs_carrycost, NULL, NULL, NULL },

	{ "cum_biv_norm_dist",
	  "fff", "a, b, rho",
	  &help_cum_biv_norm_dist, cum_biv_norm_dist, NULL, NULL, NULL },

	{ "opt_garman_kohlhagen",
	  "sffffff", "call_put_flag, spot, strike, time, domestic_rate, "
	  "foreign_rate, volatility",
	  &help_opt_garman_kohlhagen, opt_garman_kohlhagen, NULL, NULL, NULL },

	{ "opt_french",
	  "sfffffff", "call_put_flag, spot, strike, time, t2, rate, "
	  "volatility, cost of carry",
	  &help_opt_french, opt_french, NULL, NULL, NULL },

	{ "opt_jump_diff",
	  "sfffffff", "call_put_flag, spot, strike, time, rate, volatility, "
	  "lambda, gamma",
	  &help_opt_jump_diff, opt_jump_diff, NULL, NULL, NULL },

	{ "opt_exec",
	  "sfffffff", "call_put_flag, spot, strike, time, rate, volatility, "
	  "cost_of_carry, lambda",
	  &help_opt_exec, opt_exec, NULL, NULL, NULL },

	{ "opt_bjerStens",
	  "sffffff", "call_put_flag, spot, strike, time, rate, cost_of_carry, "
	  "volatility",
	  &help_opt_bjerStens, opt_bjerStens, NULL, NULL, NULL },

	{ "opt_miltersen_schwartz",
	  "sfffffffffffff", "call_put_flag, p_t, f_t, x, t1, t2, v_s, v_e, "
	  "v_f, rho_se, rho_sf, rho_ef, kappa_e, kappa_f)",
	  &help_opt_miltersen_schwartz, opt_miltersen_schwartz, NULL, NULL, NULL },

	{ "opt_BAW_amer",
	  "sffffff", "call_put_flag, spot, strike, time, rate, cost_of_carry, "
	  "volatility",
	  &help_opt_BAW_amer, opt_BAW_amer, NULL, NULL, NULL },

	{ "opt_rgw",
	  "fffffff", "call_put_flag, spot, strike, t1, t2, rate, d, volatility",
	  &help_opt_rgw, opt_rgw, NULL, NULL, NULL },

	{ "opt_forward_start",
	  "sfffffff", "call_put_flag, spot, alpha, time1, time, rate, "
	  "volatility, cost_of_carry",
	  &help_opt_forward_start, opt_forward_start, NULL, NULL, NULL },

	{ "opt_time_switch",
	  "sfffffffff", "call_put_flag, spot, strike, a, time, m, dt, rate, "
	  "cost_of_carry, volatility",
	  &help_opt_time_switch, opt_time_switch, NULL, NULL, NULL },

	{ "opt_simple_chooser",
	  "fffffff", "spot, strike, time1, time2, rate, cost_of_carry, "
	  "volatility",
	  &help_opt_simple_chooser, opt_simple_chooser, NULL, NULL, NULL },

	{ "opt_complex_chooser",
	  "fffffffff", "spot, strike_call, strike_put, time, time_call, "
	  "time_put, rate, cost_of_carry, volatility",
	  &help_opt_complex_chooser, opt_complex_chooser, NULL, NULL, NULL },

	{ "opt_on_options",
	  "sffffffff", "type_flag, spot, strike1, strike2, time1, time2, "
	  "rate, cost_of_carry, volatility",
	  &help_opt_on_options, opt_on_options, NULL, NULL, NULL },

	{ "opt_extendible_writer",
	  "sffffffff", "type_flag, spot, strike1, strike2, time1, time2, "
	  "rate, cost_of_carry, volatility",
	  &help_opt_extendible_writer, opt_extendible_writer, NULL, NULL, NULL },

	{ NULL}
};
