/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-lookup.c:  Built in lookup functions and functions registration
 *
 * Authors:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *  JP Rosevear <jpr@arcavia.com>
 */

#include <config.h>
#include "func.h"
#include "parse-util.h"
#include "eval.h"
#include "cell.h"
#include "str.h"
#include "sheet.h"
#include "expr-name.h"

#include <string.h>
#include <stdlib.h>

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

	if (height) {
		length = value_area_get_height (ei->pos, data);
	} else {
		length = value_area_get_width (ei->pos, data);
	}

	for (lp = 0; lp < length; lp++){
		const Value *v;

		if (height) {
			v = value_area_fetch_x_y (ei->pos, data, 0, lp);
		} else {
			v = value_area_fetch_x_y (ei->pos, data, lp, 0);
		}

		g_return_val_if_fail (v != NULL, -1);

		if (!find_compare_type_valid (find, v)) {
			continue;
		}

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

	if (height) {
		high = value_area_get_height (ei->pos, data);
	} else {
		high = value_area_get_width (ei->pos, data);
	}
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

			if (height) {
				v = value_area_fetch_x_y (ei->pos,
							  data, 0, mid);
			} else {
				v = value_area_fetch_x_y (ei->pos,
							  data, mid, 0);
			}

			g_return_val_if_fail (v != NULL, -1);

			if (find_compare_type_valid (find, v)) {
				break;
			}

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

				if (height) {
					v = value_area_fetch_x_y (ei->pos,
								  data,
								  0, adj);
				} else {
					v = value_area_fetch_x_y (ei->pos,
								  data,
								  adj, 0);
				}

				g_return_val_if_fail (v != NULL, -1);

				if (!find_compare_type_valid (find, v)) {
					break;
				}

				comp = value_compare (find, v, FALSE);
				if (comp != IS_EQUAL) {
					break;
				}

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

static char *help_address = {
	N_("@FUNCTION=ADDRESS\n"
	   "@SYNTAX=ADDRESS(row_num,col_num[,abs_num,a1,text])\n"

	   "@DESCRIPTION="
	   "ADDRESS returns a cell address as text for specified row "
	   "and column numbers. "
	   "\n"
	   "If @abs_num is 1 or omitted, ADDRESS returns absolute reference. "
	   "If @abs_num is 2 ADDRESS returns absolute row and relative "
	   "column.  If @abs_num is 3 ADDRESS returns relative row and "
	   "absolute column. "
	   "If @abs_num is 4 ADDRESS returns relative reference. "
	   "If @abs_num is greater than 4 ADDRESS returns #NUM! error. "
	   "\n"
	   "@a1 is a logical value that specifies the reference style.  If "
	   "@a1 is TRUE or omitted, ADDRESS returns an A1-style reference, "
	   "i.e. $D$4.  Otherwise ADDRESS returns an R1C1-style reference, "
	   "i.e. R4C4. "
	   "\n"
	   "@text specifies the name of the worksheet to be used as the "
	   "external reference.  "
	   "\n"
	   "If @row_num or @col_num is less than one, ADDRESS returns #NUM! "
	   "error. "
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
	gchar *text, *buf;
	Value *v;

	row = value_get_as_int (args[0]);
	col = value_get_as_int (args[1]);

	if (row < 1 || col < 1)
	        return value_new_error (ei->pos, gnumeric_err_NUM);

	abs_num = args[2] ? value_get_as_int (args [2]) : 1;

	if (args[3] == NULL)
	        a1 = 1;
	else {
		gboolean err;
	        a1 = value_get_as_bool (args[3], &err);
		if (err)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	if (args[4] == NULL) {
	        text = g_new (gchar, 1);
	        text[0] = '\0';
	} else {
		const char *s = value_peek_string (args[4]);
		gboolean space = (strchr (s, ' ') != 0);

		text = g_strconcat (space ? "'" : "",
				    s,
				    space ? "'!" : "!",
				    NULL);
	}

	buf = g_new (gchar, strlen (text) + 50);

	switch (abs_num) {
	case 1:
	        if (a1)
		        sprintf (buf, "%s$%s$%d", text, col_name (col - 1), row);
		else
		        sprintf (buf, "%sR%dC%d", text, row, col);
		break;
	case 2:
	        if (a1)
		        sprintf (buf, "%s%s$%d", text, col_name (col - 1), row);
		else
		        sprintf (buf, "%sR%dC[%d]", text, row, col);
		break;
	case 3:
	        if (a1)
		        sprintf (buf, "%s$%s%d", text, col_name (col - 1), row);
		else
		        sprintf (buf, "%sR[%d]C%d", text, row, col);
		break;
	case 4:
	        if (a1)
		        sprintf (buf, "%s%s%d", text, col_name (col - 1), row);
		else
		        sprintf (buf, "%sR[%d]C[%d]", text, row, col);
		break;
	default:
	        g_free (text);
	        g_free (buf);
		return value_new_error (ei->pos, gnumeric_err_NUM);
	}
	v = value_new_string (buf);
	g_free (text);
	g_free (buf);

	return v;
}

/***************************************************************************/

static char *help_choose = {
	N_("@FUNCTION=CHOOSE\n"
	   "@SYNTAX=CHOOSE(index[,value1][,value2]...)\n"

	   "@DESCRIPTION="
	   "CHOOSE returns the value of index @index. "
	   "@index is rounded to an integer if it is not."
	   "\n"
	   "If @index < 1 or @index > number of values: returns #VAL!."
	   "\n"
	   "@EXAMPLES=\n"
	   "CHOOSE(3,\"Apple\",\"Orange\",\"Grape\",\"Perry\") equals \"Grape\".\n"
	   "\n"
	   "@SEEALSO=IF")
};

static Value *
gnumeric_choose (FunctionEvalInfo *ei, ExprList *l)
{
	int     index;
	int     argc;
	Value  *v;

	argc =  expr_list_length (l);

	if (argc < 1 || !l->data)
		return value_new_error (ei->pos, _("#ARG!"));

	v = expr_eval (l->data, ei->pos, EVAL_STRICT);
	if (!v)
		return NULL;

	if ((v->type != VALUE_INTEGER) && (v->type != VALUE_FLOAT)) {
		value_release (v);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	index = value_get_as_int(v);
	value_release (v);
	for (l = l->next; l != NULL ; l = l->next) {
		index--;
		if (!index)
			return expr_eval (l->data, ei->pos, EVAL_PERMIT_NON_SCALAR);
	}
	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_vlookup = {
	N_("@FUNCTION=VLOOKUP\n"
	   "@SYNTAX=VLOOKUP(value,range,column[,approximate])\n"

	   "@DESCRIPTION="
	   "VLOOKUP function finds the row in range that has a first "
	   "column similar to value.  If @approximate is not true it finds "
	   "the row with an exact equivilance.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the row with value less "
	   "than @value.  It returns the value in the row found at a 1 based "
	   "offset in @column columns into the @range."
	   "\n"
	   "Returns #NUM! if @column < 0. "
	   "Returns #REF! if @column falls outside @range."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=HLOOKUP")
};

static Value *
gnumeric_vlookup (FunctionEvalInfo *ei, Value **args)
{
	int col_idx, index = -1;
	gboolean approx;

	col_idx = value_get_as_int (args[2]);

	if (!find_type_valid (args[0]))
		return value_new_error (ei->pos, gnumeric_err_NA);
	if (col_idx <= 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	if (col_idx > value_area_get_width (ei->pos, args [1]))
		return value_new_error (ei->pos, gnumeric_err_REF);

	if (!args[3]) {
		approx = TRUE;
	} else {
		approx = value_get_as_checked_bool (args [3]);
	}

	if (approx) {
		index = find_index_bisection (ei, args[0], args[1], 1, TRUE);
	} else {
		index = find_index_linear (ei, args[0], args[1], 0, TRUE);
	}

	if (index >= 0) {
	        const Value *v;

		v = value_area_fetch_x_y (ei->pos, args [1], col_idx-1, index);
		g_return_val_if_fail (v != NULL, NULL);
		return value_duplicate (v);
	}

	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static char *help_hlookup = {
	N_("@FUNCTION=HLOOKUP\n"
	   "@SYNTAX=HLOOKUP(value,range,row[,approximate])\n"

	   "@DESCRIPTION="
	   "HLOOKUP function finds the col in range that has a first "
	   "row cell similar to value.  If @approximate is not true it finds "
	   "the col with an exact equivilance.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the col with value less "
	   "than @value it returns the value in the col found at a 1 based "
	   "offset in @row rows into the @range."
	   "\n"
	   "Returns #NUM! if @row < 0. "
	   "Returns #REF! if @row falls outside @range."
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
	if (row_idx > value_area_get_height (ei->pos, args [1]))
		return value_new_error (ei->pos, gnumeric_err_REF);

	if (!args[3]) {
		approx = TRUE;
	} else {
		approx = value_get_as_checked_bool (args [3]);
	}

	if (approx) {
		index = find_index_bisection (ei, args[0], args[1], 1, FALSE);
	} else {
		index = find_index_linear (ei, args[0], args[1], 0, FALSE);
	}

	if (index >= 0) {
	        const Value *v;

		v = value_area_fetch_x_y (ei->pos, args[1], index, row_idx-1);
		g_return_val_if_fail (v != NULL, NULL);
		return value_duplicate (v);
	}

	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static char *help_lookup = {
	N_("@FUNCTION=LOOKUP\n"
	   "@SYNTAX=LOOKUP(value,vector1,vector2)\n"

	   "@DESCRIPTION="
	   "LOOKUP function finds the row index of 'value' in @vector1 "
	   "and returns the contents of value2 at that row index. "
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. Alternatively a single array can be used."
	   "\n"
	   "If LOOKUP can't find @value it uses the next largest value less "
	   "than value. "
	   "The data must be sorted. "
	   "\n"
	   "If @value is smaller than the first value it returns #N/A"
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
	int width = value_area_get_width (ei->pos, args[1]);
	int height = value_area_get_height (ei->pos, args[1]);

	if (!find_type_valid (args[0])) {
		return value_new_error (ei->pos, gnumeric_err_NA);
	}

	if (result) {
		int width = value_area_get_width (ei->pos, result);
		int height = value_area_get_height (ei->pos, result);

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
		int width = value_area_get_width (ei->pos, result);
		int height = value_area_get_height (ei->pos, result);

		if (width > height) {
			v = value_area_fetch_x_y (ei->pos, result,
						  index, height - 1);
		} else {
			v = value_area_fetch_x_y (ei->pos, result,
						  width - 1, index);
		}
		return value_duplicate (v);
	}

	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static char *help_match = {
	N_("@FUNCTION=MATCH\n"
	   "@SYNTAX=MATCH(seek,vector[,type])\n"

	   "@DESCRIPTION="
	   "MATCH function finds the row index of @seek in @vector "
	   "and returns it. "
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. Alternatively a single array can be used."
	   "\n"
	   "The @type parameter, which defaults to +1, controls the search:\n"
	   "If @type = 1,  finds largest value <= @seek.\n"
	   "If @type = 0,  finds first value == @seek.\n"
	   "If @type = -1, finds smallest value >= @seek.\n"
	   "\n"
	   "For type 0, the data can be in any order.  For types -1 and +1, "
	   "the data must be sorted.  (And in this case, MATCH uses a binary "
	   "search to locate the index.)"
	   "\n"
	   "If @seek could not be found, #N/A is returned."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOOKUP")
};

static Value *
gnumeric_match (FunctionEvalInfo *ei, Value **args)
{
	int type, index = -1;
	int width = value_area_get_width (ei->pos, args[1]);
	int height = value_area_get_height (ei->pos, args[1]);

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

static char *help_indirect = {
	N_("@FUNCTION=INDIRECT\n"
	   "@SYNTAX=INDIRECT(ref_text,[format])\n"

	   "@DESCRIPTION="
	   "INDIRECT function returns the contents of the cell pointed to "
	   "by the ref_text string. The string specifices a single cell "
	   "reference the format of which is either A1 or R1C1 style. The "
	   "style is set by the format boolean, which defaults to the former."
	   "\n"
	   "If @ref_text is not a valid reference returns #REF! "
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 3.14 and A2 contains A1, then\n"
	   "INDIRECT(A2) equals 3.14.\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_indirect (FunctionEvalInfo *ei, Value **args)
{
#if 0
	/* What good is this ? the parser handles both forms */
	gboolean a1_style = args[1] ? value_get_as_bool (args[1], &error) : TRUE;
#endif

	ParsePos  pp;
	ExprTree *expr;
	char	 *text = value_get_as_string (args[0]);

	expr = expr_parse_str_simple (text,
		parse_pos_init_evalpos (&pp, ei->pos));
	g_free (text);

	if (expr != NULL) {
		if (expr->any.oper == OPER_NAME &&
		    !expr->name.name->builtin) {
			ExprTree *tmp = expr->name.name->t.expr_tree;
			expr_tree_ref (tmp);
			expr_tree_unref (expr);
			expr = tmp;
		}
		if (expr->any.oper == OPER_VAR) {
			Value *res = expr_eval (expr, ei->pos, EVAL_STRICT);
			expr_tree_unref (expr);
			return res;
		} else if (expr->any.oper == OPER_CONSTANT) {
			Value *res = value_duplicate (expr->constant.value);
			expr_tree_unref (expr);
			return res;
		}
		expr_tree_unref (expr);
	}
	return value_new_error (ei->pos, gnumeric_err_REF);
}

/*
 * FIXME: The concept of multiple range references needs core support.
 *        hence this whole implementation is a cop-out really.
 */
static char *help_index = {
	N_(
	"@FUNCTION=INDEX\n"
	"@SYNTAX=INDEX(array,[row, col, area])\n"
	"@DESCRIPTION="
	"INDEX gives a reference to a cell in the given @array."
	"The cell is pointed out by @row and @col, which count the rows and columns"
	"in the array.\n"
	"If @row and @col are ommited the are assumed to be 1."
	"@area has to be 1; references to multiple areas are not yet implemented."
	"If the reference falls outside the range of the @array, INDEX returns a"
	"#REF! error.\n"
	"\n"
	"@EXAMPLES="
	"Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, 17.3,"
	"21.3, 25.9, and 40.1. Then INDEX(A1:A5,4,1,1) equals 25,9\n"
	"@SEEALSO=")
};

static Value *
gnumeric_index (FunctionEvalInfo *ei, Value **args)
{
	Value *area = args[0];
	int    col_off = 0, row_off = 0;

	if (args[3] &&
	    value_get_as_int (args[3]) != 1) {
		g_warning ("Multiple range references unimplemented");
		return value_new_error (ei->pos, gnumeric_err_REF);
	}

	if (args[1])
		row_off = value_get_as_int (args[1]) - 1;

	if (args[2])
		col_off = value_get_as_int (args[2]) - 1;

	if (col_off < 0 ||
	    col_off >= value_area_get_width (ei->pos, area) ||
	    row_off < 0 ||
	    row_off >= value_area_get_height (ei->pos, area))
		return value_new_error (ei->pos, gnumeric_err_REF);

	return value_duplicate (value_area_fetch_x_y (ei->pos, area, col_off, row_off));
}

/***************************************************************************/

static char *help_column = {
	N_("@FUNCTION=COLUMN\n"
	   "@SYNTAX=COLUMN([reference])\n"

	   "@DESCRIPTION="
	   "COLUMN function returns an array of the column numbers "
	   "taking a default argument of the containing cell position."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "COLUMN() in E1 equals 5.\n"
	   "\n"
	   "@SEEALSO=COLUMNS,ROW,ROWS")
};

static Value *
gnumeric_column (FunctionEvalInfo *ei, ExprList *nodes)
{
	ExprTree *expr;

	if (!nodes || !nodes->data)
		return value_new_int (ei->pos->eval.col+1);

	expr = (ExprTree *)nodes->data;

	if (expr->any.oper == OPER_VAR)
		return value_new_int (cellref_get_abs_col (&expr->var.ref,
							   ei->pos) + 1);
	if (expr->any.oper == OPER_CONSTANT &&
	    expr->constant.value->type == VALUE_CELLRANGE) {
		int i, j, col;
		Value const * range = expr->constant.value;
		CellRef const * a = &range->v_range.cell.a;
		CellRef const * b = &range->v_range.cell.b;
		Value * res = value_new_array (abs (b->col - a->col) + 1,
					       abs (b->row - a->row) + 1);

		col = cellref_get_abs_col (a, ei->pos) + 1;
		for (i = abs (b->col - a->col) ; i >= 0 ; --i)
			for (j = abs (b->row - a->row) ; j >= 0 ; --j)
				value_array_set(res, i, j,
						value_new_int(col+i));

		return res;
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_columns = {
	N_("@FUNCTION=COLUMNS\n"
	   "@SYNTAX=COLUMNS(reference)\n"

	   "@DESCRIPTION="
	   "COLUMNS function returns the number of columns in area or "
	   "array reference."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "COLUMNS(H2:J3) equals 3.\n"
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be even slightly meaningful */
static Value *
gnumeric_columns (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_width (ei->pos, args [0]));
}

/***************************************************************************/

static char *help_offset = {
	N_("@FUNCTION=OFFSET\n"
	   "@SYNTAX=OFFSET(range,row,col,height,width)\n"

	   "@DESCRIPTION="
	   "OFFSET function returns a cell range. "
	   "The cell range starts at offset (@col,@row) from @range, "
	   "and is of height @height and width @width."
	   "\n"
	   "If range is neither a reference nor a range returns #VALUE!.  "
	   "If either height or width is omitted the height or width "
	   "of the reference is used."
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
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
	a.row += row_offset; b.row += row_offset;
	a.col += col_offset; b.col += col_offset;

	width = (args[3] != NULL)
	    ? value_get_as_int (args[3])
	    : value_area_get_width (ei->pos, args [0]);
	height = (args[4] != NULL)
	    ? value_get_as_int (args[4])
	    : value_area_get_height (ei->pos, args [0]);

	if (width < 1 || height < 1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	else if (a.row < 0 || a.col < 0)
		return value_new_error (ei->pos, gnumeric_err_REF);
	else if (a.row >= SHEET_MAX_ROWS || a.col >= SHEET_MAX_COLS)
		return value_new_error (ei->pos, gnumeric_err_REF);

	/* Special case of a single cell */
	if (width == 1 && height == 1) {
		/* FIXME FIXME : do we need to check for recalc here ?? */
		Cell const * c =
		    sheet_cell_fetch (eval_sheet (a.sheet, ei->pos->sheet),
				      a.col, a.row);
		return value_duplicate (c->value);
	}

	b.row += width-1;
	b.col += height-1;
	return value_new_cellrange (&a, &b, ei->pos->eval.col, ei->pos->eval.row);
}

/***************************************************************************/

static char *help_row = {
	N_("@FUNCTION=ROW\n"
	   "@SYNTAX=ROW([reference])\n"

	   "@DESCRIPTION="
	   "ROW function returns an array of the row numbers taking "
	   "a default argument of the containing cell position."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "ROW() in G13 equals 13.\n"
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
};

static Value *
gnumeric_row (FunctionEvalInfo *ei, ExprList *nodes)
{
	ExprTree *expr;

	if (!nodes || !nodes->data)
		return value_new_int (ei->pos->eval.row+1);

	expr = (ExprTree *)nodes->data;

	if (expr->any.oper == OPER_VAR)
		return value_new_int (cellref_get_abs_row (&expr->var.ref,
							    ei->pos) + 1);
	if (expr->any.oper == OPER_CONSTANT &&
	    expr->constant.value->type == VALUE_CELLRANGE) {
		int i, j, row;
		Value const * range = expr->constant.value;
		CellRef const * a = &range->v_range.cell.a;
		CellRef const * b = &range->v_range.cell.b;
		Value * res = value_new_array (abs (b->col - a->col) + 1,
					       abs (b->row - a->row) + 1);

		row = cellref_get_abs_row (a, ei->pos) + 1;
		for (i = abs (b->col - a->col) ; i >= 0 ; --i)
			for (j = abs (b->row - a->row) ; j >= 0 ; --j)
				value_array_set(res, i, j,
						value_new_int(row+j));

		return res;
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_rows = {
	N_("@FUNCTION=ROWS\n"
	   "@SYNTAX=ROWS(reference)\n"

	   "@DESCRIPTION="
	   "ROWS function returns the number of rows in area or array "
	   "reference."
	   "\n"
	   "If @reference is neither an array nor a reference nor a range "
	   "returns #VALUE!."
	   "\n"
	   "@EXAMPLES=\n"
	   "ROWS(H7:I13) equals 7.\n"
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_rows (FunctionEvalInfo *ei, Value **args)
{
	return value_new_int (value_area_get_height (ei->pos, args [0]));
}

/***************************************************************************/

static char *help_hyperlink = {
	N_("@FUNCTION=HYPERLINK\n"
	   "@SYNTAX=HYPERLINK(link_location, optional_label)\n"

	   "@DESCRIPTION="
	   "HYPERLINK function currently returns its 2nd argument, "
	   "or if that is omitted the 1st argument."
	   "\n"
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

static char *help_transpose = {
	N_("@FUNCTION=TRANSPOSE\n"
	   "@SYNTAX=TRANSPOSE(matrix)\n"

	   "@DESCRIPTION="
	   "TRANSPOSE function returns the transpose of the input "
	   "@matrix."
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

	int const cols = value_area_get_width (ep, matrix);
	int const rows = value_area_get_height (ep, matrix);

	/* Return the value directly for a singleton */
	if (rows == 1 && cols == 1)
		return value_duplicate(value_area_get_x_y (ep, matrix, 0, 0));

	/* REMEMBER this is a transpose */
	res = value_new_array_non_init (rows, cols);

	for (r = 0; r < rows; ++r){
		res->v_array.vals [r] = g_new (Value *, cols);
		for (c = 0; c < cols; ++c)
			res->v_array.vals[r][c] =
			    value_duplicate(value_area_get_x_y (ep, matrix,
								c, r));
	}

	return res;
}

/***************************************************************************/

void lookup_functions_init (void);
void
lookup_functions_init (void)
{
	FunctionCategory *cat = function_get_category_with_translation ("Data / Lookup", _("Data / Lookup"));

	function_add_args  (cat, "address",   "ff|ffs",
			    "row_num,col_num,abs_num,a1,text",
			    &help_address,  gnumeric_address);
        function_add_nodes (cat, "choose",     0,     "index,value...",
			    &help_choose,   gnumeric_choose);
	function_add_nodes (cat, "column",    NULL,    "ref",
			    &help_column,   gnumeric_column);
	function_add_args  (cat, "columns",   "A",    "ref",
			    &help_columns, gnumeric_columns);
	function_add_args  (cat, "hlookup",
			    "?Af|b","val,range,col_idx,approx",
			    &help_hlookup, gnumeric_hlookup);
	function_add_args  (cat, "hyperlink",
			    "s|s","link_location, optional_label",
			    &help_hyperlink, gnumeric_hyperlink);
	function_add_args  (cat, "indirect",  "s|b","ref_string,format",
			    &help_indirect, gnumeric_indirect);
	function_add_args  (cat, "index",     "A|fff","reference,row,col,area",
			    &help_index,    gnumeric_index);
	function_add_args  (cat, "lookup",    "?A|r", "val,range,range",
			    &help_lookup,   gnumeric_lookup);
	function_add_args  (cat, "match",     "?A|f", "val,range,approx",
			    &help_match,    gnumeric_match);
	function_add_args  (cat, "offset",    "rff|ff","ref,row,col,hight,width",
			    &help_offset,   gnumeric_offset);
	function_add_nodes (cat, "row",       NULL,    "ref",
			    &help_row,      gnumeric_row);
	function_add_args  (cat, "rows",      "A",    "ref",
			    &help_rows,    gnumeric_rows);
	function_add_args  (cat, "transpose","A",
			    "array",
			    &help_transpose,   gnumeric_transpose);
	function_add_args  (cat, "vlookup",
			    "?Af|b","val,range,col_idx,approx",
			    &help_vlookup, gnumeric_vlookup);
}
