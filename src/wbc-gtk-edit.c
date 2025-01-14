/*
 * wbc-gtk-edit.c: Keeps track of the cell editing process.
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2000-2005 Miguel de Icaza (miguel@novell.com)
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
 */
#include <gnumeric-config.h>
#include <gnumeric.h>

#include <gnm-pane-impl.h>
#include <wbc-gtk-impl.h>
#include <workbook-view.h>
#include <workbook-priv.h>
#include <application.h>
#include <clipboard.h>
#include <complete-sheet.h>
#include <commands.h>
#include <gnumeric-conf.h>
#include <mstyle.h>
#include <style-color.h>
#include <sheet-control-gui-priv.h>
#include <sheet-style.h>
#include <sheet-view.h>
#include <sheet.h>
#include <cell.h>
#include <expr.h>
#include <gnm-format.h>
#include <number-match.h>
#include <parse-util.h>
#include <ranges.h>
#include <selection.h>
#include <validation.h>
#include <value.h>
#include <widgets/gnm-expr-entry.h>
#include <gui-util.h>
#include <command-context.h>

#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define GNM_RESPONSE_REMOVE (-1000)

/*
 * Shuts down the auto completion engine
 */
void
wbcg_auto_complete_destroy (WBCGtk *wbcg)
{
	g_free (wbcg->auto_complete_text);
	wbcg->auto_complete_text = NULL;

	if (wbcg->edit_line.signal_changed) {
		g_signal_handler_disconnect (wbcg_get_entry (wbcg),
					     wbcg->edit_line.signal_changed);
		wbcg->edit_line.signal_changed = 0;
	}

	if (wbcg->auto_complete != NULL) {
		g_object_unref (wbcg->auto_complete);
		wbcg->auto_complete = NULL;
	}

	wbcg->auto_completing = FALSE;
}

/**
 * wbcg_edit_finish:
 * @wbcg: #WBCGtk
 * @result: what should we do with the content
 * @showed_dialog: (optional) (out): indicates whether a dialog was displayed.
 *
 * Returns: %TRUE if editing completed successfully, or we were no editing.
 **/
