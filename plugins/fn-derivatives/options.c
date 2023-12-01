/*
 * Options pricing
 *
 * Authors:
 *   Elliot Lee <sopwith@redhat.com> All initial work.
 *   Morten Welinder <terra@gnome.org> Port to new plugin framework.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnm-i18n.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>

#include <func.h>
#include <mathfunc.h>
#include <sf-dpq.h>
#include <sf-gamma.h>
#include <value.h>

#include <math.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

/* Some common decriptors */
#define DEF_ARG_CALL_PUT_FLAG { GNM_FUNC_HELP_ARG, F_("call_put_flag:'c' for a call and 'p' for a put") }
#define DEF_ARG_SPOT { GNM_FUNC_HELP_ARG, F_("spot:spot price") }
#define DEF_ARG_STRIKE { GNM_FUNC_HELP_ARG, F_("strike:strike price") }
#define DEF_ARG_TIME_MATURITY_Y { GNM_FUNC_HELP_ARG, F_("time:time to maturity in years") }
#define DEF_ARG_TIME_MATURITY_D { GNM_FUNC_HELP_ARG, F_("time:time to maturity in days") }
#define DEF_ARG_TIME_DIVIDEND { GNM_FUNC_HELP_ARG, F_("time_payout:time to dividend payout") }
#define DEF_ARG_TIME_EXPIRATION { GNM_FUNC_HELP_ARG, F_("time_exp:time to expiration") }
#define DEF_ARG_RATE_RISKFREE { GNM_FUNC_HELP_ARG, F_("rate:risk-free interest rate to the exercise date in percent") }
#define DEF_ARG_RATE_ANNUALIZED { GNM_FUNC_HELP_ARG, F_("rate:annualized interest rate") }
#define DEF_ARG_RATE_RISKFREE_ANN { GNM_FUNC_HELP_ARG, F_("rate:annualized risk-free interest rate") }
#define DEF_ARG_VOLATILITY { GNM_FUNC_HELP_ARG, F_("volatility:annualized volatility of the asset in percent for the period through to the exercise date") }
#define DEF_ARG_VOLATILITY_SHORT { GNM_FUNC_HELP_ARG, F_("volatility:annualized volatility of the asset") }
#define DEF_ARG_AMOUNT { GNM_FUNC_HELP_ARG, F_("d:amount of the dividend to be paid expressed in currency") }
#define DEF_ARG_CC_OPT { GNM_FUNC_HELP_ARG, F_("cost_of_carry:net cost of holding the underlying asset (for common stocks, the risk free rate less the dividend yield), defaults to 0") }
#define DEF_ARG_CC { GNM_FUNC_HELP_ARG, F_("cost_of_carry:net cost of holding the underlying asset") }

#define DEF_NOTE_UNITS { GNM_FUNC_HELP_NOTE, F_("The returned value will be expressed in the same units as @{strike} and @{spot}.")}


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
option_side (char const *s)
{
	if (s[0] == 'p' || s[0] == 'P')
		return OS_Put;
	else if (s[0] == 'c' || s[0] == 'C')
		return OS_Call;
	else
		return OS_Error;
}

static OptionType
option_type (char const *s)
{
	if (s[0] == 'a' || s[0] == 'A')
		return OT_Amer;
	else if (s[0] == 'e' || s[0] == 'E')
		return OT_Euro;
	else
		return OT_Error;
}

/* The normal distribution function */
static gnm_float
ncdf (gnm_float x)
{
	return pnorm (x, 0, 1, TRUE, FALSE);
}

static gnm_float
npdf (gnm_float x)
{
	return dnorm (x, 0, 1, FALSE);
}

static int
Sgn (gnm_float a)
{
	if ( a >0)
		return 1;
	else if (a < 0)
		return -1;
	else
		return 0;
}

/* The cumulative bivariate normal distribution function */
static gnm_float
cum_biv_norm_dist1 (gnm_float a, gnm_float b, gnm_float rho)
{
	gnm_float rho1, rho2, delta;
	gnm_float a1, b1, sum = 0.0;
	int i, j;

	static const gnm_float x[] = {0.24840615, 0.39233107, 0.21141819, 0.03324666, 0.00082485334};
	static const gnm_float y[] = {0.10024215, 0.48281397, 1.0609498, 1.7797294, 2.6697604};
	a1 = a / gnm_sqrt (2 * (1 - (rho * rho)));
	b1 = b / gnm_sqrt (2 * (1 - (rho * rho)));

	if (a <= 0 && b <= 0 && rho <= 0) {
		for (i = 0; i != 5; ++i) {
			for (j = 0; j != 5; ++j) {
				sum = sum + x[i] * x[j] * gnm_exp (a1 * (2 * y[i] - a1) + b1 * (2 *
y[j] - b1) + 2 * rho * (y[i] - a1) * (y[j] - b1));
			}
		}
		return gnm_sqrt (1 - (rho * rho)) / M_PIgnum * sum;
	} else if (a <= 0 && b >= 0 && rho >= 0)
		return ncdf (a) - cum_biv_norm_dist1 (a,-b,-rho);
	else if (a >= 0 && b <= 0 && rho >= 0)
		return ncdf (b) - cum_biv_norm_dist1 (-a,b,-rho);
	else if (a >= 0 && b >= 0 && rho <= 0)
		return ncdf (a) + ncdf (b) - 1 + cum_biv_norm_dist1 (-a,-b,rho);
	else if ((a * b * rho) > 0) {
		rho1 = (rho * a - b) * Sgn (a) / gnm_sqrt ((a * a) - 2 * rho * a
							   * b + (b * b));
		rho2 = (rho * b - a) * Sgn (b) / gnm_sqrt ((a * a) - 2 * rho * a
							   * b + (b * b));
		delta = (1 - Sgn (a) * Sgn (b)) / 4;
		return (cum_biv_norm_dist1 (a,0.0,rho1) +
			cum_biv_norm_dist1 (b,0.0,rho2) -
			delta);
	}
	return gnm_nan;

}

static GnmValue *
cum_biv_norm_dist(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);
	gnm_float rho = value_get_as_float (argv[2]);
	gnm_float result = cum_biv_norm_dist1 (a,b,rho);

	if (gnm_isnan (result))
		return value_new_error_NUM (ei->pos);
	else
		return value_new_float (result);
}

static GnmFuncHelp const help_cum_biv_norm_dist[] = {
	{ GNM_FUNC_HELP_NAME, F_("CUM_BIV_NORM_DIST:cumulative bivariate normal distribution")},
        { GNM_FUNC_HELP_ARG, F_("a:limit for first random variable")},
        { GNM_FUNC_HELP_ARG, F_("b:limit for second random variable")},
        { GNM_FUNC_HELP_ARG, F_("rho:correlation of the two random variables")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CUM_BIV_NORM_DIST calculates the probability that two standard "
					"normal distributed random variables with correlation @{rho} are "
					"respectively each less than @{a} and @{b}.")},
        { GNM_FUNC_HELP_EXAMPLES, "=CUM_BIV_NORM_DIST(0,0,0.5)" },
        { GNM_FUNC_HELP_END}
};



/* the generalized Black and Scholes formula*/
static gnm_float
opt_bs1 (OptionSide side,
	 gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v,
	 gnm_float b)
{
	gnm_float d1 = (gnm_log (s / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	gnm_float d2 = d1 - v * gnm_sqrt (t);

	switch (side) {
	case OS_Call:
		return (s * gnm_exp ((b - r) * t) * ncdf (d1) -
			x * gnm_exp (-r * t) * ncdf (d2));
	case OS_Put:
		return (x * gnm_exp (-r * t) * ncdf (-d2) -
			s * gnm_exp ((b - r) * t) * ncdf (-d1));
	default:
		return gnm_nan;
	}
}


static GnmValue *
opt_bs (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0;
	gnm_float gfresult = opt_bs1 (call_put, s, x, t, r, v, b);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_bs[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BS:price of a European option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_BS uses the Black-Scholes model to calculate "
					"the price of a European option struck at @{strike} "
					"on an asset with spot price @{spot}.")},
	DEF_NOTE_UNITS,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_VEGA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};

/* Delta for the generalized Black and Scholes formula */
static gnm_float
opt_bs_delta1 (OptionSide side,
	       gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float d1 =
		(gnm_log (s / x) + (b + (v * v) / 2) * t) /
		(v * gnm_sqrt (t));

	switch (side) {
	case OS_Call:
		return gnm_exp ((b - r) * t) * ncdf (d1);

	case OS_Put:
		return gnm_exp ((b - r) * t) * (ncdf (d1) - 1);

	default:
		return gnm_nan;
	}
}


static GnmValue *
opt_bs_delta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0;
	gnm_float gfresult = opt_bs_delta1 (call_put, s, x, t, r, v, b);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);

	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_bs_delta[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BS_DELTA:delta of a European option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_BS_DELTA uses the Black-Scholes model to calculate "
					"the 'delta' of a European option struck at @{strike} "
					"on an asset with spot price @{spot}.")},
	DEF_NOTE_UNITS,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_VEGA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Gamma for the generalized Black and Scholes formula */
