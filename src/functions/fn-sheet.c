/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-sheet.c:  Built in sheet functions
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <value.h>
#include <selection.h>

#include <stdlib.h>
#include <libgnome/gnome-i18n.h>

/***************************************************************************/

static const char *help_selection = {
	N_("@FUNCTION=SELECTION\n"
	   "@SYNTAX=SELECTION(permit_intersection)\n"

	   "@DESCRIPTION="
	   "SELECTION function returns a list with the values in the current "
	   "selection.  This is usually used to implement on-the-fly computation "
	   "of values.\n"
	   "If @permit_intersection is TRUE the user specifed selection "
	   "ranges are returned, EVEN IF THEY OVERLAP.  "
	   "If @permit_intersection is FALSE a distict set of regions is "
	   "returned, however, there may be more of them than "
	   "the user initially specified."

	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

typedef struct
{
	GSList * res;
	int	index;
} selection_accumulator;

static void
accumulate_regions (Sheet *sheet,  Range const *r, gpointer closure)
{
	selection_accumulator *accum = closure;
	CellRef a, b;

	/* Fill it in */
	/* start */
	a.sheet = sheet;
	a.col_relative = a.row_relative = FALSE;
	a.col = r->start.col;
	a.row = r->start.row;

	/* end */
	b.sheet = sheet;
	b.col_relative = b.row_relative = FALSE;
	b.col = r->end.col;
	b.row = r->end.row;

	/* Dummy up the eval pos it does not matter */
	accum->res = g_slist_prepend (accum->res,
				      value_new_cellrange(&a, &b, 0, 0));
	accum->index++;
}

/* This routine is used to implement the auto_expr functionality.  It is called
 * to provide the selection to the defined functions.
 */
static Value *
gnumeric_selection (FunctionEvalInfo *ei, Value *argv [])
{
	Sheet * const sheet = ei->pos->sheet;
	gboolean const permit_intersection = argv [0]->v_bool.val;
	Value * res;
	int i;

	selection_accumulator accum;
	accum.res = NULL;
	accum.index = 0;
	selection_apply (sheet, &accumulate_regions,
			 permit_intersection, &accum);

	i = accum.index;
	res = value_new_array_empty (i, 1);
	while (i-- > 0) {
		/* pop the 1st element off the list */
		Value *range = accum.res->data;
		accum.res = g_slist_remove (accum.res, range);

		value_array_set (res, i, 0, range);
	}
	return res;
}

/***************************************************************************/

static const char *help_gnumeric_version = {
	N_("@FUNCTION=GNUMERIC_VERSION\n"
	   "@SYNTAX=GNUMERIC_VERSION()\n"

	   "@DESCRIPTION="
	   "GNUMERIC_VERSION return the version of gnumeric as a string."

	   "\n"
	   "@EXAMPLES=\n"
	   "GNUMERIC_VERSION().\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_version (FunctionEvalInfo *ei, Value *argv [])
{
	return value_new_string (GNUMERIC_VERSION);
}

/***************************************************************************/

/*
 * WARNING * WARNING * WARNING
 *
 * The case of the function names being registered must be consistent
 * with the auto expressions in src/workbook.c
 *
 * There are some locales (notably tr_TR) do NOT had 'i' as the lower case
 * of 'I'.  Note that we should also not use TRUE/FALSE or any other
 * translatable string.
 *
 * WARNING * WARNING * WARNING
 */
void sheet_functions_init (void);
void
sheet_functions_init (void)
{
	FunctionCategory *cat0 = function_get_category_with_translation
	  ("Sheet", _("Sheet"));
	FunctionCategory *cat1 = function_get_category_with_translation
	  ("Gnumeric", _("Gnumeric"));

	function_add_args (cat0, "selection", "b",  "permit_intersection",
			   &help_selection, gnumeric_selection);
	function_add_args (cat1, "gnumeric_version", "",  "",
			   &help_gnumeric_version, gnumeric_version);
}
