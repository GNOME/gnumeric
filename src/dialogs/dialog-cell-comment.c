/*
 * dialog-cell-comment.c: Dialog box for editing a cell comment
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
#include <commands.h>

#include <libgnome/gnome-i18n.h>

#define GLADE_FILE "cell-comment.glade"
#define COMMENT_DIALOG_KEY "cell-comment-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	Sheet              *sheet;
	CellPos const      *pos;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GtkTextBuffer      *text;
	GladeXML           *gui;
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

	if (!cmd_set_comment (WORKBOOK_CONTROL (state->wbcg), state->sheet, state->pos, text))
		gtk_widget_destroy (state->dialog);
	g_free (text);
	return;
}

void
dialog_cell_comment (WorkbookControlGUI *wbcg, Sheet *sheet, CellPos const *pos)
{
	CommentState *state;
	GtkWidget *textview;
	CellComment   *comment;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (pos != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, COMMENT_DIALOG_KEY))
		return;

	state = g_new (CommentState, 1);
	state->wbcg  = wbcg;
	state->sheet  = sheet;
	state->pos  = pos;

	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
	g_return_if_fail (state->gui != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "comment_dialog");
	g_return_if_fail (state->dialog != NULL);

	textview = glade_xml_get_widget (state->gui, "textview");
	g_return_if_fail (textview != NULL);
	state->text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));

	comment = cell_has_comment_pos (sheet, pos);
	if (comment)
		gtk_text_buffer_set_text (state->text, cell_comment_text_get (comment), 
					  -1);

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
