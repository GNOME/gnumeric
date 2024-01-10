/*
 * fn-flt:  Floating point functions
 *
 * Authors:
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
#include <gnm-i18n.h>
#include <value.h>

#include <math.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GnmFuncHelp const help_flt_radix[] = {
        { GNM_FUNC_HELP_NAME, F_("FLT.RADIX:return the floating point system's base")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The FLT.RADIX function returns the base of the floating point number system in use.") },
        { GNM_FUNC_HELP_EXAMPLES, "=FLT.RADIX()" },
        { GNM_FUNC_HELP_SEEALSO, "FLT.NEXTAFTER"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_flt_radix (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_int (GNM_RADIX);
}

/***************************************************************************/

static GnmFuncHelp const help_flt_nextafter[] = {
        { GNM_FUNC_HELP_NAME, F_("FLT.NEXTAFTER:next representable value")},
        { GNM_FUNC_HELP_ARG, F_("x:floating point value")},
        { GNM_FUNC_HELP_ARG, F_("y:direction")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The FLT.NEXTAFTER function returns the next floating point value after @{x} in the direction of @{y}.  \"+\" and \"-\" are special values for @{y} that represent positive and negative infinity.") },
        { GNM_FUNC_HELP_EXAMPLES, "=FLT.NEXTAFTER(1,\"+\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=FLT.NEXTAFTER(1,0)" },
        { GNM_FUNC_HELP_SEEALSO, "FLT.RADIX"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_flt_nextafter (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	GnmValue const *yval = argv[1];
	gnm_float y;

	if (VALUE_IS_STRING (yval) && g_str_equal (value_peek_string (yval), "+"))
		y = gnm_pinf;
	else if (VALUE_IS_STRING (yval) && g_str_equal (value_peek_string (yval), "-"))
		y = gnm_ninf;
	else if (VALUE_IS_NUMBER (yval))
		y = value_get_as_float (yval);
	else
		return value_new_error_VALUE (ei->pos);

	return value_new_float (nextafter (x, y));
}

/***************************************************************************/

GnmFuncDescriptor const flt_functions[] = {
	{ "flt.nextafter", "fS", help_flt_nextafter,
	  gnumeric_flt_nextafter, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "flt.radix", "", help_flt_radix,
	  gnumeric_flt_radix, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        {NULL}
};
