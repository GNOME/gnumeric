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
#include <bonobo/bonobo-ui-main.h> 

#include <bonobo/bonobo-generic-factory.h>

#define DUMMY
#ifdef DUMMY
#include <stdio.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-persist-file.h>
#include <bonobo/bonobo-persist-stream.h>
#include <gtk/gtklabel.h>
#endif

char const *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char const *gnumeric_data_dir = GNUMERIC_DATADIR;

#ifdef DUMMY
/*
 * Loads an Workbook from a Bonobo_Stream
 */
static void
load_workbook_from_stream (BonoboPersistStream       *ps,
			   Bonobo_Stream              stream,
			   Bonobo_Persist_ContentType type,
			   void                      *data,
			   CORBA_Environment         *ev)
{
	g_message (__PRETTY_FUNCTION__);
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

BonoboObject *
dummy_add_interfaces (BonoboObject *control)
{
	BonoboPersistFile   *file;
	BonoboPersistStream *stream;
	
	g_return_val_if_fail (BONOBO_IS_OBJECT (control), NULL);

	/* Interface Bonobo::PersistStream */
	stream = bonobo_persist_stream_new
		(load_workbook_from_stream, NULL,
		 NULL, "OAFIID:GNOME_Gnumeric_Control", NULL);
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
#endif

static BonoboObject *
gnumeric_component_factory (BonoboGenericFactory *this,
			    const char           *oaf_iid,
			    void                 *data)
{
	BonoboControl *control;
	BonoboObject *retval;
#ifdef DUMMY
	GtkWidget *w;
	int a;

	g_message ("Trying to produce a '%s'", oaf_iid);
	w = gtk_label_new ("gnumeric-component "
				      "doesn't do anything useful yet");
	gtk_widget_show(w);
	control = bonobo_control_new (w);
	printf ("control=0x%p\n", control);
	if (!dummy_add_interfaces (BONOBO_OBJECT (control)))
		return NULL;

 	retval = BONOBO_OBJECT (control);

	printf ("gnumeric-component doesn't do anything useful yet\n");
	return retval;
#endif
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
