#ifndef GNUMERIC_PLUGIN_LOADER_H
#define GNUMERIC_PLUGIN_LOADER_H

#include <glib.h>
#include <glib-object.h>
#include <libxml/tree.h>
#include "gnumeric.h"
#include "error-info.h"
#include "plugin.h"

#define TYPE_GNUMERIC_PLUGIN_LOADER             (gnumeric_plugin_loader_get_type ())
#define GNUMERIC_PLUGIN_LOADER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNUMERIC_PLUGIN_LOADER, GnumericPluginLoader))
#define GNUMERIC_PLUGIN_LOADER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GNUMERIC_PLUGIN_LOADER, GnumericPluginLoaderClass))
#define IS_GNUMERIC_PLUGIN_LOADER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNUMERIC_PLUGIN_LOADER))
#define IS_GNUMERIC_PLUGIN_LOADER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_GNUMERIC_PLUGIN_LOADER))

typedef struct _GnumericPluginLoaderClass GnumericPluginLoaderClass;

struct _GnumericPluginLoader {
	GObject object;

	GnmPlugin *plugin;
	gboolean is_base_loaded;
	gint n_loaded_services;
};

struct _GnumericPluginLoaderClass {
	GObjectClass parent_class;

	void (*set_attributes) (GnumericPluginLoader *loader, GHashTable *attrs, ErrorInfo **ret_error);
	void (*load_base) (GnumericPluginLoader *loader, ErrorInfo **ret_error);
	void (*unload_base) (GnumericPluginLoader *loader, ErrorInfo **ret_error);
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
	void (*load_service_ui) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
	void (*unload_service_ui) (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
};

GType gnumeric_plugin_loader_get_type (void);

void gnumeric_plugin_loader_set_attributes (GnumericPluginLoader *loader,
                                            GHashTable *attrs,
                                            ErrorInfo **ret_error);
void gnumeric_plugin_loader_set_plugin (GnumericPluginLoader *loader, GnmPlugin *plugin);
void gnumeric_plugin_loader_load_base (GnumericPluginLoader *loader, ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload_base (GnumericPluginLoader *loader, ErrorInfo **ret_error);
void gnumeric_plugin_loader_load_service (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
void gnumeric_plugin_loader_unload_service (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
gboolean gnumeric_plugin_loader_is_base_loaded (GnumericPluginLoader *loader);

#endif /* GNUMERIC_PLUGIN_LOADER_H */
