/*
 * dialog-goto-cell.c:  Implements the Define Name dialog box
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
dialog_define_names (Workbook *wb)
{
	static GtkWidget *dialog;
	GtkWidget *l;
	GtkCList  *clist;
	GtkEntry  *name, *refers;
	
	if (!dialog){
		dialog = gnome_dialog_new (_("Define names"),
				  GNOME_STOCK_BUTTON_APPLY,
				  GNOME_STOCK_BUTTON_CLOSE,
				  _("Add"),
				  _("Remove"),
				  NULL);
		
		clist = (GtkCList *) gtk_clist_new (1);
		gtk_clist_column_titles_hide (clist);

		name   = (GtkEntry *) gtk_entry_new ();
		refers = (GtkEntry *) gtk_entry_new ();
		
		box = gtk_vbox_new (0, 2);

		l = gtk_label_new (_("Names in the workbook"));
		gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
		
		gtk_box_pack_start (GTK_BOX (box), l, TRUE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (name), TRUE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (clist), TRUE, TRUE, 0);

		l = gtk_label_new (_("Refers to"));
		gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
		gtk_box_pack_start (GTK_BOX (box), l, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (refers), TRUE, TRUE, 0);
	}

	gtk_widget_show_all (dialog);
}
