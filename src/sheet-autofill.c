/*
 * sheet-autofill.c: Provides the autofill features
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org), 1998
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "sheet-autofill.h"

static GList *autofill_functions;

void
sheet_autofill (Sheet *sheet, int base_col, int base_row, int w, int h, int end_col, int end_row)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = autofill_functions; l; l = l->next){
		AutofillFunction *aff = l->data;

		if ((*aff)(sheet, base_col, base_row, w, h, end_col, end_row))
			return;
	}
}
void
register_autofill_function (AutofillFunction fn)
{
	g_return_if_fail (fn != NULL);
	
	autofill_functions = g_list_prepend (autofill_functions, fn);
}

static void
autofill_integers (Sheet *sheet, int base_col, int base_row, int w, int h, int end_col, int end_row)
{
}

void
autofill_init (void)
{
	register_autofill_function (autofill_integers);
}
