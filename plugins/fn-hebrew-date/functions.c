/*
 * fn-hebrew-date.c:  Built in hebrew date functions.
 *
 * Author:
 *   Yaacov Zamir <kzamir@walla.co.il>
 *
 * Based on Date functions by:
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnm-i18n.h>
#include <func.h>
#include <value.h>

#include <parse-util.h>
#include <cell.h>
#include <value.h>
#include <mathfunc.h>
#include <workbook.h>
#include <sheet.h>
#include <gnm-datetime.h>

#include <math.h>

#include <glib.h>
#include "hdate.h"
#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

#define DATE_CONV(ep)               sheet_date_conv ((ep)->sheet)
#define UNICODE_MONTH_PREFIX "\xd7\x91\xd6\xbc\xd6\xb0"

static void
gnumeric_hdate_get_date (GnmValue const * const *arg, int *year, int *month, int *day)
{
	GDate date;

	if (arg[0] == NULL || arg[1]  == NULL || arg[2] == NULL)
		g_date_set_time_t (&date, time (NULL));

	*year = (arg[0]) ? value_get_as_int (arg[0])
		: g_date_get_year (&date);
	*month = (arg[1]) ? value_get_as_int (arg[1]) :
		(int)g_date_get_month (&date);
	*day = (arg[2]) ? value_get_as_int (arg[2]) :
		g_date_get_day (&date);

	return;
}

static GnmValue *
gnumeric_date_get_date (GnmFuncEvalInfo * ei, GnmValue const * const val,
			int *year, int *month, int *day)
{
	GDate date;

	if (val == NULL)
		g_date_set_time_t (&date, time (NULL));
	else if (!datetime_value_to_g (&date, val, DATE_CONV (ei->pos)))
		return value_new_error_NUM (ei->pos);

	*year = g_date_get_year (&date);
	*month = g_date_get_month (&date);
	*day = g_date_get_day (&date);

	return NULL;
}

/***************************************************************************/

static GnmFuncHelp const help_hdate[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE:Hebrew date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date, defaults to the current year")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year, defaults to the current month")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month, defaults to the current day")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE(2001,3,30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_HEB,DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;
	char *res;

	gnumeric_hdate_get_date (argv, &year, &month, &day);

	if (0 != hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear))
		return value_new_error_VALUE (ei->pos);

	res = g_strdup_printf ("%d %s %d",
			       hday + 1,
			       hdate_get_hebrew_month_name (hmonth),
			       hyear);

	return value_new_string_nocopy (res);
}

/***************************************************************************/



static GnmFuncHelp const help_date2hdate[] = {
	{ GNM_FUNC_HELP_NAME, F_("DATE2HDATE:Hebrew date") },
        { GNM_FUNC_HELP_ARG, F_("date:Gregorian date, defaults to today")},
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2HDATE(DATE(2001,3,30))" },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2HDATE()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE,DATE2HDATE_HEB"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_date2hdate (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;
	char *res;
	GnmValue *val;

	val = gnumeric_date_get_date (ei,argv[0], &year, &month, &day);
	if (val != NULL)
		return val;

	if (0 != hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear))
		return value_new_error_VALUE (ei->pos);

	res = g_strdup_printf ("%d %s %d",
			       hday + 1,
			       hdate_get_hebrew_month_name (hmonth),
			       hyear);

	return value_new_string_nocopy (res);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_heb[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_HEB:Hebrew date in Hebrew") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date, defaults to the current year")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year, defaults to the current month")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month, defaults to the current day")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_HEB(2001,3,30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_HEB()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE,DATE"},
	{ GNM_FUNC_HELP_END }
};

static void
build_hdate (GString *res, int hyear, int hmonth, int hday)
{
	hdate_int_to_hebrew (res, hday + 1);
	g_string_append (res, " " UNICODE_MONTH_PREFIX);
	g_string_append (res, hdate_get_hebrew_month_name_heb (hmonth));
	g_string_append_c (res, ' ');
	hdate_int_to_hebrew (res, hyear);
}

