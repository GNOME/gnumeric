/*
 * Plugin for Gnumeric for the Perl scripting language.
 */

#include <glib.h>
#include <gnome.h>

#include "../../src/plugin.h"

#include <EXTERN.h>
#include <perl.h>

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
    char *argv[] = { "", NULL, NULL };
    char *name;

    /* Initialize Gnumeric plugin information. */
    pd->can_unload = no_unloading_for_me;
    pd->title = "Perl Plugin";

    /* Initialize the Perl interpreter. */
    argv[1] = gnome_unconditional_datadir_file("gnumeric/perl/startup.pl");
    gnumeric_perl_interp = perl_alloc();
    perl_construct(gnumeric_perl_interp);
    perl_parse(gnumeric_perl_interp, xs_init, 2, argv, NULL);
    perl_run(gnumeric_perl_interp);
    g_free(argv[1]);
}

