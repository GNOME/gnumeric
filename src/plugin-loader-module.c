/*
 * plugin-loader-module.c: Support for "g_module" (shared libraries) plugins.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <config.h>
#include <glib.h>
#include <libgnome/libgnome.h>
#include <gal/util/e-xml-utils.h>
#include <gal/util/e-util.h>
#include <gnome-xml/parser.h>
#include <gnome-xml/parserInternals.h>
#include <gnome-xml/xmlmemory.h>
#include "gnumeric.h"
#include "file.h"
#include "plugin.h"
#include "plugin-service.h"
#include "plugin-loader.h"
#include "plugin-loader-module.h"

struct _GnumericPluginLoaderModule {
	GnumericPluginLoader loader;

	gchar *module_file_name;

	GModule *handle;
	void (*plugin_init_func) (void);
	void (*plugin_cleanup_func) (void);
};

struct _GnumericPluginLoaderModuleClass {
	GnumericPluginLoaderClass parent_class;
};

#define PARENT_TYPE (gnumeric_plugin_loader_get_type ())

static GnumericPluginLoaderClass *parent_class = NULL;

static void gnumeric_plugin_loader_module_set_attributes (GnumericPluginLoader *loader, GList *attr_names, GList *attr_values, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_module_load (GnumericPluginLoader *loader, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_module_unload (GnumericPluginLoader *loader, ErrorInfo **ret_error);

static void gnumeric_plugin_loader_module_load_service_general (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_module_load_service_file_opener (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_module_load_service_file_saver (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_module_load_service_function_group (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_module_unload_service_function_group (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_module_load_service_plugin_loader (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);


static void
gnumeric_plugin_loader_module_set_attributes (GnumericPluginLoader *loader,
                                              GList *attr_names, GList *attr_values,
                                              ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;
	gchar *module_file_name = NULL;
	GList *ln, *lv;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	for (ln = attr_names, lv = attr_values;
	     ln != NULL && lv != NULL;
	     ln = ln->next, lv = lv->next) {
		gchar *name, *value;

		name = (gchar *) ln->data;
		value = (gchar *) lv->data;
		if (strcmp ((gchar *) ln->data, "module_file") == 0) {
			module_file_name = (gchar *) lv->data;
		}
	}

	if (module_file_name != NULL) {
		loader_module->module_file_name = g_strdup (module_file_name);
	} else {
		*ret_error = error_info_new_str (
		             _("Module file name not given."));
	}
}

static void
gnumeric_plugin_loader_module_load (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	if (g_module_supported ()) {
		gchar *full_module_file_name;
		GModule *handle;
		ModulePluginFileStruct *plugin_file_struct = NULL;
		gpointer plugin_init_func = NULL, plugin_cleanup_func = NULL;

		full_module_file_name = g_concat_dir_and_file (plugin_info_peek_dir_name (loader->plugin),
		                                               loader_module->module_file_name);
		handle = g_module_open (full_module_file_name, 0);
		if (handle != NULL) {
			g_module_symbol (handle, "plugin_file_struct", (gpointer) &plugin_file_struct);
			g_module_symbol (handle, "plugin_init", &plugin_init_func);
			g_module_symbol (handle, "plugin_cleanup", &plugin_cleanup_func);
		}
		if (handle != NULL && plugin_file_struct != NULL &&
		    plugin_file_struct->magic_number == GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER &&
		    strcmp (plugin_file_struct->gnumeric_plugin_version, GNUMERIC_VERSION) == 0) {
			loader_module->handle = handle;
			loader_module->plugin_init_func = plugin_init_func;
			loader_module->plugin_cleanup_func = plugin_cleanup_func;
			plugin_file_struct->pinfo = loader->plugin;
			if (loader_module->plugin_init_func != NULL) {
				loader_module->plugin_init_func ();
			}
		} else {
			if (handle == NULL) {
				*ret_error = error_info_new_printf (
				             _("Unable to open module file \"%s\": %s"),
				             full_module_file_name, g_module_error());
			} else {
				*ret_error = error_info_new_printf (
				             _("Module file \"%s\" has invalid format."),
				             full_module_file_name);
				if (plugin_file_struct == NULL) {
					error_info_add_details (*ret_error,
					                        error_info_new_str (
					                        _("File doesn't contain (\"plugin_file_struct\" symbol).")));
				} else if (plugin_file_struct->magic_number != GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER) {
 					error_info_add_details (*ret_error,
 					                        error_info_new_str (
					                        _("File has a bad magic number.")));
				} else if (strcmp (plugin_file_struct->gnumeric_plugin_version, GNUMERIC_VERSION) == 0) {
					error_info_add_details (*ret_error,
					                        error_info_new_printf (
					                        _("Plugin version \"%s\" is different from application \"%s\"."),
					                        plugin_file_struct->gnumeric_plugin_version, GNUMERIC_VERSION));
 				}
				g_module_close (handle);
			}
		}
		g_free (full_module_file_name);
	} else {
		*ret_error = error_info_new_str (
		             _("Dynamic module loading is not supported in this system."));
	}
}

static void
gnumeric_plugin_loader_module_unload (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	if (loader_module->plugin_cleanup_func != NULL) {
		loader_module->plugin_cleanup_func ();
	}
	if (!g_module_close (loader_module->handle)) {
		*ret_error = error_info_new_printf (
		             _("Unable to close module file \"%s\": %s"),
		             loader_module->module_file_name, g_module_error());
	}
	loader_module->handle = NULL;
	loader_module->plugin_init_func = NULL;
	loader_module->plugin_cleanup_func = NULL;
}

static gint
gnumeric_plugin_loader_module_info_get_extra_info_list (GnumericPluginLoader *loader,
                                                        GList **ret_keys_list,
                                                        GList **ret_values_list)
{
	GnumericPluginLoaderModule *loader_module;
	GList *keys_list = NULL, *values_list = NULL;
	gint n_items = 0;

	g_return_val_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader), 0);
	g_return_val_if_fail (ret_keys_list != NULL && ret_values_list != NULL, 0);

	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	keys_list = g_list_prepend (keys_list, g_strdup (_("Loader")));
	values_list = g_list_prepend (values_list, g_strdup ("g_module"));
	n_items++;
	keys_list = g_list_prepend (keys_list, g_strdup (_("Module file name")));
	values_list = g_list_prepend (values_list, g_strdup (loader_module->module_file_name));
	n_items++;

	*ret_keys_list = g_list_reverse (keys_list);
	*ret_values_list = g_list_reverse (values_list);

	return n_items;
}

static void
gnumeric_plugin_loader_module_init (GnumericPluginLoaderModule *loader_module)
{
	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader_module));

	loader_module->module_file_name = NULL;
	loader_module->handle = NULL;
}

static void
gnumeric_plugin_loader_module_destroy (GtkObject *obj)
{
	GnumericPluginLoaderModule *loader_module;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (obj));

	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (obj);
	g_free (loader_module->module_file_name);
}

static void
gnumeric_plugin_loader_module_class_init (GnumericPluginLoaderModuleClass *klass)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class =  GNUMERIC_PLUGIN_LOADER_CLASS (klass);
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (klass);

	parent_class = gtk_type_class (PARENT_TYPE);

	gnumeric_plugin_loader_class->set_attributes = gnumeric_plugin_loader_module_set_attributes;
	gnumeric_plugin_loader_class->load = gnumeric_plugin_loader_module_load;
	gnumeric_plugin_loader_class->unload = gnumeric_plugin_loader_module_unload;
	gnumeric_plugin_loader_class->load_service_general = gnumeric_plugin_loader_module_load_service_general;
	gnumeric_plugin_loader_class->load_service_file_opener = gnumeric_plugin_loader_module_load_service_file_opener;
	gnumeric_plugin_loader_class->load_service_file_saver = gnumeric_plugin_loader_module_load_service_file_saver;
	gnumeric_plugin_loader_class->load_service_function_group = gnumeric_plugin_loader_module_load_service_function_group;
	gnumeric_plugin_loader_class->unload_service_function_group = gnumeric_plugin_loader_module_unload_service_function_group;
	gnumeric_plugin_loader_class->load_service_plugin_loader = gnumeric_plugin_loader_module_load_service_plugin_loader;
	gnumeric_plugin_loader_class->get_extra_info_list = gnumeric_plugin_loader_module_info_get_extra_info_list;

	gtk_object_class->destroy = gnumeric_plugin_loader_module_destroy;
}

E_MAKE_TYPE (gnumeric_plugin_loader_module, "GnumericPluginLoaderModule", GnumericPluginLoaderModule, &gnumeric_plugin_loader_module_class_init, gnumeric_plugin_loader_module_init, PARENT_TYPE)

/*
 * Service - general
 */

