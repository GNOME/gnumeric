/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

	switch (value->type) {
	default:
		return value_new_error_NUM (ei->pos);

	case VALUE_STRING:
		if (flags & V2B_STRINGS_GENERAL) {
			vstring = format_match_number
				(value_peek_string (value), NULL,
				 workbook_date_conv (ei->pos->sheet->workbook));
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
		char buf[GNM_MANT_DIG + 10];
		char *err;

		value_release (vstring);

		if (val < min_value || val > max_value)
			return value_new_error_NUM (ei->pos);

		g_ascii_formatd (buf, sizeof (buf) - 1,
				 "%.0" GNM_FORMAT_f,
				 val);

		v = g_ascii_strtoll (buf, &err, src_base);
		if (*err != 0)
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
			min = max = (int)(gnm_log (v + 0.5) /
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
		int thisdigit = gnm_fmod (v + 0.5, dest_base);
		v = gnm_floor ((v + 0.5) / dest_base);
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
        { GNM_FUNC_HELP_DESCRIPTION, F_("BASE converts @{n} to its string representation in base @{b}."
					" Leading zeroes will be added to reach the minimum length given by @{length}.") },
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
        { GNM_FUNC_HELP_SEEALSO, "DEC2BIN,BIN2OCT,BIN2HEX" },
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
        { GNM_FUNC_HELP_SEEALSO, ("OCT2BIN,BIN2DEC,BIN2HEX") },
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
        { GNM_FUNC_HELP_SEEALSO, ("HEX2BIN,BIN2OCT,BIN2DEC") },
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
        { GNM_FUNC_HELP_SEEALSO, ("BIN2DEC,DEC2OCT,DEC2HEX") },
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
        { GNM_FUNC_HELP_SEEALSO, ("OCT2DEC,DEC2BIN,DEC2HEX") },
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
        { GNM_FUNC_HELP_SEEALSO, ("HEX2DEC,DEC2BIN,DEC2OCT") },
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

	if (base < 2 || base >= 37)
		return value_new_error_NUM (ei->pos);

	return value_new_error_NUM (ei->pos);
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
					"\t'g'  \t\t\tGram\n"
					"\t'sg' \t\t\tSlug\n"
					"\t'lbm'\t\tPound\n"
					"\t'u'  \t\t\tU (atomic mass)\n"
					"\t'ozm'\t\tOunce\n\n"
					"Distance:\n"
					"\t'm'   \t\tMeter\n"
					"\t'mi'  \t\tStatute mile\n"
					"\t'Nmi' \t\tNautical mile\n"
					"\t'in'  \t\t\tInch\n"
					"\t'ft'  \t\t\tFoot\n"
					"\t'yd'  \t\tYard\n"
					"\t'ang' \t\tAngstrom\n"
					"\t'Pica'\t\tPica Points\n"
					"\t'picapt'\t\tPica Points\n"
					"\t'pica'\t\tPica\n\n"
					"Time:\n"
					"\t'yr'  \t\t\tYear\n"
					"\t'day' \t\tDay\n"
					"\t'hr'  \t\t\tHour\n"
					"\t'mn'  \t\tMinute\n"
					"\t'sec' \t\tSecond\n\n"
					"Pressure:\n"
					"\t'Pa'  \t\tPascal\n"
					"\t'atm' \t\tAtmosphere\n"
					"\t'mmHg'\t\tmm of Mercury\n\n"
					"Force:\n"
					"\t'N'   \t\t\tNewton\n"
					"\t'dyn' \t\tDyne\n"
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
					"\t'W'    \t\tWatt\n\n"
					"Magnetism:\n"
					"\t'T'    \t\tTesla\n"
					"\t'ga'   \t\tGauss\n\n"
					"Temperature:\n"
					"\t'C'    \t\tDegree Celsius\n"
					"\t'F'    \t\tDegree Fahrenheit\n"
					"\t'K'    \t\tDegree Kelvin\n\n"
					"Liquid measure:\n"
					"\t'tsp'  \t\tTeaspoon\n"
					"\t'tbs'  \t\tTablespoon\n"
					"\t'oz'   \t\tFluid ounce\n"
					"\t'cup'  \t\tCup\n"
					"\t'pt'   \t\tPint\n"
					"\t'qt'   \t\tQuart\n"
					"\t'gal'  \t\tGallon\n"
					"\t'l'    \t\t\tLiter\n\n"
					"For metric units any of the following prefixes can be used:\n"
					"\t'Y'  \tyotta \t\t1E+24\n"
					"\t'Z'  \tzetta \t\t1E+21\n"
					"\t'E'  \texa   \t\t1E+18\n"
					"\t'P'  \tpeta  \t\t1E+15\n"
					"\t'T'  \ttera  \t\t1E+12\n"
					"\t'G'  \tgiga  \t\t1E+09\n"
					"\t'M'  \tmega  \t\t1E+06\n"
					"\t'k'  \tkilo  \t\t1E+03\n"
					"\t'h'  \thecto \t\t1E+02\n"
					"\t'e'  \tdeca (deka)\t1E+01\n"
					"\t'd'  \tdeci  \t\t1E-01\n"
					"\t'c'  \tcenti \t\t1E-02\n"
					"\t'm'  \tmilli \t\t1E-03\n"
					"\t'u'  \tmicro \t\t1E-06\n"
					"\t'n'  \tnano  \t\t1E-09\n"
					"\t'p'  \tpico  \t\t1E-12\n"
					"\t'f'  \tfemto \t\t1E-15\n"
					"\t'a'  \tatto  \t\t1E-18\n"
					"\t'z'  \tzepto \t\t1E-21\n"
					"\t'y'  \tyocto \t\t1E-24") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible (except \"picapt\").") },
 	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CONVERT(3,\"lbm\",\"g\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=CONVERT(5.8,\"m\",\"in\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=CONVERT(7.9,\"cal\",\"J\")" },
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
	        for (i = 0; prefixes[i].str != NULL; i++)
		        if (strncmp (unit_name, prefixes[i].str, 1) == 0) {
			        *prefix = prefixes[i].c;
				unit_name++;
				break;
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

static GnmValue *
gnumeric_convert (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        /* Weight and mass constants */
#define one_g_to_sg     0.00006852205001
#define one_g_to_lbm    0.002204622915
#define one_g_to_u      6.02217e+23
#define one_g_to_ozm    0.035273972

	/* Distance constants */
#define one_m_to_mi     (one_m_to_yd / 1760)
#define one_m_to_Nmi    (1 / GNM_const (1852.0))
#define one_m_to_in     (10000 / GNM_const (254.0))
#define one_m_to_ft     (one_m_to_in / 12)
#define one_m_to_yd     (one_m_to_ft / 3)
#define one_m_to_ang    GNM_const (1e10)
#define one_m_to_pica   236.2204724409449
#define one_m_to_Pica   one_m_to_pica * 12

	/* Time constants */
#define one_yr_to_day   365.25
#define one_yr_to_hr    (24 * one_yr_to_day)
#define one_yr_to_mn    (60 * one_yr_to_hr)
#define one_yr_to_sec   (60 * one_yr_to_mn)

	/* Pressure constants */
#define one_Pa_to_atm   0.9869233e-5
#define one_Pa_to_mmHg  0.00750061708

	/* Force constants */
#define one_N_to_dyn    100000
#define one_N_to_lbf    0.224808924

	/* Power constants */
#define one_HP_to_W     745.701

	/* Energy constants */
#define one_J_to_e      9999995.193
#define one_J_to_c      0.239006249
#define one_J_to_cal    0.238846191
#define one_J_to_eV     6.2146e+18
#define one_J_to_HPh    (GNM_const (1.0) / (3600 * one_HP_to_W))
#define one_J_to_Wh     (GNM_const (1.0) / 3600)
#define one_J_to_flb    23.73042222
#define one_J_to_BTU    0.000947815

	/* Magnetism constants */
#define one_T_to_ga     10000

	/* Temperature constants */
	const gnm_float C_K_offset = GNM_const (273.15);

	/* Liquid measure constants */
#define one_tsp_to_tbs  (GNM_const (1.0) / 3)
#define one_tsp_to_oz   (GNM_const (1.0) / 6)
#define one_tsp_to_cup  (GNM_const (1.0) / 48)
#define one_tsp_to_pt   (GNM_const (1.0) / 96)
#define one_tsp_to_qt   (GNM_const (1.0) / 192)
#define one_tsp_to_gal  (GNM_const (1.0) / 768)
#define one_tsp_to_l    0.004929994

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

	static const eng_convert_unit_t weight_units[] = {
	        { "g",    1.0 },
		{ "sg",   one_g_to_sg },
		{ "lbm",  one_g_to_lbm },
		{ "u",    one_g_to_u },
		{ "ozm",  one_g_to_ozm },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t distance_units[] = {
	        { "m",    1.0 },
		{ "mi",   one_m_to_mi },
		{ "Nmi",  one_m_to_Nmi },
		{ "in",   one_m_to_in },
		{ "ft",   one_m_to_ft },
		{ "yd",   one_m_to_yd },
		{ "ang",  one_m_to_ang },
		{ "Pica", one_m_to_Pica },
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
		{ "atm",  one_Pa_to_atm },
		{ "mmHg", one_Pa_to_mmHg },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t force_units[] = {
	        { "N",    1.0 },
		{ "dyn",  one_N_to_dyn },
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
		{ "tbs",  one_tsp_to_tbs },
		{ "oz",   one_tsp_to_oz },
		{ "cup",  one_tsp_to_cup },
		{ "pt",   one_tsp_to_pt },
		{ "qt",   one_tsp_to_qt },
		{ "gal",  one_tsp_to_gal },
		{ "l",    one_tsp_to_l },
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

	gnm_float n;
	char const *from_unit, *to_unit;
	GnmValue *v;

	n = value_get_as_float (argv[0]);
	from_unit = value_peek_string (argv[1]);
	to_unit = value_peek_string (argv[2]);

	if (strcmp (from_unit, "C") == 0 && strcmp (to_unit, "F") == 0)
	        return value_new_float (n * 9 / 5 + 32);
	else if (strcmp (from_unit, "F") == 0 && strcmp (to_unit, "C") == 0)
	        return value_new_float ((n - 32) * 5 / 9);
	else if (strcmp (from_unit, "F") == 0 && strcmp (to_unit, "F") == 0)
	        return value_new_float (n);
	else if (strcmp (from_unit, "F") == 0 && strcmp (to_unit, "K") == 0)
	        return value_new_float ((n - 32) * 5 / 9 + C_K_offset);
	else if (strcmp (from_unit, "K") == 0 && strcmp (to_unit, "F") == 0)
	        return value_new_float ((n - C_K_offset) * 9 / 5 + 32);
	else if (strcmp (from_unit, "C") == 0 && strcmp (to_unit, "K") == 0)
	        return value_new_float (n + C_K_offset);
	else if (strcmp (from_unit, "K") == 0 && strcmp (to_unit, "C") == 0)
	        return value_new_float (n - C_K_offset);

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
	if (convert (magnetism_units, prefixes, from_unit, to_unit, n, &v,
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
	  gnumeric_base, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        { "besseli",     "ff",    help_besseli,
	  gnumeric_besseli, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "besselj",     "ff",    help_besselj,
	  gnumeric_besselj, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "besselk",     "ff",    help_besselk,
	  gnumeric_besselk, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bessely",     "ff",    help_bessely,
	  gnumeric_bessely, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "bin2dec",     "S",     help_bin2dec,
	  gnumeric_bin2dec, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bin2hex",     "S|f",   help_bin2hex,
	  gnumeric_bin2hex, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bin2oct",     "S|f",   help_bin2oct,
	  gnumeric_bin2oct, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "convert",     "fss",   help_convert,
	  gnumeric_convert, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2bin",     "S|f",   help_dec2bin,
	  gnumeric_dec2bin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2oct",     "S|f",   help_dec2oct,
	  gnumeric_dec2oct, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2hex",     "S|f",   help_dec2hex,
	  gnumeric_dec2hex, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "decimal",     "Sf",    help_decimal,
	  gnumeric_decimal, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        { "delta",       "f|f",   help_delta,
	  gnumeric_delta, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "erf",         "f|f",   help_erf,
	  gnumeric_erf , NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "erfc",        "f",     help_erfc,
	  gnumeric_erfc, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "gestep",      "f|f",   help_gestep,
	  gnumeric_gestep, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "hex2bin",     "S|f",   help_hex2bin,
	  gnumeric_hex2bin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "hex2dec",     "S",     help_hex2dec,
	  gnumeric_hex2dec, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "hex2oct",     "S|f",   help_hex2oct,
	  gnumeric_hex2oct, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "hexrep",     "f",   help_hexrep,
	  gnumeric_hexrep, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "invsuminv",    NULL,            help_invsuminv,
	  NULL, gnumeric_invsuminv, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        { "oct2bin",     "S|f",   help_oct2bin,
	  gnumeric_oct2bin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "oct2dec",     "S",     help_oct2dec,
	  gnumeric_oct2dec, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "oct2hex",     "S|f",   help_oct2hex,
	  gnumeric_oct2hex, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};
