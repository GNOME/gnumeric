/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * python-loader.c: Support for Python plugins.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "python-loader.h"

#include <Python.h>
#include "pygobject.h"
#include "py-gnumeric.h"
#include "gnm-python.h"

#include "gnm-plugin.h"
#include "application.h"
#include "workbook.h"
#include "sheet.h"
#include "workbook-view.h"
#include "wbc-gtk.h"
#include "gui-util.h"
#include "value.h"
#include "expr.h"
#include "expr-impl.h"
#include "func.h"

#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin-service.h>
#include <goffice/app/go-plugin-loader.h>
#include <goffice/app/module-plugin-defs.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <glib.h>

typedef struct {
	GObject base;

	gchar *module_name;

	GnmPython *py_object;
	GnmPyInterpreter *py_interpreter_info;
	PyObject *main_module;
	PyObject *main_module_dict;
} GnmPythonPluginLoader;
typedef GObjectClass GnmPythonPluginLoaderClass;

#define PLUGIN_GET_LOADER(plugin) \
	GNM_PYTHON_PLUGIN_LOADER (g_object_get_data (G_OBJECT (plugin), "python-loader"))
#define SERVICE_GET_LOADER(service) \
	PLUGIN_GET_LOADER (plugin_service_get_plugin (service))
#define SWITCH_TO_PLUGIN(plugin) \
	gnm_py_interpreter_switch_to (PLUGIN_GET_LOADER (plugin)->py_interpreter_info)

static void
gplp_set_attributes (GOPluginLoader *loader, GHashTable *attrs, ErrorInfo **ret_error)
{
	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (loader);
	gchar *module_name = NULL;

	GO_INIT_RET_ERROR_INFO (ret_error);
	module_name = g_hash_table_lookup (attrs, "module_name");
	if (module_name != NULL) {
		loader_python->module_name = g_strdup (module_name);
	} else {
		*ret_error = error_info_new_str (
		             _("Python module name not given."));
	}
}

