/*
 * fn-lookup.c:  Built in lookup functions and functions registration
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static char *help_vlookup = {
	N_("@FUNCTION=VLOOKUP\n"
	   "@SYNTAX=VLOOKUP(value,range,column,[approximate])\n"

	   "@DESCRIPTION="
	   "The VLOOKUP function finds the row in range that has a first column similar to value. "
	   "if approximate is not true it finds the row with an exact equivilance. "
	   "if approximate is true, then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the row with value less than value. "
	   "it returns the value in the row found at a 1 based offset in column columns into the range."
	   "\n"
	   "Returns #NUM! if column < 0."
	   "Returns #REF! if column falls outside range."
	   "\n"
	   "@SEEALSO=HLOOKUP")
};

static int
lookup_similar (Value *data, Value *templ, Value *next_largest, int approx)
{
	int ans ;

	g_return_val_if_fail (data != NULL, 0) ;
	g_return_val_if_fail (templ != NULL, 0) ;

	switch (templ->type)
	{
	case VALUE_INTEGER:
	case VALUE_FLOAT:
		{
			float_t a,b ;
			a = value_get_as_double (data) ;
			b = value_get_as_double (templ) ;
/*			printf ("Num: %f %f %f\n", a, b, next_largest?value_get_as_double(next_largest):9999.0) ; */
			if (a == b)
				return 1 ;
			else if (approx && a < b) {
				if (!next_largest)
					return -1 ;
				else if (value_get_as_double(next_largest) <= a)
					return -1 ;
			}
			return 0 ;
			break ;
		}
	case VALUE_STRING:
	default:
		{
			char *a, *b ;
			a = value_string (data) ;
			b = value_string (templ) ;
			if (approx)
			{
				ans = strcasecmp (a,b) ;
				if (approx && ans < 0) {
					if (next_largest) {
						char *c = value_string (next_largest) ;
						int cmp = strcasecmp(a,c) ;
						g_free (c) ;
						if (cmp >= 0)
							return -1 ;
					}
					else
						return -1 ;
				}
			}
			else
				ans = strcmp (a,b) ;
			g_free (a) ;
			g_free (b) ;
			return (ans==0) ;
			break ;
		}
	}
	return 0 ;
}

