/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-filter.c: support for 'auto-filters'
 *
 * Copyright (C) 2002-2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "libgnumeric.h"
#include "sheet-filter.h"
#include "sheet-filter-combo.h"

#include "workbook.h"
#include "sheet.h"
#include "sheet-private.h"
#include "cell.h"
#include "expr.h"
#include "value.h"
#include "gnm-format.h"
#include "ranges.h"
#include "str.h"
#include "number-match.h"
#include "gutils.h"
#include "sheet-object.h"
#include "gnm-filter-combo-foo-view.h"
#include "gnm-cell-combo-foo-view.h"
#include <gsf/gsf-impl-utils.h>

#include <glib/gi18n-lib.h>
#include <stdlib.h>
#include <string.h>

/**
 * gnm_filter_condition_new_single :
 * @op : #GnmFilterOp
 * @v : #GnmValue
 *
 * Create a new condition with 1 value.
 * Absorbs the reference to @v.
 **/
GnmFilterCondition *
gnm_filter_condition_new_single (GnmFilterOp op, GnmValue *v)
{
	GnmFilterCondition *res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = op;	res->op[1] = GNM_FILTER_UNUSED;
	res->value[0] = v;
	return res;
}

/**
 * gnm_filter_condition_new_double :
 * @op0 : #GnmFilterOp
 * @v0 : #GnmValue
 * @join_with_and :
 * @op1 : #GnmFilterOp
 * @v1 : #GnmValue
 *
 * Create a new condition with 2 value.
 * Absorbs the reference to @v0 and @v1.
 **/
GnmFilterCondition *
gnm_filter_condition_new_double (GnmFilterOp op0, GnmValue *v0,
				 gboolean join_with_and,
				 GnmFilterOp op1, GnmValue *v1)
{
	GnmFilterCondition *res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = op0;	res->op[1] = op1;
	res->is_and = join_with_and;
	res->value[0] = v0;	res->value[1] = v1;
	return res;
}

GnmFilterCondition *
gnm_filter_condition_new_bucket (gboolean top, gboolean absolute, float n)
{
	GnmFilterCondition *res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = GNM_FILTER_OP_TOP_N | (top ? 0 : 1) | (absolute ? 0 : 2);
	res->op[1] = GNM_FILTER_UNUSED;
	res->count = n;
	return res;
}

GnmFilterCondition *
gnm_filter_condition_dup (GnmFilterCondition const *src)
{
	GnmFilterCondition *dst;

	if (src == NULL)
		return NULL;

	dst = g_new0 (GnmFilterCondition, 1);
	dst->op[0]    = src->op[0];
	dst->op[1]    = src->op[1];
	dst->is_and   = src->is_and;
	dst->count    = src->count;
	dst->value[0] = value_dup (src->value[0]);
	dst->value[1] = value_dup (src->value[1]);
	return dst;
}

void
gnm_filter_condition_unref (GnmFilterCondition *cond)
{
	g_return_if_fail (cond != NULL);
	if (cond->value[0] != NULL)
		value_release (cond->value[0]);
	if (cond->value[1] != NULL)
		value_release (cond->value[1]);
}

/*****************************************************************************/

typedef struct  {
	GnmFilterCondition const *cond;
	GnmValue		 *val[2];
	GnmValue		 *alt_val[2];
	GORegexp		  regexp[2];
	Sheet			 *target_sheet; /* not necessarilly the src */
} FilterExpr;

static void
filter_expr_init (FilterExpr *fexpr, unsigned i,
		  GnmFilterCondition const *cond,
		  GnmFilter const *filter)
{
	GnmValue *tmp = cond->value[i];

	if (tmp && VALUE_IS_STRING (tmp)) {
		GnmFilterOp op = cond->op[i];
		char const *str = value_peek_string (tmp);
		GODateConventions const *date_conv =
			workbook_date_conv (filter->sheet->workbook);

		if ((op == GNM_FILTER_OP_EQUAL || op == GNM_FILTER_OP_NOT_EQUAL) &&
		    gnm_regcomp_XL (fexpr->regexp + i, str, REG_ICASE) == REG_OK) {
			fexpr->val[i] = NULL;
			return;
		}

		fexpr->val[i] = format_match_number (str, NULL, date_conv);
		if (fexpr->val[i] != NULL)
			return;
	}
	fexpr->val[i] = value_dup (tmp);
}

