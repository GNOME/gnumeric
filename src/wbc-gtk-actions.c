/*
 * wbc-gtk-actions.c: Callbacks and tables for all the menus and stock toolbars
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
#include <gnumeric.h>

#include <libgnumeric.h>
#include <application.h>
#include <gnm-commands-slicer.h>
#include <commands.h>
#include <clipboard.h>
#include <selection.h>
#include <search.h>
#include <ranges.h>
#include <cell.h>
#include <stf.h>
#include <value.h>
#include <gnm-format.h>
#include <sheet.h>
#include <sort.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <sheet-utils.h>
#include <sheet-style.h>
#include <style-border.h>
#include <style-color.h>
#include <tools/filter.h>
#include <sheet-control-gui-priv.h>
#include <sheet-view.h>
#include <cmd-edit.h>
#include <workbook.h>
#include <workbook-view.h>
#include <wbc-gtk-impl.h>
#include <workbook-cmd-format.h>
#include <dialogs/dialogs.h>
#include <sheet-object-image.h>
#include <sheet-object-widget.h>
#include <gnm-so-filled.h>
#include <gnm-so-line.h>
#include <sheet-object-graph.h>
#include <sheet-object-component.h>
#include <gui-util.h>
#include <gui-file.h>
#include <gnumeric-conf.h>
#include <expr.h>
#include <print.h>
#include <print-info.h>
#include <gnm-pane-impl.h>
#include <gutils.h>
#include <widgets/gnm-fontbutton.h>

#include <goffice/goffice.h>
#include <goffice/component/goffice-component.h>

#include <glib/gi18n-lib.h>
#include <gsf/gsf-input.h>
#include <string.h>
#include <glib/gstdio.h>
#include <errno.h>

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
static GNM_ACTION_DEF (cb_file_save)	{ gui_file_save (wbcg, wb_control_view (GNM_WBC (wbcg))); }
static GNM_ACTION_DEF (cb_file_save_as)	{ gui_file_save_as
		(wbcg, wb_control_view (GNM_WBC (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_SAVE, NULL, FALSE); }

static GNM_ACTION_DEF (cb_file_sendto) {
	WorkbookControl *wbc = GNM_WBC (wbcg);
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

		workbook_view_save_to_uri (wbv, fs, uri, io_context);

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
	SheetView *sv = sheet_get_view (sheet, wb_control_view (GNM_WBC (wbcg)));
	GnmParsePos pp;
	char *message;
	char * selection;
	GnmRange const *r = selection_first_range (sv,
				       GO_CMD_CONTEXT (wbcg), _("Set Print Area"));
	if (r != NULL) {
		parse_pos_init_sheet (&pp, sheet);
		selection = undo_range_name (sheet, r);
		message = g_strdup_printf (_("Set Print Area to %s"), selection);
		cmd_define_name	(GNM_WBC (wbcg), "Print_Area", &pp,
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
	cmd_define_name	(GNM_WBC (wbcg), "Print_Area", &pp,
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
						wb_control_view (GNM_WBC (wbcg)));
		wb_control_sheet_focus (GNM_WBC (wbcg), sheet);
		sv_selection_reset (sv);
		sv_selection_add_range (sv, r);
		gnm_sheet_view_make_cell_visible (sv, r->start.col, r->start.row, FALSE);
		g_free (r);
	}
}

static GNM_ACTION_DEF (cb_file_print_area_toggle_col)
{
	cmd_page_break_toggle (GNM_WBC (wbcg),
			       wbcg_cur_sheet (wbcg),
			       TRUE);
}
static GNM_ACTION_DEF (cb_file_print_area_toggle_row)
{
	cmd_page_break_toggle (GNM_WBC (wbcg),
			       wbcg_cur_sheet (wbcg),
			       FALSE);
}

static GNM_ACTION_DEF (cb_file_print_area_clear_pagebreaks)
{
	cmd_page_breaks_clear (GNM_WBC (wbcg), wbcg_cur_sheet (wbcg));
}

static GNM_ACTION_DEF (cb_file_print)
{
	gnm_print_sheet (GNM_WBC (wbcg),
		wbcg_cur_sheet (wbcg), FALSE, GNM_PRINT_SAVED_INFO, NULL);
}

static GNM_ACTION_DEF (cb_file_print_preview)
{
	gnm_print_sheet (GNM_WBC (wbcg),
		wbcg_cur_sheet (wbcg), TRUE, GNM_PRINT_ACTIVE_SHEET, NULL);
}

static GNM_ACTION_DEF (cb_doc_meta_data)	{ dialog_doc_metadata_new (wbcg, 0); }
static GNM_ACTION_DEF (cb_file_preferences)	{ dialog_preferences (wbcg, NULL); }
static GNM_ACTION_DEF (cb_file_history_full)    { dialog_recent_used (wbcg); }
static GNM_ACTION_DEF (cb_file_close)		{ wbc_gtk_close (wbcg); }

static GNM_ACTION_DEF (cb_file_quit)
{
	/* If we are still loading initial files, short circuit */
	if (!gnm_app_initial_open_complete ()) {
		g_object_set (gnm_app_get_app (), "shutting-down", TRUE, NULL);
		return;
	}

	/* If we were editing when the quit request came in abort the edit. */
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);

	dialog_quit (wbcg);
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_edit_clear_all)
{
	cmd_selection_clear (GNM_WBC (wbcg),
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_OBJECTS | CLEAR_COMMENTS);
}

static GNM_ACTION_DEF (cb_edit_clear_formats)
	{ cmd_selection_clear (GNM_WBC (wbcg), CLEAR_FORMATS); }
static GNM_ACTION_DEF (cb_edit_clear_comments)
	{ cmd_selection_clear (GNM_WBC (wbcg), CLEAR_COMMENTS); }
static GNM_ACTION_DEF (cb_edit_clear_content)
	{ cmd_selection_clear (GNM_WBC (wbcg), CLEAR_VALUES); }
static GNM_ACTION_DEF (cb_edit_clear_all_filtered)
{
	cmd_selection_clear (GNM_WBC (wbcg),
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_OBJECTS | CLEAR_COMMENTS | CLEAR_FILTERED_ONLY);
}

static GNM_ACTION_DEF (cb_edit_clear_formats_filtered)
	{ cmd_selection_clear (GNM_WBC (wbcg), CLEAR_FORMATS | CLEAR_FILTERED_ONLY); }
static GNM_ACTION_DEF (cb_edit_clear_comments_filtered)
	{ cmd_selection_clear (GNM_WBC (wbcg), CLEAR_COMMENTS | CLEAR_FILTERED_ONLY); }
static GNM_ACTION_DEF (cb_edit_clear_content_filtered)
	{ cmd_selection_clear (GNM_WBC (wbcg), CLEAR_VALUES | CLEAR_FILTERED_ONLY); }

