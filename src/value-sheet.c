/*
 * value-sheet.c:  Utilies for sheet specific value handling
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 */
#include <config.h>
#include "value.h"
#include "ranges.h"
#include "str.h"
#include "sheet.h"
#include "cell.h"
#include "workbook.h"
#include "parse-util.h"

#include <gnome.h>

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
		printf ("Float: %f\n", value->v_float.val);
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
		printf ("%s%s%s%d\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), c->row+1);
		c = &value->v_range.cell.b;
		if (sheet && sheet->name_quoted)
			printf ("%s:", sheet->name_unquoted);
		else
			printf ("%p :", sheet);
		printf ("%s%s%s%d\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), c->row+1);
		break;
	}
	default:
		printf ("Unhandled item type\n");
	}
}

static void
encode_cellref (GString *dest, CellRef const *ref, gboolean use_relative_syntax)
{
	if (ref->sheet){
		g_string_append (dest, ref->sheet->name_quoted);
		g_string_append_c (dest, '!');
	}
	if (use_relative_syntax && !ref->col_relative)
		g_string_append_c (dest, '$');
	g_string_append (dest, col_name (ref->col));

	if (use_relative_syntax && !ref->row_relative)
		g_string_append_c (dest, '$');
	g_string_sprintfa (dest, "%d", ref->row+1);
}


/*
 * value_cellrange_get_as_string:
 * @value: a value containing a VALUE_CELLRANGE
 * @use_relative_syntax: true if you want the result to contain relative indicators
 *
 * Returns: a string reprensenting the Value, for example:
 * use_relative_syntax == TRUE: $a$4:$b$1
 * use_relative_syntax == FALSE: a4:b1
 */
char *
value_cellrange_get_as_string (Value const *value, gboolean use_relative_syntax)
{
	GString *str;
	char *ans;
	CellRef const *a, *b;

	g_return_val_if_fail (value != NULL, NULL);
	g_return_val_if_fail (value->type == VALUE_CELLRANGE, NULL);

	a = &value->v_range.cell.a;
	b = &value->v_range.cell.b;

	str = g_string_new ("");
	encode_cellref (str, a, use_relative_syntax);

	/* FIXME : should we normalize ? */
	if ((a->col != b->col) || (a->row != b->row) ||
	    (a->col_relative != b->col_relative) || (a->sheet != b->sheet)){
		g_string_append_c (str, ':');

		encode_cellref (str, b, use_relative_syntax);
	}
	ans = str->str;
	g_string_free (str, FALSE);
	return ans;
}

int
value_area_get_width (EvalPos const *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);

	if (v->type == VALUE_CELLRANGE) {
		/* FIXME: 3D references, may not clip correctly */
		/*
		 * FIXME : should we normalize ?
		 *         should we handle relative references ?
		 *         inversions ??
		 */
		Sheet *sheeta = v->v_range.cell.a.sheet ?
			v->v_range.cell.a.sheet:ep->sheet;
		int ans = v->v_range.cell.b.col -
			  v->v_range.cell.a.col + 1;
		if (sheeta && sheeta->cols.max_used < ans) /* Clip */
			ans = sheeta->cols.max_used+1;
		return ans;
	} else if (v->type == VALUE_ARRAY) {
		return v->v_array.x;
	} else {
		return 1;
	}
}

int
value_area_get_height (EvalPos const *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);

	if (v->type == VALUE_CELLRANGE) {
		/* FIXME: 3D references, may not clip correctly */
		/*
		 * FIXME : should we normalize ?
		 *         should we handle relative references ?
		 *         inversions ??
		 */
		Sheet *sheeta = eval_sheet (v->v_range.cell.a.sheet, ep->sheet);
		int ans = v->v_range.cell.b.row -
			  v->v_range.cell.a.row + 1;
		if (sheeta && sheeta->rows.max_used < ans) /* Clip */
			ans = sheeta->rows.max_used + 1;
		return ans;
	} else if (v->type == VALUE_ARRAY) {
		return v->v_array.y;
	} else {
		return 1;
	}
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

		sheet = a->sheet?a->sheet:ep->sheet;
		g_return_val_if_fail (sheet != NULL, NULL);

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
 *    non-NULL on error, or value_terminate() if some invoked routine requested
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
			ep, v, TRUE,
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
	g_return_if_fail (ep != NULL);
	g_return_if_fail (ref->type == VALUE_CELLRANGE);

	cell_get_abs_col_row (&ref->v_range.cell.a, &ep->eval,
			      &dest->start.col, &dest->start.row);
	cell_get_abs_col_row (&ref->v_range.cell.b, &ep->eval,
			      &dest->end.col, &dest->end.row);
	range_normalize (dest);

	*start_sheet = eval_sheet (ref->v_range.cell.a.sheet, ep->sheet);
	*end_sheet = eval_sheet (ref->v_range.cell.b.sheet, ep->sheet);
}
