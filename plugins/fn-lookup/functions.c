/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Range lookup functions
 *
 * Authors:
 *   Michael Meeks <michael@ximian.com>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   JP Rosevear <jpr@arcavia.com>
 *   Morten Welinder (terra@diku.dk)
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
#include <dependent.h>
#include <cell.h>
#include <str.h>
#include <sheet.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <parse-util.h>

#include <string.h>
#include <stdlib.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;


/* Useful routines for multiple functions */
static gboolean
find_type_valid (const Value *find)
{
	/* Excel does not lookup errors or blanks */
	if (VALUE_IS_NUMBER (find) || find->type == VALUE_STRING) {
		return TRUE;
	}

	return FALSE;
}

static gboolean
find_compare_type_valid (const Value *find, const Value *val)
{
	if (!val) {
		return FALSE;
	}

	if ((VALUE_IS_NUMBER (find) && VALUE_IS_NUMBER (val)) ||
	    (find->type == val->type)) {
		return TRUE;
	}

	return FALSE;
}

/**
 * find_bound_walk:
 * @l: lower bound
 * @h: upper bound
 * @start: starting point
 * @up: is first step incrementing
 * @reset: reset static values
 *
 * This function takes and upper and lower integer bound
 * and then walks that range starting with the given
 * starting point.  The walk is done by incrementing or
 * decrementing the starting point (based on the up value)
 * until the upper or lower bound is reached.  At this
 * point the step is reversed and the values move to the
 * opposite boundary (not repeating any values of course)
 *
 * Return value: the next value in the range
 **/
static int
find_bound_walk (int l, int h, int start, gboolean up, gboolean reset)
{
	static int low, high, current, orig;
	static gboolean sup, started;

	g_return_val_if_fail (l >= 0, -1);
	g_return_val_if_fail (h >= 0, -1);
	g_return_val_if_fail (h >= l, -1);
	g_return_val_if_fail (start >= l, -1);
	g_return_val_if_fail (start <= h, -1);

	if (reset) {
		low = l;
		high = h;
		current = start;
		orig = start;
		sup = up;
		started = up;
		return current;
	}

	if (sup) {
		current++;
		if (current > high && sup == started) {
			current = orig - 1;
			sup = FALSE;
		} else if (current > high && sup != started) {
			return -1;
		}
	} else {
		current--;
		if (current < low && sup == started) {
			current = orig + 1;
			sup = TRUE;
		} else if (current < low && sup != started) {
			return -1;
		}
	}
	return current;
}

static int
find_index_linear (FunctionEvalInfo *ei, Value *find, Value *data,
		   gint type, gboolean height)
{
	const Value *index_val = NULL;
	ValueCompare comp;
	int length, lp, index = -1;

	if (height)
		length = value_area_get_height (data, ei->pos);
	else
		length = value_area_get_width (data, ei->pos);

	for (lp = 0; lp < length; lp++){
		const Value *v;

		if (height)
			v = value_area_fetch_x_y (data, 0, lp, ei->pos);
		else
			v = value_area_fetch_x_y (data, lp, 0, ei->pos);

		g_return_val_if_fail (v != NULL, -1);

		if (!find_compare_type_valid (find, v))
			continue;

		comp = value_compare (find, v, FALSE);

		if (type >= 1 && comp == IS_GREATER) {
			ValueCompare comp = TYPE_MISMATCH;

			if (index >= 0) {
				comp = value_compare (v, index_val, FALSE);
			}

			if (index < 0 ||
			    (index >= 0 && comp == IS_GREATER)) {
				index = lp;
				index_val = v;
			}
		} else if (type <= -1 && comp == IS_LESS) {
			ValueCompare comp = TYPE_MISMATCH;

			if (index >= 0) {
				comp = value_compare (v, index_val, FALSE);
			}

			if (index < 0 ||
			    (index >= 0 && comp == IS_LESS)) {
				index = lp;
				index_val = v;
			}
		} else if (comp == IS_EQUAL) {
			return lp;
		}
	}

	return index;
}

