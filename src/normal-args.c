/*
 * normal-args.c: This file calls gnome_init.
 *
 * The default Gnumeric bootstraps with this file.  The
 * CORBA version of Gnumeric bootstraps with corba-args.c
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org).
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-ui-init.h>

poptContext
gnumeric_arg_parse (int argc, char *argv [])
{
	poptContext ctx = NULL;
	GnomeProgram *program = gnome_program_init (PACKAGE, VERSION,
		LIBGNOMEUI_MODULE, argc, argv,
		GNOME_PARAM_APP_PREFIX,		GNUMERIC_PREFIX,
		GNOME_PARAM_APP_SYSCONFDIR,	GNUMERIC_SYSCONFDIR,
		GNOME_PARAM_APP_DATADIR,	GNUMERIC_DATADIR,
		GNOME_PARAM_APP_LIBDIR,		GNUMERIC_LIBDIR,
		GNOME_PARAM_POPT_TABLE,		gnumeric_popt_options,
		NULL);

	g_object_get (G_OBJECT (program),
		GNOME_PARAM_POPT_CONTEXT,	&ctx,
		NULL);
	return ctx;
}
