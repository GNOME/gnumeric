/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * value-sheet.c:  Utilies for sheet specific value handling
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 *   Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"

#include "value.h"
#include "format.h"
#include "ranges.h"
#include "str.h"
#include "sheet.h"
#include "cell.h"
#include "workbook.h"
#include "parse-util.h"

/* Debugging utility to print a GnmValue */
void
value_dump (GnmValue const *value)
{
	switch (value->type){
	case VALUE_EMPTY:
		printf ("EMPTY\n");
		break;

	case VALUE_ERROR:
		printf ("ERROR: %s\n", value->v_err.mesg->str);
		break;

	case VALUE_BOOLEAN:
		printf ("BOOLEAN: %s\n", format_boolean (value->v_bool.val));
		break;

	case VALUE_STRING:
		printf ("STRING: %s\n", value->v_str.val->str);
		break;

	case VALUE_INTEGER:
		printf ("NUM: %d\n", value->v_int.val);
		break;

	case VALUE_FLOAT:
		printf ("Float: %" GNUM_FORMAT_f "\n", value->v_float.val);
		break;

	case VALUE_ARRAY: {
		int x, y;

		printf ("Array: { ");
		for (y = 0; y < value->v_array.y; y++)
			for (x = 0; x < value->v_array.x; x++)
				value_dump (value->v_array.vals [x][y]);
		printf ("}\n");
		break;
	}
	case VALUE_CELLRANGE: {
		/*
		 * Do NOT normalize the ranges.
		 * Lets see them in their inverted glory if need be.
		 */
		GnmCellRef const *c = &value->v_range.cell.a;
		Sheet const *sheet = c->sheet;

		printf ("CellRange\n");
		if (sheet && sheet->name_unquoted)
			printf ("%s:", sheet->name_quoted);
		else
			printf ("%p :", sheet);
		printf ("%s%s%s%s\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), row_name(c->row));
		c = &value->v_range.cell.b;
		if (sheet && sheet->name_quoted)
			printf ("%s:", sheet->name_unquoted);
		else
			printf ("%p :", sheet);
		printf ("%s%s%s%s\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), row_name(c->row));
		break;
	}
	default:
		printf ("Unhandled item type\n");
	}
}

int
value_area_get_width (GnmValue const *v, GnmEvalPos const *ep)
{
	g_return_val_if_fail (v, 0);

	if (v->type == VALUE_CELLRANGE) {
		GnmRangeRef const *r = &v->v_range.cell;
		int ans = r->b.col - r->a.col;

		if (r->a.col_relative) {
			if (!r->b.col_relative)
				ans -= ep->eval.col;
		} else if (r->b.col_relative)
			ans += ep->eval.col;
		if (ans < 0)
			ans = -ans;
		return ans + 1;
	} else if (v->type == VALUE_ARRAY)
		return v->v_array.x;
	return 1;
}

int
value_area_get_height (GnmValue const *v, GnmEvalPos const *ep)
{
	g_return_val_if_fail (v, 0);

	if (v->type == VALUE_CELLRANGE) {
		GnmRangeRef const *r = &v->v_range.cell;
		int ans = r->b.row - r->a.row;

		if (r->a.row_relative) {
			if (!r->b.row_relative)
				ans -= ep->eval.row;
		} else if (r->b.row_relative)
			ans += ep->eval.row;

		if (ans < 0)
			ans = -ans;
		return ans + 1;
	} else if (v->type == VALUE_ARRAY)
		return v->v_array.y;
	return 1;
}

GnmValue const *
value_area_fetch_x_y (GnmValue const *v, int x, int y, GnmEvalPos const *ep)
{
	GnmValue const * const res = value_area_get_x_y (v, x, y, ep);
	if (res && res->type != VALUE_EMPTY)
		return res;

	return value_zero;
}

/*
 * An internal routine to get a cell from an array or range.  If any
 * problems occur a NULL is returned.
 */
GnmValue const *
value_area_get_x_y (GnmValue const *v, int x, int y, GnmEvalPos const *ep)
{
	g_return_val_if_fail (v, NULL);

	if (v->type == VALUE_ARRAY){
		g_return_val_if_fail (x < v->v_array.x &&
				      y < v->v_array.y,
				      NULL);
		return v->v_array.vals [x][y];
	} else if (v->type == VALUE_CELLRANGE) {
		GnmCellRef const * const a = &v->v_range.cell.a;
		GnmCellRef const * const b = &v->v_range.cell.b;
		int a_col = a->col;
		int a_row = a->row;
		int b_col = b->col;
		int b_row = b->row;
		GnmCell *cell;
		Sheet *sheet;

		/* Handle relative references */
		if (a->col_relative)
			a_col += ep->eval.col;
		if (a->row_relative)
			a_row += ep->eval.row;
		if (b->col_relative)
			b_col += ep->eval.col;
		if (b->row_relative)
			b_row += ep->eval.row;

		/* Handle inverted references */
		if (a_row > b_row) {
			int tmp = a_row;
			a_row = b_row;
			b_row = tmp;
		}
		if (a_col > b_col) {
			int tmp = a_col;
			a_col = b_col;
			b_col = tmp;
		}

		a_col += x;
		a_row += y;

		/*
		 * FIXME FIXME FIXME
		 * This should return NA but some of the math functions may
		 * rely on this for now.
		 */
		g_return_val_if_fail (a_row<=b_row, NULL);
		g_return_val_if_fail (a_col<=b_col, NULL);

		sheet = eval_sheet (a->sheet, ep->sheet);

		g_return_val_if_fail (IS_SHEET (sheet), NULL);

		/* Speedup */
		if (sheet->cols.max_used < a_col ||
		    sheet->rows.max_used < a_row)
			return NULL;

		cell = sheet_cell_get (sheet, a_col, a_row);
		if (cell != NULL) {
			cell_eval (cell);
			return cell->value;
		}
	} else
		return v;

	return NULL;
}

typedef struct
{
	ValueAreaFunc  callback;
	GnmEvalPos const *ep;
	gpointer	  real_data;
	int base_col, base_row;
} WrapperClosure;

static GnmValue *
cb_wrapper_foreach_cell_in_area (Sheet *sheet, int col, int row,
				 GnmCell *cell, void *user_data)
{
	WrapperClosure *wrap = (WrapperClosure *)user_data;
	GnmValue const *v;
	if (cell != NULL) {
		cell_eval (cell);
		v = cell->value;
	} else
		v = NULL;
       	return (*wrap->callback) (v, wrap->ep,
		col - wrap->base_col, row - wrap->base_row, wrap->real_data);
}

/**
 * value_area_foreach:
 *
 * For each existing element in an array or range , invoke the
 * callback routine.
 *
 * Return value:
 *    non-NULL on error, or VALUE_TERMINATE if some invoked routine requested
 *    to stop (by returning non-NULL).
 **/
GnmValue *
value_area_foreach (GnmValue const *v, GnmEvalPos const *ep,
		    CellIterFlags flags,
		    ValueAreaFunc callback,
		    void *closure)
{
	int x, y;
	GnmValue *tmp;

	g_return_val_if_fail (callback != NULL, NULL);

        if (v->type == VALUE_CELLRANGE) {
		WrapperClosure wrap;
		GnmRange  r;
		Sheet *start_sheet, *end_sheet;

		rangeref_normalize (&v->v_range.cell, ep, &start_sheet, &end_sheet, &r);

		wrap.callback = callback;
		wrap.ep = ep;
		wrap.real_data = closure;
		wrap.base_col = r.start.col;
		wrap.base_row = r.start.row;
		return workbook_foreach_cell_in_range (
			ep, v, flags,
			&cb_wrapper_foreach_cell_in_area,
			(void *)&wrap);
	}

	/* If not an array, apply callback to singleton */
        if (v->type != VALUE_ARRAY)
		return (*callback) (v, ep, 0, 0, closure);

	for (x = v->v_array.x; --x >= 0;)
		for (y = v->v_array.y; --y >= 0;)
			if ((tmp = (*callback)(v->v_array.vals [x][y], ep, x, y, closure)) != NULL)
				return tmp;

	return NULL;
}

