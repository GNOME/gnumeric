/*
 * Support for dynamically-loaded Gnumeric plugin components.
 *
 * Authors:
 *  Old plugin engine:
 *    Tom Dyas (tdyas@romulus.rutgers.edu)
 *    Dom Lachowicz (dominicl@seas.upenn.edu)
 *  New plugin engine:
 *    Zbigniew Chyla (cyba@gnome.pl)
 */

#include <config.h>
#include <dirent.h>
#include <glib.h>
#include <gmodule.h>
#include <gnome.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include <gal/util/e-util.h>
#include <gnome-xml/parser.h>
#include <gnome-xml/parserInternals.h>
#include <gnome-xml/xmlmemory.h>
#include <fnmatch.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gutils.h"
#include "command-context.h"
#include "xml-io.h"
#include "file.h"
#include "workbook.h"
#include "workbook-view.h"
#include "error-info.h"
#include "plugin.h"

#define PLUGIN_INFO_FILE_NAME          "plugin.xml"

typedef enum {
	PLUGIN_ACTIVATION_MODULE,
	PLUGIN_ACTIVATION_LAST
} PluginActivation;

struct _PluginInfo {
	gchar   *dir_name;
	gchar   *id;
	gchar   *name;
	gchar   *description;

	void     (*free_func) (PluginInfo *);
	void     (*activate_func) (PluginInfo *, ErrorInfo **error);
	void     (*deactivate_func) (PluginInfo *, ErrorInfo **error);
	gboolean (*can_deactivate_func) (PluginInfo *pinfo);
	gint     (*get_extra_info_list_func) (PluginInfo *pinfo, GList **ret_keys_list, GList **ret_values_list);
	void     (*print_info_func) (PluginInfo *);

	gboolean is_active;

	PluginActivation activation;
	union {
		struct {
			GModule *handle;
			gchar *file_name;
			gboolean (*cleanup_plugin_func) (PluginInfo *pinfo);
			gboolean (*can_deactivate_plugin_func) (PluginInfo *pinfo);
		} module;
	} t;
};

static const gchar *plugin_activation_type_strings[PLUGIN_ACTIVATION_LAST] = {
	"g_module"
};

static GList *known_plugin_id_list = NULL;
static gboolean known_plugin_id_list_is_ready = FALSE;

static GList *available_plugin_info_list = NULL;
static gboolean available_plugin_info_list_is_ready = FALSE;

static GList *saved_active_plugin_id_list = NULL;
static gboolean saved_active_plugin_id_list_is_ready = FALSE;


static void module_plugin_free (PluginInfo *pinfo);
static void module_plugin_info_read (PluginInfo *pinfo, xmlNodePtr tree, ErrorInfo **error);
static void module_plugin_activate (PluginInfo *pinfo, ErrorInfo **error);
static void module_plugin_deactivate (PluginInfo *pinfo, ErrorInfo **error);
static gboolean module_plugin_can_deactivate (PluginInfo *pinfo);
static gint module_plugin_info_get_extra_info_list (PluginInfo *pinfo, GList **ret_keys_list, GList **ret_values_list);
static void module_plugin_print_info (PluginInfo *pinfo);

/*
 * Accessor functions
 */

gchar *
plugin_info_get_dir_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->dir_name);
}

gchar *
plugin_info_get_id (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->id);
}

gchar *
plugin_info_get_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->name);
}

gchar *
plugin_info_get_description (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->description);
}

gboolean
plugin_info_is_active (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, FALSE);

	return pinfo->is_active;
}

gint
plugin_info_get_extra_info_list (PluginInfo *pinfo, GList **ret_keys_list, GList **ret_values_list)
{
	GList *keys_list, *values_list;
	gint n_items;

	g_return_val_if_fail (pinfo != NULL, 0);
	g_return_val_if_fail (ret_keys_list != NULL, 0);
	g_return_val_if_fail (ret_values_list != NULL, 0);

	if (pinfo->get_extra_info_list_func != NULL) {
		n_items = pinfo->get_extra_info_list_func (pinfo, &keys_list, &values_list);
		*ret_keys_list = keys_list;
		*ret_values_list = values_list;
	} else {
		n_items = 0;
		*ret_keys_list = NULL;
		*ret_values_list = NULL;
	}

	return n_items;
}