static GNM_ACTION_DEF (cb_edit_delete_rows)
{
	WorkbookControl *wbc   = GNM_WBC (wbcg);
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
	WorkbookControl *wbc   = GNM_WBC (wbcg);
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
		WorkbookControl *wbc   = GNM_WBC (wbcg);
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
	sv_select_cur_row (wb_control_cur_sheet_view (GNM_WBC (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_col)
{
	sv_select_cur_col (wb_control_cur_sheet_view (GNM_WBC (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_array)
{
	sv_select_cur_array (wb_control_cur_sheet_view (GNM_WBC (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_depends)
{
	sv_select_cur_depends (wb_control_cur_sheet_view (GNM_WBC (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_inputs)
{
	sv_select_cur_inputs (wb_control_cur_sheet_view (GNM_WBC (wbcg)));
}
static GNM_ACTION_DEF (cb_edit_select_object)
{
	scg_object_select_next (wbcg_cur_scg (wbcg), FALSE);
}

static GNM_ACTION_DEF (cb_edit_cut)
{
	if (!wbcg_is_editing (wbcg)) {
		SheetControlGUI *scg = wbcg_cur_scg (wbcg);
		WorkbookControl *wbc = GNM_WBC (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		if (scg != NULL && scg->selected_objects != NULL)
			gnm_app_clipboard_cut_copy_obj (wbc, TRUE, sv,
				go_hash_keys (scg->selected_objects));
		else
			gnm_sheet_view_selection_cut (sv, wbc);
	} else
		gtk_editable_cut_clipboard (GTK_EDITABLE (wbcg_get_entry (wbcg)));
}

static GNM_ACTION_DEF (cb_edit_copy)
{
	if (!wbcg_is_editing (wbcg)) {
		SheetControlGUI *scg = wbcg_cur_scg (wbcg);
		WorkbookControl *wbc = GNM_WBC (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		if (scg != NULL && scg->selected_objects != NULL)
			gnm_app_clipboard_cut_copy_obj (wbc, FALSE, sv,
				go_hash_keys (scg->selected_objects));
		else
			gnm_sheet_view_selection_copy (sv, wbc);
	} else
		gtk_editable_copy_clipboard (GTK_EDITABLE (wbcg_get_entry (wbcg)));
}

static GNM_ACTION_DEF (cb_edit_paste)
{
	if (!wbcg_is_editing (wbcg)) {
		WorkbookControl *wbc = GNM_WBC (wbcg);
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

	wbv = wb_control_view (GNM_WBC (wbcg));
	sv = sheet_get_view (sheet, wbv);
	wb_view_sheet_focus (wbv, sheet);
	sv_selection_set (sv, pos,
		pos->col, pos->row,
		pos->col, pos->row);
	gnm_sheet_view_make_cell_visible (sv, pos->col, pos->row, FALSE);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);

	sr->query_func = cb_edit_search_replace_query;
	sr->user_data = wbcg;

	return cmd_search_replace (wbc, sr);
}


static GNM_ACTION_DEF (cb_edit_search_replace) { dialog_search_replace (wbcg, cb_edit_search_replace_action); }
static GNM_ACTION_DEF (cb_edit_search) { dialog_search (wbcg); }

static GNM_ACTION_DEF (cb_edit_fill_autofill)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_top);
}
static GNM_ACTION_DEF (cb_edit_goto_bottom)
{
	wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_bottom);
}
static GNM_ACTION_DEF (cb_edit_goto_first)
{
	wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_first);
}
static GNM_ACTION_DEF (cb_edit_goto_last)
{
	wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_last);
}
static GNM_ACTION_DEF (cb_edit_goto)
{
	dialog_goto_cell (wbcg);
}
static GNM_ACTION_DEF (cb_edit_goto_cell_indicator)
{
	if (GNM_IS_WBC_GTK (wbcg))
		wbcg_focus_current_cell_indicator (WBC_GTK (wbcg));
}

static GNM_ACTION_DEF (cb_edit_recalc)
{
	/* TODO:
	 * f9  -  do any necessary calculations across all sheets
	 * shift-f9  -  do any necessary calcs on current sheet only
	 * ctrl-alt-f9 -  force a full recalc across all sheets
	 * ctrl-alt-shift-f9  -  a full-monty super recalc
	 */
	workbook_recalc_all (wb_control_get_workbook (GNM_WBC (wbcg)));
}

static GNM_ACTION_DEF (cb_repeat)	{ command_repeat (GNM_WBC (wbcg)); }

/****************************************************************************/

static GNM_ACTION_DEF (cb_direction)
{
	Sheet *sheet = wb_control_cur_sheet (GNM_WBC (wbcg));
	cmd_toggle_rtl (GNM_WBC (wbcg), sheet);
}

static GNM_ACTION_DEF (cb_view_zoom_in)
{
	Sheet *sheet = wb_control_cur_sheet (GNM_WBC (wbcg));
	int zoom = (int)(sheet->last_zoom_factor_used * 100. + .5) - 10;
	if ((zoom % 15) != 0)
		zoom = 15 * (int)(zoom/15);
	zoom += 15;
	if (zoom <= 390)
		cmd_zoom (GNM_WBC (wbcg), g_slist_append (NULL, sheet),
			  (double) (zoom + 10) / 100);
}
static GNM_ACTION_DEF (cb_view_zoom_out)
{
	Sheet *sheet = wb_control_cur_sheet (GNM_WBC (wbcg));
	int zoom = (int)(sheet->last_zoom_factor_used * 100. + .5) - 10;
	if ((zoom % 15) != 0)
		zoom = 15 * (int)(zoom/15);
	else
		zoom -= 15;
	if (0 <= zoom)
		cmd_zoom (GNM_WBC (wbcg), g_slist_append (NULL, sheet),
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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

		gnm_sheet_view_freeze_panes (sv, &frozen_tl, &unfrozen_tl);
	} else
		gnm_sheet_view_freeze_panes (sv, NULL, NULL);
}

/****************************************************************************/

static void
insert_date_time_common (WBCGtk *wbcg, gboolean do_date, gboolean do_time)
{
	if (wbcg_edit_start (wbcg, FALSE, FALSE)) {
		WorkbookControl *wbc = GNM_WBC (wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		Sheet *sheet = sv_sheet (sv);
		GnmCell const *cell = sheet_cell_fetch (sheet,
							sv->edit_pos.col,
							sv->edit_pos.row);
		GODateConventions const *date_conv = sheet_date_conv (sheet);
		GnmValue *v = value_new_float
			(go_date_timet_to_serial_raw (time (NULL), date_conv));
		char *txt;
		char *dtxt = NULL;
		char *ttxt = NULL;
		GtkEntry *entry;

		if (do_date) {
			GOFormat *fmt = gnm_format_for_date_editing (cell);
			dtxt = format_value (fmt, v, -1, date_conv);
			go_format_unref (fmt);
		}

		if (do_time) {
			GOFormat const *fmt = go_format_default_time ();
			ttxt = format_value (fmt, v, -1, date_conv);
		}

		value_release (v);

		if (do_date && do_time) {
			txt = g_strconcat (dtxt, " ", ttxt, NULL);
			g_free (dtxt);
			g_free (ttxt);
		} else if (do_date)
			txt = dtxt;
		else
			txt = ttxt;

		wb_control_edit_line_set (wbc, txt);
		g_free (txt);

		// Explicitly place cursor at end.  Otherwise the cursor
		// position depends on whether the new contents matches
		// the old which is weird.
		entry = wbcg_get_entry (wbcg);
		gtk_editable_set_position (GTK_EDITABLE (entry), -1);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	dialog_cell_comment (wbcg, sheet, &sv->edit_pos);
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_sheet_name)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
static GNM_ACTION_DEF (cb_tools_compare)	{ dialog_sheet_compare (wbcg); }
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

static GNM_ACTION_DEF (cb_auto_filter)          { cmd_autofilter_add_remove (GNM_WBC (wbcg)); }
static GNM_ACTION_DEF (cb_show_all)		{ filter_show_all (GNM_WBC (wbcg)); }
static GNM_ACTION_DEF (cb_data_filter)		{ dialog_advanced_filter (wbcg); }
static GNM_ACTION_DEF (cb_data_validate)	{ dialog_cell_format (wbcg, FD_VALIDATION,
								      (1 << FD_VALIDATION) | (1 << FD_INPUT_MSG)); }
static GNM_ACTION_DEF (cb_data_text_to_columns) { stf_text_to_columns (GNM_WBC (wbcg), GO_CMD_CONTEXT (wbcg)); }
static GNM_ACTION_DEF (cb_data_consolidate)	{ dialog_consolidate (wbcg); }
static GNM_ACTION_DEF (cb_data_table)		{ dialog_data_table (wbcg); }
static GNM_ACTION_DEF (cb_data_slicer_create)	{ dialog_data_slicer (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_data_slicer_refresh)	{ cmd_slicer_refresh (GNM_WBC (wbcg)); }
static GNM_ACTION_DEF (cb_data_slicer_edit)	{ dialog_data_slicer (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_data_export)	        { gui_file_save_as
		(wbcg, wb_control_view (GNM_WBC (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_EXPORT, NULL, FALSE); }
static GNM_ACTION_DEF (cb_data_export_text)	        { gui_file_save_as
		(wbcg, wb_control_view (GNM_WBC (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_EXPORT,
		 "Gnumeric_stf:stf_assistant",
		 FALSE); }
static GNM_ACTION_DEF (cb_data_export_csv)	        { gui_file_save_as
		(wbcg, wb_control_view (GNM_WBC (wbcg)),
		 GNM_FILE_SAVE_AS_STYLE_EXPORT,
		 "Gnumeric_stf:stf_csv",
		 FALSE); }
static GNM_ACTION_DEF (cb_data_export_repeat)	{ gui_file_export_repeat (wbcg); }

static void
hide_show_detail_real (WBCGtk *wbcg, gboolean is_cols, gboolean show)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	argv[1] = (char *)"help:gnumeric";
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
	show_url (wbcg, "https://gitlab.gnome.org/GNOME/gnumeric/issues");
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
			SheetObjectImage *soi = g_object_new (GNM_SO_IMAGE_TYPE, NULL);
			sheet_object_image_set_image (soi, "", data, len);
			wbcg_insert_object (wbcg, GNM_SO (soi));
			g_object_unref (input);
		} else
			go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);

		g_free (uri);
	}
}

static GNM_ACTION_DEF (cb_insert_hyperlink)	{ dialog_hyperlink (wbcg, GNM_SHEET_CONTROL (wbcg_cur_scg (wbcg))); }
static GNM_ACTION_DEF (cb_formula_guru)		{ dialog_formula_guru (wbcg, NULL); }
static GNM_ACTION_DEF (cb_insert_sort_ascending) { workbook_cmd_wrap_sort (GNM_WBC (wbcg), 1);}
static GNM_ACTION_DEF (cb_insert_sort_descending){ workbook_cmd_wrap_sort (GNM_WBC (wbcg), 0);}

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

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
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

	cmd_sort (GNM_WBC (wbcg), data);
}
static GNM_ACTION_DEF (cb_sort_ascending)  { sort_by_rows (wbcg, FALSE); }
static GNM_ACTION_DEF (cb_sort_descending) { sort_by_rows (wbcg, TRUE); }

static void
cb_add_graph (GogGraph *graph, gpointer wbcg)
{
	GnmGraphDataClosure *data = (GnmGraphDataClosure *) g_object_get_data (G_OBJECT (graph), "data-closure");
	if (data) {
		if (data->new_sheet) {
			WorkbookControl *wbc = GNM_WBC (wbcg);
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
	/* we nedd to ref the component otherwise it will be destroyed when
	 it's editor window will be deleted */
	g_object_ref (component);
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
	                                              GNM_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
	                                              GNM_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                              NULL);
	go_components_add_filter (GTK_FILE_CHOOSER (dlg));
	if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT) {
		char *uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dlg));
		GOComponent *component;
		component = go_component_new_from_uri (uri);
		g_free (uri);
		if (component)
			wbcg_insert_object (WBC_GTK (wbcg), sheet_object_component_new (component));
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
	GSList *range_list = selection_get_ranges (
		wb_control_cur_sheet_view (wbc), FALSE);
	cmd_merge_cells (wbc, wb_control_cur_sheet (wbc), range_list, TRUE);
	range_fragment_free (range_list);
}

static GNM_ACTION_DEF (cb_merge_cells)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	GSList *range_list = selection_get_ranges (
		wb_control_cur_sheet_view (wbc), FALSE);
	cmd_merge_cells (wbc, wb_control_cur_sheet (wbc), range_list, FALSE);
	range_fragment_free (range_list);
}

static GNM_ACTION_DEF (cb_unmerge_cells)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
		default:
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
	default:
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
	cmd_selection_format (GNM_WBC (wbcg), mstyle, NULL, descriptor);
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

	cmd_selection_format (GNM_WBC (wbcg), NULL, borders,
		add ? _("Add Borders") : _("Remove borders"));
}

static GNM_ACTION_DEF (cb_format_add_borders)	{ mutate_borders (wbcg, TRUE); }
static GNM_ACTION_DEF (cb_format_clear_borders)	{ mutate_borders (wbcg, FALSE); }

static void
modify_format (WBCGtk *wbcg,
	       GOFormat *(*format_modify_fn) (GOFormat const *format),
	       char const *descriptor)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
					     r, cb_calc_decs, &decs);
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

static GNM_ACTION_DEF (cb_format_dec_indent) { workbook_cmd_dec_indent (GNM_WBC (wbcg)); }
static GNM_ACTION_DEF (cb_format_inc_indent) { workbook_cmd_inc_indent (GNM_WBC (wbcg)); }

static GNM_ACTION_DEF (cb_copydown)
{
	WorkbookControl  *wbc = GNM_WBC (wbcg);
	cmd_copyrel (wbc, 0, -1, _("Copy down"));
}

static GNM_ACTION_DEF (cb_copyright)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	/* xgettext: copy from the cell to the left into current cell --
	   this has nothing whatsoever to do with copyright.  */
	cmd_copyrel (wbc, -1, 0, _("Copy right"));
}

static GNM_ACTION_DEF (cb_format_cells_auto_fit_height)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	workbook_cmd_autofit_selection
		(wbc, wb_control_cur_sheet (wbc), FALSE);
}

static GNM_ACTION_DEF (cb_format_cells_auto_fit_width)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	workbook_cmd_autofit_selection
		(wbc, wb_control_cur_sheet (wbc), TRUE);
}

static GNM_ACTION_DEF (cb_format_column_auto_fit)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	cmd_selection_colrow_hide (GNM_WBC (wbcg), TRUE, FALSE);
}
static GNM_ACTION_DEF (cb_format_column_unhide)
{
	cmd_selection_colrow_hide (GNM_WBC (wbcg), TRUE, TRUE);
}

static GNM_ACTION_DEF (cb_format_row_auto_fit)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	cmd_selection_colrow_hide (GNM_WBC (wbcg), FALSE, FALSE);
}
static GNM_ACTION_DEF (cb_format_row_unhide)
{
	cmd_selection_colrow_hide (GNM_WBC (wbcg), FALSE, TRUE);
}

static GNM_ACTION_DEF (cb_file_menu)
{
	wbc_gtk_load_templates (wbcg);
}

static GNM_ACTION_DEF (cb_insert_menu)
{
	GtkAction *action = wbcg_find_action (wbcg, "MenuInsertObject");
	SheetControlGUI	*scg = wbcg_cur_scg (wbcg);
	gtk_action_set_sensitive (action, go_components_get_mime_types () != NULL && scg && scg_sheet (scg)->sheet_type == GNM_SHEET_DATA);
}

#define TOGGLE_HANDLER(flag,property)					\
static GNM_ACTION_DEF (cb_sheet_pref_ ## flag )				\
{									\
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));		\
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

/* Actions that are always sensitive */
static GnmActionEntry const permanent_actions[] = {
	{ .name = "MenuFile",
	  .label = N_("_File"),
	  .callback = G_CALLBACK (cb_file_menu)
	},
	{ .name = "MenuFileNewFromTemplate",
	  .icon = "document-new",
	  .label = N_("New From Template")
	},
	{ .name = "FileNew",
	  .icon = "document-new",
	  .label = N_("_New"),
	  .accelerator = "<control>n",
	  .tooltip = N_("Create a new workbook"),
	  .callback = G_CALLBACK (cb_file_new)
	},
	{ .name = "FileOpen",
	  .icon = "document-open",
	  .label = GNM_N_STOCK_OPEN,
	  .label_context = GNM_STOCK_LABEL_CONTEXT,
	  .accelerator = "<control>o",
	  .tooltip = N_("Open a file"),
	  .callback = G_CALLBACK (cb_file_open)
	},
	{ .name = "FileSave",
	  .icon = "document-save",
	  .label = GNM_N_STOCK_SAVE,
	  .label_context = GNM_STOCK_LABEL_CONTEXT,
	  .accelerator = "<control>s",
	  .tooltip = N_("Save the current workbook"),
	  .callback = G_CALLBACK (cb_file_save)
	},
	{ .name = "FileSaveAs",
	  .icon = "document-save-as",
	  .label = GNM_N_STOCK_SAVE_AS,
	  .label_context = GNM_STOCK_LABEL_CONTEXT,
	  .accelerator = "<control><shift>s",
	  .tooltip = N_("Save the current workbook with a different name"),
	  .callback = G_CALLBACK (cb_file_save_as)
	},
	{ .name = "FileSend",
	  .icon = "gnumeric-link-email",
	  .label = N_("Sen_d To..."),
	  .tooltip = N_("Send the current file via email"),
	  .callback = G_CALLBACK (cb_file_sendto)
	},
	{ .name = "FilePageSetup",
	  .icon = "document-page-setup",
	  .label = N_("Page Set_up..."),
	  .tooltip = N_("Setup the page settings for your current printer"),
	  .callback = G_CALLBACK (cb_file_page_setup)
	},
	{ .name = "FilePrintPreview",
	  .icon = "document-print-preview",
	  .label = N_("Print Pre_view"),
	  .tooltip = N_("Print preview"),
	  .callback = G_CALLBACK (cb_file_print_preview)
	},
	{ .name = "FilePrint",
	  .icon = "document-print",
	  .label = N_("Print"),
	  .accelerator = "<control>p",
	  .tooltip = N_("Print the current file"),
	  .callback = G_CALLBACK (cb_file_print)
	},
	{ .name = "FilePrintArea",
	  .label = N_("Print Area & Breaks")
	},
	{ .name = "FileHistoryFull",
	  .label = N_("Full _History..."),
	  .tooltip = N_("Access previously used file"),
	  .callback = G_CALLBACK (cb_file_history_full)
	},
	{ .name = "FileClose",
	  .icon = "window-close",
	  .label = N_("_Close"),
	  .accelerator = "<control>w",
	  .tooltip = N_("Close the current file"),
	  .callback = G_CALLBACK (cb_file_close)
	},
	{ .name = "FileQuit",
	  .icon = "application-exit",
	  .label = N_("_Quit"),
	  .accelerator = "<control>q",
	  .tooltip = N_("Quit the application"),
	  .callback = G_CALLBACK (cb_file_quit)
	},
	/* ---------------------------------------- */
	{ .name = "MenuEdit",
	  .label = N_("_Edit")
	},
	{ .name = "MenuEditClear",
	  .icon = "edit-clear",
	  .label = N_("C_lear")
	},
	{ .name = "MenuEditDelete",
	  .icon = "edit-delete",
	  .label = N_("_Delete")
	},
	{ .name = "MenuEditItems",
	  .label = N_("_Modify")
	},
	{ .name = "MenuEditSheet",
	  .label = N_("S_heet")
	},
	{ .name = "MenuEditSelect",
	  .label = N_("_Select")
	},

	{ .name = "EditCopy",
	  .icon = "edit-copy",
	  .label = N_("_Copy"),
	  .accelerator = "<control>c",
	  .tooltip = N_("Copy the selection"),
	  .callback = G_CALLBACK (cb_edit_copy)
	},

	/* ---------------------------------------- */
	{ .name = "MenuView",
	  .label = N_("_View")
	},
	{ .name = "MenuViewWindows",
	  .label = N_("_Windows")
	},
	{ .name = "MenuViewToolbars",
	  .label = N_("_Toolbars")
	},
	/* ---------------------------------------- */
	{ .name = "MenuInsert",
	  .label = N_("_Insert"),
	  .callback = G_CALLBACK (cb_insert_menu)
	},
	{ .name = "MenuInsertObject",
	  .label = N_("_Object")
	},
	{ .name = "MenuInsertSpecial",
	  .label = N_("S_pecial")
	},
	{ .name = "MenuInsertFormulaWrap",
	  .icon = "gnumeric-formulaguru",
	  .label = N_("Func_tion Wrapper")
	},
	{ .name = "InsertNames",
	  .label = N_("_Name..."),
	  .accelerator = "F3",
	  .tooltip = N_("Insert a defined name"),
	  .callback = G_CALLBACK (cb_paste_names)
	},
	/* ---------------------------------------- */
	{ .name = "MenuFormat",
	  .label = N_("F_ormat")
	},
	{ .name = "MenuFormatCells",
	  .label = N_("_Cells")
	},
	{ .name = "MenuFormatText",
	  .label = N_("_Text")
	},
	{ .name = "MenuFormatTextUnderline",
	  .label = N_("_Underline")
	},
	{ .name = "MenuFormatColumn",
	  .label = N_("C_olumn")
	},
	{ .name = "MenuFormatRow",
	  .label = N_("_Row")
	},
	{ .name = "MenuFormatSheet",
	  .label = N_("_Sheet")
	},
	/* ---------------------------------------- */
	{ .name = "MenuTools",
	  .label = N_("_Tools")
	},
	{ .name = "MenuToolsScenarios",
	  .label = N_("Sce_narios")
	},
	/* ---------------------------------------- */
	{ .name = "MenuStatistics",
	  .label = N_("_Statistics")
	},
	{ .name = "MenuStatisticsDescriptive",
	  .label = N_("_Descriptive Statistics")
	},
	{ .name = "MenuToolFrequencies",
	  .label = N_("Fre_quency Tables")
	},
	{ .name = "MenuStatisticsTimeSeries",
	  .label = N_("De_pendent Observations")
	},
	{ .name = "MenuToolForecast",
	  .label = N_("F_orecast")
	},
	{ .name = "MenuStatisticsOneSample",
	  .label = N_("_One Sample Tests")
	},
	{ .name = "MenuToolOneMedian",
	  .label = N_("Claims About a Me_dian")
	},
	{ .name = "MenuStatisticsTwoSamples",
	  .label = N_("_Two Sample Tests")
	},
	{ .name = "MenuToolTwoMedians",
	  .label = N_("Claims About Two Me_dians")
	},
	{ .name = "MenuToolTTest",
	  .label = N_("Claims About Two _Means")
	},
	{ .name = "MenuStatisticsMultipleSamples",
	  .label = N_("_Multiple Sample Tests")
	},
	{ .name = "MenuANOVA",
	  .label = N_("_ANOVA")
	},
	{ .name = "MenuContingencyTests",
	  .label = N_("Contin_gency Table")
	},
	/* ---------------------------------------- */
	{ .name = "MenuData",
	  .label = N_("_Data")
	},
	{ .name = "MenuFilter",
	  .label = N_("_Filter")
	},
	{ .name = "MenuEditFill",
	  .label = N_("F_ill")
	},
	{ .name = "MenuRandomGenerator",
	  .label = N_("_Random Generators")
	},
	{ .name = "MenuOutline",
	  .label = N_("_Group and Outline")
	},
	{ .name = "MenuExternalData",
	  .label = N_("Import _Data")
	},
	{ .name = "MenuExportData",
	  .label = N_("E_xport Data")
	},
	{ .name = "MenuSlicer",
	  .label = N_("Data S_licer")
	},
	/* ---------------------------------------- */
	{ .name = "MenuHelp",
	  .label = N_("_Help")
	},

	{ .name = "HelpDocs",
	  .icon = "help-browser",
	  .label = N_("_Contents"),
	  .accelerator = "F1",
	  .tooltip = N_("Open a viewer for Gnumeric's documentation"),
	  .callback = G_CALLBACK (cb_help_docs)
	},
	{ .name = "HelpFunctions",
	  .icon = "gnumeric-formulaguru",
	  .label = N_("_Functions"),
	  .tooltip = N_("Functions help"),
	  .callback = G_CALLBACK (cb_help_function)
	},
	{ .name = "HelpWeb",
	  .label = N_("Gnumeric on the _Web"),
	  .tooltip = N_("Browse to Gnumeric's website"),
	  .callback = G_CALLBACK (cb_help_web)
	},
	{ .name = "HelpIRC",
	  .label = N_("_Live Assistance"),
	  .tooltip = N_("See if anyone is available to answer questions"),
	  .callback = G_CALLBACK (cb_help_irc)
	},
	{ .name = "HelpBug",
	  .label = N_("Report a _Problem"),
	  .tooltip = N_("Report problem"),
	  .callback = G_CALLBACK (cb_help_bug)
	},
	{ .name = "HelpAbout",
	  .icon = "help-about",
	  .label = N_("_About"),
	  .tooltip = N_("About this application"),
	  .callback = G_CALLBACK (cb_help_about)
	},
};

/* actions that are sensitive only in data sheets */
static GnmActionEntry const data_only_actions[] = {
	{ .name = "EditCut",
	  .icon = "edit-cut",
	  .label = N_("Cu_t"),
	  .accelerator = "<control>x",
	  .tooltip = N_("Cut the selection"),
	  .callback = G_CALLBACK (cb_edit_cut)
	},
	{ .name = "EditPaste",
	  .icon = "edit-paste",
	  .label = N_("_Paste"),
	  .accelerator = "<control>v",
	  .tooltip = N_("Paste the clipboard"),
	  .callback = G_CALLBACK (cb_edit_paste)
	},
};

#define FULLSCREEN_ACCEL "F11"

static GnmActionEntry const semi_permanent_actions[] = {
	/* Edit -> Sheet */
	{ .name = "SheetReorder",
	  .label = N_("_Manage Sheets..."),
	  .tooltip = N_("Manage the sheets in this workbook"),
	  .callback = G_CALLBACK (cb_sheet_order)
	},
	{ .name = "InsertSheet",
	  .label = N_("_Insert"),
	  .tooltip = N_("Insert a new sheet"),
	  .callback = G_CALLBACK (wbcg_insert_sheet)
	},
	/* ICK A DUPLICATE : we have no way to override a label on one proxy */
	{ .name = "SheetInsert",
	  .label = N_("_Sheet"),
	  .tooltip = N_("Insert a new sheet"),
	  .callback = G_CALLBACK (wbcg_insert_sheet)
	},
	{ .name = "InsertSheetAtEnd",
	  .label = N_("_Append"),
	  .tooltip = N_("Append a new sheet"),
	  .callback = G_CALLBACK (wbcg_append_sheet)
	},
	{ .name = "EditDuplicateSheet",
	  .label = N_("_Duplicate"),
	  .tooltip = N_("Make a copy of the current sheet"),
	  .callback = G_CALLBACK (wbcg_clone_sheet)
	},
	{ .name = "SheetRemove",
	  .label = N_("_Remove"),
	  .tooltip = N_("Irrevocably remove an entire sheet"),
	  .callback = G_CALLBACK (cb_sheet_remove)
	},
	{ .name = "SheetChangeName",
	  .label = N_("Re_name..."),
	  .tooltip = N_("Rename the current sheet"),
	  .callback = G_CALLBACK (cb_sheet_name)
	},
	{ .name = "SheetResize",
	  .label = N_("Resize..."),
	  .tooltip = N_("Change the size of the current sheet"),
	  .callback = G_CALLBACK (cb_sheet_resize)
	},

	/* View */
	{ .name = "ViewNew",
	  .icon = "document-new",
	  .label = N_("_New View..."),
	  .tooltip = N_("Create a new view of the workbook"),
	  .callback = G_CALLBACK (cb_view_new)
	},

	/* Format */
	{ .name = "FormatWorkbook",
	  .icon = "document-properties",
	  .label = N_("View _Properties..."),
	  .tooltip = N_("Modify the view properties"),
	  .callback = G_CALLBACK (cb_workbook_attr)
	},

	{ .name = "ViewStatusbar",
	  .toggle = TRUE,
	  .label = N_("View _Statusbar"),
	  .tooltip = N_("Toggle visibility of statusbar"),
	  .callback = G_CALLBACK (cb_view_statusbar),
	  .is_active = TRUE
	},

	{ .name = "ViewFullScreen",
	  .toggle = TRUE,
	  .icon = "view-fullscreen",
	  .label = N_("F_ull Screen"),
	  .accelerator = FULLSCREEN_ACCEL,
	  .tooltip = N_("Switch to or from full screen mode"),
	  .callback = G_CALLBACK (cb_view_fullscreen)
	},
};

#define ZOOM_IN_ACCEL NULL
#define ZOOM_OUT_ACCEL NULL

static GnmActionEntry const actions[] = {
/* File */
	{ .name = "FileMetaData",
	  .icon = "document-properties",
	  .label = N_("Document Proper_ties..."),
	  .tooltip = N_("Edit document properties"),
	  .callback = G_CALLBACK (cb_doc_meta_data)
	},

/* File->PrintArea */
        { .name = "FilePrintAreaSet",
	  .label = N_("Set Print Area"),
	  .tooltip = N_("Use the current selection as print area"),
	  .callback = G_CALLBACK (cb_file_print_area_set)
	},
        { .name = "FilePrintAreaClear",
	  .label = N_("Clear Print Area"),
	  .tooltip = N_("Undefine the print area"),
	  .callback = G_CALLBACK (cb_file_print_area_clear)
	},
        { .name = "FilePrintAreaShow",
	  .label = N_("Show Print Area"),
	  .tooltip = N_("Select the print area"),
	  .callback = G_CALLBACK (cb_file_print_area_show)
	},
        { .name = "FilePrintAreaToggleColPageBreak",
	  .label = N_("Set Column Page Break"),
	  .tooltip = N_("Split the page to the left of this column"),
	  .callback = G_CALLBACK (cb_file_print_area_toggle_col)
	},
        { .name = "FilePrintAreaToggleRowPageBreak",
	  .label = N_("Set Row Page Break"),
	  .tooltip = N_("Split the page above this row"),
	  .callback = G_CALLBACK (cb_file_print_area_toggle_row)
	},
        { .name = "FilePrintAreaClearAllPageBreak",
	  .label = N_("Clear All Page Breaks"),
	  .tooltip = N_("Remove all manual pagebreaks from this sheet"),
	  .callback = G_CALLBACK (cb_file_print_area_clear_pagebreaks)
	},

/* Edit -> Clear */
	{ .name = "EditClearAll",
	  .icon = "edit-clear",
	  .label = N_("_All"),
	  .tooltip = N_("Clear the selected cells' formats, comments, and contents"),
	  .callback = G_CALLBACK (cb_edit_clear_all)
	},
	{ .name = "EditClearFormats",
	  .label = N_("_Formats & Hyperlinks"),
	  .tooltip = N_("Clear the selected cells' formats and hyperlinks"),
	  .callback = G_CALLBACK (cb_edit_clear_formats)
	},
	{ .name = "EditClearComments",
	  .icon = "gnumeric-comment-delete",
	  .label = N_("Co_mments"),
	  .tooltip = N_("Delete the selected cells' comments"),
	  .callback = G_CALLBACK (cb_edit_clear_comments)
	},
	{ .name = "EditClearContent",
	  .icon = "edit-clear",
	  .label = N_("_Contents"),
	  .tooltip = N_("Clear the selected cells' contents"),
	  .callback = G_CALLBACK (cb_edit_clear_content)
	},
	{ .name = "EditClearAllFiltered",
	  .icon = "edit-clear",
	  .label = N_("A_ll Filtered Rows"),
	  .tooltip = N_("Clear the selected cells' formats, comments, and contents in the filtered rows"),
	  .callback = G_CALLBACK (cb_edit_clear_all_filtered)
	},
	{ .name = "EditClearFormatsFiltered",
	  .label = N_("F_ormats & Hyperlinks in Filtered Rows"),
	  .tooltip = N_("Clear the selected cells' formats and hyperlinks in the filtered rows"),
	  .callback = G_CALLBACK (cb_edit_clear_formats_filtered)
	},
	{ .name = "EditClearCommentsFiltered",
	  .icon = "gnumeric-comment-delete",
	  .label = N_("Comme_nts in Filtered Rows"),
	  .tooltip = N_("Delete the selected cells' comments in the filtered rows"),
	  .callback = G_CALLBACK (cb_edit_clear_comments_filtered)
	},
	{ .name = "EditClearContentFiltered",
	  .icon = "edit-clear",
	  .label = N_("Content_s of Filtered Rows"),
	  .tooltip = N_("Clear the selected cells' contents in the filtered rows"),
	  .callback = G_CALLBACK (cb_edit_clear_content_filtered)
	},

/* Edit -> Delete */
	/*Translators: Delete "Rows"*/
	{ .name = "EditDeleteRows",
	  .icon = "gnumeric-row-delete",
	  .label = N_("_Rows"),
	  .tooltip = N_("Delete the row(s) containing the selected cells"),
	  .callback = G_CALLBACK (cb_edit_delete_rows)
	},
	/*Translators: Delete "Columns"*/
	{ .name = "EditDeleteColumns",
	  .icon = "gnumeric-column-delete",
	  .label = N_("_Columns"),
	  .tooltip = N_("Delete the column(s) containing the selected cells"),
	  .callback = G_CALLBACK (cb_edit_delete_columns)
	},
	{ .name = "EditDeleteCells",
	  .label = N_("C_ells..."),
	  .accelerator = "<control>minus",
	  .tooltip = N_("Delete the selected cells, shifting others into their place"),
	  .callback = G_CALLBACK (cb_edit_delete_cells)
	},
	{ .name = "EditClearHyperlinks",
	  .icon = "gnumeric-link-delete",
	  .label = N_("_Hyperlinks"),
	  .tooltip = N_("Delete the selected cells' hyperlinks"),
	  .callback = G_CALLBACK (cb_edit_delete_links)
	},
	/* A duplicate that should not go into the menus, used only for the accelerator */
	{ .name = "EditDeleteCellsXL",
	  .label = N_("C_ells..."),
	  .accelerator = "<control>KP_Subtract",
	  .tooltip = N_("Delete the selected cells, shifting others into their place"),
	  .callback = G_CALLBACK (cb_edit_delete_cells)
	},

/* Edit -> Select */

	/* Note : The accelerators involving space are just for display
	 *	purposes.  We actually handle this in
	 *		gnm-pane.c:gnm_pane_key_mode_sheet
	 *	with the rest of the key movement and rangeselection.
	 *	Otherwise input methods would steal them */
	{ .name = "EditSelectAll",
	  .label = N_("_All"),
	  .accelerator = "<control><shift>space",
	  .tooltip = N_("Select all cells in the spreadsheet"),
	  .callback = G_CALLBACK (cb_edit_select_all)
	},
	{ .name = "EditSelectColumn",
	  .label = N_("_Column"),
	  .accelerator = "<control>space",
	  .tooltip = N_("Select an entire column"),
	  .callback = G_CALLBACK (cb_edit_select_col)
	},
	{ .name = "EditSelectRow",
	  .label = N_("_Row"),
	  .accelerator = "<shift>space",
	  .tooltip = N_("Select an entire row"),
	  .callback = G_CALLBACK (cb_edit_select_row)
	},

	{ .name = "EditSelectArray",
	  .label = N_("Arra_y"),
	  .accelerator = "<control>slash",
	  .tooltip = N_("Select an array of cells"),
	  .callback = G_CALLBACK (cb_edit_select_array)
	},
	{ .name = "EditSelectDepends",
	  .label = N_("_Depends"),
	  .accelerator = "<control>bracketright",
	  .tooltip = N_("Select all the cells that depend on the current edit cell"),
	  .callback = G_CALLBACK (cb_edit_select_depends)
	},
	{ .name = "EditSelectInputs",
	  .label = N_("_Inputs"),
	  .accelerator = "<control>bracketleft",
	  .tooltip = N_("Select all the cells are used by the current edit cell"),
	  .callback = G_CALLBACK (cb_edit_select_inputs)
	},

	{ .name = "EditSelectObject",
	  .label = N_("Next _Object"),
	  .accelerator = "<control>Tab",
	  .tooltip = N_("Select the next sheet object"),
	  .callback = G_CALLBACK (cb_edit_select_object)
	},

	{ .name = "EditGotoTop",
	  .icon = "go-top",
	  .label = N_("Go to Top"),
	  .tooltip = N_("Go to the top of the data"),
	  .callback = G_CALLBACK (cb_edit_goto_top)
	},
	{ .name = "EditGotoBottom",
	  .icon = "go-bottom",
	  .label = N_("Go to Bottom"),
	  .tooltip = N_("Go to the bottom of the data"),
	  .callback = G_CALLBACK (cb_edit_goto_bottom)
	},
	{ .name = "EditGotoFirst",
	  .icon = "go-first",
	  .label = N_("Go to First"),
	  .tooltip = N_("Go to the first data cell"),
	  .callback = G_CALLBACK (cb_edit_goto_first)
	},
	{ .name = "EditGotoLast",
	  .icon = "go-last",
	  .label = N_("Go to Last"),
	  .tooltip = N_("Go to the last data cell"),
	  .callback = G_CALLBACK (cb_edit_goto_last)
	},
	{ .name = "EditGoto",
	  .icon = "go-jump",
	  .label = N_("_Go to Cell..."),
	  .accelerator = "<control>g",
	  .tooltip = N_("Jump to a specified cell"),
	  .callback = G_CALLBACK (cb_edit_goto)
	},
	/* This is a navigational aid that is not supposed to appear */
	/* in the menu */
	{ .name = "EditGotoCellIndicator",
	  .label = N_("Go to Current Cell Indicator"),
	  .accelerator = "<shift><control>g",
	  .tooltip = N_("Go to Current Cell Indicator"),
	  .callback = G_CALLBACK (cb_edit_goto_cell_indicator)
	},

/* Edit */
	{ .name = "Repeat",
	  .label = N_("Repeat"),
	  .accelerator = "F4",
	  .tooltip = N_("Repeat the previous action"),
	  .callback = G_CALLBACK (cb_repeat)
	},
	{ .name = "EditPasteSpecial",
	  .icon = "edit-paste",
	  .label = N_("P_aste Special..."),
	  .accelerator = "<shift><control>v",
	  .tooltip = N_("Paste with optional filters and transformations"),
	  .callback = G_CALLBACK (cb_edit_paste_special)
	},

	{ .name = "EditComment",
	  .icon = "gnumeric-comment-edit",
	  .label = N_("Co_mment..."),
	  .tooltip = N_("Edit the selected cell's comment"),
	  .callback = G_CALLBACK (cb_insert_comment)
	},
	{ .name = "EditHyperlink",
	  .icon = "gnumeric-link-edit",
	  .label = N_("Hyper_link..."),
	  .accelerator = "<control>K",
	  .tooltip = N_("Edit the selected cell's hyperlink"),
	  .callback = G_CALLBACK (cb_insert_hyperlink)
	},
#if 0
	{ .name = "EditGenerateName",
	  .label = N_("_Auto generate names..."),
	  .tooltip = N_("Use the current selection to create names"),
	  .callback = G_CALLBACK (cb_auto_generate__named_expr)
	},
#endif

	{ .name = "EditFind",
	  .icon = "edit-find",
	  .label = N_("S_earch..."),
	  .accelerator = "<control>f",
	  .tooltip = N_("Search for something"),
	  .callback = G_CALLBACK (cb_edit_search)
	},
	{ .name = "EditReplace",
	  .icon = "edit-find-replace",
	  .label = N_("Search _& Replace..."),
	  .accelerator = "<control>h",
	  .tooltip = N_("Search for something and replace it with something else"),
	  .callback = G_CALLBACK (cb_edit_search_replace)
	},

	{ .name = "EditRecalc",
	  .label = N_("Recalculate"),
	  .accelerator = "F9",
	  .tooltip = N_("Recalculate the spreadsheet"),
	  .callback = G_CALLBACK (cb_edit_recalc)
	},

	{ .name = "EditPreferences",
	  .icon = "preferences-system",
	  .label = N_("Preferences..."),
	  .tooltip = N_("Change Gnumeric Preferences"),
	  .callback = G_CALLBACK (cb_file_preferences)
	},

/* View */
	{ .name = "ViewFreezeThawPanes",
	  .label = N_("_Freeze Panes"),
	  .tooltip = N_("Freeze the top left of the sheet"),
	  .callback = G_CALLBACK (cb_view_freeze_panes)
	},
	{ .name = "ViewZoom",
	  .icon = "zoom-fit-best", /* dubious */
	  .label = N_("_Zoom..."),
	  .tooltip = N_("Zoom the spreadsheet in or out"),
	  .callback = G_CALLBACK (cb_view_zoom)
	},
	{ .name = "ViewZoomIn",
	  .icon = "zoom-in",
	  .label = N_("Zoom _In"),
	  .accelerator = ZOOM_IN_ACCEL,
	  .tooltip = N_("Increase the zoom to make things larger"),
	  .callback = G_CALLBACK (cb_view_zoom_in)
	},
	{ .name = "ViewZoomOut",
	  .icon = "zoom-out",
	  .label = N_("Zoom _Out"),
	  .accelerator = ZOOM_OUT_ACCEL,
	  .tooltip = N_("Decrease the zoom to make things smaller"),
	  .callback = G_CALLBACK (cb_view_zoom_out)
	},

/* Insert */
	{ .name = "InsertCells",
	  .label = N_("C_ells..."),
	  .accelerator = "<control>plus",
	  .tooltip = N_("Insert new cells"),
	  .callback = G_CALLBACK (cb_insert_cells)
	},
	/* A duplicate that should not go into the menus, used only for the accelerator */
	{ .name = "InsertCellsXL",
	  .label = N_("C_ells..."),
	  .accelerator = "<control>KP_Add",
	  .tooltip = N_("Insert new cells"),
	  .callback = G_CALLBACK (cb_insert_cells)
	},
	/*Translators: Insert "Columns"*/
	{ .name = "InsertColumns",
	  .icon = "gnumeric-column-add",
	  .label = N_("_Columns"),
	  .tooltip = N_("Insert new columns"),
	  .callback = G_CALLBACK (cb_insert_cols)
	},
	/*Translators: Insert "Rows"*/
	{ .name = "InsertRows",
	  .icon = "gnumeric-row-add",
	  .label = N_("_Rows"),
	  .tooltip = N_("Insert new rows"),
	  .callback = G_CALLBACK (cb_insert_rows)
	},

	{ .name = "ChartGuru",
	  .icon = "gnumeric-graphguru",
	  .label = N_("C_hart..."),
	  .tooltip = N_("Insert a Chart"),
	  .callback = G_CALLBACK (cb_launch_chart_guru)
	},
	{ .name = "NewGOComponent",
	  .icon = "New Goffice_Component",
	  .label = N_("_New..."),
	  .tooltip = N_("Insert a new Goffice component object"),
	  .callback = G_CALLBACK (cb_launch_go_component_new)
	},
	{ .name = "GOComponentFromFile",
	  .icon = "New Goffice_Component from a file",
	  .label = N_("_From File..."),
	  .tooltip = N_("Insert a new Goffice component object from a file"),
	  .callback = G_CALLBACK (cb_launch_go_component_from_file)
	},
	{ .name = "InsertImage",
	  .icon = "insert-image",
	  .label = N_("_Image..."),
	  .tooltip = N_("Insert an image"),
	  .callback = G_CALLBACK (cb_insert_image)
	},

	{ .name = "InsertComment",
	  .icon = "gnumeric-comment-add",
	  .label = N_("Co_mment..."),
	  .tooltip = N_("Insert a comment"),
	  .callback = G_CALLBACK (cb_insert_comment)
	},
	{ .name = "InsertHyperlink",
	  .icon = "gnumeric-link-add",
	  .label = N_("Hyper_link..."),
	  .accelerator = "<control>K",
	  .tooltip = N_("Insert a Hyperlink"),
	  .callback = G_CALLBACK (cb_insert_hyperlink)
	},
	{ .name = "InsertSortDecreasing",
	  .icon = "view-sort-descending",
	  .label = N_("Sort (_Descending)"),
	  .tooltip = N_("Wrap with SORT (descending)"),
	  .callback = G_CALLBACK (cb_insert_sort_descending)
	},
	{ .name = "InsertSortIncreasing",
	  .icon = "view-sort-ascending",
	  .label = N_("Sort (_Ascending)"),
	  .tooltip = N_("Wrap with SORT (ascending)"),
	  .callback = G_CALLBACK (cb_insert_sort_ascending)
	},

/* Insert -> Special */
	{ .name = "InsertCurrentDate",
	  .label = N_("Current _Date"),
	  .accelerator = "<control>semicolon",
	  .tooltip = N_("Insert the current date into the selected cell(s)"),
	  .callback = G_CALLBACK (cb_insert_current_date)
	},

	{ .name = "InsertCurrentTime",
	  .label = N_("Current _Time"),
	  .accelerator = "<control>colon",
	  .tooltip = N_("Insert the current time into the selected cell(s)"),
	  .callback = G_CALLBACK (cb_insert_current_time)
	},

	{ .name = "InsertCurrentDateTime",
	  .label = N_("Current D_ate and Time"),
	  .accelerator = "<control>period",
	  .tooltip = N_("Insert the current date and time into the selected cell(s)"),
	  .callback = G_CALLBACK (cb_insert_current_date_time)
	},

/* Insert -> Name */
	{ .name = "EditNames",
	  .label = N_("_Names..."),
	  .accelerator = "<control>F3",
	  .tooltip = N_("Edit defined names for expressions"),
	  .callback = G_CALLBACK (cb_define_name)
	},

/* Format */
	{ .name = "FormatAuto",
	  .label = N_("_Autoformat..."),
	  .tooltip = N_("Format a region of cells according to a pre-defined template"),
	  .callback = G_CALLBACK (cb_autoformat)
	},
	{ .name = "SheetDirection",
	  .icon = "format-text-direction-ltr",
	  .label = N_("Direction"),
	  .tooltip = N_("Toggle sheet direction, left-to-right vs right-to-left"),
	  .callback = G_CALLBACK (cb_direction)
	},

/* Format -> Cells */
	{ .name = "FormatCells",
	  .label = N_("_Format..."),
	  .accelerator = "<control>1",
	  .tooltip = N_("Modify the formatting of the selected cells"),
	  .callback = G_CALLBACK (cb_format_cells)
	},
	{ .name = "FormatCellsCond",
	  .label = N_("_Conditional Formatting..."),
	  .tooltip = N_("Modify the conditional formatting of the selected cells"),
	  .callback = G_CALLBACK (cb_format_cells_cond)
	},
	{ .name = "FormatCellsFitHeight",
	  .icon = "gnumeric-row-size",
	  .label = N_("Auto Fit _Height"),
	  .tooltip = N_("Ensure rows are just tall enough to display content of selection"),
	  .callback = G_CALLBACK (cb_format_cells_auto_fit_height)
	},
	{ .name = "FormatCellsFitWidth",
	  .icon = "gnumeric-column-size",
	  .label = N_("Auto Fit _Width"),
	  .tooltip = N_("Ensure columns are just wide enough to display content of selection"),
	  .callback = G_CALLBACK (cb_format_cells_auto_fit_width)
	},


/* Format -> Col */
	{ .name = "ColumnSize",
	  .icon = "gnumeric-column-size",
	  .label = N_("_Width..."),
	  .tooltip = N_("Change width of the selected columns"),
	  .callback = G_CALLBACK (cb_set_column_width)
	},
	{ .name = "ColumnAutoSize",
	  .icon = "gnumeric-column-size",
	  .label = N_("_Auto Fit Width"),
	  .tooltip = N_("Ensure columns are just wide enough to display their content"),
	  .callback = G_CALLBACK (cb_format_column_auto_fit)
	},
	{ .name = "ColumnHide",
	  .icon = "gnumeric-column-hide",
	  .label = N_("_Hide"),
	  .accelerator = "<control>0",
	  .tooltip = N_("Hide the selected columns"),
	  .callback = G_CALLBACK (cb_format_column_hide)
	},
	{ .name = "ColumnUnhide",
	  .icon = "gnumeric-column-unhide",
	  .label = N_("_Unhide"),
	  .accelerator = "<control>parenright",
	  .tooltip = N_("Make any hidden columns in the selection visible"),
	  .callback = G_CALLBACK (cb_format_column_unhide)
	},
	{ .name = "ColumnDefaultSize",
	  .icon = "gnumeric-column-size",
	  .label = N_("_Standard Width"),
	  .tooltip = N_("Change the default column width"),
	  .callback = G_CALLBACK (cb_format_column_std_width)
	},

/* Format -> Row */
	{ .name = "RowSize",
	  .icon = "gnumeric-row-size",
	  .label = N_("H_eight..."),
	  .tooltip = N_("Change height of the selected rows"),
	  .callback = G_CALLBACK (cb_set_row_height)
	},
	{ .name = "RowAutoSize",
	  .icon = "gnumeric-row-size",
	  .label = N_("_Auto Fit Height"),
	  .tooltip = N_("Ensure rows are just tall enough to display their content"),
	  .callback = G_CALLBACK (cb_format_row_auto_fit)
	},
	{ .name = "RowHide",
	  .icon = "gnumeric-row-hide",
	  .label = N_("_Hide"),
	  .accelerator = "<control>9",
	  .tooltip = N_("Hide the selected rows"),
	  .callback = G_CALLBACK (cb_format_row_hide)
	},
	{ .name = "RowUnhide",
	  .icon = "gnumeric-row-unhide",
	  .label = N_("_Unhide"),
	  .accelerator = "<control>parenleft",
	  .tooltip = N_("Make any hidden rows in the selection visible"),
	  .callback = G_CALLBACK (cb_format_row_unhide)
	},
	{ .name = "RowDefaultSize",
	  .icon = "gnumeric-row-size",
	  .label = N_("_Standard Height"),
	  .tooltip = N_("Change the default row height"),
	  .callback = G_CALLBACK (cb_format_row_std_height)
	},

/* Tools */
	{ .name = "ToolsPlugins",
	  .label = N_("_Plug-ins..."),
	  .tooltip = N_("Manage available plugin modules"),
	  .callback = G_CALLBACK (cb_tools_plugins)
	},
	{ .name = "ToolsAutoCorrect",
	  .label = N_("Auto _Correct..."),
	  .tooltip = N_("Automatically perform simple spell checking"),
	  .callback = G_CALLBACK (cb_tools_autocorrect)
	},
	{ .name = "ToolsAutoSave",
	  .label = N_("_Auto Save..."),
	  .tooltip = N_("Automatically save the current document at regular intervals"),
	  .callback = G_CALLBACK (cb_tools_auto_save)
	},
	{ .name = "ToolsGoalSeek",
	  .label = N_("_Goal Seek..."),
	  .tooltip = N_("Iteratively recalculate to find a target value"),
	  .callback = G_CALLBACK (cb_tools_goal_seek)
	},
	{ .name = "ToolsSolver",
	  .label = N_("_Solver..."),
	  .tooltip = N_("Iteratively recalculate with constraints to approach a target value"),
	  .callback = G_CALLBACK (cb_tools_solver)
	},
	{ .name = "ToolsSimulation",
	  .label = N_("Si_mulation..."),
	  .tooltip = N_("Test decision alternatives by using Monte Carlo "
		   "simulation to find out probable outputs and risks related to them"),
	  .callback = G_CALLBACK (cb_tools_simulation)
	},
	{ .name = "ToolsCompare",
	  .label = N_("Compare Sheets..."),
	  .tooltip = N_("Find differences between two sheets"),
	  .callback = G_CALLBACK (cb_tools_compare)
	},

/* Tools -> Scenarios */
	{ .name = "ToolsScenarios",
	  .label = N_("_View..."),
	  .tooltip = N_("View, delete and report different scenarios"),
	  .callback = G_CALLBACK (cb_tools_scenarios)
	},
	{ .name = "ToolsScenarioAdd",
	  .label = N_("_Add..."),
	  .tooltip = N_("Add a new scenario"),
	  .callback = G_CALLBACK (cb_tools_scenario_add)
	},

/* Statistics */

	{ .name = "ToolsSampling",
	  .label = N_("_Sampling..."),
	  .tooltip = N_("Periodic and random samples"),
	  .callback = G_CALLBACK (cb_tools_sampling)
	},

/* Statistics -> Descriptive*/

	{ .name = "ToolsCorrelation",
	  .label = N_("_Correlation..."),
	  .tooltip = N_("Pearson Correlation"),
	  .callback = G_CALLBACK (cb_tools_correlation)
	},
	{ .name = "ToolsCovariance",
	  .label = N_("Co_variance..."),
	  .tooltip = N_("Covariance"),
	  .callback = G_CALLBACK (cb_tools_covariance)
	},
	{ .name = "ToolsDescStatistics",
	  .label = N_("_Descriptive Statistics..."),
	  .tooltip = N_("Various summary statistics"),
	  .callback = G_CALLBACK (cb_tools_desc_statistics)
	},

/* Statistics -> Descriptive -> Frequencies */

	{ .name = "ToolsFrequency",
	  .label = N_("Fre_quency Tables..."),
	  .tooltip = N_("Frequency tables for non-numeric data"),
	  .callback = G_CALLBACK (cb_tools_frequency)
	},
	{ .name = "ToolsHistogram",
	  .label = N_("_Histogram..."),
	  .tooltip = N_("Various frequency tables for numeric data"),
	  .callback = G_CALLBACK (cb_tools_histogram)
	},
	{ .name = "ToolsRanking",
	  .label = N_("Ranks And _Percentiles..."),
	  .tooltip = N_("Ranks, placements and percentiles"),
	  .callback = G_CALLBACK (cb_tools_ranking)
	},

/* Statistics -> DependentObservations */

	{ .name = "ToolsFourier",
	  .label = N_("_Fourier Analysis..."),
	  .tooltip = N_("Fourier Analysis"),
	  .callback = G_CALLBACK (cb_tools_fourier)
	},
	{ .name = "ToolsPrincipalComponents",
	  .label =
	  N_("Principal Components Analysis..."),
	  .tooltip = N_("Principal Components Analysis"),
	  .callback = G_CALLBACK (cb_tools_principal_components)
	},
/* Statistics -> DependentObservations -> Forecast*/

	{ .name = "ToolsExpSmoothing",
	  .label = N_("_Exponential Smoothing..."),
	  .tooltip = N_("Exponential smoothing..."),
	  .callback = G_CALLBACK (cb_tools_exp_smoothing)
	},
	{ .name = "ToolsAverage",
	  .label = N_("_Moving Average..."),
	  .tooltip = N_("Moving average..."),
	  .callback = G_CALLBACK (cb_tools_average)
	},
	{ .name = "ToolsRegression",
	  .label = N_("_Regression..."),
	  .tooltip = N_("Regression Analysis"),
	  .callback = G_CALLBACK (cb_tools_regression)
	},
	{ .name = "ToolsKaplanMeier",
	  .label = N_("_Kaplan-Meier Estimates..."),
	  .tooltip = N_("Creation of Kaplan-Meier Survival Curves"),
	  .callback = G_CALLBACK (cb_tools_kaplan_meier)
	},

/* Statistics -> OneSample */

	{ .name = "ToolsNormalityTests",
	  .label = N_("_Normality Tests..."),
	  .tooltip = N_("Testing a sample for normality"),
	  .callback = G_CALLBACK (cb_tools_normality_tests)
	},
	{ .name = "ToolsOneMeanTest",
	  .label = N_("Claims About a _Mean..."),
	  .tooltip = N_("Testing the value of a mean"),
	  .callback = G_CALLBACK (cb_tools_one_mean_test)
	},

/* Statistics -> OneSample -> OneMedian*/

	{ .name = "ToolsOneMedianSignTest",
	  .label = N_("_Sign Test..."),
	  .tooltip = N_("Testing the value of a median"),
	  .callback = G_CALLBACK (cb_tools_sign_test_one_median)
	},
	{ .name = "ToolsOneMedianWilcoxonSignedRank",
	  .label = N_("_Wilcoxon Signed Rank Test..."),
	  .tooltip = N_("Testing the value of a median"),
	  .callback = G_CALLBACK (cb_tools_wilcoxon_signed_rank_one_median)
	},

/* Statistics -> TwoSamples */

	{ .name = "ToolsFTest",
	  .label = N_("Claims About Two _Variances"),
	  .tooltip = N_("Comparing two population variances"),
	  .callback = G_CALLBACK (cb_tools_ftest)
	},

/* Statistics -> TwoSamples -> Two Means*/

	{ .name = "ToolTTestPaired",
	  .label = N_("_Paired Samples..."),
	  .tooltip = N_("Comparing two population means for two paired samples"),
	  .callback = G_CALLBACK (cb_tools_ttest_paired)
	},

	{ .name = "ToolTTestEqualVar",
	  .label = N_("Unpaired Samples, _Equal Variances..."),
	  .tooltip = N_("Comparing two population means for two unpaired samples from populations with equal variances"),
	  .callback = G_CALLBACK (cb_tools_ttest_equal_var)
	},

	{ .name = "ToolTTestUnequalVar",
	  .label = N_("Unpaired Samples, _Unequal Variances..."),
	  .tooltip = N_("Comparing two population means for two unpaired samples from populations with unequal variances"),
	  .callback = G_CALLBACK (cb_tools_ttest_unequal_var)
	},

	{ .name = "ToolZTest",
	  .label = N_("Unpaired Samples, _Known Variances..."),
	  .tooltip = N_("Comparing two population means from populations with known variances"),
	  .callback = G_CALLBACK (cb_tools_ztest)
	},

/* Statistics -> TwoSamples -> Two Medians*/

	{ .name = "ToolsTwoMedianSignTest",
	  .label = N_("_Sign Test..."),
	  .tooltip = N_("Comparing the values of two medians of paired observations"),
	  .callback = G_CALLBACK (cb_tools_sign_test_two_medians)
	},
	{ .name = "ToolsTwoMedianWilcoxonSignedRank",
	  .label = N_("_Wilcoxon Signed Rank Test..."),
	  .tooltip = N_("Comparing the values of two medians of paired observations"),
	  .callback = G_CALLBACK (cb_tools_wilcoxon_signed_rank_two_medians)
	},
	{ .name = "ToolsTwoMedianWilcoxonMannWhitney",
	  .label = N_("Wilcoxon-_Mann-Whitney Test..."),
	  .tooltip = N_("Comparing the values of two medians of unpaired observations"),
	  .callback = G_CALLBACK (cb_tools_wilcoxon_mann_whitney)
	},

/* Statistics -> MultipleSamples */

/* Statistics -> MultipleSamples -> ANOVA*/

	{ .name = "ToolsANOVAoneFactor",
	  .label = N_("_One Factor..."),
	  .tooltip = N_("One Factor Analysis of Variance..."),
	  .callback = G_CALLBACK (cb_tools_anova_one_factor)
	},
	{ .name = "ToolsANOVAtwoFactor",
	  .label = N_("_Two Factor..."),
	  .tooltip = N_("Two Factor Analysis of Variance..."),
	  .callback = G_CALLBACK (cb_tools_anova_two_factor)
	},

/* Statistics -> MultipleSamples -> ContingencyTable*/

	{ .name = "ToolsHomogeneity",
	  .label = N_("Test of _Homogeneity..."),
	  .tooltip = N_("Chi Squared Test of Homogeneity..."),
	  .callback = G_CALLBACK (cb_tools_chi_square_homogeneity)
	},
	{ .name = "ToolsIndependence",
	  .label = N_("Test of _Independence..."),
	  .tooltip = N_("Chi Squared Test of Independence..."),
	  .callback = G_CALLBACK (cb_tools_chi_square_independence)
	},

/* Data */
	{ .name = "DataSort",
	  .icon = "view-sort-ascending",
	  .label = N_("_Sort..."),
	  .tooltip = N_("Sort the selected region"),
	  .callback = G_CALLBACK (cb_data_sort)
	},
	{ .name = "DataShuffle",
	  .label = N_("Sh_uffle..."),
	  .tooltip = N_("Shuffle cells, rows or columns"),
	  .callback = G_CALLBACK (cb_data_shuffle)
	},
	{ .name = "DataValidate",
	  .label = N_("_Validate..."),
	  .tooltip = N_("Validate input with preset criteria"),
	  .callback = G_CALLBACK (cb_data_validate)
	},
	{ .name = "DataTextToColumns",
	  .label = N_("T_ext to Columns..."),
	  .tooltip = N_("Parse the text in the selection into data"),
	  .callback = G_CALLBACK (cb_data_text_to_columns)
	},
	{ .name = "DataConsolidate",
	  .label = N_("_Consolidate..."),
	  .tooltip = N_("Consolidate regions using a function"),
	  .callback = G_CALLBACK (cb_data_consolidate)
	},
	{ .name = "DataTable",
	  .label = N_("_Table..."),
	  .tooltip = N_("Create a Data Table to evaluate a function with multiple inputs"),
	  .callback = G_CALLBACK (cb_data_table)
	},
	{ .name = "DataExport",
	  .label = N_("E_xport into Other Format..."),
	  .tooltip = N_("Export the current workbook or sheet"),
	  .callback = G_CALLBACK (cb_data_export)
	},
	{ .name = "DataExportText",
	  .label = N_("Export as _Text File..."),
	  .tooltip = N_("Export the current sheet as a text file"),
	  .callback = G_CALLBACK (cb_data_export_text)
	},
	{ .name = "DataExportCSV",
	  .label = N_("Export as _CSV File..."),
	  .tooltip = N_("Export the current sheet as a csv file"),
	  .callback = G_CALLBACK (cb_data_export_csv)
	},
	{ .name = "DataExportRepeat",
	  .label = N_("Repeat Export"),
	  .accelerator = "<control>E",
	  .tooltip = N_("Repeat the last data export"),
	  .callback = G_CALLBACK (cb_data_export_repeat)
	},

/* Data -> Fill */
	{ .name = "EditFillAutofill",
	  .label = N_("Auto_fill"),
	  .tooltip = N_("Automatically fill the current selection"),
	  .callback = G_CALLBACK (cb_edit_fill_autofill)
	},
	{ .name = "ToolsMerge",
	  .label = N_("_Merge..."),
	  .tooltip = N_("Merges columnar data into a sheet creating duplicate sheets for each row"),
	  .callback = G_CALLBACK (cb_tools_merge)
	},
	{ .name = "ToolsTabulate",
	  .label = N_("_Tabulate Dependency..."),
	  .tooltip = N_("Make a table of a cell's value as a function of other cells"),
	  .callback = G_CALLBACK (cb_tools_tabulate)
	},
	{ .name = "EditFillSeries",
	  .label = N_("_Series..."),
	  .tooltip = N_("Fill according to a linear or exponential series"),
	  .callback = G_CALLBACK (cb_edit_fill_series)
	},
	{ .name = "RandomGeneratorUncorrelated",
	  .label = N_("_Uncorrelated..."),
	  .tooltip = N_("Generate random numbers of a selection of distributions"),
	  .callback = G_CALLBACK (cb_tools_random_generator_uncorrelated)
	},
	{ .name = "RandomGeneratorCorrelated",
	  .label = N_("_Correlated..."),
	  .tooltip = N_("Generate variates for correlated normal distributed random variables"),
	  .callback = G_CALLBACK (cb_tools_random_generator_correlated)
	},
	{ .name = "CopyDown",
	  .label = N_("Fill Downwards"),
	  .accelerator = "<control>D",
	  .tooltip = N_("Copy the content from the top row to the cells below"),
	  .callback = G_CALLBACK (cb_copydown)
	},
	{ .name = "CopyRight",
	  .label = N_("Fill to Right"),
	  .accelerator = "<control>R",
	  .tooltip = N_("Copy the content from the left column to the cells on the right"),
	  .callback = G_CALLBACK (cb_copyright)
	},


/* Data -> Outline */
	{ .name = "DataOutlineHideDetail",
	  .icon = "gnumeric-detail-hide",
	  .label = N_("_Hide Detail"),
	  .tooltip = N_("Collapse an outline group"),
	  .callback = G_CALLBACK (cb_data_hide_detail)
	},
	{ .name = "DataOutlineShowDetail",
	  .icon = "gnumeric-detail-show",
	  .label = N_("_Show Detail"),
	  .tooltip = N_("Uncollapse an outline group"),
	  .callback = G_CALLBACK (cb_data_show_detail)
	},
	{ .name = "DataOutlineGroup",
	  .icon = "gnumeric-group",
	  .label = N_("_Group..."),
	  .accelerator = "<shift><alt>Right",
	  .tooltip = N_("Add an outline group"),
	  .callback = G_CALLBACK (cb_data_group)
	},
	{ .name = "DataOutlineUngroup",
	  .icon = "gnumeric-ungroup",
	  .label = N_("_Ungroup..."),
	  .accelerator = "<shift><alt>Left",
	  .tooltip = N_("Remove an outline group"),
	  .callback = G_CALLBACK (cb_data_ungroup)
	},

/* Data -> Filter */
	{ .name = "DataAutoFilter",
	  .icon = "gnumeric-autofilter",
	  .label = N_("Add _Auto Filter"),
	  .tooltip = N_("Add or remove a filter"),
	  .callback = G_CALLBACK (cb_auto_filter)
	},
	{ .name = "DataFilterShowAll",
	  .label = N_("_Clear Advanced Filter"),
	  .tooltip = N_("Show all rows hidden by an advanced filter"),
	  .callback = G_CALLBACK (cb_show_all)
	},
	{ .name = "DataFilterAdvancedfilter",
	  .label = N_("Advanced _Filter..."),
	  .tooltip = N_("Filter data with given criteria"),
	  .callback = G_CALLBACK (cb_data_filter)
	},
/* Data -> External */
	{ .name = "DataImportText",
	  .label = N_("Import _Text File..."),
	  .tooltip = N_("Import data from a text file"),
	  .callback = G_CALLBACK (cb_data_import_text)
	},
	{ .name = "DataImportOther",
	  .label = N_("Import _Other File..."),
	  .tooltip = N_("Import data from a file"),
	  .callback = G_CALLBACK (cb_data_import_other)
	},

/* Data -> Data Slicer */
	/* label and tip are context dependent, see wbcg_menu_state_update */
	{ .name = "DataSlicer",
	  .label = N_("Add _Data Slicer"),
	  .tooltip = N_("Create a data slicer"),
	  .callback = G_CALLBACK (cb_data_slicer_create)
	},
	{ .name = "DataSlicerRefresh",
	  .label = N_("_Refresh"),
	  .tooltip = N_("Regenerate a data slicer from the source data"),
	  .callback = G_CALLBACK (cb_data_slicer_refresh)
	},
	{ .name = "DataSlicerEdit",
	  .label = N_("_Edit Data Slicer..."),
	  .tooltip = N_("Adjust a data slicer"),
	  .callback = G_CALLBACK (cb_data_slicer_edit)
	},

/* Standard Toolbar */
	{ .name = "AutoSum",
	  .icon = "gnumeric-autosum",
	  .label = N_("Sum"),
	  .accelerator = "<alt>equal",
	  .tooltip = N_("Sum into the current cell"),
	  .callback = G_CALLBACK (cb_autosum)
	},
	{ .name = "InsertFormula",
	  .icon = "gnumeric-formulaguru",
	  .label = N_("_Function..."),
	  .tooltip = N_("Edit a function in the current cell"),
	  .callback = G_CALLBACK (cb_formula_guru)
	},

	{ .name = "SortAscending",
	  .icon = "view-sort-ascending",
	  .label = N_("Sort Ascending"),
	  .tooltip = N_("Sort the selected region in ascending order based on the first column selected"),
	  .callback = G_CALLBACK (cb_sort_ascending)
	},
	{ .name = "SortDescending",
	  .icon = "view-sort-descending",
	  .label = N_("Sort Descending"),
	  .tooltip = N_("Sort the selected region in descending order based on the first column selected"),
	  .callback = G_CALLBACK (cb_sort_descending)
	},

/* Object Toolbar */
	{ .name = "CreateFrame",
	  .icon = "gnumeric-object-frame",
	  .label = N_("Frame"),
	  .tooltip = N_("Create a frame"),
	  .callback = G_CALLBACK (cmd_create_frame)
	},
	{ .name = "CreateCheckbox",
	  .icon = "gnumeric-object-checkbox",
	  .label = N_("Checkbox"),
	  .tooltip = N_("Create a checkbox"),
	  .callback = G_CALLBACK (cmd_create_checkbox)
	},
	{ .name = "CreateScrollbar",
	  .icon = "gnumeric-object-scrollbar",
	  .label = N_("Scrollbar"),
	  .tooltip = N_("Create a scrollbar"),
	  .callback = G_CALLBACK (cmd_create_scrollbar)
	},
	{ .name = "CreateSlider",
	  .icon = "gnumeric-object-slider",
	  .label = N_("Slider"),
	  .tooltip = N_("Create a slider"),
	  .callback = G_CALLBACK (cmd_create_slider)
	},
	{ .name = "CreateSpinButton",
	  .icon = "gnumeric-object-spinbutton",
	  .label = N_("SpinButton"),
	  .tooltip = N_("Create a spin button"),
	  .callback = G_CALLBACK (cmd_create_spinbutton)
	},
	{ .name = "CreateList",
	  .icon = "gnumeric-object-list",
	  .label = N_("List"),
	  .tooltip = N_("Create a list"),
	  .callback = G_CALLBACK (cmd_create_list)
	},
	{ .name = "CreateCombo",
	  .icon = "gnumeric-object-combo",
	  .label = N_("Combo Box"),
	  .tooltip = N_("Create a combo box"),
	  .callback = G_CALLBACK (cmd_create_combo)
	},
	{ .name = "CreateLine",
	  .icon = "gnumeric-object-line",
	  .label = N_("Line"),
	  .tooltip = N_("Create a line object"),
	  .callback = G_CALLBACK (cmd_create_line)
	},
	{ .name = "CreateArrow",
	  .icon = "gnumeric-object-arrow",
	  .label = N_("Arrow"),
	  .tooltip = N_("Create an arrow object"),
	  .callback = G_CALLBACK (cmd_create_arrow)
	},
	{ .name = "CreateRectangle",
	  .icon = "gnumeric-object-rectangle",
	  .label = N_("Rectangle"),
	  .tooltip = N_("Create a rectangle object"),
	  .callback = G_CALLBACK (cmd_create_rectangle)
	},
	{ .name = "CreateEllipse",
	  .icon = "gnumeric-object-ellipse",
	  .label = N_("Ellipse"),
	  .tooltip = N_("Create an ellipse object"),
	  .callback = G_CALLBACK (cmd_create_ellipse)
	},
	{ .name = "CreateButton",
	  .icon = "gnumeric-object-button",
	  .label = N_("Button"),
	  .tooltip = N_("Create a button"),
	  .callback = G_CALLBACK (cmd_create_button)
	},
	{ .name = "CreateRadioButton",
	  .icon = "gnumeric-object-radiobutton",
	  .label = N_("RadioButton"),
	  .tooltip = N_("Create a radio button"),
	  .callback = G_CALLBACK (cmd_create_radiobutton)
	},

/* Format toolbar */
	{ .name = "FormatMergeCells",
	  .icon = "gnumeric-cells-merge",
	  .label = N_("Merge"),
	  .tooltip = N_("Merge a range of cells"),
	  .callback = G_CALLBACK (cb_merge_cells)
	},
	{ .name = "FormatUnmergeCells",
	  .icon = "gnumeric-cells-split",
	  .label = N_("Unmerge"),
	  .tooltip = N_("Split merged ranges of cells"),
	  .callback = G_CALLBACK (cb_unmerge_cells)
	},

	{ .name = "FormatAsGeneral",
	  .label = N_("General"),
	  .accelerator = "<control>asciitilde",
	  .tooltip = N_("Format the selection as General"),
	  .callback = G_CALLBACK (cb_format_as_general)
	},
	{ .name = "FormatAsNumber",
	  .label = N_("Number"),
	  .accelerator = "<control>exclam",
	  .tooltip = N_("Format the selection as numbers"),
	  .callback = G_CALLBACK (cb_format_as_number)
	},
	{ .name = "FormatAsCurrency",
	  .label = N_("Currency"),
	  .accelerator = "<control>dollar",
	  .tooltip = N_("Format the selection as currency"),
	  .callback = G_CALLBACK (cb_format_as_currency)
	},
	{ .name = "FormatAsAccounting",
	  .icon = "gnumeric-format-accounting",
	  .label = N_("Accounting"),
	  .tooltip = N_("Format the selection as accounting"),
	  .callback = G_CALLBACK (cb_format_as_accounting)
	},
	{ .name = "FormatAsPercentage",
	  .icon = "gnumeric-format-percentage",
	  .label = N_("Percentage"),
	  .accelerator = "<control>percent",
	  .tooltip = N_("Format the selection as percentage"),
	  .callback = G_CALLBACK (cb_format_as_percentage)
	},
	{ .name = "FormatAsScientific",
	  .label = N_("Scientific"),
	  .accelerator = "<control>asciicircum",
	  .tooltip = N_("Format the selection as scientific"),
	  .callback = G_CALLBACK (cb_format_as_scientific)
	},
	{ .name = "FormatAsDate",
	  .label = N_("Date"),
	  .accelerator = "<control>numbersign",
	  .tooltip = N_("Format the selection as date"),
	  .callback = G_CALLBACK (cb_format_as_date)
	},
	{ .name = "FormatAsTime",
	  .label = N_("Time"),
	  .accelerator = "<control>at",
	  .tooltip = N_("Format the selection as time"),
	  .callback = G_CALLBACK (cb_format_as_time)
	},
	{ .name = "FormatAddBorders",
	  .label = N_("AddBorders"),
	  .accelerator = "<control>ampersand",
	  .tooltip = N_("Add a border around the selection"),
	  .callback = G_CALLBACK (cb_format_add_borders)
	},
	{ .name = "FormatClearBorders",
	  .label = N_("ClearBorders"),
	  .accelerator = "<control>underscore",
	  .tooltip = N_("Clear the border around the selection"),
	  .callback = G_CALLBACK (cb_format_clear_borders)
	},

	{ .name = "FormatWithThousands",
	  .icon = "gnumeric-format-thousand-separator",
	  .label = N_("Thousands Separator"),
	  .tooltip = N_("Set the format of the selected cells to include a thousands separator"),
	  .callback = G_CALLBACK (cb_format_with_thousands)
	},
	{ .name = "FormatIncreasePrecision",
	  .icon = "gnumeric-format-precision-increase",
	  .label = N_("Increase Precision"),
	  .tooltip = N_("Increase the number of decimals displayed"),
	  .callback = G_CALLBACK (cb_format_inc_precision)
	},
	{ .name = "FormatDecreasePrecision",
	  .icon = "gnumeric-format-precision-decrease",
	  .label = N_("Decrease Precision"),
	  .tooltip = N_("Decrease the number of decimals displayed"),
	  .callback = G_CALLBACK (cb_format_dec_precision)
	},

	/* Gtk marks these accelerators as invalid because they use Tab
	 * enable them manually in gnm-pane.c */
	{ .name = "FormatDecreaseIndent",
	  .label = N_("Decrease Indentation"),
	  .icon = "format-indent-less",
	  .accelerator = "<control><alt><shift>Tab",
	  .tooltip = N_("Decrease the indent, and align the contents to the left"),
	  .callback = G_CALLBACK (cb_format_dec_indent)
	},
	{ .name = "FormatIncreaseIndent",
	  .label = N_("Increase Indentation"),
	  .icon = "format-indent-more",
	  .accelerator = "<control><alt>Tab",
	  .tooltip = N_("Increase the indent, and align the contents to the left"),
	  .callback = G_CALLBACK (cb_format_inc_indent)
	},

	/* ---------------------------------------- */

	{ .name = "SheetDisplayOutlines",
	  .toggle = TRUE,
	  .label = N_("Display _Outlines"),
	  .accelerator = "<control>8",
	  .tooltip = N_("Toggle whether or not to display outline groups"),
	  .callback = G_CALLBACK (cb_sheet_pref_display_outlines)
	},
	{ .name = "SheetOutlineBelow",
	  .toggle = TRUE,
	  .label = N_("Outlines _Below"),
	  .tooltip = N_("Toggle whether to display row outlines on top or bottom"),
	  .callback = G_CALLBACK (cb_sheet_pref_outline_symbols_below)
	},
	{ .name = "SheetOutlineRight",
	  .toggle = TRUE,
	  .label = N_("Outlines _Right"),
	  .tooltip = N_("Toggle whether to display column outlines on the left or right"),
	  .callback = G_CALLBACK (cb_sheet_pref_outline_symbols_right)
	},
	{ .name = "SheetDisplayFormulas",
	  .toggle = TRUE,
	  .icon = "gnumeric-formulaguru",
	  .label = N_("Display _Formul\303\246"),
	  .accelerator = "<control>quoteleft",
	  .tooltip = N_("Display the value of a formula or the formula itself"),
	  .callback = G_CALLBACK (cb_sheet_pref_display_formulas)
	},
	{ .name = "SheetHideZeros",
	  .toggle = TRUE,
	  .label = N_("_Hide Zeros"),
	  .tooltip = N_("Toggle whether or not to display zeros as blanks"),
	  .callback = G_CALLBACK (cb_sheet_pref_hide_zero)
	},
	{ .name = "SheetHideGridlines",
	  .toggle = TRUE,
	  .label = N_("Hide _Gridlines"),
	  .tooltip = N_("Toggle whether or not to display gridlines"),
	  .callback = G_CALLBACK (cb_sheet_pref_hide_grid)
	},
	{ .name = "SheetHideColHeader",
	  .toggle = TRUE,
	  .label = N_("Hide _Column Headers"),
	  .tooltip = N_("Toggle whether or not to display column headers"),
	  .callback = G_CALLBACK (cb_sheet_pref_hide_col_header)
	},
	{ .name = "SheetHideRowHeader",
	  .toggle = TRUE,
	  .label = N_("Hide _Row Headers"),
	  .tooltip = N_("Toggle whether or not to display row headers"),
	  .callback = G_CALLBACK (cb_sheet_pref_hide_row_header)
	},

	/* TODO : Make this a sub menu when we have more convention types */
	{ .name = "SheetUseR1C1",
	  .toggle = TRUE,
	  .label = N_("Use R1C1 N_otation "),
	  .tooltip = N_("Display addresses as R1C1 or A1"),
	  .callback = G_CALLBACK (cb_sheet_pref_use_r1c1)
	},

	{ .name = "AlignLeft",
	  .toggle = TRUE,
	  .icon = "format-justify-left",
	  .label = N_("_Left Align"),
	  .tooltip = N_("Align left"),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_align_left)
	},
	{ .name = "AlignCenter",
	  .toggle = TRUE,
	  .icon = "format-justify-center",
	  .label = N_("_Center"),
	  .tooltip = N_("Center horizontally"),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_align_center)
	},
	{ .name = "AlignRight",
	  .toggle = TRUE,
	  .icon = "format-justify-right",
	  .label = N_("_Right Align"),
	  .tooltip = N_("Align right"),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_align_right)
	},
	{ .name = "CenterAcrossSelection",
	  .toggle = TRUE,
	  .icon = "gnumeric-center-across-selection",
	  .label = N_("_Center Across Selection"),
	  .tooltip = N_("Center horizontally across the selection"),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_center_across_selection)
	},
	{ .name = "MergeAndCenter",
	  .toggle = TRUE,
	  .label = N_("_Merge and Center"),
	  .tooltip = N_("Merge the selection into 1 cell, and center horizontally."),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_merge_and_center)
	},
