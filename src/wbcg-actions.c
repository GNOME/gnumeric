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

#include "widgets/widget-editable-label.h"
#include "widgets/preview-file-selection.h"
#include <gtk/gtkactiongroup.h>
#include <gtk/gtkstock.h>
#include <glib/gi18n.h>
#include <string.h>

static GNM_ACTION_DEF (cb_file_new)
{
	Workbook *wb = workbook_new_with_sheets
		(gnm_app_prefs->initial_sheet_number);

	(void) workbook_control_gui_new (NULL, wb, NULL);
}

static GNM_ACTION_DEF (cb_file_open)
{
	gui_file_open (wbcg, NULL);
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}

static GNM_ACTION_DEF (cb_file_save)
{
	gui_file_save (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}

static GNM_ACTION_DEF (cb_file_save_as)
{
	gui_file_save_as (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}
static GNM_ACTION_DEF (cb_file_sendto)
{
	wb_view_sendto (wb_control_view (WORKBOOK_CONTROL (wbcg)),
		GNM_CMD_CONTEXT (wbcg));
}

static GNM_ACTION_DEF (cb_file_page_setup)
{
	dialog_printer_setup (wbcg,
		wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}

static GNM_ACTION_DEF (cb_file_print)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet_print (wbcg, sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static GNM_ACTION_DEF (cb_file_print_preview)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	sheet_print (wbcg, sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static GNM_ACTION_DEF (cb_file_summary)
	{ dialog_summary_update (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_file_preferences)
	{ dialog_preferences (wbcg, 0); }
static GNM_ACTION_DEF (cb_file_close)
	{ wbcg_close_control (wbcg); }

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
	wbcg_edit_finish (wbcg, FALSE, NULL);

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

static GNM_ACTION_DEF (cb_edit_select_all)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	scg_select_all (wbcg_cur_scg (wbcg));
}
static GNM_ACTION_DEF (cb_edit_select_row)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	sv_select_cur_row (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_col)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	sv_select_cur_col (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_array)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
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

static GNM_ACTION_DEF (cb_edit_undo)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wbcg_edit_finish (wbcg, FALSE, NULL);
	command_undo (wbc);
}

static void
cb_undo_combo (G_GNUC_UNUSED gpointer p,
	       gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	wbcg_edit_finish (wbcg, FALSE, NULL);
	for (i = 0; i < num; i++)
		command_undo (wbc);
}

static GNM_ACTION_DEF (cb_edit_redo)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wbcg_edit_finish (wbcg, FALSE, NULL);
	command_redo (wbc);
}

static void
cb_redo_combo (G_GNUC_UNUSED gpointer p,
	       gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	wbcg_edit_finish (wbcg, FALSE, NULL);
	for (i = 0; i < num; i++)
		command_redo (wbc);
}

static GNM_ACTION_DEF (cb_edit_cut)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	sv_selection_cut (sv, wbc);
}

static GNM_ACTION_DEF (cb_edit_copy)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	sv_selection_copy (sv, wbc);
}

static GNM_ACTION_DEF (cb_edit_paste)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	cmd_paste_to_selection (wbc, sv, PASTE_DEFAULT);
}

static GNM_ACTION_DEF (cb_edit_paste_special)
{
	dialog_paste_special (wbcg);
}

static GNM_ACTION_DEF (cb_edit_delete)
{
	dialog_delete_cells (wbcg);
}

static GNM_ACTION_DEF (cb_sheet_remove)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetControlGUI *res;

	if (wbcg_sheet_to_page_index (wbcg, wb_control_cur_sheet (wbc), &res) >= 0)
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


static GNM_ACTION_DEF (cb_edit_search_replace)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_search_replace (wbcg, cb_edit_search_replace_action);
}


static GNM_ACTION_DEF (cb_edit_search)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_search (wbcg);
}

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
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_goto_cell (wbcg);
}

static GNM_ACTION_DEF (cb_edit_recalc)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	/* TODO :
	 * f9  -  do any necessary calculations across all sheets
	 * shift-f9  -  do any necessary calcs on current sheet only
	 * ctrl-alt-f9 -  force a full recalc across all sheets
	 * ctrl-alt-shift-f9  -  a full-monty super recalc
	 */
	workbook_recalc_all (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_view_zoom)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_zoom (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_view_freeze_panes)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);

	scg_mode_edit (SHEET_CONTROL (scg));
	if (scg->active_panes == 1) {
		GnmCellPos frozen_tl, unfrozen_tl;
		GnmCanvas const *gcanvas = scg_pane (scg, 0);
		frozen_tl = gcanvas->first;
		unfrozen_tl = sv->edit_pos;

		if (unfrozen_tl.col == gcanvas->first.col) {
			if (unfrozen_tl.row == gcanvas->first.row)
				unfrozen_tl.col = unfrozen_tl.row = -1;
			else
				unfrozen_tl.col = frozen_tl.col = 0;
		} else if (unfrozen_tl.row == gcanvas->first.row)
			unfrozen_tl.row = frozen_tl.row = 0;

		if (unfrozen_tl.col < gcanvas->first.col ||
		    unfrozen_tl.col > gcanvas->last_visible.col)
			unfrozen_tl.col = (gcanvas->first.col + gcanvas->last_visible.col) / 2;
		if (unfrozen_tl.row < gcanvas->first.row ||
		    unfrozen_tl.row > gcanvas->last_visible.row)
			unfrozen_tl.row = (gcanvas->first.row + gcanvas->last_visible.row) / 2;

		g_return_if_fail (unfrozen_tl.col > frozen_tl.col ||
				  unfrozen_tl.row > frozen_tl.row);

		sv_freeze_panes (sv, &frozen_tl, &unfrozen_tl);
	} else
		sv_freeze_panes (sv, NULL, NULL);
}

