/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * workbook-control.c: The base class for the displaying a workbook.
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "workbook-control-priv.h"

#include "application.h"
#include "workbook-view.h"
#include "workbook-priv.h"
#include "sheet.h"
#include "sheet-view.h"
#include "selection.h"
#include "commands.h"
#include "value.h"
#include "ranges.h"
#include "expr-name.h"
#include "command-context.h"

#include <gsf/gsf-impl-utils.h>

#define WBC_CLASS(o) WORKBOOK_CONTROL_CLASS (G_OBJECT_GET_CLASS (o))
#define WBC_VIRTUAL_FULL(func, handle, arglist, call)		\
void wb_control_ ## func arglist				\
{								\
	WorkbookControlClass *wbc_class = WBC_CLASS (wbc);	\
								\
	g_return_if_fail (wbc_class != NULL);			\
								\
	if (wbc_class != NULL && wbc_class->handle != NULL)	\
		wbc_class->handle call;				\
}
#define WBC_VIRTUAL(func, arglist, call) WBC_VIRTUAL_FULL(func, func, arglist, call)

WorkbookControl *
wb_control_wrapper_new (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb,
			void *extra)
{
	WorkbookControlClass *wbc_class = WBC_CLASS (wbc);

	g_return_val_if_fail (wbc_class != NULL, NULL);

	if (wbc_class != NULL && wbc_class->control_new != NULL)
		return wbc_class->control_new (wbc, wbv, wb, extra);
	return NULL;
}

/**
 * wb_control_update_title :
 * @wbc : #WorkbookControl
 *
 * Set the controls title to @title.  Additionally notify the application that
 * the list of windows should be updated.
 **/
void
wb_control_update_title (WorkbookControl *wbc)
{
	WorkbookControlClass *wbc_class = WBC_CLASS (wbc);

	g_return_if_fail (wbc_class != NULL);

	if (wbc_class != NULL && wbc_class->set_title != NULL) {
		Workbook const *wb = wb_control_workbook (wbc);
		if (workbook_is_dirty (wb)) {
			char *tmp = g_strconcat ("*", wb->basename, NULL);
			wbc_class->set_title (wbc, tmp);
			g_free (tmp);
		} else
			wbc_class->set_title (wbc, wb->basename);
	}
}

WBC_VIRTUAL (prefs_update,
	(WorkbookControl *wbc), (wbc))
WBC_VIRTUAL (style_feedback,
	(WorkbookControl *wbc, GnmStyle const *changes), (wbc, changes))
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

WBC_VIRTUAL_FULL (undo_redo_truncate, undo_redo.truncate,
	(WorkbookControl *wbc, int n, gboolean is_undo), (wbc, n, is_undo))
WBC_VIRTUAL_FULL (undo_redo_pop, undo_redo.pop,
	(WorkbookControl *wbc, gboolean is_undo), (wbc, is_undo))
WBC_VIRTUAL_FULL (undo_redo_push, undo_redo.push,
	(WorkbookControl *wbc, gboolean is_undo, char const *text, gpointer key),
	(wbc, is_undo, text, key))
WBC_VIRTUAL_FULL (undo_redo_labels, undo_redo.labels,
	(WorkbookControl *wbc, char const *undo, char const *redo),
	(wbc, undo, redo))

WBC_VIRTUAL_FULL (menu_state_sheet_prefs, menu_state.sheet_prefs,
	(WorkbookControl *wbc, Sheet const *sheet), (wbc, sheet))
WBC_VIRTUAL_FULL (menu_state_update, menu_state.update,
        (WorkbookControl *wbc, int flags),
	(wbc, flags))
WBC_VIRTUAL_FULL (menu_state_sheet_count, menu_state.sheet_count,
	(WorkbookControl *wbc), (wbc))

WBC_VIRTUAL (paste_from_selection,
	(WorkbookControl *wbc, GnmPasteTarget const *pt), (wbc, pt))
WBC_VIRTUAL (update_action_sensitivity,
	(WorkbookControl *wbc), (wbc))

void
wb_control_sheet_add (WorkbookControl *wbc, SheetView *sv)
{
	WorkbookControlClass *wbc_class;

	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));

	wbc_class = WBC_CLASS (wbc);
	if (wbc_class != NULL && wbc_class->sheet.add != NULL) {
		Sheet *new_sheet = sv_sheet (sv);

		wbc_class->sheet.add (wbc, sv);

		/* If this is the current sheet init the display */
		if (new_sheet == wb_control_cur_sheet (wbc)) {
			WorkbookView *wbv = wb_control_view (wbc);
			wb_control_sheet_focus (wbc, new_sheet);
			wb_view_selection_desc (wbv, TRUE, wbc);
			wb_view_edit_line_set (wbv, wbc);
			wb_control_auto_expr_value (wbc);
			wb_control_style_feedback (wbc, NULL);
			wb_control_menu_state_sheet_prefs (wbc, new_sheet);
			wb_control_menu_state_update (wbc, MS_ALL);
			wb_control_update_action_sensitivity (wbc);
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

/**
 * wb_control_validation_msg :
 * 	 1 : ignore invalid and accept result
 * 	 0 : discard invalid and finish editing
 *	-1 : continue editing
 */
int
wb_control_validation_msg (WorkbookControl *wbc, ValidationStyle v,
			   char const *title, char const *msg)
{
	WorkbookControlClass *wbc_class;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), 1);

	wbc_class = WBC_CLASS (wbc);
	if (wbc_class != NULL && wbc_class->validation_msg != NULL)
		return wbc_class->validation_msg (wbc, v, title, msg);
	return 1; /* no handler, always accept */
}

