/*
 * workbook-edit.c: Keeps track of the cell editing process.
 *
 * Author:
 *   Miguel de Icaza (miguel@helixcode.com)
 *
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook.h"
#include "workbook-private.h"
#include "workbook-edit.h"
#include "complete-sheet.h"
#include "application.h"
#include "commands.h"
#include "sheet-control-gui.h"
#include "item-cursor.h"
#include "sheet.h"
#include "cell.h"
#include "expr.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "widgets/gnumeric-expr-entry.h"

#include <ctype.h>

/*
 * Shuts down the auto completion engine
 */
void
wbcg_auto_complete_destroy (WorkbookControlGUI *wbcg)
{
	if (wbcg->auto_complete_text){
		g_free (wbcg->auto_complete_text);
		wbcg->auto_complete_text = NULL;
	}

	if (wbcg->edit_line.signal_changed >= 0) {
		gtk_signal_disconnect (GTK_OBJECT (wbcg_get_entry (wbcg)),
				       wbcg->edit_line.signal_changed);
		wbcg->edit_line.signal_changed = -1;
	}

	if (wbcg->auto_complete){
		gtk_object_unref (GTK_OBJECT (wbcg->auto_complete));
		wbcg->auto_complete = NULL;
	} else
		g_assert (wbcg->auto_complete == NULL);

	wbcg->auto_completing = FALSE;

}

static void
workbook_edit_toolbars_set_sensitive (WorkbookControlGUI *wbcg, gboolean sensitive)
{
#ifdef ENABLE_BONOBO
	CORBA_Environment ev;
#endif
	static gboolean is_sensitive = TRUE;

	/* Don't disable/enable again (prevent toolbar flickering) */
	if (is_sensitive != sensitive)
		is_sensitive = sensitive;
	else
		return;

#ifdef ENABLE_BONOBO
	CORBA_exception_init (&ev);
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/StandardToolbar",
				      "sensitive", sensitive ? "1" : "0", &ev);
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/FormatToolbar",
				      "sensitive", sensitive ? "1" : "0", &ev);
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/ObjectToolbar",
				      "sensitive", sensitive ? "1" : "0", &ev);
	CORBA_exception_free (&ev);
#else
	gtk_widget_set_sensitive (wbcg->standard_toolbar, sensitive);
	gtk_widget_set_sensitive (wbcg->format_toolbar, sensitive);
	gtk_widget_set_sensitive (wbcg->object_toolbar, sensitive);
#endif

}

static void
toolbar_timer_clear (WorkbookControlGUI *wbcg)
{
	/* Remove previous ui timer */
	if (wbcg->toolbar_sensitivity_timer != 0) {
		gtk_timeout_remove (wbcg->toolbar_sensitivity_timer);
		wbcg->toolbar_sensitivity_timer = 0;
	}
}

static gboolean
cb_thaw_ui_toolbar (gpointer *data)
{
        WorkbookControlGUI *wbcg = (WorkbookControlGUI *)data;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	workbook_edit_toolbars_set_sensitive (wbcg, TRUE);
	toolbar_timer_clear (wbcg);

	return TRUE;
}

/* In milliseconds */
static void
workbook_edit_set_sensitive (WorkbookControlGUI *wbcg, gboolean flag1, gboolean flag2)
{
	/* These are only sensitive while editing */
	gtk_widget_set_sensitive (wbcg->ok_button, flag1);
	gtk_widget_set_sensitive (wbcg->cancel_button, flag1);

	gtk_widget_set_sensitive (wbcg->func_button, flag2);
	toolbar_timer_clear (wbcg);

	/* Toolbars are insensitive while editing */
	if (flag2) {
		/* We put the re-enabling of the ui on a timer */
		wbcg->toolbar_sensitivity_timer =
			gtk_timeout_add (500, /* seems a reasonable amount */
					 (GtkFunction) cb_thaw_ui_toolbar,
					 wbcg);
	} else
		workbook_edit_toolbars_set_sensitive (wbcg, flag2);
}

