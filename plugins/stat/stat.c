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
	
int
init_plugin (PluginData *pd)
{
	pd->can_unload = can_unload;
	pd->cleanup_plugin = cleanup_plugin;
	pd->title = g_strdup (_("OLD Statistics Plugin, not required anymore"));

	return 0;
}

static void
cleanup_plugin (PluginData *pd)
{
	g_free (pd->title);
}



			
