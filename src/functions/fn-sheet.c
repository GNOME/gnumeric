/*
 * fn-sheet.c:  Built in sheet functions
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"


static char *help_selection = {
	N_("@FUNCTION=SELECTION\n"
	   "@SYNTAX=SELECTION(x)\n"

	   "@DESCRIPTION="
	   "The SELECTION function returns a list with the values in the current mouse cursor. "
	   "This is usually used to implement on-the-flight computation of values"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_selection (Sheet *sheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *value;
	GList *l;
	int numrange,lp;
	
	/* Type checking */
	if (expr_node_list != NULL){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	numrange=g_list_length (sheet->selections);
	value = value_array_new (numrange, 1);
	
	lp = 0;
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = (SheetSelection *) l->data;
		Value *single_value;
		CellRef *cell_ref;

		single_value = value->v.array.vals [lp++][0];
		single_value->type = VALUE_CELLRANGE;

		/* Fill it in */
		/*   start */
		cell_ref = &single_value->v.cell_range.cell_a;
		cell_ref->sheet = sheet;
		cell_ref->col_relative = 0;
		cell_ref->row_relative = 0;
		
		cell_ref->col = ss->start_col;
		cell_ref->row = ss->start_row;

		/*   end */
		cell_ref = &single_value->v.cell_range.cell_b;
		cell_ref->sheet = sheet;
		cell_ref->col_relative = 0;
		cell_ref->row_relative = 0;
		
		cell_ref->col = ss->end_col;
		cell_ref->row = ss->end_row;
	}

	return value;
}

FunctionDefinition sheet_functions [] = {
	{ "selection", 0,    "", &help_selection, gnumeric_selection, NULL },
	{ NULL, NULL }
};
