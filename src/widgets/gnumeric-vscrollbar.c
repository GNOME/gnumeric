/*
 * gnumeric-vscrollbar.c : The Gnumeric Vertical scrollbar widget.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <gnome.h>

#include "gnumeric-type-util.h"
#include "gnumeric-util.h"

#include "gnumeric-vscrollbar.h"

enum {
  OFFSET_CHANGED,
  LAST_SIGNAL
};

static guint vscrollbar_signals[LAST_SIGNAL] = { 0 };
static GtkWidgetClass *parent_class = NULL;

static gint
gnumeric_vscrollbar_timer (GtkRange *range)
{
	GtkAdjustment *a = range->adjustment;

	/*
	 * We increase the upper bound, this will enable scrolling trough
	 * even if the upper bound is reached. We ofcourse won't go further then
	 * maximum number of rows a sheet can consist of
	 */
	if ((a->value >= a->upper - (a->page_size * 2)) && (a->upper + 1 < SHEET_MAX_ROWS)) {
		a->upper += (a->step_increment * 2);
		gtk_adjustment_changed (a);
	}

	return GTK_RANGE_CLASS (parent_class)->timer (range);
}

static void
gnumeric_vscrollbar_adjustment_value_changed (GtkAdjustment *adjustment, gpointer data)
{
	GnumericVScrollbar *vs = GNUMERIC_VSCROLLBAR (data);

	if (vs->tip) {
		char *buffer = g_strdup_printf (_("Row: %d"), (int) adjustment->value + 1);

		gtk_label_set_text (GTK_LABEL (vs->tip), buffer);
		g_free (buffer);
	}

	if (vs->live)
		gtk_signal_emit (GTK_OBJECT (vs), vscrollbar_signals[OFFSET_CHANGED],
				 (int) GTK_RANGE (vs)->adjustment->value);
}

static gint
gnumeric_vscrollbar_button_press (GtkWidget *widget, GdkEventButton *event)
{
	GnumericVScrollbar *vs    = GNUMERIC_VSCROLLBAR (widget);
	GtkRange           *range = GTK_RANGE (widget);

	vs->live = TRUE;
	if (event->window == range->slider) {
		/*
		 * We show a small tooltip which contains
		 * the toprow so the user knows where the view "lands" when the button
		 * is released
		 */	
		if (!vs->tip) 
			vs->tip = gnumeric_create_tooltip ();

		gnumeric_vscrollbar_adjustment_value_changed (GTK_RANGE (vs)->adjustment,
							      (gpointer) vs);
		
		gnumeric_position_tooltip (vs->tip, 1);
		gtk_widget_show_all (gtk_widget_get_toplevel (vs->tip));

		vs->live = FALSE;
	}
	
	return parent_class->button_press_event (widget, event);
}

static gint
gnumeric_vscrollbar_button_release (GtkWidget *widget, GdkEventButton *event)
{
	GnumericVScrollbar *vs    = GNUMERIC_VSCROLLBAR (widget);

	if (vs->tip) {
		gtk_widget_hide (gtk_widget_get_toplevel (vs->tip));
		gtk_widget_destroy (vs->tip);
		vs->tip = NULL;
	}
		
	gtk_signal_emit (GTK_OBJECT (vs), vscrollbar_signals[OFFSET_CHANGED],
			 (int) GTK_RANGE (vs)->adjustment->value);

	return parent_class->button_release_event (widget, event);
}

static void
gnumeric_vscrollbar_init (GnumericVScrollbar *vs)
{
	vs->tip  = NULL;
	vs->live = FALSE;
}

static void
gnumeric_vscrollbar_class_init (GnumericVScrollbarClass *klass)
{
	GtkObjectClass *object_class = (GtkObjectClass *) klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

	parent_class = gtk_type_class (gtk_vscrollbar_get_type ());

	/*
	 * Connect our own callbacks
	 */
	widget_class->button_press_event   = gnumeric_vscrollbar_button_press;
	widget_class->button_release_event = gnumeric_vscrollbar_button_release;

	/*
	 * We override the range class's timer
	 */
	GTK_RANGE_CLASS (klass)->timer     = gnumeric_vscrollbar_timer;
	
	/*
	 * Create the signals we emit ourselves
	 */
	klass->offset_changed = NULL;
	vscrollbar_signals[OFFSET_CHANGED] =
		gtk_signal_new ("offset_changed",
				GTK_RUN_FIRST | GTK_RUN_NO_RECURSE,
				object_class->type,
				GTK_SIGNAL_OFFSET (GnumericVScrollbarClass, offset_changed),
				gtk_marshal_NONE__INT,
				GTK_TYPE_NONE, 1, GTK_TYPE_INT);


	gtk_object_class_add_signals (object_class, vscrollbar_signals, LAST_SIGNAL);
}

GtkWidget *
gnumeric_vscrollbar_new (GtkAdjustment *adjustment)
{
	GtkWidget *vs;
	
	vs = gtk_widget_new (GNUMERIC_VSCROLLBAR_TYPE,
			     "adjustment", adjustment,
			     NULL);

	/*
	 * We will have to hook up a signal to the adjustment
	 * itself, this is for updating the tooltip when doing delayed scrolling
	 */
	gtk_signal_connect (GTK_OBJECT (GTK_RANGE (vs)->adjustment), "value_changed",
			    (GtkSignalFunc) gnumeric_vscrollbar_adjustment_value_changed,
			    (gpointer) vs);
			    
	return vs;
}

GNUMERIC_MAKE_TYPE_WITH_CLASS (gnumeric_vscrollbar, "GnumericVScrollbar",
			       GnumericVScrollbar, GnumericVScrollbarClass,
			       gnumeric_vscrollbar_class_init, gnumeric_vscrollbar_init,
			       gtk_vscrollbar_get_type ())