static int
find_index_bisection (FunctionEvalInfo *ei, Value *find, Value *data,
		      gint type, gboolean height)
{
	ValueCompare comp = TYPE_MISMATCH;
	int high, low = 0, prev = -1, mid = -1;

	if (height)
		high = value_area_get_height (data, ei->pos);
	else
		high = value_area_get_width (data, ei->pos);
	high--;

	if (high < low) {
		return -1;
	}

	while (low <= high) {
		const Value *v = NULL;
		int start;

		if ((type >= 1) != (comp == IS_LESS)) {
			prev = mid;
		}

		mid = ((low + high) / 2);
		mid = find_bound_walk (low, high, mid,
				       type >= 0 ? TRUE : FALSE, TRUE);

		start = mid;

		/*
		 * Excel handles type mismatches by skipping first one
		 * way then the other (if necessary) to find a valid
		 * value.  The initial direction depends on the search
		 * type.
		 */
		while (!find_compare_type_valid (find, v) && mid != -1) {
			gboolean rev = FALSE;

			if (height)
				v = value_area_get_x_y (data, 0, mid, ei->pos);
			else
				v = value_area_get_x_y (data, mid, 0, ei->pos);

			if (find_compare_type_valid (find, v))
				break;

			mid = find_bound_walk (0, 0, 0, FALSE, FALSE);

			if (!rev && type >= 0 && mid < start) {
				high = mid;
				rev = TRUE;
			} else if (!rev && type < 0 && mid > start) {
				low = mid;
				rev = TRUE;
			}
		}

		/*
		 * If we couldn't find another entry in the range
		 * with an appropriate type, return the best previous
		 * value
		 */
		if (mid == -1 && ((type >= 1) != (comp == IS_LESS))) {
			return prev;
		} else if (mid == -1) {
			return -1;
		}

		comp = value_compare (find, v, FALSE);

		if (type >= 1 && comp == IS_GREATER) {
			low = mid + 1;
		} else if (type >= 1 && comp == IS_LESS) {
			high = mid - 1;
		} else if (type <= -1 && comp == IS_GREATER) {
			high = mid - 1;
		} else if (type <= -1 && comp == IS_LESS) {
			low = mid + 1;
		} else if (comp == IS_EQUAL) {
			/* This is due to excel, it does a
			 * linear search after the bisection search
			 * to find either the first or last value
			 * that is equal.
			 */
			while ((type <= -1 && mid > low) ||
			       (type >= 0 && mid < high)) {
				int adj = 0;

				if (type >= 0) {
					adj = mid + 1;
				} else {
					adj = mid - 1;
				}

				if (height)
					v = value_area_fetch_x_y (data, 0, adj, ei->pos);
				else                                                    
					v = value_area_fetch_x_y (data, adj, 0, ei->pos);

				g_return_val_if_fail (v != NULL, -1);

				if (!find_compare_type_valid (find, v))
					break;

				comp = value_compare (find, v, FALSE);
				if (comp != IS_EQUAL)
					break;

				mid = adj;
			}
			return mid;
		}
	}

	/* Try and return a reasonable value */
	if ((type >= 1) != (comp == IS_LESS)) {
		return mid;
	}

	return prev;
}

/***************************************************************************/

