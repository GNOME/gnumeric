#ifndef PLUGIN_PY_GNUMERIC_H
#define PLUGIN_PY_GNUMERIC_H

#include <Python.h>
#include <glib.h>
#include <plugin.h>
#include "gnm-py-interpreter.h"

void     py_initgnumeric (GnmPyInterpreter *interpreter);

Value    *call_python_function (PyObject *python_fn, const EvalPos *eval_pos, gint n_args, Value **args);
gchar    *py_exc_to_string (void);
PyObject      *py_new_Sheet_object (Sheet *sheet);
PyObject      *py_new_Workbook_object (Workbook *wb);

#endif /* PLUGIN_PY_GNUMERIC_H */