static void
filter_expr_release (FilterExpr *fexpr, unsigned i)
{
	if (fexpr->val[i] != NULL)
		value_release (fexpr->val[i]);
	else
		go_regfree (fexpr->regexp + i);
}

static gboolean
filter_expr_eval (GnmFilterOp op, GnmValue const *src, GORegexp const *regexp,
		  GnmCell *cell)
{
	GnmValue *target = cell->value;
	GnmValDiff cmp;

	if (src == NULL) {
		GOFormat const *format = gnm_cell_get_format (cell);
		GODateConventions const *date_conv =
			workbook_date_conv (cell->base.sheet->workbook);
		char *str = format_value (format, target, NULL, -1, date_conv);
		GORegmatch rm;
		int res = go_regexec (regexp, str, 1, &rm, 0);

		switch (res) {
		case REG_OK:
			if (rm.rm_so == 0 && strlen (str) == (size_t)rm.rm_eo) {
				g_free (str);
				return op == GNM_FILTER_OP_EQUAL;
			}
			/* fall through */

		case REG_NOMATCH:
			g_free (str);
			return op == GNM_FILTER_OP_NOT_EQUAL;

		default:
			g_free (str);
			g_warning ("Unexpected regexec result");
			return FALSE;
		}
	}

	cmp = value_compare (target, src, TRUE);
	switch (op) {
	case GNM_FILTER_OP_EQUAL     : return cmp == IS_EQUAL;
	case GNM_FILTER_OP_NOT_EQUAL : return cmp != IS_EQUAL;
	case GNM_FILTER_OP_GTE	: if (cmp == IS_EQUAL) return TRUE; /* fall */
	case GNM_FILTER_OP_GT	: return cmp == IS_GREATER;
	case GNM_FILTER_OP_LTE	: if (cmp == IS_EQUAL) return TRUE; /* fall */
	case GNM_FILTER_OP_LT	: return cmp == IS_LESS;
	default :
		g_warning ("Huh?");
		return FALSE;
	}
}

static GnmValue *
cb_filter_expr (GnmCellIter const *iter, FilterExpr const *fexpr)
{
	if (iter->cell != NULL) {
		unsigned int ui;

		for (ui = 0; ui < G_N_ELEMENTS (fexpr->cond->op); ui++) {
			gboolean res;

			if (fexpr->cond->op[ui] == GNM_FILTER_UNUSED)
				continue;

			res = filter_expr_eval (fexpr->cond->op[ui],
						fexpr->val[ui],
						fexpr->regexp + ui,
						iter->cell);
			if (fexpr->cond->is_and && !res)
				goto nope;   /* AND(...,FALSE,...) */
			if (res && !fexpr->cond->is_and)
				return NULL;   /* OR(...,TRUE,...) */
		}

		if (fexpr->cond->is_and)
			return NULL;  /* AND(TRUE,...,TRUE) */
	}

 nope:
	colrow_set_visibility (fexpr->target_sheet, FALSE, FALSE,
		iter->pp.eval.row, iter->pp.eval.row);
	return NULL;
}

/*****************************************************************************/

static GnmValue *
cb_filter_non_blanks (GnmCellIter const *iter, Sheet *target_sheet)
{
	if (gnm_cell_is_blank (iter->cell))
		colrow_set_visibility (target_sheet, FALSE, FALSE,
			iter->pp.eval.row, iter->pp.eval.row);
	return NULL;
}

static GnmValue *
cb_filter_blanks (GnmCellIter const *iter, Sheet *target_sheet)
{
	if (!gnm_cell_is_blank (iter->cell))
		colrow_set_visibility (target_sheet, FALSE, FALSE,
			iter->pp.eval.row, iter->pp.eval.row);
	return NULL;
}

/*****************************************************************************/

