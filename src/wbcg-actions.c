/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * wbcg-actions.c: The callbacks and tables for all the menus and stock toolbars
 *
 * Copyright (C) 2003-2004 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#include "libgnumeric.h"
#include "application.h"
#include "print.h"
#include "commands.h"
#include "clipboard.h"
#include "selection.h"
#include "search.h"
#include "ranges.h"
#include "cell.h"
#include "stf.h"
#include "value.h"
#include "format.h"
#include "sheet.h"
#include "sort.h"
#include "sheet-merge.h"
#include "sheet-filter.h"
#include "sheet-style.h"
#include "style-border.h"
#include "style-color.h"
#include "tools/filter.h"
#include "sheet-control-gui-priv.h"
#include "sheet-view.h"
#include "cmd-edit.h"
#include "gnumeric-canvas.h"
#include "workbook-priv.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook-control-gui-priv.h"
#include "workbook-control.h"
#include "workbook-cmd-format.h"
#include "io-context.h"
#include "plugin-util.h"
#include "dialogs/dialogs.h"
#include "sheet-object-image.h"
#include "sheet-object-widget.h"
#include "sheet-object-graphic.h"
#include "sheet-object-graph.h"

#include "gui-util.h"
#include "gui-file.h"
#include "gnumeric-gconf.h"
#include <goffice/graph/gog-guru.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-data-set.h>
#include <goffice/utils/go-file.h>

#include "widgets/widget-editable-label.h"
#include <gtk/gtkactiongroup.h>
#include <gtk/gtktoggleaction.h>
#include <gtk/gtkstock.h>
#include <glib/gi18n.h>
#include <gsf/gsf-input.h>
#include <string.h>

static GNM_ACTION_DEF (cb_file_new)
{
	GdkScreen *screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
	Workbook *wb = workbook_new_with_sheets
		(gnm_app_prefs->initial_sheet_number);
	WorkbookControl *new_wbc = workbook_control_gui_new (NULL, wb, screen);
	WorkbookControlGUI *new_wbcg = WORKBOOK_CONTROL_GUI (new_wbc);
	wbcg_copy_toolbar_visibility (new_wbcg, wbcg);	
}

static GNM_ACTION_DEF (cb_file_open)	{ gui_file_open (wbcg, NULL); }
static GNM_ACTION_DEF (cb_file_save)	{ gui_file_save (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg))); }
static GNM_ACTION_DEF (cb_file_save_as)	{ gui_file_save_as (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg))); }
static GNM_ACTION_DEF (cb_file_sendto)	{
	wb_view_sendto (wb_control_view (WORKBOOK_CONTROL (wbcg)), GNM_CMD_CONTEXT (wbcg)); }
static GNM_ACTION_DEF (cb_file_page_setup) { dialog_printer_setup (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_file_print)	{
	sheet_print (wbcg, wbcg_cur_sheet (wbcg), FALSE, PRINT_ACTIVE_SHEET); }
static GNM_ACTION_DEF (cb_file_print_preview) {
	sheet_print (wbcg, wbcg_cur_sheet (wbcg), TRUE, PRINT_ACTIVE_SHEET); }
static GNM_ACTION_DEF (cb_file_summary)		{ dialog_summary_update (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_file_preferences)	{ dialog_preferences (wbcg, 0); }
static GNM_ACTION_DEF (cb_file_close)		{ wbcg_close_control (wbcg); }

static GNM_ACTION_DEF (cb_file_quit)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GList *ptr, *workbooks, *clean_no_closed = NULL;
	gboolean ok = TRUE;
	gboolean ask_user = TRUE;
	gboolean discard_all = FALSE;

	/* If we are still loading initial files, short circuit */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		return;
	}

	/* If we were editing when the quit request came in abort the edit. */
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);

	/* list is modified during workbook destruction */
	workbooks = g_list_copy (gnm_app_workbook_list ());

	for (ptr = workbooks; ok && ptr != NULL ; ptr = ptr->next) {
		Workbook *wb = ptr->data;
		WorkbookView *wb_view;
		GList *old_ptr;

		g_return_if_fail (IS_WORKBOOK (wb));
		g_return_if_fail (wb->wb_views != NULL);

		if (wb_control_workbook (wbc) == wb)
			continue;
		if (discard_all) {

		} else {
			wb_view = g_ptr_array_index (wb->wb_views, 0);
			switch (wbcg_close_if_user_permits (wbcg, wb_view, FALSE,
								TRUE, ask_user)) {
			case 0 : ok = FALSE;	/* canceled */
				break;
			case 1 :		/* closed */
				break;
			case 2 : clean_no_closed = g_list_prepend (clean_no_closed, wb);
				break;
			case 3 :		/* save_all */
				ask_user = FALSE;
				break;
			case 4 :		/* discard_all */
				discard_all = TRUE;
				old_ptr = ptr;
				for (ptr = ptr->next; ptr != NULL ; ptr = ptr->next) {
					Workbook *wba = ptr->data;
					old_ptr = ptr;
					if (wb_control_workbook (wbc) == wba)
						continue;
					workbook_set_dirty (wba, FALSE);
					workbook_unref (wba);
				}
				ptr = old_ptr;
				break;
			};
		}
	}

	if (discard_all) {
		workbook_set_dirty (wb_control_workbook (wbc), FALSE);
		workbook_unref (wb_control_workbook (wbc));
		for (ptr = clean_no_closed; ptr != NULL ; ptr = ptr->next)
			workbook_unref (ptr->data);
	} else
	/* only close pristine books if nothing was canceled. */
	if (ok && wbcg_close_if_user_permits (wbcg,
					      wb_control_view (wbc), TRUE, TRUE, ask_user)
	    > 0)
		for (ptr = clean_no_closed; ptr != NULL ; ptr = ptr->next)
			workbook_unref (ptr->data);

	g_list_free (workbooks);
	g_list_free (clean_no_closed);
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_edit_clear_all)
{
	cmd_selection_clear (WORKBOOK_CONTROL (wbcg),
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_COMMENTS);
}

static GNM_ACTION_DEF (cb_edit_clear_formats)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_FORMATS); }
static GNM_ACTION_DEF (cb_edit_clear_comments)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_COMMENTS); }
static GNM_ACTION_DEF (cb_edit_clear_content)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_VALUES); }

static GNM_ACTION_DEF (cb_edit_delete_rows)
{
	WorkbookControl *wbc   = WORKBOOK_CONTROL (wbcg);
	SheetView       *sv    = wb_control_cur_sheet_view (wbc);
	Sheet           *sheet = wb_control_cur_sheet (wbc);
	GnmRange const *sel;
	int rows;

	if (!(sel = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), _("Delete"))))
		return;
	rows = range_height (sel);

	cmd_delete_rows (wbc, sheet, sel->start.row, rows);
}
static GNM_ACTION_DEF (cb_edit_delete_columns)
{
	WorkbookControl *wbc   = WORKBOOK_CONTROL (wbcg);
	SheetView       *sv    = wb_control_cur_sheet_view (wbc);
	Sheet           *sheet = wb_control_cur_sheet (wbc);
	GnmRange const *sel;
	int cols;

	if (!(sel = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), _("Delete"))))
		return;
	cols = range_width (sel);

	cmd_delete_cols (wbc, sheet, sel->start.col, cols);
}

static GNM_ACTION_DEF (cb_edit_delete_cells)
{
	dialog_delete_cells (wbcg);
}

