/*
 * boot.c
 *
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>
#include <goffice/app/error-info.h>
#include <goffice/app/module-plugin-defs.h>

#include "perl-loader.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

GType perl_get_loader_type (ErrorInfo **ret_error);

GType
perl_get_loader_type (ErrorInfo **ret_error)
{
	GNM_INIT_RET_ERROR_INFO (ret_error);
	return TYPE_GNM_PLUGIN_LOADER_PERL;
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
}

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
}
