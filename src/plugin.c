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

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "plugin.h"

#include "gui-util.h"
#include "gutils.h"
#include "command-context.h"
#include "file.h"
#include "workbook.h"
#include "workbook-view.h"
#include "error-info.h"
#include "plugin-loader.h"
#include "plugin-loader-module.h"
#include "plugin-service.h"
#include "gnumeric-gconf.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <locale.h>
#include <gmodule.h>
#include <application.h>
#include <libgnome/libgnome.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>
#include <gal/util/e-util.h>
#include <gal/util/e-xml-utils.h>

#define PLUGIN_INFO_FILE_NAME          "plugin.xml"
#define PLUGIN_DEBUG 10

typedef struct _PluginLoaderStaticInfo PluginLoaderStaticInfo;
struct _PluginLoaderStaticInfo {
	gchar *loader_type_str;
	GSList *attr_names, *attr_values;
};

typedef struct _PluginDependency PluginDependency;
struct _PluginDependency {
	gchar *plugin_id;
	PluginInfo *pinfo;             /* don't use directly */
	PluginDependencyType dep_type;
};

struct _PluginInfo {
	gboolean has_full_info;
	gchar   *dir_name;
	gchar   *id;
	gchar   *name;
	gchar   *description;

	gboolean is_active;
	gint n_deps[DEPENDENCY_LAST];
	PluginDependency **dependencies_v;
	PluginLoaderStaticInfo *loader_static_info;
	GnumericPluginLoader *loader;
	PluginService **services_v;
	PluginServicesData *services_data;
};

typedef struct _PluginLoaderTypeInfo PluginLoaderTypeInfo;
struct _PluginLoaderTypeInfo {
	gchar  *id_str;
	gboolean has_type;
	GType loader_type;
	PluginLoaderGetTypeCallback get_type_callback;
	gpointer get_type_callback_data;
};

static GHashTable *plugins_marked_for_deactivation_hash = NULL;
static GSList *known_plugin_id_list = NULL;
static GSList *available_plugin_info_list = NULL;
static GSList *saved_active_plugin_id_list = NULL;

static GSList *registered_loader_types = NULL;


static void plugin_get_loader_if_needed (PluginInfo *pinfo, ErrorInfo **ret_error);
static void plugin_info_read (PluginInfo *pinfo, const gchar *dir_name, ErrorInfo **ret_error);
static PluginInfo *plugin_info_read_for_dir (const gchar *dir_name, ErrorInfo **ret_error);
static GSList *plugin_info_list_read_for_subdirs_of_dir (const gchar *dir_name, ErrorInfo **ret_error);
static GSList *plugin_info_list_read_for_subdirs_of_dir_list (GSList *dir_list, ErrorInfo **ret_error);
static GSList *plugin_info_list_read_for_all_dirs (ErrorInfo **ret_error);


/*
 * Plugin cache
 */

typedef struct {
	gchar *dir_name;
	gchar *file_state;
	gchar *plugin_id;
} PluginFileState;

static gboolean plugin_file_state_list_changed;
static GSList *plugin_file_state_list;
static GHashTable *plugin_file_state_dir_hash;

static gchar *
get_file_state_as_string (const gchar *file_name)
{
	struct stat st;
	gchar *str;

	if (stat (file_name, &st) == -1) {
		return NULL;
	}

	str = g_strdup_printf ("%ld:%ld:%ld:%ld", (glong) st.st_dev,
	                       (glong) st.st_ino, (glong) st.st_size,
	                       (glong) st.st_mtime);
	return str;
}

static gchar *
plugin_file_state_as_string (PluginFileState *state)
{
	return g_strdup_printf ("%s|%s|%s", state->plugin_id, state->file_state,
	                        state->dir_name);
}

static PluginFileState *
plugin_file_state_from_string (const gchar *str)
{
	PluginFileState *state;
	gchar **strv;

	strv = g_strsplit (str, "|", 3);
	if (strv[0] == NULL || strv[1] == NULL || strv[2] == NULL) {
		g_strfreev (strv);
		return NULL;
	}
	state = g_new (PluginFileState, 1);
	state->plugin_id = strv[0];
	state->file_state = strv[1];
	state->dir_name = strv[2];
	g_free (strv);

	return state;
}

static void
plugin_file_state_free (PluginFileState *state)
{
	g_free (state->dir_name);
	g_free (state->file_state);
	g_free (state->plugin_id);
	g_free (state);
}

static void
plugin_cache_init (void)
{
	GSList *state_str_list, *l;

	plugin_file_state_list = NULL;
	plugin_file_state_dir_hash = g_hash_table_new (&g_str_hash, &g_str_equal);

	state_str_list = gnm_gconf_get_plugin_file_states ();

	for (l = state_str_list; l != NULL; l = l->next) {
		PluginFileState *state;

		state = plugin_file_state_from_string ((gchar *) l->data);
		if (state != NULL) {
			plugin_file_state_list = g_slist_prepend (plugin_file_state_list, state);
			g_hash_table_insert (plugin_file_state_dir_hash, state->dir_name, state);
		}
	}
	g_slist_free_custom (state_str_list, g_free);

	plugin_file_state_list_changed = FALSE;
}

static void
plugin_cache_shutdown (void)
{
	if (plugin_file_state_list_changed) {
		GSList *state_str_list = NULL, *l;

		for (l = plugin_file_state_list; l != NULL; l = l->next) {
			state_str_list = g_slist_prepend (state_str_list,
			                                 plugin_file_state_as_string (
			                                 (PluginFileState *) l->data));
		}

                gnm_gconf_set_plugin_file_states (state_str_list);
		g_slist_free_custom (state_str_list, g_free);
		plugin_message (0, "Cache changed\n");
	}
	g_hash_table_destroy (plugin_file_state_dir_hash);
	g_slist_free_custom (plugin_file_state_list, (GFreeFunc) plugin_file_state_free);
}

static gboolean
plugin_info_read_full_info_if_needed_error_info (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	ErrorInfo *read_error;
	gchar *old_id, *old_dir_name;

	*ret_error = NULL;
	if (pinfo->has_full_info) {
		return TRUE;
	}

	old_id = pinfo->id;
	old_dir_name = pinfo->dir_name;
	plugin_info_read (pinfo, old_dir_name, &read_error);
	if (read_error == NULL && strcmp (pinfo->id, old_id) == 0) {
		plugin_message (1, "Read plugin (%s) information from XML file.\n",
		                pinfo->id);
		pinfo->has_full_info = TRUE;
	} else {
		plugin_message (1, "Can't read plugin (%s) information from XML file!\n",
		                old_id);
		if (read_error == NULL) {
			read_error = error_info_new_printf (
			             _("File contain plugin info with invalid id (%s), expected %s."),
			             pinfo->id, old_id);
		}
		*ret_error = error_info_new_str_with_details (
		             _("Couldn't read plugin info from file."),
		             read_error);
	}
	g_free (old_id);
	g_free (old_dir_name);

	return *ret_error == NULL;
}

