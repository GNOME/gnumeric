/*
 * Support for dynamically-loaded Gnumeric plugin components.
 *
 * Authors:
 *    Tom Dyas (tdyas@romulus.rutgers.edu)
 *    Dom Lachowicz (dominicl@seas.upenn.edu)
 */

#include <config.h>
#include <dirent.h>
#include <glib.h>
#include <gmodule.h>
#include <gnome.h>
#include <string.h>
#include <sys/stat.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gutils.h"
#include "plugin.h"
#include "command-context.h"

/*
 * This structure is private
 */
struct _PluginData
{
	gchar   *file_name;
	GModule *handle;

        PluginInitFn init_plugin;
        PluginCleanupFn cleanup_plugin;
        PluginCanUnloadFn can_unload;

	gchar   *title;
        gchar   *descr;
        off_t    size;
        time_t   modified;

        gboolean initialized;
        gboolean version_checked;
	
	/* filled in by plugin */
	void    *user_data;
};

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
		gnumeric_error_plugin (context, mesg);
		g_free (mesg);
	}

	/* Ok, we've checked the version */
        pd->version_checked = TRUE;

	return mismatch;
}

static void
plugin_close (PluginData *pd)
{
        g_return_if_fail (pd != NULL );

	if (pd->handle)
		g_module_close (pd->handle);
	pd->handle = NULL;

	if (pd->file_name)
		g_free (pd->file_name);
	pd->file_name = NULL;

	if (pd->title)
		g_free (pd->title);
	pd->title = NULL;

	if (pd->descr)
		g_free (pd->descr);
	pd->descr = NULL;

	g_free (pd);
}

PluginData *
plugin_load (CommandContext *context, const gchar *modfile)
{
	PluginData *data;
	PluginInitResult res = PLUGIN_OK; /* start out optimistic */
	struct stat sbuf;

	g_return_val_if_fail (modfile != NULL, NULL);
	
	data = g_new0 (PluginData, 1);
	if (!data) {
		g_print ("allocation error");
		return NULL;
	}
	
	data->initialized = FALSE;
	data->version_checked = FALSE;
	data->file_name = g_strdup (modfile);
	data->handle = g_module_open (modfile, 0);
	if (!data->handle) {
		char *str;
		str = g_strconcat(_("unable to open module file: "), g_module_error(), NULL);
		gnumeric_error_plugin (context, str);
		g_free (data->file_name);
		g_free (str);
		g_free (data);
		return NULL;
	}
	
	if (!g_module_symbol (data->handle, "init_plugin", (gpointer *) &data->init_plugin)){
		gnumeric_error_plugin (context, 
			_("Plugin must contain init_plugin function."));
		goto error;
	}

	if (stat (data->file_name, &sbuf) < 0) {
	        gnumeric_error_plugin (context,
			_("Couldn't determine size or modification date"));
		goto error;
	} else {
	        data->size = sbuf.st_size;
		data->modified = sbuf.st_ctime;
	}	
	
	res = data->init_plugin (context, data);
	if (res != PLUGIN_OK) {
		/* Avoid displaying 2 error boxes */
		if (res == PLUGIN_ERROR)
			gnumeric_error_plugin (context,
					     _("init_plugin returned error"));
		goto error;
	}

	/* Add some extra checking to ensure that we cannot load old plugins that do
	 * not check versioning
	 */
        if (!data->version_checked) {
		gchar *mesg =
		    g_strdup_printf (_("Unable to open plugin '%s'\n"
				       "The plugin did not check it's version.\n"
				       "It is probably for a different version of Gnumeric than '%s'."),
				     data->file_name,
				     GNUMERIC_VERSION);
		gnumeric_error_plugin (context, mesg);
		g_free (mesg);
		goto error;
	}

	plugin_list = g_list_append (plugin_list, data);
	return data;

 error:
	plugin_close (data);
	return NULL;
}

void
plugin_unload (CommandContext *context, PluginData *pd)
{
	g_return_if_fail (pd != NULL);

	if (pd->can_unload && !pd->can_unload (pd)) {
		gnumeric_error_plugin (context, _("Plugin is still in use."));
		return;
	}

	if (pd->cleanup_plugin)
		pd->cleanup_plugin (pd);

	plugin_list = g_list_remove (plugin_list, pd);
	plugin_close (pd);
}

static void
plugin_load_plugins_in_dir (CommandContext *context, const char *directory)
{
	DIR *d;
	struct dirent *e;
	
	if ((d = opendir (directory)) == NULL)
		return;

	while ((e = readdir (d)) != NULL){
		int len;
		len = strlen (e->d_name);
		/*
		 * Install all files in the directory with the name
		 * gnum_*.so
		 */
		if (len > 8 &&
		    strncmp (e->d_name, "gnum_", 3) == 0 &&
		    strncmp (e->d_name + len - 3, ".so", 3) == 0){
			char *plugin_name;

			plugin_name = g_strconcat (directory, e->d_name, NULL);
			plugin_load (context, plugin_name);
			g_free (plugin_name);
		}
	}
	closedir (d);
}

static void
load_all_plugins (CommandContext *context)
{
	char *plugin_dir;

	/* Load the user plugins */
	plugin_dir = gnumeric_usr_plugin_dir ();
	if (plugin_dir != NULL) {
		plugin_load_plugins_in_dir (context, plugin_dir);
		g_free (plugin_dir);
	}

	/* Load the system plugins */
	plugin_dir = gnumeric_sys_plugin_dir ();
	if (plugin_dir != NULL) {
		plugin_load_plugins_in_dir (context, plugin_dir);
		g_free (plugin_dir);
	} else
		g_warning ("Missing plugin directory.");
}

void
plugins_init (CommandContext *context)
{
	if (!g_module_supported ())
		return;

	load_all_plugins (context);
}

/*
 * Initializes PluginData structure
 */
gboolean       
plugin_data_init (PluginData *pd, PluginCanUnloadFn can_unload_fn,
		  PluginCleanupFn cleanup_fn,
		  const gchar *title, const gchar *descr)
{
        g_return_val_if_fail (pd != NULL, FALSE);
        g_return_val_if_fail (can_unload_fn != NULL, FALSE);
	g_return_val_if_fail (cleanup_fn != NULL, FALSE);
	g_return_val_if_fail (pd->initialized == FALSE, FALSE);

	/* mark as initialized - don't init an already init()'d plugin */
	pd->initialized = TRUE;

	pd->title = g_strdup (title);
	pd->descr = g_strdup (descr);
	pd->can_unload = can_unload_fn;
	pd->cleanup_plugin = cleanup_fn;

	return TRUE;
}

const gchar *
plugin_data_get_filename (const PluginData *pd)
{
        return pd->file_name;
}

const gchar *
plugin_data_get_title (const PluginData *pd)
{
        return pd->title;
}

const gchar *
plugin_data_get_descr (const PluginData *pd)
{
        return pd->descr;
}

/*
 * Sets the plugin's private data to 'priv_data'
 * Returns the previous data stored or NULL if none
 */
void *
plugin_data_set_user_data (PluginData *pd, void *user_data)
{
        void *data = pd->user_data;

	pd->user_data = user_data;

	return data;
}

/*
 * Returns the private data of this plugin
 */
void *
plugin_data_get_user_data (const PluginData *pd)
{
        return (void *)pd->user_data;
}

/*
 * Returns the size of the plugin in bytes
 */
off_t
plugin_data_get_size (const PluginData *pd)
{
        return pd->size;
}

/*
 * Returns the last modification date of the plugin
 * In UNIX-like time
 */
time_t
plugin_data_last_modified (const PluginData *pd)
{
        return pd->modified;
}
