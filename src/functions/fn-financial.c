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



	 PV * PVIF(k%, nper) + PMT * ( 1 + rate * type ) *
	      FVIFA(k%, nper) + FV = 0

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
calculate_principal (float_t starting_principal, float_t payment,
		     float_t rate, float_t period)
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

static float_t
calculate_npv (float_t rate, float_t *values, int n)
{
	float_t sum;
        int     i;

	sum = 0;
	for (i=0; i<n; i++)
	        sum += values[i] / pow(1 + rate, i);

	return sum;
}

static int
annual_year_basis(Value *value_date, int basis)
{
        GDate    *date;
        gboolean leap_year;

	switch (basis) {
	case 0:
	        return 360;
	case 1:
	        date = get_date (value_date);
		if (date != NULL) {
		        leap_year = g_date_is_leap_year (g_date_year (date));
			g_date_free (date);
		} else
		        return -1;
	        return leap_year ? 366 : 365;
	case 2:
	        return 360;
	case 3:
	        return 365;
	case 4:
	        return 360;
	default:
	        return -1;
	}
}

static int
days_monthly_basis(Value *issue_date, Value *maturity_date, int basis)
{
        GDate    *date_i, *date_m;
	int      issue_day, issue_month, issue_year;
	int      maturity_day, maturity_month, maturity_year;
        int      months, days, years;
	gboolean leap_year;
	int      maturity, issue;

	date_i = get_date (issue_date);
	date_m = get_date (maturity_date);
	if (date_i != NULL && date_m != NULL) {
	        issue_year = g_date_year (date_i);
	        issue_month = g_date_month (date_i);
	        issue_day = g_date_day (date_i);
	        maturity_year = g_date_year (date_m);
	        maturity_month = g_date_month (date_m);
	        maturity_day = g_date_day (date_m);

	        years = maturity_year - issue_year;
	        months = maturity_month - issue_month;
	        days = maturity_day - issue_day;

		months = years * 12 + months;
		leap_year = g_date_is_leap_year (issue_year);

		g_date_free (date_i);
		g_date_free (date_m);
	} else {
	        g_date_free (date_i);
	        g_date_free (date_m);
	        return -1;
	}

	switch (basis) {
	case 0:
	        if (issue_month == 2 && maturity_month != 2 &&
		    issue_year == maturity_year){
			if (leap_year)
				return months * 30 + days - 1;
			else
				return months * 30 + days - 2;
		}
	        return months * 30 + days;
	case 1:
	case 2:
	case 3:
	        issue = get_serial_date(issue_date);
	        maturity = get_serial_date(maturity_date);
	        return maturity - issue;
	case 4:
	        return months * 30 + days;
	default:
	        return -1;
	}
}

/***************************************************************************/

static char *help_accrint = {
	N_("@FUNCTION=ACCRINT\n"
	   "@SYNTAX=ACCRINT(issue,first_interest,settlement,rate,par,"
	   "frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ACCRINT calculates and returns the accrued interest for a "
	   "security paying periodic interest.  @rate is the annual "
	   "rate of the security and @par is the par value of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @basis is omitted, US 30/360 is applied. "
	   "If issue date or settlement date is not valid, ACCRINT returns "
	   "NUM! error."
	   "If @rate or @par is zero or negative, ACCRINT returns NUM! error."
	   "If @basis < 0 or @basis > 4, ACCRINT returns NUM! error. "
	   "If issue date is after maturity date or they are the same, "
	   "ACCRINT returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ACCRINTM")
};

static Value *
gnumeric_accrint (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, a, d, par, freq, coefficient, x;
	int     basis;

	rate = value_get_as_float (argv[3]);
	par = value_get_as_float (argv[4]);
	freq = value_get_as_float (argv[5]);
	if (argv[6] == NULL)
	        basis = 0;
	else
	        basis = value_get_as_int (argv[6]);

	a = days_monthly_basis (argv[0], argv[2], basis);
	d = annual_year_basis (argv[0], basis);

	if (a < 0 || d < 0 || par <= 0 || rate <= 0 || basis < 0 || basis > 4)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	coefficient = par * rate / freq;
	x = a / d;

	return value_new_float (coefficient * freq * x);
}

/***************************************************************************/

static char *help_accrintm = {
	N_("@FUNCTION=ACCRINTM\n"
	   "@SYNTAX=ACCRINTM(issue,maturity,rate[,par,basis])\n"
	   "@DESCRIPTION="
	   "ACCRINTM calculates and returns the accrued interest for a "
	   "security from @issue to @maturity date.  @rate is the annual "
	   "rate of the security and @par is the par value of the security. "
	   "If you omit @par, ACCRINTM applies $1,000 instead.  @basis is "
	   "the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @basis is omitted, US 30/360 is applied. "
	   "If issue date or maturity date is not valid, ACCRINTM returns "
	   "NUM! error."
	   "If @rate or @par is zero or negative, ACCRINTM returns NUM! error."
	   "If @basis < 0 or @basis > 4, ACCRINTM returns NUM! error. "
	   "If issue date is after maturity date or they are the same, "
	   "ACCRINTM returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ACCRINT")
};

static Value *
gnumeric_accrintm (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, a, d, par;
	int     basis;

	rate = value_get_as_float (argv[2]);
	if (argv[3] == NULL)
	        par = 1000;
	else
	        par = value_get_as_float (argv[3]);
	if (argv[4] == NULL)
	        basis = 0;
	else
	        basis = value_get_as_int (argv[4]);

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (a < 0 || d < 0 || par <= 0 || rate <= 0 || basis < 0 || basis > 4)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	return value_new_float (par * rate * a/d);
}

