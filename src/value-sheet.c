/*
 * value-sheet.c:  Utilies for sheet specific value handling
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 */
#include <config.h>
#include <locale.h>
#include "value.h"
#include "utils.h"
#include "symbol.h"
#include "eval.h"

#include <stdio.h>

/* Debugging utility to print a Value */
void
value_dump (const Value *value)
{
	switch (value->type){
	case VALUE_EMPTY:
		printf ("EMPTY\n");
		break;

	case VALUE_ERROR:
		printf ("ERROR: %s\n", value->v.error.mesg->str);
		break;

	case VALUE_BOOLEAN:
		printf ("BOOLEAN: %s\n", value->v.v_bool ?_("TRUE"):_("FALSE"));
		break;

	case VALUE_STRING:
		printf ("STRING: %s\n", value->v.str->str);
		break;

	case VALUE_INTEGER:
		printf ("NUM: %d\n", value->v.v_int);
		break;

	case VALUE_FLOAT:
		printf ("Float: %f\n", value->v.v_float);
		break;

	case VALUE_ARRAY: {
		int x, y;
		
		printf ("Array: { ");
		for (y = 0; y < value->v.array.y; y++)
			for (x = 0; x < value->v.array.x; x++)
				value_dump (value->v.array.vals [x][y]);
		printf ("}\n");
		break;
	}
	case VALUE_CELLRANGE: {
		CellRef const *c = &value->v.cell_range.cell_a;
		Sheet const *sheet = c->sheet;

		printf ("CellRange\n");
		if (sheet && sheet->name)
			printf ("'%s':", sheet->name);
		else
			printf ("%p :", sheet);
		printf ("%s%s%s%d\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), c->row+1);
		c = &value->v.cell_range.cell_b;
		if (sheet && sheet->name)
			printf ("'%s':", sheet->name);
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
encode_cellref (GString *dest, const CellRef *ref, gboolean use_relative_syntax)
{
	if (ref->sheet){
		g_string_append_c (dest, '\'');
		g_string_append (dest, ref->sheet->name);
		g_string_append (dest, "'!");
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
 * use_relative_syntax == TRUE: $a4:$b$1
 * use_relative_syntax == FALSE: a4:b1
 */
char *
value_cellrange_get_as_string (const Value *value, gboolean use_relative_syntax)
{
	GString *str;
	char *ans;
	CellRef const * a, * b;

	g_return_val_if_fail (value != NULL, NULL);
	g_return_val_if_fail (value->type == VALUE_CELLRANGE, NULL);

	a = &value->v.cell_range.cell_a;
	b = &value->v.cell_range.cell_b;

	str = g_string_new ("");
	encode_cellref (str, a, use_relative_syntax);

	if ((a->col != b->col) || (a->row != b->row) ||
	    (a->col_relative != b->col_relative) || (a->sheet != b->sheet)){
		g_string_append_c (str, ':');
		
		encode_cellref (str, b, use_relative_syntax);
	}
	ans = str->str;
	g_string_free (str, FALSE);
	return ans;
}

guint
value_area_get_width (const EvalPosition *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE, 1);

	if (v->type == VALUE_ARRAY)
		return v->v.array.x;
	else { /* FIXME: 3D references, may not clip correctly */
		Sheet *sheeta = v->v.cell_range.cell_a.sheet ?
			v->v.cell_range.cell_a.sheet:ep->sheet;
		guint ans = v->v.cell_range.cell_b.col -
			    v->v.cell_range.cell_a.col + 1;
		if (sheeta && sheeta->cols.max_used < ans) /* Clip */
			ans = sheeta->cols.max_used+1;
		return ans;
	}
}

guint
value_area_get_height (const EvalPosition *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE, 1);

	if (v->type == VALUE_ARRAY)
		return v->v.array.y;
	else { /* FIXME: 3D references, may not clip correctly */
		Sheet *sheeta = eval_sheet (v->v.cell_range.cell_a.sheet, ep->sheet);
		guint ans = v->v.cell_range.cell_b.row -
		            v->v.cell_range.cell_a.row + 1;
		if (sheeta && sheeta->rows.max_used < ans) /* Clip */
			ans = sheeta->rows.max_used + 1;
		return ans;
	}
}

Value const *
value_area_fetch_x_y (EvalPosition const *ep, Value const *v, guint x, guint y)
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
const Value *
value_area_get_x_y (EvalPosition const *ep, Value const *v, guint x, guint y)
{
	g_return_val_if_fail (v, NULL);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE,
			      NULL);

	if (v->type == VALUE_ARRAY){
		g_return_val_if_fail (x < v->v.array.x &&
				      y < v->v.array.y,
				      NULL);
		return v->v.array.vals [x][y];
	} else {
		CellRef const * const a = &v->v.cell_range.cell_a;
		CellRef const * const b = &v->v.cell_range.cell_b;
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

		/* Handle inverted refereneces */
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
	}
	
	return NULL;
}

typedef struct
{
	value_area_foreach_callback	 callback;
	EvalPosition const		*ep;
	void				*real_data;
} WrapperClosure;
	
static Value *
wrapper_foreach_cell_in_area_callback (Sheet *sheet, int col, int row,
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
value_area_foreach (EvalPosition const *ep, Value const *v,
		    value_area_foreach_callback callback,
		    void *closure)
{
	int x, y;
	Value *tmp;

	g_return_val_if_fail (callback != NULL, FALSE);

        if (v->type == VALUE_CELLRANGE)
	{
		WrapperClosure wrap;
		wrap.callback = callback;
		wrap.ep = ep;
		wrap.real_data = closure;
		return sheet_cell_foreach_range (
			eval_sheet (v->v.cell_range.cell_a.sheet, ep->sheet),
			TRUE,
			v->v.cell_range.cell_a.col,
			v->v.cell_range.cell_a.row,
			v->v.cell_range.cell_b.col,
			v->v.cell_range.cell_b.row,
			&wrapper_foreach_cell_in_area_callback,
			(void *)&wrap);
	}

	/* If not an array, apply callback to singleton */
        if (v->type != VALUE_ARRAY)
		return (*callback)(ep, v, closure);

	for (x = v->v.array.x; --x >= 0;)
		for (y = v->v.array.y; --y >= 0;)
			if ((tmp = (*callback)(ep, v->v.array.vals [x][y], closure)) != NULL)
				return tmp;

	return NULL;
}

/*
 * Initialize temporarily with statics.  The real versions from the locale
 * will be setup in constants_init
 */
char const *gnumeric_err_NULL  = "#NULL!";
char const *gnumeric_err_DIV0  = "#DIV/0!";
char const *gnumeric_err_VALUE = "#VALUE!";
char const *gnumeric_err_REF   = "#REF!";
char const *gnumeric_err_NAME  = "#NAME?";
char const *gnumeric_err_NUM   = "#NUM!";
char const *gnumeric_err_NA    = "#N/A";

static struct gnumeric_error_info
{
	char const *str;
	int len;
} gnumeric_error_data[7];

static char const *
gnumeric_error_init (int const indx, char const * str)
{
	g_return_val_if_fail (indx >= 0, str);
	g_return_val_if_fail (indx < sizeof(gnumeric_error_data)/sizeof(struct gnumeric_error_info), str);

	gnumeric_error_data[indx].str = str;
	gnumeric_error_data[indx].len = strlen(str);
	return str;
}

/*
 * value_is_error : Check to see if a string begins with one of the magic
 * error strings.
 *
 * @str : The string to test
 * @offset : A place to store the size of the leading error string if it
 *           exists.
 *
 * returns : an error if there is one, or NULL.
 */
Value *
value_is_error (char const * const str, int *offset)
{
	int i = sizeof(gnumeric_error_data)/sizeof(struct gnumeric_error_info);

	g_return_val_if_fail (str != NULL, NULL);

	while (--i >= 0) {
		int const len = gnumeric_error_data[i].len;
		if (strncmp (str, gnumeric_error_data[i].str, len) == 0) {
			*offset = len;
			return value_new_error (NULL, gnumeric_error_data[i].str);
		}
	}
	return NULL;
}

void
constants_init (void)
{
	int i = 0;

	symbol_install (global_symbol_table, "FALSE", SYMBOL_VALUE,
			value_new_bool (FALSE));
	symbol_install (global_symbol_table, "TRUE", SYMBOL_VALUE,
			value_new_bool (TRUE));
	symbol_install (global_symbol_table, "GNUMERIC_VERSION", SYMBOL_VALUE,
			value_new_float (atof (GNUMERIC_VERSION)));

	gnumeric_err_NULL	= gnumeric_error_init (i++, _("#NULL!"));
	gnumeric_err_DIV0	= gnumeric_error_init (i++, _("#DIV/0!"));
	gnumeric_err_VALUE	= gnumeric_error_init (i++, _("#VALUE!"));
	gnumeric_err_REF	= gnumeric_error_init (i++, _("#REF!"));
	gnumeric_err_NAME	= gnumeric_error_init (i++, _("#NAME?"));
	gnumeric_err_NUM	= gnumeric_error_init (i++, _("#NUM!"));
	gnumeric_err_NA		= gnumeric_error_init (i++, _("#N/A"));
}
