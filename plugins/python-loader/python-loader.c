/*
 * python-loader.c: Support for Python plugins.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <Python.h>
#ifdef WITH_PYGTK
#include "pygobject.h"
#endif
#include <stdlib.h>
#include <glib.h>
#include "application.h"
#include "workbook.h"
#include "sheet.h"
#include "workbook-view.h"
#include "workbook-control-gui.h"
#include "gui-util.h"
#include "value.h"
#include "expr.h"
#include "expr-impl.h"
#include "io-context.h"
#include "plugin-util.h"
#include "plugin.h"
#include "plugin-service.h"
#include "plugin-loader.h"
#include "module-plugin-defs.h"
#include "py-gnumeric.h"
#include "gnm-python.h"
#include "python-loader.h"

typedef struct _GnumericPluginLoaderPython GnumericPluginLoaderPython;

struct _GnumericPluginLoaderPython {
	GnumericPluginLoader loader;

	gchar *module_name;

	GnmPython *py_object;
	GnmPyInterpreter *py_interpreter_info;
	PyObject *main_module;
	PyObject *main_module_dict;
};

typedef struct {
	GnumericPluginLoaderClass parent_class;
} GnumericPluginLoaderPythonClass;

static GObjectClass *parent_class = NULL;

static void gnumeric_plugin_loader_python_set_attributes (GnumericPluginLoader *loader, GHashTable *attrs, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_python_load_base (GnumericPluginLoader *loader, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_python_unload_base (GnumericPluginLoader *loader, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_python_load_service_file_opener (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_python_load_service_file_saver (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_python_load_service_function_group (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);
static void gnumeric_plugin_loader_python_unload_service_function_group (GnumericPluginLoader *loader, PluginService *service, ErrorInfo **ret_error);

#define PLUGIN_GET_LOADER(plugin) \
	GNUMERIC_PLUGIN_LOADER_PYTHON (g_object_get_data (G_OBJECT (plugin), "python-loader"))
#define SERVICE_GET_LOADER(service) \
	PLUGIN_GET_LOADER (plugin_service_get_plugin (service))
#define SWITCH_TO_PLUGIN(plugin) \
	gnm_py_interpreter_switch_to (PLUGIN_GET_LOADER (plugin)->py_interpreter_info)


static void
gnumeric_plugin_loader_python_set_attributes (GnumericPluginLoader *loader, GHashTable *attrs, ErrorInfo **ret_error)
{
	GnumericPluginLoaderPython *loader_python = GNUMERIC_PLUGIN_LOADER_PYTHON (loader);
	gchar *module_name = NULL;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	module_name = g_hash_table_lookup (attrs, "module_name");
	if (module_name != NULL) {
		loader_python->module_name = g_strdup (module_name);
	} else {
		*ret_error = error_info_new_str (
		             _("Python module name not given."));
	}
}

static void
gnumeric_plugin_loader_python_load_base (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderPython *loader_python = GNUMERIC_PLUGIN_LOADER_PYTHON (loader);
	const gchar *python_file_extensions[]
		= {"py", "pyc", "pyo", NULL}, **file_ext;
	GnmPython *py_object;
	GnmPyInterpreter *py_interpreter_info;
	gchar *full_module_file_name = NULL;
	FILE *f;
	ErrorInfo *open_error;
	PyObject *modules, *main_module, *main_module_dict;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	g_object_set_data (G_OBJECT (loader->plugin), "python-loader", loader);

	py_object = gnm_python_object_get ();
	py_interpreter_info = gnm_python_new_interpreter (py_object, loader->plugin);
	if (py_interpreter_info == NULL) {
		*ret_error = error_info_new_str ("Cannot create new Python interpreter.");
		gnm_python_clear_error_if_needed (py_object);
		g_object_unref (py_object);
		return;
	}

	for (file_ext = python_file_extensions; *file_ext != NULL; file_ext++) {
		gchar *file_name = g_strconcat (
			loader_python->module_name, ".", *file_ext, NULL);
		gchar *path = g_build_filename (
			gnm_plugin_get_dir_name (loader->plugin),
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
gnumeric_plugin_loader_python_unload_base (GnumericPluginLoader *loader, ErrorInfo **ret_error)
{
	GnumericPluginLoaderPython *loader_python = GNUMERIC_PLUGIN_LOADER_PYTHON (loader);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	g_object_steal_data (G_OBJECT (loader->plugin), "python-loader");
	gnm_python_destroy_interpreter (
		loader_python->py_object, loader_python->py_interpreter_info);
	g_object_unref (loader_python->py_object);
}

static void
gnumeric_plugin_loader_python_init (GnumericPluginLoaderPython *loader_python)
{
	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_PYTHON (loader_python));

	loader_python->module_name = NULL;
	loader_python->py_object = NULL;
	loader_python->py_interpreter_info = NULL;
}

static void
gnumeric_plugin_loader_python_finalize (GObject *obj)
{
	GnumericPluginLoaderPython *loader_python = GNUMERIC_PLUGIN_LOADER_PYTHON (obj);

	g_free (loader_python->module_name);
	loader_python->module_name = NULL;

	parent_class->finalize (obj);
}

static void
gnumeric_plugin_loader_python_class_init (GObjectClass *gobject_class)
{
	GnumericPluginLoaderClass *gnumeric_plugin_loader_class =  GNUMERIC_PLUGIN_LOADER_CLASS (gobject_class);

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnumeric_plugin_loader_python_finalize;

	gnumeric_plugin_loader_class->set_attributes = gnumeric_plugin_loader_python_set_attributes;
	gnumeric_plugin_loader_class->load_base = gnumeric_plugin_loader_python_load_base;
	gnumeric_plugin_loader_class->unload_base = gnumeric_plugin_loader_python_unload_base;
	gnumeric_plugin_loader_class->load_service_file_opener = gnumeric_plugin_loader_python_load_service_file_opener;
	gnumeric_plugin_loader_class->load_service_file_saver = gnumeric_plugin_loader_python_load_service_file_saver;
	gnumeric_plugin_loader_class->load_service_function_group = gnumeric_plugin_loader_python_load_service_function_group;
	gnumeric_plugin_loader_class->unload_service_function_group = gnumeric_plugin_loader_python_unload_service_function_group;
}

PLUGIN_CLASS (
	GnumericPluginLoaderPython, gnumeric_plugin_loader_python,
	gnumeric_plugin_loader_python_class_init,
	gnumeric_plugin_loader_python_init, TYPE_GNUMERIC_PLUGIN_LOADER)

/*
 * Service - file_opener
 */

