/*
 * fn-information.c:  Information built-in functions
 *
 * Author:
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 *
 */
#include <config.h>
#include <gnome.h>
#include <ctype.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"


static char *help_countblank = {
        N_("@FUNCTION=COUNTBLANK\n"
           "@SYNTAX=COUNTBLANK(range)\n"

           "@DESCRIPTION="
           "COUNTBLANK returns the number of blank cells in a range. "
           "\n"
           "@SEEALSO=COUNT")
};

static FuncReturn *
gnumeric_countblank (FunctionEvalInfo *s)
{
        Sheet *sheet;
        Value *range;
	int   col_a, col_b, row_a, row_b;
	int   i, j;
	int   count;

	range = s->a.args[0];
	sheet = range->v.cell_range.cell_a.sheet;
	col_a = range->v.cell_range.cell_a.col;
	col_b = range->v.cell_range.cell_b.col;
	row_a = range->v.cell_range.cell_a.row;
	row_b = range->v.cell_range.cell_b.row;
	count = 0;

	for (i=col_a; i<=col_b; i++)
	        for (j=row_a; j<=row_b; j++) {
		        Cell *cell = sheet_cell_get(sheet, i, j);

		        if (cell == NULL || cell->value == NULL)
			        count++;
		}

	return (FuncReturn *)value_new_int (count);
}

void information_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Information"));

        function_new (cat, "countblank", "r",  "range",       &help_countblank,
		      FUNCTION_ARGS, gnumeric_countblank);
}