static FILE *
gnumeric_fopen_error_info (const char *file_name, const char *mode, ErrorInfo **ret_error)
{
	FILE *f;

	g_return_val_if_fail (file_name != NULL, NULL);
	g_return_val_if_fail (mode != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	f = g_fopen (file_name, mode);
	if (f == NULL) {
		if (strchr (mode, 'w') != NULL && strchr (mode, 'r') == NULL) {
			*ret_error = error_info_new_printf (
			             _("Error while opening file \"%s\" for writing."),
			             file_name);
		} else {
			*ret_error = error_info_new_printf (
			             _("Error while opening file \"%s\" for reading."),
			             file_name);
		}
		error_info_add_details (*ret_error, error_info_new_from_errno ());
	}

	return f;
}
static void
gplp_load_base (GOPluginLoader *loader, ErrorInfo **ret_error)
{
	static gchar const *python_file_extensions[] = {"py", "pyc", "pyo", NULL};

	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (loader);
	gchar const **file_ext;
	GnmPython *py_object;
	GnmPyInterpreter *py_interpreter_info;
	gchar *full_module_file_name = NULL;
	FILE *f;
	GOPlugin *plugin = go_plugin_loader_get_plugin (loader);
	ErrorInfo *open_error = NULL;
	PyObject *modules, *main_module, *main_module_dict;

	GO_INIT_RET_ERROR_INFO (ret_error);
	g_object_set_data (G_OBJECT (plugin), "python-loader", loader);

	py_object = gnm_python_object_get (ret_error);
	if (py_object == NULL)
		return;		/* gnm_python_object_get sets ret_error */
	py_interpreter_info = gnm_python_new_interpreter (py_object, plugin);
	if (py_interpreter_info == NULL) {
		*ret_error = error_info_new_str (_("Cannot create new Python interpreter."));
		gnm_python_clear_error_if_needed (py_object);
		g_object_unref (py_object);
		return;
	}

	for (file_ext = python_file_extensions; *file_ext != NULL; file_ext++) {
		gchar *file_name = g_strconcat (
			loader_python->module_name, ".", *file_ext, NULL);
		gchar *path = g_build_filename (
			go_plugin_get_dir_name (plugin),
			file_name, NULL);
		g_free (file_name);
		if (g_file_test (path, G_FILE_TEST_EXISTS)) {
			full_module_file_name = path;
			break;
		} else
			g_free (path);
	}
	if (full_module_file_name == NULL) {
		*ret_error = error_info_new_printf (
		             _("Module \"%s\" doesn't exist."),
		             loader_python->module_name);
		gnm_python_destroy_interpreter (py_object, py_interpreter_info);
		g_object_unref (py_object);
		return;
	}
	f = gnumeric_fopen_error_info (full_module_file_name, "r", &open_error);
	g_free (full_module_file_name);
	if (f == NULL) {
		*ret_error = open_error;
		gnm_python_destroy_interpreter (py_object, py_interpreter_info);
		g_object_unref (py_object);
		return;
	}

	if (PyRun_SimpleFile (f, loader_python->module_name) != 0) {
		(void) fclose (f);
		*ret_error = error_info_new_printf (
		             _("Execution of module \"%s\" failed."),
		             loader_python->module_name);
		gnm_python_destroy_interpreter (py_object, py_interpreter_info);
		g_object_unref (py_object);
		return;
	}
	(void) fclose (f);

	modules = PyImport_GetModuleDict ();
	g_return_if_fail (modules != NULL);
	main_module = PyDict_GetItemString (modules, (char *) "__main__");
	g_return_if_fail (main_module != NULL);
	main_module_dict = PyModule_GetDict (main_module);
	g_return_if_fail (main_module_dict != NULL);
	loader_python->py_object = py_object;
	loader_python->py_interpreter_info = py_interpreter_info;
	loader_python->main_module = main_module;
	loader_python->main_module_dict = main_module_dict;
}

static void
gplp_unload_base (GOPluginLoader *loader, ErrorInfo **ret_error)
{
	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (loader);
	GOPlugin *plugin = go_plugin_loader_get_plugin (loader);

	GO_INIT_RET_ERROR_INFO (ret_error);
	g_object_steal_data (G_OBJECT (plugin), "python-loader");
	gnm_python_destroy_interpreter (
		loader_python->py_object, loader_python->py_interpreter_info);
	g_object_unref (loader_python->py_object);
}

/*
 * Service - file_opener
 */

typedef struct {
	PyObject *python_func_file_probe;
	PyObject *python_func_file_open;
} ServiceLoaderDataFileOpener;

static void
gplp_loader_data_opener_free (ServiceLoaderDataFileOpener *loader_data)
{
	Py_DECREF (loader_data->python_func_file_probe);
	Py_DECREF (loader_data->python_func_file_open);
	g_free (loader_data);
}

static gboolean
gplp_func_file_probe (GOFileOpener const *fo, GOPluginService *service,
		      GsfInput *input, FileProbeLevel pl)
{
	ServiceLoaderDataFileOpener *loader_data;
	PyObject *probe_result = NULL;
	PyObject *input_wrapper;
	gboolean result;

	g_return_val_if_fail (IS_GO_PLUGIN_SERVICE_FILE_OPENER (service), FALSE);
	g_return_val_if_fail (input != NULL, FALSE);
	g_return_val_if_fail (_PyGObject_API != NULL, FALSE);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	input_wrapper = pygobject_new (G_OBJECT (input));
	if (input_wrapper == NULL) {
		g_warning ("s", py_exc_to_string ());
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
	}
	if (input_wrapper != NULL &&
	    loader_data->python_func_file_probe != NULL) {
		/* wrapping adds a reference */
		g_object_unref (G_OBJECT (input));
		probe_result = PyObject_CallFunction
			(loader_data->python_func_file_probe,
			 (char *) "O", input_wrapper);
		Py_DECREF (input_wrapper);
	}
	if (probe_result != NULL) {
		result = PyObject_IsTrue (probe_result);
		Py_DECREF (probe_result);
	} else {
		PyErr_Clear ();
		result = FALSE;
	}

	return result;
}

static void
gplp_func_file_open (GOFileOpener const *fo,
		     GOPluginService *service,
		     IOContext *io_context,
		     gpointer wb_view,
		     GsfInput *input)
{
	ServiceLoaderDataFileOpener *loader_data;
	Sheet *sheet;
	PyObject *open_result = NULL;
	PyObject *input_wrapper;

	g_return_if_fail (IS_GO_PLUGIN_SERVICE_FILE_OPENER (service));
	g_return_if_fail (input != NULL);
	g_return_if_fail (_PyGObject_API != NULL);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	sheet = sheet_new (wb_view_get_workbook (wb_view), _("Some name"));
	input_wrapper = pygobject_new (G_OBJECT (input));
	if (input_wrapper != NULL) {
		 /* wrapping adds a reference */
		g_object_unref (G_OBJECT (input));
		open_result = PyObject_CallFunction
			(loader_data->python_func_file_open,
			 (char *) "NO",
			 py_new_Sheet_object (sheet), input_wrapper);
		Py_DECREF (input_wrapper);
	}
	if (open_result != NULL) {
		Py_DECREF (open_result);
		workbook_sheet_attach (wb_view_get_workbook (wb_view), sheet);
	} else {
		gnumeric_io_error_string (io_context, py_exc_to_string ());
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
		g_object_unref (sheet);
	}
}

static void
gplp_load_service_file_opener (GOPluginLoader *loader,
			       GOPluginService *service,
			       ErrorInfo **ret_error)
{
	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (loader);
	gchar *func_name_file_probe, *func_name_file_open;
	PyObject *python_func_file_probe, *python_func_file_open;

	g_return_if_fail (IS_GO_PLUGIN_SERVICE_FILE_OPENER (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_py_interpreter_switch_to (loader_python->py_interpreter_info);
	func_name_file_probe = g_strconcat (
		plugin_service_get_id (service), "_file_probe", NULL);
	python_func_file_probe = PyDict_GetItemString (loader_python->main_module_dict,
	                                               func_name_file_probe);
	gnm_python_clear_error_if_needed (loader_python->py_object);
	func_name_file_open = g_strconcat (
		plugin_service_get_id (service), "_file_open", NULL);
	python_func_file_open = PyDict_GetItemString (loader_python->main_module_dict,
	                                              func_name_file_open);
	gnm_python_clear_error_if_needed (loader_python->py_object);
	if (python_func_file_open != NULL) {
		PluginServiceFileOpenerCallbacks *cbs;
		ServiceLoaderDataFileOpener *loader_data;

		cbs = plugin_service_get_cbs (service);
		cbs->plugin_func_file_probe = gplp_func_file_probe;
		cbs->plugin_func_file_open = gplp_func_file_open;

		loader_data = g_new (ServiceLoaderDataFileOpener, 1);
		loader_data->python_func_file_probe = python_func_file_probe;
		loader_data->python_func_file_open = python_func_file_open;
		if (python_func_file_probe != NULL)
			Py_INCREF (loader_data->python_func_file_probe);
		Py_INCREF (loader_data->python_func_file_open);
		g_object_set_data_full
			(G_OBJECT (service), "loader_data", loader_data,
			 (GDestroyNotify) gplp_loader_data_opener_free);
	} else {
		*ret_error = error_info_new_printf (
		             _("Python file \"%s\" has invalid format."),
		             loader_python->module_name);
		error_info_add_details (*ret_error,
		                        error_info_new_printf (
		                        _("File doesn't contain \"%s\" function."),
		                        func_name_file_open));
	}
	g_free (func_name_file_probe);
	g_free (func_name_file_open);
}

/*
 * Service - file_saver
 */

typedef struct {
	PyObject *python_func_file_save;
} ServiceLoaderDataFileSaver;

static void
gplp_loader_data_saver_free (ServiceLoaderDataFileSaver *loader_data)
{
	Py_DECREF (loader_data->python_func_file_save);
	g_free (loader_data);
}

static void
gplp_func_file_save (GOFileSaver const *fs, GOPluginService *service,
		     IOContext *io_context, gconstpointer wb_view,
		     GsfOutput *output)
{
	ServiceLoaderDataFileSaver *saver_data;
	PyObject *py_workbook;
	PyObject *save_result = NULL;
	PyObject *output_wrapper;

	g_return_if_fail (IS_GO_PLUGIN_SERVICE_FILE_SAVER (service));
	g_return_if_fail (output != NULL);
	g_return_if_fail (_PyGObject_API != NULL);

	saver_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	py_workbook = py_new_Workbook_object (wb_view_get_workbook (wb_view));
	output_wrapper = pygobject_new (G_OBJECT (output));
	if (output_wrapper != NULL) {
		/* wrapping adds a reference */
		g_object_unref (G_OBJECT (output));
		save_result = PyObject_CallFunction
			(saver_data->python_func_file_save,
			 (char *) "NO", py_workbook, output_wrapper);
		Py_DECREF (output_wrapper);
	}
	if (save_result != NULL) {
		Py_DECREF (save_result);
	} else {
		gnumeric_io_error_string (io_context, py_exc_to_string ());
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
	}
}

static void
gplp_load_service_file_saver (GOPluginLoader *loader,
			      GOPluginService *service,
			      ErrorInfo **ret_error)
{
	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (loader);
	gchar *func_name_file_save;
	PyObject *python_func_file_save;

	g_return_if_fail (IS_GO_PLUGIN_SERVICE_FILE_SAVER (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_py_interpreter_switch_to (loader_python->py_interpreter_info);
	func_name_file_save = g_strconcat (
		plugin_service_get_id (service), "_file_save", NULL);
	python_func_file_save = PyDict_GetItemString (loader_python->main_module_dict,
	                                              func_name_file_save);
	gnm_python_clear_error_if_needed (loader_python->py_object);
	if (python_func_file_save != NULL) {
		PluginServiceFileSaverCallbacks *cbs;
		ServiceLoaderDataFileSaver *saver_data;

		cbs = plugin_service_get_cbs (service);
		cbs->plugin_func_file_save = gplp_func_file_save;

		saver_data = g_new (ServiceLoaderDataFileSaver, 1);
		saver_data->python_func_file_save = python_func_file_save;
		Py_INCREF (saver_data->python_func_file_save);
		g_object_set_data_full
			(G_OBJECT (service), "loader_data", saver_data,
			 (GDestroyNotify) gplp_loader_data_saver_free);
	} else {
		*ret_error = error_info_new_printf (
		             _("Python file \"%s\" has invalid format."),
		             loader_python->module_name);
		if (python_func_file_save == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("File doesn't contain \"%s\" function."),
			                        func_name_file_save));
		}
	}
	g_free (func_name_file_save);
}

/*
 * Service - function_group
 */

typedef struct {
	PyObject *python_fn_info_dict;
} ServiceLoaderDataFunctionGroup;


static void
gplp_loader_data_fngroup_free (ServiceLoaderDataFunctionGroup *loader_data)
{
	Py_DECREF (loader_data->python_fn_info_dict);
	g_free (loader_data);
}

static GnmValue *
call_python_function_args (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GOPluginService *service;
	ServiceLoaderDataFunctionGroup *loader_data;
	PyObject *fn_info_tuple;
	PyObject *python_fn;
	GnmFunc const * fndef;

	gint min_n_args, max_n_args, n_args;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);
	g_return_val_if_fail (args != NULL, NULL);

	fndef = ei->func_call->func;
	service = (GOPluginService *) gnm_func_get_user_data (fndef);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	fn_info_tuple = PyDict_GetItemString (loader_data->python_fn_info_dict,
	                                      (gchar *) gnm_func_get_name (fndef));
	g_assert (fn_info_tuple != NULL);
	python_fn = PyTuple_GetItem (fn_info_tuple, 2);
	function_def_count_args (fndef, &min_n_args, &max_n_args);
	for (n_args = min_n_args; n_args < max_n_args && args[n_args] != NULL; n_args++) {
		;
	}
	return call_python_function (python_fn, ei->pos, n_args, args);
}

static GnmValue *
call_python_function_nodes (GnmFuncEvalInfo *ei,
			    int argc, GnmExprConstPtr const *argv)
{
	GOPluginService *service;
	ServiceLoaderDataFunctionGroup *loader_data;
	PyObject *python_fn;
	GnmFunc const * fndef;
	GnmValue **values;
	gint i;
	GnmValue *ret_value;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);

	fndef = ei->func_call->func;
	service = (GOPluginService *) gnm_func_get_user_data (fndef);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	python_fn = PyDict_GetItemString (loader_data->python_fn_info_dict,
	                                  (gchar *) gnm_func_get_name (fndef));

	values = g_new (GnmValue *, argc);
	for (i = 0; i < argc; i++) {
		values[i] = gnm_expr_eval (argv[i], ei->pos, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
	}
	ret_value = call_python_function (python_fn, ei->pos, argc,
					  (GnmValue const * const *)values);
	for (i = 0; i < argc; i++) {
		value_release (values[i]);
	}
	g_free (values);

	return ret_value;
}

static GnmFuncHelp const *
python_function_get_gnumeric_help (PyObject *python_fn_info_dict, PyObject *python_fn,
                                   const gchar *fn_name)
{
	gchar *help_attr_name;
	PyObject *cobject_help_value;

	help_attr_name = g_strdup_printf ("_CGnumericHelp_%s", fn_name);
	cobject_help_value = PyDict_GetItemString (python_fn_info_dict, help_attr_name);

	if (cobject_help_value == NULL) {
		PyObject *python_fn_help = ((PyFunctionObject *) python_fn)->func_doc;
		if (python_fn_help != NULL && PyString_Check (python_fn_help)) {
			GnmFuncHelp *new_help = g_new(GnmFuncHelp, 2);
			gchar *help_str = PyString_AsString (python_fn_help);
			new_help[0].type = GNM_FUNC_HELP_OLD;
			new_help[0].text = help_str;
			new_help[1].type = GNM_FUNC_HELP_END;
			new_help[1].text = NULL;

			cobject_help_value = PyCObject_FromVoidPtr (new_help, &g_free);
			PyDict_SetItemString (python_fn_info_dict, help_attr_name, cobject_help_value);
		}
	}
	g_free (help_attr_name);
	if (cobject_help_value == NULL)
		return NULL;

	return (GnmFuncHelp const *) PyCObject_AsVoidPtr (cobject_help_value);
}

static gboolean
gplp_func_desc_load (GOPluginService *service,
		     char const *name,
		     GnmFuncDescriptor *res)
{
	ServiceLoaderDataFunctionGroup *loader_data;
	PyObject *fn_info_obj;

	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	fn_info_obj = PyDict_GetItemString (loader_data->python_fn_info_dict,
					    (gchar *) name);
	if (fn_info_obj == NULL) {
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
		return FALSE;
	}

	if (PyTuple_Check (fn_info_obj)) {
		PyObject *python_args, *python_arg_names;
		PyObject *python_fn;

		if (PyTuple_Size (fn_info_obj) == 3 &&
		    (python_args = PyTuple_GetItem (fn_info_obj, 0)) != NULL &&
			PyString_Check (python_args) &&
		    (python_arg_names = PyTuple_GetItem (fn_info_obj, 1)) != NULL &&
		    PyString_Check (python_arg_names) &&
		    (python_fn = PyTuple_GetItem (fn_info_obj, 2)) != NULL &&
		    PyFunction_Check (python_fn)) {
			res->arg_spec	= PyString_AsString (python_args);
			res->arg_names  = PyString_AsString (python_arg_names);
			res->help	= python_function_get_gnumeric_help (
				loader_data->python_fn_info_dict, python_fn, name);
			res->fn_args	= &call_python_function_args;
			res->fn_nodes	= NULL;
			res->linker	= NULL;
			res->unlinker	= NULL;
			res->impl_status = GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC;
			res->test_status = GNM_FUNC_TEST_STATUS_UNKNOWN;
			return TRUE;
		}

		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
		return FALSE;
	}

	if (PyFunction_Check (fn_info_obj)) {
		res->arg_spec	= "";
		res->arg_names  = "";
		res->help	= python_function_get_gnumeric_help (
			loader_data->python_fn_info_dict, fn_info_obj, name);
		res->fn_args	= NULL;
		res->fn_nodes	= &call_python_function_nodes;
		res->linker	= NULL;
		res->unlinker	= NULL;
		res->impl_status = GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC;
		res->test_status = GNM_FUNC_TEST_STATUS_UNKNOWN;
		return TRUE;
	}

	gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
	return FALSE;
}

static void
gplp_load_service_function_group (GOPluginLoader *loader,
				  GOPluginService *service,
				  ErrorInfo **ret_error)
{
	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (loader);
	gchar *fn_info_dict_name;
	PyObject *python_fn_info_dict;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_py_interpreter_switch_to (loader_python->py_interpreter_info);
	fn_info_dict_name = g_strconcat (
		plugin_service_get_id (service), "_functions", NULL);
	python_fn_info_dict = PyDict_GetItemString (loader_python->main_module_dict,
	                                             fn_info_dict_name);
	gnm_python_clear_error_if_needed (loader_python->py_object);
	if (python_fn_info_dict != NULL && PyDict_Check (python_fn_info_dict)) {
		PluginServiceFunctionGroupCallbacks *cbs;
		ServiceLoaderDataFunctionGroup *loader_data;

		cbs = plugin_service_get_cbs (service);
		cbs->func_desc_load = &gplp_func_desc_load;

		loader_data = g_new (ServiceLoaderDataFunctionGroup, 1);
		loader_data->python_fn_info_dict = (PyObject *) python_fn_info_dict;
		Py_INCREF (loader_data->python_fn_info_dict);
		g_object_set_data_full
			(G_OBJECT (service), "loader_data", loader_data,
			 (GDestroyNotify) gplp_loader_data_fngroup_free);
	} else {
		*ret_error = error_info_new_printf (
		             _("Python file \"%s\" has invalid format."),
		             loader_python->module_name);
		if (python_fn_info_dict == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("File doesn't contain \"%s\" dictionary."),
			                        fn_info_dict_name));
		} else if (!PyDict_Check (python_fn_info_dict)) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("Object \"%s\" is not a dictionary."),
			                        fn_info_dict_name));
		}
	}
	g_free (fn_info_dict_name);
}

