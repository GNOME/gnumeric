/*
 * GtkComboBox: A customizable ComboBox.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Adrian E Feiguin (feiguin@ifir.edu.ar)
 *   Paolo Molnaro (lupus@debian.org).
 */
#include <config.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkarrow.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkframe.h>
#include "gtk-combo-box.h"

static GtkHBoxClass *gtk_combo_box_parent_class;

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
	GtkWidget *popwin;
	GtkWidget *arrow_button;
	
	/*
	 * Closure for invoking the callbacks above
	 */
	void *closure;
};

static void
gtk_combo_box_finalize (GtkObject *object)
{
	GtkComboBox *combo_box = GTK_COMBO_BOX (object);

	gtk_object_destroy (GTK_OBJECT (combo_box->priv->popwin));
	gtk_object_unref (GTK_OBJECT (combo_box->priv->popwin));
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

void
gtk_combo_box_popup_hide (GtkComboBox *combo_box)
{
	gboolean popup_info_destroyed = FALSE;
	
	g_return_if_fail (combo_box != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo_box));

	gtk_widget_hide (combo_box->priv->popwin);
	gtk_grab_remove (combo_box->priv->popwin);

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
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (combo_box->priv->arrow_button), FALSE);
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

	ph = combo_box->priv->popwin->allocation.height;
	pw = combo_box->priv->popwin->allocation.width;
	
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

	gtk_combo_box_get_pos (combo_box, &x, &y);

	gtk_widget_set_uposition (combo_box->priv->popwin, x, y);
	gtk_widget_realize (combo_box->priv->popwin);
	gtk_widget_show (combo_box->priv->popwin);

	gtk_grab_add (combo_box->priv->popwin);
	gdk_pointer_grab (combo_box->priv->popwin->window, TRUE,
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
		gtk_combo_box_popup_hide (combo_box);

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

static void
gtk_combo_box_init (GtkComboBox *combo_box)
{
	GtkWidget *arrow, *event_box;
	GdkCursor *cursor;
	
	combo_box->priv = g_new0 (GtkComboBoxPrivate, 1);

	/*
	 * Create the arrow
	 */
	combo_box->priv->arrow_button = gtk_toggle_button_new ();
	arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (combo_box->priv->arrow_button), arrow);
	gtk_box_pack_end (GTK_BOX (combo_box), combo_box->priv->arrow_button, FALSE, FALSE, 0);
	gtk_signal_connect (
		GTK_OBJECT (combo_box->priv->arrow_button), "toggled",
		GTK_SIGNAL_FUNC (gtk_combo_toggle_pressed), combo_box);
	gtk_widget_show_all (combo_box->priv->arrow_button);

	/*
	 * The pop-down container
	 */
	combo_box->priv->popwin = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_object_sink (GTK_OBJECT (combo_box->priv->popwin));
	gtk_widget_ref (combo_box->priv->popwin);
	gtk_window_set_policy (GTK_WINDOW (combo_box->priv->popwin), TRUE, TRUE, FALSE);

	event_box = gtk_event_box_new ();
	gtk_container_add (GTK_CONTAINER (combo_box->priv->popwin), event_box);
	gtk_widget_show (event_box);
	
	gtk_widget_realize (event_box);
	cursor = gdk_cursor_new (GDK_TOP_LEFT_ARROW);
	gdk_window_set_cursor (event_box->window, cursor);
	gdk_cursor_destroy (cursor);
	
	combo_box->priv->frame = gtk_frame_new (NULL);
	gtk_container_add (GTK_CONTAINER (event_box), combo_box->priv->frame);
	gtk_frame_set_shadow_type (GTK_FRAME (combo_box->priv->frame), GTK_SHADOW_OUT);
	gtk_widget_show (combo_box->priv->frame);

	gtk_signal_connect (
		GTK_OBJECT (combo_box->priv->popwin), "button_press_event",
		GTK_SIGNAL_FUNC (gtk_combo_box_button_press), combo_box);
	
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
 *
 * Sets the displayed widget for the @combo_box to be @display_widget
 */
void
gtk_combo_box_set_display (GtkComboBox *combo_box, GtkWidget *display_widget)
{
	g_return_if_fail (combo_box != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo_box));
	g_return_if_fail (display_widget != NULL);
	g_return_if_fail (GTK_IS_WIDGET (display_widget));

	if (combo_box->priv->display_widget){
		if (combo_box->priv->display_widget != display_widget)
			gtk_container_remove (GTK_CONTAINER (combo_box),
					      combo_box->priv->display_widget);
	}

	gtk_box_pack_start (GTK_BOX (combo_box), display_widget, TRUE, TRUE, 0);
}

void
gtk_combo_box_construct (GtkComboBox *combo_box, GtkWidget *display_widget, GtkWidget *pop_down_widget)
{
	g_return_if_fail (combo_box != NULL);
	g_return_if_fail (GTK_IS_COMBO_BOX (combo_box));
	g_return_if_fail (display_widget  != NULL);
	g_return_if_fail (GTK_IS_WIDGET (display_widget));

	GTK_BOX (combo_box)->spacing = 0;
	GTK_BOX (combo_box)->homogeneous = FALSE;

	combo_box->priv->pop_down_widget = pop_down_widget;
	combo_box->priv->display_widget = display_widget;

	/*
	 * Finish setup
	 */
	gtk_combo_box_set_display (combo_box, display_widget);

	gtk_container_add (GTK_CONTAINER (combo_box->priv->frame), pop_down_widget);
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


