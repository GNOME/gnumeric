#include "gtk-combo-text.h"
#include <gtk/gtksignal.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklist.h>

/* static GtkComboBoxClass *gtk_combo_text_parent_class;*/

static void
gtk_combo_text_class_init (GtkObjectClass *class)
{
}

static void
gtk_combo_text_init (GtkComboText *object)
{
}


GtkType
gtk_combo_text_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GtkComboText",
			sizeof (GtkComboText),
			sizeof (GtkComboTextClass),
			(GtkClassInitFunc) gtk_combo_text_class_init,
			(GtkObjectInitFunc) gtk_combo_text_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_combo_box_get_type (), &info);
	}

	return type;
}


static void
list_select_cb (GtkWidget *caller, gpointer data)
{
	GtkComboText *combo = GTK_COMBO_TEXT (data);
	GtkEntry *entry = GTK_ENTRY (combo->entry);
	gchar *value = (gchar*) gtk_object_get_data
		(GTK_OBJECT (caller), "value");

	g_return_if_fail (entry && value);
	
	gtk_entry_set_text (entry, value);
	gtk_signal_emit_by_name (GTK_OBJECT (entry), "activate");

	gtk_combo_box_popup_hide (GTK_COMBO_BOX (combo));
}

void
gtk_combo_text_construct (GtkComboText *combo_text)
{
	GtkWidget *entry, *list;

	entry = combo_text->entry = gtk_entry_new ();
	list = combo_text->list = gtk_list_new ();

	gtk_widget_show (entry);
	gtk_widget_show (list);
	gtk_combo_box_construct (GTK_COMBO_BOX (combo_text), entry, list);
}

GtkWidget*
gtk_combo_text_new ()
{
	GtkComboText *combo_text;

	combo_text = gtk_type_new (gtk_combo_text_get_type ());
	gtk_combo_text_construct (combo_text);
	return GTK_WIDGET (combo_text);
}

void
gtk_combo_text_add_item (GtkComboText *combo_text,
			 const gchar *item,
			 const gchar *value)
{
	GtkWidget *listitem;
		
	g_return_if_fail (item);
		
	if (!value)
		value = item;

	listitem = gtk_list_item_new_with_label (item);
	gtk_widget_show (listitem);

	gtk_object_set_data_full (GTK_OBJECT (listitem), "value",
				  g_strdup (value), g_free);
	gtk_signal_connect (GTK_OBJECT (listitem), "select",
			    GTK_SIGNAL_FUNC (list_select_cb),
			    (gpointer) combo_text);

	gtk_container_add (GTK_CONTAINER (combo_text->list),
			   listitem);
}
