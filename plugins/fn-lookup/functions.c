/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Range lookup functions
 *
 * Authors:
 *   Michael Meeks <michael@ximian.com>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   JP Rosevear <jpr@arcavia.com>
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

#include <parse-util.h>
#include <dependent.h>
#include <cell.h>
#include <str.h>
#include <sheet.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <expr-impl.h>
#include <application.h>
#include <expr-name.h>
#include <mathfunc.h>
#include <parse-util.h>
#include <gnm-i18n.h>

#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

#include <string.h>
#include <stdlib.h>

GNM_PLUGIN_MODULE_HEADER;

static GHashTable *lookup_string_cache;
static GHashTable *lookup_float_cache;

static void
clear_caches (void)
{
	if (lookup_string_cache) {
		g_hash_table_destroy (lookup_string_cache);
		lookup_string_cache = NULL;
	}
	if (lookup_float_cache) {
		g_hash_table_destroy (lookup_float_cache);
		lookup_float_cache = NULL;
	}
}

static GHashTable *
get_cache (GnmFuncEvalInfo *ei, GnmValue const *data, gboolean stringp)
{
	GnmSheetRange sr;
	GHashTable *h, *cache;
	Sheet *end_sheet;
	GnmRangeRef const *rr;

	if (data->type != VALUE_CELLRANGE)
		return NULL;
	rr = value_get_rangeref (data);

        gnm_rangeref_normalize (rr, ei->pos, &sr.sheet, &end_sheet, &sr.range);
	if (sr.sheet != end_sheet)
		return NULL;

	if (!lookup_string_cache) {
		lookup_string_cache = g_hash_table_new_full
			((GHashFunc)gnm_sheet_range_hash,
			 (GEqualFunc)gnm_sheet_range_equal,
			 (GDestroyNotify)gnm_sheet_range_free,
			 (GDestroyNotify)g_hash_table_destroy);
		lookup_float_cache = g_hash_table_new_full
			((GHashFunc)gnm_sheet_range_hash,
			 (GEqualFunc)gnm_sheet_range_equal,
			 (GDestroyNotify)gnm_sheet_range_free,
			 (GDestroyNotify)g_hash_table_destroy);
	}

	cache = stringp ? lookup_string_cache : lookup_float_cache;

	h = g_hash_table_lookup (cache, &sr);
	if (!h) {
		if (stringp)
			h = g_hash_table_new_full (g_str_hash,
						   g_str_equal,
						   g_free, NULL);
		else
			h = g_hash_table_new_full ((GHashFunc)gnm_float_hash,
						   (GEqualFunc)gnm_float_equal,
						   g_free, NULL);
		g_hash_table_insert (cache, gnm_sheet_range_dup (&sr), h);
	}

	return h;
}

/* -------------------------------------------------------------------------- */

static gboolean
find_type_valid (GnmValue const *find)
{
	/* Excel does not lookup errors or blanks */
	if (VALUE_IS_EMPTY (find))
		return FALSE;
	return VALUE_IS_NUMBER (find) || VALUE_IS_STRING (find);
}

static gboolean
find_compare_type_valid (GnmValue const *find, GnmValue const *val)
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

 again:
	if (sup) {
		current++;
		if (current > high && sup == started) {
			current = orig;
			sup = FALSE;
			goto again;
		} else if (current > high && sup != started) {
			return -1;
		}
	} else {
		current--;
		if (current < low && sup == started) {
			current = orig;
			sup = TRUE;
			goto again;
		} else if (current < low && sup != started) {
			return -1;
		}
	}
	return current;
}

static int
calc_length (GnmValue const *data, GnmEvalPos const *ep, gboolean vertical)
{
	if (vertical)
		return value_area_get_height (data, ep);
	else
		return value_area_get_width (data, ep);
}

static const GnmValue *
get_elem (GnmValue const *data, guint ui,
	  GnmEvalPos const *ep, gboolean vertical)
{
	if (vertical)
		return value_area_fetch_x_y (data, 0, ui, ep);
	else
		return value_area_fetch_x_y (data, ui, 0, ep);
}

