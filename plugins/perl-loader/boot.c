/*
 * boot.c
 *
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>

#include "perl-loader.h"

static GSList *stuff_to_free;

GNM_PLUGIN_MODULE_HEADER;

void
gnm_perl_loader_free_later (gconstpointer data)
{
	stuff_to_free = g_slist_prepend (stuff_to_free, (gpointer)data);
}

GType perl_get_loader_type (GOErrorInfo **ret_error);

G_MODULE_EXPORT GType
perl_get_loader_type (GOErrorInfo **ret_error)
{
	GO_INIT_RET_ERROR_INFO (ret_error);
	return gnm_perl_plugin_loader_get_type ();
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	g_slist_free_full (stuff_to_free, g_free);
	stuff_to_free = NULL;
}

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	GTypeModule *module = go_plugin_get_type_module (plugin);
	gnm_perl_plugin_loader_register_type (module);
}
