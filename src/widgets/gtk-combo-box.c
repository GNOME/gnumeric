/*
 * GtkComboBox: A customizable ComboBox.

 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Adrian E Feiguin (feiguin@ifir.edu.ar)
 *   Paolo Molnaro (lupus@debian.org).
 *   Jon K Hellan (hellan@acm.org)
 */

#include <config.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkarrow.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtktearoffmenuitem.h>
#include <gdk/gdkkeysyms.h>
#include "gtk-combo-box.h"

static GtkHBoxClass *gtk_combo_box_parent_class;
static int gtk_combo_toggle_pressed (GtkToggleButton *tbutton,
				     GtkComboBox *combo_box);
static void gtk_combo_popup_tear_off (GtkComboBox *combo,
				      gboolean set_position);
static void gtk_combo_set_tearoff_state (GtkComboBox *combo,
					 gboolean torn_off);
static void gtk_combo_popup_reparent (GtkWidget *popup, GtkWidget *new_parent, 
				      gboolean unrealize);
static gboolean cb_popup_delete (GtkWidget *w, GdkEventAny *event,
			     GtkComboBox *combo);
static void gtk_combo_tearoff_bg_copy (GtkComboBox *combo);

enum {
	POP_DOWN_WIDGET,
	POP_DOWN_DONE,
	LAST_SIGNAL
};

static gint gtk_combo_box_signals [LAST_SIGNAL] = { 0, };

struct _GtkComboBoxPrivate {
	GtkWidget *pop_down_widget;
	GtkWidget *display_widget;

	/*
	 * Internal widgets used to implement the ComboBox
	 */
	GtkWidget *frame;
	GtkWidget *arrow_button;

	GtkWidget *toplevel;	/* Popup's toplevel when not torn off */
	GtkWidget *tearoff_window; /* Popup's toplevel when torn off */
	guint torn_off;
	
	GtkWidget *tearable;	/* The tearoff "button" */
	GtkWidget *popup;	/* Popup */

	/*
	 * Closure for invoking the callbacks above
	 */
	void *closure;
};

static void
gtk_combo_box_finalize (GtkObject *object)
{
	GtkComboBox *combo_box = GTK_COMBO_BOX (object);

	gtk_object_destroy (GTK_OBJECT (combo_box->priv->toplevel));
	gtk_object_unref (GTK_OBJECT (combo_box->priv->toplevel));
	if (combo_box->priv->tearoff_window) {
		gtk_object_destroy
			(GTK_OBJECT (combo_box->priv->tearoff_window));
		gtk_object_unref
			(GTK_OBJECT (combo_box->priv->tearoff_window)); // ??
	}
	g_free (combo_box->priv);

	GTK_OBJECT_CLASS (gtk_combo_box_parent_class)->finalize (object);
}

typedef GtkObject * (*GtkSignal_POINTER__NONE) (GtkObject * object,
						gpointer user_data);
static void
my_marshal_POINTER__NONE (GtkObject * object,
			  GtkSignalFunc func,
			  gpointer func_data,
			  GtkArg * args)
{
	GtkSignal_POINTER__NONE rfunc;
	GtkObject **return_val;
	return_val = GTK_RETLOC_OBJECT (args[0]);
	rfunc = (GtkSignal_POINTER__NONE) func;
	*return_val = (*rfunc) (object, func_data);
}

