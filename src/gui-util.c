/*
 * gnumeric-util.c:  Various GUI utility functions. 
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"

void
gnumeric_notice (char *str)
{
	GtkWidget *dialog;
	GtkWidget *label;

	label = gtk_label_new (str);
	dialog = gnome_dialog_new (_("Notice"), GNOME_STOCK_BUTTON_OK, NULL);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);
	gnome_dialog_run_modal (GNOME_DIALOG (dialog));
	gtk_object_destroy (GTK_OBJECT (dialog));
}

