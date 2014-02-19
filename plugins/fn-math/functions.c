/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-math.c:  Built in mathematical functions and functions registration
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@gnome.org)
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
#include <cell.h>
#include <sheet.h>
#include <workbook.h>
#include <mathfunc.h>
#include <sf-trig.h>
#include <sf-gamma.h>
#include <rangefunc.h>
#include <collect.h>
#include <value.h>
#include <expr.h>
#include <position.h>
#include <regression.h>
#include <gnm-i18n.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

#include <math.h>
#include <string.h>

#define UNICODE_PI "\360\235\234\213"
#define UNICODE_MINUS "\xe2\x88\x92"

GNM_PLUGIN_MODULE_HEADER;


#define FUNCTION_A_DESC   GNM_FUNC_HELP_DESCRIPTION, F_("Numbers, text and logical values are "	\
							"included in the calculation too. If the cell contains text or " \
							"the argument evaluates to FALSE, it is counted as value zero (0). " \
							"If the argument evaluates to TRUE, it is counted as one (1).")

/***************************************************************************/

static GnmFuncHelp const help_gcd[] = {
	{ GNM_FUNC_HELP_NAME, F_("GCD:the greatest common divisor")},
	{ GNM_FUNC_HELP_ARG, F_("n0:positive integer")},
	{ GNM_FUNC_HELP_ARG, F_("n1:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("GCD calculates the greatest common divisor of the given numbers @{n0},@{n1},..., the greatest integer that is a divisor of each argument.")},
	{ GNM_FUNC_HELP_NOTE, F_("If any of the arguments is not an integer, it is truncated.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GCD(470,770)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=GCD(470,770,1495)" },
	{ GNM_FUNC_HELP_SEEALSO, "LCM"},
	{ GNM_FUNC_HELP_END}
};

static const gnm_float gnm_gcd_max = 1 / GNM_EPSILON;

static gnm_float
gnm_gcd (gnm_float a, gnm_float b)
{
	while (b > 0.5) {
		gnm_float r = gnm_fmod (a, b);
		a = b;
		b = r;
	}
	return a;
}

static int
range_gcd (gnm_float const *xs, int n, gnm_float *res)
{
	if (n > 0) {
		int i;
		gnm_float gcd_so_far = gnm_fake_floor (xs[0]);

		for (i = 0; i < n; i++) {
			gnm_float thisx = gnm_fake_floor (xs[i]);
			if (thisx < 0 || thisx > gnm_gcd_max)
				return 1;
			else
				gcd_so_far = gnm_gcd (thisx, gcd_so_far);
		}

		if (gcd_so_far == 0)
			return 1;

		*res = gcd_so_far;
		return 0;
	} else
		return 1;
}

static GnmValue *
gnumeric_gcd (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_gcd,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_lcm[] = {
	{ GNM_FUNC_HELP_NAME, F_("LCM:the least common multiple")},
	{ GNM_FUNC_HELP_ARG, F_("n0:positive integer")},
	{ GNM_FUNC_HELP_ARG, F_("n1:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("LCM calculates the least common multiple of the given numbers @{n0},@{n1},..., the smallest integer that is a multiple of each argument.")},
	{ GNM_FUNC_HELP_NOTE, F_("If any of the arguments is not an integer, it is truncated.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LCM(2,13)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=LCM(4,7,5)" },
	{ GNM_FUNC_HELP_SEEALSO, "GCD"},
	{ GNM_FUNC_HELP_END}
};

static gnm_float
gnm_lcm (gnm_float a, gnm_float b)
{
	return a * (b / gnm_gcd (a, b));
}

static int
range_lcm (gnm_float const *xs, int n, gnm_float *res)
{
	int i;
	gnm_float lcm;

	if (n <= 0)
		return 1;

	lcm = 1;
	for (i = 0; i < n; i++) {
		gnm_float thisx = gnm_fake_floor (xs[i]);
		if (thisx == 1)
			continue;
		if (thisx < 1 || thisx > gnm_gcd_max || lcm > gnm_gcd_max)
			return 1;
		lcm = gnm_lcm (lcm, thisx);
	}

	*res = lcm;
	return 0;
}

static GnmValue *
gnumeric_lcm (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_lcm,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);

}

/***************************************************************************/

static GnmFuncHelp const help_gd[] = {
	{ GNM_FUNC_HELP_NAME, F_("GD:Gudermannian function")},
	{ GNM_FUNC_HELP_ARG, F_("x:value")},
	{ GNM_FUNC_HELP_EXAMPLES, "=GD(0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "TAN,TANH" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Gudermannian.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Gudermannian_function") },
	{ GNM_FUNC_HELP_END }
};

static gnm_float
gnm_gd (gnm_float x)
{
	return 2 * gnm_atan (gnm_tanh (x / 2));
}

static GnmValue *
gnumeric_gd (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_gd (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_hypot[] = {
	{ GNM_FUNC_HELP_NAME, F_("HYPOT:the square root of the sum of the squares of the arguments")},
	{ GNM_FUNC_HELP_ARG, F_("n0:number")},
	{ GNM_FUNC_HELP_ARG, F_("n1:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=HYPOT(3,4)" },
	{ GNM_FUNC_HELP_SEEALSO, "MIN,MAX"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_hypot (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_hypot,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);

}

/***************************************************************************/

static GnmFuncHelp const help_abs[] = {
	{ GNM_FUNC_HELP_NAME, F_("ABS:absolute value")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ABS gives the absolute value of @{x}, i.e. the non-negative number of the same magnitude as @{x}.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ABS(7)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ABS(-3.14)" },
	{ GNM_FUNC_HELP_SEEALSO, "CEIL,CEILING,FLOOR,INT,MOD"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_abs (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_abs (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_acos[] = {
	{ GNM_FUNC_HELP_NAME, F_("ACOS:the arc cosine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ACOS(0.1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ACOS(-0.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "COS,SIN,DEGREES,RADIANS"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_acos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t < -1.0 || t > 1.0)
		return value_new_error_NUM (ei->pos);
	return value_new_float (gnm_acos (t));
}

/***************************************************************************/

static GnmFuncHelp const help_acosh[] = {
	{ GNM_FUNC_HELP_NAME, F_("ACOSH:the hyperbolic arc cosine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ACOSH(0.1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ACOSH(-0.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "ACOS,ASINH"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_acosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t < 1.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_acosh (t));
}

/***************************************************************************/

static GnmFuncHelp const help_acot[] = {
	{ GNM_FUNC_HELP_NAME, F_("ACOT:inverse cotangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:value")},
	{ GNM_FUNC_HELP_EXAMPLES, "=ACOT(0.2)" },
	{ GNM_FUNC_HELP_SEEALSO, "COT,TAN" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:InverseCotangent.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Trigonometric_functions") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_acot (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_acot (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_acoth[] = {
	{ GNM_FUNC_HELP_NAME, F_("ACOTH:the inverse hyperbolic cotangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=ACOTH(2.2)" },
	{ GNM_FUNC_HELP_SEEALSO, "COTH,TANH" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:InverseHyperbolicCotangent.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Inverse_hyperbolic_function") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_acoth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_acoth (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_asin[] = {
	{ GNM_FUNC_HELP_NAME, F_("ASIN:the arc sine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ASIN calculates the arc sine of @{x}; that is the value whose sine is @{x}.")},
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} falls outside the range -1 to 1, "
				 "ASIN returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ASIN(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ASIN(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,COS,ASINH,DEGREES,RADIANS"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_asin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t < -1.0 || t > 1.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_asin (t));
}

/***************************************************************************/

static GnmFuncHelp const help_asinh[] = {
	{ GNM_FUNC_HELP_NAME, F_("ASINH:the inverse hyperbolic sine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ASINH calculates the inverse hyperbolic sine of @{x}; that is the value whose hyperbolic sine is @{x}.")},
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ASINH(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ASINH(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "ASIN,ACOSH,SIN,COS"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_asinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_asinh (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_atan[] = {
	{ GNM_FUNC_HELP_NAME, F_("ATAN:the arc tangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ATAN calculates the arc tangent "
					"of @{x}; that is the value whose "
					"tangent is @{x}.")},
	{ GNM_FUNC_HELP_NOTE, F_("The result will be between "
				 "\xe2\x88\x92" "\xcf\x80" "/2 and "
				 "+" "\xcf\x80" "/2.")},
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ATAN(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ATAN(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "TAN,COS,SIN,DEGREES,RADIANS"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_atan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_atan (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_atanh[] = {
	{ GNM_FUNC_HELP_NAME, F_("ATANH:the inverse hyperbolic tangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ATANH calculates the inverse hyperbolic tangent of @{x}; that is the value whose hyperbolic tangent is @{x}.")},
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the absolute value of @{x} is greater than 1.0, ATANH returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ATANH(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ATANH(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "ATAN,COS,SIN"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_atanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t <= -1.0 || t >= 1.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_atanh (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_atan2[] = {
	{ GNM_FUNC_HELP_NAME, F_("ATAN2:the arc tangent of the ratio "
				 "@{y}/@{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:x-coordinate")},
	{ GNM_FUNC_HELP_ARG, F_("y:y-coordinate")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ATAN2 calculates the direction from "
					"the origin to the point (@{x},@{y}) "
					"as an angle from the x-axis in "
					"radians.")},
	{ GNM_FUNC_HELP_NOTE, F_("The result will be between "
				 "\xe2\x88\x92" "\xcf\x80" " and "
				 "+" "\xcf\x80" ".")},
	{ GNM_FUNC_HELP_NOTE, F_("The order of the arguments may be "
				 "unexpected.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ATAN2(0.5,1.0)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ATAN2(-0.5,2.0)" },
	{ GNM_FUNC_HELP_SEEALSO, "ATAN,ATANH,COS,SIN"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_atan2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float y = value_get_as_float (argv[1]);

	if (x == 0 && y == 0)
		return value_new_error_DIV0 (ei->pos);

	return value_new_float (gnm_atan2 (y, x));
}

/***************************************************************************/

static GnmFuncHelp const help_ceil[] = {
	{ GNM_FUNC_HELP_NAME, F_("CEIL:smallest integer larger than or equal to @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CEIL(@{x}) is the smallest integer that is at least as large as @{x}.")},
	{ GNM_FUNC_HELP_ODF, F_("This function is the OpenFormula function CEILING(@{x}).")},
	{ GNM_FUNC_HELP_EXAMPLES, "=CEIL(0.4)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=CEIL(-1.1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=CEIL(-2.9)" },
	{ GNM_FUNC_HELP_SEEALSO, "CEILING,FLOOR,ABS,INT,MOD"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ceil (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_fake_ceil (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_countif[] = {
	{ GNM_FUNC_HELP_NAME, F_("COUNTIF:count of the cells meeting the given @{criteria}")},
	{ GNM_FUNC_HELP_ARG, F_("range:cell area")},
	{ GNM_FUNC_HELP_ARG, F_("criteria:condition for a cell to be counted")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "COUNT,SUMIF"},
	{ GNM_FUNC_HELP_END}
};

typedef struct {
	GnmCriteria *crit;
	int count;
} CountIfClosure;

static GnmValue *
cb_countif (GnmCellIter const *iter, CountIfClosure *res)
{
	GnmCell *cell = iter->cell;
	GnmValue *v;

	if (cell) {
		gnm_cell_eval (cell);
		v = cell->value;
	} else
		v = value_new_empty ();  /* Never released */

	if (!VALUE_IS_EMPTY (v) && !VALUE_IS_NUMBER (v) && !VALUE_IS_STRING (v))
		return NULL;

	if (!res->crit->fun (v, res->crit))
		return NULL;

	res->count++;

	return NULL;
}

static GnmValue *
gnumeric_countif (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValueRange const *r = &argv[0]->v_range;
	Sheet		*sheet;
	GnmValue        *problem;
	CountIfClosure   res;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	/* XL has some limitations on @range that we currently emulate, but do
	 * not need to.
	 * 1) @range must be a range, arrays are not supported
	 * 2) @range can not be 3d */
	if (r->type != VALUE_CELLRANGE ||
	    ((sheet = eval_sheet (r->cell.a.sheet, ei->pos->sheet)) != r->cell.b.sheet &&
	     r->cell.b.sheet != NULL) ||
	    (!VALUE_IS_NUMBER (argv[1]) && !VALUE_IS_STRING (argv[1])))
	        return value_new_error_VALUE (ei->pos);

	res.count = 0;
	res.crit = parse_criteria (argv[1], date_conv);
	problem = sheet_foreach_cell_in_range
		(sheet, res.crit->iter_flags,
		 r->cell.a.col, r->cell.a.row, r->cell.b.col, r->cell.b.row,
		 (CellIterFunc) &cb_countif, &res);
	free_criteria (res.crit);

	if (NULL != problem)
	        return value_new_error_VALUE (ei->pos);

	return value_new_int (res.count);
}

/***************************************************************************/

static GnmFuncHelp const help_sumif[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMIF:sum of the cells in @{actual_range} for which the corresponding cells in the range meet the given @{criteria}")},
	{ GNM_FUNC_HELP_ARG, F_("range:cell area")},
	{ GNM_FUNC_HELP_ARG, F_("criteria:condition for a cell to be summed")},
	{ GNM_FUNC_HELP_ARG, F_("actual_range:cell area, defaults to @{range}")},
	{ GNM_FUNC_HELP_NOTE, F_("If the @{actual_range} has a size that "
				 "differs"
				 " from the size of @{range}, @{actual_range} "
				 "is resized (retaining the top-left corner)"
				 " to match the size of @{range}.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "SUM,COUNTIF"},
	{ GNM_FUNC_HELP_END}
};

typedef struct {
	GnmCriteria *crit;
	Sheet *target_sheet;
	int offset_col, offset_row;
	gnm_float sum;
	int count;
} SumIfClosure;

static GnmValue *
cb_sumif (GnmCellIter const *iter, SumIfClosure *res)
{
	GnmCell *cell = iter->cell;
	GnmValue *v;

	if (cell) {
		gnm_cell_eval (cell);
		v = cell->value;
	} else
		v = value_new_empty ();  /* Never released */

	if (!VALUE_IS_EMPTY (v) && !VALUE_IS_NUMBER (v) && !VALUE_IS_STRING (v))
		return NULL;

	if (!res->crit->fun (v, res->crit))
		return NULL;

	if (NULL != res->target_sheet) {
		GnmCell *cell = sheet_cell_get
			(res->target_sheet,
			 iter->pp.eval.col + res->offset_col,
			 iter->pp.eval.row + res->offset_row);
		if (!cell)
			return NULL;

		gnm_cell_eval (cell);
		v = cell->value;
	}

	if (!VALUE_IS_FLOAT (v))
		return NULL;

	res->sum += value_get_as_float (v);
	res->count++;

	return NULL;
}

static GnmValue *
gnumeric_sumif (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmRange rs;
	Sheet *start_sheet, *end_sheet;
	SumIfClosure res;
	GnmValue *problem;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	/* XL has some limitations on @range that we currently emulate, but do
	 * not need to.
	 * 1) @range must be a range, arrays are not supported
	 * 2) @range can not be 3d */
	if (argv[0]->type != VALUE_CELLRANGE ||
	    (!VALUE_IS_NUMBER (argv[1]) && !VALUE_IS_STRING (argv[1])) ||
	    (argv[2] != NULL && argv[2]->type != VALUE_CELLRANGE))
	        return value_new_error_VALUE (ei->pos);

	gnm_rangeref_normalize (&argv[0]->v_range.cell, ei->pos,
				&start_sheet, &end_sheet,
				&rs);
	if (start_sheet != end_sheet)
		return value_new_error_VALUE (ei->pos);

	if (argv[2]) {
		GnmRange ra;
		/* See 557782.  */
		gnm_rangeref_normalize (&argv[2]->v_range.cell, ei->pos,
					&res.target_sheet, &end_sheet,
					&ra);
		if (res.target_sheet != end_sheet)
			return value_new_error_VALUE (ei->pos);

		res.offset_col = ra.start.col - rs.start.col;
		res.offset_row = ra.start.row - rs.start.row;
	} else
		res.target_sheet = NULL;

	res.sum = 0;
	res.count = 0;
	res.crit = parse_criteria (argv[1], date_conv);
	problem = sheet_foreach_cell_in_range
		(start_sheet, res.crit->iter_flags,
		 rs.start.col, rs.start.row, rs.end.col, rs.end.row,
		 (CellIterFunc) &cb_sumif, &res);
	free_criteria (res.crit);

	if (NULL != problem)
	        return value_new_error_VALUE (ei->pos);
	return value_new_float (res.sum);
}

/***************************************************************************/

static GnmFuncHelp const help_averageif[] = {
	{ GNM_FUNC_HELP_NAME, F_("AVERAGEIF:average of the cells in @{actual range} for which the corresponding cells in the range meet the given @{criteria}")},
	{ GNM_FUNC_HELP_ARG, F_("range:cell area")},
	{ GNM_FUNC_HELP_ARG, F_("criteria:condition for a cell to be included")},
	{ GNM_FUNC_HELP_ARG, F_("actual_range:cell area, defaults to @{range}")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "SUMIF,COUNTIF"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_averageif (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmRange rs;
	Sheet *start_sheet, *end_sheet;
	SumIfClosure res;
	GnmValue *problem;
	GODateConventions const *date_conv =
		workbook_date_conv (ei->pos->sheet->workbook);

	/* XL has some limitations on @range that we currently emulate, but do
	 * not need to.
	 * 1) @range must be a range, arrays are not supported
	 * 2) @range can not be 3d */
	if (argv[0]->type != VALUE_CELLRANGE ||
	    (!VALUE_IS_NUMBER (argv[1]) && !VALUE_IS_STRING (argv[1])) ||
	    (argv[2] != NULL && argv[2]->type != VALUE_CELLRANGE))
	        return value_new_error_VALUE (ei->pos);

	gnm_rangeref_normalize (&argv[0]->v_range.cell, ei->pos,
				&start_sheet, &end_sheet,
				&rs);
	if (start_sheet != end_sheet)
		return value_new_error_VALUE (ei->pos);

	if (argv[2]) {
		GnmRange ra;
		/* See 557782.  */
		gnm_rangeref_normalize (&argv[2]->v_range.cell, ei->pos,
					&res.target_sheet, &end_sheet,
					&ra);
		if (res.target_sheet != end_sheet)
			return value_new_error_VALUE (ei->pos);

		res.offset_col = ra.start.col - rs.start.col;
		res.offset_row = ra.start.row - rs.start.row;
	} else
		res.target_sheet = NULL;

	res.sum = 0.;
	res.count = 0;
	res.crit = parse_criteria (argv[1], date_conv);
	problem = sheet_foreach_cell_in_range
		(start_sheet, res.crit->iter_flags,
		 rs.start.col, rs.start.row, rs.end.col, rs.end.row,
		 (CellIterFunc) &cb_sumif, &res);
	free_criteria (res.crit);

	if (NULL != problem)
	        return value_new_error_VALUE (ei->pos);
	if (res.count == 0)
		return value_new_error_DIV0 (ei->pos);
	return value_new_float (res.sum/res.count);
}
/***************************************************************************/

static GnmFuncHelp const help_ceiling[] = {
	{ GNM_FUNC_HELP_NAME, F_("CEILING:nearest multiple of @{significance} whose absolute value is at least ABS(@{x})")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("significance:base multiple (defaults to 1 for @{x} > 0 and -1 for @{x} <0)")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CEILING(@{x},@{significance}) is the nearest multiple of @{significance} whose absolute value is at least ABS(@{x}).")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} or @{significance} is non-numeric, CEILING returns a #VALUE! error.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} and @{significance} have different signs, CEILING returns a #NUM! error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_ODF, F_("CEILING(@{x}) is exported to ODF as CEILING(@{x},SIGN(@{x}),1). CEILING(@{x},@{significance}) is the OpenFormula function CEILING(@{x},@{significance},1).")},
	{ GNM_FUNC_HELP_EXAMPLES, "=CEILING(2.43,1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=CEILING(123.123,3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=CEILING(-2.43,-1)" },
	{ GNM_FUNC_HELP_SEEALSO, "CEIL,FLOOR,ABS,INT,MOD"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ceiling (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float s = argv[1] ? value_get_as_float (argv[1]) : (x > 0 ? 1 : -1);

	if (x == 0 || s == 0)
		return value_new_int (0);

	if (x / s < 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_fake_ceil (x / s) * s);
}

/***************************************************************************/

static GnmFuncHelp const help_cos[] = {
	{ GNM_FUNC_HELP_NAME, F_("COS:the cosine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:angle in radians")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Cosine.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Trigonometric_functions") },
	{ GNM_FUNC_HELP_EXAMPLES, "=COS(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COS(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,TAN,SINH,COSH,TANH,RADIANS,DEGREES" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_cos (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_cospi[] = {
	{ GNM_FUNC_HELP_NAME, F_("COSPI:the cosine of Pi*@{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number of half turns")},
	{ GNM_FUNC_HELP_EXAMPLES, "=COSPI(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COSPI(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "COS" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cospi (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_cospi (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_cosh[] = {
	{ GNM_FUNC_HELP_NAME, F_("COSH:the hyperbolic cosine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=COSH(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COSH(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,TAN,SINH,COSH,TANH" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_cosh (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_cot[] = {
	{ GNM_FUNC_HELP_NAME, F_("COT:the cotangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=COT(0.12)" },
	{ GNM_FUNC_HELP_SEEALSO, "TAN,ACOT" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Cotangent.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Trigonometric_functions") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cot (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_cot (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_coth[] = {
	{ GNM_FUNC_HELP_NAME, F_("COTH:the hyperbolic cotangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=COTH(0.12)" },
	{ GNM_FUNC_HELP_SEEALSO, "TANH,ACOTH" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:HyperbolicCotangent.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Hyperbolic_function") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_coth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_coth (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_degrees[] = {
	{ GNM_FUNC_HELP_NAME, F_("DEGREES:equivalent degrees to @{x} radians")},
	{ GNM_FUNC_HELP_ARG, F_("x:angle in radians")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=DEGREES(2.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "RADIANS,PI"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_degrees (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float ((value_get_as_float (argv[0]) * 180.0) /
				M_PIgnum);
}

/***************************************************************************/

static GnmFuncHelp const help_exp[] = {
	{ GNM_FUNC_HELP_NAME, F_("EXP:e raised to the power of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_NOTE, F_("e is the base of the natural logarithm.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EXP(2)" },
	{ GNM_FUNC_HELP_SEEALSO, "LOG,LOG2,LOG10"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_exp (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_exp (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_expm1[] = {
	{ GNM_FUNC_HELP_NAME, F_("EXPM1:EXP(@{x})-1")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_NOTE, F_("This function has a higher resulting precision than evaluating EXP(@{x})-1.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EXPM1(0.01)" },
	{ GNM_FUNC_HELP_SEEALSO, "EXP,LN1P"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_expm1 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_expm1 (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_fact[] = {
	{ GNM_FUNC_HELP_NAME, F_("FACT:the factorial of @{x}, i.e. @{x}!")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_NOTE, F_("The domain of this function has been extended using the GAMMA function.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FACT(3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=FACT(9)" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_fact (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gboolean x_is_integer = (x == gnm_floor (x));

	if (x < 0 && x_is_integer)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_fact (x));
}

/***************************************************************************/

static GnmFuncHelp const help_gamma[] = {
	{ GNM_FUNC_HELP_NAME, F_("GAMMA:the Gamma function")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=GAMMA(-1.8)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=GAMMA(2.4)" },
	{ GNM_FUNC_HELP_SEEALSO, "GAMMALN"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_gamma (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_gamma (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_gammaln[] = {
	{ GNM_FUNC_HELP_NAME, F_("GAMMALN:natural logarithm of the Gamma function")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GAMMALN(23)" },
	{ GNM_FUNC_HELP_SEEALSO, "GAMMA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_gammaln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gboolean x_is_integer = (x == gnm_floor (x));

	if (x < 0 && (x_is_integer ||
		      gnm_fmod (gnm_floor (-x), 2.0) == 0.0))
		return value_new_error_NUM (ei->pos);
	else
		return value_new_float (gnm_lgamma (x));
}

/***************************************************************************/

static GnmFuncHelp const help_igamma[] = {
	{ GNM_FUNC_HELP_NAME, F_("IGAMMA:the incomplete Gamma function")},
	{ GNM_FUNC_HELP_ARG, F_("a:number")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("lower:if true (the default), the lower incomplete gamma function, otherwise the upper incomplete gamma function")},
	{ GNM_FUNC_HELP_ARG, F_("regularize:if true (the default), the regularized version of the incomplete gamma function")},
	{ GNM_FUNC_HELP_ARG, F_("real:if true (the default), the real part of the result, otherwise the imaginary part")},
	{ GNM_FUNC_HELP_NOTE, F_("The regularized incomplete gamma function is the unregularized incomplete gamma function divided by gamma(@{a})") },
	{ GNM_FUNC_HELP_NOTE, F_("This is a real valued function as long as neither @{a} nor @{z} are negative.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IGAMMA(2.5,-1.8,TRUE,TRUE,TRUE)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=IGAMMA(2.5,-1.8,TRUE,TRUE,FALSE)" },
	{ GNM_FUNC_HELP_SEEALSO, "GAMMA,IMIGAMMA"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_igamma (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float z = value_get_as_float (argv[1]);
	gboolean lower = argv[2] ? value_get_as_checked_bool (argv[2]) : TRUE;
	gboolean reg = argv[3] ? value_get_as_checked_bool (argv[3]) : TRUE;
	gboolean re = argv[4] ? value_get_as_checked_bool (argv[4]) : TRUE;
	complex_t ig, ca, cz;

	complex_init (&ca, a, 0);
	complex_init (&cz, z, 0);
	complex_igamma (&ig, &ca, &cz, lower, reg);

	return value_new_float (re ? ig.re : ig.im);
}

/***************************************************************************/

static GnmFuncHelp const help_beta[] = {
	{ GNM_FUNC_HELP_NAME, F_("BETA:Euler beta function")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("y:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BETA function returns the value of the Euler beta function extended to all real numbers except 0 and negative integers.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x}, @{y}, or (@{x} + @{y}) are non-positive integers, BETA returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BETA(2,3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=BETA(-0.5,0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BETALN,GAMMALN"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Beta_function") },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_beta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

	return value_new_float (gnm_beta (a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_betaln[] = {
	{ GNM_FUNC_HELP_NAME, F_("BETALN:natural logarithm of the absolute value of the Euler beta function")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("y:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("BETALN function returns the natural logarithm of the absolute value of the Euler beta function extended to all real numbers except 0 and negative integers.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x}, @{y}, or (@{x} + @{y}) are non-positive integers, BETALN returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=BETALN(2,3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=BETALN(-0.5,0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "BETA,GAMMALN"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Beta_function") },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_betaln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);
	int sign;

	return value_new_float (gnm_lbeta3 (a, b, &sign));
}

/***************************************************************************/

static GnmFuncHelp const help_combin[] = {
	{ GNM_FUNC_HELP_NAME, F_("COMBIN:binomial coefficient")},
	{ GNM_FUNC_HELP_ARG, F_("n:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("k:non-negative integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COMBIN returns the binomial coefficient \"@{n} choose @{k}\","
					" the number of @{k}-combinations of an @{n}-element set "
					"without repetition.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} is less than @{k} COMBIN returns #NUM!") },
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=COMBIN(8,6)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COMBIN(6,2)" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Binomial_coefficient") },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_combin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n = gnm_floor (value_get_as_float (argv[0]));
	gnm_float k = gnm_floor (value_get_as_float (argv[1]));

	if (k >= 0 && n >= k)
		return value_new_float (combin (n ,k));

	return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_combina[] = {
	{ GNM_FUNC_HELP_NAME, F_("COMBINA:the number of @{k}-combinations of an @{n}-element set "
				 "with repetition")},
	{ GNM_FUNC_HELP_ARG, F_("n:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("k:non-negative integer")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=COMBINA(5,3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COMBINA(6,3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COMBINA(42,3)" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Multiset") },
	{ GNM_FUNC_HELP_SEEALSO, "COMBIN" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_combina (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n = gnm_floor (value_get_as_float (argv[0]));
	gnm_float k = gnm_floor (value_get_as_float (argv[1]));

	if (k >= 0 && n >= 0)
		return value_new_float (combin (n + k - 1, k));

	return value_new_error_NUM (ei->pos);
}
/***************************************************************************/

static GnmFuncHelp const help_floor[] = {
	{ GNM_FUNC_HELP_NAME, F_("FLOOR:nearest multiple of @{significance} whose absolute value is at most ABS(@{x})") },
	{ GNM_FUNC_HELP_ARG, F_("x:number") },
	{ GNM_FUNC_HELP_ARG, F_("significance:base multiple (defaults to 1 for @{x} > 0 and -1 for @{x} <0)") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
			"FLOOR(@{x},@{significance}) is the nearest multiple of @{significance} whose absolute value is at most ABS(@{x})") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_ODF, F_("FLOOR(@{x}) is exported to ODF as FLOOR(@{x},SIGN(@{x}),1). FLOOR(@{x},@{significance}) is the OpenFormula function FLOOR(@{x},@{significance},1).")},
	{ GNM_FUNC_HELP_EXAMPLES, "=FLOOR(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=FLOOR(5,2)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=FLOOR(-5,-2)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=FLOOR(-5,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "CEIL,CEILING,ABS,INT,MOD" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_floor (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float s = argv[1] ? value_get_as_float (argv[1]) : (x > 0 ? 1 : -1);

	if (x == 0)
		return value_new_int (0);

	if (s == 0)
		return value_new_error_DIV0 (ei->pos);

	if (x / s < 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_fake_floor (x / s) * s);
}

/***************************************************************************/

static GnmFuncHelp const help_int[] = {
	{ GNM_FUNC_HELP_NAME, F_("INT:largest integer not larger than @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
 	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=INT(7.2)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=INT(-5.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "CEIL,CEILING,FLOOR,ABS,MOD"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_int (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_fake_floor
				(value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_log[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOG:logarithm of @{x} with base @{base}")},
	{ GNM_FUNC_HELP_ARG, F_("x:positive number")},
	{ GNM_FUNC_HELP_ARG, F_("base:base of the logarithm, defaults to 10")},
	{ GNM_FUNC_HELP_NOTE, F_("@{base} must be positive and not equal to 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} \xe2\x89\xa4 0, LOG returns #NUM! error.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LOG(2)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=LOG(8192,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "LN,LOG2,LOG10"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_log (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);
	gnm_float base = argv[1] ? value_get_as_float (argv[1]) : 10;
	gnm_float res;

	if (base == 1. || base <= 0.)
		return value_new_error_NUM (ei->pos);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	if (base == 2)
		res = gnm_log2 (t);
	else if (base == 0.5)
		res = -gnm_log2 (t);
	else if (base == 10)
		res = gnm_log10 (t);
	else
		res = gnm_log (t) / gnm_log (base);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_ln[] = {
	{ GNM_FUNC_HELP_NAME, F_("LN:the natural logarithm of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:positive number")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} \xe2\x89\xa4 0, LN returns #NUM! error.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LN(7)" },
	{ GNM_FUNC_HELP_SEEALSO, "EXP,LOG2,LOG10"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_ln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log (t));
}

/***************************************************************************/

static GnmFuncHelp const help_ln1p[] = {
	{ GNM_FUNC_HELP_NAME, F_("LN1P:LN(1+@{x})")},
	{ GNM_FUNC_HELP_ARG, F_("x:positive number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("LN1P calculates LN(1+@{x}) but yielding a higher precision than evaluating LN(1+@{x}).")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} \xe2\x89\xa4 -1, LN returns #NUM! error.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LN1P(0.01)" },
	{ GNM_FUNC_HELP_SEEALSO, "EXP,LN,EXPM1"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_ln1p (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t <= -1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log1p (t));
}

/***************************************************************************/

static GnmFuncHelp const help_power[] = {
	{ GNM_FUNC_HELP_NAME, F_("POWER:the value of @{x} raised to the power @{y} raised to the power of 1/@{z}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("y:number")},
	{ GNM_FUNC_HELP_ARG, F_("z:number")},
	{ GNM_FUNC_HELP_NOTE, F_("If both @{x} and @{y} equal 0, POWER returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} = 0 and @{y} < 0, POWER returns #DIV/0!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0 and @{y} is not an integer, POWER returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("@{z} defaults to 1") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a positive integer, POWER returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} < 0, @{y} is odd, and @{z} is even, POWER returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=POWER(2,7)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=POWER(3,3.141)" },
	{ GNM_FUNC_HELP_SEEALSO, "EXP"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_power (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float y = value_get_as_float (argv[1]);
	gnm_float z = argv[2] ? value_get_as_float (argv[2]) : 1;

	if ((x > 0) || (x == 0 && y > 0) || (x < 0 && y == gnm_floor (y))) {
		gnm_float r = gnm_pow (x, y);
		gboolean z_even = gnm_fmod (z, 2.0) == 0;
		if (z <= 0 || z != gnm_floor (z) || (r < 0 && z_even))
			return value_new_error_NUM (ei->pos);
		if (z != 1)
			r = (r < 0 ? -1 : +1) * gnm_pow (gnm_abs (r), 1 / z);
		return value_new_float (r);
	}

	if (x == 0 && y != 0)
		return value_new_error_DIV0 (ei->pos);
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_pochhammer[] = {
	{ GNM_FUNC_HELP_NAME, F_("POCHHAMMER:the value of GAMMA(@{x}+@{n})/GAMMA(@{x})")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("n:number")},
	{ GNM_FUNC_HELP_EXAMPLES, "=POCHHAMMER(1,5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=POCHHAMMER(6,0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "GAMMA"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_pochhammer (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float n = value_get_as_float (argv[1]);

	return value_new_float (pochhammer (x, n));
}

/***************************************************************************/

static GnmFuncHelp const help_log2[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOG2:the base-2 logarithm of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:positive number")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} \xe2\x89\xa4 0, LOG2 returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LOG2(1024)" },
	{ GNM_FUNC_HELP_SEEALSO, "EXP,LOG10,LOG"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_log2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log2 (t));
}

/***************************************************************************/

static GnmFuncHelp const help_log10[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOG10:the base-10 logarithm of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:positive number")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} \xe2\x89\xa4 0, LOG10 returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=LOG10(1024)" },
	{ GNM_FUNC_HELP_SEEALSO, "EXP,LOG2,LOG"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_log10 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv[0]);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log10 (t));
}

/***************************************************************************/

static GnmFuncHelp const help_mod[] = {
	{ GNM_FUNC_HELP_NAME, F_("MOD:the remainder of @{x} under division by @{n}")},
	{ GNM_FUNC_HELP_ARG, F_("x:integer")},
	{ GNM_FUNC_HELP_ARG, F_("n:integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("MOD function returns the remainder when @{x} is divided by @{n}.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} is 0, MOD returns #DIV/0!")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MOD(23,7)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=MOD(23,-7)" },
	{ GNM_FUNC_HELP_SEEALSO, "CEIL,CEILING,FLOOR,ABS,INT,ABS"},
	{ GNM_FUNC_HELP_END}
};

/*
 * MOD(-1,-3) = -1
 * MOD(2,-3) = -2
 * MOD(10.6,2) = 0.6
 * MOD(-10.6,2) = 1.4
 * MOD(10.6,-2) = -0.6
 * MOD(-10.6,-2) = -1.4
 */

static GnmValue *
gnumeric_mod (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);
	gnm_float babs, r;

	if (b == 0)
		return value_new_error_DIV0 (ei->pos);

	babs = gnm_abs (b);
	r = gnm_fmod (gnm_abs (a), babs);
	if (r > 0) {
		if ((a < 0) != (b < 0))
			r = babs - r;
		if (b < 0)
			r = -r;
	}

	return value_new_float (r);
}

/***************************************************************************/

static GnmFuncHelp const help_radians[] = {
	{ GNM_FUNC_HELP_NAME, F_("RADIANS:the number of radians equivalent to @{x} degrees")},
	{ GNM_FUNC_HELP_ARG, F_("x:angle in degrees")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=RADIANS(180)" },
	{ GNM_FUNC_HELP_SEEALSO, "PI,DEGREES"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_radians (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float ((value_get_as_float (argv[0]) * M_PIgnum) /
				180);
}

/***************************************************************************/

static GnmFuncHelp const help_sin[] = {
	{ GNM_FUNC_HELP_NAME, F_("SIN:the sine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:angle in radians")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SIN(0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "COS,TAN,CSC,SEC,SINH,COSH,TANH,RADIANS,DEGREES" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Sine.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Trigonometric_functions") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_sin (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_sinpi[] = {
	{ GNM_FUNC_HELP_NAME, F_("SINPI:the sine of Pi*@{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number of half turns")},
	{ GNM_FUNC_HELP_EXAMPLES, "=SINPI(0.5)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=SINPI(1)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sinpi (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_sinpi (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_csc[] = {
	{ GNM_FUNC_HELP_NAME, F_("CSC:the cosecant of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:angle in radians")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is not Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CSC(0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,COS,TAN,SEC,SINH,COSH,TANH,RADIANS,DEGREES" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Cosecant.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Trigonometric_functions") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_csc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (1./gnm_sin (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_csch[] = {
	{ GNM_FUNC_HELP_NAME, F_("CSCH:the hyperbolic cosecant of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is not Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CSCH(0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,COS,TAN,CSC,SEC,SINH,COSH,TANH" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:HyperbolicCosecant.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Hyperbolic_function") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_csch (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (1./gnm_sinh (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_sec[] = {
	{ GNM_FUNC_HELP_NAME, F_("SEC:Secant")},
	{ GNM_FUNC_HELP_ARG, F_("x:angle in radians")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is not Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("SEC(@{x}) is exported to OpenFormula as 1/COS(@{x}).") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SEC(0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,COS,TAN,CSC,SINH,COSH,TANH,RADIANS,DEGREES" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:Secant.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Trigonometric_functions") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (1./gnm_cos (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_sech[] = {
	{ GNM_FUNC_HELP_NAME, F_("SECH:the hyperbolic secant of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is not Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("SECH(@{x}) is exported to OpenFormula as 1/COSH(@{x}).") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SECH(0.5)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,COS,TAN,CSC,SEC,SINH,COSH,TANH" },
	{ GNM_FUNC_HELP_EXTREF, F_("wolfram:HyperbolicSecant.html") },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Hyperbolic_function") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sech (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (1./gnm_cosh (value_get_as_float (argv[0])));
}
/***************************************************************************/
static GnmFuncHelp const help_sinh[] = {
	{ GNM_FUNC_HELP_NAME, F_("SINH:the hyperbolic sine of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SINH(0.1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=SINH(-0.1)" },
	{ GNM_FUNC_HELP_SEEALSO, "SIN,COSH,ASINH"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_sinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_sinh (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_sqrt[] = {
	{ GNM_FUNC_HELP_NAME, F_("SQRT:square root of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:non-negative number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} is negative, SQRT returns #NUM!")},
	{ GNM_FUNC_HELP_EXAMPLES, "=SQRT(2)"},
	{ GNM_FUNC_HELP_SEEALSO, "POWER"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_sqrt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	if (x < 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_sqrt (x));
}

/***************************************************************************/

static GnmFuncHelp const help_suma[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMA:sum of all values and cells referenced")},
	{ GNM_FUNC_HELP_ARG, F_("area0:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area1:second cell area")},
	{ FUNCTION_A_DESC },
	{ GNM_FUNC_HELP_EXAMPLES, "=SUMA(11,TRUE,FALSE,12)"},
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,SUM,COUNT"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_suma (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_sum,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_sumsq[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMSQ:sum of the squares of all values and cells referenced")},
	{ GNM_FUNC_HELP_ARG, F_("area0:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("area1:second cell area")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SUMSQ(11,TRUE,FALSE,12)"},
	{ GNM_FUNC_HELP_SEEALSO, "SUM,COUNT"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_sumsq (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_sumsq,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_multinomial[] = {
	{ GNM_FUNC_HELP_NAME, F_("MULTINOMIAL:multinomial coefficient (@{x1}+\xe2\x8b\xaf+@{xn}) choose (@{x1},\xe2\x80\xa6,@{xn})")},
	{ GNM_FUNC_HELP_ARG, F_("x1:first number")},
	{ GNM_FUNC_HELP_ARG, F_("x2:second number")},
	{ GNM_FUNC_HELP_ARG, F_("xn:nth number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MULTINOMIAL(2,3,4)"},
	{ GNM_FUNC_HELP_SEEALSO, "COMBIN,SUM"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Multinomial_theorem") },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_multinomial (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_multinomial,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_g_product[] = {
	{ GNM_FUNC_HELP_NAME, F_("G_PRODUCT:product of all the values and cells referenced")},
	{ GNM_FUNC_HELP_ARG, F_("x1:number")},
	{ GNM_FUNC_HELP_ARG, F_("x2:number")},
	{ GNM_FUNC_HELP_NOTE, F_("Empty cells are ignored and the empty product is 1.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=G_PRODUCT(2,5,9)"},
	{ GNM_FUNC_HELP_SEEALSO, "SUM,COUNT"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_g_product (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_product,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_tan[] = {
	{ GNM_FUNC_HELP_NAME, F_("TAN:the tangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:angle in radians")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=TAN(3)"},
	{ GNM_FUNC_HELP_SEEALSO, "TANH,COS,COSH,SIN,SINH,DEGREES,RADIANS"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_tan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_tan (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_tanh[] = {
	{ GNM_FUNC_HELP_NAME, F_("TANH:the hyperbolic tangent of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=TANH(2)"},
	{ GNM_FUNC_HELP_SEEALSO, "TAN,SIN,SINH,COS,COSH"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_tanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_tanh (value_get_as_float (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_pi[] = {
	{ GNM_FUNC_HELP_NAME, F_("PI:the constant " "\360\235\234\213")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible, but it "
				  "returns " "\360\235\234\213" " with a better "
				  "precision.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=PI()" },
	{ GNM_FUNC_HELP_SEEALSO, "SQRTPI"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_pi (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (M_PIgnum);
}

/***************************************************************************/

static GnmFuncHelp const help_trunc[] = {
	{ GNM_FUNC_HELP_NAME, F_("TRUNC:@{x} truncated to @{d} digits")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("d:non-negative integer, defaults to 0")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{d} is omitted or negative then it defaults to zero. If it is not an integer then it is truncated to an integer.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=TRUNC(35.12)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=TRUNC(43.15,1)"},
	{ GNM_FUNC_HELP_SEEALSO, "INT"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_trunc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float number = value_get_as_float (argv[0]);
	gnm_float digits = argv[1] ? value_get_as_float (argv[1]) : 0;

	if (digits >= 0) {
		if (digits <= GNM_MAX_EXP) {
			gnm_float p10 = gnm_pow10 ((int)digits);
			number = gnm_fake_trunc (number * p10) / p10;
		}
	} else {
		if (digits >= GNM_MIN_EXP) {
			/* Keep p10 integer.  */
			gnm_float p10 = gnm_pow10 ((int)-digits);
			number = gnm_fake_trunc (number / p10) * p10;
		} else
			number = 0;
	}

	return value_new_float (number);
}

/***************************************************************************/

static GnmFuncHelp const help_even[] = {
	{ GNM_FUNC_HELP_NAME, F_("EVEN:@{x} rounded away from 0 to the next even integer")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EVEN(5.4)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=EVEN(-5.4)"},
	{ GNM_FUNC_HELP_SEEALSO, "ODD"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_even (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float number, ceiled;
	int     sign = 1;

	number = value_get_as_float (argv[0]);
	if (number < 0) {
	        sign = -1;
		number = -number;
	}
	ceiled = gnm_ceil (number);
	if (gnm_fmod (ceiled, 2) == 0)
	        if (number > ceiled)
		        number = sign * (ceiled + 2);
		else
		        number = sign * ceiled;
	else
	        number = sign * (ceiled + 1);

	return value_new_float (number);
}

/***************************************************************************/

static GnmFuncHelp const help_odd[] = {
	{ GNM_FUNC_HELP_NAME, F_("ODD:@{x} rounded away from 0 to the next odd integer")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ODD(5.4)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ODD(-5.4)"},
	{ GNM_FUNC_HELP_SEEALSO, "EVEN"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_odd (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float number, ceiled;
	int     sign = 1;

	number = value_get_as_float (argv[0]);
	if (number < 0) {
	        sign = -1;
		number = -number;
	}
	ceiled = gnm_ceil (number);
	if (gnm_fmod (ceiled, 2) == 1)
	        if (number > ceiled)
		        number = sign * (ceiled + 2);
		else
		        number = sign * ceiled;
	else
		number = sign * (ceiled + 1);

	return value_new_float (number);
}

/***************************************************************************/

static GnmFuncHelp const help_factdouble[] = {
	{ GNM_FUNC_HELP_NAME, F_("FACTDOUBLE:double factorial")},
	{ GNM_FUNC_HELP_ARG, F_("x:non-negative integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("FACTDOUBLE function returns the double factorial @{x}!!")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} is not an integer, it is truncated. If @{x} is negative, FACTDOUBLE returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FACTDOUBLE(5)"},
	{ GNM_FUNC_HELP_SEEALSO, "FACT"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_factdouble (GnmFuncEvalInfo *ei, GnmValue const * const *argv)

{
	gnm_float number = value_get_as_float (argv[0]);
	int inumber, n;
	gnm_float res;

	if (number < 0)
		return value_new_error_NUM (ei->pos);

	inumber = (int)MIN (number, (gnm_float)INT_MAX);
	n = (inumber + 1) / 2;

	if (inumber & 1) {
		gnm_float lres = gnm_lgamma (n + 0.5) + n * M_LN2gnum;
		/* Round as the result ought to be integer.  */
		res = gnm_floor (0.5 + gnm_exp (lres) / gnm_sqrt (M_PIgnum));
	} else
		res = gnm_fact (n) * gnm_pow2 (n);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_fib[] = {
	{ GNM_FUNC_HELP_NAME, F_("FIB:Fibonacci numbers")},
	{ GNM_FUNC_HELP_ARG, F_("n:positive integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("FIB(@{n}) is the @{n}th Fibonacci number.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{n} is not an integer, it is truncated. If it is negative or zero FIB returns #NUM!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=FIB(23)"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_fib (GnmFuncEvalInfo *ei, GnmValue const * const *argv)

{
	static int fibs[47];
	static int fib_count = G_N_ELEMENTS (fibs);
	static gboolean inited = FALSE;
	gnm_float n = gnm_floor (value_get_as_float (argv[0]));

	if (n <= 0)
		return value_new_error_NUM (ei->pos);

	if (n < fib_count) {
		if (!inited) {
			int i;
			fibs[1] = fibs[2] = 1;
			for (i = 3; i < fib_count; i++)
				fibs[i] = fibs[i - 1] + fibs[i - 2];
			inited = TRUE;
		}
		return value_new_int (fibs[(int)n]);
	} else {
		gnm_float s5 = gnm_sqrt (5.0);
		gnm_float r1 = (1 + s5) / 2;
		gnm_float r2 = (1 - s5) / 2;
		/* Use the Binet form. */
		return value_new_float ((gnm_pow (r1, n) - gnm_pow (r2, n)) / s5);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_quotient[] = {
	{ GNM_FUNC_HELP_NAME, F_("QUOTIENT:integer portion of a division")},
	{ GNM_FUNC_HELP_ARG, F_("numerator:integer")},
	{ GNM_FUNC_HELP_ARG, F_("denominator:non-zero integer")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("QUOTIENT yields the integer portion of the division @{numerator}/@{denominator}.\n"
					"QUOTIENT (@{numerator},@{denominator})\xe2\xa8\x89@{denominator}+MOD(@{numerator},@{denominator})=@{numerator}")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=QUOTIENT(23,5)"},
	{ GNM_FUNC_HELP_SEEALSO, "MOD"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_quotient (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float num = value_get_as_float (argv[0]);
	gnm_float den = value_get_as_float (argv[1]);

	if (den == 0)
	        return value_new_error_DIV0 (ei->pos);
	else
	        return value_new_float (gnm_trunc (num / den));
}

/***************************************************************************/

static GnmFuncHelp const help_sign[] = {
	{ GNM_FUNC_HELP_NAME, F_("SIGN:sign of @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("SIGN returns 1 if the @{x} is positive and it returns -1 if @{x} is negative.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SIGN(3)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=SIGN(-3)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=SIGN(0)"},
	{ GNM_FUNC_HELP_SEEALSO, "ABS"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_sign (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n = value_get_as_float (argv[0]);

	if (n > 0)
		return value_new_int (1);
	else if (n == 0)
		return value_new_int (0);
	else
		return value_new_int (-1);
}

/***************************************************************************/

static GnmFuncHelp const help_sqrtpi[] = {
	{ GNM_FUNC_HELP_NAME, F_("SQRTPI:the square root of @{x} times "
				 "\360\235\234\213")},
	{ GNM_FUNC_HELP_ARG, F_("x:non-negative number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SQRTPI(2)"},
	{ GNM_FUNC_HELP_SEEALSO, "PI"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_sqrtpi (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n = value_get_as_float (argv[0]);

	if (n < 0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_sqrt (M_PIgnum * n));
}

/***************************************************************************/

static GnmFuncHelp const help_rounddown[] = {
	{ GNM_FUNC_HELP_NAME, F_("ROUNDDOWN:@{x} rounded towards 0")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("d:integer, defaults to 0")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{d} is greater than zero, @{x} is rounded toward 0 to the given number of digits.\n"
					"If @{d} is zero, @{x} is rounded toward 0 to the next integer.\n"
					"If @{d} is less than zero, @{x} is rounded toward 0 to the left of the decimal point")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDDOWN(5.5)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDDOWN(-3.3)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDDOWN(1501.15,1)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDDOWN(1501.15,-2)"},
	{ GNM_FUNC_HELP_SEEALSO, "ROUND,ROUNDUP"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_rounddown (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return gnumeric_trunc (ei, argv);
}

/***************************************************************************/

static GnmFuncHelp const help_round[] = {
	{ GNM_FUNC_HELP_NAME, F_("ROUND:rounded @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("d:integer, defaults to 0")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{d} is greater than zero, @{x} is rounded to the given number of digits.\n"
					"If @{d} is zero, @{x} is rounded to the next integer.\n"
					"If @{d} is less than zero, @{x} is rounded to the left of the decimal point")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUND(5.5)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUND(-3.3)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUND(1501.15,1)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUND(1501.15,-2)"},
	{ GNM_FUNC_HELP_SEEALSO, "ROUNDDOWN,ROUNDUP"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_round (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float number = value_get_as_float (argv[0]);
	gnm_float digits = argv[1] ? value_get_as_float (argv[1]) : 0;

	if (digits >= 0) {
		if (digits <= GNM_MAX_EXP) {
			gnm_float p10 = gnm_pow10 ((int)digits);
			number = gnm_fake_round (number * p10) / p10;
		}
	} else {
		if (digits >= GNM_MIN_EXP) {
			/* Keep p10 integer.  */
			gnm_float p10 = gnm_pow10 ((int)-digits);
			number = gnm_fake_round (number / p10) * p10;
		} else
			number = 0;
	}

	return value_new_float (number);
}

/***************************************************************************/

static GnmFuncHelp const help_roundup[] = {
	{ GNM_FUNC_HELP_NAME, F_("ROUNDUP:@{x} rounded away from 0")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("d:integer, defaults to 0")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If @{d} is greater than zero, @{x} is rounded away from 0 to the given number of digits.\n"
					"If @{d} is zero, @{x} is rounded away from 0 to the next integer.\n"
					"If @{d} is less than zero, @{x} is rounded away from 0 to the left of the decimal point")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDUP(5.5)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDUP(-3.3)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDUP(1501.15,1)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROUNDUP(1501.15,-2)"},
	{ GNM_FUNC_HELP_SEEALSO, "ROUND,ROUNDDOWN,INT"},
	{ GNM_FUNC_HELP_END}
};

static gnm_float
gnm_fake_roundup (gnm_float x)
{
	return (x < 0) ? gnm_fake_floor (x) : gnm_fake_ceil (x);
}

static GnmValue *
gnumeric_roundup (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float number = value_get_as_float (argv[0]);
	gnm_float digits = argv[1] ? value_get_as_float (argv[1]) : 0;

	if (digits >= 0) {
		if (digits <= GNM_MAX_EXP) {
			gnm_float p10 = gnm_pow10 ((int)digits);
			number = gnm_fake_roundup (number * p10) / p10;
		}
	} else {
		if (digits >= GNM_MIN_EXP) {
			/* Keep p10 integer.  */
			gnm_float p10 = gnm_pow10 ((int)-digits);
			number = gnm_fake_roundup (number / p10) * p10;
		} else
			number = 0;
	}

	return value_new_float (number);
}

/***************************************************************************/

static GnmFuncHelp const help_mround[] = {
	{ GNM_FUNC_HELP_NAME, F_("MROUND:@{x} rounded to a multiple of @{m}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number")},
	{ GNM_FUNC_HELP_ARG, F_("m:number")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} and @{m} have different sign, MROUND returns #NUM!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=MROUND(1.7,0.2)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=MROUND(321.123,0.12)"},
	{ GNM_FUNC_HELP_SEEALSO, "ROUNDDOWN,ROUND,ROUNDUP"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_mround (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float const accuracy_limit = 0.0000003;
	gnm_float number, multiple;
	gnm_float div, mod;
	int     sign = 1;

	number = value_get_as_float (argv[0]);
	multiple = value_get_as_float (argv[1]);

	/* Weird, but XL compatible.  */
	if (multiple == 0)
		return value_new_int (0);

	if ((number > 0 && multiple < 0)
	    || (number < 0 && multiple > 0))
		return value_new_error_NUM (ei->pos);

	if (number < 0) {
	        sign = -1;
		number = -number;
		multiple = -multiple;
	}

	mod = gnm_fmod (number, multiple);
	div = number - mod;

	return value_new_float (sign * (
					div + ((mod + accuracy_limit >= multiple / 2) ? multiple : 0)));
}

/***************************************************************************/

static GnmFuncHelp const help_arabic[] = {
	{ GNM_FUNC_HELP_NAME, F_("ARABIC:the Roman numeral @{roman} as number")},
	{ GNM_FUNC_HELP_ARG, F_("roman:Roman numeral")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("Any Roman symbol to the left of a larger symbol "
					"(directly or indirectly) reduces the final value "
					"by the symbol amount, otherwise, it increases the "
					"final amount by the symbol's amount.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"I\")"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"CDLII\")"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"MCDXC\")"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"MDCCCXCIX\")"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"MCMXCIX\")"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"mmmcmxcix\")"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"MIM\")"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ARABIC(\"IVM\")"},
	{ GNM_FUNC_HELP_SEEALSO, "ROMAN"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_arabic (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	const gchar *roman = (const gchar *)value_peek_string (argv[0]);
	int slen = strlen (roman);
	int last = 0;
	int result = 0;
	gchar *this = (gchar *)(roman + slen);

	while (this > roman) {
		int this_val = 0;
		this = g_utf8_prev_char (this);
		switch (*this) {
		case 'i':
		case 'I':
			this_val = 1;
			break;
		case 'v':
		case 'V':
			this_val = 5;
			break;
		case 'x':
		case 'X':
			this_val = 10;
			break;
		case 'l':
		case 'L':
			this_val = 50;
			break;
		case 'c':
		case 'C':
			this_val = 100;
			break;
		case 'd':
		case 'D':
			this_val = 500;
			break;
		case 'm':
		case 'M':
			this_val = 1000;
			break;
		default:
			break;
		}
		if (this_val > 0) {
			if (this_val < last)
				result -= this_val;
			else {
				result += this_val;
				last = this_val;
			}
		}
	}
	return value_new_int (result);
}

/***************************************************************************/

static GnmFuncHelp const help_roman[] = {
	{ GNM_FUNC_HELP_NAME, F_("ROMAN:@{n} as a roman numeral text")},
	{ GNM_FUNC_HELP_ARG, F_("n:non-negative integer")},
	{ GNM_FUNC_HELP_ARG, F_("type:0,1,2,3,or 4, defaults to 0")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ROMAN returns the arabic number @{n} as a roman numeral text.\n"
					"If @{type} is 0 or it is omitted, ROMAN returns classic roman numbers.\n"
					"Type 1 is more concise than classic type, type 2 is more concise than "
					"type 1, and type 3 is more concise than type 2. Type 4 is a simplified type.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ROMAN(999)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROMAN(999,1)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROMAN(999,2)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROMAN(999,3)"},
	{ GNM_FUNC_HELP_EXAMPLES, "=ROMAN(999,4)"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_roman (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	static char const letter[] = { 'M', 'D', 'C', 'L', 'X', 'V', 'I' };
	int const largest = 1000;
	char buf[256];
	char *p;
	gnm_float n = gnm_floor (value_get_as_float (argv[0]));
	gnm_float form = argv[1] ? gnm_floor (value_get_as_float (argv[1])) : 0;
	int i, j, dec;

	dec = largest;

	if (n < 0 || n > 3999)
		return value_new_error_VALUE (ei->pos);
	if (form < 0 || form > 4)
		return value_new_error_VALUE (ei->pos);

	if (n == 0)
		return value_new_string ("");

	for (i = j = 0; dec > 1; dec /= 10, j += 2) {
	        for (; n > 0; i++) {
		        if (n >= dec) {
			        buf[i] = letter[j];
				n -= dec;
			} else if (n >= dec - dec / 10) {
			        buf[i++] = letter[j + 2];
				buf[i] = letter[j];
				n -= dec - dec / 10;
			} else if (n >= dec / 2) {
			        buf[i] = letter[j + 1];
				n -= dec / 2;
			} else if (n >= dec / 2 - dec / 10) {
			        buf[i++] = letter[j + 2];
				buf[i] = letter[j + 1];
				n -= dec / 2 - dec / 10;
			} else if (dec == 10) {
			        buf[i] = letter[j + 2];
				n--;
			} else
			        break;
		}
	}
	buf[i] = '\0';


	if (form > 0) {
	        /* Replace ``XLV'' with ``VL'' */
	        if ((p = strstr (buf, "XLV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'L';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``XCV'' with ``VC'' */
	        if ((p = strstr (buf, "XCV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'C';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``CDL'' with ``LD'' */
	        if ((p = strstr (buf, "CDL")) != NULL) {
		        *p++ = 'L';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``CML'' with ``LM'' */
	        if ((p = strstr (buf, "CML")) != NULL) {
		        *p++ = 'L';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``CMVC'' with ``LMVL'' */
	        if ((p = strstr (buf, "CMVC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'M';
			*p++ = 'V';
			*p++ = 'L';
		}
	}
	if (form == 1) {
	        /* Replace ``CDXC'' with ``LDXL'' */
	        if ((p = strstr (buf, "CDXC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'D';
			*p++ = 'X';
			*p++ = 'L';
		}
	        /* Replace ``CDVC'' with ``LDVL'' */
	        if ((p = strstr (buf, "CDVC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'D';
			*p++ = 'V';
			*p++ = 'L';
		}
	        /* Replace ``CMXC'' with ``LMXL'' */
	        if ((p = strstr (buf, "CMXC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'M';
			*p++ = 'X';
			*p++ = 'L';
		}
	        /* Replace ``XCIX'' with ``VCIV'' */
	        if ((p = strstr (buf, "XCIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'C';
			*p++ = 'I';
			*p++ = 'V';
		}
	        /* Replace ``XLIX'' with ``VLIV'' */
	        if ((p = strstr (buf, "XLIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'L';
			*p++ = 'I';
			*p++ = 'V';
		}
	}
	if (form > 1) {
	        /* Replace ``XLIX'' with ``IL'' */
	        if ((p = strstr (buf, "XLIX")) != NULL) {
		        *p++ = 'I';
			*p++ = 'L';
			for ( ; *p; p++)
			        *p = *(p+2);
		}
	        /* Replace ``XCIX'' with ``IC'' */
	        if ((p = strstr (buf, "XCIX")) != NULL) {
		        *p++ = 'I';
			*p++ = 'C';
			for ( ; *p; p++)
			        *p = *(p+2);
		}
	        /* Replace ``CDXC'' with ``XD'' */
	        if ((p = strstr (buf, "CDXC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+2);
		}
	        /* Replace ``CDVC'' with ``XDV'' */
	        if ((p = strstr (buf, "CDVC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'D';
			*p++ = 'V';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``CDIC'' with ``XDIX'' */
	        if ((p = strstr (buf, "CDIC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'D';
			*p++ = 'I';
			*p++ = 'X';
		}
	        /* Replace ``LMVL'' with ``XMV'' */
	        if ((p = strstr (buf, "LMVL")) != NULL) {
		        *p++ = 'X';
			*p++ = 'M';
			*p++ = 'V';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``CMIC'' with ``XMIX'' */
	        if ((p = strstr (buf, "CMIC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'M';
			*p++ = 'I';
			*p++ = 'X';
		}
	        /* Replace ``CMXC'' with ``XM'' */
	        if ((p = strstr (buf, "CMXC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+2);
		}
	}
	if (form > 2) {
	        /* Replace ``XDV'' with ``VD'' */
	        if ((p = strstr (buf, "XDV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``XDIX'' with ``VDIV'' */
	        if ((p = strstr (buf, "XDIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'D';
			*p++ = 'I';
			*p++ = 'V';
		}
	        /* Replace ``XMV'' with ``VM'' */
	        if ((p = strstr (buf, "XMV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+1);
		}
	        /* Replace ``XMIX'' with ``VMIV'' */
	        if ((p = strstr (buf, "XMIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'M';
			*p++ = 'I';
			*p++ = 'V';
		}
	}
	if (form == 4) {
	        /* Replace ``VDIV'' with ``ID'' */
	        if ((p = strstr (buf, "VDIV")) != NULL) {
		        *p++ = 'I';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+2);
		}
	        /* Replace ``VMIV'' with ``IM'' */
	        if ((p = strstr (buf, "VMIV")) != NULL) {
		        *p++ = 'I';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+2);
		}
	}

	return value_new_string (buf);
}

/***************************************************************************/

static GnmFuncHelp const help_sumx2my2[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMX2MY2:sum of the difference of squares")},
	{ GNM_FUNC_HELP_ARG, F_("array0:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("array1:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("SUMX2MY2 function returns the sum of the difference of squares of "
					"corresponding values in two arrays. The equation of SUMX2MY2 is SUM(x^2-y^2).")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold numbers 13, 22, 31, 33, and 39.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then SUMX2MY2(A1:A5,B1:B5) yields -1299.")},
	{ GNM_FUNC_HELP_SEEALSO, "SUMSQ,SUMX2PY2"},
	{ GNM_FUNC_HELP_END}
};

static int
gnm_range_sumx2my2 (gnm_float const *xs, const gnm_float *ys,
		    int n, gnm_float *res)
{
	gnm_float s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += xs[i] * xs[i] - ys[i] * ys[i];

	*res = s;
	return 0;
}


static GnmValue *
gnumeric_sumx2my2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      gnm_range_sumx2my2,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_sumx2py2[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMX2PY2:sum of the sum of squares")},
	{ GNM_FUNC_HELP_ARG, F_("array0:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("array1:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("SUMX2PY2 function returns the sum of the sum of squares of "
					"corresponding values in two arrays. The equation of SUMX2PY2 is SUM(x^2+y^2).")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array0} and @{array1} have different number of data points, SUMX2PY2 returns #N/A.\n"
				 "Strings and empty cells are simply ignored.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold numbers 13, 22, 31, 33, and 39.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then SUMX2PY2(A1:A5,B1:B5) yields 7149.") },
	{ GNM_FUNC_HELP_SEEALSO, "SUMSQ,SUMX2MY2"},
	{ GNM_FUNC_HELP_END}
};

static int
gnm_range_sumx2py2 (gnm_float const *xs, const gnm_float *ys,
		    int n, gnm_float *res)
{
	gnm_float s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += xs[i] * xs[i] + ys[i] * ys[i];

	*res = s;
	return 0;
}

static GnmValue *
gnumeric_sumx2py2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      gnm_range_sumx2py2,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_sumxmy2[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMXMY2:sum of the squares of differences")},
	{ GNM_FUNC_HELP_ARG, F_("array0:first cell area")},
	{ GNM_FUNC_HELP_ARG, F_("array1:second cell area")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("SUMXMY2 function returns the sum of the squares of the differences of "
					"corresponding values in two arrays. The equation of SUMXMY2 is SUM((x-y)^2).")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{array0} and @{array1} have different number of data points, SUMXMY2 returns #N/A.\n"
				 "Strings and empty cells are simply ignored.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold numbers 13, 22, 31, 33, and 39.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then SUMXMY2(A1:A5,B1:B5) yields 409.") },
	{ GNM_FUNC_HELP_SEEALSO, "SUMSQ,SUMX2MY2,SUMX2PY2"},
	{ GNM_FUNC_HELP_END}
};

static int
gnm_range_sumxmy2 (gnm_float const *xs, const gnm_float *ys,
		   int n, gnm_float *res)
{
	gnm_float s = 0;
	int i;

	for (i = 0; i < n; i++) {
		gnm_float d = (xs[i] - ys[i]);
		s += d * d;
	}

	*res = s;
	return 0;
}

static GnmValue *
gnumeric_sumxmy2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return float_range_function2 (argv[0], argv[1],
				      ei,
				      gnm_range_sumxmy2,
				      COLLECT_IGNORE_BLANKS |
				      COLLECT_IGNORE_STRINGS |
				      COLLECT_IGNORE_BOOLS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_seriessum[] = {
	{ GNM_FUNC_HELP_NAME, F_("SERIESSUM:sum of a power series at @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:number where to evaluate the power series")},
	{ GNM_FUNC_HELP_ARG, F_("n:non-negative integer, exponent of the lowest term of the series")},
	{ GNM_FUNC_HELP_ARG, F_("m:increment to each exponent")},
	{ GNM_FUNC_HELP_ARG, F_("coeff:coefficients of the power series")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5 contain numbers 1.23, 2.32, 2.98, 3.42, and 4.33.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("Then SERIESSUM(2,1,2.23,A1:A5) evaluates as 5056.37439843926") },
	{ GNM_FUNC_HELP_SEEALSO, "COUNT,SUM"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_seriessum (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float n = value_get_as_float (argv[1]);
	gnm_float m = value_get_as_float (argv[2]);
	GnmValue *result = NULL;
	int N;
	/* Ignore blanks; err on bools or strings.  */
	gnm_float *data =
		collect_floats_value (argv[3], ei->pos,
				      COLLECT_IGNORE_BLANKS, &N, &result);

	if (result)
		goto done;

	if (x == 0) {
		if (n <= 0 || n + (N - 1) * m <= 0)
			result = value_new_error_NUM (ei->pos);
		else
			result = value_new_float (0);
	} else {
		gnm_float x_m = gnm_pow (x, m);
		gnm_float sum = 0;
		int i;
		x = gnm_pow (x, n);

		for (i = 0; i < N; i++) {
			sum += data[i] * x;
			x *= x_m;
		}

		if (gnm_finite (sum))
			result = value_new_float (sum);
		else
			result = value_new_error_NUM (ei->pos);
	}

done:
	g_free (data);
	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_minverse[] = {
	{ GNM_FUNC_HELP_NAME, F_("MINVERSE:the inverse matrix of @{matrix}")},
	{ GNM_FUNC_HELP_ARG, F_("matrix:a square matrix")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{matrix} is not invertible, MINVERSE returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{matrix} does not contain an equal number of columns and rows, MINVERSE returns #VALUE!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "MMULT,MDETERM,LINSOLVE"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_minverse (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmValue *res = NULL;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	if (A->cols != A->rows || gnm_matrix_is_empty (A)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	if (gnm_matrix_invert (A->data, A->rows))
		res = gnm_matrix_to_value (A);
	else
		res = value_new_error_NUM (ei->pos);

out:
	if (A) gnm_matrix_free (A);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_mpseudoinverse[] = {
	{ GNM_FUNC_HELP_NAME, F_("MPSEUDOINVERSE:the pseudo-inverse matrix of @{matrix}")},
	{ GNM_FUNC_HELP_ARG, F_("matrix:a matrix")},
	{ GNM_FUNC_HELP_ARG, F_("threshold:a relative size threshold for discarding eigenvalues")},
	{ GNM_FUNC_HELP_SEEALSO, "MINVERSE"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_mpseudominverse (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmMatrix *B = NULL;
	GnmValue *res = NULL;
	gnm_float threshold = argv[1] ? value_get_as_float (argv[1]) : 256 * GNM_EPSILON;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	if (gnm_matrix_is_empty (A)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	B = gnm_matrix_new (A->cols, A->rows);  /* Shape of A^t */
	gnm_matrix_pseudo_inverse (A->data, A->rows, A->cols, threshold, B->data);
	res = gnm_matrix_to_value (B);

out:
	if (A) gnm_matrix_free (A);
	if (B) gnm_matrix_free (B);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_cholesky[] = {
	{ GNM_FUNC_HELP_NAME, F_("CHOLESKY:the Cholesky decomposition of the symmetric positive-definite @{matrix}")},
	{ GNM_FUNC_HELP_ARG, F_("matrix:a symmetric positive definite matrix")},
	{ GNM_FUNC_HELP_NOTE, F_("If the Cholesky-Banachiewicz algorithm applied to @{matrix} fails, Cholesky returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{matrix} does not contain an equal number of columns and rows, CHOLESKY returns #VALUE!") },
	{ GNM_FUNC_HELP_SEEALSO, "MINVERSE,MMULT,MDETERM"},
	{ GNM_FUNC_HELP_END}
};

static gboolean
gnm_matrix_cholesky  (GnmMatrix const *A, GnmMatrix *B)
{
	int r, c, k;
	gnm_float sum;
	int n = A->cols;

	for (r = 0; r < n; r++) {
		for (c = 0; c < r; c++) {
			sum = 0.;
			for (k = 0; k < c; k++)
				sum += B->data[r][k] * B->data[c][k];
			B->data[c][r] = 0;
			B->data[r][c] = (A->data[r][c] - sum) / B->data[c][c];
		}
		sum = 0;
		for (k = 0; k < r; k++)
			sum += B->data[r][k] * B->data[r][k];
		B->data[r][r] = gnm_sqrt (A->data[r][r] - sum);
	}
	return TRUE;
}

static gboolean
symmetric (GnmMatrix const *m)
{
	int c, r;

	if (m->cols != m->rows)
		return FALSE;

	for (c = 0; c < m->cols; ++c)
		for (r = c + 1; r < m->rows; ++r)
			if (m->data[r][c] != m->data[c][r])
				return FALSE;

	return TRUE;
}

static GnmValue *
gnumeric_cholesky (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmMatrix *B = NULL;
	GnmValue *res = NULL;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	if (A->cols != A->rows || gnm_matrix_is_empty (A) || !symmetric (A)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	B = gnm_matrix_new (A->rows, A->cols);

	if (gnm_matrix_cholesky (A, B))
		res = gnm_matrix_to_value (B);
	else
		res = value_new_error_NUM (ei->pos);

out:
	if (A) gnm_matrix_free (A);
	if (B) gnm_matrix_free (B);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_munit[] = {
	{ GNM_FUNC_HELP_NAME, F_("MUNIT:the @{n} by @{n} identity matrix")},
	{ GNM_FUNC_HELP_ARG, F_("n:size of the matrix")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.")},
	{ GNM_FUNC_HELP_SEEALSO, "MMULT,MDETERM,MINVERSE"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_munit (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float n = value_get_as_float (argv[0]);
	gint c, ni;
	GnmValue *res;

	if (n <= 0)
		return value_new_error_NUM (ei->pos);

	/*
	 * This provides some protection against bogus sizes and
	 * running out of memory.
	 */
	if (n * n >= G_MAXINT ||
	    n > 5000) /* Arbitrary */
		return value_new_error_NUM (ei->pos);

	ni = (int)n;
	res = value_new_array (ni, ni);
	for (c = 0; c < ni; ++c) {
		value_release (res->v_array.vals[c][c]);
		res->v_array.vals[c][c] =  value_new_int (1);
	}

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_mmult[] = {
	{ GNM_FUNC_HELP_NAME, F_("MMULT:the matrix product of @{mat1} and @{mat2}")},
	{ GNM_FUNC_HELP_ARG, F_("mat1:a matrix")},
	{ GNM_FUNC_HELP_ARG, F_("mat2:a matrix")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "TRANSPOSE,MINVERSE"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_mmult (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmMatrix *B = NULL;
	GnmMatrix *C = NULL;
	GnmValue *res = NULL;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	B = gnm_matrix_from_value (argv[1], &res, ei->pos);
	if (!B) goto out;

	if (A->cols != B->rows || gnm_matrix_is_empty (A) || gnm_matrix_is_empty (B)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	C = gnm_matrix_new (A->rows, B->cols);
	gnm_matrix_multiply (C, A, B);
	res = gnm_matrix_to_value (C);

out:
	if (A) gnm_matrix_free (A);
	if (B) gnm_matrix_free (B);
	if (C) gnm_matrix_free (C);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_linsolve[] = {
	{ GNM_FUNC_HELP_NAME, F_("LINSOLVE:solve linear equation")},
	{ GNM_FUNC_HELP_ARG, F_("A:a matrix")},
	{ GNM_FUNC_HELP_ARG, F_("B:a matrix")},
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("Solves the equation @{A}*X=@{B} and returns X.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the matrix @{A} is singular, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_SEEALSO, "MINVERSE"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_linsolve (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmMatrix *B = NULL;
	GnmValue *res = NULL;
	GORegressionResult regres;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	B = gnm_matrix_from_value (argv[1], &res, ei->pos);
	if (!B) goto out;

	if (A->cols != A->rows || gnm_matrix_is_empty (A) ||
	    B->rows != A->rows || gnm_matrix_is_empty (B)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	regres = gnm_linear_solve_multiple (A->data, B->data, A->rows, B->cols);

	if (regres != GO_REG_ok && regres != GO_REG_near_singular_good) {
		res = value_new_error_NUM (ei->pos);
	} else {
		int c, r;

		res = value_new_array_non_init (B->cols, B->rows);
		for (c = 0; c < B->cols; c++) {
			res->v_array.vals[c] = g_new (GnmValue *, B->rows);
			for (r = 0; r < B->rows; r++)
				res->v_array.vals[c][r] =
					value_new_float (B->data[r][c]);
		}
	}

out:
	if (A) gnm_matrix_free (A);
	if (B) gnm_matrix_free (B);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_mdeterm[] = {
	{ GNM_FUNC_HELP_NAME, F_("MDETERM:the determinant of the matrix @{matrix}")},
	{ GNM_FUNC_HELP_ARG, F_("matrix:a square matrix")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that A1,...,A4 contain numbers 2, 3, 7, and 3; B1,..., B4 4, 2, 4, and 1; C1,...,C4 9, 4, 3; and 2; and D1,...,D4 7, 3, 6, and 5. Then MDETERM(A1:D4) yields 148.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "MMULT,MINVERSE"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_mdeterm (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmValue *res = NULL;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	if (A->cols != A->rows || gnm_matrix_is_empty (A)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	res = value_new_float (gnm_matrix_determinant (A->data, A->rows));

out:
	if (A) gnm_matrix_free (A);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_sumproduct[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMPRODUCT:multiplies components and adds the results") },
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("Multiplies corresponding data entries in the "
	     "given arrays or ranges, and then returns the sum of those "
	     "products.") },
	{ GNM_FUNC_HELP_NOTE, F_("If an entry is not numeric, the value zero is used instead.") },
	{ GNM_FUNC_HELP_NOTE, F_("If arrays or range arguments do not have the same dimensions, "
				 "return #VALUE! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("This function ignores logicals, so using SUMPRODUCT(A1:A5>0) will not work.  Instead use SUMPRODUCT(--(A1:A5>0))") },
#if 0
	"@EXAMPLES=\n"
	"Let us assume that the cells A1, A2, ..., A5 contain numbers "
	"11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	"numbers 13, 22, 31, 33, and 39.  Then\n"
	"SUMPRODUCT(A1:A5,B1:B5) equals 3370.\n"
#endif
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is not OpenFormula compatible. Use ODF.SUMPRODUCT instead.") },
	{ GNM_FUNC_HELP_SEEALSO, "SUM,PRODUCT,G_PRODUCT,ODF.SUMPRODUCT" },
	{ GNM_FUNC_HELP_END }
};

static GnmFuncHelp const help_odf_sumproduct[] = {
	{ GNM_FUNC_HELP_NAME, F_("ODF.SUMPRODUCT:multiplies components and adds the results") },
	{ GNM_FUNC_HELP_DESCRIPTION,
	  F_("Multiplies corresponding data entries in the "
	     "given arrays or ranges, and then returns the sum of those "
	     "products.") },
	{ GNM_FUNC_HELP_NOTE, F_("If an entry is not numeric or logical, the value zero is used instead.") },
	{ GNM_FUNC_HELP_NOTE, F_("If arrays or range arguments do not have the same dimensions, "
				 "return #VALUE! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("This function differs from SUMPRODUCT by considering booleans.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is not Excel compatible. Use SUMPRODUCT instead.") },
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "SUMPRODUCT,SUM,PRODUCT,G_PRODUCT" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sumproduct_common (gboolean ignore_bools, GnmFuncEvalInfo *ei,
			    int argc, GnmExprConstPtr const *argv)
{
	gnm_float **data;
	GnmValue *result;
	int i;
	gboolean size_error = FALSE;
	int sizex = -1, sizey = -1;

	if (argc == 0)
		return value_new_error_VALUE (ei->pos);

	data = g_new0 (gnm_float *, argc);

	for (i = 0; i < argc; i++) {
		int thissizex, thissizey, x, y;
		GnmExpr const *expr = argv[i];
		GnmValue *val = gnm_expr_eval
			(expr, ei->pos,
			 GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
			 GNM_EXPR_EVAL_PERMIT_EMPTY);

		if (!val) {
			size_error = TRUE;
			break;
		}

		thissizex = value_area_get_width (val, ei->pos);
		thissizey = value_area_get_height (val, ei->pos);

		if (i == 0) {
			sizex = thissizex;
			sizey = thissizey;
		} else if (sizex != thissizex || sizey != thissizey)
			size_error = TRUE;

		data[i] = g_new (gnm_float, thissizex * thissizey);
		for (y = 0; y < thissizey; y++) {
			for (x = 0; x < thissizex; x++) {
				/* FIXME: efficiency worries?  */
				GnmValue const *v = value_area_fetch_x_y (val, x, y, ei->pos);
				switch (v->type) {
				case VALUE_ERROR:
					/*
					 * We carefully tranverse the argument
					 * list and then the arrays in such an
					 * order that the first error we see is
					 * the final result.
					 *
					 * args: left-to-right.
					 * arrays: horizontal before vertical.
					 *
					 * Oh, size_error has the lowest
					 * significance -- it will be checked
					 * outside the arg loop.
					 */
					result = value_dup (v);
					value_release (val);
					goto done;
				case VALUE_FLOAT:
					data[i][y * thissizex + x] = value_get_as_float (v);
					break;
				case VALUE_BOOLEAN:
					data[i][y * thissizex + x] =
						ignore_bools
						? 0.0
						: value_get_as_float (v);
					break;
				default :
					/* Ignore strings to be consistent with XL */
					data[i][y * thissizex + x] = 0.;
				}
			}
		}
		value_release (val);
	}

	if (size_error) {
		/*
		 * If we found no errors in the data set and also the sizes
		 * do not match, we will get here.
		 */
		result = value_new_error_VALUE (ei->pos);
	} else {

		void *state = gnm_accumulator_start ();
		GnmAccumulator *acc = gnm_accumulator_new ();
		int j;

		for (j = 0; j < sizex * sizey; j++) {
			int i;
			GnmQuad product;
			gnm_quad_init (&product, data[0][j]);
			for (i = 1; i < argc; i++) {
				GnmQuad q;
				gnm_quad_init (&q, data[i][j]);
				gnm_quad_mul (&product, &product, &q);
			}
			gnm_accumulator_add_quad (acc, &product);
		}

		result = value_new_float (gnm_accumulator_value (acc));
		gnm_accumulator_free (acc);
		gnm_accumulator_end (state);
	}

done:
	for (i = 0; i < argc; i++)
		g_free (data[i]);
	g_free (data);
	return result;
}

static GnmValue *
gnumeric_sumproduct (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return gnumeric_sumproduct_common (TRUE, ei, argc, argv);
}

static GnmValue *
gnumeric_odf_sumproduct (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return gnumeric_sumproduct_common (FALSE, ei, argc, argv);
}

/***************************************************************************/

static GnmFuncHelp const help_eigen[] = {
	{ GNM_FUNC_HELP_NAME, F_("EIGEN:eigenvalues and eigenvectors of the symmetric @{matrix}")},
	{ GNM_FUNC_HELP_ARG, F_("matrix:a symmetric matrix")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{matrix} is not symmetric, EIGEN returns #NUM!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{matrix} does not contain an equal number of columns and rows, EIGEN returns #VALUE!") },
	{ GNM_FUNC_HELP_END}
};


typedef struct {
	gnm_float val;
	int index;
} gnumeric_eigen_ev_t;

static int
compare_gnumeric_eigen_ev (const void *a, const void *b)
{
	const gnumeric_eigen_ev_t *da = a;
	const gnumeric_eigen_ev_t *db = b;
	gnm_float ea = da->val;
	gnm_float eb = db->val;

	/* Compare first by magnitude (descending).  */
	if (gnm_abs (ea) > gnm_abs (eb))
		return -1;
	else if (gnm_abs (ea) < gnm_abs (eb))
		return +1;

	/* Then by value (still descending.  */
	if (ea > eb)
		return -1;
	else if (ea < eb)
		return +1;
	else
		return 0;
}

static GnmValue *
gnumeric_eigen (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmMatrix *A = NULL;
	GnmMatrix *EIG = NULL;
	gnm_float *eigenvalues = NULL;
	GnmValue *res = NULL;
	int i, c, r;
	gnumeric_eigen_ev_t *ev_sort;

	A = gnm_matrix_from_value (argv[0], &res, ei->pos);
	if (!A) goto out;

	if (A->cols != A->rows || gnm_matrix_is_empty (A) || !symmetric (A)) {
		res = value_new_error_VALUE (ei->pos);
		goto out;
	}

	EIG = gnm_matrix_new (A->rows, A->cols);
	eigenvalues = g_new0 (gnm_float, A->cols);

	if (!gnm_matrix_eigen (A, EIG, eigenvalues)) {
		res = value_new_error_NUM (ei->pos);
		goto out;
	}

	/* Sorting eigenvalues */
	ev_sort = g_new (gnumeric_eigen_ev_t, A->cols);
	for (i = 0; i < A->cols; i++) {
		ev_sort[i].val = eigenvalues[i];
		ev_sort[i].index = i;
	}
	qsort (ev_sort, A->cols, sizeof (gnumeric_eigen_ev_t), compare_gnumeric_eigen_ev);

	res = value_new_array_non_init (A->cols, A->rows + 1);
	for (c = 0; c < A->cols; ++c) {
		res->v_array.vals[c] = g_new (GnmValue *, A->rows + 1);
		res->v_array.vals[c][0] = value_new_float (eigenvalues[ev_sort[c].index]);
		for (r = 0; r < A->rows; ++r) {
			gnm_float tmp = EIG->data[r][ev_sort[c].index];
			res->v_array.vals[c][r + 1] = value_new_float (tmp);
		}
	}

	g_free (ev_sort);

out:
	if (A) gnm_matrix_free (A);
	if (EIG) gnm_matrix_free (EIG);
	g_free (eigenvalues);
	return res;
}


/***************************************************************************/

GnmFuncDescriptor const math_functions[] = {
	{ "abs",     "f",     help_abs,
	  gnumeric_abs, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "acos",    "f",     help_acos,
	  gnumeric_acos, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "acosh",   "f",     help_acosh,
	  gnumeric_acosh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "acot",     "f",     help_acot,
	  gnumeric_acot, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "acoth",     "f",     help_acoth,
	  gnumeric_acoth, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "arabic",       "S",             help_arabic,
	  gnumeric_arabic, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "asin",    "f",     help_asin,
	  gnumeric_asin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "asinh",   "f",     help_asinh,
	  gnumeric_asinh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "atan",    "f",     help_atan,
	  gnumeric_atan, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "atanh",   "f",     help_atanh,
	  gnumeric_atanh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "atan2",   "ff",  help_atan2,
	  gnumeric_atan2, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "beta",     "ff",      help_beta,
	  gnumeric_beta, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "betaln",   "ff",      help_betaln,
	  gnumeric_betaln, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "cholesky","A",      help_cholesky,
	  gnumeric_cholesky, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "cos",     "f",     help_cos,
	  gnumeric_cos, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "cosh",    "f",     help_cosh,
	  gnumeric_cosh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "cospi",   "f",     help_cospi,
	  gnumeric_cospi, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "cot",     "f",     help_cot,
	  gnumeric_cot, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "coth",     "f",     help_coth,
	  gnumeric_coth, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

/* MS Excel puts this in statistical */
	{ "countif", "rS",  help_countif,
	  gnumeric_countif, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "ceil",    "f",     help_ceil,
	  gnumeric_ceil, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "ceiling", "f|f",  help_ceiling,
	  gnumeric_ceiling, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "degrees", "f",     help_degrees,
	  gnumeric_degrees, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "even",    "f",     help_even,
	  gnumeric_even, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "exp",     "f",     help_exp,
	  gnumeric_exp, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "expm1",   "f",     help_expm1,
	  gnumeric_expm1, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "fact",    "f",     help_fact,
	  gnumeric_fact, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUPERSET, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },

/* MS Excel puts this in the engineering functions */
	{ "factdouble", "f",  help_factdouble,
	  gnumeric_factdouble, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "fib", "f",  help_fib,
	  gnumeric_fib, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "combin",  "ff",       help_combin,
	  gnumeric_combin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "combina",  "ff",       help_combina,
	  gnumeric_combina, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "csc",     "f",     help_csc,
	  gnumeric_csc, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "csch",     "f",     help_csch,
	  gnumeric_csch, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "floor",   "f|f",   help_floor,
	  gnumeric_floor, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gamma",    "f",     help_gamma,
	  gnumeric_gamma, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "gammaln",      "f",
	  help_gammaln, gnumeric_gammaln, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gcd", NULL,  help_gcd,
	  NULL, gnumeric_gcd, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gd",   "f",   help_gd,
	  gnumeric_gd, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hypot", NULL, help_hypot,
	  NULL, gnumeric_hypot, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "igamma",    "ff|bbb",  help_igamma,
	  gnumeric_igamma, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "int",     "f",     help_int,
	  gnumeric_int, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "lcm", NULL, help_lcm,
	  NULL, gnumeric_lcm, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ln",      "f",     help_ln,
	  gnumeric_ln, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "ln1p",    "f",     help_ln1p,
	  gnumeric_ln1p, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "log",     "f|f",  help_log,
	  gnumeric_log, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "log2",    "f",     help_log2,
	  gnumeric_log2, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "log10",   "f",     help_log10,
	  gnumeric_log10, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "mod",     "ff",  help_mod,
	  gnumeric_mod, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mround",  "ff",  help_mround,
	  gnumeric_mround, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "multinomial", NULL, help_multinomial,
	  NULL, gnumeric_multinomial, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "odd" ,    "f",     help_odd,
	  gnumeric_odd, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "power",   "ff|f",       help_power,
	  gnumeric_power, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUPERSET, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "pochhammer",   "ff",       help_pochhammer,
	  gnumeric_pochhammer, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
	{ "g_product", NULL,     help_g_product,
	  NULL, gnumeric_g_product, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "quotient" , "ff",  help_quotient,
	  gnumeric_quotient, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "radians", "f",     help_radians,
	  gnumeric_radians, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "roman",      "f|f",  help_roman,
	  gnumeric_roman, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "round",      "f|f",  help_round,
	  gnumeric_round, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rounddown",  "f|f",  help_rounddown,
	  gnumeric_rounddown, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "roundup",    "f|f",  help_roundup,
	  gnumeric_roundup, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sec",     "f",     help_sec,
	  gnumeric_sec, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "sech",     "f",     help_sech,
	  gnumeric_sech, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "seriessum", "fffA",  help_seriessum,
	  gnumeric_seriessum, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sign",    "f",     help_sign,
	  gnumeric_sign, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sin",     "f",     help_sin,
	  gnumeric_sin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "sinh",    "f",     help_sinh,
	  gnumeric_sinh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "sinpi",   "f",     help_sinpi,
	  gnumeric_sinpi, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "sqrt",    "f",     help_sqrt,
	  gnumeric_sqrt, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sqrtpi",  "f",     help_sqrtpi,
	  gnumeric_sqrtpi, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "suma", NULL,  help_suma,
	  NULL, gnumeric_suma, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumif",   "rS|r",  help_sumif,
	  gnumeric_sumif, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "averageif",   "rS|r",  help_averageif,
	  gnumeric_averageif, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "sumproduct", NULL,  help_sumproduct,
	  NULL, gnumeric_sumproduct, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "odf.sumproduct", NULL,  help_odf_sumproduct,
	  NULL, gnumeric_odf_sumproduct, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "sumsq", NULL,       help_sumsq,
	  NULL, gnumeric_sumsq, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumx2my2", "AA",  help_sumx2my2,
	  gnumeric_sumx2my2, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumx2py2", "AA",  help_sumx2py2,
	  gnumeric_sumx2py2, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumxmy2",  "AA",  help_sumxmy2,
	  gnumeric_sumxmy2, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tan",     "f",     help_tan,
	  gnumeric_tan, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "tanh",    "f",     help_tanh,
	  gnumeric_tanh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "trunc",   "f|f",  help_trunc,
	  gnumeric_trunc, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pi",      "", help_pi,
	  gnumeric_pi, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "mmult",   "AA",  help_mmult,
	  gnumeric_mmult, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "minverse","A",      help_minverse,
	  gnumeric_minverse, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mpseudoinverse","A|f", help_mpseudoinverse,
	  gnumeric_mpseudominverse, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "linsolve", "AA",  help_linsolve,
	  gnumeric_linsolve, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "mdeterm", "A",  help_mdeterm,
	  gnumeric_mdeterm, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "munit","f",      help_munit,
	  gnumeric_munit, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "eigen","A",      help_eigen,
	  gnumeric_eigen, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
#if 0
	{ "logmdeterm", "A|si",
	  help_logmdeterm, gnumeric_logmdeterm, NULL, NULL, NULL },
#endif
	{NULL}
};