static void
gtk_combo_box_class_init (GtkObjectClass *object_class)
{
	gtk_combo_box_parent_class = gtk_type_class (gtk_hbox_get_type ());

	object_class->finalize = gtk_combo_box_finalize;

	gtk_combo_box_signals [POP_DOWN_WIDGET] = gtk_signal_new (
		"pop_down_widget",
		GTK_RUN_LAST,
		object_class->type,
		GTK_SIGNAL_OFFSET (GtkComboBoxClass, pop_down_widget),
		my_marshal_POINTER__NONE,
		GTK_TYPE_POINTER, 0, GTK_TYPE_NONE);

	gtk_combo_box_signals [POP_DOWN_DONE] = gtk_signal_new (
		"pop_down_done",
		GTK_RUN_LAST,
		object_class->type,
		GTK_SIGNAL_OFFSET (GtkComboBoxClass, pop_down_done),
		gtk_marshal_BOOL__POINTER,
		GTK_TYPE_BOOL, 1, GTK_TYPE_OBJECT);

	gtk_object_class_add_signals (object_class, gtk_combo_box_signals, LAST_SIGNAL);
}

static void
deactivate_arrow (GtkComboBox *combo_box)
{
	GtkToggleButton *arrow;

	arrow = GTK_TOGGLE_BUTTON (combo_box->priv->arrow_button);
	gtk_signal_handler_block_by_func
		(GTK_OBJECT (arrow),
		 GTK_SIGNAL_FUNC (gtk_combo_toggle_pressed), combo_box);

	gtk_toggle_button_set_active (arrow, FALSE);
	
       	gtk_signal_handler_unblock_by_func
		(GTK_OBJECT (arrow),
		 GTK_SIGNAL_FUNC (gtk_combo_toggle_pressed), combo_box);
}

/**
 * gtk_combo_box_popup_hide_unconditional
 * @combo_box  Combo box
 *
 * Hide popup, whether or not it is torn off.
 */
static void
gtk_combo_box_popup_hide_unconditional (GtkComboBox *combo_box)
{
	gboolean popup_info_destroyed = FALSE;

	g_return_if_fail (combo_box != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo_box));

	gtk_widget_hide (combo_box->priv->toplevel);
	gtk_widget_hide (combo_box->priv->popup);
	if (combo_box->priv->torn_off) {
		GTK_TEAROFF_MENU_ITEM (combo_box->priv->tearable)->torn_off
			= FALSE;
		gtk_combo_set_tearoff_state (combo_box, FALSE);
	}
	
	gtk_grab_remove (combo_box->priv->toplevel);
	gdk_pointer_ungrab (GDK_CURRENT_TIME);
		
	gtk_object_ref (GTK_OBJECT (combo_box->priv->pop_down_widget));
	gtk_signal_emit (GTK_OBJECT (combo_box),
			 gtk_combo_box_signals [POP_DOWN_DONE],
			 combo_box->priv->pop_down_widget, &popup_info_destroyed);

	if (popup_info_destroyed){
		gtk_container_remove (
			GTK_CONTAINER (combo_box->priv->frame),
			combo_box->priv->pop_down_widget);
		combo_box->priv->pop_down_widget = NULL;
	}
	gtk_object_unref (GTK_OBJECT (combo_box->priv->pop_down_widget));
	deactivate_arrow (combo_box);
}

/**
 * gtk_combo_box_popup_hide
 * @combo_box  Combo box
 *
 * Hide popup, but not when it is torn off.
 * This is the external interface - for subclasses and apps which expect a
 * regular combo which doesn't do tearoffs.
 */
void
gtk_combo_box_popup_hide (GtkComboBox *combo_box)
{
	if (!combo_box->priv->torn_off)
		gtk_combo_box_popup_hide_unconditional (combo_box);
	else if (GTK_WIDGET_VISIBLE (combo_box->priv->toplevel)) {
		/* Both popup and tearoff window present. Get rid of just
                   the popup shell. */
		gtk_combo_popup_tear_off (combo_box, FALSE);
		deactivate_arrow (combo_box);
	}		 
}

/*
 * Find best location for displaying
 */
