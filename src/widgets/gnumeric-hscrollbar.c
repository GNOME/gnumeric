/*
 * gnumeric-hscrollbar.c : The Gnumeric Horizontal scrollbar widget.
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
#include "application.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "gnumeric-hscrollbar.h"

#include <gnome.h>
#include <gal/util/e-util.h>

enum {
	OFFSET_CHANGED,
	LAST_SIGNAL
};

static guint hscrollbar_signals[LAST_SIGNAL] = { 0 };
static GtkWidgetClass *parent_class = NULL;

static gint
gnumeric_hscrollbar_timer (GtkRange *range)
{
	GtkAdjustment *a = range->adjustment;

	/*
	 * We increase the upper bound, this will enable scrolling trough
	 * even if the upper bound is reached. We ofcourse won't go further then
	 * maximum number of columns a sheet can consist of
	 */
	if ((a->value >= a->upper - (a->page_size * 2)) && (a->upper + 1 < SHEET_MAX_COLS)) {
		a->upper += (a->step_increment * 2);
		gtk_adjustment_changed (a);
	}

	return GTK_RANGE_CLASS (parent_class)->timer (range);
}

static void
gnumeric_hscrollbar_adjustment_value_changed (GtkAdjustment *adjustment, gpointer data)
{
	GnumericHScrollbar *hs = GNUMERIC_HSCROLLBAR (data);

	if (hs->live.now)
		gtk_signal_emit (GTK_OBJECT (hs), hscrollbar_signals[OFFSET_CHANGED],
				 (int) GTK_RANGE (hs)->adjustment->value, FALSE);
	else
		gtk_signal_emit (GTK_OBJECT (hs), hscrollbar_signals[OFFSET_CHANGED],
				 (int) GTK_RANGE (hs)->adjustment->value, TRUE);
}

static gint
gnumeric_hscrollbar_button_press (GtkWidget *widget, GdkEventButton *event)
{
	GnumericHScrollbar *hs    = GNUMERIC_HSCROLLBAR (widget);
	GtkRange           *range = GTK_RANGE (widget);

	if (event->window == range->slider) {
		gnumeric_hscrollbar_adjustment_value_changed (range->adjustment, hs);
		
		if (event->state & GDK_SHIFT_MASK)
			hs->live.now = !hs->live.def;
		else
			hs->live.now = hs->live.def;
	} else
		hs->live.now = TRUE;

	return parent_class->button_press_event (widget, event);
}

static gint
gnumeric_hscrollbar_button_release (GtkWidget *widget, GdkEventButton *event)
{
	GnumericHScrollbar *hs    = GNUMERIC_HSCROLLBAR (widget);

	gtk_signal_emit (GTK_OBJECT (hs), hscrollbar_signals[OFFSET_CHANGED],
			 (int) GTK_RANGE (hs)->adjustment->value, FALSE);
			 
	return parent_class->button_release_event (widget, event);
}

static void
gnumeric_hscrollbar_init (GnumericHScrollbar *hs)
{
	hs->live.def = hs->live.now = application_live_scrolling ();
}

static void
gnumeric_hscrollbar_class_init (GnumericHScrollbarClass *klass)
{
	GtkObjectClass *object_class = (GtkObjectClass *) klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

	parent_class = gtk_type_class (gtk_hscrollbar_get_type ());

	/*
	 * Connect our own callbacks
	 */
	widget_class->button_press_event   = gnumeric_hscrollbar_button_press;
	widget_class->button_release_event = gnumeric_hscrollbar_button_release;

	/*
	 * We override the range class's timer
	 */
	GTK_RANGE_CLASS (klass)->timer     = gnumeric_hscrollbar_timer;
	
	/*
	 * Create the signals we emit ourselves
	 */
	klass->offset_changed = NULL;
	hscrollbar_signals[OFFSET_CHANGED] =
		gtk_signal_new ("offset_changed",
				GTK_RUN_FIRST | GTK_RUN_NO_RECURSE,
				object_class->type,
				GTK_SIGNAL_OFFSET (GnumericHScrollbarClass, offset_changed),
				gtk_marshal_NONE__INT_INT,
				GTK_TYPE_NONE, 2, GTK_TYPE_INT, GTK_TYPE_INT);


	gtk_object_class_add_signals (object_class, hscrollbar_signals, LAST_SIGNAL);
}

GtkWidget *
gnumeric_hscrollbar_new (GtkAdjustment *adjustment)
{
	GtkWidget *hs;
	
	hs = gtk_widget_new (GNUMERIC_HSCROLLBAR_TYPE,
			     "adjustment", adjustment,
			     NULL);

	/*
	 * We will have to hook up a signal to the adjustment
	 * itself, this is for updating the tooltip when doing delayed scrolling
	 */
	gtk_signal_connect (GTK_OBJECT (GTK_RANGE (hs)->adjustment), "value_changed",
			    (GtkSignalFunc) gnumeric_hscrollbar_adjustment_value_changed,
			    (gpointer) hs);
			    
	return hs;
}

E_MAKE_TYPE (gnumeric_hscrollbar, "GnumericHScrollbar", GnumericHScrollbar,
	     gnumeric_hscrollbar_class_init, gnumeric_hscrollbar_init,
	     GTK_TYPE_HSCROLLBAR);