static GNM_ACTION_DEF (cb_edit_select_all)
{
	scg_select_all (wbcg_cur_scg (wbcg));
}
static GNM_ACTION_DEF (cb_edit_select_row)
{
	sv_select_cur_row (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_col)
{
	sv_select_cur_col (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_array)
{
	sv_select_cur_array (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_depends)
{
	sv_select_cur_depends (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_inputs)
{
	sv_select_cur_inputs (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}

static GNM_ACTION_DEF (cb_edit_cut)
{
	if (!wbcg_is_editing (wbcg)) {
		WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		sv_selection_cut (sv, wbc);
	} else
		gtk_editable_cut_clipboard (GTK_EDITABLE (wbcg_get_entry (wbcg)));
}

static GNM_ACTION_DEF (cb_edit_copy)
{
	if (!wbcg_is_editing (wbcg)) {
		WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		sv_selection_copy (sv, wbc);
	} else
		gtk_editable_copy_clipboard (GTK_EDITABLE (wbcg_get_entry (wbcg)));
}

static GNM_ACTION_DEF (cb_edit_paste)
{
	if (!wbcg_is_editing (wbcg)) {
		WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		cmd_paste_to_selection (wbc, sv, PASTE_DEFAULT);
	} else
		gtk_editable_paste_clipboard (GTK_EDITABLE (wbcg_get_entry (wbcg)));
}

static GNM_ACTION_DEF (cb_edit_paste_special)
{
	dialog_paste_special (wbcg);
}

static GNM_ACTION_DEF (cb_sheet_remove)
{
	SheetControlGUI *res;
	if (wbcg_sheet_to_page_index (wbcg, wbcg_cur_sheet (wbcg), &res) >= 0)
		scg_delete_sheet_if_possible (NULL, res);
}

static GNM_ACTION_DEF (cb_edit_duplicate_sheet)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *old_sheet = wb_control_cur_sheet (wbc);
	cmd_clone_sheet (wbc, old_sheet);
}

static void
common_cell_goto (WorkbookControlGUI *wbcg, Sheet *sheet, GnmCellPos const *pos)
{
	SheetView *sv = sheet_get_view (sheet,
		wb_control_view (WORKBOOK_CONTROL (wbcg)));

	wb_view_sheet_focus (sv_wbv (sv), sheet);
	sv_selection_set (sv, pos,
		pos->col, pos->row,
		pos->col, pos->row);
	sv_make_cell_visible (sv, pos->col, pos->row, FALSE);
}

static int
cb_edit_search_replace_query (SearchReplaceQuery q, GnmSearchReplace *sr, ...)
{
	int res = 0;
	va_list pvar;
	WorkbookControlGUI *wbcg = sr->user_data;

	va_start (pvar, sr);

	switch (q) {
	case SRQ_fail: {
		GnmCell *cell = va_arg (pvar, GnmCell *);
		char const *old_text = va_arg (pvar, const char *);
		char const *new_text = va_arg (pvar, const char *);

		char *err;

		err = g_strdup_printf
			(_("In cell %s, the current contents\n"
			   "        %s\n"
			   "would have been replaced by\n"
			   "        %s\n"
			   "which is invalid.\n\n"
			   "The replace has been aborted "
			   "and nothing has been changed."),
			 cellpos_as_string (&cell->pos),
			 old_text,
			 new_text);

		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR, err);
		g_free (err);
		break;
	}

	case SRQ_query: {
		GnmCell *cell = va_arg (pvar, GnmCell *);
		char const *old_text = va_arg (pvar, const char *);
		char const *new_text = va_arg (pvar, const char *);
		Sheet *sheet = cell->base.sheet;
		char *pos_name = g_strconcat (sheet->name_unquoted, "!",
					      cell_name (cell), NULL);

		common_cell_goto (wbcg, sheet, &cell->pos);

		res = dialog_search_replace_query (wbcg, sr, pos_name,
						   old_text, new_text);
		g_free (pos_name);
		break;
	}

	case SRQ_querycommment: {
		Sheet *sheet = va_arg (pvar, Sheet *);
		GnmCellPos *cp = va_arg (pvar, GnmCellPos *);
		const char *old_text = va_arg (pvar, const char *);
		const char *new_text = va_arg (pvar, const char *);
		char *pos_name = g_strdup_printf (_("Comment in cell %s!%s"),
						  sheet->name_unquoted,
						  cellpos_as_string (cp));
		common_cell_goto (wbcg, sheet, cp);

		res = dialog_search_replace_query (wbcg, sr, pos_name,
						   old_text, new_text);
		g_free (pos_name);
		break;
	}

	}

	va_end (pvar);
	return res;
}

static gboolean
cb_edit_search_replace_action (WorkbookControlGUI *wbcg,
			       GnmSearchReplace *sr)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);

	sr->query_func = cb_edit_search_replace_query;
	sr->user_data = wbcg;

	return cmd_search_replace (wbc, sheet, sr);
}


static GNM_ACTION_DEF (cb_edit_search_replace) { dialog_search_replace (wbcg, cb_edit_search_replace_action); }
static GNM_ACTION_DEF (cb_edit_search) { dialog_search (wbcg); }

static GNM_ACTION_DEF (cb_edit_fill_autofill)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	Sheet	  *sheet = wb_control_cur_sheet (wbc);

	GnmRange const *total = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), _("Autofill"));
	if (total) {
		GnmRange src = *total;
		gboolean do_loop;
		GSList *merges, *ptr;

		/* This could be more efficient, but it is not important */
		if (range_trim (sheet, &src, TRUE) ||
		    range_trim (sheet, &src, FALSE))
			return; /* Region totally empty */

		/* trim is a bit overzealous, it forgets about merges */
		do {
			do_loop = FALSE;
			merges = sheet_merge_get_overlap (sheet, &src);
			for (ptr = merges ; ptr != NULL ; ptr = ptr->next) {
				GnmRange const *r = ptr->data;
				if (src.end.col < r->end.col) {
					src.end.col = r->end.col;
					do_loop = TRUE;
				}
				if (src.end.row < r->end.row) {
					src.end.row = r->end.row;
					do_loop = TRUE;
				}
			}
		} while (do_loop);

		/* Make it autofill in only one direction */
 		if ((total->end.col - src.end.col) >=
		    (total->end.row - src.end.row))
 			src.end.row = total->end.row;
 		else
 			src.end.col = total->end.col;

 		cmd_autofill (wbc, sheet, FALSE,
			      total->start.col, total->start.row,
			      src.end.col - total->start.col + 1,
			      src.end.row - total->start.row + 1,
			      total->end.col, total->end.row,
			      FALSE);
 	}
}

static GNM_ACTION_DEF (cb_edit_fill_series)
{
	dialog_fill_series (wbcg);
}

static GNM_ACTION_DEF (cb_edit_goto)
{
	dialog_goto_cell (wbcg);
}

static GNM_ACTION_DEF (cb_edit_recalc)
{
	/* TODO :
	 * f9  -  do any necessary calculations across all sheets
	 * shift-f9  -  do any necessary calcs on current sheet only
	 * ctrl-alt-f9 -  force a full recalc across all sheets
	 * ctrl-alt-shift-f9  -  a full-monty super recalc
	 */
	workbook_recalc_all (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));
}

static GNM_ACTION_DEF (cb_repeat)	{ command_repeat (WORKBOOK_CONTROL (wbcg)); }

/****************************************************************************/

static GNM_ACTION_DEF (cb_view_zoom)	{ dialog_zoom (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_view_new)	{ dialog_new_view (wbcg); }
static GNM_ACTION_DEF (cb_view_freeze_panes)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);

	scg_mode_edit (SHEET_CONTROL (scg));
	if (scg->active_panes == 1) {
		gboolean center = FALSE;
		GnmCanvas const *gcanvas = scg_pane (scg, 0);
		GnmCellPos frozen_tl, unfrozen_tl;

		frozen_tl = gcanvas->first;
		unfrozen_tl = sv->edit_pos;

		/* If edit pos is out of visible range */
		if (unfrozen_tl.col < gcanvas->first.col ||
		    unfrozen_tl.col > gcanvas->last_visible.col ||
		    unfrozen_tl.row < gcanvas->first.row ||
		    unfrozen_tl.row > gcanvas->last_visible.row)
			center = TRUE;

		if (unfrozen_tl.col == gcanvas->first.col) {
			/* or edit pos is in top left visible cell */
			if (unfrozen_tl.row == gcanvas->first.row)
				center = TRUE;
			else
				unfrozen_tl.col = frozen_tl.col = 0;
		} else if (unfrozen_tl.row == gcanvas->first.row)
			unfrozen_tl.row = frozen_tl.row = 0;

		if (center) {
			unfrozen_tl.col = (gcanvas->first.col + gcanvas->last_visible.col) / 2;
			unfrozen_tl.row = (gcanvas->first.row + gcanvas->last_visible.row) / 2;
		}

		g_return_if_fail (unfrozen_tl.col > frozen_tl.col ||
				  unfrozen_tl.row > frozen_tl.row);

		sv_freeze_panes (sv, &frozen_tl, &unfrozen_tl);
	} else
		sv_freeze_panes (sv, NULL, NULL);
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_insert_current_date)
{
	if (wbcg_edit_start (wbcg, FALSE, FALSE)) {
		Workbook const *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
		GnmValue *v = value_new_int (
			datetime_timet_to_serial (time (NULL), workbook_date_conv (wb)));
		char *txt = format_value (style_format_default_date (), v, NULL, -1,
				workbook_date_conv (wb));
		value_release (v);
		wb_control_edit_line_set (WORKBOOK_CONTROL (wbcg), txt);
		g_free (txt);
	}
}

static GNM_ACTION_DEF (cb_insert_current_time)
{
	if (wbcg_edit_start (wbcg, FALSE, FALSE)) {
		Workbook const *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
		GnmValue *v = value_new_float (datetime_timet_to_seconds (time (NULL)) / (24.0 * 60 * 60));
		char *txt = format_value (style_format_default_time (), v, NULL, -1,
				workbook_date_conv (wb));
		value_release (v);
		wb_control_edit_line_set (WORKBOOK_CONTROL (wbcg), txt);
		g_free (txt);
	}
}

static GNM_ACTION_DEF (cb_define_name)
{
	dialog_define_names (wbcg);
}

static GNM_ACTION_DEF (cb_insert_rows)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet     *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const *sel;

	/* TODO : No need to check simplicty.  XL applies for each non-discrete
	 * selected region, (use selection_apply).  Arrays and Merged regions
	 * are permitted.
	 */
	if (!(sel = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), _("Insert rows"))))
		return;
	cmd_insert_rows (wbc, sheet, sel->start.row, range_height (sel));
}

static GNM_ACTION_DEF (cb_insert_cols)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const *sel;

	/* TODO : No need to check simplicty.  XL applies for each non-discrete
	 * selected region, (use selection_apply).  Arrays and Merged regions
	 * are permitted.
	 */
	if (!(sel = selection_first_range (sv, GNM_CMD_CONTEXT (wbc),
					   _("Insert columns"))))
		return;
	cmd_insert_cols (wbc, sheet, sel->start.col, range_width (sel));
}

static GNM_ACTION_DEF (cb_insert_cells) { dialog_insert_cells (wbcg); }

static GNM_ACTION_DEF (cb_insert_comment)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	dialog_cell_comment (wbcg, sheet, &sv->edit_pos);
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_sheet_name)
{
	SheetControlGUI *scg = wbcg_cur_scg(wbcg);
	editable_label_start_editing (EDITABLE_LABEL(scg->label));
}

static GNM_ACTION_DEF (cb_sheet_order)		{ dialog_sheet_order (wbcg); }
static GNM_ACTION_DEF (cb_format_cells)		{ dialog_cell_format (wbcg, FD_CURRENT); }
static GNM_ACTION_DEF (cb_autoformat)		{ dialog_autoformat (wbcg); }
static GNM_ACTION_DEF (cb_workbook_attr)	{ dialog_workbook_attr (wbcg); }
static GNM_ACTION_DEF (cb_format_preferences)	{ dialog_preferences (wbcg, 1); }
static GNM_ACTION_DEF (cb_tools_plugins)	{ dialog_plugin_manager (wbcg); }
static GNM_ACTION_DEF (cb_tools_autocorrect)	{ dialog_autocorrect (wbcg); }
static GNM_ACTION_DEF (cb_tools_auto_save)	{ dialog_autosave (wbcg); }
static GNM_ACTION_DEF (cb_tools_goal_seek)	{ dialog_goal_seek (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_tabulate)	{ dialog_tabulate (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_merge)		{ dialog_merge (wbcg); }
static GNM_ACTION_DEF (cb_tools_solver)		{ dialog_solver (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_scenario_add)	{ dialog_scenario_add (wbcg); }
static GNM_ACTION_DEF (cb_tools_scenarios)	{ dialog_scenarios (wbcg); }
static GNM_ACTION_DEF (cb_tools_simulation)	{ dialog_simulation (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_anova_one_factor) { dialog_anova_single_factor_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_anova_two_factor) { dialog_anova_two_factor_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_correlation)	{ dialog_correlation_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_covariance)	{ dialog_covariance_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_desc_statistics) { dialog_descriptive_stat_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_exp_smoothing)	{ dialog_exp_smoothing_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_average)	{ dialog_average_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_fourier)	{ dialog_fourier_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_histogram)	{ dialog_histogram_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_ranking)	{ dialog_ranking_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_regression)	{ dialog_regression_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_sampling)	{ dialog_sampling_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_ttest_paired)	{ dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_PAIRED); }
static GNM_ACTION_DEF (cb_tools_ttest_equal_var) { dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_UNPAIRED_EQUALVARIANCES); }
static GNM_ACTION_DEF (cb_tools_ttest_unequal_var) { dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_UNPAIRED_UNEQUALVARIANCES); }
static GNM_ACTION_DEF (cb_tools_ztest)		{ dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_ZTEST); }
static GNM_ACTION_DEF (cb_tools_ftest)		{ dialog_ftest_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_random_generator) { dialog_random_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_data_sort)		{ dialog_cell_sort (wbcg); }
static GNM_ACTION_DEF (cb_data_shuffle)		{ dialog_shuffle (wbcg); }
static GNM_ACTION_DEF (cb_data_import_text)	{ gui_file_open (wbcg, "Gnumeric_stf:stf_druid"); }