static gboolean
plugin_info_read_full_info_if_needed (PluginInfo *pinfo)
{
	ErrorInfo *error;

	if (plugin_info_read_full_info_if_needed_error_info (pinfo, &error)) {
		return TRUE;
	} else {
		g_warning ("plugin_info_read_full_info_if_needed: couldn't read plugin info from file.");
		error_info_print (error);
		error_info_free (error);
		return FALSE;
	}
}

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

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return g_strdup (_("Unknown name"));
	}
	return g_strdup (pinfo->name);
}

gchar *
plugin_info_get_description (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return NULL;
	}
	return g_strdup (pinfo->description);
}

gchar *
plugin_info_get_config_prefix (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup_printf ("Gnumeric/Plugins-%s", pinfo->id);
}

gint
plugin_info_get_extra_info_list (PluginInfo *pinfo, GSList **ret_keys_list, GSList **ret_values_list)
{
	ErrorInfo *ignored_error;

	*ret_keys_list = NULL;
	*ret_values_list = NULL;

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return 0;
	}
	plugin_get_loader_if_needed (pinfo, &ignored_error);
	if (ignored_error == NULL) {
		return gnumeric_plugin_loader_get_extra_info_list (pinfo->loader, ret_keys_list, ret_values_list);
	} else {
		error_info_free (ignored_error);
		return 0;
	}
}

gboolean
plugin_info_is_active (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, FALSE);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return FALSE;
	}
	return pinfo->is_active;
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

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return _("Unknown name");
	}
	return pinfo->name;
}

const gchar *
plugin_info_peek_description (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return NULL;
	}
	return pinfo->description;
}

const gchar *
plugin_info_peek_loader_type_str (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return NULL;
	}
	return pinfo->loader_static_info->loader_type_str;
}

/*
 * If loader_type_str == NULL, it returns TRUE for plugin providing _any_ loader.
 */
gboolean
plugin_info_provides_loader_by_type_str (PluginInfo *pinfo, const gchar *loader_type_str)
{
	PluginService **service_ptr;

	g_return_val_if_fail (pinfo != NULL, FALSE);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return FALSE;
	}
	for (service_ptr = pinfo->services_v; *service_ptr != NULL; service_ptr++) {
		if ((*service_ptr)->service_type == PLUGIN_SERVICE_PLUGIN_LOADER &&
		    (loader_type_str == NULL ||
		     strcmp ((*service_ptr)->t.plugin_loader.loader_id, loader_type_str) == 0)) {
			return TRUE;
		}
	}

	return FALSE;
}

gboolean
plugin_info_is_loaded (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, FALSE);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return FALSE;
	}
	return pinfo->loader != NULL && gnumeric_plugin_loader_is_loaded (pinfo->loader);
}

PluginServicesData *
plugin_info_peek_services_data (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return NULL;
	}
	return pinfo->services_data;
}

struct _GnumericPluginLoader *
plugin_info_get_loader (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return NULL;
	}
	return pinfo->loader;
}

/*
 *
 */

void
plugin_loader_register_type (const gchar *id_str, GType loader_type)
{
	PluginLoaderTypeInfo *loader_type_info;

	g_return_if_fail (id_str != NULL);

	loader_type_info = g_new (PluginLoaderTypeInfo, 1);
	loader_type_info->id_str = g_strdup (id_str);
	loader_type_info->has_type = TRUE;
	loader_type_info->loader_type = loader_type;
	loader_type_info->get_type_callback = NULL;
	loader_type_info->get_type_callback_data = NULL;
	registered_loader_types = g_slist_append (registered_loader_types, loader_type_info);
}

void
plugin_loader_register_id_only (const gchar *id_str, PluginLoaderGetTypeCallback callback,
                                gpointer callback_data)
{
	PluginLoaderTypeInfo *loader_type_info;

	g_return_if_fail (id_str != NULL);
	g_return_if_fail (callback != NULL);

	loader_type_info = g_new (PluginLoaderTypeInfo, 1);
	loader_type_info->id_str = g_strdup (id_str);
	loader_type_info->has_type = FALSE;
	loader_type_info->loader_type = G_TYPE_NONE;
	loader_type_info->get_type_callback = callback;
	loader_type_info->get_type_callback_data = callback_data;
	registered_loader_types = g_slist_append (registered_loader_types, loader_type_info);
}

GType
plugin_loader_get_type_by_id (const gchar *id_str, ErrorInfo **ret_error)
{
	GSList *l;

	g_return_val_if_fail (id_str != NULL, G_TYPE_NONE);
	g_return_val_if_fail (ret_error != NULL, G_TYPE_NONE);

	*ret_error = NULL;
	for (l = registered_loader_types; l != NULL; l = l->next) {
		PluginLoaderTypeInfo *loader_type_info = l->data;

		if (strcmp (loader_type_info->id_str, id_str) == 0) {
			if (loader_type_info->has_type) {
				return loader_type_info->loader_type;
			} else {
				ErrorInfo *error;
				GType loader_type;

				loader_type = loader_type_info->get_type_callback (
				              loader_type_info->get_type_callback_data,
				              &error);
				if (error == NULL) {
					loader_type_info->has_type = TRUE;
					loader_type_info->loader_type = loader_type;
					return loader_type_info->loader_type;
				} else {
					*ret_error = error_info_new_printf (
					             _("Error while preparing loader \"%s\"."),
					             id_str);
					error_info_add_details (*ret_error, error);
					return G_TYPE_NONE;
				}
			}
		}
	}

	*ret_error = error_info_new_printf (
	             _("Unsupported loader type \"%s\"."),
	             id_str);
	return G_TYPE_NONE;
}

gboolean
plugin_loader_is_available_by_id (const gchar *id_str)
{
	GSList *l;

	g_return_val_if_fail (id_str != NULL, FALSE);

	for (l = registered_loader_types; l != NULL; l = l->next) {
		PluginLoaderTypeInfo const *loader_type_info = l->data;
		if (strcmp (loader_type_info->id_str, id_str) == 0)
			return TRUE;
	}

	return FALSE;
}

