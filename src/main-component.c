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

#include <stdio.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-persist-file.h>
#include <bonobo/bonobo-persist-stream.h>
#include "gui-gnumeric.h"
#include "workbook-view.h"
#include "workbook-control-component-priv.h"

char const *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char const *gnumeric_data_dir = GNUMERIC_DATADIR;

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
				(wbcc, control, ui_container);
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

static BonoboObject *
add_interfaces (BonoboObject *control, WorkbookControl *wbc)
{
	BonoboPersistFile   *file;
	BonoboPersistStream *stream;
	
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

	g_signal_connect (G_OBJECT (control), "activate",
			  G_CALLBACK (control_activated_cb), wbc);

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
