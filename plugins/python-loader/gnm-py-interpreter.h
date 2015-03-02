#ifndef PLUGIN_GNM_PY_INTERPRETER_H
#define PLUGIN_GNM_PY_INTERPRETER_H

#include <glib.h>
#include <gnumeric.h>
#include "python-loader.h"

#define GNM_PY_INTERPRETER_TYPE     (gnm_py_interpreter_get_type ())
#define GNM_PY_INTERPRETER(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PY_INTERPRETER_TYPE, GnmPyInterpreter))
#define GNM_IS_PY_INTERPRETER(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PY_INTERPRETER_TYPE))

GType gnm_py_interpreter_get_type (void);
typedef struct _GnmPyInterpreter GnmPyInterpreter;

GnmPyInterpreter *gnm_py_interpreter_new	(GOPlugin *plugin);
void              gnm_py_interpreter_destroy	(GnmPyInterpreter *interpreter,
						 GnmPyInterpreter *new_interpreter);
void              gnm_py_interpreter_switch_to	(GnmPyInterpreter *interpreter);
void              gnm_py_interpreter_run_string (GnmPyInterpreter *interpreter,
                                                 char const *str,
						 char **opt_stdout, char **opt_stderr);
char const       *gnm_py_interpreter_get_name	(GnmPyInterpreter *interpreter);
GOPlugin        *gnm_py_interpreter_get_plugin	(GnmPyInterpreter *interpreter);
int               gnm_py_interpreter_compare	(gconstpointer a, gconstpointer b);

#endif /* PLUGIN_GNM_PY_INTERPRETER_H */
