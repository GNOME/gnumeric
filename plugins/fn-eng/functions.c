/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-eng.c:  Built in engineering functions and functions registration
 *
 * Authors:
 *   Michael Meeks <michael@ximian.com>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <complex.h>
#include <str.h>
#include <value.h>
#include <mathfunc.h>

#include <math.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/**
 * FIXME: In the long term this needs optimising.
 **/
static Value *
val_to_base (FunctionEvalInfo *ei, Value **argv, int num_argv,
	     int src_base, int dest_base)
{
	Value *value;
	int max, places;
	char *err, buffer[40];
	const char *str;
	gnm_float v, b10;
	int digit;

	g_return_val_if_fail (src_base > 1 && src_base <= 36,
			      value_new_error (ei->pos, gnumeric_err_VALUE));
	g_return_val_if_fail (dest_base > 1 && dest_base <= 36,
			      value_new_error (ei->pos, gnumeric_err_VALUE));

	value = argv[0];
	if (VALUE_IS_EMPTY (value))
		return value_new_error (ei->pos, gnumeric_err_NUM);
	else if (VALUE_IS_EMPTY_OR_ERROR (value))
		return value_duplicate (value);

	places = (num_argv >= 2 && argv[1]) ? value_get_as_int (argv[1]) : 0;
	str = value_peek_string (value);

	v = strtol (str, &err, src_base);
	if (*err)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	b10 = powgnum (src_base, 10);
	if (v >= b10 / 2) /* N's complement */
		v = v - b10;

	if (dest_base == 10)
		return value_new_int (v);

	if (v < 0) {
		max = 10;
		v += powgnum (dest_base, max);
	} else {
		if (v == 0)
			max = 1;
		else
			max = (int)(loggnum (v + 0.5) /
				    loggnum (dest_base)) + 1;
	}

	if (places > max)
		max = places;
	if (max >= (int)sizeof (buffer))
		return value_new_error (ei->pos, _("Unimplemented"));

	for (digit = max - 1; digit >= 0; digit--) {
		int thisdigit = fmodgnum (v + 0.5, dest_base);
		v = floorgnum ((v + 0.5) / dest_base);
		buffer[digit] = 
			thisdigit["0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"];
	}
	buffer[max] = 0;
	return value_new_string (buffer);
}

/***************************************************************************/

static const char *help_bin2dec = {
	N_("@FUNCTION=BIN2DEC\n"
	   "@SYNTAX=BIN2DEC(x)\n"

	   "@DESCRIPTION="
	   "BIN2DEC function converts a binary number "
	   "in string or number to its decimal equivalent.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BIN2DEC(101) equals 5.\n"
	   "\n"
	   "@SEEALSO=DEC2BIN, BIN2OCT, BIN2HEX")
};

static Value *
gnumeric_bin2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 2, 10);
}

/***************************************************************************/

static const char *help_bin2oct = {
	N_("@FUNCTION=BIN2OCT\n"
	   "@SYNTAX=BIN2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "BIN2OCT function converts a binary number to an octal number. "
	   "@places is an optional field, specifying to zero pad to that "
	   "number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BIN2OCT(110111) equals 67.\n"
	   "\n"
	   "@SEEALSO=OCT2BIN, BIN2DEC, BIN2HEX")
};

static Value *
gnumeric_bin2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 2, 8);
}

/***************************************************************************/

static const char *help_bin2hex = {
	N_("@FUNCTION=BIN2HEX\n"
	   "@SYNTAX=BIN2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "BIN2HEX function converts a binary number to a "
	   "hexadecimal number.  @places is an optional field, specifying "
	   "to zero pad to that number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BIN2HEX(100111) equals 27.\n"
	   "\n"
	   "@SEEALSO=HEX2BIN, BIN2OCT, BIN2DEC")
};

static Value *
gnumeric_bin2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 2, 16);
}

/***************************************************************************/