typedef struct {
	PyObject *python_func_file_probe;
	PyObject *python_func_file_open;
} ServiceLoaderDataFileOpener;

static gboolean
gnumeric_plugin_loader_python_func_file_probe (
	GnumFileOpener const *fo, PluginService *service,
	GsfInput *input, FileProbeLevel pl)
{
#ifndef WITH_PYGTK
	g_warning ("Probing from Python plugins requires gnome-python "
		   "and Python bindings for libgsf");
	return FALSE;
#else	
	ServiceLoaderDataFileOpener *loader_data;
	PyObject *probe_result = NULL;
	PyObject *input_wrapper;
	gboolean result;

	g_return_val_if_fail (GNM_IS_PLUGIN_SERVICE_FILE_OPENER (service), FALSE);
	g_return_val_if_fail (input != NULL, FALSE);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	input_wrapper = pygobject_new (G_OBJECT (input));
	if (input_wrapper == NULL) {
		g_warning (convert_python_exception_to_string ());
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
	}
	if (input_wrapper != NULL) {
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
#endif
}

static void
gnumeric_plugin_loader_python_func_file_open (GnumFileOpener const *fo, 
					      PluginService *service,
                                              IOContext *io_context, 
					      WorkbookView *wb_view,
                                              GsfInput *input)
{
#ifndef WITH_PYGTK
	gnumeric_io_error_string
		(io_context,
		 "File opening from python plugins requires gnome-python "
		 "and Python bindings for libgsf");
#else	
	ServiceLoaderDataFileOpener *loader_data;
	Sheet *sheet;
	PyObject *open_result = NULL;
	PyObject *input_wrapper;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FILE_OPENER (service));
	g_return_if_fail (input != NULL);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	sheet = sheet_new (wb_view_workbook (wb_view), _("Some name"));
	input_wrapper = pygobject_new (G_OBJECT (input));
	if (input_wrapper != NULL) {
		open_result = PyObject_CallFunction
			(loader_data->python_func_file_open,
			 (char *) "NO", 
			 py_new_Sheet_object (sheet), input_wrapper);
		Py_DECREF (input_wrapper);
	}
	if (open_result != NULL) {
		Py_DECREF (open_result);
		workbook_sheet_attach (wb_view_workbook (wb_view), sheet, NULL);
	} else {
		gnumeric_io_error_string (io_context, convert_python_exception_to_string ());
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
		sheet_destroy (sheet);
	}
#endif
}

