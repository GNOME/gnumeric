/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * workbook-edit.c: Keeps track of the cell editing process.
 *
 * Author:
 *   Miguel de Icaza (miguel@ximian.com)
 *   Jody Goldberg (jody@gnome.org)
 *
 * (C) 2000-2005 Ximian, Inc.
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "workbook-edit.h"

#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook-priv.h"
#include "application.h"
#include "complete-sheet.h"
#include "commands.h"
#include "gnumeric-gconf.h"
#include "mstyle.h"
#include "style-color.h"
#include "sheet-control-gui-priv.h"
#include "sheet-style.h"
#include "sheet-view.h"
#include "sheet.h"
#include "cell.h"
#include "expr.h"
#include "gnm-format.h"
#include "number-match.h"
#include "parse-util.h"
#include "validation.h"
#include "value.h"
#include "widgets/gnumeric-expr-entry.h"

#include <gtk/gtk.h>
#include <string.h>

/*
 * Shuts down the auto completion engine
 */
void
wbcg_auto_complete_destroy (WorkbookControlGUI *wbcg)
{
	g_free (wbcg->auto_complete_text);
	wbcg->auto_complete_text = NULL;

	if (wbcg->edit_line.signal_changed) {
		g_signal_handler_disconnect (wbcg_get_entry (wbcg),
					     wbcg->edit_line.signal_changed);
		wbcg->edit_line.signal_changed = 0;
	}

	if (wbcg->auto_complete != NULL) {
		g_object_unref (G_OBJECT (wbcg->auto_complete));
		wbcg->auto_complete = NULL;
	}

	wbcg->auto_completing = FALSE;
}

/**
 * wbcg_edit_finish :
 * @wbcg : #WorkbookControlGUI
 * @result : what should we do with the content
 * @showed_dialog : If non-NULL will indicate if a dialog was displayed.
 *
 * Return TRUE if editing completed successfully, or we were no editing.
 **/
