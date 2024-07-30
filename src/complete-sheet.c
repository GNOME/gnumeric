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
#include <gnumeric.h>
#include <complete-sheet.h>
#include <gnumeric-conf.h>

#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <parse-util.h>

#include <gsf/gsf-impl-utils.h>
#include <string.h>

#define SEARCH_STEPS	50

#define PARENT_TYPE	GNM_COMPLETE_TYPE

static GObjectClass *parent_class;


static void
search_strategy_reset_search (GnmCompleteSheet *cs)
{
	cs->current.col = cs->entry.col;
	cs->current.row = cs->entry.row;
	cs->cell = NULL;
}

/*
 * Very simple search strategy: up until blank.
 */
static gboolean
search_strategy_next (GnmCompleteSheet *cs)
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
	GnmCompleteSheet *cs = GNM_COMPLETE_SHEET (object);
	g_free (cs->current_text);
	parent_class->finalize (object);
}

static gboolean
text_matches (GnmCompleteSheet const *cs)
{
	char const *text;
	GnmComplete const *complete = &cs->parent;

	if (cs->cell->value == NULL ||
	    !VALUE_IS_STRING (cs->cell->value) ||
	    gnm_cell_has_expr (cs->cell))
		return FALSE;

	text = value_peek_string (cs->cell->value);
	if (!g_str_has_prefix (text, complete->text))
		return FALSE;

	(*complete->notify)(text, complete->notify_closure);
	return TRUE;
}

static gboolean
complete_sheet_search_iteration (GnmComplete *complete)
{
	GnmCompleteSheet *cs = GNM_COMPLETE_SHEET (complete);
	int i;

	if ((int)strlen (complete->text) <
	    gnm_conf_get_core_gui_editing_autocomplete_min_chars ())
		return FALSE;

	if (!g_str_has_prefix (cs->current_text, complete->text))
		search_strategy_reset_search (cs);

	for (i = 0; i < SEARCH_STEPS; i++) {
		if (!search_strategy_next (cs))
			return FALSE;

		if (text_matches (cs))
			return FALSE;
	}

	return TRUE;
}

static void
complete_sheet_class_init (GObjectClass *object_class)
{
	GnmCompleteClass *auto_complete_class = (GnmCompleteClass *) object_class;

	parent_class = g_type_class_peek (PARENT_TYPE);
	object_class->finalize = complete_sheet_finalize;
	auto_complete_class->search_iteration = complete_sheet_search_iteration;
}

/**
 * gnm_complete_sheet_new:
 * @sheet: #Sheet
 * @col: column
 * @row: row
 * @notify: (scope async): #GnmCompleteMatchNotifyFn
 * @notify_closure: user data
 *
 * Returns: (transfer full): the new #GnmComplete.
 **/
GnmComplete *
gnm_complete_sheet_new (Sheet *sheet, int col, int row, GnmCompleteMatchNotifyFn notify, void *notify_closure)
{
	/*
	 * Somehow every time I pronounce this, I feel like something is not quite right.
	 */
	GnmCompleteSheet *cs;

	cs = g_object_new (GNM_COMPLETE_SHEET_TYPE, NULL);
	gnm_complete_construct (GNM_COMPLETE (cs), notify, notify_closure);

	cs->sheet = sheet;
	cs->entry.col = col;
	cs->entry.row = row;
	cs->current_text = g_strdup ("");
	search_strategy_reset_search (cs);

	return GNM_COMPLETE (cs);
}

GSF_CLASS (GnmCompleteSheet, gnm_complete_sheet,
	   complete_sheet_class_init, NULL, PARENT_TYPE)
