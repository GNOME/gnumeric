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
#include <rangefunc.h>
#include <collect.h>
#include <value.h>
#include <expr.h>
#include <regression.h>
#include <gnm-i18n.h>

#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

#include <math.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

typedef struct {
        GSList *list;
        int    num;
} math_sums_t;

static GnmValue *
callback_function_sumxy (GnmCellIter const *iter, gpointer user)
{
	GnmCell *cell;
	if (NULL == (cell = iter->cell))
	        return NULL;
	gnm_cell_eval (cell);

	if (VALUE_IS_NUMBER (cell->value)) {
		math_sums_t *mm = user;
		gnm_float *p = g_new (gnm_float, 1);
		*p = value_get_as_float (cell->value);
		mm->list = g_slist_append (mm->list, p);
		mm->num++;

		return NULL;
	} else if (VALUE_IS_ERROR (cell->value))
		return VALUE_TERMINATE;  /* FIXME: This is probably wrong.  */
	else
		return NULL;
}

/***************************************************************************/

static GnmFuncHelp const help_gcd[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=GCD\n"
	   "@SYNTAX=GCD(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "GCD returns the greatest common divisor of given numbers.\n"
	   "\n"
	   "* If any of the arguments is less than one, GCD returns #NUM! "
	   "error.\n"
	   "* If any of the arguments is non-integer, it is truncated.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "GCD(470,770) equals 10.\n"
	   "GCD(470,770,1495) equals 5.\n"
	   "\n"
	   "@SEEALSO=LCM")
	},
	{ GNM_FUNC_HELP_END }
};

static const double gnm_gcd_max = 1 / GNM_EPSILON;

static gnm_float
gnm_gcd (gnm_float a, gnm_float b)
{
	g_return_val_if_fail (a > 0 && a <= gnm_gcd_max, -1);
	g_return_val_if_fail (b > 0 && b <= gnm_gcd_max, -1);

	while (gnm_abs (b) > 0.5) {
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
		gnm_float gcd_so_far = xs[0];

		for (i = 0; i < n; i++) {
			gnm_float thisx = gnm_fake_floor (xs[i]);
			if (thisx <= 0 || thisx > gnm_gcd_max)
				return 1;
			else
				gcd_so_far = gnm_gcd (thisx, gcd_so_far);
		}
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LCM\n"
	   "@SYNTAX=LCM(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "LCM returns the least common multiple of integers.  The least "
	   "common multiple is the smallest positive number that is a "
	   "multiple of all integer arguments given.\n"
	   "\n"
	   "* If any of the arguments is less than one, LCM returns #NUM!.\n"
	   "* If any of the arguments is non-integer, it is truncated.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LCM(2,13) equals 26.\n"
	   "LCM(4,7,5) equals 140.\n"
	   "\n"
	   "@SEEALSO=GCD")
	},
	{ GNM_FUNC_HELP_END }
};

static int
range_lcm (gnm_float const *xs, int n, gnm_float *res)
{
	/* This function violates the "const".  */
	gnm_float *xsuc = (gnm_float *)xs;

	if (n > 0) {
		int i, j;
		gnm_float gcd_so_far = 1;

		for (i = j = 0; i < n; i++) {
			int k;
			gnm_float thisx = gnm_fake_floor (xsuc[i]);

			if (thisx < 1 || thisx > gnm_gcd_max)
				return 1;

			for (k = 0; k < j; k++)
				thisx /= gnm_gcd (thisx, xsuc[k]);

			if (thisx == 1)
				continue;

			xsuc[j++] = thisx;
			gcd_so_far *= thisx;
		}

		*res = gcd_so_far;
		return 0;
	} else
		return 1;
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

static GnmFuncHelp const help_hypot[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=HYPOT\n"
	   "@SYNTAX=HYPOT(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "HYPOT returns the square root of the sum of the squares of the arguments.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "HYPOT(3,4) equals 5.\n"
	   "\n"
	   "@SEEALSO=MIN,MAX")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ABS\n"
	   "@SYNTAX=ABS(b1)\n"

	   "@DESCRIPTION="
	   "ABS implements the Absolute Value function:  the result is "
	   "to drop the negative sign (if present).  This can be done for "
	   "integers and floating point numbers.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ABS(7) equals 7.\n"
	   "ABS(-3.14) equals 3.14.\n"
	   "\n"
	   "@SEEALSO=CEIL, CEILING, FLOOR, INT, MOD")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_abs (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_abs (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_acos[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ACOS\n"
	   "@SYNTAX=ACOS(x)\n"

	   "@DESCRIPTION="
	   "ACOS function calculates the arc cosine of @x; that "
	   "is the value whose cosine is @x.\n"
	   "\n"
	   "* The value it returns is in radians.\n"
	   "* If @x falls outside the range -1 to 1, ACOS returns "
	   "the #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ACOS(0.1) equals 1.470629.\n"
	   "ACOS(-0.1) equals 1.670964.\n"
	   "\n"
	   "@SEEALSO=COS, SIN, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_acos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t < -1.0 || t > 1.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_acos (t));
}

/***************************************************************************/

static GnmFuncHelp const help_acosh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ACOSH\n"
	   "@SYNTAX=ACOSH(x)\n"

	   "@DESCRIPTION="
	   "ACOSH  function  calculates  the inverse hyperbolic "
	   "cosine of @x; that is the value whose hyperbolic cosine is "
	   "@x.\n"
	   "\n"
	   "* If @x is less than 1.0, ACOSH() returns the #NUM! error.\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "ACOSH(2) equals 1.31696.\n"
	   "ACOSH(5.3) equals 2.35183.\n"
	   "\n"
	   "@SEEALSO=ACOS, ASINH, DEGREES, RADIANS ")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_acosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t < 1.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_acosh (t));
}

/***************************************************************************/

static GnmFuncHelp const help_asin[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ASIN\n"
	   "@SYNTAX=ASIN(x)\n"

	   "@DESCRIPTION="
	   "ASIN function calculates the arc sine of @x; that is "
	   "the value whose sine is @x.\n"
	   "\n"
	   "* If @x falls outside the range -1 to 1, ASIN returns "
	   "the #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ASIN(0.5) equals 0.523599.\n"
	   "ASIN(1) equals 1.570797.\n"
	   "\n"
	   "@SEEALSO=SIN, COS, ASINH, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_asin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t < -1.0 || t > 1.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_asin (t));
}

/***************************************************************************/

static GnmFuncHelp const help_asinh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ASINH\n"
	   "@SYNTAX=ASINH(x)\n"

	   "@DESCRIPTION="
	   "ASINH function calculates the inverse hyperbolic sine of @x; "
	   "that is the value whose hyperbolic sine is @x.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ASINH(0.5) equals 0.481212.\n"
	   "ASINH(1.0) equals 0.881374.\n"
	   "\n"
	   "@SEEALSO=ASIN, ACOSH, SIN, COS, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_asinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_asinh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_atan[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ATAN\n"
	   "@SYNTAX=ATAN(x)\n"

	   "@DESCRIPTION="
	   "ATAN function calculates the arc tangent of @x; that "
	   "is the value whose tangent is @x.\n\n"
	   "* Return value is in radians.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ATAN(0.5) equals 0,463648.\n"
	   "ATAN(1) equals 0,785398.\n"
	   "\n"
	   "@SEEALSO=TAN, COS, SIN, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_atan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_atan (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_atanh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ATANH\n"
	   "@SYNTAX=ATANH(x)\n"

	   "@DESCRIPTION="
	   "ATANH function calculates the inverse hyperbolic tangent "
	   "of @x; that is the value whose hyperbolic tangent is @x.\n"
	   "\n"
	   "* If the absolute value of @x is greater than 1.0, ATANH "
	   "returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ATANH(0.5) equals 0.549306.\n"
	   "ATANH(0.8) equals 1.098612.\n"
	   "\n"
	   "@SEEALSO=ATAN, TAN, SIN, COS, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_atanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t <= -1.0 || t >= 1.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_atanh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_atan2[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ATAN2\n"
	   "@SYNTAX=ATAN2(b1,b2)\n"

	   "@DESCRIPTION="
	   "ATAN2 function calculates the arc tangent of the two "
	   "variables @b1 and @b2.  It is similar to calculating the arc "
	   "tangent of @b2 / @b1, except that the signs of both arguments "
	   "are used to determine the quadrant of the result.\n\n"
	   "* The result is in radians.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ATAN2(0.5,1.0) equals 1.107149.\n"
	   "ATAN2(-0.5,2.0) equals 1.815775.\n"
	   "\n"
	   "@SEEALSO=ATAN, ATANH, COS, SIN, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_atan2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv [0]);
	gnm_float y = value_get_as_float (argv [1]);

	if (x == 0 && y == 0)
		return value_new_error_DIV0 (ei->pos);

	return value_new_float (gnm_atan2 (y, x));
}