gboolean
wbcg_edit_finish (WBCGtk *wbcg, WBCEditResult result,
		  gboolean *showed_dialog)
{
	Sheet *sheet;
	SheetView *sv;
	WorkbookControl *wbc;
	WorkbookView	*wbv;

	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);

	wbc = GNM_WBC (wbcg);
	wbv = wb_control_view (wbc);

	wbcg_focus_cur_scg (wbcg);

	gnm_expr_entry_close_tips (wbcg_get_entry_logical (wbcg));

	if (showed_dialog != NULL)
		*showed_dialog = FALSE;

	/* Remove the range selection cursor if it exists */
	if (NULL != wbcg->rangesel)
		scg_rangesel_stop (wbcg->rangesel, result == WBC_EDIT_REJECT);

	if (!wbcg_is_editing (wbcg)) {
		/* We may have a guru up even if we are not editing. remove it.
		 * Do NOT remove until later it if we are editing, it is possible
		 * that we may want to continue editing.
		 */
		if (wbcg->edit_line.guru != NULL) {
			GtkWidget *w = wbcg->edit_line.guru;
			wbc_gtk_detach_guru (wbcg);
			gtk_widget_destroy (w);
		}

		return TRUE;
	}

	g_return_val_if_fail (IS_SHEET (wbcg->editing_sheet), TRUE);

	sheet = wbcg->editing_sheet;
	sv = sheet_get_view (sheet, wbv);

	/* Save the results before changing focus */
	if (result != WBC_EDIT_REJECT) {
		ValidationStatus valid = GNM_VALIDATION_STATUS_VALID;
		char *free_txt = NULL;
		char const *txt;
		GnmStyle const *mstyle;
		char const *expr_txt = NULL;
		GOFormat const *fmt;
		GnmValue *value;
		GOUndo *u = NULL;
		GSList	*selection = selection_get_ranges (sv, FALSE);
		GnmParsePos    pp;
		GnmExprTop const *texpr = NULL;

		parse_pos_init_editpos (&pp, sv);

		/* Array only works on single range.  */
		if (result == WBC_EDIT_ACCEPT_ARRAY &&
		    (selection == NULL || selection->next != NULL))
			result = WBC_EDIT_ACCEPT_RANGE;

		/******* Check whether we would split a range ********/

		switch (result) {
		case (WBC_EDIT_ACCEPT_RANGE):
		case (WBC_EDIT_ACCEPT_ARRAY): {
			if (sheet_ranges_split_region (sheet, selection,
						       GO_CMD_CONTEXT (wbc), _("Set Text"))) {
				range_fragment_free (selection);
				if (showed_dialog != NULL)
					*showed_dialog = TRUE;
				return FALSE;
			}

			if (result == WBC_EDIT_ACCEPT_ARRAY &&
			    sheet_range_contains_merges_or_arrays
			    	(sheet, selection->data,
				 GO_CMD_CONTEXT (wbc), _("Set Text"),
				 TRUE, FALSE)) {
				range_fragment_free (selection);
				if (showed_dialog != NULL)
					*showed_dialog = TRUE;
				return FALSE;
			}

			break;
		}
		case (WBC_EDIT_ACCEPT_WO_AC):
		case (WBC_EDIT_ACCEPT): {
			GnmCell const *cell = sheet_cell_get
				(sheet, sv->edit_pos.col, sv->edit_pos.row);
			if (gnm_cell_is_nonsingleton_array (cell)) {
				gnm_cmd_context_error_splits_array (GO_CMD_CONTEXT (wbc),
								    _("Set Text"), NULL);
				if (showed_dialog != NULL)
					*showed_dialog = TRUE;
				range_fragment_free (selection);
				return FALSE;
			}
			break;
		}
		case (WBC_EDIT_REJECT):
		default:
			/* We should not be able to get here! */
			break;
		}


		/******* Check whether the range is locked ********/

		switch (result) {
		case (WBC_EDIT_ACCEPT_RANGE):
		case (WBC_EDIT_ACCEPT_ARRAY): {
			if (cmd_selection_is_locked_effective (sheet, selection, wbc,
							       _("Set Text"))) {
				range_fragment_free (selection);
				if (showed_dialog != NULL)
					*showed_dialog = TRUE;
				return FALSE;
			}
			break;
		}
		case (WBC_EDIT_ACCEPT_WO_AC):
		case (WBC_EDIT_ACCEPT): {
			GnmRange r;
			r.end = r.start = pp.eval;

			if (cmd_cell_range_is_locked_effective (sheet, &r, wbc,
							       _("Set Text"))) {
				range_fragment_free (selection);
				if (showed_dialog != NULL)
					*showed_dialog = TRUE;
				return FALSE;
			}
			break;
		}
		case (WBC_EDIT_REJECT):
		default:
			/* We should not be able to get here! */
			break;
		}
		/*****************************************************/

		txt = wbcg_edit_get_display_text (wbcg);
		mstyle = sheet_style_get (sheet, sv->edit_pos.col, sv->edit_pos.row);
		fmt = gnm_cell_get_format (sheet_cell_fetch (sheet, sv->edit_pos.col,
							     sv->edit_pos.row));

		value = format_match (txt, fmt, sheet_date_conv (sheet));
		if (value == NULL)
			expr_txt = gnm_expr_char_start_p (txt);
		else
			value_release (value);

		/* NOTE : do not modify gnm_expr_char_start_p to exclude "-"
		 * it _can_ start an expression, which is required for rangesel
		 * it just isn't an expression. */
		if (expr_txt != NULL && *expr_txt != '\0' && strcmp (expr_txt, "-")) {
			GnmExprTop const *texpr_test = NULL;
			GnmParseError  perr;

			parse_error_init (&perr);
			texpr_test = gnm_expr_parse_str (expr_txt,
							 &pp, GNM_EXPR_PARSE_DEFAULT, NULL, &perr);
			/* Try adding a single extra closing paren to see if it helps */
			if (texpr_test == NULL && perr.err != NULL &&
			    perr.err->code == PERR_MISSING_PAREN_CLOSE) {
				GnmParseError tmp_err;
				char *tmp = g_strconcat (txt, ")", NULL);
				parse_error_init (&tmp_err);
				texpr_test = gnm_expr_parse_str (gnm_expr_char_start_p (tmp),
								 &pp, GNM_EXPR_PARSE_DEFAULT,
								 NULL, &tmp_err);
				parse_error_free (&tmp_err);

				if (texpr_test != NULL) {
					txt = free_txt = tmp;
					expr_txt = gnm_expr_char_start_p (txt);
				} else
					g_free (tmp);
			}

			if (texpr_test == NULL && perr.err != NULL) {
				ValidationStatus reedit;

				/* set focus _before_ selection.  gtk2 seems to
				 * screw with selection in gtk_entry_grab_focus
				 * (no longer required now that we clear
				 * gtk-entry-select-on-focus) */
				gtk_window_set_focus (wbcg_toplevel (wbcg),
						      (GtkWidget *) wbcg_get_entry (wbcg));

				if (perr.begin_char != 0 || perr.end_char != 0) {
					int offset = expr_txt - txt;
					gtk_editable_select_region (GTK_EDITABLE (wbcg_get_entry (wbcg)),
								    offset + perr.begin_char,
								    offset + perr.end_char);
				} else
					gtk_editable_set_position (
								   GTK_EDITABLE (wbcg_get_entry (wbcg)), -1);

				reedit = wb_control_validation_msg (GNM_WBC (wbcg),
								    GNM_VALIDATION_STYLE_PARSE_ERROR, NULL,
								    perr.err->message);
				if (showed_dialog != NULL)
					*showed_dialog = TRUE;

				parse_error_free (&perr);
				if (reedit == GNM_VALIDATION_STATUS_INVALID_EDIT) {
					range_fragment_free (selection);
					return FALSE;
				}
				/* restore focus to sheet , or we'll leave edit
				 * mode only to jump right back in the new
				 * cell because it looks like someone just
				 * focused on the edit line (eg hit F2) */
				wbcg_focus_cur_scg (wbcg);
			}
			if (texpr_test != NULL)
				gnm_expr_top_unref (texpr_test);
		}

		/* We only enter an array formula if the text is a formula */
		if (result == WBC_EDIT_ACCEPT_ARRAY && !expr_txt)
			result = WBC_EDIT_ACCEPT_RANGE;

		if (result == WBC_EDIT_ACCEPT_ARRAY) {
			GnmParsePos pp_array;
			GnmRange *r = selection->data;

			parse_pos_init (&pp_array, sheet->workbook, sheet, r->start.col, r->start.row);

			if ((texpr = gnm_expr_parse_str
			     (expr_txt, &pp_array, GNM_EXPR_PARSE_DEFAULT,
			      sheet_get_conventions (sheet), NULL)) == NULL)
				result = WBC_EDIT_ACCEPT_RANGE;
		}

		/* We need to save the information that we will temporarily overwrite */
		/* We then assign the information. No need to worry about formatting */
		/* Finally we can check the validation! */

		switch (result) {
		case (WBC_EDIT_ACCEPT_RANGE): {
			GSList	*l;

			for (l = selection; l != NULL; l = l->next) {
				GnmRange *r = l->data;
				u = go_undo_combine (u,  clipboard_copy_range_undo (sheet, r));
			}
			for (l = selection; l != NULL; l = l->next) {
				GnmRange *r = l->data;
				/* We do this separately since there may be overlap between ranges */
				sheet_range_set_text (&pp, r, txt);
				valid =	gnm_validation_eval_range (wbc, sheet, &sv->edit_pos, r,
							       showed_dialog);
				if (valid != GNM_VALIDATION_STATUS_VALID)
					break;
			}
			break;
		}
		case (WBC_EDIT_ACCEPT_ARRAY): {
			GnmRange *r = selection->data;

			u = go_undo_combine (u,  clipboard_copy_range_undo (sheet, r));
			if (texpr) {
				gnm_expr_top_ref (texpr);
				gnm_cell_set_array_formula (sheet,
							    r->start.col, r->start.row,
							    r->end.col, r->end.row,
							    texpr);
				sheet_region_queue_recalc (sheet, r);
			}
			valid =	gnm_validation_eval_range (wbc, sheet, &sv->edit_pos, r,
						       showed_dialog);
			break;
		}
		case (WBC_EDIT_ACCEPT_WO_AC):
		case (WBC_EDIT_ACCEPT): {
			GnmRange r;
			GnmCell *cell;

			range_init_cellpos (&r, &sv->edit_pos);
			u = clipboard_copy_range_undo (sheet, &r);

			cell = sheet_cell_fetch (sheet,
						 sv->edit_pos.col,
						 sv->edit_pos.row);
			sheet_cell_set_text (cell, txt, wbcg->edit_line.markup);
			valid = gnm_validation_eval (wbc, mstyle, sheet, &sv->edit_pos, showed_dialog);
			break;
		}
		case (WBC_EDIT_REJECT):
		default:
			/* We should not be able to get here! */
			break;
		}

		range_fragment_free (selection);

		/* We need to rebuild the original info first. */

		go_undo_undo (u);
		g_object_unref (u);

		/* Now we can respond to our validation information */

		if (valid != GNM_VALIDATION_STATUS_VALID) {
			result = WBC_EDIT_REJECT;
			if (valid == GNM_VALIDATION_STATUS_INVALID_EDIT) {
				gtk_window_set_focus (wbcg_toplevel (wbcg),
					(GtkWidget *) wbcg_get_entry (wbcg));
				g_free (free_txt);
				if (texpr != NULL)
					gnm_expr_top_unref (texpr);
				return FALSE;
			}
		} else {
			if (result == WBC_EDIT_ACCEPT_ARRAY) {
				cmd_area_set_array_expr (wbc, sv, texpr);

			} else {
				PangoAttrList *res_markup = wbcg->edit_line.markup
					? pango_attr_list_copy (wbcg->edit_line.markup)
					: NULL;
				if (result == WBC_EDIT_ACCEPT)
					cmd_set_text (wbc, sheet, &sv->edit_pos, txt, res_markup, TRUE);
				else if (result == WBC_EDIT_ACCEPT_WO_AC)
					cmd_set_text (wbc, sheet, &sv->edit_pos, txt, res_markup, FALSE);
				else
					cmd_area_set_text (wbc, sv, txt, res_markup);
				if (res_markup)
					pango_attr_list_unref (res_markup);
			}
		}
		if (texpr != NULL)
			gnm_expr_top_unref (texpr);
		g_free (free_txt);
	} else {
		if (sv == wb_control_cur_sheet_view (wbc)) {
			/* Redraw the cell contents in case there was a span */
			GnmRange tmp; tmp.start = tmp.end = sv->edit_pos;
			sheet_range_bounding_box (sv->sheet, &tmp);
			gnm_sheet_view_redraw_range (wb_control_cur_sheet_view (wbc), &tmp);
		}

		/* Reload the entry widget with the original contents */
		wb_view_edit_line_set (wbv, wbc);
	}

	/* Stop editing */
	wbcg->editing = FALSE;
	wbcg->editing_sheet = NULL;
	wbcg->editing_cell = NULL;

	if (wbcg->edit_line.guru != NULL) {
		GtkWidget *w = wbcg->edit_line.guru;
		wbc_gtk_detach_guru (wbcg);
		gtk_widget_destroy (w);
	}

	if (wbcg->edit_line.signal_insert) {
		g_signal_handler_disconnect (wbcg_get_entry (wbcg),
					     wbcg->edit_line.signal_insert);
		wbcg->edit_line.signal_insert = 0;
	}
	if (wbcg->edit_line.signal_delete) {
		g_signal_handler_disconnect (wbcg_get_entry (wbcg),
					     wbcg->edit_line.signal_delete);
		wbcg->edit_line.signal_delete = 0;
	}
	if (wbcg->edit_line.signal_cursor_pos) {
		g_signal_handler_disconnect (wbcg_get_entry (wbcg),
					     wbcg->edit_line.signal_cursor_pos);
		wbcg->edit_line.signal_cursor_pos = 0;
	}
	if (wbcg->edit_line.signal_selection_bound) {
		g_signal_handler_disconnect (wbcg_get_entry (wbcg),
					     wbcg->edit_line.signal_selection_bound);
		wbcg->edit_line.signal_selection_bound = 0;
	}

	if (wbcg->edit_line.cell_attrs != NULL) {
		pango_attr_list_unref (wbcg->edit_line.cell_attrs);
		wbcg->edit_line.cell_attrs = NULL;
	}

	if (wbcg->edit_line.markup) {
		pango_attr_list_unref (wbcg->edit_line.markup);
		wbcg->edit_line.markup = NULL;
	}

	if (wbcg->edit_line.full_content != NULL) {
		pango_attr_list_unref (wbcg->edit_line.full_content);
		wbcg->edit_line.full_content = NULL;
	}

	if (wbcg->edit_line.cur_fmt) {
		pango_attr_list_unref (wbcg->edit_line.cur_fmt);
		wbcg->edit_line.cur_fmt = NULL;
	}

	/* set pos to 0, to ensure that if we start editing by clicking on the
	 * editline at the last position, we'll get the right style feedback */
	gtk_editable_set_position ((GtkEditable *) wbcg_get_entry (wbcg), 0);

	wb_control_update_action_sensitivity (wbc);

	if (!sheet->workbook->during_destruction) {
		/* restore focus to original sheet in case things were being selected
		 * on a different page.  Do no go through the view, rangesel is
		 * specific to the control.  */
		wb_control_sheet_focus (wbc, sheet);
		/* Only the edit sheet has an edit cursor */
		scg_edit_stop (wbcg_cur_scg (wbcg));
	}
	wbcg_auto_complete_destroy (wbcg);
	wb_control_style_feedback (wbc, NULL);	/* in case markup messed with things */

	return TRUE;
}