static void
gtk_combo_box_get_pos (GtkComboBox *combo_box, int *x, int *y)
{
	GtkWidget *wcombo = GTK_WIDGET (combo_box);
	int ph, pw;

	gdk_window_get_origin (wcombo->window, x, y);
	*y += wcombo->allocation.height + wcombo->allocation.y;
	*x += wcombo->allocation.x;

	ph = combo_box->priv->popup->allocation.height;
	pw = combo_box->priv->popup->allocation.width;

	if ((*y + ph) > gdk_screen_height ())
		*y = gdk_screen_height () - ph;

	if ((*x + pw) > gdk_screen_width ())
		*x = gdk_screen_width () - pw;
}

static void
gtk_combo_box_popup_display (GtkComboBox *combo_box)
{
	int x, y;

	g_return_if_fail (combo_box != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo_box));
	
	/*
	 * If we have no widget to display on the popdown,
	 * create it
	 */
	if (!combo_box->priv->pop_down_widget){
		GtkWidget *pw = NULL;

		gtk_signal_emit (GTK_OBJECT (combo_box),
				 gtk_combo_box_signals [POP_DOWN_WIDGET], &pw);
		g_assert (pw != NULL);
		combo_box->priv->pop_down_widget = pw;
		gtk_container_add (GTK_CONTAINER (combo_box->priv->frame), pw);
	}

	if (combo_box->priv->torn_off) {
		/* To give the illusion that tearoff still displays the
		 * popup, we copy the image in the popup window to the
		 * background. Thus, it won't be blank after reparenting */
		gtk_combo_tearoff_bg_copy (combo_box);

		/* We force an unrealize here so that we don't trigger
		 * redrawing/ clearing code - we just want to reveal our
		 * backing pixmap.
		 */
		gtk_combo_popup_reparent (combo_box->priv->popup,
					  combo_box->priv->toplevel, TRUE);
	}

	gtk_combo_box_get_pos (combo_box, &x, &y);
	
	gtk_widget_set_uposition (combo_box->priv->toplevel, x, y);
	gtk_widget_realize (combo_box->priv->popup);
	gtk_widget_show (combo_box->priv->popup);
	gtk_widget_realize (combo_box->priv->toplevel);
	gtk_widget_show (combo_box->priv->toplevel);
	
	gtk_grab_add (combo_box->priv->toplevel);
	gdk_pointer_grab (combo_box->priv->toplevel->window, TRUE,
			  GDK_BUTTON_PRESS_MASK |
			  GDK_BUTTON_RELEASE_MASK |
			  GDK_POINTER_MOTION_MASK,
			  NULL, NULL, GDK_CURRENT_TIME);
}

static int
gtk_combo_toggle_pressed (GtkToggleButton *tbutton, GtkComboBox *combo_box)
{
	if (tbutton->active)
		gtk_combo_box_popup_display (combo_box);
	else
		gtk_combo_box_popup_hide_unconditional (combo_box);

	return TRUE;
}

static  gint
gtk_combo_box_button_press (GtkWidget *widget, GdkEventButton *event, GtkComboBox *combo_box)
{
	GtkWidget *child;

	child = gtk_get_event_widget ((GdkEvent *) event);
	if (child != widget){
		while (child){
			if (child == widget)
				return FALSE;
			child = child->parent;
		}
	}

	gtk_combo_box_popup_hide (combo_box);
	return TRUE;
}

/**
 * gtk_combo_box_key_press
 * @widget     Widget
 * @event      Event
 * @combo_box  Combo box
 *
 * Key press handler which dismisses popup on escape.
 * Popup is dismissed whether or not popup is torn off.
 */
static  gint
gtk_combo_box_key_press (GtkWidget *widget, GdkEventKey *event,
			 GtkComboBox *combo_box)
{
	if (event->keyval == GDK_Escape) {
		gtk_combo_box_popup_hide_unconditional (combo_box);
		return TRUE;
	} else
		return FALSE;
}

static void
cb_state_change (GtkWidget *widget, GtkStateType old_state, GtkComboBox *combo_box)
{
	GtkStateType const new_state = GTK_WIDGET_STATE(widget);
	gtk_widget_set_state (combo_box->priv->display_widget, new_state);
}

