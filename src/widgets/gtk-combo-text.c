#include <config.h>
#include "gtk-combo-text.h"
#include <gtk/gtksignal.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklist.h>
#include <gtk/gtkscrolledwindow.h>

static GtkObjectClass *gtk_combo_text_parent_class;

static gboolean cb_pop_down (GtkWidget *w, GtkWidget *pop_down,
			     gpointer dummy);

static void
gtk_combo_text_destroy (GtkObject *object)
{
	GtkComboText *ct = GTK_COMBO_TEXT (object);

	if (ct->elements != NULL) {
		g_hash_table_destroy (ct->elements);
		ct->elements = NULL;
	}
	gtk_signal_disconnect_by_func (GTK_OBJECT (ct),
				       GTK_SIGNAL_FUNC (cb_pop_down), NULL);
	(*gtk_combo_text_parent_class->destroy) (object);
}

static void
gtk_combo_text_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = &gtk_combo_text_destroy;
	gtk_combo_text_parent_class = gtk_type_class (gtk_combo_box_get_type ());
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
gtk_combo_text_select_item (GtkComboText *ct, int elem)
{
	gtk_list_select_item (GTK_LIST(ct->list), elem);
}

/* FIXME : This is over kill.  There must be a more elegant way of handling
 * this.
 *
 * TODO : Add autoscroll
 */
static gboolean
cb_enter (GtkWidget *w, GdkEventCrossing *event,
	  gpointer user)
{
	GtkComboText *ct = user;
	ct->cached_entry = w;
	ct->cache_mouse_state = GTK_WIDGET_STATE(w);
	if (GTK_STATE_SELECTED != ct->cache_mouse_state)
		gtk_widget_set_state (w, GTK_STATE_ACTIVE);

	return TRUE;
}
static gboolean
cb_exit (GtkWidget *w, GdkEventCrossing *event,
	  gpointer user)
{
	GtkComboText *ct = user;
	gtk_widget_set_state (w, ct->cache_mouse_state);
	return TRUE;
}

static gboolean
cb_pop_down (GtkWidget *w, GtkWidget *pop_down, gpointer dummy)
{
	GtkComboText *ct = GTK_COMBO_TEXT (w);

	if (ct->cached_entry)
		gtk_widget_set_state (ct->cached_entry, ct->cache_mouse_state);
	ct->cached_entry = NULL;

	return FALSE;
}

void
gtk_combo_text_add_item (GtkComboText *ct,
			 const gchar *item,
			 const gchar *value)
{
	GtkWidget *listitem;
	gchar *value_copy;

	g_return_if_fail (item);

	if (!value)
		value = item;

	value_copy = g_strdup (value);
	g_hash_table_insert (ct->elements, (gpointer)value_copy,
			     GINT_TO_POINTER (g_hash_table_size (ct->elements)));

	listitem = gtk_list_item_new_with_label (item);
	gtk_widget_show (listitem);

	gtk_object_set_data_full (GTK_OBJECT (listitem), "value",
				  value_copy, g_free);
	gtk_signal_connect (GTK_OBJECT (listitem), "select",
			    GTK_SIGNAL_FUNC (list_select_cb),
			    (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (listitem), "enter-notify-event",
			    GTK_SIGNAL_FUNC (cb_enter),
			    (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (listitem), "leave-notify-event",
			    GTK_SIGNAL_FUNC (cb_exit),
			    (gpointer) ct);

	gtk_container_add (GTK_CONTAINER (ct->list),
			   listitem);
}

static void
gtk_combo_text_construct (GtkComboText *ct, gboolean const is_scrolled)
{
	GtkWidget *entry, *list, *scroll, *display_widget;

	ct->elements = g_hash_table_new (&g_str_hash,
					 &g_str_equal);

	/* Probably irrelevant, but lets be careful */
	ct->cache_mouse_state = GTK_STATE_NORMAL;
	ct->cached_entry = NULL;

	entry = ct->entry = gtk_entry_new ();
	list = ct->list = gtk_list_new ();
	if (is_scrolled) {
		display_widget = scroll = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll),
						GTK_POLICY_NEVER,
						GTK_POLICY_AUTOMATIC);

		gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW(scroll), list);
		gtk_widget_set_usize (scroll, 0, 200); /* MAGIC NUMBER */
	} else
		display_widget = list;

	gtk_widget_show (display_widget);
	gtk_widget_show (entry);
	gtk_combo_box_construct (GTK_COMBO_BOX (ct), entry, display_widget);
}

GtkWidget*
gtk_combo_text_new (gboolean const is_scrolled)
{
	GtkComboText *ct;

	ct = gtk_type_new (gtk_combo_text_get_type ());
	gtk_combo_text_construct (ct, is_scrolled);
	gtk_signal_connect (GTK_OBJECT (ct), "pop_down_done",
			    GTK_SIGNAL_FUNC (cb_pop_down), NULL);
	return GTK_WIDGET (ct);
}

