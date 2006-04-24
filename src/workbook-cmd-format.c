/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * workbook.c:  Workbook format commands hooked to the menus
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "workbook-cmd-format.h"

#include "dependent.h"
#include "ranges.h"
#include "gui-util.h"
#include "selection.h"
#include "workbook-control.h"
#include "workbook.h"
#include "application.h"
#include "dialogs.h"
#include "sheet.h"
#include "commands.h"
#include "style-border.h"
#include "style-color.h"

struct closure_colrow_resize {
	gboolean	 is_cols;
	ColRowIndexList *selection;
};

static gboolean
cb_colrow_collect (SheetView *sv, GnmRange const *r, gpointer user_data)
{
	struct closure_colrow_resize *info = user_data;
	int first, last;

	if (info->is_cols) {
		first = r->start.col;
		last = r->end.col;
	} else {
		first = r->start.row;
		last = r->end.row;
	}

	info->selection = colrow_get_index_list (first, last, info->selection);
	return TRUE;
}

void
workbook_cmd_resize_selected_colrow (WorkbookControl *wbc, Sheet *sheet,
				     gboolean is_cols, int new_size_pixels)
{
	struct closure_colrow_resize closure;
	closure.is_cols = is_cols;
	closure.selection = NULL;
	selection_foreach_range (sheet_get_view (sheet, wb_control_view (wbc)),
				 TRUE, &cb_colrow_collect, &closure);
	cmd_resize_colrow (wbc, sheet, is_cols, closure.selection, new_size_pixels);
}
