/*
 * gtk-combo-text: A combo box for selecting from a list.
 */
#include <config.h>
#include <ctype.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklist.h>
#include <gtk/gtkscrolledwindow.h>
#include "gnumeric-combo-text.h"

static GtkObjectClass *gnm_combo_text_parent_class;

static gboolean cb_pop_down (GtkWidget *w, GtkWidget *pop_down,
			     gpointer dummy);

static void update_list_selection (GnmComboText *ct, const gchar *text);

static void
entry_activate_cb (GtkWidget *entry, gpointer data)
{
	GnmComboText *combo = GNM_COMBO_TEXT (data);

	update_list_selection (combo,
			       gtk_entry_get_text (GTK_ENTRY (combo->entry)));
}

static void
list_select_cb (GtkWidget *list, GtkWidget *child, gpointer data)
{
	GnmComboText *combo = GNM_COMBO_TEXT (data);
	GtkEntry *entry = GTK_ENTRY (combo->entry);
	gchar *value = (gchar*) gtk_object_get_data
		(GTK_OBJECT (child), "value");

	g_return_if_fail (entry && value);

	if (combo->cached_entry == child)
		combo->cached_entry = NULL;

	gtk_entry_set_text (entry, value);
	gtk_signal_handler_block_by_func (GTK_OBJECT (entry), 
					  GTK_SIGNAL_FUNC (entry_activate_cb),
					  (gpointer) combo);
	gtk_signal_emit_by_name (GTK_OBJECT (entry), "activate");
	gtk_signal_handler_unblock_by_func (GTK_OBJECT (entry), 
					  GTK_SIGNAL_FUNC (entry_activate_cb),
					  (gpointer) combo);

	gtk_combo_box_popup_hide (GTK_COMBO_BOX (data));
}

static void
list_unselect_cb (GtkWidget *list, GtkWidget *child, gpointer data)
{
	if (GTK_WIDGET_VISIBLE (list)) /* Undo interactive unselect */
		gtk_list_select_child (GTK_LIST (list), child);
}

static void
update_list_selection (GnmComboText *ct, const gchar *text)
{
	gpointer candidate;
	GtkWidget *child;

	gtk_signal_handler_block_by_func (GTK_OBJECT (ct->list), 
					  GTK_SIGNAL_FUNC (list_select_cb),
					  (gpointer) ct);
	gtk_signal_handler_block_by_func (GTK_OBJECT (ct->list), 
					  GTK_SIGNAL_FUNC (list_unselect_cb),
					  (gpointer) ct);
	
	gtk_list_unselect_all (GTK_LIST (ct->list));
	candidate = g_hash_table_lookup (ct->elements, (gconstpointer) text);
	if (candidate && GTK_IS_WIDGET (candidate)) {
		child = GTK_WIDGET (candidate);
		gtk_list_select_child (GTK_LIST (ct->list), child);
		gtk_widget_grab_focus (child);
	}
	gtk_signal_handler_unblock_by_func (GTK_OBJECT (ct->list), 
					    GTK_SIGNAL_FUNC (list_select_cb),
					    (gpointer) ct);
	gtk_signal_handler_unblock_by_func (GTK_OBJECT (ct->list), 
					    GTK_SIGNAL_FUNC (list_unselect_cb),
					    (gpointer) ct);
}


static void
gnm_combo_text_destroy (GtkObject *object)
{
	GnmComboText *ct = GNM_COMBO_TEXT (object);

	if (ct->elements != NULL) {
		g_hash_table_destroy (ct->elements);
		ct->elements = NULL;
	}
	gtk_signal_disconnect_by_func (GTK_OBJECT (ct),
				       GTK_SIGNAL_FUNC (cb_pop_down), NULL);
	gtk_signal_disconnect_by_func (GTK_OBJECT (ct->list),
				       GTK_SIGNAL_FUNC (list_unselect_cb),
				       (gpointer) ct);
	(*gnm_combo_text_parent_class->destroy) (object);
}