#warning "Add justify"
#warning "h/v distributed?"

	{ .name = "AlignTop",
	  .icon = "gnumeric-format-valign-top",
	  .toggle = TRUE,
	  .label = N_("Align _Top"),
	  .tooltip = N_("Align Top"),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_align_top)
	},
	{ .name = "AlignVCenter",
	  .icon = "gnumeric-format-valign-center",
	  .toggle = TRUE,
	  .label = N_("_Vertically Center"),
	  .tooltip = N_("Vertically Center"),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_align_vcenter)
	},
	{ .name = "AlignBottom",
	  .icon = "gnumeric-format-valign-bottom",
	  .toggle = TRUE,
	  .label = N_("Align _Bottom"),
	  .tooltip = N_("Align Bottom"),
	  .hide_vertical = TRUE,
	  .callback = G_CALLBACK (cb_align_bottom)
	},
};

static GnmActionEntry const font_actions[] = {
	{ .name = "FontBold",
	  .toggle = TRUE,
	  .icon = "format-text-bold",
	  .label = N_("_Bold"),
	  .accelerator = "<control>b",	/* ALSO "<control>2" */
	  .tooltip = N_("Bold"),
	  .callback = G_CALLBACK (cb_font_bold),
	  .is_active = FALSE },
	{ .name = "FontItalic",
	  .toggle = TRUE,
	  .icon = "format-text-italic",
	  .label = N_("_Italic"),
	  .accelerator = "<control>i",	/* ALSO "<control>3" */
	  .tooltip = N_("Italic"),
	  .callback = G_CALLBACK (cb_font_italic),
	  .is_active = FALSE },
	{ .name = "FontUnderline",
	  .toggle = TRUE,
	  .icon = "format-text-underline",
	  .label = N_("_Underline"),
	  .accelerator = "<control>u",	/* ALSO "<control>4" */
	  .tooltip = N_("Underline"),
	  .callback = G_CALLBACK (cb_font_underline),
	  .is_active = FALSE },
	{ .name = "FontDoubleUnderline",
	  .toggle = TRUE,
	  .icon = "stock_text_underlined-double",	/* from icon theme */
	  .label = N_("_Double Underline"),
	  .accelerator = "<control><shift>d",
	  .tooltip = N_("Double Underline"),
	  .callback = G_CALLBACK (cb_font_double_underline),
	  .is_active = FALSE },
	{ .name = "FontSingleLowUnderline",
	  .toggle = TRUE,
	  .label = N_("_Single Low Underline"),
	  .accelerator = "<control><shift>l",
	  .tooltip = N_("Single Low Underline"),
	  .callback = G_CALLBACK (cb_font_underline_low),
	  .is_active = FALSE },
	{ .name = "FontDoubleLowUnderline",
	  .toggle = TRUE,
	  .label = N_("Double _Low Underline"),
	  .tooltip = N_("Double Low Underline"),
	  .callback = G_CALLBACK (cb_font_double_underline_low),
	  .is_active = FALSE },
	{ .name = "FontStrikeThrough",
	  .toggle = TRUE,
	  .icon = "format-text-strikethrough",
	  .label = N_("_Strikethrough"),
	  .accelerator = "<control>5",
	  .tooltip = N_("Strikethrough"),
	  .callback = G_CALLBACK (cb_font_strikethrough),
	  .is_active = FALSE },
	{ .name = "FontSuperscript",
	  .toggle = TRUE,
	  .icon = "gnumeric-superscript",
	  .label = N_("Su_perscript"),
	  .accelerator = "<control>asciicircum",
	  .tooltip = N_("Superscript"),
	  .callback = G_CALLBACK (cb_font_superscript),
	  .is_active = FALSE },
	{ .name = "FontSubscript",
	  .toggle = TRUE,
	  .icon = "gnumeric-subscript",
	  .label = N_("Subscrip_t"),
	  .accelerator = "<control>underscore",
	  .tooltip = N_("Subscript"),
	  .callback = G_CALLBACK (cb_font_subscript), .is_active = FALSE }
};

