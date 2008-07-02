/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-financial.c:  Built in financial functions and functions registration
 *
 * Authors:
 *   Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 *   Morten Welinder (terra@gnome.org)
 *   Vladimir Vuksan (vuksan@veus.hr)
 *   Andreas J. Guelzow (aguelzow@taliesin.ca)
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
#include <cell.h>
#include <tools/goal-seek.h>
#include <collect.h>
#include <value.h>
#include <str.h>
#include <mathfunc.h>
#include <gnm-format.h>
#include <workbook.h>
#include <sheet.h>
#include <gnm-datetime.h>
#include <gnm-i18n.h>
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

#include <math.h>
#include <limits.h>
#include <string.h>

#include "sc-fin.h"

GNM_PLUGIN_MODULE_HEADER;

#define is_valid_basis(B) ((B) >= 0 && (B) <= 5)
#define is_valid_freq(F) ((F) == 1 || (F) == 2 || (F) == 4)
#define is_valid_paytype(t) ((t) == 0 || (t) == 1)

static int
value_get_basis (GnmValue const *v, int defalt)
{
	if (v) {
		gnm_float b = value_get_as_float (v);

		if (b < 0 || b >= 6)
			return -1;
		return (int)b;
	} else
		return defalt;
}

static int
value_get_freq (GnmValue const *v)
{
	gnm_float f;

	g_return_val_if_fail (v != NULL, -1);

	f = value_get_as_float (v);
	if (f < 1 || f >= 5)
		return -1;
	else {
		int i = (int)f;
		return i == 3 ? -1 : i;
	}
}

static int
value_get_paytype (GnmValue const *v)
{
	return (v == NULL || value_is_zero (v)) ? 0 : 1;
}

/***************************************************************************
 *
 *
 * Below are some of the functions that are used quite often in
 * financial analysis.
 *
 * Present value interest factor
 *
 *	 PVIF = (1 + k) ^ n
 *
 * Future value interest factor
 *
 *       FVIF = 1 / PVIF
 *
 * Present value interest factor of annuities
 *
 *                1          1
 *	 PVIFA = --- - -----------
 *                k     k*(1+k)^n
 *
 * Future value interest factor of annuities
 *
 *                (1+k)^n - 1
 *       FVIFA = ----------------
 *			k
 *
 *
 *
 *	 PV * PVIF(k%, nper) + PMT * ( 1 + rate * type ) *
 *	      FVIFA(k%, nper) + FV = 0
 *
 */

static gnm_float
calculate_pvif (gnm_float rate, gnm_float nper)
{
	return pow1p (rate, nper);
}

static gnm_float
calculate_fvifa (gnm_float rate, gnm_float nper)
{
	/* Removable singularity at rate == 0.  */
	if (rate == 0)
		return nper;
	else
		return pow1pm1 (rate, nper) / rate;
}


static gnm_float
calculate_pmt (gnm_float rate, gnm_float nper, gnm_float pv, gnm_float fv,
	       int type)
{
	gnm_float pvif, fvifa;

	/* Calculate the PVIF and FVIFA */

	pvif = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

        return ((-pv * pvif - fv ) / ((1.0 + rate * type) * fvifa));
}

static gnm_float
calculate_ipmt (gnm_float rate, gnm_float per, gnm_float nper,
		gnm_float pv, gnm_float fv, int type)
{
	gnm_float pmt = calculate_pmt (rate, nper, pv, fv, /*type*/ 0);
	gnm_float ipmt = -(pv * pow1p (rate, per - 1) * rate +
			   pmt * pow1pm1 (rate, per - 1));

	return (type == 0) ? ipmt : ipmt / (1 + rate);
}

/***************************************************************************/

/* Returns the number of days between issue date and maturity date
 * accoring to the day counting system specified by the 'basis'
 * argument.  Basis may have one of the following values:
 *
 *	0  for US 30/360 (days in a month/days in a year)
 *	1  for actual days/actual days
 *	2  for actual days/360
 *	3  for actual days/365
 *	4  for European 30/360
 *
 */
static int
days_monthly_basis (GnmValue const *issue_date,
		    GnmValue const *maturity_date,
		    int basis, GODateConventions const *date_conv)
{
        GDate    date_i, date_m;
	int      issue_day, issue_month, issue_year;
	int      maturity_day, maturity_month, maturity_year;
        int      months, days, years;
	gboolean leap_year;
	int      maturity, issue;

	if (!datetime_value_to_g (&date_i, issue_date, date_conv) ||
	    !datetime_value_to_g (&date_m, maturity_date, date_conv))
		return -1;

	issue_year = g_date_get_year (&date_i);
	issue_month = g_date_get_month (&date_i);
	issue_day = g_date_get_day (&date_i);
	maturity_year = g_date_get_year (&date_m);
	maturity_month = g_date_get_month (&date_m);
	maturity_day = g_date_get_day (&date_m);

	years = maturity_year - issue_year;
	months = maturity_month - issue_month;
	days = maturity_day - issue_day;

	months = years * 12 + months;
	leap_year = g_date_is_leap_year (issue_year);

	switch (basis) {
	case 0:
		if (issue_month == 2 && maturity_month != 2 &&
		    issue_year == maturity_year) {
			if (leap_year)
				return months * 30 + days - 1;
			else
				return months * 30 + days - 2;
		}
		return months * 30 + days;
	case 1:
	case 2:
	case 3:
		issue = datetime_value_to_serial (issue_date, date_conv);
		maturity = datetime_value_to_serial (maturity_date, date_conv);
		return maturity - issue;
	case 4:
		return months * 30 + days;
	default:
		return -1;
	}
}

/***************************************************************************/

/* Returns the number of coupons to be paid between the settlement
 * and maturity dates.
 */
static gnm_float
coupnum (GDate const *settlement, GDate const *maturity,
	 GnmCouponConvention const *conv)
{
        int        months;
	GDate      this_coupondate = *maturity;

	if (!g_date_valid (maturity) || !g_date_valid (settlement))
		return gnm_nan;

	months = g_date_get_month (maturity) - g_date_get_month (settlement) +
		12 *
		(g_date_get_year (maturity) - g_date_get_year  (settlement));

	gnm_date_add_months (&this_coupondate, -months);

	if (conv->eom && g_date_is_last_of_month (maturity))
		while (g_date_valid (&this_coupondate) &&
		       !g_date_is_last_of_month (&this_coupondate))
			gnm_date_add_days (&this_coupondate, 1);

	if (!g_date_valid (&this_coupondate))
		return gnm_nan;

	if (g_date_get_day (settlement) >= g_date_get_day (&this_coupondate))
		months--;

	return (1 + months / (12 / conv->freq));
}

static gnm_float
couppcd (GDate const *settlement, GDate const *maturity,
	 GnmCouponConvention const *conv)
{
	GDate date;
	go_coup_cd (&date, settlement, maturity, conv->freq, conv->eom, FALSE);
	return datetime_g_to_serial (&date, conv->date_conv);
}

static gnm_float
coupncd (GDate const *settlement, GDate const *maturity,
	 GnmCouponConvention const *conv)
{
	GDate date;
	go_coup_cd (&date, settlement, maturity, conv->freq, conv->eom, TRUE);
	return datetime_g_to_serial (&date, conv->date_conv);
}

static gnm_float
price (GDate *settlement, GDate *maturity, gnm_float rate, gnm_float yield,
       gnm_float redemption, GnmCouponConvention const *conv)
{
	gnm_float a, d, e, sum, den, basem1, exponent, first_term, last_term;
	int       n;

	a = go_coupdaybs (settlement, maturity, conv);
	d = go_coupdaysnc (settlement, maturity, conv);
	e = go_coupdays (settlement, maturity, conv);
	n = coupnum (settlement, maturity, conv);

	den = 100.0 * rate / conv->freq;
	basem1 = yield / conv->freq;
	exponent = d / e;

	if (n == 1)
		return (redemption + den) / (1 + exponent * basem1) -
			a / e * den;

	sum = den * pow1p (basem1, 1 - n - exponent) *
		pow1pm1 (basem1, n) / basem1;

	first_term = redemption / pow1p (basem1, (n - 1.0 + d / e));
	last_term = a / e * den;

	return (first_term + sum - last_term);
}

/************************************************************************
 *
 * Reading and verifying the arguments for the various COUP____
 * functions. Calls the passed coup_fn to do the real work
 *
 ***********************************************************************/

static GnmValue *
func_coup (GnmFuncEvalInfo *ei, GnmValue const * const *argv,
	   gnm_float (coup_fn) (GDate const *settle, GDate const *mat,
				GnmCouponConvention const *conv))
{
        GDate   settlement, maturity;
	GnmCouponConvention conv;

        conv.freq  = value_get_freq (argv[2]);
	conv.basis = value_get_basis (argv[3], BASIS_MSRB_30_360);
	conv.eom   = argv[4] ? value_get_as_checked_bool (argv[4]) : TRUE;
	conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);

        if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

	if (!is_valid_basis (conv.basis) ||
	    !is_valid_freq (conv.freq) ||
	    g_date_compare (&settlement, &maturity) >= 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (coup_fn (&settlement, &maturity, &conv));
}

/***************************************************************************
 *
 * Financial function implementations
 *
 */

static GnmFuncHelp const help_accrint[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ACCRINT\n"
	   "@SYNTAX=ACCRINT(issue,first_interest,settlement,rate,par,"
	   "frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ACCRINT calculates the accrued interest for a security that "
	   "pays periodic interest.\n\n"
	   "@issue is the issue date of the security.  @first_interest is "
	   "the first interest date of the security.  @settlement is the "
	   "settlement date of the security.  The settlement date is always "
	   "after the issue date (the date when the security is bought). "
	   "@rate is the annual rate of the security and @par is the par "
	   "value of the security. @frequency is the number of coupon "
	   "payments per year.\n\n"
	   "Allowed frequencies are:\n"
	   "  1 = annual,\n"
	   "  2 = semi,\n"
	   "  4 = quarterly.\n\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @issue date, @first_interest date, or @settlement date is not "
	   "valid, ACCRINT returns #NUM! error.\n"
	   "* The dates must be @issue < @first_interest < @settlement, or "
	   "ACCRINT returns #NUM! error.\n"
	   "* If @rate <= 0 or @par <= 0 , ACCRINT returns #NUM! error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis < 0 or @basis > 4, ACCRINT returns #NUM! error.\n"
	   "* If @issue date is after @settlement date or they are the same, "
	   "ACCRINT returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ACCRINTM")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_accrint (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate      issue, first_interest, settlement;
	gnm_float rate, a, d, par, freq;
	int        basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

        if (!datetime_value_to_g (&issue, argv[0], date_conv) ||
	    !datetime_value_to_g (&first_interest, argv[1], date_conv) ||
	    !datetime_value_to_g (&settlement, argv[2], date_conv))
		return value_new_error_VALUE (ei->pos);

	rate           = value_get_as_float (argv[3]);
	par            = value_get_as_float (argv[4]);
	freq           = value_get_freq (argv[5]);
	basis          = value_get_basis (argv[6], BASIS_MSRB_30_360);

        if (rate <= 0.	||
	    par <= 0.	||
	    !is_valid_freq (freq)	||
	    !is_valid_basis (basis)	||
	    g_date_compare (&issue, &settlement) >= 0)
		return value_new_error_NUM (ei->pos);

	a = days_monthly_basis (argv[0], argv[2], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);
	if (a < 0 || d <= 0)
		return value_new_error_NUM (ei->pos);

	/* FIXME : According to XL docs
	 *
	 * NC = number of quasi-coupon periods that fit in odd period. If this
	 *	number contains a fraction, raise it to the next whole number.
	 * Ai = number of accrued days for the ith quasi-coupon period within odd period.
	 * NLi = normal length in days of the ith quasi-coupon period within odd period.
	 *
	 * XL == par * (rate/freq) * Sum (1..NC of Ai / NLi
	 */
	return value_new_float (par * rate * a / d);
}

/***************************************************************************/

