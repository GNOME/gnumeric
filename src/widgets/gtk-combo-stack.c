#include <gtk/gtk.h>
#include <libgnomeui/gnome-stock.h>
#include "gtk-combo-stack.h"

static GtkObjectClass *gtk_combo_stack_parent_class;

enum {
	POP,
	LAST_SIGNAL
};
static gint gtk_combo_box_signals [LAST_SIGNAL] = { 0, };

static void
gtk_combo_stack_destroy (GtkObject *object)
{
    /*
	GtkComboText *ct = GTK_COMBO_TEXT (object);

	if (ct->elements != NULL) {
		g_hash_table_destroy (ct->elements);
		ct->elements = NULL;
	}
	gtk_signal_disconnect_by_func (GTK_OBJECT (ct),
				       GTK_SIGNAL_FUNC (cb_pop_down), NULL);
	gtk_signal_disconnect_by_func (GTK_OBJECT (ct->list),
				       GTK_SIGNAL_FUNC (list_unselect_cb),
				       (gpointer) ct);
	(*gtk_combo_text_parent_class->destroy) (object);
    */
}

static void
gtk_combo_stack_class_init (GtkObjectClass *object_class)
{
	/*
	  object_class->destroy = &gtk_combo_text_destroy;
	*/
	gtk_combo_stack_parent_class = gtk_type_class (gtk_combo_box_get_type ());

	gtk_combo_box_signals [POP] = gtk_signal_new (
		"pop",
		GTK_RUN_LAST,
		object_class->type,
		GTK_SIGNAL_OFFSET (GtkComboBoxClass, pop_down_done),
		gtk_marshal_NONE__INT,
		GTK_TYPE_NONE, 1, GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, gtk_combo_box_signals, LAST_SIGNAL);
}

static void
gtk_combo_stack_init (GtkComboStack *object)
{
	object->num_items = 0;
}

GtkType
gtk_combo_stack_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GtkComboStack",
			sizeof (GtkComboStack),
			sizeof (GtkComboStackClass),
			(GtkClassInitFunc) gtk_combo_stack_class_init,
			(GtkObjectInitFunc) gtk_combo_stack_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_combo_box_get_type (), &info);
	}

	return type;
}

static void
gtk_combo_stack_pop (GtkComboStack *combo,
		     gint num)
{
	if (combo->num_items == 0)
		return;

	gtk_list_clear_items (GTK_LIST (combo->list), 0, num - 1);
	gtk_container_remove (GTK_CONTAINER (combo->list),
			      GTK_LIST (combo->list)->children->data);

	gtk_signal_emit_by_name (GTK_OBJECT (combo), "pop", num);

	combo->num_items -= num;
	if (!combo->num_items)
	{
		gtk_widget_set_sensitive (combo->button, FALSE);
		gtk_combo_box_set_arrow_sensitive (GTK_COMBO_BOX (combo), FALSE);
	}
}

static void
button_cb (GtkWidget *button, gpointer data)
{
	GtkComboStack *combo = GTK_COMBO_STACK (data);

	gtk_combo_stack_pop (combo, 1);
}

static void
list_select_cb (GtkWidget *list, GtkWidget *child, gpointer data)
{
	GtkComboStack *combo = GTK_COMBO_STACK (data);
	gint index = combo->num_items -
	    GPOINTER_TO_INT (gtk_object_get_data
			     (GTK_OBJECT (child), "value")) + 1;

	gtk_combo_box_popup_hide (GTK_COMBO_BOX (combo));

	gtk_combo_stack_pop (combo, index);
}

static void
gtk_combo_stack_construct (GtkComboStack *combo,
			   const gchar *stock_name,
			   gboolean const is_scrolled)
{
	GtkWidget *button, *list, *scroll, *display_widget, *pixmap;
	
	button = combo->button = gtk_button_new ();
	list = combo->list = gtk_list_new ();

	/* Create the button */
	pixmap = gnome_stock_new_with_icon (stock_name);
	gtk_widget_show (pixmap);
	gtk_container_add (GTK_CONTAINER (button), pixmap);
	gtk_widget_set_sensitive (button, FALSE);
	gtk_combo_box_set_arrow_sensitive (GTK_COMBO_BOX (combo), FALSE);
	
	if (is_scrolled) {
		display_widget = scroll = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll),
						GTK_POLICY_NEVER,
						GTK_POLICY_AUTOMATIC);

		gtk_scrolled_window_add_with_viewport (
			GTK_SCROLLED_WINDOW(scroll), list);
		gtk_container_set_focus_hadjustment (
			GTK_CONTAINER (list),
			gtk_scrolled_window_get_hadjustment (
				GTK_SCROLLED_WINDOW (scroll)));
		gtk_container_set_focus_vadjustment (
			GTK_CONTAINER (list),
			gtk_scrolled_window_get_vadjustment (
				GTK_SCROLLED_WINDOW (scroll)));
		gtk_widget_set_usize (scroll, 0, 200); /* MAGIC NUMBER */
	} else
		display_widget = list;

	gtk_signal_connect (GTK_OBJECT (list), "select-child",
			    GTK_SIGNAL_FUNC (list_select_cb),
			    (gpointer) combo);
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (button_cb),
			    (gpointer) combo);

	gtk_widget_show (display_widget);
	gtk_widget_show (button);
	gtk_combo_box_construct (GTK_COMBO_BOX (combo), button, display_widget);
}

GtkWidget*
gtk_combo_stack_new (const gchar *stock,
		     gboolean const is_scrolled)
{
	GtkComboStack *ct;

	ct = gtk_type_new (gtk_combo_stack_get_type ());
	gtk_combo_stack_construct (ct, stock, is_scrolled);
	/*
	gtk_signal_connect (GTK_OBJECT (ct), "pop_down_done",
			    GTK_SIGNAL_FUNC (cb_pop_down), NULL);
	*/
	return GTK_WIDGET (ct);
}

void
gtk_combo_stack_push_item (GtkComboStack *combo,
			   const gchar *item)
{
	GtkWidget *listitem;
	GList *tmp_list; /* We can only prepend GLists to a GtkList */
	
	g_return_if_fail (item);

	combo->num_items++;

	listitem = gtk_list_item_new_with_label (item);
	gtk_object_set_data (GTK_OBJECT (listitem), "value",
			     GINT_TO_POINTER (combo->num_items));
	gtk_widget_show (listitem);

	tmp_list = g_list_alloc ();
	tmp_list->data = listitem;
	tmp_list->next = NULL;
	gtk_list_prepend_items (GTK_LIST (combo->list),
				tmp_list);

	gtk_widget_set_sensitive (combo->button, TRUE);
	gtk_combo_box_set_arrow_sensitive (GTK_COMBO_BOX (combo), TRUE);
	
/*	g_list_free (tmp_list);*/
}