static void
workbook_edit_complete_notify (char const *text, void *closure)
{
	WBCGtk *wbcg = closure;

	g_free (wbcg->auto_complete_text);
	wbcg->auto_complete_text = g_strdup (text);

	scg_reload_item_edits (wbcg_cur_scg (wbcg));
}

static void
cb_entry_changed (G_GNUC_UNUSED GtkEntry *entry, WBCGtk *wbcg)
{
	char const *text;
	int text_len;
	WorkbookView *wbv = wb_control_view (GNM_WBC (wbcg));

	text = gtk_entry_get_text (wbcg_get_entry (wbcg));
	text_len = strlen (text);

	if (text_len > wbcg->auto_max_size)
		wbcg->auto_max_size = text_len;

	if (wbv->do_auto_completion && wbcg->auto_completing)
		gnm_complete_start (GNM_COMPLETE (wbcg->auto_complete), text);
}

static gboolean
cb_set_attr_list_len (PangoAttribute *a, gpointer len_bytes)
{
	a->start_index = 0;
	a->end_index = GPOINTER_TO_INT (len_bytes);
	return FALSE;
}

static void
cb_entry_insert_text (GtkEditable *editable,
		      gchar const *text,
		      gint         len_bytes,
		      gint        *pos_in_chars,
		      WBCGtk *wbcg)
{
	char const *str = gtk_entry_get_text (GTK_ENTRY (editable));
	int pos_in_bytes = g_utf8_offset_to_pointer (str, *pos_in_chars) - str;

	if (wbcg->auto_completing &&
	    len_bytes != 0 &&
	    (!g_unichar_isalpha (g_utf8_get_char (text)) ||
	     *pos_in_chars != gtk_entry_get_text_length (GTK_ENTRY (editable)))) {
		wbcg->auto_completing = FALSE;
	}

	if (wbcg->edit_line.full_content) {
		(void)pango_attr_list_filter (wbcg->edit_line.cur_fmt,
					      cb_set_attr_list_len,
					      GINT_TO_POINTER (len_bytes));

		go_pango_attr_list_open_hole (wbcg->edit_line.full_content,
					      pos_in_bytes, len_bytes);
		pango_attr_list_splice (wbcg->edit_line.full_content,
					wbcg->edit_line.cur_fmt,
					pos_in_bytes, 0);

		go_pango_attr_list_open_hole (wbcg->edit_line.markup,
					      pos_in_bytes, len_bytes);
		pango_attr_list_splice (wbcg->edit_line.markup,
					wbcg->edit_line.cur_fmt,
					pos_in_bytes, 0);
	}
}

