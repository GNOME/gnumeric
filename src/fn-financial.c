/*
 * fn-financial.c:  Built in financial functions and functions registration
 *
 * Authors:
 *  Vladimir Vuksan (vuksan@veus.hr)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 *

 */
#include <config.h>
#include <math.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"
#include "goal-seek.h"

/*

Below are some of the functions that are used quite often in
financial analysis.

Present value interest factor

	 PVIF = (1 + k) ^ n

Future value interest factor

         FVIF = 1 / PVIF

Present value interest factor of annuities

                  1          1
	 PVIFA = --- - -----------
                  k     k*(1+k)^n

Future value interest factor of annuities

                  (1+k)^n - 1
         FVIFA = ----------------
	                k



	 PV * PVIF(k%, nper) + PMT * ( 1 + rate * type ) * FVIFA(k%, nper) + FV = 0

 */

static float_t
calculate_pvif (float_t rate, float_t nper)
{
	return (pow (1+rate, nper));
}

#if 0
static float_t
calculate_fvif (float_t rate, float_t nper)
{
	return (1.0 / calculate_pvif (rate,nper));
}

static float_t
calculate_pvifa (float_t rate, float_t nper)
{
	return ((1.0 / rate) - (1.0 / (rate * pow (1+rate, nper))));
}
#endif

static float_t
calculate_fvifa (float_t rate, float_t nper)
{
	return ((pow (1+rate, nper) - 1) / rate);
}

/*

Principal for period x is calculated using this formula

PR(x) = PR(0) * ( 1 + rate ) ^ x + PMT * ( ( 1 + rate ) ^ x - 1 ) / rate )

*/

static float_t
calculate_principal (float_t starting_principal, float_t payment, float_t rate, float_t period)
{
	return (starting_principal * pow (1.0 + rate, period) + payment *
		((pow(1+rate, period) - 1) / rate));
}

static float_t
calculate_pmt (float_t rate, float_t nper, float_t pv, float_t fv, int type)
{
	float_t pvif, fvifa;

	/* Calculate the PVIF and FVIFA */

	pvif = calculate_pvif (rate,nper);
	fvifa = calculate_fvifa (rate,nper);

        return (((-1.0) * pv * pvif  - fv ) / ((1.0 + rate * type) * fvifa));
}


static char *help_effect = {
	N_("@FUNCTION=EFFECT\n"
	   "@SYNTAX=EFFECT(b1,b2)\n"
	   "@DESCRIPTION=Calculates the effective interest rate from "
	   "a given nominal rate.\n"
	   "Effective interest rate is calculated using this formulae:\n"
	   "\n"
	   "         r"
           "( 1 + ------ ) ^ nper - 1"
           "       nper"
	   "\n"
	   "where:\n"
	   "\n"
	   "r = nominal interest rate (stated in yearly terms)"
	   "nper = number of periods used for compounding"
	   "\n"
	   "For example credit cards will list an APR (annual percentage rate) which "
	   "is a nominal interest rate."
	   "\n"
	   "For example if you wanted to find out how much you are actually paying interest "
	   "on your credit card that states an APR of 19% that is compounded monthly "
	   "you would type in:"
	   "\n"
	   "=EFFECT(.19,12) and you would get .2075 or 20.75%. That is the effective percentage you will "
	   "pay on your loan."
	   "\n"
	   "@SEEALSO=NOMINAL")
};


static Value *
gnumeric_effect (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate;
	int nper;

	rate = value_get_as_float (argv[0]);
	nper = value_get_as_int (argv[1]);

	/* Rate or number of periods cannot be negative */
	if ( (rate < 0) || (nper <= 0) )
		return function_error (ei, _("effect - domain error"));

        return value_new_float ( pow( (1 + rate/nper) , nper) - 1 );

}


static char *help_nominal = {
	N_("@FUNCTION=NOMINAL\n"
	   "@SYNTAX=NOMINAL(b1,b2)\n"
	   "@DESCRIPTION=Calculates the nominal interest rate from "
	   "a given effective rate.\n"
	   "Nominal interest rate is given by a formula:\n"
	   "\n"
           "nper * (( 1 + r ) ^ (1 / nper) - 1 )"
	   "\n"
	   "where:\n"
	   "\n"
	   "r = effective interest rate"
	   "nper = number of periods used for compounding"
	   "\n"
	   "@SEEALSO=EFFECT")
};

