/*
 * fn-eng.c:  Built in engineering functions and functions registration
 *
 * Authors:
 *   Michael Meeks <michael@ximian.com>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
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

#include <parse-util.h>
#include <value.h>
#include <mathfunc.h>
#include <sf-bessel.h>
#include <sf-dpq.h>
#include <collect.h>
#include <number-match.h>
#include <workbook.h>
#include <sheet.h>

#include <goffice/goffice.h>
#include <gsf/gsf-utils.h>
#include <gnm-plugin.h>

#include <math.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

GNM_PLUGIN_MODULE_HEADER;


typedef enum {
	V2B_STRINGS_GENERAL = 1,        /* Allow "1/1/2000" as number.  */
	V2B_STRINGS_0XH = 2,            /* Allow "4444h" and "0xABCD".  */
	V2B_STRINGS_MAXLEN = 4,         /* Impose 10 character input length.  */
	V2B_STRINGS_BLANK_ZERO = 8,     /* Treat "" as "0".  */
	V2B_NUMBER = 16,                /* Wants a number, not a string.  */
	V2B_KILLME
} Val2BaseFlags;



/**
 * FIXME: In the long term this needs optimising.
 **/
static GnmValue *
val_to_base (GnmFuncEvalInfo *ei,
	     GnmValue const *value,
	     GnmValue const *aplaces,
	     int src_base, int dest_base,
	     gnm_float min_value, gnm_float max_value,
	     Val2BaseFlags flags)
{
	int digit, min, max, places;
	gnm_float v;
	GString *buffer;
	GnmValue *vstring = NULL;

	g_return_val_if_fail (src_base > 1 && src_base <= 36,
			      value_new_error_VALUE (ei->pos));
	g_return_val_if_fail (dest_base > 1 && dest_base <= 36,
			      value_new_error_VALUE (ei->pos));

	/* func.c ought to take care of this.  */
	if (VALUE_IS_BOOLEAN (value))
		return value_new_error_VALUE (ei->pos);
	if (aplaces && VALUE_IS_BOOLEAN (aplaces))
		return value_new_error_VALUE (ei->pos);

	switch (value->v_any.type) {
	default:
		return value_new_error_NUM (ei->pos);

	case VALUE_STRING:
		if (flags & V2B_STRINGS_GENERAL) {
			vstring = format_match_number
				(value_peek_string (value), NULL,
				 sheet_date_conv (ei->pos->sheet));
			if (!vstring || !VALUE_IS_FLOAT (vstring)) {
				value_release (vstring);
				return value_new_error_VALUE (ei->pos);
			}
		} else {
			char const *str = value_peek_string (value);
			size_t len;
			gboolean hsuffix = FALSE;
			char *err;

			if ((flags & V2B_STRINGS_BLANK_ZERO) && *str == 0)
				str = "0";

			/* This prevents leading spaces, signs, etc, and "".  */
			if (!g_ascii_isalnum (*str))
				return value_new_error_NUM (ei->pos);

			len = strlen (str);
			/* We check length in bytes.  Since we are going to
			   require nothing but digits, that is fine.  */
			if ((flags & V2B_STRINGS_MAXLEN) && len > 10)
				return value_new_error_NUM (ei->pos);

			if (flags & V2B_STRINGS_0XH) {
				if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
					str += 2;
				else if (str[len - 1] == 'h' || str[len - 1] == 'H')
					hsuffix = TRUE;
			}

			v = g_ascii_strtoll (str, &err, src_base);
			if (err == str || err[hsuffix] != 0)
				return value_new_error_NUM (ei->pos);

			if (v < min_value || v > max_value)
				return value_new_error_NUM (ei->pos);

			break;
		}
		/* Fall through.  */

	case VALUE_FLOAT: {
		gnm_float val = gnm_fake_trunc (value_get_as_float (vstring ? vstring : value));
		char *buf;
		char *err;
		gboolean fail;

		value_release (vstring);

		if (val < min_value || val > max_value)
			return value_new_error_NUM (ei->pos);

		buf = g_strdup_printf ("%.0" GNM_FORMAT_f, val);
		v = g_ascii_strtoll (buf, &err, src_base);
		fail = (*err != 0);
		g_free (buf);

		if (fail)
			return value_new_error_NUM (ei->pos);
		break;
	}
	}

	if (src_base != 10) {
		gnm_float b10 = gnm_pow (src_base, 10);
		if (v >= b10 / 2) /* N's complement */
			v = v - b10;
	}

	if (flags & V2B_NUMBER)
		return value_new_float (v);

	if (v < 0) {
		min = 1;
		max = 10;
		v += gnm_pow (dest_base, max);
	} else {
		if (v == 0)
			min = max = 1;
		else
			min = max = (int)(gnm_log (v + GNM_const(0.5)) /
					  gnm_log (dest_base)) + 1;
	}

	if (aplaces) {
		gnm_float fplaces = value_get_as_float (aplaces);
		if (fplaces < min || fplaces > 10)
			return value_new_error_NUM (ei->pos);
		places = (int)fplaces;
		if (v >= 0 && places > max)
			max = places;
	} else
		places = 1;

	buffer = g_string_sized_new (max);
	g_string_set_size (buffer, max);

	for (digit = max - 1; digit >= 0; digit--) {
		int thisdigit = gnm_fmod (v + GNM_const(0.5), dest_base);
		v = gnm_floor ((v + GNM_const(0.5)) / dest_base);
		buffer->str[digit] =
			thisdigit["0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"];
	}

	return value_new_string_nocopy (g_string_free (buffer, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_base[] = {
        { GNM_FUNC_HELP_NAME, F_("BASE:string of digits representing the number @{n} in base @{b}") },
        { GNM_FUNC_HELP_ARG, F_("n:integer") },
        { GNM_FUNC_HELP_ARG, F_("b:base (2 \xe2\x89\xa4 @{b} \xe2\x89\xa4 36)") },
        { GNM_FUNC_HELP_ARG, F_("length:minimum length of the resulting string") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("BASE converts @{n} to its string representation in base @{b}. "
					"Leading zeroes will be added to reach the minimum length given by @{length}.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BASE(255,16,4)" },
        { GNM_FUNC_HELP_SEEALSO, "DECIMAL" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_base (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	static const gnm_float max = 1 / GNM_EPSILON;
	gnm_float base = value_get_as_float (argv[1]);

	if (base < 2 || base >= 37)
		return value_new_error_NUM (ei->pos);

	return val_to_base (ei, argv[0], argv[2], 10, (int)base,
			    -max, +max,
			    V2B_STRINGS_GENERAL | V2B_STRINGS_0XH);
}

/***************************************************************************/

static GnmFuncHelp const help_bin2dec[] = {
        { GNM_FUNC_HELP_NAME, F_("BIN2DEC:decimal representation of the binary number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a binary number, either as a string or as a number involving only the digits 0 and 1") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BIN2DEC(101)" },
        { GNM_FUNC_HELP_SEEALSO, "DEC2BIN,BIN2OCT,BIN2HEX,BASE" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_bin2dec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], NULL,
			    2, 10,
			    0, GNM_const(1111111111.0),
			    V2B_STRINGS_MAXLEN |
			    V2B_STRINGS_BLANK_ZERO |
			    V2B_NUMBER);
}

/***************************************************************************/

static GnmFuncHelp const help_bin2oct[] = {
        { GNM_FUNC_HELP_NAME, F_("BIN2OCT:octal representation of the binary number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a binary number, either as a string or as a number involving only the digits 0 and 1") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, BIN2OCT pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, BIN2OCT returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BIN2OCT(110111)" },
        { GNM_FUNC_HELP_EXAMPLES, "=BIN2OCT(110111,4)" },
        { GNM_FUNC_HELP_SEEALSO, ("OCT2BIN,BIN2DEC,BIN2HEX,BASE") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_bin2oct (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    2, 8,
			    0, GNM_const(1111111111.0),
			    V2B_STRINGS_MAXLEN | V2B_STRINGS_BLANK_ZERO);
}

/***************************************************************************/

static GnmFuncHelp const help_bin2hex[] = {
        { GNM_FUNC_HELP_NAME, F_("BIN2HEX:hexadecimal representation of the binary number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a binary number, either as a string or as a number involving only the digits 0 and 1") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, BIN2HEX pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, BIN2HEX returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BIN2HEX(100111)" },
        { GNM_FUNC_HELP_EXAMPLES, "=BIN2HEX(110111,4)" },
        { GNM_FUNC_HELP_SEEALSO, ("HEX2BIN,BIN2OCT,BIN2DEC,BASE") },
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_bin2hex (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    2, 16,
			    0, GNM_const(1111111111.0),
			    V2B_STRINGS_MAXLEN | V2B_STRINGS_BLANK_ZERO);
}

/***************************************************************************/

static GnmFuncHelp const help_dec2bin[] = {
        { GNM_FUNC_HELP_NAME, F_("DEC2BIN:binary representation of the decimal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:integer (\xe2\x88\x92 513 < @{x} < 512)") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given and @{x} is non-negative, "
					"DEC2BIN pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, "
					"DEC2BIN returns #NUM!")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given and @{x} is negative, @{places} is ignored.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < \xe2\x88\x92 512 or @{x} > 511, DEC2BIN returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DEC2BIN(42,6)" },
        { GNM_FUNC_HELP_EXAMPLES, "=DEC2BIN(-42,6)" },
        { GNM_FUNC_HELP_SEEALSO, ("BIN2DEC,DEC2OCT,DEC2HEX,BASE") },
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_dec2bin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    10, 2,
			    -512, 511,
			    V2B_STRINGS_GENERAL);
}

/***************************************************************************/

static GnmFuncHelp const help_dec2oct[] = {
        { GNM_FUNC_HELP_NAME, F_("DEC2OCT:octal representation of the decimal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:integer") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, DEC2OCT pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, DEC2OCT returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DEC2OCT(42)" },
        { GNM_FUNC_HELP_SEEALSO, ("OCT2DEC,DEC2BIN,DEC2HEX,BASE") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_dec2oct (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    10, 8,
			    -536870912, 536870911,
			    V2B_STRINGS_GENERAL);
}

/***************************************************************************/

static GnmFuncHelp const help_dec2hex[] = {
        { GNM_FUNC_HELP_NAME, F_("DEC2HEX:hexadecimal representation of the decimal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:integer") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, DEC2HEX pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, DEC2HEX returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DEC2HEX(42)" },
        { GNM_FUNC_HELP_SEEALSO, ("HEX2DEC,DEC2BIN,DEC2OCT,BASE") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_dec2hex (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    10, 16,
			    GNM_const(-549755813888.0), GNM_const(549755813887.0),
			    V2B_STRINGS_GENERAL);
}

/***************************************************************************/

static GnmFuncHelp const help_decimal[] = {
        { GNM_FUNC_HELP_NAME, F_("DECIMAL:decimal representation of @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:number in base @{base}") },
        { GNM_FUNC_HELP_ARG, F_("base:base of @{x}, (2 \xe2\x89\xa4 @{base} \xe2\x89\xa4 36)") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DECIMAL(\"A1\",16)" },
        { GNM_FUNC_HELP_EXAMPLES, "=DECIMAL(\"A1\",15)" },
        { GNM_FUNC_HELP_SEEALSO, "BASE" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_decimal (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float base = value_get_as_float (argv[1]);
	static gnm_float pow_2_40 = GNM_const(1099511627776.0);

	if (base < 2 || base >= 37)
		return value_new_error_NUM (ei->pos);

	return val_to_base (ei, argv[0], NULL,
			    (int)base, 10,
			    0, pow_2_40 - 1,
			    V2B_STRINGS_MAXLEN |
			    V2B_STRINGS_BLANK_ZERO |
			    V2B_NUMBER);
}

/***************************************************************************/

static GnmFuncHelp const help_oct2dec[] = {
        { GNM_FUNC_HELP_NAME, F_("OCT2DEC:decimal representation of the octal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a octal number, either as a string or as a number") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=OCT2DEC(\"124\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=OCT2DEC(124)" },
        { GNM_FUNC_HELP_SEEALSO, ("DEC2OCT,OCT2BIN,OCT2HEX") },
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_oct2dec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], NULL,
			    8, 10,
			    0, GNM_const(7777777777.0),
			    V2B_STRINGS_MAXLEN |
			    V2B_STRINGS_BLANK_ZERO |
			    V2B_NUMBER);
}

/***************************************************************************/

static GnmFuncHelp const help_oct2bin[] = {
        { GNM_FUNC_HELP_NAME, F_("OCT2BIN:binary representation of the octal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a octal number, either as a string or as a number") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, OCT2BIN pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, OCT2BIN returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=OCT2BIN(\"213\")" },
        { GNM_FUNC_HELP_SEEALSO, ("BIN2OCT,OCT2DEC,OCT2HEX") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_oct2bin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    8, 2,
			    0, GNM_const(7777777777.0),
			    V2B_STRINGS_MAXLEN | V2B_STRINGS_BLANK_ZERO);
}

/***************************************************************************/

static GnmFuncHelp const help_oct2hex[] = {
        { GNM_FUNC_HELP_NAME, F_("OCT2HEX:hexadecimal representation of the octal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a octal number, either as a string or as a number") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, OCT2HEX pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, OCT2HEX returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=OCT2HEX(132)" },
        { GNM_FUNC_HELP_SEEALSO, ("HEX2OCT,OCT2BIN,OCT2DEC") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_oct2hex (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    8, 16,
			    0, GNM_const(7777777777.0),
			    V2B_STRINGS_MAXLEN | V2B_STRINGS_BLANK_ZERO);
}

/***************************************************************************/

static GnmFuncHelp const help_hex2bin[] = {
        { GNM_FUNC_HELP_NAME, F_("HEX2BIN:binary representation of the hexadecimal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a hexadecimal number, either as a string or as a number if no A to F are needed") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, HEX2BIN pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, HEX2BIN returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=HEX2BIN(\"2A\")" },
        { GNM_FUNC_HELP_SEEALSO, ("BIN2HEX,HEX2OCT,HEX2DEC") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_hex2bin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    16, 2,
			    0, GNM_const(9999999999.0),
			    V2B_STRINGS_MAXLEN | V2B_STRINGS_BLANK_ZERO);
}

/***************************************************************************/

static GnmFuncHelp const help_hex2oct[] = {
        { GNM_FUNC_HELP_NAME, F_("HEX2OCT:octal representation of the hexadecimal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a hexadecimal number, either as a string or as a number if no A to F are needed") },
        { GNM_FUNC_HELP_ARG, F_("places:number of digits") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("If @{places} is given, HEX2OCT pads the result with zeros to achieve "
					"exactly @{places} digits. If this is not possible, HEX2OCT returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=HEX2OCT(\"2A\")" },
        { GNM_FUNC_HELP_SEEALSO, ("OCT2HEX,HEX2BIN,HEX2DEC") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_hex2oct (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return val_to_base (ei, argv[0], argv[1],
			    16, 8,
			    0, GNM_const(9999999999.0),
			    V2B_STRINGS_MAXLEN | V2B_STRINGS_BLANK_ZERO);
}

/***************************************************************************/

static GnmFuncHelp const help_hex2dec[] = {
        { GNM_FUNC_HELP_NAME, F_("HEX2DEC:decimal representation of the hexadecimal number @{x}") },
        { GNM_FUNC_HELP_ARG, F_("x:a hexadecimal number, either as a string or as a number if no A to F are needed") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=HEX2DEC(\"2A\")" },
        { GNM_FUNC_HELP_SEEALSO, ("DEC2HEX,HEX2BIN,HEX2OCT") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_hex2dec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	static gnm_float pow_2_40 = GNM_const(1099511627776.0);
	return val_to_base (ei, argv[0], NULL,
			    16, 10,
			    0, pow_2_40 - 1,
			    V2B_STRINGS_MAXLEN |
			    V2B_STRINGS_BLANK_ZERO |
			    V2B_NUMBER);
}

/***************************************************************************/

static GnmFuncHelp const help_besseli[] = {
        { GNM_FUNC_HELP_NAME, F_("BESSELI:Modified Bessel function of the first kind of order @{\xce\xb1} at @{x}") },
        { GNM_FUNC_HELP_ARG, F_("X:number") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xb1:order (any non-negative number)") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} or @{\xce\xb1} are not numeric, #VALUE! is returned. If @{\xce\xb1} < 0, #NUM! is returned.") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible if only integer orders @{\xce\xb1} are used.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BESSELI(0.7,3)" },
        { GNM_FUNC_HELP_SEEALSO, "BESSELJ,BESSELK,BESSELY" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Bessel_function") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_besseli (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float order = value_get_as_float (argv[1]);
	return value_new_float (gnm_bessel_i (x, order));
}

/***************************************************************************/

static GnmFuncHelp const help_besselk[] = {
        { GNM_FUNC_HELP_NAME, F_("BESSELK:Modified Bessel function of the second kind of order @{\xce\xb1} at @{x}") },
        { GNM_FUNC_HELP_ARG, F_("X:number") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xb1:order (any non-negative number)") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} or @{\xce\xb1} are not numeric, #VALUE! is returned. If @{\xce\xb1} < 0, #NUM! is returned.") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible if only integer orders @{\xce\xb1} are used.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BESSELK(3,9)" },
        { GNM_FUNC_HELP_SEEALSO, "BESSELI,BESSELJ,BESSELY" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Bessel_function") },
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_besselk (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float order = value_get_as_float (argv[1]);	/* the order */
	return value_new_float (gnm_bessel_k (x, order));
}

/***************************************************************************/

static GnmFuncHelp const help_besselj[] = {
        { GNM_FUNC_HELP_NAME, F_("BESSELJ:Bessel function of the first kind of order @{\xce\xb1} at @{x}") },
        { GNM_FUNC_HELP_ARG, F_("X:number") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xb1:order (any non-negative integer)") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} or @{\xce\xb1} are not numeric, #VALUE! is returned. "
				 "If @{\xce\xb1} < 0, #NUM! is returned.") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible if only integer orders @{\xce\xb1} are used.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BESSELJ(0.89,3)" },
        { GNM_FUNC_HELP_SEEALSO, "BESSELI,BESSELK,BESSELY" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Bessel_function") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_besselj (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float y = value_get_as_float (argv[1]);
	return value_new_float (gnm_bessel_j (x, y));
}

/***************************************************************************/

static GnmFuncHelp const help_bessely[] = {
        { GNM_FUNC_HELP_NAME, F_("BESSELY:Bessel function of the second kind of order @{\xce\xb1} at @{x}") },
        { GNM_FUNC_HELP_ARG, F_("X:number") },
        { GNM_FUNC_HELP_ARG, F_("\xce\xb1:order (any non-negative integer)") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} or @{\xce\xb1} are not numeric, #VALUE! is returned. "
				 "If @{\xce\xb1} < 0, #NUM! is returned.") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible if only integer orders @{\xce\xb1} are used.") },
        { GNM_FUNC_HELP_EXAMPLES, "=BESSELY(4,2)" },
        { GNM_FUNC_HELP_SEEALSO, "BESSELI,BESSELJ,BESSELK" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Bessel_function") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_bessely (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float y = value_get_as_float (argv[1]);
	return value_new_float (gnm_bessel_y (x, y));
}

/***************************************************************************/

static GnmFuncHelp const help_convert[] = {
        { GNM_FUNC_HELP_NAME, F_("CONVERT:a converted measurement") },
        { GNM_FUNC_HELP_ARG, F_("x:number") },
        { GNM_FUNC_HELP_ARG, F_("from:unit (string)") },
        { GNM_FUNC_HELP_ARG, F_("to:unit (string)") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("CONVERT returns a conversion from one measurement system to another. "
					"@{x} is a value in @{from} units that is to be converted into @{to} units.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{from} and @{to} are different types, CONVERT returns #N/A!") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("@{from} and @{to} can be any of the following:\n\n"
					"Weight and mass:\n"
					"\t'brton'\t\tImperial ton\n"
					"\t'cwt'\t\t\tU.S. (short) hundredweight\n"
					"\t'g'  \t\t\tGram\n"
					"\t'grain'\t\tGrain\n"
					"\t'hweight'\t\tImperial (long) hundredweight\n"
					"\t'LTON'\t\tImperial ton\n"
					"\t'sg' \t\t\tSlug\n"
					"\t'shweight'\tU.S. (short) hundredweight\n"
					"\t'lbm'\t\tPound\n"
					"\t'lcwt'\t\tImperial  (long) hundredweight\n"
					"\t'u'  \t\t\tU (atomic mass)\n"
					"\t'uk_cwt'\t\tImperial  (long) hundredweight\n"
					"\t'uk_ton'\t\tImperial ton\n"
					"\t'ozm'\t\tOunce\n"
					"\t'stone'\t\tStone\n"
					"\t'ton'\t\t\tTon\n\n"
					"Distance:\n"
					"\t'm'   \t\tMeter\n"
					"\t'mi'  \t\tStatute mile\n"
					"\t'survey_mi' \tU.S. survey mile\n"
					"\t'Nmi' \t\tNautical mile\n"
					"\t'in'  \t\t\tInch\n"
					"\t'ft'  \t\t\tFoot\n"
					"\t'yd'  \t\tYard\n"
					"\t'ell' \t\t\tEnglish Ell\n"
					"\t'ang' \t\tAngstrom\n"
					"\t'ly' \t\t\tLight-Year\n"
					"\t'pc' \t\t\tParsec\n"
					"\t'parsec' \t\tParsec\n"
					"\t'Pica'\t\tPica Points\n"
					"\t'Picapt'\t\tPica Points\n"
					"\t'picapt'\t\tPica Points\n"
					"\t'pica'\t\tPica\n\n"
					"Time:\n"
					"\t'yr'  \t\t\tYear\n"
					"\t'day' \t\tDay\n"
					"\t'hr'  \t\t\tHour\n"
					"\t'mn'  \t\tMinute\n"
					"\t'sec' \t\tSecond\n\n"
					"Pressure:\n"
					"\t'Pa'  \t\t\tPascal\n"
					"\t'psi' \t\t\tPSI\n"
					"\t'atm' \t\tAtmosphere\n"
					"\t'Pa'  \t\t\tPascal\n"
					"\t'mmHg'\t\tmm of Mercury\n"
					"\t'Torr'\t\t\tTorr\n\n"
					"Force:\n"
					"\t'N'   \t\t\tNewton\n"
					"\t'dyn' \t\tDyne\n"
					"\t'pond' \t\tPond\n"
					"\t'lbf' \t\t\tPound force\n\n"
					"Energy:\n"
					"\t'J'    \t\t\tJoule\n"
					"\t'e'    \t\tErg\n"
					"\t'c'    \t\tThermodynamic calorie\n"
					"\t'cal'  \t\tIT calorie\n"
					"\t'eV'   \t\tElectron volt\n"
					"\t'HPh'  \t\tHorsepower-hour\n"
					"\t'Wh'   \t\tWatt-hour\n"
					"\t'flb'  \t\tFoot-pound\n"
					"\t'BTU'  \t\tBTU\n\n"
					"Power:\n"
					"\t'HP'   \t\tHorsepower\n"
					"\t'PS'   \t\tPferdestärke\n"
					"\t'W'    \t\tWatt\n\n"
					"Magnetism:\n"
					"\t'T'    \t\tTesla\n"
					"\t'ga'   \t\tGauss\n\n"
					"Temperature:\n"
					"\t'C'    \t\tDegree Celsius\n"
					"\t'F'    \t\tDegree Fahrenheit\n"
					"\t'K'    \t\tKelvin\n"
					"\t'Rank' \t\tDegree Rankine\n"
					"\t'Reau' \t\tDegree Réaumur\n\n"
					"Volume (liquid measure):\n"
					"\t'tsp'  \t\tTeaspoon\n"
					"\t'tspm'  \t\tTeaspoon (modern, metric)\n"
					"\t'tbs'  \t\tTablespoon\n"
					"\t'oz'   \t\tFluid ounce\n"
					"\t'cup'  \t\tCup\n"
					"\t'pt'   \t\tPint\n"
					"\t'us_pt'\t\tU.S. pint\n"
					"\t'uk_pt'\t\tImperial pint (U.K.)\n"
					"\t'qt'   \t\tQuart\n"
					"\t'uk_qt'   \t\tImperial quart\n"
					"\t'gal'  \t\tGallon\n"
					"\t'uk_gal'  \t\tImperial gallon\n"
					"\t'GRT'  \t\tRegistered ton\n"
					"\t'regton' \t\tRegistered ton\n"
					"\t'MTON' \t\tMeasurement ton (freight ton)\n"
					"\t'l'    \t\t\tLiter\n"
					"\t'L'    \t\tLiter\n"
					"\t'lt'   \t\t\tLiter\n"
					"\t'ang3' \t\tCubic Angstrom\n"
					"\t'ang^3' \t\tCubic Angstrom\n"
					"\t'barrel' \t\tU.S. oil barrel (bbl)\n"
					"\t'bushel' \t\tU.S. bushel\n"
					"\t'ft3' \t\t\tCubic feet\n"
					"\t'ft^3' \t\tCubic feet\n"
					"\t'in3' \t\tCubic inch\n"
					"\t'in^3' \t\tCubic inch\n"
					"\t'ly3' \t\t\tCubic light-year\n"
					"\t'ly^3' \t\tCubic light-year\n"
					"\t'm3' \t\tCubic meter\n"
					"\t'm^3' \t\tCubic meter\n"
					"\t'mi3' \t\tCubic mile\n"
					"\t'mi^3' \t\tCubic mile\n"
					"\t'yd3' \t\tCubic yard\n"
					"\t'yd^3' \t\tCubic yard\n"
					"\t'Nmi3' \t\tCubic nautical mile\n"
					"\t'Nmi^3' \t\tCubic nautical mile\n"
					"\t'Picapt3' \t\tCubic Pica\n"
					"\t'Picapt^3' \tCubic Pica\n"
					"\t'Pica3' \t\tCubic Pica\n"
					"\t'Pica^3' \t\tCubic Pica\n\n"
					"Area:\n"
					"\t'uk_acre' \t\tInternational acre\n"
					"\t'us_acre' \t\tU.S. survey/statute acre\n"
					"\t'ang2' \t\tSquare angstrom\n"
					"\t'ang^2' \t\tSquare angstrom\n"
					"\t'ar' \t\t\tAre\n"
					"\t'ha' \t\t\tHectare\n"
					"\t'in2' \t\tSquare inches\n"
					"\t'in^2' \t\tSquare inches\n"
					"\t'ly2' \t\t\tSquare light-year\n"
					"\t'ly^2' \t\tSquare light-year\n"
					"\t'm2' \t\tSquare meter\n"
					"\t'm^2' \t\tSquare meter\n"
					"\t'Morgen' \t\tMorgen (North German Confederation)\n"
					"\t'mi2' \t\tSquare miles\n"
					"\t'mi^2' \t\tSquare miles\n"
					"\t'Nmi2' \t\tSquare nautical miles\n"
					"\t'Nmi^2' \t\tSquare nautical miles\n"
					"\t'Picapt2' \t\tSquare Pica\n"
					"\t'Picapt^2' \tSquare Pica\n"
					"\t'Pica2' \t\tSquare Pica\n"
					"\t'Pica^2' \t\tSquare Pica\n"
					"\t'yd2' \t\tSquare yards\n"
					"\t'yd^2' \t\tSquare yards\n\n"
					"Bits and Bytes:\n"
					"\t'bit' \t\t\tBit\n"
					"\t'byte' \t\tByte\n\n"
					"Speed:\n"
					"\t'admkn' \t\tAdmiralty knot\n"
					"\t'kn' \t\t\tknot\n"
					"\t'm/h' \t\tMeters per hour\n"
					"\t'm/hr' \t\tMeters per hour\n"
					"\t'm/s' \t\tMeters per second\n"
					"\t'm/sec' \t\tMeters per second\n"
					"\t'mph' \t\tMiles per hour\n\n"
					"For metric units any of the following prefixes can be used:\n"
					"\t'Y'  \tyotta \t\t1E+24\n"
					"\t'Z'  \tzetta \t\t1E+21\n"
					"\t'E'  \texa   \t\t1E+18\n"
					"\t'P'  \tpeta  \t\t1E+15\n"
					"\t'T'  \ttera  \t\t1E+12\n"
					"\t'G'  \tgiga  \t\t1E+09\n"
					"\t'M'  \tmega  \t\t1E+06\n"
					"\t'k'  \tkilo  \t\t\t1E+03\n"
					"\t'h'  \thecto \t\t1E+02\n"
					"\t'e'  \tdeca (deka)\t1E+01\n"
					"\t'd'  \tdeci  \t\t1E-01\n"
					"\t'c'  \tcenti \t\t1E-02\n"
					"\t'm'  \tmilli \t\t\t1E-03\n"
					"\t'u'  \tmicro \t\t1E-06\n"
					"\t'n'  \tnano  \t\t1E-09\n"
					"\t'p'  \tpico  \t\t1E-12\n"
					"\t'f'  \tfemto \t\t1E-15\n"
					"\t'a'  \tatto  \t\t1E-18\n"
					"\t'z'  \tzepto \t\t1E-21\n"
					"\t'y'  \tyocto \t\t1E-24\n\n"
					"For bits and bytes any of the following prefixes can be also be used:\n"
					"\t'Yi'  \tyobi \t\t2^80\n"
					"\t'Zi'  \tzebi \t\t\t2^70\n"
					"\t'Ei'  \texbi \t\t2^60\n"
					"\t'Pi'  \tpebi \t\t2^50\n"
					"\t'Ti'  \ttebi \t\t\t2^40\n"
					"\t'Gi'  \tgibi \t\t\t2^30\n"
					"\t'Mi'  \tmebi \t\t2^20\n"
					"\t'ki'  \tkibi \t\t\t2^10") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible (except \"picapt\").") },
 	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CONVERT(3,\"lbm\",\"g\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=CONVERT(5.8,\"m\",\"in\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=CONVERT(7.9,\"cal\",\"J\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=CONVERT(3,\"Yibyte\",\"bit\")" },
        { GNM_FUNC_HELP_END}
};

typedef struct {
        char const *str;
	gnm_float c;
} eng_convert_unit_t;


static gnm_float
get_constant_of_unit(const eng_convert_unit_t units[],
		     const eng_convert_unit_t prefixes[],
		     char const *unit_name,
		     gnm_float *c, gnm_float *prefix)
{
        int i;

	*prefix = 1;
	for (i = 0; units[i].str != NULL; i++)
	        if (strcmp (unit_name, units[i].str) == 0) {
		        *c = units[i].c;
			return 1;
		}

	if (prefixes != NULL)
	        for (i = 0; prefixes[i].str != NULL; i++) {
			int prefix_length = strlen (prefixes[i].str);
		        if (strncmp (unit_name, prefixes[i].str, prefix_length) == 0) {
			        *prefix = prefixes[i].c;
				unit_name += prefix_length;
				break;
			}
		}

	for (i = 0; units[i].str != NULL; i++)
	        if (strcmp (unit_name, units[i].str) == 0) {
		        *c = units[i].c;
			return 1;
		}

	return 0;
}

/* See also http://physics.nist.gov/cuu/Units/prefixes.html */

static gboolean
convert (eng_convert_unit_t const units[],
	 eng_convert_unit_t const prefixes[],
	 char const *from_unit, char const *to_unit,
	 gnm_float n, GnmValue **v, GnmEvalPos const *ep)
{
        gnm_float from_c, from_prefix, to_c, to_prefix;

	if (get_constant_of_unit (units, prefixes, from_unit, &from_c,
				  &from_prefix)) {

	        if (!get_constant_of_unit (units, prefixes,
					   to_unit, &to_c, &to_prefix))
			*v = value_new_error_NUM (ep);
	        else if (from_c == 0 || to_prefix == 0)
	                *v = value_new_error_NUM (ep);
		else
			*v = value_new_float (((n * from_prefix) / from_c) *
					      to_c / to_prefix);
		return TRUE;
	}

	return FALSE;
}

typedef enum {
	temp_invalid = 0,
	temp_K,
	temp_C,
	temp_F,
	temp_Rank,
	temp_Reau
} temp_types;

static temp_types
convert_temp_unit (char const *unit) {
	if (0 == strcmp (unit, "K"))
		return temp_K;
	else if (0 == strcmp (unit, "C"))
		return temp_C;
	else if (0 == strcmp (unit, "F"))
		return temp_F;
	else if (0 == strcmp (unit, "Reau"))
		return temp_Reau;
	else if (0 == strcmp (unit, "Rank"))
		return temp_Rank;
	return temp_invalid;
}

static gboolean
convert_temp (char const *from_unit, char const *to_unit, gnm_float n, GnmValue **v, GnmEvalPos const *ep)
{
	/* Temperature constants */
	const gnm_float C_K_offset = GNM_const (273.15);

	temp_types from_unit_type = convert_temp_unit (from_unit);
	temp_types to_unit_type = convert_temp_unit (to_unit);

	gnm_float nO = n;

	if ((from_unit_type == temp_invalid) || (to_unit_type == temp_invalid))
		return FALSE;

	/* Convert from from_unit to K */
	switch (from_unit_type) {
	case temp_C:
		n += C_K_offset;
		break;
	case temp_F:
		n = (n - 32) * 5 / 9 + C_K_offset;
		break;
	case temp_Rank:
		n = n * 5/9;
		break;
	case temp_Reau:
		n = n * 5/4 + C_K_offset;
		break;
	default:
		break;
	}

	/* temperatures below 0K do not exist */
	if (n < 0) {
		*v = value_new_error_NUM (ep);
		return TRUE;
	}

	if (from_unit_type == to_unit_type) {
		*v = value_new_float (nO);
		return TRUE;
	}

	/* Convert from K to to_unit */
	switch (to_unit_type) {
	case temp_C:
		n -= C_K_offset;
		break;
	case temp_F:
		n = (n - C_K_offset) * 9/5 + 32;
		break;
	case temp_Rank:
		n = n * 9/5;
		break;
	case temp_Reau:
		n = (n - C_K_offset) * 4/5;
		break;
	default:
		break;
	}

	*v = value_new_float (n);

	return TRUE;
}

static GnmValue *
gnumeric_convert (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        /* Weight and mass constants */
#define one_g_to_cwt    (one_g_to_lbm/100)  /* exact relative definition */
#define one_g_to_grain  (one_g_to_lbm*7000) /* exact relative definition */
#define one_g_to_uk_cwt (one_g_to_lbm/112)  /* exact relative definition */
#define one_g_to_uk_ton (one_g_to_lbm/2240) /* exact relative definition */
#define one_g_to_stone  (one_g_to_lbm/14)   /* exact relative definition */
#define one_g_to_ton    (one_g_to_lbm/2000) /* exact relative definition */
#define one_g_to_sg     GNM_const (0.00006852205001)
#define one_g_to_lbm    (1/GNM_const (453.59237)) /* exact definition */
#define one_g_to_u      GNM_const (6.02217e+23)
#define one_g_to_ozm    (one_g_to_lbm*16)   /* exact relative definition */

	/* Distance constants */
#define one_m_to_mi     (one_m_to_yd / 1760)
#define one_m_to_survey_mi (1 / GNM_const (1609.347218694))
#define one_m_to_Nmi    (1 / GNM_const (1852.0))
#define one_m_to_in     (10000 / GNM_const (254.0))
#define one_m_to_ft     (one_m_to_in / 12)
#define one_m_to_yd     (one_m_to_ft / 3)
#define one_m_to_ell    (one_m_to_in / 45)
#define one_m_to_ang    GNM_const (1e10)
#define one_m_to_ly     (1 / GNM_const (9.4607304725808E15))
#define one_m_to_pc     (GNM_const (1e-16)/GNM_const (3.0856776))
#define one_m_to_pica   GNM_const(236.2204724409449)
#define one_m_to_Pica   one_m_to_pica * 12

	/* Time constants */
#define one_yr_to_day   GNM_const(365.25)
#define one_yr_to_hr    (24 * one_yr_to_day)
#define one_yr_to_mn    (60 * one_yr_to_hr)
#define one_yr_to_sec   (60 * one_yr_to_mn)

	/* Pressure constants */
#define one_Pa_to_atm   GNM_const(0.9869233e-5)
#define one_Pa_to_mmHg  GNM_const(0.00750061708)
#define one_Pa_to_psi   GNM_const(0.000145037738)
#define one_Pa_to_Torr  (GNM_const (760.)/GNM_const (101325.))

	/* Force constants */
#define one_N_to_dyn    GNM_const(100000.)
#define one_N_to_lbf    GNM_const(0.224808924)
#define one_N_to_pond   GNM_const(0.00010197)


	/* Power constants */
#define one_HP_to_W     GNM_const(745.701)
#define one_PS_to_W     GNM_const(735.49875)

	/* Energy constants */
#define one_J_to_e      GNM_const(9999995.193)
#define one_J_to_c      GNM_const(0.239006249)
#define one_J_to_cal    GNM_const(0.238846191)
#define one_J_to_eV     GNM_const(6.2146e+18)
#define one_J_to_HPh    (GNM_const (1.0) / (3600 * one_HP_to_W))
#define one_J_to_Wh     (GNM_const (1.0) / 3600)
#define one_J_to_flb    GNM_const(23.73042222)
#define one_J_to_BTU    GNM_const(0.000947815)

	/* Magnetism constants */
#define one_T_to_ga     GNM_const(10000.)

	/* Liquid measure constants */
#define one_l_to_uk_gal (GNM_const (1.0) / GNM_const (4.54609))
#define one_tsp_to_tspm (one_tsp_to_l/GNM_const (0.005))
#define one_tsp_to_tbs  (GNM_const (1.0) / 3)
#define one_tsp_to_oz   (GNM_const (1.0) / 6)
#define one_tsp_to_cup  (GNM_const (1.0) / 48)
#define one_tsp_to_pt   (GNM_const (1.0) / 96)
#define one_tsp_to_uk_pt (one_tsp_to_l * one_l_to_uk_gal * 8)
#define one_tsp_to_qt   (GNM_const (1.0) / 192)
#define one_tsp_to_uk_qt (one_tsp_to_l * one_l_to_uk_gal * 2)
#define one_tsp_to_gal  (GNM_const (1.0) / 768)
#define one_tsp_to_uk_gal (one_tsp_to_l * one_l_to_uk_gal)
#define one_tsp_to_l    GNM_const (0.004928921593749999)
#define one_tsp_to_ang3 (one_tsp_to_l * GNM_const (1E-27))
#define one_tsp_to_barrel (one_tsp_to_gal / 42)
#define one_tsp_to_bushel (one_tsp_to_gal / GNM_const (9.3092))
#define one_tsp_to_grt  (one_tsp_to_cubic_ft / 100)
#define one_tsp_to_mton  (one_tsp_to_cubic_ft / 40)
#define one_tsp_to_cubic_m  (one_tsp_to_l / 1000)
#define one_tsp_to_cubic_ft (one_tsp_to_cubic_m * one_m_to_ft * one_m_to_ft * one_m_to_ft)
#define one_tsp_to_cubic_in (one_tsp_to_cubic_m * one_m_to_in * one_m_to_in * one_m_to_in)
#define one_tsp_to_cubic_ly (one_tsp_to_cubic_m * one_m_to_ly * one_m_to_ly * one_m_to_ly)
#define one_tsp_to_cubic_mi (one_tsp_to_cubic_m * one_m_to_mi * one_m_to_mi * one_m_to_mi)
#define one_tsp_to_cubic_yd (one_tsp_to_cubic_m * one_m_to_yd * one_m_to_yd * one_m_to_yd)
#define one_tsp_to_cubic_Nmi (one_tsp_to_cubic_m * one_m_to_Nmi * one_m_to_Nmi * one_m_to_Nmi)
#define one_tsp_to_cubic_Pica (one_tsp_to_cubic_m * one_m_to_Pica * one_m_to_Pica * one_m_to_Pica)

	/* Bits and Bytes */
#define one_byte_to_bit 8

	/* Area constants */
#define one_m2_to_uk_acre     (1/GNM_const (4046.8564224))
#define one_m2_to_us_acre     (1/(4046 + GNM_const (13525426.)/15499969))
#define one_m2_to_ang2        (one_m_to_ang * one_m_to_ang)
#define one_m2_to_ar          GNM_const (0.01)
#define one_m2_to_ha          (one_m2_to_ar / hecto)
#define one_m2_to_ft2         (one_m_to_ft * one_m_to_ft)
#define one_m2_to_in2         (one_m_to_in * one_m_to_in)
#define one_m2_to_ly2         (one_m_to_ly * one_m_to_ly)
#define one_m2_to_mi2         (one_m_to_mi * one_m_to_mi)
#define one_m2_to_Nmi2        (one_m_to_Nmi * one_m_to_Nmi)
#define one_m2_to_yd2         (one_m_to_yd * one_m_to_yd)
#define one_m2_to_Pica2       (one_m_to_Pica * one_m_to_Pica)
#define one_m2_to_Morgen      (one_m2_to_ha * 4)

	/* Speed constants */
#define one_mh_to_kn          one_m_to_Nmi
#define one_mh_to_admkn       (1/GNM_const (1853.184))
#define one_mh_to_msec        (GNM_const (1.0)/(60*60))
#define one_mh_to_mph          one_m_to_mi


	/* Prefixes */
#define yotta  GNM_const (1e+24)
#define zetta  GNM_const (1e+21)
#define exa    GNM_const (1e+18)
#define peta   GNM_const (1e+15)
#define tera   GNM_const (1e+12)
#define giga   GNM_const (1e+09)
#define mega   GNM_const (1e+06)
#define kilo   GNM_const (1e+03)
#define hecto  GNM_const (1e+02)
#define deka   GNM_const (1e+01)
#define deci   GNM_const (1e-01)
#define centi  GNM_const (1e-02)
#define milli  GNM_const (1e-03)
#define micro  GNM_const (1e-06)
#define nano   GNM_const (1e-09)
#define pico   GNM_const (1e-12)
#define femto  GNM_const (1e-15)
#define atto   GNM_const (1e-18)
#define zepto  GNM_const (1e-21)
#define yocto  GNM_const (1e-24)

	/* Binary Prefixes */
#define yobi  (zebi * kibi)
#define zebi  (exbi * kibi)
#define exbi  (pebi * kibi)
#define pebi  (tebi * kibi)
#define tebi  (gibi * kibi)
#define gibi  (mebi * kibi)
#define mebi  (kibi * kibi)
#define kibi  GNM_const (1024.0)

	static const eng_convert_unit_t weight_units[] = {
	        { "g",    1.0 },
		{ "brton",one_g_to_uk_ton },
		{ "cwt",  one_g_to_cwt },
		{ "grain",one_g_to_grain },
		{ "hweight",one_g_to_uk_cwt },
		{ "LTON",one_g_to_uk_ton },
		{ "sg",   one_g_to_sg },
		{ "shweight",one_g_to_cwt },
		{ "lbm",  one_g_to_lbm },
		{ "lcwt", one_g_to_uk_cwt },
		{ "u",    one_g_to_u },
		{ "uk_cwt",one_g_to_uk_cwt },
		{ "uk_ton",one_g_to_uk_ton },
		{ "ozm",  one_g_to_ozm },
		{ "stone",one_g_to_stone },
		{ "ton",  one_g_to_ton },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t distance_units[] = {
	        { "m",    1.0 },
		{ "mi",   one_m_to_mi },
		{ "survey_mi", one_m_to_survey_mi},
		{ "Nmi",  one_m_to_Nmi },
		{ "in",   one_m_to_in },
		{ "ft",   one_m_to_ft },
		{ "yd",   one_m_to_yd },
		{ "ell",  one_m_to_ell },
		{ "ang",  one_m_to_ang },
		{ "pc",   one_m_to_pc },
		{ "parsec", one_m_to_pc },
		{ "ly",   one_m_to_ly },
		{ "Pica", one_m_to_Pica },
		{ "Picapt", one_m_to_Pica },
		{ "picapt", one_m_to_Pica },
		{ "pica", one_m_to_pica },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t time_units[] = {
	        { "yr",   1.0 },
		{ "day",  one_yr_to_day },
		{ "hr",   one_yr_to_hr },
		{ "mn",   one_yr_to_mn },
		{ "sec",  one_yr_to_sec },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t pressure_units[] = {
	        { "Pa",   1.0 },
		{ "psi",  one_Pa_to_psi },
		{ "atm",  one_Pa_to_atm },
		{ "mmHg", one_Pa_to_mmHg },
		{ "Torr", one_Pa_to_Torr },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t force_units[] = {
	        { "N",    1.0 },
		{ "dyn",  one_N_to_dyn },
		{ "pond", one_N_to_pond },
		{ "lbf",  one_N_to_lbf },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t energy_units[] = {
	        { "J",    1.0 },
		{ "e",    one_J_to_e },
		{ "c",    one_J_to_c },
		{ "cal",  one_J_to_cal },
		{ "eV",   one_J_to_eV },
		{ "HPh",  one_J_to_HPh },
		{ "Wh",   one_J_to_Wh },
		{ "flb",  one_J_to_flb },
		{ "BTU",  one_J_to_BTU },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t power_units[] = {
	        { "HP",   1.0 },
		{ "PS",   one_HP_to_W/one_PS_to_W },
		{ "W",    one_HP_to_W },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t magnetism_units[] = {
	        { "T",    1.0 },
		{ "ga",   one_T_to_ga },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t liquid_units[] = {
	        { "tsp",  1.0 },
		{ "tspm", one_tsp_to_tspm },
		{ "tbs",  one_tsp_to_tbs },
		{ "oz",   one_tsp_to_oz },
		{ "cup",  one_tsp_to_cup },
		{ "pt",   one_tsp_to_pt },
		{ "qt",   one_tsp_to_qt },
		{ "uk_qt", one_tsp_to_uk_qt },
		{ "gal",  one_tsp_to_gal },
		{ "uk_gal", one_tsp_to_uk_gal },
		{ "us_pt",one_tsp_to_pt },
		{ "uk_pt",one_tsp_to_uk_pt },
		{ "l",    one_tsp_to_l },
		{ "L",    one_tsp_to_l },
		{ "lt",   one_tsp_to_l },
		{ "ang3", one_tsp_to_ang3 },
		{ "ang^3", one_tsp_to_ang3 },
		{ "bushel", one_tsp_to_bushel },
		{ "barrel", one_tsp_to_barrel },
		{ "GRT", one_tsp_to_grt },
		{ "regton", one_tsp_to_grt },
		{ "MTON", one_tsp_to_mton },
		{ "ft3", one_tsp_to_cubic_ft},
		{ "ft^3", one_tsp_to_cubic_ft},
		{ "in3", one_tsp_to_cubic_in},
		{ "in^3", one_tsp_to_cubic_in},
		{ "ly3", one_tsp_to_cubic_ly},
		{ "ly^3", one_tsp_to_cubic_ly},
		{ "m3", one_tsp_to_cubic_m},
		{ "m^3", one_tsp_to_cubic_m},
		{ "mi3", one_tsp_to_cubic_mi},
		{ "mi^3", one_tsp_to_cubic_mi},
		{ "yd3", one_tsp_to_cubic_yd},
		{ "yd^3", one_tsp_to_cubic_yd},
		{ "Nmi3", one_tsp_to_cubic_Nmi},
		{ "Nmi^3", one_tsp_to_cubic_Nmi},
		{ "Pica3", one_tsp_to_cubic_Pica},
		{ "Pica^3", one_tsp_to_cubic_Pica},
		{ "Picapt3", one_tsp_to_cubic_Pica},
		{ "Picapt^3", one_tsp_to_cubic_Pica},
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t information_units[] = {
	        { "byte",    1.0 },
		{ "bit",  one_byte_to_bit},
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t speed_units[] = {
		{ "m/h", 1.0 },
		{ "m/hr", 1.0 },
		{ "admkn", one_mh_to_admkn },
		{ "kn", one_mh_to_kn },
		{ "m/s", one_mh_to_msec },
		{ "m/sec", one_mh_to_msec },
		{ "mph", one_mh_to_mph },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t area_units[] = {
		{ "m2", 1.0 },
		{ "m^2", 1.0 },
		{ "uk_acre", one_m2_to_uk_acre },
		{ "us_acre", one_m2_to_us_acre },
		{ "ang2", one_m2_to_ang2 },
		{ "ang^2", one_m2_to_ang2 },
		{ "ar", one_m2_to_ar },
		{ "ha", one_m2_to_ha },
		{ "in2", one_m2_to_in2 },
		{ "in^2", one_m2_to_in2 },
		{ "ft2", one_m2_to_ft2 },
		{ "ft^2", one_m2_to_ft2 },
		{ "ly2", one_m2_to_ly2 },
		{ "ly^2", one_m2_to_ly2 },
		{ "mi2", one_m2_to_mi2 },
		{ "mi^2", one_m2_to_mi2 },
		{ "Nmi2", one_m2_to_Nmi2 },
		{ "Nmi^2", one_m2_to_Nmi2 },
		{ "yd2", one_m2_to_yd2 },
		{ "yd^2", one_m2_to_yd2 },
		{ "Picapt2", one_m2_to_Pica2 },
		{ "Picapt^2", one_m2_to_Pica2 },
		{ "Pica2", one_m2_to_Pica2 },
		{ "Pica^2", one_m2_to_Pica2 },
		{ "Morgen", one_m2_to_Morgen },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t prefixes[] = {
	        { "Y", yotta },
	        { "Z", zetta },
	        { "E", exa },
	        { "P", peta },
	        { "T", tera },
	        { "G", giga },
	        { "M", mega },
	        { "k", kilo },
	        { "h", hecto },
	        { "e", deka },
	        { "d", deci },
	        { "c", centi },
	        { "m", milli },
	        { "u", micro },
	        { "n", nano },
	        { "p", pico },
	        { "f", femto },
	        { "a", atto },
	        { "z", zepto },
	        { "y", yocto },
		{ NULL,0.0 }
	};
	static const eng_convert_unit_t binary_prefixes[] = {
	        { "Yi", yobi },
	        { "Zi", zebi },
	        { "Ei", exbi },
	        { "Pi", pebi },
	        { "Ti", tebi },
	        { "Gi", gibi },
	        { "Mi", mebi },
	        { "ki", kibi },
		{ NULL,0.0 }
	};

	gnm_float n;
	char const *from_unit, *to_unit;
	GnmValue *v;

	n = value_get_as_float (argv[0]);
	from_unit = value_peek_string (argv[1]);
	to_unit = value_peek_string (argv[2]);

	if (convert_temp (from_unit, to_unit, n, &v, ei->pos))
	        return v;

	if (convert (weight_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (distance_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (time_units, NULL, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (pressure_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (force_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (energy_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (power_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (magnetism_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (liquid_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (information_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (information_units, binary_prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (speed_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;
	if (convert (area_units, prefixes, from_unit, to_unit, n, &v,
		     ei->pos))
	        return v;

	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_erf[] = {
        { GNM_FUNC_HELP_NAME, F_("ERF:Gauss error function") },
        { GNM_FUNC_HELP_ARG, F_("lower:lower limit of the integral, defaults to 0") },
        { GNM_FUNC_HELP_ARG, F_("upper:upper limit of the integral") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ERF returns 2/sqrt(\xcf\x80)* integral from @{lower} to @{upper} of exp(-t*t) dt") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible if two arguments are supplied and neither is negative.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ERF(0.4)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ERF(6,10)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ERF(1.6448536269515/SQRT(2))" },
        { GNM_FUNC_HELP_SEEALSO, "ERFC" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Error_function") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_erf (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float lower = value_get_as_float (argv[0]);
	gnm_float ans;

	if (argv[1]) {
		gnm_float upper = value_get_as_float (argv[1]);
		ans = 2 * pnorm2 (lower * M_SQRT2gnum, upper * M_SQRT2gnum);
	} else
		ans = gnm_erf (lower);

	return value_new_float (ans);
}

/***************************************************************************/

static GnmFuncHelp const help_erfc[] = {
        { GNM_FUNC_HELP_NAME, F_("ERFC:Complementary Gauss error function") },
        { GNM_FUNC_HELP_ARG, F_("x:number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ERFC returns 2/sqrt(\xcf\x80)* integral from @{x} to \xe2\x88\x9e of exp(-t*t) dt") },
        { GNM_FUNC_HELP_EXAMPLES, "=ERFC(6)" },
        { GNM_FUNC_HELP_SEEALSO, "ERF" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Error_function") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_erfc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);

	return value_new_float (gnm_erfc (x));
}

/***************************************************************************/

static GnmFuncHelp const help_delta[] = {
        { GNM_FUNC_HELP_NAME, F_("DELTA:Kronecker delta function") },
        { GNM_FUNC_HELP_ARG, F_("x0:number") },
        { GNM_FUNC_HELP_ARG, F_("x1:number, defaults to 0") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("DELTA  returns 1 if  @{x1} = @{x0} and 0 otherwise.") },
	{ GNM_FUNC_HELP_NOTE, F_("If either argument is non-numeric, #VALUE! is returned.") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=DELTA(42.99,43)" },
        { GNM_FUNC_HELP_SEEALSO, "EXACT,GESTEP" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_delta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float y = argv[1] ? value_get_as_float (argv[1]) : 0;

	return value_new_int (x == y);
}

/***************************************************************************/

static GnmFuncHelp const help_gestep[] = {
        { GNM_FUNC_HELP_NAME, F_("GESTEP:step function with step at @{x1} evaluated at @{x0}") },
        { GNM_FUNC_HELP_ARG, F_("x0:number") },
        { GNM_FUNC_HELP_ARG, F_("x1:number, defaults to 0") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("GESTEP returns 1 if  @{x1} \xe2\x89\xa4 @{x0} and 0 otherwise.") },
	{ GNM_FUNC_HELP_NOTE, F_("If either argument is non-numeric, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=GESTEP(5,4)" },
        { GNM_FUNC_HELP_SEEALSO, "DELTA" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_gestep (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float y = argv[1] ? value_get_as_float (argv[1]) : 0;

	return value_new_int (x >= y);
}

/***************************************************************************/

static GnmFuncHelp const help_hexrep[] = {
        { GNM_FUNC_HELP_NAME, F_("HEXREP:hexadecimal representation of numeric value") },
        { GNM_FUNC_HELP_ARG, F_("x:number") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("HEXREP returns a hexadecimal string representation of @{x}.") },
	{ GNM_FUNC_HELP_NOTE, F_("This is a function meant for debugging.  The layout of the result may change and even depend on how Gnumeric was compiled.") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_hexrep (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	unsigned char data[sizeof(gnm_float)];
	unsigned ui;
	char res[2 * sizeof(gnm_float) + 1];
	static const char hex[16] = "0123456789abcdef";

	/* We don't have a long double version yet.  */
	memset (data, 0, sizeof(data));
	GSF_LE_SET_DOUBLE (data, x);

	for (ui = 0; ui < G_N_ELEMENTS (data); ui++) {
		unsigned char b = data[ui];
		res[2 * ui] = hex[b >> 4];
		res[2 * ui + 1] = hex[b & 0xf];
	}
	res[2 * ui] = 0;

	return value_new_string (res);
}

/***************************************************************************/

static GnmFuncHelp const help_invsuminv[] = {
        { GNM_FUNC_HELP_NAME, F_("INVSUMINV:the reciprocal of the sum of reciprocals of the arguments") },
        { GNM_FUNC_HELP_ARG, F_("x0:non-negative number") },
        { GNM_FUNC_HELP_ARG, F_("x1:non-negative number") },
	{ GNM_FUNC_HELP_NOTE, F_("If any of the arguments is negative, #VALUE! is returned.\n"
				 "If any argument is zero, the result is zero.") },
        { GNM_FUNC_HELP_DESCRIPTION, F_("INVSUMINV sum calculates the reciprocal (the inverse) "
					"of the sum of reciprocals (inverses) of all its arguments.") },
        { GNM_FUNC_HELP_EXAMPLES, "=INVSUMINV(2000,2000)" },
        { GNM_FUNC_HELP_SEEALSO, "HARMEAN" },
        { GNM_FUNC_HELP_END}
};


static int
range_invsuminv (gnm_float const *xs, int n, gnm_float *res)
{
	int i;
	gnm_float suminv = 0;
	gboolean zerop = FALSE;

	if (n <= 0) return 1;

	for (i = 0; i < n; i++) {
		gnm_float x = xs[i];
		if (x < 0) return 1;
		if (x == 0)
			zerop = TRUE;
		else
			suminv += 1 / x;
	}

	*res = zerop ? 0 : 1 / suminv;
	return 0;
}

static GnmValue *
gnumeric_invsuminv (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_invsuminv,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

GnmFuncDescriptor const engineering_functions[] = {
        { "base",     "Sf|f",    help_base,
	  gnumeric_base, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        { "besseli",     "ff",    help_besseli,
	  gnumeric_besseli, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "besselj",     "ff",    help_besselj,
	  gnumeric_besselj, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "besselk",     "ff",    help_besselk,
	  gnumeric_besselk, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bessely",     "ff",    help_bessely,
	  gnumeric_bessely, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "bin2dec",     "S",     help_bin2dec,
	  gnumeric_bin2dec, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bin2hex",     "S|f",   help_bin2hex,
	  gnumeric_bin2hex, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bin2oct",     "S|f",   help_bin2oct,
	  gnumeric_bin2oct, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "convert",     "fss",   help_convert,
	  gnumeric_convert, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2bin",     "S|f",   help_dec2bin,
	  gnumeric_dec2bin, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2oct",     "S|f",   help_dec2oct,
	  gnumeric_dec2oct, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2hex",     "S|f",   help_dec2hex,
	  gnumeric_dec2hex, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "decimal",     "Sf",    help_decimal,
	  gnumeric_decimal, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        { "delta",       "f|f",   help_delta,
	  gnumeric_delta, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "erf",         "f|f",   help_erf,
	  gnumeric_erf , NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "erfc",        "f",     help_erfc,
	  gnumeric_erfc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "gestep",      "f|f",   help_gestep,
	  gnumeric_gestep, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "hex2bin",     "S|f",   help_hex2bin,
	  gnumeric_hex2bin, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "hex2dec",     "S",     help_hex2dec,
	  gnumeric_hex2dec, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "hex2oct",     "S|f",   help_hex2oct,
	  gnumeric_hex2oct, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "hexrep",     "f",   help_hexrep,
	  gnumeric_hexrep, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "invsuminv",    NULL,            help_invsuminv,
	  NULL, gnumeric_invsuminv,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        { "oct2bin",     "S|f",   help_oct2bin,
	  gnumeric_oct2bin, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "oct2dec",     "S",     help_oct2dec,
	  gnumeric_oct2dec, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "oct2hex",     "S|f",   help_oct2hex,
	  gnumeric_oct2hex, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};
