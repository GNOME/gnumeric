/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-date.c:  Built in date functions.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder <terra@gnome.org>
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
#include <gnm-datetime.h>
#include <value.h>
#include <mathfunc.h>
#include <gnm-format.h>
#include <workbook.h>
#include <sheet.h>
#include <collect.h>
#include <gnm-i18n.h>

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

#define DAY_SECONDS (3600*24)
#define DATE_CONV(ep)		workbook_date_conv ((ep)->sheet->workbook)

static GnmValue *
make_date (GnmValue *res)
{
	value_set_fmt (res, go_format_default_date ());
	return res;
}

static int
value_get_basis (const GnmValue *v, int defalt)
{
	if (v) {
		gnm_float b = value_get_as_float (v);

		if (b < 0 || b >= 6)
			return -1;
		return (int)b;
	} else
		return defalt;
}

/***************************************************************************/

static GnmFuncHelp const help_date[] = {
        { GNM_FUNC_HELP_NAME, F_("DATE:create a date serial value.")},
        { GNM_FUNC_HELP_ARG, F_("year:year of date")},
        { GNM_FUNC_HELP_ARG, F_("month:month of year")},
        { GNM_FUNC_HELP_ARG, F_("day:day of month")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The DATE function creates date serial values.  1-Jan-1900 is serial value 1, 2-Jan-1900 is serial value 2, and so on.  For compatibility reasons, a serial value is reserved for the non-existing date 29-Feb-1900.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{month} or @{day} is less than 1 or too big, then the year and/or month will be adjusted.") },
	{ GNM_FUNC_HELP_NOTE, F_("For spreadsheets created with the Mac version of Excel, serial 1 is 1-Jan-1904.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE(2008,1,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE(2008,13,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE(2008,1,-10)" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "TODAY,YEAR,MONTH,DAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_date (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float year  = value_get_as_float (argv [0]);
	gnm_float month = value_get_as_float (argv [1]);
	gnm_float day   = value_get_as_float (argv [2]);
	GDate date;
	GODateConventions const *conv = DATE_CONV (ei->pos);

	if (year < 0 || year >= 10000)
		goto error;
	if (year < 1900) /* 1900, not 100.  Ick!  */
		year += 1900;

	/* This uses floor and not trunc on purpose.  */
	month = gnm_floor (month);
	if (gnm_abs (month) > 120000)  /* Actual number not critical.  */
		goto error;

	/* This uses floor and not trunc on purpose.  */
	day = gnm_floor (day);
	if (day < -32768 || day >= 32768)
		day = 32767;  /* Absurd, but yes.  */

        g_date_clear (&date, 1);

	g_date_set_dmy (&date, 1, 1, (int)year);
	gnm_date_add_months (&date, (int)month - 1);
	gnm_date_add_days (&date, (int)day - 1);

	if (!g_date_valid (&date) ||
	    g_date_get_year (&date) < gnm_date_convention_base (conv) ||
	    g_date_get_year (&date) >= 11900)
		goto error;

	return make_date (value_new_int (datetime_g_to_serial (&date, conv)));

 error:
	return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_unix2date[] = {
        { GNM_FUNC_HELP_NAME, F_("UNIX2DATE:create a date value from a Unix timestamp")},
        { GNM_FUNC_HELP_ARG, F_("t:Unix time stamp")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The UNIT2DATE function translates Unix timestamps into date serial values.  Unix timestamps are number of seconds since Midnight 1-Jan-1900.") },
        { GNM_FUNC_HELP_EXAMPLES, "=UNIX2DATE(1000000000)" },
        { GNM_FUNC_HELP_SEEALSO, "DATE2UNIX,DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_unix2date (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float futime = value_get_as_float (argv [0]);
	time_t    utime  = (time_t)futime;
	gnm_float serial;

	/* Check for overflow.  */
	if (gnm_abs (futime - utime) >= 1.0)
		return value_new_error_VALUE (ei->pos);

	serial = datetime_timet_to_serial_raw (utime, DATE_CONV (ei->pos));
	if (serial == G_MAXINT)
		return value_new_error_VALUE (ei->pos);

	return make_date (value_new_float (serial +
					   (futime - utime) / DAY_SECONDS));
}

/***************************************************************************/

static GnmFuncHelp const help_date2unix[] = {
        { GNM_FUNC_HELP_NAME, F_("DATE2UNIX:translate a date serial value to a Unix timestamp") },
        { GNM_FUNC_HELP_ARG, F_("d:date serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The DATE2UNIX function translates a date serial values into a Unix timestamp.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2UNIX(DATE(2000,1,1))" },
        { GNM_FUNC_HELP_SEEALSO, "UNIX2DATE,DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_date2unix (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float fserial = value_get_as_float (argv [0]);
	int        serial  = (int)fserial;
	time_t     utime   = datetime_serial_to_timet (serial, DATE_CONV (ei->pos));

	/* Check for overflow.  */
	if (gnm_abs (fserial - serial) >= 1.0 || utime == (time_t)-1)
		return value_new_error_VALUE (ei->pos);

	return value_new_int (utime +
		gnm_fake_round (DAY_SECONDS * (fserial - serial)));
}

/***************************************************************************/

static GnmFuncHelp const help_datevalue[] = {
        { GNM_FUNC_HELP_NAME, F_("DATEVALUE:return date part of a date and time serial value.")},
        { GNM_FUNC_HELP_ARG, F_("serial:date and time serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DATEVALUE returns the date serial value part of a date and time serial value.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DATEVALUE(NOW())" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "TIMEVALUE,DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_datevalue (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_int (datetime_value_to_serial (argv[0], DATE_CONV (ei->pos)));
}

/***************************************************************************/

static GnmFuncHelp const help_datedif[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DATEDIF\n"
	   "@SYNTAX=DATEDIF(date1,date2,interval)\n"

	   "@DESCRIPTION="
	   "DATEDIF returns the difference between two dates.  @interval is "
	   "one of six possible values:  \"y\", \"m\", \"d\", \"ym\", "
	   "\"md\", and \"yd\".\n\n"
	   "The first three options will return the "
	   "number of complete years, months, or days, respectively, between "
	   "the two dates specified.\n\n"
	   "  \"ym\" will return the number of full months between the two "
	   "dates, not including the difference in years.\n"
	   "  \"md\" will return the number of full days between the two "
	   "dates, not including the difference in months.\n"
	   "  \"yd\" will return the number of full days between the two "
	   "dates, not including the difference in years.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DATEDIF(DATE(2000,4,30),DATE(2003,8,4),\"d\") equals 1191.\n"
	   "DATEDIF(DATE(2000,4,30),DATE(2003,8,4),\"y\") equals 3.\n"
	   "\n"
	   "@SEEALSO=DATE")
	},
	{ GNM_FUNC_HELP_END }
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

	gnm_date_add_years (gdate1,
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
				g_warning("datedif is known to differ from "
					  "Excel for some values.");
				need_warning = FALSE;
			}
		}
	}

	return g_date_days_between (gdate1, gdate2);
}

static int
datedif_opt_md (GDate *gdate1, GDate *gdate2, gboolean excel_compat)
{
	int day;

	g_assert (g_date_valid (gdate1));
	g_assert (g_date_valid (gdate2));

	day = g_date_get_day (gdate1);

	gnm_date_add_months (gdate1,
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
		   gnm_date_add_months */
		/* ( i feel this is inferior because it reports e.g.:
		     datedif(1/31/95,3/1/95,"d") == -2 ) */
		gnm_date_add_days (gdate1,
				   day - g_date_get_day (gdate1));
	}

	return g_date_days_between (gdate1, gdate2);
}

static GnmValue *
gnumeric_datedif (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int date1, date2;
	char const *opt;
	GDate d1, d2;
	GODateConventions const *conv = DATE_CONV (ei->pos);

	date1 = gnm_floor (value_get_as_float (argv [0]));
	date2 = gnm_floor (value_get_as_float (argv [1]));
	opt = value_peek_string (argv[2]);

	if (date1 > date2)
		return value_new_error_NUM (ei->pos);
	if (!strcmp (opt, "d"))
		return value_new_int (date2 - date1);

	datetime_serial_to_g (&d1, date1, conv);
	datetime_serial_to_g (&d2, date2, conv);
	if (!g_date_valid (&d1) || !g_date_valid (&d2))
		return value_new_error_VALUE (ei->pos);

	if (!strcmp (opt, "m"))
		return value_new_int (datetime_g_months_between (&d1, &d2));
	else if (!strcmp (opt, "y"))
		return value_new_int (datetime_g_years_between (&d1, &d2));
	else if (!strcmp (opt, "ym"))
		return value_new_int (datedif_opt_ym (&d1, &d2));
	else if (!strcmp (opt, "yd"))
		return value_new_int (datedif_opt_yd (&d1, &d2, TRUE));
	else if (!strcmp (opt, "md"))
		return value_new_int (datedif_opt_md (&d1, &d2, TRUE));
	else
		return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_edate[] = {
        { GNM_FUNC_HELP_NAME, F_("EDATE:adjust a date by a number of months") },
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
        { GNM_FUNC_HELP_ARG, F_("months:signed number of months")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("EDATE returns @{date} moved forward or backward the number of months specified by @{months}.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=EDATE(DATE(2001,12,30),2)" },
        { GNM_FUNC_HELP_SEEALSO, "DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_edate (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GODateConventions const *conv = DATE_CONV (ei->pos);
	gnm_float serial = value_get_as_float (argv[0]);
	gnm_float months = value_get_as_float (argv[1]);
	GDate date;

	if (serial < 0 || serial > INT_MAX)
                  return value_new_error_NUM (ei->pos);
	if (months > INT_MAX / 2 || -months > INT_MAX / 2)
                  return value_new_error_NUM (ei->pos);

	datetime_serial_to_g (&date, (int)serial, conv);
	gnm_date_add_months (&date, (int)months);

	if (!g_date_valid (&date) ||
	    g_date_get_year (&date) < 1900 ||
	    g_date_get_year (&date) > 9999)
		return value_new_error_NUM (ei->pos);

	return make_date (value_new_int (datetime_g_to_serial (&date, conv)));
}

/***************************************************************************/

static GnmFuncHelp const help_today[] = {
        { GNM_FUNC_HELP_NAME, F_("TODAY:return the date serial value of today") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The TODAY function returns the date serial value of the day it is computed.  Recomputing on a later date will produce a different value.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TODAY()" },
        { GNM_FUNC_HELP_SEEALSO, "DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_today (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return make_date (value_new_int (datetime_timet_to_serial (time (NULL), DATE_CONV (ei->pos))));
}

/***************************************************************************/

static GnmFuncHelp const help_now[] = {
        { GNM_FUNC_HELP_NAME, F_("NOW:return the date and time serial value of the current time.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The NOW function returns the date and time serial value of the moment it is computed.  Recomputing later will produce a different value.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NOW()" },
        { GNM_FUNC_HELP_SEEALSO, "DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_now (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (datetime_timet_to_serial_raw (time (NULL), DATE_CONV (ei->pos)));
}

/***************************************************************************/

static GnmFuncHelp const help_time[] = {
        { GNM_FUNC_HELP_NAME, F_("TIME:create a time serial value.")},
        { GNM_FUNC_HELP_ARG, F_("hour:hour of the day")},
        { GNM_FUNC_HELP_ARG, F_("minute:minute within the hour")},
        { GNM_FUNC_HELP_ARG, F_("second:second within the minute")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The TIME function computes the fractional day between midnight at the time given by @{hour}, @{minute}, and @{second}.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TIME(12,30,2)" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "HOUR,MINUTE,SECOND"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_time (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float hours, minutes, seconds;

	hours   = value_get_as_float (argv [0]);
	minutes = value_get_as_float (argv [1]);
	seconds = value_get_as_float (argv [2]);

	return make_date (value_new_float ((hours * 3600 + minutes * 60 + seconds) /
					   DAY_SECONDS));
}

/***************************************************************************/

static GnmFuncHelp const help_timevalue[] = {
        { GNM_FUNC_HELP_NAME, F_("TIMEVALUE:return time part of a date and time serial value.")},
        { GNM_FUNC_HELP_ARG, F_("serial:date and time serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("TIMEVALUE returns the time-of-day part of a date and time serial value.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TIMEVALUE(NOW())" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "DATEVALUE,TIME"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_timevalue (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float raw = datetime_value_to_serial_raw (argv[0], DATE_CONV (ei->pos));
	return value_new_float (raw - (int)raw);
}

/***************************************************************************/

static GnmFuncHelp const help_hour[] = {
        { GNM_FUNC_HELP_NAME, F_("HOUR:compute hour part of fractional day.")},
        { GNM_FUNC_HELP_ARG, F_("time:time of day as fractional day.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The HOUR function computes the hour part of the fractional day given by @{time}.") },
        { GNM_FUNC_HELP_EXAMPLES, "=HOUR(TIME(12,30,2))" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "TIME,MINUTE,SECOND"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hour (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int secs = datetime_value_to_seconds (argv[0], DATE_CONV (ei->pos));

	if (secs < 0)
		return value_new_error_NUM (ei->pos);
	else
		return value_new_int (secs / 3600);
}

/***************************************************************************/

static GnmFuncHelp const help_minute[] = {
        { GNM_FUNC_HELP_NAME, F_("MINUTE:compute minute part of fractional day.")},
        { GNM_FUNC_HELP_ARG, F_("time:time of day as fractional day.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The MINUTE function computes the minute part of the fractional day given by @{time}.") },
        { GNM_FUNC_HELP_EXAMPLES, "=MINUTE(TIME(12,30,2))" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "TIME,HOUR,SECOND"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_minute (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int secs = datetime_value_to_seconds (argv[0], DATE_CONV (ei->pos));

	if (secs < 0)
		return value_new_error_NUM (ei->pos);
	else
		return value_new_int (secs / 60 % 60);
}

/***************************************************************************/

static GnmFuncHelp const help_second[] = {
        { GNM_FUNC_HELP_NAME, F_("SECOND:compute seconds part of fractional day.")},
        { GNM_FUNC_HELP_ARG, F_("time:time of day as fractional day.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The SECOND function computes the seconds part of the fractional day given by @{time}.") },
        { GNM_FUNC_HELP_EXAMPLES, "=SECOND(TIME(12,30,2))" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "TIME,HOUR,MINUTE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_second (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int secs = datetime_value_to_seconds (argv[0], DATE_CONV (ei->pos));

	if (secs < 0)
		return value_new_error_NUM (ei->pos);
	else
		return value_new_int (secs % 60);
}

/***************************************************************************/

static GnmFuncHelp const help_year[] = {
        { GNM_FUNC_HELP_NAME, F_("YEAR:Return the year part of a date serial value.") },
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The YEAR function returns the year part of @{date}.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=YEAR(TODAY())" },
        { GNM_FUNC_HELP_EXAMPLES, "=YEAR(DATE(1940,4,9))" },
        { GNM_FUNC_HELP_SEEALSO, "DATE,MONTH,DAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_year (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;

	if (datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos)))
		return value_new_int (g_date_get_year (&date));
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_month[] = {
        { GNM_FUNC_HELP_NAME, F_("MONTH:Return the month part of a date serial value.") },
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The MONTH function returns the month part of @{date}.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=MONTH(TODAY())" },
        { GNM_FUNC_HELP_EXAMPLES, "=MONTH(DATE(1940,4,9))" },
        { GNM_FUNC_HELP_SEEALSO, "DATE,YEAR,DAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_month (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;

	if (datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos)))
		return value_new_int (g_date_get_month (&date));
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_day[] = {
        { GNM_FUNC_HELP_NAME, F_("DAY:Return the day-of-month part of a date serial value.") },
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The DAY function returns the day-of-month part of @{date}.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DAY(TODAY())" },
        { GNM_FUNC_HELP_EXAMPLES, "=DAY(DATE(1940,4,9))" },
        { GNM_FUNC_HELP_SEEALSO, "DATE,YEAR,MONTH"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_day (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;

	if (datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos)))
		return value_new_int (g_date_get_day (&date));
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_weekday[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=WEEKDAY\n"
	   "@SYNTAX=WEEKDAY (date[, method])\n"

	   "@DESCRIPTION="
	   "WEEKDAY converts a serial number to a weekday.\n"
	   "\n"
	   "This function returns an integer indicating the day of week.\n"
	   "@METHOD indicates the numbering system.  It defaults to 1.\n"
	   "\n"
	   "  For @METHOD=1: Sunday is 1, Monday is 2, etc.\n"
	   "  For @METHOD=2: Monday is 1, Tuesday is 2, etc.\n"
	   "  For @METHOD=3: Monday is 0, Tuesday is 1, etc.\n"
	   "\n"
	   "* Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "WEEKDAY(\"10/24/1968\") equals 5 (Thursday).\n"
	   "\n"
	   "@SEEALSO=DAY, MONTH, TIME, NOW, YEAR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_weekday (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	int res;
	gnm_float method = argv[1] ? value_get_as_float (argv[1]) : 1;

	if (method < 1 || method >= 4)
		return value_new_error_NUM (ei->pos);

	if (!datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos)))
		return value_new_error_NUM (ei->pos);

	switch ((int)method) {
	case 1: res = (g_date_get_weekday (&date) % 7) + 1; break;
	case 2: res = (g_date_get_weekday (&date) + 6) % 7 + 1; break;
	case 3: res = (g_date_get_weekday (&date) + 6) % 7; break;
	default:
		return value_new_error_NUM (ei->pos);
	}

	return value_new_int (res);
}

/***************************************************************************/

static GnmFuncHelp const help_days360[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DAYS360 \n"
	   "@SYNTAX=DAYS360 (date1,date2,method)\n"

	   "@DESCRIPTION="
	   "DAYS360 returns the number of days from @date1 to @date2 following "
	   "a 360-day calendar in which all months are assumed to have 30 days."
	   "\n\n"
	   "* If @method is 1, the European method will be used.  In this "
	   "case, if the day of the month is 31 it will be considered as 30."
	   "\n"
	   "* If @method is 0 or omitted, the MS Excel (tm) US method will be used.  "
	   "This is a somewhat complicated industry standard method "
	   "where the last day of February is considered to be the 30th day "
	   "of the month, but only for the first date."
	   "\n"
	   "* If @method is 2, a saner version of the US method is "
	   "used in which both dates get the same February treatment."
	   "\n"
	   "* Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "* This function is mostly Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DAYS360(DATE(2003, 2, 3), DATE(2007, 4, 2)) equals 1499.\n"
	   "\n"
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_days360 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	basis_t basis;
	GDate date1, date2;
	GODateConventions const *date_conv = DATE_CONV (ei->pos);
	gnm_float serial1 = datetime_value_to_serial (argv[0], date_conv);
	gnm_float serial2 = datetime_value_to_serial (argv[1], date_conv);
	gnm_float method = argv[2] ? gnm_floor (value_get_as_float (argv[2])) : 0;

	switch ((int)method) {
	case 0: basis = BASIS_MSRB_30_360; break;
	default:
	case 1: basis = BASIS_30E_360; break;
	case 2: basis = BASIS_MSRB_30_360_SYM; break;
	}

	datetime_serial_to_g (&date1, serial1, date_conv);
	datetime_serial_to_g (&date2, serial2, date_conv);
	if (!g_date_valid (&date1) || !g_date_valid (&date2))
		return value_new_error_VALUE (ei->pos);

	return value_new_int (days_between_basis (&date1, &date2, basis));
}

/***************************************************************************/

static GnmFuncHelp const help_eomonth[] = {
        { GNM_FUNC_HELP_NAME, F_("EOMONTH:calculate end of month") },
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
        { GNM_FUNC_HELP_ARG, F_("months:signed number of months")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("EOMONTH returns the date serial value of the end of the month specified by @{date} adjusted forward or backward the number of months specified by @{months}.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=EOMONTH(DATE(2001,12,14),2)" },
        { GNM_FUNC_HELP_SEEALSO, "EDATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_eomonth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float months = argv[1] ? value_get_as_float (argv[1]) : 0;
	GDate date;
	GODateConventions const *conv = DATE_CONV (ei->pos);

	datetime_value_to_g (&date, argv[0], conv);
	if (!g_date_valid (&date))
                  return value_new_error_VALUE (ei->pos);

	if (months > INT_MAX / 2 || -months > INT_MAX / 2)
                  return value_new_error_NUM (ei->pos);

	gnm_date_add_months (&date, (int)months);
	if (!g_date_valid (&date) ||
	    g_date_get_year (&date) < 1900 ||
	    g_date_get_year (&date) > 9999)
		return value_new_error_NUM (ei->pos);

	g_date_set_day (&date,
			g_date_get_days_in_month (g_date_get_month (&date),
						  g_date_get_year (&date)));

	return make_date (value_new_int (datetime_g_to_serial (&date, conv)));
}

/***************************************************************************/

static GnmFuncHelp const help_workday[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=WORKDAY\n"
	   "@SYNTAX=WORKDAY (start_date,days[,holidays])\n"

	   "@DESCRIPTION="
	   "WORKDAY returns the date which is @days working days "
	   "from the @start_date.  Weekends and holidays optionally "
	   "supplied in @holidays are respected.\n"
	   "\n"
	   "* WORKDAY returns #NUM! if @start_date or @days are invalid.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DAY(WORKDAY(DATE(2001,1,5),30)) equals 16 and\n"
	   "MONTH(WORKDAY(DATE(2001,1,5),30)) equals 2.\n"
	   "\n"
	   "@SEEALSO=NETWORKDAYS")
	},
	{ GNM_FUNC_HELP_END }
};

static gint
float_compare (gnm_float const *a, gnm_float const *b)
{
        if (*a < *b)
                return -1;
	else if (*a == *b)
		return 0;
	else
		return 1;
}

static GnmValue *
gnumeric_workday (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	GODateConventions const *conv = DATE_CONV (ei->pos);
	gnm_float days = value_get_as_float (argv[1]);
	int idays;
	gnm_float *holidays = NULL;
	int nholidays;
	GDateWeekday weekday;
	int serial;

	datetime_value_to_g (&date, argv[0], conv);
	if (!g_date_valid (&date))
		goto bad;

	if (argv[2]) {
		int i, j;
		GDate hol;
		GnmValue *result = NULL;

		holidays = collect_floats_value (argv[2], ei->pos,
						 COLLECT_COERCE_STRINGS |
						 COLLECT_IGNORE_BOOLS |
						 COLLECT_IGNORE_BLANKS,
						 &nholidays, &result);
		if (result)
			return result;
		qsort (holidays, nholidays, sizeof (holidays[0]), (void *) &float_compare);

		for (i = j = 0; i < nholidays; i++) {
			gnm_float s = holidays[i];
			int hserial;
			if (s < 0 || s > INT_MAX)
				goto bad;
			hserial = (int)s;
			if (j > 0 && hserial == holidays[j - 1])
				continue;  /* Dupe */
			datetime_serial_to_g (&hol, hserial, conv);
			if (!g_date_valid (&hol))
				goto bad;
			if (g_date_get_weekday (&hol) >= G_DATE_SATURDAY)
				continue;
			holidays[j++] = hserial;
		}
		nholidays = j;
	} else {
		holidays = NULL;
		nholidays = 0;
	}

	if (days > INT_MAX / 2 || -days > INT_MAX / 2)
		return value_new_error_NUM (ei->pos);
	idays = (int)days;

	weekday = g_date_get_weekday (&date);
	serial = datetime_g_to_serial (&date, conv);

	if (idays > 0) {
		int h = 0;

		if (weekday >= G_DATE_SATURDAY) {
			serial -= (weekday - G_DATE_FRIDAY);
			weekday = G_DATE_FRIDAY;
		}

		while (idays > 0) {
			int dm5 = idays % 5;
			int ds = idays / 5 * 7 + dm5;

			weekday += dm5;
			if (weekday >= G_DATE_SATURDAY) {
				ds += 2;
				weekday -= 5;
			}

			/*
			 * "ds" is now the number of calendar days to advance
			 * but we may be passing holiday.
			 */
			idays = 0;
			while (h < nholidays && holidays[h] <= serial + ds) {
				if (holidays[h] > serial)
					idays++;
				h++;
			}

			serial += ds;
		}
	} else if (idays < 0) {
		int h = nholidays - 1;

		if (weekday >= G_DATE_SATURDAY) {
			serial += (G_DATE_SUNDAY - weekday + 1);
			weekday = G_DATE_MONDAY;
		}

		idays = -idays;
		while (idays > 0) {
			int dm5 = idays % 5;
			int ds = idays / 5 * 7 + dm5;

			weekday -= dm5;
			if ((int)weekday < (int)G_DATE_MONDAY) {
				ds += 2;
				weekday += 5;
			}

			/*
			 * "ds" is now the number of calendar days to retreat
			 * but we may be passing holiday.
			 */
			idays = 0;
			while (h >= 0 && holidays[h] >= serial + ds) {
				if (holidays[h] < serial)
					idays++;
				h--;
			}

			serial -= ds;
		}
	}

	g_free (holidays);

	datetime_serial_to_g (&date, serial, conv);
	if (!g_date_valid (&date) ||
	    g_date_get_year (&date) < 1900 ||
	    g_date_get_year (&date) > 9999)
		return value_new_error_NUM (ei->pos);

	return make_date (value_new_int (datetime_g_to_serial (&date, conv)));

 bad:
	g_free (holidays);
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_networkdays[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NETWORKDAYS\n"
	   "@SYNTAX=NETWORKDAYS (start_date,end_date[,holidays])\n"

	   "@DESCRIPTION="
	   "NETWORKDAYS returns the number of non-weekend non-holidays between "
	   "@start_date and @end_date including these dates. "
	   "Holidays are optionally supplied in @holidays.\n"
	   "\n"
	   "* NETWORKDAYS returns #NUM! if @start_date or @end_date are "
	   "invalid.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "NETWORKDAYS(DATE(2001,1,2),DATE(2001,2,15)) equals 33.\n"
	   "\n"
	   "@SEEALSO=WORKDAY")
	},
	{ GNM_FUNC_HELP_END }
};

/*
 * A utility routine to return the last monday <= the serial if it's valid
 * Returns -1 on error
 */
static int
get_serial_weekday (int serial, int *offset, GODateConventions const *conv)
{
	GDate date;

	if (serial <= 0)
		return serial;
	datetime_serial_to_g (&date, serial, conv);
        if (g_date_valid (&date)) {
		/* Jan 1 1900 was a monday so we won't go < 0 */
		*offset = (int)g_date_get_weekday (&date) - 1;
		serial -= *offset;
		if (*offset > 4)
			*offset = 4;
	} else
		serial = -1;
	return serial;
}

typedef struct {
	int start_serial, end_serial;
	int res;
} networkdays_holiday_closure;

static GnmValue *
cb_networkdays_holiday (GnmValueIter const *v_iter,
			networkdays_holiday_closure *cls)
{
	int serial;
	GDate date;
	GODateConventions const *conv = DATE_CONV (v_iter->ep);

	if (VALUE_IS_ERROR (v_iter->v))
		return value_dup (v_iter->v);
	serial = datetime_value_to_serial (v_iter->v, conv);
        if (serial <= 0)
		return value_new_error_NUM (v_iter->ep);

	if (serial < cls->start_serial || cls->end_serial < serial)
		return NULL;

	datetime_serial_to_g (&date, serial, conv);
        if (!g_date_valid (&date))
		return value_new_error_NUM (v_iter->ep);
	if (g_date_get_weekday (&date) < G_DATE_SATURDAY)
		++cls->res;
	return NULL;
}

static GnmValue *
gnumeric_networkdays (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int start_serial;
	int end_serial;
	int start_offset, end_offset, res;
	networkdays_holiday_closure cls;
	GDate start_date;
	GODateConventions const *conv = DATE_CONV (ei->pos);

	start_serial = datetime_value_to_serial (argv[0], conv);
	end_serial = datetime_value_to_serial (argv[1], conv);

	/* Swap if necessary */
	if (start_serial > end_serial) {
		int tmp = start_serial;
		start_serial = end_serial;
		end_serial = tmp;
	}

	datetime_serial_to_g (&start_date, start_serial, DATE_CONV (ei->pos));
	cls.start_serial = start_serial;
	cls.end_serial = end_serial;
	cls.res = 0;

	/* Move to mondays, and check for problems */
	start_serial = get_serial_weekday (start_serial, &start_offset, conv);
	end_serial = get_serial_weekday (end_serial, &end_offset, conv);
	if (!g_date_valid (&start_date) || start_serial <= 0 || end_serial <= 0)
                  return value_new_error_NUM (ei->pos);

	res = end_serial - start_serial;
	res -= ((res/7)*2);	/* Remove weekends */

	if (argv[2] != NULL) {
		GnmValue *e =
			value_area_foreach (argv[2], ei->pos,
					    CELL_ITER_IGNORE_BLANK,
					    (GnmValueIterFunc)&cb_networkdays_holiday,
					    &cls);
		if (e)
			return e;
	}

	res = res - start_offset + end_offset - cls.res;

	if (g_date_get_weekday (&start_date) < G_DATE_SATURDAY)
		res++;

	return value_new_int (res);
}

/***************************************************************************/

static GnmFuncHelp const help_isoweeknum[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISOWEEKNUM\n"
	   "@SYNTAX=ISOWEEKNUM (date)\n"

	   "@DESCRIPTION="
	   "ISOWEEKNUM returns the ISO 8601 week number of @date.\n"
	   "\n"
	   "An ISO 8601 week starts on Monday. Weeks are numbered from 1. A "
	   "week including days from two different years is assigned to the "
	   "year which includes the most days. This means that Dec 31 could "
	   "be in week 1 of the following year, and Jan 1 could be in week 52 "
	   "or 53 of the previous year. ISOWEEKNUM returns the week number.\n"
	   "\n"
	   "* ISOWEEKNUM returns #NUM! if date is invalid.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 12/21/00 then ISOWEEKNUM(A1)=51"
	   "\n"
	   "@SEEALSO=WEEKNUM, ISOYEAR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isoweeknum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos));
	if (!g_date_valid (&date))
                  return value_new_error_VALUE (ei->pos);

	return value_new_int (datetime_weeknum (&date, WEEKNUM_METHOD_ISO));
}

/***************************************************************************/

static GnmFuncHelp const help_isoyear[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISOYEAR\n"
	   "@SYNTAX=ISOYEAR (date)\n"

	   "@DESCRIPTION="
	   "ISOYEAR returns the year of the ISO 8601 week number of @date."
	   "\n\n"
	   "An ISO 8601 week starts on Monday. Weeks are numbered from 1. A "
	   "week including days from two different years is assigned to the "
	   "year which includes the most days. This means that Dec 31 could "
	   "be in week 1 of the following year, and Jan 1 could be in week 52 "
	   "or 53 of the previous year. ISOYEAR returns the year the week is "
	   "assigned to.\n"
	   "\n"
	   "* ISOYEAR returns #NUM! if date is invalid."
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 12/31/2001 then ISOYEAR(A1)=2002"
	   "\n"
	   "@SEEALSO=ISOWEEKNUM")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isoyear (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	int year;
	int month;
	int isoweeknum;

	datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos));
	if (!g_date_valid (&date))
		return value_new_error_VALUE (ei->pos);

	isoweeknum = datetime_weeknum (&date, WEEKNUM_METHOD_ISO);
	year = g_date_get_year (&date);
	month = g_date_get_month (&date);
	if (isoweeknum >= 52 && month == G_DATE_JANUARY)
		year--;
	else if (isoweeknum == 1 && month == G_DATE_DECEMBER)
		year++;

	return value_new_int (year);
}

/***************************************************************************/

static GnmFuncHelp const help_weeknum[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=WEEKNUM\n"
	   "@SYNTAX=WEEKNUM (date[,method])\n"

	   "@DESCRIPTION="
	   "WEEKNUM returns the week number of @date according to the given "
	   "@method.\n"
	   "\n"
	   "@method defaults to 1.\n\n"
	   "  For @method=1, week starts on Sunday, and days before first "
	   "Sunday are in week 0.\n"
	   "  For @method=2, week starts on Monday, and days before first "
	   "Monday are in week 0.\n"
	   "  For @method=150, the ISO 8601 week number is returned.\n"
	   "\n"
	   "* WEEKNUM returns #NUM! if @date or @method is invalid.\n"
	   "* This function is Excel compatible, except that Excel does not "
	   "support ISO 8601 week numbers.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 12/21/00 then WEEKNUM(A1,2)=51"
	   "\n"
	   "@SEEALSO=ISOWEEKNUM")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_weeknum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	gnm_float method = argv[1] ? gnm_floor (value_get_as_float (argv[1])) : 1;

	if (!(method == WEEKNUM_METHOD_SUNDAY ||
	      method == WEEKNUM_METHOD_MONDAY ||
	      method == WEEKNUM_METHOD_ISO))
		return value_new_error_VALUE (ei->pos);

	datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos));
	if (!g_date_valid (&date))
                  return value_new_error_VALUE (ei->pos);

	return value_new_int (datetime_weeknum (&date, (int)method));
}

/***************************************************************************/

static GnmFuncHelp const help_yearfrac[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=YEARFRAC\n"
	   "@SYNTAX=YEARFRAC (start_date, end_date [,basis])\n"

	   "@DESCRIPTION="
	   "YEARFRAC returns the number of full days between @start_date and "
	   "@end_date according to the @basis.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=DATEDIF")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_yearfrac (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GODateConventions const *conv = DATE_CONV (ei->pos);
	GDate start_date, end_date;
	int basis = value_get_basis (argv[2], BASIS_MSRB_30_360);

	if (basis < 0 || basis > 4 ||
	    !datetime_value_to_g (&start_date, argv[0], conv) ||
	    !datetime_value_to_g (&end_date, argv[1], conv))
		return value_new_error_NUM (ei->pos);

	return value_new_float (yearfrac (&start_date, &end_date, basis));
}

/***************************************************************************/

GnmFuncDescriptor const datetime_functions[] = {
	{ "date",        "fff",  N_("year,month,day"), help_date,
	  gnumeric_date, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "datevalue",   "f",    N_("date_str"), help_datevalue,
	  gnumeric_datevalue, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "datedif",     "ffs",  N_("date1,date2,interval"), help_datedif,
	  gnumeric_datedif, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "day",         "f",    N_("date"), help_day,
	  gnumeric_day, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "days360",     "ff|f", N_("date1,date2,method"), help_days360,
	  gnumeric_days360, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "edate",       "ff",   N_("date,months"), help_edate,
	  gnumeric_edate, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "eomonth",     "f|f",  N_("start_date,months"), help_eomonth,
	  gnumeric_eomonth, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hour",        "f",    N_("time"), help_hour,
	  gnumeric_hour, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "minute",      "f",    N_("time"), help_minute,
	  gnumeric_minute, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "month",       "f",    N_("date"), help_month,
	  gnumeric_month, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "networkdays", "ff|?", N_("start_date,end_date,holidays"),
	  help_networkdays, gnumeric_networkdays, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "now",         "",     "", help_now,
	  gnumeric_now, NULL, NULL, NULL, NULL,
	  GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_TIME,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "second",      "f",    N_("time"), help_second,
	  gnumeric_second, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "time",        "fff",  N_("hours,minutes,seconds"), help_time,
	  gnumeric_time, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_TIME,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "timevalue",   "f",    N_("timetext"), help_timevalue,
	  gnumeric_timevalue, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "today",       "",     "", help_today,
	  gnumeric_today, NULL, NULL, NULL, NULL,
	  GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "weekday",     "f|f",  N_("date"), help_weekday,
	  gnumeric_weekday, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "weeknum",     "f|f",  N_("date"), help_weeknum,
	  gnumeric_weeknum, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "workday",     "ff|?", N_("date,days,holidays"), help_workday,
	  gnumeric_workday, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_BASIC },
	{ "year",        "f",    N_("date"), help_year,
	  gnumeric_year, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yearfrac",	"ff|f",    N_("date"), help_yearfrac,
	  gnumeric_yearfrac, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "unix2date",   "f",    N_("unixtime"), help_unix2date,
	  gnumeric_unix2date, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "date2unix",   "f",    N_("serial"), help_date2unix,
	  gnumeric_date2unix, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "isoweeknum",  "f",    N_("date"), help_isoweeknum,
	  gnumeric_isoweeknum, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "isoyear",     "f",    N_("date"), help_isoyear,
	  gnumeric_isoyear, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        {NULL}
};
