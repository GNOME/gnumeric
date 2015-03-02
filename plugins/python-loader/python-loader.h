#ifndef GNM_PYTHON_PLUGIN_LOADER_H
#define GNM_PYTHON_PLUGIN_LOADER_H

#include <glib-object.h>

#define TYPE_GNM_PYTHON_PLUGIN_LOADER	(gnm_python_plugin_loader_get_type ())
#define GNM_PYTHON_PLUGIN_LOADER(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_GNM_PYTHON_PLUGIN_LOADER, GnmPythonPluginLoader))
#define GNM_IS_PYTHON_PLUGIN_LOADER(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_GNM_PYTHON_PLUGIN_LOADER))

GType gnm_python_plugin_loader_get_type (void);

#endif /* GNM_PYTHON_PLUGIN_LOADER_H */