/***************************************************************************/

static GnmFuncHelp const help_ceil[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CEIL\n"
	   "@SYNTAX=CEIL(x)\n"

	   "@DESCRIPTION="
	   "CEIL function rounds @x up to the next nearest integer.\n\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CEIL(0.4) equals 1.\n"
	   "CEIL(-1.1) equals -1.\n"
	   "CEIL(-2.9) equals -2.\n"
	   "\n"
	   "@SEEALSO=CEILING, FLOOR, ABS, INT, MOD")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ceil (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_fake_ceil (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_countif[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COUNTIF\n"
	   "@SYNTAX=COUNTIF(range,criteria)\n"

	   "@DESCRIPTION="
	   "COUNTIF function counts the number of cells in the given @range "
	   "that meet the given @criteria.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "23, 27, 28, 33, and 39.  Then\n"
	   "COUNTIF(A1:A5,\"<=28\") equals 3.\n"
	   "COUNTIF(A1:A5,\"<28\") equals 2.\n"
	   "COUNTIF(A1:A5,\"28\") equals 1.\n"
	   "COUNTIF(A1:A5,\">28\") equals 2.\n"
	   "\n"
	   "@SEEALSO=COUNT,SUMIF")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        GnmCriteriaFunc  test;
        GnmValue *test_value;
	GODateConventions const *date_conv;
	int count;
} CountIfClosure;

static GnmValue *
cb_countif (GnmCellIter const *iter, CountIfClosure *res)
{
	GnmCell *cell;
	if (NULL != (cell = iter->cell)) {
		gnm_cell_eval (cell);
		if ((VALUE_IS_NUMBER (cell->value) ||
		     VALUE_IS_STRING (cell->value)) &&
		    (res->test) (cell->value, res->test_value, res->date_conv))
			res->count++;
	}

	return NULL;
}

static GnmValue *
gnumeric_countif (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GnmValueRange const *r = &argv[0]->v_range;
	Sheet		*sheet;
	GnmValue        *problem;
	CellIterFlags	 iter_flags;
	CountIfClosure   res;

	/* XL has some limitations on @range that we currently emulate, but do
	 * not need to.
	 * 1) @range must be a range, arrays are not supported
	 * 2) @range can not be 3d */
	if (r->type != VALUE_CELLRANGE ||
	    ((sheet = eval_sheet (r->cell.a.sheet, ei->pos->sheet)) != r->cell.b.sheet &&
	      r->cell.b.sheet != NULL) ||
	    (!VALUE_IS_NUMBER (argv[1]) && !VALUE_IS_STRING (argv[1])))
	        return value_new_error_VALUE (ei->pos);

	res.date_conv = sheet ?	workbook_date_conv (sheet->workbook) : NULL;

	res.count = 0;
	parse_criteria (argv[1], &res.test, &res.test_value, &iter_flags,
		workbook_date_conv (ei->pos->sheet->workbook));
#warning 2006/May/31  Why do we not filter non-existent as a flag, rather than checking for NULL in cb_countif
	problem = sheet_foreach_cell_in_range (sheet, iter_flags,
		r->cell.a.col, r->cell.a.row, r->cell.b.col, r->cell.b.row,
		(CellIterFunc) &cb_countif, &res);
	value_release (res.test_value);
	if (NULL != problem)
	        return value_new_error_VALUE (ei->pos);
	return value_new_int (res.count);
}

/***************************************************************************/

static GnmFuncHelp const help_sumif[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SUMIF\n"
	   "@SYNTAX=SUMIF(range,criteria[,actual_range])\n"

	   "@DESCRIPTION="
	   "SUMIF function sums the values in the given @range that meet "
	   "the given @criteria.  If @actual_range is given, SUMIF sums "
	   "the values in the @actual_range whose corresponding components "
	   "in @range meet the given @criteria.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "23, 27, 28, 33, and 39.  Then\n"
	   "SUMIF(A1:A5,\"<=28\") equals 78.\n"
	   "SUMIF(A1:A5,\"<28\") equals 50.\n"
	   "In addition, if the cells B1, B2, ..., B5 hold numbers "
	   "5, 3, 2, 6, and 7 then:\n"
	   "SUMIF(A1:A5,\"<=27\",B1:B5) equals 8.\n"
	   "\n"
	   "@SEEALSO=COUNTIF, SUM")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
        GnmCriteriaFunc  test;
        GnmValue            *test_value;
	GODateConventions const *date_conv;

	Sheet		*target_sheet;
	GnmCellPos	 offset;
	gnm_float	 sum;
} SumIfClosure;

static GnmValue *
cb_sumif (GnmCellIter const *iter, SumIfClosure *res)
{
	GnmCell *cell;
	if (NULL == (cell = iter->cell))
		return NULL;
	gnm_cell_eval (cell);

	if ((VALUE_IS_NUMBER (cell->value) || VALUE_IS_STRING (cell->value)) &&
	    (res->test) (cell->value, res->test_value, res->date_conv)) {
		if (NULL != res->target_sheet) {
			cell = sheet_cell_get (res->target_sheet,
				iter->pp.eval.col + res->offset.col,
				iter->pp.eval.row + res->offset.row);
			if (cell != NULL) {
				gnm_cell_eval (cell);
				switch (cell->value->type) {
				case VALUE_FLOAT:
					/* FIXME: Check bools.  */
					res->sum += value_get_as_float (cell->value);
					break;
				default:
					break;
				}
			}
		} else
			/* FIXME: Check bools.  */
			res->sum += value_get_as_float (cell->value);
	}

	return NULL;
}

static GnmValue *
gnumeric_sumif (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GnmValueRange const *r = &argv[0]->v_range;
	Sheet		*sheet;
	GnmValue        *problem;
	CellIterFlags	 iter_flags;
	SumIfClosure	 res;
	int		 tmp, col_end, row_end;

	/* XL has some limitations on @range that we currently emulate, but do
	 * not need to.
	 * 1) @range must be a range, arrays are not supported
	 * 2) @range can not be 3d */
	if (r->type != VALUE_CELLRANGE ||
	    ((sheet = eval_sheet (r->cell.a.sheet, ei->pos->sheet)) != r->cell.b.sheet &&
	      r->cell.b.sheet != NULL) ||
	    (!VALUE_IS_NUMBER (argv[1]) && !VALUE_IS_STRING (argv[1])))
	        return value_new_error_VALUE (ei->pos);

	res.date_conv = sheet ?	workbook_date_conv (sheet->workbook) : NULL;

	col_end = r->cell.b.col;
	row_end = r->cell.b.row;
	if (NULL != argv[2]) {
		GnmValueRange const *target = &argv[2]->v_range;
		res.target_sheet = eval_sheet (target->cell.a.sheet, ei->pos->sheet);
		if (target->cell.b.sheet && res.target_sheet != target->cell.b.sheet)
			return value_new_error_VALUE (ei->pos);
		res.offset.col = target->cell.a.col - r->cell.a.col;
		res.offset.row = target->cell.a.row - r->cell.a.row;

		/* no need to search items with no value */
		tmp = target->cell.b.col - target->cell.a.col;
		if (tmp < (r->cell.b.col - r->cell.a.col))
			col_end = r->cell.a.col + tmp;
		tmp = target->cell.b.row - target->cell.a.row;
		if (tmp < (r->cell.b.row - r->cell.a.row))
			row_end = r->cell.a.row + tmp;
	} else
		res.target_sheet = NULL;

	res.sum = 0.;
	parse_criteria (argv[1], &res.test, &res.test_value, &iter_flags,
		workbook_date_conv (ei->pos->sheet->workbook));
#warning 2006/May/31  Why do we not filter non-existent as a flag, rather than checking for NULL in cb_sumif
	problem = sheet_foreach_cell_in_range (sheet, iter_flags,
		r->cell.a.col, r->cell.a.row, col_end, row_end,
		(CellIterFunc) &cb_sumif, &res);
	value_release (res.test_value);

	if (NULL != problem)
	        return value_new_error_VALUE (ei->pos);
	return value_new_float (res.sum);
}

