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

static void
no_cleanup_for_me (PluginData *pd)
{
        return;
}

#define PERL_TITLE _("Perl Plugin")
#define PERL_DESCR _("This plugin enables PERL support in Gnumeric")

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	char *argv[] = { "", NULL, NULL, NULL };
	char *arg;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	/* Initialize the Perl interpreter. */
	arg = gnome_unconditional_datadir_file("gnumeric/perl/lib");
	argv[1] = g_strconcat("-I", arg, NULL);
	g_free(arg);
	argv[2] = gnome_unconditional_datadir_file("gnumeric/perl/startup.pl");
	gnumeric_perl_interp = perl_alloc();
	perl_construct(gnumeric_perl_interp);
	perl_parse(gnumeric_perl_interp, xs_init, 3, argv, NULL);
	perl_run(gnumeric_perl_interp);

	if (plugin_data_init (pd, no_unloading_for_me, no_cleanup_for_me,
			      PERL_TITLE, PERL_DESCR))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;

}
