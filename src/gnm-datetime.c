/*
 * gnm-datetime.c:
 *
 * Copyright (C) 2005
 *   Miguel de Icaza (miguel@gnu.org)
 *   Morten Welinder <terra@gnome.org>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <goffice/goffice.h>
#include <value.h>
#include <string.h>
#include <gnm-datetime.h>
#include <gnm-format.h>
#include <number-match.h>

/*
 * Figure out whether the format engine in goffice allows negative values
 * or (as XL) considers them errors.
 */
gboolean
gnm_datetime_allow_negative (void)
{
	static int allow = -1;

	if (allow == -1) {
		GOFormat *fmt = go_format_new_from_XL ("yyyy-mm-dd");
		GnmValue *v = value_new_int (-42);
		GODateConventions const *conv =
			go_date_conv_from_str ("Lotus:1900");
		char *text = format_value (fmt, v, -1, conv);

		allow = (strcmp (text, "1899-11-19") == 0);

		value_release (v);
		go_format_unref (fmt);
		g_free (text);
	}

	return (gboolean)allow;
}

gnm_float
datetime_value_to_serial_raw (GnmValue const *v, GODateConventions const *conv)
{
	gnm_float serial;

	if (VALUE_IS_NUMBER (v))
		serial = value_get_as_float (v);
	else {
		char const *str = value_peek_string (v);
		GnmValue *conversion = format_match_number (str, go_format_default_date (), conv);

		if (conversion) {
			serial = value_get_as_float (conversion);
			value_release (conversion);
		} else
			serial = G_MAXINT;
	}

	if (serial < 0 && !gnm_datetime_allow_negative ())
		serial = G_MAXINT;

	return serial;
}

/* ------------------------------------------------------------------------- */

int
datetime_value_to_serial (GnmValue const *v, GODateConventions const *conv)
{
	gnm_float serial = datetime_value_to_serial_raw (v, conv);
	if (serial >= G_MAXINT || serial < G_MININT)
		return G_MAXINT;
	return go_date_serial_raw_to_serial (serial);
}

/* ------------------------------------------------------------------------- */

gboolean
datetime_value_to_g (GDate *res, GnmValue const *v, GODateConventions const *conv)
{
	int serial = datetime_value_to_serial (v, conv);
	if (serial == G_MAXINT) {
		g_date_clear (res, 1);
		return FALSE;
	}
	go_date_serial_to_g (res, serial, conv);
	return g_date_valid (res);
}

/* ------------------------------------------------------------------------- */

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
int
annual_year_basis (GnmValue const *value_date, GOBasisType basis,
		   GODateConventions const *date_conv)
{
        GDate    date;

	switch (basis) {
	case GO_BASIS_MSRB_30_360:
	        return 360;
	case GO_BASIS_ACT_ACT:
		if (!datetime_value_to_g (&date, value_date, date_conv))
		        return -1;
		return g_date_is_leap_year (g_date_get_year (&date))
			? 366 : 365;
	case GO_BASIS_ACT_360:
	        return 360;
	case GO_BASIS_ACT_365:
	        return 365;
	case GO_BASIS_30E_360:
	        return 360;
	default:
	        return -1;
	}
}

