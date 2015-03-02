#ifndef PLUGIN_GNM_PYTHON_H
#define PLUGIN_GNM_PYTHON_H

#include <glib.h>
#include <gnumeric.h>
#include "gnm-py-interpreter.h"

#define GNM_PYTHON_TYPE        (gnm_python_get_type ())
#define GNM_PYTHON(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PYTHON_TYPE, GnmPython))
#define GNM_IS_PYTHON(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PYTHON_TYPE))

GType gnm_python_get_type (void);
typedef struct _GnmPython GnmPython;

GnmPython        *gnm_python_object_get (GOErrorInfo **err);
GnmPyInterpreter *gnm_python_new_interpreter (GnmPython *gpy, GOPlugin *plugin);
void              gnm_python_destroy_interpreter (GnmPython *gpy, GnmPyInterpreter *interpreter);
GnmPyInterpreter *gnm_python_get_current_interpreter (GnmPython *gpy);
GnmPyInterpreter *gnm_python_get_default_interpreter (GnmPython *gpy);
GSList           *gnm_python_get_interpreters (GnmPython *gpy);
void              gnm_python_clear_error_if_needed (GnmPython *gpy);

void gnm_py_interpreter_register_type		(GTypeModule *plugin);
void gnm_python_register_type			(GTypeModule *plugin);
void gnm_py_command_line_register_type		(GTypeModule *plugin);
void gnm_py_interpreter_selector_register_type	(GTypeModule *plugin);
void gnm_python_plugin_loader_register_type	(GTypeModule *plugin);

#endif /* PLUGIN_GNM_PYTHON_H */
