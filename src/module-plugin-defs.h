#ifndef GNUMERIC_MODULE_PLUGIN_DEFS_H
#define GNUMERIC_MODULE_PLUGIN_DEFS_H

#include "plugin-loader-module.h"
#include "plugin.h"

/*
 * Every g_module plugin should put somewhere a line with:
 * GNUMERIC_MODULE_PLUGIN_INFO_DECL;
 */

#define GNUMERIC_MODULE_PLUGIN_INFO_DECL     ModulePluginFileStruct plugin_file_struct = GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER

void     plugin_init_general (ErrorInfo **ret_error);
void     plugin_cleanup_general (ErrorInfo **ret_error);
gboolean plugin_can_deactivate_general (void);

void      plugin_init (void);
void      plugin_cleanup (void);

#ifdef PLUGIN_ID
static GnmPlugin *gnm_get_current_plugin (void)
{
	static GnmPlugin *plugin = NULL;
	if (plugin == NULL) plugin = plugins_get_plugin_by_id (PLUGIN_ID);
	return plugin;
}
#define PLUGIN (gnm_get_current_plugin ())
#endif

#endif /* GNUMERIC_MODULE_PLUGIN_DEFS_H */