static void
gtk_combo_box_init (GtkComboBox *combo_box)
{
	GtkWidget *arrow;
	GdkCursor *cursor;

	combo_box->priv = g_new0 (GtkComboBoxPrivate, 1);

	/*
	 * Create the arrow
	 */
	combo_box->priv->arrow_button = gtk_toggle_button_new ();
	GTK_WIDGET_UNSET_FLAGS (combo_box->priv->arrow_button, GTK_CAN_FOCUS);

	arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (combo_box->priv->arrow_button), arrow);
	gtk_box_pack_end (GTK_BOX (combo_box), combo_box->priv->arrow_button, FALSE, FALSE, 0);
	gtk_signal_connect (
		GTK_OBJECT (combo_box->priv->arrow_button), "toggled",
		GTK_SIGNAL_FUNC (gtk_combo_toggle_pressed), combo_box);
	gtk_widget_show_all (combo_box->priv->arrow_button);

	/*
	 * prelight the display widget when mousing over the arrow.
	 */
	gtk_signal_connect (
		GTK_OBJECT (combo_box->priv->arrow_button), "state-changed",
		GTK_SIGNAL_FUNC (cb_state_change), combo_box);

	/*
	 * The pop-down container
	 */

	combo_box->priv->toplevel = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_widget_ref (combo_box->priv->toplevel);
	gtk_object_sink (GTK_OBJECT (combo_box->priv->toplevel));
	gtk_window_set_policy (GTK_WINDOW (combo_box->priv->toplevel),
			       FALSE, TRUE, FALSE);

	combo_box->priv->popup = gtk_event_box_new ();
	gtk_container_add (GTK_CONTAINER (combo_box->priv->toplevel),
			   combo_box->priv->popup);
	gtk_widget_show (combo_box->priv->popup);

	gtk_widget_realize (combo_box->priv->popup);
	cursor = gdk_cursor_new (GDK_TOP_LEFT_ARROW);
	gdk_window_set_cursor (combo_box->priv->popup->window, cursor);
	gdk_cursor_destroy (cursor);

	combo_box->priv->torn_off = FALSE;
	combo_box->priv->tearoff_window = NULL;
	
	combo_box->priv->frame = gtk_frame_new (NULL);
	gtk_container_add (GTK_CONTAINER (combo_box->priv->popup),
			   combo_box->priv->frame);
	gtk_frame_set_shadow_type (GTK_FRAME (combo_box->priv->frame), GTK_SHADOW_OUT);

	gtk_signal_connect (
		GTK_OBJECT (combo_box->priv->toplevel), "button_press_event",
		GTK_SIGNAL_FUNC (gtk_combo_box_button_press), combo_box);
	gtk_signal_connect (
		GTK_OBJECT (combo_box->priv->toplevel), "key_press_event",
		GTK_SIGNAL_FUNC (gtk_combo_box_key_press), combo_box);
}

GtkType
gtk_combo_box_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"MyGtkComboBox",
			sizeof (GtkComboBox),
			sizeof (GtkComboBoxClass),
			(GtkClassInitFunc) gtk_combo_box_class_init,
			(GtkObjectInitFunc) gtk_combo_box_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_hbox_get_type (), &info);
	}

	return type;
}

/**
 * gtk_combo_box_set_display:
 * @combo_box: the Combo Box to modify
 * @display_widget: The widget to be displayed

 * Sets the displayed widget for the @combo_box to be @display_widget
 */
void
gtk_combo_box_set_display (GtkComboBox *combo_box, GtkWidget *display_widget)
{
	g_return_if_fail (combo_box != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo_box));
	g_return_if_fail (display_widget != NULL);
	g_return_if_fail (GTK_IS_WIDGET (display_widget));

	if (combo_box->priv->display_widget &&
	    combo_box->priv->display_widget != display_widget)
		gtk_container_remove (GTK_CONTAINER (combo_box),
				      combo_box->priv->display_widget);

	combo_box->priv->display_widget = display_widget;

	gtk_box_pack_start (GTK_BOX (combo_box), display_widget, TRUE, TRUE, 0);
}

