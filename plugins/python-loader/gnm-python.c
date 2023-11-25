/*
 * gnm-python.c :
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>

#include "gnm-py-interpreter.h"
#include "gnm-python.h"
#include "py-gnumeric.h"

#include <goffice/goffice.h>
#include <goffice/app/module-plugin-defs.h>
#include <gsf/gsf-impl-utils.h>

#include <glib/gi18n-lib.h>
#include <unistd.h>

#include <Python.h>

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

static GnmPython *gnm_python_obj = NULL;

static int
cb_test_signals (void)
{
	if (PyErr_CheckSignals () < 0)
		exit (0);
	return TRUE;
}

static void
gnm_python_init (GnmPython *gpy)
{
	gpy->default_interpreter = gnm_py_interpreter_new (NULL);
	gpy->current_interpreter = gpy->default_interpreter;
	gpy->interpreters = g_slist_append (NULL, gpy->default_interpreter);
	g_return_if_fail (gnm_python_obj == NULL);
	gnm_python_obj = gpy;
	g_timeout_add_full (G_PRIORITY_LOW, 100, (GSourceFunc) cb_test_signals,
	                    gnm_python_obj, NULL);
}

static void
gnm_python_finalize (GObject *obj)
{
	GnmPython *gpy = GNM_PYTHON (obj);

	g_source_remove_by_user_data (gnm_python_obj);
	if (gpy->default_interpreter != NULL) {
		GSList *ints = g_slist_copy (gpy->interpreters);
		GO_SLIST_FOREACH (ints, GnmPyInterpreter, interpreter,
			if (interpreter != gpy->default_interpreter) {
				gnm_py_interpreter_destroy (interpreter, gpy->default_interpreter);
			}
		);
		g_slist_free (ints);
		gnm_py_interpreter_switch_to (gpy->default_interpreter);
		GO_SLIST_REMOVE (gpy->interpreters, gpy->default_interpreter);
		g_object_unref (gpy->default_interpreter);
		gpy->default_interpreter = NULL;
	}
	gnm_python_obj = NULL;

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

/* ---------- */

GnmPython *
gnm_python_object_get (GOErrorInfo **err)
{
	GO_INIT_RET_ERROR_INFO (err);
	if (!Py_IsInitialized ()) {
		PyImport_AppendInittab ("Gnumeric", py_initgnumeric);
		Py_InitializeEx (1);
#if defined(WITH_THREAD) && PY_VERSION_HEX < 0x03070000
		PyEval_InitThreads ();
#endif
	}
	if (err && *err != NULL) {
		Py_Finalize ();
		return NULL;
	}

	if (gnm_python_obj == NULL) {
		(void) g_object_new (GNM_PYTHON_TYPE, NULL);
	} else {
		g_object_ref (gnm_python_obj);
	}

	return gnm_python_obj;
}

static void
cb_interpreter_switched (GnmPyInterpreter *interpreter, GnmPython *gpy)
{
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));
	g_return_if_fail (GNM_IS_PYTHON (gpy));

	gpy->current_interpreter = interpreter;
	g_signal_emit (
		gpy, signals[SWITCHED_INTERPRETER_SIGNAL], 0, interpreter);
}

GnmPyInterpreter *
gnm_python_new_interpreter (GnmPython *gpy, GOPlugin *plugin)
{
	GnmPyInterpreter *interpreter;

	g_return_val_if_fail (GNM_IS_PYTHON (gpy), NULL);
	g_return_val_if_fail (GO_IS_PLUGIN (plugin), NULL);

	interpreter = gnm_py_interpreter_new (plugin);
	GO_SLIST_PREPEND (gpy->interpreters, interpreter);
	gpy->current_interpreter = interpreter;
	g_signal_connect (
		interpreter, "set_current", G_CALLBACK (cb_interpreter_switched), gpy);
	g_signal_emit (gpy, signals[CREATED_INTERPRETER_SIGNAL], 0, interpreter);
	g_object_ref (gpy);

	return interpreter;
}

void
gnm_python_destroy_interpreter (GnmPython *gpy, GnmPyInterpreter *interpreter)
{
	g_return_if_fail (GNM_IS_PYTHON (gpy));
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));
	g_return_if_fail (interpreter != gpy->default_interpreter);

	GO_SLIST_REMOVE (gpy->interpreters, interpreter);
	gnm_py_interpreter_destroy (interpreter, gpy->default_interpreter);
	g_object_unref (gpy);
}

GnmPyInterpreter *
gnm_python_get_current_interpreter (GnmPython *gpy)
{
	g_return_val_if_fail (GNM_IS_PYTHON (gpy), NULL);

	return gpy->current_interpreter;
}

GnmPyInterpreter *
gnm_python_get_default_interpreter (GnmPython *gpy)
{
	g_return_val_if_fail (GNM_IS_PYTHON (gpy), NULL);

	return gpy->default_interpreter;
}

GSList *
gnm_python_get_interpreters (GnmPython *gpy)
{
	g_return_val_if_fail (GNM_IS_PYTHON (gpy), NULL);

	return gpy->interpreters;
}

void
gnm_python_clear_error_if_needed (GnmPython *gpy)
{
	g_return_if_fail (GNM_IS_PYTHON (gpy));

	if (PyErr_Occurred () != NULL) {
		PyErr_Clear ();
	}
}

GSF_DYNAMIC_CLASS (GnmPython, gnm_python,
	gnm_python_class_init, gnm_python_init,
	G_TYPE_OBJECT)
