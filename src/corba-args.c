/*
 * corba-args.c:  This routine bootstraps Gnumeric with CORBA.
 *
 * The non-CORBA bootstrap code is found on normal-args.c
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <libgnorba/gnorba.h>
#include <bonobo/gnome-bonobo.h>
#include "sheet.h"
#include "main.h"
#include "embeddable-grid.h"
#include "corba.h"

void
gnumeric_arg_parse (int argc, char *argv [])
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	
	gnome_CORBA_init_with_popt_table (
		"gnumeric", VERSION, &argc, argv,
		gnumeric_popt_options, 0, &ctx, GNORBA_INIT_SERVER_FUNC, &ev);

	if (bonobo_init (gnome_CORBA_ORB (), NULL, NULL) == FALSE){
		g_error ("Failure starting up Bonobo");
	}
	if (!WorkbookFactory_init ()){
		g_warning (_("Could not initialize the Gnumeric Workbook factory"));
	}

	EmbeddableGridFactory_init ();
}