static GNM_ACTION_DEF (cb_view_new)
{
	dialog_new_view (wbcg);
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_insert_current_date)
{
	if (wbcg_edit_start (wbcg, FALSE, FALSE)) {
		Workbook const *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
		GnmValue *v = value_new_int (datetime_timet_to_serial (time (NULL),
								    workbook_date_conv (wb)));
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
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_define_names (wbcg);
}

static GNM_ACTION_DEF (cb_insert_rows)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet     *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const *sel;

	wbcg_edit_finish (wbcg, FALSE, NULL);

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

	wbcg_edit_finish (wbcg, FALSE, NULL);

	/* TODO : No need to check simplicty.  XL applies for each non-discrete
	 * selected region, (use selection_apply).  Arrays and Merged regions
	 * are permitted.
	 */
	if (!(sel = selection_first_range (sv, GNM_CMD_CONTEXT (wbc),
					   _("Insert columns"))))
		return;
	cmd_insert_cols (wbc, sheet, sel->start.col, range_width (sel));
}

static GNM_ACTION_DEF (cb_insert_cells)
{
	dialog_insert_cells (wbcg);
}

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

static GNM_ACTION_DEF (cb_sheet_order)
{
        dialog_sheet_order (wbcg);
}

static GNM_ACTION_DEF (cb_format_cells)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_cell_format (wbcg, FD_CURRENT);
}

static GNM_ACTION_DEF (cb_autoformat)
{
	dialog_autoformat (wbcg);
}

static GNM_ACTION_DEF (cb_workbook_attr)
{
	dialog_workbook_attr (wbcg);
}

static GNM_ACTION_DEF (cb_format_preferences)
{
	dialog_preferences (wbcg, 1);
}



static GNM_ACTION_DEF (cb_tools_plugins)
{
	dialog_plugin_manager (wbcg);
}

static GNM_ACTION_DEF (cb_tools_autocorrect)
{
	dialog_autocorrect (wbcg);
}

static GNM_ACTION_DEF (cb_tools_auto_save)
{
	dialog_autosave (wbcg);
}

static GNM_ACTION_DEF (cb_tools_goal_seek)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_goal_seek (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_tabulate)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_tabulate (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_merge)
{
	dialog_merge (wbcg);
}

static GNM_ACTION_DEF (cb_tools_solver)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_solver (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_scenario_add)
{
	dialog_scenario_add (wbcg);
}

static GNM_ACTION_DEF (cb_tools_scenarios)
{
	dialog_scenarios (wbcg);
}

static GNM_ACTION_DEF (cb_tools_simulation)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_simulation (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_anova_one_factor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_anova_single_factor_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_anova_two_factor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_anova_two_factor_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_correlation)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_correlation_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_covariance)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_covariance_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_desc_statistics)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_descriptive_stat_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_exp_smoothing)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_exp_smoothing_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_average)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_average_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_fourier)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_fourier_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_histogram)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_histogram_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_ranking)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ranking_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_regression)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_regression_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_sampling)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_sampling_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_ttest_paired)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_PAIRED);
}

static GNM_ACTION_DEF (cb_tools_ttest_equal_var)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_UNPAIRED_EQUALVARIANCES);
}

static GNM_ACTION_DEF (cb_tools_ttest_unequal_var)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_UNPAIRED_UNEQUALVARIANCES);
}

static GNM_ACTION_DEF (cb_tools_ztest)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_ZTEST);
}

static GNM_ACTION_DEF (cb_tools_ftest)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ftest_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_tools_random_generator)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_random_tool (wbcg, wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_data_sort)
{
	dialog_cell_sort (wbcg);
}

static GNM_ACTION_DEF (cb_data_shuffle)
{
	dialog_shuffle (wbcg);
}

static GNM_ACTION_DEF (cb_data_import_text)
{
	gui_file_open (wbcg, "Gnumeric_stf:stf_druid");
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}

static GNM_ACTION_DEF (cb_auto_filter)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmFilter *filter = sv_first_selection_in_filter (sv);

