/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * wbcg-actions.c: The callbacks and tables for all the menus and stock toolbars
 *
 * Copyright (C) 2003-2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 *
 */
#include <gnumeric-config.h>
#include "gnumeric.h"

#include "libgnumeric.h"
#include "application.h"
#include "gnm-commands-slicer.h"
#include "commands.h"
#include "clipboard.h"
#include "selection.h"
#include "search.h"
#include "ranges.h"
#include "cell.h"
#include "stf.h"
#include "value.h"
#include "gnm-format.h"
#include "sheet.h"
#include "sort.h"
#include "sheet-merge.h"
#include "sheet-filter.h"
#include "sheet-utils.h"
#include "sheet-style.h"
#include "style-border.h"
#include "style-color.h"
#include "tools/filter.h"
#include "sheet-control-gui-priv.h"
#include "sheet-view.h"
#include "cmd-edit.h"
#include "workbook.h"
#include "workbook-view.h"
#include "wbc-gtk-impl.h"
#include "workbook-cmd-format.h"
#include "dialogs/dialogs.h"
#include "sheet-object-image.h"
#include "sheet-object-widget.h"
#include "gnm-so-filled.h"
#include "gnm-so-line.h"
#include "sheet-object-graph.h"
#include "sheet-object-component.h"
#include "gui-util.h"
#include "gui-file.h"
#include "gnumeric-conf.h"
#include "expr.h"
#include "print.h"
#include "print-info.h"
#include "gnm-pane-impl.h"
#include "gutils.h"

#include <goffice/goffice.h>
#include <goffice/component/goffice-component.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <gsf/gsf-input.h>
#include <string.h>
#include <glib/gstdio.h>

static gboolean
cb_cleanup_sendto (gpointer path)
{
	char *dir = g_path_get_dirname (path);

	g_unlink (path);
	g_free (path);	/* the attachment */

	g_rmdir (dir);
	g_free (dir);	/* the tempdir */

	return FALSE;
}


static GNM_ACTION_DEF (cb_file_new)
{
	GdkScreen *screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
	Workbook *wb = workbook_new_with_sheets
		(gnm_conf_get_core_workbook_n_sheet ());
	WBCGtk *new_wbcg = wbc_gtk_new (NULL, wb, screen, NULL);
	wbcg_copy_toolbar_visibility (new_wbcg, wbcg);
}

static GNM_ACTION_DEF (cb_file_open)	{ gui_file_open (wbcg, GNM_FILE_OPEN_STYLE_OPEN, NULL); }
static GNM_ACTION_DEF (cb_file_save)	{ gui_file_save (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg))); }
static GNM_ACTION_DEF (cb_file_save_as)	{ gui_file_save_as
		(wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_SAVE, NULL); }

static GNM_ACTION_DEF (cb_file_sendto) {
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView *wbv = wb_control_view (wbc);
	GOCmdContext *gcc = GO_CMD_CONTEXT (wbcg);
	gboolean problem = FALSE;
	GOIOContext *io_context;
	Workbook *wb;
	GOFileSaver *fs;

	wb = wb_control_get_workbook (wbc);
	g_object_ref (wb);
	fs = workbook_get_file_saver (wb);
	if (fs == NULL)
		fs = go_file_saver_get_default ();

	io_context = go_io_context_new (gcc);
	if (fs != NULL) {
		char *template, *full_name, *uri;
		char *basename = g_path_get_basename (go_doc_get_uri (GO_DOC (wb)));

		template = g_build_filename (g_get_tmp_dir (),
					     ".gnm-sendto-XXXXXX", NULL);
		problem = (g_mkdtemp_full (template, 0600) == NULL);

		if (problem) {
			g_free (template);
			goto out;
		}

		full_name = g_build_filename (template, basename, NULL);
		g_free (basename);
		uri = go_filename_to_uri (full_name);

		wb_view_save_to_uri (wbv, fs, uri, io_context);

		if (go_io_error_occurred (io_context) ||
		    go_io_warning_occurred (io_context))
			go_io_error_display (io_context);

		if (go_io_error_occurred (io_context)) {
			problem = TRUE;
		} else {
			/* mutt does not handle urls with no destination
			 * so pick something to arbitrary */
			GError *err;
			GdkScreen *screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
			char *url, *tmp = go_url_encode (full_name, 0);
			url = g_strdup_printf ("mailto:someone?attach=%s", tmp);
			g_free (tmp);

			err = go_gtk_url_show (url, screen);

			if (err != NULL) {
				go_cmd_context_error (GO_CMD_CONTEXT (io_context), err);
				g_error_free (err);
				go_io_error_display (io_context);
				problem = TRUE;
			}
		}
		g_free (template);
		g_free (uri);

		if (problem) {
			cb_cleanup_sendto (full_name);
		} else {
			/*
			 * We wait a while before we clean up to ensure the file is
			 * loaded by the mailer.
			 */
			g_timeout_add (1000 * 10, cb_cleanup_sendto, full_name);
		}
	} else {
		go_cmd_context_error_export (GO_CMD_CONTEXT (io_context),
			_("Default file saver is not available."));
		go_io_error_display (io_context);
		problem = TRUE;
	}

 out:
	g_object_unref (io_context);
	g_object_unref (wb);

	/* What do we do with "problem"? */
}

static GNM_ACTION_DEF (cb_file_page_setup)
{
	dialog_printer_setup (wbcg, wbcg_cur_sheet (wbcg));
}

static GNM_ACTION_DEF (cb_file_print_area_set)
{
	Sheet *sheet = wbcg_cur_sheet (wbcg);
	SheetView *sv = sheet_get_view (sheet, wb_control_view (WORKBOOK_CONTROL (wbcg)));
	GnmParsePos pp;
	char *message;
	char * selection;
	GnmRange const *r = selection_first_range (sv,
				       GO_CMD_CONTEXT (wbcg), _("Set Print Area"));
	if (r != NULL) {
		parse_pos_init_sheet (&pp, sheet);
		selection = undo_range_name (sheet, r);
		message = g_strdup_printf (_("Set Print Area to %s"), selection);
		cmd_define_name	(WORKBOOK_CONTROL (wbcg), "Print_Area", &pp,
				 gnm_expr_top_new_constant
				 (value_new_cellrange_r (NULL, r)),
				 message);
		g_free (selection);
		g_free (message);
	}
}

static GNM_ACTION_DEF (cb_file_print_area_clear)
{
	GnmParsePos pp;
	Sheet *sheet = wbcg_cur_sheet (wbcg);

	parse_pos_init_sheet (&pp, sheet);
	cmd_define_name	(WORKBOOK_CONTROL (wbcg), "Print_Area", &pp,
			 gnm_expr_top_new_constant
			 (value_new_error_REF (NULL)),
			 _("Clear Print Area"));
}

static GNM_ACTION_DEF (cb_file_print_area_show)
{
	Sheet *sheet = wbcg_cur_sheet (wbcg);
	GnmRange *r = sheet_get_nominal_printarea (sheet);

	if (r != NULL) {
		SheetView *sv = sheet_get_view (sheet,
						wb_control_view (WORKBOOK_CONTROL (wbcg)));
		wb_control_sheet_focus (WORKBOOK_CONTROL (wbcg), sheet);
		sv_selection_reset (sv);
		sv_selection_add_range (sv, r);
		sv_make_cell_visible (sv, r->start.col, r->start.row, FALSE);
		g_free (r);
	}
}

static GNM_ACTION_DEF (cb_file_print_area_toggle_col)
{
	cmd_page_break_toggle (WORKBOOK_CONTROL (wbcg),
			       wbcg_cur_sheet (wbcg),
			       TRUE);
}
static GNM_ACTION_DEF (cb_file_print_area_toggle_row)
{
	cmd_page_break_toggle (WORKBOOK_CONTROL (wbcg),
			       wbcg_cur_sheet (wbcg),
			       FALSE);
}

static GNM_ACTION_DEF (cb_file_print_area_clear_pagebreaks)
{
	cmd_page_breaks_clear (WORKBOOK_CONTROL (wbcg), wbcg_cur_sheet (wbcg));
}

static GNM_ACTION_DEF (cb_file_print)
{
	gnm_print_sheet (WORKBOOK_CONTROL (wbcg),
		wbcg_cur_sheet (wbcg), FALSE, GNM_PRINT_SAVED_INFO, NULL);
}

static GNM_ACTION_DEF (cb_file_print_preview)
{
	gnm_print_sheet (WORKBOOK_CONTROL (wbcg),
		wbcg_cur_sheet (wbcg), TRUE, GNM_PRINT_ACTIVE_SHEET, NULL);
}

static GNM_ACTION_DEF (cb_doc_meta_data)	{ dialog_doc_metadata_new (wbcg, 0); }
static GNM_ACTION_DEF (cb_file_preferences)	{ dialog_preferences (wbcg, NULL); }
static GNM_ACTION_DEF (cb_file_history_full)    { dialog_recent_used (wbcg); }
static GNM_ACTION_DEF (cb_file_close)		{ wbc_gtk_close (wbcg); }

static GNM_ACTION_DEF (cb_file_quit)
{
	/* If we are still loading initial files, short circuit */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		return;
	}

	/* If we were editing when the quit request came in abort the edit. */
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);

	dialog_quit (wbcg);
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_edit_clear_all)
{
	cmd_selection_clear (WORKBOOK_CONTROL (wbcg),
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_OBJECTS | CLEAR_COMMENTS);
}

static GNM_ACTION_DEF (cb_edit_clear_formats)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_FORMATS); }
static GNM_ACTION_DEF (cb_edit_clear_comments)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_COMMENTS); }
static GNM_ACTION_DEF (cb_edit_clear_content)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_VALUES); }
static GNM_ACTION_DEF (cb_edit_clear_all_filtered)
{
	cmd_selection_clear (WORKBOOK_CONTROL (wbcg),
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_OBJECTS | CLEAR_COMMENTS | CLEAR_FILTERED_ONLY);
}

static GNM_ACTION_DEF (cb_edit_clear_formats_filtered)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_FORMATS | CLEAR_FILTERED_ONLY); }
static GNM_ACTION_DEF (cb_edit_clear_comments_filtered)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_COMMENTS | CLEAR_FILTERED_ONLY); }
static GNM_ACTION_DEF (cb_edit_clear_content_filtered)
	{ cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_VALUES | CLEAR_FILTERED_ONLY); }

static GNM_ACTION_DEF (cb_edit_delete_rows)
{
	WorkbookControl *wbc   = WORKBOOK_CONTROL (wbcg);
	SheetView       *sv    = wb_control_cur_sheet_view (wbc);
	Sheet           *sheet = wb_control_cur_sheet (wbc);
	GnmRange const  *sel;
	int rows;

	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Delete"))))
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

	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Delete"))))
		return;
	cols = range_width (sel);

	cmd_delete_cols (wbc, sheet, sel->start.col, cols);
}

static GNM_ACTION_DEF (cb_edit_delete_cells)
{
	dialog_delete_cells (wbcg);
}
static GNM_ACTION_DEF (cb_edit_delete_links)
	{
		SheetControlGUI *scg = wbcg_cur_scg (wbcg);
		GnmStyle *style = gnm_style_new ();
		GSList *l;
		int n_links = 0;
		gchar const *format;
		gchar *name;
		WorkbookControl *wbc   = WORKBOOK_CONTROL (wbcg);
		Sheet *sheet = wb_control_cur_sheet (wbc);

		for (l = scg_view (scg)->selections; l != NULL; l = l->next) {
			GnmRange const *r = l->data;
			GnmStyleList *styles;

			styles = sheet_style_collect_hlinks (sheet, r);
			n_links += g_slist_length (styles);
			style_list_free (styles);
		}
		format = ngettext ("Remove %d Link", "Remove %d Links", n_links);
		name = g_strdup_printf (format, n_links);
		gnm_style_set_hlink (style, NULL);
		cmd_selection_format (wbc, style, NULL, name);
		g_free (name);
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
static GNM_ACTION_DEF (cb_edit_select_object)
{
	scg_object_select_next (wbcg_cur_scg (wbcg), FALSE);
}

static GNM_ACTION_DEF (cb_edit_cut)
{
	if (!wbcg_is_editing (wbcg)) {
		SheetControlGUI *scg = wbcg_cur_scg (wbcg);
		WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		if (scg != NULL && scg->selected_objects != NULL)
			gnm_app_clipboard_cut_copy_obj (wbc, TRUE, sv,
				go_hash_keys (scg->selected_objects));
		else
			sv_selection_cut (sv, wbc);
	} else
		gtk_editable_cut_clipboard (GTK_EDITABLE (wbcg_get_entry (wbcg)));
}

static GNM_ACTION_DEF (cb_edit_copy)
{
	if (!wbcg_is_editing (wbcg)) {
		SheetControlGUI *scg = wbcg_cur_scg (wbcg);
		WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		if (scg != NULL && scg->selected_objects != NULL)
			gnm_app_clipboard_cut_copy_obj (wbc, FALSE, sv,
				go_hash_keys (scg->selected_objects));
		else
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
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	if (scg)
		scg_delete_sheet_if_possible (scg);
}

static void
common_cell_goto (WBCGtk *wbcg, Sheet *sheet, GnmCellPos const *pos)
{
	SheetView *sv;
	WorkbookView *wbv;

	if (!sheet_is_visible (sheet))
		return;

	wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	sv = sheet_get_view (sheet, wbv);
	wb_view_sheet_focus (wbv, sheet);
	sv_selection_set (sv, pos,
		pos->col, pos->row,
		pos->col, pos->row);
	sv_make_cell_visible (sv, pos->col, pos->row, FALSE);
}

static int
cb_edit_search_replace_query (GnmSearchReplaceQuery q, GnmSearchReplace *sr, ...)
{
	int res;
	va_list pvar;
	WBCGtk *wbcg = sr->user_data;

	va_start (pvar, sr);

	switch (q) {
	case GNM_SRQ_FAIL: {
		GnmCell *cell = va_arg (pvar, GnmCell *);
		char const *old_text = va_arg (pvar, char const *);
		char const *new_text = va_arg (pvar, char const *);
		char *err = g_strdup_printf
			(_("In cell %s, the current contents\n"
			   "        %s\n"
			   "would have been replaced by\n"
			   "        %s\n"
			   "which is invalid.\n\n"
			   "The replace has been aborted "
			   "and nothing has been changed."),
			 cell_name (cell),
			 old_text,
			 new_text);

		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				      "%s", err);
		g_free (err);
		res = GTK_RESPONSE_NO;
		break;
	}

	case GNM_SRQ_QUERY: {
		GnmCell *cell = va_arg (pvar, GnmCell *);
		char const *old_text = va_arg (pvar, char const *);
		char const *new_text = va_arg (pvar, char const *);
		Sheet *sheet = cell->base.sheet;
		char *pos_name = g_strconcat (sheet->name_unquoted, "!",
					      cell_name (cell), NULL);

		common_cell_goto (wbcg, sheet, &cell->pos);

		res = dialog_search_replace_query (wbcg, sr, pos_name,
						   old_text, new_text);
		g_free (pos_name);
		break;
	}

	case GNM_SRQ_QUERY_COMMENT: {
		Sheet *sheet = va_arg (pvar, Sheet *);
		GnmCellPos *cp = va_arg (pvar, GnmCellPos *);
		char const *old_text = va_arg (pvar, char const *);
		char const *new_text = va_arg (pvar, char const *);
		char *pos_name = g_strdup_printf (_("Comment in cell %s!%s"),
						  sheet->name_unquoted,
						  cellpos_as_string (cp));
		common_cell_goto (wbcg, sheet, cp);

		res = dialog_search_replace_query (wbcg, sr, pos_name,
						   old_text, new_text);
		g_free (pos_name);
		break;
	}

	default:
		/* Shouldn't really happen.  */
		res = GTK_RESPONSE_CANCEL;
	}

	va_end (pvar);
	return res;
}

static gboolean
cb_edit_search_replace_action (WBCGtk *wbcg,
			       GnmSearchReplace *sr)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);

	sr->query_func = cb_edit_search_replace_query;
	sr->user_data = wbcg;

	return cmd_search_replace (wbc, sr);
}


