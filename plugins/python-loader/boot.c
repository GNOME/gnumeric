/*
 * boot.c
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <glib.h>
#include <module-plugin-defs.h>
#include "gnm-python.h"
#include "gnm-py-interpreter.h"
#include "python-loader.h"
#include "py-console.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

GtkType python_get_loader_type (ErrorInfo **ret_error);


GType
python_get_loader_type (ErrorInfo **ret_error)
{
	GNM_INIT_RET_ERROR_INFO (ret_error);
	return TYPE_GNUMERIC_PLUGIN_LOADER_PYTHON;
}

const ModulePluginUIVerbInfo console_ui_verbs[] = {
	{"ShowConsole", show_python_console},
	{NULL}
};

void
plugin_init (void)
{
	/* when loading previously loaded plugin we must re-register
	   all dynamic types */
	(void) gnm_py_interpreter_get_type ();
	(void) gnm_python_get_type ();
	(void) gnumeric_plugin_loader_python_get_type ();
}