static Value *
gnumeric_vlookup (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	CellRef *a, *b ;
	Value *next_largest = NULL ;
	int height, lp, approx, col_idx, next_largest_row=0 ;
	
	a = &argv[1]->v.cell_range.cell_a ;
	b = &argv[1]->v.cell_range.cell_b ;
	g_return_val_if_fail (a->sheet != NULL, NULL) ;
/* a->sheet must be used as inter-sheet references specify the other sheet in 'a' */
/*	g_return_val_if_fail (a->sheet == b->sheet, NULL) ; */
	g_return_val_if_fail (!a->col_relative, NULL) ;
	g_return_val_if_fail (!b->col_relative, NULL) ;
	g_return_val_if_fail (!a->row_relative, NULL) ;
	g_return_val_if_fail (!b->row_relative, NULL) ;
	g_return_val_if_fail (a->col<=b->col, NULL) ;
	g_return_val_if_fail (a->row<=b->row, NULL) ;

	col_idx = value_get_as_int (argv[2]) ;
	if (col_idx<=0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	if (col_idx>b->col-a->col+1) {
		*error_string = _("#REF!") ;
		return NULL ;
	}

	if (argv[3]) {
		int err ;
		approx = value_get_bool (argv[3], &err) ;
		if (err) {
			*error_string = _("#VALUE!") ;
			return NULL ;
		}
	}
	else
		approx = 1 ;

	height = b->row - a->row + 1 ;
	for (lp=0;lp<height;lp++) {
		int compare ;
		Cell *cell = sheet_cell_get (a->sheet, a->col, a->row+lp) ;

		g_return_val_if_fail (cell != NULL, NULL) ;
		g_return_val_if_fail (cell->value != NULL, NULL) ;

		compare = lookup_similar (cell->value, argv[0], next_largest, approx) ;
/*		printf ("Compare '%s' with '%s' : %d (%d)\n", value_string(cell->value), value_string(argv[0]), compare, approx) ; */
		if (compare == 1) {
			Cell *cell = sheet_cell_get (a->sheet, a->col+col_idx-1, a->row+lp) ;
			g_return_val_if_fail (cell != NULL, NULL) ;
			g_return_val_if_fail (cell->value != NULL, NULL) ;
			return value_duplicate (cell->value) ;
		}
		if (compare < 0) {
			next_largest = cell->value ;
			next_largest_row = lp ;
		}
	}
	if (approx && next_largest) {
		Cell *cell = sheet_cell_get (a->sheet, a->col+col_idx-1, a->row+next_largest_row) ;
		g_return_val_if_fail (cell != NULL, NULL) ;
		g_return_val_if_fail (cell->value != NULL, NULL) ;
		return value_duplicate (cell->value) ;
	}
	else
		*error_string = _("#N/A") ;

	return NULL ;
}

static char *help_hlookup = {
	N_("@FUNCTION=HLOOKUP\n"
	   "@SYNTAX=HLOOKUP(value,range,row,[approximate])\n"

	   "@DESCRIPTION="
	   "The HLOOKUP function finds the col in range that has a first row cell similar to value. "
	   "if approximate is not true it finds the col with an exact equivilance. "
	   "if approximate is true, then the values must be sorted in order of ascending value for "
	   "correct function; in this case it finds the col with value less than value. "
	   "it returns the value in the col found at a 1 based offset in row rows into the range."
	   "\n"
	   "Returns #NUM! if row < 0."
	   "Returns #REF! if row falls outside range."
	   "\n"
	   "@SEEALSO=VLOOKUP")
};

static Value *
gnumeric_hlookup (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	CellRef *a, *b ;
	Value *next_largest = NULL ;
	int height, lp, approx, row_idx, next_largest_col=0 ;
	
	a = &argv[1]->v.cell_range.cell_a ;
	b = &argv[1]->v.cell_range.cell_b ;
	g_return_val_if_fail (a->sheet != NULL, NULL) ;
/* a->sheet must be used as inter-sheet references specify the other sheet in 'a' */
/*	g_return_val_if_fail (a->sheet == b->sheet, NULL) ; */
	g_return_val_if_fail (!a->col_relative, NULL) ;
	g_return_val_if_fail (!b->col_relative, NULL) ;
	g_return_val_if_fail (!a->row_relative, NULL) ;
	g_return_val_if_fail (!b->row_relative, NULL) ;
	g_return_val_if_fail (a->col<=b->col, NULL) ;
	g_return_val_if_fail (a->row<=b->row, NULL) ;

	row_idx = value_get_as_int (argv[2]) ;
	if (row_idx<=0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	if (row_idx>b->row-a->row+1) {
		*error_string = _("#REF!") ;
		return NULL ;
	}

	if (argv[3]) {
		int err ;
		approx = value_get_bool (argv[3], &err) ;
		if (err) {
			*error_string = _("#VALUE!") ;
			return NULL ;
		}
	}
	else
		approx = 1 ;

	height = b->col - a->col + 1 ;
	for (lp=0;lp<height;lp++) {
		int compare ;
		Cell *cell = sheet_cell_get (a->sheet, a->col+lp, a->row) ;

		g_return_val_if_fail (cell != NULL, NULL) ;
		g_return_val_if_fail (cell->value != NULL, NULL) ;

		compare = lookup_similar (cell->value, argv[0], next_largest, approx) ;
/*		printf ("Compare '%s' with '%s' : %d (%d)\n", value_string(cell->value), value_string(argv[0]), compare, approx) ; */
		if (compare == 1) {
			Cell *cell = sheet_cell_get (a->sheet, a->col+lp, a->row+row_idx-1) ;
			g_return_val_if_fail (cell != NULL, NULL) ;
			g_return_val_if_fail (cell->value != NULL, NULL) ;
			return value_duplicate (cell->value) ;
		}
		if (compare < 0) {
			next_largest = cell->value ;
			next_largest_col = lp ;
		}
	}
	if (approx && next_largest) {
		Cell *cell = sheet_cell_get (a->sheet, a->col+next_largest_col, a->row+row_idx-1) ;
		g_return_val_if_fail (cell != NULL, NULL) ;
		g_return_val_if_fail (cell->value != NULL, NULL) ;
		return value_duplicate (cell->value) ;
	}
	else
		*error_string = _("#N/A") ;

	return NULL ;
}

static char *help_column = {
	N_("@FUNCTION=COLUMN\n"
	   "@SYNTAX=COLUMN([reference])\n"

	   "@DESCRIPTION="
	   "The COLUMN function returns an array of the column numbers taking a default argument "
	   "of the containing cell position."
	   "\n"
	   "If reference is neither an array nor a reference nor a range returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMNS,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_column (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *v ;

	if (!expr_node_list || !expr_node_list->data)
		return value_int(eval_col+1) ;

	v = eval_expr (tsheet, expr_node_list->data, eval_col, eval_row, error_string) ;
	if (!v)
		return NULL ;
	switch (v->type) {
	case VALUE_CELLRANGE:
		*error_string = "Arrays not yet supported" ;
		return NULL ;
	case VALUE_ARRAY:
		*error_string = _("Unimplemented\n") ;
		return NULL ;
	default:
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
}

static char *help_columns = {
	N_("@FUNCTION=COLUMNS\n"
	   "@SYNTAX=COLUMNS(reference)\n"

	   "@DESCRIPTION="
	   "The COLUMNS function returns the number of columns in area or array reference."
	   "\n"
	   "If reference is neither an array nor a reference nor a range returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_columns (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	switch (argv[0]->type) {
	case VALUE_CELLRANGE:
		return value_int (argv[0]->v.cell_range.cell_b.col - argv[0]->v.cell_range.cell_a.col + 1) ;
	case VALUE_ARRAY:
		*error_string = _("Unimplemented\n") ;
		return NULL ;
	default:
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
}

static char *help_row = {
	N_("@FUNCTION=ROW\n"
	   "@SYNTAX=ROW([reference])\n"

	   "@DESCRIPTION="
	   "The ROW function returns an array of the row numbers taking a default argument "
	   "of the containing cell position."
	   "\n"
	   "If reference is neither an array nor a reference nor a range returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMN,COLUMNS,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_row (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *v ;

	if (!expr_node_list || !expr_node_list->data)
		return value_int(eval_row+1) ;

	v = eval_expr (tsheet, expr_node_list->data, eval_col, eval_row, error_string) ;
	if (!v)
		return NULL ;
	switch (v->type) {
	case VALUE_CELLRANGE:
		*error_string = "Arrays not yet supported" ;
		return NULL ;
	case VALUE_ARRAY:
		*error_string = _("Unimplemented\n") ;
		return NULL ;
	default:
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
}

static char *help_rows = {
	N_("@FUNCTION=ROWS\n"
	   "@SYNTAX=ROWS(reference)\n"

	   "@DESCRIPTION="
	   "The ROWS function returns the number of rows in area or array reference."
	   "\n"
	   "If reference is neither an array nor a reference nor a range returns #VALUE!."
	   "\n"
	   "@SEEALSO=COLUMN,ROW,ROWS")
};

/* FIXME: Needs Array support to be enven slightly meaningful */
static Value *
gnumeric_rows (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	switch (argv[0]->type) {
	case VALUE_CELLRANGE:
		return value_int (argv[0]->v.cell_range.cell_b.row - argv[0]->v.cell_range.cell_a.row + 1) ;
	case VALUE_ARRAY:
		*error_string = _("Unimplemented\n") ;
		return NULL ;
	default:
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
}


FunctionDefinition lookup_functions [] = {
	{ "column",    "?",    "ref",                      &help_column,   gnumeric_column, NULL },
	{ "columns",   "?",    "ref",                      &help_column,   NULL, gnumeric_columns },
	{ "hlookup",   "?rf|b","val,range,col_idx,approx", &help_hlookup,  NULL, gnumeric_hlookup },
	{ "row",       "?",    "ref",                      &help_row,      gnumeric_row, NULL },
	{ "rows",      "?",    "ref",                      &help_rows,     NULL, gnumeric_rows },
	{ "vlookup",   "?rf|b","val,range,col_idx,approx", &help_vlookup,  NULL, gnumeric_vlookup },
	{ NULL, NULL }
} ;