static GNM_ACTION_DEF (cb_edit_search_replace) { dialog_search_replace (wbcg, cb_edit_search_replace_action); }
static GNM_ACTION_DEF (cb_edit_search) { dialog_search (wbcg); }

static GNM_ACTION_DEF (cb_edit_fill_autofill)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	Sheet	  *sheet = wb_control_cur_sheet (wbc);

	GnmRange const *total = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Autofill"));
	if (total) {
		GnmRange src = *total;
		gboolean do_loop;
		GSList *merges, *ptr;

		if (sheet_range_trim (sheet, &src, TRUE, TRUE))
			return; /* Region totally empty */

		/* trim is a bit overzealous, it forgets about merges */
		do {
			do_loop = FALSE;
			merges = gnm_sheet_merge_get_overlap (sheet, &src);
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

static GNM_ACTION_DEF (cb_edit_goto_top)
{
	wb_control_navigate_to_cell (WORKBOOK_CONTROL (wbcg), navigator_top);
}
static GNM_ACTION_DEF (cb_edit_goto_bottom)
{
	wb_control_navigate_to_cell (WORKBOOK_CONTROL (wbcg), navigator_bottom);
}
static GNM_ACTION_DEF (cb_edit_goto_first)
{
	wb_control_navigate_to_cell (WORKBOOK_CONTROL (wbcg), navigator_first);
}
static GNM_ACTION_DEF (cb_edit_goto_last)
{
	wb_control_navigate_to_cell (WORKBOOK_CONTROL (wbcg), navigator_last);
}
static GNM_ACTION_DEF (cb_edit_goto)
{
	dialog_goto_cell (wbcg);
}
static GNM_ACTION_DEF (cb_edit_goto_cell_indicator)
{
	if (IS_WBC_GTK (wbcg))
		wbcg_focus_current_cell_indicator (WBC_GTK (wbcg));
}

static GNM_ACTION_DEF (cb_edit_recalc)
{
	/* TODO :
	 * f9  -  do any necessary calculations across all sheets
	 * shift-f9  -  do any necessary calcs on current sheet only
	 * ctrl-alt-f9 -  force a full recalc across all sheets
	 * ctrl-alt-shift-f9  -  a full-monty super recalc
	 */
	workbook_recalc_all (wb_control_get_workbook (WORKBOOK_CONTROL (wbcg)));
}

static GNM_ACTION_DEF (cb_repeat)	{ command_repeat (WORKBOOK_CONTROL (wbcg)); }

/****************************************************************************/

static GNM_ACTION_DEF (cb_direction)
{
	cmd_toggle_rtl (WORKBOOK_CONTROL (wbcg),
		wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}

static GNM_ACTION_DEF (cb_view_zoom_in)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	int zoom = (int)(sheet->last_zoom_factor_used * 100. + .5) - 10;
	if ((zoom % 15) != 0)
		zoom = 15 * (int)(zoom/15);
	zoom += 15;
	if (zoom <= 390)
		cmd_zoom (WORKBOOK_CONTROL (wbcg), g_slist_append (NULL, sheet),
			  (double) (zoom + 10) / 100);
}
static GNM_ACTION_DEF (cb_view_zoom_out)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	int zoom = (int)(sheet->last_zoom_factor_used * 100. + .5) - 10;
	if ((zoom % 15) != 0)
		zoom = 15 * (int)(zoom/15);
	else
		zoom -= 15;
	if (0 <= zoom)
		cmd_zoom (WORKBOOK_CONTROL (wbcg), g_slist_append (NULL, sheet),
			  (double) (zoom + 10) / 100);
}

static GNM_ACTION_DEF (cb_view_fullscreen)
{
	if (wbcg->is_fullscreen)
		gtk_window_unfullscreen (wbcg_toplevel (wbcg));
	else
		gtk_window_fullscreen (wbcg_toplevel (wbcg));
}

static GNM_ACTION_DEF (cb_view_zoom)	{ dialog_zoom (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_view_new)	{ dialog_new_view (wbcg); }
static GNM_ACTION_DEF (cb_view_freeze_panes)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);

	scg_mode_edit (scg);
	if (scg->active_panes == 1) {
		gboolean center = FALSE;
		GnmPane const *pane = scg_pane (scg, 0);
		GnmCellPos frozen_tl, unfrozen_tl;

		frozen_tl = pane->first;
		unfrozen_tl = sv->edit_pos;

		if (unfrozen_tl.row == 0 && unfrozen_tl.col == 0) {
			GnmRange const *first = selection_first_range (sv, NULL, NULL);
			Sheet *sheet = sv_sheet (sv);
			gboolean full_rows = range_is_full (first, sheet, TRUE);
			gboolean full_cols = range_is_full (first, sheet, FALSE);
			if (!full_rows || !full_cols) {
				if (!full_rows && !full_cols) {
					unfrozen_tl.row = first->end.row + 1;
					unfrozen_tl.col = first->end.col + 1;
				} else if (full_rows) {
					unfrozen_tl.row = first->end.row + 1;
					unfrozen_tl.col = 0;
				} else {
					unfrozen_tl.row = 0;
					unfrozen_tl.col = first->end.col + 1;
				}
			}
		}

                /* If edit pos is out of visible range */
		if (unfrozen_tl.col < pane->first.col ||
		    unfrozen_tl.col > pane->last_visible.col ||
		    unfrozen_tl.row < pane->first.row ||
		    unfrozen_tl.row > pane->last_visible.row)
			center = TRUE;

		if (unfrozen_tl.col == pane->first.col) {
			/* or edit pos is in top left visible cell */
			if (unfrozen_tl.row == pane->first.row)
				center = TRUE;
			else
				unfrozen_tl.col = frozen_tl.col = 0;
		} else if (unfrozen_tl.row == pane->first.row)
			unfrozen_tl.row = frozen_tl.row = 0;

		if (center) {
			unfrozen_tl.col = (pane->first.col +
					   pane->last_visible.col) / 2;
			unfrozen_tl.row = (pane->first.row +
					   pane->last_visible.row) / 2;
		}

		g_return_if_fail (unfrozen_tl.col > frozen_tl.col ||
				  unfrozen_tl.row > frozen_tl.row);

		sv_freeze_panes (sv, &frozen_tl, &unfrozen_tl);
	} else
		sv_freeze_panes (sv, NULL, NULL);
}

/****************************************************************************/

static void
insert_date_time_common (WBCGtk *wbcg, gboolean do_date, gboolean do_time)
{
	if (wbcg_edit_start (wbcg, FALSE, FALSE)) {
		WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		Sheet *sheet = sv_sheet (sv);
		GnmCell const *cell = sheet_cell_fetch (sheet,
							sv->edit_pos.col,
							sv->edit_pos.row);
		GODateConventions const *date_conv =
			workbook_date_conv (sheet->workbook);
		GnmValue *v = value_new_float
			(go_date_timet_to_serial_raw (time (NULL), date_conv));
		char *txt;
		char *dtxt = NULL;
		char *ttxt = NULL;

		if (do_date) {
			GOFormat *fmt = gnm_format_for_date_editing (cell);
			dtxt = format_value (fmt, v, -1, date_conv);
			go_format_unref (fmt);
		}

		if (do_time) {
			GOFormat const *fmt = go_format_default_time ();
			ttxt = format_value (fmt, v, -1, date_conv);
		}

		if (do_date && do_time) {
			txt = g_strconcat (dtxt, " ", ttxt, NULL);
			g_free (dtxt);
			g_free (ttxt);
		} else if (do_date)
			txt = dtxt;
		else
			txt = ttxt;

		wb_control_edit_line_set (wbc, txt);

		value_release (v);
		g_free (txt);
	}
}



static GNM_ACTION_DEF (cb_insert_current_date_time)
{
	insert_date_time_common (wbcg, TRUE, TRUE);
}
static GNM_ACTION_DEF (cb_insert_current_date)
{
	insert_date_time_common (wbcg, TRUE, FALSE);
}

static GNM_ACTION_DEF (cb_insert_current_time)
{
	insert_date_time_common (wbcg, FALSE, TRUE);
}

static GNM_ACTION_DEF (cb_define_name)
{
	dialog_define_names (wbcg);
}
static GNM_ACTION_DEF (cb_paste_names)
{
	dialog_paste_names (wbcg);
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
	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Insert rows"))))
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
	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc),
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
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	dialog_sheet_rename (wbcg, sheet);
}

static GNM_ACTION_DEF (cb_sheet_order)		{ dialog_sheet_order (wbcg); }
static GNM_ACTION_DEF (cb_sheet_resize)		{ dialog_sheet_resize (wbcg); }
static GNM_ACTION_DEF (cb_format_cells)		{ dialog_cell_format (wbcg, FD_CURRENT, 0); }
static GNM_ACTION_DEF (cb_format_cells_cond)    { dialog_cell_format_cond (wbcg); }
static GNM_ACTION_DEF (cb_autoformat)		{ dialog_autoformat (wbcg); }
static GNM_ACTION_DEF (cb_workbook_attr)	{ dialog_workbook_attr (wbcg); }
static GNM_ACTION_DEF (cb_tools_plugins)	{ dialog_plugin_manager (wbcg); }
static GNM_ACTION_DEF (cb_tools_autocorrect)	{ dialog_preferences (wbcg, "Auto Correct"); }
static GNM_ACTION_DEF (cb_tools_auto_save)	{ dialog_autosave (wbcg); }
static GNM_ACTION_DEF (cb_tools_goal_seek)	{ dialog_goal_seek (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_tabulate)	{ dialog_tabulate (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_merge)		{ dialog_merge (wbcg); }

static GNM_ACTION_DEF (cb_tools_solver)         { dialog_solver (wbcg, wbcg_cur_sheet (wbcg)); }

static GNM_ACTION_DEF (cb_tools_scenario_add)	{ dialog_scenario_add (wbcg); }
static GNM_ACTION_DEF (cb_tools_scenarios)	{ dialog_scenarios (wbcg); }
static GNM_ACTION_DEF (cb_tools_simulation)	{ dialog_simulation (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_anova_one_factor) { dialog_anova_single_factor_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_anova_two_factor) { dialog_anova_two_factor_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_chi_square_independence) { dialog_chi_square_tool (wbcg, wbcg_cur_sheet (wbcg), TRUE); }
static GNM_ACTION_DEF (cb_tools_chi_square_homogeneity) { dialog_chi_square_tool (wbcg, wbcg_cur_sheet (wbcg), FALSE); }
static GNM_ACTION_DEF (cb_tools_correlation)	{ dialog_correlation_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_covariance)	{ dialog_covariance_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_desc_statistics) { dialog_descriptive_stat_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_exp_smoothing)	{ dialog_exp_smoothing_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_average)	{ dialog_average_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_fourier)	{ dialog_fourier_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_frequency)	{ dialog_frequency_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_histogram)	{ dialog_histogram_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_kaplan_meier)	{ dialog_kaplan_meier_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_normality_tests){ dialog_normality_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_one_mean_test)	{ dialog_one_mean_test_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_principal_components)	{ dialog_principal_components_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_ranking)	{ dialog_ranking_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_regression)	{ dialog_regression_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_sampling)	{ dialog_sampling_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_sign_test_one_median)	{ dialog_sign_test_tool (wbcg, wbcg_cur_sheet (wbcg), SIGNTEST); }
static GNM_ACTION_DEF (cb_tools_sign_test_two_medians)	{ dialog_sign_test_two_tool (wbcg, wbcg_cur_sheet (wbcg), SIGNTEST); }
static GNM_ACTION_DEF (cb_tools_wilcoxon_signed_rank_one_median)	{ dialog_sign_test_tool (wbcg, wbcg_cur_sheet (wbcg), SIGNTEST_WILCOXON); }
static GNM_ACTION_DEF (cb_tools_wilcoxon_signed_rank_two_medians)	{ dialog_sign_test_two_tool (wbcg, wbcg_cur_sheet (wbcg), SIGNTEST_WILCOXON); }
static GNM_ACTION_DEF (cb_tools_wilcoxon_mann_whitney)	{ dialog_wilcoxon_m_w_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_ttest_paired)	{ dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_PAIRED); }
static GNM_ACTION_DEF (cb_tools_ttest_equal_var) { dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_UNPAIRED_EQUALVARIANCES); }
static GNM_ACTION_DEF (cb_tools_ttest_unequal_var) { dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_UNPAIRED_UNEQUALVARIANCES); }
static GNM_ACTION_DEF (cb_tools_ztest)		{ dialog_ttest_tool (wbcg, wbcg_cur_sheet (wbcg), TTEST_ZTEST); }
static GNM_ACTION_DEF (cb_tools_ftest)		{ dialog_ftest_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_random_generator_uncorrelated) { dialog_random_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_tools_random_generator_correlated) { dialog_random_cor_tool (wbcg, wbcg_cur_sheet (wbcg)); }
static GNM_ACTION_DEF (cb_data_sort)		{ dialog_cell_sort (wbcg); }
static GNM_ACTION_DEF (cb_data_shuffle)		{ dialog_shuffle (wbcg); }
static GNM_ACTION_DEF (cb_data_import_text)	{ gui_file_open
		(wbcg, GNM_FILE_OPEN_STYLE_IMPORT, "Gnumeric_stf:stf_assistant"); }
