/*
 * boot.c
 *
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <goffice/app/go-plugin-impl.h>

#include "error-info.h"
#include "perl-loader.h"

GType perl_get_loader_type (ErrorInfo **ret_error);

GType
perl_get_loader_type (ErrorInfo **ret_error)
{
	GNM_INIT_RET_ERROR_INFO (ret_error);
	return TYPE_GNM_PLUGIN_LOADER_PERL;
}

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin)
{
}

G_MODULE_EXPORT void
go_plugin_cleanup (GOPlugin *plugin)
{
}