gboolean
wbcg_edit_finish (WorkbookControlGUI *wbcg, gboolean accept)
{
	Sheet *sheet;
	WorkbookControl *wbc;
	WorkbookView	*wbv;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	wbc = WORKBOOK_CONTROL (wbcg);
	wbv = wb_control_view (wbc);

	/* Restore the focus */
	wb_control_gui_focus_cur_sheet (wbcg);

	/* We may have a guru up even if if re not editing */
	if (wbcg->edit_line.guru != NULL)
		gtk_widget_destroy (wbcg->edit_line.guru);

	if (!wbcg->editing)
		return TRUE;

	g_return_val_if_fail (wbcg->editing_sheet != NULL, TRUE);

	sheet = wbcg->editing_sheet;

	/* Save the results before changing focus */
	if (accept) {
		char const *txt = wbcg_edit_get_display_text (wbcg);
		char const *expr_txt = gnumeric_char_start_expr_p (txt);

		if (expr_txt != NULL) {
			ParsePos    pp;
			ParseError  perr;
			ExprTree   *tree;

			parse_pos_init (&pp, wb_control_workbook (wbc), sheet,
					sheet->edit_pos.col, sheet->edit_pos.row);
			parse_error_init (&perr);

			tree = gnumeric_expr_parser (expr_txt, &pp, TRUE, FALSE, NULL, &perr);

			if (!tree) {
				/*
				 *If begin and end char are zero we'll simply
				 * put the cursor at the end, otherwise we
				 * select the region indicated.
				 */
				if (perr.begin_char == 0 && perr.end_char == 0)
					gtk_editable_set_position (
						GTK_EDITABLE (wbcg_get_entry (wbcg)), -1);
				else
					gtk_entry_select_region (
						GTK_ENTRY (wbcg_get_entry (wbcg)),
						perr.begin_char, perr.end_char);

				gnome_error_dialog_parented (perr.message, wbcg->toplevel);
				parse_error_free (&perr);

				gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel),
						      GTK_WIDGET (wbcg_get_entry (wbcg)));

				return FALSE;
			} else {
				cmd_set_text (wbc, sheet, &sheet->edit_pos, txt);
				expr_tree_unref (tree);
			}

			parse_error_free (&perr);
		} else
			/* Store the old value for undo */
			cmd_set_text (wbc, sheet, &sheet->edit_pos, txt);
	} else {
		/* Redraw the cell contents in case there was a span */
		int const c = sheet->edit_pos.col;
		int const r = sheet->edit_pos.row;
		sheet_redraw_cell_region (sheet, c, r, c, r);

		/* Reload the entry widget with the original contents */
		wb_view_edit_line_set (wbv, wbc);
	}

	/* Remove the range selection cursor if it exists */
	if (NULL != wbcg->rangesel)
		scg_rangesel_stop (wbcg->rangesel, FALSE);

	/* Stop editing */
	wbcg->editing = FALSE;
	wbcg->editing_sheet = NULL;
	wbcg->editing_cell = NULL;

	workbook_edit_set_sensitive (wbcg, FALSE, TRUE);

	/* restore focus to original sheet in case things were being selected
	 * on a different page.  Do no go through the view, rangesel is
	 * specific to the control.
	 */
	wb_control_sheet_focus (WORKBOOK_CONTROL (wbcg), sheet);

	/* Only the edit sheet has an edit cursor */
	scg_stop_editing (wb_control_gui_cur_sheet (wbcg));

	wbcg_auto_complete_destroy (wbcg);

	if (accept)
		workbook_recalc (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));

	return TRUE;
}

static void
workbook_edit_complete_notify (char const *text, void *closure)
{
	WorkbookControlGUI *wbcg = closure;

	if (wbcg->auto_complete_text)
		g_free (wbcg->auto_complete_text);

	wbcg->auto_complete_text = g_strdup (text);
}

static void
entry_changed (GtkEntry *entry, void *data)
{
	WorkbookControlGUI *wbcg = data;
	char *text;
	int text_len;


	text = gtk_entry_get_text (GTK_ENTRY (wbcg_get_entry (wbcg)));
	text_len = strlen (text);

	if (text_len > wbcg->auto_max_size)
		wbcg->auto_max_size = text_len;

	/*
	 * Turn off auto-completion if the user has edited or the text
	 * does not begin with an alphabetic character.
	 */
	if (text_len < wbcg->auto_max_size ||
	    !isalpha((unsigned char)*text))
		wbcg->auto_completing = FALSE;

	if (application_use_auto_complete () && wbcg->auto_completing)
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
 */
void
wbcg_edit_start (WorkbookControlGUI *wbcg,
			      gboolean blankp, gboolean cursorp)
{
	static gboolean inside_editing = FALSE;
	Sheet *sheet;
	SheetControlGUI *scg;
	Cell *cell;
	char *text = NULL;
	int col, row;

	g_return_if_fail (wbcg != NULL);

	if (wbcg->editing)
		return;

	/* Avoid recursion, and do not begin editing if a guru is up */
	if (inside_editing || wbcg_edit_has_guru (wbcg))
		return;

	inside_editing = TRUE;

	sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	g_return_if_fail (IS_SHEET (sheet));
	scg = wb_control_gui_cur_sheet (wbcg);
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	col = sheet->edit_pos.col;
	row = sheet->edit_pos.row;

	application_clipboard_unant ();
	workbook_edit_set_sensitive (wbcg, TRUE, FALSE);

	cell = sheet_cell_get (sheet, col, row);

	if (!blankp) {
		if (cell != NULL)
			text = cell_get_entered_text (cell);

		/*
		 * If this is part of an array we need to remove the
		 * '{' '}' and the size information from the display.
		 * That is not actually part of the parsable expression.
		 */
		if (NULL != cell_is_array (cell))
			gtk_entry_set_text (
				GTK_ENTRY (wbcg_get_entry (wbcg)), text);
	} else
		gtk_entry_set_text (GTK_ENTRY (wbcg_get_entry (wbcg)), "");

	gnumeric_expr_entry_set_scg (wbcg->edit_line.entry, scg);
	gnumeric_expr_entry_set_flags (
		wbcg->edit_line.entry, GNUM_EE_SHEET_OPTIONAL,
		GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL);
	scg_create_editor (scg);

	/* Redraw the cell contents in case there was a span */
	sheet_redraw_cell_region (sheet, col, row, col, row);

	/* Activate auto-completion if this is not an expression */
	if (application_use_auto_complete () &&
	    (text == NULL || isalpha((unsigned char)*text))) {
		wbcg->auto_complete = complete_sheet_new (
			sheet, col, row,
			workbook_edit_complete_notify, wbcg);
		wbcg->auto_completing = TRUE;
		wbcg->auto_max_size = 0;
	} else
		wbcg->auto_complete = NULL;

	/* Give the focus to the edit line */
	if (!cursorp)
		gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel),
				      GTK_WIDGET (wbcg_get_entry (wbcg)));


	wbcg->editing = TRUE;
	wbcg->editing_sheet = sheet;
	wbcg->editing_cell = cell;

	/*
	 * If this assert fails, it means editing was not shut down
	 * properly before
	 */
	g_assert (wbcg->edit_line.signal_changed == -1);
	wbcg->edit_line.signal_changed = gtk_signal_connect (
		GTK_OBJECT (wbcg_get_entry (wbcg)), "changed",
		GTK_SIGNAL_FUNC (entry_changed), wbcg);

	if (text)
		g_free (text);

	inside_editing = FALSE;
}

