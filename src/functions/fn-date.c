/*
 * fn-date.c:  Built in date functions.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <math.h>
#include "gnumeric.h"
#include "parse-util.h"
#include "str.h"
#include "cell.h"
#include "func.h"
#include "datetime.h"
#include "auto-format.h"

#define DAY_SECONDS (3600*24)

/***************************************************************************/

static char *help_date = {
	N_("@FUNCTION=DATE\n"
	   "@SYNTAX=DATE (year,month,day)\n"

	   "@DESCRIPTION="
	   "Computes the number of days since the 1st of january of 1900"
	   "(the date serial number) for the given year, month and day.\n"

	   "The @day might be negative (to count backwards) and it is relative "
	   "to the previous @month.  The @years should be at least 1900."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TODAY, NOW")
};

static Value *
gnumeric_date (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	int year, month, day;
	GDate date;

	year  = floor (value_get_as_float (argv [0]));
	month = floor (value_get_as_float (argv [1]));
	day   = floor (value_get_as_float (argv [2]));

	/* FIXME: someone should check this.  */
	if (year <= 30)
		year += 2000;
	else if (year < 100)
		year += 1900;

        if (!g_date_valid_dmy (1, month, year))
		return value_new_error (ei->pos, _("Invalid month or year"));

        g_date_clear(&date, 1);

	g_date_set_dmy (&date, 1, month, year);

	if (day > 0)
                g_date_set_day (&date, day);
	else
		g_date_subtract_days (&date, -day + 1);

        if (!g_date_valid (&date))
		return value_new_error (ei->pos, _("Invalid day"));

	v = value_new_int (datetime_g_to_serial (&date));

	return v;
}

/***************************************************************************/

static char *help_datevalue = {
	N_("@FUNCTION=DATEVALUE\n"
	   "@SYNTAX=DATEVALUE(date_str)\n"

	   "@DESCRIPTION="
	   "DATEVALUE returns the serial number of the date.  @date_str is "
	   "the string that contains the date.  For example, "
	   "DATEVALUE(\"1/1/1999\") equals to 36160. "
	   "\n"
	   "@EXAMPLES=\n"
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

static char *help_datedif = {
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
	   "\n"
	   "@EXAMPLES=\n"
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

	day = g_date_day (gdate1);

	g_date_add_years (gdate1,
	                       datetime_g_years_between (gdate1, gdate2));
	/* according to glib.h, feb 29 turns to feb 28 if necessary */

	if (excel_compat) {
		int new_year1, new_year2;

		/* treat all years divisible by four as leap years: */
		/* this is clearly wrong, but it's what Excel does. */
		/* ( i use 2004 here since it is clearly a leap year.) */
		new_year1 = 2004 + (g_date_year (gdate1) & 0x3);
		new_year2 = new_year1 + (g_date_year (gdate2) - g_date_year (gdate1));
		g_date_set_year (gdate1, new_year1);
		g_date_set_year (gdate2, new_year2);

		{
			static gboolean need_warning = TRUE;
			if (need_warning) {
				g_warning("datedif is known to differ from Excel for some values.");
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

	day = g_date_day (gdate1);

	g_date_add_months (gdate1,
	                   datetime_g_months_between (gdate1, gdate2));
	/* according to glib.h, days>28 decrease if necessary */

	if (excel_compat) {
		int new_year1, new_year2;

		/* treat all years divisible by four as leap years: */
		/* this is clearly wrong, but it's what Excel does. */
		/* ( i use 2004 here since it is clearly a leap year.) */
		new_year1 = 2004 + (g_date_year (gdate1) & 0x3);
		new_year2 = new_year1 + (g_date_year (gdate2) - g_date_year (gdate1));
		g_date_set_year (gdate1, new_year1);
		g_date_set_year (gdate2, new_year2);

		/* add back the days if they were decreased by
		   g_date_add_months */
		/* ( i feel this is inferior because it reports e.g.:
		     datedif(1/31/95,3/1/95,"d") == -2 ) */
		g_date_add_days (gdate1,
		                 day - g_date_day (gdate1));
	}

	return datetime_g_days_between (gdate1, gdate2);
}

static Value *
gnumeric_datedif (FunctionEvalInfo *ei, Value **argv)
{
	int date1, date2;
	char *opt;

	GDate *gdate1, *gdate2;
	Value *result;

	date1 = floor (value_get_as_float (argv [0]));
	date2 = floor (value_get_as_float (argv [1]));
	opt = argv [2]->v_str.val->str;

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

	g_date_free (gdate1);
	g_date_free (gdate2);

	return result;
}

/***************************************************************************/

static char *help_edate = {
	N_("@FUNCTION=EDATE\n"
	   "@SYNTAX=EDATE(date,months)\n"

	   "@DESCRIPTION="
	   "EDATE returns the serial number of the date that is the "
	   "specified number of months before or after a given date.  "
	   "@date is the serial number of the initial date and @months "
	   "is the number of months before (negative number) or after "
	   "(positive number) the initial date."
	   "\n"
	   "If @months is not an integer, it is truncated."
	   "\n"
	   "@EXAMPLES=\n"
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
                  g_date_free (date);
                  return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	if (months > 0)
	        g_date_add_months (date, months);
	else
	        g_date_subtract_months (date, -months);

	if (!g_date_valid (date)) {
                  g_date_free (date);
                  return value_new_error (ei->pos, gnumeric_err_NUM);
	}

	res = value_new_int (datetime_g_to_serial (date));
	g_date_free (date);
	return res;
}

/***************************************************************************/

static char *help_today = {
	N_("@FUNCTION=TODAY\n"
	   "@SYNTAX=TODAY ()\n"

	   "@DESCRIPTION="
	   "Returns the serial number for today (the number of days "
	   "elapsed since the 1st of January of 1900)."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TODAY, NOW")
};

static Value *
gnumeric_today (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_int (datetime_timet_to_serial (time (NULL)));
}

/***************************************************************************/

static char *help_now = {
	N_("@FUNCTION=NOW\n"
	   "@SYNTAX=NOW ()\n"

	   "@DESCRIPTION="
	   "Returns the serial number for the date and time at the time "
	   "it is evaluated.\n"
	   ""
	   "Serial Numbers in Gnumeric are represented as follows:"
	   "The integral part is the number of days since the 1st of "
	   "January of 1900.  The decimal part represent the fraction "
	   "of the day and is mapped into hour, minutes and seconds.\n"
	   ""
	   "For example: .0 represents the beginning of the day, and 0.5 "
	   "represents noon."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TODAY, NOW")
};

static Value *
gnumeric_now (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (datetime_timet_to_serial_raw (time (NULL)));
}

/***************************************************************************/

static char *help_time = {
	N_("@FUNCTION=TIME\n"
	   "@SYNTAX=TIME (hours,minutes,seconds)\n"

	   "@DESCRIPTION="
	   "Returns a fraction representing the time of day."
	   "\n"
	   "@EXAMPLES=\n"
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

static char *help_timevalue = {
	N_("@FUNCTION=TIMEVALUE\n"
	   "@SYNTAX=TIMEVALUE (timetext)\n"

	   "@DESCRIPTION="
	   "Returns a fraction representing the time of day, a number "
	   "between 0 and 1."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=HOUR")
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

static char *help_hour = {
	N_("@FUNCTION=HOUR\n"
	   "@SYNTAX=HOUR (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to an hour.  The hour is returned as "
	   "an integer in the range 0 (12:00 A.M.) to 23 (11:00 P.M.)."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   "\n"
	   "@EXAMPLES=\n"
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

static char *help_minute = {
	N_("@FUNCTION=MINUTE\n"
	   "@SYNTAX=MINUTE (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a minute.  The minute is returned as "
	   "an integer in the range 0 to 59."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string."
	   "\n"
	   "@EXAMPLES=\n"
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

static char *help_second = {
	N_("@FUNCTION=SECOND\n"
	   "@SYNTAX=SECOND (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a second.  The second is returned as "
	   "an integer in the range 0 to 59."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string."
	   "\n"
	   "@EXAMPLES=\n"
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

static char *help_year = {
	N_("@FUNCTION=YEAR\n"
	   "@SYNTAX=YEAR (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a year."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string."
	   "\n"
	   "@EXAMPLES=\n"
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
		res = g_date_year (date);
		g_date_free (date);
	}
	return value_new_int (res);
}

/***************************************************************************/

static char *help_month = {
	N_("@FUNCTION=MONTH\n"
	   "@SYNTAX=MONTH (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a month."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string."
	   "\n"
	   "@EXAMPLES=\n"
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
		res = g_date_month (date);
		g_date_free (date);
	}
	return value_new_int (res);
}

/***************************************************************************/

static char *help_day = {
	N_("@FUNCTION=DAY\n"
	   "@SYNTAX=DAY (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a day of month."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string."
	   "\n"
	   "@EXAMPLES=\n"
	   "day (\"10/24/1968\") equals 24.\n"
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
		res = g_date_day (date);
		g_date_free (date);
	}
	return value_new_int (res);
}

/***************************************************************************/

static char *help_weekday = {
	N_("@FUNCTION=WEEKDAY\n"
	   "@SYNTAX=WEEKDAY (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a weekday.\n"
	   "\n"
	   "This function returns an integer in the range 0-6, where "
	   "Saturday is 0, Sunday is 1, etc.\n"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string."
	   "\n"
	   "@EXAMPLES=\n"
	   "WEEKDAY(\"10/24/1968\") equals 5 (Thursday).\n"
	   "\n"
	   "@SEEALSO=DAY, MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_weekday (FunctionEvalInfo *ei, Value **argv)
{
	int res = 1;
	GDate *date;

	if (argv[0]->type == VALUE_ERROR)
		return value_duplicate (argv[0]);

	date = datetime_value_to_g (argv[0]);
	if (date != NULL) {
		res = (g_date_weekday (date) + 1) % 7;
		g_date_free (date);
	}
	return value_new_int (res);
}

/***************************************************************************/

static char *help_days360 = {
	N_("@FUNCTION=DAYS360 \n"
	   "@SYNTAX=DAYS360 (date1,date2,method)\n"

	   "@DESCRIPTION="
	   "Returns the number of days from @date1 to @date2 following a "
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
	   "string."
	   "\n"
	   "@EXAMPLES=\n"
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
	day1 = g_date_day (date1);
	day2 = g_date_day (date2);
	month1 = g_date_month (date1);
	month2 = g_date_month (date2);
	year1 = g_date_year (date1);
	year2 = g_date_year (date2);

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

	g_date_free (date1);
	g_date_free (date2);

	return value_new_int (flipped ? -result : result);
}

/***************************************************************************/

static char *help_eomonth = {
	N_("@FUNCTION=EOMONTH\n"
	   "@SYNTAX=EOMONTH (start_date,months)\n"

	   "@DESCRIPTION="
	   "Returns the last day of the month which is @months "
	   "from the @start_date."
	   "\n"
	   "Returns #NUM! if start_date or months are invalid."
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

	g_date_set_day(date, g_date_days_in_month(g_date_month(date),
						  g_date_year(date)));

	res = value_new_int (datetime_g_to_serial (date));
	g_date_free (date);
	return res;
}

/***************************************************************************/

static char *help_workday = {
	N_("@FUNCTION=WORKDAY\n"
	   "@SYNTAX=WORKDAY (start_date,days,holidays)\n"

	   "@DESCRIPTION="
	   "Returns the day which is @days working days "
	   "from the @start_date.  Weekends and holidays optionally "
	   "supplied in @holidays are respected."
	   "\n"
	   "Returns #NUM! if @start_date or @days are invalid."
	   "\n"
	   "@EXAMPLES=\n"
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
	weekday = g_date_weekday (date);

	days = value_get_as_int (argv[1]);

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

static char * help_networkdays = {
	N_("@FUNCTION=NETWORKDAYS\n"
	   "@SYNTAX=NETWORKDAYS (start_date,end_date,holidays)\n"

	   "@DESCRIPTION="
	   "Returns the number of non-weekend non-holidays between @start_date "
	   "and @end_date.  Holidays optionally supplied in @holidays."
	   "\n"
	   "Returns #NUM if start_date or end_date are invalid"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=WORKDAY")
};

/*
 * A utility routine to return the 1st monday <= the serial if its valid
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
		*offset = (int)g_date_weekday (date) - 1;
		serial -= *offset;
		if (*offset > 4)
			*offset = 4;
	} else
		serial = -1;
	g_date_free (date);
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
		if (g_date_weekday (date) < G_DATE_SATURDAY)
			++close->res;
	} else
		res = value_new_error (ep, gnumeric_err_NUM);

	g_date_free (date);
	return res;
}

static Value *
gnumeric_networkdays (FunctionEvalInfo *ei, Value **argv)
{
	int start_serial = datetime_value_to_serial (argv[0]);
	int end_serial = datetime_value_to_serial (argv[1]);
	int start_offset, end_offset, res;
	networkdays_holiday_closure close;

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

	return value_new_int (res - start_offset + end_offset + 1 - close.res);
}

/***************************************************************************/

void date_functions_init(void);
void
date_functions_init(void)
{
	FunctionDefinition *def;
	FunctionCategory *cat = function_get_category_with_translation ("Date / Time", _("Date / Time"));

	def = function_add_args (cat,  "date",           "fff",
				 "year,month,day",
				 &help_date,	      gnumeric_date);
	auto_format_function_result (def, AF_DATE);

	def = function_add_args (cat,  "datevalue",      "S",
				 "date_str",
				 &help_datevalue,   gnumeric_datevalue);

	def = function_add_args (cat,  "datedif",        "SSs",
				 "date1,date2,Interval",
				 &help_datedif,     gnumeric_datedif);

	def = function_add_args (cat,  "day",            "S",
				 "date",
				 &help_day,	      gnumeric_day);

	def = function_add_args (cat,  "days360",        "SS|f",
				 "date1,date2,method",
				 &help_days360,     gnumeric_days360);

	def = function_add_args (cat,  "edate",          "ff",
				 "serial_number,months",
				 &help_edate,       gnumeric_edate);
	auto_format_function_result (def, AF_DATE);

	def = function_add_args (cat,  "eomonth",        "S|f",
				 "start_date,months",
				 &help_eomonth,     gnumeric_eomonth);
	auto_format_function_result (def, AF_DATE);

	def = function_add_args (cat,  "hour",           "S",
				 "time",
				 &help_hour,        gnumeric_hour );

	def = function_add_args (cat,  "minute",         "S",
				 "time",
				 &help_minute,      gnumeric_minute );

	def = function_add_args (cat,  "month",          "S",
				 "date",
				 &help_month,       gnumeric_month);

	def = function_add_args (cat,  "networkdays",    "SS|?",
				 "start_date,end_date,holidays",
				 &help_networkdays, gnumeric_networkdays );

	def = function_add_args (cat,  "now",            "",
				 "",
				 &help_now,         gnumeric_now );

	def = function_add_args (cat,  "second",         "S",
				 "time",
				 &help_second,      gnumeric_second );

	def = function_add_args (cat,  "time",           "fff",
				 "hours,minutes,seconds",
				 &help_time,        gnumeric_time );
	auto_format_function_result (def, AF_TIME);

	def = function_add_args (cat,  "timevalue",      "S",
				 "",
				 &help_timevalue,   gnumeric_timevalue );

	def = function_add_args (cat,  "today",          "",
				 "",
				 &help_today,       gnumeric_today );
	auto_format_function_result (def, AF_DATE);

	def = function_add_args (cat,  "weekday",        "S",
				 "date",
				 &help_weekday,     gnumeric_weekday);

	def = function_add_args (cat,  "workday",        "Sf|?",
				 "date,days,holidays",
				 &help_workday,     gnumeric_workday);

	def = function_add_args (cat,  "year",           "S",
				 "date",
				 &help_year,        gnumeric_year);
}