static GSList *
attrs_at_byte (PangoAttrList *alist, gint bytepos)
{
	PangoAttrIterator *iter = pango_attr_list_get_iterator (alist);
	GSList *attrs = NULL;

	do {
		gint start, end;
		pango_attr_iterator_range (iter, &start, &end);
		if (start <= bytepos && bytepos < end) {
			attrs = pango_attr_iterator_get_attrs (iter);
			break;
		}
	} while (pango_attr_iterator_next (iter));
	pango_attr_iterator_destroy (iter);

	return attrs;
}

/* Find the markup to be used for new characters.  */
static void
set_cur_fmt (WBCGtk *wbcg, int target_pos_in_bytes)
{
	PangoAttrList *new_list = pango_attr_list_new ();
	GSList *ptr, *attrs = attrs_at_byte (wbcg->edit_line.markup, target_pos_in_bytes);

	for (ptr = attrs; ptr != NULL ; ptr = ptr->next) {
		PangoAttribute *attr = ptr->data;
		attr->start_index = 0;
		attr->end_index = INT_MAX;
		pango_attr_list_change (new_list, attr);
	}
	g_slist_free (attrs);
	if (wbcg->edit_line.cur_fmt)
		pango_attr_list_unref (wbcg->edit_line.cur_fmt);
	wbcg->edit_line.cur_fmt = new_list;
}