static PluginInfo *
plugin_dependency_get_plugin_info (PluginDependency *dep)
{
	g_return_val_if_fail (dep != NULL, NULL);

	if (dep->pinfo == NULL)
		dep->pinfo = plugin_db_get_plugin_info_by_plugin_id (dep->plugin_id);
	return dep->pinfo;
}

static GSList *
plugin_info_read_dependency_list (xmlNode *tree)
{
	GSList *dependency_list = NULL;
	xmlNode *node;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "dependencies") == 0, NULL);

	for (node = tree->xmlChildrenNode; node != NULL; node = node->next) {
		if (strcmp (node->name, "dep_plugin") == 0) {
			gchar *plugin_id;

			plugin_id = e_xml_get_string_prop_by_name (node, (xmlChar *)"id");
			if (plugin_id != NULL) {
				PluginDependency *dep;

				dep = g_new (PluginDependency, 1);
				dep->plugin_id = plugin_id;
				dep->pinfo = NULL;
				dep->dep_type = 0;
				if (e_xml_get_bool_prop_by_name_with_default (node, (xmlChar *)"activate", TRUE)) {
					dep->dep_type |= (2 ^ DEPENDENCY_ACTIVATE);
				}
				if (e_xml_get_bool_prop_by_name_with_default (node, (xmlChar *)"load", TRUE)) {
					dep->dep_type |= (2 ^ DEPENDENCY_LOAD);
				}
				dependency_list = g_slist_prepend (dependency_list, dep);
			}
		}
	}

	return g_slist_reverse (dependency_list);
}

static GSList *
plugin_info_read_service_list (xmlNode *tree, ErrorInfo **ret_error)
{
	GSList *service_list = NULL;
	GSList *error_list = NULL;
	xmlNode *node;
	gint i;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "services") == 0, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	for (i = 0, node = tree->xmlChildrenNode; node != NULL; i++, node = node->next) {
		if (strcmp (node->name, "service") == 0) {
			PluginService *service;
			ErrorInfo *service_error;

			service = plugin_service_read (node, &service_error);

			if (service != NULL) {
				g_assert (service_error == NULL);
				service_list = g_slist_prepend (service_list, service);
			} else {
				ErrorInfo *error;

				error = error_info_new_printf (
				        _("Error while reading service #%d info."),
				        i);
				error_info_add_details (error, service_error);
				error_list = g_slist_prepend (error_list, error);
			}
		}
	}
	if (error_list != NULL) {
		error_list = g_slist_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
		g_slist_free_custom (service_list, (GFreeFunc) plugin_service_free);
		return NULL;
	} else {
		return g_slist_reverse (service_list);
	}
}

static PluginLoaderStaticInfo *
plugin_info_read_loader_static_info (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginLoaderStaticInfo *loader_info = NULL;
	gchar *loader_type_str;
	xmlNode *node;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "loader") == 0, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	loader_type_str = e_xml_get_string_prop_by_name (tree, (xmlChar *)"type");
	if (loader_type_str != NULL) {
		loader_info = g_new (PluginLoaderStaticInfo, 1);
		loader_info->loader_type_str = loader_type_str;
		loader_info->attr_names = NULL;
		loader_info->attr_values = NULL;
		for (node = tree->xmlChildrenNode; node != NULL; node = node->next) {
			if (strcmp (node->name, "attribute") == 0) {
				gchar *name;

				name = e_xml_get_string_prop_by_name (node, (xmlChar *)"name");
				if (name != NULL) {
					gchar *value;

					value = e_xml_get_string_prop_by_name (node, (xmlChar *)"value");
					loader_info->attr_names = g_slist_prepend (loader_info->attr_names, name);
					loader_info->attr_values = g_slist_prepend (loader_info->attr_values, value);
				}
			}
		}
		loader_info->attr_names = g_slist_reverse (loader_info->attr_names);
		loader_info->attr_values = g_slist_reverse (loader_info->attr_values);
	} else {
		*ret_error = error_info_new_str (_("Unspecified loader type."));
	}

	return loader_info;
}

static void
loader_static_info_free (PluginLoaderStaticInfo *loader_info)
{
	g_return_if_fail (loader_info != NULL);

	g_free (loader_info->loader_type_str);
	e_free_string_slist (loader_info->attr_names);
	e_free_string_slist (loader_info->attr_values);
	g_free (loader_info);
}

static void
plugin_dependency_free (PluginDependency *dep)
{
	g_return_if_fail (dep != NULL);

	g_free (dep->plugin_id);
	g_free (dep);
}