static gboolean
cb_tearable_enter_leave (GtkWidget *w, GdkEventCrossing *event, gpointer data)
{
	gboolean const flag = GPOINTER_TO_INT(data);
	gtk_widget_set_state (w, flag ? GTK_STATE_PRELIGHT : GTK_STATE_NORMAL);
	return FALSE;
}

/**
 * gtk_combo_popup_tear_off
 * @combo         Combo box
 * @set_position  Set to position of popup shell if true
 *
 * Tear off the popup
 *
 * FIXME:
 * Gtk popup menus are toplevel windows, not dialogs. I think this is wrong,
 * and make the popups dialogs. But may be there should be a way to make
 * them toplevel. We can do this after creating:
 * GTK_WINDOW (tearoff)->type = GTK_WINDOW_TOPLEVEL;
 */
static void
 gtk_combo_popup_tear_off (GtkComboBox *combo, gboolean set_position)
{
	int x, y;
	
	if (!combo->priv->tearoff_window) {
		GtkWidget *tearoff;
		gchar *title;
		
		tearoff = gtk_window_new (GTK_WINDOW_DIALOG);
		gtk_widget_ref (tearoff);
		gtk_object_sink (GTK_OBJECT (tearoff));
		combo->priv->tearoff_window = tearoff;
		gtk_widget_set_app_paintable (tearoff, TRUE);
		gtk_signal_connect (GTK_OBJECT (tearoff), "key_press_event",
				    GTK_SIGNAL_FUNC (gtk_combo_box_key_press),
				    GTK_OBJECT (combo));
		gtk_widget_realize (tearoff);
		title = gtk_object_get_data (GTK_OBJECT (combo),
					     "gtk-combo-title");
		if (title)
			gdk_window_set_title (tearoff->window, title);
		gtk_window_set_policy (GTK_WINDOW (tearoff),
				       FALSE, TRUE, FALSE);
		gtk_window_set_transient_for 
			(GTK_WINDOW (tearoff),
			 GTK_WINDOW (gtk_widget_get_toplevel
				     GTK_WIDGET (combo)));
	}

	if (GTK_WIDGET_VISIBLE (combo->priv->popup)) {
		gtk_widget_hide (combo->priv->toplevel);
		
		gtk_grab_remove (combo->priv->toplevel);
		gdk_pointer_ungrab (GDK_CURRENT_TIME);
	}

	gtk_combo_popup_reparent (combo->priv->popup,
				  combo->priv->tearoff_window, FALSE);

	/* It may have got confused about size */
	gtk_widget_queue_resize (GTK_WIDGET (combo->priv->popup));

	if (set_position) {
		gtk_combo_box_get_pos (combo, &x, &y);
		gtk_widget_set_uposition (combo->priv->tearoff_window, x, y);
	}
	gtk_widget_show (GTK_WIDGET (combo->priv->popup));
	gtk_widget_show (combo->priv->tearoff_window);
		
}

/**
 * gtk_combo_set_tearoff_state
 * @combo_box  Combo box
 * @torn_off   TRUE: Tear off. FALSE: Pop down and reattach
 *
 * Set the tearoff state of the popup
 *
 * Compare with gtk_menu_set_tearoff_state in gtk/gtkmenu.c
 */
static void       
gtk_combo_set_tearoff_state (GtkComboBox *combo,
			     gboolean  torn_off)
{
	g_return_if_fail (combo != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo));
	
	if (combo->priv->torn_off != torn_off) {
		combo->priv->torn_off = torn_off;
		
		if (combo->priv->torn_off) {
			gtk_combo_popup_tear_off (combo, TRUE);
			deactivate_arrow (combo);
		} else {
			gtk_widget_hide (combo->priv->tearoff_window);
			gtk_combo_popup_reparent (combo->priv->popup,
						  combo->priv->toplevel,
						  FALSE);
		}
	}
}