gboolean
wbcg_edit_finish (WorkbookControlGUI *wbcg, WBCEditResult result,
		  gboolean *showed_dialog)
{
	Sheet *sheet;
	SheetView *sv;
	WorkbookControl *wbc;
	WorkbookView	*wbv;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	wbc = WORKBOOK_CONTROL (wbcg);
	wbv = wb_control_view (wbc);

	wbcg_focus_cur_scg (wbcg);

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
			wbcg_edit_detach_guru (wbcg);
			gtk_widget_destroy (w);
		}

		return TRUE;
	}

	g_return_val_if_fail (IS_SHEET (wbc->editing_sheet), TRUE);

	sheet = wbc->editing_sheet;
	sv = sheet_get_view (sheet, wbv);

	/* Save the results before changing focus */
	if (result != WBC_EDIT_REJECT) {
		ValidationStatus valid;
		char *free_txt = NULL;
		char const *txt = wbcg_edit_get_display_text (wbcg);
		GnmStyle *mstyle = sheet_style_get (sheet, sv->edit_pos.col, sv->edit_pos.row);
		char const *expr_txt = NULL;

		/* BE CAREFUL the standard fmts must not NOT include '@' */
		GnmValue *value = format_match (txt, gnm_style_get_format (mstyle),
						workbook_date_conv (sheet->workbook));
		if (value != NULL)
			value_release (value);
		else
			expr_txt = gnm_expr_char_start_p (txt);

		/* NOTE : do not modify gnm_expr_char_start_p to exclude "-"
		 * it _can_ start an expression, which is required for rangesel
		 * it just isn't an expression. */
		if (expr_txt != NULL && *expr_txt != '\0' && strcmp (expr_txt, "-")) {
			GnmExpr const *expr = NULL;
			GnmParsePos    pp;
			GnmParseError  perr;

			parse_pos_init_editpos (&pp, sv);
			parse_error_init (&perr);
			expr = gnm_expr_parse_str (expr_txt,
				&pp, GNM_EXPR_PARSE_DEFAULT, gnm_expr_conventions_default, &perr);
			/* Try adding a single extra closing paren to see if it helps */
			if (expr == NULL && perr.err != NULL &&
			    perr.err->code == PERR_MISSING_PAREN_CLOSE) {
				GnmParseError tmp_err;
				char *tmp = g_strconcat (txt, ")", NULL);
				parse_error_init (&tmp_err);
				expr = gnm_expr_parse_str (gnm_expr_char_start_p (tmp),
					&pp, GNM_EXPR_PARSE_DEFAULT,
					gnm_expr_conventions_default, &tmp_err);
				parse_error_free (&tmp_err);

				if (expr != NULL)
					txt = free_txt = tmp;
				else
					g_free (tmp);
			}

			if (expr == NULL && perr.err != NULL) {
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

				reedit = wb_control_validation_msg (WORKBOOK_CONTROL (wbcg),
					VALIDATION_STYLE_PARSE_ERROR, NULL, perr.err->message);
				if (showed_dialog != NULL)
					*showed_dialog = TRUE;

				parse_error_free (&perr);
				if (reedit == VALIDATION_STATUS_INVALID_EDIT)
					return FALSE;
				/* restore focus to sheet , or we'll leave edit
				 * mode only to jump right back in in the new
				 * cell because it looks like someone just
				 * focused on the edit line (eg hit F2) */
				wbcg_focus_cur_scg (wbcg);
			}
			if (expr != NULL)
				gnm_expr_unref (expr);
		}

		/* NOTE we assign the value BEFORE validating in case
		 * a validation condition depends on the new value  */
		if (result == WBC_EDIT_ACCEPT) {
			/*
			 * Copy here as callbacks will otherwise mess with
			 * the list.
			 */
			PangoAttrList *res_markup = wbcg->edit_line.markup
				? pango_attr_list_copy (wbcg->edit_line.markup)
				: NULL;
			cmd_set_text (wbc, sheet, &sv->edit_pos, txt, res_markup);
			if (res_markup) pango_attr_list_unref (res_markup);
		} else
			cmd_area_set_text (wbc, sv, txt,
				result == WBC_EDIT_ACCEPT_ARRAY);

		valid = validation_eval (wbc, mstyle, sheet, &sv->edit_pos, showed_dialog);

		if (free_txt != NULL)
			g_free (free_txt);

		if (valid != VALIDATION_STATUS_VALID) {
			result = WBC_EDIT_REJECT;
			command_undo (wbc);
			if (valid == VALIDATION_STATUS_INVALID_EDIT) {
				gtk_window_set_focus (wbcg_toplevel (wbcg),
					(GtkWidget *) wbcg_get_entry (wbcg));
				return FALSE;
			}
		}
	} else {
		if (sv == wb_control_cur_sheet_view (wbc)) {
			/* Redraw the cell contents in case there was a span */
			GnmRange tmp; tmp.start = tmp.end = sv->edit_pos;
			sheet_range_bounding_box (sv->sheet, &tmp);
			sv_redraw_range (wb_control_cur_sheet_view (wbc), &tmp);
		}

		/* Reload the entry widget with the original contents */
		wb_view_edit_line_set (wbv, wbc);
	}

	/* Stop editing */
	wbc->editing = FALSE;
	wbc->editing_sheet = NULL;
	wbc->editing_cell = NULL;

	if (wbcg->edit_line.guru != NULL) {
		GtkWidget *w = wbcg->edit_line.guru;
		wbcg_edit_detach_guru (wbcg);
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

	if (wbcg->edit_line.full_content != NULL) {
		pango_attr_list_unref (wbcg->edit_line.full_content);
		pango_attr_list_unref (wbcg->edit_line.markup);
		pango_attr_list_unref (wbcg->edit_line.cur_fmt);
		wbcg->edit_line.full_content =
			wbcg->edit_line.markup =
			wbcg->edit_line.cur_fmt = NULL;
	}
	/* set pos to 0, to ensure that if we start editing by clicking on the
	 * editline at the last position, we'll get the right style feedback */
	gtk_editable_set_position ((GtkEditable *) wbcg_get_entry (wbcg), 0);

	wb_control_update_action_sensitivity (wbc);

	/* restore focus to original sheet in case things were being selected
	 * on a different page.  Do no go through the view, rangesel is
	 * specific to the control.  */
	wb_control_sheet_focus (wbc, sheet);
	scg_edit_stop (wbcg_cur_scg (wbcg));	/* Only the edit sheet has an edit cursor */
	wbcg_auto_complete_destroy (wbcg);
	wb_control_style_feedback (wbc, NULL);	/* in case markup messed with things */

	return TRUE;
}