const gchar *
plugin_info_peek_dir_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->dir_name;
}

const gchar *
plugin_info_peek_id (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->id;
}

const gchar *
plugin_info_peek_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->name;
}

const gchar *
plugin_info_peek_description (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->description;
}

/*
 *
 */

PluginInfo *
plugin_info_read (const gchar *dir_name, xmlNodePtr tree, ErrorInfo **ret_error)
{
	PluginInfo *pinfo = NULL;
	xmlChar *x_id, *x_name, *x_description, *x_activation_type;
	xmlNodePtr information_node, activation_node;
	PluginActivation activation;

	*ret_error = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);
	g_return_val_if_fail (xmlStrcmp (tree->name, "plugin") == 0, NULL);

	x_id = xmlGetProp (tree, "id");
	information_node = xml_search_child_lang_list (tree, "information", NULL);
	if (information_node != NULL) {
		x_name = xmlGetProp (information_node, "name");
		x_description = xmlGetProp (information_node, "description");
	} else {
		x_name = NULL;
		x_description = NULL;
	}
	activation_node = xml_search_child (tree, "activation");
	if (activation_node != NULL) {
		x_activation_type = xmlGetProp (activation_node, "type");
	} else {
		x_activation_type = NULL;
	}
	if (x_activation_type == NULL) {
		activation = PLUGIN_ACTIVATION_LAST;
	} else {
		for (activation = 0; activation < PLUGIN_ACTIVATION_LAST; activation++) {
			if (xmlStrcmp (x_activation_type, plugin_activation_type_strings[activation]) == 0) {
				break;
			}
		}
	}
	if (x_id != NULL && x_name != NULL && activation != PLUGIN_ACTIVATION_LAST) {
		ErrorInfo *error;

		pinfo = g_new0 (PluginInfo, 1);
		pinfo->dir_name = g_strdup (dir_name);
		pinfo->id = g_strdup (x_id);
		pinfo->name = g_strdup (x_name);
		pinfo->description = g_strdup (x_description);
		pinfo->activation = activation;
		pinfo->is_active = FALSE;
		switch (pinfo->activation) {
		case PLUGIN_ACTIVATION_MODULE: {
			module_plugin_info_read (pinfo, tree, &error);
			break;
		}
		default:
			g_assert_not_reached ();
		}
		if (error != NULL) {
			*ret_error = error_info_new_printf (
			             _("Error while reading plugin information of type \"%s\"."),
			             plugin_activation_type_strings[pinfo->activation]);
			error_info_add_details (*ret_error, error);
			pinfo->activation = PLUGIN_ACTIVATION_LAST;
			plugin_info_free (pinfo);
			pinfo = NULL;
		}
	} else {
		if (x_id == NULL) {
			*ret_error = error_info_new_str (_("Plugin has no id."));
		} else {
			*ret_error = error_info_new_printf (
			             _("Required information for plugin with id=\"%s\" not found in file."),
			             x_id);
			if (x_name == NULL) {
				error_info_add_details (*ret_error, error_info_new_str (
				                                    _("Unknown plugin name.")));
			} else if (x_activation_type == NULL) {
				error_info_add_details (*ret_error, error_info_new_str (
				                                    _("Unknown activation type.")));
			}
		}
	}

	xmlFree (x_id);
	xmlFree (x_name);
	xmlFree (x_description);
	xmlFree (x_activation_type);

	return pinfo;
}

void
activate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (ret_error != NULL);
	g_return_if_fail (pinfo != NULL);

	*ret_error = NULL;
	if (pinfo->is_active) {
		return;
	}
	if (pinfo->activate_func != NULL) {
		pinfo->activate_func (pinfo, &error);
		if (error != NULL) {
			*ret_error = error;
		}
	}
}

