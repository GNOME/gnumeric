/*
 * Support for dynamically-loaded Gnumeric plugin components.
 *
 * Author:
 *    Tom Dyas (tdyas@romulus.rutgers.edu)
 */

#include <unistd.h>
#include <dirent.h>
#include <glib.h>
#include <gmodule.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "plugin.h"

GList *plugin_list = NULL;

PluginData *
plugin_load (gchar *modfile)
{
	PluginData *data;

	g_return_val_if_fail (modfile != NULL, NULL);
	
	g_print("Loading plugin '%s'.\n", modfile);
	
	data = g_new0 (PluginData, 1);
	if (!data){
		g_print ("allocation error");
		return NULL;
	}
	
	data->handle = g_module_open (modfile, 0);
	if (!data->handle) {
		g_print ("unable to open module file: %s\n", g_module_error());
		g_free(data);
		return NULL;
	}
	
	if (!g_module_symbol (data->handle, "init_plugin", (gpointer *) &data->init_plugin)){
		g_print ("module must contain init_plugin function");
		goto error;
	}
	
	if (!g_module_symbol (data->handle, "cleanup_plugin", (gpointer *) &data->cleanup_plugin)){
		g_print("module must contain cleanup_plugin funciton");
		goto error;
	}

	if (data->init_plugin (data) < 0){
		g_print ("init_plugin returned error");
		goto error;
	}

	plugin_list = g_list_append (plugin_list, data);
	return data;

 error:
	g_module_close (data->handle);
	g_free (data);
	return NULL;
}

void
plugin_unload (struct PluginData *pd)
{
	g_return_if_fail (pd != NULL);
	
	g_print ("unloading plugin %s\n", g_module_name (pd->handle));

	if (pd->refcount > 0) {
		g_print ("unload_plugin: refcount is positve, cannot unload\n");
		return;
	}
	
	if (pd->cleanup_plugin)
		pd->cleanup_plugin (pd);

	plugin_list = g_list_remove (plugin_list, pd);

	g_module_close (pd->handle);
	g_free (pd);
	g_print ("unload_plugin: plugin is unloaded\n");
}

static void
plugin_load_plugins_in_dir (char *directory)
{
	DIR *d;
	struct dirent *e;
	
	if ((d = opendir (directory)) == NULL)
		return;
	
	while ((e = readdir (d)) != NULL){
		if (strncmp (e->d_name + strlen (e->d_name) - 3, ".so", 3) == 0){
			char *plugin_name;
			
			plugin_name = g_copy_strings (directory, e->d_name, NULL);
			plugin_load (plugin_name);
			g_free (plugin_name);
		}
	}
	closedir (d);
}

void
plugins_init(void)
{
	char *plugin_dir;
	char *home_dir = getenv ("HOME");
	
	if (!g_module_supported())
		return;

	g_print ("plugins_init()\n");

	/* Load the user plugins */
	plugin_dir = g_copy_strings (home_dir ? home_dir : "", "/.gnumeric/plugins", NULL);
	plugin_load_plugins_in_dir (plugin_dir);
	g_free (plugin_dir);

	/* Load the system plugins */
	plugin_dir = gnome_unconditional_libdir_file ("gnumeric/plugins");
	plugin_load_plugins_in_dir (plugin_dir);
	g_free (plugin_dir);
}

