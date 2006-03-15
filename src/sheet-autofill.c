/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sheet-autofill.c: Provides the autofill features
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org), 1998
 *
 * This is more complex than it would look at first sight,
 * as we have to support autofilling of mixed data and
 * we have to do the filling accordingly.
 *
 * The idea is that the autofill routines first classify
 * the source cells into different types and the deltas
 * are computed on a per-group basis.
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "sheet-autofill.h"

#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "workbook.h"
#include "sheet-style.h"
#include "dates.h"
#include "expr.h"
#include "expr-impl.h"
#include "format.h"
#include "datetime.h"
#include "mstyle.h"
#include "ranges.h"
#include "sheet-merge.h"
#include "str.h"
#include "mathfunc.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

typedef enum {

	/*
	 * FILL_INVALID: Should never happen, used only as a flag
	 */
	FILL_INVALID,

	/*
	 * FILL_EMPTY: Cell is empty
	 */
	FILL_EMPTY,

	/*
	 * FILL_STRING_CONSTANT: We could not figure any
	 * method of autoincrement, just duplicate this constant
	 */
	FILL_STRING_CONSTANT,

	/*
	 * FILL_STRING_WITH_NUMBER: The string contains a number
	 * that is in a position where we can auto-increment.
	 */
	FILL_STRING_WITH_NUMBER,

	/*
	 * FILL_STRING_LIST: The string matches a component in
	 * one of the autofill string lists.
	 */
	FILL_STRING_LIST,

	/*
	 * FILL_NUMBER: We are dealing with a number
	 */
	FILL_NUMBER,

	/*
	 * FILL_DAYS: Kinda like FILL_NUMBER, but when calculating the deltas
	 *            look for patterns that may suggests month/year deltas.
	 * FILL_YEARS:
	 * FILL_MONTHS: Deltas of a month or year need special care
	 */
	FILL_DAYS,
	FILL_MONTHS,
	FILL_YEARS,
	/* FILL_END_OF_MONTH FIXME This seems useful.  Could we do it ? */

	/*
	 * FILL_EXPR: This is a expression
	 */
	FILL_EXPR,

	/*
	 * FILL_BOOLEAN_CONSTANT: Just duplicate this constant
	 */
	FILL_BOOLEAN_CONSTANT
} FillType;

typedef struct {
	int    count;
	char const *const *items;
} AutoFillList;

typedef struct _FillItem {
	FillType     type;
	GnmFormat *fmt;
	GnmStyle	    *style;

	GnmCellPos	     merged_size;

	union {
		GnmExpr const *expr;
		GnmValue     *value;
		GnmString *str;
		struct {
			AutoFillList const *list;
			int           num;
		} list;
		struct {
			GnmString *str;
			int        pos, endpos; /* In bytes */
			int        num;
		} numstr;
		gboolean v_bool;
	} v;

	gboolean delta_is_float;
	union {
		gnm_float d_float;
		int d_int;
	} delta;

	struct _FillItem *group_last;
	GnmDateConventions const *date_conv;
} FillItem;

static GList *autofill_lists;

static void
autofill_register_list (char const *const *list)
{
	AutoFillList *afl;
	char const *const *p = list;

	while (*p)
		p++;

	afl = g_new (AutoFillList, 1);
	afl->count = p - list;
	afl->items = list;

	autofill_lists = g_list_prepend (autofill_lists, afl);
}

static gboolean
in_list (AutoFillList const *afl, char const *s, int *n)
{
	int i;

	for (i = 0; i < afl->count; i++) {
		char const *translated_text = _(afl->items [i]);
		if (*translated_text == '*')
			translated_text++;
		if (g_ascii_strcasecmp (translated_text, s) == 0) {
			*n = i;
			return TRUE;
		}
	}

	return FALSE;
}

static AutoFillList const *
matches_list (char const *s, int *n)
{
	GList *l;
	for (l = autofill_lists; l != NULL; l = l->next) {
		AutoFillList const *afl = l->data;
		if (in_list (afl, s, n))
			return afl;
	}
	return NULL;
}

