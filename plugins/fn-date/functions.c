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
#include "utils.h"
#include "func.h"
#include "number-match.h"

#define DAY_SECONDS (3600*24)

static float_t
get_serial_date (Value *v)
{
	float_t serial;

	if (VALUE_IS_NUMBER (v))
		serial = value_get_as_float (v);
	else {
		char *format;

		if (!format_match (v->v.str->str, &serial, &format))
			serial = 0;
	}
	return floor (serial);
}


static float_t
get_serial_time (Value *v)
{
	float_t serial;

	if (VALUE_IS_NUMBER (v))
		serial = value_get_as_float (v);
	else {
		char *format;

		if (!format_match (v->v.str->str, &serial, &format))
			serial = 0;
	}
	return serial - floor (serial);
}



static char *help_date = {
	N_("@FUNCTION=DATE\n"
	   "@SYNTAX=DATE (year,month,day)\n"

	   "@DESCRIPTION="
	   "Computes the number of days since the 1st of january of 1900"
	   "(the date serial number) for the given year, month and day.\n"

	   "The day might be negative (to count backwards) and it is relative "
	   "to the previous month.  The years should be at least 1900."
	   "\n"
	   ""
	   "@SEEALSO=TODAY, NOW")
};

static Value *
gnumeric_date (struct FunctionDefinition *fd, Value *argv [], char **error_string)
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
	else if (year <= 99)
		year += 1900;

        if (!g_date_valid_dmy(1, month, year))
          {
                  *error_string = _("Invalid month or year");
                  return NULL;
          }

        g_date_clear(&date, 1);

	g_date_set_dmy (&date, 1, month, year);

	if (day > 0)
                g_date_set_day (&date, day);
	else
		g_date_subtract_days (&date, -day + 1);

        if (!g_date_valid(&date))
          {
                  *error_string = _("Invalid day");
                  return NULL;
          }

	v = value_new_int (g_date_serial (&date));

	return v;
}


static char *help_datevalue = {
	N_("@FUNCTION=DATEVALUE\n"
	   "@SYNTAX=DATEVALUE(date_str)\n"

	   "@DESCRIPTION="
	   "DATEVALUE returns the serial number of the date.  @date_str is "
	   "the string that contains the date.  For example, "
	   "DATEVALUE(\"1/1/1999\") equals to 36160. "
	   "\n"
	   ""
	   "@SEEALSO=DATE")
};

static Value *
gnumeric_datevalue (struct FunctionDefinition *fd,
		    Value *argv [], char **error_string)
{
	return value_new_int ((int) get_serial_date (argv[0]));
}

static char *help_edate = {
	N_("@FUNCTION=EDATE\n"
	   "@SYNTAX=EDATE(serial_number,months)\n"

	   "@DESCRIPTION="
	   "EDATE returns the serial number of the date that is the "
	   "specified number of months before or after a given date.  "
	   "@date is the serial number of the initial date and @months "
	   "is the number of months before (negative number) or after "
	   "(positive number) the initial date."
	   "\n"
	   "If @months is not an integer, it is truncated."
	   "\n"
	   "@SEEALSO=DATE")
};

static Value *
gnumeric_edate (struct FunctionDefinition *fd,
		Value *argv [], char **error_string)
{
	int    serial, months;
	GDate* date;

	serial = value_get_as_int(argv[0]);
	months = value_get_as_int(argv[1]);

	date = g_date_new_serial (serial);

	if (!g_date_valid(date)) {
                  *error_string = gnumeric_err_VALUE;
                  return NULL;
	}

	if (months > 0)
	        g_date_add_months (date, months);
	else
	        g_date_subtract_months (date, -months);

	if (!g_date_valid(date)) {
                  *error_string = gnumeric_err_NUM;
                  return NULL;
	}

	return value_new_int (g_date_serial (date));
}