static GnmFuncHelp const help_accrintm[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ACCRINTM\n"
	   "@SYNTAX=ACCRINTM(issue,maturity,rate[,par,basis])\n"
	   "@DESCRIPTION="
	   "ACCRINTM calculates and returns the accrued interest for a "
	   "security from @issue to @maturity date.\n\n"
	   "@issue is the issue date of the security.  @maturity is "
	   "the maturity date of the security.  @rate is the annual "
	   "rate of the security and @par is the par value of the security. "
	   "If you omit @par, ACCRINTM applies $1,000 instead.  "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @issue date or @maturity date is not valid, ACCRINTM returns "
	   "#NUM! error.\n"
	   "* If @rate <= 0 or @par <= 0, ACCRINTM returns #NUM! error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis < 0 or @basis > 4, ACCRINTM returns #NUM! error.\n"
	   "* If @issue date is after @maturity date or they are the same, "
	   "ACCRINTM returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ACCRINT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_accrintm (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate, a, d, par;
	int basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	rate  = value_get_as_float (argv[2]);
	par   = argv[3] ? value_get_as_float (argv[3]) : 1000;
	basis = value_get_basis (argv[4], BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (a < 0 || d <= 0 || par <= 0 || rate <= 0
	    || !is_valid_basis (basis))
                return value_new_error_NUM (ei->pos);

	return value_new_float (par * rate * a/d);
}

/***************************************************************************/

static GnmFuncHelp const help_intrate[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=INTRATE\n"
	   "@SYNTAX=INTRATE(settlement,maturity,investment,redemption[,basis])\n"
	   "@DESCRIPTION="
	   "INTRATE calculates and returns the interest rate of a fully "
	   "vested security.\n\n"
	   "@settlement is the settlement date of the security.  @maturity "
	   "is the maturity date of the security. @investment is the price "
	   "of the security paid at @settlement date and @redemption is "
	   "the amount to be received at @maturity date.\n\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @settlement date or @maturity date is not valid, INTRATE "
	   "returns #NUM! error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis < 0 or @basis > 4, INTRATE returns #NUM! error.\n"
	   "* If @settlement date is after @maturity date or they are the "
	   "same, INTRATE returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "If you had a bond with a settlement date of April 15, 2000, "
	   "maturity date September 30, 2000, investment of $100,000, "
	   "redemption value $103,525, using the actual/actual basis, the "
	   "bond discount rate is:"
	   "\n"
	   "=INTRATE(36631, 36799, 100000, 103525, 1) which equals 0.0648 "
	   "or 6.48%"
	   "\n"
	   "@SEEALSO=RECEIVED, DATE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_intrate (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float investment, redemption, a, d;
	int basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	investment = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (!is_valid_basis (basis) || a <= 0 || d <= 0 || investment == 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float ((redemption - investment) / investment *
				(d / a));
}

/***************************************************************************/

static GnmFuncHelp const help_received[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=RECEIVED\n"
	   "@SYNTAX=RECEIVED(settlement,maturity,investment,rate[,basis])\n"
	   "@DESCRIPTION="
	   "RECEIVED calculates and returns the amount to be received at "
	   "maturity date for a security bond.\n"
	   "\n"
	   "@settlement is the settlement date of the security.  "
	   "@maturity is the maturity date of the security.  The amount "
	   "of investment is specified in @investment.  @rate is the "
	   "security's discount rate.\n\n"
	   "@basis is the type of day counting system you want to "
	   "use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @settlement date or @maturity date is not valid, RECEIVED "
	   "returns #NUM! error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis < 0 or @basis > 4, RECEIVED returns #NUM! error.\n"
	   "* If @settlement date is after @maturity date or they are the "
	   "same, RECEIVED returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=INTRATE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_received (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float investment, discount, a, d, n;
	int basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	investment = value_get_as_float (argv[2]);
	discount   = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (a <= 0 || d <= 0 || !is_valid_basis (basis))
                return value_new_error_NUM (ei->pos);

	n = 1.0 - (discount * a/d);
	if (n == 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (investment / n);
}

/***************************************************************************/

static GnmFuncHelp const help_pricedisc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PRICEDISC\n"
	   "@SYNTAX=PRICEDISC(settlement,maturity,discount,redemption[,basis])\n"
	   "@DESCRIPTION="
	   "PRICEDISC calculates and returns the price per $100 face value "
	   "of a security bond.  The security does not pay interest at "
	   "maturity.\n\n"
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security.  @discount is "
	   "the rate for which the security is discounted.  @redemption is "
	   "the amount to be received on @maturity date.\n\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @settlement date or @maturity date is not valid, PRICEDISC "
	   "returns #NUM! error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis < 0 or @basis > 4, PRICEDISC returns #NUM! error.\n"
	   "* If @settlement date is after @maturity date or they are the "
	   "same, PRICEDISC returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PRICEMAT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pricedisc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float discount, redemption, a, d;
	int basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	discount   = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (a <= 0 || d <= 0 || !is_valid_basis (basis))
                return value_new_error_NUM (ei->pos);

	return value_new_float (redemption - discount * redemption * a/d);
}

/***************************************************************************/

static GnmFuncHelp const help_pricemat[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PRICEMAT\n"
	   "@SYNTAX=PRICEMAT(settlement,maturity,issue,rate,yield[,basis])\n"
	   "@DESCRIPTION="
	   "PRICEMAT calculates and returns the price per $100 face value "
	   "of a security.  The security pays interest at maturity.\n\n"
	   "@settlement is the settlement date of the security.  @maturity is "
	   "the maturity date of the security.  @issue is the issue date of "
	   "the security.  @rate is the discount rate of the security. "
	   "@yield is the annual yield of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @settlement date or @maturity date is not valid, PRICEMAT "
	   "returns #NUM! error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis < 0 or @basis > 4, PRICEMAT returns #NUM! error.\n"
	   "* If @settlement date is after @maturity date or they are the "
	   "same, PRICEMAT returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PRICEDISC")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pricemat (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float discount, yield, a, b, dsm, dim, n;
	int basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	discount = value_get_as_float (argv[3]);
	yield    = value_get_as_float (argv[4]);
	basis    = value_get_basis (argv[5], BASIS_MSRB_30_360);

	dsm = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	dim = days_monthly_basis (argv[2], argv[1], basis, date_conv);
	a   = days_monthly_basis (argv[2], argv[0], basis, date_conv);
	b   = annual_year_basis (argv[0], basis, date_conv);

	if (a <= 0 || b <= 0 || dsm <= 0 || dim <= 0
	    || !is_valid_basis (basis))
                return value_new_error_NUM (ei->pos);

	n = 1 + (dsm/b * yield);
	if (n == 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (((100 + (dim/b * discount * 100)) /
				 (n)) - (a/b * discount * 100));
}

/***************************************************************************/

static GnmFuncHelp const help_disc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DISC\n"
	   "@SYNTAX=DISC(settlement,maturity,par,redemption[,basis])\n"
	   "@DESCRIPTION="
	   "DISC calculates and returns the discount rate for a security. "
	   "@settlement is the settlement date of the security.\n\n"
	   "@maturity is the maturity date of the security.  @par is the "
	   "price per $100 face value of the security.  @redemption is the "
	   "redemption value per $100 face value of the security.\n\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @settlement date or @maturity date is not valid, DISC "
	   "returns #NUM! error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis < 0 or @basis > 4, DISC returns #NUM! error.\n"
	   "* If @settlement date is after @maturity date or they are the "
	   "same, DISC returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_disc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float par, redemption, dsm, b;
	int basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	par        = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], BASIS_MSRB_30_360);

	b = annual_year_basis (argv[0], basis, date_conv);
	dsm = days_monthly_basis (argv[0], argv[1], basis, date_conv);

	if (dsm <= 0 || b <= 0 || dsm <= 0 || !is_valid_basis (basis)
	    || redemption == 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float ((redemption - par) / redemption * (b / dsm));
}

/***************************************************************************/

static GnmFuncHelp const help_effect[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EFFECT\n"
	   "@SYNTAX=EFFECT(r,nper)\n"
	   "@DESCRIPTION="
	   "EFFECT calculates the effective interest rate from "
	   "a given nominal rate.\n\n"
	   "Effective interest rate is calculated using this formula:\n"
	   "\n"
           "    (1 + @r / @nper) ^ @nper - 1\n"
	   "\n"
	   "where:\n"
	   "\n"
	   "@r = nominal interest rate (stated in yearly terms)\n"
	   "@nper = number of periods used for compounding\n"
	   "\n"
	   "* If @rate < 0, EFFECT returns #NUM! error.\n"
	   "* If @nper <= 0, EFFECT returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
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
	   "@SEEALSO=NOMINAL")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_effect (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float nper = gnm_floor (value_get_as_float (argv[1]));

	/* I don't know why Excel disallows 0% for rate.  */
	if (rate <= 0 || nper < 1)
                return value_new_error_NUM (ei->pos);

        return value_new_float (pow1pm1 (rate / nper, nper));
}

/***************************************************************************/

static GnmFuncHelp const help_nominal[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NOMINAL\n"
	   "@SYNTAX=NOMINAL(r,nper)\n"
	   "@DESCRIPTION="
	   "NOMINAL calculates the nominal interest rate from "
	   "a given effective rate.\n\n"
	   "Nominal interest rate is given by a formula:\n"
	   "\n"
           "@nper * (( 1 + @r ) ^ (1 / @nper) - 1 )"
	   "\n"
	   "where:\n"
	   "\n"
	   "@r = effective interest rate\n"
	   "@nper = number of periods used for compounding\n"
	   "\n"
	   "* If @rate < 0, NOMINAL returns #NUM! error.\n"
	   "* If @nper <= 0, NOMINAL returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=EFFECT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_nominal (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float nper = gnm_floor (value_get_as_float (argv[1]));

	/* I don't know why Excel disallows 0% for rate.  */
	if (rate <= 0 || nper < 1)
                return value_new_error_NUM (ei->pos);

        return value_new_float (nper * pow1pm1 (rate, 1.0 / nper));
}

/***************************************************************************/

static GnmFuncHelp const help_ispmt[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISPMT\n"
	   "@SYNTAX=ISPMT(rate,per,nper,pv)\n"
	   "@DESCRIPTION="
	   "ISPMT function returns the interest paid on a given period.\n"
	   "\n"
	   "* If @per < 1 or @per > @nper, ISPMT returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ispmt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float tmp;

	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float per = value_get_as_int (argv[1]);
	gnm_float nper = value_get_as_int (argv[2]);
	gnm_float pv = value_get_as_float (argv[3]);

	/*
	 * It seems that with 20 periods, a period number of 20.99 is
	 * valid in XL.
	 */
	if (per < 1 || per >= nper + 1)
                return value_new_error_NUM (ei->pos);

	tmp = -pv * rate;

	return value_new_float (tmp - (tmp / nper * per));
}

/***************************************************************************/

static GnmFuncHelp const help_db[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DB\n"
	   "@SYNTAX=DB(cost,salvage,life,period[,month])\n"
	   "@DESCRIPTION="
	   "DB calculates the depreciation of an asset for a given period "
	   "using the fixed-declining balance method.  @cost is the "
	   "initial value of the asset.  @salvage is the value after the "
	   "depreciation.\n"
	   "\n"
	   "@life is the number of periods overall.  @period is the period "
	   "for which you want the depreciation to be calculated.  @month "
	   "is the number of months in the first year of depreciation.\n"
	   "\n"
	   "* If @month is omitted, it is assumed to be 12.\n"
	   "* If @cost = 0, DB returns #NUM! error.\n"
	   "* If @life <= 0, DB returns #NUM! error.\n"
	   "* If @salvage / @cost < 0, DB returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DDB,SLN,SYD")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_db (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate;
	gnm_float cost, salvage, life, period, month;
	gnm_float total;
	int        i;

	cost    = value_get_as_float (argv[0]);
	salvage = value_get_as_float (argv[1]);
	life    = value_get_as_float (argv[2]);
	period  = value_get_as_float (argv[3]);
	month   = argv[4] ? value_get_as_float (argv[4]) : 12;

	/* The third disjunct is a bit of a guess -- MW.  */
	if (cost == 0 || life <= 0 || salvage / cost < 0)
		return value_new_error_NUM (ei->pos);

	rate  = 1 - gnm_pow ((salvage / cost), (1 / life));
	rate *= 1000;
	rate  = gnm_floor (rate + 0.5) / 1000;

	total = cost * rate * month / 12;

        if (period == 1)
	       return value_new_float (total);

	for (i = 1; i < life; i++)
	       if (i == period - 1)
		       return value_new_float ((cost - total) * rate);
	       else
		       total += (cost - total) * rate;

	return value_new_float (((cost - total) * rate * (12 - month)) / 12);
}

/***************************************************************************/

static GnmFuncHelp const help_ddb[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DDB\n"
	   "@SYNTAX=DDB(cost,salvage,life,period[,factor])\n"
	   "@DESCRIPTION="
	   "DDB returns the depreciation of an asset for a given period "
	   "using the double-declining balance method or some other similar "
	   "method you specify.\n"
	   "\n"
	   "@cost is the initial value of the asset, "
	   "@salvage is the value after the last period, @life is the "
	   "number of periods, @period is the period for which you want the "
	   "depreciation to be calculated, and @factor is the factor at "
	   "which the balance declines.\n"
	   "\n"
	   "* If @factor is omitted, it is assumed to be two "
	   "(double-declining balance method).\n"
	   "* If @life <= 0, DDB returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=SLN,SYD")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ddb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float cost, salvage, life, period, factor;
	gnm_float f, prior, dep;

	cost    = value_get_as_float (argv[0]);
	salvage = value_get_as_float (argv[1]);
	life    = value_get_as_float (argv[2]);
	period  = value_get_as_float (argv[3]);
	factor  = argv[4] ? value_get_as_float (argv[4]) : 2;

	if (cost < 0 || salvage < 0 || life <= 0 ||
	    period <= 0 || period > life ||
	    factor <= 0)
		return value_new_error_NUM (ei->pos);

	if (salvage >= cost)
		return value_new_int (0);

	if (period < 1) {
		period = 1;
		if (period > life)
			return value_new_float (cost - salvage);
	}

	f = factor / life;
	prior = -cost * pow1pm1 (-f, period - 1);
	dep = (cost - prior) * f;

	/* Depreciation cannot exceed book value.  */
	dep = MIN (dep, MAX (0, cost - prior - salvage));
	return value_new_float (dep);
}

/***************************************************************************/

static GnmFuncHelp const help_sln[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SLN\n"
	   "@SYNTAX=SLN(cost,salvage_value,life)\n"
	   "@DESCRIPTION="
	   "SLN function will determine the straight line depreciation "
	   "of an asset for a single period.\n"
	   "\n"
	   "The formula is:\n"
	   "\n"
	   "Depreciation expense = ( @cost - @salvage_value ) / @life\n"
	   "\n"
	   "@cost is the cost of an asset when acquired (market value).\n"
	   "@salvage_value is the amount you get when asset is sold at the end "
	   "of the asset's useful life.\n"
	   "@life is the anticipated life of an asset.\n"
	   "\n"
	   "* If @life <= 0, SLN returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "For example, lets suppose your company purchases a new machine "
	   "for $10,000, which has a salvage value of $700 and will have a "
	   "useful life of 10 years. The SLN yearly depreciation is "
	   "computed as follows:"
	   "\n"
	   "=SLN(10000, 700, 10)"
	   "\n"
	   "This will return the yearly depreciation figure of $930."
	   "\n"
	   "@SEEALSO=SYD")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_sln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float cost,salvage_value,life;

	cost          = value_get_as_float (argv[0]);
	salvage_value = value_get_as_float (argv[1]);
	life          = value_get_as_float (argv[2]);

	/* Life of an asset cannot be negative */
	if (life <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float ((cost - salvage_value) / life);
}

/***************************************************************************/

