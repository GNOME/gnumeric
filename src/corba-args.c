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
#if USING_OAF
#	include <liboaf/liboaf.h>
#else
#	include <libgnorba/gnorba.h>
#endif
#include <bonobo.h>
#include "sheet.h"
#include "main.h"
#include "embeddable-grid.h"
#include "corba.h"

void
gnumeric_arg_parse (int argc, char *argv [])
{
#if !USING_OAF
	CORBA_Environment ev;
#endif
	CORBA_ORB         orb;

	ctx = NULL;

#if USING_OAF
	gnomelib_register_popt_table (oaf_popt_options, _("Oaf options"));
	gnome_init_with_popt_table ("gnumeric", VERSION,
				    argc, argv, gnumeric_popt_options, 0, &ctx);
	
	orb = oaf_init (argc, argv);
#else
	CORBA_exception_init (&ev);

	gnome_CORBA_init_with_popt_table (
		"gnumeric", VERSION, &argc, argv,
		gnumeric_popt_options, 0, &ctx, GNORBA_INIT_SERVER_FUNC, &ev);

	CORBA_exception_free (&ev);

	orb = gnome_CORBA_ORB ();
#endif

	if (bonobo_init (orb, NULL, NULL) == FALSE)
		g_error ("Failure starting up Bonobo");

	if (!WorkbookFactory_init ())
		g_warning (_("Could not initialize the Gnumeric Workbook factory"));

	EmbeddableGridFactory_init ();
}
