/*
 * fn-financial.c:  Built in financial functions and functions registration
 *
 * Authors:
 *  Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 *  Morten Welinder (terra@diku.dk)
 *  Vladimir Vuksan (vuksan@veus.hr)
 *

 */
#include <config.h>
#include "func.h"
#include "parse-util.h"
#include "cell.h"
#include "goal-seek.h"
#include "collect.h"
#include "auto-format.h"
#include "datetime.h"
#include "str.h"

#include <math.h>
#include <limits.h>
#include <string.h>


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
 *	                k
 *
 *
 *
 *	 PV * PVIF(k%, nper) + PMT * ( 1 + rate * type ) *
 *	      FVIFA(k%, nper) + FV = 0
 *
 */

static gnum_float
calculate_pvif (gnum_float rate, gnum_float nper)
{
	return (pow (1 + rate, nper));
}

#if 0
static gnum_float
calculate_fvif (gnum_float rate, gnum_float nper)
{
	return (1.0 / calculate_pvif (rate,nper));
}

static gnum_float
calculate_pvifa (gnum_float rate, gnum_float nper)
{
	return ((1.0 / rate) - (1.0 / (rate * pow (1 + rate, nper))));
}
#endif

static gnum_float
calculate_fvifa (gnum_float rate, gnum_float nper)
{
	return ((pow (1 + rate, nper) - 1) / rate);
}


/*
 *
 * Principal for period x is calculated using this formula
 *
 * PR(x) = PR(0) * ( 1 + rate ) ^ x + PMT * ( ( 1 + rate ) ^ x - 1 ) / rate )
 *
 */
static gnum_float
calculate_principal (gnum_float starting_principal, gnum_float payment,
		     gnum_float rate, gnum_float period)
{
	return (starting_principal * pow (1.0 + rate, period) + payment *
		((pow (1 + rate, period) - 1) / rate));
}

static gnum_float
calculate_pmt (gnum_float rate, gnum_float nper, gnum_float pv, gnum_float fv,
	       int type)
{
	gnum_float pvif, fvifa;

	/* Calculate the PVIF and FVIFA */

	pvif = calculate_pvif (rate,nper);
	fvifa = calculate_fvifa (rate,nper);

        return ((-pv * pvif - fv ) / ((1.0 + rate * type) * fvifa));
}

/***************************************************************************/

/*
 * Returns the number of days in the year for the given date accoring to
 * the day counting system specified by 'basis' argument.  Basis may have
 * one of the following values:
 *
 *	0  for US 30/360 (days in a month/days in a year)
 *	1  for actual days/actual days
 *	2  for actual days/360
 *	3  for actual days/365
 *	4  for European 30/360
 *
 * This function returns 360 for basis 0, 2, and 4, it returns value
 * 365 for basis 3, and value 365 or 366 for basis 1 accoring to the
 * year of the given date (366 is returned if the date is in a leap
 * year).
 */
static int
annual_year_basis (Value *value_date, int basis)
{
        GDate    *date;
        gboolean leap_year;

	switch (basis) {
	case 0:
	        return 360;
	case 1:
	        date = datetime_value_to_g (value_date);
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
days_monthly_basis (Value *issue_date, Value *maturity_date, int basis)
{
        GDate    *date_i, *date_m;
	int      issue_day, issue_month, issue_year;
	int      maturity_day, maturity_month, maturity_year;
        int      months, days, years;
	gboolean leap_year;
	int      maturity, issue;

	date_i = datetime_value_to_g (issue_date);
	date_m = datetime_value_to_g (maturity_date);
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
	        if (date_i) g_date_free (date_i);
	        if (date_m) g_date_free (date_m);
	        return -1;
	}

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
	        issue = datetime_value_to_serial (issue_date);
	        maturity = datetime_value_to_serial (maturity_date);
	        return maturity - issue;
	case 4:
	        return months * 30 + days;
	default:
	        return -1;
	}
}

static GDateDay
days_in_month (GDateYear year, GDateMonth month)
{
        switch (month) {
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
	        return 31;
	case 4:
	case 6:
	case 9:
	case 11:
	        return 30;
	case 2:
	        if (g_date_is_leap_year (year))
		        return 29;
		else
		        return 28;
	default:
	        return 0;
	}
}

/***************************************************************************/

/*
 * Returns the number of days in the coupon period of the settlement date.
 * Currently, returns negative numbers if the branch is not implemented.
 */
static gnum_float
coupdays (GDate *settlement, GDate *maturity, int freq, int basis)
{
        GDateYear  sy, my;
	GDateMonth sm, mm;
	GDateDay   sd, md;

        switch (basis) {
        case 0:
        case 2:
        case 4:
                if (freq == 1)
                        return 360.0;
                else if (freq == 2)
                        return 180.0;
                else
                        return 90.0;
                break;
        case 3:
                return 365.0 / freq;
        case 1:
		sy = g_date_year  (settlement);
		my = g_date_year  (maturity);
		sm = g_date_month (settlement);
		mm = g_date_month (maturity);
		sd = g_date_day   (settlement);
		md = g_date_day   (maturity);

	        if (freq == 1) {
		        if (g_date_is_leap_year (sy)) {
			        if (sm <= 2) {
				        if (mm <= 2) {
						if (sm < mm ||
						    (sm == mm && sd < md))
						        return 365.0;
						else
						        return 366.0;
					}
					return 366.0;
				}
				if (mm <= 2) {
				        if (mm == 2 && md >= 28)
				                return 365.0;
					else
					        return 366.0;
				}
				if (sm < mm || (sm == mm && sd < md))
				        return 366.0;
				else
				        return 365.0;
			} else if (g_date_is_leap_year (sy - 1)) {
			        if (sm <= 2)
				        if (mm <= 2)
				                if (sm < mm ||
						    (sm == mm && sd < md))
						        return 366.0;
					        else
					                return 365.0;
					else
					        return 365.0;
				else
				        return 365.0;
			} else if (g_date_is_leap_year (sy + 1)) {
			        if (sm <= 2)
			                return 365.0;
				if (mm <= 2)
				        return 365.0;
				if (sm < mm || (sm == mm && sd < md))
					return 365.0;
				else
				        return 366.0;
			} else
			        return 365.0;
		} else if (freq == 2) {
		  switch (mm) {
		  case 1:
		          if (sm >= 8 || (sm == 7 && sd >= md)
			      || (sm == 1 && sd < md))
			          return 184.0;
			  else if (g_date_is_leap_year (sy))
			          return 182.0;
			  else
			          return 181.0;
		  case 2:
		          if (sm >= 9 || (sm == 8 && sd >= md) || sm == 1
			      || (sm == 2 && sd < md))
			          if (md < 28)
				          return 184.0;
				  else if (sm >= 9)
				          if (g_date_is_leap_year (sy + 1))
					          return 182.0 + 2*(md == 28);
					  else
				                  return 181.0;
				  else if (sm <= 2)
				          if (g_date_is_leap_year (sy) )
					          if (md == 28)
						          return 184.0;
						  else if (g_date_is_leap_year
							   (md))
						          return 184.0;
						  else
						          return 182.0;
					  else
				                  return 181.0;
				  else if (sm <= 7)
				          if (md == 28)
					          if (g_date_is_leap_year (sy))
						          return 182.0;
						  else
						          return 181.0;
					  else
				                  if (g_date_is_leap_year (sy)
						      ||
						      g_date_is_leap_year (sy + 1))
						          return 182.0;
						  else
						          return 181.0;
				  else if (md == 28) /* sm == 8 */
				          if (sm < 28)
					          if (g_date_is_leap_year (sy))
						          return 182.0;
						  else
						          return 181.0;
					  else
				                  return 184.0;
				  else
				          if (g_date_is_leap_year (sy) ||
					      g_date_is_leap_year (sy + 1))
					          return 182.0;
					  else
				                  return 181.0;
			  else if (g_date_is_leap_year (sy))
			          return 182.0;
			  else
			          return 181.0;
		  case 3:
		          if ((sm >= 4 && sm <= 8) || (sm == 9 && sd < md)
			      || (sm == 3 && sd >= md))
			          return 183.0 + (md < 30);
			  else if (sm <= 3)
			          if (g_date_is_leap_year (sy))
				          return 182.0;
				  else
				          return 181.0;
			  else
			          if (g_date_is_leap_year (sy + 1))
				          return 182.0;
				  else
				          return 181.0;
		  case 4:
		          if ((sm >= 5 && sm <= 9) || (sm == 10 && sd < md)
			      || (sm == 4 && sd >= md))
			          return 183.0;
			  else if (sm <= 4)
			          if (g_date_is_leap_year (sy))
				          return 183.0 - (md == 30);
				  else
				          return 182.0 - (md == 30);
			  else if (g_date_is_leap_year (sy + 1))
			          return 183.0 - (md == 30);
			  else
			          return 182.0 - (md == 30);
		  case 5:
		          if ((sm >= 6 && sm <= 10) || (sm == 11 && sd < md)
			      || (sm == 5 && sd >= md))
			          return 183.0 + (md < 30);
			  else
			          if (sm <= 5)
				          if (g_date_is_leap_year (sy))
					          return 182.0;
					  else
				                  return 181.0;
				  else
				          if (g_date_is_leap_year (sy + 1))
					          return 182.0;
					  else
				                  return 181.0;
		  case 6:
		          if ((sm >= 7 && sm <= 11) || (sm == 12 && sd < md)
			      || (sm == 6 && sd >= md))
			          return 183.0;
			  else
			          if (sm <= 6)
				          if (g_date_is_leap_year (sy))
					          return 183.0 - (md == 30);
					  else
				                  return 182.0 - (md == 30);
				  else
				          if (g_date_is_leap_year (sy + 1))
					          return 183.0 - (md == 30);
					  else
				                  return 182.0 - (md == 30);
		  case 7:
		          if ((sm >= 8 && sm <= 12) || (sm == 7 && sd >= md)
			      || (sm == 1 && sd < md))
			          return 184.0;
			  else
			          if (g_date_is_leap_year (sy))
				          return 182.0;
				  else
				          return 181.0;
		  case 8:
		          if (sm == 1 || sm >= 9 || (sm == 2 && sd < md)
			      || (sm == 8 && sd >= md))
			          if (md < 28)
				          return 184.0;
				  else if (sm <= 2)
				          if (g_date_is_leap_year (sy))
					          return 184.0 -
						    2*(sm == 2 && sy == my);
					  else
				                  return 181.0;
				  else
				          if (g_date_is_leap_year (sy + 1))
					          if (md == 28 ||
						      !g_date_is_leap_year (my))
						          return 184.0;
						  else
						          return 182.0;
					  else
				                  return 181.0;
			  else
			          if (g_date_is_leap_year (sy)
				      || (md >= 29 &&
					  ((g_date_is_leap_year (sy + 1)
					    && g_date_is_leap_year (my)
					    && sy + 1 == my)
					   || my == sy)))
				          return 182.0;
				  else
				          return 181.0;
		  case 9:
		          if ((sm >= 4 && sm <= 8) || (sm == 9 && sd < md)
			      || (sm == 3 && sd >= md))
			          return 183.0 + (md < 30);
			  else
			          if (sm <= 3)
				          if (g_date_is_leap_year (sy))
					          return 182.0;
					  else
				                  return 181.0;
				  else
				          if (g_date_is_leap_year (sy + 1))
					          return 182.0;
					  else
				                  return 181.0;
		  case 10:
		          if ((sm >= 5 && sm <= 9) || (sm == 10 && sd < md)
			      || (sm == 4 && sd >= md))
			          return 183.0;
			  else
			          if (sm <= 4)
				          if (g_date_is_leap_year (sy))
					          return 183.0 - (md >= 30);
					  else
				                  return 182.0 - (md >= 30);
				  else
				          if (g_date_is_leap_year (sy + 1))
					          return 183.0 - (md >= 30);
					  else
				                  return 182.0 - (md >= 30);
		  case 11:
		          if ((sm >= 6 && sm <= 10) || (sm == 11 && sd < md)
			      || (sm == 5 && sd >= md))
			          return 183.0 + (md < 30);
			  else
			          if (sm <= 5)
				          if (g_date_is_leap_year (sy))
					          return 182.0;
					  else
				                  return 181.0;
				  else
				          if (g_date_is_leap_year (sy + 1))
					          return 182.0;
					  else
				                  return 181.0;
		  case 12:
		          if ((sm >= 7 && sm <= 11) || (sm == 12 && sd < md)
			      || (sm == 6 && sd >= md))
			          return 183.0;
			  else
			          if (sm <= 6)
				          if (g_date_is_leap_year (sy))
					          return 183.0 - (md >= 30);
					  else
				                  return 182.0 - (md >= 30);
				  else
				          if (g_date_is_leap_year (sy + 1))
					          return 183.0 - (md >= 30);
					  else
				                  return 182.0 - (md >= 30);
		  default:
		          return -1.0;  /* Should never occur */
		  }
		} else {
		        /* Frequency 4 */

		        return -1.0;
		}
        default:
                return -1.0;
        }
}