static void
workbook_edit_complete_notify (char const *text, void *closure)
{
	WorkbookControlGUI *wbcg = closure;
	SheetControlGUI    *scg  = wbcg_cur_scg (wbcg);

	g_free (wbcg->auto_complete_text);
	wbcg->auto_complete_text = g_strdup (text);

	SCG_FOREACH_PANE (scg, pane,
		if (pane->editor != NULL)
			foo_canvas_item_request_update (FOO_CANVAS_ITEM (pane->editor)););
}

static void
cb_entry_changed (GtkEntry *entry, WorkbookControlGUI *wbcg)
{
	char const *text;
	int text_len;
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));

	text = gtk_entry_get_text (wbcg_get_entry (wbcg));
	text_len = strlen (text);

	if (text_len > wbcg->auto_max_size)
		wbcg->auto_max_size = text_len;

	if (wbv->do_auto_completion && wbcg->auto_completing)
		complete_start (wbcg->auto_complete, text);
}

static gboolean
cb_set_attr_list_len (PangoAttribute *a, gpointer len_bytes)
{
	a->start_index = 0;
	a->end_index = GPOINTER_TO_INT (len_bytes);
	return FALSE;
}

struct cb_splice {
	guint pos, len;
	PangoAttrList *result;
};

static gboolean
cb_splice (PangoAttribute *attr, gpointer _data)
{
	struct cb_splice *data = _data;

	if (attr->start_index >= data->pos) {
		PangoAttribute *new_attr = pango_attribute_copy (attr);
		new_attr->start_index += data->len;
		new_attr->end_index += data->len;
		pango_attr_list_insert (data->result, new_attr);
	} else if (attr->end_index <= data->pos) {
		PangoAttribute *new_attr = pango_attribute_copy (attr);
		pango_attr_list_insert (data->result, new_attr);
	} else {
		PangoAttribute *new_attr = pango_attribute_copy (attr);
		new_attr->end_index = data->pos;
		pango_attr_list_insert (data->result, new_attr);

		new_attr = pango_attribute_copy (attr);
		new_attr->start_index = data->pos + data->len;
		new_attr->end_index += data->len;
		pango_attr_list_insert (data->result, new_attr);
	}

	return FALSE;
}

static gboolean
cb_splice_true (G_GNUC_UNUSED PangoAttribute *attr, G_GNUC_UNUSED gpointer data)
{
	return TRUE;
}

static void
gnm_pango_attr_list_splice (PangoAttrList *tape,
			    PangoAttrList *piece,
			    guint pos, guint len)
{
	struct cb_splice data;
	PangoAttrList *tape2;

	data.result = tape;
	data.pos = pos;
	data.len = len;

	/* Clean out the tape.  */
	tape2 = pango_attr_list_filter (tape, cb_splice_true, NULL);

	if (tape2) {
		(void)pango_attr_list_filter (tape2, cb_splice, &data);
		pango_attr_list_unref (tape2);
	}

	/* Apply the new attributes.  */
	pango_attr_list_splice (data.result, piece, pos, 0);
}


static void
cb_entry_insert_text (GtkEditable *editable,
		      gchar const *text,
		      gint         len_bytes,
		      gint        *pos_in_chars,
		      WorkbookControlGUI *wbcg)
{
	char const *str = gtk_entry_get_text (GTK_ENTRY (editable));
	int pos_in_bytes = g_utf8_offset_to_pointer (str, *pos_in_chars) - str;

	if (wbcg->auto_completing &&
	    len_bytes != 0 &&
	    (!g_unichar_isalpha (g_utf8_get_char (text)) ||
	     *pos_in_chars != GTK_ENTRY (editable)->text_length)) {
		wbcg->auto_completing = FALSE;
	}

	if (wbcg->edit_line.full_content) {
		(void)pango_attr_list_filter (wbcg->edit_line.cur_fmt,
					      cb_set_attr_list_len,
					      GINT_TO_POINTER (len_bytes));

		gnm_pango_attr_list_splice (wbcg->edit_line.full_content,
					    wbcg->edit_line.cur_fmt,
					    pos_in_bytes, len_bytes);

		gnm_pango_attr_list_splice (wbcg->edit_line.markup,
					    wbcg->edit_line.cur_fmt,
					    pos_in_bytes, len_bytes);
	}
}