/***************************************************************************/

static GnmFuncHelp const help_ceiling[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CEILING\n"
	   "@SYNTAX=CEILING(x[,significance])\n"

	   "@DESCRIPTION="
	   "CEILING function rounds @x up to the nearest multiple of "
	   "@significance.\n"
	   "\n"
	   "* If @x or @significance is non-numeric CEILING returns "
	   "#VALUE! error.\n"
	   "* If @x and @significance have different signs CEILING returns "
	   "#NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CEILING(2.43,1) equals 3.\n"
	   "CEILING(123.123,3) equals 126.\n"
	   "\n"
	   "@SEEALSO=CEIL, FLOOR, ABS, INT, MOD")
	},
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COS\n"
	   "@SYNTAX=COS(x)\n"

	   "@DESCRIPTION="
	   "COS function returns the cosine of @x, where @x is given "
           "in radians.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COS(0.5) equals 0.877583.\n"
	   "COS(1) equals 0.540302.\n"
	   "\n"
	   "@SEEALSO=COSH, SIN, SINH, TAN, TANH, RADIANS, DEGREES")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_cos (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_cosh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COSH\n"
	   "@SYNTAX=COSH(x)\n"

	   "@DESCRIPTION="
	   "COSH function returns the hyperbolic cosine of @x, which "
	   "is defined mathematically as\n\n\t(exp(@x) + exp(-@x)) / 2.\n\n"
	   "* @x is in radians.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COSH(0.5) equals 1.127626.\n"
	   "COSH(1) equals 1.543081.\n"
	   "\n"
	   "@SEEALSO=COS, SIN, SINH, TAN, TANH, RADIANS, DEGREES, EXP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_cosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_cosh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_degrees[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DEGREES\n"
	   "@SYNTAX=DEGREES(x)\n"

	   "@DESCRIPTION="
	   "DEGREES computes the number of degrees equivalent to @x radians.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DEGREES(2.5) equals 143.2394.\n"
	   "\n"
	   "@SEEALSO=RADIANS, PI")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_degrees (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float ((value_get_as_float (argv [0]) * 180.0) /
				M_PIgnum);
}

/***************************************************************************/