void
deactivate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (!pinfo->is_active || !can_deactivate_plugin (pinfo)) {
		return;
	}
	if (pinfo->deactivate_func != NULL) {
		pinfo->deactivate_func (pinfo, &error);
		if (error != NULL) {
			*ret_error = error;
		}
	}
}

gboolean
can_deactivate_plugin (PluginInfo *pinfo)
{
	gboolean can_deactivate = FALSE;

	g_return_val_if_fail (pinfo != NULL, FALSE);
	g_return_val_if_fail (pinfo->is_active, FALSE);

	if (pinfo->can_deactivate_func != NULL) {
		can_deactivate = pinfo->can_deactivate_func (pinfo);
	} else {
		can_deactivate = FALSE;
	}

	return can_deactivate;
}

void
plugin_info_free (PluginInfo *pinfo)
{
	if (pinfo == NULL) {
		return;
	}
	g_free (pinfo->id);
	g_free (pinfo->name);
	g_free (pinfo->description);
	if (pinfo->free_func != NULL) {
		pinfo->free_func (pinfo);
	}
	g_free (pinfo);
}

/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_dir (const gchar *dir_name, ErrorInfo **ret_error)
{
	GList *plugin_info_list = NULL;
	gchar *file_name;
	xmlDocPtr doc;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	file_name = g_concat_dir_and_file (dir_name, PLUGIN_INFO_FILE_NAME);
	doc = xmlParseFile (file_name);
	if (doc != NULL && doc->root != NULL
	    && xmlStrcmp (doc->root->name, "gnumeric_plugin_group") == 0) {
		gint i;
		xmlNodePtr node;
		GList *plugin_error_list = NULL;

		for (i = 0, node = doc->root->childs; node != NULL; i++, node = node->next) {
			if (xmlStrcmp (node->name, "plugin") == 0) {
				PluginInfo *pinfo;
				ErrorInfo *error;

				pinfo = plugin_info_read (dir_name, node, &error);
				if (pinfo != NULL) {
					g_assert (error == NULL);
					plugin_info_list = g_list_prepend (plugin_info_list, pinfo);
				} else {
					ErrorInfo *new_error;

					new_error = error_info_new_printf (
					            _("Can't read plugin #%d info."),
					            i + 1);
					error_info_add_details (new_error, error);
					plugin_error_list = g_list_prepend (plugin_error_list, new_error);
				}
			}
		}
		if (plugin_error_list != NULL) {
			plugin_error_list = g_list_reverse (plugin_error_list);
			*ret_error = error_info_new_printf (
			             _("Errors occured while reading plugin informations from file \"%s\"."),
			             file_name);
			error_info_add_details_list (*ret_error, plugin_error_list);
		}
	} else {
		if (!g_file_exists (file_name)) {
			/* no error */
		} else if (access (file_name, R_OK) != 0) {
			*ret_error = error_info_new_printf (
			             _("Can't read plugin group info file (\"%s\")."),
			             file_name);
		} else {
			*ret_error = error_info_new_printf (
			             _("File \"%s\" is not valid plugin group info file."),
			             file_name);
		}
	}
	g_free (file_name);

	return g_list_reverse (plugin_info_list);
}

void
plugin_info_print (PluginInfo *pinfo)
{
	g_return_if_fail (pinfo != NULL);

	printf ("Directory:   \"%s\"\n", pinfo->dir_name);
	printf ("ID:          \"%s\"\n", pinfo->id);
	printf ("Name:        \"%s\"\n", pinfo->name);
	printf ("Description: \"%s\"\n", pinfo->description);
	printf ("Activation:   %s\n", pinfo->activation == PLUGIN_ACTIVATION_MODULE ? "module" : "invalid");
	if (pinfo->print_info_func != NULL) {
		pinfo->print_info_func (pinfo);
	} 
}


