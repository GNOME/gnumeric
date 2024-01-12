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
#include <gnumeric.h>
#include <gui-util.h>
#include <io-context-gtk.h>
#include <goffice/goffice.h>
#include <application.h>
#include <libgnumeric.h>
#include <dialogs/dialogs.h>
#include <gnm-i18n.h>

#include <gsf/gsf-impl-utils.h>
#include <stdlib.h>
#include <string.h>

#define ICG_POPUP_DELAY 3.0

#define IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GNM_TYPE_IO_CONTEXT_GTK, GnmIOContextGtk))
#define GNM_IS_IO_CONTEXT_GTK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GNM_TYPE_IO_CONTEXT_GTK))

struct GnmIOContextGtk_ {
	GOIOContext parent;
	GtkWindow *window;
	GtkWindow *parent_window;
	GtkProgressBar *file_bar;
	GtkProgressBar *work_bar;
	GTimer *timer;
	guint files_total;
	guint files_done;

	double progress;
	char *progress_msg;
	gdouble latency;

	gboolean interrupted;

	gboolean show_splash;
	gboolean show_warnings;
};

struct GnmIOContextGtkClass_ {
	GOIOContextClass parent_class;
};

enum {
	PROP_0,
	PROP_SHOW_SPLASH,
	PROP_SHOW_WARNINGS
};

static void
cb_icg_window_destroyed (GObject *window, GnmIOContextGtk *icg)
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
		GnmIOContextGtk *icg)
{
	gtk_widget_hide (GTK_WIDGET (icg->window));
	return TRUE;
}

static void
cb_realize (GtkWindow *window, void *dummy)
{
	int sx, sy;
	GdkWindowHints hints;
	GtkAllocation allocation;
	GdkGeometry geom;
	GdkRectangle rect;

	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902.  */
	gdk_screen_get_monitor_geometry (gtk_window_get_screen (window),
					 0, &rect);
	sx = rect.width;
	sy = rect.height;
	gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);

	geom.base_width = allocation.width;
	geom.base_height = allocation.height;
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
icg_show_gui (GnmIOContextGtk *icg)
{
	GtkBox *box;
	GtkWidget *frame;

	box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 0));
	if (icg->show_splash)
		gtk_box_pack_start (box, gtk_image_new_from_resource ("/org/gnumeric/gnumeric/images/gnumeric_splash_1.4.png"),
				    TRUE, FALSE, 0);

	/* Don't show this unless we need it. */
	if (icg->files_total > 1) {
		double f = icg->files_done / (double)icg->files_total;
		icg->file_bar = GTK_PROGRESS_BAR
			(g_object_new (GTK_TYPE_PROGRESS_BAR,
				       "text", "Files",
				       "show-text", TRUE,
				       "fraction", f,
				       "inverted", FALSE,
				       NULL));
		gtk_box_pack_start (box, GTK_WIDGET (icg->file_bar),
				    FALSE, FALSE, 0);
	}

	icg->work_bar = GTK_PROGRESS_BAR
		(g_object_new (GTK_TYPE_PROGRESS_BAR,
			       "inverted", FALSE,
			       "text", icg->progress_msg,
			       "show-text", TRUE,
			       "fraction", icg->progress,
			       NULL));
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
		gnm_io_context_gtk_set_transient_for (icg, icg->parent_window);

	gtk_widget_show_all (GTK_WIDGET (icg->window));
}

static gboolean
icg_user_is_impatient (GnmIOContextGtk *icg)
{
	gdouble t = g_timer_elapsed (icg->timer, NULL);
	double progress = icg->progress;
	double forecast_delay = ICG_POPUP_DELAY / 3.0;
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
			double forecast = icg->latency;
			forecast += (t - icg->latency) / progress;
			ret = (forecast > ICG_POPUP_DELAY);
		}
	}

	return ret;
}

static char *
icg_get_password (GOCmdContext *cc, char const *filename)
{
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (cc);
	return icg->show_warnings ?
		dialog_get_password (icg->window, filename) : NULL;
}

