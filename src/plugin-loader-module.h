#ifndef GNUMERIC_PLUGIN_LOADER_MODULE_H
#define GNUMERIC_PLUGIN_LOADER_MODULE_H

#include <glib-object.h>

#define TYPE_GNUMERIC_PLUGIN_LOADER_MODULE            (gnumeric_plugin_loader_module_get_type ())
#define GNUMERIC_PLUGIN_LOADER_MODULE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE, GnumericPluginLoaderModule))
#define GNUMERIC_PLUGIN_LOADER_MODULE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE, GnumericPluginLoaderModuleClass))
#define IS_GNUMERIC_PLUGIN_LOADER_MODULE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE))
#define IS_GNUMERIC_PLUGIN_LOADER_MODULE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE))

typedef struct _GnumericPluginLoaderModule GnumericPluginLoaderModule;
typedef struct _GnumericPluginLoaderModuleClass GnumericPluginLoaderModuleClass;

GType gnumeric_plugin_loader_module_get_type (void);

#endif /* GNUMERIC_PLUGIN_LOADER_MODULE_H */
