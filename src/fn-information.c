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

static Value *
gnumeric_countblank (struct FunctionDefinition *n,
		     Value *argv [], char **error_string)
{
        Sheet *sheet;
        Value *range;
	int   col_a, col_b, row_a, row_b;
	int   i, j;
	int   count;

	range = argv[0];
	sheet = (Sheet *) range->v.cell_range.cell_a.sheet;
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

	return value_new_int (count);
}


FunctionDefinition information_functions [] = {
        { "countblank", "r",  "range",       &help_countblank,
	  NULL, gnumeric_countblank },
        { NULL, NULL }
};