static void
gplp_unload_service_function_group (GOPluginLoader *loader,
				    GOPluginService *service,
				    ErrorInfo **ret_error)
{
	ServiceLoaderDataFunctionGroup *loader_data;

	g_return_if_fail (IS_GNM_PYTHON_PLUGIN_LOADER (loader));
	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	Py_DECREF (loader_data->python_fn_info_dict);
}

typedef struct {
	PyObject *ui_actions;
} ServiceLoaderDataUI;

static void
gplp_loader_data_ui_free (ServiceLoaderDataUI *loader_data)
{
	Py_DECREF (loader_data->ui_actions);
	g_free (loader_data);
}

static void
gplp_func_exec_action (GOPluginService *service,
		       GnmAction const *action,
		       WorkbookControl *wbc,
		       ErrorInfo **ret_error)
{
	ServiceLoaderDataUI *loader_data;
	PyObject *fn, *ret;

	g_return_if_fail (_PyGObject_API != NULL);

	GO_INIT_RET_ERROR_INFO (ret_error);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	fn = PyDict_GetItemString (loader_data->ui_actions, action->id);
	if (fn == NULL) {
		*ret_error = error_info_new_printf (_("Unknown action: %s"),
						    action->id);
		return;
	} else if (!PyFunction_Check (fn)) {
		*ret_error = error_info_new_printf (
			_("Not a valid function for action: %s"), action->id);
		return;
	}
	ret = PyObject_CallFunction (fn, (char *) "N",
				     py_new_Gui_object (WBC_GTK (wbc)));
	if (ret == NULL) {
		*ret_error = error_info_new_str (py_exc_to_string ());
		PyErr_Clear ();
	} else {
		Py_DECREF (ret);
	}
}

