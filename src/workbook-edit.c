/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * workbook-edit.c: Keeps track of the cell editing process.
 *
 * Author:
 *   Miguel de Icaza (miguel@ximian.com)
 *   Jody Goldberg (jody@gnome.org)
 *
 * (C) 2000-2001 Ximian, Inc.
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
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
#include "sheet-control-gui-priv.h"
#include "sheet-style.h"
#include "sheet-view.h"
#include "sheet.h"
#include "cell.h"
#include "expr.h"
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

	if (wbcg->edit_line.signal_changed >= 0) {
		g_signal_handler_disconnect (GTK_OBJECT (wbcg_get_entry (wbcg)),
			wbcg->edit_line.signal_changed);
		wbcg->edit_line.signal_changed = -1;
	}

	if (wbcg->auto_complete != NULL) {
		g_object_unref (G_OBJECT (wbcg->auto_complete));
		wbcg->auto_complete = NULL;
	}

	wbcg->auto_completing = FALSE;

}

gboolean
wbcg_edit_finish (WorkbookControlGUI *wbcg, gboolean accept)
{
	Sheet *sheet;
	SheetView *sv;
	WorkbookControl *wbc;
	WorkbookView	*wbv;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	wbc = WORKBOOK_CONTROL (wbcg);
	wbv = wb_control_view (wbc);

	wbcg_focus_cur_scg (wbcg);

	/* Remove the range selection cursor if it exists */
	if (NULL != wbcg->rangesel)
		scg_rangesel_stop (wbcg->rangesel, !accept);

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

	g_return_val_if_fail (IS_SHEET (wbcg->wb_control.editing_sheet), TRUE);

	sheet = wbcg->wb_control.editing_sheet;
	sv = sheet_get_view (sheet, wbv);

	/* Save the results before changing focus */
	if (accept) {
		ValidationStatus valid;
		char *free_txt = NULL;
		char const *txt = wbcg_edit_get_display_text (wbcg);
		MStyle *mstyle = sheet_style_get (sheet, sv->edit_pos.col, sv->edit_pos.row);
		char const *expr_txt = NULL;

		/* BE CAREFUL the standard fmts must not NOT include '@' */
		Value *value = format_match (txt, mstyle_get_format (mstyle));
		if (value != NULL)
			value_release (value);
		else
			expr_txt = gnm_expr_char_start_p (txt);

		/* NOTE : do not modify gnm_expr_char_start_p to exclude "-"
		 * it _can_ start an expression, which is required for rangesel
		 * it just isn't an expression. */
		if (expr_txt != NULL && *expr_txt != '\0' && strcmp (expr_txt, "-")) {
			GnmExpr const *expr = NULL;
			ParsePos    pp;
			ParseError  perr;

			parse_pos_init_editpos (&pp, sv);
			parse_error_init (&perr);
			expr = gnm_expr_parse_str (expr_txt,
				&pp, GNM_EXPR_PARSE_DEFAULT, gnm_expr_conventions_default, &perr);
			/* Try adding a single extra closing paren to see if it helps */
			if (expr == NULL && perr.err != NULL &&
			    perr.err->code == PERR_MISSING_PAREN_CLOSE) {
				ParseError tmp_err;
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
				 */
				gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel),
						      GTK_WIDGET (wbcg_get_entry (wbcg)));

				if (perr.begin_char != 0 || perr.end_char != 0) {
					int offset = expr_txt - txt;
					gtk_entry_select_region (wbcg_get_entry (wbcg),
						offset + perr.begin_char,
						offset + perr.end_char);
				} else
					gtk_editable_set_position (
						GTK_EDITABLE (wbcg_get_entry (wbcg)), -1);

				reedit = wb_control_validation_msg (WORKBOOK_CONTROL (wbcg),
					VALIDATION_STYLE_PARSE_ERROR, NULL, perr.err->message);

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
		 * a validation condition depends on the new value.
		 */
		cmd_set_text (wbc, sheet, &sv->edit_pos, txt);
		valid = validation_eval (wbc, mstyle, sheet, &sv->edit_pos);

		if (free_txt != NULL)
			g_free (free_txt);

		if (valid != VALIDATION_STATUS_VALID) {
			accept = FALSE;
			command_undo (wbc);
			if (valid == VALIDATION_STATUS_INVALID_EDIT) {
				gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel),
						      GTK_WIDGET (wbcg_get_entry (wbcg)));
				  return FALSE;
			}
		} else
			accept = TRUE;
	} else {
		if (sv == wb_control_cur_sheet_view (wbc)) {
			/* Redraw the cell contents in case there was a span */
			Range tmp; tmp.start = tmp.end = sv->edit_pos;
			sheet_range_bounding_box (sv->sheet, &tmp);
			sv_redraw_range (wb_control_cur_sheet_view (wbc), &tmp);
		}

		/* Reload the entry widget with the original contents */
		wb_view_edit_line_set (wbv, wbc);
	}

	/* Stop editing */
	wbcg->wb_control.editing = FALSE;
	wbcg->wb_control.editing_sheet = NULL;
	wbcg->wb_control.editing_cell = NULL;

	if (wbcg->edit_line.guru != NULL) {
		GtkWidget *w = wbcg->edit_line.guru;
		wbcg_edit_detach_guru (wbcg);
		gtk_widget_destroy (w);
	}
	wb_control_edit_set_sensitive (wbc, FALSE, TRUE);

	/* restore focus to original sheet in case things were being selected
	 * on a different page.  Do no go through the view, rangesel is
	 * specific to the control.
	 */
	wb_control_sheet_focus (WORKBOOK_CONTROL (wbcg), sheet);

	/* Only the edit sheet has an edit cursor */
	scg_edit_stop (wbcg_cur_scg (wbcg));

	wbcg_auto_complete_destroy (wbcg);

	/* really necessary ? the commands should have taken care of it */
	if (accept && sheet->workbook->recalc_auto)
		workbook_recalc (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));

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
entry_changed (GtkEntry *entry, void *data)
{
	WorkbookControlGUI *wbcg = data;
	char const *text;
	int text_len;
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));

	text = gtk_entry_get_text (wbcg_get_entry (wbcg));
	text_len = strlen (text);

	if (text_len > wbcg->auto_max_size)
		wbcg->auto_max_size = text_len;

	/*
	 * Turn off auto-completion if the user has edited or the text
	 * does not begin with an alphabetic character.
	 */
	if (text_len < wbcg->auto_max_size ||
	    !g_unichar_isalpha (g_utf8_get_char (text)))
		wbcg->auto_completing = FALSE;

	if (wbv->do_auto_completion && wbcg->auto_completing)
		complete_start (wbcg->auto_complete, text);
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
	Cell *cell;
	char *text = NULL;
	int col, row;
	WorkbookView *wbv;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	if (wbcg_is_editing (wbcg))
		return TRUE;

	/* Avoid recursion, and do not begin editing if a guru is up */
	if (inside_editing || wbcg_edit_has_guru (wbcg))
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
	    mstyle_get_content_locked (sheet_style_get (sv->sheet, col, row))) {
		char *pos =  g_strdup_printf ( _("%s!%s is locked"),
			sv->sheet->name_quoted, cell_coord_name (col, row));
		gnumeric_error_invalid (COMMAND_CONTEXT (wbcg), pos,
			wb_view_is_protected (wbv, FALSE)
			 ? _("Unprotect the workbook to enable editing.")
			 : _("Unprotect the sheet to enable editing."));
		inside_editing = FALSE;
		g_free (pos);
		return FALSE;
	}

	application_clipboard_unant ();
	wb_control_edit_set_sensitive (WORKBOOK_CONTROL (wbcg), TRUE, FALSE);

	cell = sheet_cell_get (sv->sheet, col, row);
	if (!blankp) {
		if (cell != NULL)
			text = cell_get_entered_text (cell);

		/* If this is part of an array we need to remove the
		 * '{' '}' and the size information from the display.
		 * That is not actually part of the parsable expression.
		 */
		if (NULL != cell_is_array (cell))
			gtk_entry_set_text (wbcg_get_entry (wbcg), text);
	} else
		gtk_entry_set_text (wbcg_get_entry (wbcg), "");

	gnm_expr_entry_set_scg (wbcg->edit_line.entry, scg);
	gnm_expr_entry_set_flags (wbcg->edit_line.entry,
		GNM_EE_SHEET_OPTIONAL,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL);
	scg_edit_start (scg);

	/* Redraw the cell contents in case there was a span */
	sheet_redraw_region (sv->sheet, col, row, col, row);

	/* Activate auto-completion if this is not an expression */
	if (wbv->do_auto_completion &&
	    (text == NULL || g_unichar_isalpha (g_utf8_get_char (text)))) {
		wbcg->auto_complete = complete_sheet_new (
			sv->sheet, col, row,
			workbook_edit_complete_notify, wbcg);
		wbcg->auto_completing = TRUE;
		wbcg->auto_max_size = 0;
	} else
		wbcg->auto_complete = NULL;

	/* Give the focus to the edit line */
	if (!cursorp) {
		GtkEntry *w = wbcg_get_entry (wbcg);
		gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel), GTK_WIDGET (w));
		/* the gtk-entry-select-on-focus property of GtkEntry is not
		 * what we want here.  Rather than trying some gtk-2.2 specific
		 * magic just undo the selection change here */
		gtk_editable_select_region (GTK_EDITABLE (w), -1, -1);
	}

	wbcg->wb_control.editing = TRUE;
	wbcg->wb_control.editing_sheet = sv->sheet;
	wbcg->wb_control.editing_cell = cell;

	/* If this assert fails, it means editing was not shut down
	 * properly before
	 */
	g_return_val_if_fail (wbcg->edit_line.signal_changed == -1, TRUE);
	wbcg->edit_line.signal_changed = g_signal_connect (
		G_OBJECT (wbcg_get_entry (wbcg)),
		"changed",
		G_CALLBACK (entry_changed), wbcg);

	if (text)
		g_free (text);

	inside_editing = FALSE;
	return TRUE;
}

