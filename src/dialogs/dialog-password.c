#include <gnumeric-config.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkenums.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkvbox.h>
#include <glib/gi18n-lib.h>
#include "dialogs.h"

static void
cb_accept_password (GtkWidget *IGNORED, GtkDialog *d)
{
	gtk_dialog_response (d, GTK_RESPONSE_ACCEPT);
}

/*
 * Note: filename is fs encoded, not UTF-8.
 */
char *
dialog_get_password (GtkWindow *parent, const char *filename)
{
	char *res = NULL;
	char *str;
	char *dispname;
	char *primary;
	char *secondary;
	GtkWidget *d, *hb, *vb, *pwb, *image, *label, *entry;

	dispname  = g_filename_display_name (filename);
	primary   = g_strdup_printf (_("%s is encrypted"), dispname);
	g_free (dispname);
	secondary = _("Encrypted files require a password\nbefore they can be opened.");
	label = gtk_label_new (NULL);
	str = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">"
			       "%s</span>\n\n%s", primary, secondary);
	gtk_label_set_markup (GTK_LABEL (label), str);
	g_free (primary);
	g_free (str);

	gtk_label_set_selectable (GTK_LABEL (label), TRUE);

	d = gtk_dialog_new_with_buttons ("", parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT |
					 GTK_DIALOG_NO_SEPARATOR,
					 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					 GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					 NULL);
	gtk_window_set_resizable (GTK_WINDOW (d), FALSE);
	hb = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d)->vbox), hb,
			    TRUE, TRUE, 8);
	image = gtk_image_new_from_stock ("Gnumeric_Protection_Yes_Dialog",
					  GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hb), image, FALSE, FALSE, 0);
	vb = gtk_vbox_new (FALSE, 8);
	gtk_box_pack_start (GTK_BOX (hb), vb, TRUE, TRUE, 6);
	gtk_box_pack_start_defaults (GTK_BOX (vb), label);
	pwb = gtk_hbox_new (FALSE, 8);
	/* Strange width so that width of primary/secondary text will win. */
	entry = g_object_new (GTK_TYPE_ENTRY,
			      "visibility", FALSE,
			      "width-request", 1, NULL);
	gtk_box_pack_start (GTK_BOX (pwb), gtk_label_new (_("Password :")),
			    FALSE, FALSE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (pwb), entry);
	gtk_box_pack_start (GTK_BOX (vb), pwb, FALSE, FALSE, 0);
	gtk_widget_show_all (d);

	g_signal_connect (G_OBJECT (entry),
		"activate",
		G_CALLBACK (cb_accept_password), d);

	if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT)
		res = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	gtk_widget_destroy (d);
	return res;
}