static void
gplp_load_service_ui (GOPluginLoader *loader,
		      GOPluginService *service,
		      ErrorInfo **ret_error)
{

	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (loader);
	gchar *ui_action_names;
	PyObject *ui_actions;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_UI (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_py_interpreter_switch_to (loader_python->py_interpreter_info);
	ui_action_names = g_strconcat (plugin_service_get_id (service),
				     "_ui_actions", NULL);
	ui_actions = PyDict_GetItemString (loader_python->main_module_dict,
					   ui_action_names);
	gnm_python_clear_error_if_needed (loader_python->py_object);
	if (ui_actions != NULL && PyDict_Check (ui_actions)) {
		PluginServiceUICallbacks *cbs;
		ServiceLoaderDataUI *loader_data;

		cbs = plugin_service_get_cbs (service);
		cbs->plugin_func_exec_action = gplp_func_exec_action;

		loader_data = g_new (ServiceLoaderDataUI, 1);
		loader_data->ui_actions = ui_actions;
		Py_INCREF (loader_data->ui_actions);
		g_object_set_data_full
			(G_OBJECT (service), "loader_data", loader_data,
			 (GDestroyNotify) gplp_loader_data_ui_free);
	} else {
		*ret_error = error_info_new_printf (
		             _("Python file \"%s\" has invalid format."),
		             loader_python->module_name);
		if (ui_actions == NULL) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("File doesn't contain \"%s\" dictionary."),
			                        ui_action_names));
		} else if (!PyDict_Check (ui_actions)) {
			error_info_add_details (*ret_error,
			                        error_info_new_printf (
			                        _("Object \"%s\" is not a dictionary."),
			                        ui_action_names));
		}
	}
	g_free (ui_action_names);
}