static GNM_ACTION_DEF (cb_data_import_other)	{ gui_file_open
		(wbcg, GNM_FILE_OPEN_STYLE_IMPORT, NULL); }

static GNM_ACTION_DEF (cb_auto_filter)          { cmd_autofilter_add_remove (WORKBOOK_CONTROL (wbcg)); }
static GNM_ACTION_DEF (cb_show_all)		{ filter_show_all (WORKBOOK_CONTROL (wbcg)); }
static GNM_ACTION_DEF (cb_data_filter)		{ dialog_advanced_filter (wbcg); }
static GNM_ACTION_DEF (cb_data_validate)	{ dialog_cell_format (wbcg, FD_VALIDATION, 
								      (1 << FD_VALIDATION) | (1 << FD_INPUT_MSG)); }
static GNM_ACTION_DEF (cb_data_text_to_columns) { stf_text_to_columns (WORKBOOK_CONTROL (wbcg), GO_CMD_CONTEXT (wbcg)); }
static GNM_ACTION_DEF (cb_data_consolidate)	{ dialog_consolidate (wbcg); }
static GNM_ACTION_DEF (cb_data_table)		{ dialog_data_table (wbcg); }
static GNM_ACTION_DEF (cb_data_slicer_create)	{ dialog_data_slicer (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_slicer_refresh)	{ cmd_slicer_refresh (WORKBOOK_CONTROL (wbcg)); }
static GNM_ACTION_DEF (cb_data_slicer_edit)	{ dialog_data_slicer (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_data_export)	        { gui_file_save_as
		(wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_EXPORT, NULL); }
static GNM_ACTION_DEF (cb_data_export_text)	        { gui_file_save_as
		(wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_EXPORT, "Gnumeric_stf:stf_assistant"); }
static GNM_ACTION_DEF (cb_data_export_csv)	        { gui_file_save_as
		(wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_EXPORT, "Gnumeric_stf:stf_csv"); }
static GNM_ACTION_DEF (cb_data_export_repeat)	{ gui_file_export_repeat (wbcg); }

static void
hide_show_detail_real (WBCGtk *wbcg, gboolean is_cols, gboolean show)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	char const *operation = show ? _("Show Detail") : _("Hide Detail");
	GnmRange const *r = selection_first_range (sv, GO_CMD_CONTEXT (wbc),
						operation);

	/* This operation can only be performed on a whole existing group */
	if (sheet_colrow_can_group (sv_sheet (sv), r, is_cols)) {
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc), operation,
			_("can only be performed on an existing group"));
		return;
	}

	cmd_selection_colrow_hide (wbc, is_cols, show);
}

static void
hide_show_detail (WBCGtk *wbcg, gboolean show)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	Sheet const *sheet = sv_sheet (sv);
	char const *operation = show ? _("Show Detail") : _("Hide Detail");
	GnmRange const *r = selection_first_range (sv,
		GO_CMD_CONTEXT (wbc), operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, sheet, TRUE) ^ range_is_full (r, sheet, FALSE))
		is_cols = !range_is_full (r, sheet, TRUE);
	else {
		dialog_col_row (wbcg, operation,
			(ColRowCallback_t) hide_show_detail_real,
			GINT_TO_POINTER (show));
		return;
	}

	hide_show_detail_real (wbcg, is_cols, show);
}

static void
group_ungroup_colrow (WBCGtk *wbcg, gboolean group)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	Sheet const *sheet = sv_sheet (sv);
	char const *operation = group ? _("Group") : _("Ungroup");
	GnmRange const *r = selection_first_range (sv,
		GO_CMD_CONTEXT (wbc), operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, sheet, TRUE) ^ range_is_full (r, sheet, FALSE))
		is_cols = !range_is_full (r, sheet, TRUE);
	else {
		dialog_col_row (wbcg, operation,
			(ColRowCallback_t) cmd_selection_group,
			GINT_TO_POINTER (group));
		return;
	}

	cmd_selection_group (wbc, is_cols, group);
}

static GNM_ACTION_DEF (cb_data_hide_detail)	{ hide_show_detail (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_data_show_detail)	{ hide_show_detail (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_group)		{ group_ungroup_colrow (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_ungroup)		{ group_ungroup_colrow (wbcg, FALSE); }

static GNM_ACTION_DEF (cb_help_function)	{ dialog_function_select_help (wbcg); }
static GNM_ACTION_DEF (cb_help_docs)
{
	char   *argv[] = { NULL, NULL, NULL };
	GError *err = NULL;

#ifndef G_OS_WIN32
	argv[0] = (char *)"yelp";
	argv[1] = (char *)"ghelp:gnumeric";
	g_spawn_async (NULL, argv, NULL,
		       G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL
		       | G_SPAWN_STDERR_TO_DEV_NULL,
		       NULL, NULL, NULL, &err);
#else
	/* TODO : Should really start in same directory as the gspawn-* helpers
	 * are installed in case they are not in the path */
	argv[0] = (char *)"hh";
	argv[1] = g_build_filename (gnm_sys_data_dir (), "doc", "C",
			"gnumeric.chm", NULL);
	g_spawn_async (NULL, argv, NULL,
		       G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL
		       | G_SPAWN_STDERR_TO_DEV_NULL,
		       NULL, NULL, NULL, &err);
	g_free (argv[1]);
#endif
	if (NULL != err) {
		GOErrorInfo *ei = go_error_info_new_printf
			(_("Unable to start the help browser (%s).\n"
			   "The system error message is: \n\n%s"),
			 argv[0], err->message);
		go_cmd_context_error_info (GO_CMD_CONTEXT (wbcg), ei);
		g_error_free (err);
		g_free (ei);
	}
}

static void
show_url (WBCGtk *wbcg, const char *url)
{
	GError *err;
	GdkScreen *screen;

	screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
	err = go_gtk_url_show (url, screen);
	if (err != NULL) {
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);
		g_error_free (err);
	}
}


static GNM_ACTION_DEF (cb_help_web)
{
	show_url (wbcg, "http://www.gnumeric.org/");
}

static GNM_ACTION_DEF (cb_help_irc)
{
	show_url (wbcg, "irc://irc.gnome.org/gnumeric");
}

static GNM_ACTION_DEF (cb_help_bug)
{
	show_url (wbcg, "http://bugzilla.gnome.org/enter_bug.cgi?product=Gnumeric");
}

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
		gtk_editable_set_position (GTK_EDITABLE (entry),
					   gtk_entry_get_text_length (entry)-1);
	}
}

static GNM_ACTION_DEF (cb_insert_image)
{
	char *uri = go_gtk_select_image (wbcg_toplevel (wbcg), NULL);

	if (uri) {
		GError *err = NULL;
		GsfInput *input = go_file_open (uri, &err);

		if (input != NULL) {
			unsigned len = gsf_input_size (input);
			guint8 const *data = gsf_input_read (input, len, NULL);
			SheetObjectImage *soi = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL);
			sheet_object_image_set_image (soi, "", (guint8 *)data, len, TRUE);
			wbcg_insert_object (wbcg, SHEET_OBJECT (soi));
			g_object_unref (input);
		} else
			go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);

		g_free (uri);
	}
}

static GNM_ACTION_DEF (cb_insert_hyperlink)	{ dialog_hyperlink (wbcg, SHEET_CONTROL (wbcg_cur_scg (wbcg))); }
static GNM_ACTION_DEF (cb_formula_guru)		{ dialog_formula_guru (wbcg, NULL); }
static GNM_ACTION_DEF (cb_insert_sort_ascending) { workbook_cmd_wrap_sort (WORKBOOK_CONTROL (wbcg), 1);}
static GNM_ACTION_DEF (cb_insert_sort_descending){ workbook_cmd_wrap_sort (WORKBOOK_CONTROL (wbcg), 0);}

static void
sort_by_rows (WBCGtk *wbcg, gboolean descending)
{
	SheetView *sv;
	GnmRange *sel;
	GnmRange tmp_ns ={{0,0},{0,0}}, tmp_s ={{0,0},{0,0}};
	GnmSortData *data;
	GnmSortClause *clause;
	int numclause, i;
	GSList *l;
	int cnt_singletons = 0, cnt_non_singletons = 0;
	gboolean top_to_bottom = TRUE;
	gboolean not_acceptable = FALSE;

	g_return_if_fail (IS_WBC_GTK (wbcg));

	sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	for (l = sv->selections; l != NULL; l = l->next) {
		GnmRange const *r = l->data;
		if (range_is_singleton (r)) {
			cnt_singletons++;
			tmp_s = *r;
		} else {
			cnt_non_singletons++;
			tmp_ns = *r;
		}
	}

	not_acceptable = (cnt_non_singletons > 1 ||
			  (cnt_non_singletons == 0 && cnt_singletons > 1));

	if (!not_acceptable && cnt_singletons > 0 && cnt_non_singletons == 1) {
		gboolean first = TRUE;
		for (l = sv->selections; l != NULL; l = l->next) {
			GnmRange const *r = l->data;
			gboolean t_b = FALSE, l_r = FALSE;

			if (!range_is_singleton (r))
				continue;
			t_b = r->start.col >= tmp_ns.start.col &&
				r->end.col <= tmp_ns.end.col;
			l_r = r->start.row >= tmp_ns.start.row &&
				r->end.row <= tmp_ns.end.row;
			if (!t_b && !l_r) {
				not_acceptable = TRUE;
				break;
			}
			if (!t_b || !l_r) {
				if (first) {
					first = FALSE;
					top_to_bottom = t_b;
				} else {
					if ((top_to_bottom && !t_b) ||
					    (!top_to_bottom && !l_r)) {
						not_acceptable = TRUE;
						break;
					}
				}
			}
		}
	}

	if (not_acceptable) {
		GError *msg = g_error_new (go_error_invalid(), 0,
					   _("%s does not support multiple ranges"),
					   _("Sort"));
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), msg);
		g_error_free (msg);
		return;
	}

	if (cnt_singletons == 1 && cnt_non_singletons == 0) {
		Sheet *sheet = sv_sheet (sv);

		sel = g_new0 (GnmRange, 1);
		range_init_full_sheet (sel, sheet);
		sel->start.row = tmp_s.start.row;
		range_clip_to_finite (sel, sheet);
		numclause = 1;
		clause = g_new0 (GnmSortClause, 1);
		clause[0].offset = tmp_s.start.col - sel->start.col;
		clause[0].asc = descending;
		clause[0].cs = gnm_conf_get_core_sort_default_by_case ();
		clause[0].val = TRUE;
	} else if (cnt_singletons == 0) {
		sel = gnm_range_dup (&tmp_ns);
		range_clip_to_finite (sel, sv_sheet (sv));

		numclause = range_width (sel);
		clause = g_new0 (GnmSortClause, numclause);
		for (i = 0; i < numclause; i++) {
			clause[i].offset = i;
			clause[i].asc = descending;
			clause[i].cs = gnm_conf_get_core_sort_default_by_case ();
			clause[i].val = TRUE;
		}
	} else /* cnt_singletons > 0 &&  cnt_non_singletons == 1*/ {
		sel = gnm_range_dup (&tmp_ns);
		range_clip_to_finite (sel, sv_sheet (sv));
		numclause = cnt_singletons;
		clause = g_new0 (GnmSortClause, numclause);
		i = numclause - 1;
		for (l = sv->selections; l != NULL; l = l->next) {
			GnmRange const *r = l->data;
			if (!range_is_singleton (r))
				continue;
			if (i >= 0) {
				clause[i].offset = (top_to_bottom) ?
					r->start.col - sel->start.col
					: r->start.row - sel->start.row;
				clause[i].asc = descending;
				clause[i].cs = gnm_conf_get_core_sort_default_by_case ();
				clause[i].val = TRUE;
			}
			i--;
		}
	}

	data = g_new (GnmSortData, 1);
	data->sheet = sv_sheet (sv);
	data->range = sel;
	data->num_clause = numclause;
	data->clauses = clause;
	data->locale = NULL;

	data->retain_formats = gnm_conf_get_core_sort_default_retain_formats ();

	/* Hard code sorting by row.  I would prefer not to, but user testing
	 * indicates
	 * - that the button should always does the same things
	 * - that the icon matches the behavior
	 * - XL does this.
	 */

	/* Note that if the user specified rows by singleton selection we switch */
	/* to column sorting */
	data->top = top_to_bottom;

	if (sheet_range_has_heading (data->sheet, data->range, data->top, FALSE))
		data->range->start.row += 1;

	cmd_sort (WORKBOOK_CONTROL (wbcg), data);
}
static GNM_ACTION_DEF (cb_sort_ascending)  { sort_by_rows (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_sort_descending) { sort_by_rows (wbcg, TRUE); }

static void
cb_add_graph (GogGraph *graph, gpointer wbcg)
{
	GnmGraphDataClosure *data = (GnmGraphDataClosure *) g_object_get_data (G_OBJECT (graph), "data-closure");
	if (data) {
		if (data->new_sheet) {
			WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
			Sheet *sheet = wb_control_cur_sheet (wbc);
			WorkbookSheetState *old_state = workbook_sheet_state_new (wb_control_get_workbook (wbc));
			Sheet *new_sheet = workbook_sheet_add_with_type (
				wb_control_get_workbook (wbc),
				GNM_SHEET_OBJECT, -1,
				gnm_sheet_get_max_cols (sheet),
				gnm_sheet_get_max_rows (sheet));
			SheetObject *sog = sheet_object_graph_new (graph);
			print_info_set_paper_orientation (new_sheet->print_info, GTK_PAGE_ORIENTATION_LANDSCAPE);
			sheet_object_set_sheet (sog, new_sheet);
			wb_view_sheet_focus (wb_control_view (wbc), new_sheet);
			cmd_reorganize_sheets (wbc, old_state, sheet);
			g_object_unref (sog);
			return;
		}
	}
	wbcg_insert_object (WBC_GTK (wbcg), sheet_object_graph_new (graph));
}

static GNM_ACTION_DEF (cb_launch_chart_guru)
{
	GClosure *closure = g_cclosure_new (G_CALLBACK (cb_add_graph),
					    wbcg, NULL);
	sheet_object_graph_guru (wbcg, NULL, closure);
	g_closure_sink (closure);
}

static void
cb_add_component_new (GOComponent *component, gpointer wbcg)
{
	wbcg_insert_object (WBC_GTK (wbcg), sheet_object_component_new (component));
}

static void
component_changed_cb (GOComponent *component, gpointer data)
{
	cb_add_component_new (component, data);
}

static GNM_ACTION_DEF (cb_launch_go_component_new)
{
	gchar const *mime_type;
	gint result;
	GtkWidget *dialog = go_component_mime_dialog_new ();
	result = gtk_dialog_run (GTK_DIALOG (dialog));
	if (result == GTK_RESPONSE_OK) {
		mime_type = go_component_mime_dialog_get_mime_type ((GOComponentMimeDialog *) dialog);
		if (mime_type) {
			GtkWindow *win;
			GOComponent *component = go_component_new_by_mime_type (mime_type);
			if (component) {
				g_signal_connect (G_OBJECT (component), "changed", G_CALLBACK (component_changed_cb), wbcg);
				win = go_component_edit (component);
				gtk_window_set_transient_for (win, GTK_WINDOW (wbcg_toplevel (wbcg)));
				g_object_set_data_full (G_OBJECT (win), "component", component, g_object_unref);
			}
		}
	}
	gtk_widget_destroy (dialog);
}

static GNM_ACTION_DEF (cb_launch_go_component_from_file)
{
	GtkWidget *dlg = gtk_file_chooser_dialog_new (_("Choose object file"),
	                                              GTK_WINDOW (wbcg_toplevel (wbcg)),
	                                              GTK_FILE_CHOOSER_ACTION_OPEN,
	                                              GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
	                                              GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                              NULL);
	go_components_add_filter (GTK_FILE_CHOOSER (dlg));
	if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT) {
		char *uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dlg));
		GOComponent *component;
		component = go_component_new_from_uri (uri);
		g_free (uri);
		if (component) {
			wbcg_insert_object (WBC_GTK (wbcg), sheet_object_component_new (component));
			g_object_unref (component);
		}
	}
	gtk_widget_destroy (dlg);
}