static void
icg_progress_set (GOCmdContext *cc, double val)
{
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (cc);

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
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (cc);

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
icg_error_error_info (GOCmdContext *cc, GOErrorInfo *error)
{
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (cc);
	if (icg->show_warnings) {
		GtkWidget *dialog = gnm_go_error_info_dialog_create (error);
		gtk_widget_show_all (GTK_WIDGET (dialog));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
}

static void
icg_error_error_info_list (GOCmdContext *cc, GSList *error)
{
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (cc);
	if (icg->show_warnings && error != NULL && error->data != NULL) {
		GtkWidget *dialog = gnm_go_error_info_dialog_create
			(error->data);
		gtk_widget_show_all (GTK_WIDGET (dialog));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
}

static void
icg_set_num_files (GOIOContext *icg, guint files_total)
{
	GNM_IO_CONTEXT_GTK (icg)->files_total = files_total;
}

static void
icg_processing_file (GOIOContext *ioc, char const *file)
{
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (ioc);

	g_return_if_fail (icg->files_done < icg->files_total);

	icg->files_done++;
	if (icg->window != NULL && icg->file_bar != NULL) {
		int len = strlen (file);
		int maxlen = 40;

		if (icg->files_total > 0)
			gtk_progress_bar_set_fraction
				(icg->file_bar,
				 icg->files_done / (double)icg->files_total);

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
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (obj);

	gnm_io_context_gtk_discharge_splash (icg);
	g_free (icg->progress_msg);
	G_OBJECT_CLASS (g_type_class_peek (GO_TYPE_IO_CONTEXT))->finalize (obj);
}

static void
icg_set_property (GObject *obj, guint property_id,
		  GValue const *value, GParamSpec *pspec)
{
	GnmIOContextGtk *icg = GNM_IO_CONTEXT_GTK (obj);

	switch (property_id) {
	case PROP_SHOW_SPLASH:
		icg->show_splash = g_value_get_boolean (value);
		break;
	case PROP_SHOW_WARNINGS:
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
	cc_class->error.error_info_list      = icg_error_error_info_list;
}

static void
icg_class_init (GObjectClass *gobj_klass)
{
	GOIOContextClass *ioc_klass = (GOIOContextClass *)gobj_klass;

	gobj_klass->finalize	   = icg_finalize;
	gobj_klass->set_property   = icg_set_property;

        g_object_class_install_property (gobj_klass, PROP_SHOW_SPLASH,
		 g_param_spec_boolean ("show-splash",
				       P_("Show splash"),
				       P_("Show a splash screen if loading takes more than a moment"),
				       TRUE,
				       GSF_PARAM_STATIC | G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
        g_object_class_install_property (gobj_klass, PROP_SHOW_WARNINGS,
		 g_param_spec_boolean ("show-warnings",
				       P_("Show warnings"),
				       P_("Show warning and password dialogs"),
				       TRUE,
				       GSF_PARAM_STATIC | G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

	ioc_klass->set_num_files   = icg_set_num_files;
	ioc_klass->processing_file = icg_processing_file;
}

static void
icg_init (GnmIOContextGtk *icg)
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

GSF_CLASS_FULL (GnmIOContextGtk, gnm_io_context_gtk,
		NULL, NULL, icg_class_init, NULL,
		icg_init, GO_TYPE_IO_CONTEXT, 0,
		GSF_INTERFACE (icg_gnm_cmd_context_init, GO_TYPE_CMD_CONTEXT))

void
gnm_io_context_gtk_set_transient_for (GnmIOContextGtk *icg, GtkWindow *parent_window)
{
	icg->parent_window = parent_window;
	if (icg->window)
		go_gtk_window_set_transient (parent_window, icg->window);
}

gboolean
gnm_io_context_gtk_get_interrupted (GnmIOContextGtk *icg)
{
	return icg->interrupted;
}

void
gnm_io_context_gtk_discharge_splash (GnmIOContextGtk *icg)
{
	if (icg->window) {
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (icg->window),
			G_CALLBACK (cb_icg_window_destroyed), icg);
		gtk_window_set_focus (icg->window, NULL);
		gtk_window_set_default (icg->window, NULL);
		gtk_widget_destroy (GTK_WIDGET (icg->window));
		icg->window = NULL;
		icg->work_bar = NULL;
		icg->file_bar = NULL;
	}

	if (icg->timer) {
		g_timer_destroy (icg->timer);
		icg->timer = NULL;
	}
}