static void
gnumeric_plugin_loader_python_load_service_file_opener (GnumericPluginLoader *loader,
                                                        PluginService *service,
                                                        ErrorInfo **ret_error)
{
	GnumericPluginLoaderPython *loader_python = GNUMERIC_PLUGIN_LOADER_PYTHON (loader);
	gchar *func_name_file_probe, *func_name_file_open;
	PyObject *python_func_file_probe, *python_func_file_open;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FILE_OPENER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
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
		cbs->plugin_func_file_probe = gnumeric_plugin_loader_python_func_file_probe;
		cbs->plugin_func_file_open = gnumeric_plugin_loader_python_func_file_open;

		loader_data = g_new (ServiceLoaderDataFileOpener, 1);
		loader_data->python_func_file_probe = python_func_file_probe;
		loader_data->python_func_file_open = python_func_file_open;
		g_object_set_data (G_OBJECT (service), "loader_data", loader_data);
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
gnumeric_plugin_loader_python_func_file_save (GnumFileSaver const *fs, PluginService *service,
                                              IOContext *io_context, WorkbookView *wb_view,
                                              const gchar *file_name)
{
	ServiceLoaderDataFileSaver *saver_data;
	PyObject *py_workbook;
	PyObject *save_result;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FILE_SAVER (service));
	g_return_if_fail (file_name != NULL);

	saver_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	py_workbook = py_new_Workbook_object (wb_view_workbook (wb_view));
	save_result = PyObject_CallFunction
		(saver_data->python_func_file_save,
		 (char *) "Ns", py_workbook, file_name);
	if (save_result != NULL) {
		Py_DECREF (save_result);
	} else {
		gnumeric_io_error_string (io_context, convert_python_exception_to_string ());
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
	}
}

