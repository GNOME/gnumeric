/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * workbook.c:  Workbook format commands hooked to the menus
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
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
#include "format.h"
#include "sheet.h"
#include "commands.h"
#include "style-border.h"
#include "style-color.h"

#include <libgnome/gnome-i18n.h>

/* Adds borders to all the selected regions on the sheet.
 * FIXME: This is a little more simplistic then it should be, it always
 * removes and/or overwrites any borders. What we should do is
 * 1) When adding -> don't add a border if the border is thicker than 'THIN'
 * 2) When removing -> don't remove unless the border is 'THIN'
 */
void
workbook_cmd_mutate_borders (WorkbookControl *wbc, Sheet *sheet, gboolean add)
{
	StyleBorder *borders [STYLE_BORDER_EDGE_MAX];
	int i;

	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; ++i)
		if (i <= STYLE_BORDER_RIGHT)
			borders[i] = style_border_fetch (
				add ? STYLE_BORDER_THIN : STYLE_BORDER_NONE,
				style_color_black (), style_border_get_orientation (i));
		else
			borders[i] = NULL;

	cmd_selection_format (wbc, NULL, borders,
			      add ? _("Add Borders") : _("Remove borders"));
}

struct closure_colrow_resize {
	gboolean	 is_cols;
	ColRowIndexList *selection;
};

static gboolean
cb_colrow_collect (SheetView *sv, Range const *r, gpointer user_data)
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
workbook_cmd_resize_selected_colrow (WorkbookControl *wbc, gboolean is_cols,
				     Sheet *sheet, int new_size_pixels)
{
	struct closure_colrow_resize closure;
	closure.is_cols = is_cols;
	closure.selection = NULL;
	selection_foreach_range (sheet_get_view (sheet, wb_control_view (wbc)),
				 TRUE, &cb_colrow_collect, &closure);
	cmd_resize_colrow (wbc, sheet, is_cols, closure.selection, new_size_pixels);
}

void
workbook_cmd_format_column_auto_fit (GtkWidget *widget, WorkbookControl *wbc)
{
	Sheet *sheet = wb_control_cur_sheet (wbc);
	workbook_cmd_resize_selected_colrow (wbc, TRUE, sheet, -1);
}

void
sheet_dialog_set_column_width (GtkWidget *ignored, WorkbookControlGUI *wbcg)
{
	dialog_col_width (wbcg, FALSE);
}

void
workbook_cmd_format_row_auto_fit (GtkWidget *widget, WorkbookControl *wbc)
{
	Sheet *sheet = wb_control_cur_sheet (wbc);
	workbook_cmd_resize_selected_colrow (wbc, FALSE, sheet, -1);
}

void
sheet_dialog_set_row_height (GtkWidget *ignored, WorkbookControlGUI *wbcg)
{
	dialog_row_height (wbcg, FALSE);
}

void
workbook_cmd_format_column_hide (GtkWidget *widget, WorkbookControl *wbc)
{
	cmd_selection_colrow_hide (wbc, TRUE, FALSE);
}

void
workbook_cmd_format_column_unhide (GtkWidget *widget, WorkbookControl *wbc)
{
	cmd_selection_colrow_hide (wbc, TRUE, TRUE);
}

void
workbook_cmd_format_column_std_width (GtkWidget *widget, WorkbookControl *wbc)
{
	dialog_col_width ((WorkbookControlGUI *)wbc, TRUE);
}

void
workbook_cmd_format_row_std_height (GtkWidget *widget, WorkbookControl *wbc)
{
	dialog_row_height ((WorkbookControlGUI *)wbc, TRUE);
}

void
workbook_cmd_format_row_hide (GtkWidget *widget, WorkbookControl *wbc)
{
	cmd_selection_colrow_hide (wbc, FALSE, FALSE);
}

void
workbook_cmd_format_row_unhide (GtkWidget *widget, WorkbookControl *wbc)
{
	cmd_selection_colrow_hide (wbc, FALSE, TRUE);
}
