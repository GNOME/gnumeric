#ifndef GNUMERIC_PLUGIN_LOADER_H
#define GNUMERIC_PLUGIN_LOADER_H

#include <glib.h>
#include <gtk/gtkobject.h>
#include <libxml/tree.h>
#include "gnumeric.h"
#include "error-info.h"
#include "plugin.h"

#define TYPE_GNUMERIC_PLUGIN_LOADER             (gnumeric_plugin_loader_get_type ())
#define GNUMERIC_PLUGIN_LOADER(obj)             (GTK_CHECK_CAST ((obj), TYPE_GNUMERIC_PLUGIN_LOADER, GnumericPluginLoader))
#define GNUMERIC_PLUGIN_LOADER_CLASS(klass)     (GTK_CHECK_CLASS_CAST ((klass), TYPE_GNUMERIC_PLUGIN_LOADER, GnumericPluginLoaderClass))
#define IS_GNUMERIC_PLUGIN_LOADER(obj)          (GTK_CHECK_TYPE ((obj), TYPE_GNUMERIC_PLUGIN_LOADER))
#define IS_GNUMERIC_PLUGIN_LOADER_CLASS(klass)  (GTK_CHECK_CLASS_TYPE ((klass), TYPE_GNUMERIC_PLUGIN_LOADER))

typedef struct _GnumericPluginLoader GnumericPluginLoader;
typedef struct _GnumericPluginLoaderClass GnumericPluginLoaderClass;

struct _GnumericPluginLoader {
	GtkObject object;

	PluginInfo *plugin;
	gboolean is_loaded;
	gint n_loaded_services;
};

struct _GnumericPluginLoaderClass {
	GtkObjectClass parent_class;

	void (*set_attributes) (GnumericPluginLoader *loader, GList *attr_names, GList *attr_values, ErrorInfo **ret_error);
	void (*load) (GnumericPluginLoader *loader, ErrorInfo **ret_error);
	void (*unload) (GnumericPluginLoader *loader, ErrorInfo **ret_error);
	void (*load_service_general) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*unload_service_general) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*load_service_file_opener) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*unload_service_file_opener) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*load_service_file_saver) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*unload_service_file_saver) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*load_service_function_group) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*unload_service_function_group) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*load_service_plugin_loader) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*unload_service_plugin_loader) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	gint (*get_extra_info_list) (GnumericPluginLoader *loader, GList **ret_keys_list, GList **ret_values_list);
};

GtkType gnumeric_plugin_loader_get_type (void);

void gnumeric_plugin_loader_set_attributes (GnumericPluginLoader *loader,
                                            GList *attr_names, GList *attr_values,
                                            ErrorInfo **ret_error);
void gnumeric_plugin_loader_set_plugin (GnumericPluginLoader *loader, PluginInfo *plugin);
void gnumeric_plugin_loader_load (GnumericPluginLoader *loader, ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload (GnumericPluginLoader *loader, ErrorInfo **ret_error);
void gnumeric_plugin_loader_load_service (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload_service (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
gint gnumeric_plugin_loader_get_extra_info_list (GnumericPluginLoader *loader, GList **ret_keys_list, GList **ret_values_list);
gboolean gnumeric_plugin_loader_is_loaded (GnumericPluginLoader *loader);

#endif /* GNUMERIC_PLUGIN_LOADER_H */