static void
plugin_info_read (PluginInfo *pinfo, const gchar *dir_name, ErrorInfo **ret_error)
{
	gchar *file_name;
	xmlDocPtr doc;
	gchar *id, *name, *description;
	xmlNode *tree, *information_node, *dependencies_node, *loader_node, *services_node;
	ErrorInfo *services_error, *loader_error;
	GSList *service_list;
	GSList *dependency_list;
	PluginLoaderStaticInfo *loader_static_info;

	*ret_error = NULL;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (dir_name != NULL);
	g_return_if_fail (ret_error != NULL);

	file_name = g_concat_dir_and_file (dir_name, PLUGIN_INFO_FILE_NAME);
	doc = xmlParseFile (file_name);
	if (doc == NULL || doc->xmlRootNode == NULL || strcmp (doc->xmlRootNode->name, "plugin") != 0) {
		if (access (file_name, R_OK) != 0) {
			*ret_error = error_info_new_printf (
			             _("Can't read plugin info file (\"%s\")."),
			             file_name);
		} else {
			*ret_error = error_info_new_printf (
			             _("File \"%s\" is not valid plugin info file."),
			             file_name);
		}
		g_free (file_name);
		xmlFreeDoc (doc);
		return;
	}
	tree = doc->xmlRootNode;
	id = e_xml_get_string_prop_by_name (tree, (xmlChar *)"id");
	information_node = e_xml_get_child_by_name (tree, (xmlChar *)"information");
	if (information_node != NULL) {
		xmlNode *node;
		xmlChar *val;

		node = e_xml_get_child_by_name_by_lang_list (
		       information_node, "name", NULL);
		if (node != NULL) {
			val = xmlNodeGetContent (node);
			name = g_strdup ((gchar *)val);
			xmlFree (val);
		} else {
			name = NULL;
		}
		node = e_xml_get_child_by_name_by_lang_list (
		       information_node, "description", NULL);
		if (node != NULL) {
			val = xmlNodeGetContent (node);
			description = g_strdup ((gchar *)val);
			xmlFree (val);
		} else {
			description = NULL;
		}
	} else {
		name = NULL;
		description = NULL;
	}
	dependencies_node = e_xml_get_child_by_name (tree, (xmlChar *)"dependencies");
	if (dependencies_node != NULL) {
		dependency_list = plugin_info_read_dependency_list (dependencies_node);
	} else {
		dependency_list = NULL;
	}
	loader_node = e_xml_get_child_by_name (tree, (xmlChar *)"loader");
	if (loader_node != NULL) {
		loader_static_info = plugin_info_read_loader_static_info (loader_node, &loader_error);
	} else {
		loader_static_info = NULL;
		loader_error = NULL;
	}
	services_node = e_xml_get_child_by_name (tree, (xmlChar *)"services");
	if (services_node != NULL) {
		service_list = plugin_info_read_service_list (services_node, &services_error);
	} else {
		service_list = NULL;
		services_error = NULL;
	}
	if (id != NULL && name != NULL && loader_static_info != NULL && service_list != NULL) {
		PluginService **service_ptr;
		gint i;

		g_assert (loader_error == NULL);
		g_assert (services_error == NULL);

		pinfo->dir_name = g_strdup (dir_name);
		pinfo->id = id;
		pinfo->name = name;
		pinfo->description = description;
		for (i = 0; i < DEPENDENCY_LAST; i++) {
			pinfo->n_deps[i] = 0;
		}
		pinfo->is_active = FALSE;
		g_slist_to_vector (pinfo->dependencies_v, PluginDependency, dependency_list);
		g_slist_free (dependency_list);
		pinfo->loader_static_info = loader_static_info;
		pinfo->loader = NULL;
		g_slist_to_vector (pinfo->services_v, PluginService, service_list);
		g_slist_free (service_list);
		for (service_ptr = pinfo->services_v; *service_ptr != NULL; service_ptr++) {
			plugin_service_set_plugin (*service_ptr, pinfo);
		}
		pinfo->services_data = plugin_services_data_new ();
	} else {
		if (id == NULL) {
			*ret_error = error_info_new_str (_("Plugin has no id."));
			error_info_free (loader_error);
			error_info_free (services_error);
		} else {
			GSList *error_list = NULL;
			ErrorInfo *error;

			if (name == NULL) {
				error_list = g_slist_prepend (error_list, error_info_new_str (
				             _("Unknown plugin name.")));
			}
			if (loader_static_info == NULL) {
				if (loader_error != NULL) {
					error = error_info_new_printf (
					        _("Errors while reading loader info for plugin with id=\"%s\"."),
					        id);
					error_info_add_details (error, loader_error);
				} else {
					error = error_info_new_printf (
					        _("No loader defined for plugin with id=\"%s\"."),
					        id);
				}
				error_list = g_slist_prepend (error_list, error);
			}
			if (service_list == NULL) {
				if (services_error != NULL) {
					error = error_info_new_printf (
					        _("Errors while reading services for plugin with id=\"%s\"."),
					        id);
					error_info_add_details (error, services_error);
				} else {
					error = error_info_new_printf (
					        _("No services defined for plugin with id=\"%s\"."),
					        id);
				}
				error_list = g_slist_prepend (error_list, error);
			}
			g_assert (error_list != NULL);
			error_list = g_slist_reverse (error_list);
			*ret_error = error_info_new_from_error_list (error_list);
		}

		g_slist_free_custom (dependency_list, (GFreeFunc) plugin_dependency_free);
		if (loader_static_info != NULL) {
			loader_static_info_free (loader_static_info);
		}
		g_slist_free_custom (service_list, (GFreeFunc) plugin_service_free);
		g_free (id);
		g_free (name);
		g_free (description);
	}
	g_free (file_name);
	xmlFreeDoc (doc);
}

static PluginInfo *
plugin_info_new_from_xml (const gchar *dir_name, ErrorInfo **ret_error)
{
	PluginInfo *pinfo;
	ErrorInfo *error;

	*ret_error = NULL;
	pinfo = g_new (PluginInfo, 1);
	plugin_info_read (pinfo, dir_name, &error);
	if (error == NULL) {
		pinfo->has_full_info = TRUE;
	} else {
		*ret_error = error;
		g_free (pinfo);
		pinfo = NULL;
	}

	return pinfo;
}

static PluginInfo *
plugin_info_new_with_id_and_dir_name_only (const gchar *id, const gchar *dir_name)
{
	PluginInfo *pinfo;

	pinfo = g_new0 (PluginInfo, 1);
	pinfo->id = g_strdup (id);
	pinfo->dir_name = g_strdup (dir_name);
	pinfo->has_full_info = FALSE;

	return pinfo;
}

void
plugin_inc_dependants (PluginInfo *pinfo, PluginDependencyType dep_type)
{
	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (dep_type < DEPENDENCY_LAST);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return;
	}
	pinfo->n_deps[dep_type]++;
}

void
plugin_dec_dependants (PluginInfo *pinfo, PluginDependencyType dep_type)
{
	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (dep_type < DEPENDENCY_LAST && pinfo->n_deps[dep_type] > 0);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return;
	}
	pinfo->n_deps[dep_type]--;
}

void
plugin_dependencies_inc_dependants (PluginInfo *pinfo, PluginDependencyType dep_type)
{
	PluginDependency **dep_ptr;

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return;
	}
	for (dep_ptr = pinfo->dependencies_v; *dep_ptr != NULL; dep_ptr++) {
		PluginInfo *dep_pinfo;

		if (((*dep_ptr)->dep_type & (2 ^ dep_type)) != 0) {
			dep_pinfo = plugin_dependency_get_plugin_info (*dep_ptr);
			g_assert (dep_pinfo != NULL);
			plugin_inc_dependants (dep_pinfo, dep_type);
		}
	}
}

void
plugin_dependencies_dec_dependants (PluginInfo *pinfo, PluginDependencyType dep_type)
{
	PluginDependency **dep_ptr;

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return;
	}
	for (dep_ptr = pinfo->dependencies_v; *dep_ptr != NULL; dep_ptr++) {
		PluginInfo *dep_pinfo;

		if (((*dep_ptr)->dep_type & (2 ^ dep_type)) != 0) {
			dep_pinfo = plugin_dependency_get_plugin_info (*dep_ptr);
			g_assert (dep_pinfo != NULL);
			plugin_dec_dependants (dep_pinfo, dep_type);
		}
	}
}