static char *help_today = {
	N_("@FUNCTION=TODAY\n"
	   "@SYNTAX=TODAY ()\n"

	   "@DESCRIPTION="
	   "Returns the serial number for today (the number of days "
	   "elapsed since the 1st of January of 1900)"
	   "\n"

	   ""
	   "@SEEALSO=TODAY, NOW")
};

static Value *
gnumeric_today (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	GDate date;

        g_date_clear(&date, 1);
	g_date_set_time (&date, time (NULL));

	return value_new_int (g_date_serial (&date));
}

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
	   "of the day and is mapped into hour, minutes and seconds\n"
	   ""
	   "For example: .0 represents the beginning of the day, and 0.5 "
	   "represents noon"
	   "\n"

	   ""
	   "@SEEALSO=TODAY, NOW")
};

static Value *
gnumeric_now (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);
	int secs;

	GDate date;
        g_date_clear (&date, 1);
	g_date_set_time (&date, t);

	secs = tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
	return value_new_float (g_date_serial (&date) + secs / (double)DAY_SECONDS);
}

static char *help_time = {
	N_("@FUNCTION=TIME\n"
	   "@SYNTAX=TIME (hours,minutes,seconds)\n"

	   "@DESCRIPTION="
	   "Returns a fraction representing the time of day."
	   "\n"

	   ""
	   "@SEEALSO=HOUR")
};

static Value *
gnumeric_time (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	float_t hours, minutes, seconds;

	hours   = value_get_as_float (argv [0]);
	minutes = value_get_as_float (argv [1]);
	seconds = value_get_as_float (argv [2]);

	return value_new_float ((hours * 3600 + minutes * 60 + seconds) / DAY_SECONDS);
}


static char *help_timevalue = {
	N_("@FUNCTION=TIMEVALUE\n"
	   "@SYNTAX=TIMEVALUE (timetext)\n"

	   "@DESCRIPTION="
	   "Returns a fraction representing the time of day, a number "
	   "between 0 and 1."
	   "\n"
	   ""
	   "@SEEALSO=HOUR")
};

static Value *
gnumeric_timevalue (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	return value_new_float (get_serial_time (argv[0]));
}