/****************************************************************************/

static GOActionComboPixmapsElement const halignment_combo_info[] = {
	{ N_("Align left"),		"format-justify-left",		GNM_HALIGN_LEFT },
	{ N_("Center horizontally"),	"format-justify-center",	GNM_HALIGN_CENTER },
	{ N_("Align right"),		"format-justify-right",		GNM_HALIGN_RIGHT },
	{ N_("Fill horizontally"),	"gnumeric-format-halign-fill",	GNM_HALIGN_FILL },
	{ N_("Justify horizontally"),	"format-justify-fill",		GNM_HALIGN_JUSTIFY },
	{ N_("Distributed"),		"gnumeric-format-halign-distributed",	GNM_HALIGN_DISTRIBUTED },
	{ N_("Center horizontally across the selection"),
					"gnumeric-center-across-selection", GNM_HALIGN_CENTER_ACROSS_SELECTION },
	{ N_("Align numbers right, and text left"),
					"gnumeric-format-halign-general",	GNM_HALIGN_GENERAL },
	{ NULL, NULL }
};
static GOActionComboPixmapsElement const valignment_combo_info[] = {
	{ N_("Align top"),		"gnumeric-format-valign-top",		GNM_VALIGN_TOP },
	{ N_("Center vertically"),	"gnumeric-format-valign-center",	GNM_VALIGN_CENTER },
	{ N_("Align bottom"),		"gnumeric-format-valign-bottom",	GNM_VALIGN_BOTTOM },
	{ N_("Justify"),		"gnumeric-format-valign-justify",	GNM_VALIGN_JUSTIFY },
	// Reuse "center" icon as I don't know what this one is
	{ N_("Align distributed"),	"gnumeric-format-valign-center",	GNM_VALIGN_DISTRIBUTED },
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
		      "visible-horizontal", FALSE,
		      NULL);
	g_signal_connect (G_OBJECT (wbcg->halignment),
		"activate",
		G_CALLBACK (cb_halignment_activated), wbcg);
	gnm_action_group_add_action (wbcg->actions, GTK_ACTION (wbcg->halignment));

	wbcg->valignment = go_action_combo_pixmaps_new ("VAlignmentSelector",
						       valignment_combo_info, 5, 1);
	g_object_set (G_OBJECT (wbcg->valignment),
		      "label", _("Vertical Alignment"),
		      "tooltip", _("Vertical Alignment"),
		      NULL);
	g_signal_connect (G_OBJECT (wbcg->valignment),
		"activate",
		G_CALLBACK (cb_valignment_activated), wbcg);
	gnm_action_group_add_action (wbcg->actions, GTK_ACTION (wbcg->valignment));
}

