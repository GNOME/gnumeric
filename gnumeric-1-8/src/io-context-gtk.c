/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * io-context-gtk.c : gtk based io error context.
 *   It may be used e.g. for displaying progress and error messages
 *   before the first workbook is displayed.
 *
 * Author:
 *	Jon K Hellan <hellan@acm.org>
 *
 * (C) 2002 Jon K Hellan
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gui-util.h"
#include "io-context-gtk.h"
#include <goffice/app/io-context-priv.h>
#include "application.h"
#include "libgnumeric.h"
#include "dialogs.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkprogressbar.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkicontheme.h>
#include <stdlib.h>
#include <string.h>

#define ICG_POPUP_DELAY 3.0

#define IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_IO_CONTEXT_GTK, IOContextGtk))
#define IS_IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_IO_CONTEXT_GTK))

struct _IOContextGtk {
	IOContext parent;
	GtkWindow *window;
	GtkWindow *parent_window;
	GtkProgressBar *file_bar;
	GtkProgressBar *work_bar;
	GTimer *timer;
	guint files_total;
	guint files_done;

	gfloat progress;
	char *progress_msg;
	gdouble latency;

	gboolean interrupted;

	gboolean show_splash;
	gboolean show_warnings;
};

struct _IOContextGtkClass {
	IOContextClass parent_class;
};

enum {
	PROP_0,
	PROP_SHOW_SPLASH,
	PROP_SHOW_WARNINGS
};

static void
cb_icg_window_destroyed (GObject *window, IOContextGtk *icg)
{
	icg->window   = NULL;
	icg->parent_window   = NULL;
	icg->work_bar = NULL;
	icg->file_bar = NULL;
	if (icg->files_done == 0) {
		gnm_shutdown ();	/* Pretend to be well behaved */
		gnm_pre_parse_shutdown ();
		exit (0);		/* Stop pretending */
	} else
		icg->interrupted = TRUE;
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
cb_realize (GtkWindow *window, void *dummy)
{
	int sx, sy;
	GdkWindowHints hints;
	GtkAllocation *allocation;
	GdkGeometry geom;
	GdkRectangle rect;

	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902.  */
	gdk_screen_get_monitor_geometry (window->screen, 0, &rect);
	sx = rect.width;
	sy = rect.height;
	allocation = &GTK_WIDGET (window)->allocation;

	geom.base_width = allocation->width;
	geom.base_height = allocation->height;
	geom.min_width = geom.max_width = geom.base_width;
	geom.min_height = geom.max_height = geom.base_height;

	gtk_window_move (window,
			 sx / 2 - geom.min_width / 2,
			 sy / 2 - geom.min_height / 2);
	hints = GDK_HINT_POS | GDK_HINT_USER_POS |
		GDK_HINT_BASE_SIZE | GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE |
		GDK_HINT_USER_SIZE;

	gtk_window_set_geometry_hints (window, NULL, &geom, hints);
	gtk_window_set_decorated (window, FALSE);
}

static void
icg_show_gui (IOContextGtk *icg)
{
	static gboolean init_splash = TRUE;
	GtkBox *box;
	GtkWidget *frame;

	if (init_splash && icg->show_splash){
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_inline
			(-1, gnumeric_splash, FALSE, NULL);
		gtk_icon_theme_add_builtin_icon ("GnmSplash",
			gdk_pixbuf_get_width (pixbuf), pixbuf);
		g_object_unref (pixbuf);
		init_splash = FALSE;
	}

	box = GTK_BOX (gtk_vbox_new (FALSE, 0));
	gtk_box_pack_start (box, gtk_image_new_from_pixbuf (
		gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					  "GnmSplash", 360, 220, NULL)),
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

	icg->window = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
	gtk_window_set_type_hint (GTK_WINDOW (icg->window),
		GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
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

	g_signal_connect (G_OBJECT (icg->window), "realize",
			  G_CALLBACK (cb_realize), NULL);

	if (icg->parent_window)
		go_gtk_window_set_transient (icg->window, icg->parent_window);

	gtk_widget_show_all (GTK_WIDGET (icg->window));
}

static gboolean
icg_user_is_impatient (IOContextGtk *icg)
{
	gdouble t = g_timer_elapsed (icg->timer, NULL);
	gfloat progress = icg->progress;
	gfloat forecast_delay = ICG_POPUP_DELAY / 3.0;
	gboolean ret = FALSE;

	if (icg->progress == 0. && icg->files_done == 0)
		icg->latency = t;

	if (t >= forecast_delay) {
		if (icg->files_total > 1) {
			progress += icg->files_done;
			progress /= icg->files_total;
		}
		if (progress <= 0.0) {
			/* We're likely to be back shortly.  */
			ret = (t > ICG_POPUP_DELAY * 0.8);
		} else {
			gfloat forecast = icg->latency;
			forecast += (t - icg->latency) / progress;
			ret = (forecast > ICG_POPUP_DELAY);
		}
	}

	return ret;
}

static char *
icg_get_password (GOCmdContext *cc, char const *filename)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);
	return icg->show_warnings ?
		dialog_get_password (icg->window, filename) : NULL;
}