static GnmValue *
gnumeric_hdate_heb (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;
	GString *res;

	gnumeric_hdate_get_date (argv, &year, &month, &day);

	if (0 != hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear))
		return value_new_error_VALUE (ei->pos);

	res = g_string_new (NULL);
	build_hdate (res, hyear, hmonth, hday);

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_date2hdate_heb[] = {
	{ GNM_FUNC_HELP_NAME, F_("DATE2HDATE_HEB:Hebrew date in Hebrew") },
        { GNM_FUNC_HELP_ARG, F_("date:Gregorian date, defaults to today")},
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2HDATE_HEB(DATE(2001,3,30))" },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2HDATE_HEB()" },
        { GNM_FUNC_HELP_SEEALSO, "DATE2HDATE,HDATE_HEB"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_date2hdate_heb (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;
	GString *res;
	GnmValue *val;

	val = gnumeric_date_get_date (ei,argv[0], &year, &month, &day);
	if (val != NULL)
		return val;

	if (0 != hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear))
		return value_new_error_VALUE (ei->pos);

	res = g_string_new (NULL);
	build_hdate (res, hyear, hmonth, hday);

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_month[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_MONTH:Hebrew month of Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date, defaults to the current year")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year, defaults to the current month")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month, defaults to the current day")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_MONTH(2001,3,30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_MONTH()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_JULIAN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_month (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;

	gnumeric_hdate_get_date (argv, &year, &month, &day);

	if (0 != hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear))
		return value_new_error_VALUE (ei->pos);

	return value_new_int (hmonth);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_day[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_DAY:Hebrew day of Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date, defaults to the current year")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year, defaults to the current month")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month, defaults to the current day")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_DAY(2001,3,30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_DAY()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_JULIAN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_day (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;

	gnumeric_hdate_get_date (argv, &year, &month, &day);

	if (0 != hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear))
		return value_new_error_VALUE (ei->pos);

	return value_new_int (hday + 1);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_year[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_YEAR:Hebrew year of Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date, defaults to the current year")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year, defaults to the current month")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month, defaults to the current day")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_YEAR(2001,3,30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_YEAR()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_JULIAN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_year (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;

	gnumeric_hdate_get_date (argv, &year, &month, &day);

	if (0 != hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear))
		return value_new_error_VALUE (ei->pos);

	return value_new_int (hyear);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_julian[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_JULIAN:Julian day number for given Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date, defaults to the current year")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year, defaults to the current month")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month, defaults to the current day")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_JULIAN(2001,3,30)" },
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_JULIAN()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_julian (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int julian;

	gnumeric_hdate_get_date (argv, &year, &month, &day);

	julian = hdate_gdate_to_jd (day, month, year);

	return value_new_int (julian);
}

/***************************************************************************/

static GnmFuncHelp const help_date2julian[] = {
	{ GNM_FUNC_HELP_NAME, F_("DATE2JULIAN:Julian day number for given Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("date:Gregorian date, defaults to today")},
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2JULIAN(DATE(2001,3,30))" },
        { GNM_FUNC_HELP_EXAMPLES, "=DATE2JULIAN()" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_JULIAN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_date2julian (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int julian;
	GnmValue *val;

	val = gnumeric_date_get_date (ei, argv[0], &year, &month, &day);
	if (val != NULL)
		return val;

	julian = hdate_gdate_to_jd (day, month, year);

	return value_new_int (julian);
}

/***************************************************************************/

GnmFuncDescriptor const hebrew_datetime_functions[] = {
	{"hdate", "|fff", help_hdate,
	 gnumeric_hdate, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{"hdate_heb", "|fff", help_hdate_heb,
	 gnumeric_hdate_heb, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_day", "|fff", help_hdate_day,
	 gnumeric_hdate_day, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_month", "|fff", help_hdate_month,
	 gnumeric_hdate_month, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_year", "|fff", help_hdate_year,
	 gnumeric_hdate_year, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_julian", "|fff", help_hdate_julian,
	 gnumeric_hdate_julian, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"date2hdate", "|f", help_date2hdate,
	 gnumeric_date2hdate, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"date2hdate_heb", "|f", help_date2hdate_heb,
	 gnumeric_date2hdate_heb, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"date2julian", "|f", help_date2julian,
	 gnumeric_date2julian, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{NULL}
};
