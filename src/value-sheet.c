/*
 * value-sheet.c:  Utilies for sheet specific value handling
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"

#include "value.h"
#include "ranges.h"
#include "str.h"
#include "sheet.h"
#include "cell.h"
#include "workbook.h"
#include "parse-util.h"

/* Debugging utility to print a Value */
void
value_dump (Value const *value)
{
	switch (value->type){
	case VALUE_EMPTY:
		printf ("EMPTY\n");
		break;

	case VALUE_ERROR:
		printf ("ERROR: %s\n", value->v_err.mesg->str);
		break;

	case VALUE_BOOLEAN:
		printf ("BOOLEAN: %s\n", value->v_bool.val ?_("TRUE"):_("FALSE"));
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
		CellRef const *c = &value->v_range.cell.a;
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
value_area_get_width (EvalPos const *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);

	if (v->type == VALUE_CELLRANGE) {
		RangeRef const *r = &v->v_range.cell;
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
value_area_get_height (EvalPos const *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);

	if (v->type == VALUE_CELLRANGE) {
		RangeRef const *r = &v->v_range.cell;
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

Value const *
value_area_fetch_x_y (EvalPos const *ep, Value const *v, int x, int y)
{
	Value const * const res = value_area_get_x_y (ep, v, x, y);
	static Value *value_zero = NULL;
	if (res)
		return res;

	if (value_zero == NULL)
		value_zero = value_new_int (0);
	return value_zero;
}

/*
 * An internal routine to get a cell from an array or range.  If any
 * problems occur a NULL is returned.
 */
Value const *
value_area_get_x_y (EvalPos const *ep, Value const *v, int x, int y)
{
	g_return_val_if_fail (v, NULL);

	if (v->type == VALUE_ARRAY){
		g_return_val_if_fail (x < v->v_array.x &&
				      y < v->v_array.y,
				      NULL);
		return v->v_array.vals [x][y];
	} else if (v->type == VALUE_CELLRANGE) {
		CellRef const * const a = &v->v_range.cell.a;
		CellRef const * const b = &v->v_range.cell.b;
		int a_col = a->col;
		int a_row = a->row;
		int b_col = b->col;
		int b_row = b->row;
		Cell *cell;
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

		if (cell && cell->value)
			return cell->value;
	} else {
		return v;
	}

	return NULL;
}

typedef struct
{
	ValueAreaFunc  callback;
	EvalPos const *ep;
	void	      *real_data;
} WrapperClosure;

static Value *
cb_wrapper_foreach_cell_in_area (Sheet *sheet, int col, int row,
				 Cell *cell, void *user_data)
{
	WrapperClosure * wrap;
	if (cell == NULL || cell->value == NULL)
	        return NULL;

       	wrap = (WrapperClosure *)user_data;
       	return (*wrap->callback)(wrap->ep, cell->value, wrap->real_data);
}

/**
 * value_area_foreach:
 *
 * For each existing element in an array or range , invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 *
 * Return value:
 *    non-NULL on error, or VALUE_TERMINATE if some invoked routine requested
 *    to stop (by returning non-NULL).
 */
Value *
value_area_foreach (EvalPos const *ep, Value const *v,
		    ValueAreaFunc callback,
		    void *closure)
{
	int x, y;
	Value *tmp;

	g_return_val_if_fail (callback != NULL, FALSE);

        if (v->type == VALUE_CELLRANGE) {
		WrapperClosure wrap;
		wrap.callback = callback;
		wrap.ep = ep;
		wrap.real_data = closure;
		return workbook_foreach_cell_in_range (
			ep, v, CELL_ITER_IGNORE_BLANK,
			&cb_wrapper_foreach_cell_in_area,
			(void *)&wrap);
	}

	/* If not an array, apply callback to singleton */
        if (v->type != VALUE_ARRAY)
		return (*callback)(ep, v, closure);

	for (x = v->v_array.x; --x >= 0;)
		for (y = v->v_array.y; --y >= 0;)
			if ((tmp = (*callback)(ep, v->v_array.vals [x][y], closure)) != NULL)
				return tmp;

	return NULL;
}

/**
 * range_ref_normalize :  Take a range_ref from a Value and normalize it
 *     by converting to absolute coords and handling inversions.
 */
void
value_cellrange_normalize (EvalPos const *ep, Value const *ref,
			   Sheet **start_sheet, Sheet **end_sheet, Range *dest)
{
	g_return_if_fail (ref != NULL);
	g_return_if_fail (ref->type == VALUE_CELLRANGE);

	rangeref_normalize   (ep, &ref->v_range.cell,
			      start_sheet, end_sheet, dest);
}