static GnmFuncHelp const help_syd[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage_value,life,period)\n"
	   "@DESCRIPTION="
	   "SYD function calculates the sum-of-years digits depreciation "
	   "for an asset based on its cost, salvage value, anticipated life "
	   "and a particular period. This method accelerates the rate of the "
	   "depreciation, so that more depreciation expense occurs in "
	   "earlier periods than in later ones. The depreciable cost is the "
	   "actual cost minus the salvage value. The useful life is the "
	   "number of periods (typically years) over which the asset is "
	   "depreciated.\n"
	   "\n"
	   "The Formula used for sum-of-years digits depreciation is:\n"
	   "\n"
	   "Depreciation expense =\n\n\t ( @cost - @salvage_value ) * "
	   "(@life - @period + 1) * 2 / @life * (@life + 1).\n"
	   "\n"
	   "@cost is the cost of an asset when acquired (market value).\n"
	   "@salvage_value is the amount you get when asset sold at the end of "
	   "its useful life.\n"
	   "@life is the anticipated life of an asset.\n"
	   "@period is the period for which we need the expense.\n"
	   "\n"
	   "* If @life <= 0, SYD returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "For example say a company purchases a new computer for $5000 "
	   "which has a salvage value of $200, and a useful life of five "
	   "years. We would use the following to calculate the second "
	   "year's depreciation using the SYD method:"
	   "\n"
	   "=SYD(5000, 200, 5, 2) which returns 1,280.00."
	   "\n"
	   "@SEEALSO=SLN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_syd (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float cost, salvage_value, life, period;

	cost          = value_get_as_float (argv[0]);
	salvage_value = value_get_as_float (argv[1]);
	life          = value_get_as_float (argv[2]);
	period        = value_get_as_float (argv[3]);

	/* Life of an asset cannot be negative */
	if (life <= 0)
		return value_new_error_NUM (ei->pos);

        return value_new_float (((cost - salvage_value) *
				 (life - period + 1) * 2) /
				(life * (life + 1.0)));
}

/***************************************************************************/

