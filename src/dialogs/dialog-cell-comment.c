/*
 * dialog-cell-comment.c: Dialog box for editing a cell comment
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <sheet.h>
#include <cell.h>
#include <sheet-object-cell-comment.h>
#include <workbook-edit.h>
#include <ranges.h>
#include <gtk/gtk.h>

#include <libgnome/gnome-i18n.h>

#define GLADE_FILE "cell-comment.glade"
#define COMMENT_DIALOG_KEY "zoom-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	Sheet              *sheet;
	CellPos const      *pos;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GtkTextBuffer      *text;
	GladeXML           *gui;
	CellComment        *comment;
} CommentState;


static gboolean
dialog_cell_comment_destroy (GtkObject *w, CommentState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_cell_comment_cancel_clicked (GtkWidget *button, CommentState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_cell_comment_ok_clicked (GtkWidget *button, CommentState *state)
{
	GtkTextIter start;
	GtkTextIter end;
	char *text;

	gtk_text_buffer_get_bounds  (state->text, &start, &end);
	text = gtk_text_buffer_get_text (state->text, &start, &end, TRUE);

	if (state->comment)
		cell_comment_text_set (state->comment, text);
	else
		cell_set_comment (state->sheet, state->pos, NULL, text);
	g_free (text);
	sheet_set_dirty (state->sheet, TRUE);

	gtk_widget_destroy (state->dialog);
	return;
}

void
dialog_cell_comment (WorkbookControlGUI *wbcg, Sheet *sheet, CellPos const *pos)
{
	CommentState *state;
	Range r;
	GtkWidget *textview;
	GSList *comments = NULL;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (pos != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, COMMENT_DIALOG_KEY))
		return;

	state = g_new (CommentState, 1);
	state->wbcg  = wbcg;
	state->sheet  = sheet;
	state->pos  = pos;
	state->comment = NULL;

	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
	g_return_if_fail (state->gui != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "comment_dialog");
	g_return_if_fail (state->dialog != NULL);

	textview = glade_xml_get_widget (state->gui, "textview");
	g_return_if_fail (textview != NULL);
	state->text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));

	r.start = r.end = *pos;
	comments = sheet_objects_get (sheet, &r, CELL_COMMENT_TYPE);
	if (comments) {
		char const *text;

		state->comment = CELL_COMMENT (comments->data);
		if (state->comment == NULL)
			g_warning ("Invalid comment");
		if (comments->next != NULL)
			g_warning ("More than one comment associated with a cell ?");

		text = cell_comment_text_get (comments->data);
		gtk_text_buffer_set_text (state->text, text, -1);

		g_slist_free (comments);
	}


	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_cell_comment_ok_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_cell_comment_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"cell-comment.html");

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (dialog_cell_comment_destroy), state);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       COMMENT_DIALOG_KEY);
	gtk_widget_show_all (state->dialog);
	gtk_widget_grab_focus (textview);
}
