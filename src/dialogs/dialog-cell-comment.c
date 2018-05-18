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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <sheet.h>
#include <cell.h>
#include <sheet-object-cell-comment.h>
#include <wbc-gtk.h>
#include <ranges.h>
#include <commands.h>
#include <widgets/gnm-text-view.h>

#define COMMENT_DIALOG_KEY "cell-comment-dialog"

typedef struct {
	WBCGtk *wbcg;
	Sheet              *sheet;
	GnmCellPos const      *pos;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GnmTextView        *gtv;
	GtkBuilder         *gui;
} CommentState;

static void
cb_dialog_cell_comment_destroy (CommentState *state)
{
	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_free (state);
}

static void
cb_cell_comment_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
				CommentState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_cell_comment_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			    CommentState *state)
{
	char          *text;
	PangoAttrList *attr;
	char const *author;

	author = gtk_entry_get_text
		(GTK_ENTRY (go_gtk_builder_get_widget
			    (state->gui, "new-author-entry")));
	g_object_get (G_OBJECT (state->gtv), "text", &text,
		      "attributes", &attr, NULL);
	if (!cmd_set_comment (GNM_WBC (state->wbcg),
			      state->sheet, state->pos, text, attr, author))
		gtk_widget_destroy (state->dialog);
	g_free (text);
	pango_attr_list_unref (attr);
}

static void
cb_wrap_toggled (GtkToggleButton *button, GObject *gtv)
{
	g_object_set (gtv, "wrap",
		      gtk_toggle_button_get_active (button) ? GTK_WRAP_WORD : GTK_WRAP_NONE,
		      NULL);
}

void
dialog_cell_comment (WBCGtk *wbcg, Sheet *sheet, GnmCellPos const *pos)
{
	CommentState	*state;
	GtkWidget	*box, *check, *old_author, *new_author;
	GnmComment	*comment;
	GtkBuilder	*gui;
	char *title, *cell_name;
	char const*real_user;
	GnmCellRef ref;
	GnmParsePos pp;
	GnmConventionsOut out;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (pos != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, COMMENT_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/cell-comment.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (CommentState, 1);
	state->wbcg  = wbcg;
	state->sheet = sheet;
	state->pos   = pos;
	state->gui   = gui;

	state->dialog = go_gtk_builder_get_widget (state->gui, "comment_dialog");
	g_return_if_fail (state->dialog != NULL);

	box = go_gtk_builder_get_widget (state->gui, "dialog-vbox");
	g_return_if_fail (box != NULL);
	state->gtv = gnm_text_view_new ();
	gtk_widget_show_all (GTK_WIDGET (state->gtv));
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (state->gtv),
			    TRUE, TRUE, TRUE);
	g_object_set (state->gtv, "wrap", GTK_WRAP_WORD, NULL);

	gnm_cellref_init (&ref, sheet, pos->col, pos->row, FALSE);
	out.accum = g_string_new (NULL);
	parse_pos_init_sheet (&pp, sheet);
	out.pp = &pp;
	out.convs = sheet->convs;
	cellref_as_string (&out, &ref, FALSE);
	cell_name = g_string_free (out.accum, FALSE);

	old_author = go_gtk_builder_get_widget (state->gui, "old-author-entry");
	new_author = go_gtk_builder_get_widget (state->gui, "new-author-entry");

	real_user = g_get_real_name ();
	if ((real_user != NULL) && g_utf8_validate (real_user, -1, NULL)) {
		gtk_entry_set_text (GTK_ENTRY (new_author), real_user);
	}
	gtk_widget_grab_focus (GTK_WIDGET (state->gtv));

	comment = sheet_get_comment (sheet, pos);
	if (comment) {
		char const *text;
		PangoAttrList *attr;
		g_object_get (G_OBJECT (comment), "text", &text,
			      "markup", &attr, NULL);
		g_object_set (state->gtv, "text", text,
			      "attributes", attr, NULL);
		if (attr != NULL)
			pango_attr_list_unref (attr);

		text = cell_comment_author_get (comment);
		if (text != NULL)
			gtk_label_set_text (GTK_LABEL (old_author),
					    text);
		title = g_strdup_printf (_("Edit Cell Comment (%s)"),
					 cell_name);
	} else {
		title = g_strdup_printf (_("New Cell Comment (%s)"),
					 cell_name);
		gtk_widget_hide (old_author);
		gtk_widget_hide (go_gtk_builder_get_widget (state->gui,
						       "old-author-label"));
	}
	gtk_window_set_title (GTK_WINDOW (state->dialog), title);
	g_free (title);

	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_cell_comment_ok_clicked), state);

	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_cell_comment_cancel_clicked), state);

	check = go_gtk_builder_get_widget (state->gui, "wrap-check");
	g_signal_connect (G_OBJECT (check),
			  "toggled",
			  G_CALLBACK (cb_wrap_toggled), state->gtv);
	cb_wrap_toggled (GTK_TOGGLE_BUTTON (check), G_OBJECT (state->gtv));

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_CELL_COMMENT);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_cell_comment_destroy);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       COMMENT_DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
