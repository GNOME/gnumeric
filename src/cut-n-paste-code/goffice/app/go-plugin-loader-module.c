/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * plugin-loader-module.c: Support for "g_module" (shared libraries) plugins.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <goffice/goffice-config.h>
#include "go-plugin-loader-module.h"
#include "module-plugin-defs.h"

#include <goffice/app/file.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/go-plugin-service.h>
#include <goffice/app/go-plugin-loader.h>
#include <goffice/app/error-info.h>

#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>
#include <glib/gi18n.h>
#include <string.h>

static void go_plugin_loader_module_set_attributes (GOPluginLoader *loader, GHashTable *attrs, ErrorInfo **ret_error);
static void go_plugin_loader_module_load_base (GOPluginLoader *loader, ErrorInfo **ret_error);
static void go_plugin_loader_module_unload_base (GOPluginLoader *loader, ErrorInfo **ret_error);

static void go_plugin_loader_module_load_service_file_opener (GOPluginLoader *loader, GOPluginService *service, ErrorInfo **ret_error);
static void go_plugin_loader_module_load_service_file_saver (GOPluginLoader *loader, GOPluginService *service, ErrorInfo **ret_error);
static void go_plugin_loader_module_load_service_plugin_loader (GOPluginLoader *loader, GOPluginService *service, ErrorInfo **ret_error);

static void
go_plugin_loader_module_set_attributes (GOPluginLoader *loader, GHashTable *attrs,
					ErrorInfo **err)
{
	GOPluginLoaderModule *loader_module = GO_PLUGIN_LOADER_MODULE (loader);
	gchar *module_file_name = g_hash_table_lookup (attrs, "module_file");

	if (module_file_name != NULL)
		loader_module->module_file_name = g_strdup (module_file_name);
	else
		*err = error_info_new_str ( _("Module file name not given."));
}

static void
go_plugin_loader_module_load_base (GOPluginLoader *loader, ErrorInfo **err)
{
	GOPluginLoaderModule *loader_module = GO_PLUGIN_LOADER_MODULE (loader);

	GNM_INIT_RET_ERROR_INFO (err);
	if (g_module_supported ()) {
		gchar *full_module_file_name;
		GModule *handle;
		ModulePluginFileStruct *plugin_file_struct = NULL;
		gpointer plugin_init_func = NULL, plugin_shutdown_func = NULL;

		full_module_file_name = g_build_filename (gnm_plugin_get_dir_name (
			go_plugin_loader_get_plugin (loader)),
			loader_module->module_file_name, NULL);
		handle = g_module_open (full_module_file_name, 0);
		if (handle != NULL) {
			g_module_symbol (handle, "plugin_file_struct", (gpointer) &plugin_file_struct);
			g_module_symbol (handle, "go_plugin_init", &plugin_init_func);
			g_module_symbol (handle, "go_plugin_shutdown", &plugin_shutdown_func);
		}
		if (handle != NULL && plugin_file_struct != NULL &&
		    plugin_file_struct->magic_number == GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER &&
		    strcmp (plugin_file_struct->version, GNUMERIC_VERSION) == 0) {
			loader_module->handle = handle;
			loader_module->plugin_init = plugin_init_func;
			loader_module->plugin_shutdown = plugin_shutdown_func;
			if (loader_module->plugin_init != NULL) {
				loader_module->plugin_init ();
			}
		} else {
			if (handle == NULL) {
				*err = error_info_new_printf (
					_("Unable to open module file \"%s\"."),
					full_module_file_name);
				error_info_add_details (*err, error_info_new_str (g_module_error()));
			} else {
				*err = error_info_new_printf (
					_("Module file \"%s\" has invalid format."),
					full_module_file_name);
				if (plugin_file_struct == NULL) {
					error_info_add_details (*err, error_info_new_str (
						_("File doesn't contain (\"plugin_file_struct\" symbol).")));
				} else if (plugin_file_struct->magic_number != GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER) {
					error_info_add_details (*err, error_info_new_str (
						_("File has a bad magic number.")));
				} else if (strcmp (plugin_file_struct->version, GNUMERIC_VERSION) == 0) {
					error_info_add_details (*err, error_info_new_printf (
						_("Plugin version \"%s\" is different from application \"%s\"."),
						plugin_file_struct->version, GNUMERIC_VERSION));
				}
				g_module_close (handle);
			}
		}
		g_free (full_module_file_name);
	} else
		*err = error_info_new_str (
			_("Dynamic module loading is not supported in this system."));
}