typedef struct {
	unsigned count;
	unsigned elements;
	gboolean find_max;
	GnmValue const **vals;
	Sheet	*target_sheet;
} FilterItems;

static GnmValue *
cb_filter_find_items (GnmCellIter const *iter, FilterItems *data)
{
	GnmValue const *v = iter->cell->value;
	if (data->elements >= data->count) {
		unsigned j, i = data->elements;
		GnmValDiff const cond = data->find_max ? IS_GREATER : IS_LESS;
		while (i-- > 0)
			if (value_compare (v, data->vals[i], TRUE) == cond) {
				for (j = 0; j < i ; j++)
					data->vals[j] = data->vals[j+1];
				data->vals[i] = v;
				break;
			}
	} else {
		data->vals [data->elements++] = v;
		if (data->elements == data->count) {
			qsort (data->vals, data->elements,
			       sizeof (GnmValue *),
			       data->find_max ? value_cmp : value_cmp_reverse);
		}
	}
	return NULL;
}

static GnmValue *
cb_hide_unwanted_items (GnmCellIter const *iter, FilterItems const *data)
{
	if (iter->cell != NULL) {
		int i = data->elements;
		GnmValue const *v = iter->cell->value;

		while (i-- > 0)
			if (data->vals[i] == v)
				return NULL;
	}
	colrow_set_visibility (data->target_sheet, FALSE, FALSE,
		iter->pp.eval.row, iter->pp.eval.row);
	return NULL;
}

/*****************************************************************************/

typedef struct {
	gboolean	 initialized, find_max;
	gnm_float	 low, high;
	Sheet		*target_sheet;
} FilterPercentage;

static GnmValue *
cb_filter_find_percentage (GnmCellIter const *iter, FilterPercentage *data)
{
	if (VALUE_IS_NUMBER (iter->cell->value)) {
		gnm_float const v = value_get_as_float (iter->cell->value);

		if (data->initialized) {
			if (data->low > v)
				data->low = v;
			else if (data->high < v)
				data->high = v;
		} else {
			data->initialized = TRUE;
			data->low = data->high = v;
		}
	}
	return NULL;
}

static GnmValue *
cb_hide_unwanted_percentage (GnmCellIter const *iter,
			     FilterPercentage const *data)
{
	if (iter->cell != NULL && VALUE_IS_NUMBER (iter->cell->value)) {
		gnm_float const v = value_get_as_float (iter->cell->value);
		if (data->find_max) {
			if (v >= data->high)
				return NULL;
		} else {
			if (v <= data->low)
				return NULL;
		}
	}
	colrow_set_visibility (data->target_sheet, FALSE, FALSE,
		iter->pp.eval.row, iter->pp.eval.row);
	return NULL;
}
/*****************************************************************************/

/**
 * gnm_filter_combo_apply :
 * @fcombo : #GnmFilterCombo
 * @target_sheet : @Sheet
 *
 **/
