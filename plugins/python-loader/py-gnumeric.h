#ifndef PLUGIN_PY_GNUMERIC_H
#define PLUGIN_PY_GNUMERIC_H

#include <Python.h>
#include <glib.h>
#include "gnm-py-interpreter.h"

void     py_initgnumeric (GnmPyInterpreter *interpreter);

GnmValue *call_python_function	 (PyObject *python_fn, GnmEvalPos const *eval_pos,
				  gint n_args, GnmValue **args);
gchar    *py_exc_to_string	 (void);
PyObject *py_new_Sheet_object	 (Sheet *sheet);
PyObject *py_new_Workbook_object (Workbook *wb);
PyObject *py_new_Gui_object	 (WorkbookControlGUI *wbcg);

#endif /* PLUGIN_PY_GNUMERIC_H */