#warning Add undo/redo
	if (filter == NULL) {
		GnmRange const *src = selection_first_range (sv,
			GNM_CMD_CONTEXT (wbcg), _("Add Filter"));
		GnmRange region = *src;

		/* only one row selected -- assume that the user wants to
		 * filter the region below this row. */
		if (src != NULL && src->start.row == src->end.row)
			sheet_filter_guess_region  (sv->sheet, &region);

		if (src == NULL || region.start.row == region.end.row) {
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

static GNM_ACTION_DEF (cb_show_all)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	filter_show_all (wb_control_cur_sheet (wbc));
}

static GNM_ACTION_DEF (cb_data_filter)
{
	dialog_advanced_filter (wbcg);
}

static GNM_ACTION_DEF (cb_data_validate)
{
	dialog_cell_format (wbcg, FD_VALIDATION);
}

static GNM_ACTION_DEF (cb_data_text_to_columns)
{
	stf_text_to_columns (WORKBOOK_CONTROL (wbcg),
			     GNM_CMD_CONTEXT (wbcg));
}

#ifdef ENABLE_PIVOTS
static GNM_ACTION_DEF (cb_data_pivottable)
{
	dialog_pivottable (wbcg);
}
#endif

static GNM_ACTION_DEF (cb_data_consolidate)
{
	dialog_consolidate (wbcg);
}

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

static GNM_ACTION_DEF (cb_data_hide_detail)
	{ hide_show_detail (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_data_show_detail)
	{ hide_show_detail (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_group)
	{ group_ungroup_colrow (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_ungroup)
	{ group_ungroup_colrow (wbcg, FALSE); }

static GNM_ACTION_DEF (cb_help_about)
{
	dialog_about (wbcg);
}

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

	/* force focus back to sheet */
	wbcg_focus_cur_scg (wbcg);
}

static GNM_ACTION_DEF (cb_insert_image)
{
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	GtkFileSelection *fsel;

	fsel = GTK_FILE_SELECTION (
		preview_file_selection_new (_("Select an image"), TRUE));
	gtk_file_selection_hide_fileop_buttons (fsel);

	if (gnumeric_dialog_file_selection (wbcg, fsel)) {
		int fd, file_size;
		IOContext *ioc = gnumeric_io_context_new (GNM_CMD_CONTEXT (wbcg));
		unsigned char const *data = gnumeric_mmap_open (ioc,
			gtk_file_selection_get_filename (fsel), &fd, &file_size);

		if (data != NULL) {
			SheetObject *so = sheet_object_image_new ("",
				(guint8 *)data, file_size, TRUE);
			gnumeric_mmap_close (ioc, data, fd, file_size);
			scg_mode_create_object (scg, so);
		} else
			gnumeric_io_error_display (ioc);

		g_object_unref (G_OBJECT (ioc));
	}

	gtk_widget_destroy (GTK_WIDGET (fsel));
}

static GNM_ACTION_DEF (cb_insert_hyperlink)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_hyperlink (wbcg, SHEET_CONTROL (wbcg_cur_scg (wbcg)));
}

static GNM_ACTION_DEF (cb_formula_guru)
{
	dialog_formula_guru (wbcg, NULL);
}

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

static void
set_selection_halign (WorkbookControlGUI *wbcg, StyleHAlignFlags halign)
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
	{ set_selection_halign (wbcg, HALIGN_LEFT); }
static GNM_ACTION_DEF (cb_align_right)
	{ set_selection_halign (wbcg, HALIGN_RIGHT); }
static GNM_ACTION_DEF (cb_align_center)
	{ set_selection_halign (wbcg, HALIGN_CENTER); }
static GNM_ACTION_DEF (cb_center_across_selection)
	{ set_selection_halign (wbcg, HALIGN_CENTER_ACROSS_SELECTION); }

static void
set_selection_valign (WorkbookControlGUI *wbcg, StyleVAlignFlags valign)
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
	{ set_selection_valign (wbcg, VALIGN_TOP); }
static GNM_ACTION_DEF (cb_align_vcenter)
	{ set_selection_valign (wbcg, VALIGN_CENTER); }
static GNM_ACTION_DEF (cb_align_bottom)
	{ set_selection_valign (wbcg, VALIGN_BOTTOM); }

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
toggle_current_font_attr (WorkbookControlGUI *wbcg,
			  gboolean bold,	gboolean italic,
			  gboolean underline,	gboolean double_underline,
			  gboolean strikethrough)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmStyle *new_style, *current_style;

	/* If the user did not initiate this action ignore it.
	 * This happens whenever the ui updates and the current cell makes a
	 * change to the toolbar indicators.
	 */
	if (wbcg->updating_ui)
		return;

	g_warning ("DOO");

	new_style = mstyle_new ();
	current_style = sheet_style_get (sheet,
		sv->edit_pos.col,
		sv->edit_pos.row);

	if (bold)
		mstyle_set_font_bold (new_style,
			!mstyle_get_font_bold (current_style));

	if (italic)
		mstyle_set_font_italic (new_style,
			!mstyle_get_font_italic (current_style));

	if (underline)
		mstyle_set_font_uline (new_style,
			(mstyle_get_font_uline (current_style) == UNDERLINE_NONE)
			? UNDERLINE_SINGLE : UNDERLINE_NONE);
	if (double_underline)
		mstyle_set_font_uline (new_style,
			(mstyle_get_font_uline (current_style) == UNDERLINE_DOUBLE)
			? UNDERLINE_DOUBLE : UNDERLINE_NONE);

	if (strikethrough)
		mstyle_set_font_strike (new_style,
					!mstyle_get_font_strike (current_style));

	if (bold || italic || underline || strikethrough)
		cmd_selection_format (wbc, new_style, NULL,
			    _("Set Font Style"));
	else
		mstyle_unref (new_style);
}

/**
 * Pop up cell format dialog at font page. Used from font select toolbar
 * button, which is displayed in vertical mode instead of font name / font
 * size controls.
 */
static GNM_ACTION_DEF (cb_font_name)
	{ dialog_cell_format (wbcg, FD_FONT); }
static GNM_ACTION_DEF (cb_font_bold)
	{ toggle_current_font_attr (wbcg, TRUE, FALSE, FALSE, FALSE, FALSE); }
static GNM_ACTION_DEF (cb_font_italic)
	{ toggle_current_font_attr (wbcg, FALSE, TRUE, FALSE, FALSE, FALSE); }
static GNM_ACTION_DEF (cb_font_underline)
	{ toggle_current_font_attr (wbcg, FALSE, FALSE, TRUE, FALSE, FALSE); }
static GNM_ACTION_DEF (cb_font_double_underline)
	{ toggle_current_font_attr (wbcg, FALSE, FALSE, FALSE, TRUE, FALSE); }
static GNM_ACTION_DEF (cb_font_strikethrough)
	{ toggle_current_font_attr (wbcg, FALSE, FALSE, FALSE, FALSE, TRUE); }

static gboolean
cb_font_name_changed (G_GNUC_UNUSED gpointer p,
		      char const *font_name, WorkbookControlGUI *wbcg)
{
	GnmStyle *style;

	if (wbcg->updating_ui)
		return TRUE;

	style = mstyle_new ();
	mstyle_set_font_name (style, font_name);
	cmd_selection_format (WORKBOOK_CONTROL (wbcg),
		style, NULL, _("Set Font"));

	wbcg_focus_cur_scg (wbcg);	/* Restore the focus to the sheet */
	return TRUE;
}

static gboolean
cb_font_size_changed (G_GNUC_UNUSED gpointer p,
		      char const *sizetext, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *style;
	double size;

	if (wbcg->updating_ui)
		return TRUE;

#warning "Check what happens when user enters size < 1 (should check too large too)"
	/* Make 1pt a minimum size for fonts */
	size = atof (sizetext);
	if (size < 1.0) {
		/* gtk_entry_set_text (entry, "10"); */
		return FALSE;
	}

	style = mstyle_new ();
	mstyle_set_font_size (style, size);
	cmd_selection_format (wbc, style, NULL, _("Set Font Size"));

	wbcg_focus_cur_scg (wbcg);	/* Restore the focus to the sheet */
	return TRUE;
}

static void
apply_number_format (WorkbookControlGUI *wbcg,
		     char const *translated_format, char const *descriptor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *mstyle = mstyle_new ();

	mstyle_set_format_text (mstyle, translated_format);
	cmd_selection_format (wbc, mstyle, NULL, descriptor);
}

static GNM_ACTION_DEF (cb_format_as_money)
{
	apply_number_format (wbcg,
		cell_formats[FMT_ACCOUNT][2], _("Format as Money"));
}

static GNM_ACTION_DEF (cb_format_as_percent)
{
	apply_number_format (wbcg, "0.00%", _("Format as Percentage"));
}

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
	g_warning ("Killroy was here in cb_copydown");
}

