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
#include <goffice/app/module-plugin-defs.h>
#include <glib.h>

GOFFICE_MODULE_PLUGIN_INFO_DECL(GNUMERIC_VERSION);

GType python_get_loader_type (ErrorInfo **ret_error);
G_MODULE_EXPORT GType
python_get_loader_type (ErrorInfo **ret_error)
{
	GNM_INIT_RET_ERROR_INFO (ret_error);
	return TYPE_GNM_PYTHON_PLUGIN_LOADER;
}

ModulePluginUIActions const console_ui_actions[] = {
	{ "ShowConsole", show_python_console },
	{ NULL }
};

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	/* when loading previously loaded plugin we must re-register
	   all dynamic types */
	(void) gnm_py_interpreter_get_type ();
	(void) gnm_python_get_type ();
	(void) gnm_python_plugin_loader_get_type ();
}
