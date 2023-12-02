/*
 * fn-financial.c:  Built in financial functions and functions registration
 *
 * Authors:
 *   Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 *   Morten Welinder (terra@gnome.org)
 *   Vladimir Vuksan (vuksan@veus.hr)
 *   Andreas J. Guelzow (aguelzow@pyrshep.ca)
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
#include <tools/goal-seek.h>
#include <collect.h>
#include <value.h>
#include <mathfunc.h>
#include <gnm-format.h>
#include <workbook.h>
#include <sheet.h>
#include <gnm-datetime.h>
#include <gnm-i18n.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>

#include <math.h>
#include <limits.h>
#include <string.h>

#include "sc-fin.h"

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

#define FREQ_HELP \
	{ GNM_FUNC_HELP_NOTE, F_("@{frequency} may be 1 (annual), 2 (semi-annual), or 4 (quarterly).") }

#define TYPE_HELP \
	{ GNM_FUNC_HELP_NOTE, F_("If @{type} is 0, the default, payment is at the end of each period.  If @{type} is 1, payment is at the beginning of each period.") }


/***************************************************************************/

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

        return ((-pv * pvif - fv ) / ((1 + rate * type) * fvifa));
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
static double
coupnum (GDate const *settlement, GDate const *maturity,
	 GoCouponConvention const *conv)
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

static double
couppcd (GDate const *settlement, GDate const *maturity,
	 GoCouponConvention const *conv)
{
	GDate date;
	go_coup_cd (&date, settlement, maturity, conv->freq, conv->eom, FALSE);
	return go_date_g_to_serial (&date, conv->date_conv);
}

static double
coupncd (GDate const *settlement, GDate const *maturity,
	 GoCouponConvention const *conv)
{
	GDate date;
	go_coup_cd (&date, settlement, maturity, conv->freq, conv->eom, TRUE);
	return go_date_g_to_serial (&date, conv->date_conv);
}

