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
	
	l = g_slist_nth (group, len - n - 1);
	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (l->data), 1);
}

static void
popup_menu_item_activated (GtkWidget *item, void *value)
{
	int *dest = gtk_object_get_user_data (GTK_OBJECT (item));

	*dest = GPOINTER_TO_INT (value);
	gtk_main_quit ();
}

int
run_popup_menu (GdkEvent *event, char **strings)
{
	GtkWidget *menu;
	int i;

	/* Create the popup menu */
	menu = gtk_menu_new ();
	for (i = 0;*strings; strings++, i++){
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (_(*strings));
		
		gtk_widget_show (item);
		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (popup_menu_item_activated), GINT_TO_POINTER (i));

		/* Pass a pointer where we want the result stored */
		gtk_object_set_user_data (GTK_OBJECT (item), &i);
		
		gtk_menu_append (GTK_MENU (menu), item);
	}

	i = -1;
	
	/* Configure it: */
	gtk_signal_connect (GTK_OBJECT (menu), "deactivate",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

	/* popup the menu */
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			event->button.button, event->button.time);
	gtk_main ();

	gtk_widget_destroy (menu);
	
	return i;
}
