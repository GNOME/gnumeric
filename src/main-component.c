/*
 * main-component.c: Main entry point for the Gnumeric component
 *
 * Author:
 *   Jon Kåre Hellan <hellan@acm.org>
 *
 * Copyright (C) 2002, Jon Kåre Hellan
 */

#include <gnumeric-config.h>
#include <glib.h>
#include <libgnumeric.h>
#include "bonobo-io.h"
#include <bonobo/bonobo-ui-main.h> 

#include <bonobo/bonobo-generic-factory.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-persist-file.h>
#include <bonobo/bonobo-persist-stream.h>
#include "workbook-control-component-priv.h"
#include "plugin.h"

char const *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char const *gnumeric_data_dir = GNUMERIC_DATADIR;

static float preferred_zoom_levels[] = {
	1.0 / 4.0, 1.0 / 2.0, 3.0 / 4.0, 1.0, 1.5, 2.0, 3.0, 5.0
};

static const gint n_zoom_levels = (sizeof (preferred_zoom_levels) / sizeof (float));

static void
control_activated_cb (BonoboControl *control, gboolean activate,
		      WorkbookControlComponent *wbcc)
{
	g_return_if_fail (BONOBO_IS_CONTROL (control));
	g_return_if_fail (IS_WORKBOOK_CONTROL_COMPONENT (wbcc));

	if (activate) {
		Bonobo_UIContainer ui_container;

		ui_container = bonobo_control_get_remote_ui_container (control,
								       NULL);
		if (ui_container != CORBA_OBJECT_NIL)
			workbook_control_component_activate
				(wbcc, ui_container);
	} else {
		BonoboUIComponent* uic;

		uic = bonobo_control_get_ui_component (control);
		if (uic) {
			bonobo_ui_component_rm (uic, "/", NULL);
			bonobo_ui_component_unset_container (uic, NULL);
		}
	}
}

/*
 * Loads an Workbook from a Bonobo_File
 */
static gint
load_workbook_from_file (BonoboPersistFile *pf, const CORBA_char *text_uri,
		      CORBA_Environment *ev, void *closure)
{
	g_message (__PRETTY_FUNCTION__);
	return 0;
}

static float
zoom_level_from_index (int index)
{
	if (index >= 0 && index < n_zoom_levels)
		return preferred_zoom_levels [index];
	else
		return 1.0;
}

/* When we respond to zoom whatever */
static void
set_zoom_level_cb (BonoboZoomable *zoomable, float new_zoom_level,
		   WorkbookControlComponent *wbcc)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_COMPONENT (wbcc));

	wbcc_set_zoom_factor (wbcc, new_zoom_level);
}

/* When 'zoom -' is pressed in Nautilus */
static void
zoom_in_cb (BonoboZoomable *zoomable, WorkbookControlComponent *wbcc)
{
 	float zoom_level;
	float new_zoom_level;
	int index, i;
	
	g_return_if_fail (IS_WORKBOOK_CONTROL_COMPONENT (wbcc));

	zoom_level = wbcc_get_zoom_factor (wbcc);

	index = -1;
	
	/* find next greater zoom level index */
	for (i = 0; i < n_zoom_levels; i++) {
		if (preferred_zoom_levels [i] > zoom_level) {
			index = i;
			break;
		}
	}
	if (index == -1)
		return;

	new_zoom_level = zoom_level_from_index (index);

	g_signal_emit_by_name (G_OBJECT (zoomable), "set_zoom_level",
			       new_zoom_level);
}

/* When 'zoom +' is pressed in Nautilus */
static void
zoom_out_cb (BonoboZoomable *zoomable, WorkbookControlComponent *wbcc)
{
 	float zoom_level;
	float new_zoom_level;
	int index, i;

	g_return_if_fail (IS_WORKBOOK_CONTROL_COMPONENT (wbcc));

	zoom_level = wbcc_get_zoom_factor (wbcc);

	index = -1;
	
	/* find next lower zoom level index */
	for (i = n_zoom_levels - 1; i >= 0; i--) {
		if (preferred_zoom_levels [i] < zoom_level) {
			index = i;
			break;
		}
	}
	if (index == -1)
		return;

	new_zoom_level = zoom_level_from_index (index);

	g_signal_emit_by_name (G_OBJECT (zoomable), "set_zoom_level",
			       new_zoom_level);
}

/* When 'zoom 100' is pressed in Nautilus */
static void
zoom_to_fit_cb (BonoboZoomable *zoomable, gpointer unused)
{
	g_signal_emit_by_name (G_OBJECT (zoomable), "set_zoom_level", 1.0);
}

