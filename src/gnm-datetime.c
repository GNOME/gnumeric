/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

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
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "value.h"
#include "gnm-datetime.h"
#include "number-match.h"

gnm_float
datetime_value_to_serial_raw (GnmValue const *v, GODateConventions const *conv)
{
	gnm_float serial;

	if (VALUE_IS_NUMBER (v))
		serial = value_get_as_float (v);
	else {
		char const *str = value_peek_string (v);
		GnmValue *conversion = format_match (str, NULL, conv);

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

int
datetime_value_to_serial (GnmValue const *v, GODateConventions const *conv)
{
	return datetime_serial_raw_to_serial (
		datetime_value_to_serial_raw (v, conv));
}

/* ------------------------------------------------------------------------- */

gboolean
datetime_value_to_g (GDate *res, GnmValue const *v, GODateConventions const *conv)
{
	int serial = datetime_value_to_serial (v, conv);
	if (serial == 0)
		return FALSE;
	datetime_serial_to_g (res, serial, conv);
	return TRUE;
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
annual_year_basis (GnmValue const *value_date, basis_t basis,
		   GODateConventions const *date_conv)
{
        GDate    date;

	switch (basis) {
	case BASIS_MSRB_30_360:
	        return 360;
	case BASIS_ACT_ACT:
		if (!datetime_value_to_g (&date, value_date, date_conv))
		        return -1;
		return g_date_is_leap_year (g_date_get_year (&date))
			? 366 : 365;
	case BASIS_ACT_360:
	        return 360;
	case BASIS_ACT_365:
	        return 365;
	case BASIS_30E_360:
	        return 360;
	default:
	        return -1;
	}
}

gnm_float
yearfrac (GDate const *from, GDate const *to, basis_t basis)
{
	int days = days_between_basis (from, to, basis);
	gnm_float peryear;

	if (days < 0) {
		const GDate *tmp;
		days = -days;
		tmp = from; from = to; to = tmp;
	}

	switch (basis) {
	case BASIS_ACT_ACT: {
		int y1 = g_date_get_year (from);
		int y2 = g_date_get_year (to);
		GDate d1, d2;
		int feb29s, years;

		d1 = *from;
		g_date_add_years (&d1, 1);
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