/*
 * Returns the number of days from the beginning of the coupon period to
 * the settlement date.  Currently, returns negative numbers if the branch
 * is not implemented.
 */
static int
coupdaybs (GDate *settlement, GDate *maturity, int freq, int basis)
{
        int        d, months, days;
	GDateYear  sy, my;
	GDateMonth sm, mm;
	GDateDay   sd, md;

	sy = g_date_year  (settlement);
	my = g_date_year  (maturity);
	sm = g_date_month (settlement);
	mm = g_date_month (maturity);
	sd = g_date_day   (settlement);
	md = g_date_day   (maturity);

	months = mm - sm;

        switch (basis) {
	case 0: /* US 30/360 */
	        if (freq == 1) {
	                if (sd == 31)
			        if (md == 31 || (md == 30))
				        sd = 30;

			if (md == 31 || (md == 29 && mm == 2)
			    || (md == 28 && mm == 2))
			        md = 30;

			days = md - sd;
			d = 360 - months*30 - days;

			if (d >= 360) {
			        if (days == 0 && months == 0)
				        d = 0;
				else if (d % 360 == 0)
				        d = 360;
				else
				        d %= 360;
			}
		} else if (freq == 2) {
		        d = -1;  /* FIXME */
		} else {
		        d = -1;  /* FIXME */
		}

		return d;
	case 1:
	case 2:
	case 3:
	        return -1;
	case 4: /* European 30/360 */
	        if (freq == 1) {
		        if (sd == 31)
			        sd = 30;

			if (md == 29 && mm == 2 && g_date_is_leap_year (my))
			        md = 28;
			else if (md == 31)
			        md = 30;
			else if (md == 28 && mm == 2
				 &&   g_date_is_leap_year (sy)
				 && ! g_date_is_leap_year (my))
			        md = 29;

			days = md - sd;
			d = 360 - months*30 - days;

			if (d >= 360) {
			        if (days == 0 && months == 0)
				        d = 0;
				else if (d % 360 == 0)
				        d = 360;
				else
				        d %= 360;
			}

		} else if (freq == 2) {
		        d = -1;  /* FIXME */
		} else {
		        d = -1;  /* FIXME */
		}
		return d;
	default:
	        return -1;
	}
}

static int
coupdaysnc_b123 (GDate *date1, GDateYear y2, GDateMonth m2, GDateDay d2,
		 GDateYear new_y)
{
        GDateYear  y1;
	GDate      *date2;
	gint       serial1, serial2;

	y1 = g_date_year (date1);
	serial1 = g_date_julian (date1);

	if (d2 == 29 && m2 == 2 && !g_date_is_leap_year (new_y))
	        date2 = g_date_new_dmy (28, m2, new_y);
	else if (d2 == 28 && m2 == 2
		 && !g_date_is_leap_year (y2)
		 && g_date_is_leap_year (new_y))
	        date2 = g_date_new_dmy (29, m2, new_y);
	else
	        date2 = g_date_new_dmy (d2, m2, new_y);

	serial2 = g_date_julian (date2);
	g_date_free (date2);

	return serial2 - serial1;
}

/*
 * Returns the number of days from the settlement date to the next
 * coupon date.
 */
static int
coupdaysnc (GDate *settlement, GDate *maturity, int freq, int basis)
{
        GDateYear  sy, my;
	GDateMonth sm, mm;
	GDateDay   sd, md;
	gint       days, months;

	sy = g_date_year (settlement);
	sm = g_date_month (settlement);
	sd = g_date_day (settlement);
	my = g_date_year (maturity);
	mm = g_date_month (maturity);
	md = g_date_day (maturity);

        switch (basis) {
	case 0: /* US 30/360 */
	        if (freq == 1) {
		        if (sd == 31)
			        if (md == 31 || (md == 30))
				        sd = 30;

			if (md == 29 && mm == 2) {
			        if (sd == 28 && sm == 2) {
				        if (g_date_is_leap_year (sy))
					        return 2;
					else
					        return 360;
				} else if (sd == 29 && sm == 2)
				        return 360;
				else
				        md = 30;
			} else if (md == 31)
			        md = 30;
			else if (md == 28 && mm == 2) {
			        if (sm > 2) {
				        if (!g_date_is_leap_year (sy + 1) ||
					    !g_date_is_leap_year (my))
					        md = 30;
				} else if (sd == 28 && sm == 2) {
				        if (g_date_is_leap_year (sy))
					        if (g_date_is_leap_year (my))
						        return 362;
						else
						        md = 30;
					else if (g_date_is_leap_year (sy + 1) &&
						 g_date_is_leap_year (my))
					        return 358;
					else
					        return 360;
				} else if (sd == 29 && sm == 2) {
				        if (g_date_is_leap_year (my))
					        return 361;
					else
					        return 360;
				} else if (!g_date_is_leap_year (sy) ||
					   !g_date_is_leap_year (my))
				        md = 30;
			}

			months = mm - sm;
			days = months*30 + md - sd;
			if (days > 0)
			        return days;
			else
			        return 360 + days;
		} else if (freq == 2) {
		        return -1; /* FIXME: the frequency 2 */
		} else {
		        return -1; /* FIXME: the frequency 4 */
		}
	case 1: /* Actual days/actual days */
	case 2: /* Actual days/360 */
	case 3: /* Actual days/365 */
	        days = coupdaysnc_b123 (settlement, my, mm, md, sy);
		if (freq == 1) {
		        if (days > 0)
			        return days;
			return coupdaysnc_b123 (settlement, my, mm, md, sy + 1);
		} else if (freq == 2) {
		        return -1; /* FIXME: the frequency 2 */
		} else {
		        return -1; /* FIXME: the frequency 4 */
		}
	case 4: /* European 30/360 */
	        if (freq == 1) {
		        if (sd == 31)
			        sd = 30;

			if (md == 29 && mm == 2) {
			        if (sm <= 2) {
				        if (sd == 29 && sm == 2)
					        return 359;
					else if (! g_date_is_leap_year (sy)) {
					        if (sm == 2 && sd == 28 &&
						    g_date_is_leap_year (sy + 1))
						        return 361;
						else
						        md = 28;
					}
				} else if (! g_date_is_leap_year (sy + 1))
				        md = 28;
			} else if (md == 31)
			        md = 30;
			else if (md == 28 && mm == 2
				 && !g_date_is_leap_year (my)) {
			        if (sm > 2) {
				        if (g_date_is_leap_year (sy + 1))
					        md = 29;
				} else if (g_date_is_leap_year (sy))
				        md = 29;
				else if (g_date_is_leap_year (sy + 1) && sm == 2
					 && sd == 28)
				        return 361;
			}

			months = mm - sm;
		        days = months*30 + md - sd;
			if (days > 0)
			        return days;
			else
			        return 360 + days;
		} else if (freq == 2) {
		        return -1; /* FIXME: the frequency 2 */
		} else {
		        return -1; /* FIXME: the frequency 4 */
		}
	default:
	        return -1; /* FIXME */
	}
}

/* Returns the numbers of coupons to be paid between the settlement
 * and maturity dates, rounded up.
 */
static int
coupnum (GDate *settlement, GDate *maturity, int freq, int basis)
{
        int        years, months, days;
	GDateYear  sy, my;
	GDateMonth sm, mm;
	GDateDay   sd, md;

	sy = g_date_year  (settlement);
	sm = g_date_month (settlement);
	sd = g_date_day   (settlement);
	my = g_date_year  (maturity);
	mm = g_date_month (maturity);
	md = g_date_day   (maturity);

	years = my - sy;
	months = mm - sm;
	if (md == days_in_month (my, mm) && sd == days_in_month (sy, sm))
	        days = 0;
	else
	        days = md - sd + (g_date_is_leap_year (sy) && sd == 28 && sm == 2);

	if (freq == 1)
	        return years + (months > 0) + (months == 0 && days > 0);
	else if (freq == 2)
	        return years*2
		        + (months >  6) + (months ==  6 && days > 0)
		        + (months >  0) + (months ==  0 && days > 0)
		        - (months < -6) - (months == -6 && days <= 0);
	else
	        return years*4
		        + (months >  9) + (months ==  9 && days > 0)
		        + (months >  6) + (months ==  6 && days > 0)
		        + (months >  3) + (months ==  3 && days > 0)
		        + (months >  0) + (months ==  0 && days > 0)
		        - (months < -3) - (months == -3 && days <= 0)
		        - (months < -6) - (months == -6 && days <= 0)
		        - (months < -9) - (months == -9 && days <= 0);
}

