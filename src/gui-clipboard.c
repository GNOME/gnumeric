/*
 * gui-clipboard.c: Implements the X11 based copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gui-clipboard.h"

#include "gui-util.h"
#include "clipboard.h"
#include "selection.h"
#include "application.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "ranges.h"
#include "sheet.h"
#include "sheet-style.h"
#include "commands.h"
#include "xml-io.h"
#include "value.h"
#include "dialog-stf.h"
#include "stf-parse.h"
#include "mstyle.h"

#include <libgnome/gnome-i18n.h>
#include <libxml/globals.h>
#include <locale.h>
#include <string.h>
#include <ctype.h>

/* The name of our clipboard atom and the 'magic' info number */
#define GNUMERIC_ATOM_NAME "application/x-gnumeric"
#define GNUMERIC_ATOM_INFO 2001

/* The name of the TARGETS atom (don't change unless you know what you are doing!) */
#define TARGETS_ATOM_NAME "TARGETS"

static CellRegion *
x_selection_to_cell_region (WorkbookControlGUI *wbcg, const guchar *src, int len)
{
	DialogStfResult_t *dialogresult;
	CellRegion *cr = NULL;
	char *data;
	char const *c;

	data = g_new (char, len + 1);
	memcpy (data, src, len);
	data[len] = 0;

	if (!stf_parse_convert_to_unix (data)) {
		g_free (data);
		g_warning (_("Error while trying to pre-convert clipboard data"));
		return cellregion_new (NULL);
	}

	if ((c = stf_parse_is_valid_data (data)) != NULL) {
		char *message;

		message = g_strdup_printf (_("The data on the clipboard does not seem to be valid text.\nThe character '%c' (ASCII decimal %d) was encountered.\nMost likely your locale settings are wrong."),
					   *c, (int) ((unsigned char)*c));
		g_warning (message);
		g_free (message);

		g_free (data);

		return cellregion_new (NULL);
	}

	/*
	 * See if this is a "single line + line end" or a "multiline"
	 * string. If this is _not_ the case we won't invoke the STF, it is
	 * unlikely that the user will actually need it in this case.
	 * NOTE: This is making an assumption on what the user 'wants', this
	 * is not really a good thing. We should put this in a config dialog.
	 */
	if (strchr (data, '\n') == NULL) {
		CellCopy *ccopy;

		ccopy = g_new (CellCopy, 1);
		ccopy->type = CELL_COPY_TYPE_TEXT;
		ccopy->col_offset = 0;
		ccopy->row_offset = 0;
		ccopy->u.text = g_strdup (data);
		ccopy->comment = NULL;

		cr = cellregion_new (NULL);
		cr->content = g_list_prepend (cr->content, ccopy);
		cr->cols = cr->rows = 1;
	} else {
		dialogresult = stf_dialog (wbcg, "clipboard", data);

		if (dialogresult != NULL) {
			GSList *iterator;
			int col, rowcount;

			stf_parse_options_set_lines_to_parse (dialogresult->parseoptions, dialogresult->lines);
			cr = stf_parse_region (dialogresult->parseoptions, dialogresult->newstart);

			if (cr == NULL) {
				g_free (data);
				g_warning (_("Parse error while trying to parse data into cellregion"));
				return cellregion_new (NULL);
			}

			iterator = dialogresult->formats;
			col = 0;
			rowcount = stf_parse_get_rowcount (dialogresult->parseoptions, dialogresult->newstart);
			while (iterator) {
				StyleRegion *sr = g_new (StyleRegion, 1);

				sr->range.start.col = col;
				sr->range.start.row = 0;
				sr->range.end.col   = col;
				sr->range.end.row   = rowcount;
				sr->style = mstyle_new_default ();
				mstyle_set_format (sr->style, iterator->data);

				cr->styles = g_slist_prepend (cr->styles, sr);

				iterator = g_slist_next (iterator);

				col++;
			}

			stf_dialog_result_free (dialogresult);
		} else {
			g_free (data);
			return cellregion_new (NULL);
		}
	}

	g_free (data);

	return cr;
}

/**
 * x_selection_received:
 *
 * Invoked when the selection has been received by our application.
 * This is triggered by a call we do to gtk_selection_convert.
 */
