/*
 * datetime.c: Date and time routines grabbed from elsewhere.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Morten Welinder <terra@diku.dk>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   Andreas J. Guelzow <aguelzow@taliesin.ca>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "datetime.h"

#include "number-match.h"
#include "value.h"
#include <math.h>

#define SECS_PER_DAY (24 * 60 * 60)
#define HALF_SEC (0.5 / SECS_PER_DAY)

/* ------------------------------------------------------------------------- */

/* One less that the Julian day number of 19000101.  */
static int date_origin = 0;

/*
 * The serial number of 19000228.  Excel allocates a serial number for
 * the non-existing date 19000229.
 */
static const int date_serial_19000228 = 59;

static void
date_init (void)
{
	/* Day 1 means 1st of January of 1900 */
	GDate* date = g_date_new_dmy (1, 1, 1900);
	date_origin = g_date_julian (date) - 1;
	g_date_free (date);
}

/* ------------------------------------------------------------------------- */

int
datetime_g_to_serial (GDate *date)
{
	int day;

	if (!date_origin)
		date_init ();

	day = g_date_julian (date) - date_origin;
	return day + (day > date_serial_19000228);
}

/* ------------------------------------------------------------------------- */

GDate*
datetime_serial_to_g (int serial)
{
	if (!date_origin)
		date_init ();

	if (serial <= date_serial_19000228)
		return g_date_new_julian (serial + date_origin);
	else if (serial == date_serial_19000228 + 1)
		g_warning ("Request for date 19000229.");
	return g_date_new_julian (serial + date_origin - 1);
}

/* ------------------------------------------------------------------------- */

gnum_float
datetime_value_to_serial_raw (const Value *v)
{
	gnum_float serial;

	if (VALUE_IS_NUMBER (v))
		serial = value_get_as_float (v);
	else {
		const char *str = value_peek_string (v);
		Value *conversion = format_match (str, NULL, NULL);

		if (conversion) {
			if (VALUE_IS_NUMBER (conversion))
				serial = value_get_as_float (conversion);
			else
				serial =  0;
			value_release (conversion);
		} else
			serial = 0;
	}
	return serial;
}

/* ------------------------------------------------------------------------- */

gnum_float
datetime_timet_to_serial_raw (time_t t)
{
	struct tm *tm = localtime (&t);
	int secs;
	GDate date;

        g_date_clear (&date, 1);
	g_date_set_time (&date, t);
	secs = tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
	return datetime_g_to_serial (&date) + secs / (double)SECS_PER_DAY;
}

/* ------------------------------------------------------------------------- */

int
datetime_serial_raw_to_serial (gnum_float raw)
{
	return (int) floor (raw + HALF_SEC);
}

/* ------------------------------------------------------------------------- */

int
datetime_value_to_serial (const Value *v)
{
	return datetime_serial_raw_to_serial (datetime_value_to_serial_raw (v));
}

/* ------------------------------------------------------------------------- */

int
datetime_timet_to_serial (time_t t)
{
	return datetime_serial_raw_to_serial (datetime_timet_to_serial_raw (t));
}

/* ------------------------------------------------------------------------- */

time_t
datetime_serial_to_timet (int serial)
{
	GDate* gd = datetime_serial_to_g (serial);
	struct tm tm;

	if (!gd)
		return (time_t)-1;

	g_date_to_struct_tm (gd, &tm);
	g_date_free (gd);

	return mktime (&tm);
}

/* ------------------------------------------------------------------------- */

GDate *
datetime_value_to_g (const Value *v)
{
	int serial = datetime_value_to_serial (v);
	return serial ? datetime_serial_to_g (serial) : NULL;
}

/* ------------------------------------------------------------------------- */
/* This is time-only assuming a 24h day.  It probably loses completely on */
/* days with summer time ("daylight savings") changes.  */

int
datetime_serial_raw_to_seconds (gnum_float raw)
{
	raw += HALF_SEC;
	return (raw - floor (raw)) * SECS_PER_DAY;
}

/* ------------------------------------------------------------------------- */

int
datetime_value_to_seconds (const Value *v)
{
	return datetime_serial_raw_to_seconds (datetime_value_to_serial_raw (v));
}

