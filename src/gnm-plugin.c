/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-plugin-service.c: Plugin services - reading XML info, activating, etc.
 *                   (everything independent of plugin loading method)
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include "gutils.h"
#include "func.h"
#include "gnm-plugin.h"
#include "gnumeric-gconf.h"
#include "application.h"

#include <goffice/app/go-cmd-context.h>
#include <goffice/app/go-plugin-service.h>
#include <goffice/app/go-plugin-service-impl.h>
#include <goffice/app/error-info.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-libxml-extras.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define CXML2C(s) ((char const *)(s))

typedef PluginServiceSimpleClass	PluginServiceFunctionGroupClass;
struct _PluginServiceFunctionGroup {
	PluginServiceSimple	base;

	gchar *category_name, *translated_category_name;
	GSList *function_name_list;

	GnmFuncGroup	*func_group;
	PluginServiceFunctionGroupCallbacks cbs;
};

static void
plugin_service_function_group_finalize (GObject *obj)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (obj);
	GObjectClass *parent_class;

	g_free (service_function_group->category_name);
	service_function_group->category_name = NULL;
	g_free (service_function_group->translated_category_name);
	service_function_group->translated_category_name = NULL;
	go_slist_free_custom (service_function_group->function_name_list, g_free);
	service_function_group->function_name_list = NULL;

	parent_class = g_type_class_peek (GO_PLUGIN_SERVICE_TYPE);
	parent_class->finalize (obj);
}

static void
plugin_service_function_group_read_xml (GOPluginService *service, xmlNode *tree, ErrorInfo **ret_error)
{
	xmlNode *category_node, *translated_category_node, *functions_node;
	gchar *category_name, *translated_category_name;
	GSList *function_name_list = NULL;

	GO_INIT_RET_ERROR_INFO (ret_error);
	category_node = e_xml_get_child_by_name_no_lang (tree, "category");
	if (category_node != NULL) {
		xmlChar *val;

		val = xmlNodeGetContent (category_node);
		category_name = g_strdup ((gchar *)val);
		xmlFree (val);
	} else {
		category_name = NULL;
	}
	translated_category_node = e_xml_get_child_by_name_by_lang (tree, "category");
	if (translated_category_node != NULL) {
		gchar *lang;

		lang = xml_node_get_cstr (translated_category_node, "xml:lang");
		if (lang != NULL) {
			xmlChar *val;

			val = xmlNodeGetContent (translated_category_node);
			translated_category_name = g_strdup ((gchar *)val);
			xmlFree (val);
			g_free (lang);
		} else {
			translated_category_name = NULL;
		}
	} else {
		translated_category_name = NULL;
	}
	functions_node = e_xml_get_child_by_name (tree, (xmlChar *)"functions");
	if (functions_node != NULL) {
		xmlNode *node;

		for (node = functions_node->xmlChildrenNode; node != NULL; node = node->next) {
			gchar *func_name;

			if (strcmp (CXML2C (node->name), "function") != 0 ||
			    (func_name = xml_node_get_cstr (node, "name")) == NULL) {
				continue;
			}
			GO_SLIST_PREPEND (function_name_list, func_name);
		}
		GO_SLIST_REVERSE (function_name_list);
	}
	if (category_name != NULL && function_name_list != NULL) {
		PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);

		service_function_group->category_name = category_name;
		service_function_group->translated_category_name = translated_category_name;
		service_function_group->function_name_list = function_name_list;
	} else {
		GSList *error_list = NULL;

		if (category_name == NULL) {
			GO_SLIST_PREPEND (error_list, error_info_new_str (
				_("Missing function category name.")));
		}
		if (function_name_list == NULL) {
			GO_SLIST_PREPEND (error_list, error_info_new_str (
				_("Function group is empty.")));
		}
		GO_SLIST_REVERSE (error_list);
		*ret_error = error_info_new_from_error_list (error_list);

		g_free (category_name);
		g_free (translated_category_name);
		go_slist_free_custom (function_name_list, g_free);
	}
}

static gboolean
plugin_service_function_group_func_desc_load (GnmFunc const *fn_def,
					      GnmFuncDescriptor *res)
{
	GOPluginService	   *service = gnm_func_get_user_data (fn_def);
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	ErrorInfo *error = NULL;

	g_return_val_if_fail (fn_def != NULL, FALSE);

	plugin_service_load (service, &error);
	if (error != NULL) {
		error_info_print (error);
		error_info_free (error);
		return FALSE;
	}
	if (NULL == service_function_group->cbs.func_desc_load) {
                error = error_info_new_printf (_("No func_desc_load method.\n"));
		error_info_print (error);
		error_info_free (error);
		return FALSE;
	}
	return service_function_group->cbs.func_desc_load (service,
		gnm_func_get_name (fn_def), res);
}