static void
cb_entry_cursor_pos (WBCGtk *wbcg)
{
	gint start, end, target_pos_in_chars, target_pos_in_bytes;
	GtkEditable *entry = GTK_EDITABLE (wbcg_get_entry (wbcg));
	char const *str = gtk_entry_get_text (GTK_ENTRY (entry));
	int edit_pos = gtk_editable_get_position (entry);

	if (str[0] == 0)
		return;

	if (edit_pos != gtk_entry_get_text_length (GTK_ENTRY (entry))) {
		/* The cursor is no longer at the end.  */
		wbcg->auto_completing = FALSE;
	}

	if (!wbcg->edit_line.full_content)
		return;

	/* 1) Use first selected character if there is a selection
	 * 2) Use the character just before the edit pos if it exists
	 * 3) Use the first character */
	if (gtk_editable_get_selection_bounds (entry, &start, &end))
		target_pos_in_chars = start;
	else {
		target_pos_in_chars = edit_pos;
		if (target_pos_in_chars > 0)
			target_pos_in_chars--;
	}

	target_pos_in_bytes = g_utf8_offset_to_pointer (str, target_pos_in_chars) - str;

	/* Make bold/italic/etc buttons show the right thing.  */
	{
		GnmStyle *style = gnm_style_new ();
		GSList *ptr, *attrs = attrs_at_byte (wbcg->edit_line.full_content, target_pos_in_bytes);
		for (ptr = attrs; ptr != NULL ; ptr = ptr->next) {
			PangoAttribute *attr = ptr->data;
			gnm_style_set_from_pango_attribute (style, attr);
			pango_attribute_destroy (attr);
		}
		wb_control_style_feedback (GNM_WBC (wbcg), style);
		gnm_style_unref (style);
		g_slist_free (attrs);
	}

	set_cur_fmt (wbcg, target_pos_in_bytes);
}

static void
cb_entry_delete_text (GtkEditable    *editable,
		      gint            start_pos,
		      gint            end_pos,
		      WBCGtk *wbcg)
{
	if (wbcg->auto_completing)
		wbcg_auto_complete_destroy (wbcg);

	if (wbcg->edit_line.full_content) {
		char const *str = gtk_entry_get_text (GTK_ENTRY (editable));
		guint start_pos_in_bytes =
			g_utf8_offset_to_pointer (str, start_pos) - str;
		guint end_pos_in_bytes =
			g_utf8_offset_to_pointer (str, end_pos) - str;
		guint len_bytes = end_pos_in_bytes - start_pos_in_bytes;

		go_pango_attr_list_erase (wbcg->edit_line.full_content,
					  start_pos_in_bytes,
					  len_bytes);
		go_pango_attr_list_erase (wbcg->edit_line.markup,
					  start_pos_in_bytes,
					  len_bytes);
		cb_entry_cursor_pos (wbcg);
	}
}

static void
wbcg_edit_init_markup (WBCGtk *wbcg, PangoAttrList *markup)
{
	SheetView const *sv;
	char const *text;
	GnmStyle const *style;

	g_return_if_fail (wbcg->edit_line.full_content == NULL);

	wbcg->edit_line.markup = markup;

	sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	style = sheet_style_get (sv->sheet, sv->edit_pos.col, sv->edit_pos.row);
	wbcg->edit_line.cell_attrs = gnm_style_generate_attrs_full (style);

	wbcg->edit_line.full_content = pango_attr_list_copy (wbcg->edit_line.cell_attrs);
	pango_attr_list_splice (wbcg->edit_line.full_content, markup, 0, 0);

	text = gtk_entry_get_text (wbcg_get_entry (wbcg));
	set_cur_fmt (wbcg, strlen (text) - 1);
}

struct cb_set_or_unset {
	const PangoAttribute *attr;
	gboolean set_in_ref;
};

static gboolean
cb_set_or_unset (PangoAttribute *attr, gpointer _data)
{
	struct cb_set_or_unset *data = _data;
	if (pango_attribute_equal (attr, data->attr))
		data->set_in_ref = TRUE;
	return FALSE;
}

static void
set_or_unset (PangoAttrList *dst, const PangoAttribute *attr,
	      PangoAttrList *ref)
{
	struct cb_set_or_unset data;

	data.attr = attr;
	data.set_in_ref = FALSE;
	(void)pango_attr_list_filter (ref, cb_set_or_unset, &data);

	if (data.set_in_ref)
		go_pango_attr_list_unset (dst,
					  attr->start_index, attr->end_index,
					  attr->klass->type);
	else
		pango_attr_list_change (dst, pango_attribute_copy (attr));
}

/**
 * wbcg_edit_add_markup:
 * @wbcg: #WBCGtk
 * @attr: #PangoAttribute
 *
 * Absorbs the ref to @attr.
 **/
void
wbcg_edit_add_markup (WBCGtk *wbcg, PangoAttribute *attr)
{
	GObject *entry = (GObject *)wbcg_get_entry (wbcg);
	if (wbcg->edit_line.full_content == NULL)
		wbcg_edit_init_markup (wbcg, pango_attr_list_new ());

	if (gtk_editable_get_selection_bounds (GTK_EDITABLE (entry),
					       &attr->start_index, &attr->end_index)) {
		char const *str = gtk_entry_get_text (GTK_ENTRY (entry));

		attr->start_index = g_utf8_offset_to_pointer (str, attr->start_index) - str;
		attr->end_index = g_utf8_offset_to_pointer (str, attr->end_index) - str;
		set_or_unset (wbcg->edit_line.full_content, attr,
			      wbcg->edit_line.cell_attrs);
		set_or_unset (wbcg->edit_line.markup, attr,
			      wbcg->edit_line.cell_attrs);
	}

	/* the format to use when inserting text, we will resize it later */
	attr->start_index = 0;
	attr->end_index = INT_MAX;
	set_or_unset (wbcg->edit_line.cur_fmt, attr,
		      wbcg->edit_line.cell_attrs);
	pango_attribute_destroy (attr);
	wbc_gtk_markup_changer (wbcg);
}

/**
 * wbcg_edit_get_markup:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none) (nullable): a PangoAttrList of the current markup while
 * editing.
 **/
PangoAttrList *
wbcg_edit_get_markup (WBCGtk *wbcg, gboolean full)
{
	return full ? wbcg->edit_line.full_content : wbcg->edit_line.markup;
}


static void
cb_warn_toggled (GtkToggleButton *button, gboolean *b)
{
	*b = gtk_toggle_button_get_active (button);
}

