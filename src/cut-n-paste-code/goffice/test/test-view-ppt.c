/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * test-view-ppt.c - 
 * Copyright (C) 2002, Ximian, Inc.
 *
 * Authors:
 *    <clahey@ximian.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU Library General Public
 * License as published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this file; if not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 **/


#include <goffice/goffice-config.h>
#include <stdio.h>
#include <gsf/gsf-utils.h>
#include <glib.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkmain.h>
#include <gdk/gdkkeysyms.h>

#include <libpresent/load-ppt.h>
#include <libpresent/present-view.h>

gint window_count;

static gboolean
key_press_event (GtkWidget      *widget,
		 GdkEventKey    *event,
		 GtkWidget      *window)
{
	if (event->type != GDK_KEY_PRESS ||
	    (event->state != 0 &&
	     event->state != GDK_CONTROL_MASK))
		return FALSE;

	if (event->keyval == GDK_q ||
	    event->keyval == GDK_Q ||
	    event->keyval == GDK_Escape) {
		gtk_widget_destroy (window);
		window_count --;
		if (window_count == 0) {
			gtk_main_quit();
		}
	} else
		return FALSE;

	return TRUE;
}

int
main (int argc, char *argv[])
{
	int k;

	gtk_init (&argc, &argv);
	gsf_init ();
	for (k = 1 ; k < argc ; k++) {
		PresentPresentation *presentation;
		PresentView *view;
		GtkWidget *window;

		presentation = load_ppt (argv[k]);

		if (presentation) {
			view = present_view_new (presentation);
			gtk_widget_show (GTK_WIDGET (view));

			window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
			gtk_window_fullscreen (GTK_WINDOW (window));
			gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (view));
			window_count ++;
			g_signal_connect (window, "key_press_event",
					  G_CALLBACK (key_press_event), window);
			gtk_widget_show (window);
			g_object_unref (presentation);
		}
	}

	gtk_main();

	gsf_shutdown ();

	return 0;
}


