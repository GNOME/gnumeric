/* vim: set sw=8: */

/*
 * workbook-control.c: The base class for the displaying a workbook.
 *
 * Copyright (C) 2000-2001 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "workbook-control-priv.h"

#include "workbook-view.h"
#include "workbook.h"
#include "parse-util.h"
#include "sheet.h"
#include "selection.h"
#include "commands.h"

#include <gal/util/e-util.h>
#include <libgnome/gnome-i18n.h>

#define WBC_CLASS(o) WORKBOOK_CONTROL_CLASS ((o)->context.gtk_object.klass)
#define WBC_VIRTUAL_FULL(func, handle, arglist, call)		\
void wb_control_ ## func arglist				\
{								\
	WorkbookControlClass *wbc_class;			\
								\
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));		\
								\
	wbc_class = WBC_CLASS (wbc);				\
	if (wbc_class != NULL && wbc_class->handle != NULL)	\
		wbc_class->handle call;				\
}
#define WBC_VIRTUAL(func, arglist, call) WBC_VIRTUAL_FULL(func, func, arglist, call)

WorkbookControl *
wb_control_wrapper_new (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb)
{
	WorkbookControlClass *wbc_class;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);

	wbc_class = WBC_CLASS (wbc);
	if (wbc_class != NULL && wbc_class->control_new != NULL)
		return wbc_class->control_new (wbc, wbv, wb);
	return NULL;
}

WBC_VIRTUAL (title_set,
	(WorkbookControl *wbc, char const * title), (wbc, title))
WBC_VIRTUAL (prefs_update,
	(WorkbookControl *wbc), (wbc))
WBC_VIRTUAL (format_feedback, (WorkbookControl *wbc), (wbc))
WBC_VIRTUAL (zoom_feedback,
	(WorkbookControl *wbc), (wbc))
WBC_VIRTUAL (edit_line_set,
	(WorkbookControl *wbc, char const *text), (wbc, text))
WBC_VIRTUAL (selection_descr_set,
	(WorkbookControl *wbc, char const *text), (wbc, text))
WBC_VIRTUAL (auto_expr_value, (WorkbookControl *wbc), (wbc))

WBC_VIRTUAL_FULL (sheet_remove, sheet.remove,
	(WorkbookControl *wbc, Sheet *sheet), (wbc, sheet))
WBC_VIRTUAL_FULL (sheet_rename, sheet.rename,
	(WorkbookControl *wbc, Sheet *sheet), (wbc, sheet))
WBC_VIRTUAL_FULL (sheet_focus, sheet.focus,
	(WorkbookControl *wbc, Sheet *sheet), (wbc, sheet))
WBC_VIRTUAL_FULL (sheet_move, sheet.move,
	(WorkbookControl *wbc, Sheet *sheet, int new_pos),
	(wbc, sheet, new_pos))
WBC_VIRTUAL_FULL (sheet_remove_all, sheet.remove_all,
	(WorkbookControl *wbc), (wbc))

WBC_VIRTUAL_FULL (undo_redo_clear, undo_redo.clear,
	(WorkbookControl *wbc, gboolean is_undo), (wbc, is_undo))
WBC_VIRTUAL_FULL (undo_redo_truncate, undo_redo.truncate,
	(WorkbookControl *wbc, int n, gboolean is_undo), (wbc, n, is_undo))
WBC_VIRTUAL_FULL (undo_redo_pop, undo_redo.pop,
	(WorkbookControl *wbc, gboolean is_undo), (wbc, is_undo))
WBC_VIRTUAL_FULL (undo_redo_push, undo_redo.push,
	(WorkbookControl *wbc, char const *text, gboolean is_undo),
	(wbc, text, is_undo))
WBC_VIRTUAL_FULL (undo_redo_labels, undo_redo.labels,
	(WorkbookControl *wbc, char const *undo, char const *redo),
	(wbc, undo, redo))

WBC_VIRTUAL_FULL (menu_state_sheet_prefs, menu_state.sheet_prefs,
	(WorkbookControl *wbc, Sheet const *sheet), (wbc, sheet))
WBC_VIRTUAL_FULL (menu_state_update, menu_state.update,
        (WorkbookControl *wbc, Sheet const *sheet, int flags),
	(wbc, sheet, flags))
WBC_VIRTUAL_FULL (menu_state_sensitivity, menu_state.sensitivity,
        (WorkbookControl *wbc, gboolean sensitive),
	(wbc, sensitive))
	
WBC_VIRTUAL (paste_from_selection,
	(WorkbookControl *wbc, PasteTarget const *pt, guint32 time),
	(wbc, pt, time))

void
wb_control_sheet_add (WorkbookControl *wbc, Sheet *new_sheet)
{
	WorkbookControlClass *wbc_class;

	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));

	wbc_class = WBC_CLASS (wbc);
	if (wbc_class != NULL && wbc_class->sheet.add != NULL) {

		wbc_class->sheet.add (wbc, new_sheet);

		/* If this is the current sheet init the display */
		if (new_sheet == wb_control_cur_sheet (wbc)) {
			WorkbookView *wbv = wb_control_view (wbc);
			wb_control_sheet_focus (wbc, new_sheet);
			wb_view_selection_desc (wbv, TRUE, wbc);
			wb_view_edit_line_set (wbv, wbc);
			wb_control_auto_expr_value (wbc);
			wb_control_format_feedback (wbc);
			wb_control_menu_state_sheet_prefs (wbc, new_sheet);
			wb_control_menu_state_update (wbc, new_sheet, MS_ALL);
		}
	}
}