static char *help_hour = {
	N_("@FUNCTION=HOUR\n"
	   "@SYNTAX=HOUR (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to an hour.  The hour is returned as "
	   "an integer in the range 0 (12:00 A.M.) to 23 (11:00 P.M.)"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=MINUTE, NOW, TIME, SECOND")
};

static Value *
gnumeric_hour (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	int secs;

	secs = (int)(get_serial_time (argv[0]) * DAY_SECONDS + 0.5);
	return value_new_int (secs / 3600);
}


static char *help_minute = {
	N_("@FUNCTION=MINUTE\n"
	   "@SYNTAX=MINUTE (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a minute.  The minute is returned as "
	   "an integer in the range 0 to 59"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=HOUR, NOW, TIME, SECOND")
};

static Value *
gnumeric_minute (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	int secs;

	secs = (int)(get_serial_time (argv[0]) * DAY_SECONDS + 0.5);
	return value_new_int ((secs / 60) % 60);
}


static char *help_second = {
	N_("@FUNCTION=SECOND\n"
	   "@SYNTAX=SECOND (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a second.  The second is returned as "
	   "an integer in the range 0 to 59"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=HOUR, MINUTE, NOW, TIME")
};

static Value *
gnumeric_second (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	int secs;

	secs = (int)(get_serial_time (argv[0]) * DAY_SECONDS + 0.5);
	return value_new_int (secs % 60);
}


static char *help_year = {
	N_("@FUNCTION=YEAR\n"
	   "@SYNTAX=YEAR (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a year."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=DAY, MONTH, TIME, NOW")
};

static Value *
gnumeric_year (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	int res;
	GDate *date = g_date_new_serial (get_serial_date (argv[0]));
	res = g_date_year (date);
	g_date_free (date);
	return value_new_int (res);
}


static char *help_month = {
	N_("@FUNCTION=MONTH\n"
	   "@SYNTAX=MONTH (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a month."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=DAY, TIME, NOW, YEAR")
};

static Value *
gnumeric_month (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	int res;
	GDate *date = g_date_new_serial (get_serial_date (argv[0]));
	res = g_date_month (date);
	g_date_free (date);
	return value_new_int (res);
}


static char *help_day = {
	N_("@FUNCTION=DAY\n"
	   "@SYNTAX=DAY (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a day."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_day (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	int res;
	GDate *date = g_date_new_serial (get_serial_date (argv[0]));
	res = g_date_day (date);
	g_date_free (date);
	return value_new_int (res);
}


static char *help_weekday = {
	N_("@FUNCTION=WEEKDAY\n"
	   "@SYNTAX=WEEKDAY (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a weekday.  FIXME: explain."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_weekday (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	int res;
	GDate *date = g_date_new_serial (get_serial_date (argv[0]));
	res = (g_date_weekday (date) + 1) % 7;
	g_date_free (date);
	return value_new_int (res);
}


static char *help_days360 = {
	N_("@FUNCTION=DAY\n"
	   "@SYNTAX=DAYS360 (date1,date2,method)\n"

	   "@DESCRIPTION="
	   "Returns the number of days from @date1 to @date2 following a "
	   "360-day calendar in which all months are assumed to have 30 days."
	   "\n"
	   "If method is true, the European method will be used.  In this "
	   "case, if the day of the month is 31 it will be considered as 30."
	   "\n"
	   "If method is false or omitted, the US method will be used.  "
	   "This is a somewhat complicated industry standard method."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_days360 (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	enum { METHOD_US, METHOD_EUROPE } method;
	GDate *date1, *date2;
	int day1, day2, month1, month2, year1, year2, result;
	gboolean flipped;
	float_t serial1, serial2;

	if (argv[2]) {
		int err;
		method = value_get_as_bool (argv[2], &err) ? METHOD_EUROPE : METHOD_US;
		if (err) {
			*error_string = _("Unsupported method");
			return NULL;
		}
	} else
		method = METHOD_US;

	serial1 = get_serial_date (argv[0]);
	serial2 = get_serial_date (argv[1]);
	if ((flipped = (serial1 > serial2))) {
		float_t tmp = serial1;
		serial1 = serial2;
		serial2 = tmp;
	}

	date1 = g_date_new_serial (serial1);
	date2 = g_date_new_serial (serial2);
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



FunctionDefinition date_functions [] = {
	{ "date",      "fff",  "year,month,day",        &help_date,
	  NULL, gnumeric_date  },
	{ "datevalue", "?",    "date_str",              &help_datevalue,
	  NULL, gnumeric_datevalue  },
	{ "day",       "?",    "date",                  &help_day,
	  NULL, gnumeric_day  },
	{ "days360",   "??|i", "date1,date2,method",    &help_days360,
	  NULL, gnumeric_days360  },
	{ "edate",     "ff",   "serial_number,months",  &help_edate,
	  NULL, gnumeric_edate  },
	{ "hour",      "?",    "time",                  &help_hour,
	  NULL, gnumeric_hour },
	{ "minute",    "?",    "time",                  &help_minute,
	  NULL, gnumeric_minute },
	{ "month",     "?",    "date",                  &help_month,
	  NULL, gnumeric_month  },
	{ "now",       "",     "",                      &help_now,
	  NULL, gnumeric_now },
	{ "second",    "?",    "time",                  &help_second,
	  NULL, gnumeric_second },
	{ "time",      "fff",  "hours,minutes,seconds", &help_time,
	  NULL, gnumeric_time },
	{ "timevalue", "?",    "",                      &help_timevalue,
	  NULL, gnumeric_timevalue },
	{ "today",     "",     "",                      &help_today,
	  NULL, gnumeric_today },
	{ "weekday",   "?",    "date",                  &help_weekday,
	  NULL, gnumeric_weekday  },
	{ "year",      "?",    "date",                  &help_year,
	  NULL, gnumeric_year  },
	{ NULL, NULL },
};