static void
plugin_service_function_group_func_ref_notify  (GnmFunc *fn_def, int refcount)
{
	GOPluginService *service;

	service = gnm_func_get_user_data (fn_def);
	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service));
	if (refcount == 0) {
		go_plugin_use_unref (service->plugin);
	} else {
		go_plugin_use_ref (service->plugin);
	}
}

static void
plugin_service_function_group_activate (GOPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);

	GO_INIT_RET_ERROR_INFO (ret_error);
	service_function_group->func_group = gnm_func_group_fetch_with_translation (
		service_function_group->category_name,
		service_function_group->translated_category_name);
	GO_SLIST_FOREACH (service_function_group->function_name_list, char, fname,
		GnmFunc *fn_def;

		fn_def = gnm_func_add_stub (
			service_function_group->func_group, fname,
			plugin_service_function_group_func_desc_load,
			plugin_service_function_group_func_ref_notify);
		gnm_func_set_user_data (fn_def, service);
	);
	service->is_active = TRUE;
}

static void
plugin_service_function_group_deactivate (GOPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);

	GO_INIT_RET_ERROR_INFO (ret_error);
	GO_SLIST_FOREACH (service_function_group->function_name_list, char, fname,
		gnm_func_free (gnm_func_lookup (fname, NULL));
	);
	service->is_active = FALSE;
}

static char *
plugin_service_function_group_get_description (GOPluginService *service)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	int n_functions;
	char const *category_name;

	n_functions = g_slist_length (service_function_group->function_name_list);
	category_name = service_function_group->translated_category_name != NULL
		? service_function_group->translated_category_name
		: service_function_group->category_name;

	return g_strdup_printf (ngettext (
			"%d function in category \"%s\"",
			"Group of %d functions in category \"%s\"",
			n_functions),
		n_functions, category_name);
}

static void
plugin_service_function_group_init (PluginServiceFunctionGroup *s)
{
	GO_PLUGIN_SERVICE (s)->cbs_ptr = &s->cbs;
	s->category_name = NULL;
	s->translated_category_name = NULL;
	s->function_name_list = NULL;
	s->func_group = NULL;
}
static void
plugin_service_function_group_class_init (GObjectClass *gobject_class)
{
	GOPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	gobject_class->finalize		= plugin_service_function_group_finalize;
	plugin_service_class->read_xml	= plugin_service_function_group_read_xml;
	plugin_service_class->activate	= plugin_service_function_group_activate;
	plugin_service_class->deactivate = plugin_service_function_group_deactivate;
	plugin_service_class->get_description = plugin_service_function_group_get_description;
}

GSF_CLASS (PluginServiceFunctionGroup, plugin_service_function_group,
           plugin_service_function_group_class_init, plugin_service_function_group_init,
           GO_PLUGIN_SERVICE_SIMPLE_TYPE)

/****************************************************************************/

/*
 * PluginServiceUI
 */
typedef GOPluginServiceClass PluginServiceUIClass;
struct _PluginServiceUI {
	PluginServiceSimple	base;

	char *file_name;
	GSList *actions;

	gpointer layout_id;
	PluginServiceUICallbacks cbs;
};

static void
plugin_service_ui_init (PluginServiceUI *s)
{
	GO_PLUGIN_SERVICE (s)->cbs_ptr = &s->cbs;
	s->file_name = NULL;
	s->actions = NULL;
	s->layout_id = NULL;
	s->cbs.plugin_func_exec_action = NULL;
}

static void
plugin_service_ui_finalize (GObject *obj)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (obj);
	GObjectClass *parent_class;

	g_free (service_ui->file_name);
	service_ui->file_name = NULL;
	go_slist_free_custom (service_ui->actions, (GFreeFunc)gnm_action_free);
	service_ui->actions = NULL;

	parent_class = g_type_class_peek (GO_PLUGIN_SERVICE_TYPE);
	parent_class->finalize (obj);
}