static gboolean
string_has_number (GnmString const *str, int *num, int *bytepos, int *byteendpos)
{
	char const *s = str->str;
	char const *end, *p;
	gboolean neg, hassign;
	unsigned long val;
	long sval;

	neg = (*s == '-');
	hassign = (*s == '-' || *s == '+');
	if (hassign)
		s++;

	if (g_unichar_isdigit (g_utf8_get_char (s))) {
		/* Number in front.  */
		p = s;
	} else {
		/* Maybe number in back.  */

		p = s + strlen (s);
		while (p > s) {
			char const *p1 = g_utf8_prev_char (p);
			gunichar c = g_utf8_get_char (p1);
			if (!g_unichar_isdigit (c))
				break;
			p = p1;
		}

		if (*p == 0)
			return FALSE;

		/* Only take sign into account if whole string is number.  */
		if (s != p)
			hassign = neg = FALSE;
	}

	errno = FALSE;
	val = strtoul (p, (gchar **)&end, 10);
	sval = neg ? -(long)val : (long)val;
	*num = (int)sval;

	*bytepos = (hassign ? p - 1 : p) - str->str;
	*byteendpos = end - str->str;

	return errno == 0 && *num == (int)sval;
}

static void
fill_item_destroy (FillItem *fi)
{
	switch (fi->type) {
	case FILL_DAYS: /* Should not happen */
	case FILL_MONTHS:
	case FILL_YEARS:
	case FILL_NUMBER:
		break;

	case FILL_STRING_CONSTANT:
		gnm_string_unref (fi->v.str);
		break;

	case FILL_STRING_WITH_NUMBER:
		gnm_string_unref (fi->v.numstr.str);
		break;

	default:
		break;
	}
	if (fi->style) {
		mstyle_unref (fi->style);
		fi->style = NULL;
	}
	g_free (fi);
}

static FillItem *
fill_item_new (Sheet *sheet, int col, int row)
{
	GnmValue     *value;
	GnmValueType  value_type;
	FillItem  *fi;
	GnmCell *cell;
	GnmCellPos	pos;
	GnmRange const *merged;

	pos.col = col;
	pos.row = row;

	fi = g_new (FillItem, 1);
	fi->type = FILL_EMPTY;
	fi->date_conv = workbook_date_conv (sheet->workbook);
	mstyle_ref ((fi->style = sheet_style_get (sheet, col, row)));
	merged = sheet_merge_is_corner (sheet, &pos);
	if (merged != NULL) {
		fi->merged_size.col = merged->end.col - col + 1;
		fi->merged_size.row = merged->end.row - row + 1;
	} else
		fi->merged_size.col = fi->merged_size.row = 1;

	cell = sheet_cell_get (sheet, col, row);
	if (!cell)
		return fi;

	fi->fmt = NULL;
	if (cell_has_expr (cell)) {
		fi->type = FILL_EXPR;
		fi->v.expr = cell->base.expression;

		return fi;
	}

	value = cell->value;
	if (!value)
		return fi;

	fi->fmt = VALUE_FMT (value);
	value_type = VALUE_TYPE (value);

	if (value_type == VALUE_INTEGER || value_type == VALUE_FLOAT) {
		const GnmFormat *sf = cell_get_format (cell);

		fi->type    = FILL_NUMBER;
		fi->v.value = value;
		if (sf->family == FMT_DATE)
			fi->type = sf->family_info.date_has_days
				? FILL_DAYS
				: sf->family_info.date_has_months ? FILL_MONTHS : FILL_YEARS;
		return fi;
	}

	if (value_type == VALUE_STRING) {
		AutoFillList const *list;
		int  num, pos, endpos;

		list = matches_list (value->v_str.val->str, &num);
		if (list) {
			fi->type = FILL_STRING_LIST;
			fi->v.list.list = list;
			fi->v.list.num  = num;
			return fi;
		}

		if (string_has_number (value->v_str.val, &num, &pos, &endpos)) {
			fi->type = FILL_STRING_WITH_NUMBER;
			fi->v.numstr.str = gnm_string_ref (value->v_str.val);
			fi->v.numstr.num = num;
			fi->v.numstr.pos = pos;
			fi->v.numstr.endpos = endpos;
		} else {
			fi->type = FILL_STRING_CONSTANT;
			fi->v.str = gnm_string_ref (value->v_str.val);
		}

		return fi;
	}

	if (value_type == VALUE_BOOLEAN) {
		fi->type = FILL_BOOLEAN_CONSTANT;
		fi->v.v_bool = value->v_bool.val;

		return fi;
	}
	return fi;
}

