/*
 * boot.c
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <glib.h>
#include <module-plugin-defs.h>
#include <error-info.h>
#include "gnm-python.h"
#include "gnm-py-interpreter.h"
#include "python-loader.h"
#include "py-console.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

GType python_get_loader_type (ErrorInfo **ret_error);


GType
python_get_loader_type (ErrorInfo **ret_error)
{
	GNM_INIT_RET_ERROR_INFO (ret_error);
	return TYPE_GNM_PLUGIN_LOADER_PYTHON;
}

ModulePluginUIActions const console_ui_actions[] = {
	{ "ShowConsole", show_python_console },
	{ NULL }
};

void
plugin_init (void)
{
	/* when loading previously loaded plugin we must re-register
	   all dynamic types */
	(void) gnm_py_interpreter_get_type ();
	(void) gnm_python_get_type ();
	(void) gnm_plugin_loader_python_get_type ();
}