static void
plugin_get_loader_if_needed (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	PluginLoaderStaticInfo *loader_info;
	GType loader_type;
	ErrorInfo *error;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	if (!plugin_info_read_full_info_if_needed_error_info (pinfo, ret_error)) {
		return;
	}
	*ret_error = NULL;
	if (pinfo->loader != NULL) {
		return;
	}
	loader_info = pinfo->loader_static_info;
	loader_type = plugin_loader_get_type_by_id (loader_info->loader_type_str, &error);
	if (error == NULL) {
		GnumericPluginLoader *loader;
		ErrorInfo *error;

		loader = GNUMERIC_PLUGIN_LOADER (g_object_new (loader_type, NULL));
		gnumeric_plugin_loader_set_attributes (loader, loader_info->attr_names, loader_info->attr_values, &error);
		if (error == NULL) {
			pinfo->loader = loader;
			gnumeric_plugin_loader_set_plugin (loader, pinfo);
		} else {
			gtk_object_destroy (GTK_OBJECT (loader));
			loader = NULL;
			*ret_error = error_info_new_printf (
			             _("Error initializing plugin loader (\"%s\")."),
			             loader_info->loader_type_str);
			error_info_add_details (*ret_error, error);
		}
	} else {
		*ret_error = error;
	}
}

void
activate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	GSList *error_list = NULL;
	PluginDependency **dep_ptr;
	gint i;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	if (!plugin_info_read_full_info_if_needed_error_info (pinfo, ret_error)) {
		return;
	}
	*ret_error = NULL;
	if (pinfo->is_active) {
		return;
	}
	for (dep_ptr = pinfo->dependencies_v; *dep_ptr != NULL; dep_ptr++) {
		PluginInfo *dep_pinfo;

		if (((*dep_ptr)->dep_type & (2 ^ DEPENDENCY_ACTIVATE)) == 0) {
			continue;
		}
		dep_pinfo = plugin_dependency_get_plugin_info (*dep_ptr);
		if (dep_pinfo != NULL) {
			ErrorInfo *dep_error;

			activate_plugin (dep_pinfo, &dep_error);
			if (dep_error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("We depend on plugin with id=\"%s\" which couldn't be activated."),
				            (*dep_ptr)->plugin_id);
				error_info_add_details (new_error, dep_error);
				error_list = g_slist_prepend (error_list, new_error);
			}
		} else {
			error_list = g_slist_prepend (error_list, error_info_new_printf (
			             _("We depend on plugin with id=\"%s\" which is not available."),
			             (*dep_ptr)->plugin_id));
		}
	}
	if (error_list == NULL) {
		for (i = 0; pinfo->services_v[i] != NULL; i++) {
			ErrorInfo *service_error;

			plugin_service_activate (pinfo->services_v[i], &service_error);
			if (service_error != NULL) {
				ErrorInfo *error;

				error = error_info_new_printf (
				        _("Error while activating plugin service #%d."),
				        i);
				error_info_add_details (error, service_error);
				error_list = g_slist_prepend (error_list, error);
			}
		}
	}
	if (error_list != NULL) {
		*ret_error = error_info_new_from_error_list (error_list);
		/* FIXME - deactivate activated services */
	} else {
		pinfo->is_active = TRUE;
		plugin_dependencies_inc_dependants (pinfo, DEPENDENCY_ACTIVATE);
	}
}

void
deactivate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	GSList *error_list = NULL;
	gint i;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	if (!plugin_info_read_full_info_if_needed_error_info (pinfo, ret_error)) {
		return;
	}
	*ret_error = NULL;
	if (!pinfo->is_active) {
		return;
	}
	for (i = 0; pinfo->services_v[i] != NULL; i++) {
		ErrorInfo *service_error;

		plugin_service_deactivate (pinfo->services_v[i], &service_error);
		if (service_error != NULL) {
			ErrorInfo *error;

			error = error_info_new_printf (
			        _("Error while deactivating plugin service #%d."),
			        i);
			error_info_add_details (error, service_error);
			error_list = g_slist_prepend (error_list, error);
		}
	}
	if (error_list != NULL) {
		*ret_error = error_info_new_from_error_list (error_list);
		/* FIXME - some services are still active (or broken) */
	} else {
		pinfo->is_active = FALSE;
		plugin_dependencies_dec_dependants (pinfo, DEPENDENCY_ACTIVATE);
	}
}

