/*
 * fn-date.c:  Built in date functions.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <libgnome/lib_date.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

#define DAY_SECONDS (3600*24)

static char *help_date = {
	N_("<function>DATE</function>"
	   "<syntax>DATE (year,month,day)</syntax>n"

	   "<description>"
	   "Computes the number of days since the 1st of jannuary of 1900"
	   "(the date serial number) for the given year, month and day.<p>"

	   "The day might be negative (to count backwards) and it is relative"
	   "to the previous month"
	   "<p>"
	   
	   "</description>"
	   "<seealso>TODAY, NOW</seealso>")
};

static Value *
gnumeric_date (struct FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	int year, month, day;

	year  = 1900 + value_get_as_double (argv [0]);
	month = value_get_as_double (argv [1]);
	day   = value_get_as_double (argv [2]);

	/* Count backwars in the month */
	if (day < 0){
		month--;
		if (month < 1){
			year--;
			month = 12;
		}
		day = month_length [leap (year)][month] - day;
	}
	v->type = VALUE_INTEGER;
	v->v.v_int = calc_days (year, month, day);

	return v;
}

static char *help_today = {
	N_("<function>TODAY</function>"
	   "<syntax>TODAY ()</syntax>n"

	   "<description>"
	   "Returns the serial number for today (the number of days"
	   "trasncurred since the 1st of Jannuary of 1990"
	   "<p>"
	   
	   "</description>"
	   "<seealso>TODAY, NOW</seealso>")
};

static Value *
gnumeric_today (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);

	v->type = VALUE_INTEGER;
	v->v.v_int = calc_days (tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

	return v;
}

static char *help_now = {
	N_("<function>NOW</function>"
	   "<syntax>NOW ()</syntax>n"

	   "<description>"
	   "Returns the serial number for the date and time at the time"
	   "it is evaluated.<p>"
	   ""
	   "Serial Numbers in Gnumeric are represented as follows:"
	   "The integral part is the number of days since the 1st of "
	   "Jannuary of 1900.  The decimal part represent the fraction "
	   "of the day and is mapped into hour, minutes and seconds<p>"
	   ""
	   "For example: .0 represents the beginning of the day, and 0.5 "
	   "represents noon"
	   "<p>"
	   
	   "</description>"
	   "<seealso>TODAY, NOW</seealso>")
};

static Value *
gnumeric_now (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	time_t t = time (NULL);
	struct tm *tm = localtime (&t);

	v->type = VALUE_FLOAT;
	v->v.v_float =
		calc_days (tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday) +
		((tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec)/DAY_SECONDS) -
		calc_days (1900, 1, 1);

	return v;
}

static char *help_time = {
	N_("<function>TIME</function>"
	   "<syntax>TIME (hours,minutes,seconds)</syntax>n"

	   "<description>"
	   "Returns a fraction representing the hour"
	   "<p>"
	   
	   "</description>"
	   "<seealso>HOUR</seealso>")
};

static Value *
gnumeric_time (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	float_t hours, minutes, seconds;

	hours   = value_get_as_double (argv [0]);
	minutes = value_get_as_double (argv [1]);
	seconds = value_get_as_double (argv [2]);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = (hours * 3600 + minutes * 60 + seconds) / DAY_SECONDS;

	return v;
}

static char *help_hour = {
	N_("<function>HOUR</function>"
	   "<syntax>HOUR (serial_number)</syntax>n"

	   "<description>"
	   "Converts a serial number to an hour.  The hour is returned as "
	   "an integer in the range 0 (12:00 A.M.) to 23 (11:00 P.M.)"
	   "<p>"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string"
	   "</description>"
	   "<seealso>MINUTE, NOW, TIME, SECOND</seealso>")
};

static char *help_minute = {
	N_("<function>MINUTE</function>"
	   "<syntax>MINUTE (serial_number)</syntax>n"

	   "<description>"
	   "Converts a serial number to a minute.  The minute is returned as "
	   "an integer in the range 0 to 59"
	   "<p>"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string"
	   "</description>"
	   "<seealso>HOUR, NOW, TIME, SECOND</seealso>")
};

static char *help_second = {
	N_("<function>SECOND</function>"
	   "<syntax>SECOND (serial_number)</syntax>n"

	   "<description>"
	   "Converts a serial number to a second.  The second is returned as "
	   "an integer in the range 0 to 59"
	   "<p>"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string"
	   "</description>"
	   "<seealso>HOUR, MINUTE, NOW, TIME</seealso>")
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
	N_("<function>YEAR</function>"
	   "<syntax>YEAR (serial_number)</syntax>n"

	   "<description>"
	   "Converts a serial number to a year."
	   "<p>"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string"
	   "</description>"
	   "<seealso>DAY, MONTH, TIME, NOW</seealso>")
};

static char *help_month = {
	N_("<function>MONTH</function>"
	   "<syntax>MONTH (serial_number)</syntax>n"

	   "<description>"
	   "Converts a serial number to a month."
	   "<p>"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string"
	   "</description>"
	   "<seealso>DAY, TIME, NOW, YEAR</seealso>")
};

static char *help_day = {
	N_("<function>DAY</function>"
	   "<syntax>DARY (serial_number)</syntax>n"

	   "<description>"
	   "Converts a serial number to a day."
	   "<p>"
	   "Note that Gnumeric will perform regular string to serial "
	   "number conversion for you, so you can enter a date as a "
	   "string"
	   "</description>"
	   "<seealso>MONTH, TIME, NOW, YEAR</seealso>")
};

static Value *
gnumeric_year_month_day (FunctionDefinition *fd, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	int serial;
	int year  = 1900;
	int month = 1;
	int day   = 1;
	
	serial   = floor (value_get_as_double (argv [0]));

	v->type = VALUE_INTEGER;
	calc_new_date (&year, &month, &day, floor (serial));
	
	if (fd->name [0] == 'y')
		v->v.v_int = year;
	else if (fd->name [0] == 'm')
		v->v.v_int = month;
	else if (fd->name [0] == 'd')
		v->v.v_int = day;

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





