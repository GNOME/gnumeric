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
#include <gnome.h>
#include "plugin.h"
#include "error-info.h"
#include "module-plugin-defs.h"
#define dirty _perl_dirty
#undef _perl_dirty

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

extern void xs_init(void);

static PerlInterpreter *gnumeric_perl_interp;

void
plugin_cleanup_general (ErrorInfo **ret_error)
{
	*ret_error = NULL;
}

void
plugin_init_general (ErrorInfo **ret_error)
{
	char *argv[] = { "", NULL, NULL, NULL };
	char *arg;

	*ret_error = NULL;
	/* Initialize the Perl interpreter. */
	arg = gnumeric_sys_data_dir ("perl");
	argv[1] = g_strconcat("-I", arg, NULL);
	argv[2] = g_strconcat (arg, "startup.pl", NULL);
	g_free(arg);
	gnumeric_perl_interp = perl_alloc();
	perl_construct(gnumeric_perl_interp);
	perl_parse(gnumeric_perl_interp, xs_init, 3, argv, NULL);
	perl_run(gnumeric_perl_interp);
	/* Don't try to deactivate the plugin */
	gnm_plugin_use_ref (plugins_get_plugin_by_id ("Gnumeric_perl"));
}
