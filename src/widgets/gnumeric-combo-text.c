/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-combo-text: A combo box for selecting from a list.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "gnumeric-combo-text.h"

#include <ctype.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklist.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkscrolledwindow.h>
#include <gal/util/e-util.h>

#define GNM_COMBO_TEXT_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gnm_combo_text_get_type (), GnmComboTextClass)
typedef struct _GnmComboTextClass   GnmComboTextClass;

struct _GnmComboTextClass {
	GtkComboBoxClass parent_class;

	gboolean (* selection_changed)	(GnmComboText *ct, GtkWidget *new_item);
	gboolean (* entry_changed)	(GnmComboText *ct, char const *new_str);
};

enum {
	SELECTION_CHANGED,
	ENTRY_CHANGED,
	LAST_SIGNAL
};
static guint combo_text_signals [LAST_SIGNAL] = { 0 };

static GtkObjectClass *gnm_combo_text_parent_class;

static void
cb_entry_activate (GtkWidget *entry, gpointer ct)
{
	char const *text = gtk_entry_get_text (GTK_ENTRY (entry));
	gboolean accept_change = TRUE;

	gtk_signal_emit (GTK_OBJECT (ct), combo_text_signals [ENTRY_CHANGED],
		text, &accept_change);

	if (accept_change)
		gnm_combo_text_set_text (GNM_COMBO_TEXT (ct), text,
			GNM_COMBO_TEXT_CURRENT);
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
cb_list_select (GtkWidget *list, GtkWidget *child, gpointer data)
{
	GnmComboText *ct = GNM_COMBO_TEXT (data);
	GtkEntry *entry = GTK_ENTRY (ct->entry);
	gboolean notify_entry, accept_change;
	char *text = g_strdup ((child != NULL)
		? GTK_LABEL (GTK_BIN (child)->child)->label : "");

	if (ct->cached_entry == child)
		ct->cached_entry = NULL;

	accept_change = notify_entry = TRUE;
	gtk_signal_emit (GTK_OBJECT (ct), combo_text_signals [SELECTION_CHANGED],
			 child, &notify_entry);

	if (notify_entry)
		gtk_signal_emit (GTK_OBJECT (ct), combo_text_signals [ENTRY_CHANGED],
				 text, &accept_change);

	if (accept_change)
		gtk_entry_set_text (entry, text);
	g_free (text);

	gtk_combo_box_popup_hide (GTK_COMBO_BOX (data));
}

static void
cb_list_unselect (GtkWidget *list, GtkWidget *child, gpointer data)
{
	if (GTK_WIDGET_VISIBLE (list)) /* Undo interactive unselect */
		gtk_list_select_child (GTK_LIST (list), child);
}

static void
cb_toggle (GtkWidget *child, gpointer data)
{
	GnmComboText *ct = GNM_COMBO_TEXT (data);

	gtk_list_select_child (GTK_LIST (ct->list), child);
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

static void
cb_list_mapped (GtkWidget *widget, gpointer user_data)
{
	GtkList *list = GTK_LIST (widget);

	if (g_list_length (list->selection) > 0)
		gtk_widget_grab_focus (GTK_WIDGET ((list->selection->data)));
}

static void
cb_scroll_size_request (GtkWidget *widget, GtkRequisition *requisition,
			GnmComboText *ct)
{
	GtkRequisition list_req;

	gtk_widget_size_request	(ct->list, &list_req);
	if (requisition->height < list_req.height) {
		int height = list_req.height;
		GtkWidget const *w = GTK_CONTAINER (ct->list)->focus_child;

		if (w != NULL) {
			/* Magic number, max number of items before we scroll */
			height = w->requisition.height * 10;
		if (height > list_req.height)
			height = list_req.height;
		}

		/* FIXME : Why do we need 4 ??
		 * without it things end up scrolling.
		 */
		requisition->height = height +
			GTK_CONTAINER (widget)->border_width * 2 + 4;
	}

	if (requisition->width < ct->entry->allocation.width)
		requisition->width = ct->entry->allocation.width +
			GTK_CONTAINER (widget)->border_width * 2;
}

static void
gnm_combo_text_construct (GnmComboText *ct)
{
	ct->cached_entry	= NULL;
	ct->cache_mouse_state	= GTK_STATE_NORMAL;

	ct->entry = gtk_entry_new ();
	ct->list = gtk_list_new ();
	ct->scroll = gtk_scrolled_window_new (NULL, NULL);

	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (ct->scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport (
		GTK_SCROLLED_WINDOW (ct->scroll), ct->list);
	gtk_container_set_focus_hadjustment (
		GTK_CONTAINER (ct->list),
		gtk_scrolled_window_get_hadjustment (
			GTK_SCROLLED_WINDOW (ct->scroll)));
	gtk_container_set_focus_vadjustment (
		GTK_CONTAINER (ct->list),
		gtk_scrolled_window_get_vadjustment (
			GTK_SCROLLED_WINDOW (ct->scroll)));

	gtk_signal_connect (GTK_OBJECT (ct->entry),
		"activate",
		GTK_SIGNAL_FUNC (cb_entry_activate), (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (ct->scroll),
		"size_request",
		GTK_SIGNAL_FUNC (cb_scroll_size_request), (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (ct->list),
		"select-child",
		GTK_SIGNAL_FUNC (cb_list_select), (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (ct->list),
		"unselect-child",
		GTK_SIGNAL_FUNC (cb_list_unselect), (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (ct->list),
		"map",
		GTK_SIGNAL_FUNC (cb_list_mapped), NULL);

	gtk_widget_show (ct->entry);
	gtk_combo_box_construct (GTK_COMBO_BOX (ct),
		ct->entry, ct->scroll);
	gtk_signal_connect (GTK_OBJECT (ct),
		"pop_down_done",
		GTK_SIGNAL_FUNC (cb_pop_down), NULL);
}

static void
gnm_combo_text_destroy (GtkObject *object)
{
	GnmComboText *ct = GNM_COMBO_TEXT (object);

	gtk_signal_disconnect_by_func (GTK_OBJECT (ct),
				       GTK_SIGNAL_FUNC (cb_pop_down), NULL);
	gtk_signal_disconnect_by_func (GTK_OBJECT (ct->list),
				       GTK_SIGNAL_FUNC (cb_list_unselect),
				       (gpointer) ct);
	(*gnm_combo_text_parent_class->destroy) (object);
}

static void
gnm_combo_text_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = &gnm_combo_text_destroy;
	gnm_combo_text_parent_class = gtk_type_class (gtk_combo_box_get_type ());

	combo_text_signals [SELECTION_CHANGED] =
		gtk_signal_new ("selection_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (GnmComboTextClass, selection_changed),
				gtk_marshal_BOOL__POINTER,
				GTK_TYPE_BOOL, 1, GTK_TYPE_POINTER);
	combo_text_signals [ENTRY_CHANGED] =
		gtk_signal_new ("entry_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (GnmComboTextClass, entry_changed),
				gtk_marshal_BOOL__POINTER,
				GTK_TYPE_BOOL, 1, GTK_TYPE_POINTER);

	gtk_object_class_add_signals (object_class, combo_text_signals, LAST_SIGNAL);
}

/**
 * gnm_combo_text_new :
 * @cmp_func : an optional comparison routine.
 */
GtkWidget*
gnm_combo_text_new (GCompareFunc cmp_func)
{
	GnmComboText *ct;

	if (cmp_func == NULL)
		cmp_func = &g_str_equal;

	ct = gtk_type_new (gnm_combo_text_get_type ());
	ct->cmp_func = cmp_func;
	gnm_combo_text_construct (ct);
	return GTK_WIDGET (ct);
}
GtkWidget *
gnm_combo_text_glade_new (void)
{
	return gnm_combo_text_new (NULL);
}

E_MAKE_TYPE(gnm_combo_text, "GnmComboText", GnmComboText, gnm_combo_text_class_init, NULL, gtk_combo_box_get_type ())

/**
 * gnm_combo_text_set_text :
 * @ct :
 * @text : the label for the new item
 * @start : where to begin the search in the list.
 *
 * return TRUE if the item is found in the list.
 */
gboolean
gnm_combo_text_set_text (GnmComboText *ct, const gchar *text,
			 GnmComboTextSearch start)
{
	gboolean found = FALSE;
	gpointer candidate = NULL;
	GtkWidget *child;
	GtkList   *list;
	GList *ptr, *current;
	char const *label = "";

	list = GTK_LIST (ct->list);
	/* Be careful */
	current = (start != GNM_COMBO_TEXT_FROM_TOP && list->selection != NULL)
		? g_list_find (list->children, list->selection->data)
		: NULL;
	if (current != NULL) {
		if (start == GNM_COMBO_TEXT_NEXT && current != NULL)
			current = current->next;
		for (ptr = current ; ptr != NULL ; ptr = ptr->next) {
			label = GTK_LABEL (GTK_BIN (ptr->data)->child)->label;
			if (ct->cmp_func (label, text)) {
				candidate = ptr->data;
				break;
			}
		}
	}

	if (candidate == NULL)
		for (ptr = list->children ; ptr != current ; ptr = ptr->next) {
			label = GTK_LABEL (GTK_BIN (ptr->data)->child)->label;
			if (ct->cmp_func (label, text)) {
				candidate = ptr->data;
				break;
			}
		}

	gtk_signal_handler_block_by_func (GTK_OBJECT (ct->list), 
					  GTK_SIGNAL_FUNC (cb_list_select),
					  (gpointer) ct);
	gtk_signal_handler_block_by_func (GTK_OBJECT (ct->list), 
					  GTK_SIGNAL_FUNC (cb_list_unselect),
					  (gpointer) ct);
	
	gtk_list_unselect_all (list);

	/* Use visible label rather than supplied text just in case */
	if (candidate && GTK_IS_WIDGET (candidate)) {
		child = GTK_WIDGET (candidate);
		gtk_list_select_child (GTK_LIST (ct->list), child);
		gtk_widget_grab_focus (child);
		gtk_entry_set_text (GTK_ENTRY (ct->entry), label);
		found = TRUE;
	} else
		gtk_entry_set_text (GTK_ENTRY (ct->entry), text);

	gtk_signal_handler_unblock_by_func (GTK_OBJECT (ct->list), 
					    GTK_SIGNAL_FUNC (cb_list_select),
					    (gpointer) ct);
	gtk_signal_handler_unblock_by_func (GTK_OBJECT (ct->list), 
					    GTK_SIGNAL_FUNC (cb_list_unselect),
					    (gpointer) ct);
	return found;
}

/**
 * gnm_combo_text_add_item :
 * @ct : The text combo that will get the new element.
 * @label : the user visible label for the new item
 * @key   : The unique key to identify this item.
 *
 * It is ok to have multiple items with the same label, but the key must be
 * unique.
 */
GtkWidget *
gnm_combo_text_add_item (GnmComboText *ct,
			 const gchar *label)
{
	GtkWidget *listitem;

	g_return_val_if_fail (label, NULL);

	listitem = gtk_list_item_new_with_label (label);
	gtk_signal_connect (GTK_OBJECT (listitem),
		"enter-notify-event",
		GTK_SIGNAL_FUNC (cb_enter), (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (listitem),
		"leave-notify-event",
		GTK_SIGNAL_FUNC (cb_exit), (gpointer) ct);
	gtk_signal_connect (GTK_OBJECT (listitem),
		"toggle",
		GTK_SIGNAL_FUNC (cb_toggle), (gpointer) ct);

	gtk_container_add (GTK_CONTAINER (ct->list), listitem);

	gtk_widget_show (listitem);
	return listitem;
}

void
gnm_combo_text_clear (GnmComboText *ct)
{
	g_return_if_fail (GNM_IS_COMBO_TEXT (ct));

	gtk_list_clear_items (GTK_LIST (ct->list), 0, -1);
}