static GSList *
attrs_at_byte (PangoAttrList *alist, guint bytepos)
{
	PangoAttrIterator *iter = pango_attr_list_get_iterator (alist);
	GSList *attrs = NULL;

	do {
		guint start, end;
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
set_cur_fmt (WorkbookControlGUI *wbcg, int target_pos_in_bytes)
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
cb_entry_cursor_pos (WorkbookControlGUI *wbcg)
{
	guint start, end, target_pos_in_chars, target_pos_in_bytes;
	GtkEditable *entry = GTK_EDITABLE (wbcg_get_entry (wbcg));
	char const *str = gtk_entry_get_text (GTK_ENTRY (entry));
	int edit_pos = gtk_editable_get_position (entry);

	if (str[0] == 0)
		return;

	if (edit_pos != GTK_ENTRY (entry)->text_length) {
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
		wb_control_style_feedback (WORKBOOK_CONTROL (wbcg), style);
		gnm_style_unref (style);
		g_slist_free (attrs);
	}

	set_cur_fmt (wbcg, target_pos_in_bytes);
}

typedef struct {
	unsigned start_pos, end_pos, len; /* in bytes not chars */
} EntryDeleteTextClosure;


/*
 *
 * |       +-------------------+            The attribute
 *
 *                                +----+    (1)
 *                  +------------------+    (2)
 *                  +------+                (3)
 *   +--+                                   (4)
 *   +--------------+                       (5)
 *   +---------------------------------+    (6)
 *
 */
static gboolean
cb_delete_filter (PangoAttribute *a, EntryDeleteTextClosure *change)
{
	if (change->start_pos >= a->end_index)
		return FALSE;  /* (1) */

	if (change->start_pos <= a->start_index) {
		if (change->end_pos >= a->end_index)
			return TRUE; /* (6) */

		a->end_index -= change->len;
		if (change->end_pos >= a->start_index)
			a->start_index = change->start_pos; /* (5) */
		else
			a->start_index -= change->len; /* (4) */
	} else {
		if (change->end_pos >= a->end_index)
			a->end_index = change->start_pos;  /* (2) */
		else
			a->end_index -= change->len; /* (3) */
	}

	return FALSE;
}

static void
cb_entry_delete_text (GtkEditable    *editable,
		      gint            start_pos,
		      gint            end_pos,
		      WorkbookControlGUI *wbcg)
{
	if (wbcg->auto_completing) {
		wbcg_auto_complete_destroy (wbcg);
		SCG_FOREACH_PANE (wbcg_cur_scg (wbcg), pane,
			if (pane->editor != NULL)
				foo_canvas_item_request_update (FOO_CANVAS_ITEM (pane->editor)););
	}

	if (wbcg->edit_line.full_content) {
		char const *str = gtk_entry_get_text (GTK_ENTRY (editable));
		PangoAttrList *gunk;
		EntryDeleteTextClosure change;

		change.start_pos = g_utf8_offset_to_pointer (str, start_pos) - str;
		change.end_pos   = g_utf8_offset_to_pointer (str, end_pos) - str;
		change.len = change.end_pos - change.start_pos;
		gunk = pango_attr_list_filter (wbcg->edit_line.full_content,
					       (PangoAttrFilterFunc) cb_delete_filter, &change);
		if (gunk != NULL)
			pango_attr_list_unref (gunk);
		gunk = pango_attr_list_filter (wbcg->edit_line.markup,
					       (PangoAttrFilterFunc) cb_delete_filter, &change);
		if (gunk != NULL)
			pango_attr_list_unref (gunk);
		cb_entry_cursor_pos (wbcg);
	}
}

static void
wbcg_edit_init_markup (WorkbookControlGUI *wbcg, PangoAttrList *markup)
{
	SheetView const *sv  = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	const char *text = gtk_entry_get_text (wbcg_get_entry (wbcg));

	g_return_if_fail (wbcg->edit_line.full_content == NULL);

	wbcg->edit_line.markup = markup;
	wbcg->edit_line.full_content = gnm_style_generate_attrs_full (
		sheet_style_get (sv->sheet, sv->edit_pos.col, sv->edit_pos.row));
	pango_attr_list_splice (wbcg->edit_line.full_content, markup, 0, 0);

	set_cur_fmt (wbcg, strlen (text) - 1);
}

/**
 * wbcg_edit_add_markup :
 * @wbcg : #WorkbookControlGUI
 * @attr : #PangoAttribute
 *
 * Absorb the ref to @attr and merge it into the list
 **/
void
wbcg_edit_add_markup (WorkbookControlGUI *wbcg, PangoAttribute *attr)
{
	GObject *entry = (GObject *)wbcg_get_entry (wbcg);
	if (wbcg->edit_line.full_content == NULL)
		wbcg_edit_init_markup (wbcg, pango_attr_list_new ());

	if (gtk_editable_get_selection_bounds (GTK_EDITABLE (entry),
					       &attr->start_index, &attr->end_index)) {
		char const *str = gtk_entry_get_text (GTK_ENTRY (entry));

		attr->start_index = g_utf8_offset_to_pointer (str, attr->start_index) - str;
		attr->end_index = g_utf8_offset_to_pointer (str, attr->end_index) - str;
		pango_attr_list_change (wbcg->edit_line.full_content,
					pango_attribute_copy (attr));
		pango_attr_list_change (wbcg->edit_line.markup,
					pango_attribute_copy (attr));
	}

	/* the format to use when inserting text, we will resize it later */
	attr->start_index = 0;
	attr->end_index = INT_MAX;
	pango_attr_list_change (wbcg->edit_line.cur_fmt, attr);
	g_signal_emit (G_OBJECT (wbcg), wbcg_signals [WBCG_MARKUP_CHANGED], 0);
}

/**
 * wbcg_edit_get_markup :
 * @wbcg : #WorkbookControlGUI
 *
 * Returns a potentially NULL PangoAttrList of the current markup while
 * editing.  The list belongs to @wbcg and should not be freed.
 **/
PangoAttrList *
wbcg_edit_get_markup (WorkbookControlGUI *wbcg, gboolean full)
{
	return full ? wbcg->edit_line.full_content : wbcg->edit_line.markup;
}

/**
 * wbcg_edit_start:
 *
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
 * Returns TRUE if we did indeed start editing.  Returns FALSE if the
 * cell-to-be-edited was locked.
 */
gboolean
wbcg_edit_start (WorkbookControlGUI *wbcg,
		 gboolean blankp, gboolean cursorp)
{
	static gboolean inside_editing = FALSE;
	SheetView *sv;
	SheetControlGUI *scg;
	GnmCell *cell;
	char *text = NULL;
	int col, row;
	WorkbookView *wbv;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	if (wbcg_is_editing (wbcg))
		return TRUE;

	/* Avoid recursion, and do not begin editing if a guru is up */
	if (inside_editing || wbcg_edit_get_guru (wbcg) != NULL)
		return TRUE;
	inside_editing = TRUE;

	wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	scg = wbcg_cur_scg (wbcg);

	col = sv->edit_pos.col;
	row = sv->edit_pos.row;

	/* don't edit a locked cell */
	/* TODO : extend this to disable edits that cannot succeed
	 * like editing a single cell of an array.  I think we have enough
	 * information if we look at the selection.
	 */
	if (wb_view_is_protected (wbv, TRUE) &&
	    gnm_style_get_content_locked (sheet_style_get (sv->sheet, col, row))) {
		char *pos =  g_strdup_printf ( _("%s!%s is locked"),
			sv->sheet->name_quoted, cell_coord_name (col, row));
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbcg), pos,
			wb_view_is_protected (wbv, FALSE)
			 ? _("Unprotect the workbook to enable editing.")
			 : _("Unprotect the sheet to enable editing."));
		inside_editing = FALSE;
		g_free (pos);
		return FALSE;
	}

	gnm_app_clipboard_unant ();

	cell = sheet_cell_get (sv->sheet, col, row);
	if (blankp)
		gtk_entry_set_text (wbcg_get_entry (wbcg), "");
	else if (cell != NULL) {
		text = cell_get_entered_text (cell);

		/* If this is part of an array we need to remove the
		 * '{' '}' and the size information from the display.
		 * That is not actually part of the parsable expression.
		 */
		if (NULL != cell_is_array (cell))
			gtk_entry_set_text (wbcg_get_entry (wbcg), text);

		if (cell->value != NULL) {
			GOFormat *fmt = VALUE_FMT (cell->value);
			if (fmt != NULL && go_format_is_markup (fmt))
				wbcg_edit_init_markup (wbcg,
					pango_attr_list_copy (fmt->markup));
		}
	}

	gnm_expr_entry_set_scg (wbcg->edit_line.entry, scg);
	gnm_expr_entry_set_flags (wbcg->edit_line.entry,
		GNM_EE_SHEET_OPTIONAL | GNM_EE_FORMULA_ONLY,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL | GNM_EE_FORMULA_ONLY);
	scg_edit_start (scg);

	/* Redraw the cell contents in case there was a span */
	sheet_redraw_region (sv->sheet, col, row, col, row);

	if (cursorp && /* autocompletion code will not work in the edit line */
	    wbv->do_auto_completion &&
	    (text == NULL || g_unichar_isalpha (g_utf8_get_char (text)))) {
		wbcg->auto_complete = complete_sheet_new (
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

	wbcg->wb_control.editing = TRUE;
	wbcg->wb_control.editing_sheet = sv->sheet;
	wbcg->wb_control.editing_cell = cell;

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
	wb_control_update_action_sensitivity (WORKBOOK_CONTROL (wbcg));

	gtk_editable_set_position (GTK_EDITABLE (wbcg_get_entry (wbcg)), -1);

	inside_editing = FALSE;
	return TRUE;
}

GtkEntry *
wbcg_get_entry (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (wbcg != NULL, NULL);

	return gnm_expr_entry_get_entry (wbcg->edit_line.entry);
}

GnmExprEntry *
wbcg_get_entry_logical (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (wbcg != NULL, NULL);

	if (wbcg->edit_line.temp_entry != NULL)
		return wbcg->edit_line.temp_entry;

	return wbcg->edit_line.entry;
}

void
wbcg_set_entry (WorkbookControlGUI *wbcg, GnmExprEntry *entry)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (wbcg->edit_line.temp_entry != entry) {
		scg_rangesel_stop (wbcg_cur_scg (wbcg), FALSE);
		wbcg->edit_line.temp_entry = entry;
	}
}

static void
wbcg_edit_attach_guru_main (WorkbookControlGUI *wbcg, GtkWidget *guru)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);

	g_return_if_fail (guru != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
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
}

