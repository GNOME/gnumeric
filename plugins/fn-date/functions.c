/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-date.c:  Built in date functions.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder <terra@diku.dk>
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
#include <str.h>
#include <cell.h>
#include <datetime.h>
#include <value.h>
#include <auto-format.h>
#include <mathfunc.h>

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <libgnome/gnome-i18n.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

#define DAY_SECONDS (3600*24)

/***************************************************************************/

static const char *help_date = {
	N_("@FUNCTION=DATE\n"
	   "@SYNTAX=DATE (year,month,day)\n"

	   "@DESCRIPTION="
	   "DATE returns the number of days since the 1st of january of 1900"
	   "(the date serial number) for the given year, month and day.\n"
	   "\n"
	   "If @month < 1 or @month > 12, the year will be corrected.  A "
	   "similar correction takes place for days.\n"
	   "\n"
	   "The @years should be at least 1900.  If "
	   "@years < 1900, it is assumed to be 1900 + @years.\n"
	   "\n"
	   "If the given date is not valid, DATE returns #NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "DATE(2001, 3, 30) returns 'Mar 30, 2001'.\n "
	   "\n"
	   "@SEEALSO=TODAY, NOW")
};

static Value *
gnumeric_date (FunctionEvalInfo *ei, Value **argv)
{
	int year, month, day;
	GDate date;

	year  = value_get_as_int (argv [0]);
	month = value_get_as_int (argv [1]);
	day   = value_get_as_int (argv [2]);

	if (year < 0 || year > 9999)
		goto error;

	if (year < 1900) /* 1900, not 100.  Ick!  */
		year += 1900;

        g_date_clear (&date, 1);

	g_date_set_dmy (&date, 1, 1, year);
	if (!g_date_valid (&date))
		goto error;

	if (month > 0)
		g_date_add_months (&date, month - 1);
	else
		g_date_subtract_months (&date, 1 - month);
	if (!g_date_valid (&date))
		goto error;

	if (day > 0)
                g_date_add_days (&date, day - 1);
	else
		g_date_subtract_days (&date, 1 - day);
	if (!g_date_valid (&date))
		goto error;

	if (g_date_get_year (&date) < 1900 || g_date_get_year (&date) >= 11900)
		goto error;

	return value_new_int (datetime_g_to_serial (&date));

 error:
	return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_unix2date = {
	N_("@FUNCTION=UNIX2DATE\n"
	   "@SYNTAX=UNIX2DATE(unixtime)\n"

	   "@DESCRIPTION="
	   "UNIX2DATE converts a unix time into a spreadsheet date and time.\n"
	   "\n"
	   "A unix time is the number of seconds since midnight January 1, 1970.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=NOW, DATE, DATE2UNIX")
};

static Value *
gnumeric_unix2date (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float futime = value_get_as_float (argv [0]);
	time_t utime = (time_t)futime;

	/* Check for overflow.  */
	if (gnumabs (futime - utime) >= 1.0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (datetime_timet_to_serial_raw (utime) +
				(futime - utime));
}

/***************************************************************************/

static const char *help_date2unix = {
	N_("@FUNCTION=DATE2UNIX\n"
	   "@SYNTAX=DATE2UNIX(serial)\n"

	   "@DESCRIPTION="
	   "DATE2UNIX converts a spreadsheet date and time serial number "
	   "into a unix time.\n"
	   "\n"
	   "A unix time is the number of seconds since midnight January 1, 1970.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=NOW, DATE, UNIX2DATE")
};

static Value *
gnumeric_date2unix (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float fserial = value_get_as_float (argv [0]);
	int serial = (int)fserial;
	time_t utime = datetime_serial_to_timet (serial);

	/* Check for overflow.  */
	if (gnumabs (fserial - serial) >= 1.0 || utime == (time_t)-1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_int (utime +
		gnumeric_fake_round (DAY_SECONDS * (fserial - serial)));
}

/***************************************************************************/

static const char *help_datevalue = {
	N_("@FUNCTION=DATEVALUE\n"
	   "@SYNTAX=DATEVALUE(date_str)\n"

	   "@DESCRIPTION="
	   "DATEVALUE returns the serial number of the date.  @date_str is "
	   "the string that contains the date.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DATEVALUE(\"1/1/1999\") equals 36161."
	   "\n"
	   "@SEEALSO=DATE")
};

static Value *
gnumeric_datevalue (FunctionEvalInfo *ei, Value **argv)
{
	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);
	return value_new_int (datetime_value_to_serial (argv[0]));
}

/***************************************************************************/

static const char *help_datedif = {
	N_("@FUNCTION=DATEDIF\n"
	   "@SYNTAX=DATEDIF(date1,date2,interval)\n"

	   "@DESCRIPTION="
	   "DATEDIF returns the difference between two dates.  @interval is "
	   "one of six possible values:  \"y\", \"m\", \"d\", \"ym\", "
	   "\"md\", and \"yd\".\n"
	   "The first three options will return the "
	   "number of complete years, months, or days, respectively, between "
	   "the two dates specified.\n"
	   "\"ym\" will return the number of full months between the two "
	   "dates, not including the difference in years.\n"
	   "\"md\" will return the number of full days between the two "
	   "dates, not including the difference in months.\n"
	   "\"yd\" will return the number of full days between the two "
	   "dates, not including the difference in years.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DATEDIF(DATE(2000,4,30),DATE(2003,8,4),\"d\") equals 1191.\n"
	   "DATEDIF(DATE(2000,4,30),DATE(2003,8,4),\"y\") equals 3.\n"
	   "\n"
	   "@SEEALSO=DATE")
};

static int
datedif_opt_ym (GDate *gdate1, GDate *gdate2)
{
	g_assert (g_date_valid (gdate1));
	g_assert (g_date_valid (gdate2));

	return datetime_g_months_between (gdate1, gdate2) % 12;
}

static int
datedif_opt_yd (GDate *gdate1, GDate *gdate2, int excel_compat)
{
	int day;

	g_assert (g_date_valid (gdate1));
	g_assert (g_date_valid (gdate2));

	day = g_date_get_day (gdate1);

	g_date_add_years (gdate1,
			  datetime_g_years_between (gdate1, gdate2));
	/* according to glib.h, feb 29 turns to feb 28 if necessary */

	if (excel_compat) {
		int new_year1, new_year2;

		/* treat all years divisible by four as leap years: */
		/* this is clearly wrong, but it's what Excel does. */
		/* (I use 2004 here since it is clearly a leap year.) */
		new_year1 = 2004 + (g_date_get_year (gdate1) & 0x3);
		new_year2 = new_year1 + (g_date_get_year (gdate2) -
					 g_date_get_year (gdate1));
		g_date_set_year (gdate1, new_year1);
		g_date_set_year (gdate2, new_year2);

		{
			static gboolean need_warning = TRUE;
			if (need_warning) {
				g_warning("datedif is known to differ from Excel "
					  "for some values.");
				need_warning = FALSE;
			}
		}
	}

	return datetime_g_days_between (gdate1, gdate2);
}

static int
datedif_opt_md (GDate *gdate1, GDate *gdate2, int excel_compat)
{
	int day;

	g_assert (g_date_valid (gdate1));
	g_assert (g_date_valid (gdate2));

	day = g_date_get_day (gdate1);

	g_date_add_months (gdate1,
	                   datetime_g_months_between (gdate1, gdate2));
	/* according to glib.h, days>28 decrease if necessary */

	if (excel_compat) {
		int new_year1, new_year2;

		/* treat all years divisible by four as leap years: */
		/* this is clearly wrong, but it's what Excel does. */
		/* (I use 2004 here since it is clearly a leap year.) */
		new_year1 = 2004 + (g_date_get_year (gdate1) & 0x3);
		new_year2 = new_year1 + (g_date_get_year (gdate2) -
					 g_date_get_year (gdate1));
		g_date_set_year (gdate1, new_year1);
		g_date_set_year (gdate2, new_year2);

		/* add back the days if they were decreased by
		   g_date_add_months */
		/* ( i feel this is inferior because it reports e.g.:
		     datedif(1/31/95,3/1/95,"d") == -2 ) */
		g_date_add_days (gdate1,
		                 day - g_date_get_day (gdate1));
	}

	return datetime_g_days_between (gdate1, gdate2);
}

static Value *
gnumeric_datedif (FunctionEvalInfo *ei, Value **argv)
{
	int date1, date2;
	const char *opt;

	GDate *gdate1, *gdate2;
	Value *result;

	date1 = floor (value_get_as_float (argv [0]));
	date2 = floor (value_get_as_float (argv [1]));
	opt = value_peek_string (argv[2]);

	if (date1 > date2) {
		return value_new_error (ei->pos, gnumeric_err_NUM);
	}

	if (!strcmp (opt, "d")) {
		return value_new_int (date2 - date1);
	}

	gdate1 = datetime_serial_to_g (date1);
	gdate2 = datetime_serial_to_g (date2);

	if (!g_date_valid (gdate1) || !g_date_valid (gdate2)) {
		result = value_new_error (ei->pos, gnumeric_err_VALUE);
	} else {
		if (!strcmp (opt, "m")) {
			result = value_new_int (
				datetime_g_months_between (gdate1, gdate2));
		} else if (!strcmp (opt, "y")) {
			result = value_new_int (
				datetime_g_years_between (gdate1, gdate2));
		} else if (!strcmp (opt, "ym")) {
			result = value_new_int (
				datedif_opt_ym (gdate1, gdate2));
		} else if (!strcmp (opt, "yd")) {
			result = value_new_int (
				datedif_opt_yd (gdate1, gdate2, 1));
		} else if (!strcmp (opt, "md")) {
			result = value_new_int (
				datedif_opt_md (gdate1, gdate2, 1));
		} else {
			result = value_new_error (
				ei->pos, gnumeric_err_VALUE);
		}
	}

	datetime_g_free (gdate1);
	datetime_g_free (gdate2);

	return result;
}

/***************************************************************************/

static const char *help_edate = {
	N_("@FUNCTION=EDATE\n"
	   "@SYNTAX=EDATE(date,months)\n"

	   "@DESCRIPTION="
	   "EDATE returns the serial number of the date that is the "
	   "specified number of months before or after a given date.  "
	   "@date is the serial number of the initial date and @months "
	   "is the number of months before (negative number) or after "
	   "(positive number) the initial date.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "If @months is not an integer, it is truncated."
	   "\n"
	   "@EXAMPLES=\n"
	   "EDATE(DATE(2001,12,30),2) returns 'Feb 28, 2002'.\n"
	   "\n"
	   "@SEEALSO=DATE")
};

static Value *
gnumeric_edate (FunctionEvalInfo *ei, Value **argv)
{
	int    serial, months;
	GDate* date;
	Value *res;

	serial = value_get_as_int(argv[0]);
	months = value_get_as_int(argv[1]);

	date = datetime_serial_to_g (serial);

	if (!g_date_valid (date)) {
                  datetime_g_free (date);
                  return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	if (months > 0)
	        g_date_add_months (date, months);
	else
	        g_date_subtract_months (date, -months);

	if (!g_date_valid (date)) {
                  datetime_g_free (date);
                  return value_new_error (ei->pos, gnumeric_err_NUM);
	}

	res = value_new_int (datetime_g_to_serial (date));
	datetime_g_free (date);
	return res;
}

/***************************************************************************/

static const char *help_today = {
	N_("@FUNCTION=TODAY\n"
	   "@SYNTAX=TODAY()\n"

	   "@DESCRIPTION="
	   "TODAY returns the serial number for today (the number of days "
	   "elapsed since the 1st of January of 1900).\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "TODAY() returns 'Nov 6, 2001' on that particular day.\n "
	   "\n"
	   "@SEEALSO=NOW")
};

static Value *
gnumeric_today (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_int (datetime_timet_to_serial (time (NULL)));
}

/***************************************************************************/

static const char *help_now = {
	N_("@FUNCTION=NOW\n"
	   "@SYNTAX=NOW ()\n"

	   "@DESCRIPTION="
	   "NOW returns the serial number for the date and time at the time "
	   "it is evaluated.\n"
	   ""
	   "Serial Numbers in Gnumeric are represented as follows:"
	   "The integral part is the number of days since the 1st of "
	   "January of 1900.  The decimal part represent the fraction "
	   "of the day and is mapped into hour, minutes and seconds.\n"
	   ""
	   "For example: .0 represents the beginning of the day, and 0.5 "
	   "represents noon.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "NOW().\n"
	   "\n"
	   "@SEEALSO=TODAY")
};

static Value *
gnumeric_now (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (datetime_timet_to_serial_raw (time (NULL)));
}

/***************************************************************************/

static const char *help_time = {
	N_("@FUNCTION=TIME\n"
	   "@SYNTAX=TIME (hours,minutes,seconds)\n"

	   "@DESCRIPTION="
	   "TIME returns a fraction representing the time of day.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "TIME(3, 5, 23) equals 3:05AM.\n"
	   "\n"
	   "@SEEALSO=HOUR")
};

static Value *
gnumeric_time (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float hours, minutes, seconds;

	hours   = value_get_as_float (argv [0]);
	minutes = value_get_as_float (argv [1]);
	seconds = value_get_as_float (argv [2]);

	return value_new_float ((hours * 3600 + minutes * 60 + seconds) /
				DAY_SECONDS);
}

/***************************************************************************/

static const char *help_timevalue = {
	N_("@FUNCTION=TIMEVALUE\n"
	   "@SYNTAX=TIMEVALUE (timetext)\n"

	   "@DESCRIPTION="
	   "TIMEVALUE returns a fraction representing the time of day, a number "
	   "between 0 and 1.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "TIMEVALUE(\"3:05\") equals 0.128472.\n"
	   "TIMEVALUE(\"2:24:53 PM\") equals 0.600613.\n"
	   "\n"
	   "@SEEALSO=HOUR,MINUTE")
};

static Value *
gnumeric_timevalue (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float raw;
	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	raw = datetime_value_to_serial_raw (argv[0]);
	return value_new_float (raw - (int)raw);
}

/***************************************************************************/

static const char *help_hour = {
	N_("@FUNCTION=HOUR\n"
	   "@SYNTAX=HOUR (serial_number)\n"

	   "@DESCRIPTION="
	   "HOUR converts a serial number to an hour.  The hour is returned as "
	   "an integer in the range 0 (12:00 A.M.) to 23 (11:00 P.M.)."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "HOUR(0.128472) equals 3.\n"
	   "\n"
	   "@SEEALSO=MINUTE, NOW, TIME, SECOND")
};

static Value *
gnumeric_hour (FunctionEvalInfo *ei, Value **argv)
{
	int secs;
	secs = datetime_value_to_seconds (argv[0]);
	return value_new_int (secs / 3600);
}

/***************************************************************************/

static const char *help_minute = {
	N_("@FUNCTION=MINUTE\n"
	   "@SYNTAX=MINUTE (serial_number)\n"

	   "@DESCRIPTION="
	   "MINUTE converts a serial number to a minute.  The minute is returned "
	   "as an integer in the range 0 to 59."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "MINUTE(0.128472) equals 5.\n"
	   "\n"
	   "@SEEALSO=HOUR, NOW, TIME, SECOND")
};

static Value *
gnumeric_minute (FunctionEvalInfo *ei, Value **argv)
{
	int secs;

	secs = datetime_value_to_seconds (argv[0]);
	return value_new_int ((secs / 60) % 60);
}

/***************************************************************************/

static const char *help_second = {
	N_("@FUNCTION=SECOND\n"
	   "@SYNTAX=SECOND (serial_number)\n"

	   "@DESCRIPTION="
	   "SECOND converts a serial number to a second.  The second is returned "
	   "as an integer in the range 0 to 59."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "SECOND(0.600613) equals 53.\n"
	   "\n"
	   "@SEEALSO=HOUR, MINUTE, NOW, TIME")
};

static Value *
gnumeric_second (FunctionEvalInfo *ei, Value **argv)
{
	int secs;

	secs = datetime_value_to_seconds (argv[0]);
	return value_new_int (secs % 60);
}

/***************************************************************************/

static const char *help_year = {
	N_("@FUNCTION=YEAR\n"
	   "@SYNTAX=YEAR (serial_number)\n"

	   "@DESCRIPTION="
	   "YEAR converts a serial number to a year."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "YEAR(DATE(2003, 4, 30)) equals 2003.\n"
	   "\n"
	   "@SEEALSO=DAY, MONTH, TIME, NOW")
};

static Value *
gnumeric_year (FunctionEvalInfo *ei, Value **argv)
{
	int res = 1900;
	GDate *date;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date != NULL) {
		res = g_date_get_year (date);
		g_date_free (date);
	}
	return value_new_int (res);
}

/***************************************************************************/

static const char *help_month = {
	N_("@FUNCTION=MONTH\n"
	   "@SYNTAX=MONTH (serial_number)\n"

	   "@DESCRIPTION="
	   "MONTH converts a serial number to a month."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "MONTH(DATE(2003, 4, 30)) equals 4.\n"
	   "\n"
	   "@SEEALSO=DAY, TIME, NOW, YEAR")
};

static Value *
gnumeric_month (FunctionEvalInfo *ei, Value **argv)
{
	int res = 1;
	GDate *date;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date != NULL) {
		res = g_date_get_month (date);
		g_date_free (date);
	}
	return value_new_int (res);
}

/***************************************************************************/

static const char *help_day = {
	N_("@FUNCTION=DAY\n"
	   "@SYNTAX=DAY (serial_number)\n"

	   "@DESCRIPTION="
	   "DAY converts a serial number to a day of month."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DAY(\"10/24/1968\") equals 24.\n"
	   "\n"
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_day (FunctionEvalInfo *ei, Value **argv)
{
	int res = 1;
	GDate *date;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date != NULL) {
		res = g_date_get_day (date);
		g_date_free (date);
	}
	return value_new_int (res);
}

/***************************************************************************/

static const char *help_weekday = {
	N_("@FUNCTION=WEEKDAY\n"
	   "@SYNTAX=WEEKDAY (serial_number[, method])\n"

	   "@DESCRIPTION="
	   "WEEKDAY converts a serial number to a weekday.\n"
	   "\n"
	   "This function returns an integer indicating the day of week.\n"
	   "@METHOD indicates the numbering system.  It defaults to 1.\n"
	   "\n"
	   "For @METHOD=1: Sunday is 1, Monday is 2, etc.\n"
	   "For @METHOD=2: Monday is 1, Tuesday is 2, etc.\n"
	   "For @METHOD=3: Monday is 0, Tuesday is 1, etc.\n"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "WEEKDAY(\"10/24/1968\") equals 5 (Thursday).\n"
	   "\n"
	   "@SEEALSO=DAY, MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_weekday (FunctionEvalInfo *ei, Value **argv)
{
	int res;
	GDate *date;
	int method = argv[1] ? value_get_as_int (argv[1]) : 1;

	if (method < 1 || method > 3)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	date = datetime_value_to_g (argv[0]);
	if (!date)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	switch (method) {
	case 1: res = (g_date_get_weekday (date) % 7) + 1; break;
	case 2: res = (g_date_get_weekday (date) + 6) % 7 + 1; break;
	case 3: res = (g_date_get_weekday (date) + 6) % 7; break;
	default: abort ();
	}

	g_date_free (date);

	return value_new_int (res);
}

/***************************************************************************/

static const char *help_days360 = {
	N_("@FUNCTION=DAYS360 \n"
	   "@SYNTAX=DAYS360 (date1,date2,method)\n"

	   "@DESCRIPTION="
	   "DAYS360 returns the number of days from @date1 to @date2 following a "
	   "360-day calendar in which all months are assumed to have 30 days."
	   "\n"
	   "If @method is true, the European method will be used.  In this "
	   "case, if the day of the month is 31 it will be considered as 30."
	   "\n"
	   "If @method is false or omitted, the US method will be used.  "
	   "This is a somewhat complicated industry standard method."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DAYS360(DATE(2003, 2, 3), DATE(2007, 4, 2)) equals 1499.\n"
	   "\n"
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_days360 (FunctionEvalInfo *ei, Value **argv)
{
	enum { METHOD_US, METHOD_EUROPE } method;
	GDate *date1, *date2;
	int day1, day2, month1, month2, year1, year2, result;
	gboolean flipped;
	gnum_float serial1, serial2;

	if (argv[2]) {
		gboolean err;
		method = value_get_as_bool (argv[2], &err) ? METHOD_EUROPE :
		  METHOD_US;
		if (err)
			return value_new_error (ei->pos, _("Unsupported method"));
	} else
		method = METHOD_US;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);
	if (argv[1]->type == VALUE_ERROR)
		return value_duplicate (argv[1]);
	serial1 = datetime_value_to_serial (argv[0]);
	serial2 = datetime_value_to_serial (argv[1]);
	if ((flipped = (serial1 > serial2))) {
		gnum_float tmp = serial1;
		serial1 = serial2;
		serial2 = tmp;
	}

	date1 = datetime_serial_to_g (serial1);
	date2 = datetime_serial_to_g (serial2);
	day1 = g_date_get_day (date1);
	day2 = g_date_get_day (date2);
	month1 = g_date_get_month (date1);
	month2 = g_date_get_month (date2);
	year1 = g_date_get_year (date1);
	year2 = g_date_get_year (date2);

	switch (method) {
	case METHOD_US:
		if (month1 == 2 && month2 == 2 &&
		    g_date_is_last_of_month (date1) &&
		    g_date_is_last_of_month (date2))
			day2 = 30;

		if (month1 == 2 && g_date_is_last_of_month (date1))
			day1 = 30;

		if (day2 == 31 && day1 >= 30)
			day2 = 30;

		if (day1 == 31)
			day1 = 30;
		break;

	case METHOD_EUROPE:
		if (day1 == 31)
			day1 = 30;
		if (day2 == 31)
			day2 = 30;
		break;

	default:
		abort ();
	}

	result = ((year2 - year1) * 12 + (month2 - month1)) * 30 +
		(day2 - day1);

	datetime_g_free (date1);
	datetime_g_free (date2);

	return value_new_int (flipped ? -result : result);
}

/***************************************************************************/

static const char *help_eomonth = {
	N_("@FUNCTION=EOMONTH\n"
	   "@SYNTAX=EOMONTH (start_date,months)\n"

	   "@DESCRIPTION="
	   "EOMONTH returns the last day of the month which is @months "
	   "from the @start_date."
	   "\n"
	   "Returns #NUM! if start_date or months are invalid.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 12/21/00 then EOMONTH(A1,0)=12/31/00, "
	   "EOMONTH(A1,5)=5/31/01, and EOMONTH(A1,2)=2/28/01\n"
	   "\n"
	   "@SEEALSO=MONTH")
};

static Value *
gnumeric_eomonth (FunctionEvalInfo *ei, Value **argv)
{
	Value *res;
	int months = 0;
	GDate *date;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date == NULL || !g_date_valid (date))
                  return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (argv[1] != NULL)
		months = value_get_as_int (argv[1]);

	if (months > 0)
		g_date_add_months(date, months);
	else if (months < 0)
		g_date_subtract_months(date, -months);

	g_date_set_day(date, g_date_get_days_in_month(g_date_get_month(date),
						      g_date_get_year(date)));

	res = value_new_int (datetime_g_to_serial (date));
	g_date_free (date);
	return res;
}

/***************************************************************************/

static const char *help_workday = {
	N_("@FUNCTION=WORKDAY\n"
	   "@SYNTAX=WORKDAY (start_date,days,holidays)\n"

	   "@DESCRIPTION="
	   "WORKDAY returns the date which is @days working days "
	   "from the @start_date.  Weekends and holidays optionally "
	   "supplied in @holidays are respected."
	   "\n"
	   "Returns #NUM! if @start_date or @days are invalid.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DAY(WORKDAY(DATE(2001,1,5),30)) equals 16 and\n"
	   "MONTH(WORKDAY(DATE(2001,1,5),30)) equals 2.\n"
	   "\n"
	   "@SEEALSO=NETWORKDAYS")
};

static Value *
gnumeric_workday (FunctionEvalInfo *ei, Value **argv)
{
	Value *res;
	int days;
	GDateWeekday weekday;
	GDate *date;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date == NULL || !g_date_valid (date))
                  return value_new_error (ei->pos, gnumeric_err_VALUE);
	weekday = g_date_get_weekday (date);

	days = value_get_as_int (argv[1]);

#warning WORKDAY is partially unimplemented.
	if (argv[2] != NULL)
		return value_new_error (ei->pos, _("Unimplemented"));

	/* FIXME : How to deal with starting dates that are weekends
	 *         or holidays ?? */
	for (; days < 0 ; ++days) {
		g_date_subtract_days(date, 1);
		if (weekday == G_DATE_MONDAY)
			weekday = G_DATE_SUNDAY;
		else
			--weekday;

		if (weekday == G_DATE_SATURDAY || weekday == G_DATE_SUNDAY)
		/* FIXME : || is_holiday() */
			--days;
	}
	for (; days > 0 ; --days) {
		g_date_add_days(date, 1);
		if (weekday == G_DATE_SUNDAY)
			weekday = G_DATE_MONDAY;
		else
			++weekday;

		if (weekday == G_DATE_SATURDAY || weekday == G_DATE_SUNDAY)
		/* FIXME : || is_holiday() */
			++days;
	}

	res = value_new_int (datetime_g_to_serial (date));
	g_date_free (date);
	return res;
}

/***************************************************************************/

static const char *help_networkdays = {
	N_("@FUNCTION=NETWORKDAYS\n"
	   "@SYNTAX=NETWORKDAYS (start_date,end_date,holidays)\n"

	   "@DESCRIPTION="
	   "NETWORKDAYS returns the number of non-weekend non-holidays between "
	   "@start_date and @end_date including these dates. " 
	   "Holidays are optionally supplied in @holidays."
	   "\n"
	   "Returns #NUM! if start_date or end_date are invalid.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "NETWORKDAYS(DATE(2001,1,2),DATE(2001,2,15)) equals 33.\n"
	   "\n"
	   "@SEEALSO=WORKDAY")
};

/*
 * A utility routine to return the last monday <= the serial if its valid
 * Returns -1 on error
 */
static int
get_serial_weekday (int serial, int * offset)
{
	GDate * date;
	if (serial <= 0)
		return serial;
	date = datetime_serial_to_g (serial);
        if (g_date_valid (date)) {
		/* Jan 1 1900 was a monday so we won't go < 0 */
		*offset = (int)g_date_get_weekday (date) - 1;
		serial -= *offset;
		if (*offset > 4)
			*offset = 4;
	} else
		serial = -1;
	datetime_g_free (date);
	return serial;
}

typedef struct
{
	int start_serial, end_serial;
	int res;
} networkdays_holiday_closure;

static Value *
networkdays_holiday_callback(EvalPos const *ep,
			     Value const *v, void *user_data)
{
	Value *res = NULL;
	networkdays_holiday_closure * close =
	    (networkdays_holiday_closure *)user_data;
	int serial;
	GDate * date;

	if (v->type == VALUE_ERROR)
		return value_duplicate (v);
	serial = datetime_value_to_serial (v);
        if (serial <= 0)
		return value_new_error (ep, gnumeric_err_NUM);

	if (serial < close->start_serial || close->end_serial < serial)
		return NULL;

	date = datetime_serial_to_g (serial);
        if (g_date_valid (date)) {
		if (g_date_get_weekday (date) < G_DATE_SATURDAY)
			++close->res;
	} else
		res = value_new_error (ep, gnumeric_err_NUM);

	datetime_g_free (date);
	return res;
}

static Value *
gnumeric_networkdays (FunctionEvalInfo *ei, Value **argv)
{
	int start_serial;
	int end_serial;
	int start_offset, end_offset, res;
	networkdays_holiday_closure close;
	GDate * start_date;
	


	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);
	if (argv[1]->type == VALUE_ERROR)
		return value_duplicate (argv[1]);

	start_serial = datetime_value_to_serial (argv[0]);
	end_serial = datetime_value_to_serial (argv[1]);

	/* Swap if necessary */
	if (start_serial > end_serial) {
		int tmp = start_serial;
		start_serial = end_serial;
		end_serial = tmp;
	}

	start_date = datetime_serial_to_g (start_serial);
	close.start_serial = start_serial;
	close.end_serial = end_serial;
	close.res = 0;

	/* Move to mondays, and check for problems */
	start_serial = get_serial_weekday (start_serial, &start_offset);
	end_serial = get_serial_weekday (end_serial, &end_offset);
	if (start_serial < 0 || end_serial < 0)
                  return value_new_error (ei->pos, gnumeric_err_NUM);

	res = end_serial - start_serial;
	res -= ((res/7)*2);	/* Remove weekends */

	if (argv[2] != NULL) {
		value_area_foreach (ei->pos, argv[2],
				    &networkdays_holiday_callback,
				    &close);
	}

	res = res - start_offset + end_offset - close.res;

	if (g_date_get_weekday (start_date) < G_DATE_SATURDAY)
		res++;
	datetime_g_free (start_date);

	return value_new_int (res);
}

/***************************************************************************/

static const char *help_isoweeknum = {
	N_("@FUNCTION=ISOWEEKNUM\n"
	   "@SYNTAX=ISOWEEKNUM (date)\n"

	   "@DESCRIPTION="
	   "ISOWEEKNUM returns the ISO 8601 week number of @date."
	   "\n"
	   "Returns #NUM! if date is invalid."
	   "\n"
	   "An ISO 8601 week starts on Monday. Weeks are numbered from 1. A "
	   "week including days from two different years is assigned to the "
	   "year which includes the most days. This means that Dec 31 could "
	   "be in week 1 of the following year, and Jan 1 could be in week 52 "
	   "or 53 of the previous year. ISOWEEKNUM returns the week number, "
	   "while ISOYEAR returns the year the week is assigned to."
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 12/21/00 then ISOWEEKNUM(A1)=51"
	   "\n"
	   "@SEEALSO=WEEKNUM, ISOYEAR")
};

static Value *
gnumeric_isoweeknum (FunctionEvalInfo *ei, Value **argv)
{
	Value *res;
	GDate *date;
	int isoweeknum;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date == NULL || !g_date_valid (date))
                  return value_new_error (ei->pos, gnumeric_err_VALUE);

	isoweeknum = datetime_weeknum (date, WEEKNUM_METHOD_ISO);
	res = value_new_int (isoweeknum);

	g_date_free (date);
	return res;
}

/***************************************************************************/

static const char *help_isoyear = {
	N_("@FUNCTION=ISOYEAR\n"
	   "@SYNTAX=ISOYEAR (date)\n"

	   "@DESCRIPTION="
	   "ISOYEAR returns the year of the ISO 8601 week number of @date."
	   "\n"
	   "Returns #NUM! if date is invalid."
	   "\n"
	   "An ISO 8601 week starts on Monday. Weeks are numbered from 1. A "
	   "week including days from two different years is assigned to the "
	   "year which includes the most days. This means that Dec 31 could "
	   "be in week 1 of the following year, and Jan 1 could be in week 52 "
	   "or 53 of the previous year. ISOYEAR returns the year the week is "
	   "assigned to, while ISOWEEKNUM returns the week number."
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 12/31/2001 then ISOYEAR(A1)=2002"
	   "\n"
	   "@SEEALSO=ISOWEEKNUM")
};

static Value *
gnumeric_isoyear (FunctionEvalInfo *ei, Value **argv)
{
	Value *res;
	GDate *date;
	int isoyear;
	int year;
	int month;
	int isoweeknum;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date == NULL || !g_date_valid (date))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	isoweeknum = datetime_weeknum (date, WEEKNUM_METHOD_ISO);
	year = g_date_get_year (date);
	month = g_date_get_month (date);
	if (isoweeknum >= 52 && month == G_DATE_JANUARY)
		year--;
	else if (isoweeknum == 1 && month == G_DATE_DECEMBER)
		year++;
	
	res = value_new_int (year);

	g_date_free (date);
	return res;
}

/***************************************************************************/

static const char *help_weeknum = {
	N_("@FUNCTION=WEEKNUM\n"
	   "@SYNTAX=WEEKNUM (date, method)\n"

	   "@DESCRIPTION="
	   "WEEKNUM returns the week number of @date according to the given "
	   "@method.\n"
	   "\n"
	   "@method defaults to 1.\n"
	   "For method=1, week starts on Sunday, and days before first Sunday "
	   "are in week 0.\n"
	   "For method=2, week starts on Monday, and days before first Monday "
	   "are in week 0.\n"
	   "For method=150, the ISO 8601 week number is returned.\n"
	   "\n"
	   "Returns #NUM! if date or method is invalid."
	   "\n"
	   "This function is Excel compatible, except that Excel does not "
	   "support ISO 8601 week numbers."
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 12/21/00 then WEEKNUM(A1,2)=51"
	   "\n"
	   "@SEEALSO=ISOWEEKNUM")
};

static Value *
gnumeric_weeknum (FunctionEvalInfo *ei, Value **argv)
{
	Value *res;
	GDate *date;
	int weeknum;
	int method = argv[1] ? value_get_as_int (argv[1]) : 1;

	if (!(method == WEEKNUM_METHOD_SUNDAY ||
	      method == WEEKNUM_METHOD_MONDAY ||
	      method == WEEKNUM_METHOD_ISO))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date == NULL || !g_date_valid (date))
                  return value_new_error (ei->pos, gnumeric_err_VALUE);

	weeknum = datetime_weeknum (date, method);
	res = value_new_int (weeknum);

	g_date_free (date);
	return res;
}

/***************************************************************************/

const ModulePluginFunctionInfo datetime_functions[] = {
	{ "date",        "fff",  "year,month,day", &help_date, gnumeric_date, NULL, NULL, NULL },
	{ "unix2date",   "f",    "unixtime", &help_unix2date, gnumeric_unix2date, NULL, NULL, NULL },
	{ "date2unix",   "f",    "serial", &help_date2unix, gnumeric_date2unix, NULL, NULL, NULL },
	{ "datevalue",   "S",    "date_str", &help_datevalue, gnumeric_datevalue, NULL, NULL, NULL },
	{ "datedif",     "SSs",  "date1,date2,Interval", &help_datedif, gnumeric_datedif, NULL, NULL, NULL },
	{ "day",         "S",    "date", &help_day, gnumeric_day, NULL, NULL, NULL },
	{ "days360",     "SS|f", "date1,date2,method", &help_days360, gnumeric_days360, NULL, NULL, NULL },
	{ "edate",       "ff",   "serial_number,months", &help_edate, gnumeric_edate, NULL, NULL, NULL },
	{ "eomonth",     "S|f",  "start_date,months", &help_eomonth, gnumeric_eomonth, NULL, NULL, NULL },
	{ "hour",        "S",    "time", &help_hour, gnumeric_hour, NULL, NULL, NULL },
	{ "minute",      "S",    "time", &help_minute, gnumeric_minute, NULL, NULL, NULL },
	{ "month",       "S",    "date", &help_month, gnumeric_month, NULL, NULL, NULL },
	{ "networkdays", "SS|?", "start_date,end_date,holidays", &help_networkdays, gnumeric_networkdays, NULL, NULL, NULL },
	{ "now",         "",     "", &help_now, gnumeric_now, NULL, NULL, NULL },
	{ "second",      "S",    "time", &help_second, gnumeric_second, NULL, NULL, NULL },
	{ "time",        "fff",  "hours,minutes,seconds", &help_time, gnumeric_time, NULL, NULL, NULL },
	{ "timevalue",   "S",    "", &help_timevalue, gnumeric_timevalue, NULL, NULL, NULL },
	{ "today",       "",     "", &help_today, gnumeric_today, NULL, NULL, NULL },
	{ "weekday",     "S|f",  "date", &help_weekday, gnumeric_weekday, NULL, NULL, NULL },
	{ "workday",     "Sf|?", "date,days,holidays", &help_workday, gnumeric_workday, NULL, NULL, NULL },
	{ "year",        "S",    "date", &help_year, gnumeric_year, NULL, NULL, NULL },
	{ "isoweeknum",  "S",    "date", &help_isoweeknum, gnumeric_isoweeknum, NULL, NULL, NULL },
	{ "isoyear",     "S",    "date", &help_isoyear, gnumeric_isoyear, NULL, NULL, NULL },
	{ "weeknum",     "S|f",  "date", &help_weeknum, gnumeric_weeknum, NULL, NULL, NULL },
        {NULL}
};

/* FIXME: Should be merged into the above.  */
static const struct {
	const char *func;
	AutoFormatTypes typ;
} af_info[] = {
	{ "date", AF_DATE },
	{ "unix2date", AF_DATE },
	{ "edate", AF_DATE },
	{ "eomonth", AF_DATE },
	{ "time", AF_TIME },
	{ "today", AF_DATE },
	{ NULL, AF_UNKNOWN }
};

void
plugin_init (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_by_name (af_info[i].func, af_info[i].typ);
}

void
plugin_cleanup (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_remove (af_info[i].func);
}