static void
create_object (WBCGtk *wbcg, GType t,
	       char const *first_property_name,
	       ...)
{
	va_list	args;
	va_start (args, first_property_name);
	wbcg_insert_object (wbcg, (SheetObject *)
		g_object_new_valist (t, first_property_name, args));
	va_end (args);
}

static GNM_ACTION_DEF (cmd_create_frame)
	{ create_object (wbcg, sheet_widget_frame_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_button)
	{ create_object (wbcg, sheet_widget_button_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_radiobutton)
	{ create_object (wbcg, sheet_widget_radio_button_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_scrollbar)
	{ create_object (wbcg, sheet_widget_scrollbar_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_slider)
	{ create_object (wbcg, sheet_widget_slider_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_spinbutton)
	{ create_object (wbcg, sheet_widget_spinbutton_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_checkbox)
	{ create_object (wbcg, sheet_widget_checkbox_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_list)
	{ create_object (wbcg, sheet_widget_list_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_combo)
	{ create_object (wbcg, sheet_widget_combo_get_type(), NULL); }
static GNM_ACTION_DEF (cmd_create_line)
	{ create_object (wbcg, GNM_SO_LINE_TYPE, NULL); }
static GNM_ACTION_DEF (cmd_create_arrow) {
	GOArrow arrow;
	go_arrow_init_kite (&arrow, 8., 10., 3.);
	create_object (wbcg, GNM_SO_LINE_TYPE, "end-arrow", &arrow, NULL);
}
static GNM_ACTION_DEF (cmd_create_rectangle)
	{ create_object (wbcg, GNM_SO_FILLED_TYPE, NULL); }
static GNM_ACTION_DEF (cmd_create_ellipse)
	{ create_object (wbcg, GNM_SO_FILLED_TYPE, "is-oval", TRUE, NULL); }

/*****************************************************************************/
static void
wbcg_set_selection_halign (WBCGtk *wbcg, GnmHAlign halign)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView	*wb_view;
	GnmStyle *style;

	if (wbcg->updating_ui)
		return;

	/* This is a toggle button.  If we are already enabled
	 * then revert to general */
	wb_view = wb_control_view (wbc);
	if (gnm_style_get_align_h (wb_view->current_style) == halign)
		halign = GNM_HALIGN_GENERAL;

	style = gnm_style_new ();
	gnm_style_set_align_h (style, halign);
	cmd_selection_format (wbc, style, NULL, _("Set Horizontal Alignment"));
}
static GNM_ACTION_DEF (cb_align_left)
	{ wbcg_set_selection_halign (wbcg, GNM_HALIGN_LEFT); }
static GNM_ACTION_DEF (cb_align_right)
	{ wbcg_set_selection_halign (wbcg, GNM_HALIGN_RIGHT); }
static GNM_ACTION_DEF (cb_align_center)
	{ wbcg_set_selection_halign (wbcg, GNM_HALIGN_CENTER); }
static GNM_ACTION_DEF (cb_center_across_selection)
	{ wbcg_set_selection_halign (wbcg, GNM_HALIGN_CENTER_ACROSS_SELECTION); }

/*****************************************************************************/

static void
wbcg_set_selection_valign (WBCGtk *wbcg, GnmVAlign valign)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView	*wb_view;
	GnmStyle *style;

	if (wbcg->updating_ui)
		return;

	/* This is a toggle button.  If we are already enabled
	 * then revert to general */
	wb_view = wb_control_view (wbc);
	if (gnm_style_get_align_v (wb_view->current_style) == valign) {
		if (valign == GNM_VALIGN_BOTTOM)
			return;
		valign = GNM_VALIGN_BOTTOM;
	}

	style = gnm_style_new ();
	gnm_style_set_align_v (style, valign);
	cmd_selection_format (wbc, style, NULL, _("Set Vertical Alignment"));
}
static GNM_ACTION_DEF (cb_align_top)
	{ wbcg_set_selection_valign (wbcg, GNM_VALIGN_TOP); }
static GNM_ACTION_DEF (cb_align_vcenter)
	{ wbcg_set_selection_valign (wbcg, GNM_VALIGN_CENTER); }
static GNM_ACTION_DEF (cb_align_bottom)
	{ wbcg_set_selection_valign (wbcg, GNM_VALIGN_BOTTOM); }

/*****************************************************************************/

static GNM_ACTION_DEF (cb_merge_and_center)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GSList *range_list = selection_get_ranges (
		wb_control_cur_sheet_view (wbc), FALSE);
	cmd_merge_cells (wbc, wb_control_cur_sheet (wbc), range_list, TRUE);
	range_fragment_free (range_list);
}

static GNM_ACTION_DEF (cb_merge_cells)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GSList *range_list = selection_get_ranges (
		wb_control_cur_sheet_view (wbc), FALSE);
	cmd_merge_cells (wbc, wb_control_cur_sheet (wbc), range_list, FALSE);
	range_fragment_free (range_list);
}

static GNM_ACTION_DEF (cb_unmerge_cells)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GSList *range_list = selection_get_ranges (
		wb_control_cur_sheet_view (wbc), FALSE);
	cmd_unmerge_cells (wbc, wb_control_cur_sheet (wbc), range_list);
	range_fragment_free (range_list);
}

static GNM_ACTION_DEF (cb_view_statusbar)
{
	wbcg_toggle_visibility (wbcg, GTK_TOGGLE_ACTION (a));
}

/*****************************************************************************/

static void
toggle_font_attr (WBCGtk *wbcg, GtkToggleAction *act,
		  GnmStyleElement t, unsigned true_val, unsigned false_val)
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
			attr = pango_attr_underline_new
				(gnm_translate_underline_to_pango (val));
			break;
		case MSTYLE_FONT_STRIKETHROUGH:
			attr = pango_attr_strikethrough_new (val);
			break;
		case MSTYLE_FONT_SCRIPT:
			switch (val) {
			default:
			case GO_FONT_SCRIPT_STANDARD:
				wbcg_edit_add_markup
					(wbcg, go_pango_attr_superscript_new (FALSE));
				attr = go_pango_attr_subscript_new (FALSE);
				break;
			case GO_FONT_SCRIPT_SUPER:
				attr = go_pango_attr_superscript_new (TRUE);
				break;
			case GO_FONT_SCRIPT_SUB:
				attr = go_pango_attr_subscript_new (TRUE);
				break;
			}
			break;
		}
		wbcg_edit_add_markup (wbcg, attr);
		return;
	}

	new_style = gnm_style_new ();
	switch (t) {
	default :
	case MSTYLE_FONT_BOLD:		gnm_style_set_font_bold (new_style, val); break;
	case MSTYLE_FONT_ITALIC:	gnm_style_set_font_italic (new_style, val); break;
	case MSTYLE_FONT_UNDERLINE:	gnm_style_set_font_uline (new_style, val); break;
	case MSTYLE_FONT_STRIKETHROUGH: gnm_style_set_font_strike (new_style, val); break;
	case MSTYLE_FONT_SCRIPT:	gnm_style_set_font_script (new_style, val); break;
	}

	cmd_selection_format_toggle_font_style (wbc, new_style, t);
}

static void cb_font_bold (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_BOLD, TRUE, FALSE); }
static void cb_font_italic (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_ITALIC, TRUE, FALSE); }
static void cb_font_underline (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_UNDERLINE, UNDERLINE_SINGLE, UNDERLINE_NONE); }
static void cb_font_double_underline (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_UNDERLINE, UNDERLINE_DOUBLE, UNDERLINE_NONE); }
static void cb_font_underline_low (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_UNDERLINE, UNDERLINE_SINGLE_LOW, UNDERLINE_NONE); }
static void cb_font_double_underline_low (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_UNDERLINE, UNDERLINE_DOUBLE_LOW, UNDERLINE_NONE); }
static void cb_font_strikethrough (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_STRIKETHROUGH, TRUE, FALSE); }
static void cb_font_subscript (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_SCRIPT, GO_FONT_SCRIPT_SUB, GO_FONT_SCRIPT_STANDARD); }
static void cb_font_superscript (GtkToggleAction *act, WBCGtk *wbcg)
	{ toggle_font_attr (wbcg, act, MSTYLE_FONT_SCRIPT, GO_FONT_SCRIPT_SUPER, GO_FONT_SCRIPT_STANDARD); }

static void
apply_number_format (WBCGtk *wbcg,
		     GOFormat *format,
		     char const *descriptor)
{
	GnmStyle *mstyle = gnm_style_new ();
	gnm_style_set_format (mstyle, format);
	cmd_selection_format (WORKBOOK_CONTROL (wbcg), mstyle, NULL, descriptor);
}

static GNM_ACTION_DEF (cb_format_as_general)
{
	apply_number_format (wbcg,
			     go_format_general (),
			     _("Format as General"));
}

static GNM_ACTION_DEF (cb_format_as_number)
{
	GOFormat *fmt = go_format_new_from_XL ("0");
	apply_number_format (wbcg, fmt, _("Format as Number"));
	go_format_unref (fmt);
}

static GNM_ACTION_DEF (cb_format_as_currency)
{
	GOFormatDetails *details = go_format_details_new (GO_FORMAT_CURRENCY);
	GString *str = g_string_new (NULL);
	GOFormat *fmt;

	details->currency = go_format_locale_currency ();
	details->num_decimals = 2;
	go_format_generate_str (str, details);
	go_format_details_free (details);

	fmt = go_format_new_from_XL (str->str);
	g_string_free (str, TRUE);
	apply_number_format (wbcg, fmt, _("Format as Currency"));
	go_format_unref (fmt);
}

static GNM_ACTION_DEF (cb_format_as_accounting)
{
	apply_number_format (wbcg,
			     go_format_default_accounting (),
			     _("Format as Accounting"));
}

static GNM_ACTION_DEF (cb_format_as_percentage)
{
	GOFormat *fmt = go_format_new_from_XL ("0%");
	apply_number_format (wbcg, fmt, _("Format as Percentage"));
	go_format_unref (fmt);
}

static GNM_ACTION_DEF (cb_format_as_scientific)
{
	GOFormat *fmt = go_format_new_from_XL ("0.00E+00");
	apply_number_format (wbcg, fmt, _("Format as Percentage"));
	go_format_unref (fmt);
}

static GNM_ACTION_DEF (cb_format_as_time)
{
	apply_number_format (wbcg,
			     go_format_default_time (),
			     _("Format as Time"));
}

static GNM_ACTION_DEF (cb_format_as_date)
{
	apply_number_format (wbcg,
			     go_format_default_date (),
			     _("Format as Date"));
}

/* Adds borders to all the selected regions on the sheet.
 * FIXME: This is a little more simplistic then it should be, it always
 * removes and/or overwrites any borders. What we should do is
 * 1) When adding -> don't add a border if the border is thicker than 'THIN'
 * 2) When removing -> don't remove unless the border is 'THIN'
 */
