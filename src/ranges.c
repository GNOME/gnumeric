/*
 * ranges.c: various functions for common operations on cell ranges.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 *
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <string.h>
#include <ctype.h>
#include <gnome.h>
#include "numbers.h"
#include "symbol.h"
#include "str.h"
#include "expr.h"
#include "utils.h"
#include "gnumeric.h"
#include "ranges.h"

/**
 * range_parse:
 * @sheet: the sheet where the cell range is evaluated
 * @range: a range specification (ex: "A1", "A1:C3").
 * @v: a pointer to a Value where the return value is placed.
 *
 * Returns TRUE if the @range was succesfully parsed.  If
 * this is the case, @v will point to a newly allocated
 * Value structure of type VALUE_CELLRANGE
 */
gboolean
range_parse (Sheet *sheet, const char *range, Value **v)
{
	CellRef a, b;
	char *p;
	
	g_return_val_if_fail (range != NULL, FALSE);
	g_return_val_if_fail (v != NULL, FALSE);

	a.col_relative = 0;
	b.col_relative = 0;
	a.row_relative = 0;
	b.row_relative = 0;
	
	if (!parse_cell_name (range, &a.col, &a.row))
		return FALSE;

	a.sheet = sheet;
	
	p = strstr (range, ":");
	if (p){
		if (!parse_cell_name (range, &b.col, &b.row))
			return FALSE;

		b.sheet = sheet;
	} else
		b = a;

	*v = value_new_cellrange (&a, &b);

	return  TRUE;
}

/**
 * range_list_destroy:
 * @ranges: a list of value ranges to destroy. 
 *
 * Destroys a list of ranges returned from parse_cell_range_list
 */
void
range_list_destroy (GSList *ranges)
{
	GSList *l;

	for (l = ranges; l; l = l->next){
		Value *value = l->data;
		
		value_release (value);
	}
	g_slist_free (ranges);
}


/**
 * range_list_parse:
 * @sheet: Sheet where the range specification is relatively parsed to
 * @range_spec: a range or list of ranges to parse (ex: "A1", "A1:B1,C2,D2:D4")
 *
 * Parses a list of ranges, relative to the @sheet and returns a list with the
 * results.
 *
 * Returns a GSList containing Values of type VALUE_CELLRANGE, or NULL on failure
 */
GSList *
range_list_parse (Sheet *sheet, const char *range_spec)
{
	
	char *copy, *range, *range_copy, *r;
	GSList *ranges = NULL;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range_spec != NULL, NULL);
	
	range_copy = copy = g_strdup (range_spec);

	while ((r = strtok (range_copy, ",")) != NULL){
		Value *v = NULL;
		
		if (!range_parse (sheet, r, &v)){
			range_list_destroy (ranges);
			g_free (copy);
			return NULL;
		}

		ranges = g_slist_prepend (ranges, v);
		range_copy = NULL;
	}
	
	g_free (copy);

	return ranges;
}

/**
 * range_list_foreach_full:
 * 
 * foreach cell in the range, make sure it exists, and invoke the routine
 * @callback on the resulting cell, passing @data to it
 *
 */
void
range_list_foreach_full (GSList *ranges, void (*callback)(Cell *cell, void *data),
			 void *data, gboolean create_empty)
{
	GSList *l;

	{
		static int message_shown;

		if (!message_shown){
			g_warning ("This routine should also iterate trough the sheets in the ranges");
			message_shown = TRUE;
		}
	}

	for (l = ranges; l; l = l->next){
		Value *value = l->data;
		CellRef a, b;
		int col, row;
		
		g_assert (value->type == VALUE_CELLRANGE);

		a = value->v.cell_range.cell_a;
		b = value->v.cell_range.cell_b;

		for (col = a.col; col <= b.col; col++)
			for (row = a.row; row < b.row; row++){
				Cell *cell;

				if (create_empty)
					cell = sheet_cell_fetch (a.sheet, col, row);
				else
					cell = sheet_cell_get (a.sheet, col, row);
				if (cell)
					(*callback)(cell, data);
			}
	}
}

void
range_list_foreach_all (GSList *ranges,
			void (*callback)(Cell *cell, void *data),
			void *data)
{
	range_list_foreach_full (ranges, callback, data, TRUE);
}

void
range_list_foreach (GSList *ranges, void (*callback)(Cell *cell, void *data),
		    void *data)
{
	range_list_foreach_full (ranges, callback, data, FALSE);
}

/**
 * range_set_style:
 * @ranges: a list of Cell ranges.
 * @style: a style definition to apply.
 *
 * This routine attaches the style to the cell ranges specified.
 */
void
range_set_style (GSList *ranges, Style *style)
{
	GSList *l;

	g_return_if_fail (style != NULL);
	
	for (l = ranges; l; l = l->next){
		Value *value = l->data;
		CellRef a, b;
		int col, row;
		
		g_assert (value->type == VALUE_CELLRANGE);

		a = value->v.cell_range.cell_a;
		b = value->v.cell_range.cell_b;

		sheet_style_attach (a.sheet, a.col, a.row, b.col, b.row, style);
	}
}
