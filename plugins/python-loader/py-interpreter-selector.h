#ifndef PY_INTERPRETER_SELECTOR
#define PY_INTERPRETER_SELECTOR

#include <glib.h>
#include <gnumeric.h>
#include <goffice/goffice.h>
#include "gnm-py-interpreter.h"

#define GNM_PY_INTERPRETER_SELECTOR_TYPE     (gnm_py_interpreter_selector_get_type ())
#define GNM_PY_INTERPRETER_SELECTOR(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PY_INTERPRETER_SELECTOR_TYPE, GnmPyInterpreterSelector))
#define GNM_IS_PY_INTERPRETER_SELECTOR(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PY_INTERPRETER_SELECTOR_TYPE))

GType gnm_py_interpreter_selector_get_type (void);
typedef struct _GnmPyInterpreterSelector GnmPyInterpreterSelector;

GtkWidget         *gnm_py_interpreter_selector_new (GOErrorInfo **err);
GnmPyInterpreter  *gnm_py_interpreter_selector_get_current (GnmPyInterpreterSelector *sel);

#endif /* PY_INTERPRETER_SELECTOR */
