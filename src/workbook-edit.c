/*
 * workbook-edit.c: Keeps track of the cell editing process.
 *
 * Author:
 *   Miguel de Icaza (miguel@helixcode.com)
 *
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include <workbook.h>
#include <workbook-private.h>
#include <workbook-edit.h>
#include "complete-sheet.h"
#include "application.h"
#include "commands.h"
#include "sheet-view.h"
#include "cell.h"
#include "rendered-value.h"
#include "gnumeric-util.h"

/*
 * Shuts down the auto completion engine
 */
void
workbook_auto_complete_destroy (Workbook *wb)
{
	WorkbookPrivate *wb_priv = wb->priv;

	if (wb_priv->auto_complete_text){
		g_free (wb_priv->auto_complete_text);
		wb_priv->auto_complete_text = NULL;
	}

	gtk_signal_disconnect (GTK_OBJECT (workbook_get_entry (wb)),
			       wb_priv->edit_line.signal_changed);
	wb_priv->edit_line.signal_changed = -1;
	
	if (application_use_auto_complete_get ()){
		gtk_object_unref (GTK_OBJECT (wb_priv->auto_complete));
		wb_priv->auto_complete = NULL;
	} else
		g_assert (wb_priv->auto_complete == NULL);

	wb_priv->auto_completing = FALSE;
	
}

void
workbook_finish_editing (Workbook *wb, gboolean const accept)
{
	Sheet *sheet;

	g_return_if_fail (wb != NULL);

	if (!wb->editing)
		return;

	g_return_if_fail (wb->editing_sheet != NULL);

	/* Stop editing */
	sheet = wb->editing_sheet;
	wb->editing = FALSE;
	wb->editing_sheet = NULL;
	wb->editing_cell = NULL;

	/* These are only sensitive while editing */
	gtk_widget_set_sensitive (wb->priv->ok_button, FALSE);
	gtk_widget_set_sensitive (wb->priv->cancel_button, FALSE);

	/* Toolbars are insensitive while editing */
	gtk_widget_set_sensitive (wb->priv->func_button, TRUE);
	gtk_widget_set_sensitive (wb->priv->standard_toolbar, TRUE);
	gtk_widget_set_sensitive (wb->priv->format_toolbar, TRUE);

	/* Save the results before changing focus */
	if (accept) {
		/* TODO: Get a context */
		const char *txt = workbook_edit_get_display_text (sheet->workbook);

		
		/* Store the old value for undo */
		/*
		 * TODO: What should we do in case of failure ?
		 * maybe another parameter that will force an end ?
		 */
		cmd_set_text (workbook_command_context_gui (wb),
			      sheet, &sheet->cursor.edit_pos, txt);
	} else {
		/* Redraw the cell contents in case there was a span */
		int const c = sheet->cursor.edit_pos.col;
		int const r = sheet->cursor.edit_pos.row;
		sheet_redraw_cell_region (sheet, c, r, c, r);

		/* Reload the entry widget with the original contents */
		sheet_load_cell_val (sheet);
	}

	/*
	 * restore focus to original sheet in case things were being selected
	 * on a different page
	 */
	workbook_focus_sheet (sheet);

	/*
	 * FIXME :
	 * If user was editing on the input line, get the focus back
	 * This code was taken from workbook_focus_current_sheet which also needs
	 * fixing.  There can be multiple views.  We have no business at all
	 * assigning focus to the first.
	 */
	gtk_window_set_focus (GTK_WINDOW (wb->toplevel),
			      SHEET_VIEW (sheet->sheet_views->data)->sheet_view);

	/* Only the edit sheet has an edit cursor */
	sheet_stop_editing (sheet);

	workbook_auto_complete_destroy (wb);
	
	if (accept)
		workbook_recalc (wb);
}

static void
workbook_edit_complete_notify (const char *text, void *closure)
{
	Workbook *wb = closure;

	if (wb->priv->auto_complete_text)
		g_free (wb->priv->auto_complete_text);

	wb->priv->auto_complete_text = g_strdup (text);
}

static void
entry_changed (GtkEntry *entry, void *data)
{
	Workbook *wb = data;
	char *text;
	int text_len;

	
	text = gtk_entry_get_text (workbook_get_entry (wb));
	text_len = strlen (text);

	if (text_len > wb->priv->auto_max_size)
		wb->priv->auto_max_size = text_len;

	/*
	 * Turn off auto-completion if the user has edited
	 */
	if (text_len < wb->priv->auto_max_size)
		wb->priv->auto_completing = FALSE;

	if (application_use_auto_complete_get () && wb->priv->auto_completing)
		complete_start (wb->priv->auto_complete, text);
}

static void
workbook_edit_auto_complete_init (Workbook *wb)
{
}

