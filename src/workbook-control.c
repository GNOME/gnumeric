/* vim: set sw=8: */

/*
 * workbook-control.c: 
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
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
#include <config.h>
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gnumeric-type-util.h"
#include "parse-util.h"
#include "sheet.h"

#include <gnome.h> /* Ick.  This is required to get _("") */

#define WBC_CLASS(o) WORKBOOK_CONTROL_CLASS ((o)->gtk_object.klass)
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
WBC_VIRTUAL (progress_set,
	(WorkbookControl *wbc, gfloat val), (wbc, val))
WBC_VIRTUAL (format_feedback,
	(WorkbookControl *wbc, MStyle *style), (wbc, style))
WBC_VIRTUAL (zoom_feedback,
	     (WorkbookControl *wbc), (wbc))
WBC_VIRTUAL (edit_line_set,
	     (WorkbookControl *wbc, char const *text), (wbc, text))
WBC_VIRTUAL (selection_descr_set,
	     (WorkbookControl *wbc, char const *text), (wbc, text))
WBC_VIRTUAL (auto_expr_value,
	     (WorkbookControl *wbc, char const *value), (wbc, value))

WBC_VIRTUAL_FULL (sheet_add, sheet.add,
	     (WorkbookControl *wbc, Sheet *sheet), (wbc, sheet))
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
WBC_VIRTUAL_FULL (undo_redo_pop, undo_redo.pop,
	(WorkbookControl *wbc, gboolean is_undo), (wbc, is_undo))
WBC_VIRTUAL_FULL (undo_redo_push, undo_redo.push,
	(WorkbookControl *wbc, char const *text, gboolean is_undo),
	(wbc, text, is_undo))
WBC_VIRTUAL_FULL (undo_redo_labels, undo_redo.labels,
	(WorkbookControl *wbc, char const *undo, char const *redo),
	(wbc, undo, redo))

WBC_VIRTUAL_FULL (paste_special_enable, paste.special_enable,
	(WorkbookControl *wbc, gboolean enable), (wbc, enable))
WBC_VIRTUAL_FULL (paste_from_selection, paste.from_selection,
	(WorkbookControl *wbc, PasteTarget const *pt, guint32 time),
	(wbc, pt, time))

WBC_VIRTUAL_FULL (system_err, error.system,
	(WorkbookControl *wbc, char const *msg), (wbc, msg))
WBC_VIRTUAL_FULL (plugin_err, error.plugin,
	(WorkbookControl *wbc, char const *msg), (wbc, msg))
WBC_VIRTUAL_FULL (read_err, error.read,
	(WorkbookControl *wbc, char const *msg), (wbc, msg))
WBC_VIRTUAL_FULL (save_err, error.save,
	(WorkbookControl *wbc, char const *msg), (wbc, msg))
WBC_VIRTUAL_FULL (invalid_err, error.invalid,
	(WorkbookControl *wbc, char const *msg, char const *val),
	(wbc, msg, val))

/**
 * wb_control_push_err_template
 * @str: printf template to display message
 *
 * Push a printf template to the stack. The template is used to provide
 * context for error messages. E.g.: "Could not read file: %s"
 */
void
wb_control_push_err_template (WorkbookControl *wbc, const char *str)
{
	wbc->template_list = g_slist_prepend (wbc->template_list,
					      g_strdup (str));
}

/**
 * wb_control_pop_err_template:
 *
 * Call this routine to remove the current template from the stack.
 */
void
wb_control_pop_err_template (WorkbookControl *wbc)
{
	if (wbc->template_list) {
		GSList *tlist = wbc->template_list;
		g_free (wbc->template_list->data);
		wbc->template_list = wbc->template_list->next;
		g_slist_free_1(tlist);
	}
}

/**
 * format_message:
 *
 * Format a message using the template on the stack (if any).
 * The caller must free the returned message.
 * FIXME: Make it accept varargs.
 */
static char *
format_message (WorkbookControl *context, char const *message)
{
#warning Use this
	GSList *tlist = context->template_list;
	char const * const msg = message ? message : "";

	if (tlist)
		return g_strdup_printf ((char *) (tlist->data), msg);
	else
		return g_strdup (msg);
}

void
wb_control_splits_array_err (WorkbookControl *wbc, char const *cmd)
{
	wb_control_invalid_err (wbc, cmd, _("Would split an array."));
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
workbook_parse_and_jump (WorkbookControl *wbc, const char *text)
{
	int col, row;

	if (parse_cell_name (text, &col, &row, TRUE, NULL)){
		Sheet *sheet = wb_control_cur_sheet (wbc);
		sheet_cursor_set (sheet, col, row, col, row, col, row);
		sheet_make_cell_visible (sheet, col, row);
		return TRUE;
	}

	wb_control_invalid_err (wbc, _("Address"), text);
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
workbook_control_ctor_class (GtkObjectClass *object_class)
{
	parent_class = gtk_type_class (gtk_object_get_type ());
	object_class->destroy = wbc_destroy;
}

GNUMERIC_MAKE_TYPE(workbook_control, "WorkbookControl", WorkbookControl,
		   workbook_control_ctor_class, NULL, gtk_object_get_type ())

void
workbook_control_set_view (WorkbookControl *wbc,
			   WorkbookView *optional_view, Workbook *optional_wb)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (wbc->wb_view == NULL);

	wbc->wb_view = (optional_view != NULL) ? optional_view : workbook_view_new (optional_wb);
	wb_view_attach_control (wbc);
}

void
workbook_control_sheets_init (WorkbookControl *wbc)
{
	GList *sheets, *ptr;

	/* Add views all all existing sheets */
	sheets = workbook_sheets (wb_control_workbook (wbc));
	for (ptr = sheets; ptr != NULL ; ptr = ptr->next)
		wb_control_sheet_add (wbc, ptr->data);
	g_list_free (sheets);
}
