/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gui-clipboard.c: Implements the X11 based copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jody@gnome.org)
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
x_clipboard_to_cell_region (WorkbookControlGUI *wbcg,
			    guchar const *src, int len)
{
	DialogStfResult_t *dialogresult;
	CellRegion *cr = NULL;
	char *data;

	data = g_new (char, len + 1);
	memcpy (data, src, len);
	data[len] = 0;

	if (stf_parse_convert_to_unix (data) < 0) {
		g_free (data);
		g_warning (_("Error while trying to pre-convert clipboard data"));
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
 * x_clipboard_received:
 *
 * Invoked when the selection has been received by our application.
 * This is triggered by a call we do to gtk_clipboard_request_contents.
 */
static void
x_clipboard_received (GtkClipboard *clipboard, GtkSelectionData *sel,
		      gpointer closure)
{
	WorkbookControlGUI *wbcg = closure;
	WorkbookControl	   *wbc  = WORKBOOK_CONTROL (wbcg);
	PasteTarget	   *pt   = wbcg->clipboard_paste_callback_data;
	CellRegion *content = NULL;
	gboolean clear_content = FALSE;

	/* The data is a list of atoms */
	if (sel->target == gdk_atom_intern (TARGETS_ATOM_NAME, FALSE)) {
		/* in order of preference */
		static char const *formats [] = {
			GNUMERIC_ATOM_NAME,
			/* "text/html", */
			"UTF8_STRING",
			"COMPOUND_TEXT",
			NULL
		};

		GdkAtom const *targets = (GdkAtom *) sel->data;
		unsigned const atom_count = (sel->length / sizeof (GdkAtom));
		unsigned i, j;

		/* Nothing on clipboard? */
		if (sel->length < 0) {
			if (wbcg->clipboard_paste_callback_data != NULL) {
				g_free (wbcg->clipboard_paste_callback_data);
				wbcg->clipboard_paste_callback_data = NULL;
			}
			return;
		}

		/* what do we like best */
		for (i = 0 ; formats[i] != NULL ; i++) {
			GdkAtom atom = gdk_atom_intern (formats[i], FALSE);

			/* do they offer what we want ? */
			for (j = 0; j < atom_count && targets [j] != atom ; j++)
				;
			if (j < atom_count) {
				gtk_clipboard_request_contents (clipboard, atom,
					x_clipboard_received, wbcg);
				break;
			}
		}
		/* If all else fails try STRING */
		if (formats[i] == NULL)
			gtk_clipboard_request_contents (clipboard, GDK_SELECTION_TYPE_STRING,
				 x_clipboard_received, wbcg);

		/* NOTE : We don't release the date resources
		 * (wbcg->clipboard_paste_callback_data), the reason for
		 * this is that we will actually call ourself again
		 * (indirectly through the gtk_clipboard_request_contents
		 * and that call _will_ free the data (and also needs it).
		 * So we won't release anything.
		 */
	} else if (sel->target == gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE)) {
		/* The data is the gnumeric specific XML interchange format */
		content = xml_cellregion_read (wbc, pt->sheet, sel->data, sel->length);
#if 0
	} else if (sel->target == atom_html) {
		content = html_cellregion_read (wbc, pt->sheet, sel->data, sel->length);
#endif
	} else {  /* The data is probably in String format */
		clear_content = TRUE;
		/* Did X provide any selection? */
		if (sel->length > 0)
			content = x_clipboard_to_cell_region (wbcg, sel->data, sel->length);
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

	if (clear_content && wbcg->clipboard_paste_callback_data != NULL) {
		g_free (wbcg->clipboard_paste_callback_data);
		wbcg->clipboard_paste_callback_data = NULL;
	}
}

/**
 * x_clipboard_get_cb
 *
 * Callback invoked when another application requests we render the selection.
 */
static void
x_clipboard_get_cb (GtkClipboard *gclipboard, GtkSelectionData *selection_data,
		     guint info, WorkbookControl *wbc)
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
	if (selection_data->target == atom_gnumeric
#if 0
	    /* The 'info' parameter is junk with gtk_clipboard_set_with_owner
	       on gtk 2.0  */
	    && info == GNUMERIC_ATOM_INFO
#endif
		) {
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
 * x_clipboard_clear_cb:
 *
 * Callback for the "we lost the X selection" signal. Only invoked for
 * "primary".
 */
static gint
x_clipboard_clear_cb (GtkClipboard *clipboard,
		      gpointer      data)
{
	application_clipboard_clear (FALSE);

	return TRUE;
}

void
x_request_clipboard (WorkbookControlGUI *wbcg, PasteTarget const *pt)
{
	PasteTarget *new_pt;
	GtkClipboard *primary = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	GdkAtom atom_targets  = gdk_atom_intern (TARGETS_ATOM_NAME, FALSE);

	if (wbcg->clipboard_paste_callback_data != NULL)
		g_free (wbcg->clipboard_paste_callback_data);

	new_pt = g_new (PasteTarget, 1);
	*new_pt = *pt;
	wbcg->clipboard_paste_callback_data = new_pt;

	/* Query the formats, This will callback x_clipboard_received */
	gtk_clipboard_request_contents (primary, atom_targets,
		  x_clipboard_received, wbcg);
}

gboolean
x_claim_clipboard (WorkbookControlGUI *wbcg)
{
	gboolean clipboard_owner_set;
	gboolean primary_owner_set;
	static const GtkTargetEntry targets[] = {
		{ (char *) GNUMERIC_ATOM_NAME,  GTK_TARGET_SAME_WIDGET, GNUMERIC_ATOM_INFO },
		/* { (char *)"text/html", 0, 0 }, */
		{ (char *)"UTF8_STRING", 0, 0 },
		{ (char *)"COMPOUND_TEXT", 0, 0 },
		{ (char *)"STRING", 0, 0 },
	};
	GtkClipboard *clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

	return gtk_clipboard_set_with_owner (clipboard,
		targets, G_N_ELEMENTS (targets),
		(GtkClipboardGetFunc) x_clipboard_get_cb,
		(GtkClipboardClearFunc) x_clipboard_clear_cb,
		G_OBJECT (wbcg));
}