static GNM_ACTION_DEF (cb_auto_filter)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmFilter *filter = sv_first_selection_in_filter (sv);

#warning Add undo/redo
	if (filter == NULL) {
		GnmRange region;
		GnmRange const *src = selection_first_range (sv,
			GNM_CMD_CONTEXT (wbcg), _("Add Filter"));

		if (src == NULL)
			return;

		/* only one row selected -- assume that the user wants to
		 * filter the region below this row. */
		region = *src;
		if (src->start.row == src->end.row)
			sheet_filter_guess_region  (sv->sheet, &region);
		if (region.start.row == region.end.row) {
			gnm_cmd_context_error_invalid	(GNM_CMD_CONTEXT (wbcg),
				 _("AutoFilter"), _("Requires more than 1 row"));
			return;
		}
		gnm_filter_new (sv->sheet, &region);
	} else {
		/* keep distinct to simplify undo/redo later */
		gnm_filter_remove (filter);
		gnm_filter_free (filter);
	}
	sheet_update (sv->sheet);
}

static GNM_ACTION_DEF (cb_show_all)		{ filter_show_all (wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_data_filter)		{ dialog_advanced_filter (wbcg); }
static GNM_ACTION_DEF (cb_data_validate)	{ dialog_cell_format (wbcg, FD_VALIDATION); }
static GNM_ACTION_DEF (cb_data_text_to_columns) { stf_text_to_columns (WORKBOOK_CONTROL (wbcg), GNM_CMD_CONTEXT (wbcg)); }
static GNM_ACTION_DEF (cb_data_consolidate)	{ dialog_consolidate (wbcg); }

#ifdef ENABLE_PIVOTS
static GNM_ACTION_DEF (cb_data_pivottable)	{ dialog_pivottable (wbcg); }
#endif

static void
hide_show_detail_real (WorkbookControlGUI *wbcg, gboolean is_cols, gboolean show)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	char const *operation = show ? _("Show Detail") : _("Hide Detail");
	GnmRange const *r = selection_first_range (sv, GNM_CMD_CONTEXT (wbc),
						operation);

	/* This operation can only be performed on a whole existing group */
	if (sheet_colrow_can_group (sv_sheet (sv), r, is_cols)) {
		gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc), operation,
					_("can only be performed on an existing group"));
		return;
	}

	cmd_selection_colrow_hide (wbc, is_cols, show);
}

static void
hide_show_detail (WorkbookControlGUI *wbcg, gboolean show)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	char const *operation = show ? _("Show Detail") : _("Hide Detail");
	GnmRange const *r = selection_first_range (sv, GNM_CMD_CONTEXT (wbc),
						operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, TRUE) ^ range_is_full (r, FALSE))
		is_cols = !range_is_full (r, TRUE);
	else {
		GtkWidget *dialog = dialog_col_row (wbcg, operation,
						    (ColRowCallback_t) hide_show_detail_real,
						    GINT_TO_POINTER (show));
		gtk_widget_show (dialog);
		return;
	}

	hide_show_detail_real (wbcg, is_cols, show);
}

static void
group_ungroup_colrow (WorkbookControlGUI *wbcg, gboolean group)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	char const *operation = group ? _("Group") : _("Ungroup");
	GnmRange const *r = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, TRUE) ^ range_is_full (r, FALSE))
		is_cols = !range_is_full (r, TRUE);
	else {
		GtkWidget *dialog = dialog_col_row (wbcg, operation,
			(ColRowCallback_t) cmd_selection_group,
			GINT_TO_POINTER (group));
		gtk_widget_show (dialog);
		return;
	}

	cmd_selection_group (wbc, is_cols, group);
}

static GNM_ACTION_DEF (cb_data_hide_detail)	{ hide_show_detail (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_data_show_detail)	{ hide_show_detail (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_group)		{ group_ungroup_colrow (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_ungroup)		{ group_ungroup_colrow (wbcg, FALSE); }

static GNM_ACTION_DEF (cb_help_docs)
{
	char   *argv[] = { (char *)"yelp", NULL, NULL };
	GError *error = NULL;

#warning handle translations when we generate them
	argv[1] = gnumeric_sys_data_dir ("doc/C/gnumeric.xml");
	g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
		NULL, NULL, NULL, &error);
	g_free (argv[1]);
}

static GNM_ACTION_DEF (cb_help_web) { }
static GNM_ACTION_DEF (cb_help_irc) { }
static GNM_ACTION_DEF (cb_help_bug) { }
static GNM_ACTION_DEF (cb_help_about) { dialog_about (wbcg); }

static GNM_ACTION_DEF (cb_autosum)
{
	GtkEntry *entry;
	gchar const *txt;

	if (wbcg_is_editing (wbcg))
		return;

	entry = wbcg_get_entry (wbcg);
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=sum(", 5)) {
		if (!wbcg_edit_start (wbcg, TRUE, TRUE))
			return; /* attempt to edit failed */
		gtk_entry_set_text (entry, "=sum()");
		gtk_editable_set_position (GTK_EDITABLE (entry), 5);
	} else {
		if (!wbcg_edit_start (wbcg, FALSE, TRUE))
			return; /* attempt to edit failed */

		/*
		 * FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_editable_set_position (GTK_EDITABLE (entry), entry->text_length-1);
	}
}

static GNM_ACTION_DEF (cb_insert_image)
{
	char *uri = gui_image_file_select (wbcg, NULL, FALSE);

	if (uri) {
		GError *err = NULL;
		GsfInput *input = go_file_open (uri, &err);

		if (input != NULL) {
			unsigned len = gsf_input_size (input);
			guint8 const *data = gsf_input_read (input, len, NULL);
			scg_mode_create_object (wbcg_cur_scg (wbcg),
				sheet_object_image_new ("", (guint8 *)data, len, TRUE));
			g_object_unref (input);
		} else
			gnm_cmd_context_error (GNM_CMD_CONTEXT (wbcg), err);

		g_free (uri);
	}
}

static GNM_ACTION_DEF (cb_insert_hyperlink)	{ dialog_hyperlink (wbcg, SHEET_CONTROL (wbcg_cur_scg (wbcg))); }
static GNM_ACTION_DEF (cb_formula_guru)		{ dialog_formula_guru (wbcg, NULL); }

static void
sort_by_rows (WorkbookControlGUI *wbcg, gboolean descending)
{
	SheetView *sv;
	GnmRange *sel;
	GnmRange const *tmp;
	GnmSortData *data;
	GnmSortClause *clause;
	int numclause, i;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));

	if (!(tmp = selection_first_range (sv, GNM_CMD_CONTEXT (wbcg), _("Sort"))))
		return;

	sel = range_dup (tmp);
	range_clip_to_finite (sel, sv_sheet (sv));

	numclause = range_width (sel);
	clause = g_new0 (GnmSortClause, numclause);
	for (i=0; i < numclause; i++) {
		clause[i].offset = i;
		clause[i].asc = descending;
		clause[i].cs = gnm_app_prefs->sort_default_by_case;
		clause[i].val = TRUE;
	}

	data = g_new (GnmSortData, 1);
	data->sheet = sv_sheet (sv);
	data->range = sel;
	data->num_clause = numclause;
	data->clauses = clause;

	data->retain_formats = gnm_app_prefs->sort_default_retain_formats;

	/* Hard code sorting by row.  I would prefer not to, but user testing
	 * indicates
	 * - that the button should always does the same things
	 * - that the icon matches the behavior
	 * - XL does this.
	 */
	data->top = TRUE;

	if (range_has_header (data->sheet, data->range, data->top, FALSE))
		data->range->start.row += 1;

	cmd_sort (WORKBOOK_CONTROL (wbcg), data);
}
static GNM_ACTION_DEF (cb_sort_ascending)  { sort_by_rows (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_sort_descending) { sort_by_rows (wbcg, TRUE); }

static void
cb_add_graph (GogGraph *graph, gpointer wbcg)
{
	SheetControlGUI *scg = wbcg_cur_scg (WORKBOOK_CONTROL_GUI (wbcg));
	scg_mode_create_object (scg, sheet_object_graph_new (graph));
}

static GNM_ACTION_DEF (cb_launch_chart_guru)
{
	GClosure *closure = g_cclosure_new (G_CALLBACK (cb_add_graph),
					    wbcg, NULL);
	sheet_object_graph_guru (wbcg, NULL, closure);
	g_closure_sink (closure);
}

static void
create_object (WorkbookControlGUI *wbcg, SheetObject *so)
{
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));

	scg_mode_create_object (scg, so);
	workbook_recalc (sheet->workbook);
	sheet_update (sheet);
}

static void
create_object_type (WorkbookControlGUI *wbcg, GType t)
{
	create_object (wbcg, g_object_new (t, NULL));
}

static GNM_ACTION_DEF (cmd_create_label)
	{ create_object_type (wbcg, sheet_object_text_get_type ()); }
static GNM_ACTION_DEF (cmd_create_frame)
	{ create_object_type (wbcg, sheet_widget_frame_get_type()); }
static GNM_ACTION_DEF (cmd_create_button)
	{ create_object_type (wbcg, sheet_widget_button_get_type()); }
static GNM_ACTION_DEF (cmd_create_radiobutton)
	{ create_object_type (wbcg, sheet_widget_radio_button_get_type()); }
static GNM_ACTION_DEF (cmd_create_scrollbar)
	{ create_object_type (wbcg, sheet_widget_scrollbar_get_type()); }
static GNM_ACTION_DEF (cmd_create_slider)
	{ create_object_type (wbcg, sheet_widget_slider_get_type()); }
static GNM_ACTION_DEF (cmd_create_spinbutton)
	{ create_object_type (wbcg, sheet_widget_spinbutton_get_type()); }
static GNM_ACTION_DEF (cmd_create_checkbox)
	{ create_object_type (wbcg, sheet_widget_checkbox_get_type()); }
static GNM_ACTION_DEF (cmd_create_list)
	{ create_object_type (wbcg, sheet_widget_list_get_type()); }
static GNM_ACTION_DEF (cmd_create_combo)
	{ create_object_type (wbcg, sheet_widget_combo_get_type()); }
static GNM_ACTION_DEF (cmd_create_line)
	{ create_object (wbcg, sheet_object_line_new (FALSE)); }
static GNM_ACTION_DEF (cmd_create_arrow)
	{ create_object (wbcg, sheet_object_line_new (TRUE)); }
static GNM_ACTION_DEF (cmd_create_rectangle)
	{ create_object (wbcg, sheet_object_box_new (FALSE)); }
static GNM_ACTION_DEF (cmd_create_ellipse)
	{ create_object (wbcg, sheet_object_box_new (TRUE)); }

void
wbcg_set_selection_halign (WorkbookControlGUI *wbcg, StyleHAlignFlags halign)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView	*wb_view;
	GnmStyle *style;

	if (wbcg->updating_ui)
		return;

	/* This is a toggle button.  If we are already enabled
	 * then revert to general */
	wb_view = wb_control_view (wbc);
	style = wb_view->current_format;
	if (mstyle_get_align_h (style) == halign)
		halign = HALIGN_GENERAL;

	style = mstyle_new ();
	mstyle_set_align_h (style, halign);
	cmd_selection_format (wbc, style, NULL, _("Set Horizontal Alignment"));
}

static GNM_ACTION_DEF (cb_align_left)
	{ wbcg_set_selection_halign (wbcg, HALIGN_LEFT); }
static GNM_ACTION_DEF (cb_align_right)
	{ wbcg_set_selection_halign (wbcg, HALIGN_RIGHT); }
static GNM_ACTION_DEF (cb_align_center)
	{ wbcg_set_selection_halign (wbcg, HALIGN_CENTER); }
static GNM_ACTION_DEF (cb_center_across_selection)
	{ wbcg_set_selection_halign (wbcg, HALIGN_CENTER_ACROSS_SELECTION); }

void
wbcg_set_selection_valign (WorkbookControlGUI *wbcg, StyleVAlignFlags valign)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView	*wb_view;
	GnmStyle *style;

	if (wbcg->updating_ui)
		return;

	/* This is a toggle button.  If we are already enabled
	 * then revert to general */
	wb_view = wb_control_view (wbc);
	style = wb_view->current_format;
	if (mstyle_get_align_h (style) == valign) {
		if (valign == VALIGN_BOTTOM)
			return;
		valign = VALIGN_BOTTOM;
	}

	style = mstyle_new ();
	mstyle_set_align_v (style, valign);
	cmd_selection_format (wbc, style, NULL, _("Set Vertical Alignment"));
}