static void
gnm_combo_text_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = &gnm_combo_text_destroy;
	gnm_combo_text_parent_class = gtk_type_class (gtk_combo_box_get_type ());
}

GtkType
gnm_combo_text_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GnmComboText",
			sizeof (GnmComboText),
			sizeof (GnmComboTextClass),
			(GtkClassInitFunc) gnm_combo_text_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_combo_box_get_type (), &info);
	}

	return type;
}

static gint
strcase_equal (gconstpointer v, gconstpointer v2)
{
	return g_strcasecmp ((const gchar*) v, (const gchar*)v2) == 0;
}


/*
 * a char* hash function from ASU
 *
 * This is cut/paste from gutils.c
 * We've got to do this, because this widget will soon move out of the
 * Gnumeric source and into a separate library.
 */
static guint
strcase_hash (gconstpointer v)
{
	const unsigned char *s = (const unsigned char *)v;
	const unsigned char *p;
	guint h = 0, g;

	for(p = s; *p != '\0'; p += 1) {
		h = ( h << 4 ) + tolower (*p);
		if ( ( g = h & 0xf0000000 ) ) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h /* % M */;
}

/**
 * gnm_combo_text_set_case_sensitive
 * @combo_text:  ComboText widget
 * @val:         make case sensitive if TRUE
 *
 * Specifies whether the text entered into the GtkEntry field and the text
 * in the list items is case sensitive. Because the values are stored in a
 * hash, it is not legal to change case sensitivity when the list contains
 * elements.
 *
 * Returns: The function returns -1 if request could not be honored. On
 * success, it returns 0.
 */
gint
gnm_combo_text_set_case_sensitive (GnmComboText *combo, gboolean val)
{
	if (combo->elements
	    && g_hash_table_size (combo->elements) > 0
	    && val != combo->case_sensitive)
		return -1;
	else {
		combo->case_sensitive = val;
		if (val != combo->case_sensitive) {
			GHashFunc hashfunc;
			GCompareFunc comparefunc;

			g_hash_table_destroy (combo->elements);
			if (combo->case_sensitive) {
				hashfunc = g_str_hash;
				comparefunc = g_str_equal;
			} else {
				hashfunc = strcase_hash;
				comparefunc = strcase_equal;
			}
			combo->elements = g_hash_table_new (hashfunc,
							    comparefunc);
		}
		return 0;
	}
}

static void
cb_toggle (GtkWidget *child, gpointer data)
{
	GnmComboText *ct = GNM_COMBO_TEXT (data);

	gtk_list_select_child (GTK_LIST (ct->list), child);
}

void
gnm_combo_text_select_item (GnmComboText *ct, int elem)
{
	gtk_list_select_item (GTK_LIST(ct->list), elem);
}

void
gnm_combo_text_set_text (GnmComboText *ct, const gchar *text)
{
	gtk_entry_set_text (GTK_ENTRY (ct->entry), text);
	update_list_selection (ct, text);
}

/*
 * We can't just cache the old widget state on entry: If the pointer is
 * dragged, we receive two enter-notify-events, and the original cached
 * value would be overwritten with the GTK_STATE_ACTIVE we just set.
 *
 * However, we know that the gtklist only uses GTK_STATE_SELECTED and
 * GTK_STATE_NORMAL. We're OK if we only cache those two.
 */
static gboolean
cb_enter (GtkWidget *w, GdkEventCrossing *event,
	  gpointer user)
{
	GnmComboText *ct = user;
	GtkStateType state = GTK_WIDGET_STATE (w);

	if (state == GTK_STATE_NORMAL || state == GTK_STATE_SELECTED) {
		ct->cached_entry = w;
		ct->cache_mouse_state = state;
	}
	if (state != GTK_STATE_SELECTED)
		gtk_widget_set_state (w, GTK_STATE_ACTIVE);

	return TRUE;
}
static gboolean
cb_exit (GtkWidget *w, GdkEventCrossing *event,
	  gpointer user)
{
	GnmComboText *ct = user;

	if (ct->cached_entry == w)
		gtk_widget_set_state (w, ct->cache_mouse_state);

	return TRUE;
}

static gboolean
cb_pop_down (GtkWidget *w, GtkWidget *pop_down, gpointer dummy)
{
	GnmComboText *ct = GNM_COMBO_TEXT (w);

	if (ct->cached_entry)
		gtk_widget_set_state (ct->cached_entry, ct->cache_mouse_state);
	ct->cached_entry = NULL;

	return FALSE;
}

static void
cb_remove_from_hash (GtkWidget *child, gpointer data)
{
	GnmComboText *ct = GNM_COMBO_TEXT (data);
	gchar *value;
	
	if (ct->elements) {
		value = gtk_object_get_data (GTK_OBJECT (child), "value");
		g_hash_table_remove (ct->elements, value);
	}
}

void
gnm_combo_text_add_item (GnmComboText *ct,
			 const gchar *item,
			 const gchar *value)
{
	GtkWidget *listitem;
	gchar *value_copy;

	g_return_if_fail (item);

	if (!value)
		value = item;

	value_copy = g_strdup (value);

	listitem = gtk_list_item_new_with_label (item);
	gtk_widget_show (listitem);

	gtk_object_set_data_full (GTK_OBJECT (listitem), "value",
				  value_copy, g_free);
	gtk_signal_connect (GTK_OBJECT (listitem), "enter-notify-event",
			    GTK_SIGNAL_FUNC (cb_enter),
			    (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (listitem), "leave-notify-event",
			    GTK_SIGNAL_FUNC (cb_exit),
			    (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (listitem), "toggle",
			    GTK_SIGNAL_FUNC (cb_toggle),
			    (gpointer) ct);

	gtk_container_add (GTK_CONTAINER (ct->list),
			   listitem);

	g_hash_table_insert (ct->elements, (gpointer)value_copy,
			     (gpointer) listitem);

	gtk_signal_connect (GTK_OBJECT (listitem), "destroy",
			    GTK_SIGNAL_FUNC (cb_remove_from_hash),
			    (gpointer) ct);
}

static void
cb_list_mapped (GtkWidget *widget, gpointer user_data)
{
	GtkList *list = GTK_LIST (widget);

	if (g_list_length (list->selection) > 0)
		gtk_widget_grab_focus (GTK_WIDGET ((list->selection->data)));
}

void
gnm_combo_text_construct (GnmComboText *ct, gboolean const is_scrolled)
{
	GtkWidget *entry, *list, *scroll, *display_widget;

	ct->case_sensitive = FALSE;
	ct->elements = g_hash_table_new (&strcase_hash,
					 &strcase_equal);

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

	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (entry_activate_cb),
			    (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (list), "select-child",
			    GTK_SIGNAL_FUNC (list_select_cb),
			    (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (list), "unselect-child",
			    GTK_SIGNAL_FUNC (list_unselect_cb),
			    (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (list), "map",
			    GTK_SIGNAL_FUNC (cb_list_mapped), NULL);

	gtk_widget_show (display_widget);
	gtk_widget_show (entry);
	gtk_combo_box_construct (GTK_COMBO_BOX (ct), entry, display_widget);
	gtk_signal_connect (GTK_OBJECT (ct), "pop_down_done",
			    GTK_SIGNAL_FUNC (cb_pop_down), NULL);
}

GtkWidget*
gnm_combo_text_new (gboolean const is_scrolled)
{
	GnmComboText *ct;

	ct = gtk_type_new (gnm_combo_text_get_type ());
	gnm_combo_text_construct (ct, is_scrolled);
	return GTK_WIDGET (ct);
}