void
gnm_filter_combo_apply (GnmFilterCombo *fcombo, Sheet *target_sheet)
{
	GnmFilter const *filter;
	GnmFilterCondition const *cond;
	int col, start_row, end_row;
	CellIterFlags iter_flags = CELL_ITER_IGNORE_HIDDEN;

	g_return_if_fail (IS_GNM_FILTER_COMBO (fcombo));

	filter = fcombo->filter;
	cond = fcombo->cond;
	col = sheet_object_get_range (SHEET_OBJECT (fcombo))->start.col;
	start_row = filter->r.start.row + 1;
	end_row = filter->r.end.row;

	if (start_row > end_row ||
	    cond == NULL ||
	    cond->op[0] == GNM_FILTER_UNUSED)
		return;

	/*
	 * For the combo we filter a temporary sheet using the data from
	 * filter->sheet and need to include everything from the source,
	 * because it has a different set of conditions
	 */
	if (target_sheet != filter->sheet)
		iter_flags = CELL_ITER_ALL;

	if (0x10 >= (cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		FilterExpr data;
		data.cond = cond;
		data.target_sheet = target_sheet;
		filter_expr_init (&data, 0, cond, filter);
		if (cond->op[1] != GNM_FILTER_UNUSED)
			filter_expr_init (&data, 1, cond, filter);

		sheet_foreach_cell_in_range (filter->sheet,
			iter_flags,
			col, start_row, col, end_row,
			(CellIterFunc) cb_filter_expr, &data);

		filter_expr_release (&data, 0);
		if (cond->op[1] != GNM_FILTER_UNUSED)
			filter_expr_release (&data, 1);
	} else if (cond->op[0] == GNM_FILTER_OP_BLANKS)
		sheet_foreach_cell_in_range (filter->sheet,
			CELL_ITER_IGNORE_HIDDEN,
			col, start_row, col, end_row,
			(CellIterFunc) cb_filter_blanks, target_sheet);
	else if (cond->op[0] == GNM_FILTER_OP_NON_BLANKS)
		sheet_foreach_cell_in_range (filter->sheet,
			CELL_ITER_IGNORE_HIDDEN,
			col, start_row, col, end_row,
			(CellIterFunc) cb_filter_non_blanks, target_sheet);
	else if (0x30 == (cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		if (cond->op[0] & 0x2) { /* relative */
			FilterPercentage data;
			gnm_float	 offset;

			data.find_max = (cond->op[0] & 0x1) ? FALSE : TRUE;
			data.initialized = FALSE;
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN | CELL_ITER_IGNORE_BLANK,
				col, start_row, col, end_row,
				(CellIterFunc) cb_filter_find_percentage, &data);
			offset = (data.high - data.low) * cond->count / 100.;
			data.high -= offset;
			data.low  += offset;
			data.target_sheet = target_sheet;
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN,
				col, start_row, col, end_row,
				(CellIterFunc) cb_hide_unwanted_percentage, &data);
		} else { /* absolute */
			FilterItems data;
			data.find_max = (cond->op[0] & 0x1) ? FALSE : TRUE;
			data.elements    = 0;
			data.count  = cond->count;
			data.vals   = g_alloca (sizeof (GnmValue *) * data.count);
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN | CELL_ITER_IGNORE_BLANK,
				col, start_row, col, end_row,
				(CellIterFunc) cb_filter_find_items, &data);
			data.target_sheet = target_sheet;
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN,
				col, start_row, col, end_row,
				(CellIterFunc) cb_hide_unwanted_items, &data);
		}
	} else
		g_warning ("Invalid operator %d", cond->op[0]);
}

enum {
	COND_CHANGED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

typedef struct {
	SheetObjectClass parent;

	void (*cond_changed) (GnmFilterCombo *);
} GnmFilterComboClass;

static void
gnm_filter_combo_finalize (GObject *object)
{
	GnmFilterCombo *fcombo = GNM_FILTER_COMBO (object);
	GObjectClass *parent;
	if (fcombo->cond != NULL) {
		gnm_filter_condition_unref (fcombo->cond);
		fcombo->cond = NULL;
	}
	parent = g_type_class_peek (SHEET_OBJECT_TYPE);
	parent->finalize (object);
}

static void
gnm_filter_combo_init (SheetObject *so)
{
	/* keep the arrows from wandering with their cells */
	so->flags &= ~SHEET_OBJECT_MOVE_WITH_CELLS;
}
static SheetObjectView *
gnm_filter_combo_foo_view_new (SheetObject *so, SheetObjectViewContainer *container)
{
	return gnm_cell_combo_foo_view_new (so,
		gnm_filter_combo_foo_view_get_type (), container);
}
static void
gnm_filter_combo_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (gobject_class);

	/* Object class method overrides */
	gobject_class->finalize = gnm_filter_combo_finalize;

	/* SheetObject class method overrides */
	so_class->new_view	= gnm_filter_combo_foo_view_new;
	so_class->read_xml_dom  = NULL;
	so_class->write_xml_sax = NULL;
	so_class->prep_sax_parser = NULL;
	so_class->copy          = NULL;

	signals[COND_CHANGED] = g_signal_new ("cond-changed",
		 GNM_FILTER_COMBO_TYPE,
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET (GnmFilterComboClass, cond_changed),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__VOID,
		 G_TYPE_NONE, 0);
}