static gnm_float
opt_bs_gamma1 (gnm_float s,gnm_float x,gnm_float t,gnm_float r,gnm_float v,gnm_float b)
{
	gnm_float d1;

	d1 = (gnm_log (s / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	return gnm_exp ((b - r) * t) * npdf (d1) / (s * v * gnm_sqrt (t));
}


static GnmValue *
opt_bs_gamma (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float s = value_get_as_float (argv[0]);
	gnm_float x = value_get_as_float (argv[1]);
	gnm_float t = value_get_as_float (argv[2]);
	gnm_float r = value_get_as_float (argv[3]);
	gnm_float v = value_get_as_float (argv[4]);
	gnm_float b = argv[5] ? value_get_as_float (argv[5]) : 0;
	gnm_float gfresult = opt_bs_gamma1 (s,x,t,r,v,b);
	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_bs_gamma[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BS_GAMMA:gamma of a European option")},
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_BS_GAMMA uses the Black-Scholes model to calculate "
					"the 'gamma' of a European option struck at @{strike} "
					"on an asset with spot price @{spot}. The gamma of an "
					"option is the second derivative of its price "
					"with respect to the price of the underlying asset.")},
	{ GNM_FUNC_HELP_NOTE, F_("Gamma is expressed as the rate of change "
				 "of delta per unit change in @{spot}.")},
	{ GNM_FUNC_HELP_NOTE, F_("Gamma is the same for calls and puts.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_VEGA"},
        { GNM_FUNC_HELP_END}
};

/* theta for the generalized Black and Scholes formula */
static gnm_float
opt_bs_theta1 (OptionSide side,
	       gnm_float s,gnm_float x,gnm_float t,gnm_float r,gnm_float v,gnm_float b)
{
	gnm_float d1 = (gnm_log (s / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	gnm_float d2 = d1 - v * gnm_sqrt (t);

	switch (side) {
	case OS_Call:
		return -s * gnm_exp ((b - r) * t) * npdf (d1) * v / (2 * gnm_sqrt (t)) -
			(b - r) * s * gnm_exp ((b - r) * t) * ncdf (d1) - r * x * gnm_exp (-r * t) * ncdf (d2);
	case OS_Put:
		return -s * gnm_exp ((b - r) * t) * npdf (d1) * v / (2 * gnm_sqrt (t)) +
			(b - r) * s * gnm_exp ((b - r) * t) * ncdf (-d1) + r * x * gnm_exp (-r * t) * ncdf (-d2);
	default:
		return gnm_nan;
	}
}

static GnmValue *
opt_bs_theta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0;
	gnm_float gfresult = opt_bs_theta1 (call_put, s, x, t, r, v, b);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_bs_theta[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BS_THETA:theta of a European option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_BS_THETA uses the Black-Scholes model to calculate "
					"the 'theta' of a European option struck at @{strike} "
					"on an asset with spot price @{spot}. The theta of an "
					"option is the rate of change of its price with "
					"respect to time to expiry.")},
	{ GNM_FUNC_HELP_NOTE, F_("Theta is expressed as the negative of the rate of change "
				 "of the option value, per 365.25 days.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_VEGA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Vega for the generalized Black and Scholes formula */
static gnm_float
opt_bs_vega1 (gnm_float s, gnm_float x, gnm_float t,
	      gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float d1 = (gnm_log (s / x) + (b + (v * v) / 2) * t) /
		(v * gnm_sqrt (t));
	return s * gnm_exp ((b - r) * t) * npdf (d1) * gnm_sqrt (t);
}

static GnmValue *
opt_bs_vega (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float s = value_get_as_float (argv[0]);
	gnm_float x = value_get_as_float (argv[1]);
	gnm_float t = value_get_as_float (argv[2]);
	gnm_float r = value_get_as_float (argv[3]);
	gnm_float v = value_get_as_float (argv[4]);
	gnm_float b = argv[5] ? value_get_as_float (argv[5]) : 0;

	return value_new_float (opt_bs_vega1 (s, x, t, r, v, b));
}

static GnmFuncHelp const help_opt_bs_vega[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BS_VEGA:vega of a European option")},
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_BS_VEGA uses the Black-Scholes model to calculate "
					"the 'vega' of a European option struck at @{strike} "
					"on an asset with spot price @{spot}. The vega of an "
					"option is the rate of change of its price with "
					"respect to volatility.")},
	{ GNM_FUNC_HELP_NOTE, F_("Vega is the same for calls and puts.")},
	/* xgettext:no-c-format */
	{ GNM_FUNC_HELP_NOTE, F_("Vega is expressed as the rate of change "
				 "of option value, per 100% volatility.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Rho for the generalized Black and Scholes formula */
static gnm_float
opt_bs_rho1 (OptionSide side, gnm_float s, gnm_float x,
	     gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float d1 = (gnm_log (s / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	gnm_float d2 = d1 - v * gnm_sqrt (t);
	switch (side) {
	case OS_Call:
		if (b != 0)
			return t * x * gnm_exp (-r * t) * ncdf (d2);
		else
			return -t *  opt_bs1 (side, s, x, t, r, v, b);

	case OS_Put:
		if (b != 0)
			return -t * x * gnm_exp (-r * t) * ncdf (-d2);
		else
			return -t * opt_bs1 (side, s, x, t, r, v, b);

	default:
		return gnm_nan;
	}
}


static GnmValue *
opt_bs_rho (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0;
	gnm_float gfresult = opt_bs_rho1 (call_put, s, x, t, r, v, b);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_bs_rho[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BS_RHO:rho of a European option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_BS_RHO uses the Black-Scholes model to calculate "
					"the 'rho' of a European option struck at @{strike} "
					"on an asset with spot price @{spot}. The rho of an "
					"option is the rate of change of its price with "
					"respect to the risk free interest rate.")},
	/* xgettext:no-c-format */
	{ GNM_FUNC_HELP_NOTE, F_("Rho is expressed as the rate of change "
				 "of the option value, per 100% change in @{rate}.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_THETA,OPT_BS_VEGA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};

/* Carry for the generalized Black and Scholes formula */
static gnm_float
opt_bs_carrycost1 (OptionSide side, gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float d1 = (gnm_log (s / x) + (b + (v * v) / 2) * t) /
		(v * gnm_sqrt (t));

	switch (side) {
	case OS_Call:
		return t * s * gnm_exp ((b - r) * t) * ncdf (d1);
	case OS_Put:
		return -t * s * gnm_exp ((b - r) * t) * ncdf (-d1);
	default:
		return gnm_nan; /*should never get to here*/
	}
}

static GnmValue *
opt_bs_carrycost (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0;
	gnm_float gfresult = opt_bs_carrycost1 (call_put, s, x, t, r, v, b);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}


static GnmFuncHelp const help_opt_bs_carrycost[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BS_CARRYCOST:elasticity of a European option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_BS_CARRYCOST uses the Black-Scholes model to calculate "
					"the 'elasticity' of a European option struck at @{strike} "
					"on an asset with spot price @{spot}. The elasticity of an option "
					"is the rate of change of its price "
					"with respect to its @{cost_of_carry}.")},
	/* xgettext:no-c-format */
	{ GNM_FUNC_HELP_NOTE, F_("Elasticity is expressed as the rate of change "
				 "of the option value, per 100% volatility.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Currency Options - Garman and Kohlhagen */
static gnm_float
opt_garman_kohlhagen1 (OptionSide side,
		       gnm_float s, gnm_float x, gnm_float t,
		       gnm_float r, gnm_float rf, gnm_float v)
{
	gnm_float d1 = (gnm_log (s / x) + (r - rf + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	gnm_float d2 = d1 - v * gnm_sqrt (t);
	switch (side) {
	case OS_Call:
		return s * gnm_exp (-rf * t) * ncdf (d1) - x * gnm_exp (-r * t) * ncdf (d2);
	case OS_Put:
		return x * gnm_exp (-r * t) * ncdf (-d2) - s * gnm_exp (-rf * t) * ncdf (-d1);
	default:
		return gnm_nan; /*should never get to here*/
	}
}

static GnmValue *
opt_garman_kohlhagen (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float rf = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);
	gnm_float gfresult = opt_garman_kohlhagen1 (call_put, s, x, t, r, rf, v);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);
	else
		return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_garman_kohlhagen[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_GARMAN_KOHLHAGEN:theoretical price of a European currency option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
        { GNM_FUNC_HELP_ARG, F_("time:number of days to exercise")},
        { GNM_FUNC_HELP_ARG, F_("domestic_rate:domestic risk-free interest rate to the exercise date in percent")},
        { GNM_FUNC_HELP_ARG, F_("foreign_rate:foreign risk-free interest rate to the exercise date in percent")},
	DEF_ARG_VOLATILITY,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_GARMAN_KOHLHAGEN values the theoretical price of a European "
					"currency option struck at @{strike} on an asset with spot price @{spot}.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* French (1984) adjusted Black and scholes model for trading day volatility */
static gnm_float
opt_french1 (OptionSide side, gnm_float s, gnm_float x, gnm_float tradingt, gnm_float calendart,
	     gnm_float r, gnm_float v, gnm_float  b)
{
	gnm_float d1 = (gnm_log (s / x) + b * calendart + ((v * v) / 2) * tradingt) / (v * gnm_sqrt (tradingt));
	gnm_float d2 = d1 - v * gnm_sqrt (tradingt);

	switch (side) {
	case OS_Call:
		return s * gnm_exp ((b - r) * calendart) * ncdf (d1) - x * gnm_exp (-r * calendart) * ncdf (d2);
	case OS_Put:
		return x * gnm_exp (-r * calendart) * ncdf (-d2) - s * gnm_exp ((b - r) * calendart) * ncdf (-d1);
	default:
		return gnm_nan;
	}
}


static GnmValue *
opt_french (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float t1 = value_get_as_float (argv[4]);
	gnm_float r = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);
	gnm_float b = value_get_as_float (argv[7]);
	gnm_float gfresult = opt_french1 (call_put, s, x, t, t1, r, v, b);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);
	else
		return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_french[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_FRENCH:theoretical price of a European option adjusted for trading day volatility")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
        { GNM_FUNC_HELP_ARG, F_("time:ratio of the number of calendar days to exercise and the number of calendar days in the year")},
        { GNM_FUNC_HELP_ARG, F_("ttime:ratio of the number of trading days to exercise and the number of trading days in the year")},
	DEF_ARG_RATE_RISKFREE,
	DEF_ARG_VOLATILITY,
	DEF_ARG_CC_OPT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_FRENCH values the theoretical price of a "
					"European option adjusted for trading day volatility, struck at "
					"@{strike} on an asset with spot price @{spot}.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};

/* Merton jump diffusion model*/
static gnm_float
opt_jump_diff1 (OptionSide side, gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v,
		gnm_float lambda, gnm_float gamma)
{
	gnm_float delta, sum;
	gnm_float Z, vi;
	int i;

	delta = gnm_sqrt (gamma * (v * v) / lambda);
	Z = gnm_sqrt ((v * v) - lambda * (delta * delta));
	sum = 0.0;
	for (i = 0; i != 11; ++i) {
		vi = gnm_sqrt ((Z * Z) + (delta * delta) * (i / t));
		sum = sum + gnm_exp (-lambda * t) * gnm_pow (lambda * t, i) / gnm_fact(i) *
			opt_bs1 (side, s, x, t, r, vi, r);
	}
	return sum;
}

static GnmValue *
opt_jump_diff (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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

static GnmFuncHelp const help_opt_jump_diff[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_JUMP_DIFF:theoretical price of an option according to the Jump Diffusion process")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
        { GNM_FUNC_HELP_ARG, F_("rate:the annualized rate of interest")},
	DEF_ARG_VOLATILITY,
        { GNM_FUNC_HELP_ARG, F_("lambda:expected number of 'jumps' per year")},
        { GNM_FUNC_HELP_ARG, F_("gamma:proportion of volatility explained by the 'jumps'")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_JUMP_DIFF models the theoretical price of an option according "
					"to the Jump Diffusion process (Merton).")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};



/* Miltersen schwartz (1997) commodity option model */
static gnm_float
opt_miltersen_schwartz1 (OptionSide side, gnm_float p_t, gnm_float f_t, gnm_float x, gnm_float t1,
			 gnm_float t2, gnm_float v_s, gnm_float v_e, gnm_float v_f, gnm_float rho_se,
			 gnm_float rho_sf, gnm_float rho_ef, gnm_float kappa_e, gnm_float kappa_f)
{
	gnm_float vz, vxz;
	gnm_float d1, d2;

	vz = (v_s * v_s) * t1 + 2 * v_s * (v_f * rho_sf * 1 / kappa_f * (t1 - 1 / kappa_f * gnm_exp (-kappa_f * t2) * (gnm_exp (kappa_f * t1) - 1))
					    - v_e * rho_se * 1 / kappa_e * (t1 - 1 / kappa_e * gnm_exp (-kappa_e * t2) * (gnm_exp (kappa_e * t1) - 1)))
		+ (v_e * v_e) * 1 / (kappa_e * kappa_e) * (t1 + 1 / (2 * kappa_e) * gnm_exp (-2 * kappa_e * t2) * (gnm_exp (2 * kappa_e * t1) - 1)
							 - 2 * 1 / kappa_e * gnm_exp (-kappa_e * t2) * (gnm_exp (kappa_e * t1) - 1))
		+ (v_f * v_f) * 1 / (kappa_f * kappa_f) * (t1 + 1 / (2 * kappa_f) * gnm_exp (-2 * kappa_f * t2) * (gnm_exp (2 * kappa_f * t1) - 1)
							 - 2 * 1 / kappa_f * gnm_exp (-kappa_f * t2) * (gnm_exp (kappa_f * t1) - 1))
		- 2 * v_e * v_f * rho_ef * 1 / kappa_e * 1 / kappa_f * (t1 - 1 / kappa_e * gnm_exp (-kappa_e * t2) * (gnm_exp (kappa_e * t1) - 1)
									- 1 / kappa_f * gnm_exp (-kappa_f * t2) * (gnm_exp (kappa_f * t1) - 1)
									+ 1 / (kappa_e + kappa_f) * gnm_exp (-(kappa_e + kappa_f) * t2) * (gnm_exp ((kappa_e + kappa_f) * t1) - 1));

	vxz = v_f * 1 / kappa_f * (v_s * rho_sf * (t1 - 1 / kappa_f * (1 - gnm_exp (-kappa_f * t1)))
				   + v_f * 1 / kappa_f * (t1 - 1 / kappa_f * gnm_exp (-kappa_f * t2) * (gnm_exp (kappa_f * t1) - 1) - 1 / kappa_f * (1 - gnm_exp (-kappa_f * t1))
							  + 1 / (2 * kappa_f) * gnm_exp (-kappa_f * t2) * (gnm_exp (kappa_f * t1) - gnm_exp (-kappa_f * t1)))
				   - v_e * rho_ef * 1 / kappa_e * (t1 - 1 / kappa_e * gnm_exp (-kappa_e * t2) * (gnm_exp (kappa_e * t1) - 1) - 1 / kappa_f * (1 - gnm_exp (-kappa_f * t1))
								   + 1 / (kappa_e + kappa_f) * gnm_exp (-kappa_e * t2) * (gnm_exp (kappa_e * t1) - gnm_exp (-kappa_f * t1))));

	vz = gnm_sqrt (vz);

	d1 = (gnm_log (f_t / x) - vxz + (vz * vz) / 2) / vz;
	d2 = (gnm_log (f_t / x) - vxz - (vz * vz) / 2) / vz;

	switch (side) {
	case OS_Call:
		return p_t * (f_t * gnm_exp (-vxz) * ncdf (d1) - x * ncdf (d2));
	case OS_Put:
		return p_t * (x * ncdf (-d2) - f_t * gnm_exp (-vxz) * ncdf (-d1));
	default:
		return gnm_nan;
	}
}

static GnmValue *
opt_miltersen_schwartz (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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

	gnm_float gfresult =
		opt_miltersen_schwartz1 (call_put, p_t, f_t, x, t1, t2,
					 v_s, v_e, v_f,
					 rho_se, rho_sf, rho_ef, kappa_e, kappa_f);

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);
	return value_new_float (gfresult);
}


static GnmFuncHelp const help_opt_miltersen_schwartz[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_MILTERSEN_SCHWARTZ:theoretical price of options on commodities futures according to Miltersen & Schwartz")},
	DEF_ARG_CALL_PUT_FLAG,
        { GNM_FUNC_HELP_ARG, F_("p_t:zero coupon bond with expiry at option maturity")},
        { GNM_FUNC_HELP_ARG, F_("f_t:futures price")},
	DEF_ARG_STRIKE,
        { GNM_FUNC_HELP_ARG, F_("t1:time to maturity of the option")},
        { GNM_FUNC_HELP_ARG, F_("t2:time to maturity of the underlying commodity futures contract")},
        { GNM_FUNC_HELP_ARG, F_("v_s:volatility of the spot commodity price")},
        { GNM_FUNC_HELP_ARG, F_("v_e:volatility of the future convenience yield")},
        { GNM_FUNC_HELP_ARG, F_("v_f:volatility of the forward rate of interest")},
        { GNM_FUNC_HELP_ARG, F_("rho_se:correlation between the spot commodity price and the convenience yield")},
        { GNM_FUNC_HELP_ARG, F_("rho_sf:correlation between the spot commodity price and the forward interest rate")},
        { GNM_FUNC_HELP_ARG, F_("rho_ef:correlation between the forward interest rate and the convenience yield")},
        { GNM_FUNC_HELP_ARG, F_("kappa_e:speed of mean reversion of the convenience yield")},
        { GNM_FUNC_HELP_ARG, F_("kappa_f:speed of mean reversion of the forward interest rate")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
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

	if (!(s > 0))
		return gnm_nan;

	infinity = 100000000;
	epsilon = 0.00001;
	sx = s - d * gnm_exp (-r * t1);
	if (d <= (x * (1 - gnm_exp (-r * (t2 - t1)))))
		/* Not optimal to exercise */
		return opt_bs1 (OS_Call, sx, x, t2, r, v, 0);

	ci = opt_bs1 (OS_Call, s, x, t2 - t1, r, v, 0);
	HighS = s;
	while ((ci - HighS - d + x) > 0 && HighS < infinity) {

		HighS *= 2;
		ci = opt_bs1 (OS_Call, HighS, x, t2 - t1, r, v, 0);
	}
	if (HighS > infinity)
		return opt_bs1 (OS_Call, sx, x, t2, r, v, 0);

	LowS = 0.0;
	i = HighS * GNM_const(0.5);
	ci = opt_bs1 (OS_Call, i, x, t2 - t1, r, v, 0);

	/* search algorithm to find the critical stock price i */
	while (gnm_abs (ci - i - d + x) > epsilon && HighS - LowS > epsilon) {
		if ((ci - i - d + x) < 0)
			HighS = i;
		else
			LowS = i;
		i = (HighS + LowS) / 2;
		ci = opt_bs1 (OS_Call, i, x, (t2 - t1), r, v, 0);
	}

	a1 = (gnm_log (sx / x) + (r + (v * v) / 2) * t2) / (v * gnm_sqrt (t2));
	a2 = a1 - v * gnm_sqrt (t2);
	b1 = (gnm_log (sx / i) + (r + (v * v) / 2) * t1) / (v * gnm_sqrt (t1));
	b2 = b1 - v * gnm_sqrt (t1);

	gfresult = sx * ncdf (b1) + sx * cum_biv_norm_dist1 (a1, -b1, -gnm_sqrt (t1 / t2))
		- x * gnm_exp (-r * t2) * cum_biv_norm_dist1 (a2, -b2, -gnm_sqrt (t1 / t2)) - (x - d)
		* gnm_exp (-r * t1) * ncdf (b2);
	return gfresult;
}


static GnmValue *
opt_rgw(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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

static GnmFuncHelp const help_opt_rgw[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_RGW:theoretical price of an American option according to the Roll-Geske-Whaley approximation")},
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_DIVIDEND,
	DEF_ARG_TIME_EXPIRATION,
	DEF_ARG_RATE_ANNUALIZED,
        DEF_ARG_AMOUNT,
	DEF_ARG_VOLATILITY,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};

/* the Barone-Adesi and Whaley (1987) American approximation */
static GnmValue *
opt_baw_amer (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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

	if (gnm_isnan (gfresult))
		return value_new_error_NUM (ei->pos);

	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_baw_amer[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BAW_AMER:theoretical price of an option according to the Barone Adesie & Whaley approximation")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_D,
	DEF_ARG_RATE_RISKFREE_ANN,
	DEF_ARG_CC,
        DEF_ARG_VOLATILITY_SHORT,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
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
		n = 2 * b / (v * v);
		k = 2 * r / ((v * v) * (1 - gnm_exp (-r * t)));
		d1 = (gnm_log (sk / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
		q2 = (-(n - 1) + gnm_sqrt ((n - 1) * (n - 1) + 4 * k)) / 2;
		a2 = (sk / q2) * (1 - gnm_exp ((b - r) * t) * ncdf (d1));
		if (s < sk)
			gfresult = opt_bs1 (OS_Call, s, x, t, r, v, b) + a2 * gnm_pow (s / sk, q2);
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
	n = 2 * b / (v * v);
	m = 2 * r / (v * v);
	q2u = (-(n - 1) + gnm_sqrt (((n - 1) * (n - 1)) + 4 * m)) / 2;
	su = x / (1 - 1 / q2u);
	h2 = -(b * t + 2 * v * gnm_sqrt (t)) * x / (su - x);
	si = x + (su - x) * (1 - gnm_exp (h2));

	k = 2 * r / ((v * v) * (1 - gnm_exp (-r * t)));
	d1 = (gnm_log (si / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	q2 = (-(n - 1) + gnm_sqrt (((n - 1) * (n - 1)) + 4 * k)) / 2;
	LHS = si - x;
	RHS = opt_bs1 (OS_Call, si, x, t, r, v, b) + (1 - gnm_exp ((b - r) * t) * ncdf (d1)) * si / q2;
	bi = gnm_exp ((b - r) * t) * ncdf (d1) * (1 - 1 / q2)
		+ (1 - gnm_exp ((b - r) * t) * ncdf (d1) / (v * gnm_sqrt (t))) / q2;
	e = 0.000001;

	/* Newton Raphson algorithm for finding critical price si */
	while ((gnm_abs (LHS - RHS) / x) > e)
	{
		si = (x + RHS - bi * si) / (1 - bi);
		d1 = (gnm_log (si / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
		LHS = si - x;
		RHS = opt_bs1 (OS_Call, si, x, t, r, v, b) + (1 - gnm_exp ((b - r) * t) * ncdf (d1)) * si / q2;
		bi = gnm_exp ((b - r) * t) * ncdf (d1) * (1 - 1 / q2)
			+ (1 - gnm_exp ((b - r) * t) * npdf (d1) / (v * gnm_sqrt (t))) / q2;
	}
	return si;
}

static gnm_float
opt_baw_put (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float sk = NRA_p (x, t, r, v, b);
	gnm_float n = 2 * b / (v * v);
	gnm_float k = 2 * r / ((v * v) * (1 - gnm_exp (-r * t)));
	gnm_float d1 = (gnm_log (sk / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	gnm_float q1 = (-(n - 1) - gnm_sqrt (((n - 1) * (n - 1)) + 4 * k)) / 2;
	gnm_float a1 = -(sk / q1) * (1 - gnm_exp ((b - r) * t) * ncdf (-d1));

	if (s > sk)
		return opt_bs1 (OS_Put, s, x, t, r, v, b) + a1 * gnm_pow (s/ sk, q1);
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
	n = 2 * b / (v * v);
	m = 2 * r / (v * v);
	q1u = (-(n - 1) - gnm_sqrt (((n - 1) * (n - 1)) + 4 * m)) / 2;
	su = x / (1 - 1 / q1u);
	h1 = (b * t - 2 * v * gnm_sqrt (t)) * x / (x - su);
	si = su + (x - su) * gnm_exp (h1);

	k = 2 * r / ((v * v) * (1 - gnm_exp (-r * t)));
	d1 = (gnm_log (si / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	q1 = (-(n - 1) - gnm_sqrt (((n - 1) * (n - 1)) + 4 * k)) / 2;
	LHS = x - si;
	RHS = opt_bs1 (OS_Put, si, x, t, r, v, b) - (1 - gnm_exp ((b - r) * t) * ncdf (-d1)) * si / q1;
	bi = -gnm_exp ((b - r) * t) * ncdf (-d1) * (1 - 1 / q1)
		- (1 + gnm_exp ((b - r) * t) * npdf (-d1) / (v * gnm_sqrt (t))) / q1;
	e = 0.000001;

	/* Newton Raphson algorithm for finding critical price si */
	while(gnm_abs (LHS - RHS) / x > e) {
		si = (x - RHS + bi * si) / (1 + bi);
		d1 = (gnm_log (si / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
		LHS = x - si;
		RHS = opt_bs1 (OS_Put, si, x, t, r, v, b) - (1 - gnm_exp ((b - r) * t) * ncdf (-d1)) * si / q1;
		bi = -gnm_exp ((b - r) * t) * ncdf (-d1) * (1 - 1 / q1)
			- (1 + gnm_exp ((b - r) * t) * ncdf (-d1) / (v * gnm_sqrt (t))) / q1;
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
		return gnm_nan;
	}
}

static GnmValue *
opt_bjer_stens (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string (argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float x = value_get_as_float (argv[2]);
	gnm_float t = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float v = value_get_as_float (argv[5]);
	gnm_float b = argv[6] ? value_get_as_float (argv[6]) : 0;
	gnm_float gfresult =
		opt_bjer_stens1 (call_put, s, x, t, r, v, b);
	return value_new_float (gfresult);
}


static GnmFuncHelp const help_opt_bjer_stens[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BJER_STENS:theoretical price of American options according to the Bjerksund & Stensland approximation technique")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_D,
	DEF_ARG_RATE_RISKFREE_ANN,
        DEF_ARG_VOLATILITY_SHORT,
	DEF_ARG_CC_OPT,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};

static gnm_float
opt_bjer_stens1_c (gnm_float s, gnm_float x, gnm_float t, gnm_float r, gnm_float v, gnm_float b)
{
	if (b >= r) /* Never optimal to exersice before maturity */
		return opt_bs1 (OS_Call, s, x, t, r, v, b);
	else {
		gnm_float Beta =
			(GNM_const(0.5) - b / (v * v)) +
			gnm_sqrt (gnm_pow (b / (v * v) - GNM_const(0.5), 2) + 2 * r / (v * v));
		gnm_float BInfinity = Beta / (Beta - 1) * x;
		gnm_float B0 = MAX (x, r / (r - b) * x);
		gnm_float ht = -(b * t + 2 * v * gnm_sqrt (t)) * B0 / (BInfinity - B0);
		gnm_float I = B0 + (BInfinity - B0) * (1 - gnm_exp (ht));
		if (s >= I)
			return s - x;
		else {
			gnm_float alpha = (I - x) * gnm_pow (I ,-Beta);
			return alpha * gnm_pow (s ,Beta) -
				alpha * phi (s, t, Beta, I, I, r, v, b) +
				phi (s, t, 1, I, I, r, v, b) -
				phi (s, t, 1, x, I, r, v, b) -
				x * phi (s, t, 0, I, I, r, v, b) +
				x * phi (s, t, 0, x, I, r, v, b);
		}
	}
}

static gnm_float
phi (gnm_float s, gnm_float t, gnm_float gamma, gnm_float H, gnm_float I, gnm_float r, gnm_float v, gnm_float b)
{
	gnm_float lambda, kappa;
	gnm_float d;
	gnm_float gfresult;

	lambda = (-r + gamma * b + GNM_const(0.5) * gamma * (gamma - 1) * (v * v)) * t;
	d = -(gnm_log (s / H) + (b + (gamma - GNM_const(0.5)) * (v * v)) * t) / (v * gnm_sqrt (t));
	kappa = 2 * b / (v * v) + (2 * gamma - 1);
	gfresult = gnm_exp (lambda) * gnm_pow (s, gamma) * (ncdf (d) - gnm_pow (I / s, kappa) * ncdf (d - 2 * gnm_log (I / s) / (v * gnm_sqrt (t))));

	return gfresult;
}


/* Executive stock options */
static GnmValue *
opt_exec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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
		gnm_exp (-lambda * t) * opt_bs1 (call_put, s, x, t, r, v, b);
	return value_new_float (gfresult);
}


static GnmFuncHelp const help_opt_exec[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_EXEC:theoretical price of executive stock options")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_D,
	DEF_ARG_RATE_RISKFREE_ANN,
        DEF_ARG_VOLATILITY_SHORT,
	DEF_ARG_CC,
        { GNM_FUNC_HELP_ARG, F_("lambda:jump rate for executives")},
	{ GNM_FUNC_HELP_NOTE, F_("The model assumes executives forfeit their options if they leave the company.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};





/* Forward start options */
static GnmValue *
opt_forward_start(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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
		s * gnm_exp ((b - r) * t1) * opt_bs1 (call_put, 1, alpha, t - t1, r, v, b);
	return value_new_float (gfresult);

}


static GnmFuncHelp const help_opt_forward_start[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_FORWARD_START:theoretical price of forward start options")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
        { GNM_FUNC_HELP_ARG, F_("alpha:fraction setting the strike price at the future date @{time_start}")},
        { GNM_FUNC_HELP_ARG, F_("time_start:time until the option starts in days")},
	DEF_ARG_TIME_MATURITY_D,
	DEF_ARG_RATE_RISKFREE_ANN,
        DEF_ARG_VOLATILITY_SHORT,
	DEF_ARG_CC,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* time switch options (discrete) */
static GnmValue *
opt_time_switch (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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
	default: return value_new_error_NUM (ei->pos);
	}

	sum = 0.0;
	n = t / dt;
	for (i = 1; i < n; ++i) {
		d = (gnm_log (s / x) + (b - (v * v) / 2) * i * dt) / (v * gnm_sqrt (i * dt));
		sum = sum + ncdf (Z * d) * dt;
	}

	gfresult = a * gnm_exp (-r * t) * sum + dt * a * gnm_exp (-r * t) * m;
	return value_new_float (gfresult);

}

static GnmFuncHelp const help_opt_time_switch[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_TIME_SWITCH:theoretical price of time switch options")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
        { GNM_FUNC_HELP_ARG, F_("a:amount received for each time period")},
	DEF_ARG_TIME_MATURITY_Y,
        { GNM_FUNC_HELP_ARG, F_("m:number of time units the option has already met the condition")},
	{ GNM_FUNC_HELP_ARG, F_("dt:agreed upon discrete time period expressed as "
				"a fraction of a year")},
	DEF_ARG_RATE_RISKFREE_ANN,
	DEF_ARG_CC,
	DEF_ARG_VOLATILITY_SHORT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_TIME_SWITCH models the theoretical price of time switch options. (Pechtl 1995). "
					"The holder receives @{a} * @{dt} for each period that the asset price was "
					"greater than @{strike} (for a call) or below it (for a put).")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* simple chooser options */
static GnmValue *
opt_simple_chooser (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float s = value_get_as_float (argv[0]);
	gnm_float x = value_get_as_float (argv[1]);
	gnm_float t1 = value_get_as_float (argv[2]);
	gnm_float t2 = value_get_as_float (argv[3]);
	gnm_float r = value_get_as_float (argv[4]);
	gnm_float b = value_get_as_float (argv[5]);
	gnm_float v = value_get_as_float (argv[6]);

	gnm_float d = (gnm_log (s / x) + (b + (v * v) / 2) * t2) / (v * gnm_sqrt (t2));
	gnm_float y = (gnm_log (s / x) + b * t2 + (v * v) * t1 / 2) / (v * gnm_sqrt (t1));
	gnm_float gfresult =
		s * gnm_exp ((b - r) * t2) * ncdf ( d) - x * gnm_exp (-r * t2) * ncdf ( d - v * gnm_sqrt (t2)) -
		s * gnm_exp ((b - r) * t2) * ncdf (-y) + x * gnm_exp (-r * t2) * ncdf (-y + v * gnm_sqrt (t1));

	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_simple_chooser[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_SIMPLE_CHOOSER:theoretical price of a simple chooser option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
        { GNM_FUNC_HELP_ARG, F_("time1:time in years until the holder chooses a put or a call option")},
        { GNM_FUNC_HELP_ARG, F_("time2:time in years until the chosen option expires")},
	DEF_ARG_CC,
	DEF_ARG_VOLATILITY_SHORT,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Complex chooser options */
static GnmValue *
opt_complex_chooser(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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
	d1 = (gnm_log (s / I) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	d2 = d1 - v * gnm_sqrt (t);
	y1 = (gnm_log (s / xc) + (b + (v * v) / 2) * tc) / (v * gnm_sqrt (tc));
	y2 = (gnm_log (s / xp) + (b + (v * v) / 2) * tp) / (v * gnm_sqrt (tp));
	rho1 = gnm_sqrt (t / tc);
	rho2 = gnm_sqrt (t / tp);

	gfresult = s * gnm_exp ((b - r) * tc) * cum_biv_norm_dist1 (d1, y1, rho1) - xc * gnm_exp (-r * tc)
		* cum_biv_norm_dist1 (d2, y1 - v * gnm_sqrt (tc), rho1) - s * gnm_exp ((b - r) * tp)
		* cum_biv_norm_dist1 (-d1, -y2, rho2) + xp * gnm_exp (-r * tp) * cum_biv_norm_dist1 (-d2, -y2 + v * gnm_sqrt (tp), rho2);

	return value_new_float (gfresult);

}

static GnmFuncHelp const help_opt_complex_chooser[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_COMPLEX_CHOOSER:theoretical price of a complex chooser option")},
	DEF_ARG_SPOT,
        { GNM_FUNC_HELP_ARG, F_("strike_call:strike price, if exercised as a call option")},
        { GNM_FUNC_HELP_ARG, F_("strike_put:strike price, if exercised as a put option")},
        { GNM_FUNC_HELP_ARG, F_("time:time in years until the holder chooses a put or a call option")},
        { GNM_FUNC_HELP_ARG, F_("time_call:time in years to maturity of the call option if chosen")},
        { GNM_FUNC_HELP_ARG, F_("time_put:time in years  to maturity of the put option if chosen")},
	DEF_ARG_RATE_RISKFREE_ANN,
	DEF_ARG_CC,
	DEF_ARG_VOLATILITY,
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
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
	while (gnm_abs (yi) > epsilon)
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
opt_on_options (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *type_flag = value_peek_string (argv[0]);
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

	if (!strcmp (type_flag, "cc") || !strcmp (type_flag, "pc"))
		call_put = OS_Call;
	else
		call_put = OS_Put;

	I = CriticalValueOptionsOnOptions (call_put, x1, x2, t2 - t1, r, b, v);

	rho = gnm_sqrt (t1 / t2);
	y1 = (gnm_log (s / I) + (b + (v * v) / 2) * t1) / (v * gnm_sqrt (t1));
	y2 = y1 - v * gnm_sqrt (t1);
	z1 = (gnm_log (s / x1) + (b + (v * v) / 2) * t2) / (v * gnm_sqrt (t2));
	z2 = z1 - v * gnm_sqrt (t2);

	if (!strcmp (type_flag, "cc"))
		gfresult = s * gnm_exp ((b - r) * t2) * cum_biv_norm_dist1 (z1, y1, rho) -
			x1 * gnm_exp (-r * t2) * cum_biv_norm_dist1 (z2, y2, rho) - x2 * gnm_exp (-r * t1) * ncdf (y2);
	else if (!strcmp (type_flag, "pc"))
		gfresult = x1 * gnm_exp (-r * t2) * cum_biv_norm_dist1 (z2, -y2, -rho) -
			s * gnm_exp ((b - r) * t2) * cum_biv_norm_dist1 (z1, -y1, -rho) + x2 * gnm_exp (-r * t1) * ncdf (-y2);
	else if (!strcmp (type_flag, "cp"))
		gfresult = x1 * gnm_exp (-r * t2) * cum_biv_norm_dist1 (-z2, -y2, rho) -
			s * gnm_exp ((b - r) * t2) * cum_biv_norm_dist1 (-z1, -y1, rho) - x2 * gnm_exp (-r * t1) * ncdf (-y2);
	else if (!strcmp (type_flag, "pp"))
		gfresult = s * gnm_exp ((b - r) * t2) * cum_biv_norm_dist1 (-z1, y1, -rho) -
			x1 * gnm_exp (-r * t2) * cum_biv_norm_dist1 (-z2, y2, -rho) + gnm_exp (-r * t1) * x2 * ncdf (y2);
	else
		return value_new_error_VALUE (ei->pos);

	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_on_options[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_ON_OPTIONS:theoretical price of options on options")},
        { GNM_FUNC_HELP_ARG, F_("type_flag:'cc' for calls on calls, 'cp' for calls on puts, and so on for 'pc', and 'pp'")},
	DEF_ARG_SPOT,
        { GNM_FUNC_HELP_ARG, F_("strike1:strike price at which the option being valued is struck")},
        { GNM_FUNC_HELP_ARG, F_("strike2:strike price at which the underlying option is struck")},
        { GNM_FUNC_HELP_ARG, F_("time1:time in years to maturity of the option")},
        { GNM_FUNC_HELP_ARG, F_("time2:time in years to the maturity of the underlying option")},
	DEF_ARG_RATE_RISKFREE_ANN,
        { GNM_FUNC_HELP_ARG, F_("cost_of_carry:net cost of holding the underlying asset of the underlying option")},
        { GNM_FUNC_HELP_ARG, F_("volatility:annualized volatility in price of the underlying asset of the underlying option")},
        { GNM_FUNC_HELP_NOTE, F_("For common stocks, @{cost_of_carry} is the risk free rate less the dividend yield.")},
        { GNM_FUNC_HELP_NOTE, F_("@{time2} \xe2\x89\xa5 @{time1}")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
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
	while (gnm_abs (ci - x2) > epsilon) {
		si = si - (ci - x2) / di;
		ci = opt_bs1 (side, si, x1, t, r, v, b);
		di = opt_bs_delta1 (side, si, x1, t, r, v, b);
	}
	return si;
}

/* Writer extendible options */
static GnmValue *
opt_extendible_writer (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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

	gnm_float rho = gnm_sqrt (t1 / t2);
	gnm_float z1 = (gnm_log (s / x2) + (b + (v * v) / 2) * t2) / (v * gnm_sqrt (t2));
	gnm_float z2 = (gnm_log (s / x1) + (b + (v * v) / 2) * t1) / (v * gnm_sqrt (t1));

	gnm_float gfresult;

	switch (call_put) {
	case OS_Call:
		gfresult = opt_bs1 (call_put, s, x1, t1, r, v, b) +
			s * gnm_exp ((b - r) * t2) * cum_biv_norm_dist1 (z1, -z2, -rho) -
			x2 * gnm_exp (-r * t2) * cum_biv_norm_dist1 (z1 - gnm_sqrt ((v * v) * t2), -z2 + gnm_sqrt ((v * v) * t1), -rho);
	break;
	case OS_Put:
		gfresult = opt_bs1 (call_put, s, x1, t1, r, v, b) +
			x2 * gnm_exp (-r * t2) * cum_biv_norm_dist1 (-z1 + gnm_sqrt ((v * v) * t2), z2 - gnm_sqrt ((v * v) * t1), -rho) -
			s * gnm_exp ((b - r) * t2) * cum_biv_norm_dist1 (-z1, z2, -rho);
	break;
	default:
		return value_new_error_NUM (ei->pos);
	}

	return value_new_float (gfresult);
}

static GnmFuncHelp const help_opt_extendible_writer[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_EXTENDIBLE_WRITER:theoretical price of extendible writer options")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
        { GNM_FUNC_HELP_ARG, F_("strike1:strike price at which the option is struck")},
        { GNM_FUNC_HELP_ARG, F_("strike2:strike price at which the option is re-struck if out of the money at @{time1}")},
        { GNM_FUNC_HELP_ARG, F_("time1:initial maturity of the option in years")},
        { GNM_FUNC_HELP_ARG, F_("time2:extended maturity in years if chosen")},
	DEF_ARG_RATE_RISKFREE_ANN,
	DEF_ARG_CC,
	DEF_ARG_VOLATILITY_SHORT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_EXTENDIBLE_WRITER models the theoretical price of extendible "
					"writer options. These are options that have their maturity "
					"extended to @{time2} if the option is "
					"out of the money at @{time1}.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};



/* Two asset correlation options */
static GnmValue *
opt_2_asset_correlation(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put = option_side (value_peek_string(argv[0]));
	gnm_float s1 = value_get_as_float (argv[1]);
	gnm_float s2 = value_get_as_float (argv[2]);
	gnm_float x1 = value_get_as_float (argv[3]);
	gnm_float x2 = value_get_as_float (argv[4]);
	gnm_float t = value_get_as_float (argv[5]);
	gnm_float b1 = value_get_as_float (argv[6]);
	gnm_float b2 = value_get_as_float (argv[7]);
	gnm_float r = value_get_as_float (argv[8]);
	gnm_float v1 = value_get_as_float (argv[9]);
	gnm_float v2 = value_get_as_float (argv[10]);
	gnm_float rho = value_get_as_float (argv[11]);

	gnm_float y1 = (gnm_log (s1 / x1) + (b1 - (v1 * v1) / 2) * t) / (v1 * gnm_sqrt (t));
	gnm_float y2 = (gnm_log (s2 / x2) + (b2 - (v2 * v2) / 2) * t) / (v2 * gnm_sqrt (t));

	if (call_put == OS_Call) {
		return value_new_float (s2 * gnm_exp ((b2 - r) * t)
					* cum_biv_norm_dist1 (y2 + v2 * gnm_sqrt (t), y1 + rho * v2 * gnm_sqrt (t), rho)
					- x2 * gnm_exp (-r * t) * cum_biv_norm_dist1 (y2, y1, rho));
	} else if (call_put == OS_Put) {
		return value_new_float (x2 * gnm_exp (-r * t) * cum_biv_norm_dist1 (-y2, -y1, rho)
					- s2 * gnm_exp ((b2 - r) * t) * cum_biv_norm_dist1 (-y2 - v2 * gnm_sqrt (t), -y1 - rho * v2 * gnm_sqrt (t), rho));
	} else
		return value_new_error_NUM (ei->pos);
}

static GnmFuncHelp const help_opt_2_asset_correlation[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_2_ASSET_CORRELATION:theoretical price of options on 2 assets with correlation @{rho}")},
	DEF_ARG_CALL_PUT_FLAG,
        { GNM_FUNC_HELP_ARG, F_("spot1:spot price of the underlying asset of the first option")},
        { GNM_FUNC_HELP_ARG, F_("spot2:spot price of the underlying asset of the second option")},
        { GNM_FUNC_HELP_ARG, F_("strike1:strike prices of the first option")},
        { GNM_FUNC_HELP_ARG, F_("strike2:strike prices of the second option")},
	DEF_ARG_TIME_MATURITY_Y,
        { GNM_FUNC_HELP_ARG, F_("cost_of_carry1:net cost of holding the underlying asset of the first option "
				"(for common stocks, the risk free rate less the dividend yield)")},
        { GNM_FUNC_HELP_ARG, F_("cost_of_carry2:net cost of holding the underlying asset of the second option "
				"(for common stocks, the risk free rate less the dividend yield)")},
	DEF_ARG_RATE_RISKFREE_ANN,
        { GNM_FUNC_HELP_ARG, F_("volatility1:annualized volatility in price of the underlying asset of the first option")},
        { GNM_FUNC_HELP_ARG, F_("volatility2:annualized volatility in price of the underlying asset of the second option")},
	{ GNM_FUNC_HELP_ARG, F_("rho:correlation between the two underlying assets")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_2_ASSET_CORRELATION models the theoretical price of options "
					"on 2 assets with correlation @{rho}. The payoff for a call is "
					"max(@{spot2} - @{strike2},0) if @{spot1} > @{strike1} or 0 otherwise. "
					"The payoff for a put is max (@{strike2} - @{spot2}, 0) if @{spot1} < @{strike1} or 0 otherwise.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* European option to exchange one asset for another */
static GnmValue *
opt_euro_exchange(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float s1 = value_get_as_float (argv[0]);
	gnm_float s2 = value_get_as_float (argv[1]);
	gnm_float q1 = value_get_as_float (argv[2]);
	gnm_float q2 = value_get_as_float (argv[3]);
	gnm_float t = value_get_as_float (argv[4]);
	gnm_float r = value_get_as_float (argv[5]);
	gnm_float b1 = value_get_as_float (argv[6]);
	gnm_float b2 = value_get_as_float (argv[7]);
	gnm_float v1 = value_get_as_float (argv[8]);
	gnm_float v2 = value_get_as_float (argv[9]);
	gnm_float rho = value_get_as_float (argv[10]);
	gnm_float v, d1, d2;

	v = gnm_sqrt (v1 * v1 + v2 * v2 - 2 * rho * v1 * v2);
	d1 = (gnm_log (q1 * s1 / (q2 * s2)) + (b1 - b2 + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	d2 = d1 - v * gnm_sqrt (t);

	return value_new_float (q1 * s1 * gnm_exp ((b1 - r) * t) * ncdf (d1) -
				q2 * s2 * gnm_exp ((b2 - r) * t) * ncdf (d2));
}

static GnmFuncHelp const help_opt_euro_exchange[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_EURO_EXCHANGE:theoretical price of a European option to exchange assets")},
	{ GNM_FUNC_HELP_ARG, F_("spot1:spot price of asset 1")},
        { GNM_FUNC_HELP_ARG, F_("spot2:spot price of asset 2")},
	{ GNM_FUNC_HELP_ARG, F_("qty1:quantity of asset 1")},
	{ GNM_FUNC_HELP_ARG, F_("qty2:quantity of asset 2")},
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE_ANN,
	{ GNM_FUNC_HELP_ARG, F_("cost_of_carry1:net cost of holding asset 1 "
				"(for common stocks, the risk free rate less the dividend yield)")},
	{ GNM_FUNC_HELP_ARG, F_("cost_of_carry2:net cost of holding asset 2 "
				"(for common stocks, the risk free rate less the dividend yield)")},
	{ GNM_FUNC_HELP_ARG, F_("volatility1:annualized volatility in price of asset 1")},
	{ GNM_FUNC_HELP_ARG, F_("volatility2:annualized volatility in price of asset 2")},
	{ GNM_FUNC_HELP_ARG, F_("rho:correlation between the prices of the two assets")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_EURO_EXCHANGE models the theoretical price of a European "
					"option to exchange one asset with quantity @{qty2} and spot "
					"price @{spot2} for another with quantity @{qty1} and spot price "
					"@{spot1}.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_AMER_EXCHANGE,OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* American option to exchange one asset for another */
static GnmValue *
opt_amer_exchange(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float s1 = value_get_as_float (argv[0]);
	gnm_float s2 = value_get_as_float (argv[1]);
	gnm_float q1 = value_get_as_float (argv[2]);
	gnm_float q2 = value_get_as_float (argv[3]);
	gnm_float t = value_get_as_float (argv[4]);
	gnm_float r = value_get_as_float (argv[5]);
	gnm_float b1 = value_get_as_float (argv[6]);
	gnm_float b2 = value_get_as_float (argv[7]);
	gnm_float v1 = value_get_as_float (argv[8]);
	gnm_float v2 = value_get_as_float (argv[9]);
	gnm_float rho = value_get_as_float (argv[10]);
	gnm_float v = gnm_sqrt (v1 * v1 + v2 * v2 - 2 * rho * v1 * v2);

	return value_new_float (opt_bjer_stens1 (OS_Call, q1 * s1, q2 * s2, t, r - b2, v,b1 - b2));
}

static GnmFuncHelp const help_opt_amer_exchange[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_AMER_EXCHANGE:theoretical price of an American option to exchange assets")},
	{ GNM_FUNC_HELP_ARG, F_("spot1:spot price of asset 1")},
        { GNM_FUNC_HELP_ARG, F_("spot2:spot price of asset 2")},
	{ GNM_FUNC_HELP_ARG, F_("qty1:quantity of asset 1")},
	{ GNM_FUNC_HELP_ARG, F_("qty2:quantity of asset 2")},
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE_ANN,
	{ GNM_FUNC_HELP_ARG, F_("cost_of_carry1:net cost of holding asset 1 "
				"(for common stocks, the risk free rate less the dividend yield)")},
	{ GNM_FUNC_HELP_ARG, F_("cost_of_carry2:net cost of holding asset 2 "
				"(for common stocks, the risk free rate less the dividend yield)")},
	{ GNM_FUNC_HELP_ARG, F_("volatility1:annualized volatility in price of asset 1")},
	{ GNM_FUNC_HELP_ARG, F_("volatility2:annualized volatility in price of asset 2")},
	{ GNM_FUNC_HELP_ARG, F_("rho:correlation between the prices of the two assets")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_AMER_EXCHANGE models the theoretical price of an American "
					"option to exchange one asset with quantity @{qty2} and spot "
					"price @{spot2} for another with quantity @{qty1} and spot price "
					"@{spot1}.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_EURO_EXCHANGE,OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Spread option approximation */
static GnmValue *
opt_spread_approx(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put_flag = option_side (value_peek_string(argv[0]));
	gnm_float f1 = value_get_as_float (argv[1]);
	gnm_float f2 = value_get_as_float (argv[2]);
	gnm_float x = value_get_as_float (argv[3]);
	gnm_float t = value_get_as_float (argv[4]);
	gnm_float r = value_get_as_float (argv[5]);
	gnm_float v1 = value_get_as_float (argv[6]);
	gnm_float v2 = value_get_as_float (argv[7]);
	gnm_float rho = value_get_as_float (argv[8]);

	gnm_float v = gnm_sqrt (v1 * v1 + gnm_pow ((v2 * f2 / (f2 + x)), 2) - 2 * rho * v1 * v2 * f2 / (f2 + x));
	gnm_float F = f1 / (f2 + x);

	return value_new_float (opt_bs1 (call_put_flag, F, 1, t, r, v, 0) * (f2 + x));
}

static GnmFuncHelp const help_opt_spread_approx[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_SPREAD_APPROX:theoretical price of a European option on the spread between two futures contracts")},
	DEF_ARG_CALL_PUT_FLAG,
        { GNM_FUNC_HELP_ARG, F_("fut_price1:price of the first futures contract")},
        { GNM_FUNC_HELP_ARG, F_("fut_price2:price of the second futures contract")},
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE_ANN,
        { GNM_FUNC_HELP_ARG, F_("volatility1:annualized volatility in price of the first underlying futures contract")},
        { GNM_FUNC_HELP_ARG, F_("volatility2:annualized volatility in price of the second underlying futures contract")},
        { GNM_FUNC_HELP_ARG, F_("rho:correlation between the two futures contracts")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Floating strike lookback options */
static GnmValue *
opt_float_strk_lkbk(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put_flag = option_side (value_peek_string(argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float s_min = value_get_as_float (argv[2]);
	gnm_float s_max = value_get_as_float (argv[3]);
	gnm_float t = value_get_as_float (argv[4]);
	gnm_float r = value_get_as_float (argv[5]);
	gnm_float b = value_get_as_float (argv[6]);
	gnm_float v = value_get_as_float (argv[7]);

	gnm_float a1, a2, m;

	if(OS_Call == call_put_flag)
		m = s_min;
	else if(OS_Put == call_put_flag)
		m = s_max;
	else
		return value_new_error_NUM (ei->pos);

	a1 = (gnm_log (s / m) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	a2 = a1 - v * gnm_sqrt (t);

	if(OS_Call == call_put_flag)
		return value_new_float (s * gnm_exp ((b - r) * t) * ncdf (a1) -
					m * gnm_exp (-r * t) * ncdf (a2) +
					gnm_exp (-r * t) * (v * v) / (2 * b) * s * (gnm_pow (s / m, (-2 * b / (v * v))) * ncdf (-a1 + 2 * b / v * gnm_sqrt (t)) -
										    gnm_exp (b * t) * ncdf (-a1)));
	else if(OS_Put == call_put_flag)
		return value_new_float (m * gnm_exp (-r * t) * ncdf (-a2) -
					s * gnm_exp ((b - r) * t) * ncdf (-a1) +
					gnm_exp (-r * t) * (v * v) / (2 * b) * s * (-gnm_pow (s / m, ((-2 * b) / (v * v))) * ncdf (a1 - 2 * b / v * gnm_sqrt (t)) +
										    gnm_exp (b * t) * ncdf (a1)));

	return value_new_error_VALUE (ei->pos);
}

static GnmFuncHelp const help_opt_float_strk_lkbk[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_FLOAT_STRK_LKBK:theoretical price of floating-strike lookback option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
        { GNM_FUNC_HELP_ARG, F_("spot_min:minimum spot price of the underlying asset so far observed")},
        { GNM_FUNC_HELP_ARG, F_("spot_max:maximum spot price of the underlying asset so far observed")},
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE_ANN,
	DEF_ARG_CC,
        DEF_ARG_VOLATILITY_SHORT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_FLOAT_STRK_LKBK determines the theoretical price of a "
					"floating-strike lookback option where the holder "
					"of the option may exercise on expiry at the most favourable price "
					"observed during the options life of the underlying asset.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};


/* Fixed strike lookback options */

static GnmValue *
opt_fixed_strk_lkbk(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionSide call_put_flag = option_side (value_peek_string(argv[0]));
	gnm_float s = value_get_as_float (argv[1]);
	gnm_float s_min = value_get_as_float (argv[2]);
	gnm_float s_max = value_get_as_float (argv[3]);
	gnm_float x = value_get_as_float (argv[4]);
	gnm_float t = value_get_as_float (argv[5]);
	gnm_float r = value_get_as_float (argv[6]);
	gnm_float b = value_get_as_float (argv[7]);
	gnm_float v = value_get_as_float (argv[8]);

	gnm_float d1, d2;
	gnm_float e1, e2, m;

	if (OS_Call == call_put_flag)
		m = s_max;
	else if (OS_Put == call_put_flag)
		m = s_min;
	else
		return value_new_error_VALUE (ei->pos);

	d1 = (gnm_log (s / x) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	d2 = d1 - v * gnm_sqrt (t);
	e1 = (gnm_log (s / m) + (b + (v * v) / 2) * t) / (v * gnm_sqrt (t));
	e2 = e1 - v * gnm_sqrt (t);

	if (OS_Call == call_put_flag && x > m)
		return value_new_float (s * gnm_exp ((b - r) * t) * ncdf (d1) - x * gnm_exp (-r * t) * ncdf (d2) + s * gnm_exp (-r * t) * (v * v) / (2 * b) * (-gnm_pow ((s / x), (-2 * b / (v * v))) * ncdf (d1 - 2 * b / v * gnm_sqrt (t)) + gnm_exp (b * t) * ncdf (d1)));

	else if (OS_Call == call_put_flag && x <= m)
		return value_new_float (gnm_exp (-r * t) * (m - x) + s * gnm_exp ((b - r) * t) * ncdf (e1) - gnm_exp (-r * t) * m * ncdf (e2) + s * gnm_exp (-r * t) * (v * v) / (2 * b) * (-gnm_pow ((s / m), (-2 * b / (v * v))) * ncdf (e1 - 2 * b / v * gnm_sqrt (t)) + gnm_exp (b * t) * ncdf (e1)));

	else if (OS_Put == call_put_flag && x < m)
		return value_new_float (-s * gnm_exp ((b - r) * t) * ncdf (-d1) + x * gnm_exp (-r * t) * ncdf (-d1 + v * gnm_sqrt (t)) + s * gnm_exp (-r * t) * (v * v) / (2 * b) * (gnm_pow ((s / x), (-2 * b / (v * v))) * ncdf (-d1 + 2 * b / v * gnm_sqrt (t)) - gnm_exp (b * t) * ncdf (-d1)));

	else if (OS_Put == call_put_flag && x >= m)
		return value_new_float (gnm_exp (-r * t) * (x - m) - s * gnm_exp ((b - r) * t) * ncdf (-e1) + gnm_exp (-r * t) * m * ncdf (-e1 + v * gnm_sqrt (t)) + gnm_exp (-r * t) * (v * v) / (2 * b) * s * (gnm_pow ((s / m), (-2 * b / (v * v))) * ncdf (-e1 + 2 * b / v * gnm_sqrt (t)) - gnm_exp (b * t) * ncdf (-e1)));

	return value_new_error_VALUE (ei->pos);
}

static GnmFuncHelp const help_opt_fixed_strk_lkbk[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_FIXED_STRK_LKBK:theoretical price of a fixed-strike lookback option")},
	DEF_ARG_CALL_PUT_FLAG,
	DEF_ARG_SPOT,
        { GNM_FUNC_HELP_ARG, F_("spot_min:minimum spot price of the underlying asset so far observed")},
        { GNM_FUNC_HELP_ARG, F_("spot_max:maximum spot price of the underlying asset so far observed")},
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE_ANN,
	DEF_ARG_CC,
        DEF_ARG_VOLATILITY_SHORT,
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OPT_FIXED_STRK_LKBK determines the theoretical price of a "
					"fixed-strike lookback option where the holder "
					"of the option may exercise on expiry at the most favourable price "
					"observed during the options life of the underlying asset.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};



/* Binomial Tree valuation */
static GnmValue *
opt_binomial(GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	OptionType amer_euro_flag = option_type(value_peek_string(argv[0]));
	OptionSide call_put_flag = option_side (value_peek_string(argv[1]));
	gnm_float n = gnm_floor (value_get_as_float (argv[2]));
	gnm_float s = value_get_as_float (argv[3]);
	gnm_float x = value_get_as_float (argv[4]);
	gnm_float t = value_get_as_float (argv[5]);
	gnm_float r = value_get_as_float (argv[6]);
	gnm_float v = value_get_as_float (argv[7]);
	gnm_float b = argv[8] ? value_get_as_float (argv[8]) : 0;

	gnm_float *value_array;
	gnm_float u, d, p, dt, Df, temp1, temp2, gf_result;
	gint i, j, z;

	if (n < 0 || n > 100000)
		return value_new_error_NUM (ei->pos);

	if (OS_Call == call_put_flag)
		z = 1;
        else if (OS_Put == call_put_flag)
		z = -1;
	else
		return value_new_error_NUM (ei->pos);

	if (OT_Error == amer_euro_flag)
		return value_new_error_NUM (ei->pos);

	value_array = (gnm_float *) g_try_malloc ((n + 2)* sizeof(gnm_float));
	if (value_array == NULL)
		return value_new_error_NUM (ei->pos);

	dt = t / n;
	u = gnm_exp (v * gnm_sqrt (dt));
	d = 1 / u;
	p = (gnm_exp (b * dt) - d) / (u - d);
	Df = gnm_exp (-r * dt);

	for (i = 0; i <= n; ++i) {
		temp1 = z * (s * gnm_pow (u, i) * gnm_pow (d, (n - i)) - x);
		value_array[i] = MAX (temp1, 0);
	    }

	for (j = n - 1; j > -1; --j) {
		for (i = 0; i <= j; ++i) {
			/*if (0==i)g_printerr("secondloop %d\n",j);*/
			if (OT_Euro == amer_euro_flag)
				value_array[i] = (p * value_array[i + 1] + (1 - p) * value_array[i]) * Df;
			else if (OT_Amer == amer_euro_flag) {
				temp1 = (z * (s * gnm_pow (u, i) * gnm_pow (d, (gnm_abs (i - j))) - x));
				temp2 = (p * value_array[i + 1] + (1 - p) * value_array[i]) * Df;
				value_array[i] = MAX (temp1, temp2);
			}
		}
	}
	gf_result = value_array[0];
	g_free (value_array);
	return value_new_float (gf_result);
}

static GnmFuncHelp const help_opt_binomial[] = {
	{ GNM_FUNC_HELP_NAME, F_("OPT_BINOMIAL:theoretical price of either an American or European style option using a binomial tree")},
        { GNM_FUNC_HELP_ARG, F_("amer_euro_flag:'a' for an American style option or 'e' for a European style option")},
	DEF_ARG_CALL_PUT_FLAG,
        { GNM_FUNC_HELP_ARG, F_("num_time_steps:number of time steps used in the valuation")},
	DEF_ARG_SPOT,
	DEF_ARG_STRIKE,
	DEF_ARG_TIME_MATURITY_Y,
	DEF_ARG_RATE_RISKFREE_ANN,
        DEF_ARG_VOLATILITY_SHORT,
	DEF_ARG_CC,
	{ GNM_FUNC_HELP_NOTE, F_("A larger @{num_time_steps} yields greater accuracy but  OPT_BINOMIAL is slower to calculate.")},
        { GNM_FUNC_HELP_SEEALSO, "OPT_BS,OPT_BS_DELTA,OPT_BS_RHO,OPT_BS_THETA,OPT_BS_GAMMA"},
        { GNM_FUNC_HELP_END}
};



GnmFuncDescriptor const derivatives_functions [] = {
	{ "opt_bs",
	  "sfffff|f",
	  help_opt_bs, opt_bs, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_delta",
	  "sfffff|f",
	  help_opt_bs_delta, opt_bs_delta, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_rho",
	  "sfffff|f",
	  help_opt_bs_rho, opt_bs_rho, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_theta",
	  "sfffff|f",
	  help_opt_bs_theta, opt_bs_theta, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_gamma",
	  "fffff|f",
	  help_opt_bs_gamma, opt_bs_gamma, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_vega",
	  "fffff|f",
	  help_opt_bs_vega, opt_bs_vega, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bs_carrycost",
	  "sfffff|f",
	  help_opt_bs_carrycost, opt_bs_carrycost, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "cum_biv_norm_dist",
	  "fff",
	  help_cum_biv_norm_dist, cum_biv_norm_dist, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "opt_garman_kohlhagen",
	  "sffffff",
	  help_opt_garman_kohlhagen, opt_garman_kohlhagen, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_french",
	  "sfffffff",
	  help_opt_french, opt_french, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_jump_diff",
	  "sfffffff",
	  help_opt_jump_diff, opt_jump_diff, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_exec",
	  "sfffffff",
	  help_opt_exec, opt_exec, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_bjer_stens",
	  "sffffff",
	  help_opt_bjer_stens, opt_bjer_stens, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_miltersen_schwartz",
	  "sfffffffffffff",
	  help_opt_miltersen_schwartz, opt_miltersen_schwartz, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_baw_amer",
	  "sffffff",
	  help_opt_baw_amer, opt_baw_amer, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_rgw",
	  "fffffff",
	  help_opt_rgw, opt_rgw, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_forward_start",
	  "sfffffff",
	  help_opt_forward_start, opt_forward_start, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_time_switch",
	  "sfffffffff",
	  help_opt_time_switch, opt_time_switch, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_simple_chooser",
	  "fffffff",
	  help_opt_simple_chooser, opt_simple_chooser, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_complex_chooser",
	  "fffffffff",
	  help_opt_complex_chooser, opt_complex_chooser, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_on_options",
	  "sffffffff",
	  help_opt_on_options, opt_on_options, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_extendible_writer",
	  "sffffffff",
	  help_opt_extendible_writer, opt_extendible_writer, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_2_asset_correlation",
	  "sfffffffffff",
	  help_opt_2_asset_correlation, opt_2_asset_correlation, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_euro_exchange",
	  "fffffffffff",
	  help_opt_euro_exchange, opt_euro_exchange, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_amer_exchange",
	  "fffffffffff",
	  help_opt_amer_exchange, opt_amer_exchange, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_spread_approx",
	  "sffffffff",
	  help_opt_spread_approx, opt_spread_approx, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_float_strk_lkbk",
	  "sfffffff",
	  help_opt_float_strk_lkbk, opt_float_strk_lkbk, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ "opt_fixed_strk_lkbk",
	  "sffffffff",
	  help_opt_fixed_strk_lkbk, opt_fixed_strk_lkbk, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },


	{ "opt_binomial",
	  "ssffffff|f",
	  help_opt_binomial, opt_binomial, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },

	{ NULL}
};