static void
cb_ui_service_activate (GnmAction const *action, WorkbookControl *wbc, GOPluginService *service)
{
	ErrorInfo *load_error = NULL;

	plugin_service_load (service, &load_error);
	if (load_error == NULL) {
		PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
		ErrorInfo *ignored_error = NULL;

		g_return_if_fail (service_ui->cbs.plugin_func_exec_action != NULL);
		service_ui->cbs.plugin_func_exec_action (
			service, action, wbc, &ignored_error);
		if (ignored_error != NULL) {
			error_info_print (ignored_error);
			error_info_free (ignored_error);
		}
	} else {
		error_info_print (load_error);
		error_info_free (load_error);
	}
}

static void
plugin_service_ui_read_xml (GOPluginService *service, xmlNode *tree, ErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	xmlChar *file_name;
	xmlNode *verbs_node;
	GSList *actions = NULL;

	GO_INIT_RET_ERROR_INFO (ret_error);
	file_name = xml_node_get_cstr (tree, "file");
	if (file_name == NULL) {
		*ret_error = error_info_new_str (
		             _("Missing file name."));
		return;
	}
	verbs_node = e_xml_get_child_by_name (tree, "actions");
	if (verbs_node != NULL) {
		xmlNode *ptr;
		xmlChar *name, *label, *icon;
		gboolean always_available;
		GnmAction *action;

		for (ptr = verbs_node->xmlChildrenNode; ptr != NULL; ptr = ptr->next) {
			if (xmlIsBlankNode (ptr) || ptr->name == NULL ||
			    strcmp (CXML2C (ptr->name), "action"))
				continue;
			name  = xml_node_get_cstr (ptr, "name");
			label = xml_node_get_cstr (ptr, "label");
			icon  = xml_node_get_cstr (ptr, "icon");
			if (!xml_node_get_bool (ptr, "always_available", &always_available))
				always_available = FALSE;
			action = gnm_action_new (name, label, icon, always_available,
				(GnmActionHandler) cb_ui_service_activate);
			if (NULL != name) xmlFree (name);
			if (NULL != name) xmlFree (label);
			if (NULL != name) xmlFree (icon);
			if (NULL != action)
				GO_SLIST_PREPEND (actions, action);
		}
	}
	GO_SLIST_REVERSE (actions);

	service_ui->file_name = file_name;
	service_ui->actions = actions;
}

static void
plugin_service_ui_activate (GOPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	GError *err = NULL;
	char *full_file_name;
	char *xml_ui;
	char const *textdomain;

	GO_INIT_RET_ERROR_INFO (ret_error);
	full_file_name = g_build_filename (
		go_plugin_get_dir_name (service->plugin),
		service_ui->file_name, NULL);
	if (!g_file_get_contents (full_file_name, &xml_ui, NULL, &err)) {
		*ret_error = error_info_new_printf (
		             _("Cannot read UI description from XML file %s."),
		             full_file_name);
		g_free (full_file_name);
		return;
	}
	g_free (full_file_name);

	textdomain = go_plugin_get_textdomain (service->plugin);
	service_ui->layout_id = gnm_app_add_extra_ui (
		service_ui->actions,
		xml_ui, textdomain, service);
	service->is_active = TRUE;
}

static void
plugin_service_ui_deactivate (GOPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_app_remove_extra_ui (service_ui->layout_id);
	service_ui->layout_id = NULL;
	service->is_active = FALSE;
}

static char *
plugin_service_ui_get_description (GOPluginService *service)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	int n_actions;

	n_actions = g_slist_length (service_ui->actions);
	return g_strdup_printf (
		ngettext (
			"User interface with %d action",
			"User interface with %d actions",
			n_actions),
		n_actions);
}

static void
plugin_service_ui_class_init (GObjectClass *gobject_class)
{
	GOPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_ui_finalize;
	plugin_service_class->read_xml = plugin_service_ui_read_xml;
	plugin_service_class->activate = plugin_service_ui_activate;
	plugin_service_class->deactivate = plugin_service_ui_deactivate;
	plugin_service_class->get_description = plugin_service_ui_get_description;
}

GSF_CLASS (PluginServiceUI, plugin_service_ui,
           plugin_service_ui_class_init, plugin_service_ui_init,
           GO_PLUGIN_SERVICE_TYPE)

/****************************************************************************/

#include <goffice/app/go-plugin-loader.h>
#include <goffice/app/go-plugin-loader-module.h>

typedef GOPluginLoaderModule	  GnmPluginLoaderModule;
typedef GOPluginLoaderModuleClass GnmPluginLoaderModuleClass;