static void
x_selection_received (GtkWidget *widget, GtkSelectionData *sel, guint time,
		      WorkbookControlGUI *wbcg)
{
	GdkAtom atom_targets  = gdk_atom_intern (TARGETS_ATOM_NAME, FALSE);
	GdkAtom atom_gnumeric = gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE);
	GdkAtom atom_html = gdk_atom_intern ("text/html", FALSE);
	PasteTarget *pt = wbcg->clipboard_paste_callback_data;
	CellRegion *content = NULL;
	gboolean region_pastable = FALSE;
	gboolean free_closure = FALSE;
	WorkbookControl	*wbc = WORKBOOK_CONTROL (wbcg);

	if (sel->target == atom_targets) { /* The data is a list of atoms */
		GdkAtom *atoms = (GdkAtom *) sel->data;
		gboolean gnumeric_format, html_format;
		GtkWidget *toplevel;
		int atom_count = (sel->length / sizeof (GdkAtom));
		int i;

		/* Nothing on clipboard? */
		if (sel->length < 0) {

			if (wbcg->clipboard_paste_callback_data != NULL) {
				g_free (wbcg->clipboard_paste_callback_data);
				wbcg->clipboard_paste_callback_data = NULL;
			}
			return;
		}

		gnumeric_format = html_format = FALSE;
		/* Does the remote app support Gnumeric xml ? */
		for (i = 0; i < atom_count; i++)
			if (atoms[i] == atom_gnumeric)
				gnumeric_format = TRUE;
			else if (atoms[i] == atom_html)
				html_format = TRUE;

		/* NOTE : We don't release the date resources
		 * (wbcg->clipboard_paste_callback_data), the
		 * reason for this is that we will actually call ourself
		 * again (indirectly through the gtk_selection_convert
		 * and that call _will_ free the data (and also needs it).
		 * So we won't release anything.
		 */

		/* If another instance of gnumeric put this data on the clipboard
		 * request the data in gnumeric XML format. If not, just
		 * request it in string format
		 */
		toplevel = GTK_WIDGET (wbcg_toplevel (wbcg));
		if (gnumeric_format)
			gtk_selection_convert (toplevel,
					       GDK_SELECTION_PRIMARY,
					       atom_gnumeric, time);
#if 0
		else if (html_format)
			gtk_selection_convert (toplevel,
					       GDK_SELECTION_PRIMARY,
					       atom_html, time);
#endif
		else
			gtk_selection_convert (toplevel,
					       GDK_SELECTION_PRIMARY,
					       GDK_SELECTION_TYPE_STRING, time);

	} else if (sel->target == atom_gnumeric) {
		/* The data is the gnumeric specific XML interchange format */
		content = xml_cellregion_read (wbc, pt->sheet, sel->data, sel->length);
#if 0
	} else if (sel->target == atom_html) {
		content = html_cellregion_read (wbc, pt->sheet, sel->data, sel->length);
#endif
	} else {  /* The data is probably in String format */
		free_closure = TRUE;
		/* Did X provide any selection? */
		if (sel->length > 0)
			content = x_selection_to_cell_region (wbcg, sel->data, sel->length);
	}

	if (content != NULL) {
		/*
		 * if the conversion from the X selection -> a cellregion
		 * was canceled this may have content sized -1,-1
		 */
		if (content->cols > 0 && content->rows > 0)
			cmd_paste_copy (wbc, pt, content);

		/* Release the resources we used */
		if (sel->length >= 0)
			cellregion_free (content);
	}

	if (region_pastable || free_closure) {
		/* Remove our used resources */
		if (wbcg->clipboard_paste_callback_data != NULL) {
			g_free (wbcg->clipboard_paste_callback_data);
			wbcg->clipboard_paste_callback_data = NULL;
		}
	}
}

/**
 * x_selection_handler:
 *
 * Callback invoked when another application requests we render the selection.
 */