static GDate *
coupncd (GDate *settlement, GDate *maturity, int freq, int basis)
{
        int        months, days;
	GDateYear  sy, my, year;
	GDateMonth sm, mm, month;
	GDateDay   sd, md, day;

	sy = g_date_year (settlement);
	sm = g_date_month (settlement);
	sd = g_date_day (settlement);
	my = g_date_year (maturity);
	mm = g_date_month (maturity);
	md = g_date_day (maturity);

	if (freq == 1) {
	        day = md;
		month = mm;
		if (sy ==  my || sm < mm || (sm == mm && sd < md))
		        year = sy;
		else
		        year = sy + 1;
	} else if (freq == 2) {
	        days = md - sd;
		months = mm - sm;

		if ((months > 0 || (months == 0 && days > 0)) &&
		    (months < 6 || (months == 6 && days <= 0))) {
		        month = mm;
			year = sy;
		} else if (months > 6 || (months == 6 && days > 0)) {
		        month = mm - 6;
			year = sy;
		} else if (months < -6 || (months == -6 && days <= 0)) {
		        month = mm;
			year = sy + 1;
		} else {
		        month = mm + 6;
			year = sy;
			if (month > 12) {
			        month -= 12;
				year++;
			}
		}
		if (md == days_in_month (my, mm))
		        day = days_in_month (year, month);
		else
		        day = md;
	}  else {
	        days = md - sd;
		months = mm - sm;

		if ((months > 0 || (months == 0 && days > 0)) &&
		    (months < 3 || (months == 3 && days <= 0))) {
		        month = mm;
			year = sy;
		} else if ((months > 3 || (months == 3 && days > 0))
			   && (months < 6 || (months == 6 && days <= 0))) {
		        month = mm - 3;
			year = sy;
		} else if ((months > 6 || (months == 6 && days > 0))
			   && (months < 9 || (months == 9 && days <= 0))) {
		        month = mm - 6;
			year = sy;
		} else if ((months > 9 || (months == 9 && days > 0))) {
		        month = mm - 9;
			year = sy;
		} else if (months < -9 || (months == -9 && days <= 0)) {
		        month = mm;
			year = sy + 1;
		} else if (months < -6 || (months == -6 && days <= 0)) {
		        month = mm + 9;
			year = sy;
			if (month > 12) {
			        month -= 12;
				year++;
			}
		} else if (months < -3 || (months == -3 && days <= 0)) {
		        month = mm + 6;
			year = sy;
			if (month > 12) {
			        month -= 12;
				year++;
			}
		} else {
		        month = mm + 3;
			year = sy;
			if (month > 12) {
			        month -= 12;
				year++;
			}
		}

		if (md == days_in_month (my, mm))
		        day = days_in_month (year, month);
		else
		        day = md;
	}

	if (! g_date_is_leap_year (year) && day == 29 && month == 2)
	        day = 28;

	if ((year > my || (year == my && month > mm)
	     || (year == my && month == mm && day > md))) {
	        year = my;
		month = mm;
		day = md;
	}

	if (year < sy || (year == sy && month < sm)
	    || (year == sy && month == sm && day < sd)) {
	        year = sy;
		month = mm;
		day = md;
	}

	return g_date_new_dmy (day, month, year);
}

/***************************************************************************
 *
 * Financial function implementations
 *
 */

static char *help_accrint = {
	N_("@FUNCTION=ACCRINT\n"
	   "@SYNTAX=ACCRINT(issue,first_interest,settlement,rate,par,"
	   "frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ACCRINT calculates the accrued interest for a security that "
	   "pays periodic interest.  "
	   "@issue is the issue date of the security.  @first_interest is "
	   "the first interest date of the security.  @settlement is the "
	   "settlement date of the sequrity.  The settlement date is always "
	   "after the issue date (the date when the security is bought). "
	   "@rate is the annual rate of the security and @par is the par "
	   "value of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @issue date, @first_interest date, or @settlement date is not "
	   "valid, ACCRINT returns #NUM! error. "
	   "The dates must be @issue < @first_interest < @settlement, or "
	   "ACCRINT returns #NUM! error. "
	   "If @rate <= 0 or @par <= 0 , ACCRINT returns #NUM! error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis < 0 or @basis > 4, ACCRINT returns #NUM! error. "
	   "If @issue date is after @settlement date or they are the same, "
	   "ACCRINT returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ACCRINTM")
};