/****************************************************************************/

static void
cb_custom_color_created (GOActionComboColor *caction, GtkWidget *dialog, WBCGtk *wbcg)
{
	wbc_gtk_attach_guru (wbcg, dialog);
	wbcg_set_transient (wbcg, GTK_WINDOW (dialog));
}

static void
cb_fore_color_changed (GOActionComboColor *a, WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	GnmStyle *mstyle;
	GOColor   c;
	gboolean  is_default;

	if (wbcg->updating_ui)
		return;
	c = go_action_combo_color_get_color (a, &is_default);

	if (wbcg_is_editing (wbcg)) {
		wbcg_edit_add_markup (wbcg, go_color_to_pango (c, TRUE));
		return;
	}

	mstyle = gnm_style_new ();
	gnm_style_set_font_color (mstyle, is_default
		? style_color_auto_font ()
		: gnm_color_new_go (c));
	cmd_selection_format (wbc, mstyle, NULL, _("Set Foreground Color"));
}

static void
wbc_gtk_init_color_fore (WBCGtk *gtk)
{
	GnmColor *sc_auto_font = style_color_auto_font ();
	GOColor default_color = sc_auto_font->go_color;
	style_color_unref (sc_auto_font);

	gtk->fore_color = go_action_combo_color_new ("ColorFore", "gnumeric-font",
		_("Automatic"),	default_color, NULL); /* set group to view */
	go_action_combo_color_set_allow_alpha (gtk->fore_color, TRUE);
	g_object_set (G_OBJECT (gtk->fore_color),
		      "label", _("Foreground"),
		      "tooltip", _("Foreground"),
		      NULL);
	g_signal_connect (G_OBJECT (gtk->fore_color),
		"combo-activate",
		G_CALLBACK (cb_fore_color_changed), gtk);
	g_signal_connect (G_OBJECT (gtk->fore_color),
		"display-custom-dialog",
		G_CALLBACK (cb_custom_color_created), gtk);
	gnm_action_group_add_action (gtk->font_actions,
		GTK_ACTION (gtk->fore_color));
}

