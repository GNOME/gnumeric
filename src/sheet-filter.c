/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-filter.c: support for 'auto-filters'
 *
 * Copyright (C) 2002-2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include "number-match.h"
#include "gutils.h"
#include "sheet-object.h"
#include "gnm-filter-combo-view.h"
#include "gnm-cell-combo-view.h"
#include <gsf/gsf-impl-utils.h>

#include <glib/gi18n-lib.h>
#include <stdlib.h>
#include <string.h>

static gboolean
gnm_filter_op_needs_value (GnmFilterOp op)
{
	switch (op & GNM_FILTER_OP_TYPE_MASK) {
	case GNM_FILTER_OP_TYPE_OP:
	case GNM_FILTER_OP_TYPE_BUCKETS:
	case GNM_FILTER_OP_TYPE_MATCH:
		return TRUE;
	default:
		g_assert_not_reached ();
	case GNM_FILTER_OP_TYPE_BLANKS:
	case GNM_FILTER_OP_TYPE_AVERAGE:
	case GNM_FILTER_OP_TYPE_STDDEV:
		return FALSE;
	}
}


/**
 * gnm_filter_condition_new_single :
 * @op: #GnmFilterOp
 * @v: #GnmValue
 *
 * Create a new condition with 1 value.
 * Absorbs the reference to @v.
 **/
GnmFilterCondition *
gnm_filter_condition_new_single (GnmFilterOp op, GnmValue *v)
{
	GnmFilterCondition *res;

	g_return_val_if_fail ((v != NULL) == gnm_filter_op_needs_value (op), NULL);

	res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = op;	res->op[1] = GNM_FILTER_UNUSED;
	res->value[0] = v;
	return res;
}

/**
 * gnm_filter_condition_new_double :
 * @op0: #GnmFilterOp
 * @v0: #GnmValue
 * @join_with_and:
 * @op1: #GnmFilterOp
 * @v1: #GnmValue
 *
 * Create a new condition with 2 value.
 * Absorbs the reference to @v0 and @v1.
 **/
GnmFilterCondition *
gnm_filter_condition_new_double (GnmFilterOp op0, GnmValue *v0,
				 gboolean join_with_and,
				 GnmFilterOp op1, GnmValue *v1)
{
	GnmFilterCondition *res;

	g_return_val_if_fail ((v0 != NULL) == gnm_filter_op_needs_value (op0), NULL);
	g_return_val_if_fail ((v1 != NULL) == gnm_filter_op_needs_value (op1), NULL);

	res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = op0;	res->op[1] = op1;
	res->is_and = join_with_and;
	res->value[0] = v0;	res->value[1] = v1;
	return res;
}

GnmFilterCondition *
gnm_filter_condition_new_bucket (gboolean top, gboolean absolute,
				 gboolean rel_range, double n)
{
	GnmFilterCondition *res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = GNM_FILTER_OP_TOP_N | (top ? 0 : 1) |
		(absolute ? 0 : (rel_range ? 2 : 4));
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
gnm_filter_condition_free (GnmFilterCondition *cond)
{
	if (cond == NULL)
		return;

	value_release (cond->value[0]);
	value_release (cond->value[1]);
	g_free (cond);
}

GType
gnm_filter_condition_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmFilterCondition",
			 (GBoxedCopyFunc)gnm_filter_condition_dup,
			 (GBoxedFreeFunc)gnm_filter_condition_free);
	}
	return t;
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
		    gnm_regcomp_XL (fexpr->regexp + i, str, GO_REG_ICASE, TRUE, TRUE) == GO_REG_OK) {
			/* FIXME: Do we want to anchor at the end above?  */
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
	if (fexpr->val[i] == NULL)
		go_regfree (fexpr->regexp + i);
	else
		value_release (fexpr->val[i]);
}

static char *
filter_cell_contents (GnmCell *cell)
{
	GOFormat const *format = gnm_cell_get_format (cell);
	GODateConventions const *date_conv =
		workbook_date_conv (cell->base.sheet->workbook);
	return format_value (format, cell->value, -1, date_conv);
}