static GnmFuncHelp const help_exp[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EXP\n"
	   "@SYNTAX=EXP(x)\n"

	   "@DESCRIPTION="
	   "EXP computes the value of e (the base of natural logarithms) "
	   "raised to the power of @x.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "EXP(2) equals 7.389056.\n"
	   "\n"
	   "@SEEALSO=LOG, LOG2, LOG10")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_exp (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_exp (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_expm1[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EXPM1\n"
	   "@SYNTAX=EXPM1(x)\n"

	   "@DESCRIPTION="
	   "EXPM1 computes EXP(@x)-1 with higher resulting precision than "
	   "the direct formula.\n\n"
	   "@EXAMPLES=\n"
	   "EXPM1(0.01) equals 0.01005.\n"
	   "\n"
	   "@SEEALSO=EXP, LN1P")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_expm1 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_expm1 (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_fact[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FACT\n"
	   "@SYNTAX=FACT(x)\n"

	   "@DESCRIPTION="
	   "FACT computes the factorial of @x. ie, @x!\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "FACT(3) equals 6.\n"
	   "FACT(9) equals 362880.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fact (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gboolean x_is_integer = (x == gnm_floor (x));

	if (x < 0 && x_is_integer)
		return value_new_error_NUM (ei->pos);

	if (x_is_integer)
		return value_new_float (fact (x));
	else {
		gnm_float res = gnm_exp (lgamma1p (x));
		if (x < 0 && gnm_fmod (gnm_floor (-x), 2.0) != 0.0)
			res = 0 - res;
		return value_new_float (res);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_beta[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=BETA\n"
	   "@SYNTAX=BETA(a,b)\n"

	   "@DESCRIPTION="
	   "BETA function returns the value of the mathematical beta function "
	   "extended to all real numbers except 0 and negative integers.\n"
	   "\n"
	   "* If @a, @b, or (@a + @b) are non-positive integers, BETA returns #NUM! "
	   "error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "BETA(2,3) equals 0.083333.\n"
	   "BETA(-0.5,0.5) equals #NUM!.\n"
	   "\n"
	   "@SEEALSO=BETALN,GAMMALN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_beta (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);

#warning "Improve error handling.  Relying on value_new_float to do it is cheesy"
	return value_new_float (beta (a, b));
}

/***************************************************************************/

static GnmFuncHelp const help_betaln[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=BETALN\n"
	   "@SYNTAX=BETALN(a,b)\n"

	   "@DESCRIPTION="
	   "BETALN function returns the natural logarithm of the "
	   "absolute value of the beta function.\n"
	   "\n"
	   "* If @a, @b, or (@a + @b) are non-positive integers, BETALN returns #NUM! "
	   "\n"
	   "@EXAMPLES=\n"
	   "BETALN(2,3) equals -2.48.\n"
	   "BETALN(-0.5,0.5) equals #NUM!.\n"
	   "\n"
	   "@SEEALSO=BETA,GAMMALN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_betaln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float a = value_get_as_float (argv[0]);
	gnm_float b = value_get_as_float (argv[1]);
	int sign;

	return value_new_float (lbeta3 (a, b, &sign));
}

/***************************************************************************/

static GnmFuncHelp const help_combin[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COMBIN\n"
	   "@SYNTAX=COMBIN(n,k)\n"

	   "@DESCRIPTION="
	   "COMBIN computes the number of combinations.\n"
	   "\n"
	   "* Performing this function on a non-integer or a negative number "
           "returns #NUM! error.\n"
	   "* If @n is less than @k COMBIN returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COMBIN(8,6) equals 28.\n"
	   "COMBIN(6,2) equals 15.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
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

static GnmFuncHelp const help_floor[] = {
	{ GNM_FUNC_HELP_NAME, F_("FLOOR:rounds down.") },
	{ GNM_FUNC_HELP_ARG, F_("x:value.") },
	{ GNM_FUNC_HELP_ARG, F_("significance:base multiple (defaults to 1 for @x > 0 and -1 for @x <0)") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
	   "FLOOR function rounds @x down to the next nearest multiple "
	   "of @significance.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("FLOOR(0.5) equals 0.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("FLOOR(5,2) equals 4.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("FLOOR(-5,-2) equals -4.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("FLOOR(-5,2) equals #NUM!.") },
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=INT\n"
	   "@SYNTAX=INT(a)\n"

	   "@DESCRIPTION="
	   "INT function returns the largest integer that is not "
	   "bigger than its argument.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "INT(7.2) equals 7.\n"
	   "INT(-5.5) equals -6.\n"
	   "\n"
	   "@SEEALSO=CEIL, CEILING, FLOOR, ABS, MOD")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_int (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_fake_floor
				(value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_log[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LOG\n"
	   "@SYNTAX=LOG(x[,base])\n"

	   "@DESCRIPTION="
	   "LOG computes the logarithm of @x in the given base @base.  "
	   "If no @base is given LOG returns the logarithm in base 10. "
	   "@base must be > 0. and cannot equal 1.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LOG(2) equals 0.30103.\n"
	   "LOG(8192,2) equals 13.\n"
	   "\n"
	   "@SEEALSO=LN, LOG2, LOG10")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_log (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);
	gnm_float base = argv[1] ? value_get_as_float (argv[1]) : 10;

	if (base == 1. || base <= 0.)
		return value_new_error_NUM (ei->pos);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log (t) / gnm_log (base));
}

/***************************************************************************/

static GnmFuncHelp const help_ln[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LN\n"
	   "@SYNTAX=LN(x)\n"

	   "@DESCRIPTION="
	   "LN returns the natural logarithm of @x.\n"
	   "\n"
	   "* If @x <= 0, LN returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LN(7) equals 1.94591.\n"
	   "\n"
	   "@SEEALSO=EXP, LOG2, LOG10")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log (t));
}

/***************************************************************************/

static GnmFuncHelp const help_ln1p[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LN1P\n"
	   "@SYNTAX=LN1P(x)\n"

	   "@DESCRIPTION="
	   "LN1P computes LN(1+@x) with higher resulting precision than "
	   "the direct formula.\n"
	   "\n"
	   "* If @x <= -1, LN1P returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LN1P(0.01) equals 0.00995.\n"
	   "\n"
	   "@SEEALSO=LN, EXPM1")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ln1p (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t <= -1)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log1p (t));
}

/***************************************************************************/

static GnmFuncHelp const help_power[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=POWER\n"
	   "@SYNTAX=POWER(x,y)\n"

	   "@DESCRIPTION="
	   "POWER returns the value of @x raised to the power @y.\n\n"
	   "\n"
	   "* If both @x and @y equal 0, POWER returns #NUM! error.\n"
	   "* If @x = 0 and @y < 0, POWER returns #DIV/0! error.\n"
	   "* If @x < 0 and @y is non-integer, POWER returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "POWER(2,7) equals 128.\n"
	   "POWER(3,3.141) equals 31.523749.\n"
	   "\n"
	   "@SEEALSO=EXP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_power (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv [0]);
	gnm_float y = value_get_as_float (argv [1]);

	if ((x > 0) || (x == 0 && y > 0) || (x < 0 && y == gnm_floor (y)))
		return value_new_float (gnm_pow (x, y));

	if (x == 0 && y != 0)
		return value_new_error_DIV0 (ei->pos);
	else
		return value_new_error_NUM (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_log2[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LOG2\n"
	   "@SYNTAX=LOG2(x)\n"

	   "@DESCRIPTION="
	   "LOG2 computes the base-2 logarithm of @x.\n\n"
	   "* If @x <= 0, LOG2 returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LOG2(1024) equals 10.\n"
	   "\n"
	   "@SEEALSO=EXP, LOG10, LOG")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_log2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log (t) / M_LN2gnum);
}

/***************************************************************************/

static GnmFuncHelp const help_log10[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LOG10\n"
	   "@SYNTAX=LOG10(x)\n"

	   "@DESCRIPTION="
	   "LOG10 computes the base-10 logarithm of @x.\n\n"
	   "* If @x <= 0, LOG10 returns #NUM! error.\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "LOG10(7) equals 0.845098.\n"
	   "\n"
	   "@SEEALSO=EXP, LOG2, LOG")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_log10 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float t = value_get_as_float (argv [0]);

	if (t <= 0.0)
		return value_new_error_NUM (ei->pos);

	return value_new_float (gnm_log10 (t));
}

/***************************************************************************/

static GnmFuncHelp const help_mod[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MOD\n"
	   "@SYNTAX=MOD(number,divisor)\n"

	   "@DESCRIPTION="
	   "MOD function returns the remainder when @divisor is divided "
	   "into @number.\n"
	   "\n"
	   "* MOD returns #DIV/0! if @divisor is zero.\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "MOD(23,7) equals 2.\n"
	   "\n"
	   "@SEEALSO=CEIL, CEILING, FLOOR, ABS, INT, ABS")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=RADIANS\n"
	   "@SYNTAX=RADIANS(x)\n"

	   "@DESCRIPTION="
	   "RADIANS computes the number of radians equivalent to @x degrees.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "RADIANS(180) equals 3.14159.\n"
	   "\n"
	   "@SEEALSO=PI,DEGREES")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_radians (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float ((value_get_as_float (argv [0]) * M_PIgnum) /
				180);
}

/***************************************************************************/

static GnmFuncHelp const help_sin[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SIN\n"
	   "@SYNTAX=SIN(x)\n"

	   "@DESCRIPTION="
	   "SIN function returns the sine of @x, where @x is given "
           "in radians.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "SIN(0.5) equals 0.479426.\n"
	   "\n"
	   "@SEEALSO=COS, COSH, SINH, TAN, TANH, RADIANS, DEGREES")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_sin (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_sinh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SINH\n"
	   "@SYNTAX=SINH(x)\n"

	   "@DESCRIPTION="
	   "SINH function returns the hyperbolic sine of @x, "
	   "which is defined mathematically as\n\n\t"
	   "(exp(@x) - exp(-@x)) / 2.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "SINH(0.5) equals 0.521095.\n"
	   "\n"
	   "@SEEALSO=SIN, COS, COSH, TAN, TANH, DEGREES, RADIANS, EXP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_sinh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_sqrt[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SQRT\n"
	   "@SYNTAX=SQRT(x)\n"

	   "@DESCRIPTION="
	   "SQRT function returns the square root of @x.\n"
	   "\n"
	   "* If @x is negative, SQRT returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "SQRT(2) equals 1.4142136.\n"
	   "\n"
	   "@SEEALSO=POWER")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SUMA\n"
	   "@SYNTAX=SUMA(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUMA computes the sum of all the values and cells referenced "
	   "in the argument list.  Numbers, text and logical values are "
	   "included in the calculation too.  If the cell contains text or "
	   "the argument evaluates to FALSE, it is counted as value zero (0). "
	   "If the argument evaluates to TRUE, it is counted as one (1).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43.  Then\n"
	   "SUMA(A1:A5) equals 107.\n"
	   "\n"
	   "@SEEALSO=AVERAGE, SUM, COUNT")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SUMSQ\n"
	   "@SYNTAX=SUMSQ(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUMSQ returns the sum of the squares of all the values and "
	   "cells referenced in the argument list.\n\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43.  Then\n"
	   "SUMSQ(A1:A5) equals 2925.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MULTINOMIAL\n"
	   "@SYNTAX=MULTINOMIAL(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "MULTINOMIAL returns the ratio of the factorial of a sum of "
	   "values to the product of factorials.\n\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "MULTINOMIAL(2,3,4) equals 1260.\n"
	   "\n"
	   "@SEEALSO=SUM")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=G_PRODUCT\n"
	   "@SYNTAX=G_PRODUCT(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "G_PRODUCT returns the product of all the values and cells "
	   "referenced in the argument list.\n\n"
	   "* Empty cells are ignored and the empty product is 1.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "G_PRODUCT(2,5,9) equals 90.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TAN\n"
	   "@SYNTAX=TAN(x)\n"

	   "@DESCRIPTION="
	   "TAN function returns the tangent of @x, where @x is "
	   "given in radians.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TAN(3) equals -0.1425465.\n"
	   "\n"
	   "@SEEALSO=TANH, COS, COSH, SIN, SINH, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_tan (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_tanh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TANH\n"
	   "@SYNTAX=TANH(x)\n"

	   "@DESCRIPTION="
	   "TANH function returns the hyperbolic tangent of @x, "
	   "which is defined mathematically as \n\n\tsinh(@x) / cosh(@x).\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TANH(2) equals 0.96402758.\n"
	   "\n"
	   "@SEEALSO=TAN, SIN, SINH, COS, COSH, DEGREES, RADIANS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_tanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (gnm_tanh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static GnmFuncHelp const help_pi[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PI\n"
	   "@SYNTAX=PI()\n"

	   "@DESCRIPTION="
	   "PI functions returns the value of pi.\n"
	   "\n"
	   "* This function is called with no arguments.\n"
	   "* This function is Excel compatible, except that "
           "it returns pi with a better precision.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "PI() equals about 3.141593.\n"
	   "\n"
	   "@SEEALSO=SQRTPI")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_pi (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_float (M_PIgnum);
}

/***************************************************************************/

