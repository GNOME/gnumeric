/*
 * Plugin for Gnumeric for the Perl scripting language.
 */

#include <config.h>
#include <EXTERN.h>
#include <perl.h>
#undef _
#define _perl_dirty dirty
#undef dirty
#include <glib.h>
#include <gnome.h>
#include "gnumeric.h"
#include "plugin.h"
#define dirty _perl_dirty
#undef _perl_dirty

gchar gnumeric_plugin_version[] = GNUMERIC_VERSION;

extern void xs_init(void);

static PerlInterpreter *gnumeric_perl_interp;

gboolean
can_deactivate_plugin (PluginInfo *pinfo)
{
	return FALSE;
}

gboolean
cleanup_plugin (PluginInfo *pinfo)
{
	return TRUE;
}

gboolean
init_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	char *argv[] = { "", NULL, NULL, NULL };
	char *arg;

	/* Initialize the Perl interpreter. */
	arg = gnumeric_sys_data_dir ("perl");
	argv[1] = g_strconcat("-I", arg, NULL);
	argv[2] = g_strconcat (arg, "startup.pl", NULL);
	g_free(arg);
	gnumeric_perl_interp = perl_alloc();
	perl_construct(gnumeric_perl_interp);
	perl_parse(gnumeric_perl_interp, xs_init, 3, argv, NULL);
	perl_run(gnumeric_perl_interp);

	return TRUE;
}