static Value *
gnumeric_nominal (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate;
	int nper;

	rate = value_get_as_float (argv[0]);
	nper = value_get_as_int (argv[1]);

	/* Rate or number of periods cannot be negative */
	if ( (rate < 0) || (nper <= 0) )
		return function_error (ei, _("nominal - domain error"));

        return value_new_float ( nper * ( pow( 1 + rate, 1.0/nper ) - 1 ) );

}




static char *help_sln = {
	N_("@FUNCTION=SLN\n"
	   "@SYNTAX=SLN(cost,salvage value,life)\n"

	   "@DESCRIPTION=Calculates the straight line depriciation for an"
	   "asset based on its cost, salvage value and anticipated life."
	   "\n"
	   "Formula for straight line depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) / life"
	   "\n"
	   "\tcost = cost of an asset when acquired (market value)"
	   "\tsalvage_value = amount you get when asset sold at the end of life"
	   "\tlife = anticipated life of an asset"
	   "@SEEALSO=SYD")
};


static Value *
gnumeric_sln (FunctionEvalInfo *ei, Value **argv)
{
	float_t cost,salvage_value,life;

	cost = value_get_as_float (argv[0]);
	salvage_value = value_get_as_int (argv[1]);
	life = value_get_as_float (argv[2]);

	/* Life of an asset cannot be negative */
	if (life <= 0)
		return function_error (ei, _("sln - domain error"));

        return value_new_float ((cost - salvage_value) / life);
}

static char *help_syd = {
	N_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage value,life,period)\n"

	   "@DESCRIPTION=Calculates the sum-of-years digits depriciation for an"
	   "asset based on its cost, salvage value, anticipated life and a"
	   "particular period."
	   "\n"
	   "Formula for sum-of-years digits depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) * (life-period+1) * 2 / life * (life + 1)"
	   "\n"
	   "\tcost = cost of an asset when acquired (market value)"
	   "\tsalvage_value = amount you get when asset sold at the end of life"
	   "\tlife = anticipated life of an asset"
	   "\tperiod = period for which we need the expense"
	   "@SEEALSO=SLN")
};

static Value *
gnumeric_syd (FunctionEvalInfo *ei, Value **argv)
{
	float_t cost, salvage_value, life, period;

	cost   = value_get_as_float (argv [0]);
	salvage_value = value_get_as_int (argv [1]);
	life   = value_get_as_float (argv [2]);
	period = value_get_as_float (argv [3]);

	/* Life of an asset cannot be negative */
	if (life <= 0)
		return function_error (ei, _("syd - domain error"));

        return value_new_float (((cost - salvage_value) * (life - period + 1) * 2) / (life * (life + 1.0)));
}

static char *help_dollarde = {
	N_("@FUNCTION=DOLLARDE\n"
	   "@SYNTAX=DOLLARDE(fractional_dollar,fraction)\n"
	   "@DESCRIPTION=DOLLARDE converts a dollar price expressed as a "
	   "fraction into a dollar price expressed as a decimal number. "
	   "\n"
	   "If fraction is non-integer it is truncated. "
	   "If fraction<=0 DOLLARDE returns #NUM! error. "
	   "@SEEALSO=DOLLARFR")
};


