/*
 * boot.c
 *
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>
#include "error-info.h"
#include <module-plugin-defs.h>

#include "perl-loader.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

GType perl_get_loader_type (ErrorInfo **ret_error);

GType
perl_get_loader_type (ErrorInfo **ret_error)
{
	GNM_INIT_RET_ERROR_INFO (ret_error);
	return TYPE_GNM_PLUGIN_LOADER_PERL;
}

void
plugin_cleanup(void)
{
}

void
plugin_init (void)
{
}
