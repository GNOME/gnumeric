#ifndef PLUGIN_PYTHON_LOADER_H
#define PLUGIN_PYTHON_LOADER_H

#include <glib.h>
#include <gnumeric.h>
#include <workbook-control-gui.h>

#define TYPE_GNUMERIC_PLUGIN_LOADER_PYTHON            (gnumeric_plugin_loader_python_get_type ())
#define GNUMERIC_PLUGIN_LOADER_PYTHON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNUMERIC_PLUGIN_LOADER_PYTHON, GnumericPluginLoaderPython))
#define IS_GNUMERIC_PLUGIN_LOADER_PYTHON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNUMERIC_PLUGIN_LOADER_PYTHON))

GType gnumeric_plugin_loader_python_get_type (void);

#endif /* PLUGIN_PYTHON_LOADER_H */