static GNM_ACTION_DEF (cb_align_top)
	{ wbcg_set_selection_valign (wbcg, VALIGN_TOP); }
static GNM_ACTION_DEF (cb_align_vcenter)
	{ wbcg_set_selection_valign (wbcg, VALIGN_CENTER); }
static GNM_ACTION_DEF (cb_align_bottom)
	{ wbcg_set_selection_valign (wbcg, VALIGN_BOTTOM); }

static GNM_ACTION_DEF (cb_view_statusbar)
{
	if (!wbcg->updating_ui)
		wbcg_set_statusbar_visible (wbcg, -1);
}

static GNM_ACTION_DEF (cb_merge_cells)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GSList *range_list = selection_get_ranges (sv, FALSE);
	cmd_merge_cells (wbc, sheet, range_list);
	range_fragment_free (range_list);
}

static GNM_ACTION_DEF (cb_unmerge_cells)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GSList *range_list = selection_get_ranges (sv, FALSE);
	cmd_unmerge_cells (wbc, sheet, range_list);
	range_fragment_free (range_list);
}

static void
toggle_font_attr (WorkbookControlGUI *wbcg, GtkToggleAction *act,
		  MStyleElementType t, unsigned true_val, unsigned false_val)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *new_style;
	unsigned val;

	/* If the user did not initiate this action ignore it.
	 * This happens whenever the ui updates and the current cell makes a
	 * change to the toolbar indicators.
	 */
	if (wbcg->updating_ui)
		return;

	val = gtk_toggle_action_get_active (act) ? true_val : false_val;
	if (wbcg_is_editing (wbcg)) {
		PangoAttribute *attr = NULL;
		switch (t) {
		default :
		case MSTYLE_FONT_BOLD:
			attr = pango_attr_weight_new (val ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
			break;
		case MSTYLE_FONT_ITALIC:
			attr = pango_attr_style_new (val ?  PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
			break;
		case MSTYLE_FONT_UNDERLINE:
			attr = pango_attr_underline_new ((val == UNDERLINE_SINGLE) ? PANGO_UNDERLINE_SINGLE
				: ((val == UNDERLINE_DOUBLE) ? PANGO_UNDERLINE_DOUBLE : PANGO_UNDERLINE_NONE));
			break;
		case MSTYLE_FONT_STRIKETHROUGH:
			attr = pango_attr_strikethrough_new (val);
			break;
		}
		wbcg_edit_add_markup (wbcg, attr);
		return;
	}

	new_style = mstyle_new ();
	switch (t) {
	default :
	case MSTYLE_FONT_BOLD:	 	mstyle_set_font_bold (new_style, val); break;
	case MSTYLE_FONT_ITALIC:	mstyle_set_font_italic (new_style, val); break;
	case MSTYLE_FONT_UNDERLINE:	mstyle_set_font_uline (new_style, val); break;
	case MSTYLE_FONT_STRIKETHROUGH: mstyle_set_font_strike (new_style, val); break;
	};

	cmd_selection_format (wbc, new_style, NULL, _("Set Font Style"));
}

static void cb_font_bold (GtkToggleAction *act, WorkbookControlGUI *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_BOLD, TRUE, FALSE); }
static void cb_font_italic (GtkToggleAction *act, WorkbookControlGUI *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_ITALIC, TRUE, FALSE); }
static void cb_font_underline (GtkToggleAction *act, WorkbookControlGUI *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_UNDERLINE, UNDERLINE_SINGLE, UNDERLINE_NONE); }
static void cb_font_double_underline (GtkToggleAction *act, WorkbookControlGUI *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_UNDERLINE, UNDERLINE_DOUBLE, UNDERLINE_NONE); }
static void cb_font_strikethrough (GtkToggleAction *act, WorkbookControlGUI *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_STRIKETHROUGH, TRUE, FALSE); }

static void
apply_number_format (WorkbookControlGUI *wbcg,
		     char const *translated_format, char const *descriptor)
{
	GnmStyle *mstyle = mstyle_new ();
	mstyle_set_format_text (mstyle, translated_format);
	cmd_selection_format (WORKBOOK_CONTROL (wbcg), mstyle, NULL, descriptor);
}

static GNM_ACTION_DEF (cb_format_as_number)
{
	apply_number_format (wbcg,
		cell_formats [FMT_NUMBER][0], _("Format as Number"));
}
static GNM_ACTION_DEF (cb_format_as_currency)
{
	apply_number_format (wbcg,
		cell_formats [FMT_CURRENCY][0], _("Format as Currency"));
}
static GNM_ACTION_DEF (cb_format_as_accounting)
{
	apply_number_format (wbcg,
		cell_formats[FMT_ACCOUNT][2], _("Format as Accounting"));
}

static GNM_ACTION_DEF (cb_format_as_percentage)
{
	apply_number_format (wbcg,
		cell_formats [FMT_PERCENT][0], _("Format as Percentage"));
}
static GNM_ACTION_DEF (cb_format_as_scientific)
{
	apply_number_format (wbcg,
		cell_formats [FMT_SCIENCE][0], _("Format as Scientific"));
}
static GNM_ACTION_DEF (cb_format_as_time)
{
	apply_number_format (wbcg,
		cell_formats [FMT_DATE][0], _("Format as Date"));
}
static GNM_ACTION_DEF (cb_format_as_date)
{
	apply_number_format (wbcg,
		cell_formats [FMT_TIME][0], _("Format as Time"));
}

/* Adds borders to all the selected regions on the sheet.
 * FIXME: This is a little more simplistic then it should be, it always
 * removes and/or overwrites any borders. What we should do is
 * 1) When adding -> don't add a border if the border is thicker than 'THIN'
 * 2) When removing -> don't remove unless the border is 'THIN'
 */
static void
mutate_borders (WorkbookControlGUI *wbcg, gboolean add)
{
	GnmBorder *borders [STYLE_BORDER_EDGE_MAX];
	int i;

	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; ++i)
		if (i <= STYLE_BORDER_RIGHT)
			borders[i] = style_border_fetch (
				add ? STYLE_BORDER_THIN : STYLE_BORDER_NONE,
				style_color_black (), style_border_get_orientation (i));
		else
			borders[i] = NULL;

	cmd_selection_format (WORKBOOK_CONTROL (wbcg), NULL, borders,
		add ? _("Add Borders") : _("Remove borders"));
}

static GNM_ACTION_DEF (cb_format_add_borders)	{ mutate_borders (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_format_clear_borders)	{ mutate_borders (wbcg, FALSE); }

static void
modify_format (WorkbookControlGUI *wbcg,
	       GnmFormat *(*format_modify_fn) (GnmFormat const *format),
	       char const *descriptor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView const *wbv;
	GnmFormat *new_fmt;

	wbv = wb_control_view (wbc);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_format != NULL);

	new_fmt = (*format_modify_fn) (mstyle_get_format (wbv->current_format));
	if (new_fmt != NULL) {
		GnmStyle *style = mstyle_new ();
		mstyle_set_format (style, new_fmt);
		cmd_selection_format (wbc, style, NULL, descriptor);
		style_format_unref (new_fmt);
	}
}

static GNM_ACTION_DEF (cb_format_inc_precision)
	{ modify_format (wbcg, &format_add_decimal, _("Increase precision")); }
static GNM_ACTION_DEF (cb_format_dec_precision)
	{ modify_format (wbcg, &format_remove_decimal, _("Decrease precision")); }
static GNM_ACTION_DEF (cb_format_with_thousands)
	{ modify_format (wbcg, &format_toggle_thousands, _("Toggle thousands separator")); }