static void
mutate_borders (WBCGtk *wbcg, gboolean add)
{
	GnmBorder *borders [GNM_STYLE_BORDER_EDGE_MAX];
	int i;

	for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; ++i)
		if (i <= GNM_STYLE_BORDER_RIGHT)
			borders[i] = gnm_style_border_fetch (
				add ? GNM_STYLE_BORDER_THIN : GNM_STYLE_BORDER_NONE,
				style_color_black (), gnm_style_border_get_orientation (i));
		else
			borders[i] = NULL;

	cmd_selection_format (WORKBOOK_CONTROL (wbcg), NULL, borders,
		add ? _("Add Borders") : _("Remove borders"));
}

static GNM_ACTION_DEF (cb_format_add_borders)	{ mutate_borders (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_format_clear_borders)	{ mutate_borders (wbcg, FALSE); }

static void
modify_format (WBCGtk *wbcg,
	       GOFormat *(*format_modify_fn) (GOFormat const *format),
	       char const *descriptor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView const *wbv;
	GOFormat *new_fmt;

	wbv = wb_control_view (wbc);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_style != NULL);

	new_fmt = (*format_modify_fn) (gnm_style_get_format (wbv->current_style));
	if (new_fmt != NULL) {
		GnmStyle *style = gnm_style_new ();
		gnm_style_set_format (style, new_fmt);
		cmd_selection_format (wbc, style, NULL, descriptor);
		go_format_unref (new_fmt);
	}
}

static GnmValue *
cb_calc_decs (GnmCellIter const *iter, gpointer user)
{
	int *pdecs = user;
	int decs = 0;
	GnmCell *cell = iter->cell;
	char *text;
	const char *p;
	GString const *dec = go_locale_get_decimal ();

	if (!cell || !cell->value || !VALUE_IS_NUMBER (cell->value))
		return NULL;

	/*
	 * If we are displaying an equation, we don't want to look into
	 * the rendered text.
	 */
	if (gnm_cell_has_expr (cell) && cell->base.sheet->display_formulas)
		return NULL;

	text = gnm_cell_get_rendered_text (cell);
	p = strstr (text, dec->str);
	if (p) {
		p += dec->len;

		while (g_ascii_isdigit (*p))
			decs++, p++;
	}

	*pdecs = MAX (*pdecs, decs);

	g_free (text);

	return NULL;
}

static void
inc_dec (WBCGtk *wbcg,
	 int dir,
	 GOFormat *(*format_modify_fn) (GOFormat const *format),
	 char const *descriptor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView *wbv = wb_control_view (wbc);
	GOFormat const *fmt = gnm_style_get_format (wbv->current_style);
	SheetView *sv;
	GOFormat *new_fmt;
	GnmStyle *style;
	GSList *l;
	int decs = -2;
	GString *new_fmt_str;

	if (!go_format_is_general (fmt)) {
		modify_format (wbcg, format_modify_fn, descriptor);
		return;
	}

	sv = wb_view_cur_sheet_view (wbv);
	if (!sv)
		return;

	for (l = sv->selections; l ; l = l->next) {
		GnmRange const *r = l->data;
		sheet_foreach_cell_in_range (sv_sheet (sv),
					     CELL_ITER_IGNORE_BLANK |
					     CELL_ITER_IGNORE_HIDDEN,
					     r->start.col, r->start.row,
					     r->end.col, r->end.row,
					     cb_calc_decs,
					     &decs);
	}

	new_fmt_str = g_string_new ("0");
	if (decs + dir > 0) {
		g_string_append_c (new_fmt_str, '.');
		go_string_append_c_n (new_fmt_str, '0', decs + dir);
	}
	new_fmt = go_format_new_from_XL (new_fmt_str->str);
	g_string_free (new_fmt_str, TRUE);

	style = gnm_style_new ();
	gnm_style_set_format (style, new_fmt);
	cmd_selection_format (wbc, style, NULL, descriptor);
	go_format_unref (new_fmt);
}

static GNM_ACTION_DEF (cb_format_inc_precision)
	{ inc_dec (wbcg, 1,
		   &go_format_inc_precision, _("Increase precision")); }
static GNM_ACTION_DEF (cb_format_dec_precision)
	{ inc_dec (wbcg, -1,
		   &go_format_dec_precision, _("Decrease precision")); }
static GNM_ACTION_DEF (cb_format_with_thousands)
	{ modify_format (wbcg, &go_format_toggle_1000sep, _("Toggle thousands separator")); }

static GNM_ACTION_DEF (cb_format_dec_indent) { workbook_cmd_dec_indent (WORKBOOK_CONTROL (wbcg)); }
static GNM_ACTION_DEF (cb_format_inc_indent) { workbook_cmd_inc_indent (WORKBOOK_CONTROL (wbcg)); }

static GNM_ACTION_DEF (cb_copydown)
{
	WorkbookControl  *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_copyrel (wbc, 0, -1, _("Copy down"));
}

static GNM_ACTION_DEF (cb_copyright)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	/* xgettext: copy from the cell to the left into current cell --
	   this has nothing whatsoever to do with copyright.  */
	cmd_copyrel (wbc, -1, 0, _("Copy right"));
}

static GNM_ACTION_DEF (cb_format_cells_auto_fit_height)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	workbook_cmd_autofit_selection
		(wbc, wb_control_cur_sheet (wbc), FALSE);
}

static GNM_ACTION_DEF (cb_format_cells_auto_fit_width)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	workbook_cmd_autofit_selection
		(wbc, wb_control_cur_sheet (wbc), TRUE);
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

static GNM_ACTION_DEF (cb_file_menu)
{
	wbc_gtk_load_templates (wbcg);
}

static GNM_ACTION_DEF (cb_insert_menu)
{
	GtkAction *action = gtk_action_group_get_action (wbcg->permanent_actions, "MenuInsertObject");
	SheetControlGUI	*scg = wbcg_cur_scg (wbcg);
	gtk_action_set_sensitive (action, go_components_get_mime_types () != NULL && scg && scg_sheet (scg)->sheet_type == GNM_SHEET_DATA);
}

/* Actions that are always sensitive */
static GtkActionEntry const permanent_actions[] = {
	{ "MenuFile",		NULL, N_("_File"), NULL, NULL, G_CALLBACK (cb_file_menu) },
		{ "MenuFileNewFromTemplate", GTK_STOCK_NEW,
		  N_("New From Template"), "" },
	{ "MenuEdit",		NULL, N_("_Edit") },
		{ "MenuEditClear",	GTK_STOCK_CLEAR, N_("C_lear") },
		{ "MenuEditDelete",	GTK_STOCK_DELETE, N_("_Delete") },
		{ "MenuEditItems",	GTK_STOCK_EDIT, N_("_Modify") },
		{ "MenuEditSheet",	NULL, N_("S_heet") },
		{ "MenuEditSelect",	NULL, N_("_Select") },
	{ "MenuView",		NULL, N_("_View") },
		{ "MenuViewWindows",		NULL, N_("_Windows") },
		{ "MenuViewToolbars",		NULL, N_("_Toolbars") },
	{ "MenuInsert",		NULL, N_("_Insert"), NULL, NULL, G_CALLBACK (cb_insert_menu)  },
		{ "MenuInsertObject",		NULL, N_("_Object") },
		{ "MenuInsertSpecial",		NULL, N_("S_pecial") },
		{ "MenuInsertFormulaWrap", "Gnumeric_FormulaGuru",
		  N_("Func_tion Wrapper") },
	{ "MenuFormat",		NULL, N_("F_ormat") },
		{ "MenuFormatCells",		NULL, N_("_Cells") },
		{ "MenuFormatText",		NULL, N_("_Text") },
	               { "MenuFormatTextUnderline",  NULL, N_("_Underline") },
		{ "MenuFormatColumn",		NULL, N_("C_olumn") },
		{ "MenuFormatRow",		NULL, N_("_Row") },
		{ "MenuFormatSheet",		NULL, N_("_Sheet") },
	{ "MenuTools",		NULL, N_("_Tools") },
		{ "MenuToolsScenarios",	NULL,	N_("Sce_narios") },
	{ "MenuStatistics",		NULL, N_("_Statistics") },
		{ "MenuStatisticsDescriptive",	NULL, N_("_Descriptive Statistics") },
			{ "MenuToolFrequencies",	NULL,	N_("Fre_quency Tables") },
		{ "MenuStatisticsTimeSeries",	NULL, N_("De_pendent Observations") },
			{ "MenuToolForecast",	NULL,	N_("F_orecast") },
		{ "MenuStatisticsOneSample",	NULL,	N_("_One Sample Tests") },
			{ "MenuToolOneMedian",	NULL,	N_("Claims About a Me_dian") },
		{ "MenuStatisticsTwoSamples",	NULL,	N_("_Two Sample Tests") },
			{ "MenuToolTwoMedians",	NULL,	N_("Claims About Two Me_dians") },
			{ "MenuToolTTest",	NULL,	N_("Claims About Two _Means") },
		{ "MenuStatisticsMultipleSamples",	NULL,	N_("_Multiple Sample Tests") },
			{ "MenuANOVA",	NULL,	N_("_ANOVA") },
			{ "MenuContingencyTests",	NULL,	N_("Contin_gency Table") },
	{ "MenuData",		NULL, N_("_Data") },
		{ "MenuFilter",		NULL,	N_("_Filter") },
		{ "MenuEditFill",	NULL, N_("F_ill") },
	                { "MenuRandomGenerator",	NULL, N_("_Random Generators") },
		{ "MenuOutline",	NULL,	N_("_Group and Outline") },
		{ "MenuExternalData",	NULL,	N_("Import _Data") },
		{ "MenuExportData",	NULL,	N_("E_xport Data") },
		{ "MenuSlicer",		NULL,	N_("Data S_licer") },
	{ "MenuHelp",	NULL,	N_("_Help") },

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
	{ "FilePrintArea",      NULL, N_("Print Area & Breaks")},
	{ "FilePageSetup", GTK_STOCK_PAGE_SETUP, N_("Page Set_up..."),
		NULL, N_("Setup the page settings for your current printer"),
		G_CALLBACK (cb_file_page_setup) },
	{ "FilePrintPreview", GTK_STOCK_PRINT_PREVIEW, NULL,
		NULL, N_("Print preview"),
		G_CALLBACK (cb_file_print_preview) },
	{ "FilePrint", GTK_STOCK_PRINT, NULL,
		"<control>p", N_("Print the current file"),
		G_CALLBACK (cb_file_print) },

	{ "FileHistoryFull", NULL, N_("Full _History..."),
		NULL, N_("Access previously used file"),
		G_CALLBACK (cb_file_history_full) },
	{ "FileClose", GTK_STOCK_CLOSE, NULL,
		NULL, N_("Close the current file"),
		G_CALLBACK (cb_file_close) },
	{ "FileQuit", GTK_STOCK_QUIT, NULL,
		NULL, N_("Quit the application"),
		G_CALLBACK (cb_file_quit) },

	{ "EditCopy", GTK_STOCK_COPY, NULL,
		NULL, N_("Copy the selection"),
		G_CALLBACK (cb_edit_copy) },

	{ "InsertNames", GTK_STOCK_PASTE, N_("_Name..."),
	        "F3", N_("Insert a defined name"),
	        G_CALLBACK (cb_paste_names) },

	{ "HelpDocs", GTK_STOCK_HELP, N_("_Contents"),
		"F1", N_("Open a viewer for Gnumeric's documentation"),
		G_CALLBACK (cb_help_docs) },
	{ "HelpFunctions", "Gnumeric_FormulaGuru", N_("_Functions"),
		NULL, N_("Functions help"),
		G_CALLBACK (cb_help_function) },
	{ "HelpWeb", NULL, N_("Gnumeric on the _Web"),
		NULL, N_("Browse to Gnumeric's website"),
		G_CALLBACK (cb_help_web) },
	{ "HelpIRC", NULL, N_("_Live Assistance"),
		NULL, N_("See if anyone is available to answer questions"),
		G_CALLBACK (cb_help_irc) },
	{ "HelpBug", NULL, N_("Report a _Problem"),
		NULL, N_("Report problem"),
		G_CALLBACK (cb_help_bug) },
	{ "HelpAbout", GTK_STOCK_ABOUT, N_("_About"),
		NULL, N_("About this application"),
		G_CALLBACK (cb_help_about) },
};

/* actions that are sensitive only in data sheets */
static GtkActionEntry const data_only_actions[] = {
	{ "EditCut", GTK_STOCK_CUT, NULL,
		NULL, N_("Cut the selection"),
		G_CALLBACK (cb_edit_cut) },
	{ "EditPaste", GTK_STOCK_PASTE, NULL,
		NULL, N_("Paste the clipboard"),
		G_CALLBACK (cb_edit_paste) },
};

static GtkActionEntry const semi_permanent_actions[] = {
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
		G_CALLBACK (wbcg_clone_sheet) },
	{ "SheetRemove", NULL, N_("_Remove"),
		NULL, N_("Irrevocably remove an entire sheet"),
		G_CALLBACK (cb_sheet_remove) },
	{ "SheetChangeName", NULL, N_("Re_name..."),
		NULL, N_("Rename the current sheet"),
		G_CALLBACK (cb_sheet_name) },
	{ "SheetResize", NULL, N_("Resize..."),
		NULL, N_("Change the size of the current sheet"),
		G_CALLBACK (cb_sheet_resize) },

/* View */
	{ "ViewNew", GTK_STOCK_NEW, N_("_New View..."),
		NULL, N_("Create a new view of the workbook"),
		G_CALLBACK (cb_view_new) },

/* Format */
	{ "FormatWorkbook", GTK_STOCK_PROPERTIES, N_("View _Properties..."),
		NULL, N_("Modify the view properties"),
		G_CALLBACK (cb_workbook_attr) },
};

#define FULLSCREEN_ACCEL "F11"
#define ZOOM_IN_ACCEL NULL
#define ZOOM_OUT_ACCEL NULL

