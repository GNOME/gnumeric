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
#include <config.h>
#include "sheet-autofill.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "workbook.h"
#include "sheet-style.h"
#include "dates.h"
#include "expr.h"
#include "formats.h"
#include "datetime.h"
#include "mstyle.h"
#include "ranges.h"
#include "sheet-merge.h"
#include "str.h"
#include "mathfunc.h"

#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

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
	 * FILL_FORMULA: This is a formula
	 */
	FILL_FORMULA,

	/*
	 * FILL_BOOLEAN_CONSTANT: Just duplicate this constant
	 */
	FILL_BOOLEAN_CONSTANT
} FillType;

typedef struct {
	int    count;
	const char *const *items;
} AutoFillList;

typedef struct _FillItem {
	FillType     type;
	StyleFormat *fmt;
	MStyle	    *style;

	gboolean     is_merged;
	CellPos	     merged_size;

	union {
		ExprTree *formula;
		Value    *value;
		String   *str;
		struct {
			AutoFillList const *list;
			int           num;
			int           was_i18n;
		} list;
		struct {
			String   *str;
			int       pos, num;
		} numstr;
		gboolean bool;
	} v;

	gboolean delta_is_float;
	union {
		double    d_float;
		int       d_int;
	} delta;

	struct _FillItem *group_last;
} FillItem;

static GList *autofill_lists;

static void
autofill_register_list (char const *const *list)
{
	AutoFillList *afl;
	const char *const *p = list;

	while (*p)
		p++;

	afl = g_new (AutoFillList, 1);
	afl->count = p - list;
	afl->items = list;

	autofill_lists = g_list_prepend (autofill_lists, afl);
}

static gboolean
in_list (AutoFillList const *afl, char const *s, int *n, int *is_i18n)
{
	int i;

	for (i = 0; i < afl->count; i++) {
		const char *english_text, *translated_text;

		english_text = afl->items [i];
		if (english_text [0] == '*')
			english_text++;

		if ((g_strcasecmp (english_text, s) == 0)) {
			*is_i18n = FALSE;
			*n = i;
			return TRUE;
		}

		translated_text = _(afl->items [i]);
		if (*translated_text == '*')
			translated_text++;

		if (g_strcasecmp (translated_text, s) == 0) {
			*is_i18n = TRUE;
			*n = i;
			return TRUE;
		}
	}

	return FALSE;
}

static AutoFillList const *
matches_list (char const *s, int *n, int *is_i18n)
{
	GList *l;
	for (l = autofill_lists; l != NULL; l = l->next) {
		AutoFillList const *afl = l->data;
		if (in_list (afl, s, n, is_i18n))
			return afl;
	}
	return NULL;
}

