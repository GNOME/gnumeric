/*
 * Support for dynamically-loaded Gnumeric plugin components.
 *
 * Author:
 *    Tom Dyas (tdyas@romulus.rutgers.edu)
 */

#include <config.h>
#include <unistd.h>
#include <dirent.h>
#include <glib.h>
#include <gmodule.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "plugin.h"
#include "workbook-view.h"
#include "command-context.h"

GList *plugin_list = NULL;

/* A coarse safety check to ensure that plugins are used only with the version
 * they are compiled for.
 * 
 * @plugin_version : A string representing the version of gnumeric that the
 *                   pluging was compiled for.
 *
 * return TRUE if the plugin version and the application version do not match exactly.
 */
gboolean
plugin_version_mismatch  (CommandContext *context, PluginData *pd,
			  char const * const plugin_version)
{
	gboolean const mismatch = (strcmp (plugin_version, GNUMERIC_VERSION) != 0);

	if (mismatch) {
		gchar *mesg =
		    g_strdup_printf (_("Unable to open plugin '%s'\n"
				       "Plugin version '%s' is different from application '%s'."),
				     pd->file_name, plugin_version, GNUMERIC_VERSION);
		gnumeric_error_plugin_problem (context, mesg);
		g_free (mesg);
	}

	return mismatch;
}

PluginData *
plugin_load (Workbook *wb, const gchar *modfile)
{
	/* FIXME : Get the correct command context here. */
	CommandContext *context = workbook_command_context_gui (wb);

	PluginData *data;
	PluginInitResult res;

	g_return_val_if_fail (modfile != NULL, NULL);
	
	data = g_new0 (PluginData, 1);
	if (!data){
		g_print ("allocation error");
		return NULL;
	}
	
	data->file_name = g_strdup (modfile);
	data->handle = g_module_open (modfile, 0);
	if (!data->handle) {
		char *str;
		str = g_strconcat(_("unable to open module file: "), g_module_error(), NULL);
		gnumeric_error_plugin_problem (context, str);
		g_free (str);
		g_free (data);
		return NULL;
	}
	
	if (!g_module_symbol (data->handle, "init_plugin", (gpointer *) &data->init_plugin)){
		gnumeric_error_plugin_problem (context, 
					       _("Plugin must contain init_plugin function."));
		goto error;
	}
	
	res = data->init_plugin (context, data);
	if (res != PLUGIN_OK) {
		/* Avoid displaying 2 error boxes */
		if (res == PLUGIN_ERROR)
			gnumeric_error_plugin_problem (context, 
						       _("init_plugin returned error"));
		goto error;
	}

	plugin_list = g_list_append (plugin_list, data);
	return data;

 error:
	g_module_close (data->handle);
	g_free (data->file_name);
	g_free (data);
	return NULL;
}

void
plugin_unload (Workbook *wb, PluginData *pd)
{
	g_return_if_fail (pd != NULL);
	
	if (pd->can_unload && !pd->can_unload (pd)) {
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_INFO,
				 _("Plugin is still in use.\n"));
		return;
	}
	
	if (pd->cleanup_plugin)
		pd->cleanup_plugin (pd);

	plugin_list = g_list_remove (plugin_list, pd);

	g_module_close (pd->handle);
	g_free (pd->file_name);
	g_free (pd);
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
			
			plugin_name = g_strconcat (directory, e->d_name, NULL);
			plugin_load (NULL, plugin_name);
			g_free (plugin_name);
		}
	}
	closedir (d);
}

static void
load_all_plugins (void)
{
	char *plugin_dir;
	char const * const home_dir = getenv ("HOME");
	
	/* Load the user plugins */
	if (home_dir != NULL) {
		plugin_dir = g_strconcat (home_dir, "/.gnumeric/plugins/" GNUMERIC_VERSION "/", NULL);
		plugin_load_plugins_in_dir (plugin_dir);
		g_free (plugin_dir);
	}

	/* Load the system plugins */
	plugin_dir = gnome_unconditional_libdir_file ("gnumeric/plugins/" GNUMERIC_VERSION "/");
	plugin_load_plugins_in_dir (plugin_dir);
	g_free (plugin_dir);
}

void
plugins_init (void)
{
	if (!g_module_supported ())
		return;

	load_all_plugins ();
}