#define GNM_PLUGIN_LOADER_MODULE_TYPE (gnm_plugin_loader_module_get_type ())
#define GNM_PLUGIN_LOADER_MODULE(o)  (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_LOADER_MODULE_TYPE, GnmPluginLoaderModule))
GType gnm_plugin_loader_module_get_type (void);

/*
 * Service - function_group
 */
typedef struct {
	GnmFuncDescriptor *module_fn_info_array;
	GHashTable *function_indices;
} ServiceLoaderDataFunctionGroup;

static void
function_group_loader_data_free (gpointer data)
{
	ServiceLoaderDataFunctionGroup *ld = data;

	g_hash_table_destroy (ld->function_indices);
	g_free (ld);
}

static gboolean
gnm_plugin_loader_module_func_desc_load (GOPluginService *service,
					 char const *name,
					 GnmFuncDescriptor *res)
{
	ServiceLoaderDataFunctionGroup *loader_data;
	gpointer func_index_ptr;

	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	if (g_hash_table_lookup_extended (loader_data->function_indices, (gpointer) name,
	                                  NULL, &func_index_ptr)) {
		int i = GPOINTER_TO_INT (func_index_ptr);
		*res = loader_data->module_fn_info_array[i];
		return TRUE;
	}
	return FALSE;
}
static void
gnm_plugin_loader_module_load_service_function_group (GOPluginLoader  *loader,
						      GOPluginService *service,
						      ErrorInfo **ret_error)
{
	GnmPluginLoaderModule *loader_module = GNM_PLUGIN_LOADER_MODULE (loader);
	gchar *fn_info_array_name;
	GnmFuncDescriptor *module_fn_info_array = NULL;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	fn_info_array_name = g_strconcat (
		plugin_service_get_id (service), "_functions", NULL);
	g_module_symbol (loader_module->handle, fn_info_array_name, (gpointer) &module_fn_info_array);
	if (module_fn_info_array != NULL) {
		PluginServiceFunctionGroupCallbacks *cbs;
		ServiceLoaderDataFunctionGroup *loader_data;
		gint i;

		cbs = plugin_service_get_cbs (service);
		cbs->func_desc_load = &gnm_plugin_loader_module_func_desc_load;

		loader_data = g_new (ServiceLoaderDataFunctionGroup, 1);
		loader_data->module_fn_info_array = module_fn_info_array;
		loader_data->function_indices = g_hash_table_new (&g_str_hash, &g_str_equal);
		for (i = 0; module_fn_info_array[i].name != NULL; i++) {
			g_hash_table_insert (loader_data->function_indices,
			                     (gpointer) module_fn_info_array[i].name,
			                     GINT_TO_POINTER (i));
		}
		g_object_set_data_full (
			G_OBJECT (service), "loader_data", loader_data, function_group_loader_data_free);
	} else {
		*ret_error = error_info_new_printf (
		             _("Module file \"%s\" has invalid format."),
		             loader_module->module_file_name);
		error_info_add_details (*ret_error,
					error_info_new_printf (
					_("File doesn't contain \"%s\" array."),
					fn_info_array_name));
	}
	g_free (fn_info_array_name);
}

/*
 * Service - ui
 */

typedef struct {
	ModulePluginUIActions *module_ui_actions_array;
	GHashTable *ui_actions_hash;
} ServiceLoaderDataUI;

static void
ui_loader_data_free (gpointer data)
{
	ServiceLoaderDataUI *ld = data;

	g_hash_table_destroy (ld->ui_actions_hash);
	g_free (ld);
}