static GnmFuncHelp const help_trunc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TRUNC\n"
	   "@SYNTAX=TRUNC(number[,digits])\n"

	   "@DESCRIPTION="
	   "TRUNC function returns the value of @number "
	   "truncated to the number of digits specified.\n\n"
	   "* If @digits is omitted or negative then @digits defaults to zero.\n"
	   "* If @digits is not an integer, it is truncated.\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "TRUNC(3.12) equals 3.\n"
	   "TRUNC(4.15,1) equals 4.1.\n"
	   "\n"
	   "@SEEALSO=INT")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EVEN\n"
	   "@SYNTAX=EVEN(number)\n"

	   "@DESCRIPTION="
	   "EVEN function returns the number rounded up to the "
	   "nearest even integer.  Negative numbers are rounded down.\n\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "EVEN(5.4) equals 6.\n"
	   "EVEN(-5.4) equals -6.\n"
	   "\n"
	   "@SEEALSO=ODD")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ODD\n"
	   "@SYNTAX=ODD(number)\n"

	   "@DESCRIPTION="
	   "ODD function returns the @number rounded up to the "
	   "nearest odd integer.  Negative numbers are rounded down.\n\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "ODD(4.4) equals 5.\n"
	   "ODD(-4.4) equals -5.\n"
	   "\n"
	   "@SEEALSO=EVEN")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FACTDOUBLE\n"
	   "@SYNTAX=FACTDOUBLE(number)\n"

	   "@DESCRIPTION="
	   "FACTDOUBLE function returns the double factorial "
	   "of a @number, i.e., x!!.\n"
	   "\n"
	   "* If @number is not an integer, it is truncated.\n"
	   "* If @number is negative FACTDOUBLE returns #NUM! error.\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "FACTDOUBLE(5) equals 15.\n"
	   "\n"
	   "@SEEALSO=FACT")
	},
	{ GNM_FUNC_HELP_END }
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
		res = fact (n) * gnm_pow2 (n);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_fib[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FIB\n"
	   "@SYNTAX=FIB(number)\n"

	   "@DESCRIPTION="
	   "FIB function computes Fibonacci numbers.\n"
	   "\n"
	   "* If @number is not an integer, it is truncated.\n"
	   "* If @number is negative or zero FIB returns #NUM! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "FIB(12) equals 144.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=QUOTIENT\n"
	   "@SYNTAX=QUOTIENT(numerator,denominator)\n"

	   "@DESCRIPTION="
	   "QUOTIENT function returns the integer portion "
	   "of a division.  @numerator is the divided number and "
	   "@denominator is the divisor.\n\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "QUOTIENT(23,5) equals 4.\n"
	   "\n"
	   "@SEEALSO=MOD")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SIGN\n"
	   "@SYNTAX=SIGN(number)\n"

	   "@DESCRIPTION="
	   "SIGN function returns 1 if the @number is positive, "
	   "zero if the @number is 0, and -1 if the @number is negative.\n\n"
	   "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "SIGN(3) equals 1.\n"
	   "SIGN(-3) equals -1.\n"
	   "SIGN(0) equals 0.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SQRTPI\n"
	   "@SYNTAX=SQRTPI(number)\n"

	   "@DESCRIPTION="
	   "SQRTPI function returns the square root of a @number "
	   "multiplied by pi.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "SQRTPI(2) equals 2.506628275.\n"
	   "\n"
	   "@SEEALSO=PI")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ROUNDDOWN\n"
	   "@SYNTAX=ROUNDDOWN(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUNDDOWN function rounds a given @number towards 0.\n\n"
	   "@number is the number you want rounded toward 0 and @digits is the "
	   "number of digits to which you want to round that number.\n"
	   "\n"
	   "* If @digits is greater than zero, @number is rounded toward 0 to "
	   "the given number of digits.\n"
	   "* If @digits is zero or omitted, @number is rounded toward 0 to "
	   "the next integer.\n"
	   "* If @digits is less than zero, @number is rounded toward 0 to the "
	   "left of the decimal point.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ROUNDDOWN(5.5) equals 5.\n"
	   "ROUNDDOWN(-3.3) equals -3.\n"
	   "ROUNDDOWN(1501.15,1) equals 1501.1.\n"
	   "ROUNDDOWN(1501.15,-2) equals 1500.0.\n"
	   "\n"
	   "@SEEALSO=ROUND,ROUNDUP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rounddown (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return gnumeric_trunc (ei, argv);
}

/***************************************************************************/

static GnmFuncHelp const help_round[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ROUND\n"
	   "@SYNTAX=ROUND(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUND function rounds a given number.\n\n"
	   "@number is the number you want rounded and @digits is the "
	   "number of digits to which you want to round that number.\n"
	   "\n"
	   "* If @digits is greater than zero, @number is rounded to the "
	   "given number of digits.\n"
	   "* If @digits is zero or omitted, @number is rounded to the "
	   "nearest integer.\n"
	   "* If @digits is less than zero, @number is rounded to the left "
	   "of the decimal point.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ROUND(5.5) equals 6.\n"
	   "ROUND(-3.3) equals -3.\n"
	   "ROUND(1501.15,1) equals 1501.2.\n"
	   "ROUND(1501.15,-2) equals 1500.0.\n"
	   "\n"
	   "@SEEALSO=ROUNDDOWN,ROUNDUP")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ROUNDUP\n"
	   "@SYNTAX=ROUNDUP(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUNDUP function rounds a given number away from 0.\n\n"
	   "@number is the number you want rounded away from 0 and "
	   "@digits is the "
	   "number of digits to which you want to round that number.\n"
	   "\n"
	   "* If @digits is greater than zero, @number is rounded away from "
	   "0 to the given number of digits.\n"
	   "* If @digits is zero or omitted, @number is rounded away from 0 "
	   "to the next integer.\n"
	   "* If @digits is less than zero, @number is rounded away from 0 "
	   "to the left of the decimal point.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ROUNDUP(5.5) equals 6.\n"
	   "ROUNDUP(-3.3) equals -4.\n"
	   "ROUNDUP(1501.15,1) equals 1501.2.\n"
	   "ROUNDUP(1501.15,-2) equals 1600.0.\n"
	   "\n"
	   "@SEEALSO=ROUND,ROUNDDOWN")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MROUND\n"
	   "@SYNTAX=MROUND(number,multiple)\n"

	   "@DESCRIPTION="
	   "MROUND function rounds a given number to the desired multiple.\n\n"
	   "@number is the number you want rounded and @multiple is the "
	   "the multiple to which you want to round the number.\n"
	   "\n"
	   "* If @number and @multiple have different sign, MROUND "
	   "returns #NUM! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "MROUND(1.7,0.2) equals 1.8.\n"
	   "MROUND(321.123,0.12) equals 321.12.\n"
	   "\n"
	   "@SEEALSO=ROUNDDOWN,ROUND,ROUNDUP")
	},
	{ GNM_FUNC_HELP_END }
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