GSF_CLASS (GnmFilterCombo, gnm_filter_combo,
	   gnm_filter_combo_class_init, gnm_filter_combo_init,
	   SHEET_OBJECT_TYPE);

/*************************************************************************/

static void
gnm_filter_add_field (GnmFilter *filter, int i)
{
	/* pretend to fill the cell, then clip the X start later */
	static float const a_offsets [4] = { .0, .0, 1., 1. };
	int n;
	GnmRange tmp;
	SheetObjectAnchor anchor;
	GnmFilterCombo *fcombo = g_object_new (GNM_FILTER_COMBO_TYPE, NULL);

	fcombo->filter = filter;
	tmp.start.row = tmp.end.row = filter->r.start.row;
	tmp.start.col = tmp.end.col = filter->r.start.col + i;
	sheet_object_anchor_init (&anchor, &tmp, a_offsets,
		GOD_ANCHOR_DIR_DOWN_RIGHT);
	sheet_object_set_anchor (SHEET_OBJECT (fcombo), &anchor);
	sheet_object_set_sheet (SHEET_OBJECT (fcombo), filter->sheet);

	g_ptr_array_add (filter->fields, NULL);
	for (n = filter->fields->len; --n > i ; )
		g_ptr_array_index (filter->fields, n) =
			g_ptr_array_index (filter->fields, n-1);
	g_ptr_array_index (filter->fields, n) = fcombo;
	g_object_unref (G_OBJECT (fcombo));
}

/**
 * gnm_filter_new :
 * @sheet :
 * @r :
 *
 * Init a filter and add it to @sheet
 **/
GnmFilter *
gnm_filter_new (Sheet *sheet, GnmRange const *r)
{
	GnmFilter	*filter;
	int i;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	filter = g_new0 (GnmFilter, 1);
	filter->sheet = sheet;

	filter->is_active = FALSE;
	filter->r = *r;
	filter->fields = g_ptr_array_new ();

	for (i = 0 ; i < range_width (r); i++)
		gnm_filter_add_field (filter, i);

	sheet->filters = g_slist_prepend (sheet->filters, filter);
	sheet->priv->filters_changed = TRUE;

	return filter;
}

/**
 * gnm_filter_dup :
 * @src : #GnmFilter
 * @sheet : #Sheet
 *
 * Duplicate @src into @sheet
 **/
GnmFilter *
gnm_filter_dup (GnmFilter const *src, Sheet *sheet)
{
	int i;
	GnmFilter *dst;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	dst = g_new0 (GnmFilter, 1);
	dst->sheet = sheet;

	dst->is_active = src->is_active;
	dst->r = src->r;
	dst->fields = g_ptr_array_new ();

	for (i = 0 ; i < range_width (&dst->r); i++) {
		gnm_filter_add_field (dst, i);
		gnm_filter_set_condition (dst, i,
			gnm_filter_condition_dup (
				gnm_filter_get_condition (src, i)),
			FALSE);
	}

	sheet->filters = g_slist_prepend (sheet->filters, dst);
	sheet->priv->filters_changed = TRUE;

	return dst;
}
void
gnm_filter_free	(GnmFilter *filter)
{
	unsigned i;

	g_return_if_fail (filter != NULL);

	for (i = 0 ; i < filter->fields->len ; i++)
		sheet_object_clear_sheet (g_ptr_array_index (filter->fields, i));
	g_ptr_array_free (filter->fields, TRUE);

	filter->fields = NULL;
	g_free (filter);
}

void
gnm_filter_remove (GnmFilter *filter)
{
	Sheet *sheet;
	int i;

	g_return_if_fail (filter != NULL);
	g_return_if_fail (filter->sheet != NULL);

	sheet = filter->sheet;
	sheet->priv->filters_changed = TRUE;
	sheet->filters = g_slist_remove (sheet->filters, filter);
	for (i = filter->r.start.row; ++i <= filter->r.end.row ; ) {
		ColRowInfo *ri = sheet_row_get (sheet, i);
		if (ri != NULL) {
			ri->in_filter = FALSE;
			colrow_set_visibility (sheet, FALSE, TRUE, i, i);
		}
	}
}