static const char *help_dec2bin = {
	N_("@FUNCTION=DEC2BIN\n"
	   "@SYNTAX=DEC2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "DEC2BIN function converts a decimal number to a binary number. "
	   "@places is an optional field, specifying to zero pad to that "
	   "number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DEC2BIN(42) equals 101010.\n"
	   "\n"
	   "@SEEALSO=BIN2DEC, DEC2OCT, DEC2HEX")
};

static Value *
gnumeric_dec2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 2);
}

/***************************************************************************/

static const char *help_dec2oct = {
	N_("@FUNCTION=DEC2OCT\n"
	   "@SYNTAX=DEC2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "DEC2OCT function converts a decimal number to an octal number. "
	   "@places is an optional field, specifying to zero pad to that "
	   "number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DEC2OCT(42) equals 52.\n"
	   "\n"
	   "@SEEALSO=OCT2DEC, DEC2BIN, DEC2HEX")
};

static Value *
gnumeric_dec2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 8);
}

/***************************************************************************/

static const char *help_dec2hex = {
	N_("@FUNCTION=DEC2HEX\n"
	   "@SYNTAX=DEC2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "DEC2HEX function converts a decimal number to a hexadecimal "
	   "number. @places is an optional field, specifying to zero pad "
	   "to that number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DEC2HEX(42) equals 2A.\n"
	   "\n"
	   "@SEEALSO=HEX2DEC, DEC2BIN, DEC2OCT")
};

static Value *
gnumeric_dec2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 16);
}

/***************************************************************************/

static const char *help_oct2dec = {
	N_("@FUNCTION=OCT2DEC\n"
	   "@SYNTAX=OCT2DEC(x)\n"

	   "@DESCRIPTION="
	   "OCT2DEC function converts an octal number "
	   "in a string or number to its decimal equivalent.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OCT2DEC(\"124\") equals 84.\n"
	   "\n"
	   "@SEEALSO=DEC2OCT, OCT2BIN, OCT2HEX")
};

static Value *
gnumeric_oct2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 8, 10);
}

/***************************************************************************/

static const char *help_oct2bin = {
	N_("@FUNCTION=OCT2BIN\n"
	   "@SYNTAX=OCT2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "OCT2BIN function converts an octal number to a binary "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OCT2BIN(\"213\") equals 10001011.\n"
	   "\n"
	   "@SEEALSO=BIN2OCT, OCT2DEC, OCT2HEX")
};

static Value *
gnumeric_oct2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 8, 2);
}

/***************************************************************************/

static const char *help_oct2hex = {
	N_("@FUNCTION=OCT2HEX\n"
	   "@SYNTAX=OCT2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "OCT2HEX function converts an octal number to a hexadecimal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OCT2HEX(132) equals 5A.\n"
	   "\n"
	   "@SEEALSO=HEX2OCT, OCT2BIN, OCT2DEC")
};

static Value *
gnumeric_oct2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 8, 16);
}

/***************************************************************************/

static const char *help_hex2bin = {
	N_("@FUNCTION=HEX2BIN\n"
	   "@SYNTAX=HEX2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "HEX2BIN function converts a hexadecimal number to a binary "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "HEX2BIN(\"2A\") equals 101010.\n"
	   "\n"
	   "@SEEALSO=BIN2HEX, HEX2OCT, HEX2DEC")
};

static Value *
gnumeric_hex2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 16, 2);
}

/***************************************************************************/

static const char *help_hex2oct = {
	N_("@FUNCTION=HEX2OCT\n"
	   "@SYNTAX=HEX2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "HEX2OCT function converts a hexadecimal number to an octal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces.\n"
	   "\n"
	   "* If @places is too small or negative #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "HEX2OCT(\"2A\") equals 52.\n"
	   "\n"
	   "@SEEALSO=OCT2HEX, HEX2BIN, HEX2DEC")
};

static Value *
gnumeric_hex2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 16, 8);
}

/***************************************************************************/

static const char *help_hex2dec = {
	N_("@FUNCTION=HEX2DEC\n"
	   "@SYNTAX=HEX2DEC(x)\n"

	   "@DESCRIPTION="
	   "HEX2DEC function converts a hexadecimal number "
	   "to its decimal equivalent.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "HEX2DEC(\"2A\") equals 42.\n"
	   "\n"
	   "@SEEALSO=DEC2HEX, HEX2BIN, HEX2OCT")
};

static Value *
gnumeric_hex2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 16, 10);
}

