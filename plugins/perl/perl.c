/*
 * Plugin for Gnumeric for the Perl scripting language.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <EXTERN.h>
#include <perl.h>
#undef _
#define _perl_dirty dirty
#undef dirty
#include <glib.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/error-info.h>
#include <gnm-plugin.h>
#include "gutils.h"
#define dirty _perl_dirty
#undef _perl_dirty

GNM_PLUGIN_MODULE_HEADER;

extern void xs_init(void);

static PerlInterpreter *gnumeric_perl_interp;

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *p, GOCmdContext *cc)
{
	char *argv[] = { "", NULL, NULL, NULL };
	char *arg;

	*ret_error = NULL;
	/* Initialize the Perl interpreter. */
	arg = gnm_sys_data_dir ("perl");
	argv[1] = g_strconcat("-I", arg, NULL);
	argv[2] = g_build_filename (arg, "startup.pl", NULL);
	g_free(arg);
	gnumeric_perl_interp = perl_alloc();
	perl_construct(gnumeric_perl_interp);
	perl_parse(gnumeric_perl_interp, xs_init, 3, argv, NULL);
	perl_run(gnumeric_perl_interp);
	/* Don't try to deactivate the plugin */
	gnm_plugin_use_ref (PLUGIN);
}