static void
cb_back_color_changed (GOActionComboColor *a, WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	GnmStyle *mstyle;
	GOColor   c;
	gboolean  is_default;

	if (wbcg->updating_ui)
		return;

	c = go_action_combo_color_get_color (a, &is_default);

	mstyle = gnm_style_new ();
	if (!is_default) {
		/* We need to have a pattern of at least solid to draw a background colour */
		if (!gnm_style_is_element_set  (mstyle, MSTYLE_PATTERN) ||
		    gnm_style_get_pattern (mstyle) < 1)
			gnm_style_set_pattern (mstyle, 1);

		gnm_style_set_back_color (mstyle, gnm_color_new_go (c));
	} else
		gnm_style_set_pattern (mstyle, 0);	/* Set background to NONE */
	cmd_selection_format (wbc, mstyle, NULL, _("Set Background Color"));
}

static void
wbc_gtk_init_color_back (WBCGtk *gtk)
{
	gtk->back_color = go_action_combo_color_new ("ColorBack", "gnumeric-bucket",
		_("Clear Background"), 0, NULL);
	g_object_set (G_OBJECT (gtk->back_color),
		      "label", _("Background"),
		      "tooltip", _("Background"),
		      NULL);
	g_object_connect (G_OBJECT (gtk->back_color),
		"signal::combo-activate", G_CALLBACK (cb_back_color_changed), gtk,
		"signal::display-custom-dialog", G_CALLBACK (cb_custom_color_created), gtk,
		NULL);
	gnm_action_group_add_action (gtk->actions, GTK_ACTION (gtk->back_color));
}