static void
cb_guru_set_focus (GtkWidget *window, GtkWidget *focus_widget,
		   WorkbookControlGUI *wbcg)
{
	GnmExprEntry *gee = NULL;
	if (focus_widget != NULL && IS_GNM_EXPR_ENTRY (focus_widget->parent))
		gee = GNM_EXPR_ENTRY (focus_widget->parent);
	wbcg_set_entry (wbcg, gee);
}

void
wbcg_edit_attach_guru (WorkbookControlGUI *wbcg, GtkWidget *guru)
{
	g_return_if_fail (guru != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	wbcg_edit_attach_guru_main (wbcg, guru);

	g_signal_connect (G_OBJECT (guru),
		"set-focus",
		G_CALLBACK (cb_guru_set_focus), wbcg);
}

void
wbcg_edit_attach_guru_with_unfocused_rs (WorkbookControlGUI *wbcg, GtkWidget *guru,
					 GnmExprEntry *gee)
{
	g_return_if_fail (guru != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	wbcg_edit_attach_guru_main (wbcg, guru);

	if (gnm_app_prefs->unfocused_range_selection) {
		if (gee)
			wbcg_set_entry (wbcg, gee);
	} else
		g_signal_connect (G_OBJECT (guru),
				  "set-focus",
				  G_CALLBACK (cb_guru_set_focus), wbcg);
}

void
wbcg_edit_detach_guru (WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

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

GtkWidget *
wbcg_edit_get_guru (WorkbookControlGUI const *wbcg)
{
	return wbcg->edit_line.guru;
}

gboolean
wbcg_edit_entry_redirect_p (WorkbookControlGUI const *wbcg)
{
	return (wbcg->edit_line.temp_entry != NULL);
}

gboolean
wbcg_auto_completing (WorkbookControlGUI const *wbcg)
{
	return wbcg->auto_completing;
}

static gboolean
auto_complete_matches (WorkbookControlGUI *wbcg)
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
wbcg_edit_get_display_text (WorkbookControlGUI *wbcg)
{
	if (auto_complete_matches (wbcg))
		return wbcg->auto_complete_text;
	else
		return gtk_entry_get_text (wbcg_get_entry (wbcg));
}

/* Initializes the Workbook entry */
void
wbcg_edit_ctor (WorkbookControlGUI *wbcg)
{
	g_assert (IS_WORKBOOK_CONTROL_GUI (wbcg));
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