static int
find_index_linear_equal_string (GnmFuncEvalInfo *ei,
				const char *s, GnmValue const *data,
				gboolean vertical)
{
	GHashTable *h;
	gpointer pres;
	char *sc;
	gboolean found;

	h = get_cache (ei, data, TRUE);
	if (!h)
		return -2;

	/* We need to do this early before calls to value_peek_string gets
	   called too often.  */
	sc = g_utf8_casefold (s, -1);

	if (g_hash_table_size (h) == 0) {
		int lp, length = calc_length (data, ei->pos, vertical);

		for (lp = 0; lp < length; lp++) {
			GnmValue const *v = get_elem (data, lp, ei->pos, vertical);
			char *vc;

			if (!v || !VALUE_IS_STRING (v))
				continue;

			vc = g_utf8_casefold (value_peek_string (v), -1);
			if (g_hash_table_lookup_extended (h, vc, NULL, NULL))
				g_free (vc);
			else
				g_hash_table_insert (h, vc, GINT_TO_POINTER (lp));
		}
	}

	found = g_hash_table_lookup_extended (h, sc, NULL, &pres);
	g_free (sc);

	return found ? GPOINTER_TO_INT (pres) : -1;
}

static int
find_index_linear_equal_float (GnmFuncEvalInfo *ei,
			       gnm_float f, GnmValue const *data,
			       gboolean vertical)
{
	GHashTable *h;
	gpointer pres;
	gboolean found;

	h = get_cache (ei, data, FALSE);
	if (!h)
		return -2;

	if (g_hash_table_size (h) == 0) {
		int lp, length = calc_length (data, ei->pos, vertical);

		for (lp = 0; lp < length; lp++) {
			GnmValue const *v = get_elem (data, lp, ei->pos, vertical);
			gnm_float f2;

			if (!v || !VALUE_IS_NUMBER (v))
				continue;

			f2 = value_get_as_float (v);

			if (!g_hash_table_lookup_extended (h, &f2, NULL, NULL))
				g_hash_table_insert
					(h,
					 g_memdup (&f2, sizeof (f2)),
					 GINT_TO_POINTER (lp));
		}
	}

	found = g_hash_table_lookup_extended (h, &f, NULL, &pres);

	return found ? GPOINTER_TO_INT (pres) : -1;
}