static GnmFuncHelp const help_dollarde[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DOLLARDE\n"
	   "@SYNTAX=DOLLARDE(fractional_dollar,fraction)\n"
	   "@DESCRIPTION="
	   "DOLLARDE converts a dollar price expressed as a "
	   "fraction into a dollar price expressed as a decimal number.\n"
	   "\n"
	   "@fractional_dollar is the fractional number to be converted. "
	   "@fraction is the denominator of the fraction.\n"
	   "\n"
	   "* If @fraction is non-integer it is truncated.\n"
	   "* If @fraction <= 0, DOLLARDE returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DOLLARFR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dollarde (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float f = gnm_floor (value_get_as_float (argv[1]));
	gnm_float negative = FALSE;
	gnm_float fdigits;
	gnm_float res;

	if (f < 0)
		return value_new_error_NUM (ei->pos);
	if (f == 0)
		return value_new_error_DIV0 (ei->pos);

	if (x < 0) {
		negative = TRUE;
		x = gnm_abs (x);
	}

	/*
	 * For a power of 10, this is actually one less than the
	 * number of digits.
	 */
	fdigits = 1 + gnm_floor (gnm_log10 (f - 0.5));

	res = gnm_floor (x);

	/* If f=9, then .45 means 4.5/9  */
	res += (x - res) * gnm_pow10 (fdigits) / f;

	if (negative)
		res = 0 - res;

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_dollarfr[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DOLLARFR\n"
	   "@SYNTAX=DOLLARFR(decimal_dollar,fraction)\n"
	   "@DESCRIPTION="
	   "DOLLARFR converts a decimal dollar price into "
	   "a dollar price expressed as a fraction.\n"
	   "\n"
	   "* If @fraction is non-integer it is truncated.\n"
	   "* If @fraction <= 0, DOLLARFR returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DOLLARDE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dollarfr (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float f = gnm_floor (value_get_as_float (argv[1]));
	gnm_float negative = FALSE;
	gnm_float fdigits;
	gnm_float res;

	if (f < 0)
		return value_new_error_NUM (ei->pos);
	if (f == 0)
		return value_new_error_DIV0 (ei->pos);

	if (x < 0) {
		negative = TRUE;
		x = gnm_abs (x);
	}

	/*
	 * For a power of 10, this is actually one less than the
	 * number of digits.
	 */
	fdigits = 1 + gnm_floor (gnm_log10 (f - 0.5));

	res = gnm_floor (x);
	res += (x - res) * f / gnm_pow10 (fdigits);

	if (negative)
		res = 0 - res;

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_mirr[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MIRR\n"
	   "@SYNTAX=MIRR(values,finance_rate,reinvest_rate)\n"
	   "@DESCRIPTION="
	   "MIRR function returns the modified internal rate of return "
	   "for a given periodic cash flow. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=NPV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_mirr (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float frate, rrate, npv_neg, npv_pos;
	gnm_float *values = NULL, res;
	GnmValue *result = NULL;
	int i, n;

	frate = value_get_as_float (argv[1]);
	rrate = value_get_as_float (argv[2]);

	values = collect_floats_value (argv[0], ei->pos,
				       COLLECT_IGNORE_STRINGS |
				       COLLECT_IGNORE_BLANKS,
				       &n, &result);
	if (result)
		goto out;

	for (i = 0, npv_pos = npv_neg = 0; i < n; i++) {
		gnm_float v = values[i];
		if (v >= 0)
			npv_pos += v / pow1p (rrate, i);
		else
			npv_neg += v / pow1p (frate, i);
	}

	if (npv_neg == 0 || npv_pos == 0 || rrate <= -1) {
		result = value_new_error_DIV0 (ei->pos);
		goto out;
	}

	/*
	 * I have my doubts about this formula, but it sort of looks like
	 * the one Microsoft claims to use and it produces the results
	 * that Excel does.  -- MW.
	 */
	res = gnm_pow ((-npv_pos * pow1p (rrate, n)) / (npv_neg * (1 + rrate)),
		       (1.0 / (n - 1))) - 1.0;

	result = value_new_float (res);
out:
	g_free (values);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_tbilleq[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TBILLEQ\n"
	   "@SYNTAX=TBILLEQ(settlement,maturity,discount)\n"
	   "@DESCRIPTION="
	   "TBILLEQ function returns the bond-yield equivalent (BEY) for "
	   "a treasury bill.  TBILLEQ is equivalent to\n"
	   "\n"
	   "\t(365 * @discount) / (360 - @discount * DSM),\n\n"
	   "where DSM is the days between @settlement and @maturity.\n"
	   "\n"
	   "* If @settlement is after @maturity or the @maturity is set to "
	   "over one year later than the @settlement, TBILLEQ returns "
	   "#NUM! error.\n"
	   "* If @discount is negative, TBILLEQ returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLPRICE,TBILLYIELD")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tbilleq (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float settlement, maturity, discount;
	gnm_float dsm, divisor;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	settlement = datetime_value_to_serial (argv[0], date_conv);
	maturity   = datetime_value_to_serial (argv[1], date_conv);
	discount   = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (settlement > maturity || discount < 0 || dsm > 365)
                return value_new_error_NUM (ei->pos);

	divisor = 360 - discount * dsm;
	/* This test probably isn't right, but it is better that not checking
	   at all.  --MW.  */
	if (divisor == 0)
		return value_new_error_DIV0 (ei->pos);

	return value_new_float ((365 * discount) / divisor);
}

/***************************************************************************/

static GnmFuncHelp const help_tbillprice[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TBILLPRICE\n"
	   "@SYNTAX=TBILLPRICE(settlement,maturity,discount)\n"
	   "@DESCRIPTION="
	   "TBILLPRICE function returns the price per $100 value for a "
	   "treasury bill where @settlement is the settlement date and "
	   "@maturity is the maturity date of the bill.  @discount is the "
	   "treasury bill's discount rate.\n"
	   "\n"
	   "* If @settlement is after @maturity or the @maturity is set to "
	   "over one year later than the @settlement, TBILLPRICE returns "
	   "#NUM! error.\n"
	   "* If @discount is negative, TBILLPRICE returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLEQ,TBILLYIELD")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tbillprice (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float settlement, maturity, discount;
	gnm_float res, dsm;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	settlement = datetime_value_to_serial (argv[0], date_conv);
	maturity   = datetime_value_to_serial (argv[1], date_conv);
	discount   = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (settlement > maturity || discount < 0 || dsm > 365)
                return value_new_error_NUM (ei->pos);

	res = 100 * (1.0 - (discount * dsm) / 360.0);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_tbillyield[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TBILLYIELD\n"
	   "@SYNTAX=TBILLYIELD(settlement,maturity,pr)\n"
	   "@DESCRIPTION="
	   "TBILLYIELD function returns the yield for a treasury bill. "
	   "@settlement is the settlement date and @maturity is the "
	   "maturity date of the bill.  @discount is the treasury bill's "
	   "discount rate.\n"
	   "\n"
	   "* If @settlement is after @maturity or the @maturity is set to "
	   "over one year later than the @settlement, TBILLYIELD returns "
	   "#NUM! error.\n"
	   "* If @pr is negative, TBILLYIELD returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLEQ,TBILLPRICE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tbillyield (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float settlement, maturity, pr;
	gnm_float res, dsm;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	settlement = datetime_value_to_serial (argv[0], date_conv);
	maturity   = datetime_value_to_serial (argv[1], date_conv);
	pr         = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (pr <= 0 || dsm <= 0 || dsm > 365)
                return value_new_error_NUM (ei->pos);

	res = (100.0 - pr) / pr * (360.0 / dsm);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_rate[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=RATE\n"
	   "@SYNTAX=RATE(nper,pmt,pv[,fv,type,guess])\n"
	   "@DESCRIPTION="
	   "RATE calculates the rate of an investment.\n"
	   "\n"
	   "* If @pmt is ommitted it defaults to 0\n"
	   "* If @nper <= 0, RATE returns #NUM! error.\n"
	   "* If @type != 0 and @type != 1, RATE returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PV,FV")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	int type;
	gnm_float nper, pv, fv, pmt;
} gnumeric_rate_t;

static GoalSeekStatus
gnumeric_rate_f (gnm_float rate, gnm_float *y, void *user_data)
{
	if (rate > -1.0 && rate != 0) {
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
gnumeric_rate_df (gnm_float rate, gnm_float *y, void *user_data)
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


static GnmValue *
gnumeric_rate (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GoalSeekData    data;
	GoalSeekStatus  status;
	gnumeric_rate_t udata;
	gnm_float      rate0;

	udata.nper = value_get_as_int (argv[0]);
	/* YES ZERO, it's sick but it's XL compatible */
	udata.pmt  = argv[1] ? value_get_as_float (argv[1]) : 0.0;
	udata.pv   = value_get_as_float (argv[2]);
	udata.fv   = argv[3] ? value_get_as_float (argv[3]) : 0.0;
	udata.type = value_get_paytype (argv[4]);
	rate0      = argv[5] ? value_get_as_float (argv[5]) : 0.1;

	if (udata.nper <= 0)
		return value_new_error_NUM (ei->pos);

	if (!is_valid_paytype (udata.type))
		return value_new_error_VALUE (ei->pos);

#if 0
	printf ("Guess = %.15g\n", rate0);
#endif
	goal_seek_initialize (&data);

	data.xmin = MAX (data.xmin,
			 -gnm_pow (DBL_MAX / 1e10, 1.0 / udata.nper) + 1);
	data.xmax = MIN (data.xmax,
			 gnm_pow (DBL_MAX / 1e10, 1.0 / udata.nper) - 1);

	/* Newton search from guess.  */
	status = goal_seek_newton (&gnumeric_rate_f, &gnumeric_rate_df,
				   &data, &udata, rate0);

	if (status != GOAL_SEEK_OK) {
		int factor;
		/* Lay a net of test points around the guess.  */
		for (factor = 2; !(data.havexneg && data.havexpos)
		       && factor < 100; factor *= 2) {
			goal_seek_point (&gnumeric_rate_f, &data, &udata,
					 rate0 * factor);
			goal_seek_point (&gnumeric_rate_f, &data, &udata,
					 rate0 / factor);
		}

		/* Pray we got both sides of the root.  */
		status = goal_seek_bisection (&gnumeric_rate_f, &data, &udata);
	}

	if (status == GOAL_SEEK_OK) {
#if 0
		printf ("Root = %.15g\n\n", data.root);
#endif
		return value_new_float (data.root);
	} else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_irr[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IRR\n"
	   "@SYNTAX=IRR(values[,guess])\n"
	   "@DESCRIPTION="
	   "IRR calculates and returns the internal rate of return of an "
	   "investment.  This function is closely related to the net present "
	   "value function (NPV).  The IRR is the interest rate for a "
	   "series of cash flows where the net preset value is zero.\n"
	   "\n"
	   "@values contains the series of cash flows generated by the "
	   "investment.  The payments should occur at regular intervals.  "
	   "The optional @guess is the initial value used in calculating "
	   "the IRR.  You do not have to use that, it is only provided "
	   "for the Excel compatibility.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1:A8 contain the numbers -32432, "
	   "5324, 7432, 9332, 12324, 4334, 1235, -3422.  Then\n"
	   "IRR(A1:A8) returns 0.04375. "
	   "\n"
	   "@SEEALSO=FV,NPV,PV")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        int     n;
        gnm_float *values;
} gnumeric_irr_t;

static GoalSeekStatus
irr_npv (gnm_float rate, gnm_float *y, void *user_data)
{
	const gnumeric_irr_t *p = user_data;
	const gnm_float *values = p->values;
        int n = p->n;
	gnm_float sum = 0;
	gnm_float f = 1;
	gnm_float ff = 1 / (rate + 1);
        int i;

	for (i = 0; i < n; i++) {
		sum += values[i] * f;
		f *= ff;
	}

	*y = sum;
	return gnm_finite (sum) ? GOAL_SEEK_OK : GOAL_SEEK_ERROR;
}

static GoalSeekStatus
irr_npv_df (gnm_float rate, gnm_float *y, void *user_data)
{
	const gnumeric_irr_t *p = user_data;
	const gnm_float *values = p->values;
        int n = p->n;
	gnm_float sum = 0;
	gnm_float f = 1;
	gnm_float ff = 1 / (rate + 1);
        int i;

	for (i = 1; i < n; i++) {
		sum += values[i] * (-i) * f;
		f *= ff;
	}

	*y = sum;
	return gnm_finite (sum) ? GOAL_SEEK_OK : GOAL_SEEK_ERROR;
}

static GnmValue *
gnumeric_irr (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GoalSeekData    data;
	GoalSeekStatus  status;
	GnmValue           *result = NULL;
	gnumeric_irr_t  p;
	gnm_float      rate0;

	rate0 = argv[1] ? value_get_as_float (argv[1]) : 0.1;

	p.values = collect_floats_value (argv[0], ei->pos,
					 COLLECT_IGNORE_STRINGS |
					 COLLECT_IGNORE_BLANKS,
					 &p.n, &result);
	if (result != NULL) {
		g_free (p.values);
		return result;
	}

	goal_seek_initialize (&data);

	data.xmin = -1;
	data.xmax = MIN (data.xmax,
			 gnm_pow (DBL_MAX / 1e10, 1.0 / p.n) - 1);

	status = goal_seek_newton (&irr_npv, &irr_npv_df, &data, &p, rate0);
	if (status != GOAL_SEEK_OK) {
		int i;
		gnm_float s;

		/* Lay a net of test points around the guess.  */
		for (i = 0, s = 2; !(data.havexneg && data.havexpos) && i < 10; i++, s *= 2) {
			goal_seek_point (&irr_npv, &data, &p, rate0 * s);
			goal_seek_point (&irr_npv, &data, &p, rate0 / s);
		}

		/*
		 * If the root is negative and the guess is positive it
		 * is possible to get thrown out to the left of -100%
		 * by the Newton method.
		 */
		if (!(data.havexneg && data.havexpos))
			goal_seek_newton (&irr_npv, &irr_npv_df, &data, &p, -0.99);
		if (!(data.havexneg && data.havexpos))
			goal_seek_point (&irr_npv, &data, &p, 1 - GNM_EPSILON);

		/* Pray we got both sides of the root.  */
		status = goal_seek_bisection (&irr_npv, &data, &p);
	}

	g_free (p.values);

	if (status == GOAL_SEEK_OK)
		return value_new_float (data.root);
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_pv[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PV\n"
	   "@SYNTAX=PV(rate,nper,pmt[,fv,type])\n"
	   "@DESCRIPTION="
	   "PV calculates the present value of an investment. "
	   "@rate is the periodic interest rate, @nper is the "
	   "number of periods used for compounding. "
	   "@pmt is the payment made each period, "
	   "@fv is the future value and @type is when the payment is made.\n"
	   "\n"
	   "* If @type = 1 then the payment is made at the beginning of the "
	   "period.\n"
	   "* If @type = 0 (or omitted) it is made at the end of each "
	   "period.\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=FV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float nper = value_get_as_float (argv[1]);
	gnm_float pmt  = value_get_as_float (argv[2]);
	gnm_float fv   = argv[3] ? value_get_as_float (argv[3]) : 0;
	int type       = value_get_paytype (argv[4]);
	gnm_float pvif, fvifa;

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

	/* Calculate the PVIF and FVIFA */
	pvif  = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

	if (pvif == 0)
		return value_new_error_DIV0 (ei->pos);

        return value_new_float ((-fv - pmt * (1.0 + rate * type) * fvifa) /
				pvif);
}

/***************************************************************************/

static GnmFuncHelp const help_npv[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NPV\n"
	   "@SYNTAX=NPV(rate,v1,v2,...)\n"
	   "@DESCRIPTION="
	   "NPV calculates the net present value of an investment generating "
	   "periodic payments.  @rate is the periodic interest rate and "
	   "@v1, @v2, ... are the periodic payments.  If the schedule of the "
	   "cash flows are not periodic use the XNPV function. "
	   "\n"
	   "@EXAMPLES=\n"
	   "NPV(0.17,-10000,3340,2941,2493,3233,1732,2932) equals 186.30673.\n"
	   "\n"
	   "@SEEALSO=PV,XNPV")
	},
	{ GNM_FUNC_HELP_END }
};

static int
range_npv (gnm_float const *xs, int n, gnm_float *res)
{
	if (n == 0 || xs[0] == -1)
		return 1;
	else {
		gnm_float sum = 0;
		gnm_float f = 1;
		gnm_float ff = 1 / (1 + xs[0]);
		int i;

		for (i = 1; i < n; i++) {
			f *= ff;
			sum += xs[i] * f;
		}
		*res = sum;
		return 0;
	}
}

static GnmValue *
gnumeric_npv (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_npv,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_DIV0);
}

/***************************************************************************/

static GnmFuncHelp const help_xnpv[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=XNPV\n"
	   "@SYNTAX=XNPV(rate,values,dates)\n"
	   "@DESCRIPTION="
	   "XNPV calculates the net present value of an investment.  The "
	   "schedule of the cash flows is given in @dates array.  The first "
	   "date indicates the beginning of the payment schedule.  @rate "
	   "is the interest rate and @values are the payments.\n"
	   "\n"
	   "* If @values and @dates contain unequal number of values, XNPV "
	   "returns the #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=NPV,PV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_xnpv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate, *payments = NULL, *dates = NULL;
	gnm_float sum;
	int  p_n, d_n, i;
	GnmValue *result = NULL;

	rate = value_get_as_float (argv[0]);
	sum = 0;

	payments = collect_floats_value (argv[1], ei->pos,
					 COLLECT_COERCE_STRINGS,
					 &p_n, &result);
	if (result)
		goto out;

	dates = collect_floats_value (argv[2], ei->pos,
				      COLLECT_COERCE_STRINGS,
				      &d_n, &result);
	if (result)
		goto out;

	if (p_n != d_n) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	for (i = 0; i < p_n; i++)
		sum += payments[i] /
			pow1p (rate, (dates[i] - dates[0]) / 365.0);

	result = value_new_float (sum);
 out:
	g_free (payments);
	g_free (dates);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_xirr[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=XIRR\n"
	   "@SYNTAX=XIRR(values,dates[,guess])\n"
	   "@DESCRIPTION="
	   "XIRR calculates and returns the internal rate of return of an "
	   "investment that has not necessarily periodic payments.  This "
	   "function is closely related to the net present value function "
	   "(NPV and XNPV).  The XIRR is the interest rate for a "
	   "series of cash flows where the XNPV is zero.\n"
	   "\n"
	   "@values contains the series of cash flows generated by the "
	   "investment.  @dates contains the dates of the payments.  The "
	   "first date describes the payment day of the initial payment and "
	   "thus all the other dates should be after this date. "
	   "The optional @guess is the initial value used in calculating "
	   "the XIRR.  You do not have to use that, it is only provided "
	   "for the Excel compatibility.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1:A5 contain the numbers -6000, "
	   "2134, 1422, 1933, and 1422, and the cells B1:B5 contain the "
	   "dates \"1999-01-15\", \"1999-04-04\", \"1999-05-09\", "
	   "\"2000-03-12\", and \"2000-05-1\". Then\n"
	   "XIRR(A1:A5,B1:B5) returns 0.224838. "
	   "\n"
	   "@SEEALSO=IRR,XNPV")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        int     n;
        gnm_float *values;
        gnm_float *dates;
} gnumeric_xirr_t;

static GoalSeekStatus
xirr_npv (gnm_float rate, gnm_float *y, void *user_data)
{
	gnumeric_xirr_t *p = user_data;
	gnm_float *values, *dates, sum;
        int i, n;

	values = p->values;
	dates = p->dates;
	n = p->n;

	sum = 0;
	for (i = 0; i < n; i++) {
		gnm_float d = dates[i] - dates[0];

		if (d < 0)
			return GOAL_SEEK_ERROR;
		sum += values[i] / pow1p (rate, d / 365.0);
	}

	*y = sum;
	return GOAL_SEEK_OK;
}

static GnmValue *
gnumeric_xirr (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GoalSeekData    data;
	GoalSeekStatus  status;
	GnmValue           *result = NULL;
	gnumeric_xirr_t p;
	gnm_float      rate0;
	int             n, d_n;

	goal_seek_initialize (&data);
	data.xmin = -1;
	data.xmax = MIN (1000, data.xmax);

	rate0 = argv[2] ? value_get_as_float (argv[2]) : 0.1;

	p.values = collect_floats_value (argv[0], ei->pos,
					 COLLECT_COERCE_STRINGS,
					 &n, &result);
	p.dates = NULL;

	if (result != NULL)
		goto out;

	p.dates = collect_floats_value (argv[1], ei->pos,
					COLLECT_COERCE_STRINGS,
					&d_n, &result);
	if (result != NULL)
		goto out;

	p.n = n;
	status = goal_seek_newton (&xirr_npv, NULL, &data, &p, rate0);
	if (status != GOAL_SEEK_OK) {
		int i;
		for (i = 1; i <= 1024; i += i) {
			(void)goal_seek_point (&xirr_npv, &data, &p, -1 + 10.0 / (i + 9));
			(void)goal_seek_point (&xirr_npv, &data, &p, i);
			status = goal_seek_bisection (&xirr_npv, &data, &p);
			if (status == GOAL_SEEK_OK)
				break;
		}
	}

	if (status == GOAL_SEEK_OK)
		result = value_new_float (data.root);
	else
		result = value_new_error_NUM (ei->pos);

 out:
	g_free (p.values);
	g_free (p.dates);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_fv[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FV\n"
	   "@SYNTAX=FV(rate,nper,pmt[,pv,type])\n"
	   "@DESCRIPTION="
	   "FV computes the future value of an investment. This is based "
	   "on periodic, constant payments and a constant interest rate. "
	   "The interest rate per period is @rate, @nper is the number of "
	   "periods in an annuity, @pmt is the payment made each period, "
	   "@pv is the present value and @type is when the payment is made.\n"
	   "\n"
	   "* If @type = 1 then the payment is made at the beginning of the "
	   "period.\n"
	   "* If @type = 0 it is made at the end of each period.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PV,PMT,PPMT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float nper = value_get_as_float (argv[1]);
	gnm_float pmt  = value_get_as_float (argv[2]);
	gnm_float pv   = argv[3] ? value_get_as_float (argv[3]) : 0.;
	int type       = value_get_paytype (argv[4]);
	gnm_float pvif, fvifa;

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

	pvif  = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

        return value_new_float (-((pv * pvif) + pmt *
				  (1.0 + rate * type) * fvifa));
}

/***************************************************************************/

static GnmFuncHelp const help_pmt[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PMT\n"
	   "@SYNTAX=PMT(rate,nper,pv[,fv,type])\n"
	   "@DESCRIPTION="
	   "PMT returns the amount of payment for a loan based on a constant "
	   "interest rate and constant payments (each payment is equal "
	   "amount).\n"
	   "\n"
	   "@rate is the constant interest rate.\n"
	   "@nper is the overall number of payments.\n"
	   "@pv is the present value.\n"
	   "@fv is the future value.\n"
	   "@type is the type of the payment: 0 means at the end of the period "
	   "and 1 means at the beginning of the period.\n"
	   "\n"
	   "* If @fv is omitted, Gnumeric assumes it to be zero.\n"
	   "* If @type is omitted, Gnumeric assumes it to be zero.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pmt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float nper = value_get_as_float (argv[1]);
	gnm_float pv   = value_get_as_float (argv[2]);
	gnm_float fv   = argv[3] ? value_get_as_float (argv[3]) : 0;
	int type       = value_get_paytype (argv[4]);

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

        return value_new_float (calculate_pmt (rate, nper, pv, fv, type));
}

/***************************************************************************/

static GnmFuncHelp const help_ipmt[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IPMT\n"
	   "@SYNTAX=IPMT(rate,per,nper,pv[,fv,type])\n"
	   "@DESCRIPTION="
	   "IPMT calculates the amount of a payment of an annuity going "
	   "towards interest.\n"
	   "\n"
	   "Formula for IPMT is:\n"
	   "\n"
	   "IPMT(PER) = -PRINCIPAL(PER-1) * INTEREST_RATE\n"
	   "\n"
	   "where:\n"
	   "\n"
	   "PRINCIPAL(PER-1) = amount of the remaining principal from last "
	   "period\n"
	   "\n"
	   "* If @fv is omitted, it is assumed to be 0.\n"
	   "* If @type is omitted, it is assumed to be 0.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ipmt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float per  = value_get_as_float (argv[1]);
	gnm_float nper = value_get_as_float (argv[2]);
	gnm_float pv   = value_get_as_float (argv[3]);
	gnm_float fv   = argv[4] ? value_get_as_float (argv[4]) : 0;
	int type       = value_get_paytype (argv[5]);

	/*
	 * It seems that with 20 periods, a period number of 20.99 is
	 * valid in XL.
	 */
	if (per < 1 || per >= nper + 1)
                return value_new_error_NUM (ei->pos);

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

	return value_new_float (calculate_ipmt (rate, per, nper, pv, fv, type));
}

/***************************************************************************/

static GnmFuncHelp const help_ppmt[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PPMT\n"
	   "@SYNTAX=PPMT(rate,per,nper,pv[,fv,type])\n"
	   "@DESCRIPTION="
	   "PPMT calculates the amount of a payment of an annuity going "
	   "towards principal.\n"
	   "\n"
	   "Formula for it is:"
	   "\n"
	   "PPMT(per) = PMT - IPMT(per)"
	   "\n"
	   "where:\n"
	   "\n"
	   "PMT = Payment received on annuity\n"
	   "IPMT(per) = amount of interest for period @per\n"
	   "\n"
	   "* If @fv is omitted, it is assumed to be 0.\n"
	   "* If @type is omitted, it is assumed to be 0.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=IPMT,PV,FV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ppmt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float per  = value_get_as_float (argv[1]);
	gnm_float nper = value_get_as_float (argv[2]);
	gnm_float pv   = value_get_as_float (argv[3]);
	gnm_float fv   = argv[4] ? value_get_as_float (argv[4]) : 0;
	int type       = value_get_paytype (argv[5]);

	/*
	 * It seems that with 20 periods, a period number of 20.99 is
	 * valid in XL.
	 */
	if (per < 1 || per >= nper + 1)
                return value_new_error_NUM (ei->pos);

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

	{
		gnm_float pmt = calculate_pmt (rate, nper, pv, fv, type);
		gnm_float ipmt = calculate_ipmt (rate, per, nper, pv, fv, type);
		return value_new_float (pmt - ipmt);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_nper[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NPER\n"
	   "@SYNTAX=NPER(rate,pmt,pv[,fv,type])\n"
	   "@DESCRIPTION="
	   "NPER calculates number of periods of an investment based on "
	   "periodic constant payments and a constant interest rate.\n"
	   "\n"
	   "The interest rate per period is @rate, @pmt is the payment made "
	   "each period, @pv is the present value, @fv is the future value "
	   "and @type is when the payments are due. If @type = 1, payments "
	   "are due at the beginning of the period, if @type = 0, payments "
	   "are due at the end of the period.\n"
	   "\n"
	   "* If @rate <= 0, NPER returns #DIV0 error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "For example, if you deposit $10,000 in a savings account that "
	   "earns an interest rate of 6%. To calculate how many years it "
	   "will take to double your investment use NPER as follows:"
	   "\n"
	   "=NPER(0.06, 0, -10000, 20000,0)"
	   "returns 11.895661046 which indicates that you can double your "
	   "money just before the end of the 12th year."
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_nper (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float tmp;

	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float pmt  = value_get_as_float (argv[1]);
	gnm_float pv   = value_get_as_float (argv[2]);
	gnm_float fv   = argv[3] ? value_get_as_float (argv[3]) : 0;
	int type       = value_get_paytype (argv[4]);

	if (rate == 0 && pmt != 0)
		return value_new_float (-(fv + pv) / pmt);

	if (rate <= 0.0)
		return value_new_error_DIV0 (ei->pos);

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

	tmp = (pmt * (1.0 + rate * type) - fv * rate) /
	  (pv * rate + pmt * (1.0 + rate * type));
	if (tmp <= 0.0)
		return value_new_error_VALUE (ei->pos);

        return value_new_float (gnm_log (tmp) / gnm_log1p (rate));
}

/***************************************************************************/

static GnmFuncHelp const help_duration[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DURATION\n"
	   "@SYNTAX=DURATION(settlement,maturity,coup,yield,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "DURATION calculates the duration of a security.\n"
	   "\n"
	   "@settlement is the settlement date of the security.\n"
	   "@maturity is the maturity date of the security.\n"
	   "@coup The annual coupon rate as a percentage.\n"
	   "@yield The annualized yield of the security as a percentage.\n"
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, DURATION returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=G_DURATION,MDURATION")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_duration (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nSettle, nMat;
	gnm_float fCoup, fYield;
        gnm_float fNumOfCoups;
	GnmCouponConvention conv;

	conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);
	conv.eom = TRUE;

	fCoup      = value_get_as_float (argv[2]);
	fYield     = value_get_as_float (argv[3]);
	conv.freq  = value_get_freq (argv[4]);
        conv.basis = value_get_basis (argv[5], BASIS_MSRB_30_360);

        if (!datetime_value_to_g (&nSettle, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&nMat, argv[1], conv.date_conv) ||
	    !is_valid_basis (conv.basis) ||
	    !is_valid_freq (conv.freq))
		return value_new_error_NUM (ei->pos);

	fNumOfCoups = coupnum (&nSettle, &nMat, &conv);
	return get_duration (&nSettle, &nMat, fCoup, fYield, conv.freq,
			     conv.basis, fNumOfCoups);
}

/***************************************************************************/

static GnmFuncHelp const help_g_duration[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=G_DURATION\n"
	   "@SYNTAX=G_DURATION(rate,pv,fv)\n"
	   "@DESCRIPTION="
	   "G_DURATION calculates number of periods needed for an investment "
	   "to attain a desired value. This function is similar to FV and PV "
	   "with a difference that we do not need give the direction of "
	   "cash flows e.g. -100 for a cash outflow and +100 for a cash "
	   "inflow.\n"
	   "\n"
	   "* If @rate <= 0, G_DURATION returns #DIV0 error.\n"
	   "* If @fv = 0 or @pv = 0, G_DURATION returns #DIV0 error.\n"
	   "* If @fv / @pv < 0, G_DURATION returns #VALUE error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PPMT,PV,FV,DURATION,MDURATION")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_g_duration (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate, pv, fv;

	rate = value_get_as_float (argv[0]);
	pv   = value_get_as_float (argv[1]);
	fv   = value_get_as_float (argv[2]);

	if (rate <= 0)
		return value_new_error_DIV0 (ei->pos);
	else if (fv == 0 || pv == 0)
		return value_new_error_DIV0 (ei->pos);
	else if (fv / pv < 0)
		return value_new_error_VALUE (ei->pos);

        return value_new_float (gnm_log (fv / pv) / gnm_log1p (rate));

}

/***************************************************************************/

static GnmFuncHelp const help_fvschedule[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FVSCHEDULE\n"
	   "@SYNTAX=FVSCHEDULE(principal,schedule)\n"
	   "@DESCRIPTION="
	   "FVSCHEDULE returns the future value of given initial value "
	   "after applying a series of compound periodic interest rates. "
	   "The argument @principal is the present value; @schedule is an "
	   "array of interest rates to apply. The @schedule argument must "
	   "be a range of cells.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain interest "
	   "rates 0.11, 0.13, 0.09, 0.17, and 0.03.  Then\n"
	   "FVSCHEDULE(3000,A1:A5) equals 4942.7911611."
	   "\n"
	   "@SEEALSO=PV,FV")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fvschedule (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float pv, *schedule = NULL;
	GnmValue *result = NULL;
	int i, n;

	pv       = value_get_as_float (argv[0]);
	schedule = collect_floats_value (argv[1], ei->pos,
					 0, &n, &result);
	if (result)
		goto out;

	for (i = 0; i < n; i++)
		pv *= 1 + schedule[i];

	result = value_new_float (pv);
out:
	g_free (schedule);

        return result;

}

/***************************************************************************/

static GnmFuncHelp const help_euro[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EURO\n"
	   "@SYNTAX=EURO(currency)\n"
	   "@DESCRIPTION="
	   "EURO converts one Euro to a given national currency in the "
	   "European monetary union.\n"
	   "\n"
	   "@currency is one of the following:\n"
	   "\n"
	   "    ATS\t(Austria)\n"
	   "    BEF\t(Belgium)\n"
	   "    DEM\t(Germany)\n"
	   "    ESP\t(Spain)\n"
	   "    EUR\t(Euro)\n"
	   "    FIM\t(Finland)\n"
	   "    FRF\t(France)\n"
	   "    GRD\t(Greek)\n"
	   "    IEP\t(Ireland)\n"
	   "    ITL\t(Italy)\n"
	   "    LUF\t(Luxembourg)\n"
	   "    NLG\t(Netherlands)\n"
	   "    PTE\t(Portugal)\n"
	   "\n"
	   "* If the given @currency is other than one of the above, EURO "
	   "returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "EURO(\"DEM\") returns 1.95583."
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

/*
 * Returns one euro as a given national currency. On error, negative
 * value is returned.
 */
static gnm_float
one_euro (char const *str)
{
	switch (*str) {
	case 'A':
		if (strncmp ("ATS", str, 3) == 0)
			return GNM_const (13.7603);
		break;
	case 'B':
		if (strncmp ("BEF", str, 3) == 0)
			return GNM_const (40.3399);
		break;
	case 'D':
		if (strncmp ("DEM", str, 3) == 0)
			return GNM_const (1.95583);
		break;
	case 'E':
		if (strncmp ("ESP", str, 3) == 0)
			return GNM_const (166.386);
		else if (strncmp ("EUR", str, 3) == 0)
			return GNM_const (1.0);
		break;
	case 'F':
		if (strncmp ("FIM", str, 3) == 0)
			return GNM_const (5.94573);
		else if (strncmp ("FRF", str, 3) == 0)
			return GNM_const (6.55957);
		break;
	case 'G':
		if (strncmp ("GRD", str, 3) == 0)
			return GNM_const (340.75);
		break;
	case 'I':
		if (strncmp ("IEP", str, 3) == 0)
			return GNM_const (0.787564);
		else if (strncmp ("ITL", str, 3) == 0)
			return GNM_const (1936.27);
		break;
	case 'L':
		if (strncmp ("LUX", str, 3) == 0)
			return GNM_const (40.3399);
		break;
	case 'N':
		if (strncmp ("NLG", str, 3) == 0)
			return GNM_const (2.20371);
		break;
	case 'P':
		if (strncmp ("PTE", str, 3) == 0)
			return GNM_const (200.482);
		break;
	default:
		break;
	}

	return -1;
}

static GnmValue *
gnumeric_euro (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        char const *str = value_peek_string (argv[0]);
	gnm_float v    = one_euro (str);

	if (v >= 0)
		return value_new_float (v);
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_euroconvert[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EUROCONVERT\n"
	   "@SYNTAX=EUROCONVERT(n,source,target)\n"
	   "@DESCRIPTION="
	   "EUROCONVERT converts the currency value @n of @source currency "
	   "to a target currency @target. Both currencies are given as "
	   "three-letter strings using the ISO code system names.  The "
	   "following currencies are available:\n"
	   "\n"
	   "    ATS\t(Austria)\n"
	   "    BEF\t(Belgium)\n"
	   "    DEM\t(Germany)\n"
	   "    ESP\t(Spain)\n"
	   "    EUR\t(Euro)\n"
	   "    FIM\t(Finland)\n"
	   "    FRF\t(France)\n"
	   "    GRD\t(Greek)\n"
	   "    IEP\t(Ireland)\n"
	   "    ITL\t(Italy)\n"
	   "    LUF\t(Luxembourg)\n"
	   "    NLG\t(Netherlands)\n"
	   "    PTE\t(Portugal)\n"
	   "\n"
	   "* If the given @source or @target is other than one of the "
	   "above, EUROCONVERT returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "EUROCONVERT(2.1,\"DEM\",\"EUR\") returns 1.07."
	   "\n"
	   "@SEEALSO=EURO")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_euroconvert (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float c1 = one_euro (value_peek_string (argv[1]));
	gnm_float c2 = one_euro (value_peek_string (argv[2]));

	if (c1 >= 0 && c2 >= 0) {
		gnm_float n  = value_get_as_float (argv[0]);
		return value_new_float (n * c2 / c1);
	} else
		return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_price[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PRICE\n"
	   "@SYNTAX=PRICE(settle,mat,rate,yield,redemption_price,[frequency,basis])\n"
	   "@DESCRIPTION="
	   "PRICE returns price per $100 face value of a security. "
	   "This method can only be used if the security pays periodic "
	   "interest.\n"
	   "\n"
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, PRICE returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_price (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate      settlement, maturity;
        /* gnm_float a, d, e, n; */
	/* gnm_float first_term, last_term, den, base, exponent, sum; */
	gnm_float rate, yield, redemption;
	GnmCouponConvention conv;

	conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);

	rate       = value_get_as_float (argv[2]);
	yield      = value_get_as_float (argv[3]);
	redemption = value_get_as_float (argv[4]);
        conv.freq  = value_get_freq (argv[5]);
	conv.eom   = TRUE;
        conv.basis = value_get_basis (argv[6], BASIS_MSRB_30_360);

	if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (conv.basis)
	    || !is_valid_freq (conv.freq)
            || g_date_compare (&settlement, &maturity) > 0)
		return value_new_error_NUM (ei->pos);

        if (rate < 0.0 || yield < 0.0 || redemption <= 0.0)
                return value_new_error_NUM (ei->pos);

	return value_new_float (price (&settlement, &maturity, rate, yield,
				       redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_yield[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=YIELD\n"
	   "@SYNTAX=YIELD(settlement,maturity,rate,price,redemption_price,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "YIELD returns the yield on a security that pays periodic "
	   "interest.\n"
	   "\n"
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, YIELD returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        GDate settlement, maturity;
	gnm_float rate, redemption, par;
	GnmCouponConvention conv;
} gnumeric_yield_t;

static GoalSeekStatus
gnumeric_yield_f (gnm_float yield, gnm_float *y, void *user_data)
{
	gnumeric_yield_t *data = user_data;

	*y = price (&data->settlement, &data->maturity, data->rate, yield,
		    data->redemption, &data->conv)
		- data->par;
	return GOAL_SEEK_OK;
}


static GnmValue *
gnumeric_yield (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        gnm_float n;
	gnumeric_yield_t udata;

	udata.rate       = value_get_as_float (argv[2]);
	udata.par        = value_get_as_float (argv[3]);
	udata.redemption = value_get_as_float (argv[4]);
        udata.conv.freq  = value_get_freq (argv[5]);
        udata.conv.basis = value_get_basis (argv[6], BASIS_MSRB_30_360);
        udata.conv.eom   = TRUE;
        udata.conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);

	if (!datetime_value_to_g (&udata.settlement, argv[0], udata.conv.date_conv) ||
	    !datetime_value_to_g (&udata.maturity, argv[1], udata.conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (udata.conv.basis)
	    || !is_valid_freq (udata.conv.freq)
            || g_date_compare (&udata.settlement, &udata.maturity) > 0)
		return value_new_error_NUM (ei->pos);

        if (udata.rate < 0.0 || udata.par < 0.0 || udata.redemption <= 0.0)
		return value_new_error_NUM (ei->pos);

	n = coupnum (&udata.settlement, &udata.maturity, &udata.conv);
	if (n <= 1.0) {
		gnm_float a = go_coupdaybs (&udata.settlement, &udata.maturity,
					 &udata.conv);
		gnm_float d = go_coupdaysnc (&udata.settlement, &udata.maturity,
					 &udata.conv);
		gnm_float e = go_coupdays (&udata.settlement, &udata.maturity,
					 &udata.conv);

		gnm_float coeff = udata.conv.freq * e / d;
		gnm_float num = (udata.redemption / 100.0  +
				  udata.rate / udata.conv.freq)
			- (udata.par / 100.0  +  (a / e  *
						  udata.rate / udata.conv.freq));
		gnm_float den = udata.par / 100.0  +  (a / e  *  udata.rate /
							udata.conv.freq);

		return value_new_float (num / den * coeff);
	} else {
		GoalSeekData     data;
		GoalSeekStatus   status;
		gnm_float       yield0 = 0.1;

		goal_seek_initialize (&data);
		data.xmin = MAX (data.xmin, 0);
		data.xmax = MIN (data.xmax, 1000);

		/* Newton search from guess.  */
		status = goal_seek_newton (&gnumeric_yield_f, NULL,
					   &data, &udata, yield0);

		if (status != GOAL_SEEK_OK) {
			for (yield0 = 1e-10; yield0 < data.xmax; yield0 *= 2)
				goal_seek_point (&gnumeric_yield_f, &data,
						 &udata, yield0);

			/* Pray we got both sides of the root.  */
			status = goal_seek_bisection (&gnumeric_yield_f, &data,
						      &udata);
		}

		if (status != GOAL_SEEK_OK)
			return value_new_error_NUM (ei->pos);
		return value_new_float (data.root);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_yielddisc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=YIELDDISC\n"
	   "@SYNTAX=YIELDDISC(settlement,maturity,pr,redemption[,basis])\n"
	   "@DESCRIPTION="
	   "YIELDDISC calculates the annual yield of a security that is "
	   "discounted.\n"
	   "\n"
	   "@settlement is the settlement date of the security.  "
	   "@maturity is the maturity date of the security. "
	   "@pr is the price per $100 face value of the security. "
	   "@redemption is the redemption value per $100 face value. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, YIELDDISC returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_yielddisc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     settlement, maturity;
	gnm_float fPrice, fRedemp;
	gint      basis;
	gnm_float ret, yfrac;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	fPrice     = value_get_as_float (argv[2]);
	fRedemp    = value_get_as_float (argv[3]);
        basis      = value_get_basis (argv[4], BASIS_MSRB_30_360);

        if (!is_valid_basis (basis) ||
	    !datetime_value_to_g (&settlement, argv[0], date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], date_conv))
		return value_new_error_NUM (ei->pos);

	if (fRedemp <= 0 ||
	    fPrice <= 0 ||
	    g_date_compare (&settlement, &maturity) >= 0)
		return value_new_error_NUM (ei->pos);

        ret = (fRedemp / fPrice) - 1;
	yfrac = yearfrac (&settlement, &maturity, basis);

	return value_new_float (ret / yfrac);
}

/***************************************************************************/

static GnmFuncHelp const help_yieldmat[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=YIELDMAT\n"
	   "@SYNTAX=YIELDMAT(settlement,maturity,issue,rate,pr[,basis])\n"
	   "@DESCRIPTION="
	   "YIELDMAT calculates the annual yield of a security for which "
	   "the interest is paid at maturity date.\n"
	   "\n"
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@issue is the issue date of the security. "
	   "@rate is the interest rate set to the security. "
	   "@pr is the price per $100 face value of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_yieldmat (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nSettle, nMat, nIssue;
	gnm_float fRate, fPrice;
	gint      basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	fRate      = value_get_as_float (argv[3]);
	fPrice     = value_get_as_float (argv[4]);
        basis      = value_get_basis (argv[5], BASIS_MSRB_30_360);

        if (!is_valid_basis (basis) ||
	    fRate < 0 ||
	    !datetime_value_to_g (&nSettle, argv[0], date_conv) ||
	    !datetime_value_to_g (&nMat, argv[1], date_conv) ||
	    !datetime_value_to_g (&nIssue, argv[2], date_conv))
		return value_new_error_NUM (ei->pos);

	return get_yieldmat (&nSettle, &nMat, &nIssue, fRate, fPrice, basis);
}

/***************************************************************************/

static GnmFuncHelp const help_oddfprice[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ODDFPRICE\n"
	   "@SYNTAX=ODDFPRICE(settlement,maturity,issue,first_coupon,rate,yld,redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDFPRICE returns the price per $100 face value of a security. "
	   "The security should have an odd short or long first period.\n"
	   "\n"
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@issue is the issue date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, ODDFPRICE returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
date_ratio (GDate const *d1, const GDate *d2, const GDate *d3,
	    GnmCouponConvention const *conv)
{
	GDate next_coupon, prev_coupon;
	gnm_float res;

	if (!g_date_valid (d1) || !g_date_valid (d2) || !g_date_valid (d3))
		return gnm_nan;

	go_coup_cd (&next_coupon, d1, d3, conv->freq, conv->eom, TRUE);
	go_coup_cd (&prev_coupon, d1, d3, conv->freq, conv->eom, FALSE);

	if (!g_date_valid (&prev_coupon) || !g_date_valid (&next_coupon))
		return gnm_nan;

	if (g_date_compare (&next_coupon, d2) >= 0)
		return days_between_basis (d1, d2, conv->basis) /
			go_coupdays (&prev_coupon, &next_coupon, conv);

	res = days_between_basis (d1, &next_coupon, conv->basis) /
		go_coupdays (&prev_coupon, &next_coupon, conv);
	while (1) {
		prev_coupon = next_coupon;
		gnm_date_add_months (&next_coupon, 12 / conv->freq);
		if (!g_date_valid (&next_coupon))
			return gnm_nan;
		if (g_date_compare (&next_coupon, d2) >= 0) {
			res += days_between_basis (&prev_coupon, d2, conv->basis) /
				go_coupdays (&prev_coupon, &next_coupon, conv);
			return res;
		}
		res += 1;
	}
}

static gnm_float
calc_oddfprice (const GDate *settlement, const GDate *maturity,
		const GDate *issue, const GDate *first_coupon,
		gnm_float rate, gnm_float yield, gnm_float redemption,
		GnmCouponConvention const *conv)

{
	gnm_float a = days_between_basis (issue, settlement, conv->basis);
	gnm_float ds = days_between_basis (settlement, first_coupon, conv->basis);
	gnm_float df = days_between_basis (issue, first_coupon, conv->basis);
	gnm_float e = go_coupdays (settlement, maturity, conv);
	int n = (int)coupnum (settlement, maturity, conv);
	gnm_float scale = 100.0 * rate / conv->freq;
	gnm_float f = 1.0 + yield / conv->freq;
	gnm_float sum, term1, term2;

	if (ds > e) {
		/* Odd-long corrections.  */
		switch (conv->basis) {
		case BASIS_MSRB_30_360:
		case BASIS_30E_360: {
			int cdays = days_between_basis (first_coupon, maturity, conv->basis);
			n = 1 + (int)gnm_ceil (cdays / e);
			break;
		}

		default: {
			GDate d = *first_coupon;

			for (n = 0; 1; n++) {
				GDate prev_date = d;
				gnm_date_add_months (&d, 12 / conv->freq);
				if (g_date_compare (&d, maturity) >= 0) {
					n += (int)gnm_ceil (days_between_basis (&prev_date, maturity, conv->basis) /
							    go_coupdays (&prev_date, &d, conv))
						+ 1;
					break;
				}
			}
			a = e * date_ratio (issue, settlement, first_coupon, conv);
			ds = e * date_ratio (settlement, first_coupon, first_coupon, conv);
			df = e * date_ratio (issue, first_coupon, first_coupon, conv);
		}
		}
	}

	term1 = redemption / gnm_pow (f, n - 1.0 + ds / e);
	term2 = (df / e) / gnm_pow (f, ds / e);
	sum = gnm_pow (f, -ds / e) *
		(gnm_pow (f, -n) - 1 / f) / (1 / f - 1);

	return term1 + scale * (term2 + sum - a / e);
}



static GnmValue *
gnumeric_oddfprice (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     settlement, maturity, issue, first_coupon;
	gnm_float rate, yield, redemption;
	GnmCouponConvention conv;

	rate       = value_get_as_float (argv[4]);
	yield      = value_get_as_float (argv[5]);
	redemption = value_get_as_float (argv[6]);

        conv.eom   = TRUE;
        conv.freq  = value_get_freq (argv[7]);
        conv.basis = value_get_basis (argv[8], BASIS_MSRB_30_360);
	conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);

	if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv) ||
	    !datetime_value_to_g (&issue, argv[2], conv.date_conv) ||
	    !datetime_value_to_g (&first_coupon, argv[3], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (conv.basis)
	    || !is_valid_freq (conv.freq)
            || g_date_compare (&issue, &settlement) > 0
	    || g_date_compare (&settlement, &first_coupon) > 0
	    || g_date_compare (&first_coupon, &maturity) > 0)
		return value_new_error_NUM (ei->pos);

        if (rate < 0.0 || yield < 0.0 || redemption <= 0.0)
                return value_new_error_NUM (ei->pos);

	return value_new_float
		(calc_oddfprice
		 (&settlement, &maturity, &issue, &first_coupon,
		  rate, yield, redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_oddfyield[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ODDFYIELD\n"
	   "@SYNTAX=ODDFYIELD(settlement,maturity,issue,first_coupon,rate,"
	   "pr,redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDFYIELD calculates the yield of a security having an odd first "
	   "period.\n"
	   "\n"
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, ODDFYIELD returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

struct gnumeric_oddyield_f {
        GDate settlement, maturity, issue, first_coupon;
	gnm_float rate, price, redemption;
	GnmCouponConvention conv;
};

static GoalSeekStatus
gnumeric_oddyield_f (gnm_float yield, gnm_float *y, void *user_data)
{
	struct gnumeric_oddyield_f *data = user_data;

	*y = calc_oddfprice (&data->settlement, &data->maturity,
			     &data->issue, &data->first_coupon,
			     data->rate, yield,
			     data->redemption, &data->conv)
		- data->price;
	return GOAL_SEEK_OK;
}

static GnmValue *
gnumeric_oddfyield (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	struct gnumeric_oddyield_f udata;
	GoalSeekData data;
	GoalSeekStatus status;
	gnm_float yield0 = 0.1;

	udata.rate       = value_get_as_float (argv[4]);
	udata.price      = value_get_as_float (argv[5]);
	udata.redemption = value_get_as_float (argv[6]);

        udata.conv.eom   = TRUE;
        udata.conv.freq  = value_get_freq (argv[7]);
        udata.conv.basis = value_get_basis (argv[8], BASIS_MSRB_30_360);
	udata.conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);

	if (!datetime_value_to_g (&udata.settlement, argv[0], udata.conv.date_conv) ||
	    !datetime_value_to_g (&udata.maturity, argv[1], udata.conv.date_conv) ||
	    !datetime_value_to_g (&udata.issue, argv[2], udata.conv.date_conv) ||
	    !datetime_value_to_g (&udata.first_coupon, argv[3], udata.conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (udata.conv.basis)
	    || !is_valid_freq (udata.conv.freq)
            || g_date_compare (&udata.issue, &udata.settlement) > 0
	    || g_date_compare (&udata.settlement, &udata.first_coupon) > 0
	    || g_date_compare (&udata.first_coupon, &udata.maturity) > 0)
		return value_new_error_NUM (ei->pos);

        if (udata.rate < 0.0 || udata.price <= 0.0 || udata.redemption <= 0.0)
                return value_new_error_NUM (ei->pos);

	goal_seek_initialize (&data);
	data.xmin = MAX (data.xmin, 0);
	data.xmax = MIN (data.xmax, 1000);

	/* Newton search from guess.  */
	status = goal_seek_newton (&gnumeric_oddyield_f, NULL,
				   &data, &udata, yield0);

	if (status != GOAL_SEEK_OK) {
		for (yield0 = 1e-10; yield0 < data.xmax; yield0 *= 2)
			goal_seek_point (&gnumeric_oddyield_f, &data,
					 &udata, yield0);

		/* Pray we got both sides of the root.  */
		status = goal_seek_bisection (&gnumeric_oddyield_f, &data,
					      &udata);
	}

	if (status != GOAL_SEEK_OK)
		return value_new_error_NUM (ei->pos);

	return value_new_float (data.root);
}

/***************************************************************************/

static GnmFuncHelp const help_oddlprice[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ODDLPRICE\n"
	   "@SYNTAX=ODDLPRICE(settlement,maturity,last_interest,rate,yld,"
	   "redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDLPRICE calculates the price per $100 face value of a security "
	   "that has an odd last coupon period.\n"
	   "\n"
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, ODDLPRICE returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
calc_oddlprice (const GDate *settlement, const GDate *maturity,
		const GDate *last_interest,
		gnm_float rate, gnm_float yield, gnm_float redemption,
		GnmCouponConvention *conv)
{
	GDate d = *last_interest;
	gnm_float x1, x2, x3;

	do {
		gnm_date_add_months (&d, 12 / conv->freq);
	} while (g_date_valid (&d) && g_date_compare (&d, maturity) < 0);

        x1 = date_ratio (last_interest, settlement, &d, conv);
        x2 = date_ratio (last_interest, maturity, &d, conv);
        x3 = date_ratio (settlement, maturity, &d, conv);

        return (redemption * conv->freq +
		100 * rate * (x2 - x1 * (1 + yield * x3 / conv->freq))) /
		(yield * x3 + conv->freq);
}


static GnmValue *
gnumeric_oddlprice (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     settlement, maturity, last_interest;
	gnm_float rate, yield, redemption;
	GnmCouponConvention conv;

	rate       = value_get_as_float (argv[3]);
	yield      = value_get_as_float (argv[4]);
	redemption = value_get_as_float (argv[5]);

        conv.eom   = TRUE;
        conv.freq  = value_get_freq (argv[6]);
        conv.basis = value_get_basis (argv[7], BASIS_MSRB_30_360);
	conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);

	if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv) ||
	    !datetime_value_to_g (&last_interest, argv[2], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (conv.basis) ||
	    !is_valid_freq (conv.freq) ||
            g_date_compare (&settlement, &maturity) > 0 ||
	    g_date_compare (&last_interest, &settlement) > 0)
		return value_new_error_NUM (ei->pos);

        if (rate < 0.0 || yield < 0.0 || redemption <= 0.0)
                return value_new_error_NUM (ei->pos);

	return value_new_float
		(calc_oddlprice
		 (&settlement, &maturity, &last_interest,
		  rate, yield, redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_oddlyield[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ODDLYIELD\n"
	   "@SYNTAX=ODDLYIELD(settlement,maturity,last_interest,rate,pr,"
	   "redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDLYIELD calculates the yield of a security having an odd last "
	   "period.\n"
	   "\n"
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, or 4, ODDLYIELD returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};


static gnm_float
calc_oddlyield (GDate const *settlement, GDate const *maturity,
		GDate const *last_interest,
		gnm_float rate, gnm_float price, gnm_float redemption,
		GnmCouponConvention const *conv)
{
	GDate d = *last_interest;
	gnm_float x1, x2, x3;

	do {
		gnm_date_add_months (&d, 12 / conv->freq);
	} while (g_date_valid (&d) && g_date_compare (&d, maturity) < 0);

        x1 = date_ratio (last_interest, settlement, &d, conv);
        x2 = date_ratio (last_interest, maturity, &d, conv);
        x3 = date_ratio (settlement, maturity, &d, conv);

        return (conv->freq * (redemption - price) + 100 * rate * (x2 - x1)) /
		(x3 * price + 100 * rate * x1 * x3 / conv->freq);
}


static GnmValue *
gnumeric_oddlyield (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     settlement, maturity, last_interest;
	gnm_float rate, price, redemption;
	GnmCouponConvention conv;

	rate       = value_get_as_float (argv[3]);
	price      = value_get_as_float (argv[4]);
	redemption = value_get_as_float (argv[5]);

        conv.eom   = TRUE;
        conv.freq  = value_get_freq (argv[6]);
        conv.basis = value_get_basis (argv[7], BASIS_MSRB_30_360);
	conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);

	if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv) ||
	    !datetime_value_to_g (&last_interest, argv[2], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (conv.basis) ||
	    !is_valid_freq (conv.freq) ||
            g_date_compare (&settlement, &maturity) > 0 ||
	    g_date_compare (&last_interest, &settlement) > 0)
		return value_new_error_NUM (ei->pos);

        if (rate < 0.0 || price <= 0.0 || redemption <= 0.0)
                return value_new_error_NUM (ei->pos);

	return value_new_float
		(calc_oddlyield
		 (&settlement, &maturity, &last_interest,
		  rate, price, redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_amordegrc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=AMORDEGRC\n"
	   "@SYNTAX=AMORDEGRC(cost,purchase_date,first_period,salvage,period,rate[,basis])\n"
	   "@DESCRIPTION="
	   "AMORDEGRC: Calculates depreciation for each accounting period using "
	   "French accounting conventions.   Assets purchased in the middle of "
	   "a period take prorated depreciation into account.  This is similar "
	   "to AMORLINC, except that a depreciation coefficient is applied in "
	   "the calculation depending on the life of the assets.\n"
	   "Named for AMORtissement DEGRessif Comptabilite\n"
	   "\n"
	   "@cost The value of the asset.\n"
	   "@purchase_date The date the asset was purchased.\n"
	   "@first_period The end of the first period.\n"
	   "@salvage Asset value at maturity.\n"
	   "@period The length of accounting periods.\n"
	   "@rate rate of depreciation as a percentage.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "AMORDEGRC(2400,DATE(1998,8,19),DATE(1998,12,30),300,1,0.14,1) = 733\n"
	   "\n"
	   "@SEEALSO=AMORLINC")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_amordegrc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nDate, nFirstPer;
	gnm_float fRestVal, fRate, fCost;
	gint      basis, nPer;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	fCost      = value_get_as_float (argv[0]);
	fRestVal   = value_get_as_float (argv[3]);
        nPer       = value_get_as_int (argv[4]);
	fRate      = value_get_as_float (argv[5]);
        basis      = value_get_basis (argv[6], BASIS_MSRB_30_360);

        if (!is_valid_basis (basis) ||
	    fRate < 0 ||
	    !datetime_value_to_g (&nDate, argv[1], date_conv) ||
	    !datetime_value_to_g (&nFirstPer, argv[2], date_conv))
		return value_new_error_NUM (ei->pos);

	return get_amordegrc (fCost, &nDate, &nFirstPer,
			      fRestVal, nPer, fRate, basis);
}

/***************************************************************************/



static GnmFuncHelp const help_amorlinc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=AMORLINC\n"
	   "@SYNTAX=AMORLINC(cost,purchase_date,first_period,salvage,period,rate[,basis])\n"
	   "@DESCRIPTION="
	   "AMORLINC: Calculates depreciation for each accounting period using "
	   "French accounting conventions.   Assets purchased in the middle of "
	   "a period take prorated depreciation into account.\n"
	   "Named for AMORtissement LINeaire Comptabilite.\n"
	   "\n"
	   "@cost The value of the asset.\n"
	   "@purchase_date The date the asset was purchased.\n"
	   "@first_period The end of the first period.\n"
	   "@salvage Asset value at maturity.\n"
	   "@period The length of accounting periods.\n"
	   "@rate rate of depreciation as a percentage.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  US 30/360\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "\n"
	   "* If @basis is omitted, US 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 4, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "AMORLINC(2400,DATE(1998,8,19),DATE(1998,12,31),300,1,0.15,1) = 360\n"
	   "\n"
	   "@SEEALSO=AMORDEGRC")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_amorlinc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nDate, nFirstPer;
	gnm_float fCost, fRestVal, fRate;
	gint      nPer, basis;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	fCost      = value_get_as_float (argv[0]);
	fRestVal   = value_get_as_float (argv[3]);
        nPer       = value_get_as_int (argv[4]);
	fRate      = value_get_as_float (argv[5]);
        basis      = value_get_basis (argv[6], BASIS_MSRB_30_360);

        if (!is_valid_basis (basis) ||
	    fRate < 0 ||
	    !datetime_value_to_g (&nDate, argv[1], date_conv) ||
	    !datetime_value_to_g (&nFirstPer, argv[2], date_conv))
		return value_new_error_NUM (ei->pos);

	return get_amorlinc (fCost, &nDate, &nFirstPer,
			     fRestVal, nPer, fRate, basis);
}

/***************************************************************************/

static GnmFuncHelp const help_coupdaybs[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COUPDAYBS\n"
	   "@SYNTAX=COUPDAYBS(settlement,maturity,frequency[,basis,eom])\n"
	   "@DESCRIPTION="
	   "COUPDAYBS returns the number of days from the beginning of the "
	   "coupon period to the settlement date.\n"
	   "\n"
	   "@settlement is the settlement date of the security.\n"
	   "@maturity is the maturity date of the security.\n"
	   "@frequency is the number of coupon payments per year.\n"
	   "@eom = TRUE handles end of month maturity dates special.\n"
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly, "
	   "6 = bimonthly, 12 = monthly.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  MSRB 30/360 (MSRB Rule G33 (e))\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "  5  European+ 30/360\n"
	   "\n"
	   "(See the gnumeric manual for a detailed description of these "
	   "bases).\n"
	   "\n"
	   "* If @frequency is invalid, COUPDAYBS returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, MSRB 30/360 is applied.\n"
	   "* If @basis is invalid, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COUPDAYBS (DATE(2002,11,29),DATE(2004,2,29),4,0) = 89\n"
	   "COUPDAYBS (DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE) = 0\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupdaybs (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, go_coupdaybs);
}

/***************************************************************************/

static GnmFuncHelp const help_coupdays[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COUPDAYS\n"
	   "@SYNTAX=COUPDAYS(settlement,maturity,frequency[,basis,eom])\n"
	   "@DESCRIPTION="
	   "COUPDAYS returns the number of days in the coupon period of the "
	   "settlement date.\n"
	   "\n"
	   "@settlement is the settlement date of the security.\n"
	   "@maturity is the maturity date of the security.\n"
	   "@frequency is the number of coupon payments per year.\n"
	   "@eom = TRUE handles end of month maturity dates special.\n"
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly, "
	   "6 = bimonthly, 12 = monthly.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  MSRB 30/360 (MSRB Rule G33 (e))\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "  5  European+ 30/360\n"
	   "\n"
	   "(See the gnumeric manual for a detailed description of these "
	   "bases).\n"
	   "\n"
	   "* If @frequency is invalid, COUPDAYS returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, MSRB 30/360 is applied.\n"
	   "* If @basis is invalid, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COUPDAYS (DATE(2002,11,29),DATE(2004,2,29),4,0) = 90\n"
	   "COUPDAYS (DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE) = 90\n"
	   "COUPDAYS (DATE(2002,11,29),DATE(2004,2,29),4,1,FALSE) = 91\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupdays (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, go_coupdays);
}

/***************************************************************************/

static GnmFuncHelp const help_coupdaysnc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COUPDAYSNC\n"
	   "@SYNTAX=COUPDAYSNC(settlement,maturity,frequency[,basis,eom])\n"
	   "@DESCRIPTION="
	   "COUPDAYSNC returns the number of days from the settlement date "
	   "to the next coupon date.\n"
	   "\n"
	   "@settlement is the settlement date of the security.\n"
	   "@maturity is the maturity date of the security.\n"
	   "@frequency is the number of coupon payments per year.\n"
	   "@eom = TRUE handles end of month maturity dates special.\n"
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly, "
	   "6 = bimonthly, 12 = monthly.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  MSRB 30/360 (MSRB Rule G33 (e))\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "  5  European+ 30/360\n"
	   "\n"
	   "(See the gnumeric manual for a detailed description of these "
	   "bases).\n"
	   "\n"
	   "* If @frequency is invalid, COUPDAYSNC returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, MSRB 30/360 is applied.\n"
	   "* If @basis is invalid, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COUPDAYSNC (DATE(2002,11,29),DATE(2004,2,29),4,0) = 1\n"
	   "COUPDAYSNC (DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE) = 89\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupdaysnc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, go_coupdaysnc);
}

/***************************************************************************/

static GnmFuncHelp const help_coupncd[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COUPNCD\n"
	   "@SYNTAX=COUPNCD(settlement,maturity,frequency[,basis,eom])\n"
	   "@DESCRIPTION="
	   "COUPNCD returns the coupon date following settlement.\n"
	   "\n"
	   "@settlement is the settlement date of the security.\n"
	   "@maturity is the maturity date of the security.\n"
	   "@frequency is the number of coupon payments per year.\n"
	   "@eom = TRUE handles end of month maturity dates special.\n"
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly, "
	   "6 = bimonthly, 12 = monthly.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  MSRB 30/360 (MSRB Rule G33 (e))\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "  5  European+ 30/360\n"
	   "\n"
	   "(See the gnumeric manual for a detailed description of these "
	   "bases).\n"
	   "\n"
	   "* If @frequency is invalid, COUPNCD returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, MSRB 30/360 is applied.\n"
	   "* If @basis is invalid, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COUPNCD (DATE(2002,11,29),DATE(2004,2,29),4,0) = 30-Nov-2002\n"
	   "COUPNCD (DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE) = 28-Feb-2003\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupncd (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue *res = func_coup (ei, argv, coupncd);
	value_set_fmt (res, go_format_default_date ());
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_couppcd[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COUPPCD\n"
	   "@SYNTAX=COUPPCD(settlement,maturity,frequency[,basis,eom])\n"
	   "@DESCRIPTION="
	   "COUPPCD returns the coupon date preceding settlement.\n"
	   "\n"
	   "@settlement is the settlement date of the security.\n"
	   "@maturity is the maturity date of the security.\n"
	   "@frequency is the number of coupon payments per year.\n"
	   "@eom = TRUE handles end of month maturity dates special.\n"
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly, "
	   "6 = bimonthly, 12 = monthly.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  MSRB 30/360 (MSRB Rule G33 (e))\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "  5  European+ 30/360\n"
	   "\n"
	   "(See the gnumeric manual for a detailed description of these "
	   "bases).\n"
	   "\n"
	   "* If @frequency is invalid, COUPPCD returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, MSRB 30/360 is applied.\n"
	   "* If @basis is invalid, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COUPPCD (DATE(2002,11,29),DATE(2004,2,29),4,0) = 31-Aug-2002\n"
	   "COUPPCD (DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE) = "
	   "29-Nov-2002\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_couppcd (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue *res = func_coup (ei, argv, couppcd);
	value_set_fmt (res, go_format_default_date ());
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_coupnum[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COUPNUM\n"
	   "@SYNTAX=COUPNUM(settlement,maturity,frequency[,basis,eom])\n"
	   "@DESCRIPTION="
	   "COUPNUM returns the numbers of coupons to be paid between "
	   "the settlement and maturity dates, rounded up.\n"
	   "\n"
	   "@settlement is the settlement date of the security.\n"
	   "@maturity is the maturity date of the security.\n"
	   "@frequency is the number of coupon payments per year.\n"
	   "@eom = TRUE handles end of month maturity dates special.\n"
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "6 = bimonthly, 12 = monthly.\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  MSRB 30/360 (MSRB Rule G33 (e))\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "  5  European+ 30/360\n"
	   "\n"
	   "* If @frequency is other than 1, 2, 4, 6 or 12, COUPNUM returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, MSRB 30/360 is applied.\n"
	   "* If @basis is not in between 0 and 5, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COUPNUM (DATE(2002,11,29),DATE(2004,2,29),4,0) = 6\n"
	   "COUPNUM (DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE) = 5\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupnum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, coupnum);
}

/***************************************************************************/

static GnmFuncHelp const help_cumipmt[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CUMIPMT\n"
	   "@SYNTAX=CUMIPMT(rate,nper,pv,start_period,end_period,type)\n"
	   "@DESCRIPTION="
	   "CUMIPMT returns the cumulative interest paid on a loan between "
	   "@start_period and @end_period.\n"
	   "\n"
	   "* If @rate <= 0, CUMIPMT returns #NUM! error.\n"
	   "* If @nper <= 0, CUMIPMT returns #NUM! error.\n"
	   "* If @pv <= 0, CUMIPMT returns #NUM! error.\n"
	   "* If @start_period < 1, CUMIPMT returns #NUM! error.\n"
	   "* If @end_period < @start_period, CUMIPMT returns #NUM! error.\n"
	   "* If @end_period > @nper, CUMIPMT returns #NUM! error.\n"
	   "* If @type <> 0 and @type <> 1, CUMIPMT returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cumipmt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float fRate, fVal;
	gint       nNumPeriods, nStartPer, nEndPer, nPayType;
	GnmValue      *result;

	fRate       = value_get_as_float (argv[0]);
        nNumPeriods = value_get_as_int (argv[1]);
	fVal        = value_get_as_float (argv[2]);
        nStartPer   = value_get_as_int (argv[3]);
        nEndPer     = value_get_as_int (argv[4]);
        nPayType    = value_get_paytype (argv[5]);

        if ( nStartPer < 1 || nEndPer < nStartPer || fRate <= 0
	     || nEndPer > nNumPeriods || nNumPeriods <= 0
	     || fVal <= 0 || !is_valid_paytype (nPayType) ) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	result = get_cumipmt (fRate, nNumPeriods, fVal, nStartPer, nEndPer,
			      nPayType);

 out:
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_cumprinc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CUMPRINC\n"
	   "@SYNTAX=CUMPRINC(rate,nper,pv,start_period,end_period,type)\n"
	   "@DESCRIPTION="
	   "CUMPRINC returns the cumulative principal paid on a loan between "
	   "@start_period and @end_period.\n"
	   "\n"
	   "* If @rate <= 0, CUMPRINC returns #NUM! error.\n"
	   "* If @nper <= 0, CUMPRINC returns #NUM! error.\n"
	   "* If @pv <= 0, CUMPRINC returns #NUM! error.\n"
	   "* If @start_period < 1, CUMPRINC returns #NUM! error.\n"
	   "* If @end_period < @start_period, CUMPRINC returns #NUM! error.\n"
	   "* If @end_period > @nper, CUMPRINC returns #NUM! error.\n"
	   "* If @type <> 0 and @type <> 1, CUMPRINC returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cumprinc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float fRate, fVal;
	gint       nNumPeriods, nStartPer, nEndPer, nPayType;
	GnmValue      *result;

	fRate       = value_get_as_float (argv[0]);
        nNumPeriods = value_get_as_int (argv[1]);
	fVal        = value_get_as_float (argv[2]);
        nStartPer   = value_get_as_int (argv[3]);
        nEndPer     = value_get_as_int (argv[4]);
        nPayType    = value_get_paytype (argv[5]);

        if ( nStartPer < 1 || nEndPer < nStartPer || fRate <= 0
	     || nEndPer > nNumPeriods || nNumPeriods <= 0
	     || fVal <= 0 || !is_valid_paytype (nPayType)) {
		result = value_new_error_NUM (ei->pos);
		goto out;
	}

	result = get_cumprinc (fRate, nNumPeriods, fVal, nStartPer, nEndPer,
			       nPayType);

 out:
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_mduration[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MDURATION\n"
	   "@SYNTAX=MDURATION(settlement,maturity,coupon,yield,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "MDURATION returns the Macauley duration for a security with par "
	   "value 100.\n"
	   "\n"
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "  0  MSRB 30/360 (MSRB Rule G33 (e))\n"
	   "  1  actual days/actual days\n"
	   "  2  actual days/360\n"
	   "  3  actual days/365\n"
	   "  4  European 30/360\n"
	   "  5  European+ 30/360\n"
	   "\n"
	   "* If @settlement or @maturity are not valid dates, MDURATION "
	   "returns #NUM! error.\n"
	   "* If @frequency is other than 1, 2, or 4, MDURATION returns #NUM! "
	   "error.\n"
	   "* If @basis is omitted, MSRB 30/360 is applied.\n"
	   "* If @basis is invalid, #NUM! error is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DURATION,G_DURATION")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_mduration (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nSettle, nMat;
	gnm_float fCoup, fYield;
        gnm_float fNumOfCoups;
	GnmCouponConvention conv;

	conv.date_conv = workbook_date_conv (ei->pos->sheet->workbook);
	conv.eom = TRUE;

	fCoup      = value_get_as_float (argv[2]);
	fYield     = value_get_as_float (argv[3]);
	conv.freq  = value_get_freq (argv[4]);
        conv.basis = value_get_basis (argv[5], BASIS_MSRB_30_360);
        conv.eom   = FALSE;

        if (!is_valid_basis (conv.basis) ||
	    !is_valid_freq (conv.freq) ||
	    !datetime_value_to_g (&nSettle, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&nMat, argv[1], conv.date_conv))
		return value_new_error_NUM (ei->pos);

	fNumOfCoups = coupnum (&nSettle, &nMat, &conv);
	return get_mduration (&nSettle, &nMat, fCoup, fYield, conv.freq,
			      conv.basis, fNumOfCoups);
}

/***************************************************************************/

static GnmFuncHelp const help_vdb[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=VDB\n"
	   "@SYNTAX=VDB(cost,salvage,life,start_period,end_period[,factor,switch])\n"
	   "@DESCRIPTION="
	   "VDB calculates the depreciation of an asset for a given period "
	   "or partial period using the double-declining balance method.\n"
	   "\n"
	   "* If @start_period < 0, VDB returns #NUM! error.\n"
	   "* If @start_period > @end_period, VDB returns #NUM! error.\n"
	   "* If @end_period > @life, VDB returns #NUM! error.\n"
	   "* If @cost < 0, VDB returns #NUM! error.\n"
	   "* If @salvage > @cost, VDB returns #NUM! error.\n"
	   "* If @factor <= 0, VDB returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DB")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_vdb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float cost, salvage, life, factor, start_period, end_period;
	gboolean   bflag;

	cost         = value_get_as_float (argv[0]);
	salvage      = value_get_as_float (argv[1]);
	life         = value_get_as_float (argv[2]);
	start_period = value_get_as_float (argv[3]);
	end_period   = value_get_as_float (argv[4]);
	factor       = argv[5] ? value_get_as_float (argv[5]) : 2;
        bflag        = argv[6] ? value_get_as_int (argv[6]) : 0;

        if ( start_period < 0 || end_period < start_period
	     || end_period > life || cost < 0 || salvage > cost
	     || factor <= 0)
		return value_new_error_NUM (ei->pos);

	return get_vdb (cost, salvage, life, start_period, end_period, factor,
			bflag);
}

/***************************************************************************/

GnmFuncDescriptor const financial_functions[] = {
	{ "accrint", "ffffff|f", "issue,first_interest,settlement,rate,par,frequency,basis",
	  help_accrint, gnumeric_accrint, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "accrintm", "fff|ff", "issue,maturity,rate,par,basis",
	  help_accrintm, gnumeric_accrintm, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "amordegrc", "fffffff", "cost,purchase_date,first_period,salvage,period,rate,basis",
	  help_amordegrc, gnumeric_amordegrc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "amorlinc", "fffffff", "cost,purchase_date,first_period,salvage,period,rate,basis",
	  help_amorlinc, gnumeric_amorlinc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupdaybs", "fff|fb", "settlement,maturity,frequency,basis,eom",
	  help_coupdaybs, gnumeric_coupdaybs, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupdays", "fff|fb", "settlement,maturity,frequency,basis,eom",
	  help_coupdays, gnumeric_coupdays, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupdaysnc", "fff|fb", "settlement,maturity,frequency,basis,eom",
	  help_coupdaysnc, gnumeric_coupdaysnc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupncd", "fff|fb", "settlement,maturity,frequency,basis,eom",
	  help_coupncd, gnumeric_coupncd, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupnum", "fff|fb", "settlement,maturity,frequency,basis,eom",
	  help_coupnum, gnumeric_coupnum, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "couppcd", "fff|fb", "settlement,maturity,frequency,basis,eom",
	  help_couppcd, gnumeric_couppcd, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "cumipmt", "ffffff", "rate,nper,pv,start_period,end_period,type",
	  help_cumipmt, gnumeric_cumipmt, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "cumprinc", "ffffff", "rate,nper,pv,start_period,end_period,type",
	  help_cumprinc, gnumeric_cumprinc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "db", "ffff|f", "cost,salvage,life,period,month",
	  help_db, gnumeric_db, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ddb", "ffff|f", "cost,salvage,life,period,factor",
	  help_ddb, gnumeric_ddb, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "disc", "ffff|f", "settlement,maturity,pr,redemption,basis",
	  help_disc, gnumeric_disc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dollarde", "ff", "fractional_dollar,fraction",
	  help_dollarde, gnumeric_dollarde, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dollarfr", "ff", "decimal_dollar,fraction",
	  help_dollarfr, gnumeric_dollarfr, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "duration", "fffff|f", "settlement,maturity,coup,yield,frequency,basis",
	  help_duration, gnumeric_duration, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "effect", "ff", "rate,nper",
	  help_effect,	  gnumeric_effect, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "euro", "s", "currency",
	  help_euro,	  gnumeric_euro, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{ "euroconvert", "fss", "n,source,target",
	  help_euroconvert, gnumeric_euroconvert, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fv", "fff|ff", "rate,nper,pmt,pv,type",
	  help_fv,	  gnumeric_fv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fvschedule", "fA", "pv,schedule",
	  help_fvschedule, gnumeric_fvschedule, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "g_duration", "fff", "rate,pv,fv",
	  help_g_duration, gnumeric_g_duration, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{ "intrate", "ffff|f", "settlement,maturity,investment,redemption,basis",
	  help_intrate,  gnumeric_intrate, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ipmt", "ffff|ff", "rate,per,nper,pv,fv,type",
	  help_ipmt,	  gnumeric_ipmt, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "irr", "A|f", "values,guess",
	  help_irr,	  gnumeric_irr, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ispmt", "ffff", "rate,per,nper,pv",
	  help_ispmt,	gnumeric_ispmt, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mduration", "fffff|f", "settlement,maturify,coupon,yield,frequency,basis",
	  help_mduration, gnumeric_mduration, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mirr", "Aff", "values,finance_rate,reinvest_rate",
	  help_mirr,	  gnumeric_mirr, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "nominal", "ff", "rate,nper",
	  help_nominal,  gnumeric_nominal, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "nper", "fff|ff", "rate,pmt,pv,fv,type",
	  help_nper,	  gnumeric_nper, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "npv", NULL, N_("rate,values"),
	  help_npv,	  NULL, gnumeric_npv, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "oddfprice", "fffffffff", "settlement,maturity,issue,first_coupon,rate,yld,redemption,frequency,basis",
	  help_oddfprice,  gnumeric_oddfprice, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "oddfyield", "fffffffff", "settlement,maturity,issue,first_coupon,rate,pr,redemption,frequency,basis",
	  help_oddfyield,  gnumeric_oddfyield, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "oddlprice", "ffffffff", "settlement,maturity,last_interest,rate,yld,redemption,frequency,basis",
	  help_oddlprice,  gnumeric_oddlprice, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "oddlyield", "ffffffff", "settlement,maturity,last_interest,rate,pr,redemption,frequency,basis",
	  help_oddlyield,  gnumeric_oddlyield, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pmt", "fff|ff", "rate,nper,pv,fv,type",
	  help_pmt,	  gnumeric_pmt, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ppmt", "ffff|ff", "rate,per,nper,pv,fv,type",
	  help_ppmt,	  gnumeric_ppmt, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "price", "ffffff|f", "settlement,maturity,rate,yield,redemption_price,frequency,basis",
	  help_price, gnumeric_price, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pricedisc", "ffff|f", "settlement,maturity,discount,redemption,basis",
	  help_pricedisc,  gnumeric_pricedisc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pricemat", "fffff|f", "settlement,maturity,issue,rate,yield,basis",
	  help_pricemat,  gnumeric_pricemat, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pv", "fff|ff", "rate,nper,pmt,fv,type",
	  help_pv,	  gnumeric_pv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rate", "fff|fff", "nper,pmt,pv,fv,type,guess",
	  help_rate,	  gnumeric_rate, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "received", "ffff|f", "settlement,maturity,investment,discount,basis",
	  help_received,  gnumeric_received, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sln", "fff", "cost,salvagevalue,life",
	  help_sln,	  gnumeric_sln, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "syd", "ffff", "cost,salvagevalue,life,period",
	  help_syd,	  gnumeric_syd, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tbilleq", "fff", "settlement,maturity,discount",
	  help_tbilleq,  gnumeric_tbilleq, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tbillprice", "fff", "settlement,maturity,discount",
	  help_tbillprice, gnumeric_tbillprice, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tbillyield", "fff", "settlement,maturity,pr",
	  help_tbillyield, gnumeric_tbillyield, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "vdb", "fffff|ff", "cost,salvage,life,start_period,end_period,factor,switch",
	  help_vdb, gnumeric_vdb, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "xirr", "AA|f", "values,dates,guess",
	  help_xirr,	  gnumeric_xirr, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "xnpv", "fAA", "rate,values,dates",
	  help_xnpv,	  gnumeric_xnpv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yield", "ffffff|f", "settlement,maturity,rate,price,redemption_price,frequency,basis",
	  help_yield, gnumeric_yield, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yielddisc", "ffff|f", "settlement,maturity,pr,redemption,basis",
	  help_yielddisc,  gnumeric_yielddisc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yieldmat", "fffff|f", "settlement,maturity,issue,rate,pr,basis",
	  help_yieldmat,  gnumeric_yieldmat, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        {NULL}
};