static GtkActionEntry const actions[] = {
/* File */
	{ "FileMetaData", GTK_STOCK_PROPERTIES, N_("Document Proper_ties..."),
		NULL, N_("Edit document properties"),
		G_CALLBACK (cb_doc_meta_data) },

/* File->PrintArea */
        { "FilePrintAreaSet", NULL, N_("Set Print Area"),
                NULL, N_("Use the current selection as print area"),
                G_CALLBACK (cb_file_print_area_set)},
        { "FilePrintAreaClear", NULL, N_("Clear Print Area"),
                NULL, N_("Undefine the print area"),
                G_CALLBACK (cb_file_print_area_clear)},
        { "FilePrintAreaShow", NULL, N_("Show Print Area"),
                NULL, N_("Select the print area"),
                G_CALLBACK (cb_file_print_area_show)},
        { "FilePrintAreaToggleColPageBreak", NULL, N_("Set Column Page Break"),
                NULL, N_("Split the page to the left of this column"),
                G_CALLBACK (cb_file_print_area_toggle_col)},
        { "FilePrintAreaToggleRowPageBreak", NULL, N_("Set Row Page Break"),
                NULL, N_("Split the page above this row"),
                G_CALLBACK (cb_file_print_area_toggle_row)},
        { "FilePrintAreaClearAllPageBreak", NULL, N_("Clear All Page Breaks"),
                NULL, N_("Remove all manual pagebreaks from this sheet"),
                G_CALLBACK (cb_file_print_area_clear_pagebreaks)},

/* Edit -> Clear */
	{ "EditClearAll", GTK_STOCK_CLEAR, N_("_All"),
		NULL, N_("Clear the selected cells' formats, comments, and contents"),
		G_CALLBACK (cb_edit_clear_all) },
	{ "EditClearFormats", NULL, N_("_Formats & Hyperlinks"),
		NULL, N_("Clear the selected cells' formats and hyperlinks"),
		G_CALLBACK (cb_edit_clear_formats) },
	{ "EditClearComments", "Gnumeric_CommentDelete", N_("Co_mments"),
		NULL, N_("Delete the selected cells' comments"),
		G_CALLBACK (cb_edit_clear_comments) },
	{ "EditClearContent", GTK_STOCK_CLEAR, N_("_Contents"),
		NULL, N_("Clear the selected cells' contents"),
		G_CALLBACK (cb_edit_clear_content) },
	{ "EditClearAllFiltered", GTK_STOCK_CLEAR, N_("A_ll Filtered Rows"),
		NULL, N_("Clear the selected cells' formats, comments, and contents in the filtered rows"),
		G_CALLBACK (cb_edit_clear_all_filtered) },
	{ "EditClearFormatsFiltered", NULL, N_("F_ormats & Hyperlinks in Filtered Rows"),
		NULL, N_("Clear the selected cells' formats and hyperlinks in the filtered rows"),
		G_CALLBACK (cb_edit_clear_formats_filtered) },
	{ "EditClearCommentsFiltered", "Gnumeric_CommentDelete", N_("Comme_nts in Filtered Rows"),
		NULL, N_("Delete the selected cells' comments in the filtered rows"),
		G_CALLBACK (cb_edit_clear_comments_filtered) },
	{ "EditClearContentFiltered", GTK_STOCK_CLEAR, N_("Content_s of Filtered Rows"),
		NULL, N_("Clear the selected cells' contents in the filtered rows"),
		G_CALLBACK (cb_edit_clear_content_filtered) },

/* Edit -> Delete */
	/*Translators: Delete "Rows"*/
	{ "EditDeleteRows", "Gnumeric_RowDelete", N_("_Rows"),
		NULL, N_("Delete the row(s) containing the selected cells"),
		G_CALLBACK (cb_edit_delete_rows) },
	/*Translators: Delete "Columns"*/
	{ "EditDeleteColumns", "Gnumeric_ColumnDelete", N_("_Columns"),
		NULL, N_("Delete the column(s) containing the selected cells"),
		G_CALLBACK (cb_edit_delete_columns) },
	{ "EditDeleteCells", NULL, N_("C_ells..."),
		  "<control>minus", N_("Delete the selected cells, shifting others into their place"),
		  G_CALLBACK (cb_edit_delete_cells) },
	{ "EditClearHyperlinks", "Gnumeric_Link_Delete", N_("_Hyperlinks"),
		NULL, N_("Delete the selected cells' hyperlinks"),
		G_CALLBACK (cb_edit_delete_links) },
	/* A duplicate that should not go into the menus, used only for the accelerator */
	{ "EditDeleteCellsXL", NULL, N_("C_ells..."),
		  "<control>KP_Subtract", N_("Delete the selected cells, shifting others into their place"),
		  G_CALLBACK (cb_edit_delete_cells) },

/* Edit -> Select */

	/* Note : The accelerators involving space are just for display
	 *	purposes.  We actually handle this in
	 *		gnm-pane.c:gnm_pane_key_mode_sheet
	 *	with the rest of the key movement and rangeselection.
	 *	Otherwise input methods would steal them */
	{ "EditSelectAll", NULL, N_("_All"),
		"<control><shift>space", N_("Select all cells in the spreadsheet"),
		G_CALLBACK (cb_edit_select_all) },
	{ "EditSelectColumn", NULL, N_("_Column"),
		"<control>space", N_("Select an entire column"),
		G_CALLBACK (cb_edit_select_col) },
	{ "EditSelectRow", NULL, N_("_Row"),
		"<shift>space", N_("Select an entire row"),
		G_CALLBACK (cb_edit_select_row) },

	{ "EditSelectArray", NULL, N_("Arra_y"),
		"<control>slash", N_("Select an array of cells"),
		G_CALLBACK (cb_edit_select_array) },
	{ "EditSelectDepends", NULL, N_("_Depends"),
		"<control>bracketright", N_("Select all the cells that depend on the current edit cell"),
		G_CALLBACK (cb_edit_select_depends) },
	{ "EditSelectInputs", NULL, N_("_Inputs"),
		"<control>bracketleft", N_("Select all the cells are used by the current edit cell"),
		G_CALLBACK (cb_edit_select_inputs) },

	{ "EditSelectObject", NULL, N_("Next _Object"),
	  "<control>Tab", N_("Select the next sheet object"),
		G_CALLBACK (cb_edit_select_object) },

	{ "EditGotoTop", GTK_STOCK_GOTO_TOP, N_("Go to Top"),
		NULL, N_("Go to the top of the data"),
		G_CALLBACK (cb_edit_goto_top) },
	{ "EditGotoBottom", GTK_STOCK_GOTO_BOTTOM, N_("Go to Bottom"),
		NULL, N_("Go to the bottom of the data"),
		G_CALLBACK (cb_edit_goto_bottom) },
	{ "EditGotoFirst", GTK_STOCK_GOTO_FIRST, N_("Go to First"),
		NULL, N_("Go to the first data cell"),
		G_CALLBACK (cb_edit_goto_first) },
	{ "EditGotoLast", GTK_STOCK_GOTO_LAST, N_("Go to Last"),
		NULL, N_("Go to the last data cell"),
		G_CALLBACK (cb_edit_goto_last) },
	{ "EditGoto", GTK_STOCK_JUMP_TO, N_("_Go to Cell..."),
		"<control>G", N_("Jump to a specified cell"),
		G_CALLBACK (cb_edit_goto) },
	/* Tis is a navigational aid that is not supposed to appear */
	/* in the menu */
	{ "EditGotoCellIndicator", NULL, "Go to Current Cell Indicator",
		"<shift><control>G", "Go to Current Cell Indicator",
		G_CALLBACK (cb_edit_goto_cell_indicator) },

/* Edit */
	{ "Repeat", NULL, N_("Repeat"),
		"F4", N_("Repeat the previous action"),
		G_CALLBACK (cb_repeat) },
	{ "EditPasteSpecial", NULL, N_("P_aste Special..."),
		"<shift><control>V", N_("Paste with optional filters and transformations"),
		G_CALLBACK (cb_edit_paste_special) },

	{ "EditComment", "Gnumeric_CommentEdit", N_("Co_mment..."),
		NULL, N_("Edit the selected cell's comment"),
		G_CALLBACK (cb_insert_comment) },
	{ "EditHyperlink", "Gnumeric_Link_Edit", N_("Hyper_link..."),
		"<control>K", N_("Edit the selected cell's hyperlink"),
		G_CALLBACK (cb_insert_hyperlink) },
#if 0
	{ "EditGenerateName", NULL,  N_("_Auto generate names..."),
		NULL, N_("Use the current selection to create names"),
		G_CALLBACK (cb_auto_generate__named_expr) },
#endif

	{ "EditFind", GTK_STOCK_FIND, N_("S_earch..."),
		"<control>F", N_("Search for something"),
		G_CALLBACK (cb_edit_search) },
	{ "EditReplace", GTK_STOCK_FIND_AND_REPLACE, N_("Search _& Replace..."),
		"<control>H", N_("Search for something and replace it with something else"),
		G_CALLBACK (cb_edit_search_replace) },

	{ "EditRecalc", NULL, N_("Recalculate"),
		"F9", N_("Recalculate the spreadsheet"),
		G_CALLBACK (cb_edit_recalc) },

	{ "EditPreferences", GTK_STOCK_PREFERENCES, N_("Preferences..."),
		NULL, N_("Change Gnumeric Preferences"),
		G_CALLBACK (cb_file_preferences) },

/* View */
	{ "ViewFreezeThawPanes", NULL, N_("_Freeze Panes"),
		NULL, N_("Freeze the top left of the sheet"),
		G_CALLBACK (cb_view_freeze_panes) },
	{ "ViewZoom", GTK_STOCK_ZOOM_FIT, N_("_Zoom..."),
		NULL, N_("Zoom the spreadsheet in or out"),
		G_CALLBACK (cb_view_zoom) },
	{ "ViewZoomIn", GTK_STOCK_ZOOM_IN, N_("Zoom _In"),
		ZOOM_IN_ACCEL, N_("Increase the zoom to make things larger"),
		G_CALLBACK (cb_view_zoom_in) },
	{ "ViewZoomOut", GTK_STOCK_ZOOM_OUT, N_("Zoom _Out"),
		ZOOM_OUT_ACCEL, N_("Decrease the zoom to make things smaller"),
		G_CALLBACK (cb_view_zoom_out) },

/* Insert */
	{ "InsertCells", NULL, N_("C_ells..."),
		"<control>plus", N_("Insert new cells"),
		G_CALLBACK (cb_insert_cells) },
	/* A duplicate that should not go into the menus, used only for the accelerator */
	{ "InsertCellsXL", NULL, N_("C_ells..."),
		"<control>KP_Add", N_("Insert new cells"),
		G_CALLBACK (cb_insert_cells) },
	/*Translators: Insert "Columns"*/
	{ "InsertColumns", "Gnumeric_ColumnAdd", N_("_Columns"),
		NULL, N_("Insert new columns"),
		G_CALLBACK (cb_insert_cols) },
	/*Translators: Insert "Rows"*/
	{ "InsertRows", "Gnumeric_RowAdd", N_("_Rows"),
		NULL, N_("Insert new rows"),
		G_CALLBACK (cb_insert_rows) },

	{ "ChartGuru", "Gnumeric_GraphGuru", N_("C_hart..."),
		NULL, N_("Insert a Chart"),
		G_CALLBACK (cb_launch_chart_guru) },
	{ "NewGOComponent", "New Goffice_Component", N_("_New..."),
		NULL, N_("Insert a new Goffice component object"),
		G_CALLBACK (cb_launch_go_component_new) },
	{ "GOComponentFromFile", "New Goffice_Component from a file", N_("_From File..."),
		NULL, N_("Insert a new Goffice component object from a file"),
		G_CALLBACK (cb_launch_go_component_from_file) },
	{ "InsertImage", "Gnumeric_InsertImage", N_("_Image..."),
		NULL, N_("Insert an image"),
		G_CALLBACK (cb_insert_image) },

	{ "InsertComment", "Gnumeric_CommentAdd", N_("Co_mment..."),
		NULL, N_("Insert a comment"),
		G_CALLBACK (cb_insert_comment) },
	{ "InsertHyperlink", "Gnumeric_Link_Add", N_("Hyper_link..."),
		"<control>K", N_("Insert a Hyperlink"),
		G_CALLBACK (cb_insert_hyperlink) },
	{ "InsertSortDecreasing", GTK_STOCK_SORT_DESCENDING, N_("Sort (_Descending)"),
		NULL, N_("Wrap with SORT (descending)"),
		G_CALLBACK (cb_insert_sort_descending) },
	{ "InsertSortIncreasing", GTK_STOCK_SORT_ASCENDING, N_("Sort (_Ascending)"),
		NULL, N_("Wrap with SORT (ascending)"),
		G_CALLBACK (cb_insert_sort_ascending) },

/* Insert -> Special */
	{ "InsertCurrentDate", NULL, N_("Current _Date"),
		"<control>semicolon", N_("Insert the current date into the selected cell(s)"),
		G_CALLBACK (cb_insert_current_date) },

	{ "InsertCurrentTime", NULL, N_("Current _Time"),
		"<control>colon", N_("Insert the current time into the selected cell(s)"),
		G_CALLBACK (cb_insert_current_time) },

	{ "InsertCurrentDateTime", NULL, N_("Current D_ate and Time"),
		"<control>period", N_("Insert the current date and time into the selected cell(s)"),
		G_CALLBACK (cb_insert_current_date_time) },

/* Insert -> Name */
	{ "EditNames", NULL, N_("_Names..."),
		"<control>F3", N_("Edit defined names for expressions"),
		G_CALLBACK (cb_define_name) },

/* Format */
	{ "FormatAuto", NULL, N_("_Autoformat..."),
		NULL, N_("Format a region of cells according to a pre-defined template"),
		G_CALLBACK (cb_autoformat) },
	{ "SheetDirection", GTK_STOCK_GO_FORWARD, N_("Direction"),
		NULL, N_("Toggle sheet direction, left-to-right vs right-to-left"),
		G_CALLBACK (cb_direction) },

/* Format -> Cells */
	{ "FormatCells", NULL, N_("_Format..."),
	  "<control>1", N_("Modify the formatting of the selected cells"),
	  G_CALLBACK (cb_format_cells) },
	{ "FormatCellsCond", NULL, N_("_Conditional Formatting..."), NULL,
	  N_("Modify the conditional formatting of the selected cells"),
	  G_CALLBACK (cb_format_cells_cond) },
	{ "FormatCellsFitHeight", "Gnumeric_RowSize", N_("Auto Fit _Height"), NULL,
	  N_("Ensure rows are just tall enough to display content of selection"),
	  G_CALLBACK (cb_format_cells_auto_fit_height) },
	{ "FormatCellsFitWidth", "Gnumeric_ColumnSize", N_("Auto Fit _Width"), NULL,
	  N_("Ensure columns are just wide enough to display content of selection"),
	  G_CALLBACK (cb_format_cells_auto_fit_width) },


/* Format -> Col */
	{ "ColumnSize", "Gnumeric_ColumnSize", N_("_Width..."),
		NULL, N_("Change width of the selected columns"),
		G_CALLBACK (cb_set_column_width) },
	{ "ColumnAutoSize", "Gnumeric_ColumnSize", N_("_Auto Fit Width"),
		NULL, N_("Ensure columns are just wide enough to display their content"),
		G_CALLBACK (cb_format_column_auto_fit) },
	{ "ColumnHide", "Gnumeric_ColumnHide", N_("_Hide"),
		"<control>0", N_("Hide the selected columns"),
		G_CALLBACK (cb_format_column_hide) },
	{ "ColumnUnhide", "Gnumeric_ColumnUnhide", N_("_Unhide"),
		"<control>parenright", N_("Make any hidden columns in the selection visible"),
		G_CALLBACK (cb_format_column_unhide) },
	{ "ColumnDefaultSize", "Gnumeric_ColumnSize", N_("_Standard Width"),
		NULL, N_("Change the default column width"),
		G_CALLBACK (cb_format_column_std_width) },

/* Format -> Row */
	{ "RowSize", "Gnumeric_RowSize", N_("H_eight..."),
		NULL, N_("Change height of the selected rows"),
		G_CALLBACK (cb_set_row_height) },
	{ "RowAutoSize", "Gnumeric_RowSize", N_("_Auto Fit Height"),
		NULL, N_("Ensure rows are just tall enough to display their content"),
		G_CALLBACK (cb_format_row_auto_fit) },
	{ "RowHide", "Gnumeric_RowHide", N_("_Hide"),
		"<control>9", N_("Hide the selected rows"),
		G_CALLBACK (cb_format_row_hide) },
	{ "RowUnhide", "Gnumeric_RowUnhide", N_("_Unhide"),
		"<control>parenleft", N_("Make any hidden rows in the selection visible"),
		G_CALLBACK (cb_format_row_unhide) },
	{ "RowDefaultSize", "Gnumeric_RowSize", N_("_Standard Height"),
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

/* Statistics */

	{ "ToolsSampling", NULL, N_("_Sampling..."),
		NULL, N_("Periodic and random samples"),
		G_CALLBACK (cb_tools_sampling) },

/* Statistics -> Descriptive*/

	{ "ToolsCorrelation", NULL, N_("_Correlation..."),
		NULL, N_("Pearson Correlation"),
		G_CALLBACK (cb_tools_correlation) },
	{ "ToolsCovariance", NULL, N_("Co_variance..."),
		NULL, N_("Covariance"),
		G_CALLBACK (cb_tools_covariance) },
	{ "ToolsDescStatistics", NULL, N_("_Descriptive Statistics..."),
		NULL, N_("Various summary statistics"),
		G_CALLBACK (cb_tools_desc_statistics) },

/* Statistics -> Descriptive -> Frequencies */

	{ "ToolsFrequency", NULL, N_("Fre_quency Tables..."),
		NULL, N_("Frequency tables for non-numeric data"),
		G_CALLBACK (cb_tools_frequency) },
	{ "ToolsHistogram", NULL, N_("_Histogram..."),
		NULL, N_("Various frequency tables for numeric data"),
		G_CALLBACK (cb_tools_histogram) },
	{ "ToolsRanking", NULL, N_("Ranks And _Percentiles..."),
		NULL, N_("Ranks, placements and percentiles"),
		G_CALLBACK (cb_tools_ranking) },

/* Statistics -> DependentObservations */

	{ "ToolsFourier", NULL, N_("_Fourier Analysis..."),
		NULL, N_("Fourier Analysis"),
		G_CALLBACK (cb_tools_fourier) },
	{ "ToolsPrincipalComponents", NULL,
	        N_("Principal Components Analysis..."),
		NULL, N_("Principal Components Analysis"),
		G_CALLBACK (cb_tools_principal_components) },
/* Statistics -> DependentObservations -> Forecast*/

	{ "ToolsExpSmoothing", NULL, N_("_Exponential Smoothing..."),
		NULL, N_("Exponential smoothing..."),
		G_CALLBACK (cb_tools_exp_smoothing) },
	{ "ToolsAverage", NULL, N_("_Moving Average..."),
		NULL, N_("Moving average..."),
		G_CALLBACK (cb_tools_average) },
	{ "ToolsRegression", NULL, N_("_Regression..."),
		NULL, N_("Regression Analysis"),
		G_CALLBACK (cb_tools_regression) },
	{ "ToolsKaplanMeier", NULL, N_("_Kaplan-Meier Estimates..."),
		NULL, N_("Creation of Kaplan-Meier Survival Curves"),
		G_CALLBACK (cb_tools_kaplan_meier) },

/* Statistics -> OneSample */

	{ "ToolsNormalityTests", NULL, N_("_Normality Tests..."),
		NULL, N_("Testing a sample for normality"),
		G_CALLBACK (cb_tools_normality_tests) },
	{ "ToolsOneMeanTest", NULL, N_("Claims About a _Mean..."),
	        NULL, N_("Testing the value of a mean"),
	        G_CALLBACK (cb_tools_one_mean_test) },

/* Statistics -> OneSample -> OneMedian*/

	{ "ToolsOneMedianSignTest", NULL, N_("_Sign Test..."),
		NULL, N_("Testing the value of a median"),
		G_CALLBACK (cb_tools_sign_test_one_median) },
	{ "ToolsOneMedianWilcoxonSignedRank", NULL, N_("_Wilcoxon Signed Rank Test..."),
		NULL, N_("Testing the value of a median"),
		G_CALLBACK (cb_tools_wilcoxon_signed_rank_one_median) },

/* Statistics -> TwoSamples */

	{ "ToolsFTest", NULL, N_("Claims About Two _Variances"),
		NULL, N_("Comparing two population variances"),
		G_CALLBACK (cb_tools_ftest) },

/* Statistics -> TwoSamples -> Two Means*/

	{ "ToolTTestPaired", NULL, N_("_Paired Samples..."),
		NULL, N_("Comparing two population means for two paired samples"),
		G_CALLBACK (cb_tools_ttest_paired) },

	{ "ToolTTestEqualVar", NULL, N_("Unpaired Samples, _Equal Variances..."),
		NULL, N_("Comparing two population means for two unpaired samples from populations with equal variances"),
		G_CALLBACK (cb_tools_ttest_equal_var) },

	{ "ToolTTestUnequalVar", NULL, N_("Unpaired Samples, _Unequal Variances..."),
		NULL, N_("Comparing two population means for two unpaired samples from populations with unequal variances"),
		G_CALLBACK (cb_tools_ttest_unequal_var) },

	{ "ToolZTest", NULL, N_("Unpaired Samples, _Known Variances..."),
		NULL, N_("Comparing two population means from populations with known variances"),
		G_CALLBACK (cb_tools_ztest) },

/* Statistics -> TwoSamples -> Two Medians*/

	{ "ToolsTwoMedianSignTest", NULL, N_("_Sign Test..."),
		NULL, N_("Comparing the values of two medians of paired observations"),
		G_CALLBACK (cb_tools_sign_test_two_medians) },
	{ "ToolsTwoMedianWilcoxonSignedRank", NULL, N_("_Wilcoxon Signed Rank Test..."),
		NULL, N_("Comparing the values of two medians of paired observations"),
		G_CALLBACK (cb_tools_wilcoxon_signed_rank_two_medians) },
	{ "ToolsTwoMedianWilcoxonMannWhitney", NULL, N_("Wilcoxon-_Mann-Whitney Test..."),
		NULL, N_("Comparing the values of two medians of unpaired observations"),
		G_CALLBACK (cb_tools_wilcoxon_mann_whitney) },

/* Statistics -> MultipleSamples */

/* Statistics -> MultipleSamples -> ANOVA*/

	{ "ToolsANOVAoneFactor", NULL, N_("_One Factor..."),
		NULL, N_("One Factor Analysis of Variance..."),
		G_CALLBACK (cb_tools_anova_one_factor) },
	{ "ToolsANOVAtwoFactor", NULL, N_("_Two Factor..."),
		NULL, N_("Two Factor Analysis of Variance..."),
		G_CALLBACK (cb_tools_anova_two_factor) },

/* Statistics -> MultipleSamples -> ContingencyTable*/

	{ "ToolsHomogeneity", NULL, N_("Test of _Homogeneity..."),
		NULL, N_("Chi Squared Test of Homogeneity..."),
		G_CALLBACK (cb_tools_chi_square_homogeneity) },
	{ "ToolsIndependence", NULL, N_("Test of _Independence..."),
		NULL, N_("Chi Squared Test of Independence..."),
		G_CALLBACK (cb_tools_chi_square_independence) },

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
	{ "DataTextToColumns", NULL, N_("T_ext to Columns..."),
		NULL, N_("Parse the text in the selection into data"),
		G_CALLBACK (cb_data_text_to_columns) },
	{ "DataConsolidate", NULL, N_("_Consolidate..."),
		NULL, N_("Consolidate regions using a function"),
		G_CALLBACK (cb_data_consolidate) },
	{ "DataTable", NULL, N_("_Table..."),
		NULL, N_("Create a Data Table to evaluate a function with multiple inputs"),
		G_CALLBACK (cb_data_table) },
	{ "DataExport", NULL, N_("E_xport into Other Format..."),
		NULL, N_("Export the current workbook or sheet"),
		G_CALLBACK (cb_data_export) },
	{ "DataExportText", NULL, N_("Export as _Text File..."),
		NULL, N_("Export the current sheet as a text file"),
		G_CALLBACK (cb_data_export_text) },
	{ "DataExportCSV", NULL, N_("Export as _CSV File..."),
		NULL, N_("Export the current sheet as a csv file"),
		G_CALLBACK (cb_data_export_csv) },
	{ "DataExportRepeat", NULL, N_("Repeat Export"),
		"<control>E", N_("Repeat the last data export"),
		G_CALLBACK (cb_data_export_repeat) },

/* Data -> Fill */
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
	{ "RandomGeneratorUncorrelated", NULL, N_("_Uncorrelated..."),
		NULL, N_("Generate random numbers of a selection of distributions"),
		G_CALLBACK (cb_tools_random_generator_uncorrelated) },
	{ "RandomGeneratorCorrelated", NULL, N_("_Correlated..."),
		NULL, N_("Generate variates for correlated normal distributed random variables"),
		G_CALLBACK (cb_tools_random_generator_correlated) },
	{ "CopyDown", NULL, N_("Fill Downwards"), "<control>D",
	  N_("Copy the content from the top row to the cells below"), G_CALLBACK (cb_copydown) },
	{ "CopyRight", NULL, N_("Fill to Right"), "<control>R",
	  N_("Copy the content from the left column to the cells on the right"), G_CALLBACK (cb_copyright) },


/* Data -> Outline */
	{ "DataOutlineHideDetail", "Gnumeric_HideDetail", N_("_Hide Detail"),
		NULL, N_("Collapse an outline group"),
		G_CALLBACK (cb_data_hide_detail) },
	{ "DataOutlineShowDetail", "Gnumeric_ShowDetail", N_("_Show Detail"),
		NULL, N_("Uncollapse an outline group"),
		G_CALLBACK (cb_data_show_detail) },
	{ "DataOutlineGroup", "Gnumeric_Group", N_("_Group..."),
		"<shift><alt>Right", N_("Add an outline group"),
		G_CALLBACK (cb_data_group) },
	{ "DataOutlineUngroup", "Gnumeric_Ungroup", N_("_Ungroup..."),
		"<shift><alt>Left", N_("Remove an outline group"),
		G_CALLBACK (cb_data_ungroup) },

/* Data -> Filter */
	{ "DataAutoFilter", "Gnumeric_AutoFilter", N_("Add _Auto Filter"),
		NULL, N_("Add or remove a filter"),
		G_CALLBACK (cb_auto_filter) },
	{ "DataFilterShowAll", NULL, N_("_Clear Advanced Filter"),
		NULL, N_("Show all rows hidden by an advanced filter"),
		G_CALLBACK (cb_show_all) },
	{ "DataFilterAdvancedfilter", NULL, N_("Advanced _Filter..."),
		NULL, N_("Filter data with given criteria"),
		G_CALLBACK (cb_data_filter) },
/* Data -> External */
	{ "DataImportText", GTK_STOCK_DND, N_("Import _Text File..."),
		NULL, N_("Import data from a text file"),
		G_CALLBACK (cb_data_import_text) },
	{ "DataImportOther", GTK_STOCK_DND, N_("Import _Other File..."),
		NULL, N_("Import data from a file"),
		G_CALLBACK (cb_data_import_other) },

/* Data -> Data Slicer */
	/* label and tip are context dependent, see wbcg_menu_state_update */
	{ "DataSlicer", NULL, N_("Add _Data Slicer"),
		NULL, N_("Create a data slicer"),
		G_CALLBACK (cb_data_slicer_create) },
	{ "DataSlicerRefresh", NULL, N_("_Refresh"),
		NULL, N_("Regenerate a data slicer from the source data"),
		G_CALLBACK (cb_data_slicer_refresh) },
	{ "DataSlicerEdit", NULL, N_("_Edit Data Slicer..."),
		NULL, N_("Adjust a data slicer"),
		G_CALLBACK (cb_data_slicer_edit) },

/* Standard Toolbar */
	{ "AutoSum", "Gnumeric_AutoSum", N_("Sum"),
		"<alt>equal", N_("Sum into the current cell"),
		G_CALLBACK (cb_autosum) },
	{ "InsertFormula", "Gnumeric_FormulaGuru", N_("_Function..."), NULL,
		N_("Edit a function in the current cell"),
		G_CALLBACK (cb_formula_guru) },

	{ "SortAscending", GTK_STOCK_SORT_ASCENDING, N_("Sort Ascending"), NULL,
		N_("Sort the selected region in ascending order based on the first column selected"),
		G_CALLBACK (cb_sort_ascending) },
	{ "SortDescending", GTK_STOCK_SORT_DESCENDING, N_("Sort Descending"), NULL,
		N_("Sort the selected region in descending order based on the first column selected"),
		G_CALLBACK (cb_sort_descending) },

/* Object Toolbar */
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
		NULL, N_("Create a line object"),
		G_CALLBACK (cmd_create_line) },
	{ "CreateArrow", "Gnumeric_ObjectArrow", N_("Arrow"),
		NULL, N_("Create an arrow object"),
		G_CALLBACK (cmd_create_arrow) },
	{ "CreateRectangle", "Gnumeric_ObjectRectangle", N_("Rectangle"),
		NULL, N_("Create a rectangle object"),
		G_CALLBACK (cmd_create_rectangle) },
	{ "CreateEllipse", "Gnumeric_ObjectEllipse", N_("Ellipse"),
		NULL, N_("Create an ellipse object"),
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
	{ "FormatUnmergeCells", "Gnumeric_SplitCells", N_("Unmerge"),
		NULL, N_("Split merged ranges of cells"),
		G_CALLBACK (cb_unmerge_cells) },

	{ "FormatAsGeneral", NULL, N_("General"),
		"<control>asciitilde", N_("Format the selection as General"),
		G_CALLBACK (cb_format_as_general) },
	{ "FormatAsNumber", NULL, N_("Number"),
		"<control>exclam", N_("Format the selection as numbers"),
		G_CALLBACK (cb_format_as_number) },
	{ "FormatAsCurrency", NULL, N_("Currency"),
		"<control>dollar", N_("Format the selection as currency"),
		G_CALLBACK (cb_format_as_currency) },
	{ "FormatAsAccounting", "Gnumeric_FormatAsAccounting", N_("Accounting"),
		NULL, N_("Format the selection as accounting"),
		G_CALLBACK (cb_format_as_accounting) },
	{ "FormatAsPercentage", "Gnumeric_FormatAsPercentage", N_("Percentage"),
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
		"<control>ampersand", N_("Add a border around the selection"),
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

	/* Gtk marks these accelerators as invalid because they use Tab
	 * enable them manually in gnm-pane.c */
	{ "FormatDecreaseIndent", GTK_STOCK_UNINDENT, NULL,
		"<control><alt><shift>Tab", N_("Decrease the indent, and align the contents to the left"),
		G_CALLBACK (cb_format_dec_indent) },
	{ "FormatIncreaseIndent", GTK_STOCK_INDENT, NULL,
		"<control><alt>Tab", N_("Increase the indent, and align the contents to the left"),
		G_CALLBACK (cb_format_inc_indent) },
};

#define TOGGLE_HANDLER(flag,property)					\
static GNM_ACTION_DEF (cb_sheet_pref_ ## flag )				\
{									\
	g_return_if_fail (IS_WBC_GTK (wbcg));		\
									\
	if (!wbcg->updating_ui) {					\
		Sheet *sheet = wbcg_cur_sheet (wbcg);			\
		go_object_toggle (sheet, property);			\
		sheet_update (sheet);					\
	}								\
}

TOGGLE_HANDLER (display_formulas, "display-formulas")
TOGGLE_HANDLER (hide_zero, "display-zeros")
TOGGLE_HANDLER (hide_grid, "display-grid")
TOGGLE_HANDLER (hide_col_header, "display-column-header")
TOGGLE_HANDLER (hide_row_header, "display-row-header")
TOGGLE_HANDLER (display_outlines, "display-outlines")
TOGGLE_HANDLER (outline_symbols_below, "display-outlines-below")
TOGGLE_HANDLER (outline_symbols_right, "display-outlines-right")
TOGGLE_HANDLER (use_r1c1, "use-r1c1")

static GtkToggleActionEntry const toggle_actions[] = {
	{ "SheetDisplayOutlines", NULL, N_("Display _Outlines"),
		"<control>8", N_("Toggle whether or not to display outline groups"),
		G_CALLBACK (cb_sheet_pref_display_outlines) },
	{ "SheetOutlineBelow", NULL, N_("Outlines _Below"),
		NULL, N_("Toggle whether to display row outlines on top or bottom"),
		G_CALLBACK (cb_sheet_pref_outline_symbols_below) },
	{ "SheetOutlineRight", NULL, N_("Outlines _Right"),
		NULL, N_("Toggle whether to display column outlines on the left or right"),
		G_CALLBACK (cb_sheet_pref_outline_symbols_right) },
	{ "SheetDisplayFormulas", "Gnumeric_FormulaGuru",
	  N_("Display _Formul\303\246"),
	  "<control>quoteleft",
	  N_("Display the value of a formula or the formula itself"),
		G_CALLBACK (cb_sheet_pref_display_formulas) },
	{ "SheetHideZeros", NULL, N_("_Hide Zeros"),
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

	/* TODO : Make this a sub menu when we have more convention types */
	{ "SheetUseR1C1", NULL, N_("Use R1C1 N_otation "),
		NULL, N_("Display addresses as R1C1 or A1"),
		G_CALLBACK (cb_sheet_pref_use_r1c1) },

	{ "AlignLeft", GTK_STOCK_JUSTIFY_LEFT,
		N_("_Left Align"), NULL,
		N_("Align left"), G_CALLBACK (cb_align_left), FALSE },
	{ "AlignCenter", GTK_STOCK_JUSTIFY_CENTER,
		N_("_Center"), NULL,
		N_("Center horizontally"), G_CALLBACK (cb_align_center), FALSE },
	{ "AlignRight", GTK_STOCK_JUSTIFY_RIGHT,
		N_("_Right Align"), NULL,
		N_("Align right"), G_CALLBACK (cb_align_right), FALSE },
	{ "CenterAcrossSelection", "Gnumeric_CenterAcrossSelection",
		N_("_Center Across Selection"), NULL,
		N_("Center horizontally across the selection"),
		G_CALLBACK (cb_center_across_selection), FALSE },
	{ "MergeAndCenter", NULL,
		N_("_Merge and Center"), NULL,
		N_("Merge the selection into 1 cell, and center horizontaly."),
		G_CALLBACK (cb_merge_and_center), FALSE },
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
};

static GtkToggleActionEntry const semi_permanent_toggle_actions[] = {
	{ "ViewStatusbar", NULL,
		N_("View _Statusbar"), NULL,
		N_("Toggle visibility of statusbar"),
		G_CALLBACK (cb_view_statusbar), TRUE },

	{ "ViewFullScreen", GTK_STOCK_FULLSCREEN,
		N_("F_ull Screen"), FULLSCREEN_ACCEL,
		N_("Switch to or from full screen mode"),
		G_CALLBACK (cb_view_fullscreen), FALSE }
};

static GtkToggleActionEntry const font_toggle_actions[] = {
	{ "FontBold", GTK_STOCK_BOLD,
		N_("_Bold"), "<control>b",	/* ALSO "<control>2" */
		N_("Bold"), G_CALLBACK (cb_font_bold), FALSE },
	{ "FontItalic", GTK_STOCK_ITALIC,
		N_("_Italic"), "<control>i",	/* ALSO "<control>3" */
		N_("Italic"), G_CALLBACK (cb_font_italic), FALSE },
	{ "FontUnderline", GTK_STOCK_UNDERLINE,
		N_("_Underline"), "<control>u",	/* ALSO "<control>4" */
		N_("Underline"), G_CALLBACK (cb_font_underline), FALSE },
	{ "FontDoubleUnderline", "stock_text_underlined-double",	/* from icon theme */
		N_("_Double Underline"), "<control><shift>d",
		N_("Double Underline"), G_CALLBACK (cb_font_double_underline), FALSE },
	{ "FontSingleLowUnderline", NULL,	/* from icon theme */
		N_("_Single Low Underline"), "<control><shift>l",
		N_("Single Low Underline"), G_CALLBACK (cb_font_underline_low), FALSE },
	{ "FontDoubleLowUnderline", NULL,	/* from icon theme */
		N_("Double _Low Underline"), NULL,
		N_("Double Low Underline"), G_CALLBACK (cb_font_double_underline_low), FALSE },
	{ "FontStrikeThrough", GTK_STOCK_STRIKETHROUGH,
		N_("_Strikethrough"), "<control>5",
		N_("Strikethrough"), G_CALLBACK (cb_font_strikethrough), FALSE },
	{ "FontSuperscript", "Gnumeric_Superscript",
		N_("Su_perscript"), "<control>asciicircum",
		N_("Superscript"), G_CALLBACK (cb_font_superscript), FALSE },
	{ "FontSubscript", "Gnumeric_Subscript",
		N_("Subscrip_t"), "<control>underscore",
		N_("Subscript"), G_CALLBACK (cb_font_subscript), FALSE }
};

/****************************************************************************/

static GOActionComboPixmapsElement const halignment_combo_info[] = {
	{ N_("Align left"),		GTK_STOCK_JUSTIFY_LEFT,		GNM_HALIGN_LEFT },
	{ N_("Center horizontally"),	GTK_STOCK_JUSTIFY_CENTER,	GNM_HALIGN_CENTER },
	{ N_("Align right"),		GTK_STOCK_JUSTIFY_RIGHT,	GNM_HALIGN_RIGHT },
	{ N_("Fill Horizontally"),	"Gnumeric_HAlignFill",		GNM_HALIGN_FILL },
	{ N_("Justify Horizontally"),	GTK_STOCK_JUSTIFY_FILL,		GNM_HALIGN_JUSTIFY },
	{ N_("Center horizontally across the selection"),
					"Gnumeric_CenterAcrossSelection", GNM_HALIGN_CENTER_ACROSS_SELECTION },
	{ N_("Align numbers right, and text left"),
					"Gnumeric_HAlignGeneral",	GNM_HALIGN_GENERAL },
	{ NULL, NULL }
};
static GOActionComboPixmapsElement const valignment_combo_info[] = {
	{ N_("Align Top"),		"stock_alignment-top",			GNM_VALIGN_TOP },
	{ N_("Center Vertically"),	"stock_alignment-centered-vertically",	GNM_VALIGN_CENTER },
	{ N_("Align Bottom"),		"stock_alignment-bottom",		GNM_VALIGN_BOTTOM },
	{ NULL, NULL}
};

static void
cb_halignment_activated (GOActionComboPixmaps *a, WBCGtk *wbcg)
{
	wbcg_set_selection_halign (wbcg,
		go_action_combo_pixmaps_get_selected (a, NULL));
}
static void
cb_valignment_activated (GOActionComboPixmaps *a, WBCGtk *wbcg)
{
	wbcg_set_selection_valign (wbcg,
		go_action_combo_pixmaps_get_selected (a, NULL));
}

static void
wbc_gtk_init_alignments (WBCGtk *wbcg)
{
	wbcg->halignment = go_action_combo_pixmaps_new ("HAlignmentSelector",
						       halignment_combo_info, 3, 1);
	g_object_set (G_OBJECT (wbcg->halignment),
		      "label", _("Horizontal Alignment"),
		      "tooltip", _("Horizontal Alignment"),
		      NULL);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Horizontal Alignment"));
	go_combo_pixmaps_select (wbcg->halignment, 1); /* default to none */
#endif
	g_signal_connect (G_OBJECT (wbcg->halignment),
		"activate",
		G_CALLBACK (cb_halignment_activated), wbcg);
	gtk_action_group_add_action (wbcg->actions, GTK_ACTION (wbcg->halignment));

	wbcg->valignment = go_action_combo_pixmaps_new ("VAlignmentSelector",
						       valignment_combo_info, 1, 3);
	g_object_set (G_OBJECT (wbcg->valignment),
		      "label", _("Vertical Alignment"),
		      "tooltip", _("Vertical Alignment"),
		      NULL);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Horizontal Alignment"));
	go_combo_pixmaps_select (wbcg->valignment, 1); /* default to none */
#endif
	g_signal_connect (G_OBJECT (wbcg->valignment),
		"activate",
		G_CALLBACK (cb_valignment_activated), wbcg);
	gtk_action_group_add_action (wbcg->actions, GTK_ACTION (wbcg->valignment));
}

/****************************************************************************/

void
wbc_gtk_init_actions (WBCGtk *wbcg)
{
	wbcg->permanent_actions = gtk_action_group_new ("PermanentActions");
	gtk_action_group_set_translation_domain (wbcg->permanent_actions, GETTEXT_PACKAGE);
	wbcg->actions = gtk_action_group_new ("Actions");
	gtk_action_group_set_translation_domain (wbcg->actions, GETTEXT_PACKAGE);
	wbcg->font_actions = gtk_action_group_new ("FontActions");
	gtk_action_group_set_translation_domain (wbcg->font_actions, GETTEXT_PACKAGE);
	wbcg->data_only_actions = gtk_action_group_new ("DataOnlyActions");
	gtk_action_group_set_translation_domain (wbcg->data_only_actions, GETTEXT_PACKAGE);
	wbcg->semi_permanent_actions = gtk_action_group_new ("SemiPermanentActions");
	gtk_action_group_set_translation_domain (wbcg->semi_permanent_actions, GETTEXT_PACKAGE);

	gtk_action_group_add_actions (wbcg->permanent_actions,
		permanent_actions, G_N_ELEMENTS (permanent_actions), wbcg);
	gtk_action_group_add_actions (wbcg->actions,
		actions, G_N_ELEMENTS (actions), wbcg);
	gtk_action_group_add_toggle_actions (wbcg->actions,
		toggle_actions, G_N_ELEMENTS (toggle_actions), wbcg);
	gtk_action_group_add_toggle_actions (wbcg->font_actions,
		font_toggle_actions, G_N_ELEMENTS (font_toggle_actions), wbcg);
	gtk_action_group_add_actions (wbcg->data_only_actions,
		data_only_actions, G_N_ELEMENTS (data_only_actions), wbcg);
	gtk_action_group_add_actions (wbcg->semi_permanent_actions,
		semi_permanent_actions, G_N_ELEMENTS (semi_permanent_actions), wbcg);
	gtk_action_group_add_toggle_actions (wbcg->semi_permanent_actions,
		semi_permanent_toggle_actions, G_N_ELEMENTS (semi_permanent_toggle_actions), wbcg);

	wbc_gtk_init_alignments (wbcg);
}
