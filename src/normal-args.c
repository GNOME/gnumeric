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
#include "main.h"

#include <libgnomeui/gnome-init.h>

void
gnumeric_arg_parse (int argc, char *argv [])
{
	gnome_init_with_popt_table (
		"gnumeric", VERSION, argc, argv, gnumeric_popt_options, 0, &ctx);
}