static GNM_ACTION_DEF (cb_copyright)
{
	g_warning ("Killroy was here in cb_copyright");
}

static GtkActionEntry actions[] = {
	{ "MenuFile",		NULL, N_("_File") },
	{ "MenuEdit",		NULL, N_("_Edit") },
		{ "MenuEditClear",	NULL, N_("C_lear") },
		{ "MenuEditSheet",	NULL, N_("S_heet") },
	        { "MenuEditSelect",	NULL, N_("_Select") },
	        { "MenuEditFill",	NULL, N_("_Fill") },
	{ "MenuView",		NULL, N_("_View") },
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
		NULL, N_("Save the current workbook with a different name"),
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
	{ "FilePreferences", GTK_STOCK_PREFERENCES, N_("Preferen_ces..."),
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

/* Edit -> Select */
	{ "EditSelectAll", NULL, N_("Select _All"),
		"<control>A", N_("Select all cells in the spreadsheet"),
		G_CALLBACK (cb_edit_select_all) },
#warning "Check how to write space"
	{ "EditSelectColumn", NULL, N_("Select _Column"),
		"<control><space>", N_("Select an entire column"),
		G_CALLBACK (cb_edit_select_col) },
#warning "Check how to write meta"
	{ "EditSelectRow", NULL, N_("Select _Row"),
		"<meta><space>", N_("Select an entire row"),
		G_CALLBACK (cb_edit_select_row) },
	{ "EditSelectArray", NULL, N_("Select Arra_y"),
		"<control>/", N_("Select an array of cells"),
		G_CALLBACK (cb_edit_select_array) },
	{ "EditSelectDepends", NULL, N_("Select _Depends"),
		"<control>]", N_("Select all the cells that depend on the current edit cell"),
		G_CALLBACK (cb_edit_select_depends) },
	{ "EditSelectInputs", NULL, N_("Select _Inputs"),
		"<control>[", N_("Select all the cells are used by the current edit cell"),
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
	{ "EditCut", GTK_STOCK_CUT, NULL,
		NULL, N_("Cut the selection"),
		G_CALLBACK (cb_edit_cut) },
	{ "EditCopy", GTK_STOCK_COPY, NULL,
		NULL, N_("Copy the selection"),
		G_CALLBACK (cb_edit_copy) },
	{ "EditPaste", GTK_STOCK_PASTE, NULL,
		NULL, N_("Paste the clipboard"),
		G_CALLBACK (cb_edit_paste) },
	{ "EditPasteSpecial", NULL, N_("P_aste special..."),
		NULL, N_("Paste with optional filters and transformations"),
		G_CALLBACK (cb_edit_paste_special) },

	{ "EditDelete", GTK_STOCK_DELETE, N_("_Delete..."),
		  NULL, N_("Remove selected cells, shifting others into their place"),
		  G_CALLBACK (cb_edit_delete) },

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

/* View */
	{ "ViewNew", NULL, N_("_New View..."),
		NULL, N_("Create a new view of the workbook"),
		G_CALLBACK (cb_view_new) },
	{ "ViewZoom", NULL, N_("_Zoom..."),
		NULL, N_("Zoom the spreadsheet in or out"),
		G_CALLBACK (cb_view_zoom) },
	{ "ViewFreezeThawPanes", NULL, N_("_Freeze Panes"),
		NULL, N_("Freeze the top left of the sheet"),
		G_CALLBACK (cb_view_freeze_panes) },

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
		NULL, N_("Launch the Chart Guru"),
		G_CALLBACK (cb_launch_chart_guru) },
	{ "InsertImage", "Gnumeric_InsertImage", N_("_Image..."),
		NULL, N_("Insert an image"),
		G_CALLBACK (cb_insert_image) },

	{ "InsertHyperlink", "Gnumeric_Link_Add", N_("Hyper_link..."),
		"<control>K", N_("Insert a Hyperlink"),
		G_CALLBACK (cb_insert_hyperlink) },
/* Insert -> Special */
	/* Default <Ctrl-;> (control semi-colon) to insert the current date */
	{ "InsertCurrentDate", NULL, N_("Current _date"),
		"<control>;", N_("Insert the current date into the selected cell(s)"),
		G_CALLBACK (cb_insert_current_date) },

	/* Default <Ctrl-:> (control colon) to insert the current time */
	{ "InsertCurrentTime", NULL, N_("Current _time"),
		"<control>:", N_("Insert the current time into the selected cell(s)"),
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
		G_CALLBACK (sheet_dialog_set_column_width) },
	{ "ColumnAutoSize", NULL, N_("_Auto fit selection"),
		NULL, N_("Ensure columns are just wide enough to display content"),
		G_CALLBACK (workbook_cmd_format_column_auto_fit) },
	{ "ColumnHide", "Gnumeric_ColumnHide", N_("_Hide"),
		NULL, N_("Hide the selected columns"),
		G_CALLBACK (workbook_cmd_format_column_hide) },
	{ "ColumnUnhide", "Gnumeric_ColumnUnhide", N_("_Unhide"),
		NULL, N_("Make any hidden columns in the selection visible"),
		G_CALLBACK (workbook_cmd_format_column_unhide) },
	{ "ColumnDefaultSize", NULL, N_("_Standard Width"),
		NULL, N_("Change the default column width"),
		G_CALLBACK (workbook_cmd_format_column_std_width) },

/* Format -> Row */
	{ "RowSize", "Gnumeric_RowSize", N_("H_eight..."),
		NULL, N_("Change height of the selected rows"),
		G_CALLBACK (sheet_dialog_set_row_height) },
	{ "RowAutoSize", NULL, N_("_Auto fit selection"),
		NULL, N_("Ensure rows are just tall enough to display content"),
		G_CALLBACK (workbook_cmd_format_row_auto_fit) },
	{ "RowHide", "Gnumeric_RowHide", N_("_Hide"),
		NULL, N_("Hide the selected rows"),
		G_CALLBACK (workbook_cmd_format_row_hide) },
	{ "RowUnhide", "Gnumeric_RowUnhide", N_("_Unhide"),
		NULL, N_("Make any hidden rows in the selection visible"),
		G_CALLBACK (workbook_cmd_format_row_unhide) },
	{ "RowDefaultSize", NULL, N_("_Standard Height"),
		NULL, N_("Change the default row height"),
		G_CALLBACK (workbook_cmd_format_row_std_height) },

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
	{ "DataAutoFilter", NULL, N_("Add _Auto Filter"),
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

	{ "HelpAbout", NULL, N_("_About"),
		NULL, N_("About this application"),
		G_CALLBACK (cb_help_about) },

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
	{ "FormatAsMoney", "Gnumeric_FormatAsMoney", N_("Money"),
		NULL, N_("Set the format of the selected cells to monetary"),
		G_CALLBACK (cb_format_as_money) },
	{ "FormatAsPercent", "Gnumeric_FormatAsPercent", N_("Percent"),
		NULL, N_("Set the format of the selected cells to percentage"),
		G_CALLBACK (cb_format_as_percent) },
	{ "FormatWithThousands", "Gnumeric_FormatThousandSeparator", N_("Thousands Separator"),
		NULL, N_("Set the format of the selected cells to include a thousands separator"),
		G_CALLBACK (cb_format_with_thousands) },
	{ "FormatIncreasePrecision", "Gnumeric_FormatAddPrecision", N_("Increase Precision"),
		NULL, N_("Increase the number of decimals displayed"),
		G_CALLBACK (cb_format_inc_precision) },
	{ "FormatDecreasePrecision", "Gnumeric_FormatRemovePrecision", N_("Decrease Precision"),
		NULL, N_("Decrease the number of decimals displayed"),
		G_CALLBACK (cb_format_dec_precision) },
#warning "Restore these when the gtk patch lands"
#if 1
	{ "FormatDecreaseIndent", GTK_STOCK_MISSING_IMAGE, N_("Decrease Indent"),
		NULL, N_("Align the contents to the left and decrease the indent"),
		G_CALLBACK (cb_format_dec_indent) },
	{ "FormatIncreaseIndent", GTK_STOCK_MISSING_IMAGE, N_("Increase Indent"),
		NULL, N_("Align the contents to the left and increase the indent"),
		G_CALLBACK (cb_format_inc_indent) },
#else
	{ "FormatDecreaseIndent", GTK_STOCK_UNINDENT, NULL,
		NULL, N_("Align the contents to the left and decrease the indent"),
		G_CALLBACK (cb_format_dec_indent) },
	{ "FormatIncreaseIndent", GTK_STOCK_INDENT, NULL,
		NULL, N_("Align the contents to the left and increase the indent"),
		G_CALLBACK (cb_format_inc_indent) },
#endif
/* Unattached */
	{ "CopyDown", NULL, "", "<control>D", NULL, G_CALLBACK (cb_copydown) },
	{ "CopyRight", NULL, "", "<control>R", NULL, G_CALLBACK (cb_copyright) },
};

#define TOGGLE_HANDLER(flag, code)					\
static GNM_ACTION_DEF (cb_sheet_pref_ ## flag )				\
{									\
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));		\
									\
	if (!wbcg->updating_ui) {					\
		Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));	\
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

static GtkToggleActionEntry toggle_actions[] = {
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
		N_("Align Bottom"), G_CALLBACK (cb_align_bottom), FALSE }
};

