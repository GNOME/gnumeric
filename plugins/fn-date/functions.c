/*
 * fn-date.c:  Built in date functions.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
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
	   "to the previous month.  The years should be at least 1900"
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
	
	year  = value_get_as_double (argv [0]);
	month = value_get_as_double (argv [1]);
	day   = value_get_as_double (argv [2]);

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

	v = value_int (g_date_serial (&date));

	return v;
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
	Value *v;

	GDate date;

        g_date_clear(&date, 1);
	
	g_date_set_time (&date, time (NULL));
	
	v = value_int (g_date_serial (&date));

	return v;
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
	Value *v;
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);

	GDate date;
        g_date_clear(&date, 1);
	
	g_date_set_time (&date, t);

	v = value_float (g_date_serial(&date) +
			 ((tm->tm_hour * 3600 + tm->tm_min * 60 
			   + tm->tm_sec)/(double)DAY_SECONDS));

	return v;
}

static char *help_time = {
	N_("@FUNCTION=TIME\n"
	   "@SYNTAX=TIME (hours,minutes,seconds)\n"

	   "@DESCRIPTION="
	   "Returns a fraction representing the hour"
	   "\n"
	   
	   ""
	   "@SEEALSO=HOUR")
};

static Value *
gnumeric_time (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v;
	float_t hours, minutes, seconds;

	hours   = value_get_as_double (argv [0]);
	minutes = value_get_as_double (argv [1]);
	seconds = value_get_as_double (argv [2]);

	v = value_float ((hours * 3600 + minutes * 60 + seconds) / DAY_SECONDS);

	return v;
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
	   "string\n"
	   ""
	   "@SEEALSO=MINUTE, NOW, TIME, SECOND")
};

static char *help_minute = {
	N_("@FUNCTION=MINUTE\n"
	   "@SYNTAX=MINUTE (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a minute.  The minute is returned as "
	   "an integer in the range 0 to 59"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string\n"
	   ""
	   "@SEEALSO=HOUR, NOW, TIME, SECOND")
};

static char *help_second = {
	N_("@FUNCTION=SECOND\n"
	   "@SYNTAX=SECOND (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a second.  The second is returned as "
	   "an integer in the range 0 to 59"
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string\n"
	   ""
	   "@SEEALSO=HOUR, MINUTE, NOW, TIME")
};

/*
 * Handles requests for HOUR, MINUTE and SECOND functions
 */
static Value *
gnumeric_hour_min_sec (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	float_t serial;

	serial   = value_get_as_double (argv [0]);
	serial = serial - floor (serial);
	
	v->type = VALUE_INTEGER;

	if (fd->name [0] == 'h')
		v->v.v_int = (serial * DAY_SECONDS) / 3600;
	else if (fd->name [0] == 'm')
		v->v.v_int = (((int)(serial * DAY_SECONDS)) % 3600) / 60;
	else
		v->v.v_int = (((int)(serial * DAY_SECONDS)) % 3600) % 60;
	return v;
}

static char *help_year = {
	N_("@FUNCTION=YEAR\n"
	   "@SYNTAX=YEAR (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a year."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string\n"
	   ""
	   "@SEEALSO=DAY, MONTH, TIME, NOW")
};

static char *help_month = {
	N_("@FUNCTION=MONTH\n"
	   "@SYNTAX=MONTH (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a month."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string\n"
	   ""
	   "@SEEALSO=DAY, TIME, NOW, YEAR")
};

static char *help_day = {
	N_("@FUNCTION=DAY\n"
	   "@SYNTAX=DAY (serial_number)\n"

	   "@DESCRIPTION="
	   "Converts a serial number to a day."
	   "\n"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string\n"
	   ""
	   "@SEEALSO=MONTH, TIME, NOW, YEAR")
};

static Value *
gnumeric_year_month_day (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	guint32 serial = floor (value_get_as_double (argv [0]));
	GDate* date = g_date_new_serial (serial);

	v->type = VALUE_INTEGER;
	
	if (fd->name [0] == 'y')
		v->v.v_int = g_date_year (date);
	else if (fd->name [0] == 'm')
		v->v.v_int = g_date_month (date);
	else if (fd->name [0] == 'd')
		v->v.v_int = g_date_day (date);

	g_date_free (date);

	return v;
}

FunctionDefinition date_functions [] = {
	{ "date",    "fff",  "year,month,day", &help_date,    NULL, gnumeric_date  },
	{ "day",     "f",    "serial_number",  &help_day,     NULL, gnumeric_year_month_day  },
	{ "hour",    "f",    "serial_number",  &help_hour,    NULL, gnumeric_hour_min_sec },
	{ "minute",  "f",    "serial_number",  &help_minute,  NULL, gnumeric_hour_min_sec },
	{ "month",   "f",    "serial_number",  &help_month,   NULL, gnumeric_year_month_day  },
	{ "now",     "",     "",               &help_now,     NULL, gnumeric_now },
	{ "second",  "f",    "serial_number",  &help_second,  NULL, gnumeric_hour_min_sec },
	{ "time",    "fff",  "hours,minutes,seconds", &help_time, NULL, gnumeric_time },
	{ "today",   "",     "",               &help_today,   NULL, gnumeric_today },
	{ "year",    "f",    "serial_number",  &help_year,    NULL, gnumeric_year_month_day  },
	{ NULL, NULL },
};