/***************************************************************************/

static char *help_intrate = {
	N_("@FUNCTION=INTRATE\n"
	   "@SYNTAX=INTRATE(settlement,maturity,investment,redemption"
	   "[,basis])\n"
	   "@DESCRIPTION="
	   "INTRATE calculates and returns the interest rate of a security. "
	   "@investment is the prize of the security paid at @settlement "
	   "date and @redemption is the amount to be received at @maturity "
	   "date.  @basis is the type of day counting system you want to "
	   "use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @basis is omitted, US 30/360 is applied. "
	   "If settlement date or maturity date is not valid, INTRATE returns "
	   "NUM! error."
	   "If @basis < 0 or @basis > 4, INTRATE returns NUM! error. "
	   "If settlement date is after maturity date or they are the same, "
	   "INTRATE returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=RECEIVED")
};

static Value *
gnumeric_intrate (FunctionEvalInfo *ei, Value **argv)
{
	float_t investment, redemption, a, d;
	int     basis;

	investment = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	if (argv[4] == NULL)
	        basis = 0;
	else
	        basis = value_get_as_int (argv[4]);

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (basis < 0 || basis > 4 || a <= 0 || d <= 0)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	return value_new_float ((redemption - investment) / investment *
				(d / a));
}

/***************************************************************************/

static char *help_received = {
	N_("@FUNCTION=RECEIVED\n"
	   "@SYNTAX=RECEIVED(settlement,maturity,investment,rate[,basis]"
	   "@DESCRIPTION="
	   "RECEIVED calculates and returns the amount to be received at "
	   "@maturity date for a security bond. "
	   "@basis is the type of day counting system you want to "
	   "use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @basis is omitted, US 30/360 is applied. "
	   "If settlement date or maturity date is not valid, RECEIVED "
	   "returns NUM! error."
	   "If @basis < 0 or @basis > 4, RECEIVED returns NUM! error. "
	   "If settlement date is after maturity date or they are the same, "
	   "RECEIVED returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=INTRATE")
};

static Value *
gnumeric_received (FunctionEvalInfo *ei, Value **argv)
{
	float_t investment, discount, a, d;
	int     basis;

	investment = value_get_as_float (argv[2]);
	discount = value_get_as_float (argv[3]);
	if (argv[4] == NULL)
	        basis = 0;
	else
	        basis = value_get_as_int (argv[4]);

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (a <= 0 || d <= 0 || basis < 0 || basis > 4)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	return value_new_float (investment / (1.0 - (discount * a/d)));
}

/***************************************************************************/

static char *help_pricedisc = {
	N_("@FUNCTION=PRICEDISC\n"
	   "@SYNTAX=PRICEDISC(settlement,maturity,discount,redemption[,basis]"
	   "@DESCRIPTION="
	   "PRICEDISC calculates and returns the price per $100 face value "
	   "of a security bond.  The security does not pay interest at "
	   "maturity.  @discount is the rate for which the security "
	   "is discounted.  @redemption is the amount to be received on "
	   "@maturity date.  @basis is the type of day counting system you "
	   "want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @basis is omitted, US 30/360 is applied. "
	   "If settlement date or maturity date is not valid, PRICEDISC "
	   "returns NUM! error."
	   "If @basis < 0 or @basis > 4, PRICEDISC returns NUM! error. "
	   "If settlement date is after maturity date or they are the same, "
	   "PRICEDISC returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PRICEMAT")
};

static Value *
gnumeric_pricedisc (FunctionEvalInfo *ei, Value **argv)
{
	float_t discount, redemption, a, d;
	int     basis;

	discount = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	if (argv[4] == NULL)
	        basis = 0;
	else
	        basis = value_get_as_int (argv[4]);

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (a <= 0 || d <= 0 || basis < 0 || basis > 4)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	return value_new_float (redemption - discount * redemption * a/d);
}

/***************************************************************************/

static char *help_pricemat = {
	N_("@FUNCTION=PRICEMAT\n"
	   "@SYNTAX=PRICEMAT(settlement,maturity,issue,rate,yield[,basis]"
	   "@DESCRIPTION="
	   "PRICEMAT calculates and returns the price per $100 face value "
	   "of a security.  The security pays interest at maturity. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @basis is omitted, US 30/360 is applied. "
	   "If settlement date or maturity date is not valid, PRICEMAT "
	   "returns NUM! error."
	   "If @basis < 0 or @basis > 4, PRICEMAT returns NUM! error. "
	   "If settlement date is after maturity date or they are the same, "
	   "PRICEMAT returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PRICEDISC")
};

static Value *
gnumeric_pricemat (FunctionEvalInfo *ei, Value **argv)
{
	float_t discount, yield, a, b, dsm, dim;
	int     basis;

	discount = value_get_as_float (argv[3]);
	yield = value_get_as_float (argv[4]);
	if (argv[5] == NULL)
	        basis = 0;
	else
	        basis = value_get_as_int (argv[5]);

	dsm = days_monthly_basis (argv[0], argv[1], basis);
	dim = days_monthly_basis (argv[2], argv[1], basis);
	a = days_monthly_basis (argv[2], argv[0], basis);
	b = annual_year_basis (argv[0], basis);

	if (a <= 0 || b <= 0 || dsm <= 0 || dim <= 0 || basis < 0 || 
	    basis > 4)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	return value_new_float (((100 + (dim/b * discount * 100)) /
				 (1 + (dsm/b * yield))) -
				(a/b * discount * 100));
}

/***************************************************************************/