static void
go_plugin_loader_module_unload_base (GOPluginLoader *loader, ErrorInfo **ret_error)
{
	GOPluginLoaderModule *loader_module = GO_PLUGIN_LOADER_MODULE (loader);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	if (loader_module->plugin_shutdown != NULL) {
		loader_module->plugin_shutdown ();
	}
	if (!g_module_close (loader_module->handle)) {
		*ret_error = error_info_new_printf (
			_("Unable to close module file \"%s\"."),
			loader_module->module_file_name);
		error_info_add_details (*ret_error, error_info_new_str (g_module_error()));
	}
	loader_module->handle = NULL;
	loader_module->plugin_init = NULL;
	loader_module->plugin_shutdown = NULL;
}

static void
go_plugin_loader_module_init (GOPluginLoaderModule *loader_module)
{
	g_return_if_fail (IS_GO_PLUGIN_LOADER_MODULE (loader_module));

	loader_module->module_file_name = NULL;
	loader_module->handle = NULL;
}

static void
go_plugin_loader_module_finalize (GObject *obj)
{
	GOPluginLoaderModule *loader_module = GO_PLUGIN_LOADER_MODULE (obj);
	g_free (loader_module->module_file_name);
	loader_module->module_file_name = NULL;
	G_OBJECT_CLASS (g_type_class_peek (G_TYPE_OBJECT))->finalize (obj);
}

static void
go_plugin_loader_module_class_init (GObjectClass *gobject_class)
{
	gobject_class->finalize = go_plugin_loader_module_finalize;
}

static void
go_plugin_loader_init (GOPluginLoaderClass *go_plugin_loader_class)
{
	go_plugin_loader_class->set_attributes = go_plugin_loader_module_set_attributes;
	go_plugin_loader_class->load_base = go_plugin_loader_module_load_base;
	go_plugin_loader_class->unload_base = go_plugin_loader_module_unload_base;
	go_plugin_loader_class->load_service_file_opener = go_plugin_loader_module_load_service_file_opener;
	go_plugin_loader_class->load_service_file_saver = go_plugin_loader_module_load_service_file_saver;
	go_plugin_loader_class->load_service_plugin_loader = go_plugin_loader_module_load_service_plugin_loader;
}

GSF_CLASS_FULL (GOPluginLoaderModule, go_plugin_loader_module,
	   go_plugin_loader_module_class_init, go_plugin_loader_module_init,
	   G_TYPE_OBJECT, 0,
	   GSF_INTERFACE (go_plugin_loader_init, GO_PLUGIN_LOADER_TYPE))

/*
 * Service - file_opener
 */

typedef struct {
	gboolean (*module_func_file_probe) (GnmFileOpener const *fo, GsfInput *input,
					    FileProbeLevel pl);
	void (*module_func_file_open) (GnmFileOpener const *fo, IOContext *io_context,
				       gpointer FIXME_FIXME_workbook_view,
				       GsfInput *input);
} ServiceLoaderDataFileOpener;

static gboolean
go_plugin_loader_module_func_file_probe (GnmFileOpener const *fo, GOPluginService *service,
					  GsfInput *input, FileProbeLevel pl)
{
	ServiceLoaderDataFileOpener *loader_data;

	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE_FILE_OPENER (service), FALSE);
	g_return_val_if_fail (input != NULL, FALSE);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	return loader_data->module_func_file_probe (fo, input, pl);
}

static void
go_plugin_loader_module_func_file_open (GnmFileOpener const *fo, GOPluginService *service,
					 IOContext *io_context,
					 gpointer   FIXME_FIXME_workbook_view,
					 GsfInput  *input)
{
	ServiceLoaderDataFileOpener *loader_data;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FILE_OPENER (service));
	g_return_if_fail (input != NULL);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	loader_data->module_func_file_open (fo, io_context,
		FIXME_FIXME_workbook_view, input);
}

static void
go_plugin_loader_module_load_service_file_opener (GOPluginLoader *loader,
						  GOPluginService *service,
						  ErrorInfo **ret_error)
{
	GOPluginLoaderModule *loader_module = GO_PLUGIN_LOADER_MODULE (loader);
	gchar *func_name_file_probe, *func_name_file_open;
	gpointer module_func_file_probe = NULL, module_func_file_open = NULL;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FILE_OPENER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	func_name_file_probe = g_strconcat (
		plugin_service_get_id (service), "_file_probe", NULL);
	g_module_symbol (loader_module->handle, func_name_file_probe, &module_func_file_probe);
	func_name_file_open = g_strconcat (
		plugin_service_get_id (service), "_file_open", NULL);
	g_module_symbol (loader_module->handle, func_name_file_open, &module_func_file_open);
	if (module_func_file_open != NULL) {
		PluginServiceFileOpenerCallbacks *cbs;
		ServiceLoaderDataFileOpener *loader_data;

		cbs = plugin_service_get_cbs (service);
		cbs->plugin_func_file_probe = go_plugin_loader_module_func_file_probe;
		cbs->plugin_func_file_open = go_plugin_loader_module_func_file_open;

		loader_data = g_new (ServiceLoaderDataFileOpener, 1);
		loader_data->module_func_file_probe = module_func_file_probe;
		loader_data->module_func_file_open = module_func_file_open;
		g_object_set_data_full (
					G_OBJECT (service), "loader_data", loader_data, g_free);
	} else {
		*ret_error = error_info_new_printf (
			_("Module file \"%s\" has invalid format."),
			loader_module->module_file_name);
		error_info_add_details (*ret_error, error_info_new_printf (
			_("File doesn't contain \"%s\" function."), func_name_file_open));
	}
	g_free (func_name_file_probe);
	g_free (func_name_file_open);
}