gboolean
wb_control_claim_selection (WorkbookControl *wbc)
{
	WorkbookControlClass *wbc_class;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), FALSE);

	wbc_class = WBC_CLASS (wbc);
	if (wbc_class != NULL && wbc_class->claim_selection != NULL)
		return wbc_class->claim_selection (wbc);
	return TRUE; /* no handler means we always get the selection */
}

WorkbookView *
wb_control_view (WorkbookControl *wbc)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);

	return wbc->wb_view;
}

Workbook *
wb_control_workbook (WorkbookControl *wbc)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);
	g_return_val_if_fail (wbc->wb_view != NULL, NULL);

	return wb_view_workbook (wbc->wb_view);
}

Sheet *
wb_control_cur_sheet (WorkbookControl *wbc)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);
	g_return_val_if_fail (wbc->wb_view != NULL, NULL);

	return wb_view_cur_sheet (wbc->wb_view);
}

gboolean
wb_control_parse_and_jump (WorkbookControl *wbc, const char *text)
{
	int col, row;

	/* FIXME : handle inter-sheet jumps too */
	if (parse_cell_name (text, &col, &row, TRUE, NULL)) {
		Sheet *sheet = wb_control_cur_sheet (wbc);
		sheet_selection_set (sheet, col, row, col, row, col, row);
		sheet_make_cell_visible (sheet, col, row, FALSE);
		return TRUE;
	} else {
		/* TODO : create a name */
	}

	gnumeric_error_invalid (COMMAND_CONTEXT (wbc), _("Address"), text);
	return FALSE;
}

/*****************************************************************************/

static GtkObjectClass *parent_class;
static void
wbc_destroy (GtkObject *obj)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (obj);
	if (wbc->wb_view != NULL)
		wb_view_detach_control (wbc);
	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
workbook_control_class_init (GtkObjectClass *object_class)
{
	parent_class = gtk_type_class (gtk_object_get_type ());
	object_class->destroy = wbc_destroy;
}

E_MAKE_TYPE (workbook_control, "WorkbookControl", WorkbookControl,
	     workbook_control_class_init, NULL, COMMAND_CONTEXT_TYPE);

void
workbook_control_set_view (WorkbookControl *wbc,
			   WorkbookView *opt_view, Workbook *opt_wb)
{
	WorkbookView *wbv;

	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (wbc->wb_view == NULL);

	wbv = (opt_view != NULL) ? opt_view : workbook_view_new (opt_wb);
	wb_view_attach_control (wbv, wbc);
}

void
workbook_control_init_state (WorkbookControl *wbc)
{
	GList *sheets, *ptr;
	WorkbookControlClass *wbc_class;

	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));

	/* Setup the undo/redo combos */
	command_setup_combos (wbc);

	/* Add views all all existing sheets */
	sheets = workbook_sheets (wb_control_workbook (wbc));
	for (ptr = sheets; ptr != NULL ; ptr = ptr->next)
		wb_control_sheet_add (wbc, ptr->data);
	g_list_free (sheets);

	wbc_class = WBC_CLASS (wbc);
	if (wbc_class != NULL && wbc_class->init_state != NULL)
		wbc_class->init_state (wbc);
}