/*
 * Computes the delta for the items of the same type in fill_item_list
 * and stores the delta result in the last element (we get this as
 * the parameter last
 *
 * @singleton_increment : filling with a singleton will increment not copy.
 */
static void
autofill_compute_delta (GList *list_last, gboolean singleton_increment)
{
	FillItem *fi = list_last->data;
	FillItem *lfi;

	fi->delta_is_float = FALSE;
	fi->delta.d_int = singleton_increment ? 1 : 0;

	/* In the special case of autofilling with a singleton date
	 * containing days default to number.
	 */
	if (fi->type == FILL_DAYS && list_last->prev == NULL)
		fi->type = FILL_NUMBER;

	switch (fi->type) {
	case FILL_DAYS:
	case FILL_MONTHS:
	case FILL_YEARS: {
		GDate prev, cur;

		if (list_last->prev == NULL)
			return;

		lfi = list_last->prev->data;

		datetime_value_to_g (&prev, lfi->v.value, fi->date_conv);
		datetime_value_to_g (&cur, fi->v.value, fi->date_conv);
		if (g_date_valid (&prev) && g_date_valid (&cur)) {
			int a = g_date_get_year (&prev);
			int b = g_date_get_year (&cur);

			/* look for patterns in the dates */
			if (fi->type == FILL_DAYS)
				fi->type = (g_date_get_day (&prev) !=
					    g_date_get_day (&cur))
					? FILL_NUMBER
					: ((g_date_get_month (&prev) !=
					    g_date_get_month (&cur))
					?  FILL_MONTHS : FILL_YEARS);

			if (fi->type == FILL_MONTHS) {
				a = 12*a + g_date_get_month (&prev);
				b = 12*b + g_date_get_month (&cur);
			}

			fi->delta.d_int = b - a;
		}
		if (fi->type == FILL_DAYS)
			fi->type = FILL_NUMBER;
		else if (fi->type != FILL_NUMBER)
			return;
	}
	/* fall through */

	case FILL_NUMBER: {
		gnm_float a, b;

		if (list_last->prev == NULL) {
			if ((fi->delta_is_float = (VALUE_TYPE (fi->v.value) == VALUE_FLOAT)))
				fi->delta.d_float = singleton_increment ? 1. : 0.;
			return;
		}
		lfi = list_last->prev->data;

		if (VALUE_TYPE (fi->v.value) == VALUE_INTEGER &&
		    VALUE_TYPE (lfi->v.value) == VALUE_INTEGER) {
			fi->delta_is_float = FALSE;
			fi->delta.d_int = fi->v.value->v_int.val - lfi->v.value->v_int.val;
			return;
		}

		a = value_get_as_float (lfi->v.value);
		b = value_get_as_float (fi->v.value);

		fi->delta_is_float = TRUE;
		fi->delta.d_float = b - a;
		return;
	}

	case FILL_STRING_WITH_NUMBER:
		if (list_last->prev != NULL) {
			lfi = list_last->prev->data;

			fi->delta.d_int = fi->v.numstr.num - lfi->v.numstr.num;
		}
		return;

	case FILL_STRING_LIST:
		if (list_last->prev != NULL) {
			lfi = list_last->prev->data;

			fi->delta.d_int = fi->v.list.num - lfi->v.list.num;
		}
		return;

	case FILL_EMPTY:
	case FILL_STRING_CONSTANT:
	case FILL_BOOLEAN_CONSTANT:
	case FILL_EXPR:
	case FILL_INVALID:
		return;
	}
}

