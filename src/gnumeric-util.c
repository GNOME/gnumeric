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

int
gtk_radio_group_get_selected (GSList *radio_group)
{
	GSList *l;
	int i, c;

	g_return_val_if_fail (radio_group != NULL, 0);
	
	c = g_slist_length (radio_group);
		
	for (i = 0, l = radio_group; l; l = l->next, i++){
		GtkRadioButton *button = l->data;

		if (GTK_TOGGLE_BUTTON (button)->active)
			return c - i - 1;
	}

	return 0;
}

void
gtk_radio_button_select (GSList *group, int n)
{
	GSList *l;
	int len = g_slist_length (group);
	
	l = g_slist_nth (group, n);
	gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (l->data));
}
