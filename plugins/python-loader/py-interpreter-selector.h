#ifndef PY_INTERPRETER_SELECTOR
#define PY_INTERPRETER_SELECTOR

#include <glib.h>
#include <gtk/gtkwidget.h>
#include <gnumeric.h>
#include <goffice/app/error-info.h>
#include "gnm-py-interpreter.h"

#define GNM_PY_INTERPRETER_SELECTOR_TYPE     (gnm_py_interpreter_selector_get_type ())
#define GNM_PY_INTERPRETER_SELECTOR(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PY_INTERPRETER_SELECTOR_TYPE, GnmPyInterpreterSelector))
#define IS_GNM_PY_INTERPRETER_SELECTOR(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PY_INTERPRETER_SELECTOR_TYPE))

GType gnm_py_interpreter_selector_get_type (void);
typedef struct _GnmPyInterpreterSelector GnmPyInterpreterSelector;

GtkWidget         *gnm_py_interpreter_selector_new (ErrorInfo **err);
GnmPyInterpreter  *gnm_py_interpreter_selector_get_current (GnmPyInterpreterSelector *sel);

#endif /* PY_INTERPRETER_SELECTOR */
