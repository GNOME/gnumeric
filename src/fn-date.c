/*
 * fn-date.c:  Built in date functions.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

#define DAY_SECONDS (3600*24)

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

static FuncReturn *
gnumeric_date (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
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
                  s->error_string = _("Invalid month or year");
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
                  s->error_string = _("Invalid day");
                  return NULL;
          }

	v = value_new_int (g_date_serial (&date));

	return (FuncReturn *)v;
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

static FuncReturn *
gnumeric_datevalue (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	const gchar *datestr;
	GDate       date;

	if (argv[0]->type != VALUE_STRING) {
                  s->error_string = _("#NUM!");
                  return NULL;
	}
	datestr = argv[0]->v.str->str;
	g_date_set_parse (&date, datestr);
	if (!g_date_valid(&date)) {
                  s->error_string = _("#VALUE!");
                  return NULL;
	}

	return (FuncReturn *)value_new_int (g_date_serial (&date));
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

static FuncReturn *
gnumeric_edate (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	int    serial, months;
	GDate* date;

	serial = value_get_as_int(argv[0]);
	months = value_get_as_int(argv[1]);

	date = g_date_new_serial (serial);

	if (!g_date_valid(date)) {
                  s->error_string = _("#VALUE!");
                  return NULL;
	}

	if (months > 0)
	        g_date_add_months (date, months);
	else
	        g_date_subtract_months (date, -months);

	if (!g_date_valid(date)) {
                  s->error_string = _("#NUM!");
                  return NULL;
	}

	return (FuncReturn *)value_new_int (g_date_serial (date));
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

static FuncReturn *
gnumeric_today (FunctionEvalInfo *s)
{
	GDate date;

        g_date_clear(&date, 1);
	g_date_set_time (&date, time (NULL));

	return (FuncReturn *)value_new_int (g_date_serial (&date));
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

static FuncReturn *
gnumeric_now (FunctionEvalInfo *s)
{
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);
	int secs;

	GDate date;
        g_date_clear (&date, 1);
	g_date_set_time (&date, t);

	secs = tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
	return (FuncReturn *)value_new_float (g_date_serial (&date) + secs / (double)DAY_SECONDS);
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

static FuncReturn *
gnumeric_time (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	float_t hours, minutes, seconds;

	hours   = value_get_as_float (argv [0]);
	minutes = value_get_as_float (argv [1]);
	seconds = value_get_as_float (argv [2]);

	return (FuncReturn *)value_new_float ((hours * 3600 + minutes * 60 + seconds) / DAY_SECONDS);
}


static char *help_timevalue = {
	N_("@FUNCTION=TIMEVALUE\n"
	   "@SYNTAX=TIMEVALUE (hours,minutes,seconds)\n"

	   "@DESCRIPTION="
	   "Returns a fraction representing the time of day."
	   "\n"

	   ""
	   "@SEEALSO=HOUR")
};

static FuncReturn *
gnumeric_timevalue (FunctionEvalInfo *s)
{
	/* FIXME: is this really right?  */
	return gnumeric_time (s);
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

static FuncReturn *
gnumeric_hour (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	float_t serial = value_get_as_float (argv [0]);
	int secs;

	secs = (int)((serial - floor (serial)) * DAY_SECONDS + 0.5);
	return (FuncReturn *)value_new_int (secs / 3600);
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

static FuncReturn *
gnumeric_minute (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	float_t serial = value_get_as_float (argv [0]);
	int secs;

	secs = (int)((serial - floor (serial)) * DAY_SECONDS + 0.5);
	return (FuncReturn *)value_new_int ((secs / 60) % 60);
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

static FuncReturn *
gnumeric_second (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	float_t serial = value_get_as_float (argv [0]);
	int secs;

	secs = (int)((serial - floor (serial)) * DAY_SECONDS + 0.5);
	return (FuncReturn *)value_new_int (secs % 60);
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

static FuncReturn *
gnumeric_year (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	int res;
	GDate *date = g_date_new_serial (floor (value_get_as_float (argv [0])));
	res = g_date_year (date);
	g_date_free (date);
	return (FuncReturn *)value_new_int (res);
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

static FuncReturn *
gnumeric_month (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	int res;
	GDate *date = g_date_new_serial (floor (value_get_as_float (argv [0])));
	res = g_date_month (date);
	g_date_free (date);
	return (FuncReturn *)value_new_int (res);
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

static FuncReturn *
gnumeric_day (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	int res;
	GDate *date = g_date_new_serial (floor (value_get_as_float (argv [0])));
	res = g_date_day (date);
	g_date_free (date);
	return (FuncReturn *)value_new_int (res);
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

static FuncReturn *
gnumeric_weekday (FunctionEvalInfo *s)
{
	Value **argv = s->a.args;
	int res;
	GDate *date = g_date_new_serial (floor (value_get_as_float (argv [0])));
	res = (g_date_weekday (date) + 1) % 7;
	g_date_free (date);
	return (FuncReturn *)value_new_int (res);
}


static char *help_days360 = {
	N_("@FUNCTION=DAY\n"
	   "@SYNTAX=DAYS360 (serial1,seriel2)\n"

	   "@DESCRIPTION="
	   "FIXME"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string.\n"
	   ""
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
};

static FuncReturn *
gnumeric_days360 (FunctionEvalInfo *s)
{
	s->error_string = _("Unimplemented function");
	return NULL;
}

void
date_functions_init(void)
{
	FunctionCategory *cat = function_get_category (_("Date / Time"));

	function_new (cat,  "date",      "fff",  "year,month,day",        &help_date,
		      FUNCTION_ARGS, gnumeric_date);
	function_new (cat,  "datevalue", "s",    "date_str",              &help_datevalue,
		      FUNCTION_ARGS, gnumeric_datevalue);
	function_new (cat,  "day",       "f",    "serial_number",         &help_day,
		      FUNCTION_ARGS, gnumeric_day);
	function_new (cat,  "days360",   "ff",   "serial1,serial2",       &help_days360,
		      FUNCTION_ARGS, gnumeric_days360);
	function_new (cat,  "edate",     "ff",   "serial_number,months",  &help_edate,
		      FUNCTION_ARGS, gnumeric_edate);
	function_new (cat,  "hour",      "f",    "serial_number",         &help_hour,
		      FUNCTION_ARGS, gnumeric_hour );
	function_new (cat,  "minute",    "f",    "serial_number",         &help_minute,
		      FUNCTION_ARGS, gnumeric_minute );
	function_new (cat,  "month",     "f",    "serial_number",         &help_month,
		      FUNCTION_ARGS, gnumeric_month);
	function_new (cat,  "now",       "",     "",                      &help_now,
		      FUNCTION_ARGS, gnumeric_now );
	function_new (cat,  "second",    "f",    "serial_number",         &help_second,
		      FUNCTION_ARGS, gnumeric_second );
	function_new (cat,  "time",      "fff",  "hours,minutes,seconds", &help_time,
		      FUNCTION_ARGS, gnumeric_time );
	function_new (cat,  "timevalue", "fff",  "hours,minutes,seconds", &help_timevalue,
		      FUNCTION_ARGS, gnumeric_timevalue );
	function_new (cat,  "today",     "",     "",                      &help_today,
		      FUNCTION_ARGS, gnumeric_today );
	function_new (cat,  "weekday",   "f",    "serial_number",         &help_weekday,
		      FUNCTION_ARGS, gnumeric_weekday);
	function_new (cat,  "year",      "f",    "serial_number",         &help_year,
		      FUNCTION_ARGS, gnumeric_year);
}
