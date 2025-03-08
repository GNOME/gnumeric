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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
#define DATE_CONV(ep)		sheet_date_conv ((ep)->sheet)

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
        { GNM_FUNC_HELP_NAME, F_("DATE:create a date serial value")},
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
	if (!gnm_datetime_allow_negative () && year < 1900)
		year += 1900; /* Excel compatibility.  */
	else if (year < 1000)
		year += 1900; /* Somewhat more sane.  */

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
	    g_date_get_year (&date) < (gnm_datetime_allow_negative ()
				       ? 1582
				       : go_date_convention_base (conv)) ||
	    g_date_get_year (&date) >= 11900)
		goto error;

	return make_date (value_new_int (go_date_g_to_serial (&date, conv)));

 error:
	return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_unix2date[] = {
        { GNM_FUNC_HELP_NAME, F_("UNIX2DATE:date value corresponding to the Unix timestamp @{t}")},
        { GNM_FUNC_HELP_ARG, F_("t:Unix time stamp")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The UNIX2DATE function translates Unix timestamps into the corresponding date.  A Unix timestamp is the number of seconds since midnight (0:00) of January 1st, 1970 GMT.") },
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
	if (gnm_abs (futime - utime) >= 1)
		return value_new_error_VALUE (ei->pos);

	serial = go_date_timet_to_serial_raw (utime, DATE_CONV (ei->pos));
	if (serial == G_MAXINT)
		return value_new_error_VALUE (ei->pos);

	return make_date (value_new_float (serial +
					   (futime - utime) / DAY_SECONDS));
}

/***************************************************************************/

static GnmFuncHelp const help_date2unix[] = {
        { GNM_FUNC_HELP_NAME, F_("DATE2UNIX:the Unix timestamp corresponding to a date @{d}") },
        { GNM_FUNC_HELP_ARG, F_("d:date")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The DATE2UNIX function translates a date into a Unix timestamp. A Unix timestamp is the number of seconds since midnight (0:00) of January 1st, 1970 GMT.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2UNIX(DATE(2000,1,1))" },
        { GNM_FUNC_HELP_SEEALSO, "UNIX2DATE,DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_date2unix (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float fserial = value_get_as_float (argv [0]);
	int        serial  = (int)fserial;
	time_t     utime   = go_date_serial_to_timet (serial, DATE_CONV (ei->pos));

	/* Check for overflow.  */
	if (gnm_abs (fserial - serial) >= 1 || utime == (time_t)-1)
		return value_new_error_VALUE (ei->pos);

	return value_new_int (utime +
		gnm_fake_round (DAY_SECONDS * (fserial - serial)));
}

/***************************************************************************/

static GnmFuncHelp const help_datevalue[] = {
        { GNM_FUNC_HELP_NAME, F_("DATEVALUE:the date part of a date and time serial value")},
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
        { GNM_FUNC_HELP_NAME, F_("DATEDIF:difference between dates") },
        { GNM_FUNC_HELP_ARG, F_("start_date:starting date serial value")},
        { GNM_FUNC_HELP_ARG, F_("end_date:ending date serial value")},
        { GNM_FUNC_HELP_ARG, F_("interval:counting unit")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DATEDIF returns the distance from @{start_date} to @{end_date} according to the unit specified by @{interval}.  This function exists primarly for compatibility reasons and the semantics is somewhat peculiar.  In particular, the concept of \"whole month\" is based on whether the ending day is larger than or equal to the beginning day and not on whether a whole calendar month in contained in the interval.  A similar treatment is given to \"whole year\".") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{interval} is \"y\", \"m\", or \"d\" then the distance is measured in whole years, months, or days respectively.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{interval} is \"ym\" or \"yd\" then the distance is measured in whole months or days, respectively, but excluding any difference covered by whole years.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{interval} is \"md\" then the distance is measured in days but excluding any difference covered by whole months.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DATEDIF(DATE(2003,2,3),DATE(2007,4,2),\"m\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=DATEDIF(DATE(2000,4,30),DATE(2003,8,4),\"d\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=DATEDIF(DATE(2000,4,30),DATE(2003,8,4),\"y\")" },
        { GNM_FUNC_HELP_SEEALSO, "DAYS360"},
	{ GNM_FUNC_HELP_END }
};

static int
datedif_opt_ym (GDate *gdate1, GDate *gdate2)
{
	g_assert (g_date_valid (gdate1));
	g_assert (g_date_valid (gdate2));

	return go_date_g_months_between (gdate1, gdate2) % 12;
}

static int
datedif_opt_yd (GDate *gdate1, GDate *gdate2, int excel_compat)
{
	g_assert (g_date_valid (gdate1));
	g_assert (g_date_valid (gdate2));

	g_date_get_day (gdate1);

	gnm_date_add_years (gdate1,
			    go_date_g_years_between (gdate1, gdate2));
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
			     go_date_g_months_between (gdate1, gdate2));
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

	go_date_serial_to_g (&d1, date1, conv);
	go_date_serial_to_g (&d2, date2, conv);
	if (!g_date_valid (&d1) || !g_date_valid (&d2))
		return value_new_error_VALUE (ei->pos);

	if (!strcmp (opt, "d"))
		return value_new_int (g_date_get_julian (&d2) -
				      g_date_get_julian (&d1));
	else if (!strcmp (opt, "m"))
		return value_new_int (go_date_g_months_between (&d1, &d2));
	else if (!strcmp (opt, "y"))
		return value_new_int (go_date_g_years_between (&d1, &d2));
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

	go_date_serial_to_g (&date, (int)serial, conv);
	gnm_date_add_months (&date, (int)months);

	if (!g_date_valid (&date) ||
	    g_date_get_year (&date) < 1900 ||
	    g_date_get_year (&date) > 9999)
		return value_new_error_NUM (ei->pos);

	return make_date (value_new_int (go_date_g_to_serial (&date, conv)));
}

/***************************************************************************/

static GnmFuncHelp const help_today[] = {
        { GNM_FUNC_HELP_NAME, F_("TODAY:the date serial value of today") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The TODAY function returns the date serial value of the day it is computed.  Recomputing on a later date will produce a different value.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TODAY()" },
        { GNM_FUNC_HELP_SEEALSO, "DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_today (GnmFuncEvalInfo *ei, G_GNUC_UNUSED GnmValue const * const *argv)
{
	return make_date (value_new_int (go_date_timet_to_serial (time (NULL), DATE_CONV (ei->pos))));
}

/***************************************************************************/

static GnmFuncHelp const help_now[] = {
        { GNM_FUNC_HELP_NAME, F_("NOW:the date and time serial value of the current time") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The NOW function returns the date and time serial value of the moment it is computed.  Recomputing later will produce a different value.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NOW()" },
        { GNM_FUNC_HELP_SEEALSO, "DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_now (GnmFuncEvalInfo *ei, G_GNUC_UNUSED GnmValue const * const *argv)
{
	guint64 t = g_get_real_time ();
	gnm_float r = go_date_timet_to_serial_raw (t / 1000000, DATE_CONV (ei->pos));
	r += (t % 1000000) / (24 * 60 * 60 * GNM_const(1000000.0));
	return value_new_float (r);
}

/***************************************************************************/

static GnmFuncHelp const help_time[] = {
        { GNM_FUNC_HELP_NAME, F_("TIME:create a time serial value")},
        { GNM_FUNC_HELP_ARG, F_("hour:hour of the day")},
        { GNM_FUNC_HELP_ARG, F_("minute:minute within the hour")},
        { GNM_FUNC_HELP_ARG, F_("second:second within the minute")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The TIME function computes the fractional day after midnight at the time given by @{hour}, @{minute}, and @{second}.") },
	{ GNM_FUNC_HELP_NOTE, F_("While the return value is automatically formatted to look like a time between 0:00 and 24:00, "
				 "the underlying serial time value is a number between 0 and 1.")},
 	{ GNM_FUNC_HELP_NOTE, F_("If any of @{hour}, @{minute}, and @{second} is negative, #NUM! is returned")},
        { GNM_FUNC_HELP_EXAMPLES, "=TIME(12,30,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=TIME(25,100,18)" },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "ODF.TIME,HOUR,MINUTE,SECOND"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_time (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float hours, minutes, seconds;
	gnm_float time;

	hours   = gnm_fmod (value_get_as_float (argv [0]), 24);
	minutes = value_get_as_float (argv [1]);
	seconds = value_get_as_float (argv [2]);

	if (hours < 0 || minutes < 0 || seconds < 0)
		return value_new_error_NUM (ei->pos);

	time = (hours * 3600 + minutes * 60 + seconds) / DAY_SECONDS;
	time -= gnm_fake_floor (time);

	return value_new_float (time);
}

/***************************************************************************/

static GnmFuncHelp const help_odf_time[] = {
        { GNM_FUNC_HELP_NAME, F_("ODF.TIME:create a time serial value")},
        { GNM_FUNC_HELP_ARG, F_("hour:hour")},
        { GNM_FUNC_HELP_ARG, F_("minute:minute")},
        { GNM_FUNC_HELP_ARG, F_("second:second")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The ODF.TIME function computes the time given by @{hour}, @{minute}, and @{second} as a fraction of a day.") },
	{ GNM_FUNC_HELP_NOTE, F_("While the return value is automatically formatted to look like a time between 0:00 and 24:00, "
				 "the underlying serial time value can be any number.")},
        { GNM_FUNC_HELP_EXAMPLES, "=ODF.TIME(12,30,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ODF.TIME(25,100,-18)" },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_SEEALSO, "TIME,HOUR,MINUTE,SECOND"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_odf_time (G_GNUC_UNUSED GnmFuncEvalInfo *ei, GnmValue const * const *argv)
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
        { GNM_FUNC_HELP_NAME, F_("TIMEVALUE:the time part of a date and time serial value")},
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
        { GNM_FUNC_HELP_NAME, F_("HOUR:compute hour part of fractional day")},
        { GNM_FUNC_HELP_ARG, F_("time:time of day as fractional day")},
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
        { GNM_FUNC_HELP_NAME, F_("MINUTE:compute minute part of fractional day")},
        { GNM_FUNC_HELP_ARG, F_("time:time of day as fractional day")},
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
        { GNM_FUNC_HELP_NAME, F_("SECOND:compute seconds part of fractional day")},
        { GNM_FUNC_HELP_ARG, F_("time:time of day as fractional day")},
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
        { GNM_FUNC_HELP_NAME, F_("YEAR:the year part of a date serial value") },
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
        { GNM_FUNC_HELP_NAME, F_("MONTH:the month part of a date serial value") },
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
        { GNM_FUNC_HELP_NAME, F_("DAY:the day-of-month part of a date serial value") },
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
        { GNM_FUNC_HELP_NAME, F_("WEEKDAY:day-of-week") },
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
        { GNM_FUNC_HELP_ARG, F_("method:numbering system, defaults to 1")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The WEEKDAY function returns the day-of-week of @{date}.  The value of @{method} determines how days are numbered; it defaults to 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 1, then Sunday is 1, Monday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 2, then Monday is 1, Tuesday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 3, then Monday is 0, Tuesday is 1, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 11, then Monday is 1, Tuesday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 12, then Tuesday is 1, Wednesday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 13, then Wednesday is 1, Thursday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 14, then Thursday is 1, Friday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 15, then Friday is 1, Saturday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 16, then Saturday is 1, Sunday is 2, etc.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 17, then Sunday is 1, Monday is 2, etc.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=WEEKDAY(DATE(1940,4,9))" },
        { GNM_FUNC_HELP_SEEALSO, "DATE,ISOWEEKNUM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_weekday (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	int res;
	gnm_float method = argv[1] ? value_get_as_float (argv[1]) : 1;

	if (method < 1 || method >= G_MAXINT)
		return value_new_error_NUM (ei->pos);

	if (!datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos)))
		return value_new_error_NUM (ei->pos);

	switch ((int)method) {
	case 1:
	case 17:
		res = (g_date_get_weekday (&date) % 7) + 1;
		break;
	case 2:
	case 11:
		res = (g_date_get_weekday (&date) + 6) % 7 + 1;
		break;
	case 3:
		res = (g_date_get_weekday (&date) + 6) % 7;
		break;
	case 12:
		res = (g_date_get_weekday (&date) + 5) % 7 + 1;
		break;
	case 13:
		res = (g_date_get_weekday (&date) + 4) % 7 + 1;
		break;
	case 14:
		res = (g_date_get_weekday (&date) + 3) % 7 + 1;
		break;
	case 15:
		res = (g_date_get_weekday (&date) + 2) % 7 + 1;
		break;
	case 16:
		res = (g_date_get_weekday (&date) + 1) % 7 + 1;
		break;
	default:
		return value_new_error_NUM (ei->pos);
	}

	return value_new_int (res);
}

/***************************************************************************/

static GnmFuncHelp const help_days360[] = {
        { GNM_FUNC_HELP_NAME, F_("DAYS360:days between dates") },
        { GNM_FUNC_HELP_ARG, F_("start_date:starting date serial value")},
        { GNM_FUNC_HELP_ARG, F_("end_date:ending date serial value")},
        { GNM_FUNC_HELP_ARG, F_("method:counting method")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DAYS360 returns the number of days from @{start_date} to @{end_date}.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 0, the default, the MS Excel (tm) US method will be used. This is a somewhat complicated industry standard method where the last day of February is considered to be the 30th day of the month, but only for @{start_date}.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 1, the European method will be used.  In this case, if the day of the month is 31 it will be considered as 30") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 2, a saner version of the US method is used in which both dates get the same February treatment.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DAYS360(DATE(2003,2,3),DATE(2007,4,2))" },
        { GNM_FUNC_HELP_SEEALSO, "DATEDIF"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_days360 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	go_basis_t basis;
	GDate date1, date2;
	GODateConventions const *date_conv = DATE_CONV (ei->pos);
	gnm_float serial1 = datetime_value_to_serial (argv[0], date_conv);
	gnm_float serial2 = datetime_value_to_serial (argv[1], date_conv);
	gnm_float method = argv[2] ? gnm_floor (value_get_as_float (argv[2])) : 0;

	switch ((int)method) {
	case 0: basis = GO_BASIS_MSRB_30_360; break;
	default:
	case 1: basis = GO_BASIS_30E_360; break;
	case 2: basis = GO_BASIS_MSRB_30_360_SYM; break;
	}

	go_date_serial_to_g (&date1, serial1, date_conv);
	go_date_serial_to_g (&date2, serial2, date_conv);
	if (!g_date_valid (&date1) || !g_date_valid (&date2))
		return value_new_error_VALUE (ei->pos);

	return value_new_int (go_date_days_between_basis (&date1, &date2, basis));
}

/***************************************************************************/

static GnmFuncHelp const help_eomonth[] = {
        { GNM_FUNC_HELP_NAME, F_("EOMONTH:end of month") },
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

	return make_date (value_new_int (go_date_g_to_serial (&date, conv)));
}

/***************************************************************************/

static GnmFuncHelp const help_workday[] = {
        { GNM_FUNC_HELP_NAME, F_("WORKDAY:add working days") },
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
        { GNM_FUNC_HELP_ARG, F_("days:number of days to add")},
        { GNM_FUNC_HELP_ARG, F_("holidays:array of holidays")},
        { GNM_FUNC_HELP_ARG, F_("weekend:array of 0s and 1s, indicating whether a weekday "
				"(S, M, T, W, T, F, S) is on the weekend, defaults to {1,0,0,0,0,0,1}")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("WORKDAY adjusts @{date} by @{days} skipping over weekends and @{holidays} in the process.") },
	{ GNM_FUNC_HELP_NOTE, F_("@{days} may be negative.") },
	{ GNM_FUNC_HELP_NOTE, F_("If an entry of @{weekend} is non-zero, the corresponding weekday is not a work day.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible if the last argument is omitted.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=WORKDAY(DATE(2001,12,14),2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=WORKDAY(DATE(2001,12,14),2,,{0,0,0,0,0,1,1})" },
        { GNM_FUNC_HELP_SEEALSO, "NETWORKDAYS"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_workday (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	GODateConventions const *conv = DATE_CONV (ei->pos);
	gnm_float days = value_get_as_float (argv[1]);
	int idays;
	gnm_float *holidays = NULL;
	gnm_float *weekends = NULL;
	gnm_float const default_weekends[] = {1.,0.,0.,0.,0.,0.,1.};
	int nholidays, nweekends, n_non_weekend = 0;
	GDateWeekday weekday;
	int serial = 0;
	int i;

	datetime_value_to_g (&date, argv[0], conv);
	if (!g_date_valid (&date))
		goto bad;

	if (days > INT_MAX / 2 || -days > INT_MAX / 2)
		return value_new_error_NUM (ei->pos);
	idays = (int)days;

	if (argv[3]) {
		GnmValue *result = NULL;

		weekends = collect_floats_value (argv[3], ei->pos,
						 COLLECT_COERCE_STRINGS |
						 COLLECT_ZEROONE_BOOLS |
						 COLLECT_IGNORE_BLANKS,
						 &nweekends, &result);
		if (result)
			return result;
		if (nweekends != 7)
			goto bad;

	} else {
		weekends = (gnm_float *)default_weekends;
		nweekends = 7;
	}

	for (i = 0; i < 7; i++)
		if (weekends[i] == 0)
			n_non_weekend++;
	if (n_non_weekend == 0 && idays != 0)
		goto bad;
	if (n_non_weekend == 0 && idays == 0) {
		if (weekends != default_weekends)
			g_free (weekends);
		return make_date (value_new_int (go_date_g_to_serial (&date, conv)));
	}

	if (argv[2]) {
		int j;
		GDate hol;
		GnmValue *result = NULL;

		holidays = collect_floats_value (argv[2], ei->pos,
						 COLLECT_COERCE_STRINGS |
						 COLLECT_IGNORE_BOOLS |
						 COLLECT_IGNORE_BLANKS |
						 COLLECT_SORT,
						 &nholidays, &result);
		if (result) {
			if (weekends != default_weekends)
				g_free (weekends);
			return result;
		}

		for (i = j = 0; i < nholidays; i++) {
			gnm_float s = holidays[i];
			int hserial;
			if (s < 0 || s > INT_MAX)
				goto bad;
			hserial = (int)s;
			if (j > 0 && hserial == holidays[j - 1])
				continue;  /* Dupe */
			go_date_serial_to_g (&hol, hserial, conv);
			if (!g_date_valid (&hol))
				goto bad;
			if (weekends[g_date_get_weekday (&hol) % 7] != 0)
				continue;
			holidays[j++] = hserial;
		}
		nholidays = j;
	} else {
		holidays = NULL;
		nholidays = 0;
	}


	weekday = g_date_get_weekday (&date);

	if (idays > 0) {
		int h = 0;
		guint diff = 0;
		int old_serial;

		weekday = weekday % 7;
		while (weekends[weekday]) {
			weekday = (weekday > 0) ? (weekday - 1) : G_DATE_SATURDAY;
			diff++;
		}
		g_date_subtract_days (&date, diff);
		old_serial = go_date_g_to_serial (&date, conv);

		while (idays > 0) {
			int dm_part_week = idays % n_non_weekend;
			int ds = idays / n_non_weekend * 7;

			g_date_add_days (&date, ds);

			while (dm_part_week) {
				g_date_add_days (&date, 1);
				weekday = (weekday + 1) % 7;
				if (!weekends[weekday])
					dm_part_week--;
			}

			serial = go_date_g_to_serial (&date, conv);
			/*
			 * we may have passed holidays.
			 */
			idays = 0;
			while (h < nholidays && holidays[h] <= serial) {
				if (holidays[h] > old_serial)
					idays++;
				h++;
			}
			old_serial = serial;
		}
	} else if (idays < 0) {
		int h = nholidays - 1;
		guint diff = 0;
		int old_serial;

		weekday = weekday % 7;
		while (weekends[weekday]) {
			weekday = (weekday + 1) % 7;
			diff++;
		}
		g_date_add_days (&date, diff);
		old_serial = go_date_g_to_serial (&date, conv);

		idays = -idays;
		while (idays > 0) {
			int dm_part_week = idays % n_non_weekend;
			int ds = idays / n_non_weekend * 7;

			g_date_subtract_days (&date, ds);

			while (dm_part_week) {
				g_date_subtract_days (&date, 1);
				weekday = (weekday > 0) ? (weekday - 1)
					: G_DATE_SATURDAY;
				if (!weekends[weekday])
					dm_part_week--;
			}

			serial = go_date_g_to_serial (&date, conv);
			/*
			 * we may have passed holidays.
			 */
			idays = 0;
			while (h >= 0 && holidays[h] >= serial) {
				if (holidays[h] < old_serial)
					idays++;
				h--;
			}
			old_serial = serial;
		}
	} else serial = go_date_g_to_serial (&date, conv);

	if (weekends != default_weekends)
		g_free (weekends);
	g_free (holidays);

	go_date_serial_to_g (&date, serial, conv);
	if (!g_date_valid (&date) ||
	    g_date_get_year (&date) < 1900 ||
	    g_date_get_year (&date) > 9999)
		return value_new_error_NUM (ei->pos);

	return make_date (value_new_int (go_date_g_to_serial (&date, conv)));

 bad:
	if (weekends != default_weekends)
		g_free (weekends);
	g_free (holidays);
	return value_new_error_VALUE (ei->pos);
}

/**************************************************************************
networkdays:

in OpenFormula 1.2:
The optional 4th parameter Workdays can be used to specify a different definition for the standard
work week by passing in a list of numbers which define which days of the week are workdays
(indicated by 0) or not (indicated by non-zero) in order Sunday, Monday,...,Saturday. So, the
default definition of the work week excludes Saturday and Sunday and is: {1;0;0;0;0;0;1}. To
define the work week as excluding Friday and Saturday, the third parameter would be:
{0;0;0;0;0;1;1}.

In the implementation, we are using g_date_get_weekday which returns
 typedef enum
{
  G_DATE_BAD_WEEKDAY  = 0,
  G_DATE_MONDAY       = 1,
  G_DATE_TUESDAY      = 2,
  G_DATE_WEDNESDAY    = 3,
  G_DATE_THURSDAY     = 4,
  G_DATE_FRIDAY       = 5,
  G_DATE_SATURDAY     = 6,
  G_DATE_SUNDAY       = 7
} GDateWeekday;
Since Sunday here is 7 rather than a 0 we need to make appropriate adjustments.

***************************************************************************/
static GnmFuncHelp const help_networkdays[] = {
        { GNM_FUNC_HELP_NAME, F_("NETWORKDAYS:number of workdays in range") },
        { GNM_FUNC_HELP_ARG, F_("start_date:starting date serial value")},
        { GNM_FUNC_HELP_ARG, F_("end_date:ending date serial value")},
        { GNM_FUNC_HELP_ARG, F_("holidays:array of holidays")},
        { GNM_FUNC_HELP_ARG, F_("weekend:array of 0s and 1s, indicating whether a weekday "
				"(S, M, T, W, T, F, S) is on the weekend, defaults to {1,0,0,0,0,0,1}")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NETWORKDAYS calculates the number of days from @{start_date} to @{end_date} "
					"skipping weekends and @{holidays} in the process.") },
	{ GNM_FUNC_HELP_NOTE, F_("If an entry of @{weekend} is non-zero, the corresponding weekday is not a work day.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible if the last argument is omitted.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NETWORKDAYS(DATE(2001,1,2),DATE(2001,2,15))" },
        { GNM_FUNC_HELP_EXAMPLES, "=NETWORKDAYS(DATE(2001,1,2),DATE(2001,2,15),,{0, 0, 0, 1, 1, 0, 0})" },
        { GNM_FUNC_HELP_SEEALSO, "WORKDAY"},
	{ GNM_FUNC_HELP_END }
};

static int
networkdays_calc (GDate start_date, int start_serial, int end_serial,
		  int n_non_weekend, gnm_float *weekends, int nholidays, gnm_float *holidays)
{
	int res = 0;
	int old_start_serial = start_serial;
	GDateWeekday weekday;
	int i, weeks;
	int h = 0;

	weekday = g_date_get_weekday (&start_date);
	if (weekday == G_DATE_BAD_WEEKDAY)
		return -1;
	if (weekday == G_DATE_SUNDAY)
		weekday = 0;

	weeks = (end_serial - start_serial)/7;
	start_serial = start_serial + weeks * 7;
	res = weeks * n_non_weekend;

	for (i = start_serial; i <= end_serial; i++) {
		if (!weekends[weekday])
			res++;
		weekday = (weekday + 1) % 7;
	}

	/*
	 * we may have included holidays.
	 */

	while (h < nholidays && holidays[h] <= end_serial) {
		if (holidays[h] >= old_start_serial)
			res--;
		h++;
	}

	return res;
}

static GnmValue *
gnumeric_networkdays (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int start_serial;
	int end_serial;
	int res, total_res = 0;
	GDate start_date, trouble_mar, trouble_feb, end_date;
	GODateConventions const *conv = DATE_CONV (ei->pos);
	gnm_float *holidays = NULL;
	gnm_float *weekends = NULL;
	gnm_float const default_weekends[] = { 1, 0, 0, 0, 0, 0, 1 };
	int nholidays, nweekends, n_non_weekend = 0;
	int i;
	GDateWeekday weekday;
	gboolean includes_bad_day = FALSE;

	/* Check the date period*/

	start_serial = datetime_value_to_serial (argv[0], conv);
	end_serial = datetime_value_to_serial (argv[1], conv);

	/* Swap if necessary */
	if (start_serial > end_serial) {
		int tmp = start_serial;
		start_serial = end_serial;
		end_serial = tmp;
	}

	/* Make sure that start and end serial are valid */
	if (start_serial <= 0 || end_serial <= 0)
		return value_new_error_NUM (ei->pos);

	go_date_serial_to_g (&start_date, start_serial, conv);
	if (!g_date_valid (&start_date))
		goto bad;
	go_date_serial_to_g (&end_date, end_serial, conv);
	if (!g_date_valid (&end_date))
		goto bad;

	g_date_set_dmy (&trouble_mar, 1, 3, 1900);
	if (g_date_compare (&start_date, &trouble_mar) < 0) {
		g_date_set_dmy (&trouble_feb, 28, 2, 1900);
		includes_bad_day =  (!g_date_valid (&end_date) || g_date_compare (&trouble_feb, &end_date) < 0);
	}

	/* get the weekend info */

	if (argv[3]) {
		GnmValue *result = NULL;

		weekends = collect_floats_value (argv[3], ei->pos,
						 COLLECT_COERCE_STRINGS |
						 COLLECT_ZEROONE_BOOLS |
						 COLLECT_IGNORE_BLANKS,
						 &nweekends, &result);
		if (result)
			return result;
		if (nweekends != 7)
			goto bad;

	} else {
		weekends = (gnm_float *)default_weekends;
		nweekends = 7;
	}

	/* If everything is a weekend we know the answer already */

	for (i = 0; i < 7; i++)
		if (weekends[i] == 0)
			n_non_weekend++;
	if (n_non_weekend == 0) {
		if (weekends != default_weekends)
			g_free (weekends);
		return value_new_int (0);
	}

	/* Now get the holiday info */

	if (argv[2]) {
		int j;
		GDate hol;
		GnmValue *result = NULL;

		holidays = collect_floats_value (argv[2], ei->pos,
						 COLLECT_COERCE_STRINGS |
						 COLLECT_IGNORE_BOOLS |
						 COLLECT_IGNORE_BLANKS |
						 COLLECT_SORT,
						 &nholidays, &result);
		if (result) {
			if (weekends != default_weekends)
				g_free (weekends);
			return result;
		}

		for (i = j = 0; i < nholidays; i++) {
			gnm_float s = holidays[i];
			int hserial;
			if (s < 0 || s > INT_MAX)
				goto bad;
			hserial = (int)s;
			if (j > 0 && hserial == holidays[j - 1])
				continue;  /* Dupe */
			go_date_serial_to_g (&hol, hserial, conv);
			if (!g_date_valid (&hol))
				goto bad;
			weekday = g_date_get_weekday (&hol);
			if (weekday == G_DATE_BAD_WEEKDAY)
				goto bad;
			if (weekday == G_DATE_SUNDAY)
				weekday = 0;
			/* We skip holidays that are on the weekend */
			if (weekends[weekday] != 0)
				continue;
			holidays[j++] = hserial;
		}
		nholidays = j;
	} else {
		holidays = NULL;
		nholidays = 0;
	}

	if (includes_bad_day) {
		total_res = networkdays_calc (start_date, start_serial,
					      go_date_g_to_serial (&trouble_feb, conv),
					      n_non_weekend, weekends, nholidays, holidays);
		if (total_res < 0)
			goto bad;
		res = networkdays_calc (trouble_mar, go_date_g_to_serial (&trouble_mar, conv),
					end_serial,
					n_non_weekend, weekends, nholidays, holidays);
		if (res < 0)
			goto bad;
		total_res += res;
	} else {
		total_res = networkdays_calc (start_date, start_serial, end_serial,
					n_non_weekend, weekends, nholidays, holidays);
		if (total_res < 0)
			goto bad;
	}

	if (weekends != default_weekends)
		g_free (weekends);
	g_free (holidays);

	return value_new_int (total_res);

 bad:
	if (weekends != default_weekends)
		g_free (weekends);
	g_free (holidays);
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_isoweeknum[] = {
        { GNM_FUNC_HELP_NAME, F_("ISOWEEKNUM:ISO week number")},
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ISOWEEKNUM calculates the week number according to the ISO 8601 standard.  Weeks start on Mondays and week 1 contains the first Thursday of the year.") },
	{ GNM_FUNC_HELP_NOTE, F_("January 1 of a year is sometimes in week 52 or 53 of the previous year.  Similarly, December 31 is sometimes in week 1 of the following year.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISOWEEKNUM(DATE(2000,1,1))" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISOWEEKNUM(DATE(2008,1,1))" },
        { GNM_FUNC_HELP_SEEALSO, "ISOYEAR,WEEKNUM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isoweeknum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos));
	if (!g_date_valid (&date))
                  return value_new_error_VALUE (ei->pos);

	return value_new_int (go_date_weeknum (&date, GO_WEEKNUM_METHOD_ISO));
}

/***************************************************************************/

static GnmFuncHelp const help_isoyear[] = {
        { GNM_FUNC_HELP_NAME, F_("ISOYEAR:year corresponding to the ISO week number")},
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ISOYEAR calculates the year to go with week number according to the ISO 8601 standard.") },
	{ GNM_FUNC_HELP_NOTE, F_("January 1 of a year is sometimes in week 52 or 53 of the previous year.  Similarly, December 31 is sometimes in week 1 of the following year.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISOYEAR(DATE(2000,1,1))" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISOYEAR(DATE(2008,1,1))" },
        { GNM_FUNC_HELP_SEEALSO, "ISOWEEKNUM,YEAR"},
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

	isoweeknum = go_date_weeknum (&date, GO_WEEKNUM_METHOD_ISO);
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
        { GNM_FUNC_HELP_NAME, F_("WEEKNUM:week number")},
        { GNM_FUNC_HELP_ARG, F_("date:date serial value")},
        { GNM_FUNC_HELP_ARG, F_("method:numbering system, defaults to 1")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("WEEKNUM calculates the week number according to @{method} which defaults to 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 1, then weeks start on Sundays and January 1 is in week 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 2, then weeks start on Mondays and January 1 is in week 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{method} is 150, then the ISO 8601 numbering is used.") },
        { GNM_FUNC_HELP_EXAMPLES, "=WEEKNUM(DATE(2000,1,1))" },
        { GNM_FUNC_HELP_EXAMPLES, "=WEEKNUM(DATE(2008,1,1))" },
        { GNM_FUNC_HELP_SEEALSO, "ISOWEEKNUM"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_weeknum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GDate date;
	gnm_float method = argv[1] ? gnm_floor (value_get_as_float (argv[1])) : 1;
	int m;

	if (method == 1)
		m = GO_WEEKNUM_METHOD_SUNDAY;
	else if (method == 2)
		m = GO_WEEKNUM_METHOD_MONDAY;
	else if (method == 150 || method == 21)
		m = GO_WEEKNUM_METHOD_ISO;
	else
		return value_new_error_VALUE (ei->pos);

	datetime_value_to_g (&date, argv[0], DATE_CONV (ei->pos));
	if (!g_date_valid (&date))
                  return value_new_error_VALUE (ei->pos);

	return value_new_int (go_date_weeknum (&date, m));
}

/***************************************************************************/

static GnmFuncHelp const help_yearfrac[] = {
        { GNM_FUNC_HELP_NAME, F_("YEARFRAC:fractional number of years between dates")},
        { GNM_FUNC_HELP_ARG, F_("start_date:starting date serial value")},
        { GNM_FUNC_HELP_ARG, F_("end_date:ending date serial value")},
        { GNM_FUNC_HELP_ARG, F_("basis:calendar basis")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("YEARFRAC calculates the number of days from @{start_date} to @{end_date} according to the calendar specified by @{basis}, which defaults to 0, and expresses the result as a fractional number of years.") },
	GNM_DATE_BASIS_HELP
        { GNM_FUNC_HELP_SEEALSO, "DATE"},
        { GNM_FUNC_HELP_EXAMPLES, "=YEARFRAC(DATE(2000,1,1),DATE(2001,4,1))" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_yearfrac (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GODateConventions const *conv = DATE_CONV (ei->pos);
	GDate start_date, end_date;
	int basis = value_get_basis (argv[2], GO_BASIS_MSRB_30_360);

	if (basis < 0 || basis > 4 ||
	    !datetime_value_to_g (&start_date, argv[0], conv) ||
	    !datetime_value_to_g (&end_date, argv[1], conv))
		return value_new_error_NUM (ei->pos);

	return value_new_float (yearfrac (&start_date, &end_date, basis));
}

/***************************************************************************/

static GnmFuncHelp const help_days[] = {
        { GNM_FUNC_HELP_NAME, F_("DAYS:difference between dates in days") },
        { GNM_FUNC_HELP_ARG, F_("end_date:ending date serial value")},
        { GNM_FUNC_HELP_ARG, F_("start_date:starting date serial value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("DAYS returns the positive or negative number of days from @{start_date} to @{end_date}.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DAYS(DATE(2003,2,3),DATE(2007,4,2))" },
	{ GNM_FUNC_HELP_EXAMPLES, "=DAYS(DATE(2007,4,2),DATE(2003,2,3))" },
	{ GNM_FUNC_HELP_EXAMPLES, "=DAYS(DATE(1900,2,28),DATE(1900,3,1))" },
        { GNM_FUNC_HELP_SEEALSO, "DATEDIF"},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_days (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	int date1, date2;
	GDate d1, d2;
	GODateConventions const *conv = DATE_CONV (ei->pos);

	date1 = gnm_floor (value_get_as_float (argv [0]));
	date2 = gnm_floor (value_get_as_float (argv [1]));

	go_date_serial_to_g (&d1, date1, conv);
	go_date_serial_to_g (&d2, date2, conv);

	return value_new_int (g_date_days_between (&d2, &d1));
}

/***************************************************************************/

GnmFuncDescriptor const datetime_functions[] = {
	{ "date",        "fff",   help_date,
	  gnumeric_date, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "datevalue",   "f",     help_datevalue,
	  gnumeric_datevalue, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "datedif",     "ffs",   help_datedif,
	  gnumeric_datedif, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "day",         "f",     help_day,
	  gnumeric_day, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "days360",     "ff|f",  help_days360,
	  gnumeric_days360, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "edate",       "ff",    help_edate,
	  gnumeric_edate, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "eomonth",     "f|f",   help_eomonth,
	  gnumeric_eomonth, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hour",        "f",     help_hour,
	  gnumeric_hour, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "minute",      "f",     help_minute,
	  gnumeric_minute, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "month",       "f",     help_month,
	  gnumeric_month, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "networkdays", "ff|?A",
	  help_networkdays, gnumeric_networkdays, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "now",         "", help_now,
	  gnumeric_now, NULL,
	  GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_TIME,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "odf.time",        "fff",   help_odf_time,
	  gnumeric_odf_time, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_TIME,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "second",      "f",     help_second,
	  gnumeric_second, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "time",        "fff",   help_time,
	  gnumeric_time, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_TIME,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "timevalue",   "f",     help_timevalue,
	  gnumeric_timevalue, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "today",       "", help_today,
	  gnumeric_today, NULL,
	  GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "weekday",     "f|f",   help_weekday,
	  gnumeric_weekday, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "weeknum",     "f|f",   help_weeknum,
	  gnumeric_weeknum, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "workday",     "ff|?A",  help_workday,
	  gnumeric_workday, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_BASIC },
	{ "year",        "f",     help_year,
	  gnumeric_year, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "yearfrac",	"ff|f",     help_yearfrac,
	  gnumeric_yearfrac, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "unix2date",   "f",     help_unix2date,
	  gnumeric_unix2date, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_DATE,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "date2unix",   "f",     help_date2unix,
	  gnumeric_date2unix, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "isoweeknum",  "f",     help_isoweeknum,
	  gnumeric_isoweeknum, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "isoyear",     "f",     help_isoyear,
	  gnumeric_isoyear, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "days", "ff",
	  help_days, gnumeric_days, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        {NULL}
};
