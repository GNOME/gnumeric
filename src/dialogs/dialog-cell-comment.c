/*
 * dialog-cell-comment.c: Dialog box for editing a cell comment
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"

void
dialog_cell_comment (Workbook *wb, Cell *cell)
{
	GtkWidget *dialog;
	GtkWidget *text;
	int v;
	
	g_return_if_fail (wb != NULL);
	g_return_if_fail (cell != NULL);

	dialog = gnome_dialog_new (
		_("Cell comment"),
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL,
		NULL);
	
	gnome_dialog_set_default (GNOME_DIALOG(dialog), GNOME_OK);

	text = gtk_text_new (NULL, NULL);
	gtk_text_set_editable (GTK_TEXT (text), TRUE);
	if (cell->comment){
		char *comment = cell->comment->comment->str;
		gint pos = 0;
		
		gtk_editable_insert_text (
			GTK_EDITABLE (text), comment, strlen (comment), &pos);
	}
	
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), text, TRUE, TRUE, 0);
	gtk_widget_show (text);
	gtk_widget_grab_focus (text);

	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
	if (v == -1)
		return;
	
	if (v == 0){
		char *comment;

		comment = gtk_editable_get_chars (GTK_EDITABLE (text), 0, -1);

		if (comment){
			cell_set_comment (cell, comment);
			g_free (comment);
		}
	}

	gtk_object_destroy (GTK_OBJECT (dialog));
}
