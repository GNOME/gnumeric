/*
 * fn-christian-date.c: Christian date functions.
 *
 * Author:
 *   Andreas J. Guelzow <aguelzow@pyrshep.ca>
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

#include <math.h>
#include <gnm-datetime.h>

#include <glib.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

#define DATE_CONV(ep)		sheet_date_conv ((ep)->sheet)

static void
eastersunday_calc_for_year (int year, GDate *date)
{
	int month;
	int day;
	int century, n, k, i, j, l;

	century = year/100;
	n = year - 19 * (year / 19);
	k = (century - 17) / 25;
	i = century - century / 4 - (century - k) / 3 + 19 * n + 15;
	i %= 30;
	i = i - (i / 28) * (1 - (i / 28) * (29 / (i+1)) * ((21 - n) / 11 ));
	j = year + year / 4 + i + 2 - century + century / 4;
	j %= 7;
	l = i - j;
	month = 3 + (l + 40) / 44;
	day = l + 28 - 31 * (month / 4);

	g_date_clear (date, 1);
	g_date_set_dmy (date, day, month, year);
}

static void
eastersunday_calc_no_year (GDate *date, GODateConventions const *conv, int diff)
{
	int year, serial;
	int today = go_date_timet_to_serial (time (NULL), conv);

	go_date_serial_to_g (date, today, conv);
	year = g_date_get_year (date);
	eastersunday_calc_for_year (year, date);
	serial = go_date_g_to_serial (date, conv) + diff;
	if (serial < today)
		eastersunday_calc_for_year (year + 1, date);
}

static int
adjust_year (int year, GODateConventions const *conv)
{
	if (year < 0)
		return -1;
	else if (year <= 29)
		return 2000 + year;
	else if (year <= 99)
		return 1900 + year;
	else if (year < (gnm_datetime_allow_negative () ? 1582
			 : go_date_convention_base (conv)))
		return -1;
	else if (year > 9956)
		return -1;
	else
		return year;
}

static GnmValue *
eastersunday_calc (GnmValue const *val, GnmFuncEvalInfo *ei, int diff)
{
	GODateConventions const *conv = DATE_CONV (ei->pos);
	GDate date;
	int serial;

	if (val) {
		int year = adjust_year (value_get_as_int (val), conv);

		if (year < 0)
			return value_new_error_NUM (ei->pos);

		eastersunday_calc_for_year (year, &date);
	} else
		eastersunday_calc_no_year (&date, conv, diff);

	serial = go_date_g_to_serial (&date, conv) + diff;

	if (diff < 0 &&
	    serial > 0 && serial <= 60 &&
	    go_date_convention_base (conv) == 1900) {
		/* We crossed the 29-Feb-1900 hole in the 1900 method.  */
		serial--;
	}

	return value_new_int (serial);
}

/***************************************************************************/