static int
find_index_linear (GnmFuncEvalInfo *ei,
		   GnmValue const *find, GnmValue const *data,
		   gint type, gboolean vertical)
{
	GnmValue const *index_val = NULL;
	GnmValDiff comp;
	int length, lp, index = -1;

	if (VALUE_IS_STRING (find) && type == 0) {
		const char *s = value_peek_string (find);
		int i = find_index_linear_equal_string (ei, s, data, vertical);
		if (i != -2)
			return i;
	}

	if (VALUE_IS_NUMBER (find) && type == 0) {
		gnm_float f = value_get_as_float (find);
		int i = find_index_linear_equal_float (ei, f, data, vertical);
		if (i != -2)
			return i;
	}

	length = calc_length (data, ei->pos, vertical);

	for (lp = 0; lp < length; lp++) {
		GnmValue const *v = get_elem (data, lp, ei->pos, vertical);

		g_return_val_if_fail (v != NULL, -1);

		if (!find_compare_type_valid (find, v))
			continue;

		comp = value_compare (find, v, FALSE);

		if (type >= 1 && comp == IS_GREATER) {
			GnmValDiff comp = TYPE_MISMATCH;

			if (index >= 0) {
				comp = value_compare (v, index_val, FALSE);
			}

			if (index < 0 ||
			    (index >= 0 && comp == IS_GREATER)) {
				index = lp;
				index_val = v;
			}
		} else if (type <= -1 && comp == IS_LESS) {
			GnmValDiff comp = TYPE_MISMATCH;

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
find_index_bisection (GnmFuncEvalInfo *ei,
		      GnmValue const *find, GnmValue const *data,
		      gint type, gboolean vertical)
{
	GnmValDiff comp = TYPE_MISMATCH;
	int high, low = 0, prev = -1, mid = -1;

	high = calc_length (data, ei->pos, vertical) - 1;

	if (high < low) {
		return -1;
	}

	while (low <= high) {
		GnmValue const *v = NULL;
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

			v = get_elem (data, mid, ei->pos, vertical);

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

				v = get_elem (data, adj, ei->pos, vertical);

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

static GnmFuncHelp const help_address[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ADDRESS\n"
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
	   "@SEEALSO=COLUMNNUMBER")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_address (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmConventionsOut out;
	GnmCellRef	 ref;
	GnmParsePos	 pp;
	gboolean	 err;
	int		 col, row;

	switch (args[2] ? value_get_as_int (args[2]) : 1) {
	case 1: case 5: ref.col_relative = ref.row_relative = FALSE; break;
	case 2: case 6:
		ref.col_relative = TRUE;
		ref.row_relative = FALSE;
		break;
	case 3: case 7:
		ref.col_relative = FALSE;
		ref.row_relative = TRUE;
		break;
	case 4: case 8: ref.col_relative = ref.row_relative = TRUE; break;

	default :
		return value_new_error_VALUE (ei->pos);
	}

	ref.sheet = NULL;
	row = ref.row = value_get_as_int (args[0]) - 1;
	col = ref.col = value_get_as_int (args[1]) - 1;
	out.pp = parse_pos_init_evalpos (&pp, ei->pos);
	out.convs = gnm_conventions_default;

	if (NULL != args[3]) {
		/* MS Excel is ridiculous.  This is a special case */
		if (!value_get_as_bool (args[3], &err)) {
			out.convs = gnm_conventions_xls_r1c1;
			if (ref.col_relative)
				col = ei->pos->eval.col + (++ref.col);
			if (ref.row_relative)
				row = ei->pos->eval.row + (++ref.row);
		}
		if (err)
		        return value_new_error_VALUE (ei->pos);
	}
	if (col < 0 || col >= gnm_sheet_get_max_cols (ei->pos->sheet))
		return value_new_error_VALUE (ei->pos);
	if (row < 0 || row >= gnm_sheet_get_max_rows (ei->pos->sheet))
		return value_new_error_VALUE (ei->pos);

	if (!out.convs->r1c1_addresses)
		pp.eval.col = pp.eval.row = 0;

	if (NULL != args[4]) {
		out.accum = gnm_expr_conv_quote (gnm_conventions_default,
			value_peek_string (args[4]));
		g_string_append_c (out.accum, '!');
	} else
		out.accum = g_string_new (NULL);
	cellref_as_string (&out, &ref, TRUE);

	return value_new_string_nocopy (g_string_free (out.accum, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_areas[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=AREAS\n"
	   "@SYNTAX=AREAS(reference)\n"

	   "@DESCRIPTION="
	   "AREAS returns the number of areas in @reference. "
	   "\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "AREAS((A1,B2,C3)) equals "
	   "3.\n"
	   "\n"
	   "@SEEALSO=ADDRESS,INDEX,INDIRECT,OFFSET")
	},
	{ GNM_FUNC_HELP_END }
};

/* TODO : we need to rethink EXPR_SET as an operator vs a value type */
static GnmValue *
gnumeric_areas (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmExpr const *expr;
	int res = -1;

	if (argc != 1 || argv[0] == NULL)
		return value_new_error_VALUE (ei->pos);
	expr = argv[0];

 restart:
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_CONSTANT:
		if (VALUE_IS_ERROR (expr->constant.value))
			return value_dup (expr->constant.value);
		if (expr->constant.value->type != VALUE_CELLRANGE)
			break;

	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		res = 1;
		break;
	case GNM_EXPR_OP_ANY_BINARY:
	case GNM_EXPR_OP_ANY_UNARY:
	case GNM_EXPR_OP_ARRAY_CORNER:
	case GNM_EXPR_OP_ARRAY_ELEM:
		break;

	case GNM_EXPR_OP_FUNCALL: {
		GnmValue *v = gnm_expr_eval (expr, ei->pos,
			GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
		if (v->type == VALUE_CELLRANGE)
			res = 1;
		value_release (v);
		break;
	}

	case GNM_EXPR_OP_NAME:
		if (expr->name.name->active) {
			expr = expr->name.name->texpr->expr;
			goto restart;
		}
		break;

	case GNM_EXPR_OP_SET:
		res = expr->set.argc;
		break;

	default:
		g_warning ("unknown expr type.");
	}

	if (res > 0)
		return value_new_int (res);
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_choose[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CHOOSE\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_choose (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int     index;
	GnmValue  *v;
	int i;

	if (argc < 1)
		return value_new_error_VALUE (ei->pos);

#warning TODO add array eval
	v = gnm_expr_eval (argv[0], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (!v)
		return NULL;

	if (!VALUE_IS_FLOAT (v)) {
		value_release (v);
		return value_new_error_VALUE (ei->pos);
	}

	index = value_get_as_int (v);
	value_release (v);
	for (i = 1; i < argc; i++) {
		index--;
		if (!index)
			return gnm_expr_eval (argv[i], ei->pos,
					      GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
	}
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_vlookup[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=VLOOKUP\n"
	   "@SYNTAX=VLOOKUP(value,range,column[,approximate,as_index])\n"

	   "@DESCRIPTION="
	   "VLOOKUP function finds the row in range that has a first "
	   "column similar to @value.  If @approximate is not true it finds "
	   "the row with an exact equivalence.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the row with value less "
	   "than @value.  It returns the value in the row found at a 1-based "
	   "offset in @column columns into the @range.  @as_index returns the "
	   "0-based offset that matched rather than the value.\n"
	   "\n"
	   "* VLOOKUP returns #NUM! if @column < 0.\n"
	   "* VLOOKUP returns #REF! if @column falls outside @range.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=HLOOKUP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_vlookup (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int      col_idx, index = -1;
	gboolean approx;

	col_idx = value_get_as_int (args[2]);

	if (!find_type_valid (args[0]))
		return value_new_error_NA (ei->pos);
	if (col_idx <= 0)
		return value_new_error_VALUE (ei->pos);
	if (col_idx > value_area_get_width (args [1], ei->pos))
		return value_new_error_REF (ei->pos);

	approx = (args[3] != NULL)
		? value_get_as_checked_bool (args [3]) : TRUE;
	index = approx
		? find_index_bisection (ei, args[0], args[1], 1, TRUE)
		: find_index_linear (ei, args[0], args[1], 0, TRUE);
	if (args[4] != NULL && value_get_as_checked_bool (args [4]))
		return value_new_int (index);

	if (index >= 0) {
	        GnmValue const *v;

		v = value_area_fetch_x_y (args [1], col_idx-1, index, ei->pos);
		g_return_val_if_fail (v != NULL, NULL);
		return value_dup (v);
	}

	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_hlookup[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=HLOOKUP\n"
	   "@SYNTAX=HLOOKUP(value,range,row[,approximate,as_index])\n"

	   "@DESCRIPTION="
	   "HLOOKUP function finds the col in range that has a first "
	   "row cell similar to @value.  If @approximate is not true it finds "
	   "the col with an exact equivalence.  If @approximate is true, "
	   "then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the col with value less "
	   "than @value it returns the value in the col found at a 1-based "
	   "offset in @row rows into the @range.  @as_index returns the "
	   "0-based offset "
	   "that matched rather than the value.\n"
	   "\n"
	   "* HLOOKUP returns #NUM! if @row < 0.\n"
	   "* HLOOKUP returns #REF! if @row falls outside @range.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=VLOOKUP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hlookup (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int row_idx, index = -1;
	gboolean approx;

	row_idx = value_get_as_int (args[2]);

	if (!find_type_valid (args[0]))
		return value_new_error_NA (ei->pos);
	if (row_idx <= 0)
		return value_new_error_VALUE (ei->pos);
	if (row_idx > value_area_get_height (args [1], ei->pos))
		return value_new_error_REF (ei->pos);

	approx = (args[3] != NULL)
		? value_get_as_checked_bool (args [3]) : TRUE;
	index = approx
		? find_index_bisection (ei, args[0], args[1], 1, FALSE)
		: find_index_linear (ei, args[0], args[1], 0, FALSE);
	if (args[4] != NULL && value_get_as_checked_bool (args [4]))
		return value_new_int (index);

	if (index >= 0) {
	        GnmValue const *v;

		v = value_area_fetch_x_y (args[1], index, row_idx-1, ei->pos);
		g_return_val_if_fail (v != NULL, NULL);
		return value_dup (v);
	}

	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_lookup[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LOOKUP\n"
	   "@SYNTAX=LOOKUP(value,vector1[,vector2])\n"

	   "@DESCRIPTION="
	   "LOOKUP function finds the row index of @value in @vector1 "
	   "and returns the contents of @vector2 at that row index. "
	   "Alternatively a single array can be used for @vector1. "
	   "If the area is longer than it is wide then the sense of the "
	   "search is rotated. \n"
	   "\n"
	   "* If LOOKUP can't find @value it uses the largest value less "
	   "than @value.\n"
	   "* The data must be sorted.\n"
	   "* If @value is smaller than the first value it returns #N/A.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=VLOOKUP,HLOOKUP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_lookup (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int index = -1;
	GnmValue const *result = args[2];
	int width = value_area_get_width (args[1], ei->pos);
	int height = value_area_get_height (args[1], ei->pos);

	if (!find_type_valid (args[0]))
		return value_new_error_NA (ei->pos);

	if (result) {
		int width = value_area_get_width (result, ei->pos);
		int height = value_area_get_height (result, ei->pos);

		if (width > 1 && height > 1) {
			return value_new_error_NA (ei->pos);
		}
	} else {
		result = args[1];
	}

	index = find_index_bisection (ei, args[0], args[1], 1,
				      width > height ? FALSE : TRUE);

	if (index >= 0) {
	        GnmValue const *v = NULL;
		int width = value_area_get_width (result, ei->pos);
		int height = value_area_get_height (result, ei->pos);

		if (width > height)
			v = value_area_fetch_x_y (result, index, height - 1, ei->pos);
		else
			v = value_area_fetch_x_y (result, width - 1, index, ei->pos);
		return value_dup (v);
	}

	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_match[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MATCH\n"
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
	   "* For @type = 0, the data can be in any order.  "
	   "* For @type = -1 and @type = +1, "
	   "the data must be sorted.  (And in these cases, MATCH uses "
	   "a binary search to locate the index.)\n"
	   "* If @seek could not be found, #N/A is returned.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=LOOKUP")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_match (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int type, index = -1;
	int width = value_area_get_width (args[1], ei->pos);
	int height = value_area_get_height (args[1], ei->pos);

	if (!find_type_valid (args[0]))
		return value_new_error_NA (ei->pos);

	if (width > 1 && height > 1)
		return value_new_error_NA (ei->pos);

	type = VALUE_IS_EMPTY (args[2]) ? 1 : value_get_as_int (args[2]);

	if (type == 0)
		index = find_index_linear (ei, args[0], args[1], type,
					   width > 1 ? FALSE : TRUE);
	else
		index = find_index_bisection (ei, args[0], args[1], type,
					      width > 1 ? FALSE : TRUE);

	if (index >= 0)
	        return value_new_int (index+1);
	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_indirect[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=INDIRECT\n"
	   "@SYNTAX=INDIRECT(ref_text[,format])\n"

	   "@DESCRIPTION="
	   "INDIRECT function returns the contents of the cell pointed to "
	   "by the @ref_text string. The string specifies a single cell "
	   "reference the format of which is either A1 or R1C1 style. "
	   "The boolean @format controls how @ref_text is to be interpreted: "
	   "TRUE (the default) for A1 style and FALSE for R1C1 style.\n"
	   "\n"
	   "* If @ref_text is not a valid reference in the style controlled "
	   "by @format, returns #REF! "
	   "\n"
	   "@EXAMPLES=\n"
	   "If A1 contains 3.14 and A2 contains \"A1\", then\n"
	   "INDIRECT(A2) equals 3.14.\n"
	   "\n"
	   "If B1 contains 23 and A1 contains \"R1C2\", then\n"
	   "INDIRECT(A1,FALSE) equals 23.\n"
	   "@SEEALSO=AREAS,INDEX,CELL")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_indirect (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmParsePos  pp;
	GnmValue *res = NULL;
	GnmExprTop const *texpr;
	char const *text = value_peek_string (args[0]);
	GnmConventions const *convs = gnm_conventions_default;

	if (args[1] && !value_get_as_checked_bool (args[1]))
		convs = gnm_conventions_xls_r1c1;

	texpr = gnm_expr_parse_str (text,
		parse_pos_init_evalpos (&pp, ei->pos),
		GNM_EXPR_PARSE_DEFAULT, convs, NULL);

	if (texpr != NULL) {
		res = gnm_expr_top_get_range (texpr);
		gnm_expr_top_unref (texpr);
	}
	return (res != NULL) ? res : value_new_error_REF (ei->pos);
}

/*****************************************************************************/

static GnmFuncHelp const help_index[] = {
	{ GNM_FUNC_HELP_OLD,
	F_(
	"@FUNCTION=INDEX\n"
	"@SYNTAX=INDEX(array[,row, col, area])\n"
	"@DESCRIPTION="
	"INDEX gives a reference to a cell in the given @array."
	"The cell is pointed out by @row and @col, which count the rows and "
	"columns in the array.\n"
	"\n"
	"* If @row and @col are omitted the are assumed to be 1.\n"
	"* If the reference falls outside the range of the @array, INDEX "
	"returns a #REF! error.\n"
	"\n"
	"@EXAMPLES="
	"Let us assume that the cells A1, A2, ..., A5 contain numbers 11.4, "
	"17.3, 21.3, 25.9, and 40.1. Then INDEX(A1:A5,4,1,1) equals 25.9\n"
	"\n"
	"@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_index (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmExpr const *source;
	int elem[3] = { 0, 0, 0 };
	int i = 0;
	gboolean valid;
	GnmValue *v, *res;

	if (argc == 0)
		return value_new_error_VALUE (ei->pos);
	source = argv[0];

	for (i = 0; i + 1 < argc && i < (int)G_N_ELEMENTS (elem); i++) {
		v = value_coerce_to_number (
			gnm_expr_eval (argv[i + 1], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY),
			&valid, ei->pos);
		if (!valid)
			return v;
		elem[i] = value_get_as_int (v) - 1;
		value_release (v);
	}

	if (GNM_EXPR_GET_OPER (source) == GNM_EXPR_OP_SET) {
		source = (elem[2] >= 0 && elem[2] < source->set.argc)
			? source->set.argv[elem[2]]
			: NULL;
		if (source == NULL)
			return value_new_error_REF (ei->pos);
	} else if (elem[2] != 0)
		return value_new_error_REF (ei->pos);

	v = gnm_expr_eval (source, ei->pos, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);

	if (elem[1] < 0 ||
	    elem[1] >= value_area_get_width (v, ei->pos) ||
	    elem[0] < 0 ||
	    elem[0] >= value_area_get_height (v, ei->pos)) {
		value_release (v);
		return value_new_error_REF (ei->pos);
	}

#warning Work out a way to fall back to returning value when a reference is unneeded
	if (VALUE_CELLRANGE == v->type) {
		GnmRangeRef const *src = &v->v_range.cell;
		GnmCellRef a = src->a, b = src->b;
		Sheet *start_sheet, *end_sheet;
		GnmRange r;

		gnm_rangeref_normalize (src, ei->pos, &start_sheet, &end_sheet, &r);
		r.start.row += elem[0];
		r.start.col += elem[1];
		a.row = r.start.row; if (a.row_relative) a.row -= ei->pos->eval.row;
		b.row = r.start.row; if (b.row_relative) b.row -= ei->pos->eval.row;
		a.col = r.start.col; if (a.col_relative) a.col -= ei->pos->eval.col;
		b.col = r.start.col; if (b.col_relative) b.col -= ei->pos->eval.col;
		res = value_new_cellrange_unsafe (&a, &b);
	} else if (VALUE_ARRAY == v->type)
		res = value_dup (value_area_fetch_x_y (v, elem[1], elem[0], ei->pos));
	else
		res = value_new_error_REF (ei->pos);
	value_release (v);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_column[] = {
	{ GNM_FUNC_HELP_NAME, F_("COLUMN:vector of column numbers.") },
	{ GNM_FUNC_HELP_ARG, F_("[reference].") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
	   "COLUMN function returns a Nx1 array containing the series of integers "
	   "from the first column to the last column of @reference."
	   "\n"
	   "* @reference defaults to the position of the current expression.\n"
	   "\n"
	   "* If @reference is neither an array nor a reference nor a range, "
	   "returns #VALUE! error.\n") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("COLUMN(A1:C4) equals {1,2,3}") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("COLUMN(A:C) equals {1,2,3}") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("COLUMN() in G13 equals 7.") },
	{ GNM_FUNC_HELP_SEEALSO, "COLUMNS,ROW,ROWS" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_column (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int col, width, i;
	GnmValue *res;
	GnmValue const *ref = args[0];

	if (ref == NULL) {
		col   = ei->pos->eval.col + 1; /* user visible counts from 0 */
		if (ei->pos->array != NULL)
			width = ei->pos->array->cols;
		else
			return value_new_int (col);
	} else if (ref->type == VALUE_CELLRANGE) {
		Sheet    *tmp;
		GnmRange  r;

		gnm_rangeref_normalize (&ref->v_range.cell, ei->pos, &tmp, &tmp, &r);
		col    = r.start.col + 1;
		width  = range_width (&r);
	} else
		return value_new_error_VALUE (ei->pos);

	if (width == 1)
		return value_new_int (col);

	res = value_new_array (width, 1);
	for (i = width; i-- > 0 ; )
		value_array_set (res, i, 0, value_new_int (col + i));
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_columnnumber[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COLUMNNUMBER\n"
	   "@SYNTAX=COLUMNNUMBER(name)\n"

	   "@DESCRIPTION="
	   "COLUMNNUMBER function returns an integer corresponding to the column "
	   "name supplied as a string.\n"
	   "\n"
	   "* If @name is invalid, COLUMNNUMBER returns the #VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COLUMNNUMBER(\"E\") equals 5.\n"
	   "\n"
	   "@SEEALSO=ADDRESS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_columnnumber (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	char const *name = value_peek_string (args[0]);
	int colno;
	unsigned char relative;
	char const *after = col_parse (name, &colno, &relative);

	if (after == NULL || *after)
		return value_new_error_VALUE (ei->pos);

	return value_new_int (colno + 1);
}

/***************************************************************************/

static GnmFuncHelp const help_columns[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COLUMNS\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_columns (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_int (value_area_get_width (args [0], ei->pos));
}

/***************************************************************************/

static GnmFuncHelp const help_offset[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=OFFSET\n"
	   "@SYNTAX=OFFSET(range,row,col[,height[,width]])\n"

	   "@DESCRIPTION="
	   "OFFSET function returns a cell range. "
	   "The cell range starts at offset (@row,@col) from @range, "
	   "and is of height @height and width @width.\n"
	   "\n"
	   "* If @range is neither a reference nor a range, OFFSET "
	   "returns #VALUE!.\n"
	   "* If either @height or @width is omitted, the height or width "
	   "of the reference is used.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS,INDEX,INDIRECT,ADDRESS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_offset (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int tmp;
	int row_offset, col_offset;

	/* Copy the references so we can change them */
	GnmCellRef a = args[0]->v_range.cell.a;
	GnmCellRef b = args[0]->v_range.cell.b;

	row_offset = value_get_as_int (args[1]);
	col_offset = value_get_as_int (args[2]);
	a.row     += row_offset;
	a.col     += col_offset;
	if (a.row < 0 || a.col < 0 ||
	    a.row >= gnm_sheet_get_max_rows (ei->pos->sheet) || a.col >= gnm_sheet_get_max_cols (ei->pos->sheet))
		return value_new_error_REF (ei->pos);

	if (args[3] != NULL) {
		tmp = value_get_as_int (args[3]);
		if (tmp < 1)
			return value_new_error_VALUE (ei->pos);
		b.row = a.row + tmp - 1;
	} else
		b.row += row_offset;
	if (b.col < 0 || b.row >= gnm_sheet_get_max_rows (ei->pos->sheet))
		return value_new_error_REF (ei->pos);
	if (args[4] != NULL) {
		tmp = value_get_as_int (args[4]);
		if (tmp < 1)
			return value_new_error_VALUE (ei->pos);
		b.col = a.col + tmp - 1;
	} else
		b.col += col_offset;
	if (b.col < 0 || b.col >= gnm_sheet_get_max_cols (ei->pos->sheet))
		return value_new_error_REF (ei->pos);

	return value_new_cellrange_unsafe (&a, &b);
}

/***************************************************************************/

static GnmFuncHelp const help_row[] = {
	{ GNM_FUNC_HELP_NAME, F_("ROW:vector of row numbers.") },
	{ GNM_FUNC_HELP_ARG, F_("[reference].") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_(
	   "ROW function returns a 1xN array containing the series of integers "
	   "from the first row to the last row of @reference."
	   "\n"
	   "* @reference defaults to the position of the current expression.\n"
	   "\n"
	   "* If @reference is neither an array nor a reference nor a range, "
	   "returns #VALUE! error.\n") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("ROW(A1:D3) equals {1;2;3}") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("ROW(1:3) equals {1;2;3}") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("ROW() in G13 equals 13.") },
	{ GNM_FUNC_HELP_SEEALSO, "COLUMN,COLUMNS,ROWS" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_row (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int row, n, i;
	GnmValue *res;
	GnmValue const *ref = args[0];

	if (ref == NULL) {
		row   = ei->pos->eval.row + 1; /* user visible counts from 0 */
		if (ei->pos->array != NULL)
			n = ei->pos->array->rows;
		else
			return value_new_int (row);
	} else if (ref->type == VALUE_CELLRANGE) {
		Sheet    *tmp;
		GnmRange  r;

		gnm_rangeref_normalize (&ref->v_range.cell, ei->pos, &tmp, &tmp, &r);
		row    = r.start.row + 1;
		n = range_height (&r);
	} else
		return value_new_error_VALUE (ei->pos);

	if (n == 1)
		return value_new_int (row);

	res = value_new_array (1, n);
	for (i = n ; i-- > 0 ; )
		value_array_set (res, 0, i, value_new_int (row + i));
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_rows[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ROWS\n"
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
	   "@SEEALSO=COLUMN,COLUMNS,ROW")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rows (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_int (value_area_get_height (args [0], ei->pos));
}

/***************************************************************************/

static GnmFuncHelp const help_hyperlink[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=HYPERLINK\n"
	   "@SYNTAX=HYPERLINK(link_location[,optional_label])\n"

	   "@DESCRIPTION="
	   "HYPERLINK function currently returns its 2nd argument, "
	   "or if that is omitted the 1st argument.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "HYPERLINK(\"www.gnome.org\",\"GNOME\").\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_hyperlink (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmValue const * v = args[1];
	if (v == NULL)
		v = args[0];
	return value_dup (v);
}

/***************************************************************************/

static GnmFuncHelp const help_transpose[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TRANSPOSE\n"
	   "@SYNTAX=TRANSPOSE(matrix)\n"

	   "@DESCRIPTION="
	   "TRANSPOSE function returns the transpose of the input "
	   "@matrix.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=MMULT")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_transpose (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmEvalPos const * const ep = ei->pos;
        GnmValue const * const matrix = argv[0];
	int	r, c;
        GnmValue *res;

	int const cols = value_area_get_width (matrix, ep);
	int const rows = value_area_get_height (matrix, ep);

	/* Return the value directly for a singleton */
	if (rows == 1 && cols == 1)
		return value_dup (value_area_get_x_y (matrix, 0, 0, ep));

	/* REMEMBER this is a transpose */
	res = value_new_array_non_init (rows, cols);

	for (r = 0; r < rows; ++r) {
		res->v_array.vals [r] = g_new (GnmValue *, cols);
		for (c = 0; c < cols; ++c)
			res->v_array.vals[r][c] = value_dup(
				value_area_get_x_y (matrix, c, r, ep));
	}

	return res;
}

/***************************************************************************/

GnmFuncDescriptor const lookup_functions[] = {
	{ "address",   "ff|fbs", N_("row_num,col_num,abs_num,a1,text"),
	  help_address,  gnumeric_address, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "areas", NULL,	N_("reference"),
	  help_areas,	NULL,	gnumeric_areas, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "choose", NULL,	N_("index,value,"),
	  help_choose,	NULL,	gnumeric_choose, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "column",     "|A",    N_("ref"),
	  help_column,   gnumeric_column, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "columnnumber", "s",    N_("colname"),
	  help_columnnumber, gnumeric_columnnumber, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "columns",   "A",    N_("ref"),
	  help_columns, gnumeric_columns, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hlookup",   "EAf|bb", N_("val,range,col_idx,approx,as_index"),
	  help_hlookup, gnumeric_hlookup, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hyperlink", "s|s", N_("link_location, label"),
	  help_hyperlink, gnumeric_hyperlink, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_BASIC },
	{ "indirect",  "s|b",N_("ref_string,format"),
	  help_indirect, gnumeric_indirect, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "index",     "A|fff",N_("reference,row,col,area"),
	  help_index,    NULL, gnumeric_index, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "lookup",    "EA|r", N_("val,range,range"),
	  help_lookup,   gnumeric_lookup, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "match",     "EA|f", N_("val,range,approx"),
	  help_match,    gnumeric_match, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "offset",    "rff|ff",N_("ref,row,col,height,width"),
	  help_offset,   gnumeric_offset, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "row",       "|A",   N_("ref"),
	  help_row,      gnumeric_row, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rows",      "A",    N_("ref"),
	  help_rows,     gnumeric_rows, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "transpose", "A",    N_("array"),
	  help_transpose, gnumeric_transpose, NULL, NULL, NULL, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "vlookup",   "EAf|bb", N_("val,range,col_idx,approx,as_index"),
	  help_vlookup, gnumeric_vlookup, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	g_signal_connect (gnm_app_get_app (), "recalc-finished",
			  G_CALLBACK (clear_caches), NULL);
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	g_signal_handlers_disconnect_by_func (gnm_app_get_app (),
					      G_CALLBACK (clear_caches), NULL);
}