typedef struct {
	void (*module_func_init) (ErrorInfo **ret_error);
	gboolean (*module_func_can_deactivate) (void);
	void (*module_func_cleanup) (ErrorInfo **ret_error);
} ServiceLoaderDataGeneral;

static void
gnumeric_plugin_loader_module_func_init (PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;
	ServiceLoaderDataGeneral *loader_data;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_data = (ServiceLoaderDataGeneral *) plugin_service_get_loader_data (service);
	loader_data->module_func_init (&error);
	*ret_error = error;
}

static gboolean
gnumeric_plugin_loader_module_func_can_deactivate (PluginService *service)
{
	ServiceLoaderDataGeneral *loader_data;

	g_return_val_if_fail (service != NULL, FALSE);

	loader_data = (ServiceLoaderDataGeneral *) plugin_service_get_loader_data (service);
	return loader_data->module_func_can_deactivate ();
}

static void
gnumeric_plugin_loader_module_func_cleanup (PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;
	ServiceLoaderDataGeneral *loader_data;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_data = (ServiceLoaderDataGeneral *) plugin_service_get_loader_data (service);
	loader_data->module_func_cleanup (&error);
	*ret_error = error;
}

static void
gnumeric_plugin_loader_module_load_service_general (GnumericPluginLoader *loader,
                                                    PluginService *service,
                                                    ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;
	gpointer module_func_init = NULL, module_func_cleanup = NULL, module_func_can_deactivate = NULL;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	g_module_symbol (loader_module->handle, "plugin_init_general", &module_func_init);
	g_module_symbol (loader_module->handle, "plugin_cleanup_general", &module_func_cleanup);
	g_module_symbol (loader_module->handle, "plugin_can_deactivate_general", &module_func_can_deactivate);
	if (module_func_init != NULL && module_func_cleanup != NULL && module_func_can_deactivate != NULL) {
		PluginServiceGeneral *service_general;
		ServiceLoaderDataGeneral *loader_data;

		service_general = &service->t.general;
		service_general->plugin_func_init = gnumeric_plugin_loader_module_func_init;
		service_general->plugin_func_can_deactivate = gnumeric_plugin_loader_module_func_can_deactivate;
		service_general->plugin_func_cleanup = gnumeric_plugin_loader_module_func_cleanup;

		loader_data = g_new (ServiceLoaderDataGeneral, 1);
		loader_data->module_func_init = module_func_init;
		loader_data->module_func_cleanup = module_func_cleanup;
		loader_data->module_func_can_deactivate = module_func_can_deactivate;
		plugin_service_set_loader_data (service, (gpointer) loader_data);
	} else {
		*ret_error = error_info_new_printf (
		             _("Module file \"%s\" has invalid format."),
		             loader_module->module_file_name);
		if (module_func_init == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_str (
			                        _("File doesn't contain \"plugin_init_general\" function.")));
		}
		if (module_func_cleanup == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_str (
			                        _("File doesn't contain \"plugin_cleanup_general\" function.")));
		}
		if (module_func_can_deactivate == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_str (
			                        _("File doesn't contain \"plugin_can_deactivate_general\" function.")));
		}
	}
}