static GnmFuncHelp const help_roman[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ROMAN\n"
	   "@SYNTAX=ROMAN(number[,type])\n"

	   "@DESCRIPTION="
	   "ROMAN function returns an arabic number in the roman numeral "
	   "style, as text. @number is the number you want to convert and "
	   "@type is the type of roman numeral you want.\n"
	   "\n"
	   "* If @type is 0 or it is omitted, ROMAN returns classic roman "
	   "numbers.\n"
	   "* Type 1 is more concise than classic type, type 2 is more concise "
	   "than type 1, and type 3 is more concise than type 2.  Type 4 "
	   "is simplified type."
	   "\n"
	   "* If @number is negative or greater than 3999, ROMAN returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ROMAN(999) equals CMXCIX.\n"
	   "ROMAN(999,1) equals LMVLIV.\n"
	   "ROMAN(999,2) equals XMIX.\n"
	   "ROMAN(999,3) equals VMIV.\n"
	   "ROMAN(999,4) equals IM.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SUMX2MY2\n"
	   "@SYNTAX=SUMX2MY2(array1,array2)\n"

	   "@DESCRIPTION="
	   "SUMX2MY2 function returns the sum of the difference of squares "
	   "of corresponding values in two arrays. @array1 is the first "
	   "array or range of data points and @array2 is the second array "
	   "or range of data points. The equation of SUMX2MY2 is "
	   "SUM (x^2-y^2).\n"
	   "\n"
           "* Strings and empty cells are simply ignored.\n"
	   "* If @array1 and @array2 have different number of data points, "
	   "SUMX2MY2 returns #N/A error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMX2MY2(A1:A5,B1:B5) equals -1299.\n"
	   "\n"
	   "@SEEALSO=SUMSQ,SUMX2PY2")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sumx2my2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GnmValue const *values_x = argv[0];
        GnmValue const *values_y = argv[1];
	math_sums_t items_x, items_y;
	GnmValue      *ret;
	gnm_float  sum;
	GSList     *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			CELL_ITER_ALL,
			values_x->v_range.cell.a.col,
			values_x->v_range.cell.a.row,
			values_x->v_range.cell.b.col,
			values_x->v_range.cell.b.row,
			callback_function_sumxy,
			&items_x);

		if (ret != NULL) {
		        ret = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else {
		ret = value_new_error (ei->pos,
				       _("Array version not implemented!"));
		goto out;
	}

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			CELL_ITER_ALL,
			values_y->v_range.cell.a.col,
			values_y->v_range.cell.a.row,
			values_y->v_range.cell.b.col,
			values_y->v_range.cell.b.row,
			callback_function_sumxy,
			&items_y);
		if (ret != NULL) {
		        ret = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else {
		ret = value_new_error (ei->pos,
				       _("Array version not implemented!"));
		goto out;
	}

	if (items_x.num != items_y.num) {
		ret = value_new_error_NA (ei->pos);
		goto out;
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum = 0;
	while (list1 != NULL) {
	        gnm_float x, y;

		x = *((gnm_float *) list1->data);
		y = *((gnm_float *) list2->data);
		sum += x * x - y * y;
		list1 = list1->next;
		list2 = list2->next;
	}
	ret = value_new_float (sum);

 out:
	for (list1 = items_x.list; list1; list1 = list1->next)
		g_free (list1->data);
	g_slist_free (items_x.list);

	for (list2 = items_y.list; list2; list2 = list2->next)
		g_free (list2->data);
	g_slist_free (items_y.list);

	return ret;
}

/***************************************************************************/

static GnmFuncHelp const help_sumx2py2[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SUMX2PY2\n"
	   "@SYNTAX=SUMX2PY2(array1,array2)\n"

	   "@DESCRIPTION="
	   "SUMX2PY2 function returns the sum of the sum of squares "
	   "of corresponding values in two arrays. @array1 is the first "
	   "array or range of data points and @array2 is the second array "
	   "or range of data points. The equation of SUMX2PY2 is "
	   "SUM (x^2+y^2).\n"
	   "\n"
           "* Strings and empty cells are simply ignored.\n"
	   "* If @array1 and @array2 have different number of data points, "
	   "SUMX2PY2 returns #N/A error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMX2PY2(A1:A5,B1:B5) equals 7149.\n"
	   "\n"
	   "@SEEALSO=SUMSQ,SUMX2MY2")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sumx2py2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GnmValue const *values_x = argv[0];
        GnmValue const *values_y = argv[1];
	math_sums_t items_x, items_y;
	GnmValue      *ret;
	gnm_float  sum;
	GSList     *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			CELL_ITER_ALL, /* include empties so that the lists align */
			values_x->v_range.cell.a.col,
			values_x->v_range.cell.a.row,
			values_x->v_range.cell.b.col,
			values_x->v_range.cell.b.row,
			callback_function_sumxy,
			&items_x);
		if (ret != NULL) {
		        ret = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else {
		ret = value_new_error (ei->pos,
				       _("Array version not implemented!"));
		goto out;
	}

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			CELL_ITER_ALL, /* include empties so that the lists align */
			values_y->v_range.cell.a.col,
			values_y->v_range.cell.a.row,
			values_y->v_range.cell.b.col,
			values_y->v_range.cell.b.row,
			callback_function_sumxy,
			&items_y);
		if (ret != NULL) {
			ret = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else {
		ret = value_new_error (ei->pos,
				       _("Array version not implemented!"));
		goto out;
	}

	if (items_x.num != items_y.num) {
		ret = value_new_error_NA (ei->pos);
		goto out;
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum = 0;
	while (list1 != NULL) {
	        gnm_float x, y;

		x = *((gnm_float *) list1->data);
		y = *((gnm_float *) list2->data);
		sum += x * x + y * y;
		list1 = list1->next;
		list2 = list2->next;
	}
	ret = value_new_float (sum);

 out:
	for (list1 = items_x.list; list1; list1 = list1->next)
		g_free (list1->data);
	g_slist_free (items_x.list);

	for (list2 = items_y.list; list2; list2 = list2->next)
		g_free (list2->data);
	g_slist_free (items_y.list);

	return ret;
}

static GnmFuncHelp const help_sumxmy2[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SUMXMY2\n"
	   "@SYNTAX=SUMXMY2(array1,array2)\n"

	   "@DESCRIPTION="
	   "SUMXMY2 function returns the sum of squares of differences "
	   "of corresponding values in two arrays. @array1 is the first "
	   "array or range of data points and @array2 is the second array "
	   "or range of data points. The equation of SUMXMY2 is "
	   "SUM (x-y)^2.\n"
	   "\n"
           "* Strings and empty cells are simply ignored.\n"
	   "* If @array1 and @array2 have different number of data points, "
	   "SUMXMY2 returns #N/A error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMXMY2(A1:A5,B1:B5) equals 409.\n"
	   "\n"
	   "@SEEALSO=SUMSQ,SUMX2MY2,SUMX2PY2")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sumxmy2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
        GnmValue const *values_x = argv[0];
        GnmValue const *values_y = argv[1];
	math_sums_t items_x, items_y;
	GnmValue      *ret;
	gnm_float  sum;
	GSList     *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			CELL_ITER_ALL, /* include empties so that the lists align */
			values_x->v_range.cell.a.col,
			values_x->v_range.cell.a.row,
			values_x->v_range.cell.b.col,
			values_x->v_range.cell.b.row,
			callback_function_sumxy,
			&items_x);
		if (ret != NULL) {
		        ret = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else {
		ret = value_new_error (ei->pos,
				       _("Array version not implemented!"));
		goto out;
	}

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_foreach_cell_in_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			CELL_ITER_ALL, /* include empties so that the lists align */
			values_y->v_range.cell.a.col,
			values_y->v_range.cell.a.row,
			values_y->v_range.cell.b.col,
			values_y->v_range.cell.b.row,
			callback_function_sumxy,
			&items_y);
		if (ret != NULL) {
		        ret = value_new_error_VALUE (ei->pos);
			goto out;
		}
	} else {
		ret = value_new_error (ei->pos,
				       _("Array version not implemented!"));
		goto out;
	}

	if (items_x.num != items_y.num) {
	        ret = value_new_error_NA (ei->pos);
		goto out;
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum = 0;
	while (list1 != NULL) {
	        gnm_float x, y;

		x = *((gnm_float *) list1->data);
		y = *((gnm_float *) list2->data);
		sum += (x - y) * (x - y);
		list1 = list1->next;
		list2 = list2->next;
	}
	ret = value_new_float (sum);

 out:
	for (list1 = items_x.list; list1; list1 = list1->next)
		g_free (list1->data);
	g_slist_free (items_x.list);

	for (list2 = items_y.list; list2; list2 = list2->next)
		g_free (list2->data);
	g_slist_free (items_y.list);

	return ret;
}

/***************************************************************************/

static GnmFuncHelp const help_seriessum[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SERIESSUM\n"
	   "@SYNTAX=SERIESSUM(x,n,m,coefficients)\n"

	   "@DESCRIPTION="
	   "SERIESSUM function returns the sum of a power series.  @x is "
	   "the base of the power series, @n is the initial power to raise @x, "
	   "@m is the increment to the power for each term in the series, and "
	   "@coefficients are the coefficients by which each successive power "
	   "of @x is multiplied.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "1.23, 2.32, 2.98, 3.42, and 4.33.  Then\n"
	   "SERIESSUM(3,1,2.23,A1:A5) equals 251416.43018.\n"
	   "\n"
	   "@SEEALSO=COUNT,SUM")
	},
	{ GNM_FUNC_HELP_END }
};

