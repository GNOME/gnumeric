/*
 * gnm-py-interpreter.c : GObject wrapper around Python interpreter
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <Python.h>
#include <gnumeric.h>
#include <gutils.h>
#include "py-gnumeric.h"
#include "gnm-py-interpreter.h"
#include "gnm-python.h"

#include <goffice/goffice.h>
#include <goffice/app/module-plugin-defs.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>

// Like PyDict_SetItemString, but takes ownership of val
static void
gnm_py_dict_store (PyObject *dict, const char *key, PyObject *val)
{
	PyDict_SetItemString (dict, key, val);
	Py_DECREF (val);
}

struct _GnmPyInterpreter {
	GObject parent_instance;

	PyThreadState *py_thread_state;
	PyTypeObject *stringio_class;
	GOPlugin *plugin;
};

typedef struct {
	GObjectClass parent_class;

	void (*set_current) (GnmPyInterpreter *interpreter);
} GnmPyInterpreterClass;

enum {
	SET_CURRENT_SIGNAL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_class = NULL;

static void
gnm_py_interpreter_init (GnmPyInterpreter *interpreter)
{
	interpreter->py_thread_state = NULL;
	interpreter->stringio_class  = NULL;
	interpreter->plugin = NULL;
}

static void
gnm_py_interpreter_finalize (GObject *obj)
{
	GnmPyInterpreter *interpreter = GNM_PY_INTERPRETER (obj);

	Py_CLEAR (interpreter->stringio_class);
	parent_class->finalize (obj);
}

static void
gnm_py_interpreter_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_py_interpreter_finalize;

	signals[SET_CURRENT_SIGNAL] =
	g_signal_new (
		"set_current",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (GnmPyInterpreterClass, set_current),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static wchar_t *plugin_argv[] = {(wchar_t *) L"/dev/null/python/is/buggy/gnumeric", NULL};

GnmPyInterpreter *
gnm_py_interpreter_new (GOPlugin *plugin)
{
	GnmPyInterpreter *interpreter;
	PyThreadState *py_thread_state;

	g_return_val_if_fail (plugin == NULL || GO_IS_PLUGIN (plugin), NULL);

	if (plugin != NULL) {
		PyThreadState* main_ = PyThreadState_Get ();
		py_thread_state = Py_NewInterpreter ();
		PyThreadState_Swap (main_);
	} else {
		py_thread_state = PyThreadState_Get ();
	}

	interpreter = g_object_new (GNM_PY_INTERPRETER_TYPE, NULL);
	interpreter->py_thread_state = py_thread_state;
	interpreter->plugin = plugin;
	PySys_SetArgv (G_N_ELEMENTS (plugin_argv) - 1, plugin_argv);

	if (plugin != NULL) {
		py_gnumeric_add_plugin (py_initgnumeric (), interpreter);
	}

	return interpreter;
}

void
gnm_py_interpreter_destroy (GnmPyInterpreter *interpreter,
                            GnmPyInterpreter *new_interpreter)
{
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	if (interpreter->plugin != NULL) {
		gnm_py_interpreter_switch_to (interpreter);
		Py_EndInterpreter (interpreter->py_thread_state);
	}
	(void) PyThreadState_Swap (new_interpreter->py_thread_state);
	interpreter->py_thread_state = NULL;
	g_object_unref (interpreter);
}

void
gnm_py_interpreter_switch_to (GnmPyInterpreter *interpreter)
{
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	if (PyThreadState_Get () != interpreter->py_thread_state) {
		// g_printerr ("Switching to %p\n", interpreter->py_thread_state);
		(void) PyThreadState_Swap (interpreter->py_thread_state);
		g_signal_emit (interpreter, signals[SET_CURRENT_SIGNAL], 0);
	} else {
		// g_printerr ("Not switching to %p\n", PyThreadState_Get ());
	}
}

static void
run_print_string (const char *cmd, PyObject *stdout_obj)
{
	PyObject *m, *d, *v;
	m = PyImport_AddModule ("__main__");
	if (m == NULL)
		return;
	d = PyModule_GetDict (m);
	v = PyRun_String (cmd, Py_single_input, d, d);
	if (!v)
		PyErr_Print ();
	if (PyFile_WriteString ("\n", stdout_obj) != 0)
		PyErr_Clear ();
	if (v && v != Py_None && stdout_obj) {
		if (PyFile_WriteObject (v, stdout_obj, Py_PRINT_RAW) != 0)
			PyErr_Clear ();
	}
	Py_XDECREF (v);
}

void
gnm_py_interpreter_run_string (GnmPyInterpreter *interpreter, const char *cmd,
                               char **opt_stdout, char **opt_stderr)
{
	PyObject *sys_module, *sys_module_dict;
	PyObject *saved_stdout_obj = NULL, *stdout_obj = NULL,
	         *saved_stderr_obj = NULL, *stderr_obj = NULL;
	PyObject *py_str;

	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	gnm_py_interpreter_switch_to (interpreter);

	sys_module = PyImport_AddModule ("sys");
	if (sys_module == NULL)
		PyErr_Print ();
	g_return_if_fail (sys_module != NULL);
	sys_module_dict = PyModule_GetDict (sys_module);
	g_return_if_fail (sys_module_dict != NULL);
	if (interpreter->stringio_class == NULL) {
		PyObject *stringio_module, *stringio_module_dict, *sublist;

		sublist = PyList_New (0);
		PyList_Insert (sublist, 0, PyUnicode_FromString ("StringIO"));
		stringio_module = PyImport_ImportModule ("io");
		Py_DECREF (sublist);
		if (stringio_module == NULL)
			PyErr_Print ();
		g_return_if_fail (stringio_module != NULL);
		stringio_module_dict = PyModule_GetDict (stringio_module);
		g_return_if_fail (stringio_module_dict != NULL);
		interpreter->stringio_class	=
				(PyTypeObject *) PyDict_GetItemString (stringio_module_dict,
								       "StringIO");
		g_return_if_fail (interpreter->stringio_class != NULL);
		Py_INCREF (interpreter->stringio_class);
	}
	if (opt_stdout != NULL) {
		stdout_obj = PyType_GenericNew(interpreter->stringio_class,
					       NULL, NULL);
		if (stdout_obj == NULL)
			PyErr_Print ();
		g_return_if_fail (stdout_obj != NULL);
		PyObject_CallMethod (stdout_obj, "__init__", NULL);
		saved_stdout_obj = PyDict_GetItemString (sys_module_dict,
							 "stdout");
		g_return_if_fail (saved_stdout_obj != NULL);
		Py_INCREF (saved_stdout_obj);
		PyDict_SetItemString (sys_module_dict, "stdout", stdout_obj);
		// We still own a ref to stdout_obj
	}
	if (opt_stderr != NULL) {
		stderr_obj = PyType_GenericNew(interpreter->stringio_class,
					       NULL, NULL);
		if (stderr_obj == NULL)
			PyErr_Print ();
		g_return_if_fail (stderr_obj != NULL);
		PyObject_CallMethod (stderr_obj, "__init__", NULL);
		saved_stderr_obj = PyDict_GetItemString (sys_module_dict,
							 "stderr");
		g_return_if_fail (saved_stderr_obj != NULL);
		Py_INCREF (saved_stderr_obj);
		PyDict_SetItemString (sys_module_dict, "stderr", stderr_obj);
		// We still own a ref to stderr_obj
	}
	run_print_string (cmd, stdout_obj);
	if (opt_stdout != NULL) {
		gnm_py_dict_store (sys_module_dict, "stdout", saved_stdout_obj);
		py_str = PyObject_CallMethod (stdout_obj, "getvalue", NULL);
		if (py_str && PyUnicode_Check (py_str))
			*opt_stdout = g_strdup (PyUnicode_AsUTF8 (py_str));
		else
			*opt_stdout = NULL;
		if (py_str == NULL)
			PyErr_Print ();
		Py_DECREF (stdout_obj);
	}
	if (opt_stderr != NULL) {
		gnm_py_dict_store (sys_module_dict, "stderr", saved_stderr_obj);
		py_str = PyObject_CallMethod (stderr_obj, "getvalue", NULL);
		if (py_str && PyUnicode_Check (py_str))
			*opt_stderr = g_strdup (PyUnicode_AsUTF8 (py_str));
		else
			*opt_stderr = NULL;
		if (py_str == NULL)
			PyErr_Print ();
		Py_DECREF (stderr_obj);
	}
}

const char *
gnm_py_interpreter_get_name (GnmPyInterpreter *interpreter)
{
	g_return_val_if_fail (GNM_IS_PY_INTERPRETER (interpreter), NULL);

	if (interpreter->plugin != NULL) {
		return go_plugin_get_name (interpreter->plugin);
	} else {
		return _("Default interpreter");
	}
}

GOPlugin *
gnm_py_interpreter_get_plugin (GnmPyInterpreter *interpreter)
{
	g_return_val_if_fail (GNM_IS_PY_INTERPRETER (interpreter), NULL);

	return interpreter->plugin;
}

int
gnm_py_interpreter_compare (gconstpointer a, gconstpointer b)
{
	const GnmPyInterpreter *int_a = a, *int_b = b;

	if (int_a->plugin == int_b->plugin) {
		return 0;
	} else if (int_a->plugin == NULL) {
		return -1;
	} else if (int_b->plugin == NULL) {
		return 1;
	} else {
		return g_utf8_collate (go_plugin_get_name (int_a->plugin),
				       go_plugin_get_name (int_b->plugin));
	}
}

GSF_DYNAMIC_CLASS (GnmPyInterpreter, gnm_py_interpreter,
	gnm_py_interpreter_class_init, gnm_py_interpreter_init,
	G_TYPE_OBJECT)