/*
 * Service - file_opener
 */

typedef struct {
	gboolean (*module_func_file_probe) (GnumFileOpener const *fo, const gchar *file_name,
	                                    FileProbeLevel pl);
	void (*module_func_file_open) (GnumFileOpener const *fo, IOContext *io_context,
	                               WorkbookView *wbv, const gchar *filename);
} ServiceLoaderDataFileOpener;

static gboolean
gnumeric_plugin_loader_module_func_file_probe (GnumFileOpener const *fo, PluginService *service,
                                               const gchar *file_name, FileProbeLevel pl)
{
	ServiceLoaderDataFileOpener *loader_data;

	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);

	loader_data = (ServiceLoaderDataFileOpener *) plugin_service_get_loader_data (service);
	return loader_data->module_func_file_probe (fo, file_name, pl);
}

static void
gnumeric_plugin_loader_module_func_file_open (GnumFileOpener const *fo, PluginService *service,
                                              IOContext *io_context, WorkbookView *wbv,
                                              const gchar *file_name)
{
	ServiceLoaderDataFileOpener *loader_data;

	g_return_if_fail (service != NULL);
	g_return_if_fail (file_name != NULL);

	loader_data = (ServiceLoaderDataFileOpener *) plugin_service_get_loader_data (service);
	loader_data->module_func_file_open (fo, io_context, wbv, file_name);
}

