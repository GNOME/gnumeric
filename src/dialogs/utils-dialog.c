/*
 * utils-dialog.c:
 *
 * Author:
 *  Jon K Hellan <Jon.K.Hellan@item.ntnu.no>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "utils-dialog.h"
#include <libgnomeui/libgnomeui.h>

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
		gnome_dialog_editable_enters (dialog, GTK_EDITABLE (entry));
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
		gnome_dialog_editable_enters (dialog, GTK_EDITABLE (entry));
	return entry;
}

GtkWidget *
hbox_pack_label_and_entry (GtkWidget *dialog, GtkWidget *vbox, char *str,
			   char *default_str, int entry_len)
{
        GtkWidget *box, *label, *entry;

        box = gtk_hbox_new (FALSE, 0);
	entry = gnumeric_dialog_entry_new_with_max_length
		(GNOME_DIALOG (dialog), entry_len);
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

GtkWidget *
gnumeric_load_image (char const * const name)
{
	GtkWidget *image;
	char *path;

	path = g_strconcat (GNUMERIC_ICONDIR "/", name, NULL);
	image = gnome_pixmap_new_from_file (path);
	g_free (path);

	if (image)
		gtk_widget_show (image);

	return image;
}
