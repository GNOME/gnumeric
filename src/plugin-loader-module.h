#ifndef GNUMERIC_PLUGIN_LOADER_MODULE_H
#define GNUMERIC_PLUGIN_LOADER_MODULE_H

#include <glib.h>
#include <libxml/tree.h>
#include "func.h"
#include "error-info.h"
#include "plugin.h"
#include "plugin-loader.h"

#define TYPE_GNUMERIC_PLUGIN_LOADER_MODULE            (gnumeric_plugin_loader_module_get_type ())
#define GNUMERIC_PLUGIN_LOADER_MODULE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE, GnumericPluginLoaderModule))
#define GNUMERIC_PLUGIN_LOADER_MODULE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE, GnumericPluginLoaderModuleClass))
#define IS_GNUMERIC_PLUGIN_LOADER_MODULE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE))
#define IS_GNUMERIC_PLUGIN_LOADER_MODULE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_GNUMERIC_PLUGIN_LOADER_MODULE))

typedef struct _GnumericPluginLoaderModule GnumericPluginLoaderModule;
typedef struct _GnumericPluginLoaderModuleClass GnumericPluginLoaderModuleClass;

GType gnumeric_plugin_loader_module_get_type (void);

/* All fields in this struct are PRIVATE. */
typedef struct {
	guint32 magic_number;
	gchar gnumeric_plugin_version[64];
	PluginInfo *pinfo;
} ModulePluginFileStruct;

#define GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER             0x476e756d
#define GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER  {GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER, GNUMERIC_VERSION}

typedef struct {
	gchar const *fn_name;
	gchar const *args;
	gchar const *arg_names;
	gchar const **help;
	FunctionArgs *fn_args;
	FunctionNodes *fn_nodes;
} ModulePluginFunctionInfo;

#endif /* GNUMERIC_PLUGIN_LOADER_MODULE_H */
