/*
 * graph/main.c: Main graphics component file
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999, 2000 Helix Code, Inc.  http://www.helixcode.com
 */
#include <config.h>
#include <bonobo.h>
#include <libgnorba/gnorba.h>
#include "idl/Graph.h"
#include "layout.h"

CORBA_Environment ev;
CORBA_ORB orb;

static BonoboEmbeddableFactory *factory;

static int active_layouts;

static void
init_server_factory (int argc, char **argv)
{
	gnome_CORBA_init_with_popt_table (
		"graph", VERSION,
		&argc, argv, NULL, 0, NULL, GNORBA_INIT_SERVER_FUNC, &ev);

	orb = gnome_CORBA_ORB ();

	if (bonobo_init (orb, NULL, NULL) == FALSE)
		g_error (_("I could not initialize Bonobo"));
}

static void
layout_destroyed (GtkObject *layout_object)
{
	active_layouts--;

	if (active_layouts != 0)
		return;
		
	bonobo_object_unref (BONOBO_OBJECT (factory));
	gtk_main_quit ();
}

static BonoboObject *
layout_factory (BonoboEmbeddableFactory *this, void *data)
{
	Layout *layout;

	layout = layout_new ();
	if (layout == NULL)
		return NULL;

	active_layouts++;
	gtk_signal_connect (
		GTK_OBJECT (layout), "destroy",
		layout_destroyed, NULL);

	return BONOBO_OBJECT (layout);
}

static void
layout_factory_init (void)
{
	
	factory = bonobo_embeddable_factory_new (
		"GOADID:embeddable-factory:Graph:Layout", layout_factory, NULL);

	if (factory == NULL)
		g_error ("It was not possible to register a new layout factory");
}

int
main (int argc, char **argv)
{
	CORBA_exception_init (&ev);
	
	init_server_factory (argc, argv);

	layout_factory_init ();

	bonobo_activate ();

	printf ("Graph component is active\n");
	gtk_main ();

	return 0;
}