static GnmFuncHelp const help_eastersunday[] = {
	{ GNM_FUNC_HELP_NAME, F_("EASTERSUNDAY:Easter Sunday in the Gregorian calendar "
				 "according to the Roman rite of the Christian Church") },
        { GNM_FUNC_HELP_ARG, F_("year:year between 1582 and 9956, defaults to the year of the next Easter Sunday")},
        { GNM_FUNC_HELP_NOTE, F_("Two digit years are adjusted as elsewhere in Gnumeric. Dates before 1904 may also be prohibited.")},
        { GNM_FUNC_HELP_EXAMPLES, "=EASTERSUNDAY(2001)" },
        { GNM_FUNC_HELP_EXAMPLES, "=EASTERSUNDAY()" },
	{ GNM_FUNC_HELP_ODF, F_("The 1-argument version of EASTERSUNDAY is compatible with OpenOffice "
				"for years after 1904. "
				"This function is not specified in ODF/OpenFormula.")},
        { GNM_FUNC_HELP_SEEALSO, "ASHWEDNESDAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_eastersunday (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	return eastersunday_calc (argv[0], ei, 0);
}


/***************************************************************************/

static GnmFuncHelp const help_ashwednesday[] = {
	{ GNM_FUNC_HELP_NAME, F_("ASHWEDNESDAY:Ash Wednesday in the Gregorian calendar "
				 "according to the Roman rite of the Christian Church") },
        { GNM_FUNC_HELP_ARG, F_("year:year between 1582 and 9956, defaults to the year of the next Ash Wednesday")},
        { GNM_FUNC_HELP_NOTE, F_("Two digit years are adjusted as elsewhere in Gnumeric. Dates before 1904 may also be prohibited.")},
        { GNM_FUNC_HELP_EXAMPLES, "=ASHWEDNESDAY(2001)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ASHWEDNESDAY()" },
        { GNM_FUNC_HELP_SEEALSO, "EASTERSUNDAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ashwednesday (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	return eastersunday_calc (argv[0], ei, -46);
}


/***************************************************************************/

static GnmFuncHelp const help_pentecostsunday[] = {
	{ GNM_FUNC_HELP_NAME, F_("PENTECOSTSUNDAY:Pentecost Sunday in the Gregorian calendar "
				 "according to the Roman rite of the Christian Church") },
        { GNM_FUNC_HELP_ARG, F_("year:year between 1582 and 9956, defaults to the year of the next Pentecost Sunday")},
        { GNM_FUNC_HELP_NOTE, F_("Two digit years are adjusted as elsewhere in Gnumeric. Dates before 1904 may also be prohibited.")},
        { GNM_FUNC_HELP_EXAMPLES, "=PENTECOSTSUNDAY(2001)" },
        { GNM_FUNC_HELP_EXAMPLES, "=PENTECOSTSUNDAY()" },
        { GNM_FUNC_HELP_SEEALSO, "EASTERSUNDAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pentecostsunday (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	return eastersunday_calc (argv[0], ei, +49);
}

/***************************************************************************/

static GnmFuncHelp const help_goodfriday[] = {
	{ GNM_FUNC_HELP_NAME, F_("GOODFRIDAY:Good Friday in the Gregorian calendar "
				 "according to the Roman rite of the Christian Church") },
        { GNM_FUNC_HELP_ARG, F_("year:year between 1582 and 9956, defaults to the year of the next Good Friday")},
        { GNM_FUNC_HELP_NOTE, F_("Two digit years are adjusted as elsewhere in Gnumeric. Dates before 1904 may also be prohibited.")},
        { GNM_FUNC_HELP_EXAMPLES, "=GOODFRIDAY(2001)" },
        { GNM_FUNC_HELP_EXAMPLES, "=GOODFRIDAY()" },
        { GNM_FUNC_HELP_SEEALSO, "EASTERSUNDAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_goodfriday (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	return eastersunday_calc (argv[0], ei, -2);
}

/***************************************************************************/

static GnmFuncHelp const help_ascensionthursday[] = {
	{ GNM_FUNC_HELP_NAME, F_("ASCENSIONTHURSDAY:Ascension Thursday in the Gregorian calendar "
				 "according to the Roman rite of the Christian Church") },
        { GNM_FUNC_HELP_ARG, F_("year:year between 1582 and 9956, defaults to the year of the next Ascension Thursday")},
        { GNM_FUNC_HELP_NOTE, F_("Two digit years are adjusted as elsewhere in Gnumeric. Dates before 1904 may also be prohibited.")},
        { GNM_FUNC_HELP_EXAMPLES, "=ASCENSIONTHURSDAY(2001)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ASCENSIONTHURSDAY()" },
        { GNM_FUNC_HELP_SEEALSO, "EASTERSUNDAY"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ascensionthursday (GnmFuncEvalInfo * ei, GnmValue const * const *argv)
{
	return eastersunday_calc (argv[0], ei, +39);
}

/***************************************************************************/

GnmFuncDescriptor const christian_datetime_functions[] = {
	{"ascensionthursday", "|f", help_ascensionthursday,
	 gnumeric_ascensionthursday, NULL,
	 GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_DATE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{"ashwednesday", "|f", help_ashwednesday,
	 gnumeric_ashwednesday, NULL,
	 GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_DATE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{"eastersunday", "|f", help_eastersunday,
	 gnumeric_eastersunday, NULL,
	 GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_DATE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{"goodfriday", "|f", help_goodfriday,
	 gnumeric_goodfriday, NULL,
	 GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_DATE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{"pentecostsunday", "|f", help_pentecostsunday,
	 gnumeric_pentecostsunday, NULL,
	 GNM_FUNC_VOLATILE + GNM_FUNC_AUTO_DATE,
	 GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	 GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{NULL}
};
