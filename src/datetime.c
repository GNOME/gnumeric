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
static int const date_serial_19000228 = 59;

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

/**
 * Free GDate. Can be called with NULL without complaining.
 */
void
datetime_g_free (GDate *d)
{
	if (d != NULL)
		g_date_free (d);
}

/* ------------------------------------------------------------------------- */

gnum_float
datetime_value_to_serial_raw (Value const *v)
{
	gnum_float serial;

	if (VALUE_IS_NUMBER (v))
		serial = value_get_as_float (v);
	else {
		char const *str = value_peek_string (v);
		Value *conversion = format_match (str, NULL);

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
	return datetime_g_to_serial (&date) + secs / (gnum_float)SECS_PER_DAY;
}

/* ------------------------------------------------------------------------- */

int
datetime_serial_raw_to_serial (gnum_float raw)
{
	return (int) floor (raw + HALF_SEC);
}

/* ------------------------------------------------------------------------- */

int
datetime_value_to_serial (Value const *v)
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
datetime_value_to_g (Value const *v)
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
datetime_value_to_seconds (Value const *v)
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

static gint32
days_between_BASIS_MSRB_30_360 (GDate *from, GDate *to)
{
	int y1, m1, d1, y2, m2, d2;

	y1 = g_date_year (from);
	m1 = g_date_month (from);
	d1 = g_date_day (from);
	y2 = g_date_year (to);
	m2 = g_date_month (to);
	d2 = g_date_day (to);

	if (d1 >= 30) {
		d1 = 30;
		if (d2 == 31)
			d2 = 30;
	}

	return (y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1);
}

static gint32
days_between_BASIS_30E_360 (GDate *from, GDate *to)
{
	int y1, m1, d1, y2, m2, d2;

	y1 = g_date_year (from);
	m1 = g_date_month (from);
	d1 = g_date_day (from);
	y2 = g_date_year (to);
	m2 = g_date_month (to);
	d2 = g_date_day (to);

	if (d1 == 31)
		d1 = 30;
	if (d2 == 31)
		d2 = 30;

	return (y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1);
}

static gint32
days_between_BASIS_30Ep_360 (GDate *from, GDate *to)
{
	int y1, m1, d1, y2, m2, d2;

	y1 = g_date_year (from);
	m1 = g_date_month (from);
	d1 = g_date_day (from);
	y2 = g_date_year (to);
	m2 = g_date_month (to);
	d2 = g_date_day (to);

	if (d1 == 31)
		d1 = 30;
	if (d2 == 31) {
		d2 = 1;
		m2++;
		/* No need to check for m2 == 13 since 12*30 == 360 */
	}

	return (y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1);
}

/*
 * days_between_basis
 *
 * @from      : GDate *
 * @to        : GDate *
 * @basis     : basis_t
 * see datetime.h and doc/fn-financial-basis.txt for details
 *
 * @in_order  : dates are considered in order
 *
 * returns    : Number of days strictly between from and to +1
 *
 */

gint32
days_between_basis (GDate *from, GDate *to, int basis)
{
	switch (g_date_compare (from, to)) {
	case 1:
		return (- days_between_basis (to, from, basis));
	default:
		break;
	}

	switch (basis) {
	case BASIS_ACT_ACT:
	case BASIS_ACT_360:
	case BASIS_ACT_365:
		return (g_date_julian (to) - g_date_julian (from));
		break;
	case BASIS_30E_360:
		return days_between_BASIS_30E_360 (from, to);
		break;
	case BASIS_30Ep_360:
		return days_between_BASIS_30Ep_360 (from, to);
		break;
	case BASIS_MSRB_30_360:
	default:
		return days_between_BASIS_MSRB_30_360 (from, to);
		break;
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
		/* Change to g_date_get_days_in_month with glib 1.8+ */
	} while (g_date_compare (settlement, result) < 0 );

	if (next) {
		g_date_set_julian (result, g_date_julian (maturity));
		periods--;
		g_date_subtract_months (result, periods * months);
		if (is_eom_special)
			while (!g_date_is_last_of_month (result))
				g_date_add_days (result, 1);
		/* Change to g_date_get_days_in_month with glib 1.8+ */
	}

	return result;
}

/* ------------------------------------------------------------------------- */


/*
 * Returns the number of days in the coupon period of the settlement date.
 * Currently, returns negative numbers if the branch is not implemented.
 */
gnum_float
coupdays (GDate *settlement, GDate *maturity, int freq, basis_t basis, gboolean eom)
{
	GDate *prev;
	GDate *next;
	gint32   days;

        switch (basis) {
	case BASIS_MSRB_30_360:
	case BASIS_ACT_360:
        case BASIS_30E_360:
        case BASIS_30Ep_360:
		return 360 / freq;
	case BASIS_ACT_365:
		return 365.0 / freq;
	case BASIS_ACT_ACT:
	default:
		next = coup_cd (settlement, maturity, freq, eom, TRUE);
		prev = coup_cd (settlement, maturity, freq, eom, FALSE);
		days = days_between_basis (prev, next, BASIS_ACT_ACT);
		datetime_g_free (prev);
		datetime_g_free (next);
		return days;
        }
}

/* ------------------------------------------------------------------------- */


/*
 * Returns the number of days from the beginning of the coupon period to
 * the settlement date.
 */
gnum_float
coupdaybs (GDate *settlement, GDate *maturity, int freq, basis_t basis, gboolean eom)
{
	GDate      *prev_coupon;
	gint32        days;

	prev_coupon = coup_cd (settlement, maturity, freq, eom, FALSE);
	days = days_between_basis (prev_coupon, settlement, basis);
	datetime_g_free (prev_coupon);
	return days;
}

/* ------------------------------------------------------------------------- */


/*
 * Returns the number of days from the settlement date to the next
 * coupon date.
 */

gnum_float
coupdaysnc (GDate *settlement, GDate *maturity, int freq, basis_t basis, gboolean eom)
{
	GDate      *next_coupon;
	int        days;

	next_coupon = coup_cd (settlement, maturity, freq, eom, TRUE);
	days = days_between_basis (settlement, next_coupon, basis);
	datetime_g_free (next_coupon);
	return days;
}

/* ------------------------------------------------------------------------- */
