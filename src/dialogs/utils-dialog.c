/*
 * utils-dialog.c: 
 *
 * Author:
 *  Jon K Hellan <Jon.K.Hellan@item.ntnu.no>
 */

#include <config.h>
#include <gnome.h>
#include "utils-dialog.h"

/*
 * gnumeric_dialog_entry_new
 *
 * @dialog: parent dialog window
 *
 * Calls gtk_entry_new to create an entry field, then makes <Enter>
 * activate the default button.
 */
GtkWidget *
gnumeric_dialog_entry_new (GnomeDialog *dialog)
{
	GtkWidget *entry;
	
	entry = gtk_entry_new ();
	if (dialog)
		gnome_dialog_editable_enters(dialog, GTK_EDITABLE (entry));
	return entry;
}

/*
 * gnumeric_dialog_entry_new_with_max_length
 *
 * @dialog: parent dialog window
 * @max:    max text length
 *
 * Calls gtk_entry_new_with_max_length to create an entry field, then
 * makes <Enter> activate the default button. 
 */
GtkWidget *
gnumeric_dialog_entry_new_with_max_length (GnomeDialog *dialog, guint16 max)
{
	GtkWidget *entry;
	
	entry = gtk_entry_new_with_max_length (max);
	if (dialog)
		gnome_dialog_editable_enters(dialog, GTK_EDITABLE (entry));
	return entry;
}

GtkWidget *
hbox_pack_label_and_entry(char *str, char *default_str,
			  int entry_len, GtkWidget *vbox)
{
        GtkWidget *box, *label, *entry;
	GnomeDialog *dialog;

        box = gtk_hbox_new (FALSE, 0);
	dialog = GNOME_DIALOG (gtk_widget_get_toplevel (GTK_WIDGET (vbox)));
	entry = gnumeric_dialog_entry_new_with_max_length (dialog, entry_len);
	label = gtk_label_new (str);
	gtk_entry_set_text (GTK_ENTRY (entry), default_str);

	gtk_box_pack_start_defaults (GTK_BOX (box), label);
	gtk_box_pack_start_defaults (GTK_BOX (box), entry);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), box);

	return entry;
}

GList *
add_strings_to_glist (const char *strs[])
{
        int   i;
	GList *list = NULL;

	for (i=0; strs[i] != NULL; i++) {
	        list = g_list_append (list, (gpointer) strs[i]);
	}
	return list;
}