/* We don't have a default, so zoom to 1.0 */
static void
zoom_to_default_cb (BonoboZoomable *zoomable, gpointer unused)
{
	g_signal_emit_by_name (G_OBJECT (zoomable), "set_zoom_level", 1.0);
}

static BonoboObject *
add_interfaces (BonoboObject *control, WorkbookControl *wbc)
{
	BonoboPersistFile   *file;
	BonoboPersistStream *stream;
	BonoboZoomable      *zoomable;
	WorkbookControlComponent *wbcc = WORKBOOK_CONTROL_COMPONENT (wbc);
	
	g_return_val_if_fail (BONOBO_IS_OBJECT (control), NULL);

	/* Interface Bonobo::PersistStream */
	stream = bonobo_persist_stream_new
		(gnumeric_bonobo_read_from_stream, NULL,
		 NULL, "OAFIID:GNOME_Gnumeric_Control", wbc);
	if (!stream) {
		bonobo_object_unref (BONOBO_OBJECT (control));
		return NULL;
	}

	bonobo_object_add_interface (BONOBO_OBJECT (control),
				     BONOBO_OBJECT (stream));

	/* Interface Bonobo::PersistFile */
	file = bonobo_persist_file_new (load_workbook_from_file, NULL,
					"OAFIID:GNOME_Gnumeric_Control", NULL);
	if (!file) {
		bonobo_object_unref (BONOBO_OBJECT (control));
		return NULL;
	}

	bonobo_object_add_interface (BONOBO_OBJECT (control),
				     BONOBO_OBJECT (file));

	/* Interface Bonobo::Zoomable */
	zoomable = bonobo_zoomable_new ();
	g_signal_connect (G_OBJECT (zoomable),
			  "set_zoom_level",
			  G_CALLBACK (set_zoom_level_cb),
			  wbc);
	g_signal_connect (G_OBJECT (zoomable),
			  "zoom_in",
			  G_CALLBACK (zoom_in_cb),
			  wbc);
	g_signal_connect (G_OBJECT (zoomable),
			  "zoom_out",
			  G_CALLBACK (zoom_out_cb),
			  wbc);
	g_signal_connect (G_OBJECT (zoomable),
			  "zoom_to_fit",
			  G_CALLBACK (zoom_to_fit_cb),
			  NULL);
	g_signal_connect (G_OBJECT (zoomable),
			  "zoom_to_default",
			  G_CALLBACK (zoom_to_default_cb),
			  NULL);
	bonobo_zoomable_set_parameters_full
		(zoomable,
		 1.0,
		 0.1,
		 5.0,
		 TRUE, TRUE, TRUE,
		 preferred_zoom_levels,
		 NULL,
		 n_zoom_levels);
	bonobo_object_add_interface (BONOBO_OBJECT (control),
				     BONOBO_OBJECT (zoomable));
	wbcc_set_zoomable (wbcc, zoomable);

	return control;
}

static BonoboObject *
gnumeric_component_factory (BonoboGenericFactory *this,
			    const char           *oaf_iid,
			    void                 *data)
{
	BonoboControl *control;
	GtkWidget *w;
	WorkbookControl *wbc;

	g_message ("Trying to produce a '%s'", oaf_iid);
	wbc = workbook_control_component_new (NULL, NULL);
	w = WORKBOOK_CONTROL_GUI (wbc)->table;
	g_object_set_data (G_OBJECT (w), WBC_KEY, wbc);
	gtk_widget_show(w);
	control = bonobo_control_new (w);
	if (!add_interfaces (BONOBO_OBJECT (control), wbc))
		return NULL;
	wbcc_set_bcontrol (WORKBOOK_CONTROL_COMPONENT (wbc), control);

	g_signal_connect (G_OBJECT (control), "activate",
			  G_CALLBACK (control_activated_cb), wbc);

	/* TODO: Do this in a common place for component and
	 * application. See TODO in main-application.c:main */
	{
		static gboolean p_initialized = FALSE;

		if (!p_initialized) {
			plugins_init (COMMAND_CONTEXT (wbc));
			p_initialized = TRUE;
		}
	}
 	return BONOBO_OBJECT (control);
}

int
main (int argc, char *argv [])
{
	init_init (argv[0]);
	
	BONOBO_FACTORY_INIT ("gnumeric-component", VERSION, &argc, argv);		
	gnm_common_init ();

	return bonobo_generic_factory_main ("OAFIID:GNOME_Gnumeric_Factory",
					    gnumeric_component_factory, NULL);
}