static void
x_selection_handler (GtkWidget *widget, GtkSelectionData *selection_data,
		     guint info, guint time, WorkbookControl *wbc)
{
	gboolean to_gnumeric = FALSE, content_needs_free = FALSE;
	CellRegion *clipboard = application_clipboard_contents_get ();
	GdkAtom atom_gnumeric = gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE);
	Sheet *sheet = application_clipboard_sheet_get ();
	Range const *a = application_clipboard_area_get ();

	/*
	 * Not sure how to handle this, not sure what purpose this has has
	 * (sheet being NULL). I think it is here to indicate that the selection
	 * just has been cut.
	 */
	if (!sheet)
		return;

	/*
	 * If the content was marked for a cut we need to copy it for pasting
	 * we clear it later on, because if the other application (the one that
	 * requested we render the data) is another instance of gnumeric
	 * we need the selection to remain "intact" (not cleared) so we can
	 * render it to the Gnumeric XML clipboard format
	 */
	if (clipboard == NULL) {
		content_needs_free = TRUE;
		clipboard = clipboard_copy_range (sheet, a);
	}

	g_return_if_fail (clipboard != NULL);

	/*
	 * Check whether the other application wants gnumeric XML format
	 * in fact we only have to check the 'info' variable, however
	 * to be absolutely sure I check if the atom checks out too
	 */
	if (selection_data->target == atom_gnumeric && info == GNUMERIC_ATOM_INFO) {
		int buffer_size;
		xmlChar *buffer = xml_cellregion_write (wbc, clipboard, &buffer_size);
		gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING, 8,
					(guchar *) buffer, buffer_size);
		xmlFree (buffer);
		to_gnumeric = TRUE;
	} else {
		char *rendered_selection = cellregion_to_string (clipboard);

		gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING, 8,
					(guchar *) rendered_selection, strlen (rendered_selection));

		g_free (rendered_selection);
	}

	/*
	 * If this was a CUT operation we need to clear the content that was pasted
	 * into another application and release the stuff on the clipboard
	 */
	if (content_needs_free) {

		/* If the other app was a gnumeric, emulate a cut */
		if (to_gnumeric) {
			sheet_clear_region (wbc, sheet,
				a->start.col, a->start.row,
				a->end.col,   a->end.row,
				CLEAR_VALUES|CLEAR_COMMENTS|CLEAR_RECALC_DEPS);
			application_clipboard_clear (TRUE);
		}

		cellregion_free (clipboard);
	}
}

/**
 * x_selection_clear:
 *
 * Callback for the "we lost the X selection" signal
 */
static gint
x_selection_clear (GtkWidget *widget, GdkEventSelection *event,
		   WorkbookControl *wbc)
{
	/* we have already lost the selection, no need to clear it */
	application_clipboard_clear (FALSE);

	return TRUE;
}

void
x_request_clipboard (WorkbookControlGUI *wbcg, PasteTarget const *pt, guint32 time)
{
	PasteTarget *new_pt;

	if (wbcg->clipboard_paste_callback_data != NULL)
		g_free (wbcg->clipboard_paste_callback_data);

	new_pt = g_new (PasteTarget, 1);
	*new_pt = *pt;
	wbcg->clipboard_paste_callback_data = new_pt;

	/* Query the formats, This will callback x_selection_received */
	gtk_selection_convert (
		GTK_WIDGET (wbcg_toplevel (wbcg)),
		GDK_SELECTION_PRIMARY,
		gdk_atom_intern (TARGETS_ATOM_NAME, FALSE), time);
}

/**
 * x_clipboard_bind_workbook:
 *
 * Binds the signals related to the X selection to the Workbook
 * and initialized the clipboard data structures for the Workbook.
 */
int
x_clipboard_bind_workbook (WorkbookControlGUI *wbcg)
{
	GtkWidget *toplevel = GTK_WIDGET (wbcg_toplevel (wbcg));
	GtkTargetEntry targets;

	wbcg->clipboard_paste_callback_data = NULL;

	g_signal_connect (G_OBJECT (toplevel),
		"selection_clear_event",
		G_CALLBACK (x_selection_clear), wbcg);
	g_signal_connect (G_OBJECT (toplevel),
		"selection_received",
		G_CALLBACK (x_selection_received), wbcg);
	g_signal_connect (G_OBJECT (toplevel),
		"selection_get",
		G_CALLBACK (x_selection_handler), wbcg);

	gtk_selection_add_target (toplevel,
		GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING, 0);

	/*
	 * Our specific Gnumeric XML clipboard interchange type
	 */
	targets.target = (char *)GNUMERIC_ATOM_NAME;

	/* This is not useful, but we have to set it to something: */
	targets.flags  = GTK_TARGET_SAME_WIDGET;
	targets.info   = GNUMERIC_ATOM_INFO;

	gtk_selection_add_targets (toplevel,
				   GDK_SELECTION_PRIMARY, &targets, 1);

	return FALSE;
}