static Value *
gnumeric_dollarde (FunctionEvalInfo *ei, Value **argv)
{
        float_t fractional_dollar;
	int     fraction, n, tmp;
	float_t floored, rest;

	fractional_dollar = value_get_as_float (argv [0]);
	fraction = value_get_as_int (argv [1]);

	if (fraction <= 0)
                return function_error (ei, gnumeric_err_NUM);

	tmp = fraction;
	/* Count digits in fraction */
	for (n=0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;
	tmp = (int) (rest * pow(10, n));

	return value_new_float (floored + ((float_t) tmp / fraction));
}

static char *help_dollarfr = {
	N_("@FUNCTION=DOLLARFR\n"
	   "@SYNTAX=DOLLARFR(decimal_dollar,fraction)\n"
	   "@DESCRIPTION=DOLLARFR converts a decimal dollar price into "
	   "a dollar price expressed as a fraction. "
	   "\n"
	   "If fraction is non-integer it is truncated. "
	   "If fraction<=0 DOLLARDE returns #NUM! error. "
	   "@SEEALSO=DOLLARDE")
};


static Value *
gnumeric_dollarfr (FunctionEvalInfo *ei, Value **argv)
{
        float_t fractional_dollar;
	int     fraction, n, tmp;
	float_t floored, rest;

	fractional_dollar = value_get_as_float (argv [0]);
	fraction = value_get_as_int (argv [1]);

	if (fraction <= 0)
                return function_error (ei, gnumeric_err_NUM);

	tmp = fraction;
	/* Count digits in fraction */
	for (n=0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;
	tmp = (int) (rest * fraction);

	return value_new_float (floored + ((float_t) tmp / pow(10, n)));
}


static char *help_rate = {
	N_("@FUNCTION=RATE\n"
	   "@SYNTAX=RATE(nper,pmt,pv[,fv,type,guess])\n"
	   "@DESCRIPTION=Calculates rate of an investment."
	   "@SEEALSO=PV,FV")
};

typedef struct {
	int nper, type;
	float_t pv, fv, pmt;
} gnumeric_rate_t;

static GoalSeekStatus
gnumeric_rate_f (float_t rate, float_t *y, void *user_data)
{
	if (rate > -1.0) {
		gnumeric_rate_t *data = user_data;

		*y = data->pv * calculate_pvif (rate, data->nper) +
			data->pmt * (1 + rate * data->type) * calculate_fvifa (rate, data->nper) +
			data->fv;
		return GOAL_SEEK_OK;
	} else
		return GOAL_SEEK_ERROR;
}

/* The derivative of the above function with respect to rate.  */
static GoalSeekStatus
gnumeric_rate_df (float_t rate, float_t *y, void *user_data)
{
	if (rate > -1.0 && rate != 0.0) {
		gnumeric_rate_t *data = user_data;

		*y = -data->pmt * calculate_fvifa (rate, data->nper) / rate +
			calculate_pvif (rate, data->nper - 1) * data->nper *
			(data->pv + data->pmt * (data->type + 1 / rate));
		return GOAL_SEEK_OK;
	} else
		return GOAL_SEEK_ERROR;
}


static Value *
gnumeric_rate (FunctionEvalInfo *ei, Value **argv)
{
	GoalSeekData data;
	GoalSeekStatus status;
	gnumeric_rate_t udata;
	float_t rate0;

	udata.nper = value_get_as_int (argv [0]);
	udata.pmt  = value_get_as_float (argv [1]);
	udata.pv   = value_get_as_float (argv [2]);
	udata.fv   = argv[3] ? value_get_as_float (argv [3]) : 0.0;
	udata.type = argv[4] ? value_get_as_int (argv [4]) : 0;
	/* Ignore the guess in argv[5].  */

	if (udata.nper <= 0)
		return function_error (ei, gnumeric_err_NUM);

	if (udata.type != 0 && udata.type != 1)
		return function_error (ei, gnumeric_err_VALUE);

	if (udata.pmt == 0) {
		if (udata.pv == 0 || udata.pv * udata.fv > 0)
			return function_error (ei, gnumeric_err_NUM);
		else {
			/* Exact case.  */
			return value_new_float (pow (-udata.fv / udata.pv, -1.0 / udata.nper) - 1);
		}
	}

	if (udata.pv == 0)
		rate0 = 0.1;  /* Whatever.  */
	else {
		/* Root finding case.  The following was derived by setting
		   type==0 and estimating (1+r)^n ~= 1+rn.  */
		rate0 = -((udata.pmt * udata.nper + udata.fv) / udata.pv + 1) / udata.nper;
	}

#if 0
	printf ("Guess = %.15g\n", rate0);
#endif
	goal_seek_initialise (&data);
	status = goal_seek_newton (&gnumeric_rate_f, &gnumeric_rate_df,
				   &data, &udata, rate0);
	if (status == GOAL_SEEK_OK) {
#if 0
		printf ("Root = %.15g\n\n", data.root);
#endif
		return value_new_float (data.root);
	} else
		return function_error (ei, gnumeric_err_NUM);
}

static char *help_pv = {
	N_("@FUNCTION=PV\n"
	   "@SYNTAX=PV(rate,nper,pmt,fv,type)\n"
	   "@DESCRIPTION=Calculates the present value of an investment."
	   "@SEEALSO=FV")
};


static Value *
gnumeric_pv (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, nper, pmt, fv;
	float_t pvif, fvifa;
	int type;

	rate = value_get_as_float (argv [0]);
	nper = value_get_as_float (argv [1]);
	pmt  = value_get_as_float (argv [2]);
	fv   = value_get_as_float (argv [3]);
	type = value_get_as_int (argv [4]);

	if (rate <= 0.0)
		return function_error (ei, _("pv - domain error"));

	/* Calculate the PVIF and FVIFA */
	pvif = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

        return value_new_float ( ( (-1.0) * fv - pmt * ( 1.0 + rate * type ) * fvifa ) / pvif );
}

static char *help_npv = {
	N_("@FUNCTION=NPV\n"
	   "@SYNTAX=NPV(rate,v1,v2,...)\n"
	   "@DESCRIPTION=Calculates the net present value of an investment."
	   "@SEEALSO=PV")
};

typedef struct {
        guint32 num;
        float_t rate;
        float_t sum;
} financial_npv_t;

static int
callback_function_npv (const EvalPosition *ep, Value *value, ErrorMessage *error, void *closure)
{
        financial_npv_t *mm = closure;

	if (!VALUE_IS_NUMBER (value))
		return TRUE;

	if (mm->num == 0) {
		mm->rate = value_get_as_float (value);
	} else
		mm->sum += value_get_as_float (value) / pow (1 + mm->rate, mm->num);
	mm->num++;
        return TRUE;
}

static Value *
gnumeric_npv (FunctionEvalInfo *ei, GList *nodes)
{
        financial_npv_t p;

	p.sum   = 0.0;
	p.num   = 0;

	function_iterate_argument_values (&ei->pos, callback_function_npv,
                                          &p, nodes,
					  ei->error, TRUE);
	return value_new_float (p.sum);
}


static char *help_fv = {
	N_("@FUNCTION=FV\n"
	   "@SYNTAX=FV(rate,nper,pmt,pv,type)\n"
	   "@DESCRIPTION=Calculates the future value of an investment."
	   "@SEEALSO=PV,PMT,PPMT")
};


static Value *
gnumeric_fv (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, nper, pv, pmt;
	float_t pvif, fvifa;
	int type;

	rate = value_get_as_float (argv [0]);
	nper = value_get_as_float (argv [1]);
	pmt  = value_get_as_float (argv [2]);
	pv   = value_get_as_float (argv [3]);
	type = value_get_as_int (argv [4]);

	pvif = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

        return value_new_float (-1.0 * ((pv * pvif) + pmt * (1.0 + rate * type) * fvifa));
}



static char *help_pmt = {
	N_("@FUNCTION=PMT\n"
	   "@SYNTAX=PMT(rate,nper,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates the present value of an investment."
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_pmt (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, pv, fv, nper;
	int type;

	rate = value_get_as_float (argv [0]);
	nper = value_get_as_float (argv [1]);
	pv   = value_get_as_float (argv [2]);
	fv   = value_get_as_float (argv [3]);
	type = value_get_as_int (argv [4]);

        return value_new_float (calculate_pmt (rate, nper, pv, fv, type));
}


static char *help_ipmt = {
	N_("@FUNCTION=IPMT\n"
	   "@SYNTAX=IPMT(rate,per,nper,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates the amount of a payment of an annuity going "
	   "towards interest."
	   "\n"
	   "Formula for IPMT is:\n"
	   "\n"
	   "IPMT(PER) = PMT - PRINCIPAL(PER-1) * INTEREST_RATE"
	   "\n"
	   "where:"
	   "\n"
	   "PMT = Payment received on annuity"
	   "PRINCIPA(per-1) = amount of the remaining principal from last period"
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_ipmt (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, nper, per, pv, fv;
	float_t pmt;
	int type;

	rate = value_get_as_float (argv [0]);
	nper = value_get_as_float (argv [1]);
	per  = value_get_as_float (argv [2]);
	pv   = value_get_as_float (argv [3]);
	fv   = value_get_as_float (argv [4]);
	type = value_get_as_int (argv [5]);

	/* First calculate the payment */
        pmt = calculate_pmt (rate, nper, pv, fv, type);

	/* Now we need to calculate the amount of money going towards the
	   principal */

	return value_new_float (-calculate_principal (pv, pmt, rate, per-1) * rate);
}

static char *help_ppmt = {
	N_("@FUNCTION=PPMT\n"
	   "@SYNTAX=PPMT(rate,per,nper,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates the amount of a payment of an annuity going "
	   "towards principal."
	   "\n"
	   "Formula for it is:"
	   "\n"
	   "PPMT(per) = PMT - IPMT(per)"
	   "\n"
	   "where:"
	   "\n"
	   "PMT = Payment received on annuity"
	   "IPMT(per) = amount of interest for period per"
	   "\n"
	   "@SEEALSO=IPMT,PV,FV")
};

static Value *
gnumeric_ppmt (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, nper, per, pv, fv;
	float_t ipmt, pmt;
	int type;

	rate = value_get_as_float (argv [0]);
	nper = value_get_as_float (argv [1]);
	per  = value_get_as_float (argv [2]);
	pv   = value_get_as_float (argv [3]);
	fv   = value_get_as_float (argv [4]);
	type = value_get_as_int (argv [5]);

	/* First calculate the payment */
        pmt = calculate_pmt(rate,nper,pv,fv,type);

	/*
	 * Now we need to calculate the amount of money going towards the
	 * principal
	 */
	ipmt = -calculate_principal (pv, pmt, rate, per-1) * rate;

	return value_new_float (pmt - ipmt);
}


static char *help_nper = {
	N_("@FUNCTION=NPER\n"
	   "@SYNTAX=NPER(rate,pmt,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates number of periods of an investment."
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_nper (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, pmt, pv, fv, tmp;
	int type;

	rate = value_get_as_float (argv [0]);
	pmt = value_get_as_float (argv [1]);
	pv = value_get_as_float (argv [2]);
	fv = value_get_as_float (argv [3]);
	type = value_get_as_int (argv [4]);

	if (rate <= 0.0)
		return function_error (ei, gnumeric_err_DIV0);

	tmp = (pmt * (1.0 + rate * type) - fv * rate) / (pv * rate + pmt * (1.0 + rate * type));
	if (tmp <= 0.0)
		return function_error (ei, gnumeric_err_VALUE);

        return value_new_float (log (tmp) / log (1.0 + rate));
}

static char *help_duration = {
	N_("@FUNCTION=DURATION\n"
	   "@SYNTAX=DURATION(rate,pv,fv)\n"
	   "@DESCRIPTION=Calculates number of periods needed for an investment to "
	   "attain a desired value. This function is similar to FV and PV with a "
	   "difference that we do not need give the direction of cash flows e.g. "
	   "-100 for a cash outflow and +100 for a cash inflow."
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_duration (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate,pv,fv;

	rate = value_get_as_float (argv [0]);
	pv = value_get_as_float (argv [1]);
	fv = value_get_as_float (argv [2]);

	if (rate <= 0)
		return function_error (ei, gnumeric_err_DIV0);
	else if (fv == 0 || pv == 0)
		return function_error (ei, gnumeric_err_DIV0);
	else if (fv / pv < 0)
		return function_error (ei, gnumeric_err_VALUE);

        return value_new_float (log (fv / pv) / log (1.0 + rate));

}

void finance_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Financial"));

	function_add_args  (cat, "dollarde", "ff", "fractional_dollar,fraction", &help_dollarde, gnumeric_dollarde);
	function_add_args  (cat, "dollarfr", "ff", "decimal_dollar,fraction", &help_dollarfr, gnumeric_dollarfr);
	function_add_args  (cat, "duration", "fff", "rate,pv,fv", &help_duration,    gnumeric_duration);
	function_add_args  (cat, "effect", "ff",    "rate,nper",    &help_effect,   gnumeric_effect);
	function_add_args  (cat, "fv", "fffff", "rate,nper,pmt,pv,type", &help_fv,  gnumeric_fv);
	function_add_args  (cat, "ipmt", "ffffff", "rate,per,nper,pv,fv,type", &help_ipmt, gnumeric_ipmt);
	function_add_args  (cat, "nominal", "ff",    "rate,nper",    &help_nominal, gnumeric_nominal);
	function_add_args  (cat, "nper", "fffff", "rate,pmt,pv,fv,type", &help_nper, gnumeric_nper);
        function_add_nodes (cat, "npv",      0,      "",             &help_npv,     gnumeric_npv);
	function_add_args  (cat, "pmt", "fffff", "rate,nper,pv,fv,type", &help_pmt, gnumeric_pmt);
	function_add_args  (cat, "ppmt", "ffffff", "rate,per,nper,pv,fv,type", &help_ppmt, gnumeric_ppmt);
	function_add_args  (cat, "pv", "fffff", "rate,nper,pmt,fv,type", &help_pv,  gnumeric_pv);
	function_add_args  (cat, "rate", "fff|fff", "rate,nper,pmt,fv,type,guess", &help_rate,  gnumeric_rate);
	function_add_args  (cat, "sln", "fff", "cost,salvagevalue,life", &help_sln, gnumeric_sln);
	function_add_args  (cat, "syd", "ffff", "cost,salvagevalue,life,period", &help_syd, gnumeric_syd);
}