gnm_float
yearfrac (GDate const *from, GDate const *to, GOBasisType basis)
{
	int days;
	gnm_float peryear;

	if (!g_date_valid (from) || !g_date_valid (to))
		return gnm_nan;

	days = go_date_days_between_basis (from, to, basis);

	if (days < 0) {
		const GDate *tmp;
		days = -days;
		tmp = from; from = to; to = tmp;
	}

	switch (basis) {
	case GO_BASIS_ACT_ACT: {
		int y1 = g_date_get_year (from);
		int y2 = g_date_get_year (to);
		GDate d1, d2;
		int feb29s, years;

		d1 = *from;
		gnm_date_add_years (&d1, 1);
		if (g_date_compare (to, &d1) > 0) {
			/* More than one year.  */
			years = y2 + 1 - y1;

			g_date_clear (&d1, 1);
			g_date_set_dmy (&d1, 1, 1, y1);

			g_date_clear (&d2, 1);
			g_date_set_dmy (&d2, 1, 1, y2 + 1);

			feb29s = g_date_get_julian (&d2) - g_date_get_julian (&d1) -
				365 * (y2 + 1 - y1);
		} else {
			/* Less than one year.  */
			years = 1;

			if ((g_date_is_leap_year (y1) && g_date_get_month (from) < 3) ||
			    (g_date_is_leap_year (y2) &&
			     (g_date_get_month (to) * 0x100 + g_date_get_day (to) >= 2 * 0x100 + 29)))
				feb29s = 1;
			else
				feb29s = 0;
		}

		peryear = 365 + (gnm_float)feb29s / years;

		break;
	}

	default:
		peryear = annual_year_basis (NULL, basis, NULL);
	}

	return days / peryear;
}

/* ------------------------------------------------------------------------- */
/* Like g_date_add_days, but...
 *
 * 1. Do not spew criticals.
 * 2. Number of days is signed.
 */

void
gnm_date_add_days (GDate *d, int n)
{
	if (!g_date_valid (d))
		return;

	if (n >= 0) {
		guint32 lim = 23936166;  /* 31-Dec-65535 */
		guint32 j = g_date_get_julian (d);

		if (j > lim || (unsigned)n > lim - j)
			goto bad;

		g_date_add_days (d, n);
	} else {
		int m = g_date_get_julian (d) - 1;

		if (m + n <= 0)
			goto bad;

		g_date_subtract_days (d, -n);
	}

	return;

 bad:
	g_date_clear (d, 1);
}

/* Like g_date_add_months, but...
 *
 * 1. Do not spew criticals.
 * 2. Number of months is signed.
 */
void
gnm_date_add_months (GDate *d, int n)
{
	if (!g_date_valid (d))
		return;

	if (n >= 0) {
		int m = (65535 - g_date_get_year (d)) * 12 +
			(12 - g_date_get_month (d));

		if (n > m)
			goto bad;

		g_date_add_months (d, n);
	} else {
		int m = (g_date_get_year (d) - 1) * 12 +
			(g_date_get_month (d) - 1);

		if (m + n <= 0)
			goto bad;

		g_date_subtract_months (d, -n);
	}

	return;

 bad:
	g_date_clear (d, 1);
}

/* Like g_date_add_years, but...
 *
 * 1. Do not spew criticals.
 * 2. Number of years is signed.
 */
void
gnm_date_add_years (GDate *d, int n)
{
	if (!g_date_valid (d))
		return;

	if (n >= 0) {
		int m = 65535 - g_date_get_year (d);

		if (n > m)
			goto bad;

		g_date_add_years (d, n);
	} else {
		int m = g_date_get_year (d) - 1;

		if (m + n <= 0)
			goto bad;

		g_date_subtract_years (d, -n);
	}

	return;

 bad:
	g_date_clear (d, 1);
}

#define DAY_SECONDS (3600*24)
int
datetime_value_to_seconds (GnmValue const *v, GODateConventions const *conv)
{
	int secs;
	gnm_float d = datetime_value_to_serial_raw (v, conv);
	if (d >= G_MAXINT || d < G_MININT)
		return -1;

	/* Add epsilon before we scale and translate because otherwise it
	   will not be enough.  */
	d = gnm_add_epsilon (d);

	/* Get the number down between 0 and 1 before we scale.  */
	d -= gnm_floor (d);

	/* Scale and round.  */
	secs = (int)(gnm_add_epsilon (d) * DAY_SECONDS + GNM_const(0.5));

	/* We rounded, so we might have gone too far.  */
	if (secs >= DAY_SECONDS)
		secs -= DAY_SECONDS;

	return secs;
}