static Value *
gnumeric_accrint (FunctionEvalInfo *ei, Value **argv)
{
        GDate      *settlement;
        GDate      *first_interest;
        GDate      *maturity;
	gnum_float rate, a, d, par, freq, coefficient, x;
	int        basis;
	Value      *result;

        maturity       = datetime_value_to_g (argv[0]);
	first_interest = datetime_value_to_g (argv[1]);
        settlement     = datetime_value_to_g (argv[2]);
	rate           = value_get_as_float (argv[3]);
	par            = value_get_as_float (argv[4]);
	freq           = value_get_as_float (argv[5]);
	basis          = argv[6] ? value_get_as_int (argv[6]) : 0;

	if (!maturity || !first_interest || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
	    || g_date_compare (settlement, first_interest) > 0
	    || g_date_compare (first_interest, maturity) < 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	a = days_monthly_basis (argv[0], argv[2], basis);
	d = annual_year_basis (argv[0], basis);

	if (a < 0 || d <= 0 || par <= 0 || rate <= 0 || basis < 0 || basis > 4
	    || freq == 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	coefficient = par * rate / freq;
	x = a / d;

	result = value_new_float (coefficient * freq * x);

 out:
	g_date_free (settlement);
	g_date_free (first_interest);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_accrintm = {
	N_("@FUNCTION=ACCRINTM\n"
	   "@SYNTAX=ACCRINTM(issue,maturity,rate[,par,basis])\n"
	   "@DESCRIPTION="
	   "ACCRINTM calculates and returns the accrued interest for a "
	   "security from @issue to @maturity date.  "
	   "@issue is the issue date of the security.  @maturity is "
	   "the maturity date of the security.  @rate is the annual "
	   "rate of the security and @par is the par value of the security. "
	   "If you omit @par, ACCRINTM applies $1,000 instead.  "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @issue date or @maturity date is not valid, ACCRINTM returns "
	   "#NUM! error. "
	   "If @rate <= 0 or @par <= 0, ACCRINTM returns #NUM! error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis < 0 or @basis > 4, ACCRINTM returns #NUM! error. "
	   "If @issue date is after @maturity date or they are the same, "
	   "ACCRINTM returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ACCRINT")
};

static Value *
gnumeric_accrintm (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate, a, d, par;
	int basis;

	rate  = value_get_as_float (argv[2]);
	par   = argv[3] ? value_get_as_float (argv[3]) : 1000;
	basis = argv[4] ? value_get_as_int (argv[4]) : 0;

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (a < 0 || d <= 0 || par <= 0 || rate <= 0 || basis < 0 || basis > 4)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (par * rate * a/d);
}

/***************************************************************************/

static char *help_intrate = {
	N_("@FUNCTION=INTRATE\n"
	   "@SYNTAX=INTRATE(settlement,maturity,investment,redemption"
	   "[,basis])\n"
	   "@DESCRIPTION="
	   "INTRATE calculates and returns the interest rate of a fully "
	   "vested security.  @settlement is the settlement date of the "
	   "security.  @maturity is the maturity date of the security. "
	   "@investment is the prize of the security paid at @settlement "
	   "date and @redemption is the amount to be received at @maturity "
	   "date.  "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @settlement date or @maturity date is not valid, INTRATE "
	   "returns #NUM! error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis < 0 or @basis > 4, INTRATE returns #NUM! error. "
	   "If @settlement date is after @maturity date or they are the same, "
	   "INTRATE returns #NUM! error. "
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
};

static Value *
gnumeric_intrate (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float investment, redemption, a, d;
	int basis;

	investment = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = argv[4] ? value_get_as_int (argv[4]) : 0;

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (basis < 0 || basis > 4 || a <= 0 || d <= 0 || investment == 0)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float ((redemption - investment) / investment *
				(d / a));
}

/***************************************************************************/

static char *help_received = {
	N_("@FUNCTION=RECEIVED\n"
	   "@SYNTAX=RECEIVED(settlement,maturity,investment,rate[,basis])\n"
	   "@DESCRIPTION="
	   "RECEIVED calculates and returns the amount to be received at "
	   "@maturity date for a security bond.  @settlement is the "
	   "settlement date of the security.  @maturity is the maturity date "
	   "of the security.  The amount of investement is specified in "
	   "@investment.  @rate is the security's discount rate. "
	   "@basis is the type of day counting system you want to "
	   "use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @settlement date or @maturity date is not valid, RECEIVED "
	   "returns #NUM! error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis < 0 or @basis > 4, RECEIVED returns #NUM! error. "
	   "If @settlement date is after @maturity date or they are the same, "
	   "RECEIVED returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=INTRATE")
};

static Value *
gnumeric_received (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float investment, discount, a, d, n;
	int basis;

	investment = value_get_as_float (argv[2]);
	discount   = value_get_as_float (argv[3]);
	basis      = argv[4] ? value_get_as_int (argv[4]) : 0;

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (a <= 0 || d <= 0 || basis < 0 || basis > 4)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	n = 1.0 - (discount * a/d);
	if (n == 0)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (investment / n);
}

/***************************************************************************/

static char *help_pricedisc = {
	N_("@FUNCTION=PRICEDISC\n"
	   "@SYNTAX=PRICEDISC(settlement,maturity,discount,redemption"
	   "[,basis])\n"
	   "@DESCRIPTION="
	   "PRICEDISC calculates and returns the price per $100 face value "
	   "of a security bond.  The security does not pay interest at "
	   "maturity.  @settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security.  @discount is "
	   "the rate for which the security is discounted.  @redemption is "
	   "the amount to be received on @maturity date.  "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @settlement date or @maturity date is not valid, PRICEDISC "
	   "returns #NUM! error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis < 0 or @basis > 4, PRICEDISC returns #NUM! error. "
	   "If @settlement date is after @maturity date or they are the same, "
	   "PRICEDISC returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PRICEMAT")
};

static Value *
gnumeric_pricedisc (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float discount, redemption, a, d;
	int basis;

	discount   = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = argv[4] ? value_get_as_int (argv[4]) : 0;

	a = days_monthly_basis (argv[0], argv[1], basis);
	d = annual_year_basis (argv[0], basis);

	if (a <= 0 || d <= 0 || basis < 0 || basis > 4)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (redemption - discount * redemption * a/d);
}

/***************************************************************************/

static char *help_pricemat = {
	N_("@FUNCTION=PRICEMAT\n"
	   "@SYNTAX=PRICEMAT(settlement,maturity,issue,rate,yield[,basis])\n"
	   "@DESCRIPTION="
	   "PRICEMAT calculates and returns the price per $100 face value "
	   "of a security.  The security pays interest at maturity. "
	   "@settlement is the settlement date of the security.  @maturity is "
	   "the maturity date of the security.  @issue is the issue date of "
	   "the security.  @rate is the discount rate of the security. "
	   "@yield is the annual yield of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @settlement date or @maturity date is not valid, PRICEMAT "
	   "returns #NUM! error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis < 0 or @basis > 4, PRICEMAT returns #NUM! error. "
	   "If @settlement date is after @maturity date or they are the same, "
	   "PRICEMAT returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PRICEDISC")
};

static Value *
gnumeric_pricemat (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float discount, yield, a, b, dsm, dim, n;
	int basis;

	discount = value_get_as_float (argv[3]);
	yield    = value_get_as_float (argv[4]);
	basis    = argv[5] ? value_get_as_int (argv[5]) : 0;

	dsm = days_monthly_basis (argv[0], argv[1], basis);
	dim = days_monthly_basis (argv[2], argv[1], basis);
	a   = days_monthly_basis (argv[2], argv[0], basis);
	b   = annual_year_basis (argv[0], basis);

	if (a <= 0 || b <= 0 || dsm <= 0 || dim <= 0 || basis < 0 ||
	    basis > 4)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	n = 1 + (dsm/b * yield);
	if (n == 0)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (((100 + (dim/b * discount * 100)) /
				 (n)) - (a/b * discount * 100));
}

/***************************************************************************/

static char *help_disc = {
	N_("@FUNCTION=DISC\n"
	   "@SYNTAX=DISC(settlement,maturity,par,redemption[,basis])\n"
	   "@DESCRIPTION="
	   "DISC calculates and returns the discount rate for a sequrity. "
	   "@settlement is the settlement date of the security.  @maturity "
	   "is the maturity date of the security.  @par is the price per $100 "
	   "face value of the security.  @redemption is the redeption value "
	   "per $100 face value of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @settlement date or @maturity date is not valid, DISC "
	   "returns #NUM! error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis < 0 or @basis > 4, DISC returns #NUM! error. "
	   "If @settlement date is after @maturity date or they are the same, "
	   "DISC returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_disc (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float par, redemption, dsm, b;
	int basis;

	par        = value_get_as_float (argv[2]);
	redemption = value_get_as_float (argv[3]);
	basis      = argv[4] ? value_get_as_int (argv[4]) : 0;

	b = annual_year_basis (argv[0], basis);
	dsm = days_monthly_basis (argv[0], argv[1], basis);

	if (dsm <= 0 || b <= 0 || dsm <= 0 || basis < 0 || basis > 4
	    || redemption == 0)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float ((redemption - par) / redemption * (b / dsm));
}

/***************************************************************************/

static char *help_effect = {
	N_("@FUNCTION=EFFECT\n"
	   "@SYNTAX=EFFECT(r,nper)\n"
	   "@DESCRIPTION="
	   "EFFECT calculates the effective interest rate from "
	   "a given nominal rate.\n"
	   "Effective interest rate is calculated using this formula:\n"
	   "\n"
           "    (1 + @r / @nper) ^ @nper - 1\n"
	   "\n"
	   "where:\n"
	   "\n"
	   "@r = nominal interest rate (stated in yearly terms)\n"
	   "@nper = number of periods used for compounding"
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
};

static Value *
gnumeric_effect (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate;
	int nper;

	rate = value_get_as_float (argv[0]);
	nper = value_get_as_int (argv[1]);

	/* Rate or number of periods cannot be negative */
	if (rate < 0 || nper <= 0)
		return value_new_error (ei->pos, _("effect - domain error"));

        return value_new_float (pow ((1 + rate / nper), nper) - 1);
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
           "@nper * (( 1 + @r ) ^ (1 / @nper) - 1 )"
	   "\n"
	   "where:\n"
	   "\n"
	   "@r = effective interest rate\n"
	   "@nper = number of periods used for compounding"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=EFFECT")
};

static Value *
gnumeric_nominal (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate;
	int nper;

	rate = value_get_as_float (argv[0]);
	nper = value_get_as_int (argv[1]);

	/* Rate or number of periods cannot be negative */
	if (rate < 0 || nper <= 0)
		return value_new_error (ei->pos, _("nominal - domain error"));

        return value_new_float (nper * (pow (1 + rate, 1.0 / nper ) - 1 ));

}

/***************************************************************************/

static char *help_ispmt = {
	N_("@FUNCTION=ISPMT\n"
	   "@SYNTAX=ISPMT(rate,per,nper,pv)\n"
	   "@DESCRIPTION="
	   "ISPMT function returns the interest paid on a given period. "
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
	gnum_float rate, pv, tmp;
	int nper, per;

	rate = value_get_as_float (argv[0]);
	per  = value_get_as_int (argv[1]);
	nper = value_get_as_int (argv[2]);
	pv   = value_get_as_float (argv[3]);

	if (per < 1 || per > nper)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	tmp = -pv * rate;

	return value_new_float (tmp - (tmp / nper * per));
}

/***************************************************************************/

static char *help_db = {
	N_("@FUNCTION=DB\n"
	   "@SYNTAX=DB(cost,salvage,life,period[,month])\n"
	   "@DESCRIPTION="
	   "DB calculates the depreciation of an asset for a given period "
	   "using the fixed-declining balance method.  @cost is the "
	   "initial value of the asset.  @salvage is the value after the "
	   "depreciation. "
	   "@life is the number of periods overall.  @period is the period "
	   "for which you want the depreciation to be calculated.  @month "
	   "is the number of months in the first year of depreciation. "
	   "If @month is omitted, it is assumed to be 12. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DDB,SLN,SYD")
};

static Value *
gnumeric_db (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate;
	gnum_float cost, salvage, life, period, month;
	gnum_float total;
	int i;

	cost    = value_get_as_float (argv[0]);
	salvage = value_get_as_float (argv[1]);
	life    = value_get_as_float (argv[2]);
	period  = value_get_as_float (argv[3]);
	month   = argv[4] ? value_get_as_float (argv[4]) : 12;

	/* The third disjunct is a bit of a guess -- MW.  */
	if (cost == 0 || life <= 0 || salvage / cost < 0)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	rate = 1 - pow ((salvage / cost), (1 / life));
	rate *= 1000;
	rate = floor (rate+0.5) / 1000;

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
	   "@SEEALSO=SLN,SYD")
};

static Value *
gnumeric_ddb (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float cost, salvage, life, period, factor;
	gnum_float total;
	int i;

	cost    = value_get_as_float (argv[0]);
	salvage = value_get_as_float (argv[1]);
	life    = value_get_as_float (argv[2]);
	period  = value_get_as_float (argv[3]);
	factor  = argv[4] ? value_get_as_float (argv[4]) : 2;

	if (life <= 0)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	total = 0;
	for (i = 0; i < life - 1; i++) {
	        gnum_float period_dep = (cost - total) * (factor / life);
		if (period - 1 == i)
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
	   "SLN function will determine the straight line depreciation "
	   "of an asset for a single period. The amount you paid for the "
	   "asset is the @cost, @salvage is the value of the asset at the "
	   "end of its useful life, and @life is the number of periods over "
	   "which an the asset is depreciated. This method of deprecition "
	   "devides the cost evenly over the life of an asset."
	   "\n"
	   "The formula used for straight line depriciation is:"
	   "\n"
	   "Depriciation expense = ( @cost - @salvage_value ) / @life"
	   "\n"
	   "\t@cost = cost of an asset when acquired (market value)."
	   "\t@salvage_value = amount you get when asset sold at the end "
	   "of the assets's useful life."
	   "\t@life = anticipated life of an asset."
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
};


static Value *
gnumeric_sln (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float cost,salvage_value,life;

	cost          = value_get_as_float (argv[0]);
	salvage_value = value_get_as_float (argv[1]);
	life          = value_get_as_float (argv[2]);

	/* Life of an asset cannot be negative */
	if (life <= 0)
		return value_new_error (ei->pos, _("sln - domain error"));

        return value_new_float ((cost - salvage_value) / life);
}

/***************************************************************************/

static char *help_syd = {
	N_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage_value,life,period)\n"
	   "@DESCRIPTION="
	   "SYD function calculates the sum-of-years digits depriciation "
	   "for an asset based on its cost, salvage value, anticipated life "
	   "and a particular period. This method accelerates the rate of the "
	   "depreciation, so that more depreciation expense occurs in "
	   "earlier periods than in later ones. The depreciable cost is the "
	   "actual cost minus the salvage value. The useful life is the "
	   "number of periods (typically years) over with the asset is "
	   "depreciated."
	   "\n"
	   "The Formula used for sum-of-years digits depriciation is:"
	   "\n"
	   "Depriciation expense = ( @cost - @salvage_value ) * "
	   "(@life - @period + 1) * 2 / @life * (@life + 1)."
	   "\n"
	   "\t@cost = cost of an asset when acquired (market value)."
	   "\t@salvage_value = amount you get when asset sold at the end of "
	   "its useful life."
	   "\t@life = anticipated life of an asset."
	   "\t@period = period for which we need the expense."
	   "\n"
	   "@EXAMPLES=\n"
	   "For example say a company purchases a new computer for $5000 "
	   "which has a salvage value of $200, and a useful life of three "
	   "years. We would use the following to calculate the second "
	   "year's depreciation using the SYD method:"
	   "\n"
	   "=SYD(5000, 200, 5, 2) which returns 1,280.00."
	   "\n"
	   "@SEEALSO=SLN")
};

static Value *
gnumeric_syd (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float cost, salvage_value, life, period;

	cost          = value_get_as_float (argv[0]);
	salvage_value = value_get_as_float (argv[1]);
	life          = value_get_as_float (argv[2]);
	period        = value_get_as_float (argv[3]);

	/* Life of an asset cannot be negative */
	if (life <= 0)
		return value_new_error (ei->pos, _("syd - domain error"));

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
	   "@fractional_dollar is the fractional number to be converted. "
	   "@fraction is the denominator of the fraction. "
	   "\n"
	   "If @fraction is non-integer it is truncated. "
	   "If @fraction <= 0, DOLLARDE returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DOLLARFR")
};

static Value *
gnumeric_dollarde (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float fractional_dollar;
	int        fraction, n, tmp;
	gnum_float floored, rest;

	fractional_dollar = value_get_as_float (argv[0]);
	fraction          = value_get_as_int (argv[1]);

	if (fraction <= 0)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	tmp = fraction;
	/* Count digits in fraction */
	for (n = 0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;

	return value_new_float (floored + ((gnum_float) rest * pow (10, n) /
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
	   "If @fraction <= 0, DOLLARFR returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DOLLARDE")
};

static Value *
gnumeric_dollarfr (FunctionEvalInfo *ei, Value **argv)
{
        gnum_float fractional_dollar;
	int fraction, n, tmp;
	gnum_float floored, rest;

	fractional_dollar = value_get_as_float (argv[0]);
	fraction          = value_get_as_int (argv[1]);

	if (fraction <= 0)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	/* Count digits in fraction */
	tmp = fraction;
	for (n = 0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;

	return value_new_float (floored + ((gnum_float) (rest * fraction) /
					   pow (10, n)));
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
	gnum_float frate, rrate, npv_neg, npv_pos;
	gnum_float *values = NULL, res;
	Value *result = NULL;
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
		gnum_float v = values[i];
		if (v >= 0)
			npv_pos += v / pow (1 + rrate, i);
		else
			npv_neg += v / pow (1 + frate, i);
	}

	if (npv_neg == 0 || npv_pos == 0 || rrate <= -1) {
		result = value_new_error (ei->pos, gnumeric_err_DIV0);
		goto out;
	}

	/*
	 * I have my doubts about this formula, but it sort of looks like
	 * the one Microsoft claims to use and it produces the results
	 * that Excel does.  -- MW.
	 */
	res = pow ((-npv_pos * pow (1 + rrate, n)) / (npv_neg * (1 + rrate)),
		  (1.0 / (n - 1))) - 1.0;

	result = value_new_float (res);
out:
	g_free (values);

	return result;
}

/***************************************************************************/

static char *help_tbilleq = {
	N_("@FUNCTION=TBILLEQ\n"
	   "@SYNTAX=TBILLEQ(settlement,maturity,discount)\n"
	   "@DESCRIPTION="
	   "TBILLEQ function returns the bond-yield equivalent (BEY) for "
	   "a treasury bill.  TBILLEQ is equivalent to (365 * @discount) / "
	   "(360 - @discount * DSM) where DSM is the days between @settlement "
	   "and @maturity. "
	   "\n"
	   "If @settlement is after @maturity or the @maturity is set to "
	   "over one year later than the @settlement, TBILLEQ returns "
	   "#NUM! error. "
	   "If @discount is negative, TBILLEQ returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLPRICE,TBILLYIELD")
};

static Value *
gnumeric_tbilleq (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float settlement, maturity, discount;
	gnum_float dsm, divisor;

	settlement = datetime_value_to_serial (argv[0]);
	maturity   = datetime_value_to_serial (argv[1]);
	discount   = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (settlement > maturity || discount < 0 || dsm > 356)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	divisor = 360 - discount * dsm;
	/* This test probably isn't right, but it is better that not checking
	   at all.  --MW.  */
	if (divisor == 0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);

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
	   "#NUM! error. "
	   "If @discount is negative, TBILLPRICE returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLEQ,TBILLYIELD")
};

static Value *
gnumeric_tbillprice (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float settlement, maturity, discount;
	gnum_float res, dsm;

	settlement = datetime_value_to_serial (argv[0]);
	maturity   = datetime_value_to_serial (argv[1]);
	discount   = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (settlement > maturity || discount < 0 || dsm > 356)
                return value_new_error (ei->pos, gnumeric_err_NUM);

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
	   "#NUM! error. "
	   "If @pr is negative, TBILLYIELD returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TBILLEQ,TBILLPRICE")
};

static Value *
gnumeric_tbillyield (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float settlement, maturity, pr;
	gnum_float res, dsm;

	settlement = datetime_value_to_serial (argv[0]);
	maturity   = datetime_value_to_serial (argv[1]);
	pr         = value_get_as_float (argv[2]);

	dsm = maturity - settlement;

	if (pr <= 0 || dsm <= 0 || dsm > 356)
                return value_new_error (ei->pos, gnumeric_err_NUM);

	res = (100.0 - pr) / pr * (360.0 / dsm);

	return value_new_float (res);
}

/***************************************************************************/

static char *help_rate = {
	N_("@FUNCTION=RATE\n"
	   "@SYNTAX=RATE(nper,pmt,pv[,fv,type,guess])\n"
	   "@DESCRIPTION="
	   "RATE calculates the rate of an investment."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PV,FV")
};

typedef struct {
	int nper, type;
	gnum_float pv, fv, pmt;
} gnumeric_rate_t;

static GoalSeekStatus
gnumeric_rate_f (gnum_float rate, gnum_float *y, void *user_data)
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
gnumeric_rate_df (gnum_float rate, gnum_float *y, void *user_data)
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
	GoalSeekData    data;
	GoalSeekStatus  status;
	gnumeric_rate_t udata;
	gnum_float      rate0;

	udata.nper = value_get_as_int (argv[0]);
	udata.pmt  = value_get_as_float (argv[1]);
	udata.pv   = value_get_as_float (argv[2]);
	udata.fv   = argv[3] ? value_get_as_float (argv[3]) : 0.0;
	udata.type = argv[4] ? value_get_as_int (argv[4]) : 0;
	rate0      = argv[5] ?  value_get_as_float (argv[5]) : 0.1;

	if (udata.nper <= 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	if (udata.type != 0 && udata.type != 1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

#if 0
	printf ("Guess = %.15g\n", rate0);
#endif
	goal_seek_initialise (&data);

	data.xmin = MAX (data.xmin,
			 -pow (DBL_MAX / 1e10, 1.0 / udata.nper) + 1);
	data.xmax = MIN (data.xmax,
			 pow (DBL_MAX / 1e10, 1.0 / udata.nper) - 1);

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
		return value_new_error (ei->pos, gnumeric_err_NUM);
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
	   "5324, 7432, 9332, 12324, 4334, 1235, -3422.  Then\n"
	   "IRR(A1:A8) returns 0.04375. "
	   "\n"
	   "@SEEALSO=FV,NPV,PV")
};

typedef struct {
        int     n;
        gnum_float *values;
} gnumeric_irr_t;

static GoalSeekStatus
irr_npv (gnum_float rate, gnum_float *y, void *user_data)
{
	gnumeric_irr_t *p = user_data;
	gnum_float *values, sum;
        int i, n;

	values = p->values;
	n = p->n;

	sum = 0;
	for (i = 0; i < n; i++)
	        sum += values[i] * pow (1 + rate, n - i);

	/*
	 * I changed the formula above by multiplying all terms by (1+r)^n.
	 * Since we're looking for zeros, that should not matter.  It does
	 * make the derivative below simpler, though.  -- MW.
	 */

	*y = sum;
	return GOAL_SEEK_OK;
}

static GoalSeekStatus
irr_npv_df (gnum_float rate, gnum_float *y, void *user_data)
{
	gnumeric_irr_t *p = user_data;
	gnum_float *values, sum;
        int i, n;

	values = p->values;
	n = p->n;

	sum = 0;
	for (i = 0; i < n - 1; i++)
	        sum += values[i] * (n - i) * pow (1 + rate, n - i - 1);

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
	gnum_float      rate0;

	rate0 = argv[1] ? value_get_as_float (argv[1]) : 0.1;

	p.values = collect_floats_value (argv[0], ei->pos,
					 COLLECT_IGNORE_STRINGS |
					 COLLECT_IGNORE_BLANKS,
					 &p.n, &result);
	if (result != NULL) {
		g_free (p.values);
	        return result;
	}

	goal_seek_initialise (&data);

	data.xmin = MAX (data.xmin,
			 -pow (DBL_MAX / 1e10, 1.0 / p.n) + 1);
	data.xmax = MIN (data.xmax,
			 pow (DBL_MAX / 1e10, 1.0 / p.n) - 1);

	status = goal_seek_newton (&irr_npv, &irr_npv_df, &data, &p, rate0);
	if (status != GOAL_SEEK_OK) {
		int factor;
		/* Lay a net of test points around the guess.  */
		for (factor = 2; !(data.havexneg && data.havexpos) &&
		       factor < 100; factor *= 2) {
			goal_seek_point (&irr_npv, &data, &p, rate0 * factor);
			goal_seek_point (&irr_npv, &data, &p, rate0 / factor);
		}

		/* Pray we got both sides of the root.  */
		status = goal_seek_bisection (&irr_npv, &data, &p);
	}

	g_free (p.values);

	if (status == GOAL_SEEK_OK)
		return value_new_float (data.root);
	else
		return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_pv = {
	N_("@FUNCTION=PV\n"
	   "@SYNTAX=PV(rate,nper,pmt[,fv,type])\n"
	   "@DESCRIPTION="
	   "PV calculates the present value of an investment. "
	   "@rate is the periodic interest rate, @nper is the "
	   "number of periods used for compounding. "
	   "@pmt is the payment made each period, "
	   "@fv is the future value and @type is when the payment is made. "
	   "If @type = 1 then the payment is made at the begining of the "
	   "period. If @type = 0 (or omitted) it is made at the end of each "
	   "period."
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=FV")
};

static Value *
gnumeric_pv (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate, nper, pmt, fv;
	gnum_float pvif, fvifa;
	int type;

	rate = value_get_as_float (argv[0]);
	nper = value_get_as_float (argv[1]);
	pmt  = value_get_as_float (argv[2]);
	fv   = argv[3] ? value_get_as_float (argv[3]) : 0;
	type = argv[4] ? !!value_get_as_int (argv[4]) : 0;

	/* Special case for which we cannot calculate pvif and/or fvifa.  */
	if (rate == 0)
		return value_new_float (-nper * pmt);

	/* Calculate the PVIF and FVIFA */
	pvif = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

	if (pvif == 0)
	        return value_new_error (ei->pos, gnumeric_err_DIV0);

        return value_new_float ((-fv - pmt * (1.0 + rate * type) * fvifa) /
				pvif);
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
        gnum_float rate;
        gnum_float sum;
        int     num;
} financial_npv_t;

static Value *
callback_function_npv (const EvalPos *ep, Value *value, void *closure)
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
gnumeric_npv (FunctionEvalInfo *ei, ExprList *nodes)
{
	Value *v;
        financial_npv_t p;

	p.sum   = 0.0;
	p.num   = 0;

	v = function_iterate_argument_values (ei->pos, callback_function_npv,
					      &p, nodes, TRUE, TRUE);

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
	   "returns the #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=NPV,PV")
};

static Value *
gnumeric_xnpv (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate, *payments = NULL, *dates = NULL;
	gnum_float sum;
	int  p_n, d_n, i;
	Value *result = NULL;

	rate = value_get_as_float (argv[0]);
	sum = 0;

	payments = collect_floats_value (argv[1], ei->pos,
					 COLLECT_IGNORE_STRINGS |
					 COLLECT_IGNORE_BOOLS,
					 &p_n, &result);
	if (result)
		goto out;

	dates = collect_floats_value (argv[2], ei->pos,
				      COLLECT_DATES,
				      &d_n, &result);
	if (result)
		goto out;

	if (p_n != d_n) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	for (i = 0; i < p_n; i++)
	        sum += payments[i] / pow (1 + rate, (dates[i] - dates[0]) /
					  365.0);

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
	   "\"2000-03-12\", and \"2000-05-1\". Then\n"
	   "XIRR(A1:A5,B1:B5) returns 0.224838. "
	   "\n"
	   "@SEEALSO=IRR,XNPV")
};

typedef struct {
        int     n;
        gnum_float *values;
        gnum_float *dates;
} gnumeric_xirr_t;

static GoalSeekStatus
xirr_npv (gnum_float rate, gnum_float *y, void *user_data)
{
	gnumeric_xirr_t *p = user_data;
	gnum_float *values, *dates, sum;
        int i, n;

	values = p->values;
	dates = p->dates;
	n = p->n;

	sum = 0;
	for (i = 0; i < n; i++) {
	        gnum_float d = dates[i] - dates[0];

		if (d < 0)
		        return GOAL_SEEK_ERROR;
	        sum += values[i] / pow (1 + rate, d / 365.0);
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
	gnum_float      rate0;
	int             n, d_n;

	goal_seek_initialise (&data);
	rate0 = argv[2] ? value_get_as_float (argv[2]) : 0.1;

	p.values = collect_floats_value (argv[0], ei->pos,
					 COLLECT_IGNORE_STRINGS,
					 &n, &result);
	p.dates = NULL;

	if (result != NULL)
		goto out;

	p.dates = collect_floats_value (argv[1], ei->pos,
					COLLECT_DATES,
					&d_n, &result);
	if (result != NULL)
		goto out;

	p.n = n;
	status = goal_seek_newton (&xirr_npv, NULL, &data, &p, rate0);

	if (status == GOAL_SEEK_OK)
		result = value_new_float (data.root);
	else
		result = value_new_error (ei->pos, gnumeric_err_NUM);

 out:
	g_free (p.values);
	g_free (p.dates);

	return result;
}

/***************************************************************************/

static char *help_fv = {
	N_("@FUNCTION=FV\n"
	   "@SYNTAX=FV(rate,term,pmt,pv,type)\n"
	   "@DESCRIPTION="
	   "FV computes the future value of an investment. This is based "
	   "on periodic, constant payments and a constant interest rate. "
	   "The interest rate per period is @rate, @term is the number of "
	   "periods in an annuity, @pmt is the payment made each period, "
	   "@pv is the present value and @type is when the payment is made. "
	   "If @type = 1 then the payment is made at the begining of the "
	   "period. If @type = 0 it is made at the end of each period."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PV,PMT,PPMT")
};

static Value *
gnumeric_fv (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate, nper, pv, pmt;
	gnum_float pvif, fvifa;
	int type;

	rate = value_get_as_float (argv[0]);
	nper = value_get_as_float (argv[1]);
	pmt  = value_get_as_float (argv[2]);
	pv   = (NULL != argv[3]) ? value_get_as_float (argv[3]) : 0.;
	type = (NULL != argv[4] && 0 != value_get_as_int (argv[4])) ? 1 : 0;

	pvif = calculate_pvif (rate, nper);
	fvifa = calculate_fvifa (rate, nper);

        return value_new_float (-((pv * pvif) + pmt *
				  (1.0 + rate * type) * fvifa));
}

/***************************************************************************/

static char *help_pmt = {
	N_("@FUNCTION=PMT\n"
	   "@SYNTAX=PMT(rate,nper,pv[,fv,type])\n"
	   "@DESCRIPTION="
	   "PMT returns the amount of payment for a loan based on a constant "
	   "interest rate and constant payments (each payment is equal amount). "
	   "@rate is the constant interest rate. "
	   "@nper is the overall number of payments. "
	   "@pv is the present value. "
	   "@fv is the future value. "
	   "@type is the type of the payment: 0 means at the end of the period "
	   "and 1 means at the beginning of the period. "
	   "\n"
	   "If @fv is omitted, Gnumeric assumes it to be zero. "
	   "If @type is omitted, Gnumeric assumes it to be zero. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_pmt (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate, pv, fv, nper;
	int type;

	rate = value_get_as_float (argv[0]);
	nper = value_get_as_float (argv[1]);
	pv   = value_get_as_float (argv[2]);
	fv   = argv[3] ? value_get_as_float (argv[3]) : 0;
	type = argv[4] ? !!value_get_as_int (argv[4]) : 0;

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
	   "IPMT(PER) = -PRINCIPAL(PER-1) * INTEREST_RATE"
	   "\n"
	   "where:"
	   "\n"
	   "PRINCIPAL(PER-1) = amount of the remaining principal from last "
	   "period"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_ipmt (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate, nper, per, pv, fv;
	gnum_float pmt;
	int type;

	rate = value_get_as_float (argv[0]);
	per  = value_get_as_float (argv[1]);
	nper = value_get_as_float (argv[2]);
	pv   = value_get_as_float (argv[3]);
	fv   = argv[4] ? value_get_as_float (argv[4]) : 0;
	type = argv[5] ? !!value_get_as_int (argv[5]) : 0;

	/* First calculate the payment */
        pmt = calculate_pmt (rate, nper, pv, fv, type);

	/* Now we need to calculate the amount of money going towards the
	   principal */

	return value_new_float (-calculate_principal (pv, pmt, rate, per - 1) *
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
	gnum_float rate, nper, per, pv, fv;
	gnum_float ipmt, pmt;
	int type;

	rate = value_get_as_float (argv[0]);
	per  = value_get_as_float (argv[1]);
	nper = value_get_as_float (argv[2]);
	pv   = value_get_as_float (argv[3]);
	fv   = argv[4] ? value_get_as_float (argv[4]) : 0;
	type = argv[5] ? !!value_get_as_int (argv[5]) : 0;

	/* First calculate the payment */
        pmt = calculate_pmt (rate,nper,pv,fv,type);

	/*
	 * Now we need to calculate the amount of money going towards the
	 * principal
	 */
	ipmt = -calculate_principal (pv, pmt, rate, per - 1) * rate;

	return value_new_float (pmt - ipmt);
}

/***************************************************************************/

static char *help_nper = {
	N_("@FUNCTION=NPER\n"
	   "@SYNTAX=NPER(rate,pmt,pv,fv,type)\n"
	   "@DESCRIPTION="
	   "NPER calculates number of periods of an investment based on "
	   "periodic constant payments and a constant interest rate. "
	   "The interest rate per period is @rate, @pmt is the payment made "
	   "each period, @pv is the present value, @fv is the future value "
	   "and @type is when the payments are due. If @type = 1, payments "
	   "are due at the begining of the period, if @type = 0, payments "
	   "are due at the end of the period."
	   "\n"
	   "@EXAMPLES=\n"
	   "For example, if you deposit $10,000 in a savings account that "
	   "earns an interest rate of 6%. To calculate home many years it "
	   "will take to double your investment use NPER as follows:"
	   "\n"
	   "=NPER(0.06, 0, -10000, 20000,0)"
	   "returns 11.895661046 which indicates that you can double your "
	   "money just before the end of the 12th year."
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_nper (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float rate, pmt, pv, fv, tmp;
	int type;

	rate = value_get_as_float (argv[0]);
	pmt  = value_get_as_float (argv[1]);
	pv   = value_get_as_float (argv[2]);
	fv   = value_get_as_float (argv[3]);
	type = !!value_get_as_int (argv[4]);

	if (rate <= 0.0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);

	tmp = (pmt * (1.0 + rate * type) - fv * rate) /
	  (pv * rate + pmt * (1.0 + rate * type));
	if (tmp <= 0.0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

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
	gnum_float rate,pv,fv;

	rate = value_get_as_float (argv[0]);
	pv   = value_get_as_float (argv[1]);
	fv   = value_get_as_float (argv[2]);

	if (rate <= 0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);
	else if (fv == 0 || pv == 0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);
	else if (fv / pv < 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

        return value_new_float (log (fv / pv) / log (1.0 + rate));

}

/***************************************************************************/

static char *help_fvschedule = {
	N_("@FUNCTION=FVSCHEDULE\n"
	   "@SYNTAX=FVSCHEDULE(principal,schedule)\n"
	   "@DESCRIPTION="
	   "FVSCHEDULE returns the future value of given initial value "
	   "after applying a series of compound periodic interest rates. "
	   "The argument @principal is the present value; @schedule is an "
	   "array of interest rates to apply. The @schedule argument must "
	   "be a range of cells."
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
	gnum_float pv, *schedule = NULL;
	Value *result = NULL;
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

static char *help_euro = {
	N_("@FUNCTION=EURO\n"
	   "@SYNTAX=EURO(currency)\n"
	   "@DESCRIPTION="
	   "EURO converts one Euro to a given national currency in the "
	   "European monetary union.  @currency is one of the following:\n"
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
	   "If the given @currency is other than one of the above, EURO "
	   "returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "EURO(\"DEM\") returns 1.95583."
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_euro (FunctionEvalInfo *ei, Value **argv)
{
        const char *str = value_peek_string (argv[0]);

	switch (*str) {
	case 'A':
	        if (strncmp ("ATS", str, 3) == 0)
		        return value_new_float (13.7603);
		break;
	case 'B':
	        if (strncmp ("BEF", str, 3) == 0)
		        return value_new_float (40.3399);
		break;
	case 'D':
	        if (strncmp ("DEM", str, 3) == 0)
		        return value_new_float (1.95583);
		break;
	case 'E':
	        if (strncmp ("ESP", str, 3) == 0)
		        return value_new_float (166.386);
		break;
	case 'F':
	        if (strncmp ("FIM", str, 3) == 0)
		        return value_new_float (5.94573);
		else if (strncmp ("FRF", str, 3) == 0)
		        return value_new_float (6.55957);
		break;
	case 'I':
	        if (strncmp ("IEP", str, 3) == 0)
		        return value_new_float (0.787564);
		else if (strncmp ("ITL", str, 3) == 0)
		        return value_new_float (1936.27);
		break;
	case 'L':
	        if (strncmp ("LUX", str, 3) == 0)
		        return value_new_float (40.3399);
		break;
	case 'N':
	        if (strncmp ("NLG", str, 3) == 0)
		        return value_new_float (2.20371);
		break;
	case 'P':
	        if (strncmp ("PTE", str, 3) == 0)
		        return value_new_float (200.482);
		break;
	default:
	        break;
	}

	return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_price = {
	N_("@FUNCTION=PRICE\n"
	   "@SYNTAX=PRICE(settle,mat,rate,yield,redemption_price,frequency"
	   "[,basis])\n"
	   "@DESCRIPTION="
	   "PRICE returns price per $100 face value of a security. "
	   "This method can only be used if the security pays periodic "
	   "interest. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, PRICE returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_price (FunctionEvalInfo *ei, Value **argv)
{
        GDate      *settlement;
        GDate      *maturity;
        gnum_float a, d, e, n;
	gnum_float first_term, last_term, den, base, exponent, sum;
	gnum_float rate, yield, redemption;
	gint freq, basis, k;
	Value *result;

        settlement = datetime_value_to_g (argv[0]);
        maturity   = datetime_value_to_g (argv[1]);
	rate       = value_get_as_float (argv[2]);
	yield      = value_get_as_float (argv[3]);
	redemption = value_get_as_float (argv[4]);
        freq       = value_get_as_int (argv[5]);
        basis      = argv[6] ? value_get_as_int (argv[6]) : 0;

	if (!maturity || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
            || g_date_compare (settlement, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        if (rate < 0.0 || yield < 0.0 || redemption <= 0.0) {
                result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	a = coupdaybs (settlement, maturity, freq, basis);
	d = coupdaysnc (settlement, maturity, freq, basis);
	e = coupdays (settlement, maturity, freq, basis);
	n = coupnum (settlement, maturity, freq, basis);

	sum = 0.0;
	den = 100.0 * rate / freq;
	base = 1.0 + yield / freq;
	exponent = d / e;
	for (k = 0; k < n; k++)
	        sum += den / pow (base, exponent + k);

	first_term = redemption / pow (base, (n - 1.0 + d / e));
	last_term = a / e * den;

	result = value_new_float (first_term + sum - last_term);

 out:
	g_date_free (settlement);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_yield = {
	N_("@FUNCTION=YIELD\n"
	   "@SYNTAX=YIELD(settle,mat,rate,price,redemption_price,frequency"
	   "[,basis])\n"
	   "@DESCRIPTION="
	   "Use YIELD to calculate the yield on a security that pays periodic "
	   "interest. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, YIELD returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_yield (FunctionEvalInfo *ei, Value **argv)
{
        GDate      *settlement;
        GDate      *maturity;
        gnum_float a, d, e, n;
	gnum_float coeff, num, den;
	gnum_float rate, par, redemption;
	gint       freq, basis;
	Value      *result;

        settlement = datetime_value_to_g (argv[0]);
        maturity   = datetime_value_to_g (argv[1]);
	rate       = value_get_as_float (argv[2]);
	par        = value_get_as_float (argv[3]);
	redemption = value_get_as_float (argv[4]);
        freq       = value_get_as_int (argv[5]);
        basis      = argv[6] ? value_get_as_int (argv[6]) : 0;

	if (!maturity || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
            || g_date_compare (settlement, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        if (rate < 0.0 || par < 0.0 || redemption <= 0.0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	a = coupdaybs (settlement, maturity, freq, basis);
	d = coupdaysnc (settlement, maturity, freq, basis);
	e = coupdays (settlement, maturity, freq, basis);
	n = coupnum (settlement, maturity, freq, basis);

	if (n <= 1.0) {
	        coeff = freq * e / d;
		num = (redemption / 100.0  +  rate / freq)
		        - (par / 100.0  +  (a / e  *  rate / freq));
		den = par / 100.0  +  (a / e  *   rate / freq);

		result = value_new_float (num / den * coeff);
	} else {
	        /* We should goal-seek using price */

	        result = value_new_error (ei->pos, "#UNIMPLEMENTED!");
	}

 out:
	g_date_free (settlement);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_yielddisc = {
	N_("@FUNCTION=YIELDDISC\n"
	   "@SYNTAX=YIELDDISC(settlement,maturity,pr,redemption[,basis])\n"
	   "@DESCRIPTION="
	   "YIELDDISC calculates the annual yield of a security that is "
	   "discounted. "
	   "@settlement is the settlement date of the security.  "
	   "@maturity is the maturity date of the security. "
	   "@pr is the price per $100 face value of the security. "
	   "@redemption is the redemption value per $100 face value. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, YIELDDISC returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_yielddisc (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_yieldmat = {
	N_("@FUNCTION=YIELDMAT\n"
	   "@SYNTAX=YIELDMAT(settlement,maturity,issue,rate,pr[,basis])\n"
	   "@DESCRIPTION="
	   "YIELDMAT calculates the annual yield of a security for which "
	   "the interest is payed at maturity date. "
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@issue is the issue date of the security. "
	   "@rate is the interest rate set to the security. "
	   "@pr is the price per $100 face value of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, YIELDMAT returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_yieldmat (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_oddfprice = {
	N_("@FUNCTION=ODDFPRICE\n"
	   "@SYNTAX=ODDFPRICE(settlement,maturity,issue,first_coupon,rate,"
	   "yld,redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDFPRICE returns the price per $100 face value of a security. "
	   "The security should have an odd short or long first period. "
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@issue is the issue date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, ODDFPRICE returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddfprice (FunctionEvalInfo *ei, Value **argv)
{
        GDate      *settlement;
        GDate      *maturity;
        GDate      *issue;
	GDate      *first_coupon;
        gnum_float a, ds, df, e, n;
	gnum_float term1, term2, last_term, sum;
	gnum_float rate, yield, redemption;
	gint       freq, basis, k;
	Value      *result;

        settlement   = datetime_value_to_g (argv[0]);
        maturity     = datetime_value_to_g (argv[1]);
        issue        = datetime_value_to_g (argv[2]);
        first_coupon = datetime_value_to_g (argv[3]);
	rate         = value_get_as_float (argv[4]);
	yield        = value_get_as_float (argv[5]);
	redemption   = value_get_as_float (argv[6]);
        freq         = value_get_as_int (argv[7]);
        basis        = argv[8] ? value_get_as_int (argv[8]) : 0;

	if (!maturity || !first_coupon || !settlement || !issue) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
            || g_date_compare (issue, settlement) > 0
	    || g_date_compare (settlement, first_coupon) > 0
	    || g_date_compare (first_coupon, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        if (rate < 0.0 || yield < 0.0 || redemption <= 0.0) {
                result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

	a = coupdaybs (settlement, maturity, freq, basis);
	ds = -1; /* FIXME */
	df = -1; /* FIXME */
	e = coupdays (settlement, maturity, freq, basis);
	n = coupnum (settlement, maturity, freq, basis);

	/*
	 * FIXME: Check if odd long first coupon and implement the branch
	 */

	/* Odd short first coupon */
	term1 = redemption / pow (1.0 + yield / freq, n - 1.0 + ds / e);
	term2 = (100.0 * rate / freq * df / e) / pow (1.0 + yield / freq, ds / e);;
	last_term = 100.0 * rate / freq * a / e;
	sum = 0;
	for (k = 1; k < n; k++)
	        sum += (100.0 * rate / freq) / pow (1 + yield / freq, k + ds / e);

	result = value_new_float (term1 + term2 + sum - last_term);

 out:
	g_date_free (settlement);
	g_date_free (maturity);
	g_date_free (issue);
	g_date_free (first_coupon);

	return result;
}

/***************************************************************************/

static char *help_oddfyield = {
	N_("@FUNCTION=ODDFYIELD\n"
	   "@SYNTAX=ODDFYIELD(settlement,maturity,issue,first_coupon,rate,"
	   "pr,redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDFYIELD calculates the yield of a security having an odd first "
	   "period. "
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, ODDFYIELD returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddfyield (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_oddlprice = {
	N_("@FUNCTION=ODDLPRICE\n"
	   "@SYNTAX=ODDLPRICE(settlement,maturity,last_interest,rate,yld,"
	   "redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDLPRICE calculates the price per $100 face value of a security "
	   "that has an odd last coupon period. "
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, ODDFYIELD returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddlprice (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_oddlyield = {
	N_("@FUNCTION=ODDLYIELD\n"
	   "@SYNTAX=ODDLYIELD(settlement,maturity,last_interest,rate,pr,"
	   "redemption,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "ODDLYIELD calculates the yield of a security having an odd last "
	   "period. "
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, ODDLYIELD returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_oddlyield (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_amordegrc = {
	N_("@FUNCTION=AMORDEGRC\n"
	   "@SYNTAX=AMORDEGRC(cost,purchase_date,first_period,salvage,period,"
	   "rate[,basis])\n"
	   "@DESCRIPTION="
	   "AMORDEGRC returns the depreciation for each accounting period."
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, AMORDEGRC returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_amordegrc (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_amorlinc = {
	N_("@FUNCTION=AMORLINC\n"
	   "@SYNTAX=AMORLINC(cost,purchase_date,first_period,salvage,period,"
	   "rate[,basis])\n"
	   "@DESCRIPTION="
	   "AMORLINC returns the depreciation for each accounting period."
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, AMORLINC returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_amorlinc (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_coupdaybs = {
	N_("@FUNCTION=COUPDAYBS\n"
	   "@SYNTAX=COUPDAYBS(settlement,maturity,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "COUPDAYBS returns the number of days from the beginning of the "
	   "coupon period to the settlement date. "
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, COUPDAYBS returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_coupdaybs (FunctionEvalInfo *ei, Value **argv)
{
        GDate *settlement;
        GDate *maturity;
        int freq, basis;
        int days;
	Value *result;

        settlement = datetime_value_to_g (argv[0]);
        maturity   = datetime_value_to_g (argv[1]);
        freq       = value_get_as_int (argv[2]);
	basis      = argv[3] ? value_get_as_int (argv[3]) : 0;

	if (!maturity || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
	    || g_date_compare (settlement, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        days = coupdaybs (settlement, maturity, freq, basis);

        if (days < 0)
	        result = value_new_error (ei->pos, "#UNIMPLEMENTED!");
	else
		result = value_new_int (days);

 out:
	g_date_free (settlement);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_coupdays = {
	N_("@FUNCTION=COUPDAYS\n"
	   "@SYNTAX=COUPDAYS(settlement,maturity,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "COUPDAYS returns the number of days in the coupon period of the "
	   "settlement date."
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, COUPDAYS returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_coupdays (FunctionEvalInfo *ei, Value **argv)
{
        GDate *settlement;
        GDate *maturity;
        int freq, basis;
        gnum_float days;
	Value *result;

        settlement = datetime_value_to_g (argv[0]);
        maturity   = datetime_value_to_g (argv[1]);
        freq       = value_get_as_int (argv[2]);
	basis      = argv[3] ? value_get_as_int (argv[3]) : 0;

	if (!maturity || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
	    || g_date_compare (settlement, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        days = coupdays (settlement, maturity, freq, basis);

        if (days < 0) {
	        result = value_new_error (ei->pos, "#UNIMPLEMENTED!");
		goto out;
	}

        result = value_new_float (days);

 out:
	g_date_free (settlement);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_coupdaysnc = {
	N_("@FUNCTION=COUPDAYSNC\n"
	   "@SYNTAX=COUPDAYSNC(settlement,maturity,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "COUPDAYSNC returns the number of days from the settlement date "
	   "to the next coupon date. "
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, COUPDAYSNC returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_coupdaysnc (FunctionEvalInfo *ei, Value **argv)
{
        GDate      *settlement;
        GDate      *maturity;
        int        freq, basis;
        gnum_float days;
	Value      *result;

        settlement = datetime_value_to_g (argv[0]);
        maturity   = datetime_value_to_g (argv[1]);
        freq       = value_get_as_int (argv[2]);
	basis      = argv[3] ? value_get_as_int (argv[3]) : 0;

	if (!maturity || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
	    || g_date_compare (settlement, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        days = coupdaysnc (settlement, maturity, freq, basis);

        if (days < 0)
	        result = value_new_error (ei->pos, "#UNIMPLEMENTED!");
	else
		result = value_new_float (days);

 out:
	g_date_free (settlement);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_coupncd = {
	N_("@FUNCTION=COUPNCD\n"
	   "@SYNTAX=COUPNCD(settlement,maturity,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "COUPNCD returns the coupon date following settlement."
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, COUPNCD returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_coupncd (FunctionEvalInfo *ei, Value **argv)
{
        GDate   *settlement;
        GDate   *maturity;
        GDate   *date;
        int     freq, basis;
	Value   *result;

        settlement = datetime_value_to_g (argv[0]);
        maturity   = datetime_value_to_g (argv[1]);
        freq       = value_get_as_int (argv[2]);
	basis      = argv[3] ? value_get_as_int (argv[3]) : 0;

	if (!maturity || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
	    || g_date_compare (settlement, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        date = coupncd (settlement, maturity, freq, basis);
	result = value_new_int (datetime_g_to_serial (date));
	g_date_free (date);

 out:
	g_date_free (settlement);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_couppcd = {
	N_("@FUNCTION=COUPPCD\n"
	   "@SYNTAX=COUPPCD(settlement,maturity,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "COUPPCD returns the coupon date preceeding settlement."
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, COUPPCD returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_couppcd (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_coupnum = {
	N_("@FUNCTION=COUPNUM\n"
	   "@SYNTAX=COUPNUM(settlement,maturity,frequency[,basis])\n"
	   "@DESCRIPTION="
	   "COUPNUM returns the numbers of coupons to be paid between "
	   "the settlement and maturity dates, rounded up."
	   "@settlement is the settlement date of the security. "
	   "@maturity is the maturity date of the security. "
	   "@frequency is the number of coupon payments per year. "
	   "Allowed frequencies are: 1 = annual, 2 = semi, 4 = quarterly. "
	   "@basis is the type of day counting system you want to use:\n"
	   "\n"
	   "0  US 30/360\n"
	   "1  actual days/actual days\n"
	   "2  actual days/360\n"
	   "3  actual days/365\n"
	   "4  European 30/360\n"
	   "\n"
	   "If @frequency is other than 1, 2, or 4, COUPNUM returns #NUM! "
	   "error. "
	   "If @basis is omitted, US 30/360 is applied. "
	   "If @basis is not in between 0 and 4, #NUM! error is returned. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_coupnum (FunctionEvalInfo *ei, Value **argv)
{
        GDate *settlement;
        GDate *maturity;
        int freq, basis;
        gnum_float n;
	Value *result;

        settlement = datetime_value_to_g (argv[0]);
        maturity   = datetime_value_to_g (argv[1]);
        freq       = value_get_as_int (argv[2]);
	basis      = argv[3] ? value_get_as_int (argv[3]) : 0;

	if (!maturity || !settlement) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
		goto out;
	}

        if (basis < 0 || basis > 4 || (freq != 1 && freq != 2 && freq != 4)
	    || g_date_compare (settlement, maturity) > 0) {
		result = value_new_error (ei->pos, gnumeric_err_NUM);
		goto out;
	}

        n = coupnum (settlement, maturity, freq, basis);

        result = value_new_float (n);

 out:
	g_date_free (settlement);
	g_date_free (maturity);

	return result;
}

/***************************************************************************/

static char *help_cumipmt = {
	N_("@FUNCTION=CUMIPMT\n"
	   "@SYNTAX=CUMIPMT(rate,nper,pv,start_period,end_period,type)\n"
	   "@DESCRIPTION="
	   "CUMIPMT returns the cumulative interest paid on a loan between "
	   "@start_period and @end_period."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_cumipmt (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_cumprinc = {
	N_("@FUNCTION=CUMPRINC\n"
	   "@SYNTAX=CUMPRINC(rate,nper,pv,start_period,end_period,type)\n"
	   "@DESCRIPTION="
	   "CUMPRINC returns the cumulative principal paid on a loan between "
	   "@start_period and @end_period."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_cumprinc (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_mduration = {
	N_("@FUNCTION=MDURATION\n"
	   "@SYNTAX=MDURATION(settlement,maturity,coupon,yield,frequency"
	   "[,basis])\n"
	   "@DESCRIPTION="
	   "MDURATION returns the Macauley duration for a security with par "
	   "value 100."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_mduration (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

static char *help_vdb = {
	N_("@FUNCTION=VDB\n"
	   "@SYNTAX=VDB(cost,salvage,life,start_period,end_period[,factor"
	   ",switch])\n"
	   "@DESCRIPTION="
	   "VDB calculates the depreciation of an asset for a given period "
	   "or partial period using the double-declining balance method."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DB")
};

static Value *
gnumeric_vdb (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, "#UNIMPLEMENTED!");
}

/***************************************************************************/

void finance_functions_init (void);
void
finance_functions_init (void)
{
	FunctionDefinition *def;
	FunctionCategory *cat = function_get_category_with_translation
	        ("Financial", _("Financial"));

	def = function_add_args	 (cat, "accrint", "???fff|f",
				  "issue,first_interest,settlement,rate,par,"
				  "frequency[,basis]",
				  &help_accrint, gnumeric_accrint);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "accrintm", "??f|ff",
				  "issue,maturity,rate[,par,basis]",
				  &help_accrintm, gnumeric_accrintm);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "amordegrc", "fffffff",
				  "cost,purchase_date,first_period,salvage,"
				  "period,rate,basis",
				  &help_amordegrc, gnumeric_amordegrc);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "amorlinc", "fffffff",
				  "cost,purchase_date,first_period,salvage,"
				  "period,rate,basis",
				  &help_amorlinc, gnumeric_amorlinc);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "coupdaybs", "fff|f",
				  "settlement,maturity,frequency[,basis]",
				  &help_coupdaybs, gnumeric_coupdaybs);

	def = function_add_args	 (cat, "coupdays", "fff|f",
				  "settlement,maturity,frequency[,basis]",
				  &help_coupdays, gnumeric_coupdays);

	def = function_add_args	 (cat, "coupdaysnc", "fff|f",
				  "settlement,maturity,frequency[,basis]",
				  &help_coupdaysnc, gnumeric_coupdaysnc);

	def = function_add_args	 (cat, "coupncd", "fff|f",
				  "settlement,maturity,frequency[,basis]",
				  &help_coupncd, gnumeric_coupncd);
	auto_format_function_result (def, AF_DATE);

	def = function_add_args	 (cat, "coupnum", "fff|f",
				  "settlement,maturity,frequency[,basis]",
				  &help_coupnum, gnumeric_coupnum);

	def = function_add_args	 (cat, "couppcd", "fff|f",
				  "settlement,maturity,frequency[,basis]",
				  &help_couppcd, gnumeric_couppcd);
	auto_format_function_result (def, AF_DATE);

	def = function_add_args	 (cat, "cumipmt", "ffffff",
				  "rate,nper,pv,start_period,end_period,type",
				  &help_cumipmt, gnumeric_cumipmt);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "cumprinc", "ffffff",
				  "rate,nper,pv,start_period,end_period,type",
				  &help_cumprinc, gnumeric_cumprinc);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "db", "ffff|f",
				  "cost,salvage,life,period[,month]",
				  &help_db, gnumeric_db);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "ddb", "ffff|f",
				  "cost,salvage,life,period[,factor]",
				  &help_ddb, gnumeric_ddb);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args  (cat, "disc", "??ff|f",
				  "settlement,maturity,pr,redemption[,basis]",
				  &help_disc, gnumeric_disc);

	def = function_add_args	 (cat, "dollarde", "ff",
				  "fractional_dollar,fraction",
				  &help_dollarde, gnumeric_dollarde);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "dollarfr", "ff",
				  "decimal_dollar,fraction",
				  &help_dollarfr, gnumeric_dollarfr);

	def = function_add_args	 (cat, "duration", "fff", "rate,pv,fv",
				  &help_duration, gnumeric_duration);

	def = function_add_args	 (cat, "effect",   "ff",    "rate,nper",
				  &help_effect,	  gnumeric_effect);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "euro", "s", "currency",
				  &help_euro,	  gnumeric_euro);

	def = function_add_args	 (cat, "fv", "fff|ff", "rate,nper,pmt,pv,type",
				  &help_fv,	  gnumeric_fv);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "fvschedule", "fA", "pv,schedule",
				  &help_fvschedule, gnumeric_fvschedule);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "intrate", "??ff|f",
				  "settlement,maturity,investment,redemption"
				  "[,basis]",
				  &help_intrate,  gnumeric_intrate);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "ipmt", "ffff|ff",
				  "rate,per,nper,pv,fv,type",
				  &help_ipmt,	  gnumeric_ipmt);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "irr", "A|f",
				  "values[,guess]",
				  &help_irr,	  gnumeric_irr);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "ispmt", "ffff",	   "rate,per,nper,pv",
				  &help_ispmt,	gnumeric_ispmt);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "mduration", "fffff|f",
				  "settlement,maturify,coupon,yield,"
				  "frequency[,basis]",
				  &help_mduration, gnumeric_mduration);

	def = function_add_args	 (cat, "mirr", "Aff",
				  "values,finance_rate,reinvest_rate",
				  &help_mirr,	  gnumeric_mirr);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "nominal", "ff",	   "rate,nper",
				  &help_nominal,  gnumeric_nominal);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "nper", "fffff", "rate,pmt,pv,fv,type",
				  &help_nper,	  gnumeric_nper);

	def = function_add_nodes (cat, "npv",	  0,	  "",
				  &help_npv,	  gnumeric_npv);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args  (cat, "oddfprice", "????fffff",
				  "settlement,maturity,issue,first_coupon,"
				  "rate,yld,redemption,frequency,basis",
				  &help_oddfprice,  gnumeric_oddfprice);

	def = function_add_args  (cat, "oddfyield", "????fffff",
				  "settlement,maturity,issue,first_coupon,"
				  "rate,pr,redemption,frequency,basis",
				  &help_oddfyield,  gnumeric_oddfyield);

	def = function_add_args  (cat, "oddlprice", "???fffff",
				  "settlement,maturity,last_interest,rate,"
				  "yld,redemption,frequency,basis",
				  &help_oddlprice,  gnumeric_oddlprice);

	def = function_add_args  (cat, "oddlyield", "???fffff",
				  "settlement,maturity,last_interest,rate,"
				  "pr,redemption,frequency,basis",
				  &help_oddlyield,  gnumeric_oddlyield);

	def = function_add_args	 (cat, "pmt", "fff|ff",
				  "rate,nper,pv[,fv,type]",
				  &help_pmt,	  gnumeric_pmt);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "ppmt", "ffff|ff",
				  "rate,per,nper,pv[,fv,type]",
				  &help_ppmt,	  gnumeric_ppmt);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args  (cat, "price", "??fff|ff",
				  "settle,mat,rate,yield,redemption_price,"
				  "frequency,basis",
				  &help_price, gnumeric_price);

	def = function_add_args	 (cat, "pricedisc", "??ff|f",
				  "settlement,maturity,discount,"
				  "redemption[,basis]",
				  &help_pricedisc,  gnumeric_pricedisc);

	def = function_add_args	 (cat, "pricemat", "???ff|f",
				  "settlement,maturity,issue,rate,"
				  "yield[,basis]",
				  &help_pricemat,  gnumeric_pricemat);

	def = function_add_args	 (cat, "pv", "fff|ff",
				  "rate,nper,pmt[,fv,type]",
				  &help_pv,	  gnumeric_pv);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "rate", "fff|fff",
				  "rate,nper,pmt,fv,type,guess",
				  &help_rate,	  gnumeric_rate);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "received", "??ff|f",
				  "settlement,maturity,investment,"
				  "discount[,basis]",
				  &help_received,  gnumeric_received);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "sln", "fff", "cost,salvagevalue,life",
				  &help_sln,	  gnumeric_sln);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "syd", "ffff",
				  "cost,salvagevalue,life,period",
				  &help_syd,	  gnumeric_syd);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args  (cat, "tbilleq", "??f",
				  "settlement,maturity,discount",
				  &help_tbilleq,  gnumeric_tbilleq);

	def = function_add_args  (cat, "tbillprice", "??f",
				  "settlement,maturity,discount",
				  &help_tbillprice, gnumeric_tbillprice);

	def = function_add_args  (cat, "tbillyield", "??f",
				  "settlement,maturity,pr",
				  &help_tbillyield, gnumeric_tbillyield);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "vdb", "fffff|ff",
				  "cost,salvage,life,start_period,"
				  "end_period[,factor,switch]",
				  &help_vdb, gnumeric_vdb);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args	 (cat, "xirr", "AA|f", "values,dates[,guess]",
				  &help_xirr,	  gnumeric_xirr);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args	 (cat, "xnpv", "fAA", "rate,values,dates",
				  &help_xnpv,	  gnumeric_xnpv);
	auto_format_function_result (def, AF_MONETARY);

	def = function_add_args  (cat, "yield", "??fff|ff",
				  "settle,mat,rate,price,redemption_price,"
				  "frequency,basis",
				  &help_yield, gnumeric_yield);
	auto_format_function_result (def, AF_PERCENT);

	def = function_add_args  (cat, "yielddisc", "??fff",
				  "settlement,maturity,pr,redemption,basis",
				  &help_yielddisc,  gnumeric_yielddisc);

	def = function_add_args  (cat, "yieldmat", "???fff",
				  "settlement,maturity,issue,rate,pr,basis",
				  &help_yieldmat,  gnumeric_yieldmat);
}