static void
gnumeric_plugin_loader_module_load_service_file_opener (GnumericPluginLoader *loader,
                                                        PluginService *service,
                                                        ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;
	PluginServiceFileOpener *service_file_opener;
	gchar *func_name_file_probe, *func_name_file_open;
	gpointer module_func_file_probe = NULL, module_func_file_open = NULL;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	service_file_opener = &service->t.file_opener;
	if (service_file_opener->has_probe) {
		func_name_file_probe = g_strdup_printf ("%s_file_probe", service_file_opener->id);
		g_module_symbol (loader_module->handle, func_name_file_probe, &module_func_file_probe);
	} else {
		func_name_file_probe = NULL;
		module_func_file_probe = NULL;
	}
	func_name_file_open = g_strdup_printf ("%s_file_open", service_file_opener->id);
	g_module_symbol (loader_module->handle, func_name_file_open, &module_func_file_open);
	if ((!service_file_opener->has_probe || module_func_file_probe != NULL) &&
	    module_func_file_open != NULL) {
		ServiceLoaderDataFileOpener *loader_data;

		if (service_file_opener->has_probe) {
			service_file_opener->plugin_func_file_probe = gnumeric_plugin_loader_module_func_file_probe;
		} else {
			service_file_opener->plugin_func_file_probe = NULL;
		}
		service_file_opener->plugin_func_file_open = gnumeric_plugin_loader_module_func_file_open;

		loader_data = g_new (ServiceLoaderDataFileOpener, 1);
		loader_data->module_func_file_probe = module_func_file_probe;
		loader_data->module_func_file_open = module_func_file_open;
		plugin_service_set_loader_data (service, (gpointer) loader_data);
	} else {
		*ret_error = error_info_new_printf (
		             _("Module file \"%s\" has invalid format."),
		             loader_module->module_file_name);
		if (service_file_opener->has_probe && module_func_file_probe == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("File doesn't contain \"%s\" function."),
			                        func_name_file_probe));
		}
		if (module_func_file_open == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("File doesn't contain \"%s\" function."),
			                        func_name_file_open));
		}
	}
	g_free (func_name_file_probe);
	g_free (func_name_file_open);
}

/*
 * Service - file_saver
 */