static int
type_is_compatible (FillItem *last, FillItem *current)
{
	if (last == NULL)
		return FALSE;

	if (last->type != current->type)
		return FALSE;

	if (last->type == FILL_STRING_LIST) {
		int num;
		char const *str;
			
		if (last->v.list.list == current->v.list.list)
			return TRUE;

		/* This item may be in multiple lists.  Is it in the
		 * same list as the previous element ? */
		str = current->v.list.list->items [current->v.list.num];
		if (*str == '*')
			str++;
		if (in_list (last->v.list.list, str, &num)) {
				current->v.list.list = last->v.list.list;
				current->v.list.num = num;
			return TRUE;
		}

		/* The previous element may be in multple lists.  Is it
		 * in the current list ? */
		str = last->v.list.list->items [last->v.list.num];
		if (*str == '*')
			str++;
		if (in_list (current->v.list.list, str, &num)) {
			last->v.list.list = current->v.list.list;
			last->v.list.num = num;
			return TRUE;
		}

			return FALSE;
	}

	return TRUE;
}

static GList *
autofill_create_fill_items (Sheet *sheet, gboolean singleton_increment,
			    int col, int row,
			    int region_size, int col_inc, int row_inc)
{
	FillItem *last;
	GList *item_list, *all_items, *major;
	int i;

	last = NULL;
	item_list = all_items = NULL;

	for (i = 0; i < region_size;) {
		FillItem *fi = fill_item_new (sheet, col, row);

		if (!type_is_compatible (last, fi)) {
			if (last) {
				all_items = g_list_prepend (all_items, g_list_reverse (item_list));
				item_list = NULL;
			}

			last = fi;
		}

		item_list = g_list_prepend (item_list, fi);

		if (col_inc != 0) {
			col += col_inc * fi->merged_size.col;
			i += fi->merged_size.col;
		} else {
			row += row_inc * fi->merged_size.row;
			i += fi->merged_size.row;
		}
	}

	if (item_list)
		all_items = g_list_prepend (all_items, g_list_reverse (item_list));
	all_items = g_list_reverse (all_items);

	/* Make every non-ending group point to the end element
	 * and compute the deltas.
	 */
	for (major = all_items; major; major = major->next) {
		GList *group = major->data, *minor, *last_item;
		FillItem *last_fi;

		last_item = g_list_last (group);
		last_fi = last_item->data;
		for (minor = group; minor; minor = minor->next) {
			FillItem *fi = minor->data;
			fi->group_last = last_fi;
		}

		autofill_compute_delta (last_item, singleton_increment);
	}

	return all_items;
}

static void
autofill_destroy_fill_items (GList *all_items)
{
	GList *major, *minor;

	for (major = all_items; major; major = major->next) {
		for (minor = major->data; minor; minor = minor->next)
			fill_item_destroy (minor->data);
		g_list_free (major->data);
	}

	g_list_free (all_items);
}

