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
#include "gnumeric.h"
#include "workbook-control-priv.h"

#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "selection.h"
#include "commands.h"
#include "value.h"
#include "ranges.h"
#include "expr-name.h"

#include <gal/util/e-util.h>
#include <libgnome/gnome-i18n.h>

#define WBC_CLASS(o) WORKBOOK_CONTROL_CLASS (G_OBJECT_GET_CLASS (o))
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
WBC_VIRTUAL (edit_set_sensitive,
	(WorkbookControl *wbc,
	 gboolean ok_cancel_flag, gboolean func_guru_flag),
	(wbc, ok_cancel_flag, func_guru_flag))
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
WBC_VIRTUAL_FULL (menu_state_sheet_count, menu_state.sheet_count,
	(WorkbookControl *wbc), (wbc))

WBC_VIRTUAL (paste_from_selection,
	(WorkbookControl *wbc, PasteTarget const *pt), (wbc, pt))

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
	RangeRef const *r;
	Sheet *sheet  = wb_control_cur_sheet (wbc);
	Value *target = global_range_parse (sheet, text);

	/* not an address, is it a name ? */
	if (target == NULL) {
		ParsePos pp;
		GnmNamedExpr *nexpr = expr_name_lookup (
			parse_pos_init (&pp, NULL, sheet, 0, 0), text);

		/* Not a name, create one */
		if (nexpr == NULL) {
			Range const *r = selection_first_range (sheet,  wbc,
				_("Define Name"));
			if (r != NULL) {
				char const *err;
				CellRef a, b;
				a.sheet = b.sheet = sheet;
				a.col = r->start.col;
				a.row = r->start.row;
				b.col = r->end.col;
				b.row = r->end.row;
				a.col_relative = a.row_relative = b.col_relative = b.row_relative = FALSE;
				pp.sheet = NULL; /* make it a global name */
				nexpr = expr_name_add (&pp, text,
					gnm_expr_new_constant (
						value_new_cellrange_unsafe (&a, &b)), &err);
				if (nexpr != NULL)
					return TRUE;
				gnumeric_error_invalid (COMMAND_CONTEXT (wbc), _("Name"), err);
			}
			return FALSE;
		} else {
			if (!nexpr->builtin)
				target = gnm_expr_get_range (nexpr->t.expr_tree);
			if (target == NULL) {
				gnumeric_error_invalid (COMMAND_CONTEXT (wbc), _("Address"), text);
				return FALSE;
			}
		}
	}

	r = &target->v_range.cell;
	sheet = r->a.sheet;
	sheet_selection_set (r->a.sheet, r->a.col, r->a.row,
			     r->a.col, r->a.row, r->b.col, r->b.row);
	sheet_make_cell_visible (sheet, r->a.col, r->a.row, FALSE);
	if (wb_control_cur_sheet (wbc) != sheet)
		wb_view_sheet_focus (wbc->wb_view, sheet);
	value_release (target);
	return TRUE;
}

/*****************************************************************************/

static void
wbc_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	WorkbookControl *wbc = WORKBOOK_CONTROL (obj);
	if (wbc->wb_view != NULL)
		wb_view_detach_control (wbc);
	parent_class = g_type_class_peek (COMMAND_CONTEXT_TYPE);
	if (parent_class != NULL && parent_class->finalize != NULL)
		(parent_class)->finalize (obj);
}

static void
workbook_control_class_init (GObjectClass *object_class)
{
	object_class->finalize = wbc_finalize;
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