/*
 * Service - file_saver
 */

typedef struct {
	void (*module_func_file_save) (GnmFileSaver const *fs, IOContext *io_context,
				       gconstpointer FIXME_FIXME_workbook_view,
				       GsfOutput *output);
} ServiceLoaderDataFileSaver;

static void
go_plugin_loader_module_func_file_save (GnmFileSaver const *fs, GOPluginService *service,
					 IOContext *io_context,
					 gconstpointer FIXME_FIXME_workbook_view,
					 GsfOutput *output)
{
	ServiceLoaderDataFileSaver *loader_data;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FILE_SAVER (service));
	g_return_if_fail (GSF_IS_OUTPUT (output));

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	loader_data->module_func_file_save (fs, io_context,
		FIXME_FIXME_workbook_view, output);
}

static void
go_plugin_loader_module_load_service_file_saver (GOPluginLoader *loader,
						 GOPluginService *service,
						 ErrorInfo **ret_error)
{
	GOPluginLoaderModule *loader_module = GO_PLUGIN_LOADER_MODULE (loader);
	gchar *func_name_file_save;
	gpointer module_func_file_save = NULL;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FILE_SAVER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	func_name_file_save = g_strconcat (
					   plugin_service_get_id (service), "_file_save", NULL);
	g_module_symbol (loader_module->handle, func_name_file_save, &module_func_file_save);
	if (module_func_file_save != NULL) {
		PluginServiceFileSaverCallbacks *cbs;
		ServiceLoaderDataFileSaver *loader_data;

		cbs = plugin_service_get_cbs (service);
		cbs->plugin_func_file_save = go_plugin_loader_module_func_file_save;

		loader_data = g_new (ServiceLoaderDataFileSaver, 1);
		loader_data->module_func_file_save = module_func_file_save;
		g_object_set_data_full (
					G_OBJECT (service), "loader_data", loader_data, g_free);
	} else {
		*ret_error = error_info_new_printf (
			_("Module file \"%s\" has invalid format."),
			loader_module->module_file_name);
		error_info_add_details (*ret_error, error_info_new_printf (
			_("File doesn't contain \"%s\" function."),
			func_name_file_save));
	}
	g_free (func_name_file_save);
}

/*
 * Service - plugin_loader
 */

typedef struct {
	GType (*module_func_get_loader_type) (ErrorInfo **ret_error);
} ServiceLoaderDataPluginLoader;

static GType
go_plugin_loader_module_func_get_loader_type (GOPluginService *service,
					       ErrorInfo **ret_error)
{
	ServiceLoaderDataPluginLoader *loader_data;
	ErrorInfo *error = NULL;
	GType loader_type;

	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE_PLUGIN_LOADER (service), 0);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	loader_type = loader_data->module_func_get_loader_type (&error);
	if (error == NULL) {
		return loader_type;
	} else {
		*ret_error = error;
		return (GType) 0;
	}
}

static void
go_plugin_loader_module_load_service_plugin_loader (GOPluginLoader *loader,
						    GOPluginService *service,
						    ErrorInfo **ret_error)
{
	GOPluginLoaderModule *loader_module = GO_PLUGIN_LOADER_MODULE (loader);
	gchar *func_name_get_loader_type;
	gpointer module_func_get_loader_type = NULL;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_PLUGIN_LOADER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	func_name_get_loader_type = g_strconcat (
		plugin_service_get_id (service), "_get_loader_type", NULL);
	g_module_symbol (loader_module->handle, func_name_get_loader_type,
			 &module_func_get_loader_type);
	if (module_func_get_loader_type != NULL) {
		PluginServicePluginLoaderCallbacks *cbs;
		ServiceLoaderDataPluginLoader *loader_data;

		cbs = plugin_service_get_cbs (service);
		cbs->plugin_func_get_loader_type = go_plugin_loader_module_func_get_loader_type;

		loader_data = g_new (ServiceLoaderDataPluginLoader, 1);
		loader_data->module_func_get_loader_type = module_func_get_loader_type;
		g_object_set_data_full (G_OBJECT (service),
			"loader_data", loader_data, g_free);
	} else
		*ret_error = error_info_new_printf (
			_("Module doesn't contain \"%s\" function."),
			func_name_get_loader_type);
	g_free (func_name_get_loader_type);
}