static int
range_seriessum (gnm_float const *xs, int n, gnm_float *res)
{
	if (n >= 3) {
		gnm_float x = xs[0];
		gnm_float N = xs[1];
		gnm_float m = xs[2];
		gnm_float sum = 0;

		gnm_float x_m = gnm_pow (x, m);
		gnm_float xpow = gnm_pow (x, N);
		int i;

		for (i = 3; i < n; i++) {
			sum += xs[i] * xpow;
			xpow *= x_m;
		}

		*res = sum;
		return 0;
	} else
		return 1;
}


static GnmValue *
gnumeric_seriessum (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_seriessum,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_NUM);
}

/***************************************************************************/

static GnmFuncHelp const help_minverse[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MINVERSE\n"
	   "@SYNTAX=MINVERSE(matrix)\n"

	   "@DESCRIPTION="
	   "MINVERSE function returns the inverse matrix of @matrix.\n"
	   "\n"
	   "* If @matrix cannot be inverted, MINVERSE returns #NUM! "
	   "error.\n"
	   "* If @matrix does not contain equal number of columns and "
	   "rows, MINVERSE returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=MMULT, MDETERM")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
cb_function_mmult_validate (GnmCellIter const *iter, gpointer user)
{
	GnmCell *cell = iter->cell;
        int *item_count = user;

	gnm_cell_eval (cell);
	if (!VALUE_IS_NUMBER (cell->value))
	        return VALUE_TERMINATE;

	++(*item_count);
	return NULL;
}

/* Returns TRUE on error */
static gboolean
validate_range_numeric_matrix (GnmEvalPos const *ep, GnmValue const *matrix,
			       int *rows, int *cols,
			       GnmStdError *err)
{
	GnmValue *res;
	int cell_count = 0;

	*cols = value_area_get_width (matrix, ep);
	*rows = value_area_get_height (matrix, ep);

	if (matrix->type == VALUE_ARRAY || matrix->type <= VALUE_FLOAT)
		return FALSE;
	if (matrix->type != VALUE_CELLRANGE ||
	    (matrix->v_range.cell.a.sheet != matrix->v_range.cell.b.sheet &&
	     matrix->v_range.cell.a.sheet != NULL &&
	     matrix->v_range.cell.b.sheet != NULL)) {
		*err = GNM_ERROR_VALUE;
		return TRUE;
	}

	res = sheet_foreach_cell_in_range (
		eval_sheet (matrix->v_range.cell.a.sheet, ep->sheet),
		CELL_ITER_IGNORE_BLANK,
		matrix->v_range.cell.a.col,
		matrix->v_range.cell.a.row,
		matrix->v_range.cell.b.col,
		matrix->v_range.cell.b.row,
		cb_function_mmult_validate,
		&cell_count);

	if (res != NULL || cell_count != (*rows * *cols)) {
		/* As specified in the Excel Docs */
		*err = GNM_ERROR_VALUE;
		return TRUE;
	}

	return FALSE;
}

static gnm_float **
value_to_matrix (GnmValue const *v, int cols, int rows, GnmEvalPos const *ep)
{
	gnm_float **res = g_new (gnm_float *, rows);
	int r, c;

	for (r = 0; r < rows; r++) {
		res[r] = g_new (gnm_float, cols);
		for (c = 0; c < cols; c++)
		        res[r][c] =
				value_get_as_float (value_area_get_x_y (v, c, r, ep));
	}

	return res;
}

static void
free_matrix (gnm_float **mat, G_GNUC_UNUSED int cols, int rows)
{
	int r;

	for (r = 0; r < rows; r++)
		g_free (mat[r]);

	g_free (mat);
}