WorkbookView *
wb_control_view (WorkbookControl const *wbc)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);
	return wbc->wb_view;
}

Workbook *
wb_control_workbook (WorkbookControl const *wbc)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);
	return wb_view_workbook (wbc->wb_view);
}

Sheet *
wb_control_cur_sheet (WorkbookControl const *wbc)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);
	return wb_view_cur_sheet (wbc->wb_view);
}

SheetView *
wb_control_cur_sheet_view (WorkbookControl const *wbc)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);
	return wb_view_cur_sheet_view (wbc->wb_view);
}

gboolean
wb_control_parse_and_jump (WorkbookControl *wbc, char const *text)
{
	GnmRangeRef const *r;
	GnmCellPos tmp;
	Sheet *sheet  = wb_control_cur_sheet (wbc);
	SheetView *sv;
	GnmValue *target;

	if (text == NULL || *text == '\0')
		return FALSE;

	/* not an address, is it a name ? */
	target = global_range_parse (sheet, text);
	if (target == NULL) {
		GnmParsePos pp;
		GnmNamedExpr *nexpr = expr_name_lookup (
			parse_pos_init_sheet (&pp, sheet), text);

		/* If no name, or just a placeholder exists create a name */
		if (nexpr == NULL || expr_name_is_placeholder (nexpr)) {
			GnmRange const *r = selection_first_range (
				wb_control_cur_sheet_view (wbc),
				GNM_CMD_CONTEXT (wbc),
				_("Define Name"));
			if (r != NULL) {
				GnmCellRef a, b;
				GnmExpr const *target_range;

				a.sheet = b.sheet = sheet;
				a.col = r->start.col;
				a.row = r->start.row;
				b.col = r->end.col;
				b.row = r->end.row;
				a.col_relative = a.row_relative = b.col_relative = b.row_relative = FALSE;
				pp.sheet = NULL; /* make it a global name */
				target_range = gnm_expr_new_constant (
					value_new_cellrange_unsafe (&a, &b));
				cmd_define_name (wbc, text, &pp, target_range);
			}
			return FALSE;
		} else {
			target = gnm_expr_get_range (nexpr->expr);
			if (target == NULL) {
				gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc), _("Address"), text);
				return FALSE;
			}
		}
	}

	r = &target->v_range.cell;
	sheet = r->a.sheet;
	sv = sheet_get_view (sheet, wb_control_view (wbc)),
	tmp.col = r->a.col;
	tmp.row = r->a.row;
	sv_selection_set (sv, &tmp, r->a.col, r->a.row, r->b.col, r->b.row);
	sv_make_cell_visible (sv, r->b.col, r->b.row, FALSE);
	sv_make_cell_visible (sv, r->a.col, r->a.row, FALSE);
	sv_update (sv);
	if (wb_control_cur_sheet (wbc) != sheet)
		wb_view_sheet_focus (wbc->wb_view, sheet);
	value_release (target);
	return TRUE;
}

static void
cb_wbc_clipboard_modified (GnmApp *app, WorkbookControl *wbc)
{
	wb_control_menu_state_update (wbc, MS_PASTE_SPECIAL);
}

/*****************************************************************************/

static GObjectClass *parent_klass;
static void
wbc_finalize (GObject *obj)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (obj);
	if (wbc->clipboard_changed_signal)
		g_signal_handler_disconnect (gnm_app_get_app (),
					     wbc->clipboard_changed_signal);
	wbc->clipboard_changed_signal = 0;
	if (wbc->wb_view != NULL)
		wb_view_detach_control (wbc);
	(*parent_klass->finalize) (obj);
}

static void
workbook_control_class_init (GObjectClass *object_class)
{
	parent_klass = g_type_class_peek_parent (object_class);
	object_class->finalize = wbc_finalize;
}

static void
workbook_control_init (GObject *obj)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (obj);

	/* We are not in edit mode */
	wbc->editing = FALSE;
	wbc->editing_sheet = NULL;
	wbc->editing_cell = NULL;

	wbc->clipboard_changed_signal = g_signal_connect (
		gnm_app_get_app (),
		"clipboard_modified",
		G_CALLBACK (cb_wbc_clipboard_modified), wbc);
}

GSF_CLASS (WorkbookControl, workbook_control,
	   workbook_control_class_init, workbook_control_init,
	   GO_DOC_CONTROL_TYPE);

void
wb_control_set_view (WorkbookControl *wbc,
			   WorkbookView *opt_view, Workbook *opt_wb)
{
	WorkbookView *wbv;

	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (wbc->wb_view == NULL);

	wbv = (opt_view != NULL) ? opt_view : workbook_view_new (opt_wb);
	wb_view_attach_control (wbv, wbc);
}

void
wb_control_init_state (WorkbookControl *wbc)
{
	GList *sheets, *ptr;
	Sheet *sheet;
	WorkbookView *wbv;
	WorkbookControlClass *wbc_class;

	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));

	/* Setup the undo/redo combos */
	command_setup_combos (wbc);

	/* Add views all all existing sheets */
	wbv = wb_control_view (wbc);
	sheets = workbook_sheets (wb_control_workbook (wbc));
	for (ptr = sheets; ptr != NULL ; ptr = ptr->next) {
		sheet = ptr->data;
		SHEET_FOREACH_VIEW (sheet, view, {
			if (sv_wbv (view) == wbv)
				wb_control_sheet_add (wbc, view);
		});
	}
	g_list_free (sheets);

	wbc_class = WBC_CLASS (wbc);
	if (wbc_class != NULL && wbc_class->init_state != NULL)
		wbc_class->init_state (wbc);
}
