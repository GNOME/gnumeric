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
#include "collect.h"

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
	   "@SYNTAX=EFFECT(r,nper)\n"
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
	   "@r = nominal interest rate (stated in yearly terms)\n"
	   "@nper = number of periods used for compounding"
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
		return value_new_error (&ei->pos, _("effect - domain error"));

        return value_new_float ( pow( (1 + rate/nper) , nper) - 1 );

}


static char *help_nominal = {
	N_("@FUNCTION=NOMINAL\n"
	   "@SYNTAX=NOMINAL(r,nper)\n"
	   "@DESCRIPTION=Calculates the nominal interest rate from "
	   "a given effective rate.\n"
	   "Nominal interest rate is given by a formula:\n"
	   "\n"
           "nper * (( 1 + r ) ^ (1 / nper) - 1 )"
	   "\n"
	   "where:\n"
	   "\n"
	   "r = effective interest rate\n"
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
		return value_new_error (&ei->pos, _("nominal - domain error"));

        return value_new_float ( nper * ( pow( 1 + rate, 1.0/nper ) - 1 ) );

}

static char *help_db = {
	N_("@FUNCTION=DB\n"
	   "@SYNTAX=DB(cost,salvage,life,period[,month])\n"
	   "@DESCRIPTION="
	   "DB returns the depreciation of an asset for a given period "
	   "using the fixed-declining balance method.  @cost is the "
	   "initial value of the asset.  @salvage after the depreciation. "
	   "@life is the number of periods overall.  @period is the period "
	   "for which you want the depreciation to be calculated.  @month "
	   "is the number of months in the first year of depreciation. "
	   "If @month is omitted, it is assumed to be 12. "
	   "\n"
	   "@SEEALSO=DDB,SLN,SYD,VDB")
};

static Value *
gnumeric_db (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate;
	float_t cost, salvage, life, period, month;
	float_t total;
	int     i;

	cost = value_get_as_float (argv[0]);
	salvage = value_get_as_float (argv[1]);
	life = value_get_as_float (argv[2]);
	period = value_get_as_float (argv[3]);
	if (argv[4] == NULL)
	        month = 12;
	else
	        month = value_get_as_float (argv[4]);

	rate = 1 - pow((salvage / cost), (1 / life));
	rate *= 1000;
	rate = floor(rate+0.5) / 1000;

	total = cost * rate * month / 12;

        if (period == 1)
	       return value_new_float (total);

	for (i=1; i<life; i++)
	       if (i == period-1)
		       return value_new_float ((cost - total) * rate);
	       else
		       total += (cost - total) * rate;

	return value_new_float (((cost - total) * rate * (12 - month)) / 12);
}

static char *help_ddb = {
	N_("@FUNCTION=DDB\n"
	   "@SYNTAX=DDB(cost,salvage,life,period[,factor])\n"
	   "@DESCRIPTION="
	   "DDB returns the depreciation of an asset for a given period "
	   "using the double-declining balance method or some other similar "
	   "method you specify.  @cost is the initial value of the asset, "
	   "@salvage is the value after the last period, @life is the "
	   "number of periods, @period is the period for which you want the "
	   "depreciation to be calculated, and @factor is the factor at "
	   "which the balance declines.  If @factor is omitted, it is "
	   "assumed to be two (double-declining balance method). "
	   "\n"
	   "@SEEALSO=SLN,SYD,VDB")
};

static Value *
gnumeric_ddb (FunctionEvalInfo *ei, Value **argv)
{
	float_t cost, salvage, life, period, factor;
	float_t total;
	int     i;

	cost = value_get_as_float (argv[0]);
	salvage = value_get_as_float (argv[1]);
	life = value_get_as_float (argv[2]);
	period = value_get_as_float (argv[3]);
	if (argv[4] == NULL)
	        factor = 2;
	else
	        factor = value_get_as_float (argv[4]);

	total = 0;
	for (i=0; i<life-1; i++) {
	        float_t period_dep = (cost - total) * (factor/life);
		if (period-1 == i)
		        return value_new_float (period_dep);
		else
		        total += period_dep;
	}

	return value_new_float (cost - total - salvage);
}

static char *help_sln = {
	N_("@FUNCTION=SLN\n"
	   "@SYNTAX=SLN(cost,salvage_value,life)\n"

	   "@DESCRIPTION=Calculates the straight line depriciation for an "
	   "asset based on its cost, salvage value and anticipated life."
	   "\n"
	   "Formula for straight line depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage_value ) / life"
	   "\n"
	   "\t@cost = cost of an asset when acquired (market value)"
	   "\t@salvage_value = amount you get when asset sold at the end of life"
	   "\t@life = anticipated life of an asset"
	   "\n"
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
		return value_new_error (&ei->pos, _("sln - domain error"));

        return value_new_float ((cost - salvage_value) / life);
}

static char *help_syd = {
	N_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage_value,life,period)\n"

	   "@DESCRIPTION="
	   "Calculates the sum-of-years digits depriciation for an "
	   "asset based on its cost, salvage value, anticipated life and a "
	   "particular period."
	   "\n"
	   "Formula for sum-of-years digits depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage_value ) * (life - period "
	   "+ 1) * 2 / life * (life + 1)."
	   "\n"
	   "\t@cost = cost of an asset when acquired (market value)"
	   "\t@salvage_value = amount you get when asset sold at the end of life"
	   "\t@life = anticipated life of an asset"
	   "\t@period = period for which we need the expense"
	   "\n"
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
		return value_new_error (&ei->pos, _("syd - domain error"));

        return value_new_float (((cost - salvage_value) * (life - period + 1) * 2) / (life * (life + 1.0)));
}

static char *help_dollarde = {
	N_("@FUNCTION=DOLLARDE\n"
	   "@SYNTAX=DOLLARDE(fractional_dollar,fraction)\n"
	   "@DESCRIPTION=DOLLARDE converts a dollar price expressed as a "
	   "fraction into a dollar price expressed as a decimal number. "
	   "\n"
	   "If @fraction is non-integer it is truncated. "
	   "If @fraction<=0 DOLLARDE returns #NUM! error. "
	   "\n"
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
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	tmp = fraction;
	/* Count digits in fraction */
	for (n=0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;

	return value_new_float (floored + ((float_t) rest * pow(10,n) /
					   fraction));
}

static char *help_dollarfr = {
	N_("@FUNCTION=DOLLARFR\n"
	   "@SYNTAX=DOLLARFR(decimal_dollar,fraction)\n"
	   "@DESCRIPTION=DOLLARFR converts a decimal dollar price into "
	   "a dollar price expressed as a fraction. "
	   "\n"
	   "If @fraction is non-integer it is truncated. "
	   "If @fraction <= 0 DOLLARDE returns #NUM! error. "
	   "\n"
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
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	/* Count digits in fraction */
	tmp = fraction;
	for (n=0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;

	return value_new_float (floored + ((float_t) (rest*fraction) /
					   pow(10, n)));
}


static char *help_rate = {
	N_("@FUNCTION=RATE\n"
	   "@SYNTAX=RATE(nper,pmt,pv[,fv,type,guess])\n"
	   "@DESCRIPTION=Calculates rate of an investment."
	   "\n"
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
		return value_new_error (&ei->pos, gnumeric_err_NUM);

	if (udata.type != 0 && udata.type != 1)
		return value_new_error (&ei->pos, gnumeric_err_VALUE);

	if (udata.pmt == 0) {
		if (udata.pv == 0 || udata.pv * udata.fv > 0)
			return value_new_error (&ei->pos, gnumeric_err_NUM);
		else {
			/* Exact case.  */
			return value_new_float (pow (-udata.fv / udata.pv,
						     -1.0 / udata.nper) - 1);
		}
	}

	if (udata.pv == 0)
		rate0 = 0.1;  /* Whatever.  */
	else {
		/* Root finding case.  The following was derived by setting
		   type==0 and estimating (1+r)^n ~= 1+rn.  */
		rate0 = -((udata.pmt * udata.nper + udata.fv) /
			  udata.pv + 1) / udata.nper;
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
		return value_new_error (&ei->pos, gnumeric_err_NUM);
}

static char *help_pv = {
	N_("@FUNCTION=PV\n"
	   "@SYNTAX=PV(rate,nper,pmt,fv,type)\n"
	   "@DESCRIPTION=Calculates the present value of an investment."
	   "\n"
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
		return value_new_error (&ei->pos, _("pv - domain error"));

	/* Calculate the PVIF and FVIFA */
	pvif = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

        return value_new_float ( ( (-1.0) * fv - pmt *
				   ( 1.0 + rate * type ) * fvifa ) / pvif );
}

static char *help_npv = {
	N_("@FUNCTION=NPV\n"
	   "@SYNTAX=NPV(rate,v1,v2,...)\n"
	   "@DESCRIPTION="
	   "NPV calculates the net present value of an investment generating "
	   "peridic payments.  @rate is the periodic interest rate and "
	   "@v1, @v2, ... are the periodic payments. If the schedule of the "
	   "cash flows are not periodic use the XNPV function. "
	   "\n"
	   "@SEEALSO=PV,XNPV")
};

typedef struct {
        float_t rate;
        float_t sum;
        int     num;
} financial_npv_t;

static Value *
callback_function_npv (const EvalPosition *ep, Value *value, void *closure)
{
        financial_npv_t *mm = closure;

	if (!VALUE_IS_NUMBER (value))
		return NULL;
	if (mm->num == 0) {
		mm->rate = value_get_as_float (value);
	} else
		mm->sum += value_get_as_float (value) /
		        pow (1 + mm->rate, mm->num);
	mm->num++;
        return NULL;
}

static Value *
gnumeric_npv (FunctionEvalInfo *ei, GList *nodes)
{
	Value *v;
        financial_npv_t p;

	p.sum   = 0.0;
	p.num   = 0;

	v = function_iterate_argument_values (&ei->pos, callback_function_npv,
					      &p, nodes, TRUE);

	return (v != NULL) ? v : value_new_float (p.sum);
}

static char *help_xnpv = {
	N_("@FUNCTION=XNPV\n"
	   "@SYNTAX=XNPV(rate,values,dates)\n"
	   "@DESCRIPTION="
	   "XNPV calculates the net present value of an investment.  The "
	   "schedule of the cash flows is given in @dates array.  The first "
	   "date indicates the beginning of the payment schedule.  @rate "
	   "is the interest rate and @values are the payments. "
	   "\n"
	   "If @values and @dates contain unequal number of values, XNPV "
	   "returns the #NUM error. "
	   "\n"
	   "@SEEALSO=NPV,PV")
};

static Value *
gnumeric_xnpv (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, *payments = NULL, *dates = NULL;
	float_t sum;
	int     p_n, d_n, i;
	Value   *result = NULL;

	rate = value_get_as_float (argv[0]);
	sum = 0;

	payments = collect_floats_value (argv[1], &ei->pos,
					 COLLECT_IGNORE_STRINGS |
					 COLLECT_IGNORE_BOOLS,
					 &p_n, &result);
	if (result)
		goto out;

	dates = collect_dates_value (argv[2], &ei->pos, 0,
				     &d_n, &result);
	if (result)
		goto out;

	if (p_n != d_n)
		return value_new_error (&ei->pos, gnumeric_err_NUM);

	for (i=0; i<p_n; i++)
	        sum += payments[i] / pow(1+rate, (dates[i]-dates[0])/365.0);

	result = value_new_float (sum);
 out:
	g_free (payments);
	g_free (dates);

	return result;
}


static char *help_fv = {
	N_("@FUNCTION=FV\n"
	   "@SYNTAX=FV(rate,nper,pmt,pv,type)\n"
	   "@DESCRIPTION=Calculates the future value of an investment."
	   "\n"
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
	   "\n"
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
	   "PMT = Payment received on annuity\n"
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
	   "PMT = Payment received on annuity\n"
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
	   "\n"
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
		return value_new_error (&ei->pos, gnumeric_err_DIV0);

	tmp = (pmt * (1.0 + rate * type) - fv * rate) / (pv * rate + pmt * (1.0 + rate * type));
	if (tmp <= 0.0)
		return value_new_error (&ei->pos, gnumeric_err_VALUE);

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
		return value_new_error (&ei->pos, gnumeric_err_DIV0);
	else if (fv == 0 || pv == 0)
		return value_new_error (&ei->pos, gnumeric_err_DIV0);
	else if (fv / pv < 0)
		return value_new_error (&ei->pos, gnumeric_err_VALUE);

        return value_new_float (log (fv / pv) / log (1.0 + rate));

}

void finance_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Financial"));

	function_add_args  (cat, "db", "ffff|f",
			    "cost,salvage,life,period[,month]",
			    &help_db, gnumeric_db);
	function_add_args  (cat, "ddb", "ffff|f",
			    "cost,salvage,life,period[,factor]",
			    &help_ddb, gnumeric_ddb);
	function_add_args  (cat, "dollarde", "ff", 
			    "fractional_dollar,fraction",
			    &help_dollarde, gnumeric_dollarde);
	function_add_args  (cat, "dollarfr", "ff",
			    "decimal_dollar,fraction",
			    &help_dollarfr, gnumeric_dollarfr);
	function_add_args  (cat, "duration", "fff", "rate,pv,fv",
			    &help_duration, gnumeric_duration);
	function_add_args  (cat, "effect",   "ff",    "rate,nper",
			    &help_effect,   gnumeric_effect);
	function_add_args  (cat, "fv", "fffff", "rate,nper,pmt,pv,type",
			    &help_fv,       gnumeric_fv);
	function_add_args  (cat, "ipmt", "ffffff", "rate,per,nper,pv,fv,type",
			    &help_ipmt,     gnumeric_ipmt);
	function_add_args  (cat, "nominal", "ff",    "rate,nper",
			    &help_nominal,  gnumeric_nominal);
	function_add_args  (cat, "nper", "fffff", "rate,pmt,pv,fv,type",
			    &help_nper,     gnumeric_nper);
        function_add_nodes (cat, "npv",     0,      "",
			    &help_npv,      gnumeric_npv);
	function_add_args  (cat, "pmt", "fffff", "rate,nper,pv,fv,type",
			    &help_pmt,      gnumeric_pmt);
	function_add_args  (cat, "ppmt", "ffffff", "rate,per,nper,pv,fv,type",
			    &help_ppmt,     gnumeric_ppmt);
	function_add_args  (cat, "pv", "fffff", "rate,nper,pmt,fv,type",
			    &help_pv,       gnumeric_pv);
	function_add_args  (cat, "rate", "fff|fff",
			    "rate,nper,pmt,fv,type,guess",
			    &help_rate,     gnumeric_rate);
	function_add_args  (cat, "sln", "fff", "cost,salvagevalue,life",
			    &help_sln,      gnumeric_sln);
	function_add_args  (cat, "syd", "ffff",
			    "cost,salvagevalue,life,period",
			    &help_syd,      gnumeric_syd);
        function_add_args  (cat, "xnpv", "fAA", "rate,values,dates",
			    &help_xnpv,     gnumeric_xnpv);
}