/**
 * gnm_filter_get_condition :
 * @filter :
 * @i :
 *
 **/
GnmFilterCondition const *
gnm_filter_get_condition (GnmFilter const *filter, unsigned i)
{
	GnmFilterCombo *fcombo;

	g_return_val_if_fail (filter != NULL, NULL);
	g_return_val_if_fail (i < filter->fields->len, NULL);

	fcombo = g_ptr_array_index (filter->fields, i);
	return fcombo->cond;
}

/**
 * gnm_filter_set_condition :
 * @filter :
 * @i :
 * @cond : #GnmFilterCondition
 * @apply :
 *
 * Change the @i-th condition of @filter to @cond.  If @apply is
 * TRUE @filter is used to set the visibility of the rows in @filter::sheet
 *
 * Absorbs the reference to @cond.
 **/
void
gnm_filter_set_condition (GnmFilter *filter, unsigned i,
			  GnmFilterCondition *cond,
			  gboolean apply)
{
	GnmFilterCombo *fcombo;
	gboolean set_infilter = FALSE;
	gboolean existing_cond = FALSE;
	int r;

	g_return_if_fail (filter != NULL);
	g_return_if_fail (i < filter->fields->len);

	fcombo = g_ptr_array_index (filter->fields, i);

	if (fcombo->cond != NULL) {
		existing_cond = TRUE;
		gnm_filter_condition_unref (fcombo->cond);
	}
	fcombo->cond = cond;
	g_signal_emit (G_OBJECT (fcombo), signals [COND_CHANGED], 0);

	if (apply) {
		/* if there was an existing cond then we need to do
		 * 1) unfilter everything
		 * 2) reapply all the filters
		 * This is because we do record what elements this particular
		 * field filtered
		 */
		if (existing_cond) {
			colrow_set_visibility (filter->sheet, FALSE, TRUE,
				filter->r.start.row + 1, filter->r.end.row);
			for (i = 0 ; i < filter->fields->len ; i++)
				gnm_filter_combo_apply (g_ptr_array_index (filter->fields, i),
					filter->sheet);
		} else
			/* When adding a new cond all we need to do is
			 * apply that filter */
			gnm_filter_combo_apply (fcombo, filter->sheet);
	}

	/* set the activity flag and potentially activate the
	 * in_filter flags in the rows */
	if (cond == NULL) {
		for (i = 0 ; i < filter->fields->len ; i++) {
			fcombo = g_ptr_array_index (filter->fields, i);
			if (fcombo->cond != NULL)
				break;
		}
		if (i >= filter->fields->len) {
			filter->is_active = FALSE;
			set_infilter = TRUE;
		}
	} else if (!filter->is_active) {
		filter->is_active = TRUE;
		set_infilter = TRUE;
	}

	if (set_infilter)
		for (r = filter->r.start.row; ++r <= filter->r.end.row ; ) {
			ColRowInfo *ri = sheet_row_fetch (filter->sheet, r);
			ri->in_filter = filter->is_active;
		}
}

/**
 * gnm_filter_overlaps_range :
 * @filter : #GnmFilter
 * @r : #GnmRange
 *
 * Does the range filter by @filter overlap with GnmRange @r
 **/
gboolean
gnm_filter_overlaps_range (GnmFilter const *filter, GnmRange const *r)
{
	g_return_val_if_fail (filter != NULL, FALSE);

	return range_overlap (&filter->r, r);
}

/*************************************************************************/

static gboolean
sheet_cell_or_one_below_is_not_empty (Sheet *sheet, int col, int row)
{
	return !sheet_is_cell_empty (sheet, col, row) ||
		(row < (gnm_sheet_get_max_rows (sheet) - 1) &&
		 !sheet_is_cell_empty (sheet, col, row+1));
}

/**
 * gnm_sheet_filter_guess_region :
 * @sheet : #Sheet
 * @range : #GnmRange
 *
 **/