gboolean
plugin_can_deactivate (PluginInfo *pinfo)
{
	gint i;
	PluginService **service_ptr;

	g_return_val_if_fail (pinfo != NULL, FALSE);
	g_return_val_if_fail (pinfo->is_active, FALSE);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return FALSE;
	}
	for (i = 0; i < DEPENDENCY_LAST; i++) {
		if (pinfo->n_deps[i] > 0) {
			return FALSE;
		}
	}
	for (service_ptr = pinfo->services_v; *service_ptr != NULL; service_ptr++) {
		if (!plugin_service_can_deactivate (*service_ptr)) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
plugin_info_force_mark_inactive (PluginInfo *pinfo)
{
	g_return_if_fail (pinfo != NULL);

	if (!plugin_info_read_full_info_if_needed (pinfo)) {
		return;
	}
	pinfo->is_active = FALSE;
}

void
plugin_load_service (PluginInfo *pinfo, PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	if (!plugin_info_read_full_info_if_needed_error_info (pinfo, ret_error)) {
		return;
	}
	*ret_error = NULL;
	plugin_get_loader_if_needed (pinfo, &error);
	if (error == NULL) {
		gnumeric_plugin_loader_load_service (pinfo->loader, service, &error);
		*ret_error = error;
	} else {
		*ret_error = error_info_new_str_with_details (
		             _("Cannot load plugin loader."),
		             error);
	}
}

void
plugin_unload_service (PluginInfo *pinfo, PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (pinfo->loader != NULL);
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	if (!plugin_info_read_full_info_if_needed_error_info (pinfo, ret_error)) {
		return;
	}
	*ret_error = NULL;
	gnumeric_plugin_loader_unload_service (pinfo->loader, service, &error);
	*ret_error = error;
}

void
plugin_load_dependencies (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	GSList *error_list = NULL;
	PluginDependency **dep_ptr;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	if (!plugin_info_read_full_info_if_needed_error_info (pinfo, ret_error)) {
		return;
	}
	*ret_error = NULL;
	for (dep_ptr = pinfo->dependencies_v; *dep_ptr != NULL; dep_ptr++) {
		PluginInfo *dep_pinfo;
		ErrorInfo *dep_error;

		if (((*dep_ptr)->dep_type & (2 ^ DEPENDENCY_LOAD)) == 0) {
			continue;
		}
		dep_pinfo = plugin_dependency_get_plugin_info (*dep_ptr);
		if (dep_pinfo != NULL) {
			plugin_get_loader_if_needed (dep_pinfo, &dep_error);
			if (dep_error == NULL) {
				gnumeric_plugin_loader_load (dep_pinfo->loader, &dep_error);
			} else {
				dep_error = error_info_new_str_with_details (
				             _("Cannot load plugin loader."),
				             dep_error);
			}
			if (dep_error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("We depend on plugin with id=\"%s\" which couldn't be loaded."),
				            (*dep_ptr)->plugin_id);
				error_info_add_details (new_error, dep_error);
				error_list = g_slist_prepend (error_list, new_error);
			}
		} else {
			error_list = g_slist_prepend (error_list, error_info_new_printf (
			             _("We depend on plugin with id=\"%s\" which is not available."),
			             (*dep_ptr)->plugin_id));
		}
	}

	if (error_list != NULL) {
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

void
plugin_info_free (PluginInfo *pinfo)
{
	if (pinfo == NULL) {
		return;
	}
	g_free (pinfo->id);
	g_free (pinfo->dir_name);
	if (pinfo->has_full_info) {
		g_free (pinfo->name);
		g_free (pinfo->description);
		if (pinfo->loader_static_info != NULL) {
			loader_static_info_free (pinfo->loader_static_info);
		}
		if (pinfo->loader != NULL) {
			gtk_object_destroy (GTK_OBJECT (pinfo->loader));
		}
		g_vector_free_custom (pinfo->services_v, PluginService, (GFreeFunc) plugin_service_free);
		plugin_services_data_free (pinfo->services_data);
	}
	g_free (pinfo);
}

/*
 * May return NULL without errors (is XML file doesn't exist)
 */
static PluginInfo *
plugin_info_read_for_dir (const gchar *dir_name, ErrorInfo **ret_error)
{
	PluginInfo *pinfo = NULL;
	gchar *file_name;
	gchar *file_state;
	PluginFileState *state;
	ErrorInfo *plugin_error;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	file_name = g_concat_dir_and_file (dir_name, PLUGIN_INFO_FILE_NAME);
	file_state = get_file_state_as_string (file_name);
	if (file_state == NULL) {
		g_free (file_name);
		return NULL;
	}
	state = g_hash_table_lookup (plugin_file_state_dir_hash, dir_name);
	if (state != NULL && strcmp (state->file_state, file_state) == 0) {
		pinfo = plugin_info_new_with_id_and_dir_name_only (state->plugin_id, state->dir_name);
	} else if ((pinfo = plugin_info_new_from_xml (dir_name, &plugin_error)) != NULL) {
		g_assert (plugin_error == NULL);
		if (state == NULL) {
			state = g_new (PluginFileState, 1);
			state->dir_name = g_strdup (dir_name);
			state->file_state = g_strdup (file_state);
			state->plugin_id = g_strdup (pinfo->id);
			plugin_file_state_list = g_slist_prepend (plugin_file_state_list, state);
			g_hash_table_insert (plugin_file_state_dir_hash, state->dir_name, state);
		} else {
			g_free (state->file_state);
			g_free (state->plugin_id);
			state->file_state = g_strdup (file_state);
			state->plugin_id = g_strdup (pinfo->id);
		}
		plugin_file_state_list_changed = TRUE;
	} else {
		*ret_error = error_info_new_printf (
		             _("Errors occurred while reading plugin informations from file \"%s\"."),
		             file_name);
		error_info_add_details (*ret_error, plugin_error);
	}
	g_free (file_name);
	g_free (file_state);

	return pinfo;
}

/*
 * May return partial list and some error info.
 */
static GSList *
plugin_info_list_read_for_subdirs_of_dir (const gchar *dir_name, ErrorInfo **ret_error)
{
	GSList *plugin_info_list = NULL;
	DIR *dir;
	struct dirent *entry;
	GSList *error_list = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	dir = opendir (dir_name);
	if (dir == NULL) {
		return NULL;
	}
	while ((entry = readdir (dir)) != NULL) {
		gchar *full_entry_name;
		ErrorInfo *error;
		PluginInfo *pinfo;

		if (strcmp (entry->d_name, ".") == 0 || strcmp (entry->d_name, "..") == 0) {
			continue;
		}
		full_entry_name = g_concat_dir_and_file (dir_name, entry->d_name);
		pinfo = plugin_info_read_for_dir (full_entry_name, &error);
		if (pinfo != NULL) {
			plugin_info_list = g_slist_prepend (plugin_info_list, pinfo);
		}
		if (error != NULL) {
			error_list = g_slist_prepend (error_list, error);
		}
		g_free (full_entry_name);
	}
	if (error_list != NULL) {
		error_list = g_slist_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
	closedir (dir);

	return g_slist_reverse (plugin_info_list);
}

/*
 * May return partial list and some error info.
 */
static GSList *
plugin_info_list_read_for_subdirs_of_dir_list (GSList *dir_list, ErrorInfo **ret_error)
{
	GSList *plugin_info_list = NULL;
	GSList *dir_iterator;
	GSList *error_list = NULL;

	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	for (dir_iterator = dir_list; dir_iterator != NULL; dir_iterator = dir_iterator->next) {
		gchar *dir_name;
		ErrorInfo *error;
		GSList *dir_plugin_info_list;

		dir_name = (gchar *) dir_iterator->data;
		dir_plugin_info_list = plugin_info_list_read_for_subdirs_of_dir (dir_name, &error);
		if (error != NULL) {
			error_list = g_slist_prepend (error_list, error);
		}
		if (dir_plugin_info_list != NULL) {
			plugin_info_list = g_slist_concat (plugin_info_list, dir_plugin_info_list);
		}
	}
	if (error_list != NULL) {
		error_list = g_slist_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}

	return plugin_info_list;
}

GSList *
gnumeric_extra_plugin_dirs (void)
{
	GSList *extra_dirs;
	gchar const *plugin_path_env;
	
	extra_dirs = gnm_gconf_get_plugin_extra_dirs ();
	plugin_path_env = g_getenv ("GNUMERIC_PLUGIN_PATH");
	if (plugin_path_env != NULL) {
		extra_dirs = g_slist_concat (extra_dirs,
					     g_strsplit_to_slist (plugin_path_env, ":"));
	}

	return extra_dirs;
}

/*
 * May return partial list and some error info.
 */
static GSList *
plugin_info_list_read_for_all_dirs (ErrorInfo **ret_error)
{
	GSList *dir_list;
	GSList *plugin_info_list;
	ErrorInfo *error;

	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	dir_list = g_create_slist (gnumeric_sys_plugin_dir (),
				   gnumeric_usr_plugin_dir (),
				   NULL);
	dir_list = g_slist_concat (dir_list, gnumeric_extra_plugin_dirs ());
	plugin_info_list = plugin_info_list_read_for_subdirs_of_dir_list (dir_list, &error);
	e_free_string_slist (dir_list);
	*ret_error = error;

	return plugin_info_list;
}

/*
 * May activate some plugins and return error info for the rest.
 * Doesn't report errors for plugins with unknown loader types
 * (plugin_loader_is_available_by_id() returns FALSE).
 */
void
plugin_db_activate_plugin_list (GSList *plugins, ErrorInfo **ret_error)
{
	GSList *l;
	GSList *error_list = NULL;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	for (l = plugins; l != NULL; l = l->next) {
		PluginInfo *pinfo = l->data;

		if (!pinfo->is_active) {
			ErrorInfo *error;

			activate_plugin (pinfo, &error);
			if (error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("Couldn't activate plugin \"%s\" (ID: %s)."),
				            pinfo->name,
				            pinfo->id);
				error_info_add_details (new_error, error);
				error_list = g_slist_prepend (error_list, new_error);
			}
		}
	}
	if (error_list != NULL) {
		error_list = g_slist_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

/*
 * May deactivate some plugins and return error info for the rest.
 * Doesn't report errors for plugins that are currently in use
 * (plugin_can_deactivate() returns FALSE).
 */
void
plugin_db_deactivate_plugin_list (GSList *plugins, ErrorInfo **ret_error)
{
	GSList *l;
	GSList *error_list = NULL;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	for (l = plugins; l != NULL; l = l->next) {
		PluginInfo *pinfo = l->data;

		if (pinfo->is_active && plugin_can_deactivate (pinfo)) {
			ErrorInfo *error;

			deactivate_plugin (pinfo, &error);
			if (error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("Couldn't deactivate plugin \"%s\" (ID: %s)."),
				            pinfo->name,
				            pinfo->id);
				error_info_add_details (new_error, error);
				error_list = g_slist_prepend (error_list, new_error);
			}
		}
	}
	if (error_list != NULL) {
		error_list = g_slist_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

GSList *
plugin_db_get_known_plugin_id_list (void)
{
	return known_plugin_id_list;
}

GSList *
plugin_db_get_available_plugin_info_list (void)
{
	return available_plugin_info_list;
}

PluginInfo *
plugin_db_get_plugin_info_by_plugin_id (const gchar *plugin_id)
{
	GSList *l;

	g_return_val_if_fail (plugin_id != NULL, NULL);

	for (l = available_plugin_info_list; l != NULL; l = l->next) {
		PluginInfo *pinfo = l->data;

		if (strcmp (pinfo->id, plugin_id) == 0) {
			return pinfo;
		}
	}

	return NULL;
}

void
plugin_db_update_saved_active_plugin_id_list (void)
{
	GSList *l;

	if (saved_active_plugin_id_list != NULL) {
		e_free_string_slist (saved_active_plugin_id_list);
	}
	saved_active_plugin_id_list = NULL;
	for (l = available_plugin_info_list; l != NULL; l = l->next) {
		PluginInfo *pinfo = l->data;

		if (pinfo->is_active) {
			saved_active_plugin_id_list = g_slist_prepend (saved_active_plugin_id_list,
			                                              g_strdup (pinfo->id));
		}
	}
	saved_active_plugin_id_list = g_slist_reverse (saved_active_plugin_id_list);
	gnm_gconf_set_active_plugins (saved_active_plugin_id_list);
}


void
plugin_db_mark_plugin_for_deactivation (PluginInfo *pinfo, gboolean mark)
{
	g_return_if_fail (pinfo != NULL);

	if (mark) {
		if (plugins_marked_for_deactivation_hash == NULL) {
			plugins_marked_for_deactivation_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
		}
		g_hash_table_insert (plugins_marked_for_deactivation_hash, pinfo->id, pinfo);
	} else {
		if (plugins_marked_for_deactivation_hash != NULL) {
			g_hash_table_remove (plugins_marked_for_deactivation_hash, pinfo->id);
		}
	}
}

gboolean
plugin_db_is_plugin_marked_for_deactivation (PluginInfo *pinfo)
{
	return plugins_marked_for_deactivation_hash != NULL &&
	       g_hash_table_lookup (plugins_marked_for_deactivation_hash, pinfo->id) != NULL;
}

/*
 * May return errors for some plugins.
 */
void
plugin_db_init (ErrorInfo **ret_error)
{
	ErrorInfo *error;
	GSList *new_plugin_ids, *l;
	GHashTable *known_plugin_id_hash;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	available_plugin_info_list = plugin_info_list_read_for_all_dirs (&error);
	if (error != NULL) {
		*ret_error = error;
	}

	saved_active_plugin_id_list = gnm_gconf_get_active_plugins ();
	known_plugin_id_list =  gnm_gconf_get_known_plugins ();

	/* Make a hash of the known plugins */
	known_plugin_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
	for (l = known_plugin_id_list; l != NULL; l = l->next)
		g_hash_table_insert (known_plugin_id_hash, l->data, l->data);

	/* Find the new plugins by searching in the hash */
	new_plugin_ids = NULL;
	for (l = available_plugin_info_list; l != NULL; l = l->next) {
		gchar *plugin_id = ((PluginInfo *) l->data)->id;

		if (g_hash_table_lookup (known_plugin_id_hash, plugin_id) == NULL)
			new_plugin_ids = g_slist_prepend (new_plugin_ids, g_strdup (plugin_id));
	}
	g_hash_table_destroy (known_plugin_id_hash);

	/* Store and potentially activate new plugins */
	if (new_plugin_ids != NULL) {
		if (gnm_gconf_get_activate_new_plugins ()) {
			saved_active_plugin_id_list =
				g_slist_concat (saved_active_plugin_id_list,
					       g_string_slist_copy (new_plugin_ids));
			gnm_gconf_set_active_plugins (saved_active_plugin_id_list);
		}
		known_plugin_id_list = g_slist_concat (known_plugin_id_list,
				new_plugin_ids);
		gnm_gconf_set_known_plugins (known_plugin_id_list);
	}
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
	plugin_db_deactivate_plugin_list (available_plugin_info_list, &error);
	*ret_error = error;
}

void 
plugin_db_rescan (void)
{
	ErrorInfo *error = NULL;
	GSList *available_plugins = plugin_info_list_read_for_all_dirs (&error);
	GHashTable *already_available_plugin_id_hash;
	GHashTable *new_plugins_hash;
	GHashTable *known_plugin_id_hash;
	GSList *new_plugin_ids = NULL, *old_plugins = NULL, *l;

	/* FIXME : Handle error */
	if (error) {
		error_info_free (error);
		g_warning ("Unhandled error occurred.");
		return;
	}

        /* Add new plugins to the list of known plugins */
	known_plugin_id_list =  gnm_gconf_get_known_plugins ();
	known_plugin_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
	for (l = known_plugin_id_list; l != NULL; l = l->next)
		g_hash_table_insert (known_plugin_id_hash, l->data, l->data);
	for (l = available_plugins; l != NULL; l = l->next) {
		gchar const *plugin_id = plugin_info_peek_id ((PluginInfo *) l->data);

		if (g_hash_table_lookup (known_plugin_id_hash, plugin_id) == NULL)
			new_plugin_ids = g_slist_prepend (new_plugin_ids, g_strdup (plugin_id));
	}
	g_hash_table_destroy (known_plugin_id_hash);
	if (new_plugin_ids != NULL) {
		known_plugin_id_list = g_slist_concat (known_plugin_id_list,
				new_plugin_ids);
		gnm_gconf_set_known_plugins (known_plugin_id_list);
	}
	new_plugin_ids = NULL;

	/* Find not any longer available plugins */
	new_plugins_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
	for (l = available_plugins; l != NULL; l = l->next)
		g_hash_table_insert (new_plugins_hash,
				     (char *) plugin_info_peek_id ((PluginInfo *) l->data),
				     (PluginInfo *) l->data);
	for (l = available_plugin_info_list; l != NULL; l = l->next) {
		gchar const *plugin_id = plugin_info_peek_id ((PluginInfo *) l->data);
		PluginInfo *found_plugin = g_hash_table_lookup (new_plugins_hash, plugin_id);
		
		if (found_plugin == NULL || strcmp(found_plugin->dir_name, 
						   ((PluginInfo *) l->data)->dir_name)) {
			old_plugins = g_slist_prepend (old_plugins,
						       (PluginInfo *) l->data);
		}
	}
	g_hash_table_destroy (new_plugins_hash);
	if (old_plugins != NULL) {
		ErrorInfo *ret_error = NULL;
		plugin_db_deactivate_plugin_list (old_plugins, &ret_error);
		/* FIXME: we should probably handle these errors. */
		error_info_free (ret_error);

		for (l = old_plugins; l != NULL; l = l->next) {
			if (((PluginInfo *) l->data)->is_active)
			/* FIXME What do we have to do about old still active plugins? */
				g_warning ("Plugin (%s) from (%s) shouldn't be "
					   "available any more.", 
					   ((PluginInfo *) l->data)->id,
					   ((PluginInfo *) l->data)->dir_name);
			else {
				available_plugin_info_list = g_slist_remove (
					available_plugin_info_list,
					l->data);
				plugin_info_free ((PluginInfo *) l->data);
			}
		}
		g_slist_free (old_plugins);
	}

	/* Find previously not available plugins */
	already_available_plugin_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
	for (l = available_plugin_info_list; l != NULL; l = l->next)
		g_hash_table_insert (already_available_plugin_id_hash,
				     (char *) plugin_info_peek_id ((PluginInfo *) l->data),
				     (PluginInfo *) l->data);
	for (l = available_plugins; l != NULL; l = l->next) {
		gchar const *plugin_id = plugin_info_peek_id ((PluginInfo *) l->data);
		PluginInfo *old_plugin = g_hash_table_lookup (already_available_plugin_id_hash,
							      plugin_id);

		if (old_plugin == NULL) {
			available_plugin_info_list = g_slist_prepend (available_plugin_info_list,
								      l->data);
			l->data = NULL;
		}
		
	}
	g_hash_table_destroy (already_available_plugin_id_hash);

	g_slist_free_custom (available_plugins, (GFreeFunc) plugin_info_free);
}

static void
plugin_db_activate_saved_active_plugins (ErrorInfo **ret_error)
{
	GSList *plugin_list = NULL, *l;
	ErrorInfo *error;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	for (l = saved_active_plugin_id_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = plugin_db_get_plugin_info_by_plugin_id ((gchar *) l->data);
		if (pinfo != NULL) {
			plugin_list = g_slist_prepend (plugin_list, pinfo);
		}
	}
	plugin_list = g_slist_reverse (plugin_list);
	plugin_db_activate_plugin_list (plugin_list, &error);
	*ret_error = error;
	g_slist_free (plugin_list);

#ifndef PLUGIN_DEBUG
	plugin_db_update_saved_active_plugin_id_list ();
#endif
}

void
plugins_init (CommandContext *context)
{
	GSList *error_list = NULL;
	ErrorInfo *error;

#ifdef PLUGIN_DEBUG
	gnumeric_time_counter_push ();
#endif
	plugin_loader_register_type ("g_module", TYPE_GNUMERIC_PLUGIN_LOADER_MODULE);
	plugin_cache_init ();
	plugin_db_init (&error);
	if (error != NULL) {
		error_list = g_slist_prepend (error_list, error_info_new_str_with_details (
		                             _("Errors while reading info about available plugins."),
		                             error));
	}
	plugin_db_activate_saved_active_plugins (&error);
	if (error != NULL) {
		error_list = g_slist_prepend (error_list, error_info_new_str_with_details (
		                             _("Errors while activating plugins."),
		                             error));
	}
	if (error_list != NULL) {
		error_list = g_slist_reverse (error_list);
		error = error_info_new_str_with_details_list (
		        _("Errors while initializing plugin system."),
		        error_list);

		gnumeric_error_error_info (context, error);
		error_info_free (error);
	}
#ifdef PLUGIN_DEBUG
	g_print ("plugins_init() time: %fs\n", gnumeric_time_counter_pop ());
#endif
}

static void
plugin_mark_inactive_hash_func (gpointer key, gpointer value, gpointer unused)
{
	plugin_info_force_mark_inactive ((PluginInfo *) value);
}

void
plugins_shutdown (void)
{
	ErrorInfo *ignored_error;

	if (plugins_marked_for_deactivation_hash != NULL) {
		g_hash_table_foreach (plugins_marked_for_deactivation_hash,
		                      &plugin_mark_inactive_hash_func, NULL);
		g_hash_table_destroy (plugins_marked_for_deactivation_hash);
		plugin_db_update_saved_active_plugin_id_list ();
  	}
	plugin_db_shutdown (&ignored_error);
	plugin_cache_shutdown ();
	error_info_free (ignored_error);
	gnm_conf_sync ();
}

void
plugin_message (gint level, const gchar *format, ...)
{
#ifdef PLUGIN_DEBUG
	va_list args;

	if (level <= PLUGIN_DEBUG) {
		va_start (args, format);
		vprintf (format, args);
		va_end (args);
	}
#endif
}
