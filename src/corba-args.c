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
#include "gnumeric.h"
#include "main.h"
#include "main.h"

void
gnumeric_arg_parse (int argc, char *argv [])
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	
	gnome_CORBA_init_with_popt_table (
		"gnumeric", VERSION, &argc, argv, gnumeric_popt_options, 0, &ctx, 0, &ev);

#ifdef ENABLE_BONOBO
	if (bonobo_init (gnome_CORBA_ORB (), NULL, NULL) == FALSE){
		g_error ("Failure starting up Bonobo");
	}
#endif
}
