/*
 * dialog-cell-comment.c: Dialog box for editing a cell comment
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include "gnumeric.h"
#include "gui-util.h"
#include "dialogs.h"
#include "sheet.h"
#include "cell.h"
#include "sheet-object-cell-comment.h"

void
dialog_cell_comment (WorkbookControlGUI *wbcg, Sheet *sheet, CellPos const *pos)
{
	GtkWidget   *dialog;
	GtkWidget   *textbox;
	GSList	    *comments;
	CellComment *comment = NULL;
	Range	     r;
	int v;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (pos != NULL);

	dialog = gnome_dialog_new (
		_("Cell comment"),
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL,
		NULL);

	gnome_dialog_set_default (GNOME_DIALOG(dialog), GNOME_OK);
	gtk_window_set_policy (GTK_WINDOW(dialog), FALSE, TRUE, FALSE);
	
	textbox = gtk_text_new (NULL, NULL);
	gtk_text_set_word_wrap (GTK_TEXT (textbox), TRUE);
	gtk_text_set_editable (GTK_TEXT (textbox), TRUE);

	r.start = r.end = *pos;
	comments = sheet_objects_get (sheet, &r, CELL_COMMENT_TYPE);
	if (comments) {
		gint pos = 0;
		char const *text;

		comment = CELL_COMMENT (comments->data);
		if (comment == NULL)
			g_warning ("Invalid comment");
		if (comments->next != NULL)
			g_warning ("More than one comment associated with a cell ?");

		text = cell_comment_text_get (comments->data);
		gtk_editable_insert_text (
			GTK_EDITABLE (textbox), text, strlen (text), &pos);

		g_slist_free (comments);
	}

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), textbox, TRUE, TRUE, 0);
	gtk_widget_show (textbox);
	gtk_widget_grab_focus (textbox);

	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (v == -1)
		return;

	if (v == 0) {
		char *text = gtk_editable_get_chars (GTK_EDITABLE (textbox), 0, -1);

		if (comment)
			cell_comment_text_set (comment, text);
		else
			cell_set_comment (sheet, pos, NULL, text);
		g_free (text);
		sheet_set_dirty (sheet, TRUE);
	}

	gtk_object_destroy (GTK_OBJECT (dialog));
}
