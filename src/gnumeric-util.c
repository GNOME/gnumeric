/*
 * gnumeric-util.c:  Various GUI utility functions. 
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "style.h"

void
gnumeric_notice (Workbook *wb, const char *type, const char *str)
{
	GtkWidget *dialog;

	dialog = gnome_message_box_new (str, type, GNOME_STOCK_BUTTON_OK, NULL);

	if (wb)
		gnome_dialog_set_parent (GNOME_DIALOG (dialog),
					 GTK_WINDOW (wb->toplevel));
			
	gnome_dialog_run (GNOME_DIALOG (dialog));
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
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l->data), 1);
}

static void
popup_menu_item_activated (GtkWidget *item, void *value)
{
	int *dest = gtk_object_get_user_data (GTK_OBJECT (item));

	*dest = GPOINTER_TO_INT (value);
	gtk_main_quit ();
}

int
run_popup_menu (GdkEvent *event, int button, char **strings)
{
	GtkWidget *menu;
	int i;

	g_return_val_if_fail (event != NULL, -1);
	g_return_val_if_fail (strings != NULL, -1);

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

gboolean
range_contains (Range *range, int col, int row)
{
	if ((col >= range->start_col) &&
	    (col <= range->end_col)   &&
	    (row >= range->start_row) &&
	    (row <= range->end_row))
		return TRUE;

	return FALSE;
}

char *
font_change_component (const char *fontname, int idx, char *newvalue)
{
	char *components [15];
	char *new = g_strdup (fontname), *res;
	char *p = new;
	int  n = 0, i, len;

	/* split the font name */
	for (;*p; p++){
		if (*p == '-'){
			*p = 0;
			p++;
			components [n++] = p;
		}
	}

	/* Change the value */
	components [idx] = newvalue;

	/* reassemble */
	len = 1;
	for (i = 0; i < n; i++){
		len += strlen (components [i]) + 1;
	}
	len++;
	res = g_malloc (len);
	res [0] = '-';
	res [1] = 0;
	
	for (i = 0; i < n; i++){
		strcat (res, components [i]);
		if (i + 1 != n)
			strcat (res, "-");
	}
	g_free (new);

	return res;
}

char *
font_get_bold_name (const char *fontname)
{
	char *f;
	
	/* FIXME: this scheme is poor: in some cases, the fount strength is called 'bold', 
	whereas in some others it is 'black', in others... Look font_get_italic_name  */
	f = font_change_component (fontname, 2, "bold");

	return f;
}

char *
font_get_italic_name (const char *fontname)
{
	char *f;
	
	f = font_change_component (fontname, 3, "o");
	if (style_font_new (f, 1) == NULL) {
		f = font_change_component (fontname, 3, "i");
	}

	return f;
}

static void
kill_popup_menu (GtkWidget *widget, GtkMenu *menu)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gtk_object_unref (GTK_OBJECT (menu)); 
}

void
gnumeric_auto_kill_popup_menu_on_hide (GtkMenu *menu)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gtk_signal_connect (GTK_OBJECT (menu), "hide",
			    GTK_SIGNAL_FUNC (kill_popup_menu), menu);
}

void
gnumeric_popup_menu (GtkMenu *menu, GdkEventButton *event)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));
	
	gnumeric_auto_kill_popup_menu_on_hide (menu);
	gtk_menu_popup (menu, NULL, NULL, 0, NULL, event->button, event->time);	
}