static gboolean
filter_expr_eval (GnmFilterOp op, GnmValue const *src, GORegexp const *regexp,
		  GnmCell *cell)
{
	GnmValue *target = cell->value;
	GnmValDiff cmp;
	GnmValue *fake_val = NULL;

	if (src == NULL) {
		char *str = filter_cell_contents (cell);
		GORegmatch rm;
		int res = go_regexec (regexp, str, 1, &rm, 0);
		gboolean whole = (rm.rm_so == 0 && str[rm.rm_eo] == 0);

		g_free (str);

		switch (res) {
		case GO_REG_OK:
			if (whole)
				return op == GNM_FILTER_OP_EQUAL;
			/* fall through */

		case GO_REG_NOMATCH:
			return op == GNM_FILTER_OP_NOT_EQUAL;

		default:
			g_warning ("Unexpected regexec result");
			return FALSE;
		}
	}

	if (VALUE_IS_STRING (target) && VALUE_IS_NUMBER (src)) {
		GODateConventions const *date_conv =
			workbook_date_conv (cell->base.sheet->workbook);
		char *str = format_value (NULL, src, -1, date_conv);
		fake_val = value_new_string_nocopy (str);
		src = fake_val;
	}

	cmp = value_compare (target, src, FALSE);
	value_release (fake_val);

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

int
gnm_filter_combo_index (GnmFilterCombo *fcombo)
{
	g_return_val_if_fail (IS_GNM_FILTER_COMBO (fcombo), 0);

	return (sheet_object_get_range (SHEET_OBJECT (fcombo))->start.col -
		fcombo->filter->r.start.col);
}


/**
 * gnm_filter_combo_apply :
 * @fcombo: #GnmFilterCombo
 * @target_sheet: @Sheet
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
		if (cond->op[0] & GNM_FILTER_OP_PERCENT_MASK) { /* relative */
			if (cond->op[0] & GNM_FILTER_OP_REL_N_MASK) {
				FilterItems data;
				data.find_max = (cond->op[0] & 0x1) ? FALSE : TRUE;
				data.elements    = 0;
				data.count  = 0.5 + cond->count * (end_row - start_row + 1) /100.;
				if (data.count < 1)
					data.count = 1;
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
			} else {
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
			}
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

	gnm_filter_condition_free (fcombo->cond);
	fcombo->cond = NULL;

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
gnm_filter_combo_view_new (SheetObject *so, SheetObjectViewContainer *container)
{
	return gnm_cell_combo_view_new (so,
		gnm_filter_combo_view_get_type (), container);
}
static void
gnm_filter_combo_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (gobject_class);

	/* Object class method overrides */
	gobject_class->finalize = gnm_filter_combo_finalize;

	/* SheetObject class method overrides */
	so_class->new_view	= gnm_filter_combo_view_new;
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
	   SHEET_OBJECT_TYPE)

/*************************************************************************/

static void
gnm_filter_add_field (GnmFilter *filter, int i)
{
	/* pretend to fill the cell, then clip the X start later */
	static double const a_offsets[4] = { .0, .0, 1., 1. };
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
			g_ptr_array_index (filter->fields, n - 1);
	g_ptr_array_index (filter->fields, n) = fcombo;
	/* We hold a reference to fcombo */
}

void
gnm_filter_attach (GnmFilter *filter, Sheet *sheet)
{
	int i;

	g_return_if_fail (filter != NULL);
	g_return_if_fail (filter->sheet == NULL);
	g_return_if_fail (IS_SHEET (sheet));

	gnm_filter_ref (filter);

	filter->sheet = sheet;
	sheet->filters = g_slist_prepend (sheet->filters, filter);
	sheet->priv->filters_changed = TRUE;

	for (i = 0 ; i < range_width (&(filter->r)); i++)
		gnm_filter_add_field (filter, i);
}


/**
 * gnm_filter_new :
 * @sheet:
 * @r:
 *
 * Init a filter and add it to @sheet
 **/