/****************************************************************************/

static GOActionComboPixmapsElement const border_combo_info[] = {
	{ N_("Left"),			"gnumeric-format-border-left",			11 },
	{ N_("Clear Borders"),		"gnumeric-format-border-none",			12 },
	{ N_("Right"),			"gnumeric-format-border-right",			13 },

	{ N_("All Borders"),		"gnumeric-format-border-all",			21 },
	{ N_("Outside Borders"),	"gnumeric-format-border-outside",		22 },
	{ N_("Thick Outside Borders"),	"gnumeric-format-border-thick-outside",		23 },

	{ N_("Bottom"),			"gnumeric-format-border-bottom",		31 },
	{ N_("Double Bottom"),		"gnumeric-format-border-double-bottom",		32 },
	{ N_("Thick Bottom"),		"gnumeric-format-border-thick-bottom",		33 },

	{ N_("Top and Bottom"),		"gnumeric-format-border-top-n-bottom",		41 },
	{ N_("Top and Double Bottom"),	"gnumeric-format-border-top-n-double-bottom",	42 },
	{ N_("Top and Thick Bottom"),	"gnumeric-format-border-top-n-thick-bottom",	43 },

	{ NULL, NULL}
};

static void
cb_border_activated (GOActionComboPixmaps *a, WorkbookControl *wbc)
{
	Sheet *sheet = wb_control_cur_sheet (wbc);
	GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX];
	int i;
	int index = go_action_combo_pixmaps_get_selected (a, NULL);

	/* Init the list */
	for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
		borders[i] = NULL;

	switch (index) {
	case 11 : /* left */
		borders[GNM_STYLE_BORDER_LEFT] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
			 sheet_style_get_auto_pattern_color (sheet),
			 gnm_style_border_get_orientation (GNM_STYLE_BORDER_LEFT));
		break;

	case 12 : /* none */
		for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
			borders[i] = gnm_style_border_ref (gnm_style_border_none ());
		break;

	case 13 : /* right */
		borders[GNM_STYLE_BORDER_RIGHT] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
			 sheet_style_get_auto_pattern_color (sheet),
			 gnm_style_border_get_orientation (GNM_STYLE_BORDER_RIGHT));
		break;

	case 21 : /* all */
		for (i = GNM_STYLE_BORDER_HORIZ; i <= GNM_STYLE_BORDER_VERT; ++i)
			borders[i] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
				sheet_style_get_auto_pattern_color (sheet),
				gnm_style_border_get_orientation (i));
		/* fall through */

	case 22 : /* outside */
		for (i = GNM_STYLE_BORDER_TOP; i <= GNM_STYLE_BORDER_RIGHT; ++i)
			borders[i] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
				sheet_style_get_auto_pattern_color (sheet),
				gnm_style_border_get_orientation (i));
		break;

	case 23 : /* thick_outside */
		for (i = GNM_STYLE_BORDER_TOP; i <= GNM_STYLE_BORDER_RIGHT; ++i)
			borders[i] = gnm_style_border_fetch (GNM_STYLE_BORDER_THICK,
				sheet_style_get_auto_pattern_color (sheet),
				gnm_style_border_get_orientation (i));
		break;

	case 41 : /* top_n_bottom */
	case 42 : /* top_n_double_bottom */
	case 43 : /* top_n_thick_bottom */
		borders[GNM_STYLE_BORDER_TOP] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
			sheet_style_get_auto_pattern_color (sheet),
			gnm_style_border_get_orientation (GNM_STYLE_BORDER_TOP));
	    /* Fall through */

	case 31 : /* bottom */
	case 32 : /* double_bottom */
	case 33 : /* thick_bottom */
	{
		int const tmp = index % 10;
		GnmStyleBorderType const t =
		    (tmp == 1) ? GNM_STYLE_BORDER_THIN :
		    (tmp == 2) ? GNM_STYLE_BORDER_DOUBLE
		    : GNM_STYLE_BORDER_THICK;

		borders[GNM_STYLE_BORDER_BOTTOM] = gnm_style_border_fetch (t,
			sheet_style_get_auto_pattern_color (sheet),
			gnm_style_border_get_orientation (GNM_STYLE_BORDER_BOTTOM));
		break;
	}

	default:
		g_warning ("Unknown border preset selected (%d)", index);
		return;
	}

	cmd_selection_format (wbc, NULL, borders, _("Set Borders"));
}

static void
wbc_gtk_init_borders (WBCGtk *wbcg)
{
	wbcg->borders = go_action_combo_pixmaps_new ("BorderSelector", border_combo_info, 3, 4);
	g_object_set (G_OBJECT (wbcg->borders),
		      "label", _("Borders"),
		      "tooltip", _("Borders"),
		      NULL);
#if 0
	go_combo_pixmaps_select (wbcg->borders, 1); /* default to none */
#endif
	g_signal_connect (G_OBJECT (wbcg->borders),
		"combo-activate",
		G_CALLBACK (cb_border_activated), wbcg);
	gnm_action_group_add_action (wbcg->actions, GTK_ACTION (wbcg->borders));
}

/****************************************************************************/

static void
cb_chain_sensitivity (GtkAction *src, G_GNUC_UNUSED GParamSpec *pspec,
		      GtkAction *action)
{
	gboolean old_val = gtk_action_get_sensitive (action);
	gboolean new_val = gtk_action_get_sensitive (src);
	if ((new_val != 0) == (old_val != 0))
		return;
	if (new_val)
		gtk_action_connect_accelerator (action);
	else
		gtk_action_disconnect_accelerator (action);
	g_object_set (action, "sensitive", new_val, NULL);
}


static void
create_undo_redo (GOActionComboStack **haction, char const *hname,
		  GCallback hcb,
		  GtkAction **vaction, char const *vname,
		  GCallback vcb,
		  WBCGtk *gtk,
		  char const *tooltip,
		  char const *icon_name,
		  char const *accel, const char *alt_accel,
		  const char *label)
{
	*haction = g_object_new
		(go_action_combo_stack_get_type (),
		 "name", hname,
		 "tooltip", tooltip,
		 "icon-name", icon_name,
		 "sensitive", FALSE,
		 "visible-vertical", FALSE,
		 "label", label,
		 NULL);
	gtk_action_group_add_action_with_accel
		(gtk->semi_permanent_actions,
		 GTK_ACTION (*haction),
		 accel);
	g_signal_connect (G_OBJECT (*haction), "activate", hcb, gtk);

	*vaction = g_object_new
		(GTK_TYPE_ACTION,
		 "name", vname,
		 "tooltip", tooltip,
		 "icon-name", icon_name,
		 "sensitive", FALSE,
		 "visible-horizontal", FALSE,
		 "label", label,
		 NULL);
	gtk_action_group_add_action_with_accel
		(gtk->semi_permanent_actions,
		 GTK_ACTION (*vaction),
		 alt_accel);
	g_signal_connect_swapped (G_OBJECT (*vaction), "activate", vcb, gtk);

	g_signal_connect (G_OBJECT (*haction), "notify::sensitive",
		G_CALLBACK (cb_chain_sensitivity), *vaction);
}


static void
cb_undo_activated (GOActionComboStack *a, WorkbookControl *wbc)
{
	unsigned n = workbook_find_command (wb_control_get_workbook (wbc), TRUE,
		go_action_combo_stack_selection (a));
	while (n-- > 0)
		command_undo (wbc);
}

static void
cb_redo_activated (GOActionComboStack *a, WorkbookControl *wbc)
{
	unsigned n = workbook_find_command (wb_control_get_workbook (wbc), FALSE,
		go_action_combo_stack_selection (a));
	while (n-- > 0)
		command_redo (wbc);
}

static void
wbc_gtk_init_undo_redo (WBCGtk *gtk)
{
	create_undo_redo (
		&gtk->redo_haction, "Redo", G_CALLBACK (cb_redo_activated),
		&gtk->redo_vaction, "VRedo", G_CALLBACK (command_redo),
		gtk, _("Redo the undone action"),
		"edit-redo", "<control>y", "<control><shift>z",
		_("Redo"));
	create_undo_redo (
		&gtk->undo_haction, "Undo", G_CALLBACK (cb_undo_activated),
		&gtk->undo_vaction, "VUndo", G_CALLBACK (command_undo),
		gtk, _("Undo the last action"),
		"edit-undo", "<control>z", NULL,
		_("Undo"));
}

/****************************************************************************/