static char *help_disc = {
	N_("@FUNCTION=DISC\n"
	   "@SYNTAX=DISC(settlement,maturity,par,redemption[,basis]"
	   "@DESCRIPTION="
	   "DISC calculates and returns the discount rate for a sequrity. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @basis is omitted, US 30/360 is applied. "
	   "If settlement date or maturity date is not valid, DISC "
	   "returns NUM! error."
	   "If @basis < 0 or @basis > 4, DISC returns NUM! error. "
	   "If settlement date is after maturity date or they are the same, "
	   "DISC returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_disc (FunctionEvalInfo *ei, Value **argv)
{
	float_t par, redemption, dsm, b;
	int     basis;

	par = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	if (argv[4] == NULL)
	        basis = 0;
	else
	        basis = value_get_as_int (argv[4]);

	b = annual_year_basis (argv[0], basis);
	dsm = days_monthly_basis (argv[0], argv[1], basis);

	if (dsm <= 0 || b <= 0 || dsm <= 0 || basis < 0 || basis > 4)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	return value_new_float ((redemption - par) / redemption * (b / dsm));
}

/***************************************************************************/

static char *help_effect = {
	N_("@FUNCTION=EFFECT\n"
	   "@SYNTAX=EFFECT(r,nper)\n"
	   "@DESCRIPTION="
	   "EFFECT calculates the effective interest rate from "
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
	   "For example credit cards will list an APR (annual percentage "
	   "rate) which is a nominal interest rate."
	   "\n"
	   "For example if you wanted to find out how much you are actually "
	   "paying interest on your credit card that states an APR of 19% "
	   "that is compounded monthly you would type in:"
	   "\n"
	   "=EFFECT(.19,12) and you would get .2075 or 20.75%. That is the "
	   "effective percentage you will pay on your loan."
	   "\n"
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_nominal = {
	N_("@FUNCTION=NOMINAL\n"
	   "@SYNTAX=NOMINAL(r,nper)\n"
	   "@DESCRIPTION="
	   "NOMINAL calculates the nominal interest rate from "
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
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_ispmt = {
	N_("@FUNCTION=ISPMT\n"
	   "@SYNTAX=ISPMT(rate,per,nper,pv)\n"
	   "@DESCRIPTION="
	   "ISPMT function returns the interest payed on a given period. "
	   "\n"
	   "If @per < 1 or @per > @nper, ISPMT returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PV")
};

static Value *
gnumeric_ispmt (FunctionEvalInfo *ei, Value **argv)
{
	float_t rate, pv, tmp;
	int     nper, per;

	rate = value_get_as_float (argv[0]);
	per = value_get_as_int (argv[1]);
	nper = value_get_as_int (argv[2]);
	pv = value_get_as_float (argv[3]);

	if (per < 1 || per > nper)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	tmp = - pv * rate;

	return value_new_float (tmp - (tmp / nper * per));
}

/***************************************************************************/

static char *help_db = {
	N_("@FUNCTION=DB\n"
	   "@SYNTAX=DB(cost,salvage,life,period[,month])\n"
	   "@DESCRIPTION="
	   "DB calculates the depreciation of an asset for a given period "
	   "using the fixed-declining balance method.  @cost is the "
	   "initial value of the asset.  @salvage after the depreciation. "
	   "@life is the number of periods overall.  @period is the period "
	   "for which you want the depreciation to be calculated.  @month "
	   "is the number of months in the first year of depreciation. "
	   "If @month is omitted, it is assumed to be 12. "
	   "\n"
	   "@EXAMPLES=\n"
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

/***************************************************************************/

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
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_sln = {
	N_("@FUNCTION=SLN\n"
	   "@SYNTAX=SLN(cost,salvage_value,life)\n"

	   "@DESCRIPTION="
	   "SLN calculates the straight line depriciation for an "
	   "asset based on its cost, salvage value and anticipated life."
	   "\n"
	   "Formula for straight line depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage_value ) / life"
	   "\n"
	   "\t@cost = cost of an asset when acquired (market value)"
	   "\t@salvage_value = amount you get when asset sold at the end "
	   "of life"
	   "\t@life = anticipated life of an asset"
	   "\n"
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_syd = {
	N_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage_value,life,period)\n"

	   "@DESCRIPTION="
	   "SYD calculates the sum-of-years digits depriciation for an "
	   "asset based on its cost, salvage value, anticipated life and a "
	   "particular period."
	   "\n"
	   "Formula for sum-of-years digits depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage_value ) * (life - period "
	   "+ 1) * 2 / life * (life + 1)."
	   "\n"
	   "\t@cost = cost of an asset when acquired (market value)"
	   "\t@salvage_value = amount you get when asset sold at the end of "
	   "life"
	   "\t@life = anticipated life of an asset"
	   "\t@period = period for which we need the expense"
	   "\n"
	   "@EXAMPLES=\n"
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

        return value_new_float (((cost - salvage_value) *
				 (life - period + 1) * 2) /
				(life * (life + 1.0)));
}

/***************************************************************************/

static char *help_dollarde = {
	N_("@FUNCTION=DOLLARDE\n"
	   "@SYNTAX=DOLLARDE(fractional_dollar,fraction)\n"
	   "@DESCRIPTION="
	   "DOLLARDE converts a dollar price expressed as a "
	   "fraction into a dollar price expressed as a decimal number. "
	   "\n"
	   "If @fraction is non-integer it is truncated. "
	   "If @fraction<=0 DOLLARDE returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_dollarfr = {
	N_("@FUNCTION=DOLLARFR\n"
	   "@SYNTAX=DOLLARFR(decimal_dollar,fraction)\n"
	   "@DESCRIPTION="
	   "DOLLARFR converts a decimal dollar price into "
	   "a dollar price expressed as a fraction. "
	   "\n"
	   "If @fraction is non-integer it is truncated. "
	   "If @fraction <= 0 DOLLARFR returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_mirr = {
	N_("@FUNCTION=MIRR\n"
	   "@SYNTAX=MIRR(values,finance_rate,reinvest_rate)\n"
	   "@DESCRIPTION="
	   "MIRR function returns the modified internal rate of return "
	   "for a given periodic cash flow. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=NPV")
};

static Value *
gnumeric_mirr (FunctionEvalInfo *ei, Value **argv)
{
	float_t frate, rrate, npv_neg, npv_pos;
	float_t *pos_values = NULL, *neg_values = NULL, res;
	Value   *result = NULL;
	int     n, n_pos, n_neg;

	frate = value_get_as_float (argv[1]);
	rrate = value_get_as_float (argv[2]);

	pos_values = collect_floats_value (argv[0], &ei->pos,
					   COLLECT_IGNORE_NEGATIVE,
					   &n_pos, &result);
	if (result)
		goto out;

	neg_values = collect_floats_value (argv[0], &ei->pos,
					   COLLECT_IGNORE_POSITIVE,
					   &n_neg, &result);
	if (result)
		goto out;

	n = n_pos + n_neg;

	npv_pos = calculate_npv(rrate, pos_values, n_pos);
	npv_neg = calculate_npv(frate, neg_values, n_neg);
	res = pow((-npv_pos * pow(1+rrate, n_pos)) / (npv_neg * (1+frate)), 
		  (1.0 / (n-1))) - 1.0;

	result = value_new_float (res);
out:
	g_free(pos_values);
	g_free(neg_values);

	return result;
}

/***************************************************************************/

static char *help_tbilleq = {
	N_("@FUNCTION=TBILLEQ\n"
	   "@SYNTAX=TBILLEQ(settlement,maturity,discount)\n"
	   "@DESCRIPTION="
	   "TBILLEQ function returns the bond-yield equivalent (BEY) for "
	   "a treasury bill.  TBILLEQ is equivalent to (365 * discount) / "
	   "(360 - discount * DSM) where DSM is the days between @settlement "
	   "and @maturity. "
	   "\n"
	   "If @settlement is after @maturity or the @maturity is set to "
	   "over one year later than the @settlement, TBILLEQ returns "
	   "NUM! error. "
	   "If @discount is negative, TBILLEQ returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLPRICE,TBILLYIELD")
};

static Value *
gnumeric_tbilleq (FunctionEvalInfo *ei, Value **argv)
{
	float_t settlement, maturity, discount;
	float_t dsm, divisor;

	settlement = get_serial_date (argv[0]);
	maturity = get_serial_date (argv[1]);
	discount = value_get_as_float (argv[2]);
	
	dsm = maturity - settlement;

	if (settlement > maturity || discount < 0 || dsm > 356)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	divisor = 360 - discount * dsm;
	/* This test probably isn't right, but it is better that not checking
	   at all.  --MW.  */
	if (divisor == 0)
		return value_new_error (&ei->pos, gnumeric_err_DIV0);

	return value_new_float ((365 * discount) / divisor);
}

/***************************************************************************/

static char *help_tbillprice = {
	N_("@FUNCTION=TBILLPRICE\n"
	   "@SYNTAX=TBILLPRICE(settlement,maturity,discount)\n"
	   "@DESCRIPTION="
	   "TBILLPRICE function returns the price per $100 value for a "
	   "treasury bill where @settlement is the settlement date and "
	   "@maturity is the maturity date of the bill.  @discount is the "
	   "treasury bill's discount rate. "
	   "\n"
	   "If @settlement is after @maturity or the @maturity is set to "
	   "over one year later than the @settlement, TBILLPRICE returns "
	   "NUM! error. "
	   "If @discount is negative, TBILLPRICE returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLEQ,TBILLYIELD")
};

static Value *
gnumeric_tbillprice (FunctionEvalInfo *ei, Value **argv)
{
	float_t settlement, maturity, discount;
	float_t res, dsm;

	settlement = get_serial_date (argv[0]);
	maturity = get_serial_date (argv[1]);
	discount = value_get_as_float (argv[2]);
	
	dsm = maturity - settlement;

	if (settlement > maturity || discount < 0 || dsm > 356)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	res = 100 * (1.0 - (discount * dsm) / 360.0);

	return value_new_float (res);
}

/***************************************************************************/

static char *help_tbillyield = {
	N_("@FUNCTION=TBILLYIELD\n"
	   "@SYNTAX=TBILLYIELD(settlement,maturity,pr)\n"
	   "@DESCRIPTION="
	   "TBILLYIELD function returns the yield for a treasury bill. "
	   "@settlement is the settlement date and @maturity is the "
	   "maturity date of the bill.  @discount is the treasury bill's "
	   "discount rate. "
	   "\n"
	   "If @settlement is after @maturity or the @maturity is set to "
	   "over one year later than the @settlement, TBILLYIELD returns "
	   "NUM! error. "
	   "If @pr is negative, TBILLYIELD returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLEQ,TBILLPRICE")
};

static Value *
gnumeric_tbillyield (FunctionEvalInfo *ei, Value **argv)
{
	float_t settlement, maturity, pr;
	float_t res, dsm;

	settlement = get_serial_date (argv[0]);
	maturity = get_serial_date (argv[1]);
	pr = value_get_as_float (argv[2]);
	
	dsm = maturity - settlement;

	if (pr <= 0 || dsm <= 0 || dsm > 356)
                return value_new_error (&ei->pos, gnumeric_err_NUM);

	res = (100.0 - pr) / pr * (360.0 / dsm);

	return value_new_float (res);
}

/***************************************************************************/

static char *help_rate = {
	N_("@FUNCTION=RATE\n"
	   "@SYNTAX=RATE(nper,pmt,pv[,fv,type,guess])\n"
	   "@DESCRIPTION="
	   "RATE calculates rate of an investment."
	   "\n"
	   "@EXAMPLES=\n"
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
			data->pmt * (1 + rate * data->type) *
		  calculate_fvifa (rate, data->nper) +
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

/***************************************************************************/

static char *help_irr = {
	N_("@FUNCTION=IRR\n"
	   "@SYNTAX=IRR(values[,guess])\n"
	   "@DESCRIPTION="
	   "IRR calculates and returns the internal rate of return of an "
	   "investment.  This function is closely related to the net present "
	   "value function (NPV).  The IRR is the interest rate for a "
	   "serie of cash flow where the net preset value is zero. "
	   "\n"
	   "@values contains the serie of cash flow generated by the "
	   "investment.  The payments should occur at regular intervals.  "
	   "The optional @guess is the initial value used in calculating "
	   "the IRR.  You do not have to use that, it is only provided "
	   "for the Excel compatibility. "
	   "\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1:A8 contain the numbers -32432, "
	   "5324, 7432, 9332, 12324, 4334, 1235, -3422.  Now\n"
	   "IRR(A1:A8) returns 0.04375. "
	   "\n"
	   "@SEEALSO=FV,NPV,PV")
};

typedef struct {
        int     n;
        float_t *values;
} gnumeric_irr_t;

static GoalSeekStatus
irr_npv (float_t rate, float_t *y, void *user_data)
{
	gnumeric_irr_t *p = user_data;
	float_t        *values, sum;
        int            i, n;

	values = p->values;
	n = p->n;

	sum = 0;
	for (i=0; i<n; i++)
	        sum += values[i] / pow(1 + rate, i);

	*y = sum;
	return GOAL_SEEK_OK;
}

static Value *
gnumeric_irr (FunctionEvalInfo *ei, Value **argv)
{
	GoalSeekData    data;
	GoalSeekStatus  status;
	Value           *result = NULL;
	gnumeric_irr_t  p;
	float_t         rate0;
	int             n;

	goal_seek_initialise (&data);
	rate0 = 0.1; /* Ignore the guess value */

	p.values = collect_floats_value (argv[0], &ei->pos,
					 COLLECT_IGNORE_STRINGS,
					 &n, &result);
	if (result != NULL) {
		g_free (p.values);
	        return result;
	}

	p.n = n;
	status = goal_seek_newton (&irr_npv, NULL, &data, &p, rate0);
	g_free (p.values);

	if (status == GOAL_SEEK_OK)
		return value_new_float (data.root);
	else
		return value_new_error (&ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_pv = {
	N_("@FUNCTION=PV\n"
	   "@SYNTAX=PV(rate,nper,pmt,fv,type)\n"
	   "@DESCRIPTION="
	   "PV calculates the present value of an investment."
	   "\n"
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_npv = {
	N_("@FUNCTION=NPV\n"
	   "@SYNTAX=NPV(rate,v1,v2,...)\n"
	   "@DESCRIPTION="
	   "NPV calculates the net present value of an investment generating "
	   "peridic payments.  @rate is the periodic interest rate and "
	   "@v1, @v2, ... are the periodic payments.  If the schedule of the "
	   "cash flows are not periodic use the XNPV function. "
	   "\n"
	   "@EXAMPLES=\n"
	   "NPV(0.17,-10000,3340,2941,2493,3233,1732,2932) equals 186.30673.\n"
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

/***************************************************************************/

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
	   "@EXAMPLES=\n"
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

	dates = collect_floats_value (argv[2], &ei->pos,
				      COLLECT_DATES,
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

/***************************************************************************/

static char *help_xirr = {
	N_("@FUNCTION=XIRR\n"
	   "@SYNTAX=XIRR(values,dates[,guess])\n"
	   "@DESCRIPTION="
	   "XIRR calculates and returns the internal rate of return of an "
	   "investment that has not necessarily periodic payments.  This "
	   "function is closely related to the net present value function "
	   "(NPV and XNPV).  The XIRR is the interest rate for a "
	   "serie of cash flow where the XNPV is zero. "
	   "\n"
	   "@values contains the serie of cash flow generated by the "
	   "investment.  @dates contains the dates of the payments.  The "
	   "first date describes the payment day of the initial payment and "
	   "thus all the other dates should be after this date. "
	   "The optional @guess is the initial value used in calculating "
	   "the XIRR.  You do not have to use that, it is only provided "
	   "for the Excel compatibility. "
	   "\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1:A5 contain the numbers -6000, "
	   "2134, 1422, 1933, and 1422, and the cells B1:B5 contain the "
	   "dates \"1999-01-15\", \"1999-04-04\", \"1999-05-09\", "
	   "\"2000-03-12\", and \"2000-05-1\". Then "
	   "XIRR(A1:A5,B1:B5) returns 0.224838. "
	   "\n"
	   "@SEEALSO=IRR,XNPV")
};

typedef struct {
        int     n;
        float_t *values;
        float_t *dates;
} gnumeric_xirr_t;

static GoalSeekStatus
xirr_npv (float_t rate, float_t *y, void *user_data)
{
	gnumeric_xirr_t *p = user_data;
	float_t         *values, *dates, sum;
        int             i, n;

	values = p->values;
	dates = p->dates;
	n = p->n;

	sum = 0;
	for (i=0; i<n; i++) {
	        float_t d = dates[i] - dates[0];

		if (d < 0)
		        return GOAL_SEEK_ERROR;
	        sum += values[i] / pow(1+rate, d/365.0);
	}

	*y = sum;
	return GOAL_SEEK_OK;
}

static Value *
gnumeric_xirr (FunctionEvalInfo *ei, Value **argv)
{
	GoalSeekData    data;
	GoalSeekStatus  status;
	Value           *result = NULL;
	gnumeric_xirr_t p;
	float_t         rate0;
	int             n, d_n;

	goal_seek_initialise (&data);
	rate0 = 0.1; /* Ignore the guess value */

	p.values = collect_floats_value (argv[0], &ei->pos,
					 COLLECT_IGNORE_STRINGS,
					 &n, &result);
	p.dates = NULL;

	if (result != NULL)
		goto out;

	p.dates = collect_floats_value (argv[1], &ei->pos,
					COLLECT_DATES,
					&d_n, &result);
	if (result != NULL)
		goto out;

	p.n = n;
	status = goal_seek_newton (&xirr_npv, NULL, &data, &p, rate0);

	if (status == GOAL_SEEK_OK)
		result = value_new_float (data.root);
	else
		result = value_new_error (&ei->pos, gnumeric_err_NUM);

 out:
	g_free (p.values);
	g_free (p.dates);

	return result;
}

/***************************************************************************/

static char *help_fv = {
	N_("@FUNCTION=FV\n"
	   "@SYNTAX=FV(rate,nper,pmt,pv,type)\n"
	   "@DESCRIPTION="
	   "FV calculates the future value of an investment."
	   "\n"
	   "@EXAMPLES=\n"
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

        return value_new_float (-1.0 * ((pv * pvif) + pmt *
					(1.0 + rate * type) * fvifa));
}

/***************************************************************************/

static char *help_pmt = {
	N_("@FUNCTION=PMT\n"
	   "@SYNTAX=PMT(rate,nper,pv[,fv,type])\n"
	   "@DESCRIPTION="
	   "PMT calculates the present value of an investment."
	   "\n"
	   "@EXAMPLES=\n"
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
	if (argv[3] == NULL)
	        fv = 0;
	else
	        fv   = value_get_as_float (argv [3]);
	if (argv[4] == NULL)
	        type = 0;
	else
	        type = value_get_as_int (argv [4]);

        return value_new_float (calculate_pmt (rate, nper, pv, fv, type));
}

/***************************************************************************/

static char *help_ipmt = {
	N_("@FUNCTION=IPMT\n"
	   "@SYNTAX=IPMT(rate,per,nper,pv,fv,type)\n"
	   "@DESCRIPTION="
	   "IPMT calculates the amount of a payment of an annuity going "
	   "towards interest."
	   "\n"
	   "Formula for IPMT is:\n"
	   "\n"
	   "IPMT(PER) = PMT - PRINCIPAL(PER-1) * INTEREST_RATE"
	   "\n"
	   "where:"
	   "\n"
	   "PMT = Payment received on annuity\n"
	   "PRINCIPA(per-1) = amount of the remaining principal from last "
	   "period"
	   "\n"
	   "@EXAMPLES=\n"
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
	if (argv[4] == NULL)
	        fv = 0;
	else
	        fv   = value_get_as_float (argv [4]);
	if (argv[5] == NULL)
	        type = 0;
	else
	        type = value_get_as_int (argv [5]);

	/* First calculate the payment */
        pmt = calculate_pmt (rate, nper, pv, fv, type);

	/* Now we need to calculate the amount of money going towards the
	   principal */

	return value_new_float (-calculate_principal (pv, pmt, rate, per-1) *
				rate);
}

/***************************************************************************/

static char *help_ppmt = {
	N_("@FUNCTION=PPMT\n"
	   "@SYNTAX=PPMT(rate,per,nper,pv[,fv,type])\n"
	   "@DESCRIPTION="
	   "PPMT calculates the amount of a payment of an annuity going "
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
	   "@EXAMPLES=\n"
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
	if (argv[4] == NULL)
	        fv = 0;
	else
	        fv   = value_get_as_float (argv [4]);
	if (argv[5] == NULL)
	        type = 0;
	else
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

/***************************************************************************/

static char *help_nper = {
	N_("@FUNCTION=NPER\n"
	   "@SYNTAX=NPER(rate,pmt,pv,fv,type)\n"
	   "@DESCRIPTION="
	   "NPER calculates number of periods of an investment."
	   "\n"
	   "@EXAMPLES=\n"
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

	tmp = (pmt * (1.0 + rate * type) - fv * rate) /
	  (pv * rate + pmt * (1.0 + rate * type));
	if (tmp <= 0.0)
		return value_new_error (&ei->pos, gnumeric_err_VALUE);

        return value_new_float (log (tmp) / log (1.0 + rate));
}

/***************************************************************************/

static char *help_duration = {
	N_("@FUNCTION=DURATION\n"
	   "@SYNTAX=DURATION(rate,pv,fv)\n"
	   "@DESCRIPTION="
	   "DURATION calculates number of periods needed for an investment to "
	   "attain a desired value. This function is similar to FV and PV "
	   "with a difference that we do not need give the direction of "
	   "cash flows e.g. -100 for a cash outflow and +100 for a cash "
	   "inflow."
	   "\n"
	   "@EXAMPLES=\n"
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

/***************************************************************************/

static char *help_fvschedule = {
	N_("@FUNCTION=FVSCHEDULE\n"
	   "@SYNTAX=FVSCHEDULE(pv,schedule)\n"
	   "@DESCRIPTION="
	   "FVSCHEDULE returns the future value of given initial value @pv "
	   "after applying a series of compound periodic interest rates. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain interest "
	   "rates 0.11, 0.13, 0.09, 0.17, and 0.03.  Then\n"
	   "FVSCHEDULE(3000,A1:A5) equals 4942.7911611."
	   "\n"
	   "@SEEALSO=PV,FV")
};

static Value *
gnumeric_fvschedule (FunctionEvalInfo *ei, Value **argv)
{
	float_t pv, *schedule = NULL;
	Value   *result = NULL;
	int     i, n;

	pv = value_get_as_float (argv [0]);
	schedule = collect_floats_value (argv[1], &ei->pos,
					 0, &n, &result);
	if (result)
		goto out;

	for (i=0; i<n; i++)
	        pv *= 1 + schedule[i];

	result = value_new_float (pv);
out:
	g_free (schedule);

        return result;

}

/***************************************************************************/

static char *help_euro = {
	N_("@FUNCTION=EURO\n"
	   "@SYNTAX=EURO(currency)\n"
	   "@DESCRIPTION="
	   "EURO converts one Euro to a given national currency in the "
	   "European monetary union.  Currency is one of the following:\n"
	   "    ATS     (Austria)\n"
	   "    BEF     (Belgium)\n"
	   "    DEM     (Germany)\n"
	   "    ESP     (Spain)\n"
	   "    FIM     (Finland)\n"
	   "    FRF     (France)\n"
	   "    IEP     (Ireland)\n"
	   "    ITL     (Italy)\n"
	   "    LUF     (Luxemburg)\n"
	   "    NLG     (Netherlands)\n"
	   "    PTE     (Portugal)\n"
	   "\n"
	   "If the given currency is other than one of the above, EURO "
	   "returns NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "EURO(\"DEM\") returns 1.95583."
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_euro (FunctionEvalInfo *ei, Value **argv)
{
        char *str = argv [0]->v.str->str;

	switch (*str) {
	case 'A':
	  if (strncmp("ATS", str, 3) == 0)
	    return value_new_float (13.7603);
	  break;
	case 'B':
	  if (strncmp("BEF", str, 3) == 0)
	    return value_new_float (40.3399);
	  break;
	case 'D':
	  if (strncmp("DEM", str, 3) == 0)
	    return value_new_float (1.95583);
	  break;
	case 'E':
	  if (strncmp("ESP", str, 3) == 0)
	    return value_new_float (166.386);
	  break;
	case 'F':
	  if (strncmp("FIM", str, 3) == 0)
	    return value_new_float (5.94573);
	  else if (strncmp("FRF", str, 3) == 0)
	    return value_new_float (6.55957);
	  break;
	case 'I':
	  if (strncmp("IEP", str, 3) == 0)
	    return value_new_float (0.787564);
	  else if (strncmp("ITL", str, 3) == 0)
	    return value_new_float (1936.27);
	  break;
	case 'L':
	  if (strncmp("LUX", str, 3) == 0)
	    return value_new_float (40.3399);
	  break;
	case 'N':
	  if (strncmp("NLG", str, 3) == 0)
	    return value_new_float (2.20371);
	  break;
	case 'P':
	  if (strncmp("PTE", str, 3) == 0)
	    return value_new_float (200.482);
	  break;
	default:
	  break;
	}

	return value_new_error (&ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_price = {
	N_("@FUNCTION=PRICE\n"
	   "@SYNTAX=PRICE(settle,mat,rate,yield,redemption_price,frequency,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

/*
  <jody> Available frequencies : 1 = annual, 2 = semi, 4 = quarterly.
  <jody> Available daycount basis 0 = 30/360 1 = Act/Act 2 = Act/360 3 = Act/365
  4 = 30E/360
*/

static Value *
gnumeric_price (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_yield = {
	N_("@FUNCTION=YIELD\n"
	   "@SYNTAX=YIELD(settle,mat,rate,price,redemption_price,frequency,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_yield (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_yielddisc = {
	N_("@FUNCTION=YIELDDISC\n"
	   "@SYNTAX=YIELDDISC(settlement,maturity,pr,redemption,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_yielddisc (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_yieldmat = {
	N_("@FUNCTION=YIELDMAT\n"
	   "@SYNTAX=YIELDMAT(settlement,maturity,issue,rate,pr,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_yieldmat (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_oddfprice = {
	N_("@FUNCTION=ODDFPRICE\n"
	   "@SYNTAX=ODDFPRICE(settlement,maturity,issue,first_coupon,rate,yld,redemption,frequency,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddfprice (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_oddfyield = {
	N_("@FUNCTION=ODDFYIELD\n"
	   "@SYNTAX=ODDFYIELD(settlement,maturity,issue,first_coupon,rate,pr,redemption,frequency,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddfyield (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_oddlprice = {
	N_("@FUNCTION=ODDLPRICE\n"
	   "@SYNTAX=ODDLPRICE(settlement,maturity,last_interest,rate,yld,redemption,frequency,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddlprice (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_oddlyield = {
	N_("@FUNCTION=ODDLYIELD\n"
	   "@SYNTAX=ODDLYIELD(settlement,maturity,last_interest,rate,pr,redemption,frequency,basis)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddlyield (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (&ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

void
finance_functions_init (void)
{
	FunctionCategory *cat = function_get_category (_("Financial"));

	function_add_args  (cat, "accrint", "???fff|f",
			    "issue,first_interest,settlement,rate,par,"
			    "frequency[,basis]",
			    &help_accrint, gnumeric_accrint);
	function_add_args  (cat, "accrintm", "??f|ff",
			    "issue,maturity,rate[,par,basis]",
			    &help_accrintm, gnumeric_accrintm);
	function_add_args  (cat, "db", "ffff|f",
			    "cost,salvage,life,period[,month]",
			    &help_db, gnumeric_db);
	function_add_args  (cat, "ddb", "ffff|f",
			    "cost,salvage,life,period[,factor]",
			    &help_ddb, gnumeric_ddb);
        function_add_args  (cat, "disc", "??ff|f",
			    "settlement,maturity,pr,redemption[,basis]",
			    &help_disc, gnumeric_disc);
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
	function_add_args  (cat, "euro", "s", "currency",
			    &help_euro,     gnumeric_euro);
	function_add_args  (cat, "fv", "fffff", "rate,nper,pmt,pv,type",
			    &help_fv,       gnumeric_fv);
	function_add_args  (cat, "fvschedule", "fA", "pv,schedule",
			    &help_fvschedule, gnumeric_fvschedule);
	function_add_args  (cat, "intrate", "??ff|f",
			    "settlement,maturity,investment,redemption"
			    "[,basis]",
			    &help_intrate,  gnumeric_intrate);
	function_add_args  (cat, "ipmt", "ffff|ff", "rate,per,nper,pv,fv,type",
			    &help_ipmt,     gnumeric_ipmt);
	function_add_args  (cat, "irr", "A|f",
			    "values[,guess]",
			    &help_irr,      gnumeric_irr);
	function_add_args  (cat, "ispmt", "ffff",    "rate,per,nper,pv",
			    &help_ispmt,  gnumeric_ispmt);
	function_add_args  (cat, "mirr", "Aff",
			    "values,finance_rate,reinvest_rate",
			    &help_mirr,     gnumeric_mirr);
	function_add_args  (cat, "nominal", "ff",    "rate,nper",
			    &help_nominal,  gnumeric_nominal);
	function_add_args  (cat, "nper", "fffff", "rate,pmt,pv,fv,type",
			    &help_nper,     gnumeric_nper);
        function_add_nodes (cat, "npv",     0,      "",
			    &help_npv,      gnumeric_npv);
        function_add_args  (cat, "oddfprice", "????fffff",
			    "settlement,maturity,issue,first_coupon,rate,yld,redemption,frequency,basis",
			    &help_oddfprice,  gnumeric_oddfprice);
        function_add_args  (cat, "oddfyield", "????fffff",
			    "settlement,maturity,issue,first_coupon,rate,pr,redemption,frequency,basis",
			    &help_oddfyield,  gnumeric_oddfyield);
        function_add_args  (cat, "oddlprice", "???fffff",
			    "settlement,maturity,last_interest,rate,yld,redemption,frequency,basis",
			    &help_oddlprice,  gnumeric_oddlprice);
        function_add_args  (cat, "oddlyield", "???fffff",
			    "settlement,maturity,last_interest,rate,pr,redemption,frequency,basis",
			    &help_oddlyield,  gnumeric_oddlyield);
	function_add_args  (cat, "pmt", "fff|ff", "rate,nper,pv[,fv,type]",
			    &help_pmt,      gnumeric_pmt);
	function_add_args  (cat, "ppmt", "ffff|ff",
			    "rate,per,nper,pv[,fv,type]",
			    &help_ppmt,     gnumeric_ppmt);
        function_add_args  (cat, "price", "??fff|ff",
			    "settle,mat,rate,yield,redemption_price,frequency,basis",
			    &help_price, gnumeric_price);
	function_add_args  (cat, "pricedisc", "??ff|f",
			    "settlement,maturity,discount,redemption[,basis]",
			    &help_pricedisc,  gnumeric_pricedisc);
	function_add_args  (cat, "pricemat", "???ff|f",
			    "settlement,maturity,issue,rate,yield[,basis]",
			    &help_pricemat,  gnumeric_pricemat);
	function_add_args  (cat, "pv", "fffff", "rate,nper,pmt,fv,type",
			    &help_pv,       gnumeric_pv);
	function_add_args  (cat, "rate", "fff|fff",
			    "rate,nper,pmt,fv,type,guess",
			    &help_rate,     gnumeric_rate);
	function_add_args  (cat, "received", "??ff|f",
			    "settlement,maturity,investment,discount[,basis]",
			    &help_received,  gnumeric_received);
	function_add_args  (cat, "sln", "fff", "cost,salvagevalue,life",
			    &help_sln,      gnumeric_sln);
	function_add_args  (cat, "syd", "ffff",
			    "cost,salvagevalue,life,period",
			    &help_syd,      gnumeric_syd);
        function_add_args  (cat, "tbilleq", "??f",
			    "settlement,maturity,discount",
			    &help_tbilleq,  gnumeric_tbilleq);
        function_add_args  (cat, "tbillprice", "??f",
			    "settlement,maturity,discount",
			    &help_tbillprice, gnumeric_tbillprice);
        function_add_args  (cat, "tbillyield", "??f",
			    "settlement,maturity,pr",
			    &help_tbillyield, gnumeric_tbillyield);
        function_add_args  (cat, "yield", "??fff|ff",
			    "settle,mat,rate,price,redemption_price,frequency,basis",
			    &help_yield, gnumeric_yield);
        function_add_args  (cat, "yielddisc", "??fff",
			    "settlement,maturity,pr,redemption,basis",
			    &help_yielddisc,  gnumeric_yielddisc);
        function_add_args  (cat, "yieldmat", "???fff",
			    "settlement,maturity,issue,rate,pr,basis",
			    &help_yieldmat,  gnumeric_yieldmat);
        function_add_args  (cat, "xirr", "AA|f", "values,dates[,guess]",
			    &help_xirr,     gnumeric_xirr);
        function_add_args  (cat, "xnpv", "fAA", "rate,values,dates",
			    &help_xnpv,     gnumeric_xnpv);
}