GnmFilter *
gnm_filter_new (Sheet *sheet, GnmRange const *r)
{
	GnmFilter	*filter;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	filter = g_new0 (GnmFilter, 1);

	filter->is_active = FALSE;
	filter->r = *r;
	filter->fields = g_ptr_array_new ();

	/* This creates the initial ref.  */
	gnm_filter_attach (filter, sheet);

	return filter;
}

/**
 * gnm_filter_dup :
 * @src: #GnmFilter
 * @sheet: #Sheet
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

	dst->is_active = src->is_active;
	dst->r = src->r;
	dst->fields = g_ptr_array_new ();

	/* This creates the initial ref.  */
	gnm_filter_attach (dst, sheet);

	for (i = 0 ; i < range_width (&dst->r); i++) {
		gnm_filter_add_field (dst, i);
		gnm_filter_set_condition (dst, i,
			gnm_filter_condition_dup (
				gnm_filter_get_condition (src, i)),
			FALSE);
	}

	return dst;
}

GnmFilter *
gnm_filter_ref (GnmFilter *filter)
{
	g_return_val_if_fail (filter != NULL, NULL);
	filter->ref_count++;
	return filter;
}

void
gnm_filter_unref (GnmFilter *filter)
{
	g_return_if_fail (filter != NULL);

	filter->ref_count--;
	if (filter->ref_count > 0)
		return;

	g_ptr_array_free (filter->fields, TRUE);
	g_free (filter);
}

GType
gnm_filter_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmFilter",
			 (GBoxedCopyFunc)gnm_filter_ref,
			 (GBoxedFreeFunc)gnm_filter_unref);
	}
	return t;
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
	filter->sheet = NULL;

	for (i = 0 ; i < (int)filter->fields->len ; i++) {
		SheetObject *so = g_ptr_array_index (filter->fields, i);
		sheet_object_clear_sheet (so);
		g_object_unref (so);
	}
	g_ptr_array_set_size (filter->fields, 0);
}

/**
 * gnm_filter_get_condition :
 * @filter:
 * @i:
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

void
gnm_filter_reapply (GnmFilter *filter)
{
	unsigned i;

	colrow_set_visibility (filter->sheet, FALSE, TRUE,
			       filter->r.start.row + 1, filter->r.end.row);
	for (i = 0 ; i < filter->fields->len ; i++)
		gnm_filter_combo_apply (g_ptr_array_index (filter->fields, i),
					filter->sheet);
}

static void
gnm_filter_update_active (GnmFilter *filter)
{
	unsigned i;
	gboolean old_active = filter->is_active;

	filter->is_active = FALSE;
	for (i = 0 ; i < filter->fields->len ; i++) {
		GnmFilterCombo *fcombo = g_ptr_array_index (filter->fields, i);
		if (fcombo->cond != NULL) {
			filter->is_active = TRUE;
			break;
		}
	}

	if (filter->is_active != old_active) {
		int r;
		for (r = filter->r.start.row; ++r <= filter->r.end.row ; ) {
			ColRowInfo *ri = sheet_row_fetch (filter->sheet, r);
			ri->in_filter = filter->is_active;
		}
	}
}


/**
 * gnm_filter_set_condition :
 * @filter:
 * @i:
 * @cond: #GnmFilterCondition
 * @apply:
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
	gboolean existing_cond = FALSE;

	g_return_if_fail (filter != NULL);
	g_return_if_fail (i < filter->fields->len);

	fcombo = g_ptr_array_index (filter->fields, i);

	if (fcombo->cond != NULL) {
		existing_cond = TRUE;
		gnm_filter_condition_free (fcombo->cond);
	}
	fcombo->cond = cond;
	g_signal_emit (G_OBJECT (fcombo), signals [COND_CHANGED], 0);

	if (apply) {
		/* if there was an existing cond then we need to do
		 * redo the whole filter.
		 * This is because we do not record what elements this
		 * particular field filtered
		 */
		if (existing_cond)
			gnm_filter_reapply (filter);
		else
			/* When adding a new cond all we need to do is
			 * apply that filter */
			gnm_filter_combo_apply (fcombo, filter->sheet);
	}

	gnm_filter_update_active (filter);
}