static void
gnumeric_plugin_loader_python_load_service_file_saver (GnumericPluginLoader *loader,
                                                       PluginService *service,
                                                       ErrorInfo **ret_error)
{
	GnumericPluginLoaderPython *loader_python = GNUMERIC_PLUGIN_LOADER_PYTHON (loader);
	gchar *func_name_file_save;
	PyObject *python_func_file_save;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FILE_SAVER (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
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
		cbs->plugin_func_file_save = gnumeric_plugin_loader_python_func_file_save;

		saver_data = g_new (ServiceLoaderDataFileSaver, 1);
		saver_data->python_func_file_save = python_func_file_save;
		g_object_set_data (G_OBJECT (service), "loader_data", saver_data);
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


static Value *
call_python_function_args (FunctionEvalInfo *ei, Value **args)
{
	PluginService *service;
	ServiceLoaderDataFunctionGroup *loader_data;
	PyObject *fn_info_tuple;
	PyObject *python_fn;
	FunctionDefinition const * fndef;

	gint min_n_args, max_n_args, n_args;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);
	g_return_val_if_fail (args != NULL, NULL);

	fndef = ei->func_call->func;
	service = (PluginService *) function_def_get_user_data (fndef);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	fn_info_tuple = PyDict_GetItemString (loader_data->python_fn_info_dict,
	                                      (gchar *) function_def_get_name (fndef));
	g_assert (fn_info_tuple != NULL);
	python_fn = PyTuple_GetItem (fn_info_tuple, 2);
	function_def_count_args (fndef, &min_n_args, &max_n_args);
	for (n_args = min_n_args; n_args < max_n_args && args[n_args] != NULL; n_args++) {
		;
	}
	return call_python_function (python_fn, ei->pos, n_args, args);
}

static Value *
call_python_function_nodes (FunctionEvalInfo *ei, GnmExprList *expr_tree_list)
{
	PluginService *service;
	ServiceLoaderDataFunctionGroup *loader_data;
	PyObject *python_fn;
	FunctionDefinition const * fndef;
	Value **values;
	gint n_args, i;
	GnmExprList *l;
	Value *ret_value;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);

	fndef = ei->func_call->func;
	service = (PluginService *) function_def_get_user_data (fndef);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	python_fn = PyDict_GetItemString (loader_data->python_fn_info_dict,
	                                  (gchar *) function_def_get_name (fndef));

	n_args = gnm_expr_list_length (expr_tree_list);
	values = g_new (Value *, n_args);
	for (i = 0, l = expr_tree_list; l != NULL; i++, l = l->next) {
		values[i] = gnm_expr_eval (l->data, ei->pos, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
	}
	ret_value = call_python_function (python_fn, ei->pos, n_args, values);
	for (i = 0; i < n_args; i++) {
		value_release (values[i]);
	}
	g_free (values);

	return ret_value;
}

static const gchar **
python_function_get_gnumeric_help (PyObject *python_fn_info_dict, PyObject *python_fn,
                                   const gchar *fn_name)
{
	gchar *help_attr_name;
	PyObject *cobject_help_value;

	help_attr_name = g_strdup_printf ("_CGnumericHelp_%s", fn_name);
	cobject_help_value = PyDict_GetItemString (python_fn_info_dict, help_attr_name);
	if (cobject_help_value == NULL) {
		PyObject *python_fn_help;
		gchar *help_str, **help_value;

		python_fn_help = ((PyFunctionObject *) python_fn)->func_doc;
		if (python_fn_help != NULL && PyString_Check (python_fn_help)) {
			help_str = PyString_AsString (python_fn_help);
		} else {
			help_str = NULL;
		}
		help_value = g_new (gchar *, 2);
		help_value[0] = help_str;
		help_value[1] = NULL;
		cobject_help_value = PyCObject_FromVoidPtr (help_value, &g_free);
		PyDict_SetItemString (python_fn_info_dict, help_attr_name, cobject_help_value);
	}
	g_free (help_attr_name);

	return (const gchar **) PyCObject_AsVoidPtr (cobject_help_value);
}

static gboolean
gnumeric_plugin_loader_python_func_get_full_function_info (PluginService *service,
							   const gchar *fn_name,
							   const gchar **args_ptr,
							   const gchar **arg_names_ptr,
							   const gchar ***help_ptr,
							   FunctionArgs	 *fn_args_ptr,
							   FunctionNodes *fn_nodes_ptr,
							   FuncLinkHandle   *link,
							   FuncUnlinkHandle *unlink)
{
	ServiceLoaderDataFunctionGroup *loader_data;
	PyObject *fn_info_obj;

	g_return_val_if_fail (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service), FALSE);
	g_return_val_if_fail (fn_name != NULL, FALSE);

	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	fn_info_obj = PyDict_GetItemString (loader_data->python_fn_info_dict, (gchar *) fn_name);
	if (fn_info_obj == NULL) {
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
		return FALSE;
	} else if (PyTuple_Check (fn_info_obj)) {
		PyObject *python_args, *python_arg_names;
		PyObject *python_fn;

		if (PyTuple_Size (fn_info_obj) == 3 &&
		    (python_args = PyTuple_GetItem (fn_info_obj, 0)) != NULL &&
			PyString_Check (python_args) &&
		    (python_arg_names = PyTuple_GetItem (fn_info_obj, 1)) != NULL &&
		    PyString_Check (python_arg_names) &&
		    (python_fn = PyTuple_GetItem (fn_info_obj, 2)) != NULL &&
		    PyFunction_Check (python_fn)) {
			*args_ptr = PyString_AsString (python_args);
			*arg_names_ptr = PyString_AsString  (python_arg_names);
			*help_ptr = python_function_get_gnumeric_help (loader_data->python_fn_info_dict,
			                                               python_fn, fn_name);
			*fn_args_ptr = &call_python_function_args;
			*fn_nodes_ptr = NULL;
			*link = NULL;
			*unlink = NULL;
			return TRUE;
		} else {
			gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
			return FALSE;
		}
	} else if (PyFunction_Check (fn_info_obj)) {
		*args_ptr = (char *) "";
		*arg_names_ptr = (char *) "";
		*help_ptr = python_function_get_gnumeric_help (loader_data->python_fn_info_dict,
		                                               fn_info_obj, fn_name);
		*fn_args_ptr = NULL;
		*fn_nodes_ptr = &call_python_function_nodes;
		*link = NULL;
		*unlink = NULL;
		return TRUE;
	} else {
		gnm_python_clear_error_if_needed (SERVICE_GET_LOADER (service)->py_object);
		return FALSE;
	}
}

static void
gnumeric_plugin_loader_python_load_service_function_group (GnumericPluginLoader *loader,
                                                           PluginService *service,
                                                           ErrorInfo **ret_error)
{
	GnumericPluginLoaderPython *loader_python = GNUMERIC_PLUGIN_LOADER_PYTHON (loader);
	gchar *fn_info_dict_name;
	PyObject *python_fn_info_dict;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
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
		cbs->plugin_func_get_full_function_info =
			&gnumeric_plugin_loader_python_func_get_full_function_info;

		loader_data = g_new (ServiceLoaderDataFunctionGroup, 1);
		loader_data->python_fn_info_dict = (PyObject *) python_fn_info_dict;
		Py_INCREF (loader_data->python_fn_info_dict);
		g_object_set_data (G_OBJECT (service), "loader_data", loader_data);
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
gnumeric_plugin_loader_python_unload_service_function_group (GnumericPluginLoader *loader,
                                                             PluginService *service,
                                                             ErrorInfo **ret_error)
{
	ServiceLoaderDataFunctionGroup *loader_data;

	g_return_if_fail (IS_GNUMERIC_PLUGIN_LOADER_PYTHON (loader));
	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	SWITCH_TO_PLUGIN (plugin_service_get_plugin (service));
	Py_DECREF (loader_data->python_fn_info_dict);
}
