/*
 * dialog-goto-cell.c:  Implements the GOTO CELL functionality
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
#include "workbook.h"
#include "workbook-control.h"
#include "utils-dialog.h"

static void
cb_row_selected (GtkCList *clist, int row, int col, GdkEvent *event, GtkEntry *entry)
{
	char *text;
	GtkWidget *dialog;

	gtk_clist_get_text (clist, row, col, &text);
	gtk_entry_set_text (entry, text);
	/* If the tool is double-clicked we dismiss dialog. */
	if (event && event->type == GDK_2BUTTON_PRESS) {
		dialog = gtk_widget_get_toplevel (GTK_WIDGET (clist));
		gtk_signal_emit_by_name (GTK_OBJECT (dialog), "clicked", 0);
	}
}

void
dialog_goto_cell (WorkbookControlGUI *wbcg)
{
	static GtkWidget *dialog;
	static GtkWidget *clist;
	static GtkWidget *swin;
	static GtkWidget *entry;
	char   *text;
	int    res;

	if (!dialog){
		GtkWidget *box;
		gchar *titles[2];

		titles[0] = _("Cell");
		titles[1] = NULL;

		dialog = gnome_dialog_new (_("Go to..."),
					   GNOME_STOCK_BUTTON_OK,
					   GNOME_STOCK_BUTTON_CANCEL,
					   /*  _("Special..."), */
					   /* GNOME_STOCK_BUTTON_HELP, */
					   NULL);
		gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
		gnome_dialog_set_default (GNOME_DIALOG (dialog), GNOME_OK);

		swin = gtk_scrolled_window_new (NULL, NULL);
		clist = gtk_clist_new_with_titles (1, titles);
		gtk_clist_column_titles_passive (GTK_CLIST (clist));
		gtk_container_add (GTK_CONTAINER (swin), clist);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin),
						GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

		entry = gnumeric_dialog_entry_new (GNOME_DIALOG (dialog));

		gtk_signal_connect (GTK_OBJECT (clist), "select_row",
				    GTK_SIGNAL_FUNC (cb_row_selected), entry);

		box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (box), swin);
		gtk_box_pack_start_defaults (GTK_BOX (box), entry);

		gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
					     box);

		gtk_widget_show_all (box);
	} else
		gtk_widget_show (dialog);

	gtk_widget_grab_focus (entry);

	/* Run the dialog */
	res = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (res == GNOME_OK) {

		text = gtk_entry_get_text (GTK_ENTRY (entry));

		if (*text){
			if (workbook_parse_and_jump (WORKBOOK_CONTROL (wbcg), text)) {
				char *tmp[1];
				int i = 0;
				gboolean existed = FALSE;

				/* See if it's already in the list, if so, move it to the front */
				while (gtk_clist_get_text (GTK_CLIST (clist), i, 0, tmp)) {

					if (strcmp (tmp[0], text) == 0) {
						if (i != 0)
							gtk_clist_swap_rows (GTK_CLIST (clist), 0, i);
						existed = TRUE;
						break;
					}

					i++;
				}

				if (!existed) {
					gchar *texts[1];
					texts[0] = text;
					gtk_clist_prepend (GTK_CLIST (clist), texts);
				}
			}
		}
	}

	if (res != -1)
		gnome_dialog_close (GNOME_DIALOG (dialog));
}
