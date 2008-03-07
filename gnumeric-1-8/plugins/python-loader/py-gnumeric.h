#ifndef PLUGIN_PY_GNUMERIC_H
#define PLUGIN_PY_GNUMERIC_H

#include <Python.h>
#include <glib.h>
#include <goffice/app/go-plugin.h>
#include "gnm-py-interpreter.h"
#include <gui-gnumeric.h>

void     py_initgnumeric (GnmPyInterpreter *interpreter);

GnmValue *call_python_function	 (PyObject *python_fn,
				  GnmEvalPos const *eval_pos,
				  gint n_args, GnmValue const * const *args);
gchar    *py_exc_to_string	 (void);
PyObject *py_new_Sheet_object	 (Sheet *sheet);
PyObject *py_new_Workbook_object (Workbook *wb);
PyObject *py_new_Gui_object	 (WBCGtk *wbcg);

#endif /* PLUGIN_PY_GNUMERIC_H */