static GNM_ACTION_DEF (cb_format_inc_indent)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView const *wbv;
	int i;

	wbv = wb_control_view (wbc);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_format != NULL);

	i = mstyle_get_indent (wbv->current_format);
	if (i < 20) {
		GnmStyle *style = mstyle_new ();

		if (HALIGN_LEFT != mstyle_get_align_h (wbv->current_format))
			mstyle_set_align_h (style, HALIGN_LEFT);
		mstyle_set_indent (style, i+1);
		cmd_selection_format (wbc, style, NULL,
			    _("Increase Indent"));
	}
}

static GNM_ACTION_DEF (cb_format_dec_indent)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView const *wbv;
	int i;

	wbv = wb_control_view (wbc);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_format != NULL);

	i = mstyle_get_indent (wbv->current_format);
	if (i > 0) {
		GnmStyle *style = mstyle_new ();

		mstyle_set_indent (style, i-1);
		cmd_selection_format (wbc, style, NULL,
			    _("Decrease Indent"));
	}
}

static GNM_ACTION_DEF (cb_copydown)
{
	WorkbookControl  *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_copyrel (wbc, 0, -1, _("Copy down"));
}

static GNM_ACTION_DEF (cb_copyright)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_copyrel (wbc, -1, 0, _("Copy right"));
}

static GNM_ACTION_DEF (cb_format_column_auto_fit)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	workbook_cmd_resize_selected_colrow (wbc,
		wb_control_cur_sheet (wbc), TRUE, -1);
}
static GNM_ACTION_DEF (cb_set_column_width)
{
	dialog_col_width (wbcg, FALSE);
}
static GNM_ACTION_DEF (cb_format_column_std_width)
{
	dialog_col_width (wbcg, TRUE);
}
static GNM_ACTION_DEF (cb_format_column_hide)
{
	cmd_selection_colrow_hide (WORKBOOK_CONTROL (wbcg), TRUE, FALSE);
}
static GNM_ACTION_DEF (cb_format_column_unhide)
{
	cmd_selection_colrow_hide (WORKBOOK_CONTROL (wbcg), TRUE, TRUE);
}

static GNM_ACTION_DEF (cb_format_row_auto_fit)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	workbook_cmd_resize_selected_colrow (wbc, 
		wb_control_cur_sheet (wbc), FALSE, -1);
}
static GNM_ACTION_DEF (cb_set_row_height)
{
	dialog_row_height (wbcg, FALSE);
}
static GNM_ACTION_DEF (cb_format_row_std_height)
{
	dialog_row_height (wbcg, TRUE);
}
static GNM_ACTION_DEF (cb_format_row_hide)
{
	cmd_selection_colrow_hide (WORKBOOK_CONTROL (wbcg), FALSE, FALSE);
}
static GNM_ACTION_DEF (cb_format_row_unhide)
{
	cmd_selection_colrow_hide (WORKBOOK_CONTROL (wbcg), FALSE, TRUE);
}

#ifdef GTK_STOCK_ABOUT
#define GNM_ABOUT GTK_STOCK_ABOUT
#else
#define GNM_ABOUT NULL
#endif

/* Actions that are always sensitive */
static /* const 142334 */ GtkActionEntry permanent_actions[] = {
	{ "MenuFile",		NULL, N_("_File") },
	{ "MenuEdit",		NULL, N_("_Edit") },
		{ "MenuEditClear",	GTK_STOCK_CLEAR, N_("C_lear") },
		{ "MenuEditDelete",	GTK_STOCK_DELETE, N_("_Delete") },
		{ "MenuEditSheet",	NULL, N_("S_heet") },
	        { "MenuEditSelect",	NULL, N_("_Select") },
	        { "MenuEditFill",	NULL, N_("_Fill") },
	{ "MenuView",		NULL, N_("_View") },
		{ "MenuViewToolbars",		NULL, N_("_Toolbars") },
	{ "MenuInsert",		NULL, N_("_Insert") },
		{ "MenuInsertNames",		NULL, N_("_Names") },
		{ "MenuInsertSpecial",		NULL, N_("S_pecial") },
	{ "MenuFormat",		NULL, N_("F_ormat") },
		{ "MenuFormatColumn",		NULL, N_("C_olumn") },
		{ "MenuFormatRow",		NULL, N_("_Row") },
		{ "MenuFormatSheet",		NULL, N_("_Sheet") },
	{ "MenuTools",		NULL, N_("_Tools") },
		{ "MenuToolsScenarios",	NULL,	N_("Sce_narios") },
		{ "MenuToolStatisticalAnalysis",	NULL,	N_("Statistical Anal_ysis") },
		{ "MenuANOVA",	NULL,	N_("_ANOVA") },
		{ "MenuToolForecast",	NULL,	N_("F_orecast") },
		{ "MenuToolTTest",	NULL,	N_("Two _Means") },
	{ "MenuData",		NULL, N_("_Data") },
		{ "MenuFilter",		NULL,	N_("_Filter") },
		{ "MenuOutline",	NULL,	N_("_Group and Outline") },
		{ "MenuExternalData",	NULL,	N_("Get _External Data") },
	{ "MenuHelp",	NULL,	N_("_Help") },

	{ "EditCut", GTK_STOCK_CUT, NULL,
		NULL, N_("Cut the selection"),
		G_CALLBACK (cb_edit_cut) },
	{ "EditCopy", GTK_STOCK_COPY, NULL,
		NULL, N_("Copy the selection"),
		G_CALLBACK (cb_edit_copy) },
	{ "EditPaste", GTK_STOCK_PASTE, NULL,
		NULL, N_("Paste the clipboard"),
		G_CALLBACK (cb_edit_paste) },

	{ "HelpDocs", GTK_STOCK_HELP, N_("_Contents"),
		NULL, N_("Open a viewer for Gnumeric's documentation"),
		G_CALLBACK (cb_help_docs) },
	{ "HelpWeb", NULL, N_("Gnumeric on the _Web"),
		NULL, N_("Browse to Gnumeric's website"),
		G_CALLBACK (cb_help_web) },
	{ "HelpIRC", NULL, N_("_Live Assistance"),
		NULL, N_("See if anyone is available to answer questions"),
		G_CALLBACK (cb_help_irc) },
	{ "HelpBug", NULL, N_("Report a _Problem"),
		NULL, N_("Report problem "),
		G_CALLBACK (cb_help_bug) },
	{ "HelpAbout", GNM_ABOUT, N_("_About"),
		NULL, N_("About this application"),
		G_CALLBACK (cb_help_about) },

};