/**
 * wbcg_edit_start:
 * @wbcg:       The workbook to be edited.
 * @blankp:   If true, erase current cell contents first.  If false, leave the
 *            contents alone.
 * @cursorp:  If true, create an editing cursor in the current sheet.  (If
 *            false, the text will be editing in the edit box above the sheet,
 *            but this is not handled by this function.)
 *
 * Initiate editing of a cell in the sheet.  Note that we have two modes of
 * editing:
 *  1) in-cell editing when you just start typing, and
 *  2) above sheet editing when you hit F2.
 *
 * Returns: %TRUE if we did indeed start editing.  Returns %FALSE if the
 * cell-to-be-edited was locked.
 */
gboolean
wbcg_edit_start (WBCGtk *wbcg,
		 gboolean blankp, gboolean cursorp)
{
	/* We could save this, but the situation is rare, if confusing.  */
	static gboolean warn_on_text_format = TRUE;
	SheetView *sv;
	SheetControlGUI *scg;
	GnmCell *cell;
	char *text = NULL;
	int col, row;
	WorkbookView *wbv;
	int cursor_pos = -1;

	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);

	if (wbcg_is_editing (wbcg))
		return TRUE;

	/* Avoid recursion, and do not begin editing if a guru is up */
	if (wbcg->inside_editing || wbc_gtk_get_guru (wbcg) != NULL)
		return TRUE;
	wbcg->inside_editing = TRUE;

	wbv = wb_control_view (GNM_WBC (wbcg));
	sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	scg = wbcg_cur_scg (wbcg);

	col = sv->edit_pos.col;
	row = sv->edit_pos.row;

	/* don't edit a locked cell */
	/* TODO : extend this to disable edits that cannot succeed
	 * like editing a single cell of an array.  I think we have enough
	 * information if we look at the selection.
	 */
	if (wb_view_is_protected (wbv, TRUE) &&
	    gnm_style_get_contents_locked (sheet_style_get (sv->sheet, col, row))) {
		char *pos =  g_strdup_printf ( _("%s!%s is locked"),
			sv->sheet->name_quoted, cell_coord_name (col, row));
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbcg), pos,
			wb_view_is_protected (wbv, FALSE)
			 ? _("Unprotect the workbook to enable editing.")
			 : _("Unprotect the sheet to enable editing."));
		wbcg->inside_editing = FALSE;
		g_free (pos);
		return FALSE;
	}

	cell = sheet_cell_get (sv->sheet, col, row);
	if (cell &&
	    warn_on_text_format &&
	    go_format_is_text (gnm_cell_get_format (cell)) &&
	    (gnm_cell_has_expr (cell) || !VALUE_IS_STRING (cell->value))) {
		gint res; /* Using GtkResponseType would yield a warning on the switch */
		GtkWidget *check;
		GtkWidget *align;

		GtkWidget *d = gnm_message_dialog_create
			(wbcg_toplevel (wbcg),
			 GTK_DIALOG_DESTROY_WITH_PARENT,
			 GTK_MESSAGE_WARNING,
			 _("You are about to edit a cell with \"text\" format."),
			 _("The cell does not currently contain text, though, so if "
			   "you go on editing then the contents will be turned into "
			   "text."));
		gtk_dialog_add_button (GTK_DIALOG (d), GTK_STOCK_EDIT, GTK_RESPONSE_OK);
		go_gtk_dialog_add_button
			(GTK_DIALOG (d), _("Remove format"), GTK_STOCK_REMOVE,
			 GNM_RESPONSE_REMOVE);
		gtk_dialog_add_button (GTK_DIALOG (d), GNM_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_CANCEL);

		check = gtk_check_button_new_with_label (_("Show this dialog next time."));
		g_signal_connect (check, "toggled", G_CALLBACK (cb_warn_toggled), &warn_on_text_format);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
		align = gtk_alignment_new (0.5, 0.5, 0, 0);
		gtk_container_add (GTK_CONTAINER (align), check);
		gtk_widget_show_all (align);
		gtk_box_pack_end (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (d))), align, TRUE, TRUE, 0);
		res = go_gtk_dialog_run (GTK_DIALOG (d), wbcg_toplevel (wbcg));

		switch (res) {
		case GNM_RESPONSE_REMOVE: {
			GnmStyle *style = gnm_style_new ();
			gnm_style_set_format (style, go_format_general ());
			if (!cmd_selection_format (GNM_WBC (wbcg),
						   style, NULL, NULL))
				break;
			/* Fall through.  */
		}
		default:
		case GTK_RESPONSE_CANCEL:
			wbcg->inside_editing = FALSE;
			return FALSE;
		case GTK_RESPONSE_OK:
			break;
		}
	}

	gnm_app_clipboard_unant ();

	if (blankp)
		gtk_entry_set_text (wbcg_get_entry (wbcg), "");
	else if (cell != NULL) {
		gboolean quoted = FALSE;

		text = gnm_cell_get_text_for_editing (cell, &quoted, &cursor_pos);

		if (text)
			gtk_entry_set_text (wbcg_get_entry (wbcg), text);

		if (cell->value != NULL) {
			GOFormat const *fmt = VALUE_FMT (cell->value);
			if (fmt != NULL && go_format_is_markup (fmt)) {
				PangoAttrList *markup =
					pango_attr_list_copy ((PangoAttrList *)go_format_get_markup (fmt));
				if (quoted)
					go_pango_attr_list_open_hole (markup, 0, 1);
				wbcg_edit_init_markup (wbcg, markup);
			}
		}
	}

	gnm_expr_entry_set_scg (wbcg->edit_line.entry, scg);
	gnm_expr_entry_set_flags (wbcg->edit_line.entry,
		GNM_EE_SHEET_OPTIONAL | GNM_EE_FORMULA_ONLY,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL | GNM_EE_FORMULA_ONLY | GNM_EE_FORCE_REL_REF | GNM_EE_FORCE_ABS_REF);
	scg_edit_start (scg);

	/* Redraw the cell contents in case there was a span */
	sheet_redraw_region (sv->sheet, col, row, col, row);

	if (cursorp && /* autocompletion code will not work in the edit line */
	    wbv->do_auto_completion &&
	    (text == NULL || g_unichar_isalpha (g_utf8_get_char (text)))) {
		wbcg->auto_complete = gnm_complete_sheet_new (
			sv->sheet, col, row,
			workbook_edit_complete_notify, wbcg);
		wbcg->auto_completing = TRUE;
		wbcg->auto_max_size = 0;
	} else
		wbcg->auto_complete = NULL;

	/* Give the focus to the edit line */
	if (!cursorp)
		gtk_window_set_focus (wbcg_toplevel (wbcg),
			(GtkWidget *) wbcg_get_entry (wbcg));

	wbcg->editing = TRUE;
	wbcg->editing_sheet = sv->sheet;
	wbcg->editing_cell = cell;

	/* If this assert fails, it means editing was not shut down
	 * properly before
	 */
	g_return_val_if_fail (wbcg->edit_line.signal_changed == 0, TRUE);
	wbcg->edit_line.signal_changed = g_signal_connect (
		G_OBJECT (wbcg_get_entry (wbcg)),
		"changed",
		G_CALLBACK (cb_entry_changed), wbcg);
	wbcg->edit_line.signal_insert = g_signal_connect (
		G_OBJECT (wbcg_get_entry (wbcg)),
		"insert-text",
		G_CALLBACK (cb_entry_insert_text), wbcg);
	wbcg->edit_line.signal_delete = g_signal_connect (
		G_OBJECT (wbcg_get_entry (wbcg)),
		"delete-text",
		G_CALLBACK (cb_entry_delete_text), wbcg);
	wbcg->edit_line.signal_cursor_pos = g_signal_connect_swapped (
		G_OBJECT (wbcg_get_entry (wbcg)),
		"notify::cursor-position",
		G_CALLBACK (cb_entry_cursor_pos), wbcg);
	wbcg->edit_line.signal_selection_bound = g_signal_connect_swapped (
		G_OBJECT (wbcg_get_entry (wbcg)),
		"notify::selection-bound",
		G_CALLBACK (cb_entry_cursor_pos), wbcg);

	g_free (text);
	wb_control_update_action_sensitivity (GNM_WBC (wbcg));

	wbcg->inside_editing = FALSE;

	gtk_editable_set_position (GTK_EDITABLE (wbcg_get_entry (wbcg)), cursor_pos);

	return TRUE;
}