/**
 * workbook_start_editing_at_cursor:
 *
 * @wb:       The workbook to be edited.
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
workbook_start_editing_at_cursor (Workbook *wb, gboolean blankp,
				  gboolean cursorp)
{
	Sheet *sheet;
	Cell *cell;

	g_return_if_fail (wb != NULL);

	sheet = wb->current_sheet;
	g_return_if_fail (sheet != NULL);

	application_clipboard_unant ();

	cell = sheet_cell_get (sheet,
			       sheet->cursor.edit_pos.col,
			       sheet->cursor.edit_pos.row);

	if (blankp)
		gtk_entry_set_text (workbook_get_entry (wb), "");
	else {
		/*
		 * If this is part of an array we need to remove the
		 * '{' '}' and the size information from the display.
		 * That is not actually part of the parsable expression.
		 */
		if (NULL != cell_is_array (cell)) {
			char *text = cell_get_entered_text (cell);

			gtk_entry_set_text (workbook_get_entry (wb), text);
			g_free (text);
		}
	}

	/* Redraw the cell contents in case there was a span */
	if (cursorp) {
		int const col = sheet->cursor.edit_pos.col;
		int const row = sheet->cursor.edit_pos.row;

		sheet_create_edit_cursor (sheet);
		sheet_redraw_cell_region (sheet, col, row, col, row);

		/* Activate auto-completion */
		if (application_use_auto_complete_get ()) {
			wb->priv->auto_complete = complete_sheet_new (
				sheet, col, row,
				workbook_edit_complete_notify, wb);
			wb->priv->auto_completing = TRUE;
			wb->priv->auto_max_size = 0;
		} else
			wb->priv->auto_complete = NULL;
	} else
		/* Give the focus to the edit line */
		gtk_window_set_focus (GTK_WINDOW (wb->toplevel), GTK_WIDGET (workbook_get_entry (wb)));


	/* TODO : Should we reset like this ? probably */
	wb->use_absolute_cols = wb->use_absolute_rows = FALSE;

	wb->editing = TRUE;
	wb->editing_sheet = sheet;
	wb->editing_cell = cell;

	/* These are only sensitive while editing */
	gtk_widget_set_sensitive (wb->priv->ok_button, TRUE);
	gtk_widget_set_sensitive (wb->priv->cancel_button, TRUE);

	/* Toolbars are insensitive while editing */
	gtk_widget_set_sensitive (wb->priv->func_button, FALSE);
	gtk_widget_set_sensitive (wb->priv->standard_toolbar, FALSE);
	gtk_widget_set_sensitive (wb->priv->format_toolbar, FALSE);

	/*
	 * If this assert fails, it means editing was not shut down
	 * properly before
	 */
	g_assert (wb->priv->edit_line.signal_changed == -1);
	wb->priv->edit_line.signal_changed = gtk_signal_connect (
		GTK_OBJECT (workbook_get_entry (wb)), "changed",
		GTK_SIGNAL_FUNC (entry_changed), wb);
}

GtkEntry *
workbook_get_entry (Workbook const *wb)
{
	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (wb->priv != NULL, NULL);

	/* TODO: If there is an function druid up use the edit line from there */
	return wb->priv->edit_line.entry;
}

gboolean
workbook_editing_expr (Workbook const *wb)
{
	return gnumeric_entry_at_subexpr_boundary_p (workbook_get_entry (wb));
}

gboolean
workbook_auto_completing (Workbook *wb)
{
	return wb->priv->auto_completing;
}

static gboolean
auto_complete_matches (Workbook *wb)
{
	GtkEntry *entry = workbook_get_entry (wb);
	int cursor_pos = GTK_EDITABLE (entry)->current_pos;
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
				wb->priv->auto_completing = FALSE;
		
	}

	if (!wb->priv->auto_completing)
		return FALSE;
	
	if (!wb->priv->auto_complete_text)
		return FALSE;

	equal = (strncmp (text, wb->priv->auto_complete_text, strlen (text)) == 0);

	return equal;
}

/*
 * Returns the text that must be shown by the editing entry, takes
 * into account the auto-completion text.
 */
const char *
workbook_edit_get_display_text (Workbook *wb)
{
	if (auto_complete_matches (wb))
		return wb->priv->auto_complete_text;
	else
		return gtk_entry_get_text (workbook_get_entry (wb));
}

/*
 * Initializes the Workbook entry
 */
void
workbook_edit_init (Workbook *wb)
{
	g_assert (wb != NULL);
	g_assert (IS_WORKBOOK (wb));
	g_assert (wb->priv->edit_line.entry == NULL);

	wb->priv->edit_line.entry = GTK_ENTRY (gtk_entry_new ());
	wb->priv->edit_line.signal_changed = -1;
}