void wbcg_register_actions (WorkbookControlGUI *wbcg, GtkActionGroup *group);
void
wbcg_register_actions (WorkbookControlGUI *wbcg, GtkActionGroup *group)
{
	  gtk_action_group_add_actions (group,
		actions, G_N_ELEMENTS (actions), wbcg);
	  gtk_action_group_add_toggle_actions (group,
		toggle_actions, G_N_ELEMENTS (toggle_actions), wbcg);
}

#ifdef WITH_BONOBO
static BonoboUIVerb verbs [] = {

	BONOBO_UI_UNSAFE_VERB ("FileNew", cb_file_new),
	BONOBO_UI_UNSAFE_VERB ("FileOpen", cb_file_open),
	BONOBO_UI_UNSAFE_VERB ("FileSave", cb_file_save),
	BONOBO_UI_UNSAFE_VERB ("FileSaveAs", cb_file_save_as),
	BONOBO_UI_UNSAFE_VERB ("FileSend", cb_file_sendto),
	BONOBO_UI_UNSAFE_VERB ("FilePageSetup", cb_file_page_setup),
	BONOBO_UI_UNSAFE_VERB ("FilePrint", cb_file_print),
	BONOBO_UI_UNSAFE_VERB ("FilePrintPreview", cb_file_print_preview),
	BONOBO_UI_UNSAFE_VERB ("FileSummary", cb_file_summary),
	BONOBO_UI_UNSAFE_VERB ("FilePreferences", cb_file_preferences),
	BONOBO_UI_UNSAFE_VERB ("FileClose", cb_file_close),
	BONOBO_UI_UNSAFE_VERB ("FileQuit", cb_file_quit),

	BONOBO_UI_UNSAFE_VERB ("EditClearAll", cb_edit_clear_all),
	BONOBO_UI_UNSAFE_VERB ("EditClearFormats", cb_edit_clear_formats),
	BONOBO_UI_UNSAFE_VERB ("EditClearComments", cb_edit_clear_comments),
	BONOBO_UI_UNSAFE_VERB ("EditClearContent", cb_edit_clear_content),

	BONOBO_UI_UNSAFE_VERB ("EditSelectAll", cb_edit_select_all),
	BONOBO_UI_UNSAFE_VERB ("EditSelectRow", cb_edit_select_row),
	BONOBO_UI_UNSAFE_VERB ("EditSelectColumn", cb_edit_select_col),
	BONOBO_UI_UNSAFE_VERB ("EditSelectArray", cb_edit_select_array),
	BONOBO_UI_UNSAFE_VERB ("EditSelectDepends", cb_edit_select_depends),
	BONOBO_UI_UNSAFE_VERB ("EditSelectInputs", cb_edit_select_inputs),

	BONOBO_UI_UNSAFE_VERB ("Undo", cb_edit_undo),
	BONOBO_UI_UNSAFE_VERB ("Redo", cb_edit_redo),
	BONOBO_UI_UNSAFE_VERB ("EditCut", cb_edit_cut),
	BONOBO_UI_UNSAFE_VERB ("EditCopy", cb_edit_copy),
	BONOBO_UI_UNSAFE_VERB ("EditPaste", cb_edit_paste),
	BONOBO_UI_UNSAFE_VERB ("EditPasteSpecial", cb_edit_paste_special),
	BONOBO_UI_UNSAFE_VERB ("EditDelete", cb_edit_delete),
	BONOBO_UI_UNSAFE_VERB ("EditDuplicateSheet", cb_edit_duplicate_sheet),
	BONOBO_UI_UNSAFE_VERB ("EditFillAutofill", cb_edit_fill_autofill),
	BONOBO_UI_UNSAFE_VERB ("EditFillSeries", cb_edit_fill_series),
	BONOBO_UI_UNSAFE_VERB ("EditSearch", cb_edit_search),
	BONOBO_UI_UNSAFE_VERB ("EditSearchReplace", cb_edit_search_replace),
	BONOBO_UI_UNSAFE_VERB ("EditGoto", cb_edit_goto),
	BONOBO_UI_UNSAFE_VERB ("EditRecalc", cb_edit_recalc),

	BONOBO_UI_UNSAFE_VERB ("ViewNew", cb_view_new),
	BONOBO_UI_UNSAFE_VERB ("ViewZoom", cb_view_zoom),
	BONOBO_UI_UNSAFE_VERB ("ViewFreezeThawPanes", cb_view_freeze_panes),

	BONOBO_UI_UNSAFE_VERB ("InsertCurrentDate", cb_insert_current_date),
	BONOBO_UI_UNSAFE_VERB ("InsertCurrentTime", cb_insert_current_time),
	BONOBO_UI_UNSAFE_VERB ("EditNames", cb_define_name),

	BONOBO_UI_UNSAFE_VERB ("InsertSheet", wbcg_insert_sheet),
	BONOBO_UI_UNSAFE_VERB ("InsertSheetAtEnd", wbcg_append_sheet),
	BONOBO_UI_UNSAFE_VERB ("InsertRows", cb_insert_rows),
	BONOBO_UI_UNSAFE_VERB ("InsertColumns", cb_insert_cols),
	BONOBO_UI_UNSAFE_VERB ("InsertCells", cb_insert_cells),
	BONOBO_UI_UNSAFE_VERB ("InsertFormula", cb_formula_guru),
	BONOBO_UI_UNSAFE_VERB ("InsertComment", cb_insert_comment),
	BONOBO_UI_UNSAFE_VERB ("InsertImage", cb_insert_image),
	BONOBO_UI_UNSAFE_VERB ("InsertHyperlink", cb_insert_hyperlink),

	BONOBO_UI_UNSAFE_VERB ("ColumnAutoSize", workbook_cmd_format_column_auto_fit),
	BONOBO_UI_UNSAFE_VERB ("ColumnSize", sheet_dialog_set_column_width),
	BONOBO_UI_UNSAFE_VERB ("ColumnHide", workbook_cmd_format_column_hide),
	BONOBO_UI_UNSAFE_VERB ("ColumnUnhide", workbook_cmd_format_column_unhide),
	BONOBO_UI_UNSAFE_VERB ("ColumnDefaultSize", workbook_cmd_format_column_std_width),

	BONOBO_UI_UNSAFE_VERB ("RowAutoSize", workbook_cmd_format_row_auto_fit),
	BONOBO_UI_UNSAFE_VERB ("RowSize", sheet_dialog_set_row_height),
	BONOBO_UI_UNSAFE_VERB ("RowHide", workbook_cmd_format_row_hide),
	BONOBO_UI_UNSAFE_VERB ("RowUnhide", workbook_cmd_format_row_unhide),
	BONOBO_UI_UNSAFE_VERB ("RowDefaultSize", workbook_cmd_format_row_std_height),

	BONOBO_UI_UNSAFE_VERB ("SheetChangeName", cb_sheet_name),
	BONOBO_UI_UNSAFE_VERB ("SheetReorder", cb_sheet_order),
	BONOBO_UI_UNSAFE_VERB ("SheetRemove", cb_sheet_remove),

	BONOBO_UI_UNSAFE_VERB ("FormatCells", cb_format_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatAuto", cb_autoformat),
	BONOBO_UI_UNSAFE_VERB ("FormatWorkbook", cb_workbook_attr),
	BONOBO_UI_UNSAFE_VERB ("FormatGnumeric", cb_format_preferences),

	BONOBO_UI_UNSAFE_VERB ("ToolsPlugins", cb_tools_plugins),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoCorrect", cb_tools_autocorrect),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoSave", cb_tools_auto_save),
	BONOBO_UI_UNSAFE_VERB ("ToolsGoalSeek", cb_tools_goal_seek),
	BONOBO_UI_UNSAFE_VERB ("ToolsTabulate", cb_tools_tabulate),
	BONOBO_UI_UNSAFE_VERB ("ToolsMerge", cb_tools_merge),
	BONOBO_UI_UNSAFE_VERB ("ToolsSolver", cb_tools_solver),
        BONOBO_UI_UNSAFE_VERB ("ToolsScenarioAdd", cb_tools_scenario_add),
        BONOBO_UI_UNSAFE_VERB ("ToolsScenarios", cb_tools_scenarios),
	BONOBO_UI_UNSAFE_VERB ("ToolsSimulation", cb_tools_simulation),

	BONOBO_UI_UNSAFE_VERB ("ToolsANOVAoneFactor", cb_tools_anova_one_factor),
	BONOBO_UI_UNSAFE_VERB ("ToolsANOVAtwoFactor", cb_tools_anova_two_factor),
	BONOBO_UI_UNSAFE_VERB ("ToolsCorrelation", cb_tools_correlation),
	BONOBO_UI_UNSAFE_VERB ("ToolsCovariance", cb_tools_covariance),
	BONOBO_UI_UNSAFE_VERB ("ToolsDescStatistics", cb_tools_desc_statistics),
	BONOBO_UI_UNSAFE_VERB ("ToolsExpSmoothing", cb_tools_exp_smoothing),
	BONOBO_UI_UNSAFE_VERB ("ToolsAverage", cb_tools_average),
	BONOBO_UI_UNSAFE_VERB ("ToolsFourier", cb_tools_fourier),
	BONOBO_UI_UNSAFE_VERB ("ToolsHistogram", cb_tools_histogram),
	BONOBO_UI_UNSAFE_VERB ("ToolsRanking", cb_tools_ranking),
	BONOBO_UI_UNSAFE_VERB ("ToolsRegression", cb_tools_regression),
	BONOBO_UI_UNSAFE_VERB ("ToolsSampling", cb_tools_sampling),
	BONOBO_UI_UNSAFE_VERB ("ToolTTestPaired", cb_tools_ttest_paired),
	BONOBO_UI_UNSAFE_VERB ("ToolTTestEqualVar", cb_tools_ttest_equal_var),
	BONOBO_UI_UNSAFE_VERB ("ToolTTestUnequalVar", cb_tools_ttest_unequal_var),
	BONOBO_UI_UNSAFE_VERB ("ToolZTest", cb_tools_ztest),
	BONOBO_UI_UNSAFE_VERB ("ToolsFTest", cb_tools_ftest),
	BONOBO_UI_UNSAFE_VERB ("RandomGenerator", cb_tools_random_generator),

	BONOBO_UI_UNSAFE_VERB ("DataSort", cb_data_sort),
	BONOBO_UI_UNSAFE_VERB ("DataShuffle", cb_data_shuffle),
	BONOBO_UI_UNSAFE_VERB ("DataAutoFilter", cb_auto_filter),
	BONOBO_UI_UNSAFE_VERB ("DataFilterShowAll", cb_show_all),
	BONOBO_UI_UNSAFE_VERB ("DataFilterAdvancedfilter", cb_data_filter),
	BONOBO_UI_UNSAFE_VERB ("DataValidate", cb_data_validate),
	BONOBO_UI_UNSAFE_VERB ("DataTextToColumns", cb_data_text_to_columns),
	BONOBO_UI_UNSAFE_VERB ("DataConsolidate", cb_data_consolidate),
	BONOBO_UI_UNSAFE_VERB ("DataImportText", cb_data_import_text),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineHideDetail", cb_data_hide_detail),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineShowDetail", cb_data_show_detail),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineGroup", cb_data_group),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineUngroup", cb_data_ungroup),

