/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * io-context-gtk.c : gtk based io error context.
 *   It may be used e.g. for displaying progress and error messages
 *   before the first workbook is displayed.
 *
 * Author:
 * 	Jon K Hellan <hellan@acm.org>
 *
 * (C) 2002 Jon K Hellan
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gui-util.h"
#include "io-context-gtk.h"
#include "io-context-priv.h"
#include "libgnumeric.h"
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkprogressbar.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkwindow.h>

#define ICG_POPUP_DELAY 3.0

#define IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_IO_CONTEXT_GTK, IOContextGtk))
#define IS_IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_IO_CONTEXT_GTK))

struct _IOContextGtk {
	IOContext parent;
	GtkWindow *window;
	GtkProgressBar *progress_bar;
	GTimer *timer;
};

struct _IOContextGtkClass {
	IOContextClass parent_class;
};

/* FIXME: Handle deletion of the gui */

static gboolean
icg_user_is_impatient (IOContextGtk *icg)
{
	return g_timer_elapsed (icg->timer, NULL) > ICG_POPUP_DELAY;
}

static void
icg_progress_set (CommandContext *cc, gfloat val)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);
	
	if (!GTK_WIDGET_MAPPED (GTK_WIDGET (icg->window)) &&
	    icg_user_is_impatient (icg))
		gtk_widget_show_all (GTK_WIDGET (icg->window));
	gtk_progress_bar_update (icg->progress_bar, val);
}

static void
icg_progress_message_set (CommandContext *cc, gchar const *msg)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);

	if (!GTK_WIDGET_MAPPED (GTK_WIDGET (icg->window)) &&
	    icg_user_is_impatient (icg))
		gtk_widget_show_all (GTK_WIDGET (icg->window));
	gtk_progress_bar_set_text (icg->progress_bar, msg);
}

static void
icg_error_error_info (__attribute__((unused)) CommandContext *cc,
		      ErrorInfo *error)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);
	GtkWidget *dialog = gnumeric_error_info_dialog_new (error);

	if (!GTK_WIDGET_MAPPED (GTK_WIDGET (icg->window)))
		gtk_widget_show_all (GTK_WIDGET (icg->window));
	gtk_window_set_transient_for (GTK_WINDOW (dialog), icg->window);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
cb_icg_window_destroyed (GObject *window, IOContextGtk *icg)
{
	icg->window       = NULL;
	icg->progress_bar = NULL;
	gnm_shutdown ();	/* Pretend to be well behaved */
	exit (0);		/* Stop pretending */
}

static void
icg_finalize (GObject *obj)
{
	IOContextGtk *icg;

	g_return_if_fail (IS_IO_CONTEXT_GTK (obj));

	icg = IO_CONTEXT_GTK (obj);

	if (icg->window) {
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (icg->window),
			G_CALLBACK (cb_icg_window_destroyed), icg);
		gtk_window_set_focus (icg->window, NULL);
		gtk_window_set_default (icg->window, NULL);
		gtk_object_destroy (GTK_OBJECT (icg->window));
	}
	icg->window = NULL;
	icg->progress_bar = NULL;
	if (icg->timer) 
		g_timer_destroy (icg->timer);
	icg->timer = NULL;

	G_OBJECT_CLASS (g_type_class_peek (TYPE_IO_CONTEXT))->finalize (obj);
}

static void
icg_class_init (IOContextGtkClass *klass)
{
	CommandContextClass *cc_class = COMMAND_CONTEXT_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = icg_finalize;

	cc_class->progress_set         = icg_progress_set;
	cc_class->progress_message_set = icg_progress_message_set;
	cc_class->error.error_info     = icg_error_error_info;
}

static void
icg_init (IOContextGtk *icg)
{
	icg->window       = NULL;
	icg->progress_bar = NULL;
	icg->timer        = NULL;
}

static void
icg_init_gui (IOContextGtk *icg)
{
	GtkWidget *vbox;
	GtkWidget *label;
	gchar *name_string;
	int sx, sy;
	GdkWindowHints hints;
	GdkGeometry geom;
#ifdef HAVE_GDK_SCREEN_GET_MONITOR_GEOMETRY
	GdkRectangle rect;
#endif
	
	vbox = gtk_vbox_new (FALSE, 8);
	gtk_box_pack_start (GTK_BOX (vbox),
			    gnumeric_load_image ("gnome-gnumeric.png"),
			    TRUE, FALSE, 8);

	label = gtk_label_new ("");
	name_string = g_strdup_printf
		("<span size=\"xx-large\" weight=\"bold\">%s</span>",
		 "Gnumeric");
	gtk_label_set_markup (GTK_LABEL (label), name_string);
	g_free (name_string);
	gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, FALSE, 8);

	icg->progress_bar = GTK_PROGRESS_BAR (gtk_progress_bar_new ());

	gtk_progress_bar_set_orientation (
		icg->progress_bar, GTK_PROGRESS_LEFT_TO_RIGHT);
	gtk_progress_bar_set_bar_style (
		icg->progress_bar, GTK_PROGRESS_CONTINUOUS);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (icg->progress_bar),
			    FALSE, FALSE, 8);

	icg->window = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));

	gtk_container_add (GTK_CONTAINER (icg->window),
			   GTK_WIDGET (vbox));
	g_signal_connect (G_OBJECT (icg->window), "destroy",
			  G_CALLBACK (cb_icg_window_destroyed), icg);

#ifdef HAVE_GDK_SCREEN_GET_MONITOR_GEOMETRY
	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902. This call was added for
	 * gtk2.2*/
	gdk_screen_get_monitor_geometry (icg->window->screen, 0, &rect);
	sx = rect.width;
	sy = rect.height;
#else
	sx = gdk_screen_width  ();
	sy = gdk_screen_height ();
#endif
	geom.min_width = geom.max_width = geom.base_width = sx / 3;
	geom.min_height = geom.max_height = geom.base_height = sy / 3;

	gtk_window_move (icg->window,
			 sx / 2 - geom.min_width / 2,
			 sy / 2 - geom.min_height / 2);
	hints = GDK_HINT_POS | GDK_HINT_USER_POS |
		GDK_HINT_BASE_SIZE | GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE |
		GDK_HINT_USER_SIZE;

	gtk_window_set_geometry_hints (icg->window,
				       NULL, &geom,
				       hints);

	icg->timer  = g_timer_new ();
	g_timer_start (icg->timer);
}

GSF_CLASS (IOContextGtk, io_context_gtk,
	   icg_class_init, icg_init,
	   TYPE_IO_CONTEXT)


IOContextGtk *
gnumeric_io_context_gtk_new (void)
{
	IOContextGtk *icg;

	icg = IO_CONTEXT_GTK (g_object_new (TYPE_IO_CONTEXT_GTK, NULL));
	icg_init_gui (icg);

	return icg;
}