typedef struct {
	void (*module_func_file_save) (GnumFileSaver const *fs, IOContext *io_context,
	                               WorkbookView *wbv, const gchar *filename);
} ServiceLoaderDataFileSaver;

static void
gnumeric_plugin_loader_module_func_file_save (GnumFileSaver const *fs, PluginService *service,
                                              IOContext *io_context, WorkbookView *wbv,
                                              const gchar *file_name)
{
	ServiceLoaderDataFileSaver *loader_data;

	g_return_if_fail (service != NULL);
	g_return_if_fail (file_name != NULL);

	loader_data = (ServiceLoaderDataFileSaver *) plugin_service_get_loader_data (service);
	loader_data->module_func_file_save (fs, io_context, wbv, file_name);
}

static void
gnumeric_plugin_loader_module_load_service_file_saver (GnumericPluginLoader *loader,
                                                       PluginService *service,
                                                       ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;
	PluginServiceFileSaver *service_file_saver;
	gchar *func_name_file_save;
	gpointer module_func_file_save = NULL;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	service_file_saver = &service->t.file_saver;
	func_name_file_save = g_strdup_printf ("%s_file_save", service_file_saver->id);
	g_module_symbol (loader_module->handle, func_name_file_save, &module_func_file_save);
	if (module_func_file_save != NULL) {
		ServiceLoaderDataFileSaver *loader_data;

		service_file_saver->plugin_func_file_save = gnumeric_plugin_loader_module_func_file_save;

		loader_data = g_new (ServiceLoaderDataFileSaver, 1);
		loader_data->module_func_file_save = module_func_file_save;
		plugin_service_set_loader_data (service, (gpointer) loader_data);
	} else {
		*ret_error = error_info_new_printf (
		             _("Module file \"%s\" has invalid format."),
		             loader_module->module_file_name);
		if (module_func_file_save == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("File doesn't contain \"%s\" function."),
			                        func_name_file_save));
		}
	}
	g_free (func_name_file_save);
}

/*
 * Service - function_group
 */

typedef struct {
	ModulePluginFunctionInfo *module_fn_info_array;
	GHashTable *function_indices;
} ServiceLoaderDataFunctionGroup;

static gboolean
gnumeric_plugin_loader_module_func_get_full_function_info (PluginService *service,
                                                           gchar const *fn_name,
                                                           gchar **args_ptr,
                                                           gchar **arg_names_ptr,
                                                           gchar ***help_ptr,
                                                           FunctionArgs **fn_args_ptr,
                                                           FunctionNodes **fn_nodes_ptr)
{
	ServiceLoaderDataFunctionGroup *loader_data;
	gpointer func_index_ptr;

	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (fn_name != NULL, FALSE);

	loader_data = (ServiceLoaderDataFunctionGroup *) plugin_service_get_loader_data (service);
	if (g_hash_table_lookup_extended (loader_data->function_indices, (gpointer) fn_name,
	                                  NULL, &func_index_ptr)) {
		ModulePluginFunctionInfo *fn_info;

		fn_info = &loader_data->module_fn_info_array[GPOINTER_TO_INT (func_index_ptr)];
		*args_ptr = fn_info->args;
		*arg_names_ptr = fn_info->arg_names;
		*help_ptr = fn_info->help;
		*fn_args_ptr = fn_info->fn_args;
		*fn_nodes_ptr = fn_info->fn_nodes;
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
gnumeric_plugin_loader_module_load_service_function_group (GnumericPluginLoader *loader,
                                                           PluginService *service,
                                                           ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;
	PluginServiceFunctionGroup *service_function_group;
	gchar *fn_info_array_name;
	ModulePluginFunctionInfo *module_fn_info_array = NULL;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	service_function_group = &service->t.function_group;
	fn_info_array_name = g_strdup_printf ("%s_functions", service_function_group->group_id);
	g_module_symbol (loader_module->handle, fn_info_array_name, (gpointer) &module_fn_info_array);
	if (module_fn_info_array != NULL) {
		ServiceLoaderDataFunctionGroup *loader_data;
		gint i;

		service_function_group->plugin_func_get_full_function_info = &gnumeric_plugin_loader_module_func_get_full_function_info;

		loader_data = g_new (ServiceLoaderDataFunctionGroup, 1);
		loader_data->module_fn_info_array = module_fn_info_array;
		loader_data->function_indices = g_hash_table_new (&g_str_hash, &g_str_equal);
		for (i = 0; module_fn_info_array[i].fn_name != NULL; i++) {
			g_hash_table_insert (loader_data->function_indices,
			                     (gpointer) module_fn_info_array[i].fn_name,
			                     GINT_TO_POINTER (i));
		}
		plugin_service_set_loader_data (service, (gpointer) loader_data);
	} else {
		*ret_error = error_info_new_printf (
		             _("Module file \"%s\" has invalid format."),
		             loader_module->module_file_name);
		if (module_fn_info_array == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("File doesn't contain \"%s\" array."),
			                        fn_info_array_name));
		}
	}
	g_free (fn_info_array_name);
}

