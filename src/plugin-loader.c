/*
 * plugin-loader.c: Base class for plugin loaders.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "plugin-loader.h"

#include "plugin.h"
#include "plugin-service.h"

#include <libgnome/libgnome.h>
#include <gal/util/e-util.h>

#define PARENT_TYPE (gtk_object_get_type ())
#define PL_GET_CLASS(loader)	GNUMERIC_PLUGIN_LOADER_CLASS (G_OBJECT_GET_CLASS (loader))

void gnumeric_plugin_loader_unload_service_general_real (GnumericPluginLoader *loader,
                                                         PluginService *service,
                                                         ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload_service_file_opener_real (GnumericPluginLoader *loader,
                                                             PluginService *service,
                                                             ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload_service_file_saver_real (GnumericPluginLoader *loader,
                                                            PluginService *service,
                                                            ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload_service_function_group_real (GnumericPluginLoader *loader,
                                                                PluginService *service,
                                                                ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload_service_plugin_loader_real (GnumericPluginLoader *loader,
                                                               PluginService *service,
                                                               ErrorInfo **ret_error);

static GtkObjectClass *parent_class = NULL;

static void
gnumeric_plugin_loader_init (GnumericPluginLoader *loader)
{
	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));

	loader->plugin = NULL;
	loader->is_loaded = FALSE;
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

void
gnumeric_plugin_loader_unload_service_general_real (GnumericPluginLoader *loader,
                                                    PluginService *service,
                                                    ErrorInfo **ret_error)
{
	PluginServiceGeneral *service_general;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_general = &service->t.general;
	service_general->plugin_func_init = NULL;
	service_general->plugin_func_can_deactivate = NULL;
	service_general->plugin_func_cleanup = NULL;
}

void
gnumeric_plugin_loader_unload_service_file_opener_real (GnumericPluginLoader *loader,
                                                        PluginService *service,
                                                        ErrorInfo **ret_error)
{
	PluginServiceFileOpener *service_file_opener;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_file_opener = &service->t.file_opener;
	service_file_opener->plugin_func_file_probe = NULL;
	service_file_opener->plugin_func_file_open = NULL;
}

void
gnumeric_plugin_loader_unload_service_file_saver_real (GnumericPluginLoader *loader,
                                                       PluginService *service,
                                                       ErrorInfo **ret_error)
{
	PluginServiceFileSaver *service_file_saver;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_file_saver = &service->t.file_saver;
	service_file_saver->plugin_func_file_save = NULL;
}

void
gnumeric_plugin_loader_unload_service_function_group_real (GnumericPluginLoader *loader,
                                                           PluginService *service,
                                                           ErrorInfo **ret_error)
{
	PluginServiceFunctionGroup *service_function_group;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_function_group = &service->t.function_group;
	service_function_group->plugin_func_get_full_function_info = NULL;
}

void gnumeric_plugin_loader_unload_service_plugin_loader_real (GnumericPluginLoader *loader,
                                                               PluginService *service,
                                                               ErrorInfo **ret_error)
{
	PluginServicePluginLoader *service_plugin_loader;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_plugin_loader = &service->t.plugin_loader;
	service_plugin_loader->plugin_func_get_loader_type = NULL;
}

static void
gnumeric_plugin_loader_class_init (GnumericPluginLoaderClass *klass)
{
	klass->set_attributes = NULL;
	klass->load = NULL;
	klass->unload = NULL;
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
	klass->get_extra_info_list = NULL;

	GTK_OBJECT_CLASS (klass)->destroy = gnumeric_plugin_loader_destroy;
}

E_MAKE_TYPE (gnumeric_plugin_loader, "GnumericPluginLoader", GnumericPluginLoader, &gnumeric_plugin_loader_class_init, gnumeric_plugin_loader_init, PARENT_TYPE)

void
gnumeric_plugin_loader_set_attributes (GnumericPluginLoader *loader,
                                       GList *attr_names, GList *attr_values,
                                       ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (ret_error != NULL);

	PL_GET_CLASS(loader)->set_attributes (loader, attr_names, attr_values, &error);
	*ret_error = error;
}

void
gnumeric_plugin_loader_set_plugin (GnumericPluginLoader *loader, PluginInfo *plugin)
{
	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (plugin != NULL);

	loader->plugin = plugin;
}

void
gnumeric_plugin_loader_load (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class;
	ErrorInfo *error;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (loader->is_loaded) {
		return;
	}
	plugin_load_dependencies (loader->plugin, &error);
	if (error == NULL) {
		gnumeric_plugin_loader_class = PL_GET_CLASS(loader);
		g_return_if_fail (gnumeric_plugin_loader_class->load != NULL);
		gnumeric_plugin_loader_class->load (loader, &error);
		if (error == NULL) {
			loader->is_loaded = TRUE;
			plugin_dependencies_inc_dependants (loader->plugin, DEPENDENCY_LOAD);
			plugin_message (0, "Loaded plugin \"%s\".\n", plugin_info_peek_id (loader->plugin));
		} else {
			*ret_error = error;
		}
	} else {
		*ret_error = error_info_new_str_with_details (
		             _("Error while loading plugin dependencies."),
		             error);
	}
}

void
gnumeric_plugin_loader_unload (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class;
	ErrorInfo *error;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (!loader->is_loaded) {
		return;
	}
	gnumeric_plugin_loader_class = PL_GET_CLASS(loader);
	if (gnumeric_plugin_loader_class->unload != NULL) {
		gnumeric_plugin_loader_class->unload (loader, &error);
		if (error == NULL) {
			loader->is_loaded = FALSE;
			plugin_dependencies_dec_dependants (loader->plugin, DEPENDENCY_LOAD);
			plugin_message (0, "Unloaded plugin \"%s\".\n", plugin_info_peek_id (loader->plugin));
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

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	gnumeric_plugin_loader_class = PL_GET_CLASS(loader);
	gnumeric_plugin_loader_load (loader, &error);
	if (error == NULL) {
		void (*load_service_method) (GnumericPluginLoader *, PluginService *, ErrorInfo **) = NULL;

		switch (service->service_type) {
		case PLUGIN_SERVICE_GENERAL:
			load_service_method = gnumeric_plugin_loader_class->load_service_general;
			break;
		case PLUGIN_SERVICE_FILE_OPENER:
			load_service_method = gnumeric_plugin_loader_class->load_service_file_opener;
			break;
		case PLUGIN_SERVICE_FILE_SAVER:
			load_service_method = gnumeric_plugin_loader_class->load_service_file_saver;
			break;
		case PLUGIN_SERVICE_FUNCTION_GROUP:
			load_service_method = gnumeric_plugin_loader_class->load_service_function_group;
			break;
		case PLUGIN_SERVICE_PLUGIN_LOADER:
			load_service_method = gnumeric_plugin_loader_class->load_service_plugin_loader;
			break;
		default:
			g_assert_not_reached ();
		}
		if (load_service_method != NULL) {
			load_service_method (loader, service, &error);
			*ret_error = error;
		} else {
			*ret_error = error_info_new_str (
			             _("Service not supported by loader."));
		}
	} else {
		*ret_error = error_info_new_str_with_details (
		             _("Error while loading plugin."),
		             error);
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
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	gnumeric_plugin_loader_class = PL_GET_CLASS(loader);

	switch (service->service_type) {
	case PLUGIN_SERVICE_GENERAL:
		unload_service_method = gnumeric_plugin_loader_class->unload_service_general;
		break;
	case PLUGIN_SERVICE_FILE_OPENER:
		unload_service_method = gnumeric_plugin_loader_class->unload_service_file_opener;
		break;
	case PLUGIN_SERVICE_FILE_SAVER:
		unload_service_method = gnumeric_plugin_loader_class->unload_service_file_saver;
		break;
	case PLUGIN_SERVICE_FUNCTION_GROUP:
		unload_service_method = gnumeric_plugin_loader_class->unload_service_function_group;
		break;
	case PLUGIN_SERVICE_PLUGIN_LOADER:
		unload_service_method = gnumeric_plugin_loader_class->unload_service_plugin_loader;
		break;
	default:
		g_assert_not_reached ();
	}
	if (unload_service_method != NULL) {
		unload_service_method (loader, service, &error);
		g_free (plugin_service_get_loader_data (service));
	} else {
		*ret_error = error_info_new_str (
		             _("Service not supported by loader."));
	}

	if (error == NULL) {
		g_return_if_fail (loader->n_loaded_services > 0);
		loader->n_loaded_services--;
		if (loader->n_loaded_services == 0) {
			gnumeric_plugin_loader_unload (loader, &error);
			error_info_free (error);
		}
	} else {
		*ret_error = error;
	}
}

gint
gnumeric_plugin_loader_get_extra_info_list (GnumericPluginLoader *loader, GList **ret_keys_list, GList **ret_values_list)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class;

	g_return_val_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader), 0);
	g_return_val_if_fail (ret_keys_list != NULL && ret_values_list != NULL, 0);

	gnumeric_plugin_loader_class = PL_GET_CLASS(loader);
	if (gnumeric_plugin_loader_class->get_extra_info_list != NULL) {
		return gnumeric_plugin_loader_class->get_extra_info_list (loader, ret_keys_list, ret_values_list);
	} else {
		return 0;
	}
}

gboolean
gnumeric_plugin_loader_is_loaded (GnumericPluginLoader *loader)
{
	g_return_val_if_fail (IS_GNUMERIC_PLUGIN_LOADER (loader), FALSE);

	return loader->is_loaded;
}
