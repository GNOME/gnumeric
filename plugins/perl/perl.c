/*
 * Plugin for Gnumeric for the Perl scripting language.
 */

#include <EXTERN.h>
#include <perl.h>
#undef _
#define _perl_dirty dirty
#undef dirty
#include <glib.h>
#include <gnome.h>
#include "../../src/gnumeric.h"
#include "../../src/plugin.h"
#define dirty _perl_dirty
#undef _perl_dirty

extern void xs_init(void);

static PerlInterpreter *gnumeric_perl_interp;

static int
no_unloading_for_me (PluginData *pd)
{
	return 0;
}

int
init_plugin(PluginData *pd)
{
	char *argv[] = { "", NULL, NULL, NULL };
	char *arg;

	/* Initialize Gnumeric plugin information. */
	pd->can_unload = no_unloading_for_me;
	pd->title = "Perl Plugin";

	/* Initialize the Perl interpreter. */
	arg = gnome_unconditional_datadir_file("gnumeric/perl/lib");
	argv[1] = g_strconcat("-I", arg, NULL);
	g_free(arg);
	argv[2] = gnome_unconditional_datadir_file("gnumeric/perl/startup.pl");
	gnumeric_perl_interp = perl_alloc();
	perl_construct(gnumeric_perl_interp);
	perl_parse(gnumeric_perl_interp, xs_init, 3, argv, NULL);
	perl_run(gnumeric_perl_interp);

	return 0;
}