GtkEntry *
wbcg_get_entry (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (wbcg != NULL, NULL);

	return gnm_expr_entry_get_entry (wbcg->edit_line.entry);
}

GnumericExprEntry *
wbcg_get_entry_logical (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (wbcg != NULL, NULL);

	if (wbcg->edit_line.temp_entry != NULL)
		return wbcg->edit_line.temp_entry;

	return wbcg->edit_line.entry;
}

void
wbcg_set_entry (WorkbookControlGUI *wbcg, GnumericExprEntry *entry)
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
	application_clipboard_unant ();

	wbcg->edit_line.guru = guru;
	gtk_entry_set_editable (wbcg_get_entry (wbcg), FALSE);
	wb_control_edit_set_sensitive (wbc, FALSE, FALSE);
	wb_control_menu_state_update (wbc, MS_GURU_MENU_ITEMS);

}

static void
cb_guru_set_focus (GtkWidget *window, GtkWidget *focus_widget,
		   WorkbookControlGUI *wbcg)
{
	GnumericExprEntry *gee = NULL;
	if (focus_widget != NULL && IS_GNUMERIC_EXPR_ENTRY (focus_widget->parent))
		gee = GNUMERIC_EXPR_ENTRY (focus_widget->parent);
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
					 GnumericExprEntry *gee)
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

	if (wbcg->edit_line.guru == NULL)
		return;

	wbcg_set_entry (wbcg, NULL);
	wbcg->edit_line.guru = NULL;
	gtk_entry_set_editable (wbcg_get_entry (wbcg), TRUE);
	wb_control_edit_set_sensitive (wbc, FALSE, TRUE);
	wb_control_menu_state_update (wbc, MS_GURU_MENU_ITEMS);
}