static gnm_float
price (GDate *settlement, GDate *maturity, gnm_float rate, gnm_float yield,
       gnm_float redemption, GoCouponConvention const *conv)
{
	gnm_float a, d, e, sum, den, basem1, exponent, first_term, last_term;
	int       n;

	a = go_coupdaybs (settlement, maturity, conv);
	d = go_coupdaysnc (settlement, maturity, conv);
	e = go_coupdays (settlement, maturity, conv);
	n = coupnum (settlement, maturity, conv);

	den = 100 * rate / conv->freq;
	basem1 = yield / conv->freq;
	exponent = d / e;

	if (n == 1)
		return (redemption + den) / (1 + exponent * basem1) -
			a / e * den;

	sum = den * pow1p (basem1, 1 - n - exponent) *
		pow1pm1 (basem1, n) / basem1;

	first_term = redemption / pow1p (basem1, (n - 1 + d / e));
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
	   double (coup_fn) (GDate const *settle, GDate const *mat,
			     GoCouponConvention const *conv))
{
        GDate   settlement, maturity;
	GoCouponConvention conv;

        conv.freq  = value_get_freq (argv[2]);
	conv.basis = value_get_basis (argv[3], GO_BASIS_MSRB_30_360);
	conv.eom   = argv[4] ? value_get_as_checked_bool (argv[4]) : TRUE;
	conv.date_conv = sheet_date_conv (ei->pos->sheet);

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
        { GNM_FUNC_HELP_NAME, F_("ACCRINT:accrued interest")},
        { GNM_FUNC_HELP_ARG, F_("issue:date of issue")},
        { GNM_FUNC_HELP_ARG, F_("first_interest:date of first interest payment")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("par:par value, defaults to $1000")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis, defaults to 0")},
        { GNM_FUNC_HELP_ARG, F_("calc_method:calculation method, defaults to TRUE")},
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("If @{first_interest} < @{settlement} and @{calc_method} is "
	     "TRUE, then ACCRINT returns the sum of the"
	     " interest accrued in all coupon periods from @{issue} "
	     " date until @{settlement} date.") },
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("If @{first_interest} < @{settlement} and @{calc_method} is "
	     "FALSE, then ACCRINT returns the sum of the"
	     " interest accrued in all coupon periods from @{first_interest} "
	     " date until @{settlement} date.") },
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("Otherwise ACCRINT returns the sum of the"
	     " interest accrued in all coupon periods from @{issue} "
	     " date until @{settlement} date.") },
	{ GNM_FUNC_HELP_NOTE, F_("@{frequency} must be one of 1, 2 or 4, but the exact value"
				 " does not affect the result.") },
	{ GNM_FUNC_HELP_NOTE, F_("@{issue} must precede both @{first_interest}"
				 " and @{settlement}.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=ACCRINT(DATE(2008,3,1),DATE(2008,8,31),DATE(2008,5,1),10%,1000,2,0)" },
        { GNM_FUNC_HELP_SEEALSO, "ACCRINTM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_accrint (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate      issue, first_interest, settlement;
	gnm_float  rate, a, d, par, freq;
	int        basis;
	gboolean   calc_method;

	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

        if (!datetime_value_to_g (&issue, argv[0], date_conv) ||
	    !datetime_value_to_g (&first_interest, argv[1], date_conv) ||
	    !datetime_value_to_g (&settlement, argv[2], date_conv))
		return value_new_error_VALUE (ei->pos);

	if (argv[5] == NULL)
		return value_new_error_NUM (ei->pos);

	rate           = value_get_as_float (argv[3]);
	par            = argv[4] ? value_get_as_float (argv[4]) : 1000;
	freq           = value_get_freq (argv[5]);
	basis          = value_get_basis (argv[6], GO_BASIS_MSRB_30_360);
	calc_method    = argv[6] ? value_get_as_int (argv[6]) : 1;

        if (rate <= 0	||
	    par <= 0	||
	    !is_valid_freq (freq)	||
	    !is_valid_basis (basis)	||
	    g_date_compare (&issue, &settlement) >= 0)
		return value_new_error_NUM (ei->pos);
	if (g_date_compare (&first_interest, &settlement) >= 0 || calc_method)
		a = days_monthly_basis (argv[0], argv[2], basis, date_conv);
	else
		a = days_monthly_basis (argv[1], argv[2], basis, date_conv);
	d = annual_year_basis (argv[2], basis, date_conv);
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
        { GNM_FUNC_HELP_NAME, F_("ACCRINTM:accrued interest")},
        { GNM_FUNC_HELP_ARG, F_("issue:date of issue")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("par:par value")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ACCRINTM calculates the accrued interest from @{issue} to @{maturity}.") },
	{ GNM_FUNC_HELP_NOTE, F_("@{par} defaults to $1000.") },
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=ACCRINTM(DATE(2008,4,1),DATE(2008,6,15),10%,1000,3)" },
        { GNM_FUNC_HELP_SEEALSO, "ACCRINT"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_accrintm (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate, a, d, par;
	int basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	rate  = value_get_as_float (argv[2]);
	par   = argv[3] ? value_get_as_float (argv[3]) : 1000;
	basis = value_get_basis (argv[4], GO_BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (a < 0 || d <= 0 || par <= 0 || rate <= 0
	    || !is_valid_basis (basis))
                return value_new_error_NUM (ei->pos);

	return value_new_float (par * rate * a/d);
}

/***************************************************************************/

static GnmFuncHelp const help_intrate[] = {
        { GNM_FUNC_HELP_NAME, F_("INTRATE:interest rate")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("investment:amount paid on settlement")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("INTRATE calculates the interest of a fully vested security.") },
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=INTRATE(DATE(2008,4,15),DATE(2008,9,30),100000,103525,1)" },
        { GNM_FUNC_HELP_SEEALSO, "RECEIVED"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_intrate (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float investment, redemption, a, d;
	int basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	investment = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], GO_BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (!is_valid_basis (basis) || a <= 0 || d <= 0 || investment == 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float ((redemption - investment) / investment *
				(d / a));
}

/***************************************************************************/

static GnmFuncHelp const help_received[] = {
        { GNM_FUNC_HELP_NAME, F_("RECEIVED:amount to be received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("investment:amount paid on settlement")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("RECEIVED calculates the amount to be received when a security matures.") },
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=RECEIVED(DATE(2008,4,15),DATE(2008,9,30),100000,4%,1)" },
        { GNM_FUNC_HELP_SEEALSO, "INTRATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_received (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float investment, discount, a, d, n;
	int basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	investment = value_get_as_float (argv[2]);
	discount   = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], GO_BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (a <= 0 || d <= 0 || !is_valid_basis (basis))
                return value_new_error_NUM (ei->pos);

	n = 1 - (discount * a/d);
	if (n == 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (investment / n);
}

/***************************************************************************/

static GnmFuncHelp const help_pricedisc[] = {
        { GNM_FUNC_HELP_NAME, F_("PRICEDISC:discounted price")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("discount:annual rate at which to discount")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PRICEDISC calculates the price per $100 face value of a bond that does not pay interest at maturity.") },
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "PRICEMAT"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pricedisc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float discount, redemption, a, d;
	int basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	discount   = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], GO_BASIS_MSRB_30_360);

	a = days_monthly_basis (argv[0], argv[1], basis, date_conv);
	d = annual_year_basis (argv[0], basis, date_conv);

	if (a <= 0 || d <= 0 || !is_valid_basis (basis))
                return value_new_error_NUM (ei->pos);

	return value_new_float (redemption - discount * redemption * a/d);
}

/***************************************************************************/

static GnmFuncHelp const help_pricemat[] = {
        { GNM_FUNC_HELP_NAME, F_("PRICEMAT:price at maturity")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("issue:date of issue")},
        { GNM_FUNC_HELP_ARG, F_("discount:annual rate at which to discount")},
        { GNM_FUNC_HELP_ARG, F_("yield:annual yield of security")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PRICEMAT calculates the price per $100 face value of a bond that pays interest at maturity.") },
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "PRICEDISC"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pricemat (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float discount, yield, a, b, dsm, dim, n;
	int basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	discount = value_get_as_float (argv[3]);
	yield    = value_get_as_float (argv[4]);
	basis    = value_get_basis (argv[5], GO_BASIS_MSRB_30_360);

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
        { GNM_FUNC_HELP_NAME, F_("DISC:discount rate")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("par:price per $100 face value")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DISC calculates the discount rate for a security.") },
	{ GNM_FUNC_HELP_NOTE, F_("@{redemption} is the redemption value per $100 face value.") },
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "PRICEMAT"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_disc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float par, redemption, dsm, b;
	int basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	par        = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = value_get_basis (argv[4], GO_BASIS_MSRB_30_360);

	b = annual_year_basis (argv[0], basis, date_conv);
	dsm = days_monthly_basis (argv[0], argv[1], basis, date_conv);

	if (dsm <= 0 || b <= 0 || dsm <= 0 || !is_valid_basis (basis)
	    || redemption == 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float ((redemption - par) / redemption * (b / dsm));
}

/***************************************************************************/

static GnmFuncHelp const help_effect[] = {
        { GNM_FUNC_HELP_NAME, F_("EFFECT:effective interest rate")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods used for compounding")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("EFFECT calculates the effective interest rate using the formula (1+@{rate}/@{nper})^@{nper}-1.") },
        { GNM_FUNC_HELP_EXAMPLES, "=EFFECT(19%,12)"},
        { GNM_FUNC_HELP_SEEALSO, "NOMINAL"},
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
        { GNM_FUNC_HELP_NAME, F_("NOMINAL:nominal interest rate")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods used for compounding")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NOMINAL calculates the nominal interest rate from the effective rate.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NOMINAL(10%,6)" },
        { GNM_FUNC_HELP_SEEALSO, "EFFECT"},
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

        return value_new_float (nper * pow1pm1 (rate, 1 / nper));
}

/***************************************************************************/

static GnmFuncHelp const help_ispmt[] = {
        { GNM_FUNC_HELP_NAME, F_("ISPMT:interest payment for period")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("per:period number")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ISPMT calculates the interest payment for period number @{per}.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISPMT(10%,4,10,1e6)" },
        { GNM_FUNC_HELP_SEEALSO, "PV"},
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
        { GNM_FUNC_HELP_NAME, F_("DB:depreciation of an asset")},
        { GNM_FUNC_HELP_ARG, F_("cost:initial cost of asset")},
        { GNM_FUNC_HELP_ARG, F_("salvage:value after depreciation")},
        { GNM_FUNC_HELP_ARG, F_("life:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("period:subject period")},
        { GNM_FUNC_HELP_ARG, F_("month:number of months in first year of depreciation")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DB calculates the depreciation of an asset for a given period using the fixed-declining balance method.") },
        { GNM_FUNC_HELP_SEEALSO, "DDB,SLN,SYD"},
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
	rate  = gnm_round (rate) / 1000;

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
        { GNM_FUNC_HELP_NAME, F_("DDB:depreciation of an asset")},
        { GNM_FUNC_HELP_ARG, F_("cost:initial cost of asset")},
        { GNM_FUNC_HELP_ARG, F_("salvage:value after depreciation")},
        { GNM_FUNC_HELP_ARG, F_("life:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("period:subject period")},
        { GNM_FUNC_HELP_ARG, F_("factor:factor at which the balance declines")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DDB calculates the depreciation of an asset for a given period using the double-declining balance method.") },
        { GNM_FUNC_HELP_SEEALSO, "DB,SLN,SYD"},
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
        { GNM_FUNC_HELP_NAME, F_("SLN:depreciation of an asset")},
        { GNM_FUNC_HELP_ARG, F_("cost:initial cost of asset")},
        { GNM_FUNC_HELP_ARG, F_("salvage:value after depreciation")},
        { GNM_FUNC_HELP_ARG, F_("life:number of periods")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("SLN calculates the depreciation of an asset using the straight-line method.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SLN(10000,700,10)" },
        { GNM_FUNC_HELP_SEEALSO, "DB,DDB,SYD"},
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
        { GNM_FUNC_HELP_NAME, F_("SYD:sum-of-years depreciation")},
        { GNM_FUNC_HELP_ARG, F_("cost:initial cost of asset")},
        { GNM_FUNC_HELP_ARG, F_("salvage:value after depreciation")},
        { GNM_FUNC_HELP_ARG, F_("life:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("period:subject period")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("SYD calculates the depreciation of an asset using the sum-of-years method.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SYD(5000,200,5,2)" },
        { GNM_FUNC_HELP_SEEALSO, "DB,DDB,SLN"},
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
				(life * (life + 1)));
}

/***************************************************************************/

static GnmFuncHelp const help_dollarde[] = {
        { GNM_FUNC_HELP_NAME, F_("DOLLARDE:convert to decimal dollar amount")},
        { GNM_FUNC_HELP_ARG, F_("fractional_dollar:amount to convert")},
        { GNM_FUNC_HELP_ARG, F_("fraction:denominator")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DOLLARDE converts a fractional dollar amount into a decimal amount.  This is the inverse of the DOLLARFR function.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=TEXT(DOLLARDE(0.03,16),\"0.0000\")" },
        { GNM_FUNC_HELP_SEEALSO, "DOLLARFR" },
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
	fdigits = 1 + gnm_floor (gnm_log10 (f - GNM_const(0.5)));

	res = gnm_floor (x);

	/* If f=9, then .45 means 4.5/9  */
	res += (x - res) * gnm_pow10 (fdigits) / f;

	if (negative)
		res = 0 - res;

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_dollarfr[] = {
        { GNM_FUNC_HELP_NAME, F_("DOLLARFR:convert to dollar fraction")},
        { GNM_FUNC_HELP_ARG, F_("decimal_dollar:amount to convert")},
        { GNM_FUNC_HELP_ARG, F_("fraction:denominator")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DOLLARFR converts a decimal dollar amount into a fractional amount which is represented as the digits after the decimal point.  For example, 2/8 would be represented as .2 while 3/16 would be represented as .03. This is the inverse of the DOLLARDE function.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=DOLLARFR(0.25,8)" },
        { GNM_FUNC_HELP_SEEALSO, "DOLLARDE"},
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
	fdigits = 1 + gnm_floor (gnm_log10 (f - GNM_const(0.5)));

	res = gnm_floor (x);
	res += (x - res) * f / gnm_pow10 (fdigits);

	if (negative)
		res = 0 - res;

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_mirr[] = {
        { GNM_FUNC_HELP_NAME, F_("MIRR:modified internal rate of return")},
        { GNM_FUNC_HELP_ARG, F_("values:cash flow")},
        { GNM_FUNC_HELP_ARG, F_("finance_rate:interest rate for financing cost")},
        { GNM_FUNC_HELP_ARG, F_("reinvest_rate:interest rate for reinvestments")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("MIRR calculates the modified internal rate of return of a periodic cash flow.") },
        { GNM_FUNC_HELP_SEEALSO, "IRR,XIRR"},
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
		       (GNM_const(1.0) / (n - 1))) - 1;

	result = value_new_float (res);
out:
	g_free (values);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_tbilleq[] = {
        { GNM_FUNC_HELP_NAME, F_("TBILLEQ:bond-equivalent yield for a treasury bill")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("discount:annual rate at which to discount")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("TBILLEQ calculates the bond-equivalent yield for a treasury bill.") },
        { GNM_FUNC_HELP_SEEALSO, "TBILLPRICE,TBILLYIELD"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tbilleq (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float settlement, maturity, discount;
	gnm_float dsm, divisor;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

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
        { GNM_FUNC_HELP_NAME, F_("TBILLPRICE:price of a treasury bill")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("discount:annual rate at which to discount")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("TBILLPRICE calculates the price per $100 face value for a treasury bill.") },
        { GNM_FUNC_HELP_SEEALSO, "TBILLEQ,TBILLYIELD"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tbillprice (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float settlement, maturity, discount;
	gnm_float res, dsm;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	settlement = datetime_value_to_serial (argv[0], date_conv);
	maturity   = datetime_value_to_serial (argv[1], date_conv);
	discount   = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (settlement > maturity || discount < 0 || dsm > 365)
                return value_new_error_NUM (ei->pos);

	res = 100 * (1 - (discount * dsm) / 360);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_tbillyield[] = {
        { GNM_FUNC_HELP_NAME, F_("TBILLYIELD:yield of a treasury bill")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("price:price")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("TBILLYIELD calculates the yield of a treasury bill.") },
        { GNM_FUNC_HELP_SEEALSO, "TBILLEQ,TBILLPRICE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tbillyield (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float settlement, maturity, pr;
	gnm_float res, dsm;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	settlement = datetime_value_to_serial (argv[0], date_conv);
	maturity   = datetime_value_to_serial (argv[1], date_conv);
	pr         = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (pr <= 0 || dsm <= 0 || dsm > 365)
                return value_new_error_NUM (ei->pos);

	res = (100 - pr) / pr * (360 / dsm);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_rate[] = {
        { GNM_FUNC_HELP_NAME, F_("RATE:rate of investment")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pmt:payment at each period")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
        { GNM_FUNC_HELP_ARG, F_("guess:an estimate of what the result should be")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("RATE calculates the rate of return.") },
	TYPE_HELP,
	{ GNM_FUNC_HELP_NOTE, F_("The optional @{guess} is needed because there can be more than one valid result.  It defaults to 10%.") },
        { GNM_FUNC_HELP_EXAMPLES, "=RATE(10,-1500,10000,0)" },
        { GNM_FUNC_HELP_SEEALSO, "PV,FV"},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	int type;
	gnm_float nper, pv, fv, pmt;
} gnumeric_rate_t;

static GnmGoalSeekStatus
gnumeric_rate_f (gnm_float rate, gnm_float *y, void *user_data)
{
	if (rate > -1 && rate != 0) {
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
static GnmGoalSeekStatus
gnumeric_rate_df (gnm_float rate, gnm_float *y, void *user_data)
{
	if (rate > -1 && rate != 0) {
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
	GnmGoalSeekData    data;
	GnmGoalSeekStatus  status;
	gnumeric_rate_t udata;
	gnm_float      rate0;

	udata.nper = value_get_as_int (argv[0]);
	/* YES ZERO, it's sick but it's XL compatible */
	udata.pmt  = argv[1] ? value_get_as_float (argv[1]) : 0;
	udata.pv   = value_get_as_float (argv[2]);
	udata.fv   = argv[3] ? value_get_as_float (argv[3]) : 0;
	udata.type = value_get_paytype (argv[4]);
	rate0      = argv[5] ? value_get_as_float (argv[5]) : GNM_const(0.1);

	if (udata.nper <= 0)
		return value_new_error_NUM (ei->pos);

	if (!is_valid_paytype (udata.type))
		return value_new_error_VALUE (ei->pos);

#if 0
	g_printerr ("Guess = %.15g\n", rate0);
#endif
	goal_seek_initialize (&data);

	data.xmin = MAX (data.xmin,
			 -gnm_pow (GNM_MAX / GNM_const(1e10),
				   GNM_const(1.0) / udata.nper) + 1);
	data.xmax = MIN (data.xmax,
			 gnm_pow (GNM_MAX / GNM_const(1e10),
				  GNM_const(1.0) / udata.nper) - 1);

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
		g_printerr ("Root = %.15g\n\n", data.root);
#endif
		return value_new_float (data.root);
	} else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_rri[] = {
        { GNM_FUNC_HELP_NAME, F_("RRI:equivalent interest rate for an investment increasing in value")},
        { GNM_FUNC_HELP_ARG, F_("p:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("RRI determines an equivalent interest rate for an investment that increases in value. The interest is compounded after each complete period.") },
	TYPE_HELP,
	{ GNM_FUNC_HELP_NOTE, F_("Note that @{p} need not be an integer but for fractional value the calculated rate is only approximate.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=RRI(12,5000,10000)" },
        { GNM_FUNC_HELP_SEEALSO, "PV,FV,RATE"},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_rri (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float      per, pv, fv;

	per = value_get_as_float (argv[0]);
	pv = value_get_as_float (argv[1]);
	fv = value_get_as_float (argv[2]);

	if (per < 0)
		return value_new_error_NUM (ei->pos);
        if (pv == 0 || per == 0)
		return value_new_error_DIV0 (ei->pos);

	return value_new_float (gnm_pow (fv / pv, 1 / per) - 1);
}


/***************************************************************************/

static GnmFuncHelp const help_irr[] = {
        { GNM_FUNC_HELP_NAME, F_("IRR:internal rate of return")},
        { GNM_FUNC_HELP_ARG, F_("values:cash flow")},
        { GNM_FUNC_HELP_ARG, F_("guess:an estimate of what the result should be")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IRR calculates the internal rate of return of a cash flow with periodic payments.  @{values} lists the payments (negative values) and receipts (positive values) for each period.") },
        { GNM_FUNC_HELP_EXAMPLES, "=IRR({100;100;200;-450})" },
	{ GNM_FUNC_HELP_NOTE, F_("The optional @{guess} is needed because there can be more than one valid result.  It defaults to 10%.") },
        { GNM_FUNC_HELP_SEEALSO, "XIRR"},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        int     n;
        gnm_float *values;
} gnumeric_irr_t;

static GnmGoalSeekStatus
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

static GnmGoalSeekStatus
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
	GnmGoalSeekData    data;
	GnmGoalSeekStatus  status;
	GnmValue           *result = NULL;
	gnumeric_irr_t  p;
	gnm_float      rate0;

	rate0 = argv[1] ? value_get_as_float (argv[1]) : GNM_const(0.1);

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
			 gnm_pow (GNM_MAX / GNM_const(1e10),
				  GNM_const(1.0) / p.n) - 1);

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
        { GNM_FUNC_HELP_NAME, F_("PV:present value")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective interest rate per period")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pmt:payment at each period")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PV calculates the present value of @{fv} which is @{nper} periods into the future, assuming a periodic payment of @{pmt} and an interest rate of @{rate} per period.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_EXAMPLES, "=PV(10%,10,1000,20000,0)" },
        { GNM_FUNC_HELP_SEEALSO, "FV"},
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

        return value_new_float ((-fv - pmt * (1 + rate * type) * fvifa) /
				pvif);
}

/***************************************************************************/

static GnmFuncHelp const help_npv[] = {
        { GNM_FUNC_HELP_NAME, F_("NPV:net present value")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective interest rate per period")},
        { GNM_FUNC_HELP_ARG, F_("value1:cash flow for period 1")},
        { GNM_FUNC_HELP_ARG, F_("value2:cash flow for period 2")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NPV calculates the net present value of a cash flow.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NPV(10%,100,100,-250)" },
        { GNM_FUNC_HELP_SEEALSO, "PV"},
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
        { GNM_FUNC_HELP_NAME, F_("XNPV:net present value")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("values:cash flow")},
        { GNM_FUNC_HELP_ARG, F_("dates:dates of cash flow")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("XNPV calculates the net present value of a cash flow at irregular times.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_SEEALSO, "NPV"},
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

	/* FIXME: clearly the values should be collected as pairs so
	   missing entries are lined up.  */

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
			pow1p (rate, (dates[i] - dates[0]) / 365);

	result = value_new_float (sum);
 out:
	g_free (payments);
	g_free (dates);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_xirr[] = {
        { GNM_FUNC_HELP_NAME, F_("XIRR:internal rate of return")},
        { GNM_FUNC_HELP_ARG, F_("values:cash flow")},
        { GNM_FUNC_HELP_ARG, F_("dates:dates of cash flow")},
        { GNM_FUNC_HELP_ARG, F_("guess:an estimate of what the result should be")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("XIRR calculates the annualized internal rate of return of a cash flow at arbitrary points in time.  @{values} lists the payments (negative values) and receipts (positive values) with one value for each entry in @{dates}.") },
	{ GNM_FUNC_HELP_NOTE, F_("The optional @{guess} is needed because there can be more than one valid result.  It defaults to 10%.") },
        { GNM_FUNC_HELP_SEEALSO, "IRR"},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        int n;
        const gnm_float *values;
        const gnm_float *dates;
} gnumeric_xirr_t;

static GnmGoalSeekStatus
xirr_npv (gnm_float rate, gnm_float *y, void *user_data)
{
	const gnumeric_xirr_t *p = user_data;
	gnm_float sum = 0;
        int i;

	for (i = 0; i < p->n; i++) {
		gnm_float d = p->dates[i] - p->dates[0];

		if (d < 0)
			return GOAL_SEEK_ERROR;
		sum += p->values[i] / pow1p (rate, d / 365);
	}

	*y = sum;
	return GOAL_SEEK_OK;
}

static int
gnm_range_xirr (gnm_float const *xs, const gnm_float *ys,
		int n, gnm_float *res, gpointer user)
{
	gnumeric_xirr_t p;
	gnm_float rate0 = *(gnm_float *)user;
	GnmGoalSeekData data;
	GnmGoalSeekStatus status;

	p.dates = ys;
	p.values = xs;
	p.n = n;

	goal_seek_initialize (&data);
	data.xmin = -1;
	data.xmax = MIN (1000, data.xmax);

	status = goal_seek_newton (&xirr_npv, NULL, &data, &p, rate0);
	if (status != GOAL_SEEK_OK) {
		int i;

		/* This is likely to be on the left side of the root. */
		(void)goal_seek_point (&xirr_npv, &data, &p, -1);

		for (i = 1; i <= 1024; i += i) {
			(void)goal_seek_point (&xirr_npv, &data, &p, -1 + 10.0 / (i + 9));
			(void)goal_seek_point (&xirr_npv, &data, &p, i);
			status = goal_seek_bisection (&xirr_npv, &data, &p);
			if (status == GOAL_SEEK_OK)
				break;
		}
	}

	if (status == GOAL_SEEK_OK) {
		*res = data.root;
		return 0;
	}

	return 1;
}


static GnmValue *
gnumeric_xirr (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate0 = argv[2] ? value_get_as_float (argv[2]) : GNM_const(0.1);

	return float_range_function2d (argv[0], argv[1],
				       ei,
				       gnm_range_xirr,
				       COLLECT_IGNORE_BLANKS |
				       COLLECT_COERCE_STRINGS,
				       GNM_ERROR_VALUE,
				       &rate0);
}

/***************************************************************************/

static GnmFuncHelp const help_fv[] = {
        { GNM_FUNC_HELP_NAME, F_("FV:future value")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective interest rate per period")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pmt:payment at each period")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("FV calculates the future value of @{pv} moved @{nper} periods into the future, assuming a periodic payment of @{pmt} and an interest rate of @{rate} per period.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_EXAMPLES, "=FV(10%,10,1000,20000,0)" },
        { GNM_FUNC_HELP_SEEALSO, "PV"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float rate = value_get_as_float (argv[0]);
	gnm_float nper = value_get_as_float (argv[1]);
	gnm_float pmt  = value_get_as_float (argv[2]);
	gnm_float pv   = argv[3] ? value_get_as_float (argv[3]) : 0;
	int type       = value_get_paytype (argv[4]);
	gnm_float pvif, fvifa;

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

	pvif  = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

        return value_new_float (-((pv * pvif) + pmt *
				  (1 + rate * type) * fvifa));
}

/***************************************************************************/

static GnmFuncHelp const help_pmt[] = {
        { GNM_FUNC_HELP_NAME, F_("PMT:payment for annuity")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PMT calculates the payment amount for an annuity.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_SEEALSO, "PV,FV,RATE,ISPMT"},
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
        { GNM_FUNC_HELP_NAME, F_("IPMT:interest payment for period")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("per:period number")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IPMT calculates the interest part of an annuity's payment for period number @{per}.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_EXAMPLES, "=IPMT(10%,4,10,1e6)" },
        { GNM_FUNC_HELP_SEEALSO, "PPMT"},
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
        { GNM_FUNC_HELP_NAME, F_("PPMT:interest payment for period")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("per:period number")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PPMT calculates the principal part of an annuity's payment for period number @{per}.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_EXAMPLES, "=PPMT(10%,4,10,1e6)" },
        { GNM_FUNC_HELP_SEEALSO, "IPMT"},
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
        { GNM_FUNC_HELP_NAME, F_("NPER:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("pmt:payment at each period")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NPER calculates the number of periods of an investment based on periodic constant payments and a constant interest rate.") },
	TYPE_HELP,
	{ GNM_FUNC_HELP_EXAMPLES, "=NPER(6%,0,-10000,20000,0)" },
        { GNM_FUNC_HELP_SEEALSO, "PV,FV"},
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

	if (rate == 0) {
		if (pmt == 0)
			return value_new_error_DIV0 (ei->pos);
		else
			return value_new_float (-(fv + pv) / pmt);
	}

	if (rate <= -1)
		return value_new_error_NUM (ei->pos);

	if (!is_valid_paytype (type))
		return value_new_error_VALUE (ei->pos);

	tmp = (pmt * (1 + rate * type) - fv * rate) /
	  (pv * rate + pmt * (1 + rate * type));
	if (tmp <= 0)
		return value_new_error_VALUE (ei->pos);

        return value_new_float (gnm_log (tmp) / gnm_log1p (rate));
}

/***************************************************************************/

static GnmFuncHelp const help_duration[] = {
        { GNM_FUNC_HELP_NAME, F_("DURATION:the (Macaulay) duration of a security")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("coupon:annual coupon rate")},
        { GNM_FUNC_HELP_ARG, F_("yield:annual yield of security")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DURATION calculates the (Macaulay) duration of a security.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=DURATION(TODAY(),TODAY()+365,0.05,0.08,4)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=DURATION(TODAY(),TODAY()+366,0.05,0.08,4)"},
        { GNM_FUNC_HELP_SEEALSO, "MDURATION,G_DURATION"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_duration (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nSettle, nMat;
	gnm_float fCoup, fYield;
        gnm_float fNumOfCoups;
	GoCouponConvention conv;

	conv.date_conv = sheet_date_conv (ei->pos->sheet);
	conv.eom = TRUE;

	fCoup      = value_get_as_float (argv[2]);
	fYield     = value_get_as_float (argv[3]);
	conv.freq  = value_get_freq (argv[4]);
        conv.basis = value_get_basis (argv[5], GO_BASIS_MSRB_30_360);

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
        { GNM_FUNC_HELP_NAME, F_("G_DURATION:the duration of a investment") },
        { GNM_FUNC_HELP_ARG, F_("rate:effective annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("fv:future value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("G_DURATION calculates the number of periods needed for an investment to attain a desired value.") },
	{ GNM_FUNC_HELP_ODF, F_("G_DURATION is the OpenFormula function PDURATION.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=G_DURATION(0.08,1000,2000)"},
        { GNM_FUNC_HELP_SEEALSO, "FV,PV,DURATION,MDURATION"},
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
        { GNM_FUNC_HELP_NAME, F_("FVSCHEDULE:future value")},
        { GNM_FUNC_HELP_ARG, F_("principal:initial value")},
        { GNM_FUNC_HELP_ARG, F_("schedule:range of interest rates")},
        { GNM_FUNC_HELP_DESCRIPTION, F_("FVSCHEDULE calculates the future value of @{principal} after applying a range of interest rates with compounding.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FVSCHEDULE(1000,{0.1;0.02;0.1})" },
        { GNM_FUNC_HELP_SEEALSO, "FV"},
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
					 COLLECT_IGNORE_BLANKS, &n, &result);
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
        { GNM_FUNC_HELP_NAME, F_("EURO:equivalent of 1 EUR")},
        { GNM_FUNC_HELP_ARG, F_("currency:three-letter currency code")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("EURO calculates the national currency amount corresponding to 1 EUR for any of the national currencies that were replaced by the Euro on its introduction.") },
	{ GNM_FUNC_HELP_NOTE, F_("@{currency} must be one of "
				 "ATS (Austria), "
				 "BEF (Belgium), "
				 "CYP (Cyprus), "
				 "DEM (Germany), "
				 "EEK (Estonia), "
				 "ESP (Spain), "
				 "EUR (Euro), "
				 "FIM (Finland), "
				 "FRF (France), "
				 "GRD (Greece), "
				 "IEP (Ireland), "
				 "ITL (Italy), "
				 "LUF (Luxembourg), "
				 "MTL (Malta), "
				 "NLG (The Netherlands), "
				 "PTE (Portugal), "
				 "SIT (Slovenia), or "
				 "SKK (Slovakia).") },
	{ GNM_FUNC_HELP_NOTE, F_("This function is not likely to be useful anymore.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EURO(\"DEM\")" },
        { GNM_FUNC_HELP_SEEALSO, "EUROCONVERT"},
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
	case 'C':
		if (strncmp ("CYP", str, 3) == 0)
			return GNM_const (0.585274);
		break;
	case 'D':
		if (strncmp ("DEM", str, 3) == 0)
			return GNM_const (1.95583);
		break;
	case 'E':
		if (strncmp ("ESP", str, 3) == 0)
			return GNM_const (166.386);
		else if (strncmp ("EEK", str, 3) == 0)
			return GNM_const (15.6466);
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
		if (strncmp ("LUF", str, 3) == 0)
			return GNM_const (40.3399);
		break;
	case 'M':
		if (strncmp ("MTL", str, 3) == 0)
			return GNM_const (0.429300);
		break;
	case 'N':
		if (strncmp ("NLG", str, 3) == 0)
			return GNM_const (2.20371);
		break;
	case 'P':
		if (strncmp ("PTE", str, 3) == 0)
			return GNM_const (200.482);
		break;
	case 'S':
		if (strncmp ("SIT", str, 3) == 0)
			return GNM_const (239.640);
		else if (strncmp ("SKK", str, 3) == 0)
			return GNM_const (30.1260);
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

/*
 * Returns one euro as a given national currency. On error, negative
 * value is returned.
 */
static int
euro_local_rounding (char const *str)
{
	switch (*str) {
	case 'A':
/* 		if (strncmp ("ATS", str, 3) == 0) */
/* 			return 2; */
		break;
	case 'B':
		if (strncmp ("BEF", str, 3) == 0)
			return 0;
		break;
	case 'C':
/* 		if (strncmp ("CYP", str, 3) == 0) */
/* 			return 2; /\*??*\/ */
		break;
	case 'D':
/* 		if (strncmp ("DEM", str, 3) == 0) */
/* 			return 2; */
		break;
	case 'E':
		if (strncmp ("ESP", str, 3) == 0)
			return 0;
/* 		else if (strncmp ("EEK", str, 3) == 0) */
/* 			return 2; */
/* 		else if (strncmp ("EUR", str, 3) == 0) */
/* 			return 2; */
		break;
	case 'F':
/* 		if (strncmp ("FIM", str, 3) == 0) */
/* 			return 2 */
/* 		else if (strncmp ("FRF", str, 3) == 0) */
/* 			return 2; */
		break;
	case 'G':
		if (strncmp ("GRD", str, 3) == 0)
			return 0;
		break;
	case 'I':
		if (strncmp ("ITL", str, 3) == 0)
			return 0;
/* 		else if (strncmp ("IEP", str, 3) == 0) */
/* 			return 2; */
		break;
	case 'L':
		if (strncmp ("LUF", str, 3) == 0)
			return 0;
		break;
	case 'M':
/* 		if (strncmp ("MTL", str, 3) == 0) */
/* 			return 2;  /\* ?? *\/ */
		break;
	case 'N':
/* 		if (strncmp ("NLG", str, 3) == 0) */
/* 			return 2; */
		break;
	case 'P':
		if (strncmp ("PTE", str, 3) == 0)
			return 0;
		break;
	case 'S':
/* 		if (strncmp ("SIT", str, 3) == 0) */
/* 			return 2; */
/* 		else if (strncmp ("SKK", str, 3) == 0) */
/* 			return 2;  /\* ?? *\/ */
		break;
	default:
		break;
	}

	return 2;
}

/***************************************************************************/

static GnmFuncHelp const help_euroconvert[] = {
        { GNM_FUNC_HELP_NAME, F_("EUROCONVERT:pre-Euro amount from one currency to another")},
        { GNM_FUNC_HELP_ARG, F_("n:amount")},
        { GNM_FUNC_HELP_ARG, F_("source:three-letter source currency code")},
        { GNM_FUNC_HELP_ARG, F_("target:three-letter target currency code")},
        { GNM_FUNC_HELP_ARG, F_("full_precision:whether to provide the full precision; defaults to false")},
        { GNM_FUNC_HELP_ARG, F_("triangulation_precision:number of digits (at least 3) to be rounded to after conversion of the source currency to euro; defaults to no rounding")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("EUROCONVERT converts @{n} units of currency @{source} to currency @{target}.  The rates used are the official ones used on the introduction of the Euro.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{full_precision} is true, the result is not rounded; if it false the result is rounded to 0 or 2 decimals depending on the target currency; defaults to false.")},
	{ GNM_FUNC_HELP_NOTE, F_("@{source} and @{target} must be one of the currencies listed for the EURO function.") },
	{ GNM_FUNC_HELP_NOTE, F_("This function is not likely to be useful anymore.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EUROCONVERT(1,\"DEM\",\"ITL\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=EUROCONVERT(1,\"DEM\",\"ITL\",FALSE)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=EUROCONVERT(1,\"DEM\",\"ITL\",FALSE,3)" },
        { GNM_FUNC_HELP_SEEALSO, "EURO"},
	{ GNM_FUNC_HELP_END }
};



static GnmValue *
gnumeric_euroconvert (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float c1 = one_euro (value_peek_string (argv[1]));
	gnm_float c2 = one_euro (value_peek_string (argv[2]));

	if (c1 >= 0 && c2 >= 0) {
		gnm_float n  = value_get_as_float (argv[0]);
		gnm_float inter = n / c1;
		gboolean err = FALSE;
		if (argv[3] != NULL && argv[4] != NULL) {
			int decimals = value_get_as_int (argv[4]);
			if (decimals < 3 || decimals > 100)
				return value_new_error_VALUE (ei->pos);
			else {
				gnm_float p10 = gnm_pow10 (decimals);
				inter = gnm_fake_round (inter * p10) / p10;
			}
		}
		inter = inter * c2;
		if (argv[3] != NULL && !value_get_as_bool (argv[3], &err) && !err) {
			int decimals = euro_local_rounding (value_peek_string (argv[2]));
			gnm_float p10 = gnm_pow10 (decimals);
			inter = gnm_fake_round (inter * p10) / p10;
		}
		return value_new_float (inter);
	} else
		return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_price[] = {
        { GNM_FUNC_HELP_NAME, F_("PRICE:price of a security")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("yield:annual yield of security")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("PRICE calculates the price per $100 face value of a security that pays periodic interest.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "YIELD,DURATION"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_price (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate      settlement, maturity;
	gnm_float rate, yield, redemption;
	GoCouponConvention conv;

	conv.date_conv = sheet_date_conv (ei->pos->sheet);

	rate       = value_get_as_float (argv[2]);
	yield      = value_get_as_float (argv[3]);
	redemption = value_get_as_float (argv[4]);
        conv.freq  = value_get_freq (argv[5]);
	conv.eom   = TRUE;
        conv.basis = value_get_basis (argv[6], GO_BASIS_MSRB_30_360);

	if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (conv.basis)
	    || !is_valid_freq (conv.freq)
            || g_date_compare (&settlement, &maturity) > 0)
		return value_new_error_NUM (ei->pos);

        if (rate < 0 || yield < 0 || redemption <= 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float (price (&settlement, &maturity, rate, yield,
				       redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_yield[] = {
        { GNM_FUNC_HELP_NAME, F_("YIELD:yield of a security")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("price:price of security")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("YIELD calculates the yield of a security that pays periodic interest.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "PRICE,DURATION"},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        GDate settlement, maturity;
	gnm_float rate, redemption, par;
	GoCouponConvention conv;
} gnumeric_yield_t;

static GnmGoalSeekStatus
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
        udata.conv.basis = value_get_basis (argv[6], GO_BASIS_MSRB_30_360);
        udata.conv.eom   = TRUE;
        udata.conv.date_conv = sheet_date_conv (ei->pos->sheet);

	if (!datetime_value_to_g (&udata.settlement, argv[0], udata.conv.date_conv) ||
	    !datetime_value_to_g (&udata.maturity, argv[1], udata.conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (udata.conv.basis)
	    || !is_valid_freq (udata.conv.freq)
            || g_date_compare (&udata.settlement, &udata.maturity) > 0)
		return value_new_error_NUM (ei->pos);

        if (udata.rate < 0 || udata.par < 0 || udata.redemption <= 0)
		return value_new_error_NUM (ei->pos);

	n = coupnum (&udata.settlement, &udata.maturity, &udata.conv);
	if (n <= 1) {
		gnm_float a = go_coupdaybs (&udata.settlement, &udata.maturity,
					 &udata.conv);
		gnm_float d = go_coupdaysnc (&udata.settlement, &udata.maturity,
					 &udata.conv);
		gnm_float e = go_coupdays (&udata.settlement, &udata.maturity,
					 &udata.conv);

		gnm_float coeff = udata.conv.freq * e / d;
		gnm_float num = (udata.redemption / 100  +
				  udata.rate / udata.conv.freq)
			- (udata.par / 100  +  (a / e  *
						  udata.rate / udata.conv.freq));
		gnm_float den = udata.par / 100  +  (a / e  *  udata.rate /
							udata.conv.freq);

		return value_new_float (num / den * coeff);
	} else {
		GnmGoalSeekData     data;
		GnmGoalSeekStatus   status;
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
        { GNM_FUNC_HELP_NAME, F_("YIELDDISC:yield of a discounted security")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("price:price of security")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("YIELDDISC calculates the yield of a discounted security.") },
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "PRICE,DURATION"},
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
		sheet_date_conv (ei->pos->sheet);

	fPrice     = value_get_as_float (argv[2]);
	fRedemp    = value_get_as_float (argv[3]);
        basis      = value_get_basis (argv[4], GO_BASIS_MSRB_30_360);

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
        { GNM_FUNC_HELP_NAME, F_("YIELDMAT:yield of a security")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("issue:date of issue")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("price:price of security")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("YIELDMAT calculates the yield of a security for which the interest is paid at maturity date.") },
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "YIELDDISC,YIELD"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_yieldmat (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nSettle, nMat, nIssue;
	gnm_float fRate, fPrice;
	gint      basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	fRate      = value_get_as_float (argv[3]);
	fPrice     = value_get_as_float (argv[4]);
        basis      = value_get_basis (argv[5], GO_BASIS_MSRB_30_360);

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
        { GNM_FUNC_HELP_NAME, F_("ODDFPRICE:price of a security that has an odd first period")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("issue:date of issue")},
        { GNM_FUNC_HELP_ARG, F_("first_interest:first interest date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("yield:annual yield of security")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ODDFPRICE calculates the price per $100 face value of a security that pays periodic interest, but has an odd first period.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "ODDLPRICE,ODDFYIELD"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
date_ratio (GDate const *d1, const GDate *d2, const GDate *d3,
	    GoCouponConvention const *conv)
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
		return go_date_days_between_basis (d1, d2, conv->basis) /
			go_coupdays (&prev_coupon, &next_coupon, conv);

	res = go_date_days_between_basis (d1, &next_coupon, conv->basis) /
		go_coupdays (&prev_coupon, &next_coupon, conv);
	while (1) {
		prev_coupon = next_coupon;
		gnm_date_add_months (&next_coupon, 12 / conv->freq);
		if (!g_date_valid (&next_coupon))
			return gnm_nan;
		if (g_date_compare (&next_coupon, d2) >= 0) {
			gnm_float delta = go_date_days_between_basis (&prev_coupon, d2, conv->basis) /
				go_coupdays (&prev_coupon, &next_coupon, conv);
			res += delta;
			return res;
		}
		res += 1;
	}
}

static gnm_float
calc_oddfprice (const GDate *settlement, const GDate *maturity,
		const GDate *issue, const GDate *first_coupon,
		gnm_float rate, gnm_float yield, gnm_float redemption,
		GoCouponConvention const *conv)

{
	gnm_float a = go_date_days_between_basis (issue, settlement, conv->basis);
	gnm_float ds = go_date_days_between_basis (settlement, first_coupon, conv->basis);
	gnm_float df = go_date_days_between_basis (issue, first_coupon, conv->basis);
	gnm_float e = go_coupdays (settlement, maturity, conv);
	int n = (int)coupnum (settlement, maturity, conv);
	gnm_float scale = 100 * rate / conv->freq;
	gnm_float f = 1 + yield / conv->freq;
	gnm_float sum, term1, term2;

	if (ds > e) {
		/* Odd-long corrections.  */
		switch (conv->basis) {
		case GO_BASIS_MSRB_30_360:
		case GO_BASIS_30E_360: {
			int cdays = go_date_days_between_basis (first_coupon, maturity, conv->basis);
			n = 1 + (int)gnm_ceil (cdays / e);
			break;
		}

		default: {
			GDate d = *first_coupon;

			for (n = 0; 1; n++) {
				GDate prev_date = d;
				gnm_date_add_months (&d, 12 / conv->freq);
				if (g_date_compare (&d, maturity) >= 0) {
					n += (int)gnm_ceil (go_date_days_between_basis (&prev_date, maturity, conv->basis) /
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

	term1 = redemption / gnm_pow (f, n - 1 + ds / e);
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
	GoCouponConvention conv;

	rate       = value_get_as_float (argv[4]);
	yield      = value_get_as_float (argv[5]);
	redemption = value_get_as_float (argv[6]);

        conv.eom   = TRUE;
        conv.freq  = value_get_freq (argv[7]);
        conv.basis = value_get_basis (argv[8], GO_BASIS_MSRB_30_360);
	conv.date_conv = sheet_date_conv (ei->pos->sheet);

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

        if (rate < 0 || yield < 0 || redemption <= 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float
		(calc_oddfprice
		 (&settlement, &maturity, &issue, &first_coupon,
		  rate, yield, redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_oddfyield[] = {
        { GNM_FUNC_HELP_NAME, F_("ODDFYIELD:yield of a security that has an odd first period")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("issue:date of issue")},
        { GNM_FUNC_HELP_ARG, F_("first_interest:first interest date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("price:price of security")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ODDFYIELD calculates the yield of a security that pays periodic interest, but has an odd first period.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "ODDFPRICE,ODDLYIELD"},
	{ GNM_FUNC_HELP_END }
};

struct gnumeric_oddyield_f {
        GDate settlement, maturity, issue, first_coupon;
	gnm_float rate, price, redemption;
	GoCouponConvention conv;
};

static GnmGoalSeekStatus
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
	GnmGoalSeekData data;
	GnmGoalSeekStatus status;
	gnm_float yield0 = 0.1;

	udata.rate       = value_get_as_float (argv[4]);
	udata.price      = value_get_as_float (argv[5]);
	udata.redemption = value_get_as_float (argv[6]);

        udata.conv.eom   = TRUE;
        udata.conv.freq  = value_get_freq (argv[7]);
        udata.conv.basis = value_get_basis (argv[8], GO_BASIS_MSRB_30_360);
	udata.conv.date_conv = sheet_date_conv (ei->pos->sheet);

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

        if (udata.rate < 0 || udata.price <= 0 || udata.redemption <= 0)
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
        { GNM_FUNC_HELP_NAME, F_("ODDLPRICE:price of a security that has an odd last period")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("last_interest:last interest date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("yield:annual yield of security")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ODDLPRICE calculates the price per $100 face value of a security that pays periodic interest, but has an odd last period.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "YIELD,DURATION"},
	{ GNM_FUNC_HELP_END }
};

static gnm_float
calc_oddlprice (const GDate *settlement, const GDate *maturity,
		const GDate *last_interest,
		gnm_float rate, gnm_float yield, gnm_float redemption,
		GoCouponConvention *conv)
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
	GoCouponConvention conv;

	rate       = value_get_as_float (argv[3]);
	yield      = value_get_as_float (argv[4]);
	redemption = value_get_as_float (argv[5]);

        conv.eom   = TRUE;
        conv.freq  = value_get_freq (argv[6]);
        conv.basis = value_get_basis (argv[7], GO_BASIS_MSRB_30_360);
	conv.date_conv = sheet_date_conv (ei->pos->sheet);

	if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv) ||
	    !datetime_value_to_g (&last_interest, argv[2], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (conv.basis) ||
	    !is_valid_freq (conv.freq) ||
            g_date_compare (&settlement, &maturity) > 0 ||
	    g_date_compare (&last_interest, &settlement) > 0)
		return value_new_error_NUM (ei->pos);

        if (rate < 0 || yield < 0 || redemption <= 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float
		(calc_oddlprice
		 (&settlement, &maturity, &last_interest,
		  rate, yield, redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_oddlyield[] = {
        { GNM_FUNC_HELP_NAME, F_("ODDLYIELD:yield of a security that has an odd last period")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("last_interest:last interest date")},
        { GNM_FUNC_HELP_ARG, F_("rate:nominal annual interest rate")},
        { GNM_FUNC_HELP_ARG, F_("price:price of security")},
        { GNM_FUNC_HELP_ARG, F_("redemption:amount received at maturity")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ODDLYIELD calculates the yield of a security that pays periodic interest, but has an odd last period.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "YIELD,DURATION"},
	{ GNM_FUNC_HELP_END }
};


static gnm_float
calc_oddlyield (GDate const *settlement, GDate const *maturity,
		GDate const *last_interest,
		gnm_float rate, gnm_float price, gnm_float redemption,
		GoCouponConvention const *conv)
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
	GoCouponConvention conv;

	rate       = value_get_as_float (argv[3]);
	price      = value_get_as_float (argv[4]);
	redemption = value_get_as_float (argv[5]);

        conv.eom   = TRUE;
        conv.freq  = value_get_freq (argv[6]);
        conv.basis = value_get_basis (argv[7], GO_BASIS_MSRB_30_360);
	conv.date_conv = sheet_date_conv (ei->pos->sheet);

	if (!datetime_value_to_g (&settlement, argv[0], conv.date_conv) ||
	    !datetime_value_to_g (&maturity, argv[1], conv.date_conv) ||
	    !datetime_value_to_g (&last_interest, argv[2], conv.date_conv))
		return value_new_error_VALUE (ei->pos);

        if (!is_valid_basis (conv.basis) ||
	    !is_valid_freq (conv.freq) ||
            g_date_compare (&settlement, &maturity) > 0 ||
	    g_date_compare (&last_interest, &settlement) > 0)
		return value_new_error_NUM (ei->pos);

        if (rate < 0 || price <= 0 || redemption <= 0)
                return value_new_error_NUM (ei->pos);

	return value_new_float
		(calc_oddlyield
		 (&settlement, &maturity, &last_interest,
		  rate, price, redemption, &conv));
}

/***************************************************************************/

static GnmFuncHelp const help_amordegrc[] = {
        { GNM_FUNC_HELP_NAME, F_("AMORDEGRC:depreciation of an asset using French accounting conventions")},
        { GNM_FUNC_HELP_ARG, F_("cost:initial cost of asset")},
        { GNM_FUNC_HELP_ARG, F_("purchase_date:date of purchase")},
        { GNM_FUNC_HELP_ARG, F_("first_period:end of first period")},
        { GNM_FUNC_HELP_ARG, F_("salvage:value after depreciation")},
        { GNM_FUNC_HELP_ARG, F_("period:subject period")},
        { GNM_FUNC_HELP_ARG, F_("rate:depreciation rate")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
			"AMORDEGRC calculates the depreciation of an asset using French accounting conventions. "
			"Assets purchased in the middle of a period take prorated depreciation into account. "
			"This is similar to AMORLINC, except that a depreciation coefficient is applied in the "
			"calculation depending on the life of the assets.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The depreciation coefficient used is:\n"
					"1.0 for an expected lifetime less than 3 years,\n"
					"1.5 for an expected lifetime of at least 3 years but less than 5 years,\n"
					"2.0 for an expected lifetime of at least 5 years but at most 6 years,\n"
					"2.5 for an expected lifetime of more than 6 years.") },
	{ GNM_FUNC_HELP_NOTE, F_("Special depreciation rules are applied for the last two periods resulting in a possible total "
				 "depreciation exceeding the difference of @{cost} - @{salvage}.") },
	{ GNM_FUNC_HELP_NOTE, F_("Named for AMORtissement DEGRessif Comptabilite.") },
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=AMORDEGRC(2400,DATE(1998,8,19),DATE(1998,12,30),300,1,0.14,1)" },
        { GNM_FUNC_HELP_SEEALSO, "AMORLINC"},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_amordegrc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nDate, nFirstPer;
	gnm_float fRestVal, fRate, fCost;
	gint      basis, nPer;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	fCost      = value_get_as_float (argv[0]);
	fRestVal   = value_get_as_float (argv[3]);
        nPer       = value_get_as_int (argv[4]);
	fRate      = value_get_as_float (argv[5]);
        basis      = value_get_basis (argv[6], GO_BASIS_MSRB_30_360);

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
        { GNM_FUNC_HELP_NAME, F_("AMORLINC:depreciation of an asset using French accounting conventions")},
        { GNM_FUNC_HELP_ARG, F_("cost:initial cost of asset")},
        { GNM_FUNC_HELP_ARG, F_("purchase_date:date of purchase")},
        { GNM_FUNC_HELP_ARG, F_("first_period:end of first period")},
        { GNM_FUNC_HELP_ARG, F_("salvage:value after depreciation")},
        { GNM_FUNC_HELP_ARG, F_("period:subject period")},
        { GNM_FUNC_HELP_ARG, F_("rate:depreciation rate")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
			"AMORLINC calculates the depreciation of an asset using French accounting conventions. "
			"Assets purchased in the middle of a period take prorated depreciation into account.") },
	{ GNM_FUNC_HELP_NOTE, F_("Named for AMORtissement LINeaire Comptabilite.") },
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=AMORLINC(2400,DATE(1998,8,19),DATE(1998,12,30),300,1,0.14,1)" },
        { GNM_FUNC_HELP_SEEALSO, "AMORDEGRC"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_amorlinc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nDate, nFirstPer;
	gnm_float fCost, fRestVal, fRate;
	gint      nPer, basis;
	GODateConventions const *date_conv =
		sheet_date_conv (ei->pos->sheet);

	fCost      = value_get_as_float (argv[0]);
	fRestVal   = value_get_as_float (argv[3]);
        nPer       = value_get_as_int (argv[4]);
	fRate      = value_get_as_float (argv[5]);
        basis      = value_get_basis (argv[6], GO_BASIS_MSRB_30_360);

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
        { GNM_FUNC_HELP_NAME, F_("COUPDAYBS:number of days from coupon period to settlement")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
        { GNM_FUNC_HELP_ARG, F_("eom:end-of-month flag")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COUPDAYBS calculates the number of days from the beginning of the coupon period to the settlement date.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPDAYBS(DATE(2002,11,29),DATE(2004,2,29),4,0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPDAYBS(DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE)" },
        { GNM_FUNC_HELP_SEEALSO, "COUPDAYS"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupdaybs (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, go_coupdaybs);
}

/***************************************************************************/

static GnmFuncHelp const help_coupdays[] = {
        { GNM_FUNC_HELP_NAME, F_("COUPDAYS:number of days in the coupon period of the settlement date")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
        { GNM_FUNC_HELP_ARG, F_("eom:end-of-month flag")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COUPDAYS calculates the number of days in the coupon period of the settlement date.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPDAYS(DATE(2002,11,29),DATE(2004,2,29),4,0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPDAYS(DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE)" },
        { GNM_FUNC_HELP_SEEALSO, "COUPDAYBS,COUPDAYSNC"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupdays (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, go_coupdays);
}

/***************************************************************************/

static GnmFuncHelp const help_coupdaysnc[] = {
        { GNM_FUNC_HELP_NAME, F_("COUPDAYSNC:number of days from the settlement date to the next coupon period")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
        { GNM_FUNC_HELP_ARG, F_("eom:end-of-month flag")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COUPDAYSNC calculates number of days from the settlement date to the next coupon period.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPDAYSNC(DATE(2002,11,29),DATE(2004,2,29),4,0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPDAYSNC(DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE)" },
        { GNM_FUNC_HELP_SEEALSO, "COUPDAYS,COUPDAYBS"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupdaysnc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, go_coupdaysnc);
}

/***************************************************************************/

static GnmFuncHelp const help_coupncd[] = {
        { GNM_FUNC_HELP_NAME, F_("COUPNCD:the next coupon date after settlement")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
        { GNM_FUNC_HELP_ARG, F_("eom:end-of-month flag")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COUPNCD calculates the coupon date following settlement.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPNCD(DATE(2002,11,29),DATE(2004,2,29),4,0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPNCD(DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE)" },
        { GNM_FUNC_HELP_SEEALSO, "COUPPCD,COUPDAYS,COUPDAYBS"},
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
        { GNM_FUNC_HELP_NAME, F_("COUPPCD:the last coupon date before settlement")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
        { GNM_FUNC_HELP_ARG, F_("eom:end-of-month flag")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COUPPCD calculates the coupon date preceding settlement.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPPCD(DATE(2002,11,29),DATE(2004,2,29),4,0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPPCD(DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE)" },
        { GNM_FUNC_HELP_SEEALSO, "COUPNCD,COUPDAYS,COUPDAYBS"},
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
        { GNM_FUNC_HELP_NAME, F_("COUPNUM:number of coupons")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
        { GNM_FUNC_HELP_ARG, F_("eom:end-of-month flag")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COUPNUM calculates the number of coupons to be paid between the settlement and maturity dates, rounded up.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPNUM(DATE(2002,11,29),DATE(2004,2,29),4,0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COUPNUM(DATE(2002,11,29),DATE(2004,2,29),4,0,FALSE)" },
        { GNM_FUNC_HELP_SEEALSO, "COUPNCD,COUPPCD"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coupnum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return func_coup (ei, argv, coupnum);
}

/***************************************************************************/

static GnmFuncHelp const help_cumipmt[] = {
        { GNM_FUNC_HELP_NAME, F_("CUMIPMT:cumulative interest payment")},
        { GNM_FUNC_HELP_ARG, F_("rate:interest rate per period")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("start_period:first period to accumulate for")},
        { GNM_FUNC_HELP_ARG, F_("end_period:last period to accumulate for")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CUMIPMT calculates the cumulative interest paid on a loan from @{start_period} to @{end_period}.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_SEEALSO, "IPMT"},
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
        { GNM_FUNC_HELP_NAME, F_("CUMPRINC:cumulative principal")},
        { GNM_FUNC_HELP_ARG, F_("rate:interest rate per period")},
        { GNM_FUNC_HELP_ARG, F_("nper:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("pv:present value")},
        { GNM_FUNC_HELP_ARG, F_("start_period:first period to accumulate for")},
        { GNM_FUNC_HELP_ARG, F_("end_period:last period to accumulate for")},
        { GNM_FUNC_HELP_ARG, F_("type:payment type")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CUMPRINC calculates the cumulative principal paid on a loan from @{start_period} to @{end_period}.") },
	TYPE_HELP,
        { GNM_FUNC_HELP_SEEALSO, "PPMT"},
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
        { GNM_FUNC_HELP_NAME, F_("MDURATION:the modified (Macaulay) duration of a security")},
        { GNM_FUNC_HELP_ARG, F_("settlement:settlement date")},
        { GNM_FUNC_HELP_ARG, F_("maturity:maturity date")},
        { GNM_FUNC_HELP_ARG, F_("coupon:annual coupon rate")},
        { GNM_FUNC_HELP_ARG, F_("yield:annual yield of security")},
        { GNM_FUNC_HELP_ARG, F_("frequency:number of interest payments per year")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("MDURATION calculates the modified (Macaulay) duration of a security.") },
	FREQ_HELP,
	GNM_DATE_BASIS_HELP
	{ GNM_FUNC_HELP_EXAMPLES, "=MDURATION(TODAY(),TODAY()+365,0.05,0.08,4)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=MDURATION(TODAY(),TODAY()+366,0.05,0.08,4)"},
        { GNM_FUNC_HELP_SEEALSO, "DURATION,G_DURATION"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_mduration (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GDate     nSettle, nMat;
	gnm_float fCoup, fYield;
        gnm_float fNumOfCoups;
	GoCouponConvention conv;

	conv.date_conv = sheet_date_conv (ei->pos->sheet);
	conv.eom = TRUE;

	fCoup      = value_get_as_float (argv[2]);
	fYield     = value_get_as_float (argv[3]);
	conv.freq  = value_get_freq (argv[4]);
        conv.basis = value_get_basis (argv[5], GO_BASIS_MSRB_30_360);
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
        { GNM_FUNC_HELP_NAME, F_("VDB:depreciation of an asset")},
        { GNM_FUNC_HELP_ARG, F_("cost:initial cost of asset")},
        { GNM_FUNC_HELP_ARG, F_("salvage:value after depreciation")},
        { GNM_FUNC_HELP_ARG, F_("life:number of periods")},
        { GNM_FUNC_HELP_ARG, F_("start_period:first period to accumulate for")},
        { GNM_FUNC_HELP_ARG, F_("end_period:last period to accumulate for")},
        { GNM_FUNC_HELP_ARG, F_("factor:factor at which the balance declines")},
        { GNM_FUNC_HELP_ARG, F_("no_switch:do not switch to straight-line depreciation")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("VDB calculates the depreciation of an asset for a given period range using the variable-rate declining balance method.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{no_switch} is FALSE, the calculation switches to straight-line depreciation when depreciation is greater than the declining balance calculation.") },
        { GNM_FUNC_HELP_SEEALSO, "DB,DDB"},
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
	{ "accrint", "ffff|fffb",
	  help_accrint, gnumeric_accrint, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "accrintm", "fff|ff",
	  help_accrintm, gnumeric_accrintm, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "amordegrc", "fffffff",
	  help_amordegrc, gnumeric_amordegrc, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "amorlinc", "fffffff",
	  help_amorlinc, gnumeric_amorlinc, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupdaybs", "fff|fb",
	  help_coupdaybs, gnumeric_coupdaybs, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupdays", "fff|fb",
	  help_coupdays, gnumeric_coupdays, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupdaysnc", "fff|fb",
	  help_coupdaysnc, gnumeric_coupdaysnc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupncd", "fff|fb",
	  help_coupncd, gnumeric_coupncd, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "coupnum", "fff|fb",
	  help_coupnum, gnumeric_coupnum, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "couppcd", "fff|fb",
	  help_couppcd, gnumeric_couppcd, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "cumipmt", "ffffff",
	  help_cumipmt, gnumeric_cumipmt, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "cumprinc", "ffffff",
	  help_cumprinc, gnumeric_cumprinc, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "db", "ffff|f",
	  help_db, gnumeric_db, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ddb", "ffff|f",
	  help_ddb, gnumeric_ddb, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "disc", "ffff|f",
	  help_disc, gnumeric_disc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dollarde", "ff",
	  help_dollarde, gnumeric_dollarde, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "dollarfr", "ff",
	  help_dollarfr, gnumeric_dollarfr, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "duration", "fffff|f",
	  help_duration, gnumeric_duration, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "effect", "ff",
	  help_effect,	  gnumeric_effect, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "euro", "s",
	  help_euro,	  gnumeric_euro, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{ "euroconvert", "fss|bf",
	  help_euroconvert, gnumeric_euroconvert, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fv", "fff|ff",
	  help_fv,	  gnumeric_fv, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "fvschedule", "fA",
	  help_fvschedule, gnumeric_fvschedule, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "g_duration", "fff",
	  help_g_duration, gnumeric_g_duration, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{ "intrate", "ffff|f",
	  help_intrate,  gnumeric_intrate, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ipmt", "ffff|ff",
	  help_ipmt,	  gnumeric_ipmt, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "irr", "A|f",
	  help_irr,	  gnumeric_irr, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ispmt", "ffff",
	  help_ispmt,	gnumeric_ispmt, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mduration", "fffff|f",
	  help_mduration, gnumeric_mduration, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mirr", "Aff",
	  help_mirr,	  gnumeric_mirr, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "nominal", "ff",
	  help_nominal,  gnumeric_nominal, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "nper", "fff|ff",
	  help_nper,	  gnumeric_nper, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "npv", NULL,
	  help_npv,	  NULL, gnumeric_npv,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "oddfprice", "fffffffff",
	  help_oddfprice,  gnumeric_oddfprice, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "oddfyield", "fffffffff",
	  help_oddfyield,  gnumeric_oddfyield, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "oddlprice", "ffffffff",
	  help_oddlprice,  gnumeric_oddlprice, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "oddlyield", "ffffffff",
	  help_oddlyield,  gnumeric_oddlyield, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pmt", "fff|ff",
	  help_pmt,	  gnumeric_pmt, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ppmt", "ffff|ff",
	  help_ppmt,	  gnumeric_ppmt, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "price", "ffffff|f",
	  help_price, gnumeric_price, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pricedisc", "ffff|f",
	  help_pricedisc,  gnumeric_pricedisc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pricemat", "fffff|f",
	  help_pricemat,  gnumeric_pricemat, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pv", "fff|ff",
	  help_pv,	  gnumeric_pv, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rate", "fff|fff",
	  help_rate,	  gnumeric_rate, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rri", "fff",
	  help_rri,	  gnumeric_rri, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "received", "ffff|f",
	  help_received,  gnumeric_received, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sln", "fff",
	  help_sln,	  gnumeric_sln, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "syd", "ffff",
	  help_syd,	  gnumeric_syd, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tbilleq", "fff",
	  help_tbilleq,  gnumeric_tbilleq, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tbillprice", "fff",
	  help_tbillprice, gnumeric_tbillprice, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tbillyield", "fff",
	  help_tbillyield, gnumeric_tbillyield, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "vdb", "fffff|ff",
	  help_vdb, gnumeric_vdb, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "xirr", "AA|f",
	  help_xirr,	  gnumeric_xirr, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "xnpv", "fAA",
	  help_xnpv,	  gnumeric_xnpv, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_MONETARY,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yield", "ffffff|f",
	  help_yield, gnumeric_yield, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_PERCENT,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yielddisc", "ffff|f",
	  help_yielddisc,  gnumeric_yielddisc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yieldmat", "fffff|f",
	  help_yieldmat,  gnumeric_yieldmat, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        {NULL}
};