/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_subdirs_of_dir (const gchar *dir_name, ErrorInfo **ret_error)
{
	GList *plugin_info_list = NULL;
	DIR *dir;
	struct dirent *entry;
	GList *error_list = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	dir = opendir (dir_name);
	if (dir == NULL) {
		return NULL;
	}

	while ((entry = readdir (dir)) != NULL) {
		gchar *full_entry_name;

		if (strcmp (entry->d_name, ".") == 0 || strcmp (entry->d_name, "..") == 0) {
			continue;
		}
		full_entry_name = g_concat_dir_and_file (dir_name, entry->d_name);
		if (g_file_test (full_entry_name, G_FILE_TEST_ISDIR)) {
			ErrorInfo *error;
			GList *subdir_plugin_info_list;

			subdir_plugin_info_list = plugin_info_list_read_for_dir (full_entry_name, &error);
			plugin_info_list = g_list_concat (plugin_info_list, subdir_plugin_info_list);
			if (error != NULL) {
				error_list = g_list_prepend (error_list, error);
			}
		}
		g_free (full_entry_name);
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}

	closedir (dir);

	return plugin_info_list;
}

/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_subdirs_of_dir_list (GList *dir_list, ErrorInfo **ret_error)
{
	GList *plugin_info_list = NULL;
	GList *dir_iterator;
	GList *error_list = NULL;

	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	for (dir_iterator = dir_list; dir_iterator != NULL; dir_iterator = dir_iterator->next) {
		gchar *dir_name;
		ErrorInfo *error;
		GList *dir_plugin_info_list;

		dir_name = (gchar *) dir_iterator->data;
		dir_plugin_info_list = plugin_info_list_read_for_subdirs_of_dir (dir_name, &error);
		if (error != NULL) {
			error_list = g_list_prepend (error_list, error);
		}
		if (dir_plugin_info_list != NULL) {
			plugin_info_list = g_list_concat (plugin_info_list, dir_plugin_info_list);
		}
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}

	return plugin_info_list;
}

GList *
gnumeric_extra_plugin_dirs (void)
{
	static GList *extra_dirs;
	static gboolean list_ready = FALSE;

	if (!list_ready) {
		gchar *plugin_path_env;

		extra_dirs = gnumeric_config_get_string_list ("Gnumeric/Plugin/",
		                                              "ExtraPluginsDir");
		plugin_path_env = g_getenv ("GNUMERIC_PLUGIN_PATH");
		if (plugin_path_env != NULL) {
			extra_dirs = g_list_concat (extra_dirs,
			                            g_strsplit_to_list (plugin_path_env, ":"));
		}
		list_ready = TRUE;
	}

	return g_string_list_copy (extra_dirs);
}

/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_all_dirs (ErrorInfo **ret_error)
{
	GList *dir_list;
	GList *plugin_info_list;
	ErrorInfo *error;

	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	dir_list = g_create_list (gnumeric_sys_plugin_dir (),
	                          gnumeric_usr_plugin_dir (),
	                          NULL);
	dir_list = g_list_concat (dir_list, gnumeric_extra_plugin_dirs ());
	plugin_info_list = plugin_info_list_read_for_subdirs_of_dir_list (dir_list, &error);
	e_free_string_list (dir_list);
	*ret_error = error;

	return plugin_info_list;
}

/* 
 * May activate some plugins and return error info for the rest.
 */
void
plugin_db_activate_plugin_list (GList *plugins, ErrorInfo **ret_error)
{
	GList *l;
	GList *error_list = NULL;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (plugins == NULL) {
		ErrorInfo *error;

		plugins = plugin_db_get_available_plugin_info_list (&error);
		error_info_free (error);
	}
	for (l = plugins; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (!pinfo->is_active) {
			ErrorInfo *error;

			activate_plugin ((PluginInfo *) l->data, &error);
			if (error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("Couldn't activate plugin \"%s\" (ID: %s)."),
				            pinfo->name,
				            pinfo->id);
				error_info_add_details (new_error, error);
				error_list = g_list_prepend (error_list, new_error);
			}
		}
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

/* 
 * May deactivate some plugins and return error info for the rest.
 */
void
plugin_db_deactivate_plugin_list (GList *plugins, ErrorInfo **ret_error)
{
	GList *l;
	GList *error_list = NULL;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (plugins == NULL) {
		ErrorInfo *error;

		plugins = plugin_db_get_available_plugin_info_list (&error);
		error_info_free (error);
	}
	for (l = plugins; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (pinfo->is_active && can_deactivate_plugin (pinfo)) {
			ErrorInfo *error;

			deactivate_plugin (pinfo, &error);
			if (error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("Couldn't deactivate plugin \"%s\" (ID: %s)."),
				            pinfo->name,
				            pinfo->id);
				error_info_add_details (new_error, error);
				error_list = g_list_prepend (error_list, new_error);
			}
		} else {
			if (pinfo->is_active) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("Couldn't deactivate plugin \"%s\" (ID: %s): it's still in use."),
				            pinfo->name,
				            pinfo->id);
				error_list = g_list_prepend (error_list, new_error);
			}		
		}
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

