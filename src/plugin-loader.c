/*
 * plugin-loader.c: Base class for plugin loaders.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "plugin-loader.h"

#include "plugin.h"
#include "plugin-service.h"

#include <gsf/gsf-impl-utils.h>

#define PARENT_TYPE (gtk_object_get_type ())
#define PL_GET_CLASS(loader)	GNUMERIC_PLUGIN_LOADER_CLASS (G_OBJECT_GET_CLASS (loader))

static GtkObjectClass *parent_class = NULL;

static void
gnumeric_plugin_loader_init (GnumericPluginLoader *loader)
{
	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));

	loader->plugin = NULL;
	loader->is_base_loaded = FALSE;
	loader->n_loaded_services = 0;
}

static void
gnumeric_plugin_loader_destroy (GtkObject *obj)
{
	GnumericPluginLoader *loader;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (obj));

	loader = GNUMERIC_PLUGIN_LOADER (obj);

	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
gnumeric_plugin_loader_unload_service_general_real (GnumericPluginLoader *loader,
                                                    PluginService *service,
                                                    ErrorInfo **ret_error)
{
	PluginServiceGeneralCallbacks *cbs;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_GENERAL (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	cbs = plugin_service_get_cbs (service);
	cbs->plugin_func_init = NULL;
	cbs->plugin_func_cleanup = NULL;
}

static void
gnumeric_plugin_loader_unload_service_file_opener_real (GnumericPluginLoader *loader,
                                                        PluginService *service,
                                                        ErrorInfo **ret_error)
{
	PluginServiceFileOpenerCallbacks *cbs;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FILE_OPENER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	cbs = plugin_service_get_cbs (service);
	cbs->plugin_func_file_probe = NULL;
	cbs->plugin_func_file_open = NULL;
}

static void
gnumeric_plugin_loader_unload_service_file_saver_real (GnumericPluginLoader *loader,
                                                       PluginService *service,
                                                       ErrorInfo **ret_error)
{
	PluginServiceFileSaverCallbacks *cbs;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FILE_SAVER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	cbs = plugin_service_get_cbs (service);
	cbs->plugin_func_file_save = NULL;
}

static void
gnumeric_plugin_loader_unload_service_function_group_real (GnumericPluginLoader *loader,
                                                           PluginService *service,
                                                           ErrorInfo **ret_error)
{
	PluginServiceFunctionGroupCallbacks *cbs;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	cbs = plugin_service_get_cbs (service);
	cbs->plugin_func_get_full_function_info = NULL;
}

static void
gnumeric_plugin_loader_unload_service_plugin_loader_real (GnumericPluginLoader *loader,
                                                          PluginService *service,
                                                          ErrorInfo **ret_error)
{
	PluginServicePluginLoaderCallbacks *cbs;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_PLUGIN_LOADER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	cbs = plugin_service_get_cbs (service);
	cbs->plugin_func_get_loader_type = NULL;
}

static void
gnumeric_plugin_loader_unload_service_ui_real (GnumericPluginLoader *loader,
                                               PluginService *service,
                                               ErrorInfo **ret_error)
{
	PluginServiceUICallbacks *cbs;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_UI (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	cbs = plugin_service_get_cbs (service);
	cbs->plugin_func_exec_verb = NULL;
}

static void
gnumeric_plugin_loader_class_init (GnumericPluginLoaderClass *klass)
{
	klass->set_attributes = NULL;
	klass->load_base = NULL;
	klass->unload_base = NULL;
	klass->load_service_general = NULL;
	klass->unload_service_general = gnumeric_plugin_loader_unload_service_general_real;
	klass->load_service_file_opener = NULL;
	klass->unload_service_file_opener = gnumeric_plugin_loader_unload_service_file_opener_real;
	klass->load_service_file_saver = NULL;
	klass->unload_service_file_saver = gnumeric_plugin_loader_unload_service_file_saver_real;
	klass->load_service_function_group = NULL;
	klass->unload_service_function_group = gnumeric_plugin_loader_unload_service_function_group_real;
	klass->load_service_plugin_loader = NULL;
	klass->unload_service_plugin_loader = gnumeric_plugin_loader_unload_service_plugin_loader_real;
	klass->load_service_ui = NULL;
	klass->unload_service_ui = gnumeric_plugin_loader_unload_service_ui_real;

	GTK_OBJECT_CLASS (klass)->destroy = gnumeric_plugin_loader_destroy;
}

GSF_CLASS (GnumericPluginLoader, gnumeric_plugin_loader,
	   gnumeric_plugin_loader_class_init,
	   gnumeric_plugin_loader_init, PARENT_TYPE)

void
gnumeric_plugin_loader_set_attributes (GnumericPluginLoader *loader,
                                       GHashTable *attrs,
                                       ErrorInfo **ret_error)
{
	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	PL_GET_CLASS (loader)->set_attributes (loader, attrs, ret_error);
}

void
gnumeric_plugin_loader_set_plugin (GnumericPluginLoader *loader, GnmPlugin *plugin)
{
	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN (plugin));

	loader->plugin = plugin;
}

void
gnumeric_plugin_loader_load_base (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class;
	ErrorInfo *error;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (!loader->is_base_loaded);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	gnumeric_plugin_loader_class = PL_GET_CLASS (loader);
	g_return_if_fail (gnumeric_plugin_loader_class->load_base != NULL);
	gnumeric_plugin_loader_class->load_base (loader, &error);
	if (error == NULL) {
		loader->is_base_loaded = TRUE;
		plugin_message (3, "Loaded plugin \"%s\".\n", gnm_plugin_get_id (loader->plugin));
	} else {
		*ret_error = error;
	}
}

void
gnumeric_plugin_loader_unload_base (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class;
	ErrorInfo *error;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	if (!loader->is_base_loaded) {
		return;
	}
	gnumeric_plugin_loader_class = PL_GET_CLASS (loader);
	if (gnumeric_plugin_loader_class->unload_base != NULL) {
		gnumeric_plugin_loader_class->unload_base (loader, &error);
		if (error == NULL) {
			loader->is_base_loaded = FALSE;
			plugin_message (3, "Unloaded plugin \"%s\".\n", gnm_plugin_get_id (loader->plugin));
		} else {
			*ret_error = error;
		}
	}
}

void
gnumeric_plugin_loader_load_service (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class;
	ErrorInfo *error;
	void (*load_service_method) (GnumericPluginLoader *, PluginService *, ErrorInfo **) = NULL;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE (service));
	g_return_if_fail (loader->is_base_loaded);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	gnumeric_plugin_loader_class = PL_GET_CLASS (loader);
	if (GNM_IS_PLUGIN_SERVICE_GENERAL (service)) {
		load_service_method = gnumeric_plugin_loader_class->load_service_general;
	} else if (GNM_IS_PLUGIN_SERVICE_FILE_OPENER (service)) {
		load_service_method = gnumeric_plugin_loader_class->load_service_file_opener;
	} else if (GNM_IS_PLUGIN_SERVICE_FILE_SAVER (service)) {
		load_service_method = gnumeric_plugin_loader_class->load_service_file_saver;
	} else if (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service)) {
		load_service_method = gnumeric_plugin_loader_class->load_service_function_group;
	} else if (GNM_IS_PLUGIN_SERVICE_PLUGIN_LOADER (service)) {
		load_service_method = gnumeric_plugin_loader_class->load_service_plugin_loader;
	} else if (GNM_IS_PLUGIN_SERVICE_UI (service)) {
		load_service_method = gnumeric_plugin_loader_class->load_service_ui;
	} else {
		g_assert_not_reached ();
	}
	if (load_service_method != NULL) {
		load_service_method (loader, service, &error);
		*ret_error = error;
	} else {
		*ret_error = error_info_new_str (
		             _("Service not supported by loader."));
	}

	if (*ret_error == NULL) {
		loader->n_loaded_services++;
	}
}

void
gnumeric_plugin_loader_unload_service (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class;
	void (*unload_service_method) (GnumericPluginLoader *, PluginService *, ErrorInfo **) = NULL;
	ErrorInfo *error;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	gnumeric_plugin_loader_class = PL_GET_CLASS (loader);

	if (GNM_IS_PLUGIN_SERVICE_GENERAL (service)) {
		unload_service_method = gnumeric_plugin_loader_class->unload_service_general;
	} else if (GNM_IS_PLUGIN_SERVICE_FILE_OPENER (service)) {
		unload_service_method = gnumeric_plugin_loader_class->unload_service_file_opener;
	} else if (GNM_IS_PLUGIN_SERVICE_FILE_SAVER (service)) {
		unload_service_method = gnumeric_plugin_loader_class->unload_service_file_saver;
	} else if (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service)) {
		unload_service_method = gnumeric_plugin_loader_class->unload_service_function_group;
	} else if (GNM_IS_PLUGIN_SERVICE_PLUGIN_LOADER (service)) {
		unload_service_method = gnumeric_plugin_loader_class->unload_service_plugin_loader;
	} else if (GNM_IS_PLUGIN_SERVICE_UI (service)) {
		unload_service_method = gnumeric_plugin_loader_class->unload_service_ui;
	} else {
		g_assert_not_reached ();
	}
	if (unload_service_method != NULL) {
		unload_service_method (loader, service, &error);
	} else {
		*ret_error = error_info_new_str (
		             _("Service not supported by loader."));
	}

	if (error == NULL) {
		g_return_if_fail (loader->n_loaded_services > 0);
		loader->n_loaded_services--;
/* FIXME - do not unload plugins for now
		if (loader->n_loaded_services == 0) {
			gnumeric_plugin_loader_unload_base (loader, &error);
			error_info_free (error);
		}*/
	} else {
		*ret_error = error;
	}
}

gboolean
gnumeric_plugin_loader_is_base_loaded (GnumericPluginLoader *loader)
{
	g_return_val_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader), FALSE);

	return loader->is_base_loaded;
}