static /* const 142334 */ GtkActionEntry actions[] = {
/* File */
	{ "FileNew", GTK_STOCK_NEW, NULL,
		NULL, N_("Create a new workbook"),
		G_CALLBACK (cb_file_new) },
	{ "FileOpen", GTK_STOCK_OPEN, NULL,
		NULL, N_("Open a file"),
		G_CALLBACK (cb_file_open) },
	{ "FileSave", GTK_STOCK_SAVE, NULL,
		NULL, N_("Save the current workbook"),
		G_CALLBACK (cb_file_save) },
	{ "FileSaveAs", GTK_STOCK_SAVE_AS, NULL,
		"<control><shift>s", N_("Save the current workbook with a different name"),
		G_CALLBACK (cb_file_save_as) },
	{ "FileSend", "Gnumeric_Link_EMail", N_("Sen_d To..."),
		NULL, N_("Send the current file via email"),
		G_CALLBACK (cb_file_sendto) },
	{ "FilePageSetup", NULL, N_("Page Set_up..."),
		NULL, N_("Setup the page settings for your current printer"),
		G_CALLBACK (cb_file_page_setup) },
	{ "FilePrintPreview", GTK_STOCK_PRINT_PREVIEW, NULL,
		NULL, N_("Print preview"),
		G_CALLBACK (cb_file_print_preview) },
	{ "FilePrint", GTK_STOCK_PRINT, NULL,
		NULL, N_("Print the current file"),
		G_CALLBACK (cb_file_print) },
	{ "FileSummary", GTK_STOCK_PROPERTIES, N_("Proper_ties..."),
		NULL, N_("Edit descriptive information"),
		G_CALLBACK (cb_file_summary) },
	{ "FilePreferences", GTK_STOCK_PREFERENCES, N_("Pre_ferences..."),
		NULL, N_("Change Gnumeric Preferences"),
		G_CALLBACK (cb_file_preferences) },
	{ "FileClose", GTK_STOCK_CLOSE, NULL,
		NULL, N_("Close the current file"),
		G_CALLBACK (cb_file_close) },
	{ "FileQuit", GTK_STOCK_QUIT, NULL,
		NULL, N_("Quit the application"),
		G_CALLBACK (cb_file_quit) },

/* Edit -> Clear */
	{ "EditClearAll", NULL, N_("_All"),
		NULL, N_("Clear the selected cells' formats, comments, and contents"),
		G_CALLBACK (cb_edit_clear_all) },
	{ "EditClearFormats", NULL, N_("_Formats"),
		NULL, N_("Clear the selected cells' formats"),
		G_CALLBACK (cb_edit_clear_formats) },
	{ "EditClearComments", NULL, N_("Co_mments"),
		NULL, N_("Clear the selected cells' comments"),
		G_CALLBACK (cb_edit_clear_comments) },
	{ "EditClearContent", NULL, N_("_Contents"),
		NULL, N_("Clear the selected cells' contents"),
		G_CALLBACK (cb_edit_clear_content) },

/* Edit -> Delete */
	{ "EditDeleteRows", "Gnumeric_RowDelete", N_("_Rows"),
		NULL, N_("Delete the row(s) containing the selected cells"),
		G_CALLBACK (cb_edit_delete_rows) },
	{ "EditDeleteColumns", "Gnumeric_ColumnDelete", N_("_Columns"),
		NULL, N_("Delete the column(s) containing the selected cells"),
		G_CALLBACK (cb_edit_delete_columns) },
	{ "EditDeleteCells", NULL, N_("C_ells..."),
		  NULL, N_("Delete the selected cells, shifting others into their place"),
		  G_CALLBACK (cb_edit_delete_cells) },


/* Edit -> Select */
	{ "EditSelectAll", NULL, N_("Select _All"),
		"<control>a", N_("Select all cells in the spreadsheet"),
		G_CALLBACK (cb_edit_select_all) },
	{ "EditSelectColumn", NULL, N_("Select _Column"),
		"<control>space", N_("Select an entire column"),
		G_CALLBACK (cb_edit_select_col) },
	{ "EditSelectRow", NULL, N_("Select _Row"),
		"<alt>space", N_("Select an entire row"),
		G_CALLBACK (cb_edit_select_row) },
	{ "EditSelectArray", NULL, N_("Select Arra_y"),
		"<control>slash", N_("Select an array of cells"),
		G_CALLBACK (cb_edit_select_array) },
	{ "EditSelectDepends", NULL, N_("Select _Depends"),
		"<control>bracketright", N_("Select all the cells that depend on the current edit cell"),
		G_CALLBACK (cb_edit_select_depends) },
	{ "EditSelectInputs", NULL, N_("Select _Inputs"),
		"<control>bracketleft", N_("Select all the cells are used by the current edit cell"),
		G_CALLBACK (cb_edit_select_inputs) },

/* Edit -> Fill */
	{ "EditFillAutofill", NULL, N_("Auto_fill"),
		NULL, N_("Automatically fill the current selection"),
		G_CALLBACK (cb_edit_fill_autofill) },
	{ "ToolsMerge", NULL, N_("_Merge..."),
		NULL, N_("Merges columnar data into a sheet creating duplicate sheets for each row"),
		G_CALLBACK (cb_tools_merge) },
	{ "ToolsTabulate", NULL, N_("_Tabulate Dependency..."),
		NULL, N_("Make a table of a cell's value as a function of other cells"),
		G_CALLBACK (cb_tools_tabulate) },
	{ "EditFillSeries", NULL, N_("_Series..."),
		NULL, N_("Fill according to a linear or exponential series"),
		G_CALLBACK (cb_edit_fill_series) },
	{ "RandomGenerator", NULL, N_("_Random Generator..."),
		NULL, N_("Generate random numbers of a selection of distributions"),
		G_CALLBACK (cb_tools_random_generator) },

/* Edit -> Sheet */
	{ "SheetReorder", NULL, N_("_Manage Sheets..."),
		NULL, N_("Manage the sheets in this workbook"),
		G_CALLBACK (cb_sheet_order) },
	{ "InsertSheet", NULL, N_("_Insert"),
		NULL, N_("Insert a new sheet"),
		G_CALLBACK (wbcg_insert_sheet) },
    /* ICK A DUPLICATE : we have no way to override a label on one proxy */
	{ "SheetInsert", NULL, N_("_Sheet"),
		NULL, N_("Insert a new sheet"),
		G_CALLBACK (wbcg_insert_sheet) },
	{ "InsertSheetAtEnd", NULL, N_("_Append"),
		NULL, N_("Append a new sheet"),
		G_CALLBACK (wbcg_append_sheet) },
	{ "EditDuplicateSheet", NULL, N_("_Duplicate"),
		NULL, N_("Make a copy of the current sheet"),
		G_CALLBACK (cb_edit_duplicate_sheet) },
	{ "SheetRemove", NULL, N_("_Remove"),
		NULL, N_("Irrevocably remove an entire sheet"),
		G_CALLBACK (cb_sheet_remove) },
	{ "SheetChangeName", NULL, N_("Re_name"),
		NULL, N_("Rename the current sheet"),
		G_CALLBACK (cb_sheet_name) },

/* Edit */
	{ "EditPasteSpecial", NULL, N_("P_aste special..."),
		"<shift><control>V", N_("Paste with optional filters and transformations"),
		G_CALLBACK (cb_edit_paste_special) },

	{ "InsertComment", "Gnumeric_CommentEdit", N_("Co_mment..."),
		NULL, N_("Edit the selected cell's comment"),
		G_CALLBACK (cb_insert_comment) },

	{ "EditSearch", GTK_STOCK_FIND, N_("Search..."),
		"F7", N_("Search for some text"),
		G_CALLBACK (cb_edit_search) },
	{ "EditSearchReplace", GTK_STOCK_FIND_AND_REPLACE, N_("Search and Replace..."),
		"F6", N_("Search for some text and replace it with something else"),
		G_CALLBACK (cb_edit_search_replace) },
	{ "EditGoto", GTK_STOCK_JUMP_TO, N_("_Goto cell..."),
		"F5", N_("Jump to a specified cell"),
		G_CALLBACK (cb_edit_goto) },

	{ "EditRecalc", NULL, N_("Recalculate"),
		"F9", N_("Recalculate the spreadsheet"),
		G_CALLBACK (cb_edit_recalc) },

	{ "Repeat", NULL, N_("Repeat"),
		"F4", N_("Repeat the previous action"),
		G_CALLBACK (cb_repeat) },

/* View */
	{ "ViewNew", NULL, N_("_New View..."),
		NULL, N_("Create a new view of the workbook"),
		G_CALLBACK (cb_view_new) },
	{ "ViewFreezeThawPanes", NULL, N_("_Freeze Panes"),
		NULL, N_("Freeze the top left of the sheet"),
		G_CALLBACK (cb_view_freeze_panes) },
	{ "ViewFullScreen", NULL, N_("F_ull Screen..."),
		NULL, N_("Zoom the spreadsheet in or out"),
		G_CALLBACK (cb_view_zoom) },
	{ "ViewZoom", NULL, N_("_Zoom..."),
		NULL, N_("Zoom the spreadsheet in or out"),
		G_CALLBACK (cb_view_zoom) },

/* Insert */
	{ "InsertCells", NULL, N_("C_ells..."),
		NULL, N_("Insert new cells"),
		G_CALLBACK (cb_insert_cells) },
	{ "InsertColumns", "Gnumeric_ColumnAdd", N_("_Columns"),
		NULL, N_("Insert new columns"),
		G_CALLBACK (cb_insert_cols) },
	{ "InsertRows", "Gnumeric_RowAdd", N_("_Rows"),
		NULL, N_("Insert new rows"),
		G_CALLBACK (cb_insert_rows) },

	{ "ChartGuru", "Gnumeric_GraphGuru", N_("C_hart..."),
		NULL, N_("Insert a Chart"),
		G_CALLBACK (cb_launch_chart_guru) },
	{ "InsertImage", "Gnumeric_InsertImage", N_("_Image..."),
		NULL, N_("Insert an image"),
		G_CALLBACK (cb_insert_image) },

	{ "InsertHyperlink", "Gnumeric_Link_Add", N_("Hyper_link..."),
		"<control>K", N_("Insert a Hyperlink"),
		G_CALLBACK (cb_insert_hyperlink) },
/* Insert -> Special */
	{ "InsertCurrentDate", NULL, N_("Current _date"),
		"<control>semicolon", N_("Insert the current date into the selected cell(s)"),
		G_CALLBACK (cb_insert_current_date) },

	{ "InsertCurrentTime", NULL, N_("Current _time"),
		"<control>colon", N_("Insert the current time into the selected cell(s)"),
		G_CALLBACK (cb_insert_current_time) },

/* Insert -> Name */
	{ "EditNames", NULL, N_("_Define..."),
		"<control>F3", N_("Edit sheet and workbook names"),
		G_CALLBACK (cb_define_name) },
#if 0
	GNOMEUIINFO_ITEM_NONE (N_("_Auto generate names..."),
		N_("Use the current selection to create names"),
		cb_auto_generate__named_expr),
#endif

/* Format */
	{ "FormatCells", NULL, N_("_Cells..."),
		"<control>1", N_("Modify the formatting of the selected cells"),
		G_CALLBACK (cb_format_cells) },
	{ "FormatWorkbook", NULL, N_("_Workbook..."),
		NULL, N_("Modify the workbook attributes"),
		G_CALLBACK (cb_workbook_attr) },
	{ "FormatGnumeric", GTK_STOCK_PREFERENCES, N_("_Gnumeric..."),
		NULL, N_("Edit the Gnumeric Preferences"),
		G_CALLBACK (cb_format_preferences) },
	{ "FormatAuto", NULL, N_("_Autoformat..."),
		NULL, N_("Format a region of cells according to a pre-defined template"),
		G_CALLBACK (cb_autoformat) },

/* Format -> Col */
	{ "ColumnSize", "Gnumeric_ColumnSize", N_("_Width..."),
		NULL, N_("Change width of the selected columns"),
		G_CALLBACK (cb_set_column_width) },
	{ "ColumnAutoSize", NULL, N_("_Auto fit selection"),
		NULL, N_("Ensure columns are just wide enough to display content"),
		G_CALLBACK (cb_format_column_auto_fit) },
	{ "ColumnHide", "Gnumeric_ColumnHide", N_("_Hide"),
		NULL, N_("Hide the selected columns"),
		G_CALLBACK (cb_format_column_hide) },
	{ "ColumnUnhide", "Gnumeric_ColumnUnhide", N_("_Unhide"),
		NULL, N_("Make any hidden columns in the selection visible"),
		G_CALLBACK (cb_format_column_unhide) },
	{ "ColumnDefaultSize", NULL, N_("_Standard Width"),
		NULL, N_("Change the default column width"),
		G_CALLBACK (cb_format_column_std_width) },

/* Format -> Row */
	{ "RowSize", "Gnumeric_RowSize", N_("H_eight..."),
		NULL, N_("Change height of the selected rows"),
		G_CALLBACK (cb_set_row_height) },
	{ "RowAutoSize", NULL, N_("_Auto fit selection"),
		NULL, N_("Ensure rows are just tall enough to display content"),
		G_CALLBACK (cb_format_row_auto_fit) },
	{ "RowHide", "Gnumeric_RowHide", N_("_Hide"),
		NULL, N_("Hide the selected rows"),
		G_CALLBACK (cb_format_row_hide) },
	{ "RowUnhide", "Gnumeric_RowUnhide", N_("_Unhide"),
		NULL, N_("Make any hidden rows in the selection visible"),
		G_CALLBACK (cb_format_row_unhide) },
	{ "RowDefaultSize", NULL, N_("_Standard Height"),
		NULL, N_("Change the default row height"),
		G_CALLBACK (cb_format_row_std_height) },

/* Tools */
	{ "ToolsPlugins", NULL, N_("_Plug-ins..."),
		NULL, N_("Manage available plugin modules"),
		G_CALLBACK (cb_tools_plugins) },
	{ "ToolsAutoCorrect", NULL, N_("Auto _Correct..."),
		NULL, N_("Automatically perform simple spell checking"),
		G_CALLBACK (cb_tools_autocorrect) },
	{ "ToolsAutoSave", NULL, N_("_Auto Save..."),
		NULL, N_("Automatically save the current document at regular intervals"),
		G_CALLBACK (cb_tools_auto_save) },
	{ "ToolsGoalSeek", NULL, N_("_Goal Seek..."),
		NULL, N_("Iteratively recalculate to find a target value"),
		G_CALLBACK (cb_tools_goal_seek) },
	{ "ToolsSolver", NULL, N_("_Solver..."),
		NULL, N_("Iteratively recalculate with constraints to approach a target value"),
		G_CALLBACK (cb_tools_solver) },
	{ "ToolsSimulation", NULL, N_("Si_mulation..."),
		NULL, N_("Test decision alternatives by using Monte Carlo "
			 "simulation to find out probable outputs and risks related to them"),
		G_CALLBACK (cb_tools_simulation) },

/* Tools -> Scenarios */
	{ "ToolsScenarios", NULL, N_("_View..."),
		NULL, N_("View, delete and report different scenarios"),
                G_CALLBACK (cb_tools_scenarios) },
	{ "ToolsScenarioAdd", NULL, N_("_Add..."),
		NULL, N_("Add a new scenario"),
                G_CALLBACK (cb_tools_scenario_add) },

/* Tools -> ANOVA */
	{ "ToolsANOVAoneFactor", NULL, N_("_One Factor..."),
		NULL, N_("One Factor Analysis of Variance..."),
		G_CALLBACK (cb_tools_anova_one_factor) },
	{ "ToolsANOVAtwoFactor", NULL, N_("_Two Factor..."),
		NULL, N_("Two Factor Analysis of Variance..."),
		G_CALLBACK (cb_tools_anova_two_factor) },

/* Tools -> Forecasting */
	{ "ToolsExpSmoothing", NULL, N_("_Exponential Smoothing..."),
		NULL, N_("Exponential smoothing..."),
		G_CALLBACK (cb_tools_exp_smoothing) },
	{ "ToolsAverage", NULL, N_("_Moving Average..."),
		NULL, N_("Moving average..."),
		G_CALLBACK (cb_tools_average) },

/* Tools -> Two Means */
	{ "ToolTTestPaired", NULL, N_("_Paired Samples: T-Test..."),
		NULL, N_("Comparing two population means for two paired samples: t-test..."),
		G_CALLBACK (cb_tools_ttest_paired) },

	{ "ToolTTestEqualVar", NULL, N_("Unpaired Samples, _Equal Variances: T-Test..."),
		NULL, N_("Comparing two population means for two unpaired samples from populations with equal variances: t-test..."),
		G_CALLBACK (cb_tools_ttest_equal_var) },

	{ "ToolTTestUnequalVar", NULL, N_("Unpaired Samples, _Unequal Variances: T-Test..."),
		NULL, N_("Comparing two population means for two unpaired samples from populations with unequal variances: t-test..."),
		G_CALLBACK (cb_tools_ttest_unequal_var) },

	{ "ToolZTest", NULL, N_("_Known Variances or Large Sample: Z-Test..."),
		NULL, N_("Comparing two population means from populations with known variances or using a large sample: z-test..."),
		G_CALLBACK (cb_tools_ztest) },

/* Tools -> Analysis */
	{ "ToolsCorrelation", NULL, N_("_Correlation..."),
		NULL, N_("Pearson Correlation"),
		G_CALLBACK (cb_tools_correlation) },
	{ "ToolsCovariance", NULL, N_("Co_variance..."),
		NULL, N_("Covariance"),
		G_CALLBACK (cb_tools_covariance) },
	{ "ToolsDescStatistics", NULL, N_("_Descriptive Statistics..."),
		NULL, N_("Various summary statistics"),
		G_CALLBACK (cb_tools_desc_statistics) },
	{ "ToolsFourier", NULL, N_("_Fourier Analysis..."),
		NULL, N_("Fourier Analysis"),
		G_CALLBACK (cb_tools_fourier) },
	{ "ToolsHistogram", NULL, N_("_Histogram..."),
		NULL, N_("Various frequency tables"),
		G_CALLBACK (cb_tools_histogram) },
	{ "ToolsRanking", NULL, N_("Ranks And _Percentiles..."),
		NULL, N_("Ranks, placements and percentiles"),
		G_CALLBACK (cb_tools_ranking) },
	{ "ToolsRegression", NULL, N_("_Regression..."),
		NULL, N_("Regression Analysis"),
		G_CALLBACK (cb_tools_regression) },
	{ "ToolsSampling", NULL, N_("_Sampling..."),
		NULL, N_("Periodic and random samples"),
		G_CALLBACK (cb_tools_sampling) },
	{ "ToolsFTest", NULL, N_("_Two Variances: FTest..."),
		NULL, N_("Comparing two population variances"),
		G_CALLBACK (cb_tools_ftest) },

/* Data */
	{ "DataSort", GTK_STOCK_SORT_ASCENDING, N_("_Sort..."),
		NULL, N_("Sort the selected region"),
		G_CALLBACK (cb_data_sort) },
	{ "DataShuffle", NULL, N_("Sh_uffle..."),
		NULL, N_("Shuffle cells, rows or columns"),
		G_CALLBACK (cb_data_shuffle) },
	{ "DataValidate", NULL, N_("_Validate..."),
		NULL, N_("Validate input with preset criteria"),
		G_CALLBACK (cb_data_validate) },
	{ "DataTextToColumns", NULL, N_("_Text to Columns..."),
		NULL, N_("Parse the text in the selection into data"),
		G_CALLBACK (cb_data_text_to_columns) },
	{ "DataConsolidate", NULL, N_("_Consolidate..."),
		NULL, N_("Consolidate regions using a function"),
		G_CALLBACK (cb_data_consolidate) },

#ifdef ENABLE_PIVOTS
	{ "PivotTable", "Gnumeric_PivotTable", N_("_PivotTable..."),
		NULL, N_("Create a pivot table"),
		G_CALLBACK (cb_data_pivottable) },
#endif

/* Data -> Outline */
	{ "DataOutlineHideDetail", "Gnumeric_HideDetail", N_("_Hide Detail"),
		NULL, N_("Collapse an outline group"),
		G_CALLBACK (cb_data_hide_detail) },
	{ "DataOutlineShowDetail", "Gnumeric_ShowDetail", N_("_Show Detail"),
		NULL, N_("Uncollapse an outline group"),
		G_CALLBACK (cb_data_show_detail) },
	{ "DataOutlineGroup", "Gnumeric_Group", N_("_Group..."),
		NULL, N_("Add an outline group"),
		G_CALLBACK (cb_data_group) },
	{ "DataOutlineUngroup", "Gnumeric_Ungroup", N_("_Ungroup..."),
		NULL, N_("Remove an outline group"),
		G_CALLBACK (cb_data_ungroup) },

/* Data -> Filter */
	{ "DataAutoFilter", "Gnumeric_AutoFilter", N_("Add _Auto Filter"),
		NULL, N_("Add or remove a filter"),
		G_CALLBACK (cb_auto_filter) },
	{ "DataFilterShowAll", NULL, N_("_Show All"),
		NULL, N_("Show all filtered and hidden rows"),
		G_CALLBACK (cb_show_all) },
	{ "DataFilterAdvancedfilter", NULL, N_("Advanced _Filter..."),
		NULL, N_("Filter data with given criteria"),
		G_CALLBACK (cb_data_filter) },
/* Data -> External */
	{ "DataImportText", GTK_STOCK_DND, N_("Import _Text File..."),
		NULL, N_("Import the text from a file"),
		G_CALLBACK (cb_data_import_text) },

/* Standard Toolbar */
	{ "AutoSum", "Gnumeric_AutoSum", N_("Sum"),
		NULL, N_("Sum into the current cell"),
		G_CALLBACK (cb_autosum) },
	{ "InsertFormula", "Gnumeric_FormulaGuru", N_("Function"), NULL,
		N_("Edit a function in the current cell"),
		G_CALLBACK (cb_formula_guru) },

	{ "SortAscending", GTK_STOCK_SORT_ASCENDING, N_("Sort Ascending"), NULL,
		N_("Sort the selected region in ascending order based on the first column selected"),
		G_CALLBACK (cb_sort_ascending) },
	{ "SortDescending", GTK_STOCK_SORT_DESCENDING, N_("Sort Descending"), NULL,
		N_("Sort the selected region in descending order based on the first column selected"),
		G_CALLBACK (cb_sort_descending) },

/* Object Toolbar */
	{ "CreateLabel", "Gnumeric_ObjectLabel", N_("Label"),
		NULL, N_("Create a label"),
		G_CALLBACK (cmd_create_label) },
	{ "CreateFrame", "Gnumeric_ObjectFrame", N_("Frame"),
		NULL, N_("Create a frame"),
		G_CALLBACK (cmd_create_frame) },
	{ "CreateCheckbox", "Gnumeric_ObjectCheckbox", N_("Checkbox"),
		NULL, N_("Create a checkbox"),
		G_CALLBACK (cmd_create_checkbox) },
	{ "CreateScrollbar", "Gnumeric_ObjectScrollbar", N_("Scrollbar"),
		NULL, N_("Create a scrollbar"),
		G_CALLBACK (cmd_create_scrollbar) },
	{ "CreateSlider", "Gnumeric_ObjectSlider", N_("Slider"),
		NULL, N_("Create a slider"),
		G_CALLBACK (cmd_create_slider) },
	{ "CreateSpinButton", "Gnumeric_ObjectSpinButton", N_("SpinButton"),
		NULL, N_("Create a spin button"),
		G_CALLBACK (cmd_create_spinbutton) },
	{ "CreateList", "Gnumeric_ObjectList", N_("List"),
		NULL, N_("Create a list"),
		G_CALLBACK (cmd_create_list) },
	{ "CreateCombo", "Gnumeric_ObjectCombo", N_("Combo Box"),
		NULL, N_("Create a combo box"),
		G_CALLBACK (cmd_create_combo) },
	{ "CreateLine", "Gnumeric_ObjectLine", N_("Line"),
		NULL, ("Create a line object"),
		G_CALLBACK (cmd_create_line) },
	{ "CreateArrow", "Gnumeric_ObjectArrow", N_("Arrow"),
		NULL, ("Create an arrow object"),
		G_CALLBACK (cmd_create_arrow) },
	{ "CreateRectangle", "Gnumeric_ObjectRectangle", N_("Rectangle"),
		NULL, ("Create a rectangle object"),
		G_CALLBACK (cmd_create_rectangle) },
	{ "CreateEllipse", "Gnumeric_ObjectEllipse", N_("Ellipse"),
		NULL, ("Create an ellipse object"),
		G_CALLBACK (cmd_create_ellipse) },
	{ "CreateButton", "Gnumeric_ObjectButton", N_("Button"),
		NULL, N_("Create a button"),
		G_CALLBACK (cmd_create_button) },
	{ "CreateRadioButton", "Gnumeric_ObjectRadioButton", N_("RadioButton"),
		NULL, N_("Create a radio button"),
		G_CALLBACK (cmd_create_radiobutton) },

/* Format toolbar */
	{ "FormatMergeCells", "Gnumeric_MergeCells", N_("Merge"),
		NULL, N_("Merge a range of cells"),
		G_CALLBACK (cb_merge_cells) },
	{ "FormatUnmergeCells", "Gnumeric_SplitCells", N_("Split"),
		NULL, N_("Split merged ranges of cells"),
		G_CALLBACK (cb_unmerge_cells) },

	{ "FormatAsNumber", NULL, N_("Number"),
		"<control>asciitilde", N_("Format the selection as numbers"),
		G_CALLBACK (cb_format_as_number) },
	{ "FormatAsCurrency", NULL, N_("Currency"),
		"<control>dollar", N_("Format the selection as currency"),
		G_CALLBACK (cb_format_as_currency) },
	{ "FormatAsAccounting", "Gnumeric_FormatAsAccounting", N_("Accounting"),
		"<control>exclam", N_("Format the selection as accounting"),
		G_CALLBACK (cb_format_as_accounting) },
	{ "FormatAsPercentage", "Gnumeric_FormatAsPercent", N_("Percentage"),
		"<control>percent", N_("Format the selection as percentage"),
		G_CALLBACK (cb_format_as_percentage) },
	{ "FormatAsScientific", NULL, N_("Scientific"),
		"<control>asciicircum", N_("Format the selection as scientific"),
		G_CALLBACK (cb_format_as_scientific) },
	{ "FormatAsDate", NULL, N_("Date"),
		"<control>numbersign", N_("Format the selection as date"),
		G_CALLBACK (cb_format_as_date) },
	{ "FormatAsTime", NULL, N_("Time"),
		"<control>at", N_("Format the selection as time"),
		G_CALLBACK (cb_format_as_time) },
	{ "FormatAddBorders", NULL, N_("AddBorders"),
		"<control>ampersand", N_("Add a border the the selection"),
		G_CALLBACK (cb_format_add_borders) },
	{ "FormatClearBorders", NULL, N_("ClearBorders"),
		"<control>underscore", N_("Clear the border around the selection"),
		G_CALLBACK (cb_format_clear_borders) },

	{ "FormatWithThousands", "Gnumeric_FormatThousandSeparator", N_("Thousands Separator"),
		NULL, N_("Set the format of the selected cells to include a thousands separator"),
		G_CALLBACK (cb_format_with_thousands) },
	{ "FormatIncreasePrecision", "Gnumeric_FormatAddPrecision", N_("Increase Precision"),
		NULL, N_("Increase the number of decimals displayed"),
		G_CALLBACK (cb_format_inc_precision) },
	{ "FormatDecreasePrecision", "Gnumeric_FormatRemovePrecision", N_("Decrease Precision"),
		NULL, N_("Decrease the number of decimals displayed"),
		G_CALLBACK (cb_format_dec_precision) },
	{ "FormatDecreaseIndent", GTK_STOCK_UNINDENT, NULL,
		NULL, N_("Decrease the indent, and align the contents to the left"),
		G_CALLBACK (cb_format_dec_indent) },
	{ "FormatIncreaseIndent", GTK_STOCK_INDENT, NULL,
		NULL, N_("Increase the indent, and align the contents to the left"),
		G_CALLBACK (cb_format_inc_indent) },
/* Unattached */
#warning add descriptions for copy down/right
	{ "CopyDown", NULL, "", "<control>D", NULL, G_CALLBACK (cb_copydown) },
	{ "CopyRight", NULL, "", "<control>R", NULL, G_CALLBACK (cb_copyright) }
};

