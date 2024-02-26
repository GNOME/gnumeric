/*
 * value-sheet.c:  Utilities for sheet specific value handling
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>

#include <value.h>
#include <gnm-format.h>
#include <ranges.h>
#include <sheet.h>
#include <cell.h>
#include <workbook.h>
#include <parse-util.h>
#include <goffice/goffice.h>

/* Debugging utility to print a GnmValue */
void
value_dump (GnmValue const *value)
{
	switch (value->v_any.type){
	case VALUE_EMPTY:
		g_print ("EMPTY\n");
		break;

	case VALUE_ERROR:
		g_print ("ERROR: %s\n", value->v_err.mesg->str);
		break;

	case VALUE_BOOLEAN:
		g_print ("BOOLEAN: %s\n", go_locale_boolean_name (value->v_bool.val));
		break;

	case VALUE_STRING:
		g_print ("STRING: %s\n", value->v_str.val->str);
		break;

	case VALUE_FLOAT:
		g_print ("NUMBER: %" GNM_FORMAT_f "\n", value_get_as_float (value));
		break;

	case VALUE_ARRAY: {
		int x, y;

		g_print ("Array: { ");
		for (y = 0; y < value->v_array.y; y++)
			for (x = 0; x < value->v_array.x; x++)
				value_dump (value->v_array.vals [x][y]);
		g_print ("}\n");
		break;
	}
	case VALUE_CELLRANGE: {
		/*
		 * Do NOT normalize the ranges.
		 * Lets see them in their inverted glory if need be.
		 */
		GnmCellRef const *c = &value->v_range.cell.a;
		Sheet const *sheet = c->sheet;

		g_print ("CellRange\n");
		if (sheet && sheet->name_unquoted)
			g_print ("%s:", sheet->name_quoted);
		else if (sheet)
			g_print ("%p :", (void *)sheet);
		g_print ("%s%s%s%s\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), row_name(c->row));
		c = &value->v_range.cell.b;
		if (sheet && sheet->name_quoted)
			g_print ("%s:", sheet->name_unquoted);
		else if (sheet)
			g_print ("%p :", (void *)sheet);
		g_print ("%s%s%s%s\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), row_name(c->row));
		break;
	}
	default:
		g_print ("Unhandled item type\n");
	}
}

int
value_area_get_width (GnmValue const *v, GnmEvalPos const *ep)
{
	g_return_val_if_fail (v, 0);

	if (VALUE_IS_CELLRANGE (v)) {
		GnmRange r;
		Sheet *start_sheet, *end_sheet;

		g_return_val_if_fail (ep, 0);
		gnm_rangeref_normalize (&v->v_range.cell, ep, &start_sheet, &end_sheet, &r);
		return range_width (&r);
	} else if (VALUE_IS_ARRAY (v))
		return v->v_array.x;
	return 1;
}

int
value_area_get_height (GnmValue const *v, GnmEvalPos const *ep)
{
	g_return_val_if_fail (v, 0);

	if (VALUE_IS_CELLRANGE (v)) {
		GnmRange r;
		Sheet *start_sheet, *end_sheet;

		g_return_val_if_fail (ep, 0);
		gnm_rangeref_normalize (&v->v_range.cell, ep, &start_sheet, &end_sheet, &r);
		return range_height (&r);
	} else if (VALUE_IS_ARRAY (v))
		return v->v_array.y;
	return 1;
}

/**
 * value_area_fetch_x_y:
 * @v: const #GnmValue *
 * @x: column
 * @y: row
 * @ep: const #GnmEvalPos *
 *
 * An internal routine to get a cell from an array or range.
 * Ensures that elements of CELLRANGE are evaluated
 *
 * Returns the element if it exists and is non-empty otherwise returns 0
 **/
GnmValue const *
value_area_fetch_x_y (GnmValue const *v, int x, int y, GnmEvalPos const *ep)
{
	GnmValue const * const res = value_area_get_x_y (v, x, y, ep);
	if (VALUE_IS_EMPTY (res))
		return value_zero;
	else
		return res;
}

/**
 * value_area_get_x_y:
 * @v: const #GnmValue *
 * @x: column
 * @y: row
 * @ep: const #GnmEvalPos *
 *
 * An internal routine to get a cell from an array or range.
 * Ensures that elements of CELLRANGE are evaluated
 *
 * If any problems occur a %NULL is returned.
 **/
GnmValue const *
value_area_get_x_y (GnmValue const *v, int x, int y, GnmEvalPos const *ep)
{
	g_return_val_if_fail (v, NULL);

	if (VALUE_IS_ARRAY (v)){
		g_return_val_if_fail (x < v->v_array.x &&
				      y < v->v_array.y,
				      NULL);
		return v->v_array.vals [x][y];
	} else if (VALUE_IS_CELLRANGE (v)) {
		GnmRange r;
		Sheet *start_sheet, *end_sheet;
		GnmCell *cell;

		gnm_rangeref_normalize (&v->v_range.cell, ep,
					&start_sheet, &end_sheet,
					&r);
		if (start_sheet != end_sheet)
			return NULL;

		// Full wrap-around
		x = (r.start.col + x) % gnm_sheet_get_max_cols (start_sheet);
		y = (r.start.row + y) % gnm_sheet_get_max_rows (start_sheet);

		/* Speedup */
		if (start_sheet->cols.max_used < x ||
		    start_sheet->rows.max_used < y)
			return value_new_empty ();

		cell = sheet_cell_get (start_sheet, x, y);
		if (cell != NULL) {
			gnm_cell_eval (cell);
			return cell->value;
		}

		return value_new_empty ();
	} else
		return v;
}

typedef struct {
	GnmValueIter	 v_iter;
	GnmValueIterFunc func;
	int base_col, base_row;
	gpointer  user_data;
} WrapperClosure;

static GnmValue *
cb_wrapper_foreach_cell_in_area (GnmCellIter const *iter, WrapperClosure *wrap)
{
	if (iter->cell != NULL) {
		gnm_cell_eval (iter->cell);
		wrap->v_iter.v = iter->cell->value;
	} else
		wrap->v_iter.v = NULL;
	wrap->v_iter.x		= iter->pp.eval.col - wrap->base_col;
	wrap->v_iter.y		= iter->pp.eval.row - wrap->base_row;
	wrap->v_iter.cell_iter	= iter;
	return (*wrap->func) (&wrap->v_iter, wrap->user_data);
}

/**
 * value_area_foreach:
 * @v: const #GnmValue
 * @ep: const #GnmEvalPos
 * @flags: #CellIterFlags
 * @func: (scope call): #GnmValueIterFunc
 * @user_data:
 *
 * For each existing element in an array or range, invoke the
 * callback routine.
 *
 * Returns:
 *    non-%NULL on error, or VALUE_TERMINATE if some the handler requested
 *    to stop (by returning non-%NULL).
 **/
GnmValue *
value_area_foreach (GnmValue const *v, GnmEvalPos const *ep,
		    CellIterFlags flags,
		    GnmValueIterFunc func,
		    gpointer user_data)
{
	GnmValueIter v_iter;
	GnmValue    *tmp;

	g_return_val_if_fail (func != NULL, NULL);

        if (VALUE_IS_CELLRANGE (v)) {
		WrapperClosure wrap;
		GnmRange  r;
		Sheet *start_sheet, *end_sheet;

		gnm_rangeref_normalize (&v->v_range.cell, ep, &start_sheet, &end_sheet, &r);

		wrap.v_iter.ep		= ep;
		wrap.v_iter.region	= v;
		wrap.func		= func;
		wrap.user_data		= user_data;
		wrap.base_col		= r.start.col;
		wrap.base_row	= r.start.row;
		return workbook_foreach_cell_in_range (ep, v, flags,
			(CellIterFunc) cb_wrapper_foreach_cell_in_area, &wrap);
	}

	v_iter.ep = ep;
	v_iter.region = v;
	v_iter.cell_iter = NULL;

	/* If not an array, apply func to singleton */
        if (!VALUE_IS_ARRAY (v)) {
		v_iter.x = v_iter.y = 0;
		v_iter.v = v;
		return (*func) (&v_iter, user_data);
	}

	for (v_iter.x = v->v_array.x; v_iter.x-- > 0;)
		for (v_iter.y = v->v_array.y; v_iter.y-- > 0;) {
			v_iter.v = v->v_array.vals [v_iter.x][v_iter.y];
			if ((tmp = (*func)(&v_iter, user_data)) != NULL)
				return tmp;
		}

	return NULL;
}
