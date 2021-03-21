#ifndef PLUGIN_PY_GNUMERIC_H
#define PLUGIN_PY_GNUMERIC_H

#include <Python.h>
#include <glib.h>
#include <goffice/goffice.h>
#include "gnm-py-interpreter.h"
#include <gnumeric-fwd.h>

PyObject *py_initgnumeric (void);
void py_gnumeric_shutdown (void);

void      py_gnumeric_add_plugin (PyObject *module, GnmPyInterpreter *interpreter);

GnmValue *call_python_function	 (PyObject *python_fn,
				  GnmEvalPos const *eval_pos,
				  gint n_args, GnmValue const * const *args);
gchar    *py_exc_to_string	 (void);
#endif /* PLUGIN_PY_GNUMERIC_H */