GnumericExprEntry *
wbcg_get_entry (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (wbcg != NULL, NULL);

	return wbcg->edit_line.entry;
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
		wbcg->edit_line.temp_entry = entry;
		scg_rangesel_stop (wb_control_gui_cur_sheet (wbcg), FALSE);
	}
}

void
wbcg_edit_attach_guru (WorkbookControlGUI *wbcg, GtkWidget *guru)
{
	g_return_if_fail (guru != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (wbcg->edit_line.guru == NULL);

	wbcg->edit_line.guru = guru;
	gtk_entry_set_editable (GTK_ENTRY (wbcg->edit_line.entry), FALSE);
	workbook_edit_set_sensitive (wbcg, FALSE, FALSE);
}

void
wbcg_edit_detach_guru (WorkbookControlGUI *wbcg)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (wbcg->edit_line.guru == NULL)
		return;

	wbcg_set_entry (wbcg, NULL);
	wbcg->edit_line.guru = NULL;
	gtk_entry_set_editable (GTK_ENTRY (wbcg->edit_line.entry), TRUE);
	workbook_edit_set_sensitive (wbcg, FALSE, TRUE);
}

gboolean
wbcg_edit_has_guru (WorkbookControlGUI const *wbcg)
{
	return (wbcg->edit_line.guru != NULL);
}

gboolean
wbcg_edit_entry_redirect_p (WorkbookControlGUI const *wbcg)
{
	return (wbcg->edit_line.temp_entry != NULL);
}

/* FIXME: Probably get rid of this altogether */
void
wbcg_edit_toggle_absolute (WorkbookControlGUI *wbcg)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	gnumeric_expr_entry_toggle_absolute (
		GNUMERIC_EXPR_ENTRY (wbcg_get_entry_logical (wbcg)));
}

gboolean
wbcg_editing_expr (WorkbookControlGUI const *wbcg)
{
	return (wbcg->edit_line.guru != NULL) ||
	    gnumeric_expr_entry_at_subexpr_boundary_p (
		    wbcg_get_entry (wbcg));
}

gboolean
wbcg_auto_completing (WorkbookControlGUI const *wbcg)
{
	return wbcg->auto_completing;
}

static gboolean
auto_complete_matches (WorkbookControlGUI *wbcg)
{
	GtkEntry *entry = GTK_ENTRY (wbcg_get_entry (wbcg));
	size_t cursor_pos = GTK_EDITABLE (entry)->current_pos;
	char *text = gtk_entry_get_text (entry);
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
			if (cursor_pos != strlen (text))
				wbcg->auto_completing = FALSE;

	}

	if (!wbcg->auto_completing)
		return FALSE;

	if (!wbcg->auto_complete_text)
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
		return gtk_entry_get_text (
			GTK_ENTRY (wbcg_get_entry (wbcg)));
}

/* Initializes the Workbook entry */
void
wbcg_edit_ctor (WorkbookControlGUI *wbcg)
{
	g_assert (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_assert (wbcg->edit_line.entry == NULL);

	wbcg->edit_line.entry =
		GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new ());
	wbcg->edit_line.temp_entry = NULL;
	wbcg->edit_line.guru = NULL;
	wbcg->edit_line.signal_changed = -1;
	wbcg->toolbar_sensitivity_timer = 0;
}

void
wbcg_edit_dtor (WorkbookControlGUI *wbcg)
{
	toolbar_timer_clear (wbcg);
}