static GNM_ACTION_DEF (cb_zoom_activated)
{
	WorkbookControl *wbc = (WorkbookControl *)wbcg;
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char const *new_zoom;
	int factor;
	char *end;

	if (sheet == NULL || wbcg->updating_ui || wbcg->snotebook == NULL)
		return;

	new_zoom = go_action_combo_text_get_entry (wbcg->zoom_haction);

	errno = 0; /* strtol sets errno, but does not clear it.  */
	factor = strtol (new_zoom, &end, 10);
	if (new_zoom != end && errno != ERANGE && factor == (gnm_float)factor)
		/* The GSList of sheet passed to cmd_zoom will be freed by cmd_zoom,
		 * and the sheet will force an update of the zoom combo to keep the
		 * display consistent
		 */
		cmd_zoom (wbc, g_slist_append (NULL, sheet), factor / 100.);
}

static GNM_ACTION_DEF (cb_vzoom_activated)
{
	dialog_zoom (wbcg, wbcg_cur_sheet (wbcg));
}

static void
wbc_gtk_init_zoom (WBCGtk *wbcg)
{
#warning TODO : Add zoom to selection
	static char const * const preset_zoom [] = {
		"200%",
		"150%",
		"100%",
		"75%",
		"50%",
		"25%",
		NULL
	};
	int i;

	/* ----- horizontal ----- */

	wbcg->zoom_haction =
		g_object_new (go_action_combo_text_get_type (),
			      "name", "Zoom",
			      "label", _("_Zoom"),
			      "visible-vertical", FALSE,
			      "tooltip", _("Zoom"),
			      "stock-id", "zoom-in",
			      NULL);
	go_action_combo_text_set_width (wbcg->zoom_haction, "10000%");
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		go_action_combo_text_add_item (wbcg->zoom_haction,
					       preset_zoom[i]);

	g_signal_connect (G_OBJECT (wbcg->zoom_haction),
		"activate",
		G_CALLBACK (cb_zoom_activated), wbcg);
	gnm_action_group_add_action (wbcg->actions,
				     GTK_ACTION (wbcg->zoom_haction));

	/* ----- vertical ----- */

	wbcg->zoom_vaction =
		g_object_new (GTK_TYPE_ACTION,
			      "name", "VZoom",
			      "label", _("_Zoom"),
			      "tooltip", _("Zoom"),
			      "icon-name", "zoom-in",
			      "visible-horizontal", FALSE,
			      NULL);
	g_signal_connect (G_OBJECT (wbcg->zoom_vaction),
			  "activate",
			  G_CALLBACK (cb_vzoom_activated), wbcg);
	gnm_action_group_add_action (wbcg->actions,
				     GTK_ACTION (wbcg->zoom_vaction));

	/* ----- chain ----- */

	g_signal_connect (G_OBJECT (wbcg->zoom_haction), "notify::sensitive",
		G_CALLBACK (cb_chain_sensitivity), wbcg->zoom_vaction);
}

/****************************************************************************/

typedef struct { GtkAction base; } GnmFontAction;
typedef struct { GtkActionClass base; } GnmFontActionClass;

static PangoFontDescription *
gnm_font_action_get_font_desc (GtkAction *act)
{
	PangoFontDescription *desc =
		g_object_get_data (G_OBJECT (act), "font-data");
	return desc;
}

void
wbcg_font_action_set_font_desc (GtkAction *act, PangoFontDescription *desc)
{
	PangoFontDescription *old_desc;
	GSList *p;

	old_desc = g_object_get_data (G_OBJECT (act), "font-data");
	if (!old_desc) {
		old_desc = pango_font_description_new ();
		g_object_set_data_full (G_OBJECT (act),
					"font-data", old_desc,
					(GDestroyNotify)pango_font_description_free);
	}
	pango_font_description_merge (old_desc, desc, TRUE);

	for (p = gtk_action_get_proxies (act); p; p = p->next) {
		GtkWidget *w = p->data;
		GtkWidget *child;
		GtkFontChooser *chooser;

		if (!GTK_IS_BIN (w))
			continue;

		child = gtk_bin_get_child (GTK_BIN (w));
		if (!GTK_IS_FONT_CHOOSER (child))
			continue;

		chooser = GTK_FONT_CHOOSER (child);
		gtk_font_chooser_set_font_desc (chooser, old_desc);
	}
}

static void
cb_font_set (GtkFontChooser *chooser, GtkAction *act)
{
	PangoFontDescription *desc = gtk_font_chooser_get_font_desc (chooser);
	wbcg_font_action_set_font_desc (act, desc);
	pango_font_description_free (desc);
	gtk_action_activate (act);
}

static void
cb_font_button_screen_changed (GtkWidget *widget)
{
/* Doesn't look right */
#if 0
	GdkScreen *screen = gtk_widget_get_screen (widget);

	if (screen) {
		int w = gnm_widget_measure_string (widget,
						   "XXMonospace | 99XX");
		gtk_widget_set_size_request (widget, w, -1);
	}
#endif
}

/* Filter to ignore non-scalable fonts. */
static gboolean
cb_font_filter (G_GNUC_UNUSED const PangoFontFamily *family,
		const PangoFontFace *face_,
		gpointer user)
{
	PangoFontFace *face = (PangoFontFace*)face_;
	int n_sizes;
	int *sizes = NULL;
	static int debug = -1;

	pango_font_face_list_sizes (face, &sizes, &n_sizes);
	g_free (sizes);

	if (debug == -1)
		debug = gnm_debug_flag ("fonts");

	if (n_sizes > 0 && debug) {
		PangoFontDescription *desc = pango_font_face_describe (face);
		char *s = pango_font_description_to_string (desc);
		g_printerr ("Ignoring bitmap face %s\n", s);
		g_free (s);
		pango_font_description_free (desc);
	}

	return n_sizes == 0;
}

static GtkWidget *
gnm_font_action_create_tool_item (GtkAction *action)
{
	GtkWidget *item = g_object_new
		(GTK_TYPE_TOOL_ITEM,
		 NULL);
	GtkWidget *but = g_object_new
		(gnm_font_button_get_type(),
		 "name", "font",
		 "dialog-type", GO_TYPE_FONT_SEL_DIALOG,
		 "show-preview-entry", TRUE,
		 "show-style", FALSE,
		 "relief", gtk_tool_item_get_relief_style (GTK_TOOL_ITEM (item)),
		 "focus-on-click", FALSE,
		 NULL);
	if (0) gtk_font_chooser_set_filter_func (GTK_FONT_CHOOSER (but),
					  cb_font_filter,
					  NULL,
					  NULL);
	gtk_widget_show_all (but);
	gtk_container_add (GTK_CONTAINER (item), but);
	g_signal_connect (but,
			  "font-set", G_CALLBACK (cb_font_set),
			  action);
	g_signal_connect (but,
			  "screen-changed",
			  G_CALLBACK (cb_font_button_screen_changed),
			  action);
	return item;
}

static void
gnm_font_action_class_init (GObjectClass *gobject_class)
{
	GtkActionClass *act = GTK_ACTION_CLASS (gobject_class);

	act->toolbar_item_type = GTK_TYPE_MENU_TOOL_BUTTON;
	act->create_tool_item = gnm_font_action_create_tool_item;
}

static
GSF_CLASS (GnmFontAction, gnm_font_action,
	   gnm_font_action_class_init, NULL, GTK_TYPE_ACTION)
#if 0
	;
#endif
#define GNM_FONT_ACTION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), gnm_font_action_get_type(), GnmFontAction))

static void
cb_font_changed (GtkAction *act, WBCGtk *gtk)
{
	PangoFontDescription *desc = gnm_font_action_get_font_desc (act);
	const char *family = pango_font_description_get_family (desc);
	int size = pango_font_description_get_size (desc);

	/*
	 * Ignore events during destruction.  This is an attempt at avoiding
	 * https://bugzilla.redhat.com/show_bug.cgi?id=803904 for which we
	 * blame gtk.
	 */
	if (gtk->snotebook == NULL)
		return;

	if (wbcg_is_editing (WBC_GTK (gtk))) {
		wbcg_edit_add_markup (WBC_GTK (gtk),
				      pango_attr_family_new (family));
		wbcg_edit_add_markup (WBC_GTK (gtk),
				      pango_attr_size_new (size));
	} else {
		GnmStyle *style = gnm_style_new ();
		char *font_name = pango_font_description_to_string (desc);
		char *title = g_strdup_printf (_("Setting Font %s"), font_name);
		g_free (font_name);

		gnm_style_set_font_name (style, family);
		gnm_style_set_font_size (style, size / (double)PANGO_SCALE);

		cmd_selection_format (GNM_WBC (gtk), style, NULL, title);
		g_free (title);
	}
}

static void
cb_font_name_vaction_response (GtkDialog *dialog,
			       gint       response_id,
			       GtkAction *act)
{
	WBCGtk *wbcg = g_object_get_data (G_OBJECT (act), "wbcg");

	if (response_id == GTK_RESPONSE_OK) {
		PangoFontDescription *desc = gtk_font_chooser_get_font_desc
			(GTK_FONT_CHOOSER (dialog));
		wbcg_font_action_set_font_desc (act, desc);
		pango_font_description_free (desc);
		cb_font_changed (act, wbcg);
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}


static void
cb_font_name_vaction_clicked (GtkAction *act, WBCGtk *wbcg)
{
	GtkFontChooser *font_dialog;
	const char *key = "font-name-dialog";

	if (gnm_dialog_raise_if_exists (wbcg, key))
		return;

	font_dialog = g_object_new (GO_TYPE_FONT_SEL_DIALOG, NULL);
	gtk_font_chooser_set_font_desc (font_dialog,
					gnm_font_action_get_font_desc (act));
	g_signal_connect (font_dialog, "response",
			  G_CALLBACK (cb_font_name_vaction_response),
			  act);

	gtk_window_present (GTK_WINDOW (font_dialog));

	gnm_keyed_dialog (wbcg, GTK_WINDOW (font_dialog), key);
}

static GtkAction *
wbc_gtk_init_font_name (WBCGtk *gtk, gboolean horiz)
{
	GtkAction *act = g_object_new
		(horiz ? gnm_font_action_get_type () : GTK_TYPE_ACTION,
		 "label", _("Font"),
		 "visible-vertical", !horiz,
		 "visible-horizontal", horiz,
		 "name", (horiz ? "FontName" : "VFontName"),
		 "tooltip", _("Change font"),
		 "icon-name", "gnumeric-font",
		 NULL);

	g_object_set_data (G_OBJECT (act), "wbcg", gtk);

	g_signal_connect (G_OBJECT (act),
			  "activate",
			  (horiz
			   ? G_CALLBACK (cb_font_changed)
			   : G_CALLBACK (cb_font_name_vaction_clicked)),
			  gtk);

	gnm_action_group_add_action (gtk->font_actions, act);

	return act;
}

/****************************************************************************/

static void
list_actions (GtkActionGroup *group)
{
	GList *actions, *l;

	if (!group)
		return;

	actions = gtk_action_group_list_actions (group);
	for (l = actions; l; l = l->next) {
		GtkAction *act = l->data;
		const char *name = gtk_action_get_name (act);
		g_printerr ("Action %s\n", name);
	}

	g_list_free (actions);
}

void
wbc_gtk_init_actions (WBCGtk *wbcg)
{
	static struct {
		char const *name;
		gboolean    is_font;
		unsigned    offset;
	} const toggles[] = {
		{ "FontBold",		   TRUE, G_STRUCT_OFFSET (WBCGtk, font.bold) },
		{ "FontItalic",		   TRUE, G_STRUCT_OFFSET (WBCGtk, font.italic) },
		{ "FontUnderline",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.underline) },
		{ "FontDoubleUnderline",   TRUE, G_STRUCT_OFFSET (WBCGtk, font.d_underline) },
		{ "FontSingleLowUnderline",TRUE, G_STRUCT_OFFSET (WBCGtk, font.sl_underline) },
		{ "FontDoubleLowUnderline",TRUE, G_STRUCT_OFFSET (WBCGtk, font.dl_underline) },
		{ "FontSuperscript",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.superscript) },
		{ "FontSubscript",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.subscript) },
		{ "FontStrikeThrough",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.strikethrough) },

		{ "AlignLeft",		   FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.left) },
		{ "AlignCenter",	   FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.center) },
		{ "AlignRight",		   FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.right) },
		{ "CenterAcrossSelection", FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.center_across_selection) },
		{ "AlignTop",		   FALSE, G_STRUCT_OFFSET (WBCGtk, v_align.top) },
		{ "AlignVCenter",	   FALSE, G_STRUCT_OFFSET (WBCGtk, v_align.center) },
		{ "AlignBottom",	   FALSE, G_STRUCT_OFFSET (WBCGtk, v_align.bottom) }
	};
	unsigned i;

	wbcg->permanent_actions = gtk_action_group_new ("PermanentActions");
	wbcg->actions = gtk_action_group_new ("Actions");
	wbcg->font_actions = gtk_action_group_new ("FontActions");
	wbcg->data_only_actions = gtk_action_group_new ("DataOnlyActions");
	wbcg->semi_permanent_actions = gtk_action_group_new ("SemiPermanentActions");

	gnm_action_group_add_actions (wbcg->permanent_actions,
		permanent_actions, G_N_ELEMENTS (permanent_actions), wbcg);
	gnm_action_group_add_actions (wbcg->actions,
		actions, G_N_ELEMENTS (actions), wbcg);
	gnm_action_group_add_actions (wbcg->font_actions,
		font_actions, G_N_ELEMENTS (font_actions), wbcg);
	gnm_action_group_add_actions (wbcg->data_only_actions,
		data_only_actions, G_N_ELEMENTS (data_only_actions), wbcg);
	gnm_action_group_add_actions (wbcg->semi_permanent_actions,
		semi_permanent_actions, G_N_ELEMENTS (semi_permanent_actions), wbcg);

	wbc_gtk_init_alignments (wbcg);
	wbc_gtk_init_color_fore (wbcg);
	wbc_gtk_init_color_back (wbcg);
	wbc_gtk_init_borders (wbcg);
	wbc_gtk_init_undo_redo (wbcg);
	wbc_gtk_init_zoom (wbcg);
	wbcg->font_name_haction = wbc_gtk_init_font_name (wbcg, TRUE);
	wbcg->font_name_vaction = wbc_gtk_init_font_name (wbcg, FALSE);

	for (i = G_N_ELEMENTS (toggles); i-- > 0 ; ) {
		GtkAction *act = wbcg_find_action (wbcg, toggles[i].name);
		G_STRUCT_MEMBER (GtkToggleAction *, wbcg, toggles[i].offset) =
			(GtkToggleAction*) (act);
	}

	if (gnm_debug_flag ("actions")) {
		list_actions (wbcg->permanent_actions);
		list_actions (wbcg->actions);
		list_actions (wbcg->font_actions);
		list_actions (wbcg->data_only_actions);
		list_actions (wbcg->semi_permanent_actions);
		list_actions (wbcg->file_history.actions);
		list_actions (wbcg->toolbar.actions);
		list_actions (wbcg->windows.actions);
		list_actions (wbcg->templates.actions);
	}
}