/**
 * wbcg_insert_object:
 * @wbcg: #WBCGtk *
 * @so: The object the needs to be placed
 *
 * Takes a newly created #SheetObject that has not yet been realized and
 * prepares to place it on the sheet.
 *
 * NOTE : Absorbs a reference to the object.
 **/
void
wbcg_insert_object (WBCGtk *wbcg, SheetObject *so)
{
	int i, npages;
	SheetControlGUI *scg;

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));
	g_return_if_fail (GNM_IS_SO (so));

	wbcg_insert_object_clear (wbcg);

	npages = wbcg_get_n_scg (wbcg);
	for (i = 0; i < npages; i++) {
		if (NULL != (scg = wbcg_get_nth_scg (wbcg, i))) {
			scg_object_unselect (scg, NULL);
			scg_cursor_visible (scg, FALSE);
			scg_set_display_cursor (scg);
			sc_unant (GNM_SHEET_CONTROL (scg));
		}
	}
	/* we can't set wbcg->new_object before now because if one sheet has a
	 * selected object, the new object will be destroyed by the loop
	 * above. See #669648. */
	wbcg->new_object = so;
	wb_control_update_action_sensitivity (GNM_WBC (wbcg));
}

/**
 * wbcg_insert_object_clear:
 * @wbcg: #WBCGtk
 *
 * If we are preparing to insert a new object, unref the object, and restore
 * a normal state to the scgs that was changed in wbcg_insert_object
 * (e.g., visibility of cursors)
 **/
void
wbcg_insert_object_clear (WBCGtk *wbcg)
{
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	if (NULL != wbcg->new_object) {
		int i, npages;
		SheetControlGUI *scg;

		g_object_unref (wbcg->new_object);
		wbcg->new_object = NULL;

		npages = wbcg_get_n_scg (wbcg);
		for (i = 0; i < npages; i++)
			if (NULL != (scg = wbcg_get_nth_scg (wbcg, i)))
				scg_cursor_visible (scg, TRUE);
	}
}


/**
 * wbcg_get_entry:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none): the #GtkEntry associated with the current GnmExprEntry
 **/
GtkEntry *
wbcg_get_entry (WBCGtk const *wbcg)
{
	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), NULL);
	g_return_val_if_fail (wbcg != NULL, NULL);

	return gnm_expr_entry_get_entry (wbcg->edit_line.entry);
}

/**
 * wbcg_get_entry_logical:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none): the logical (allowing redirection via
 * wbcg_set_entry for gurus) #GnmExprEntry
 **/
GnmExprEntry *
wbcg_get_entry_logical (WBCGtk const *wbcg)
{
	g_return_val_if_fail (wbcg != NULL, NULL);

	if (wbcg->edit_line.temp_entry != NULL)
		return wbcg->edit_line.temp_entry;

	return wbcg->edit_line.entry;
}

/**
 * wbcg_get_entry_underlying:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none): the #GtkEntry associated with the logical
 * #GnmExprEntry.
 **/
