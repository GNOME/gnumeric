/*
 * boot.c
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include "gnm-python.h"
#include "gnm-py-interpreter.h"
#include "python-loader.h"
#include "py-console.h"
#include <gnm-plugin.h>
#include <goffice/app/error-info.h>
#include <gnm-plugin.h>
#include <glib.h>

GNM_PLUGIN_MODULE_HEADER;

GType python_get_loader_type (ErrorInfo **ret_error);
G_MODULE_EXPORT GType
python_get_loader_type (ErrorInfo **ret_error)
{
	GO_INIT_RET_ERROR_INFO (ret_error);
	return TYPE_GNM_PYTHON_PLUGIN_LOADER;
}

ModulePluginUIActions const console_ui_actions[] = {
	{ "ShowConsole", show_python_console },
	{ NULL }
};

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	GTypeModule *module = go_plugin_get_type_module (plugin);
	gnm_py_interpreter_register_type (module);
	gnm_python_register_type (module);
	gnm_py_command_line_register_type (module);
	gnm_py_interpreter_selector_register_type (module);
	gnm_python_plugin_loader_register_type (module);
}