static int
string_has_number (String *str, int *num, int *pos)
{
	char *s = str->str, *p, *end;
	int l = strlen (s);
	gboolean found_number = FALSE;
	long val;

	errno = 0;
	val = strtol (s, &end, 10);
	if (s != end) {
		if (errno != ERANGE) {
			*num = val;
			*pos = 0;
			return TRUE;
		}
	}
	if (l <= 1)
		return FALSE;

	for (p = s + l - 1; p > str->str && isdigit ((unsigned char)*p); p--)
		found_number = TRUE;

	if (!found_number)
		return FALSE;

	p++;
	errno = 0;
	val = strtol (p, &end, 10);
	if (p != end) {
		if (errno != ERANGE) {
			*num = val;
			*pos = p - str->str;
			return TRUE;
		}
	}

	return FALSE;
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
		string_unref (fi->v.str);
		break;

	case FILL_STRING_WITH_NUMBER:
		string_unref (fi->v.numstr.str);
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

static gboolean
str_contains (char const *str, char c)
{
	char const *tmp;
	for (tmp = str ; (tmp = strchr (tmp, 'd')) != NULL ; )
		if (tmp == str || tmp[-1] != '\\')
			return TRUE;
	return FALSE;
}

static FillItem *
fill_item_new (Sheet *sheet, int col, int row)
{
	Value     *value;
	ValueType  value_type;
	FillItem  *fi;
	Cell *cell;
	CellPos	pos;
	Range const *merged;

	pos.col = col;
	pos.row = row;

	fi = g_new (FillItem, 1);
	fi->type = FILL_EMPTY;
	mstyle_ref ((fi->style = sheet_style_get (sheet, col, row)));
	merged = sheet_merge_is_corner (sheet, &pos);
	if ((fi->is_merged = (merged != NULL))) {
		fi->merged_size.col = merged->end.col - col;
		fi->merged_size.row = merged->end.row - row;
	}

	cell = sheet_cell_get (sheet, col, row);
	if (!cell)
		return fi;

	if (cell_has_expr (cell)) {
		fi->type = FILL_FORMULA;
		fi->v.formula = cell->base.expression;

		return fi;
	}

	value = cell->value;
	if (!value)
		return fi;

	value_type = value->type;

	if (value_type == VALUE_INTEGER || value_type == VALUE_FLOAT) {
		FillType fill = FILL_NUMBER;

		/* Use display format to recognize iteration types */
		char *fmt = cell_get_format (cell);
		if (fmt != NULL) {
			FormatCharacteristics info;
			FormatFamily family = cell_format_classify (fmt, &info);

			/* FIXME : We need better format classification that this.
			 * the XL format is crap.  redo it.
			 */
			if (family == FMT_DATE)
				fill =    (str_contains (fmt, 'd') || str_contains (fmt, 'D'))
					? FILL_DAYS
					: ((str_contains (fmt, 'm') || str_contains (fmt, 'M'))
					? FILL_MONTHS : FILL_YEARS);

			g_free (fmt);
		}
		fi->type    = fill;
		fi->v.value = value;
		fi->fmt = cell->format;

		return fi;
	}

	if (value_type == VALUE_STRING) {
		AutoFillList const *list;
		int  num, pos, i18;

		fi->type = FILL_STRING_CONSTANT;
		fi->v.str = string_ref (value->v_str.val);

		list = matches_list (value->v_str.val->str, &num, &i18);
		if (list) {
			fi->type = FILL_STRING_LIST;
			fi->v.list.list = list;
			fi->v.list.num  = num;
			fi->v.list.was_i18n = i18;
			return fi;
		}

		if (string_has_number (value->v_str.val, &num, &pos)) {
			fi->type = FILL_STRING_WITH_NUMBER;
			fi->v.numstr.str = value->v_str.val;
			fi->v.numstr.num = num;
			fi->v.numstr.pos = pos;
		}

		return fi;
	}

	if (value_type == VALUE_BOOLEAN) {
		fi->type = FILL_BOOLEAN_CONSTANT;
		fi->v.bool = value->v_bool.val;

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
		GDate *prev, *cur;

		if (list_last->prev == NULL)
			return;

		lfi = list_last->prev->data;

		prev = datetime_value_to_g (lfi->v.value);
		cur  = datetime_value_to_g (fi->v.value);
		if (g_date_valid (prev) && g_date_valid (cur)) {
			int a = g_date_year (prev);
			int b = g_date_year (cur);

			/* look for patterns in the dates */
			if (fi->type == FILL_DAYS)
				fi->type = (g_date_day (prev) != g_date_day (cur))
					? FILL_NUMBER
					: ((g_date_month (prev) != g_date_month (cur))
					?  FILL_MONTHS : FILL_YEARS);
			
			if (fi->type == FILL_MONTHS) {
				a = 12*a + g_date_month (prev);
				b = 12*b + g_date_month (cur);
			}

			fi->delta.d_int = b - a;
		}
		g_date_free (prev);
		g_date_free (cur);
		if (fi->type == FILL_DAYS)
			fi->type = FILL_NUMBER;
		else if (fi->type != FILL_NUMBER)
			return;
	}
	/* fall through */

	case FILL_NUMBER: {
		double a, b;

		if (list_last->prev == NULL) {
			if ((fi->delta_is_float = (fi->v.value->type == VALUE_FLOAT)))
				fi->delta.d_float = singleton_increment ? 1. : 0.;
			return;
		}
		lfi = list_last->prev->data;

		if (fi->v.value->type == VALUE_INTEGER && lfi->v.value->type == VALUE_INTEGER) {
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
	case FILL_FORMULA:
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
		/* It is possible the item is in multiple lists.  If things
		 * disagree see if we are in the previous list, and convert
		 * eg May
		 */
		if (last->v.list.list != current->v.list.list) {
			int num, is_i18n;
			if (in_list (last->v.list.list,
				     current->v.list.list->items [current->v.list.num],
				     &num, &is_i18n)) {
				current->v.list.list = last->v.list.list;
				current->v.list.num = num;
				current->v.list.was_i18n = is_i18n;
			} else
				return FALSE;
		}

		if (last->v.list.was_i18n != current->v.list.was_i18n)
			return FALSE;
	}

	return TRUE;
}

static GList *
autofill_create_fill_items (Sheet *sheet, gboolean singleton_increment,
			    int col, int row,
			    int region_count, int col_inc, int row_inc)
{
	FillItem *last;
	GList *item_list, *all_items, *major;
	int i;

	last = NULL;
	item_list = all_items = NULL;

	for (i = 0; i < region_count; i++) {
		FillItem *fi = fill_item_new (sheet, col, row);

		if (!type_is_compatible (last, fi)) {
			if (last) {
				all_items = g_list_append (all_items, item_list);
				item_list = NULL;
			}

			last = fi;
		}

		item_list = g_list_append (item_list, fi);

		col += col_inc;
		row += row_inc;
	}

	if (item_list)
		all_items = g_list_append (all_items, item_list);

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
autofill_cell (Cell *cell, int idx, FillItem *fi)
{
	/* FILL_DAYS is a place holder used to test for day/month/year fills.
	 * The last item in the minor list is the delta.  It is the only one
	 * that has had its type analyized.  So the first time we go through
	 * convert the other elements into the same type.
	 */
	if (fi->type == FILL_DAYS) {
		FillItem const *delta = fi->group_last;
		g_return_if_fail (delta != NULL);
		fi->type = delta->type;
	}

	switch (fi->type) {
	case FILL_EMPTY:
	case FILL_INVALID:
		g_warning ("This case should not be handled here\n");
		return;

	case FILL_STRING_CONSTANT:
		cell_set_value (cell, value_new_string (fi->v.str->str), NULL);
		return;

	case FILL_STRING_WITH_NUMBER: {
		FillItem *delta = fi->group_last;
		char buffer [sizeof (int) * 4], *v;
		int i;

		i = delta->v.numstr.num + idx * delta->delta.d_int;
		snprintf (buffer, sizeof (buffer)-1, "%d", i);

		if (delta->v.numstr.pos == 0) {
			char *p = delta->v.numstr.str->str;

			while (*p && isdigit ((unsigned char)*p))
			       p++;

			v = g_strconcat (buffer, p, NULL);
		} else {
			char *n = g_strdup (delta->v.numstr.str->str);
			n [delta->v.numstr.pos] = 0;

			v = g_strconcat (n, buffer, NULL);
			g_free (n);
		}
	
		sheet_cell_set_value (cell, value_new_string (v), NULL);
		g_free (v);
		return;
	}

	case FILL_DAYS : /* this should have been converted above */
		g_warning ("Please report this warning and detail the autofill\n"
			   "setup used to generate it.");
	case FILL_NUMBER: {
		FillItem const *delta = fi->group_last;
		Value *v;

		if (delta->delta_is_float) {
			double const d = value_get_as_float (delta->v.value);
			v = value_new_float (d + idx * delta->delta.d_float);
		} else {
			int const i = value_get_as_int (delta->v.value);
			v = value_new_int (i + idx * delta->delta.d_int);
		}
		cell_set_value (cell, v, fi->fmt);
		return;
	}

	case FILL_MONTHS :
	case FILL_YEARS : {
		Value *v;
		FillItem *delta = fi->group_last;
		int d = idx * delta->delta.d_int;
		GDate *date = datetime_value_to_g (delta->v.value);
		gnum_float res = datetime_value_to_serial_raw (delta->v.value);

		if (fi->type == FILL_MONTHS) {
			if (d > 0)
				g_date_add_months (date, d);
			else
				g_date_subtract_months (date, -d);
		} else {
			if (d > 0)
				g_date_add_years (date, d);
			else
				g_date_subtract_years (date, -d);
		}
		d = datetime_g_to_serial (date);
		g_date_free (date);

		res -= gnumeric_fake_floor (res);
		v = (res < 1e-6) ? value_new_int (d)
			: value_new_float (((gnum_float)d) + res);
		cell_set_value (cell, v, fi->fmt);
		return;
	}

	case FILL_STRING_LIST: {
		FillItem *delta = fi->group_last;
		const char *text;
		int n;

		n = delta->v.list.num + idx * delta->delta.d_int;

		n %= delta->v.list.list->count;

		if (n < 0)
			n += delta->v.list.list->count;

		text = delta->v.list.list->items [n];
		if (delta->v.list.was_i18n)
			text = _(text);

		if (*text == '*')
			text++;

		cell_set_value (cell, value_new_string(text), NULL);

		return;
	}

	case FILL_FORMULA:
	{
		ExprTree * func;
		ExprRewriteInfo   rwinfo;
		ExprRelocateInfo *rinfo;

		rinfo = &rwinfo.u.relocate;

		/* FIXME : Find out how to handle this */
		rwinfo.type = EXPR_REWRITE_RELOCATE;
		rinfo->target_sheet = rinfo->origin_sheet = 0;
		rinfo->col_offset = rinfo->row_offset = 0;
		rinfo->origin.start = rinfo->origin.end = cell->pos;
		eval_pos_init_cell (&rinfo->pos, cell);

		/* FIXME : I presume this is needed to invalidate
		 * relative references that will fall off the
		 * edge ?? */
		func = expr_rewrite (fi->v.formula, &rwinfo);
		cell_set_expr (cell, (func == NULL) ? fi->v.formula : func, NULL);
		return;
	}

	case FILL_BOOLEAN_CONSTANT:
		cell_set_value (cell, value_new_bool (fi->v.bool), NULL);
		return;
	}
}

static void
sheet_autofill_dir (Sheet *sheet, gboolean singleton_increment,
		    int base_col,     int base_row,
		    int region_count,
		    int start_pos,    int end_pos,
		    int col_inc,      int row_inc)
{
	GList *all_items, *major, *minor;
	int col = base_col;
	int row = base_row;
	int count, sub_index, loops, group_count;
	int count_max;

	/* Create the fill items */
	all_items = autofill_create_fill_items (sheet, singleton_increment,
		base_col, base_row, region_count, col_inc, row_inc);

	col = base_col + region_count * col_inc;
	row = base_row + region_count * row_inc;

	/* Do the autofill */
	major = all_items;
	minor = NULL;
	loops = sub_index = group_count = 0;

	count_max = (start_pos < end_pos) ? end_pos - start_pos - region_count 
					  : start_pos - end_pos - region_count;
	for (count = 0; count < count_max; count++) {
		FillItem *fi;
		Cell *cell;

		if ((minor && minor->next == NULL) || minor == NULL) {
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

		mstyle_ref (fi->style);
		sheet_style_set_pos (sheet, col, row, fi->style);

		cell = sheet_cell_get (sheet, col, row);
		if (fi->type == FILL_EMPTY) {
			if (cell)
				sheet_cell_remove (sheet, cell, TRUE);
		} else {
			if (!cell)
				cell = sheet_cell_new (sheet, col, row);
			autofill_cell (cell,
				       loops * group_count + sub_index, fi);
		}

		if (fi->is_merged) {
			Range tmp;
			range_init (&tmp, col, row,
				    col + fi->merged_size.col,
				    row + fi->merged_size.row);
			sheet_merge_add	(NULL, sheet, &tmp, TRUE);
		}

		col += col_inc;
		row += row_inc;
	}

	autofill_destroy_fill_items (all_items);
}

static void
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
	static int autofill_inited;
	int series;

	g_return_if_fail (IS_SHEET (sheet));

	if (!autofill_inited) {
		autofill_init ();
		autofill_inited = TRUE;
	}

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