static void
icg_progress_set (GOCmdContext *cc, gfloat val)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);

	if (!icg->show_splash)
		return;

	if (icg->window == NULL) {
		icg->progress = val;
		if (!icg_user_is_impatient (icg))
			return;
		icg_show_gui (icg);
	}
	gtk_progress_bar_set_fraction (icg->work_bar, val);
}

static void
icg_progress_message_set (GOCmdContext *cc, gchar const *msg)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);

	if (!icg->show_splash)
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
icg_error_error_info (GOCmdContext *cc, ErrorInfo *error)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (cc);
	if (icg->show_warnings) {
		GtkWidget *dialog = gnumeric_error_info_dialog_new (error);
		gtk_widget_show_all (GTK_WIDGET (dialog));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
}

static void
icg_set_num_files (IOContext *icg, guint files_total)
{
	IO_CONTEXT_GTK (icg)->files_total = files_total;
}

static void
icg_processing_file (IOContext *ioc, char const *file)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (ioc);

	g_return_if_fail (icg->files_done < icg->files_total);

	icg->files_done++;
	if (icg->window != NULL && icg->file_bar != NULL) {
		int len = strlen (file);
		int maxlen = 40;

		if (icg->files_total > 0)
			gtk_progress_bar_set_fraction (icg->file_bar,
				 (float) icg->files_done / (float) icg->files_total);

		gtk_progress_bar_set_fraction (icg->work_bar, 0.0);

		if (len <= maxlen)
			gtk_progress_bar_set_text (icg->file_bar, file);
		else {
			char *shown_text = g_strdup (file);
			char *p = shown_text + len;

			while (1) {
				char *last_p = p;
				while (p > shown_text && G_IS_DIR_SEPARATOR (p[-1]))
					p--;
				if (p > shown_text && shown_text + len - p < maxlen) {
					p--;
					continue;
				}
				p = g_strdup_printf ("...%s", last_p);
				gtk_progress_bar_set_text (icg->file_bar, p);
				g_free (p);
				break;
			}

			g_free (shown_text);
		}
	}
}

static void
icg_finalize (GObject *obj)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (obj);

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
icg_set_property (GObject *obj, guint property_id,
		  GValue const *value, GParamSpec *pspec)
{
	IOContextGtk *icg = IO_CONTEXT_GTK (obj);

	switch (property_id) {
	case PROP_SHOW_SPLASH :
		icg->show_splash = g_value_get_boolean (value);
		break;
	case PROP_SHOW_WARNINGS :
		icg->show_warnings = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}
static void
icg_gnm_cmd_context_init (GOCmdContextClass *cc_class)
{
	cc_class->get_password         = icg_get_password;
	cc_class->progress_set         = icg_progress_set;
	cc_class->progress_message_set = icg_progress_message_set;
	cc_class->error.error_info     = icg_error_error_info;
}

static void
icg_class_init (GObjectClass *gobj_klass)
{
	IOContextClass *ioc_klass = (IOContextClass *)gobj_klass;

	gobj_klass->finalize	   = icg_finalize;
	gobj_klass->set_property   = icg_set_property;

        g_object_class_install_property (gobj_klass, PROP_SHOW_SPLASH,
		 g_param_spec_boolean ("show-splash", "show-splash",
				       "Show a splash screen if loading takes more than a moment.",
				       TRUE,
				       GSF_PARAM_STATIC | G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
        g_object_class_install_property (gobj_klass, PROP_SHOW_WARNINGS,
		 g_param_spec_boolean ("show-warnings", "show-warnings",
				       "Show warning and password dialogs.",
				       TRUE,
				       GSF_PARAM_STATIC | G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

	ioc_klass->set_num_files   = icg_set_num_files;
	ioc_klass->processing_file = icg_processing_file;
}

static void
icg_init (IOContextGtk *icg)
{
	icg->show_splash   = TRUE;
	icg->show_warnings = TRUE;

	icg->window        = NULL;
	icg->work_bar      = NULL;
	icg->file_bar      = NULL;
	icg->files_total   = 0;
	icg->files_done    = 0;
	icg->progress	   = 0.;
	icg->progress_msg  = NULL;
	icg->latency	   = 0.;
	icg->interrupted   = FALSE;
	icg->timer	   = g_timer_new ();
	g_timer_start (icg->timer);
}

GSF_CLASS_FULL (IOContextGtk, io_context_gtk,
		NULL, NULL, icg_class_init, NULL,
		icg_init, TYPE_IO_CONTEXT, 0,
		GSF_INTERFACE (icg_gnm_cmd_context_init, GO_CMD_CONTEXT_TYPE))

void
icg_set_transient_for (IOContextGtk *icg, GtkWindow *parent_window)
{
	icg->parent_window = parent_window;
	if (icg->window)
		go_gtk_window_set_transient (icg->window, parent_window);
}

gboolean
icg_get_interrupted (IOContextGtk *icg)
{
	return icg->interrupted;
}