GList *
plugin_db_get_known_plugin_id_list (void)
{
	if (!known_plugin_id_list_is_ready) {
		if (known_plugin_id_list != NULL) {
			e_free_string_list (known_plugin_id_list);
		}
		known_plugin_id_list = gnumeric_config_get_string_list (
		                       "Gnumeric/Plugin/KnownPlugins",
		                       NULL);
		known_plugin_id_list_is_ready = TRUE; 
	}

	return known_plugin_id_list;
}

void
plugin_db_extend_known_plugin_id_list (GList *extra_ids)
{
	GList *old_ids;

	old_ids = plugin_db_get_known_plugin_id_list ();
	known_plugin_id_list = g_list_concat (old_ids, extra_ids);
	gnumeric_config_set_string_list (known_plugin_id_list,
	                                 "Gnumeric/Plugin/KnownPlugins",
	                                 NULL);
}

gboolean
plugin_db_is_known_plugin (const gchar *plugin_id)
{
	GList *l;

	g_return_val_if_fail (plugin_id != NULL, FALSE);

	for (l = plugin_db_get_known_plugin_id_list(); l != NULL; l = l->next) {
		if (strcmp ((gchar *) l->data, plugin_id) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

GList *
plugin_db_get_available_plugin_info_list (ErrorInfo **ret_error)
{
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	if (!available_plugin_info_list_is_ready) {
		ErrorInfo *error;

		if (available_plugin_info_list != NULL) {
			GList *l;

			for (l = available_plugin_info_list; l != NULL; l = l->next) {
				plugin_info_free ((PluginInfo *) l->data);
			}
		}
		available_plugin_info_list = plugin_info_list_read_for_all_dirs (&error);
		if (error != NULL) {
			*ret_error = error;
		}
		available_plugin_info_list_is_ready	= TRUE;
	}

	return available_plugin_info_list;
}

PluginInfo *
plugin_db_get_plugin_info_by_plugin_id (const gchar *plugin_id)
{
	ErrorInfo *error;
	GList *plugin_info_list, *l;

	g_return_val_if_fail (plugin_id != NULL, NULL);

	plugin_info_list = plugin_db_get_available_plugin_info_list (&error);
	error_info_free (error);
	for (l = plugin_info_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (strcmp (pinfo->id, plugin_id) == 0) {
			return pinfo;
		}
	}

	return NULL;
}

GList *
plugin_db_get_saved_active_plugin_id_list (void)
{
	if (!saved_active_plugin_id_list_is_ready) {
		if (saved_active_plugin_id_list != NULL) {
			e_free_string_list (saved_active_plugin_id_list);
		}
		saved_active_plugin_id_list = gnumeric_config_get_string_list (
		                              "Gnumeric/Plugin/ActivePlugins",
		                              NULL);
		saved_active_plugin_id_list_is_ready = TRUE;
	}

	return saved_active_plugin_id_list;
}

void
plugin_db_update_saved_active_plugin_id_list (void)
{
	GList *plugin_list, *l;
	ErrorInfo *error;

	if (saved_active_plugin_id_list != NULL) {
		e_free_string_list (saved_active_plugin_id_list);
	}
	saved_active_plugin_id_list = NULL;
	plugin_list = plugin_db_get_available_plugin_info_list (&error);
	error_info_free (error);
	for (l = plugin_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (pinfo->is_active) {
			saved_active_plugin_id_list = g_list_prepend (saved_active_plugin_id_list,
			                                              g_strdup (pinfo->id));
		}
	}
	saved_active_plugin_id_list = g_list_reverse (saved_active_plugin_id_list);
	gnumeric_config_set_string_list (saved_active_plugin_id_list,
	                                 "Gnumeric/Plugin/ActivePlugins",
	                                 NULL);
	saved_active_plugin_id_list_is_ready = TRUE;
}

void
plugin_db_extend_saved_active_plugin_id_list (GList *extra_ids)
{
	GList *old_ids;
 
	old_ids = plugin_db_get_saved_active_plugin_id_list ();
	saved_active_plugin_id_list = g_list_concat (old_ids, extra_ids);
	gnumeric_config_set_string_list (saved_active_plugin_id_list,
	                                 "Gnumeric/Plugin/ActivePlugins",
	                                 NULL);
}

gboolean
plugin_db_is_saved_active_plugin (const gchar *plugin_id)
{
	GList *l;

	g_return_val_if_fail (plugin_id != NULL, FALSE);

	for (l = plugin_db_get_saved_active_plugin_id_list (); l != NULL; l = l->next) {
		if (strcmp ((gchar *) l->data, plugin_id) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

/* 
 * May return errors for some plugins.
 */
void
plugin_db_init (ErrorInfo **ret_error)
{
	ErrorInfo *error;
	GList *known_plugin_ids, *new_plugin_ids;
	GList *plugin_list, *l;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	plugin_list = plugin_db_get_available_plugin_info_list (&error);
	if (error != NULL) {
		*ret_error = error;
	}
	known_plugin_ids = plugin_db_get_known_plugin_id_list ();
	new_plugin_ids = NULL;
	for (l = plugin_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (g_list_find_custom (known_plugin_ids, pinfo->id, &g_str_compare) == NULL) {
			new_plugin_ids = g_list_prepend (new_plugin_ids, g_strdup (pinfo->id));
		}
	}

	if (gnome_config_get_bool_with_default ("Gnumeric/Plugin/ActivateNewByDefault", FALSE)) {
		plugin_db_extend_saved_active_plugin_id_list (g_string_list_copy (new_plugin_ids));
	}

	plugin_db_extend_known_plugin_id_list (new_plugin_ids);
}

/* 
 * May return errors for some plugins.
 */
void
plugin_db_shutdown (ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	plugin_db_deactivate_plugin_list (NULL, &error);
	*ret_error = error;
}

void
plugin_db_activate_saved_active_plugins (ErrorInfo **ret_error)
{
	GList *l;
	GList *error_list = NULL;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	for (l = plugin_db_get_saved_active_plugin_id_list (); l != NULL; l = l->next) {
		PluginInfo *pinfo;
		ErrorInfo *error;

		pinfo = plugin_db_get_plugin_info_by_plugin_id ((gchar *) l->data);
		if (pinfo != NULL) {
			activate_plugin (pinfo, &error);
			if (error != NULL) {
				error_list = g_list_prepend (error_list, error);
			}
		}
	}
	plugin_db_update_saved_active_plugin_id_list ();
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

void
plugins_init (CommandContext *context)
{
	GList *error_list = NULL;
	ErrorInfo *error;

	plugin_db_init (&error);
	if (error != NULL) {
		error_list = g_list_prepend (error_list, error_info_new_str_with_details (
		                             _("Errors while reading info about available plugins."),
		                             error));
	}
	plugin_db_activate_saved_active_plugins (&error);
	if (error != NULL) {
		error_list = g_list_prepend (error_list, error_info_new_str_with_details (
		                             _("Errors while activating plugins."),
		                             error));
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		error = error_info_new_str_with_details_list (
		        _("Errors while initializing plugin system."),
		        error_list);
		gnumeric_error_info_dialog_show (WORKBOOK_CONTROL_GUI (context), error);
		error_info_free (error);
	}
}

void
plugins_shutdown (void)
{
	ErrorInfo *ignored_error;

	plugin_db_update_saved_active_plugin_id_list ();
	plugin_db_shutdown (&ignored_error);
	error_info_free (ignored_error);
	gnome_config_sync ();
}

/*
 * Module plugin
 */

static void
module_plugin_print_info (PluginInfo *pinfo)
{
	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (pinfo->activation == PLUGIN_ACTIVATION_MODULE);

	printf ("Handle:      %p\n", pinfo->t.module.handle);
	printf ("File name:   \"%s\"\n", pinfo->t.module.file_name);
}

static gint
module_plugin_info_get_extra_info_list (PluginInfo *pinfo, GList **ret_keys_list, GList **ret_values_list)
{
	GList *keys_list = NULL, *values_list = NULL;
	gint n_items = 0;

	keys_list = g_list_prepend (keys_list, g_strdup (_("Module file name")));
	values_list = g_list_prepend (values_list, g_strdup (pinfo->t.module.file_name));
	n_items++;

	*ret_keys_list = g_list_reverse (keys_list);
	*ret_values_list = g_list_reverse (values_list);

	return n_items;
}

static void
module_plugin_free (PluginInfo *pinfo)
{
	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (pinfo->activation == PLUGIN_ACTIVATION_MODULE);

	g_module_close (pinfo->t.module.handle);
	g_free (pinfo->t.module.file_name);
}

static void
module_plugin_info_read (PluginInfo *pinfo, xmlNodePtr tree, ErrorInfo **ret_error)
{
	xmlNodePtr activation_node;
	xmlChar *x_module_file;

	g_assert (pinfo != NULL);
	g_assert (pinfo->activation == PLUGIN_ACTIVATION_MODULE);
	g_assert (tree != NULL);
	g_assert (xmlStrcmp (tree->name, "plugin") == 0);
	g_assert (ret_error != NULL);

	*ret_error = NULL;
	activation_node = xml_search_child (tree, "activation");
	x_module_file = xmlGetProp (activation_node, "module_file");
	if (x_module_file != NULL) {
		gchar *full_module_file_name;

		full_module_file_name = g_concat_dir_and_file (pinfo->dir_name, x_module_file);
		if (g_file_exists (full_module_file_name)) {
			pinfo->t.module.handle = NULL;
			pinfo->t.module.file_name = g_strdup (x_module_file);
			pinfo->free_func = module_plugin_free;
			pinfo->activate_func = module_plugin_activate;
			pinfo->deactivate_func = module_plugin_deactivate;
			pinfo->can_deactivate_func = module_plugin_can_deactivate;
			pinfo->get_extra_info_list_func = module_plugin_info_get_extra_info_list;
			pinfo->print_info_func = module_plugin_print_info;
		} else {
			*ret_error = error_info_new_printf (
			             _("Module file \"%s\" doesn't exist."),
			             x_module_file);
		}
		g_free (full_module_file_name);
	} else {
		*ret_error = error_info_new_str (
		             _("Module file name not given."));
	}
	xmlFree (x_module_file);
}

typedef gboolean (*ModulePluginInitFunction) (PluginInfo *, ErrorInfo **);

static void
module_plugin_activate (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (pinfo->activation == PLUGIN_ACTIVATION_MODULE);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (pinfo->is_active) {
		return;
	}
	if (g_module_supported ()) {
		gchar *full_module_file_name;
		GModule *handle;
		gpointer init_plugin_func, cleanup_plugin_func, can_deactivate_plugin_func;
		gpointer plugin_version;

		full_module_file_name = g_concat_dir_and_file (pinfo->dir_name, pinfo->t.module.file_name);
		handle = g_module_open (full_module_file_name, 0);
		if (handle != NULL) {
			g_module_symbol (handle, "init_plugin", &init_plugin_func);
			g_module_symbol (handle, "cleanup_plugin", &cleanup_plugin_func);
			g_module_symbol (handle, "can_deactivate_plugin", &can_deactivate_plugin_func);
			g_module_symbol (handle, "gnumeric_plugin_version", &plugin_version);
		}
		if (handle != NULL && init_plugin_func != NULL &&
		    cleanup_plugin_func != NULL && can_deactivate_plugin_func != NULL &&
		    plugin_version != NULL &&
		    strcmp ((gchar *) plugin_version, GNUMERIC_VERSION) == 0) {
			ErrorInfo *init_error = NULL;

			pinfo->t.module.handle = handle;
			pinfo->t.module.cleanup_plugin_func = cleanup_plugin_func;
			pinfo->t.module.can_deactivate_plugin_func = can_deactivate_plugin_func;
			if (((ModulePluginInitFunction) init_plugin_func) (pinfo, &init_error)) {
				if (init_error != NULL) {
					g_warning ("Function init_plugin succeeded, but returned error.");
					error_info_free (init_error);
				}
				pinfo->is_active = TRUE;
			} else {
				(void) g_module_close (pinfo->t.module.handle);
				pinfo->t.module.handle = NULL;
				pinfo->t.module.cleanup_plugin_func = NULL;
				pinfo->t.module.can_deactivate_plugin_func = NULL;
				*ret_error = error_info_new_printf (
				             _("Function init_plugin inside \"%s\" returned error."),
				             full_module_file_name);
				error_info_add_details (*ret_error, init_error);
			}
		} else {
			if (handle == NULL) {
				*ret_error = error_info_new_printf (
				             _("Unable to open module file \"%s\": %s"),
				             full_module_file_name, g_module_error());
			} else if (plugin_version != NULL &&
			           strcmp ((gchar *) plugin_version, GNUMERIC_VERSION) != 0) {
				*ret_error = error_info_new_printf (
				             _("Plugin version \"%s\" is different from application \"%s\"."),
				             (gchar *) plugin_version, GNUMERIC_VERSION);
			} else {
				*ret_error = error_info_new_printf (
				             _("Module file \"%s\" has invalid format."),
				             full_module_file_name);
				if (init_plugin_func == NULL) {
					error_info_add_details (*ret_error,
					                        error_info_new_str (
					                        _("File doesn't contain \"init_plugin\" function.")));
				}
				if (cleanup_plugin_func == NULL) {
					error_info_add_details (*ret_error,
					                        error_info_new_str (
					                        _("File doesn't contain \"cleanup_plugin\" function.")));
				}
				if (can_deactivate_plugin_func == NULL) {
					error_info_add_details (*ret_error,
					                        error_info_new_str (
					                        _("File doesn't contain \"can_deactivate_plugin\" function.")));
				}
				if (plugin_version == NULL) {
					error_info_add_details (*ret_error,
					                        error_info_new_str (
					                        _("File doesn't contain version information (\"gnumeric_plugin_version\" symbol).")));
				}
			}
		}
		g_free (full_module_file_name);
	} else {
		*ret_error = error_info_new_str (
		             _("Dynamic module loading is not supported in this system."));
	}
}

static void
module_plugin_deactivate (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (pinfo->activation == PLUGIN_ACTIVATION_MODULE);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (!pinfo->is_active || !pinfo->t.module.can_deactivate_plugin_func (pinfo)) {
		return;
	}
	if (!pinfo->t.module.cleanup_plugin_func (pinfo) || !g_module_close (pinfo->t.module.handle)) {
		gchar *full_module_file_name;

		full_module_file_name = g_concat_dir_and_file (pinfo->dir_name, pinfo->t.module.file_name);
		*ret_error = error_info_new_printf (
		             _("Unable to close module file \"%s\": %s"),
		             full_module_file_name, g_module_error());
		g_free (full_module_file_name);
		/* FIXME - plugin's state is unknown, we'll mark it "inactive" for now */
	}
	pinfo->t.module.handle = NULL;
	pinfo->t.module.cleanup_plugin_func = NULL;
	pinfo->t.module.can_deactivate_plugin_func = NULL;
	pinfo->is_active = FALSE;
}

static gboolean
module_plugin_can_deactivate (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, FALSE);
	g_return_val_if_fail (pinfo->activation == PLUGIN_ACTIVATION_MODULE, FALSE);

	return pinfo->t.module.can_deactivate_plugin_func (pinfo);
}