/**
 * gtk_combo_tearoff_bg_copy
 * @combo_box  Combo box
 *
 * Copy popup window image to the tearoff window.
 */
static void
gtk_combo_tearoff_bg_copy (GtkComboBox *combo)
{
	GdkPixmap *pixmap;
	GdkGC *gc;
	GdkGCValues gc_values;

	GtkWidget *widget = combo->priv->popup;

	if (combo->priv->torn_off) {
		gc_values.subwindow_mode = GDK_INCLUDE_INFERIORS;
		gc = gdk_gc_new_with_values (widget->window,
					     &gc_values, GDK_GC_SUBWINDOW);
      
		pixmap = gdk_pixmap_new (widget->window,
					 widget->allocation.width,
					 widget->allocation.height,
					 -1);

		gdk_draw_pixmap (pixmap, gc,
				 widget->window,
				 0, 0, 0, 0, -1, -1);
		gdk_gc_unref (gc);
      
		gtk_widget_set_usize (combo->priv->tearoff_window,
				      widget->allocation.width,
				      widget->allocation.height);
      
		gdk_window_set_back_pixmap
			(combo->priv->tearoff_window->window, pixmap, FALSE);
		gdk_pixmap_unref (pixmap);
	}
}

/**
 * gtk_combo_popup_reparent
 * @popup       Popup
 * @new_parent  New parent
 * @unrealize   Unrealize popup if TRUE.
 *
 * Reparent the popup, taking care of the refcounting
 *
 * Compare with gtk_menu_reparent in gtk/gtkmenu.c
 */
static void 
gtk_combo_popup_reparent (GtkWidget *popup, 
			  GtkWidget *new_parent, 
			  gboolean unrealize)
{
	GtkObject *object = GTK_OBJECT (popup);
	gboolean was_floating = GTK_OBJECT_FLOATING (object);

	gtk_object_ref (object);
	gtk_object_sink (object);

	if (unrealize) {
		gtk_object_ref (object);
		gtk_container_remove (GTK_CONTAINER (popup->parent), popup);
		gtk_container_add (GTK_CONTAINER (new_parent), popup);
		gtk_object_unref (object);
	}
	else
		gtk_widget_reparent (GTK_WIDGET (popup), new_parent);
	gtk_widget_set_usize (new_parent, -1, -1);
  
	if (was_floating)
		GTK_OBJECT_SET_FLAGS (object, GTK_FLOATING);
	else
		gtk_object_unref (object);
}

/**
 * cb_tearable_button_release
 * @w      Widget
 * @event  Event
 * @combo  Combo box
 *
 * Toggle tearoff state.
 */
static gboolean
cb_tearable_button_release (GtkWidget *w, GdkEventButton *event,
			    GtkComboBox *combo)
{
	GtkTearoffMenuItem *tearable;
	
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_TEAROFF_MENU_ITEM (w), FALSE);

	tearable = GTK_TEAROFF_MENU_ITEM (w);
	tearable->torn_off = !tearable->torn_off;

	if (!combo->priv->torn_off) {
		gboolean need_connect;
			
		need_connect = (!combo->priv->tearoff_window);
		gtk_combo_set_tearoff_state (combo, TRUE);
		if (need_connect)
			gtk_signal_connect
				(GTK_OBJECT
				 (combo->priv->tearoff_window),  
				 "delete_event",
				 GTK_SIGNAL_FUNC (cb_popup_delete),
				 combo);
	} else
		gtk_combo_box_popup_hide_unconditional (combo);
	
	return TRUE;
}