void
gnm_sheet_filter_guess_region (Sheet *sheet, GnmRange *region)
{
	int col;
	int end_row;
	int offset;

	/* check in case only one cell selected */
	if (region->start.col == region->end.col) {
		int start = region->start.col;
		/* look for previous empty column */
		for (col = start - 1; col > 0; col--)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
				break;
		region->start.col = col - 1;

		/* look for next empty column */
		for (col = start + 1; col < gnm_sheet_get_max_cols (sheet); col++)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
				break;
		region->end.col = col - 1;
	}

	/* find first and last non-empty cells in region */
	for (col = region->start.col; col <= region->end.col; col++)
		if (sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
			break;

	if (col > region->end.col)
		return; /* all empty -- give up */
	region->start.col = col;

	for (col = region->end.col; col >= region->start.col; col--)
		if (sheet_cell_or_one_below_is_not_empty(sheet, col, region->start.row))
			break;
	region->end.col = col;

	/* now find length of longest column */
	for (col = region->start.col; col <= region->end.col; col++) {
		offset = 0;
		if (sheet_is_cell_empty(sheet, col, region->start.row))
			offset = 1;
		end_row = sheet_find_boundary_vertical (sheet, col,
			region->start.row + offset, col, 1, TRUE);
		if (end_row > region->end.row)
			region->end.row = end_row;
	}
}

/**
 * gnm_sheet_filter_insdel_colrow :
 * @sheet :
 * @is_cols :
 * @is_insert :
 * @start :
 * @count :
 *
 * Adjust filters as necessary to handle col/row insertions and deletions
 **/
void
gnm_sheet_filter_insdel_colrow (Sheet *sheet,
				gboolean is_cols, gboolean is_insert,
				int start, int count)
{
	GSList *ptr, *filters;
	GnmFilter *filter;

	g_return_if_fail (IS_SHEET (sheet));

	filters = g_slist_copy (sheet->filters);
	for (ptr = filters; ptr != NULL ; ptr = ptr->next) {
		filter = ptr->data;

		if (is_cols) {
			if (start > filter->r.end.col)	/* a */
				continue;
			if (is_insert) {
				filter->r.end.col += count;
				/* inserting in the middle of a filter adds
				 * fields.  Everything else just moves it */
				if (start > filter->r.start.col &&
				    start <= filter->r.end.col) {
					while (count--)
						gnm_filter_add_field (filter,
							start - filter->r.start.col + count);
				} else
					filter->r.start.col += count;
			} else {
				int start_del = start - filter->r.start.col;
				int end_del   = start_del + count;
				if (start_del <= 0) {
					start_del = 0;
					if (end_del > 0)
						filter->r.start.col = start;	/* c */
					else
						filter->r.start.col -= count;	/* b */
					filter->r.end.col -= count;
				} else {
					if ((unsigned)end_del > filter->fields->len) {
						end_del = filter->fields->len;
						filter->r.end.col = start - 1;	/* d */
					} else
						filter->r.end.col -= count;
				}

				if (filter->r.end.col < filter->r.start.col)
					filter = NULL;
				else
					while (end_del-- > start_del)
						g_ptr_array_remove_index (filter->fields, end_del);
			}
		} else {
			if (start > filter->r.end.row)
				continue;
			if (is_insert) {
				filter->r.end.row += count;
				if (start < filter->r.start.row)
					filter->r.start.row += count;
			} else {
				if (start <= filter->r.start.row) {
					filter->r.end.row -= count;
					if ((start+count) > filter->r.start.row)
						/* delete if the dropdowns are wiped */
						filter->r.start.row = filter->r.end.row + 1;
					else
						filter->r.start.row -= count;
				} else if ((start+count) > filter->r.end.row)
					filter->r.end.row = start -1;
				else
					filter->r.end.row -= count;

				if (filter->r.end.row < filter->r.start.row)
					filter = NULL;
			}
		}

		if (filter == NULL) {
			filter = ptr->data; /* we used it as a flag */
			gnm_filter_remove (filter);
			/* the objects are already gone */
			g_ptr_array_set_size (filter->fields, 0);
			gnm_filter_free (filter);
		}
	}
	if (filters != NULL)
		sheet->priv->filters_changed = TRUE;
	g_slist_free (filters);
}