/***************************************************************************/

static const char *help_besseli = {
	N_("@FUNCTION=BESSELI\n"
	   "@SYNTAX=BESSELI(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELI function returns the Neumann, Weber or Bessel "
	   "function.\n\n"
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated.\n"
	   "\n"
	   "* If @x or @y are not numeric a #VALUE! error is returned.\n"
	   "* If @y < 0 a #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELI(0.7,3) equals 0.007367374.\n"
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};


static Value *
gnumeric_besseli (FunctionEvalInfo *ei, Value **argv)
{
	gnm_float x, order;

	x = value_get_as_float (argv[0]);	/* value to evaluate I_n at. */
	order = value_get_as_float (argv[1]);	/* the order */

	if (order < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (bessel_i (x, order, 1.0));
}

/***************************************************************************/

static const char *help_besselk = {
	N_("@FUNCTION=BESSELK\n"
	   "@SYNTAX=BESSELK(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELK function returns the Neumann, Weber or Bessel "
	   "function. "
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated.\n"
	   "\n"
	   "* If x or n are not numeric a #VALUE! error is returned.\n"
	   "* If y < 0 a #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELK(3,9) equals 397.95880.\n"
	   "\n"
	   "@SEEALSO=BESSELI,BESSELJ,BESSELY")
};

static Value *
gnumeric_besselk (FunctionEvalInfo *ei, Value **argv)
{
	gnm_float x, order;

	x = value_get_as_float (argv[0]);	/* value to evaluate K_n at. */
	order = value_get_as_float (argv[1]);	/* the order */

	if (order < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (bessel_k (x, order, 1.0));
}

/***************************************************************************/

static const char *help_besselj = {
	N_("@FUNCTION=BESSELJ\n"
	   "@SYNTAX=BESSELJ(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELJ function returns the bessel function with "
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated.\n"
	   "\n"
	   "* If @x or @y are not numeric a #VALUE! error is returned.\n"
	   "* If @y < 0 a #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELJ(0.89,3) equals 0.013974004.\n"
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_besselj (FunctionEvalInfo *ei, Value **argv)
{
	int x, y;

	x = value_get_as_int (argv[0]);
	y = value_get_as_int (argv[1]);

	if (y < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (jn (y, value_get_as_float (argv[0])));
}

/***************************************************************************/

static const char *help_bessely = {
	N_("@FUNCTION=BESSELY\n"
	   "@SYNTAX=BESSELY(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELY function returns the Neumann, Weber or Bessel "
	   "function.\n\n"
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated.\n"
	   "\n"
	   "* If x or n are not numeric a #VALUE! error is returned.\n"
	   "* If n < 0 a #NUM! error is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELY(4,2) equals 0.215903595.\n"
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_bessely (FunctionEvalInfo *ei, Value **argv)
{
	int y;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_FLOAT)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if ((y = value_get_as_int (argv[1])) < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (yngnum (y, value_get_as_float (argv[0])));
}

/***************************************************************************/

static const char *help_convert = {
	N_("@FUNCTION=CONVERT\n"
	   "@SYNTAX=CONVERT(number,from_unit,to_unit)\n"
	   "@DESCRIPTION="
	   "CONVERT returns a conversion from one measurement system to "
	   "another.  For example, you can convert a weight in pounds "
	   "to a weight in grams.  @number is the value you want to "
	   "convert, @from_unit specifies the unit of the @number, and "
	   "@to_unit is the unit for the result.\n"
	   "\n"
	   "@from_unit and @to_unit can be any of the following:\n\n"
	   "Weight and mass:\n"
	   "\t'g'  \t\tGram\n"
           "\t'sg' \t\tSlug\n"
	   "\t'lbm'\t\tPound\n"
	   "\t'u'  \t\tU (atomic mass)\n"
	   "\t'ozm'\t\tOunce\n\n"
	   "Distance:\n"
	   "\t'm'   \t\tMeter\n"
	   "\t'mi'  \t\tStatute mile\n"
	   "\t'Nmi' \t\tNautical mile\n"
	   "\t'in'  \t\tInch\n"
	   "\t'ft'  \t\tFoot\n"
	   "\t'yd'  \t\tYard\n"
	   "\t'ang' \t\tAngstrom\n"
	   "\t'Pica'\t\tPica\n\n"
	   "Time:\n"
	   "\t'yr'  \t\tYear\n"
	   "\t'day' \t\tDay\n"
	   "\t'hr'  \t\tHour\n"
	   "\t'mn'  \t\tMinute\n"
	   "\t'sec' \t\tSecond\n\n"
	   "Pressure:\n"
	   "\t'Pa'  \t\tPascal\n"
	   "\t'atm' \t\tAtmosphere\n"
	   "\t'mmHg'\tmm of Mercury\n\n"
	   "Force:\n"
	   "\t'N'   \t\tNewton\n"
	   "\t'dyn' \t\tDyne\n"
	   "\t'lbf' \t\tPound force\n\n"
	   "Energy:\n"
	   "\t'J'    \t\tJoule\n"
	   "\t'e'    \t\tErg\n"
	   "\t'c'    \t\tThermodynamic calorie\n"
	   "\t'cal'  \t\tIT calorie\n"
	   "\t'eV'   \tElectron volt\n"
	   "\t'HPh'  \tHorsepower-hour\n"
	   "\t'Wh'   \tWatt-hour\n"
	   "\t'flb'  \t\tFoot-pound\n"
	   "\t'BTU'  \tBTU\n\n"
	   "Power:\n"
	   "\t'HP'   \tHorsepower\n"
	   "\t'W'    \tWatt\n\n"
	   "Magnetism:\n"
	   "\t'T'    \t\tTesla\n"
	   "\t'ga'   \tGauss\n\n"
	   "Temperature:\n"
	   "\t'C'    \t\tDegree Celsius\n"
	   "\t'F'    \t\tDegree Fahrenheit\n"
	   "\t'K'    \t\tDegree Kelvin\n\n"
	   "Liquid measure:\n"
	   "\t'tsp'  \t\tTeaspoon\n"
	   "\t'tbs'  \t\tTablespoon\n"
	   "\t'oz'   \t\tFluid ounce\n"
	   "\t'cup'  \tCup\n"
	   "\t'pt'   \t\tPint\n"
	   "\t'qt'   \t\tQuart\n"
	   "\t'gal'  \t\tGallon\n"
	   "\t'l'    \t\tLiter\n\n"
	   "For metric units any of the following prefixes can be used:\n"
	   "\t'Y'  \tyotta \t1E+24\n"
	   "\t'Z'  \tzetta \t1E+21\n"
	   "\t'E'  \texa   \t1E+18\n"
	   "\t'P'  \tpeta  \t1E+15\n"
	   "\t'T'  \ttera  \t\t1E+12\n"
	   "\t'G'  \tgiga  \t1E+09\n"
	   "\t'M'  \tmega  \t1E+06\n"
	   "\t'k'  \tkilo  \t\t1E+03\n"
	   "\t'h'  \thecto \t1E+02\n"
	   "\t'e'  \tdeka  \t1E+01\n"
	   "\t'd'  \tdeci  \t1E-01\n"
	   "\t'c'  \tcenti \t\t1E-02\n"
	   "\t'm'  \tmilli \t\t1E-03\n"
	   "\t'u'  \tmicro \t1E-06\n"
	   "\t'n'  \tnano  \t1E-09\n"
	   "\t'p'  \tpico  \t1E-12\n"
	   "\t'f'  \tfemto \t1E-15\n"
	   "\t'a'  \tatto  \t\t1E-18\n"
	   "\t'z'  \tzepto \t\t1E-21\n"
	   "\t'y'  \tyocto \t\t1E-24\n"
	   "\n"
	   "* If @from_unit and @to_unit are different types, CONVERT returns "
	   "#NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CONVERT(3,\"lbm\",\"g\") equals 1360.7769.\n"
	   "CONVERT(5.8,\"m\",\"in\") equals 228.3465.\n"
	   "CONVERT(7.9,\"cal\",\"J\") equals 33.07567.\n"
	   "\n"
	   "@SEEALSO=")
};

typedef struct {
        const char *str;
	gnm_float c;
} eng_convert_unit_t;


static gnm_float
get_constant_of_unit(const eng_convert_unit_t units[],
		     const eng_convert_unit_t prefixes[],
		     const char *unit_name,
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

static Value *
convert (const eng_convert_unit_t units[],
	 const eng_convert_unit_t prefixes[],
	 const char *from_unit, const char *to_unit,
	 gnm_float n, Value **v, const EvalPos *ep)
{
        gnm_float from_c, from_prefix, to_c, to_prefix;

	if (get_constant_of_unit (units, prefixes, from_unit, &from_c,
				  &from_prefix)) {

	        if (!get_constant_of_unit (units, prefixes,
					   to_unit, &to_c, &to_prefix))
			return value_new_error (ep, gnumeric_err_NUM);

	        if (from_c == 0 || to_prefix == 0)
	                return value_new_error (ep, gnumeric_err_NUM);

		*v = value_new_float (((n * from_prefix) / from_c) *
				 to_c / to_prefix);
		return *v;
	}

	return NULL;
}

static Value *
gnumeric_convert (FunctionEvalInfo *ei, Value **argv)
{
        /* Weight and mass constants */
        #define one_g_to_sg     0.00006852205001
	#define one_g_to_lbm    0.002204622915
	#define one_g_to_u      6.02217e+23
        #define one_g_to_ozm    0.035273972

	/* Distance constants */
	#define one_m_to_mi     (one_m_to_yd / 1760)
	#define one_m_to_Nmi    (1 / GNUM_const (1852.0))
	#define one_m_to_in     (10000 / GNUM_const (254.0))
	#define one_m_to_ft     (one_m_to_in / 12)
	#define one_m_to_yd     (one_m_to_ft / 3)
	#define one_m_to_ang    GNUM_const (1e10)
	#define one_m_to_Pica   2834.645669

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
	#define one_J_to_HPh    (GNUM_const (1.0) / (3600 * one_HP_to_W))
	#define one_J_to_Wh     (GNUM_const (1.0) / 3600)
	#define one_J_to_flb    23.73042222
	#define one_J_to_BTU    0.000947815

	/* Magnetism constants */
	#define one_T_to_ga     10000

	/* Temperature constants */
	const gnm_float C_K_offset = GNUM_const (273.15);

	/* Liquid measure constants */
	#define one_tsp_to_tbs  (GNUM_const (1.0) / 3)
	#define one_tsp_to_oz   (GNUM_const (1.0) / 6)
	#define one_tsp_to_cup  (GNUM_const (1.0) / 48)
	#define one_tsp_to_pt   (GNUM_const (1.0) / 96)
	#define one_tsp_to_qt   (GNUM_const (1.0) / 192)
	#define one_tsp_to_gal  (GNUM_const (1.0) / 768)
	#define one_tsp_to_l    0.004929994

	/* Prefixes */
	#define yotta  GNUM_const (1e+24)
	#define zetta  GNUM_const (1e+21)
	#define exa    GNUM_const (1e+18)
	#define peta   GNUM_const (1e+15)
	#define tera   GNUM_const (1e+12)
	#define giga   GNUM_const (1e+09)
	#define mega   GNUM_const (1e+06)
	#define kilo   GNUM_const (1e+03)
	#define hecto  GNUM_const (1e+02)
	#define deka   GNUM_const (1e+01)
	#define deci   GNUM_const (1e-01)
	#define centi  GNUM_const (1e-02)
	#define milli  GNUM_const (1e-03)
	#define micro  GNUM_const (1e-06)
	#define nano   GNUM_const (1e-09)
	#define pico   GNUM_const (1e-12)
	#define femto  GNUM_const (1e-15)
	#define atto   GNUM_const (1e-18)
	#define zepto  GNUM_const (1e-21)
	#define yocto  GNUM_const (1e-24)

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
	const char *from_unit, *to_unit;
	Value *v;

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

	return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_erf = {
	N_("@FUNCTION=ERF\n"
	   "@SYNTAX=ERF([lower limit,]upper_limit)\n"

	   "@DESCRIPTION="
	   "ERF returns the error function.  With a single argument ERF "
	   "returns the error function, defined as\n\n\t"
	   "erf(x) = 2/sqrt(pi)* integral from 0 to x of exp(-t*t) dt.\n\n"
	   "If two arguments are supplied, they are the lower and upper "
	   "limits of the integral.\n"
	   "\n"
	   "* If either @lower_limit or @upper_limit is not numeric a "
	   "#VALUE! error is returned.\n"
	   "* This function is upward-compatible with that in Excel. "
	   "(If two arguments are supplied, "
	   "Excel will not allow either to be negative.)\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ERF(0.4) equals 0.428392355.\n"
	   "ERF(1.6448536269515/SQRT(2)) equals 0.90.\n"
	   "\n"
	   "The second example shows that a random variable with a normal "
	   "distribution has a 90 percent chance of falling within "
	   "approximately 1.645 standard deviations of the mean."
	   "\n"
	   "@SEEALSO=ERFC")
};


static Value *
gnumeric_erf (FunctionEvalInfo *ei, Value **argv)
{
	gnm_float ans, lower, upper;

	lower = value_get_as_float (argv[0]);
	ans = erfgnum (lower);

	if (argv[1]) {
		upper = value_get_as_float (argv[1]);
		ans = erfgnum (upper) - ans;
	}

	return value_new_float (ans);
}

/***************************************************************************/

static const char *help_erfc = {
	N_("@FUNCTION=ERFC\n"
	   "@SYNTAX=ERFC(x)\n"

	   "@DESCRIPTION="
	   "ERFC function returns the complementary "
	   "error function, defined as\n\n\t1 - erf(x).\n\n"
	   "erfc(x) is calculated more accurately than 1 - erf(x) for "
	   "arguments larger than about 0.5.\n"
	   "\n"
	   "* If @x is not numeric a #VALUE! error is returned.  "
	   "\n"
	   "@EXAMPLES=\n"
	   "ERFC(6) equals 2.15197367e-17.\n"
	   "\n"
	   "@SEEALSO=ERF")
};

static Value *
gnumeric_erfc (FunctionEvalInfo *ei, Value **argv)
{
	gnm_float x;

	x = value_get_as_float (argv[0]);

	return value_new_float (erfcgnum (x));
}

/***************************************************************************/

static const char *help_delta = {
	N_("@FUNCTION=DELTA\n"
	   "@SYNTAX=DELTA(x[,y])\n"

	   "@DESCRIPTION="
	   "DELTA function tests for numerical equivalence of two "
	   "arguments, returning 1 in case of equality.\n\n"
	   "* @y is optional, and defaults to 0."
	   "* If either argument is non-numeric returns a #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DELTA(42.99,43) equals 0.\n"
	   "\n"
	   "@SEEALSO=EXACT,GESTEP")
};


static Value *
gnumeric_delta (FunctionEvalInfo *ei, Value **argv)
{
	Value *err = NULL;
	gboolean ans = FALSE;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_new_int (0);

	/* Promote to the largest value */
	switch ((vx->type > vy->type) ? vx->type : vy->type) {
	case VALUE_BOOLEAN:
		/* Only happens when both are bool */
		ans = vx->v_bool.val == vy->v_bool.val;
		break;
	case VALUE_EMPTY:
	case VALUE_INTEGER:
		ans = value_get_as_int (vx) == value_get_as_int (vy);
		break;
	case VALUE_FLOAT:
		ans = value_get_as_float (vx) == value_get_as_float (vy);
		break;
	default:
		err = value_new_error (ei->pos, _("Impossible"));
	}
	if (!argv[1])
		value_release (vy);

	return (err != NULL) ? err : value_new_int (ans ? 1 : 0);
}

/***************************************************************************/

static const char *help_gestep = {
	N_("@FUNCTION=GESTEP\n"
	   "@SYNTAX=GESTEP(x[,y])\n"
	   "@DESCRIPTION="
	   "GESTEP function test for if @x is >= @y, returning 1 if it "
	   "is so, and 0 otherwise. @y is optional, and defaults to 0.\n"
	   "\n"
	   "* If either argument is non-numeric returns a #VALUE! error.\n"
	   "* This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "GESTEP(5,4) equals 1.\n"
	   "\n"
	   "@SEEALSO=DELTA")
};


static Value *
gnumeric_gestep (FunctionEvalInfo *ei, Value **argv)
{
	Value *err = NULL;
	gboolean ans = FALSE;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_new_int (0);

	/* Promote to the largest value */
	switch ((vx->type > vy->type) ? vx->type : vy->type) {
	case VALUE_BOOLEAN:
		/* Only happens when both are bool */
		ans = vx->v_bool.val >= vy->v_bool.val;
		break;
	case VALUE_EMPTY:
	case VALUE_INTEGER:
		ans = value_get_as_int (vx) >= value_get_as_int (vy);
		break;
	case VALUE_FLOAT:
		ans = value_get_as_float (vx) >= value_get_as_float (vy);
		break;
	default:
		err = value_new_error (ei->pos, _("Impossible"));
	}

	if (!argv[1])
		value_release (vy);
	return (err != NULL) ? err : value_new_int (ans ? 1 : 0);
}

/***************************************************************************/

const GnmFuncDescriptor engineering_functions[] = {
        { "besseli",     "ff",   "xnum,ynum", &help_besseli,
	  gnumeric_besseli, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "besselj",     "ff",   "xnum,ynum", &help_besselj,
	  gnumeric_besselj, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "besselk",     "ff",   "xnum,ynum", &help_besselk,
	  gnumeric_besselk, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bessely",     "ff",   "xnum,ynum", &help_bessely,
	  gnumeric_bessely, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "bin2dec",     "S",    "number", &help_bin2dec,
	  gnumeric_bin2dec, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bin2hex",     "S|f",  "xnum,ynum", &help_bin2hex,
	  gnumeric_bin2hex, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "bin2oct",     "S|f",  "xnum,ynum", &help_bin2oct,
	  gnumeric_bin2oct, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "convert",     "fss",  "number,from_unit,to_unit", &help_convert,
	  gnumeric_convert, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2bin",     "S|f",  "xnum,ynum", &help_dec2bin,
	  gnumeric_dec2bin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2oct",     "S|f",  "xnum,ynum", &help_dec2oct,
	  gnumeric_dec2oct, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dec2hex",     "S|f",  "xnum,ynum", &help_dec2hex,
	  gnumeric_dec2hex, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "delta",       "f|f",  "xnum,ynum", &help_delta,
	  gnumeric_delta, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "erf",         "f|f",  "lower,upper", &help_erf,
	  gnumeric_erf , NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "erfc",        "f",    "number", &help_erfc,
	  gnumeric_erfc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "gestep",      "f|f",  "xnum,ynum", &help_gestep,
	  gnumeric_gestep, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "hex2bin",     "S|f",  "xnum,ynum", &help_hex2bin,
	  gnumeric_hex2bin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "hex2dec",     "S",    "number", &help_hex2dec,
	  gnumeric_hex2dec, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "hex2oct",     "S|f",  "xnum,ynum", &help_hex2oct,
	  gnumeric_hex2oct, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "oct2bin",     "S|f",  "xnum,ynum", &help_oct2bin,
	  gnumeric_oct2bin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "oct2dec",     "S",    "number", &help_oct2dec,
	  gnumeric_oct2dec, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "oct2hex",     "S|f",  "xnum,ynum", &help_oct2hex,
	  gnumeric_oct2hex, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};