#ifdef ENABLE_PIVOTS
	BONOBO_UI_UNSAFE_VERB ("PivotTable", cb_data_pivottable),
#endif

	BONOBO_UI_UNSAFE_VERB ("AutoSum", cb_autosum),
	BONOBO_UI_UNSAFE_VERB ("FunctionGuru", cb_formula_guru),
	BONOBO_UI_UNSAFE_VERB ("SortAscending", cb_sort_ascending),
	BONOBO_UI_UNSAFE_VERB ("SortDescending", cb_sort_descending),
	BONOBO_UI_UNSAFE_VERB ("ChartGuru", cb_launch_chart_guru),

	BONOBO_UI_UNSAFE_VERB ("HelpAbout", cb_help_about),

	BONOBO_UI_UNSAFE_VERB ("CreateLabel", cmd_create_label),
	BONOBO_UI_UNSAFE_VERB ("CreateFrame", cmd_create_frame),

	BONOBO_UI_UNSAFE_VERB ("CreateButton", cmd_create_button),
	BONOBO_UI_UNSAFE_VERB ("CreateRadioButton", cmd_create_radiobutton),
	BONOBO_UI_UNSAFE_VERB ("CreateCheckbox", cmd_create_checkbox),

	BONOBO_UI_UNSAFE_VERB ("CreateScrollbar", cmd_create_scrollbar),
	BONOBO_UI_UNSAFE_VERB ("CreateSlider", cmd_create_slider),
	BONOBO_UI_UNSAFE_VERB ("CreateSpinButton", cmd_create_spinbutton),
	BONOBO_UI_UNSAFE_VERB ("CreateList", cmd_create_list),
	BONOBO_UI_UNSAFE_VERB ("CreateCombo", cmd_create_combo),
	BONOBO_UI_UNSAFE_VERB ("CreateLine", cmd_create_line),
	BONOBO_UI_UNSAFE_VERB ("CreateArrow", cmd_create_arrow),
	BONOBO_UI_UNSAFE_VERB ("CreateRectangle", cmd_create_rectangle),
	BONOBO_UI_UNSAFE_VERB ("CreateEllipse", cmd_create_ellipse),

	BONOBO_UI_UNSAFE_VERB ("FontSelect",              &cb_font_name),
	BONOBO_UI_UNSAFE_VERB ("FontBold",		  &cb_font_bold),
	BONOBO_UI_UNSAFE_VERB ("FontItalic",		  &cb_font_italic),
	BONOBO_UI_UNSAFE_VERB ("FontUnderline",	   	  &cb_font_underline),
	BONOBO_UI_UNSAFE_VERB ("AlignLeft",		  &cb_align_left),
	BONOBO_UI_UNSAFE_VERB ("AlignCenter",		  &cb_align_center),
	BONOBO_UI_UNSAFE_VERB ("AlignRight",		  &cb_align_right),
	BONOBO_UI_UNSAFE_VERB ("CenterAcrossSelection",	  &cb_center_across_selection),
	BONOBO_UI_UNSAFE_VERB ("FormatMergeCells",	  &cb_merge_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatUnmergeCells",	  &cb_unmerge_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatAsMoney",	          &cb_format_as_money),
	BONOBO_UI_UNSAFE_VERB ("FormatAsPercent",	  &cb_format_as_percent),
	BONOBO_UI_UNSAFE_VERB ("FormatWithThousands",	  &cb_format_with_thousands),
	BONOBO_UI_UNSAFE_VERB ("FormatIncreasePrecision", &cb_format_inc_precision),
	BONOBO_UI_UNSAFE_VERB ("FormatDecreasePrecision", &cb_format_dec_precision),
	BONOBO_UI_UNSAFE_VERB ("FormatIncreaseIndent",	  &cb_format_inc_indent),
	BONOBO_UI_UNSAFE_VERB ("FormatDecreaseIndent",	  &cb_format_dec_indent),

	BONOBO_UI_VERB_END
};
#endif /* WITH_GNOME */

