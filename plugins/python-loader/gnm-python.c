/*
 * gnm-python.c :
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <Python.h>
#include <glib.h>
#include <gutils.h>
#include <gsf/gsf-impl-utils.h>
#include <plugin.h>
#include <module-plugin-defs.h>
#include "gnm-py-interpreter.h"
#include "gnm-python.h"

#define BROKEN_PY_INITIALIZE


struct _GnmPython {
	GObject parent_instance;

	GnmPyInterpreter *current_interpreter, *default_interpreter;
	GSList *interpreters;
};

typedef struct {
	GObjectClass parent_class;

	void (*created_interpreter) (GnmPython *gpy, GnmPyInterpreter *interpreter);
	void (*switched_interpreter) (GnmPython *gpy, GnmPyInterpreter *interpreter);
} GnmPythonClass;

enum {
	CREATED_INTERPRETER_SIGNAL,
	SWITCHED_INTERPRETER_SIGNAL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_class = NULL;

static void
gnm_python_init (GnmPython *gpy)
{
	gpy->default_interpreter = gnm_py_interpreter_new (NULL);
	gpy->current_interpreter = gpy->default_interpreter;
	gpy->interpreters = g_slist_append (NULL, gpy->default_interpreter);
}

static void
gnm_python_finalize (GObject *obj)
{
	GnmPython *gpy = GNM_PYTHON (obj);

	if (gpy->default_interpreter != NULL) {
		GNM_SLIST_FOREACH (gpy->interpreters, GnmPyInterpreter, interpreter,
			if (interpreter != gpy->default_interpreter) {
				gnm_py_interpreter_destroy (interpreter, gpy->default_interpreter);
			}
		);
		gnm_py_interpreter_switch_to (gpy->default_interpreter);
		g_object_unref (gpy->default_interpreter);
		gpy->default_interpreter = NULL;
	}

	parent_class->finalize (obj);
}

static void
gnm_python_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_python_finalize;

	signals[CREATED_INTERPRETER_SIGNAL] =
	g_signal_new (
		"created_interpreter",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (GnmPythonClass, created_interpreter),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE,
		1, G_TYPE_POINTER);
	signals[SWITCHED_INTERPRETER_SIGNAL] =
	g_signal_new (
		"switched_interpreter",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (GnmPythonClass, switched_interpreter),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE,
		1, G_TYPE_POINTER);
}

PLUGIN_CLASS (
	GnmPython, gnm_python, gnm_python_class_init, gnm_python_init,
	G_TYPE_OBJECT)

/* ---------- */

#ifdef BROKEN_PY_INITIALIZE
extern char **environ;
#define BROKEN_PY_ENV  "GNUMERIC_PYTHON_DUPLICATED_ENVIRONMENT"
#endif


static GnmPython *gnm_python_obj = NULL;

GnmPython *
gnm_python_object_get (void)
{
	if (!Py_IsInitialized ()) {
#ifdef BROKEN_PY_INITIALIZE
		if (getenv (BROKEN_PY_ENV) != NULL) {
			int i;

			/* Before Python 2.0, Python's convertenviron would write to
			   the strings in the environment.  We had little choice but
			   to allocate a copy of everything. */
			for (i = 0; environ[i]; i++) {
				environ[i] = g_strdup (environ[i]);
			}
			setenv (BROKEN_PY_ENV, "1", FALSE);
		}
#endif
		Py_Initialize ();
		PyEval_InitThreads ();
#ifdef WITH_PYGTK
		init_pygobject ();
#endif
	}

	if (gnm_python_obj == NULL) {
		gnm_python_obj = g_object_new (GNM_PYTHON_TYPE, NULL);
	}

	return gnm_python_obj;
}

void
gnm_python_object_shutdown (void)
{
	if (gnm_python_obj != NULL) {
		g_object_unref (gnm_python_obj);
		gnm_python_obj = NULL;
	}
}

static void
cb_interpreter_switched (GnmPyInterpreter *interpreter)
{
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	(void) gnm_python_object_get ();
	gnm_python_obj->current_interpreter = interpreter;
	g_signal_emit (
		gnm_python_obj, signals[SWITCHED_INTERPRETER_SIGNAL], 0,
		interpreter);
}

GnmPyInterpreter *
gnm_python_new_interpreter (GnmPlugin *plugin)
{
	GnmPyInterpreter *interpreter;

	g_return_val_if_fail (GNM_IS_PLUGIN (plugin), NULL);

	(void) gnm_python_object_get ();
	interpreter = gnm_py_interpreter_new (plugin);
	GNM_SLIST_PREPEND (gnm_python_obj->interpreters, interpreter);
	gnm_python_obj->current_interpreter = interpreter;
	g_signal_connect (
		interpreter, "set_current",
		G_CALLBACK (cb_interpreter_switched), NULL);
	g_signal_emit (
		gnm_python_obj, signals[CREATED_INTERPRETER_SIGNAL], 0, interpreter);

	return interpreter;
}

void
gnm_python_destroy_interpreter (GnmPyInterpreter *interpreter)
{
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	(void) gnm_python_object_get ();
	g_return_if_fail (interpreter != gnm_python_obj->default_interpreter);
	GNM_SLIST_REMOVE (gnm_python_obj->interpreters, interpreter);
	gnm_py_interpreter_destroy (interpreter, gnm_python_obj->default_interpreter);
}

GnmPyInterpreter *
gnm_python_get_current_interpreter (void)
{
	return gnm_python_object_get ()->current_interpreter;
}

GnmPyInterpreter *
gnm_python_get_default_interpreter (void)
{
	return gnm_python_object_get ()->default_interpreter;
}

GSList *
gnm_python_get_interpreters (void)
{
	return gnm_python_object_get ()->interpreters;
}

void
gnm_python_clear_error_if_needed (void)
{
	if (PyErr_Occurred () != NULL) {
		PyErr_Clear ();
	}
}