static const char *help_address = {
	N_("@FUNCTION=ADDRESS\n"
	   "@SYNTAX=ADDRESS(row_num,col_num[,abs_num,a1,text])\n"

	   "@DESCRIPTION="
	   "ADDRESS returns a cell address as text for specified row "
	   "and column numbers.\n"
	   "\n"
	   "@a1 is a logical value that specifies the reference style.  If "
	   "@a1 is TRUE or omitted, ADDRESS returns an A1-style reference, "
	   "i.e. $D$4.  Otherwise ADDRESS returns an R1C1-style reference, "
	   "i.e. R4C4.\n"
	   "\n"
	   "@text specifies the name of the worksheet to be used as the "
	   "external reference.\n"
	   "\n"
	   "* If @abs_num is 1 or omitted, ADDRESS returns absolute "
	   "reference.\n"
	   "* If @abs_num is 2 ADDRESS returns absolute row and relative "
	   "column.\n"
	   "* If @abs_num is 3 ADDRESS returns relative row and "
	   "absolute column.\n"
	   "* If @abs_num is 4 ADDRESS returns relative reference.\n"
	   "* If @abs_num is greater than 4 ADDRESS returns #VALUE! error.\n"
	   "* If @row_num or @col_num is less than one, ADDRESS returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ADDRESS(5,4) equals \"$D$5\".\n"
	   "ADDRESS(5,4,4) equals \"D5\".\n"
	   "ADDRESS(5,4,3,FALSE) equals \"R[5]C4\".\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_address (FunctionEvalInfo *ei, Value **args)
{
        int   row, col, abs_num, a1;
	gchar *sheet_name, *buf;
	char const *sheet_quote;

	row = value_get_as_int (args[0]);
	col = value_get_as_int (args[1]);

	if (row < 1 || SHEET_MAX_ROWS <= row ||
	    col < 1 || SHEET_MAX_COLS <= col)
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

	abs_num = args[2] ? value_get_as_int (args[2]) : 1;

	if (args[3] == NULL)
	        a1 = 1;
	else {
		gboolean err;
	        a1 = value_get_as_bool (args[3], &err);
		if (err)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	sheet_name = (args[4] != NULL)
		? sheet_name_quote (value_peek_string (args[4]))
		: g_strdup ("");
	sheet_quote = *sheet_name ? "!" : "";

	buf = g_new (gchar, strlen (sheet_name) + 1 + 50);
	switch (abs_num) {
	case 1: case 5:
	        if (a1)
		        sprintf (buf, "%s%s$%s$%d", sheet_name, sheet_quote, col_name (col - 1),
				 row);
		else
		        sprintf (buf, "%s%sR%dC%d", sheet_name, sheet_quote, row, col);
		break;
	case 2: case 6:
	        if (a1)
		        sprintf (buf, "%s%s%s$%d", sheet_name, sheet_quote, col_name (col - 1), row);
		else
		        sprintf (buf, "%s%sR%dC[%d]", sheet_name, sheet_quote, row, col);
		break;
	case 3: case 7:
	        if (a1)
		        sprintf (buf, "%s%s$%s%d", sheet_name, sheet_quote, col_name (col - 1), row);
		else
		        sprintf (buf, "%s%sR[%d]C%d", sheet_name, sheet_quote, row, col);
		break;
	case 4: case 8:
	        if (a1)
		        sprintf (buf, "%s%s%s%d", sheet_name, sheet_quote, col_name (col - 1), row);
		else
		        sprintf (buf, "%s%sR[%d]C[%d]", sheet_name, sheet_quote, row, col);
		break;
	default:
	        g_free (sheet_name);
	        g_free (buf);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}
	g_free (sheet_name);

	return value_new_string_nocopy (buf);
}

/***************************************************************************/

static char const *help_areas = {
	N_("@FUNCTION=AREAS\n"
	   "@SYNTAX=AREAS(references)\n"

	   "@DESCRIPTION="
	   "AREAS returns the number of areas in @reference. "
	   "\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "AREAS((A1,B2,C3)) equals "
	   "\3.\n"
	   "\n"
	   "@SEEALSO=ADDRESS,INDEX,INDIRECT,OFFSET")
};

/* TODO : we need to rethink EXPR_SET as an operator vs a value type */
static Value *
gnumeric_areas (FunctionEvalInfo *ei, GnmExprList *l)
{
	GnmExpr const *expr;
	int res = -1;
	int argc =  gnm_expr_list_length (l);

	if (argc < 1 || l->data == NULL || argc > 1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	expr = l->data;

restart :
	switch (expr->any.oper) {
	case GNM_EXPR_OP_CONSTANT:
		if (expr->constant.value->type != VALUE_CELLRANGE)
			break;

	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		res = 1;
		break;
	case GNM_EXPR_OP_ANY_BINARY:
	case GNM_EXPR_OP_ANY_UNARY:
	case GNM_EXPR_OP_ARRAY:
		break;

	case GNM_EXPR_OP_FUNCALL: {
		Value *v = gnm_expr_eval (expr, ei->pos,
			GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
		if (expr->constant.value->type == VALUE_CELLRANGE)
			res = 1;
		value_release (v);
		break;
	}

	case GNM_EXPR_OP_NAME:
		if (expr->name.name->active) {
			expr = expr->name.name->expr_tree;
			goto restart;
		}
		break;

	case GNM_EXPR_OP_SET:
		res = gnm_expr_list_length (expr->set.set);
		break;

	default:
		g_warning ("unknown expr type.");
	};

	if (res > 0)
		return value_new_int (res);
	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_choose = {
	N_("@FUNCTION=CHOOSE\n"
	   "@SYNTAX=CHOOSE(index[,value1][,value2]...)\n"

	   "@DESCRIPTION="
	   "CHOOSE returns the value of index @index. "
	   "@index is rounded to an integer if it is not.\n"
	   "\n"
	   "* If @index < 1 or @index > number of values, CHOOSE "
	   "returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CHOOSE(3,\"Apple\",\"Orange\",\"Grape\",\"Perry\") equals "
	   "\"Grape\".\n"
	   "\n"
	   "@SEEALSO=IF")
};

static Value *
gnumeric_choose (FunctionEvalInfo *ei, GnmExprList *l)
{
	int     index;
	int     argc;
	Value  *v;

	argc =  gnm_expr_list_length (l);

	if (argc < 1 || !l->data)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	v = gnm_expr_eval (l->data, ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (!v)
		return NULL;

	if ((v->type != VALUE_INTEGER) && (v->type != VALUE_FLOAT)) {
		value_release (v);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	index = value_get_as_int (v);
	value_release (v);
	for (l = l->next; l != NULL ; l = l->next) {
		index--;
		if (!index)
			return gnm_expr_eval (l->data, ei->pos,
					      GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
	}
	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_vlookup = {
	N_("@FUNCTION=VLOOKUP\n"
	   "@SYNTAX=VLOOKUP(value,range,column[,approximate,as_index])\n"

	   "@DESCRIPTION="
	   "VLOOKUP function finds the row in range that has a first "
	   "column similar to value.  If @approximate is not true it finds "
	   "the row with an exact equivilance.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the row with value less "
	   "than @value.  It returns the value in the row found at a 1 based "
	   "offset in @column columns into the @range.  @as_index returns the "
	   "0 based offset that matched rather than the value.\n"
	   "\n"
	   "* VLOOKUP returns #NUM! if @column < 0.\n"
	   "* VLOOKUP returns #REF! if @column falls outside @range.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=HLOOKUP")
};

static Value *
gnumeric_vlookup (FunctionEvalInfo *ei, Value **args)
{
	int      col_idx, index = -1;
	gboolean approx;

	col_idx = value_get_as_int (args[2]);

	if (!find_type_valid (args[0]))
		return value_new_error (ei->pos, gnumeric_err_NA);
	if (col_idx <= 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	if (col_idx > value_area_get_width (args [1], ei->pos))
		return value_new_error (ei->pos, gnumeric_err_REF);

	approx = (args[3] != NULL)
		? value_get_as_checked_bool (args [3]) : TRUE;
	index = approx
		? find_index_bisection (ei, args[0], args[1], 1, TRUE)
		: find_index_linear (ei, args[0], args[1], 0, TRUE);
	if (args[4] != NULL && value_get_as_checked_bool (args [4]))
		return value_new_int (index);

	if (index >= 0) {
	        const Value *v;

		v = value_area_fetch_x_y (args [1], col_idx-1, index, ei->pos);
		g_return_val_if_fail (v != NULL, NULL);
		return value_duplicate (v);
	}

	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static const char *help_hlookup = {
	N_("@FUNCTION=HLOOKUP\n"
	   "@SYNTAX=HLOOKUP(value,range,row[,approximate,as_index])\n"

	   "@DESCRIPTION="
	   "HLOOKUP function finds the col in range that has a first "
	   "row cell similar to value.  If @approximate is not true it finds "
	   "the col with an exact equivilance.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the col with value less "
	   "than @value it returns the value in the col found at a 1 based "
	   "offset in @row rows into the @range.  @as_index returns the offset "
	   "that matched rather than the value.\n"
	   "\n"
	   "* HLOOKUP returns #NUM! if @row < 0.\n"
	   "* HLOOKUP returns #REF! if @row falls outside @range.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=VLOOKUP")
};

static Value *
gnumeric_hlookup (FunctionEvalInfo *ei, Value **args)
{
	int row_idx, index = -1;
	gboolean approx;

	row_idx = value_get_as_int (args[2]);

	if (!find_type_valid (args[0]))
		return value_new_error (ei->pos, gnumeric_err_NA);
	if (row_idx <= 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	if (row_idx > value_area_get_height (args [1], ei->pos))
		return value_new_error (ei->pos, gnumeric_err_REF);

	approx = (args[3] != NULL)
		? value_get_as_checked_bool (args [3]) : TRUE;
	index = approx
		? find_index_bisection (ei, args[0], args[1], 1, FALSE)
		: find_index_linear (ei, args[0], args[1], 0, FALSE);
	if (args[4] != NULL && value_get_as_checked_bool (args [4]))
		return value_new_int (index);

	if (index >= 0) {
	        const Value *v;

		v = value_area_fetch_x_y (args[1], index, row_idx-1, ei->pos);
		g_return_val_if_fail (v != NULL, NULL);
		return value_duplicate (v);
	}

	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static const char *help_lookup = {
	N_("@FUNCTION=LOOKUP\n"
	   "@SYNTAX=LOOKUP(value,vector1,vector2)\n"

	   "@DESCRIPTION="
	   "LOOKUP function finds the row index of 'value' in @vector1 "
	   "and returns the contents of value2 at that row index. "
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. Alternatively a single array can be used.\n"
	   "\n"
	   "* If LOOKUP can't find @value it uses the next largest value less "
	   "than value.\n"
	   "* The data must be sorted.\n"
	   "* If @value is smaller than the first value it returns #N/A.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=VLOOKUP,HLOOKUP")
};

static Value *
gnumeric_lookup (FunctionEvalInfo *ei, Value **args)
{
	int index = -1;
	Value *result = args[2];
	int width = value_area_get_width (args[1], ei->pos);
	int height = value_area_get_height (args[1], ei->pos);

	if (!find_type_valid (args[0]))
		return value_new_error (ei->pos, gnumeric_err_NA);

	if (result) {
		int width = value_area_get_width (result, ei->pos);
		int height = value_area_get_height (result, ei->pos);

		if (width > 1 && height > 1) {
			return value_new_error (ei->pos, gnumeric_err_NA);
		}
	} else {
		result = args[1];
	}

	index = find_index_bisection (ei, args[0], args[1], 1,
				      width > height ? FALSE : TRUE);

	if (index >= 0) {
	        const Value *v = NULL;
		int width = value_area_get_width (result, ei->pos);
		int height = value_area_get_height (result, ei->pos);

		if (width > height)
			v = value_area_fetch_x_y (result, index, height - 1, ei->pos);
		else
			v = value_area_fetch_x_y (result, width - 1, index, ei->pos);
		return value_duplicate (v);
	}

	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static const char *help_match = {
	N_("@FUNCTION=MATCH\n"
	   "@SYNTAX=MATCH(seek,vector[,type])\n"

	   "@DESCRIPTION="
	   "MATCH function finds the row index of @seek in @vector "
	   "and returns it.\n"
	   "\n"
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. Alternatively a single array can be used.\n"
	   "\n"
	   "* The @type parameter, which defaults to +1, controls the search:\n"
	   "* If @type = 1, MATCH finds largest value <= @seek.\n"
	   "* If @type = 0, MATCH finds first value == @seek.\n"
	   "* If @type = -1, MATCH finds smallest value >= @seek.\n"
	   "* For type 0, the data can be in any order.  For types -1 and +1, "
	   "the data must be sorted.  (And in this case, MATCH uses a binary "
	   "search to locate the index.)\n"
	   "* If @seek could not be found, #N/A is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOOKUP")
};

static Value *
gnumeric_match (FunctionEvalInfo *ei, Value **args)
{
	int type, index = -1;
	int width = value_area_get_width (args[1], ei->pos);
	int height = value_area_get_height (args[1], ei->pos);

	if (!find_type_valid (args[0])) {
		return value_new_error (ei->pos, gnumeric_err_NA);
	}

	if (width > 1 && height > 1) {
		return value_new_error (ei->pos, gnumeric_err_NA);
	}

	type = value_get_as_int (args[2]);

	if (type == 0) {
		index = find_index_linear (ei, args[0], args[1], type,
					   width > 1 ? FALSE : TRUE);
	} else {
		index = find_index_bisection (ei, args[0], args[1], type,
					      width > 1 ? FALSE : TRUE);
	}

	if (index >= 0) {
	        return value_new_int (index+1);
	}

	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static const char *help_indirect = {
	N_("@FUNCTION=INDIRECT\n"
	   "@SYNTAX=INDIRECT(ref_text,[format])\n"

	   "@DESCRIPTION="
	   "INDIRECT function returns the contents of the cell pointed to "
	   "by the ref_text string. The string specifices a single cell "
	   "reference the format of which is either A1 or R1C1 style. The "
	   "style is set by the format boolean, which defaults to the former.\n"
	   "\n"
	   "* If @ref_text is not a valid reference returns #REF! "
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 3.14 and A2 contains A1, then\n"
	   "INDIRECT(A2) equals 3.14.\n"
	   "\n"
	   "@SEEALSO=AREAS,INDEX,")
};

static Value *
gnumeric_indirect (FunctionEvalInfo *ei, Value **args)
{
#if 0
	/* What good is this ? the parser handles both forms */
	gboolean a1_style = args[1] ? value_get_as_bool (args[1], &error) : TRUE;
#endif
	ParsePos  pp;
	char const *text = value_peek_string (args[0]);
	GnmExpr const *expr = gnm_expr_parse_str_simple (text,
		parse_pos_init_evalpos (&pp, ei->pos));
	Value *res = NULL;

	if (expr != NULL) {
		res = gnm_expr_get_range (expr);
		gnm_expr_unref (expr);
	}
	return (res != NULL) ? res : value_new_error (ei->pos, gnumeric_err_REF);
}

/*****************************************************************************/

static char const *help_index = {
	N_(
	"@FUNCTION=INDEX\n"
	"@SYNTAX=INDEX(array,[row, col, area])\n"
	"@DESCRIPTION="
	"INDEX gives a reference to a cell in the given @array."
	"The cell is pointed out by @row and @col, which count the rows and "
	"columns in the array.\n"
	"\n"
	"* If @row and @col are ommited the are assumed to be 1.\n"
	"* If the reference falls outside the range of the @array, INDEX "
	"returns a #REF! error.\n"
	"\n"
	"@EXAMPLES="
	"Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, "
	"17.3, 21.3, 25.9, and 40.1. Then INDEX(A1:A5,4,1,1) equals 25.9\n"
	"\n"
	"@SEEALSO=")
};

static Value *
gnumeric_index (FunctionEvalInfo *ei, GnmExprList *l)
{
	GnmExpr const *source;
	int elem[3] = { 0, 0, 0 };
	unsigned i = 0;
	gboolean valid;
	Value *v, *res;

	if (l == NULL)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	source = l->data;
	l = l->next;

	for (i = 0; l != NULL && i < G_N_ELEMENTS (elem) ; i++, l = l->next) {
		v = value_coerce_to_number (
			gnm_expr_eval (l->data, ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY),
			&valid, ei->pos);
		if (!valid)
			return v;
		elem[i] = value_get_as_int (v) - 1;
		value_release (v);
	}

	if (source->any.oper == GNM_EXPR_OP_SET) {
		source = gnm_expr_list_nth (source->set.set, elem[2]);
		if (elem[2] < 0 || source == NULL)
			return value_new_error (ei->pos, gnumeric_err_REF);
	} else if (elem[2] != 0)
		return value_new_error (ei->pos, gnumeric_err_REF);

	v = gnm_expr_eval (source, ei->pos, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);

	if (elem[1] < 0 ||
	    elem[1] >= value_area_get_width (v, ei->pos) ||
	    elem[0] < 0 ||
	    elem[0] >= value_area_get_height (v, ei->pos)) {
		value_release (v);
		return value_new_error (ei->pos, gnumeric_err_REF);
	}

	res = value_duplicate (value_area_fetch_x_y (v, elem[1], elem[0], ei->pos));
	value_release (v);
	return res;
}

/***************************************************************************/

static const char *help_column = {
	N_("@FUNCTION=COLUMN\n"
	   "@SYNTAX=COLUMN([reference])\n"

	   "@DESCRIPTION="
	   "COLUMN function returns an array of the column numbers "
	   "taking a default argument of the containing cell position.\n"
	   "\n"
	   "* If @reference is neither an array nor a reference nor a range, "
	   "COLUMN returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COLUMN() in E1 equals 5.\n"
	   "\n"
	   "@SEEALSO=COLUMNS,ROW,ROWS")
};

static Value *
gnumeric_column (FunctionEvalInfo *ei, Value **args)
{
	Value *ref = args[0];

	if (!ref)
		return value_new_int (ei->pos->eval.col + 1);

	switch (ref->type) {
	case VALUE_CELLRANGE: {
		int width = value_area_get_width (ref, ei->pos);
		int height = value_area_get_height (ref, ei->pos);
		CellRef const *const refa = &ref->v_range.cell.a;
		int col = cellref_get_abs_col (refa, ei->pos) + 1;
		int i, j;
		Value *res;

		if (width == 1 && height == 1)
			return value_new_int (col);

		res = value_new_array (width, height);
		for (i = width - 1; i >= 0 ; --i)
			for (j = height - 1 ; j >= 0 ; --j)
				value_array_set (res, i, j,
						 value_new_int (col + i));
		return res;
	}

	default: /* Nothing */ ;
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_columns = {
	N_("@FUNCTION=COLUMNS\n"
	   "@SYNTAX=COLUMNS(reference)\n"

	   "@DESCRIPTION="
	   "COLUMNS function returns the number of columns in area or "
	   "array reference.\n"
	   "\n"
	   "* If @reference is neither an array nor a reference nor a range, "
	   "COLUMNS returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COLUMNS(H2:J3) equals 3.\n"
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

static Value *
gnumeric_columns (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_width (args [0], ei->pos));
}

/***************************************************************************/

static const char *help_offset = {
	N_("@FUNCTION=OFFSET\n"
	   "@SYNTAX=OFFSET(range,row,col,height,width)\n"

	   "@DESCRIPTION="
	   "OFFSET function returns a cell range. "
	   "The cell range starts at offset (@col,@row) from @range, "
	   "and is of height @height and width @width.\n"
	   "\n"
	   "* If range is neither a reference nor a range returns #VALUE!.\n"
	   "* If either height or width is omitted the height or width "
	   "of the reference is used.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS,INDEX,INDIRECT,ADDRESS")
};

static Value *
gnumeric_offset (FunctionEvalInfo *ei, Value **args)
{
	int width, height;
	int row_offset, col_offset;

	/* Copy the references so we can change them */
	CellRef a = args[0]->v_range.cell.a;
	CellRef b = args[0]->v_range.cell.b;

	row_offset = value_get_as_int (args[1]);
	col_offset = value_get_as_int (args[2]);
	a.row     += row_offset; b.row += row_offset;
	a.col     += col_offset; b.col += col_offset;

	width = (args[3] != NULL)
	    ? value_get_as_int (args[3])
	    : value_area_get_width (args [0], ei->pos);
	height = (args[4] != NULL)
	    ? value_get_as_int (args[4])
	    : value_area_get_height (args [0], ei->pos);

	if (width < 1 || height < 1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	else if (a.row < 0 || a.col < 0)
		return value_new_error (ei->pos, gnumeric_err_REF);
	else if (a.row >= SHEET_MAX_ROWS || a.col >= SHEET_MAX_COLS)
		return value_new_error (ei->pos, gnumeric_err_REF);

	b.row += width-1;
	b.col += height-1;
	if (b.row >= SHEET_MAX_ROWS || b.col >= SHEET_MAX_COLS)
		return value_new_error (ei->pos, gnumeric_err_REF);

	return value_new_cellrange (&a, &b, ei->pos->eval.col, ei->pos->eval.row);
}

/***************************************************************************/

static const char *help_row = {
	N_("@FUNCTION=ROW\n"
	   "@SYNTAX=ROW([reference])\n"

	   "@DESCRIPTION="
	   "ROW function returns an array of the row numbers taking "
	   "a default argument of the containing cell position.\n"
	   "\n"
	   "* If @reference is neither an array nor a reference nor a range, "
	   "ROW returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ROW() in G13 equals 13.\n"
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
};

static Value *
gnumeric_row (FunctionEvalInfo *ei, Value **args)
{
	Value *ref = args[0];

	if (!ref)
		return value_new_int (ei->pos->eval.row + 1);

	switch (ref->type) {
	case VALUE_CELLRANGE: {
		int width  = value_area_get_width (ref, ei->pos);
		int height = value_area_get_height (ref, ei->pos);
		CellRef const *const refa = &ref->v_range.cell.a;
		int row    = cellref_get_abs_row (refa, ei->pos) + 1;
		int i, j;
		Value *res;

		if (width == 1 && height == 1)
			return value_new_int (row);

		res = value_new_array (width, height);
		for (i = width - 1; i >= 0 ; --i)
			for (j = height - 1 ; j >= 0 ; --j)
				value_array_set (res, i, j,
						 value_new_int (row + j));
		return res;
	}

	default: /* Nothing */ ;
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_rows = {
	N_("@FUNCTION=ROWS\n"
	   "@SYNTAX=ROWS(reference)\n"

	   "@DESCRIPTION="
	   "ROWS function returns the number of rows in area or array "
	   "reference.\n"
	   "\n"
	   "* If @reference is neither an array nor a reference nor a range, "
	   "ROWS returns #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ROWS(H7:I13) equals 7.\n"
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

static Value *
gnumeric_rows (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_height (args [0], ei->pos));
}

/***************************************************************************/

static const char *help_hyperlink = {
	N_("@FUNCTION=HYPERLINK\n"
	   "@SYNTAX=HYPERLINK(link_location, optional_label)\n"

	   "@DESCRIPTION="
	   "HYPERLINK function currently returns its 2nd argument, "
	   "or if that is omitted the 1st argument.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "HYPERLINK(\"www.gnome.org\",\"GNOME\").\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_hyperlink (FunctionEvalInfo *ei, Value **args)
{
	Value const * v = args[1];
	if (v == NULL)
		v = args[0];
	return value_duplicate (v);
}

/***************************************************************************/

static const char *help_transpose = {
	N_("@FUNCTION=TRANSPOSE\n"
	   "@SYNTAX=TRANSPOSE(matrix)\n"

	   "@DESCRIPTION="
	   "TRANSPOSE function returns the transpose of the input "
	   "@matrix.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=MMULT")
};


static Value *
gnumeric_transpose (FunctionEvalInfo *ei, Value **argv)
{
	EvalPos const * const ep = ei->pos;
        Value const * const matrix = argv[0];
	int	r, c;
        Value *res;

	int const cols = value_area_get_width (matrix, ep);
	int const rows = value_area_get_height (matrix, ep);

	/* Return the value directly for a singleton */
	if (rows == 1 && cols == 1)
		return value_duplicate (value_area_get_x_y (matrix, 0, 0, ep));

	/* REMEMBER this is a transpose */
	res = value_new_array_non_init (rows, cols);

	for (r = 0; r < rows; ++r){
		res->v_array.vals [r] = g_new (Value *, cols);
		for (c = 0; c < cols; ++c)
			res->v_array.vals[r][c] = value_duplicate(
				value_area_get_x_y (matrix, c, r, ep));
	}

	return res;
}

/***************************************************************************/

const GnmFuncDescriptor lookup_functions[] = {
	{ "address",   "ff|ffs", N_("row_num,col_num,abs_num,a1,text"),
	  &help_address,  gnumeric_address, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "areas",	0,	N_("reference"),
	  &help_areas,	NULL,	gnumeric_areas, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "choose",     0,	N_("index,value,"),
	  &help_choose,	NULL,	gnumeric_choose, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "column",     "|A",    N_("ref"),
	  &help_column,   gnumeric_column, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "columns",   "A",    N_("ref"),
	  &help_columns, gnumeric_columns, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hlookup",   "SAf|bb", N_("val,range,col_idx,approx,as_index"),
	  &help_hlookup, gnumeric_hlookup, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hyperlink", "s|s", N_("link_location, label"),
	  &help_hyperlink, gnumeric_hyperlink, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_BASIC },
	{ "indirect",  "s|b",N_("ref_string,format"),
	  &help_indirect, gnumeric_indirect, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "index",     "A|fff",N_("reference,row,col,area"),
	  &help_index,    NULL, gnumeric_index, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "lookup",    "SA|r", N_("val,range,range"),
	  &help_lookup,   gnumeric_lookup, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "match",     "SA|f", N_("val,range,approx"),
	  &help_match,    gnumeric_match, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "offset",    "rff|ff",N_("ref,row,col,height,width"),
	  &help_offset,   gnumeric_offset, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "row",       "|A",   N_("ref"),
	  &help_row,      gnumeric_row, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rows",      "A",    N_("ref"),
	  &help_rows,     gnumeric_rows, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "transpose", "A",    N_("array"),
	  &help_transpose, gnumeric_transpose, NULL, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "vlookup",   "SAf|bb", N_("val,range,col_idx,approx,as_index"),
	  &help_vlookup, gnumeric_vlookup, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};