static gboolean
gplp_service_load (GOPluginLoader *l, GOPluginService *s, ErrorInfo **err)
{
	if (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (s))
		gplp_load_service_function_group (l, s, err);
	else if (IS_GNM_PLUGIN_SERVICE_UI (s))
		gplp_load_service_ui (l, s, err);
	else
		return FALSE;
	return TRUE;
}

static gboolean
gplp_service_unload (GOPluginLoader *l, GOPluginService *s, ErrorInfo **err)
{
	if (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (s))
		;
	else if (IS_GNM_PLUGIN_SERVICE_UI (s))
		gplp_unload_service_function_group (l, s, err);
	else
		return FALSE;
	return TRUE;
}

static void
gplp_finalize (GObject *obj)
{
	GnmPythonPluginLoader *loader_python = GNM_PYTHON_PLUGIN_LOADER (obj);

	g_free (loader_python->module_name);
	loader_python->module_name = NULL;

	G_OBJECT_CLASS (g_type_class_peek (G_TYPE_OBJECT))->finalize (obj);
}

static void
gplp_init (GnmPythonPluginLoader *loader_python)
{
	g_return_if_fail (IS_GNM_PYTHON_PLUGIN_LOADER (loader_python));

	loader_python->module_name = NULL;
	loader_python->py_object = NULL;
	loader_python->py_interpreter_info = NULL;
}

static void
gplp_class_init (GObjectClass *gobject_class)
{
	gobject_class->finalize = gplp_finalize;
}
static void
go_plugin_loader_init (GOPluginLoaderClass *iface)
{
	iface->set_attributes		= gplp_set_attributes;
	iface->load_base		= gplp_load_base;
	iface->unload_base		= gplp_unload_base;
	iface->load_service_file_opener	= gplp_load_service_file_opener;
	iface->load_service_file_saver	= gplp_load_service_file_saver;

	iface->service_load		= gplp_service_load;
	iface->service_unload		= gplp_service_unload;
}

GSF_DYNAMIC_CLASS_FULL (GnmPythonPluginLoader, gnm_python_plugin_loader,
	NULL, NULL, gplp_class_init, NULL,
	gplp_init, G_TYPE_OBJECT, 0,
	GSF_INTERFACE_FULL (gnm_python_plugin_loader_type, go_plugin_loader_init, GO_PLUGIN_LOADER_TYPE))