static void
autofill_cell (FillItem *fi, GnmCell *cell, int idx, int limit_x, int limit_y)
{
	FillItem const *delta = fi->group_last;
	GnmValue *v;

	g_return_if_fail (delta != NULL);

	/* FILL_DAYS is a place holder used to test for day/month/year fills.
	 * The last item in the minor list is the delta.  It is the only one
	 * that has had its type analyized.  So the first time we go through
	 * convert the other elements into the same type.
	 */
	if (fi->type == FILL_DAYS)
		fi->type = delta->type;

	switch (fi->type) {
	default:
	case FILL_EMPTY:
	case FILL_INVALID:
		g_warning ("This case should not be handled here.");
		return;

	case FILL_EXPR: {
		GnmExprRewriteInfo   rwinfo;
		GnmExprRelocateInfo *rinfo;
		GnmExpr const *func;

		rinfo = &rwinfo.u.relocate;

		/* FIXME : Find out how to handle this */
		rwinfo.type = GNM_EXPR_REWRITE_RELOCATE;
		rinfo->target_sheet = rinfo->origin_sheet = NULL;
		rinfo->col_offset = rinfo->row_offset = 0;
		rinfo->origin.start = rinfo->origin.end = cell->pos;
		eval_pos_init_cell (&rinfo->pos, cell);

		func = gnm_expr_rewrite (fi->v.expr, &rwinfo);

		/* clip arrays that are only partially copied */
		if (fi->v.expr->any.oper == GNM_EXPR_OP_ARRAY) {
			GnmExprArray const *array = &fi->v.expr->array;
			if (array->cols > limit_x) {
				if (func != NULL)
					((GnmExpr*)func)->array.cols = limit_x;
				else
					func = gnm_expr_new_array (
						array->x, array->y,
						limit_x, array->rows, NULL);
			}
			if (array->rows > limit_y) {
				if (func != NULL)
					((GnmExpr*)func)->array.rows = limit_y;
				else
					func = gnm_expr_new_array (
						array->x, array->y,
						array->cols, limit_y, NULL);
			}

			if (func != NULL &&
			    func->array.x == 0 && func->array.y == 0 &&
			    func->array.corner.expr == NULL) {
				gnm_expr_ref (array->corner.expr);
				((GnmExpr*)func)->array.corner.expr = array->corner.expr;
			}
		}
		cell_set_expr (cell, (func == NULL) ? fi->v.expr : func);

		if (func)
			gnm_expr_unref (func);

		/* NOTE : _RETURN_ do not fall through to the value asignment */
		return;
	}

	case FILL_BOOLEAN_CONSTANT:
		v = value_new_bool (fi->v.v_bool);
		break;
	case FILL_STRING_CONSTANT:
		v = value_new_string (fi->v.str->str);
		break;
	case FILL_STRING_WITH_NUMBER: {
		int i = delta->v.numstr.num + idx * delta->delta.d_int;
		int prefixlen = delta->v.numstr.pos;
		char const *prefix = delta->v.numstr.str->str;
		char const *postfix = delta->v.numstr.str->str +
			delta->v.numstr.endpos;

		v = value_new_string_nocopy (g_strdup_printf (
			"%-.*s%d%s", prefixlen, prefix, i, postfix));
		break;
	}

	case FILL_DAYS : /* this should have been converted above */
		g_warning ("Please report this warning and detail the autofill"
			   "\nsetup used to generate it.");
	case FILL_NUMBER:
		if (delta->delta_is_float) {
			gnm_float d = value_get_as_float (delta->v.value);
			v = value_new_float (d + idx * delta->delta.d_float);
		} else {
			int i = value_get_as_int (delta->v.value);
			v = value_new_int (i + idx * delta->delta.d_int);
		}
		break;

	case FILL_MONTHS :
	case FILL_YEARS : {
		int d = idx * delta->delta.d_int;
		GDate date;
		gnm_float res = datetime_value_to_serial_raw (delta->v.value, fi->date_conv);

		datetime_value_to_g (&date, delta->v.value, fi->date_conv);
		if (fi->type == FILL_MONTHS) {
			if (d > 0)
				g_date_add_months (&date, d);
			else
				g_date_subtract_months (&date, -d);
		} else {
			if (d > 0)
				g_date_add_years (&date, d);
			else
				g_date_subtract_years (&date, -d);
		}
		d = datetime_g_to_serial (&date, fi->date_conv);

		res -= gnumeric_fake_floor (res);
		v = (res < 1e-6) ? value_new_int (d)
			: value_new_float (((gnm_float)d) + res);
		break;
	}

	case FILL_STRING_LIST: {
		char const *text;
		int n = delta->v.list.num + idx * delta->delta.d_int;

		n %= delta->v.list.list->count;

		if (n < 0)
			n += delta->v.list.list->count;

		text = _(delta->v.list.list->items [n]);
		if (*text == '*')
			text++;

		v = value_new_string (text);
		break;
	}
	}

	if (fi->fmt != NULL)
		value_set_fmt (v, fi->fmt);
	cell_set_value (cell, v);
}