/* ------------------------------------------------------------------------- */

int
datetime_timet_to_seconds (time_t t)
{
	return datetime_serial_raw_to_seconds (datetime_timet_to_serial_raw (t));
}

/* ------------------------------------------------------------------------- */

int
datetime_g_days_between (GDate* date1, GDate *date2)
{
	g_assert (g_date_valid (date1));
	g_assert (g_date_valid (date2));

	return (int) (g_date_julian (date2) - g_date_julian (date1));
}

/* ------------------------------------------------------------------------- */

int
datetime_g_months_between (GDate *date1, GDate *date2)
{
	g_assert (g_date_valid (date1));
	g_assert (g_date_valid (date2));

	/* find the difference according to the month and year ordinals,
	   but discount the last month if there are not enough days. */
	return 12 * (g_date_year (date2) - g_date_year (date1))
		+ g_date_month (date2) - g_date_month (date1)
		- (g_date_day (date2) >= g_date_day (date1) ? 0 : 1);
}

/* ------------------------------------------------------------------------- */

int
datetime_g_years_between (GDate *date1, GDate *date2)
{
	int months;

	g_assert (g_date_valid (date1));
	g_assert (g_date_valid (date2));

	months = datetime_g_months_between (date1, date2);
	return months > 0 ? months / 12 : -(-months / 12);
}

/* ------------------------------------------------------------------------- */

/**
 * datetime_isoweeknum (GDate *date)
 * @date date
 *
 * Returns the ISO 8601 week number.
 */
static int
datetime_isoweeknum (GDate *date)
{
	int year;
	int week;
	int wday, jan1wday, nextjan1wday;
	GDate jan1date, nextjan1date;

	g_assert (g_date_valid (date));

	year = g_date_year (date);
	wday  = g_date_weekday (date);
	g_date_set_dmy (&jan1date, 1, 1, year);
	jan1wday = g_date_weekday (&jan1date);

	week = g_date_monday_week_of_year (date);

	/* Does date belong to last week of previous year? */
	if ((week == 0) && (jan1wday > G_DATE_THURSDAY)) {
		GDate tmpdate;
		g_date_set_dmy (&tmpdate, 31, 12, year - 1);
		return datetime_isoweeknum (&tmpdate);
	}

	if ((jan1wday <= G_DATE_THURSDAY) &&
	    (jan1wday > G_DATE_MONDAY))
		week++;

	if (week == 53) {
		g_date_set_dmy (&nextjan1date, 1, 1, year + 1);
		nextjan1wday = g_date_weekday (&nextjan1date);
		if (nextjan1wday <= G_DATE_THURSDAY)
			week = 1;
	}
	
	return week;
}

/* ------------------------------------------------------------------------- */

/**
 * datetime_weeknum (GDate *date, int method)
 * @date      date
 * @method    week numbering method
 *
 * Returns week number according to the given method.
 * 1:   Week starts on Sunday. Days before first Sunday are in week 0.
 * 2:   Week starts on Monday. Days before first Monday are in week 0.
 * 150: ISO 8601 week number. See datetime_isoweeknum.
 */
int
datetime_weeknum (GDate *date, int method)
{
	int res;

	g_assert (g_date_valid (date));
	g_assert (method == WEEKNUM_METHOD_SUNDAY ||
		  method == WEEKNUM_METHOD_MONDAY ||
		  method == WEEKNUM_METHOD_ISO);

	switch (method) {
	case WEEKNUM_METHOD_SUNDAY:
		res = g_date_sunday_week_of_year (date); break;
	case WEEKNUM_METHOD_MONDAY:
		res = g_date_monday_week_of_year (date); break;
	case WEEKNUM_METHOD_ISO:
		res = datetime_isoweeknum (date); break;
	default: res = -1;
	}

	return res;
}

/* ------------------------------------------------------------------------- */

/*
 * adjust_dates_basis
 *
 * @from      : GDate *
 * @to        : GDate * 
 * @basis     : int
 *	   "0  US 30/360\n"
 *         "1  actual days/actual days\n"
 *         "2  actual days/360\n"
 *         "3  actual days/365\n"
 *         "4  European 30/360\n"
 *
 * returns    : nothing. As side effects possibly adjusts from and to date
 *
 *
 */