#define TOGGLE_HANDLER(flag, code)					\
static GNM_ACTION_DEF (cb_sheet_pref_ ## flag )				\
{									\
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));		\
									\
	if (!wbcg->updating_ui) {					\
		Sheet *sheet = wbcg_cur_sheet (wbcg);			\
		g_return_if_fail (IS_SHEET (sheet));			\
									\
		sheet->flag = !sheet->flag;				\
		code							\
	}								\
}

TOGGLE_HANDLER (display_formulas, sheet_toggle_show_formula (sheet);)
TOGGLE_HANDLER (hide_zero, sheet_toggle_hide_zeros (sheet); )
TOGGLE_HANDLER (hide_grid, sheet_adjust_preferences (sheet, TRUE, FALSE);)
TOGGLE_HANDLER (hide_col_header, sheet_adjust_preferences (sheet, FALSE, FALSE);)
TOGGLE_HANDLER (hide_row_header, sheet_adjust_preferences (sheet, FALSE, FALSE);)
TOGGLE_HANDLER (display_outlines, sheet_adjust_preferences (sheet, TRUE, TRUE);)
TOGGLE_HANDLER (outline_symbols_below, {
		sheet_adjust_outline_dir (sheet, FALSE);
		sheet_adjust_preferences (sheet, TRUE, TRUE);
})
TOGGLE_HANDLER (outline_symbols_right,{
		sheet_adjust_outline_dir (sheet, TRUE);
		sheet_adjust_preferences (sheet, TRUE, TRUE);
})