static void
sheet_autofill_dir (Sheet *sheet, gboolean singleton_increment,
		    int base_col,     int base_row,
		    int region_size,
		    int start_pos,    int end_pos,
		    int col_inc,      int row_inc)
{
	GList *all_items, *major, *minor;
	int col, row, count, sub_index, loops, group_count, count_max;

	all_items = autofill_create_fill_items (sheet, singleton_increment,
		base_col, base_row, region_size, col_inc, row_inc);

	major = all_items;
	minor = NULL;
	loops = sub_index = group_count = 0;
	col = base_col + region_size * col_inc;
	row = base_row + region_size * row_inc;
	count_max = (start_pos < end_pos)
		? end_pos - start_pos - region_size
		: start_pos - end_pos - region_size;
	for (count = 0; count < count_max; ) {
		FillItem *fi;
		GnmCell *cell;

		if ((minor != NULL && minor->next == NULL) || minor == NULL) {
			if (major == NULL) {
				major = all_items;
				loops++;
			}
			minor = major->data;
			group_count = g_list_length (minor);
			sub_index = 1;
			major = major->next;
		} else {
			minor = minor->next;
			sub_index++;
		}
		fi = minor->data;

		cell = sheet_cell_get (sheet, col, row);
		if (fi->type != FILL_EMPTY) {
			int limit_x = SHEET_MAX_COLS, limit_y = SHEET_MAX_ROWS;

			if (cell == NULL)
				cell = sheet_cell_new (sheet, col, row);

			/* Arrays are special.
			 * It would be nice to resize the existing array rather than
			 * shifting a new one, but we can look into that later.
			 * For now while using inverse autofill we make sure
			 * that if we're pasting an array we always include the
			 * corner.  autofill_cell handles the dimension clipping.
			 */
			if (fi->type == FILL_EXPR &&
			    fi->v.expr->any.oper == GNM_EXPR_OP_ARRAY) {
				GnmExprArray const *array = &fi->v.expr->array;
				int n = 0, remain = count_max - count - 1;
				if (col_inc < 0)
					n = array->x - remain;
				else if (row_inc < 0)
					n = array->y - remain;

				while (n-- > 0) {
					minor = minor->next;
					g_return_if_fail (minor != NULL);
				}
				fi = minor->data;

				if (col_inc != 0)
					limit_x = remain + 1;
				else
					limit_y = remain + 1;
			}

			autofill_cell (fi, cell,
				loops * group_count + sub_index,
				limit_x, limit_y);
		} else if (cell != NULL)
			sheet_cell_remove (sheet, cell, TRUE, TRUE);

		mstyle_ref (fi->style); /* style_set steals ref */
		sheet_style_set_pos (sheet, col, row, fi->style);

		if (fi->merged_size.col != 1 || fi->merged_size.row != 1) {
			GnmRange tmp;
			range_init (&tmp, col, row,
				    col + fi->merged_size.col - 1,
				    row + fi->merged_size.row - 1);
			sheet_merge_add	(sheet, &tmp, TRUE, NULL);
		}

		if (col_inc != 0) {
			col += col_inc * fi->merged_size.col;
			count += fi->merged_size.col;
		} else {
			row += row_inc * fi->merged_size.row;
			count += fi->merged_size.row;
		}
	}

	autofill_destroy_fill_items (all_items);
}

void
autofill_init (void)
{
	autofill_register_list (day_short);
	autofill_register_list (day_long);
	autofill_register_list (month_short);
	autofill_register_list (month_long);
}

/**
 * sheet_autofill :
 *
 * An internal routine to autofill a region.  It does NOT
 * queue a recalc, flag a status update, or regen spans.
 */
void
sheet_autofill (Sheet *sheet, gboolean singleton_increment,
		int base_col,	int base_row,
		int w,		int h,
		int end_col,	int end_row)
{
	int series;

	g_return_if_fail (IS_SHEET (sheet));

	if (base_col > end_col || base_row > end_row) {
		/* Inverse Auto-fill */
		if (base_col != end_col + w - 1) {
			for (series = 0; series < h; series++)
				sheet_autofill_dir (sheet, singleton_increment,
					base_col, base_row-series, w,
					base_col, end_col-1, -1, 0);
		} else {
			for (series = 0; series < w; series++)
				sheet_autofill_dir (sheet, singleton_increment,
					base_col-series, base_row, h,
					base_row, end_row-1, 0, -1);
		}
	} else {
		if (end_col != base_col + w - 1) {
			for (series = 0; series < h; series++)
				sheet_autofill_dir (sheet, singleton_increment,
					base_col, base_row+series, w,
					base_col, end_col+1, 1, 0);
		} else {
			for (series = 0; series < w; series++)
				sheet_autofill_dir (sheet, singleton_increment,
					base_col+series, base_row, h,
					base_row, end_row+1, 0, 1);
		}
	}
}
