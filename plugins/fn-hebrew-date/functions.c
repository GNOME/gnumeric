/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

#include <math.h>

#include <glib.h>
#include "hdate.h"
#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GnmFuncHelp const help_hdate[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE:Hebrew date") },
        { GNM_FUNC_HELP_ARG, F_("year:year of date")},
        { GNM_FUNC_HELP_ARG, F_("month:month of year")},
        { GNM_FUNC_HELP_ARG, F_("day:day of month")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE(2001,3,30)" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_HEB,DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;
	char *res;

	year = value_get_as_int (argv[0]);
	month = value_get_as_int (argv[1]);
	day = value_get_as_int (argv[2]);

	hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear);

	res = g_strdup_printf ("%d %s %d",
			       hday + 1,
			       hdate_get_hebrew_month_name (hmonth),
			       hyear);

	return value_new_string_nocopy (res);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_heb[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_HEB:Hebrew date in Hebrew") },
        { GNM_FUNC_HELP_ARG, F_("year:year of date")},
        { GNM_FUNC_HELP_ARG, F_("month:month of year")},
        { GNM_FUNC_HELP_ARG, F_("day:day of month")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_HEB(2001,3,30)" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE,DATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_heb (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;
	GString *res;

	year = value_get_as_int (argv[0]);
	month = value_get_as_int (argv[1]);
	day = value_get_as_int (argv[2]);

	hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear);

	res = g_string_new (NULL);
	hdate_int_to_hebrew (res, hday + 1);
	g_string_append_c (res, ' ');
	g_string_append (res, hdate_get_hebrew_month_name_heb (hmonth));
	g_string_append_c (res, ' ');
	hdate_int_to_hebrew (res, hyear);

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_month[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_MONTH:Hebrew month of Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_MONTH(2001,3,30)" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_JULIAN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_month (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;

	year = value_get_as_int (argv[0]);
	month = value_get_as_int (argv[1]);
	day = value_get_as_int (argv[2]);

	hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear);

	return value_new_int (hmonth);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_day[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_DAY:Hebrew day of Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_DAY(2001,3,30)" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_JULIAN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_day (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;

	year = value_get_as_int (argv[0]);
	month = value_get_as_int (argv[1]);
	day = value_get_as_int (argv[2]);

	hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear);

	return value_new_int (hday + 1);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_year[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_YEAR:Hebrew year of Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_YEAR(2001,3,30)" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE_JULIAN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_year (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int hyear, hmonth, hday;

	year = value_get_as_int (argv[0]);
	month = value_get_as_int (argv[1]);
	day = value_get_as_int (argv[2]);

	hdate_gdate_to_hdate (day, month, year, &hday, &hmonth, &hyear);

	return value_new_int (hyear);
}

/***************************************************************************/

static GnmFuncHelp const help_hdate_julian[] = {
	{ GNM_FUNC_HELP_NAME, F_("HDATE_JULIAN:Julian day number for given Gregorian date") },
        { GNM_FUNC_HELP_ARG, F_("year:Gregorian year of date")},
        { GNM_FUNC_HELP_ARG, F_("month:Gregorian month of year")},
        { GNM_FUNC_HELP_ARG, F_("day:Gregorian day of month")},
        { GNM_FUNC_HELP_EXAMPLES, "=HDATE_JULIAN(2001,3,30)" },
        { GNM_FUNC_HELP_SEEALSO, "HDATE"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hdate_julian (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	int year, month, day;
	int julian;

	year = value_get_as_int (argv[0]);
	month = value_get_as_int (argv[1]);
	day = value_get_as_int (argv[2]);

	julian = hdate_gdate_to_jd (day, month, year);

	return value_new_int (julian);
}

/***************************************************************************/

GnmFuncDescriptor const datetime_functions[] = {
	{"hdate", "fff", help_hdate,
	 gnumeric_hdate, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{"hdate_heb", "fff", help_hdate_heb,
	 gnumeric_hdate_heb, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_day", "fff", help_hdate_day,
	 gnumeric_hdate_day, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_month", "fff", help_hdate_month,
	 gnumeric_hdate_month, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_year", "fff", help_hdate_year,
	 gnumeric_hdate_year, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	 {"hdate_julian", "fff", help_hdate_julian,
	 gnumeric_hdate_julian, NULL, NULL, NULL, NULL,
	 GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{NULL}
};