static gboolean
cb_popup_delete (GtkWidget *w, GdkEventAny *event, GtkComboBox *combo)
{
	gtk_combo_box_popup_hide_unconditional (combo);
	return TRUE;
}

void
gtk_combo_box_construct (GtkComboBox *combo_box, GtkWidget *display_widget, GtkWidget *pop_down_widget)
{
	GtkWidget *tearable;
	GtkWidget *vbox;

	g_return_if_fail (combo_box != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo_box));
	g_return_if_fail (display_widget  != NULL);
	g_return_if_fail (GTK_IS_WIDGET (display_widget));

	GTK_BOX (combo_box)->spacing = 0;
	GTK_BOX (combo_box)->homogeneous = FALSE;

	combo_box->priv->pop_down_widget = pop_down_widget;
	combo_box->priv->display_widget = NULL;

	vbox = gtk_vbox_new (FALSE, 5);
	tearable = gtk_tearoff_menu_item_new ();
	gtk_signal_connect (GTK_OBJECT (tearable), "enter-notify-event",
			    GTK_SIGNAL_FUNC (cb_tearable_enter_leave),
			    GINT_TO_POINTER (TRUE));
	gtk_signal_connect (GTK_OBJECT (tearable), "leave-notify-event",
			    GTK_SIGNAL_FUNC (cb_tearable_enter_leave),
			    GINT_TO_POINTER (FALSE));
	gtk_signal_connect (GTK_OBJECT (tearable), "button-release-event",
			    GTK_SIGNAL_FUNC (cb_tearable_button_release),
			    (gpointer) combo_box);
	gtk_box_pack_start (GTK_BOX (vbox), tearable, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), pop_down_widget, TRUE, TRUE, 0);
	combo_box->priv->tearable = tearable;

	/*
	 * Finish setup
	 */
	gtk_combo_box_set_display (combo_box, display_widget);

	gtk_container_add (GTK_CONTAINER (combo_box->priv->frame), vbox);
	gtk_widget_show_all (combo_box->priv->frame);
}

GtkWidget *
gtk_combo_box_new (GtkWidget *display_widget, GtkWidget *optional_popdown)
{
	GtkComboBox *combo_box;

	g_return_val_if_fail (display_widget  != NULL, NULL);
	g_return_val_if_fail (GTK_IS_WIDGET (display_widget), NULL);

	combo_box = gtk_type_new (gtk_combo_box_get_type ());
	gtk_combo_box_construct (combo_box, display_widget, optional_popdown);
	return GTK_WIDGET (combo_box);
}

void
gtk_combo_box_set_arrow_relief (GtkComboBox *cc, GtkReliefStyle relief)
{
	g_return_if_fail (cc != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (cc));

	gtk_button_set_relief (GTK_BUTTON (cc->priv->arrow_button), relief);
}

/**
 * gtk_combo_box_set_title
 * @combo  Combo box
 * @title  Title
 *
 * Set a title to display over the tearoff window.
 *
 * FIXME:
 *
 * This should really change the title even when the popup is already torn off.
 * I guess the tearoff window could attach a listener to title change or
 * something. But I don't think we need the functionality, so I didn't bother
 * to investigate.
 */
void       
gtk_combo_box_set_title (GtkComboBox *combo,
			 const gchar *title)
{
	g_return_if_fail (combo != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo));
	
	gtk_object_set_data_full (GTK_OBJECT (combo), "gtk-combo-title",
				  g_strdup (title), (GtkDestroyNotify) g_free);
}

/**
 * gtk_combo_box_set_arrow_sensitive
 * @combo  Combo box
 * @sensitive  Sensitivity value
 *
 * Toggle the sensitivity of the arrow button
 */

void
gtk_combo_box_set_arrow_sensitive (GtkComboBox *combo,
				   gboolean sensitive)
{
	g_return_if_fail (combo != NULL);

	gtk_widget_set_sensitive (combo->priv->arrow_button, sensitive);
}
