/*
 * gnm-py-interpreter.c : GObject wrapper around Python interpreter
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <stdio.h>
#include <glib.h>
#include <Python.h>
#include <gnumeric.h>
#include <plugin.h>
#include <gutils.h>
#include <module-plugin-defs.h>
#include "py-gnumeric.h"
#include "gnm-py-interpreter.h"


struct _GnmPyInterpreter {
	GObject parent_instance;

	PyThreadState *py_thread_state;
	GnmPlugin *plugin;
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
	interpreter->plugin = NULL;
}

static void
gnm_py_interpreter_finalize (GObject *obj)
{
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

PLUGIN_CLASS (
	GnmPyInterpreter, gnm_py_interpreter, gnm_py_interpreter_class_init,
	gnm_py_interpreter_init, G_TYPE_OBJECT)


static char *plugin_argv[] = {(char *) "gnumeric", NULL};

GnmPyInterpreter *
gnm_py_interpreter_new (GnmPlugin *plugin)
{
	GnmPyInterpreter *interpreter;
	PyThreadState *py_thread_state;

	g_return_val_if_fail (plugin == NULL || GNM_IS_PLUGIN (plugin), NULL);

	if (plugin != NULL) {
		py_thread_state = Py_NewInterpreter ();
	} else {
		py_thread_state = PyThreadState_Get ();
	}
	g_return_val_if_fail (py_thread_state != NULL, NULL);

	interpreter = g_object_new (GNM_PY_INTERPRETER_TYPE, NULL);
	interpreter->py_thread_state = py_thread_state;
	interpreter->plugin = plugin;

	PySys_SetArgv (G_N_ELEMENTS (plugin_argv) - 1, plugin_argv);
	py_initgnumeric (interpreter); 

	return interpreter;
}

void
gnm_py_interpreter_destroy (GnmPyInterpreter *interpreter,
                            GnmPyInterpreter *new_interpreter)
{
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	gnm_py_interpreter_switch_to (interpreter);
	Py_EndInterpreter (interpreter->py_thread_state);
	(void) PyThreadState_Swap (new_interpreter->py_thread_state);
	interpreter->py_thread_state = NULL;
	g_object_unref (interpreter);
}

void
gnm_py_interpreter_switch_to (GnmPyInterpreter *interpreter)
{
	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	if (PyThreadState_Get ()->interp != interpreter->py_thread_state->interp) {
		(void) PyThreadState_Swap (interpreter->py_thread_state);
		g_signal_emit (
			interpreter, signals[SET_CURRENT_SIGNAL], 0);
	}
}

static char *
read_file (FILE *f)
{
	gint len;
	char *buf;

	fseek (f, 0, SEEK_END);
	len = ftell (f);
	if (len > 0) {
		buf = g_new (char, len + 1);
		buf[len] = '\0';
		rewind (f);
		fread (buf, 1, len, f);
	} else {
		buf = NULL;
	}

	return buf;
}

void
gnm_py_interpreter_run_string (GnmPyInterpreter *interpreter, const char *cmd,
                               char **opt_stdout, char **opt_stderr)
{
	PyObject *sys_module, *sys_module_dict;
	PyObject *saved_stdout_obj = NULL, *stdout_obj,
	         *saved_stderr_obj = NULL, *stderr_obj;
	FILE *stdout_f = NULL, *stderr_f = NULL;

	g_return_if_fail (GNM_IS_PY_INTERPRETER (interpreter));

	gnm_py_interpreter_switch_to (interpreter);

	sys_module = PyImport_AddModule ((char *) "sys");
	g_return_if_fail (sys_module != NULL);
	sys_module_dict = PyModule_GetDict (sys_module);
	g_return_if_fail (sys_module_dict != NULL);
	if (opt_stdout != NULL) {
		stdout_f = tmpfile ();
		stdout_obj = PyFile_FromFile (stdout_f, (char *) "<stdout>", (char *) "a", NULL);
		saved_stdout_obj = PyDict_GetItemString (sys_module_dict, (char *) "stdout");
		g_assert (saved_stdout_obj != NULL);
		Py_INCREF (saved_stdout_obj);
		PyDict_SetItemString (sys_module_dict, (char *) "stdout", stdout_obj);
	}
	if (opt_stderr != NULL) {
		stderr_f = tmpfile ();
		stderr_obj = PyFile_FromFile (stderr_f, (char *) "<stderr>", (char *) "a", NULL);
		saved_stderr_obj = PyDict_GetItemString (sys_module_dict, (char *) "stderr");
		g_assert (saved_stderr_obj != NULL);
		Py_INCREF (saved_stderr_obj);
		PyDict_SetItemString (sys_module_dict, (char *) "stderr", stderr_obj);
	}
	PyRun_SimpleString ((char *) cmd);
	if (opt_stdout != NULL) {
		PyDict_SetItemString (sys_module_dict, (char *) "stdout", saved_stdout_obj);
		Py_DECREF (saved_stdout_obj);
		*opt_stdout = read_file (stdout_f);
		fclose (stdout_f);
	}
	if (opt_stderr != NULL) {
		PyDict_SetItemString (sys_module_dict, (char *) "stderr", saved_stderr_obj);
		Py_DECREF (saved_stderr_obj);
		*opt_stderr = read_file (stderr_f);
		fclose (stderr_f);
	}
}

const char *
gnm_py_interpreter_get_name (GnmPyInterpreter *interpreter)
{
	g_return_val_if_fail (GNM_IS_PY_INTERPRETER (interpreter), NULL);

	if (interpreter->plugin != NULL) {
		return gnm_plugin_get_name (interpreter->plugin);
	} else {
		return _("Default interpreter");
	}
}

GnmPlugin *
gnm_py_interpreter_get_plugin (GnmPyInterpreter *interpreter)
{
	g_return_val_if_fail (GNM_IS_PY_INTERPRETER (interpreter), NULL);

	return interpreter->plugin;
}

int
gnm_py_interpreter_compare (gconstpointer a, gconstpointer b)
{
	const GnmPyInterpreter *int_a = a, *int_b = b;

	if (int_a->plugin == NULL && int_b->plugin == NULL) {
		return 0;
	} else if (int_a->plugin == NULL) {
		return -1;
	} else if (int_b->plugin == NULL) {
		return 1;
	} else {
		return g_utf8_collate (gnm_plugin_get_name (int_a->plugin),
				       gnm_plugin_get_name (int_b->plugin));
	}
}