static /* const 142334 */ GtkToggleActionEntry toggle_actions[] = {
	{ "SheetDisplayOutlines", NULL, N_("Display _Outlines"),
		NULL, N_("Toggle whether or not to display outline groups"),
		G_CALLBACK (cb_sheet_pref_display_outlines) },
	{ "SheetOutlineBelow", NULL, N_("Outlines _Below"),
		NULL, N_("Toggle whether to display row outlines on top or bottom"),
		G_CALLBACK (cb_sheet_pref_outline_symbols_below) },
	{ "SheetOutlineRight", NULL, N_("Outlines _Right"),
		NULL, N_("Toggle whether to display column outlines on the left or right"),
		G_CALLBACK (cb_sheet_pref_outline_symbols_right) },
	{ "SheetDisplayFormulas", NULL, N_("Display _Formulas"),
		"<control>quoteleft", N_("Display the value of a formula or the formula itself"),
		G_CALLBACK (cb_sheet_pref_display_formulas) },
	{ "SheetHideZeros", NULL, N_("Hide _Zeros"),
		NULL, N_("Toggle whether or not to display zeros as blanks"),
		G_CALLBACK (cb_sheet_pref_hide_zero) },
	{ "SheetHideGridlines", NULL, N_("Hide _Gridlines"),
		NULL, N_("Toggle whether or not to display gridlines"),
		G_CALLBACK (cb_sheet_pref_hide_grid) },
	{ "SheetHideColHeader", NULL, N_("Hide _Column Headers"),
		NULL, N_("Toggle whether or not to display column headers"),
		G_CALLBACK (cb_sheet_pref_hide_col_header) },
	{ "SheetHideRowHeader", NULL, N_("Hide _Row Headers"),
		NULL, N_("Toggle whether or not to display row headers"),
		G_CALLBACK (cb_sheet_pref_hide_row_header) },

	{ "AlignLeft", GTK_STOCK_JUSTIFY_LEFT,
		N_("_Left Align"), NULL,
		N_("Left align"), G_CALLBACK (cb_align_left), FALSE },
	{ "AlignCenter", GTK_STOCK_JUSTIFY_CENTER,
		N_("_Center"), NULL,
		N_("Center"), G_CALLBACK (cb_align_center), FALSE },
	{ "AlignRight", GTK_STOCK_JUSTIFY_RIGHT,
		N_("_Right Align"), NULL,
		N_("Right align"), G_CALLBACK (cb_align_right), FALSE },
	{ "CenterAcrossSelection", "Gnumeric_CenterAcrossSelection",
		N_("_Center Across Selection"), NULL,
		N_("Center across the selected cells"),
		G_CALLBACK (cb_center_across_selection), FALSE },
#warning "Add justify"
#warning "h/v distributed?"

#warning "Get vertical alignment icons"
	{ "AlignTop", NULL,
		N_("Align _Top"), NULL,
		N_("Align Top"), G_CALLBACK (cb_align_top), FALSE },
	{ "AlignVCenter", NULL,
		N_("_Vertically Center"), NULL,
		N_("Vertically Center"), G_CALLBACK (cb_align_vcenter), FALSE },
	{ "AlignBottom", NULL,
		N_("Align _Bottom"), NULL,
	        N_("Align Bottom"), G_CALLBACK (cb_align_bottom), FALSE },

	{ "ViewStatusbar", NULL,
	        N_("View _Statusbar"), NULL,
	        N_("Toggle visibility of statusbar"),
	        G_CALLBACK (cb_view_statusbar), TRUE }
};

static /* const 142334 */ GtkToggleActionEntry font_toggle_actions[] = {
	{ "FontBold", GTK_STOCK_BOLD,
		N_("_Bold"), "<control>B",
		N_("Bold"), G_CALLBACK (cb_font_bold), FALSE },
	{ "FontItalic", GTK_STOCK_ITALIC,
		N_("_Italic"), "<control>i",
		N_("Italic"), G_CALLBACK (cb_font_italic), FALSE },
	{ "FontUnderline", GTK_STOCK_UNDERLINE,
		N_("_Underline"), "<control>u",
		N_("Underline"), G_CALLBACK (cb_font_underline), FALSE },
#warning "Add double underline icon"
#warning "Add accelerator for double underline"
	{ "FontDoubleUnderline", GTK_STOCK_UNDERLINE,
		N_("_Underline"), NULL,
		N_("Underline"), G_CALLBACK (cb_font_double_underline), FALSE },
#warning "Should there be an accelerator for strikethrough?"
	{ "FontStrikeThrough", GTK_STOCK_UNDERLINE,
		N_("_Strike Through"), NULL,
		N_("Strike Through"), G_CALLBACK (cb_font_strikethrough), FALSE },
};

void wbcg_register_actions (WorkbookControlGUI *wbcg,
			    GtkActionGroup *menu_group,
			    GtkActionGroup *group,
			    GtkActionGroup *font_group);
void
wbcg_register_actions (WorkbookControlGUI *wbcg,
		       GtkActionGroup *menu_group,
		       GtkActionGroup *group,
		       GtkActionGroup *font_group)
{
	  gtk_action_group_add_actions (menu_group,
		permanent_actions, G_N_ELEMENTS (permanent_actions), wbcg);
	  gtk_action_group_add_actions (group,
		actions, G_N_ELEMENTS (actions), wbcg);
	  gtk_action_group_add_toggle_actions (group,
		toggle_actions, G_N_ELEMENTS (toggle_actions), wbcg);
	  gtk_action_group_add_toggle_actions (font_group,
		font_toggle_actions, G_N_ELEMENTS (font_toggle_actions), wbcg);
}