/**
 * gnm_filter_overlaps_range :
 * @filter: #GnmFilter
 * @r: #GnmRange
 *
 * Returns: %TRUE if @filter overlaps @r.
 **/
static gboolean
gnm_filter_overlaps_range (GnmFilter const *filter, GnmRange const *r)
{
	g_return_val_if_fail (filter != NULL, FALSE);
	g_return_val_if_fail (r != NULL, FALSE);

	return range_overlap (&filter->r, r);
}

/*************************************************************************/

/**
 * gnm_sheet_filter_at_pos :
 * @sheet: #Sheet
 *
 * Returns : #GnmRange
 **/
GnmFilter *
gnm_sheet_filter_at_pos (Sheet const *sheet, GnmCellPos const *pos)
{
	GSList *ptr;
	GnmRange r;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (NULL != pos, NULL);

	range_init_cellpos (&r, pos);
	for (ptr = sheet->filters; ptr != NULL ; ptr = ptr->next)
		if (gnm_filter_overlaps_range (ptr->data, &r))
			return ptr->data;

	return NULL;
}

/**
 * gnm_sheet_filter_intersect_rows:
 * @sheet:
 * @from: starting row number
 * @to: ending row number
 *
 * Returns: the filter, if any, that intersect the rows @from to @to
 **/
GnmFilter *
gnm_sheet_filter_intersect_rows (Sheet const *sheet, int from, int to)
{
	GSList *ptr;
	GnmRange r;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	range_init_rows (&r, sheet, from, to);
	for (ptr = sheet->filters; ptr != NULL ; ptr = ptr->next)
		if (gnm_filter_overlaps_range (ptr->data, &r))
			return ptr->data;

	return NULL;
}

GnmRange *
gnm_sheet_filter_can_be_extended (G_GNUC_UNUSED Sheet const *sheet,
				  GnmFilter const *f, GnmRange const *r)
{
	if (r->start.row < f->r.start.row || r->end.row > f->r.end.row)
		return NULL;
	if ((r->end.col > f->r.end.col) ||
	    (r->start.col < f->r.start.col)) {
		GnmRange *res = g_new (GnmRange, 1);
		*res = range_union (&f->r, r);
		return res;
	}
	return NULL;
}


/*************************************************************************/

struct cb_remove_col_undo {
	unsigned col;
	GnmFilterCondition *cond;
};

static void
cb_remove_col_undo_free (struct cb_remove_col_undo *r)
{
	gnm_filter_condition_free (r->cond);
	g_free (r);
}

static void
cb_remove_col_undo (GnmFilter *filter, struct cb_remove_col_undo *r,
		    G_GNUC_UNUSED gpointer data)
{
	while (filter->fields->len <= r->col)
		gnm_filter_add_field (filter, filter->fields->len);
	gnm_filter_set_condition (filter, r->col,
				  gnm_filter_condition_dup (r->cond),
				  FALSE);
}

static void
remove_col (GnmFilter *filter, unsigned col, GOUndo **pundo)
{
	GnmFilterCombo *fcombo = g_ptr_array_index (filter->fields, col);
	if (pundo) {
		struct cb_remove_col_undo *r = g_new (struct cb_remove_col_undo, 1);
		GOUndo *u;

		r->col = col;
		r->cond = gnm_filter_condition_dup (fcombo->cond);
		u = go_undo_binary_new
			(gnm_filter_ref (filter), r,
			 (GOUndoBinaryFunc)cb_remove_col_undo,
			 (GFreeFunc)gnm_filter_unref,
			 (GFreeFunc)cb_remove_col_undo_free);
		*pundo = go_undo_combine (*pundo, u);
	}
	g_object_unref (fcombo);
	g_ptr_array_remove_index (filter->fields, col);
}