GtkWidget *
wbcg_get_entry_underlying (WBCGtk const *wbcg)
{
	GnmExprEntry *ee    = wbcg_get_entry_logical (wbcg);
	GtkEntry     *entry = gnm_expr_entry_get_entry (ee);
	return GTK_WIDGET (entry);
}

void
wbcg_set_entry (WBCGtk *wbcg, GnmExprEntry *entry)
{
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	if (wbcg->edit_line.temp_entry != entry) {
		scg_rangesel_stop (wbcg_cur_scg (wbcg), FALSE);
		wbcg->edit_line.temp_entry = entry;
	}
}

/**
 * wbcg_entry_has_logical:
 * @wbcg: #WBCGtk
 *
 * Returns: %TRUE if wbcg_set_entry has redirected the edit_entry.
 **/
gboolean
wbcg_entry_has_logical (WBCGtk const *wbcg)
{
	return (wbcg->edit_line.temp_entry != NULL);
}

/****************************************************************************/

static void
wbcg_edit_attach_guru_main (WBCGtk *wbcg, GtkWidget *guru)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);

	g_return_if_fail (guru != NULL);
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));
	g_return_if_fail (wbcg->edit_line.guru == NULL);

	/* Make sure we don't have anything anted.
	 * this protects against two anted regions showing up
	 */
	gnm_app_clipboard_unant ();

	/* don't set end 'End' mode when a dialog comes up */
	wbcg_set_end_mode (wbcg, FALSE);

	wbcg->edit_line.guru = guru;
	gtk_editable_set_editable (GTK_EDITABLE (wbcg_get_entry (wbcg)), FALSE);
	wb_control_update_action_sensitivity (wbc);
	wb_control_menu_state_update (wbc, MS_GURU_MENU_ITEMS);

	g_signal_connect_object (guru, "destroy",
		G_CALLBACK (wbc_gtk_detach_guru), wbcg, G_CONNECT_SWAPPED);
}

static void
cb_guru_set_focus (G_GNUC_UNUSED GtkWidget *window,
		   GtkWidget *focus_widget, WBCGtk *wbcg)
{
	GnmExprEntry *gee = NULL;
	if (focus_widget != NULL &&
	    GNM_EXPR_ENTRY_IS (gtk_widget_get_parent (focus_widget)))
		gee = GNM_EXPR_ENTRY (gtk_widget_get_parent (focus_widget));
	wbcg_set_entry (wbcg, gee);
}

/****************************************************************************/

void
wbc_gtk_attach_guru (WBCGtk *wbcg, GtkWidget *guru)
{
	g_return_if_fail (guru != NULL);
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	wbcg_edit_attach_guru_main (wbcg, guru);
	g_signal_connect_object (G_OBJECT (guru), "set-focus",
		G_CALLBACK (cb_guru_set_focus), wbcg, 0);
}

void
wbc_gtk_attach_guru_with_unfocused_rs (WBCGtk *wbcg, GtkWidget *guru,
				       GnmExprEntry *gee)
{
	g_return_if_fail (guru != NULL);
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	wbcg_edit_attach_guru_main (wbcg, guru);

	if (gnm_conf_get_dialogs_rs_unfocused ()) {
		if (gee)
			wbcg_set_entry (wbcg, gee);
	} else
		g_signal_connect (G_OBJECT (guru), "set-focus",
			G_CALLBACK (cb_guru_set_focus), wbcg);
}

void
wbc_gtk_detach_guru (WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	/* don't sit end 'End' mode when a dialog comes up */
	wbcg_set_end_mode (wbcg, FALSE);
	if (wbcg->edit_line.guru == NULL)
		return;

	wbcg_set_entry (wbcg, NULL);
	wbcg->edit_line.guru = NULL;
	gtk_editable_set_editable (GTK_EDITABLE (wbcg_get_entry (wbcg)), TRUE);
	wb_control_update_action_sensitivity (wbc);
	wb_control_menu_state_update (wbc, MS_GURU_MENU_ITEMS);
}

/**
 * wbc_gtk_get_guru:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none): the guru attached to the workbook view.
 **/
GtkWidget *
wbc_gtk_get_guru (WBCGtk const *wbcg)
{
	return wbcg->edit_line.guru;
}

/****************************************************************************/

static gboolean
auto_complete_matches (WBCGtk *wbcg)
{
	if (!wbcg->auto_completing || wbcg->auto_complete_text == NULL)
		return FALSE;
	else {
		GtkEntry *entry = wbcg_get_entry (wbcg);
		char const *text = gtk_entry_get_text (entry);
		size_t len = strlen (text);
		return strncmp (text, wbcg->auto_complete_text, len) == 0;
	}
}

/*
 * Returns the text that must be shown by the editing entry, takes
 * into account the auto-completion text.
 */
char const *
wbcg_edit_get_display_text (WBCGtk *wbcg)
{
	if (auto_complete_matches (wbcg))
		return wbcg->auto_complete_text;
	else
		return gtk_entry_get_text (wbcg_get_entry (wbcg));
}

void
wbc_gtk_init_editline (WBCGtk *wbcg)
{
	g_assert (GNM_IS_WBC_GTK (wbcg));
	g_assert (wbcg->edit_line.entry == NULL);

	wbcg->edit_line.entry = g_object_new (GNM_EXPR_ENTRY_TYPE,
					      "with-icon", FALSE,
					      "wbcg", wbcg,
					      NULL);
	wbcg->edit_line.temp_entry = NULL;
	wbcg->edit_line.guru = NULL;
	wbcg->edit_line.signal_changed = 0;
	wbcg->edit_line.full_content = NULL;
	wbcg->edit_line.markup = NULL;
	wbcg->edit_line.cur_fmt = NULL;
}