static void
gnm_plugin_loader_module_func_exec_action (GOPluginService *service,
					   GnmAction const *action,
					   WorkbookControl *wbc,
					   ErrorInfo **ret_error)
{
	ServiceLoaderDataUI *loader_data;
	gpointer action_index_ptr;
	int action_index;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_UI (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	if (!g_hash_table_lookup_extended (loader_data->ui_actions_hash, action->id,
	                                   NULL, &action_index_ptr)) {
		*ret_error = error_info_new_printf (_("Unknown action: %s"), action->id);
		return;
	}
	action_index = GPOINTER_TO_INT (action_index_ptr);
	if (NULL != loader_data->module_ui_actions_array [action_index].handler)
		(*loader_data->module_ui_actions_array [action_index].handler) (action, wbc);
}

static void
gnm_plugin_loader_module_load_service_ui (GOPluginLoader *loader,
					  GOPluginService *service,
					  ErrorInfo **ret_error)
{
	GnmPluginLoaderModule *loader_module = GNM_PLUGIN_LOADER_MODULE (loader);
	char *ui_actions_array_name;
	ModulePluginUIActions *module_ui_actions_array = NULL;
	PluginServiceUICallbacks *cbs;
	ServiceLoaderDataUI *loader_data;
	gint i;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_UI (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	ui_actions_array_name = g_strconcat (
		plugin_service_get_id (service), "_ui_actions", NULL);
	g_module_symbol (loader_module->handle, ui_actions_array_name, (gpointer) &module_ui_actions_array);
	if (module_ui_actions_array == NULL) {
		*ret_error = error_info_new_printf (
			_("Module file \"%s\" has invalid format."),
			loader_module->module_file_name);
		error_info_add_details (*ret_error, error_info_new_printf (
			_("File doesn't contain \"%s\" array."), ui_actions_array_name));
		g_free (ui_actions_array_name);
		return;
	}
	g_free (ui_actions_array_name);

	cbs = plugin_service_get_cbs (service);
	cbs->plugin_func_exec_action = gnm_plugin_loader_module_func_exec_action;

	loader_data = g_new (ServiceLoaderDataUI, 1);
	loader_data->module_ui_actions_array = module_ui_actions_array;
	loader_data->ui_actions_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; module_ui_actions_array[i].name != NULL; i++)
		g_hash_table_insert (loader_data->ui_actions_hash,
			(gpointer) module_ui_actions_array[i].name,
			GINT_TO_POINTER (i));
	g_object_set_data_full (G_OBJECT (service),
		"loader_data", loader_data, ui_loader_data_free);
}

static gboolean
gplm_service_load (GOPluginLoader *l, GOPluginService *s, ErrorInfo **err)
{
	if (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (s))
		gnm_plugin_loader_module_load_service_function_group (l, s, err);
	else if (IS_GNM_PLUGIN_SERVICE_UI (s))
		gnm_plugin_loader_module_load_service_ui (l, s, err);
	else
		return FALSE;
	return TRUE;
}

static gboolean
gplm_service_unload (GOPluginLoader *l, GOPluginService *s, ErrorInfo **err)
{
	if (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (s)) {
		PluginServiceFunctionGroupCallbacks *cbs = plugin_service_get_cbs (s);
		cbs->func_desc_load = NULL;
	} else if (IS_GNM_PLUGIN_SERVICE_UI (s)) {
		PluginServiceUICallbacks *cbs = plugin_service_get_cbs (s);
		cbs->plugin_func_exec_action = NULL;
	} else
		return FALSE;
	return TRUE;
}

static void
go_plugin_loader_module_iface_init (GOPluginLoaderClass *iface)
{
	iface->service_load   = gplm_service_load;
	iface->service_unload = gplm_service_unload;
}

GSF_CLASS_FULL (GnmPluginLoaderModule, gnm_plugin_loader_module,
           NULL, NULL, NULL, NULL,
           NULL, GO_PLUGIN_LOADER_MODULE_TYPE, 0,
	   GSF_INTERFACE (go_plugin_loader_module_iface_init, GO_PLUGIN_LOADER_TYPE))

/****************************************************************************/

void
gnm_plugins_init (GOCmdContext *context)
{
	char const *env_var;
	GSList *dir_list = go_slist_create (
		g_build_filename (gnm_sys_lib_dir (), PLUGIN_SUBDIR, NULL),
		(gnm_usr_dir () == NULL ? NULL :
			g_build_filename (gnm_usr_dir (), PLUGIN_SUBDIR, NULL)),
		NULL);
	dir_list = g_slist_concat (dir_list,
		go_string_slist_copy (gnm_app_prefs->plugin_extra_dirs));

	env_var = g_getenv ("GNUMERIC_PLUGIN_PATH");
	if (env_var != NULL)
		GO_SLIST_CONCAT (dir_list, go_strsplit_to_slist (env_var, G_SEARCHPATH_SEPARATOR));

	go_plugins_init (GO_CMD_CONTEXT (context),
		gnm_app_prefs->plugin_file_states,
		gnm_app_prefs->active_plugins,
		dir_list,
		gnm_app_prefs->activate_new_plugins,
		gnm_plugin_loader_module_get_type ());
}
