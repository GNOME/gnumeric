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

GnmPython        *gnm_python_object_get (void);
void              gnm_python_object_shutdown (void);
GnmPyInterpreter *gnm_python_new_interpreter (GnmPlugin *plugin);
void              gnm_python_destroy_interpreter (GnmPyInterpreter *interpreter);
GnmPyInterpreter *gnm_python_get_current_interpreter (void);
GnmPyInterpreter *gnm_python_get_default_interpreter (void);
GSList           *gnm_python_get_interpreters (void);
void              gnm_python_clear_error_if_needed (void);

#endif /* PLUGIN_GNM_PYTHON_H */