static void
gnumeric_plugin_loader_module_unload_service_function_group (GnumericPluginLoader *loader,
                                                             PluginService *service,
                                                             ErrorInfo **ret_error)
{
	ServiceLoaderDataFunctionGroup *loader_data;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_data = (ServiceLoaderDataFunctionGroup *) plugin_service_get_loader_data (service);
	g_hash_table_destroy (loader_data->function_indices);
	parent_class->unload_service_function_group (loader, service, ret_error);
}

/*
 * Service - plugin_loader
 */

typedef struct {
	GtkType (*module_func_get_loader_type) (ErrorInfo **ret_error);
} ServiceLoaderDataPluginLoader;

static GtkType
gnumeric_plugin_loader_module_func_get_loader_type (PluginService *service,
                                                    ErrorInfo **ret_error)
{
	ServiceLoaderDataPluginLoader *loader_data;
	ErrorInfo *error;
	GtkType loader_type;

	g_return_val_if_fail (service != NULL, 0);
	g_return_val_if_fail (ret_error != NULL, 0);

	*ret_error = NULL;
	loader_data = (ServiceLoaderDataPluginLoader *) plugin_service_get_loader_data (service);
	loader_type = loader_data->module_func_get_loader_type (&error);
	if (error == NULL) {
		return loader_type;
	} else {
		*ret_error = error;
		return (GtkType) 0;
	}
}

static void
gnumeric_plugin_loader_module_load_service_plugin_loader (GnumericPluginLoader *loader,
                                                          PluginService *service,
                                                          ErrorInfo **ret_error)
{
	GnumericPluginLoaderModule *loader_module;
	PluginServicePluginLoader *service_plugin_loader;
	gchar *func_name_get_loader_type;
	gpointer module_func_get_loader_type = NULL;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_MODULE (loader));
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	loader_module = GNUMERIC_PLUGIN_LOADER_MODULE (loader);
	service_plugin_loader = &service->t.plugin_loader;
	func_name_get_loader_type = g_strdup_printf ("%s_get_loader_type",
	                                             service_plugin_loader->loader_id);
	g_module_symbol (loader_module->handle, func_name_get_loader_type,
	                 &module_func_get_loader_type);
	if (module_func_get_loader_type != NULL) {
		ServiceLoaderDataPluginLoader *loader_data;

		service_plugin_loader->plugin_func_get_loader_type = gnumeric_plugin_loader_module_func_get_loader_type;

		loader_data = g_new (ServiceLoaderDataPluginLoader, 1);
		loader_data->module_func_get_loader_type = module_func_get_loader_type;
		plugin_service_set_loader_data (service, (gpointer) loader_data);
	} else {
		if (module_func_get_loader_type == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("Module doesn't contain \"%s\" function."),
			                        func_name_get_loader_type));
		}
	}
	g_free (func_name_get_loader_type);
}