void
adjust_dates_basis (GDate *from, GDate *to, int basis)
{
	switch (basis) {
	case BASIS_30Ep360:
		if (g_date_day (to) >= 30) {
			g_date_set_day (to, 30);
			if (g_date_day (from) == 31)
				g_date_set_day (from, 30);
		}
		break;
	case BASIS_30_360:
		if (g_date_day(from) >= 30) {
			g_date_set_day (from, 30);
			if (g_date_day(to) == 31)
				g_date_set_day (to, 30);
		}
		break;
	case BASIS_30E360:
		if (g_date_day(from) == 31)
			g_date_set_day (from, 30);
		if (g_date_day(to) == 31)
			g_date_set_day (to, 30);
		break;
	default:
		break;
	}
	return;
}

/* ------------------------------------------------------------------------- */

/*
 * days_between_dep_basis
 *
 * @from      : GDate *
 * @to        : GDate * 
 * @basis     : int
 *	   "0  US 30/360\n"
 *         "1  actual days/actual days\n"
 *         "2  actual days/360\n"
 *         "3  actual days/365\n"
 *         "4  European 30/360\n"
 * @in_order  : dates are considered in order
 *
 * returns    : Number of days strictly between from and to +1
 *
 */

gint32
days_between_dep_basis (GDate *from, GDate *to, int basis, gboolean in_order)
{
	GDate      from_date;
	gint32     days;
	gint       years;

	switch (g_date_compare (from, to)) {
	case 1:
		return (- days_between_dep_basis (to, from, basis, !in_order));
	case 0:
		return 0;
	default:
		break;
	} 

	if (in_order) 
		adjust_dates_basis (from, to, basis);
	else
		adjust_dates_basis (to, from, basis);

	if (basis == BASIS_ACTACT) 
		return (g_date_julian (to) - g_date_julian (from));

	g_date_clear (&from_date, 1);
	g_date_set_julian (&from_date, g_date_julian (from));
	years = g_date_year(to) - g_date_year(from);
	g_date_add_years (&from_date, years);

	if ((basis == BASIS_ACT365 || basis == BASIS_ACT360) &&
	    g_date_compare (&from_date, to) >= 0) {
		years -= 1;
		g_date_set_julian (&from_date, g_date_julian (from));
		g_date_add_years (&from_date, years);
	}

	switch (basis) {
	case BASIS_ACT365:
		days = years * 365;
		break;
	default:
		days = years * 360;
		break;
	}

	switch (basis) {
	case BASIS_ACT360:
	case BASIS_ACT365:
		return days + g_date_julian (to) - g_date_julian (&from_date);

	case BASIS_30Ep360:
		return days += (30 * (g_date_month(to) - g_date_month(&from_date))
				+ ((in_order && g_date_is_last_of_month (to)) ? 30 
				   : g_date_day (to)) 
				- ((!in_order && g_date_is_last_of_month (from)) ? 30 
				   : g_date_day (&from_date)));
	default:
		return days += (30 * (g_date_month(to) - g_date_month(&from_date))
			+ g_date_day(to) - g_date_day(&from_date));
	}
}

/* ------------------------------------------------------------------------- */

/*
 * coup_cd
 *
 * @settlement: GDate *
 * @maturity  : GDate *  must follow settlement strictly
 * @freq      : int      divides 12 evenly
 * @eom       : gboolean whether to do special end of month 
 *                       handling
 * @next      : gboolean whether next or previous date
 *
 * returns    : GDate *  next  or previous coupon date
 *
 * this function does not depend on the basis of counting!
 */

GDate *
coup_cd (GDate *settlement, GDate *maturity, int freq, gboolean eom, gboolean next)
{
        int        months, periods;
	GDate      *result;
	gboolean   is_eom_special;

	is_eom_special = eom && g_date_is_last_of_month (maturity);

	months = 12 / freq;
	periods = (g_date_year(maturity) - g_date_year(settlement));
	if (periods > 0)
		periods = (periods - 1) * freq;

	result = g_date_new();

	do {
		g_date_set_julian (result, g_date_julian (maturity));
		periods++;
		g_date_subtract_months (result, periods * months);
		if (is_eom_special) 
			while (!g_date_is_last_of_month (result))
				g_date_add_days (result, 1);
	} while (g_date_compare (settlement, result) < 0 );

	if (next) {
		g_date_set_julian (result, g_date_julian (maturity));
		periods--;
		g_date_subtract_months (result, periods * months);
		if (is_eom_special) 
			while (!g_date_is_last_of_month (result))
				g_date_add_days (result, 1);
	}

	return result;
}

