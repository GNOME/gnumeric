/*
 * dialog-goto-cell.c:  Implements the GOTO CELL functionality
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

static void
cb_goto_cell (GtkEntry *entry, GnomeDialog *dialog)
{
	gtk_main_quit ();
}

static void
cb_row_selected (GtkCList *clist, int row, int col, GdkEvent *event, GtkEntry *entry)
{
	char *text;

	gtk_clist_get_text (clist, row, col, &text);
	gtk_entry_set_text (entry, text);
}

void
dialog_goto_cell (Workbook *wb)
{
	static GtkWidget *dialog;
	static GtkWidget *clist;
	static GtkWidget *entry;
	char   *text;
	
	if (!dialog){
		GtkWidget *box;
		char *titles [2];

		titles [0] = _("Cell");
		titles [1] = NULL;
		
		dialog = gnome_dialog_new (_("Go to..."),
					   GNOME_STOCK_BUTTON_HELP,
					   _("Special..."),
					   GNOME_STOCK_BUTTON_OK,
					   GNOME_STOCK_BUTTON_CANCEL,
					   NULL);
		gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
		
		clist = gtk_clist_new_with_titles (1, titles);
		gtk_clist_set_policy (GTK_CLIST (clist),
				      GTK_POLICY_AUTOMATIC,
				      GTK_POLICY_AUTOMATIC);

		entry = gtk_entry_new ();
		gtk_signal_connect (GTK_OBJECT (entry), "activate",
				    GTK_SIGNAL_FUNC (cb_goto_cell), dialog);

		gtk_signal_connect (GTK_OBJECT (clist), "select_row",
				    GTK_SIGNAL_FUNC (cb_row_selected), entry);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (box), clist);
		gtk_box_pack_start_defaults (GTK_BOX (box), entry);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
					     box);

		gtk_widget_show_all (box);
	} else
		gtk_widget_show (dialog);

	gtk_widget_grab_focus (entry);
	
	/* Run the dialog */
	gnome_dialog_run_modal (GNOME_DIALOG (dialog));

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	if (workbook_parse_and_jump (wb, text)){
		char *texts [1];

		texts [0] = text;
		
		gtk_clist_append (GTK_CLIST (clist), texts);
	}

	gnome_dialog_close (GNOME_DIALOG (dialog));
}
