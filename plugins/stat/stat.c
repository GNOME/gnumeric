/*
 * Deprecated plugin
 */
#include <config.h>
#include <gnome.h>
#include "stat.h"
#include <math.h>

static int
can_unload (PluginData *pd)
{
	return TRUE;
}


static void
cleanup_plugin (PluginData *pd)
{
	g_free (pd->title);
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	pd->can_unload = can_unload;
	pd->cleanup_plugin = cleanup_plugin;
	pd->title = g_strdup (_("OLD Statistics Plugin, not required anymore"));

	return PLUGIN_OK;
}