/* ------------------------------------------------------------------------- */

/*
 * coup_cd_xl
 *
 * @settlement: GDate *
 * @maturity  : GDate *  must follow settlement strictly
 * @freq      : int      divides 12 evenly
 * @eom       : gboolean whether to do special end of month 
 *                       handling
 * @next      : gboolean whether next or previous date
 *
 * returns    : GDate *  date as returned by XL
 *
 * this function does not depend on the basis of counting!
 */

GDate *
coup_cd_xl (GDate *settlement, GDate *maturity, int freq, gboolean next)
{
	GDate      *result;
	GDate      coupon;
	gboolean   is_eom_special;
	int        months = 12 / freq;

	is_eom_special = g_date_is_last_of_month (maturity);

	result = g_date_new();

	g_date_clear (&coupon, 1);
	g_date_set_julian (&coupon, g_date_julian (maturity));

	do {
		g_date_set_julian (result, g_date_julian (&coupon));
		g_date_subtract_months (&coupon, months);
		if (is_eom_special) 
			while (!g_date_is_last_of_month (&coupon))
				g_date_add_days (&coupon, 1);
	} while (g_date_compare (settlement, &coupon) < 0 );

	if (!next) {
		g_date_set_julian (result, g_date_julian (&coupon));
	}

	return result;
}

/* ------------------------------------------------------------------------- */


/*
 * Returns the number of days in the coupon period of the settlement date.
 * Currently, returns negative numbers if the branch is not implemented.
 */
gnum_float
coupdays (GDate *settlement, GDate *maturity, int freq, int basis, gboolean eom, 
	  gboolean xl)
{
	GDate *prev;
	GDate *next;
	int   days;

        switch (basis) {
        case BASIS_30Ep360:
        case BASIS_30E360:
        case BASIS_30_360:
		return (12 / freq) * 30;
	case BASIS_ACT360:
		return 360 / freq;
	case BASIS_ACT365:
		return 365.0 / freq;
	case BASIS_ACTACT:
	default:
		next = xl ? coup_cd_xl (settlement, maturity, freq, TRUE) :
			coup_cd (settlement, maturity, freq, eom, TRUE);
		prev = xl ? coup_cd_xl (settlement, next, freq, FALSE) :
			coup_cd (settlement, maturity, freq, eom, FALSE);
		days = days_between_dep_basis (prev, next, basis, TRUE);
		g_date_free (prev);
		g_date_free (next);
		return days;
        }
}

/* ------------------------------------------------------------------------- */


/*
 * Returns the number of days from the beginning of the coupon period to
 * the settlement date.
 */
int
coupdaybs (GDate *settlement, GDate *maturity, int freq, int basis, gboolean eom, 
	   gboolean xl)
{
	GDate      *prev_coupon;
	int        days;

	prev_coupon = xl ? coup_cd_xl (settlement, maturity, freq, FALSE) :
		coup_cd (settlement, maturity, freq, eom, FALSE);
	days = days_between_dep_basis (prev_coupon, settlement, basis, FALSE);
	g_date_free (prev_coupon);
	return days;
}

/* ------------------------------------------------------------------------- */


/*
 * Returns the number of days from the settlement date to the next
 * coupon date.
 */

int
coupdaysnc (GDate *settlement, GDate *maturity, int freq, int basis, gboolean eom, 
	    gboolean xl)
{
	GDate      *next_coupon;
	int        days;

	next_coupon = xl ? coup_cd_xl (settlement, maturity, freq, TRUE) :
		coup_cd (settlement, maturity, freq, eom, TRUE);
	days = days_between_dep_basis (settlement, next_coupon, basis, TRUE);
	g_date_free (next_coupon);
	return days;
}

/* ------------------------------------------------------------------------- */