static GnmValue *
gnumeric_minverse (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmEvalPos const * const ep = ei->pos;

	int	r, rows;
	int	c, cols;
	GnmValue *res;
        GnmValue const *values = argv[0];
	gnm_float **matrix;
	GnmStdError err;

	if (validate_range_numeric_matrix (ep, values, &rows, &cols, &err))
		return value_new_error_std (ei->pos, err);

	/* Guarantee shape and non-zero size */
	if (cols != rows || !rows || !cols)
		return value_new_error_VALUE (ei->pos);

	matrix = value_to_matrix (values, cols, rows, ep);
	if (!gnm_matrix_invert (matrix, rows)) {
		free_matrix (matrix, cols, rows);
		return value_new_error_NUM (ei->pos);
	}

	res = value_new_array_non_init (cols, rows);
	for (c = 0; c < cols; ++c) {
		res->v_array.vals[c] = g_new (GnmValue *, rows);
		for (r = 0; r < rows; ++r) {
			gnm_float tmp = matrix[r][c];
			res->v_array.vals[c][r] = value_new_float (tmp);
		}
	}
	free_matrix (matrix, cols, rows);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_mmult[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MMULT\n"
	   "@SYNTAX=MMULT(array1,array2)\n"

	   "@DESCRIPTION="
	   "MMULT function returns the matrix product of two arrays. The "
	   "result is an array with the same number of rows as @array1 and "
	   "the same number of columns as @array2.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TRANSPOSE,MINVERSE")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_mmult (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmEvalPos const * const ep = ei->pos;
	int	r, rows_a, rows_b;
	int	c, cols_a, cols_b;
        GnmValue *res;
        GnmValue const *values_a = argv[0];
        GnmValue const *values_b = argv[1];
	gnm_float *A, *B, *product;
	GnmStdError err;

	if (validate_range_numeric_matrix (ep, values_a, &rows_a, &cols_a, &err) ||
	    validate_range_numeric_matrix (ep, values_b, &rows_b, &cols_b, &err))
		return value_new_error_std (ei->pos, err);

	/* Guarantee shape and non-zero size */
	if (cols_a != rows_b || !rows_a || !rows_b || !cols_a || !cols_b)
		return value_new_error_VALUE (ei->pos);

	res = value_new_array_non_init (cols_b, rows_a);

	A = g_new (gnm_float, cols_a * rows_a);
	B = g_new (gnm_float, cols_b * rows_b);
	product = g_new (gnm_float, rows_a * cols_b);

	for (c = 0; c < cols_a; c++)
	        for (r = 0; r < rows_a; r++) {
		        GnmValue const * a =
			     value_area_get_x_y (values_a, c, r, ep);
		        A[r + c * rows_a] = value_get_as_float (a);
		}

	for (c = 0; c < cols_b; c++)
	        for (r = 0; r < rows_b; r++) {
		        GnmValue const * b =
			     value_area_get_x_y (values_b, c, r, ep);
		        B[r + c * rows_b] = value_get_as_float (b);
		}

	mmult (A, B, cols_a, rows_a, cols_b, product);

	for (c = 0; c < cols_b; c++) {
	        res->v_array.vals[c] = g_new (GnmValue *, rows_a);
	        for (r = 0; r < rows_a; r++)
		        res->v_array.vals[c][r] =
			    value_new_float (product[r + c * rows_a]);
	}
	g_free (A);
	g_free (B);
	g_free (product);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_mdeterm[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MDETERM\n"
	   "@SYNTAX=MDETERM(matrix)\n"

	   "@DESCRIPTION="
	   "MDETERM function returns the determinant of a given matrix.\n"
	   "\n"
	   "* If the @matrix does not contain equal number of columns and "
	   "rows, MDETERM returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that A1, ..., A4 contain numbers 2, 3, 7, and 3, "
	   "B1, ..., B4 4, 2, 4, and 1, C1, ..., C4 9, 4, 3, and 2, and "
	   "D1, ..., D4 7, 3, 6, and 5. Then\n"
	   "MDETERM(A1:D4) equals 148.\n"
	   "\n"
	   "@SEEALSO=MMULT, MINVERSE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_mdeterm (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmEvalPos const * const ep = ei->pos;

	int	rows, cols;
        gnm_float res;
	gnm_float **matrix;
	GnmStdError err;
        GnmValue  const *values = argv[0];

	if (validate_range_numeric_matrix (ep, values, &rows, &cols, &err))
		return value_new_error_std (ei->pos, err);

	/* Guarantee shape and non-zero size */
	if (cols != rows || !rows || !cols)
		return value_new_error_VALUE (ei->pos);

	matrix = value_to_matrix (values, cols, rows, ep);
	res = gnm_matrix_determinant (matrix, rows);
	free_matrix (matrix, cols, rows);

	return value_new_float (res);
}

/***************************************************************************/

static GnmFuncHelp const help_sumproduct[] = {
	{ GNM_FUNC_HELP_NAME, F_("SUMPRODUCT:Multiplies components and adds the results.") },
	{ GNM_FUNC_HELP_DESCRIPTION,
		F_("Multiplies corresponding data entries in the "
		   "given arrays or ranges, and then returns the sum of those "
		   "products.") },
	{ GNM_FUNC_HELP_NOTE, F_("If an entry is not numeric, the value zero is used instead.") },
	{ GNM_FUNC_HELP_NOTE, F_("If arrays or range arguments do not have the same dimensions, "
	   "return #VALUE! error.") },
	{ GNM_FUNC_HELP_NOTE, F_("SUMPRODUCTs arguments are arrays or ranges. "
				 "Attempting to use A1:A5>0 will not work, implicit intersection will kick in. "
				 "Instead use --(A1:A5>0)") },
#if 0
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMPRODUCT(A1:A5,B1:B5) equals 3370.\n"
#endif
	{ GNM_FUNC_HELP_SEEALSO, "SUM,PRODUCT,G_PRODUCT" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sumproduct (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
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
		GnmValue    *val = gnm_expr_eval (expr, ei->pos,
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
				default :
					/* Ignore booleans and strings to be consistent with XL */
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
		gnm_float sum = 0;
		int j;

		for (j = 0; j < sizex * sizey; j++) {
			gnm_float product = data[0][j];
			for (i = 1; i < argc; i++)
				product *= data[i][j];
			sum += product;
		}

		result = value_new_float (sum);
	}

 done:
	for (i = 0; i < argc; i++)
		g_free (data[i]);
	g_free (data);
	return result;
}

/***************************************************************************/

GnmFuncDescriptor const math_functions[] = {
	{ "abs",     "f", N_("number"),    help_abs,
	  gnumeric_abs, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "acos",    "f", N_("number"),    help_acos,
	  gnumeric_acos, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "acosh",   "f", N_("number"),    help_acosh,
	  gnumeric_acosh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "asin",    "f", N_("number"),    help_asin,
	  gnumeric_asin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "asinh",   "f", N_("number"),    help_asinh,
	  gnumeric_asinh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "atan",    "f", N_("number"),    help_atan,
	  gnumeric_atan, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "atanh",   "f", N_("number"),    help_atanh,
	  gnumeric_atanh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "atan2",   "ff", N_("xnum,ynum"), help_atan2,
	  gnumeric_atan2, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "cos",     "f", N_("number"),    help_cos,
	  gnumeric_cos, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "beta",     "ff", N_("a,b"),     help_beta,
	  gnumeric_beta, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "betaln",   "ff", N_("a,b"),     help_betaln,
	  gnumeric_betaln, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "cosh",    "f", N_("number"),    help_cosh,
	  gnumeric_cosh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },

/* MS Excel puts this in statistical */
	{ "countif", "rS", N_("range,criteria"), help_countif,
	  gnumeric_countif, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "ceil",    "f", N_("number"),    help_ceil,
	  gnumeric_ceil, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "ceiling", "f|f", N_("number,significance"), help_ceiling,
	  gnumeric_ceiling, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "degrees", "f", N_("number"),    help_degrees,
	  gnumeric_degrees, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "even",    "f", N_("number"),    help_even,
	  gnumeric_even, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "exp",     "f", N_("number"),    help_exp,
	  gnumeric_exp, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "expm1",   "f", N_("number"),    help_expm1,
	  gnumeric_expm1, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "fact",    "f", N_("number"),    help_fact,
	  gnumeric_fact, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUPERSET, GNM_FUNC_TEST_STATUS_BASIC },

/* MS Excel puts this in the engineering functions */
	{ "factdouble", "f", N_("number"), help_factdouble,
	  gnumeric_factdouble, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "fib", "f", N_("number"), help_fib,
	  gnumeric_fib, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "combin",  "ff", N_("n,k"),      help_combin,
	  gnumeric_combin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "floor",   "f|f", N_("number"),  help_floor,
	  gnumeric_floor, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "gcd", NULL, N_("number,number"), help_gcd,
	  NULL, gnumeric_gcd, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hypot", NULL, "",            help_hypot,
	  NULL, gnumeric_hypot, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "int",     "f", N_("number"),    help_int,
	  gnumeric_int, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "lcm", NULL, "",            help_lcm,
	  NULL, gnumeric_lcm, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ln",      "f", N_("number"),    help_ln,
	  gnumeric_ln, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "ln1p",    "f", N_("number"),    help_ln1p,
	  gnumeric_ln1p, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "log",     "f|f", N_("number,base"), help_log,
	  gnumeric_log, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "log2",    "f", N_("number"),    help_log2,
	  gnumeric_log2, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "log10",   "f", N_("number"),    help_log10,
	  gnumeric_log10, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mod",     "ff", N_("numerator,denominator"), help_mod,
	  gnumeric_mod, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mround",  "ff", N_("number,multiple"), help_mround,
	  gnumeric_mround, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "multinomial", NULL, "", help_multinomial,
	  NULL, gnumeric_multinomial, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "odd" ,    "f", N_("number"),    help_odd,
	  gnumeric_odd, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "power",   "ff", N_("base,exponent"),      help_power,
	  gnumeric_power, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "g_product", NULL, N_("number"),    help_g_product,
	  NULL, gnumeric_g_product, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "quotient" , "ff", N_("numerator,denominator"), help_quotient,
	  gnumeric_quotient, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "radians", "f", N_("number"),    help_radians,
	  gnumeric_radians, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "roman",      "f|f", N_("number,type"), help_roman,
	  gnumeric_roman, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "round",      "f|f", N_("number,digits"), help_round,
	  gnumeric_round, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rounddown",  "f|f", N_("number,digits"), help_rounddown,
	  gnumeric_rounddown, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "roundup",    "f|f", N_("number,digits"), help_roundup,
	  gnumeric_roundup, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "seriessum", NULL, N_("x,n,m,coefficients"), help_seriessum,
	  NULL, gnumeric_seriessum, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sign",    "f", N_("number"),    help_sign,
	  gnumeric_sign, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sin",     "f", N_("number"),    help_sin,
	  gnumeric_sin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "sinh",    "f", N_("number"),    help_sinh,
	  gnumeric_sinh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "sqrt",    "f", N_("number"),    help_sqrt,
	  gnumeric_sqrt, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sqrtpi",  "f", N_("number"),    help_sqrtpi,
	  gnumeric_sqrtpi, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "suma", NULL, N_("number,number,"), help_suma,
	  NULL, gnumeric_suma, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumif",   "rS|r", N_("range,criteria,actual_range"), help_sumif,
	  gnumeric_sumif, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumproduct", NULL, N_("range,range,"), help_sumproduct,
	  NULL, gnumeric_sumproduct, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumsq", NULL, N_("number"),      help_sumsq,
	  NULL, gnumeric_sumsq, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumx2my2", "AA", N_("array1,array2"), help_sumx2my2,
	  gnumeric_sumx2my2, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumx2py2", "AA", N_("array1,array2"), help_sumx2py2,
	  gnumeric_sumx2py2, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sumxmy2",  "AA", N_("array1,array2"), help_sumxmy2,
	  gnumeric_sumxmy2, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "tan",     "f", N_("number"),    help_tan,
	  gnumeric_tan, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "tanh",    "f", N_("number"),    help_tanh,
	  gnumeric_tanh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "trunc",   "f|f", N_("number,digits"), help_trunc,
	  gnumeric_trunc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "pi",      "", "",           help_pi,
	  gnumeric_pi, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "mmult",   "AA", N_("array1,array2"), help_mmult,
	  gnumeric_mmult, NULL, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "minverse","A", N_("array"),     help_minverse,
	  gnumeric_minverse, NULL, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "mdeterm", "A", N_("array,matrix_type,bandsize"), help_mdeterm,
	  gnumeric_mdeterm, NULL, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
#if 0
	{ "logmdeterm", "A|si", N_("array,matrix_type,bandsize"),
	  help_logmdeterm, gnumeric_logmdeterm, NULL, NULL, NULL },
#endif
        {NULL}
};
