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

extern int gnumeric_no_splash;

#define ICG_POPUP_DELAY 3.0

#define IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_IO_CONTEXT_GTK, IOContextGtk))
#define IS_IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_IO_CONTEXT_GTK))

struct _IOContextGtk {
	IOContext parent;
	GtkWindow *window;
	GtkProgressBar *file_bar;
	GtkProgressBar *work_bar;
	GTimer *timer;
	guint files_total;
	guint files_done;

	gfloat progress;
	char *progress_msg;
};

struct _IOContextGtkClass {
	IOContextClass parent_class;
};

#warning This is wrong once we have shown a workbook.
static void
cb_icg_window_destroyed (GObject *window, IOContextGtk *icg)
{
	icg->window   = NULL;
	icg->work_bar = NULL;
	icg->file_bar = NULL;
	gnm_shutdown ();	/* Pretend to be well behaved */
	exit (0);		/* Stop pretending */
}

static gboolean
cb_hide_splash (G_GNUC_UNUSED GtkWidget *widget,
		G_GNUC_UNUSED GdkEventButton *event,
		IOContextGtk *icg)
{
	gtk_widget_hide (GTK_WIDGET (icg->window));
	return TRUE;
}

static void
icg_show_gui (IOContextGtk *icg)
{
	GtkBox *box;
	GtkWidget *frame;
	int sx, sy;
	GdkWindowHints hints;
	GdkGeometry geom;
#ifdef HAVE_GDK_SCREEN_GET_MONITOR_GEOMETRY
	GdkRectangle rect;
#endif

	box = GTK_BOX (gtk_vbox_new (FALSE, 0));
	gtk_box_pack_start (box,
		gnumeric_load_image ("gnumeric_splash.jpg"),
		TRUE, FALSE, 0);

	/* Don't show this unless we need it. */
	if (icg->files_total > 1) {
		icg->file_bar = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
		gtk_progress_bar_set_orientation (
			icg->file_bar, GTK_PROGRESS_LEFT_TO_RIGHT);
		gtk_progress_bar_set_text (icg->file_bar, "Files");
		gtk_progress_bar_set_fraction (icg->file_bar,
			(float) icg->files_done / (float) icg->files_total);
		gtk_box_pack_start (box, GTK_WIDGET (icg->file_bar),
			FALSE, FALSE, 0);
	}

	icg->work_bar = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
	gtk_progress_bar_set_orientation (
		icg->work_bar, GTK_PROGRESS_LEFT_TO_RIGHT);
	gtk_progress_bar_set_text (icg->work_bar, icg->progress_msg);
	gtk_progress_bar_set_fraction (icg->work_bar, icg->progress);
	gtk_box_pack_start (box, GTK_WIDGET (icg->work_bar),
			    FALSE, FALSE, 0);

	/* Use a POPUP here so that it does not get buried
	 * as new windows are created.  Tack on a click handler so that a user
	 * can get rid of the thing */
	icg->window = GTK_WINDOW (gtk_window_new (GTK_WINDOW_POPUP));
	g_signal_connect (G_OBJECT (icg->window),
		"button_release_event",
		G_CALLBACK (cb_hide_splash), NULL);
	g_signal_connect (G_OBJECT (icg->window),
		"destroy",
		G_CALLBACK (cb_icg_window_destroyed), icg);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
	gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (box));
	gtk_container_add (GTK_CONTAINER (icg->window), frame);

#ifdef HAVE_GDK_SCREEN_GET_MONITOR_GEOMETRY
	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902. This call was added for
	 * gtk2.2 */
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
	gtk_widget_show_all (GTK_WIDGET (icg->window));
}

static gboolean
icg_user_is_impatient (IOContextGtk *icg)
{
	return g_timer_elapsed (icg->timer, NULL) > ICG_POPUP_DELAY;
}

static void
icg_progress_set (CommandContext *cc, gfloat val)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);

	if (gnumeric_no_splash)
		return;

	if (icg->window == NULL) {
		if (!icg_user_is_impatient (icg)) {
			icg->progress = val;
			return;
		}
		icg_show_gui (icg);
	}
	gtk_progress_bar_set_fraction (icg->work_bar, val);
}

static void
icg_progress_message_set (CommandContext *cc, gchar const *msg)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);

	if (gnumeric_no_splash)
		return;

	if (icg->window == NULL) {
		if (!icg_user_is_impatient (icg)) {
			g_free (icg->progress_msg);
			icg->progress_msg = g_strdup (msg);
			return;
		}
		icg_show_gui (icg);
	}
	gtk_progress_bar_set_text (icg->work_bar, msg);
}

static void
icg_error_error_info (G_GNUC_UNUSED CommandContext *cc,
		      ErrorInfo *error)
{
	GtkWidget *dialog = gnumeric_error_info_dialog_new (error);
	gtk_widget_show_all (GTK_WIDGET (dialog));
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
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
	g_free (icg->progress_msg);
	icg->window = NULL;
	icg->work_bar = NULL;
	icg->file_bar = NULL;
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
	icg->window      = NULL;
	icg->work_bar    = NULL;
	icg->file_bar    = NULL;
	icg->files_total = 0;
	icg->files_done  = 0;
	icg->progress	 = 0.;
	icg->progress_msg = NULL;
	icg->timer  = g_timer_new ();
	g_timer_start (icg->timer);
}

GSF_CLASS (IOContextGtk, io_context_gtk,
	   icg_class_init, icg_init,
	   TYPE_IO_CONTEXT)


/* Show the additional "files" progress bar if needed */
void
icg_set_files_total (IOContextGtk *icg, guint files_total)
{
	g_return_if_fail (IS_IO_CONTEXT_GTK (icg));
	icg->files_total = files_total;
}

void
icg_inc_files_done (IOContextGtk *icg)
{
	g_return_if_fail (IS_IO_CONTEXT_GTK (icg));
	g_return_if_fail (icg->files_done < icg->files_total);

	icg->files_done++;
	if (icg->window != NULL && icg->file_bar != NULL) {
		gtk_progress_bar_set_fraction (icg->file_bar,
			 (float) icg->files_done / (float) icg->files_total);
		gtk_progress_bar_set_fraction (icg->work_bar, 0.0);
	}
}
