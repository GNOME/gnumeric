/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * complete-sheet.c: Auto completes values from a sheet.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * This is a pretty simple implementation, should be helpful
 * to find performance hot-spots (if you bump SEARCH_STEPS to gnm_sheet_get_max_rows (sheet)/2)
 *
 * (C) 2000-2001 Ximian, Inc.
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "complete-sheet.h"

#include "sheet.h"
#include "cell.h"
#include "str.h"
#include "value.h"
#include "parse-util.h"

#include <gsf/gsf-impl-utils.h>
#include <string.h>

#define SEARCH_STEPS	50

#define PARENT_TYPE	COMPLETE_TYPE

static GObjectClass *parent_class;


static void
search_strategy_reset_search (CompleteSheet *cs)
{
	cs->current.col = cs->entry.col;
	cs->current.row = cs->entry.row;
	cs->cell = NULL;
}

/*
 * Very simple search strategy: up until blank.
 */
static gboolean
search_strategy_next (CompleteSheet *cs)
{
	cs->current.row--;
	if (cs->current.row < 0)
		return FALSE;

	cs->cell = sheet_cell_get (cs->sheet, cs->current.col, cs->current.row);
	return cs->cell != NULL;
}


static void
complete_sheet_finalize (GObject *object)
{
	CompleteSheet *cs = COMPLETE_SHEET (object);
	g_free (cs->current_text);
	parent_class->finalize (object);
}

static gboolean
text_matches (CompleteSheet const *cs)
{
	char const *text;
	Complete const *complete = &cs->parent;

	if (cs->cell->value == NULL ||
	    !VALUE_IS_STRING (cs->cell->value) ||
	    gnm_cell_has_expr (cs->cell))
		return FALSE;

	text = value_peek_string (cs->cell->value);
	if (strncmp (text, complete->text, strlen (complete->text)) != 0)
		return FALSE;

	(*complete->notify)(text, complete->notify_closure);
	return TRUE;
}

static gboolean
complete_sheet_search_iteration (Complete *complete)
{
	CompleteSheet *cs = COMPLETE_SHEET (complete);
	int i;

	/* http://bugzilla.gnome.org/show_bug.cgi?id=55026
	 * only kick in after 3 characters */
	if (strlen (complete->text) < 3)
		return FALSE;

	if (strncmp (cs->current_text, complete->text, strlen (cs->current_text)) != 0)
		search_strategy_reset_search (cs);

	for (i = 0; i < SEARCH_STEPS; i++) {
		if (!search_strategy_next (cs))
			return FALSE;

#if 0
		g_print ("Checking %s...\n", cell_coord_name (cs->current.col, cs->current.row));
#endif

		if (text_matches (cs))
			return FALSE;
	}

	return TRUE;
}

static void
complete_sheet_class_init (GObjectClass *object_class)
{
	CompleteClass *auto_complete_class = (CompleteClass *) object_class;

	parent_class = g_type_class_peek (PARENT_TYPE);
	object_class->finalize = complete_sheet_finalize;
	auto_complete_class->search_iteration = complete_sheet_search_iteration;
}

Complete *
complete_sheet_new (Sheet *sheet, int col, int row, CompleteMatchNotifyFn notify, void *notify_closure)
{
	/*
	 * Somehow every time I pronounce this, I feel like something is not quite right.
	 */
	CompleteSheet *cs;

	cs = g_object_new (COMPLETE_SHEET_TYPE, NULL);
	complete_construct (COMPLETE (cs), notify, notify_closure);

	cs->sheet = sheet;
	cs->entry.col = col;
	cs->entry.row = row;
	cs->current_text = g_strdup ("");
	search_strategy_reset_search (cs);

	return COMPLETE (cs);
}

GSF_CLASS (CompleteSheet, complete_sheet,
	   complete_sheet_class_init, NULL, PARENT_TYPE);