static void
gnm_filter_set_range (GnmFilter *filter, GnmRange *r)
{
	GnmRange old_r = filter->r;
	int i;
	int start = r->start.col;

	filter->r = *r;
	for (i = start; i < old_r.start.col; i++)
		gnm_filter_add_field (filter, i - start);
	for (i = old_r.end.col + 1; i <= r->end.col; i++)
		gnm_filter_add_field (filter, i - start);
}

/**
 * gnm_sheet_filter_insdel_colrow :
 * @sheet:
 * @is_cols:
 * @is_insert:
 * @start:
 * @count:
 * @pundo: location to store undo closures.
 *
 * Adjust filters as necessary to handle col/row insertions and deletions
 **/
void
gnm_sheet_filter_insdel_colrow (Sheet *sheet,
				gboolean is_cols, gboolean is_insert,
				int start, int count,
				GOUndo **pundo)
{
	GSList *ptr, *filters;

	g_return_if_fail (IS_SHEET (sheet));

	filters = g_slist_copy (sheet->filters);
	for (ptr = filters; ptr != NULL ; ptr = ptr->next) {
		GnmFilter *filter = ptr->data;
		gboolean kill_filter = FALSE;
		gboolean reapply_filter = FALSE;
		GnmRange r = filter->r;

		if (is_cols) {
			if (start > filter->r.end.col)	/* a */
				continue;

			sheet->priv->filters_changed = TRUE;

			if (is_insert) {
				/* INSERTING COLUMNS */
				filter->r.end.col += count;
				/* inserting in the middle of a filter adds
				 * fields.  Everything else just moves it */
				if (start > filter->r.start.col &&
				    start <= filter->r.end.col) {
					int i;
					for (i = 0; i < count; i++)
						gnm_filter_add_field (filter,
							start - filter->r.start.col + i);
				} else
					filter->r.start.col += count;
			} else {
				/* REMOVING COLUMNS */
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
					kill_filter = TRUE;
				else {
					while (end_del-- > start_del) {
						remove_col (filter, end_del, pundo);
						reapply_filter = TRUE;
					}
				}
			}
		} else {
			if (start > filter->r.end.row)
				continue;

			sheet->priv->filters_changed = TRUE;

			if (is_insert) {
				/* INSERTING ROWS */
				filter->r.end.row += count;
				if (start < filter->r.start.row)
					filter->r.start.row += count;
			} else {
				/* REMOVING ROWS */
				if (start <= filter->r.start.row) {
					filter->r.end.row -= count;
					if (start + count > filter->r.start.row)
						/* delete if the dropdowns are wiped */
						filter->r.start.row = filter->r.end.row + 1;
					else
						filter->r.start.row -= count;
				} else if (start + count > filter->r.end.row)
					filter->r.end.row = start -1;
				else
					filter->r.end.row -= count;

				if (filter->r.end.row < filter->r.start.row)
					kill_filter = TRUE;
			}
		}

		if (kill_filter) {
			/*
			 * Empty the filter as we need fresh combo boxes
			 * if we undo.
			 */
			while (filter->fields->len)
				remove_col (filter,
					    filter->fields->len - 1,
					    pundo);

			/* Restore the filters range */
			gnm_filter_remove (filter);
			filter->r = r;

			if (pundo) {
				GOUndo *u = go_undo_binary_new
					(gnm_filter_ref (filter),
					 sheet,
					 (GOUndoBinaryFunc)gnm_filter_attach,
					 (GFreeFunc)gnm_filter_unref,
					 NULL);
				*pundo = go_undo_combine (*pundo, u);
			}
			gnm_filter_unref (filter);
		} else if (reapply_filter) {
			GnmRange *range = g_new (GnmRange, 1);
			*range = r;
			if (pundo) {
				GOUndo *u = go_undo_binary_new
					(gnm_filter_ref (filter),
					 range,
					 (GOUndoBinaryFunc)gnm_filter_set_range,
					 (GFreeFunc)gnm_filter_unref,
					 g_free);
				*pundo = go_undo_combine (*pundo, u);
			}
			gnm_filter_update_active (filter);
			gnm_filter_reapply (filter);
		}
	}

	g_slist_free (filters);
}