GtkWidget *
wbcg_edit_has_guru (WorkbookControlGUI const *wbcg)
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
	GtkEntry *entry = wbcg_get_entry (wbcg);
	int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (entry));
	char const *text = gtk_entry_get_text (entry);
	gboolean equal;

	/*
	 * Ok, this sucks, ideally, I would like to do this test:
	 * cursor_pos != strlen (text) to mean "user is editing,
	 * hence, turn auto-complete off".
	 *
	 * But there is a complication: they way GtkEntry is implemented,
	 * cursor_pos is not updated until after the text has been inserted
	 * (this in turn emits the ::changed signal.
	 *
	 * So this is why it is so hairy and not really reliable.
	 */
	{
		GdkEvent *event = gtk_get_current_event ();
		gboolean perform_test = FALSE;

		if (event && event->type != GDK_KEY_PRESS)
			perform_test = TRUE;

		if (perform_test)
			if (cursor_pos != (int)strlen (text))
				wbcg->auto_completing = FALSE;

		if (event)
			gdk_event_free (event);
	}

	if (!wbcg->auto_completing)
		return FALSE;

	if (wbcg->auto_complete_text == NULL)
		return FALSE;

	equal = (strncmp (text, wbcg->auto_complete_text, strlen (text)) == 0);

	return equal;
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

	wbcg->edit_line.entry = gnumeric_expr_entry_new (wbcg, FALSE);
	wbcg->edit_line.temp_entry = NULL;
	wbcg->edit_line.guru = NULL;
	wbcg->edit_line.signal_changed = -1;
	wbcg->toolbar_sensitivity_timer = 0;
}

void
wbcg_edit_dtor (WorkbookControlGUI *wbcg)
{
	wbcg_toolbar_timer_clear (wbcg);
}
