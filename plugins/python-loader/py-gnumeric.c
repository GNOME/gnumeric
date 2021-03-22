/*
 * py-gnumeric.c - "Gnumeric" module for Python
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <application.h>
#include <workbook.h>
#include <cell.h>
#include <mstyle.h>
#include <sheet.h>
#include <sheet-style.h>
#include <position.h>
#include <value.h>
#include <expr.h>
#include <func.h>
#include <wbc-gtk.h>
#include <parse-util.h>
#include "gnm-py-interpreter.h"
#include "py-gnumeric.h"

#include <goffice/goffice.h>

#include <glib/gi18n-lib.h>

#include <Python.h>
#define NO_IMPORT_PYGOBJECT
#include <pygobject.h>

static PyObject *GnmModule = NULL;

static PyTypeObject py_CellRef_object_type;
typedef struct _py_CellRef_object py_CellRef_object;

static PyTypeObject py_RangeRef_object_type;
typedef struct _py_RangeRef_object py_RangeRef_object;
static PyObject *py_new_RangeRef_object (GnmRangeRef const *range_ref);
static GnmRangeRef *py_RangeRef_as_RangeRef (py_RangeRef_object *self);

typedef struct _py_GnumericFunc_object py_GnumericFunc_object;
static PyTypeObject py_GnumericFunc_object_type;

typedef struct _py_GnumericFuncDict_object py_GnumericFuncDict_object;
static PyTypeObject py_GnumericFuncDict_object_type;

static PyTypeObject py_GnmPlugin_object_type;
typedef struct _py_GnmPlugin_object py_GnmPlugin_object;

/*
Available types, attributes, methods, etc.:
(please update this list after adding/removing anything)

GnumericFunc:
	call

GnumericFuncDict:
	subscript

GOPlugin:
	Methods:
	- get_dir_name
	- get_id
	- get_name
	- get_description

Module Gnumeric:
	Attributes:
	- TRUE/FALSE    (values of type Boolean)
	- GnumericError (exception), GnumericError* (std exception values)
	- functions     (dictionary containing all Gnumeric functions)

*/


// Like PyDict_SetItemString, but takes ownership of val
static void
gnm_py_dict_store (PyObject *dict, const char *key, PyObject *val)
{
	PyDict_SetItemString (dict, key, val);
	Py_DECREF (val);
}





#define GNUMERIC_MODULE \
	(PyImport_AddModule ("Gnumeric"))
#define GNUMERIC_MODULE_GET(key) \
	PyDict_GetItemString (PyModule_GetDict (GNUMERIC_MODULE), (key))
#define GNUMERIC_MODULE_SET(key, val) \
	gnm_py_dict_store (PyModule_GetDict (GNUMERIC_MODULE), (key), val)
#define SET_EVAL_POS(val) \
	GNUMERIC_MODULE_SET ("Gnumeric_eval_pos", PyCapsule_New (val, "eval_pos", NULL))
#define UNSET_EVAL_POS \
	PyDict_DelItemString (PyModule_GetDict (GNUMERIC_MODULE), "Gnumeric_eval_pos")

static GnmValue *
py_obj_to_gnm_value (const GnmEvalPos *eval_pos, PyObject *py_val)
{
	PyObject *py_val_type;
	GnmValue *ret_val;

	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (py_val != NULL, NULL);

	py_val_type = PyObject_Type (py_val);
	if (py_val_type == NULL) {
		PyErr_Clear ();
		ret_val = value_new_empty ();
	} else if (py_val == Py_None) {
		ret_val = value_new_empty ();
	} else if (PyBool_Check (py_val)) {
		ret_val = value_new_bool (py_val == Py_True);
	} else if (PyLong_Check (py_val)) {
		ret_val = value_new_float ((gnm_float)PyLong_AsLong (py_val));
	} else if (PyFloat_Check (py_val)) {
		ret_val = value_new_float ((gnm_float) PyFloat_AsDouble (py_val));
	} else if (PyUnicode_Check (py_val)) {
		ret_val = value_new_string (PyUnicode_AsUTF8 (py_val));
	} else if (py_val_type == (PyObject *) &py_RangeRef_object_type) {
		GnmRangeRef *range_ref;

		range_ref = py_RangeRef_as_RangeRef ((py_RangeRef_object *) py_val);
		ret_val = value_new_cellrange_unsafe (&range_ref->a, &range_ref->b);
	} else if (PyList_Check (py_val)) {
		gint n_cols = 0, n_rows = 0, x, y;
		PyObject *col;
		gboolean valid_format = TRUE;

		if ((n_cols = PyList_Size (py_val)) > 0 &&
		    (col = PyList_GetItem (py_val, 0)) != NULL &&
		    PyList_Check (col) && (n_rows = PyList_Size (col)) > 0) {
			for (x = 1; x < n_cols; x++) {
				col = PyList_GetItem (py_val, x);
				if (col == NULL || !PyList_Check (col) || PyList_Size (col) != n_rows) {
					valid_format = FALSE;
					break;
				}
			}
		} else {
			valid_format = FALSE;
		}
		if (valid_format) {
			ret_val = value_new_array_empty (n_cols, n_rows);
			for (x = 0; x < n_cols; x++) {
				col = PyList_GetItem (py_val, x);
				for (y = 0; y < n_rows; y++) {
					PyObject *python_val;

					python_val = PyList_GetItem (col, y);
					g_assert (python_val != NULL);
					ret_val->v_array.vals[x][y] = py_obj_to_gnm_value (eval_pos, python_val);
				}
			}
		} else {
			ret_val = value_new_error (eval_pos, _("Python list is not an array"));
		}
	} else {
		PyObject *py_val_type_str;
		gchar *msg;

		py_val_type_str = PyObject_Str (py_val_type);
		msg = g_strdup_printf (_("Unsupported Python type: %s"),
				       PyUnicode_AsUTF8 (py_val_type_str));
		ret_val = value_new_error (eval_pos, msg);
		g_free (msg);
		Py_DECREF (py_val_type_str);
	}
	Py_XDECREF (py_val_type);

	return ret_val;
}

gchar *
py_exc_to_string (void)
{
	PyObject *exc_type, *exc_value, *exc_traceback;
	PyObject *exc_type_str = NULL, *exc_value_str = NULL;
	gchar *error_str;

	g_return_val_if_fail (PyErr_Occurred () != NULL, NULL);

	PyErr_Fetch (&exc_type, &exc_value, &exc_traceback);
	if (PyErr_GivenExceptionMatches (exc_type, GNUMERIC_MODULE_GET ("GnumericError"))) {
		if (exc_value != NULL) {
			exc_value_str = PyObject_Str (exc_value);
			g_assert (exc_value_str != NULL);
			error_str = g_strdup (PyUnicode_AsUTF8 (exc_value_str));
		} else {
			error_str = g_strdup (_("Unknown error"));
		}
	} else {
		exc_type_str = PyObject_Str (exc_type);
		if (exc_value != NULL) {
			exc_value_str = PyObject_Str (exc_value);
			error_str = g_strdup_printf (_("Python exception (%s: %s)"),
						     PyUnicode_AsUTF8 (exc_type_str),
						     PyUnicode_AsUTF8 (exc_value_str));
		} else {
			error_str = g_strdup_printf (_("Python exception (%s)"),
						     PyUnicode_AsUTF8 (exc_type_str));
		}
	}

	Py_DECREF (exc_type);
	Py_XDECREF (exc_value);
	Py_XDECREF (exc_traceback);
	Py_XDECREF (exc_type_str);
	Py_XDECREF (exc_value_str);

	return error_str;
}

static GnmValue *
py_exc_to_gnm_value (const GnmEvalPos *eval_pos)
{
	gchar *error_str = py_exc_to_string ();
	GnmValue *ret_value = value_new_error (eval_pos, error_str);

	g_free (error_str);
	return ret_value;
}

static PyObject *
gnm_value_to_py_obj (const GnmEvalPos *eval_pos, const GnmValue *val)
{
	PyObject *py_val = NULL;

	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (val != NULL, NULL);

	switch (val->v_any.type) {
	case VALUE_BOOLEAN:
		py_val = value_get_as_checked_bool (val) ? Py_True : Py_False;
		Py_INCREF (py_val);
		break;
	case VALUE_FLOAT:
		py_val = PyFloat_FromDouble (value_get_as_float (val));
		break;
	case VALUE_STRING:
		py_val = PyUnicode_FromString (value_peek_string (val));
		break;
	case VALUE_CELLRANGE:
		py_val = py_new_RangeRef_object (&val->v_range.cell);
		break;
	case VALUE_ARRAY: {
		gint x;

		py_val = PyList_New(val->v_array.x);
		g_return_val_if_fail (py_val != NULL, NULL);
		for (x = 0; x < val->v_array.x; x++) {
			PyObject *col, *python_val;
			gint y;

			col = PyList_New(val->v_array.y);
			for (y = 0; y < val->v_array.y; y++) {
				python_val = gnm_value_to_py_obj (eval_pos, val->v_array.vals[x][y]);
				(void) PyList_SetItem (col, y, python_val);
			}
			(void) PyList_SetItem (py_val, x, col);
		}
		break;
	}
	case VALUE_ERROR:
		g_warning ("gnm_value_to_py_obj(): unsupported value type");
	case VALUE_EMPTY:
		Py_INCREF (Py_None);
		py_val = Py_None;
		break;
	default:
		g_assert_not_reached ();
	}

	return py_val;
}

static const GnmEvalPos *
get_eval_pos (void)
{
	PyObject *gep = GNUMERIC_MODULE_GET ("Gnumeric_eval_pos");

	return gep ? PyCapsule_GetPointer (gep, "eval_pos") : NULL;
}

static PyObject *
python_call_gnumeric_function (GnmFunc *fn_def, const GnmEvalPos *opt_eval_pos, PyObject *args)
{
	gint n_args, i;
	GnmValue **values, *ret_val;
	PyObject *py_ret_val;
	const GnmEvalPos *eval_pos;

	g_return_val_if_fail (fn_def != NULL, NULL);
	g_return_val_if_fail (args != NULL && PySequence_Check (args), NULL);

	if (opt_eval_pos != NULL) {
		eval_pos = opt_eval_pos;
	} else {
		eval_pos = get_eval_pos ();
	}
	if (eval_pos == NULL) {
		PyErr_SetString (GNUMERIC_MODULE_GET ("GnumericError"),
				 "Missing Evaluation Position.");
		return NULL;
	}

	n_args = PySequence_Length (args);
	values = g_new (GnmValue *, n_args);
	for (i = 0; i < n_args; i++) {
		PyObject *py_val;

		py_val = PySequence_GetItem (args, i);
		g_assert (py_val != NULL);
		values[i] = py_obj_to_gnm_value (eval_pos, py_val);
	}

	ret_val = function_def_call_with_values (eval_pos, fn_def, n_args,
						 (GnmValue const * const *)values);
	py_ret_val = gnm_value_to_py_obj (eval_pos, ret_val);
	value_release (ret_val);
	for (i = 0; i < n_args; i++) {
		value_release (values[i]);
	}
	g_free (values);

	return py_ret_val;
}

GnmValue *
call_python_function (PyObject *python_fn, GnmEvalPos const *eval_pos, gint n_args, GnmValue const * const *args)
{
	PyObject *python_args;
	PyObject *python_ret_value;
	gint i;
	GnmValue *ret_value;
	gboolean eval_pos_set;

	g_return_val_if_fail (python_fn != NULL && PyCallable_Check (python_fn), NULL);

	python_args = PyTuple_New (n_args);
	g_return_val_if_fail (python_args != NULL, NULL);
	for (i = 0; i < n_args; i++) {
		(void) PyTuple_SetItem (python_args, i,
					gnm_value_to_py_obj (eval_pos,
							     args[i]));
	}
	if (get_eval_pos () != NULL) {
		eval_pos_set = FALSE;
	} else {
		SET_EVAL_POS ((GnmEvalPos *) eval_pos);
		eval_pos_set = TRUE;
	}
	python_ret_value = PyObject_CallObject (python_fn, python_args);
	Py_DECREF (python_args);
	if (python_ret_value != NULL) {
		ret_value = py_obj_to_gnm_value (eval_pos, python_ret_value);
	} else {
		ret_value = py_exc_to_gnm_value (eval_pos);
		PyErr_Clear ();
	}
	if (eval_pos_set) {
		UNSET_EVAL_POS;
	}

	return ret_value;
}


/*
 * CellRef
 */

/*
 * FIXME: The sheet and cells the GnmCellRef refer to may be deleted
 * behind our backs.
 */

struct _py_CellRef_object {
	PyObject_HEAD
	GnmCellRef cell_ref;
};

static struct PyMethodDef py_CellRef_object_methods[] = {
/*	{"get_tuple", (PyCFunction) py_CellPos_get_tuple_method,   METH_VARARGS},*/
	{NULL, NULL}
};

#if 0
static GnmCellRef *
py_CellRef_as_CellRef (py_CellRef_object *self)
{
	return &self->cell_ref;
}
#endif

static PyObject *
py_CellRef_object_getattr (py_CellRef_object *self, gchar *name)
{
	PyObject *result;

	if (strcmp (name, "col") == 0) {
		result = PyLong_FromLong (self->cell_ref.col);
	} else if (strcmp (name, "row") == 0) {
		result = PyLong_FromLong (self->cell_ref.row);
	} else if (strcmp (name, "sheet") == 0) {
		if (self->cell_ref.sheet)
			result = pygobject_new (G_OBJECT (self->cell_ref.sheet));
		else {
			Py_INCREF (Py_None);
			result = Py_None;
		}
	} else if (strcmp (name, "col_relative") == 0) {
		result = PyBool_FromLong (self->cell_ref.col_relative);
	} else if (strcmp (name, "row_relative") == 0) {
		result = PyBool_FromLong (self->cell_ref.row_relative);
	} else {
		result = PyObject_CallMethod ((PyObject *) self, name, NULL);
	}

	return result;
}

static void
py_CellRef_object_dealloc (py_CellRef_object *self)
{
	PyObject_Del (self);
}

static PyObject *
py_new_CellRef_object (GnmCellRef const *cell_ref)
{
	py_CellRef_object *self;

	self = PyObject_NEW (py_CellRef_object, &py_CellRef_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->cell_ref = *cell_ref;

	return (PyObject *) self;
}

static PyTypeObject py_CellRef_object_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "CellRef",
	.tp_basicsize = sizeof (py_CellRef_object),
	.tp_dealloc = (destructor) &py_CellRef_object_dealloc,
	.tp_getattr = (getattrfunc) &py_CellRef_object_getattr,
	.tp_methods = py_CellRef_object_methods
};

/*
 * RangeRef
 */

/*
 * FIXME: The sheet and cells the GnmRangeRef refer to may be deleted
 * behind our backs.
 */

struct _py_RangeRef_object {
	PyObject_HEAD
	GnmRangeRef range_ref;
};

static PyObject *
py_RangeRef_get_tuple_method (py_RangeRef_object *self, PyObject *args);

static struct PyMethodDef py_RangeRef_object_methods[] = {
	{"get_tuple", (PyCFunction) py_RangeRef_get_tuple_method, METH_VARARGS},
	{NULL, NULL}
};

static GnmRangeRef *
py_RangeRef_as_RangeRef (py_RangeRef_object *self)
{
	return &self->range_ref;
}

static PyObject *
py_RangeRef_get_tuple_method (py_RangeRef_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, ":get_tuple")) {
		return NULL;
	}

	return Py_BuildValue ("(O&O&)",
			      py_new_CellRef_object, &self->range_ref.a,
			      py_new_CellRef_object, &self->range_ref.b);
}

static PyObject *
py_RangeRef_object_getattr (py_RangeRef_object *self, gchar *name)
{
	if (strcmp (name, "start") == 0) {
		return py_new_CellRef_object (&self->range_ref.a);
	} else if (strcmp (name, "end") == 0) {
		return py_new_CellRef_object (&self->range_ref.b);
	} else {
		return PyObject_CallMethod ((PyObject *) self, name, NULL);
	}
}

static void
py_RangeRef_object_dealloc (py_RangeRef_object *self)
{
	PyObject_Del (self);
}

static PyObject *
py_new_RangeRef_object (const GnmRangeRef *range_ref)
{
	py_RangeRef_object *self;

	self = PyObject_NEW (py_RangeRef_object, &py_RangeRef_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->range_ref = *range_ref;

	return (PyObject *) self;
}

static PyTypeObject py_RangeRef_object_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "RangeRef",
	.tp_basicsize = sizeof (py_RangeRef_object),
	.tp_dealloc = (destructor) &py_RangeRef_object_dealloc,
	.tp_getattr = (getattrfunc) &py_RangeRef_object_getattr,
	.tp_methods = py_RangeRef_object_methods /*  */
};

/*
 * GnmStyle
 */

struct _py_Style_object {
	PyObject_HEAD
	gboolean ro;
	union {
		GnmStyle const	*ro_style;
		GnmStyle	*rw_style;
	} u;
};

/*
 * GnumericFunc
 */

struct _py_GnumericFunc_object {
	PyObject_HEAD
	GnmFunc *fn_def;
	GnmEvalPos *eval_pos;
};

static PyObject *
py_GnumericFunc_call (py_GnumericFunc_object *self, PyObject *args, PyObject *keywords)
{
	return python_call_gnumeric_function (self->fn_def, self->eval_pos, args);
}

static void
py_GnumericFunc_object_dealloc (py_GnumericFunc_object *self)
{
	g_return_if_fail (self != NULL);

	gnm_func_dec_usage (self->fn_def);
	g_free (self->eval_pos);
	PyObject_Del (self);
}

static PyObject *
py_new_GnumericFunc_object (GnmFunc *fn_def, const GnmEvalPos *opt_eval_pos)
{
	py_GnumericFunc_object *self;

	self = PyObject_NEW (py_GnumericFunc_object, &py_GnumericFunc_object_type);
	if (self == NULL) {
		return NULL;
	}

	gnm_func_inc_usage (fn_def);
	self->fn_def = fn_def;
	if (opt_eval_pos != NULL) {
		self->eval_pos = g_new (GnmEvalPos, 1);
		*self->eval_pos = *opt_eval_pos;
	} else {
		self->eval_pos = NULL;
	}

	return (PyObject *) self;
}

static PyTypeObject py_GnumericFunc_object_type = {
	PyVarObject_HEAD_INIT(NULL,0)
	.tp_name = "GnumericFunc",
	.tp_basicsize = sizeof (py_GnumericFunc_object),
	.tp_dealloc = (destructor) &py_GnumericFunc_object_dealloc,
	.tp_call = (ternaryfunc) py_GnumericFunc_call
};

/*
 * GnumericFuncDict
 */

struct _py_GnumericFuncDict_object {
	PyObject_HEAD
	// PyObject *module_dict;
};


static PyObject *
py_GnumericFuncDict_subscript (py_GnumericFuncDict_object *self, PyObject *key)
{
	gchar *fn_name;
	GnmFunc *fn_def;

	if (!PyArg_Parse(key, "s", &fn_name)) {
		return NULL;
	}

	fn_def = gnm_func_lookup (fn_name, NULL);
	if (fn_def == NULL) {
		/* Py_INCREF (key); FIXME?? */
		PyErr_SetObject (PyExc_KeyError, key);
		return NULL;
	}

	return py_new_GnumericFunc_object (fn_def, NULL);
}

static void
py_GnumericFuncDict_object_dealloc (py_GnumericFuncDict_object *self)
{
	PyObject_Del (self);
}

static PyObject *
py_new_GnumericFuncDict_object (PyObject *module_dict)
{
	return (PyObject*) PyObject_NEW (py_GnumericFuncDict_object, &py_GnumericFuncDict_object_type);
}

PyMappingMethods py_GnumericFuncDict_mapping_methods = {
	.mp_length = NULL,
	.mp_subscript = (binaryfunc) py_GnumericFuncDict_subscript,
	.mp_ass_subscript = NULL
};

static PyTypeObject py_GnumericFuncDict_object_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "GnumericFuncDict",
	.tp_basicsize = sizeof (py_GnumericFuncDict_object),
	.tp_dealloc = (destructor) &py_GnumericFuncDict_object_dealloc,
	.tp_as_mapping = &py_GnumericFuncDict_mapping_methods
};

/*
- * GOPlugin
 */

struct _py_GnmPlugin_object {
	PyObject_HEAD
	GOPlugin *pinfo;
};

static PyObject *
py_GnmPlugin_get_dir_name_method (py_GnmPlugin_object *self, PyObject *args);
static PyObject *
py_GnmPlugin_get_id_method (py_GnmPlugin_object *self, PyObject *args);
static PyObject *
py_GnmPlugin_get_name_method (py_GnmPlugin_object *self, PyObject *args);
static PyObject *
py_GnmPlugin_get_description_method (py_GnmPlugin_object *self, PyObject *args);

static struct PyMethodDef py_GnmPlugin_object_methods[] = {
	{"get_dir_name",
	 (PyCFunction) py_GnmPlugin_get_dir_name_method,    METH_VARARGS},
	{"get_id",
	 (PyCFunction) py_GnmPlugin_get_id_method,          METH_VARARGS},
	{"get_name",
	 (PyCFunction) py_GnmPlugin_get_name_method,        METH_VARARGS},
	{"get_description",
	 (PyCFunction) py_GnmPlugin_get_description_method, METH_VARARGS},
	{NULL, NULL}
};

static PyObject *
py_GnmPlugin_get_dir_name_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, ":get_dir_name")) {
		return NULL;
	}

	return PyUnicode_FromString (go_plugin_get_dir_name (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_id_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, ":get_id")) {
		return NULL;
	}

	return PyUnicode_FromString (go_plugin_get_id (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_name_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, ":get_name")) {
		return NULL;
	}

	return PyUnicode_FromString (go_plugin_get_name (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_description_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, ":get_description")) {
		return NULL;
	}

	return PyUnicode_FromString (go_plugin_get_description (self->pinfo));
}

#if 0
static GOPlugin *
py_GnmPlugin_as_GnmPlugin (py_GnmPlugin_object *self)
{
	return self->pinfo;
}
#endif

static PyObject *
py_GnmPlugin_object_getattr (py_GnmPlugin_object *self, gchar *name)
{
	return PyUnicode_FromString (go_plugin_get_description (self->pinfo));
}

static void
py_GnmPlugin_object_dealloc (py_GnmPlugin_object *self)
{
	g_return_if_fail (self != NULL);

	g_object_unref (self->pinfo);
	PyObject_Del (self);
}

static PyObject *
py_new_GnmPlugin_object (GOPlugin *pinfo)
{
	py_GnmPlugin_object *self;

	self = PyObject_NEW (py_GnmPlugin_object, &py_GnmPlugin_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->pinfo = pinfo;
	g_object_ref (self->pinfo);

	return (PyObject *) self;
}

static PyTypeObject py_GnmPlugin_object_type = {
	PyVarObject_HEAD_INIT(&PyType_Type, 0)
	.tp_name = "GOPlugin",
	.tp_basicsize = sizeof (py_GnmPlugin_object),
	.tp_dealloc = (destructor) &py_GnmPlugin_object_dealloc,
	.tp_getattr = (getattrfunc) &py_GnmPlugin_object_getattr,
	.tp_methods = py_GnmPlugin_object_methods
};

static void
init_err (PyObject *module_dict, const char *name, GnmStdError e)
{
	GnmValue *v = value_new_error_std (NULL, e);

	gnm_py_dict_store
		(module_dict, name,
		 PyUnicode_FromString (v->v_err.mesg->str));

	value_release (v);
}


PyObject *
py_initgnumeric (void)
{
	PyObject *module_dict;

	static struct PyModuleDef GnmModuleDef = {
		PyModuleDef_HEAD_INIT,	/* m_base */
		.m_name = "Gnumeric",
	};

	if (GnmModule)
		return GnmModule;
	GnmModule = PyModule_Create (&GnmModuleDef);
	module_dict = PyModule_GetDict (GnmModule);

	// For historical reasons.  New code use python True/False.
	gnm_py_dict_store (module_dict, "TRUE", PyBool_FromLong (TRUE));
	gnm_py_dict_store (module_dict, "FALSE", PyBool_FromLong (FALSE));

	gnm_py_dict_store
		(module_dict, "GnumericError",
		 PyErr_NewException ("Gnumeric.GnumericError",
				     NULL, NULL));

	init_err (module_dict, "GnumericErrorNULL", GNM_ERROR_NULL);
	init_err (module_dict, "GnumericErrorDIV0", GNM_ERROR_DIV0);
	init_err (module_dict, "GnumericErrorVALUE", GNM_ERROR_VALUE);
	init_err (module_dict, "GnumericErrorREF", GNM_ERROR_REF);
	init_err (module_dict, "GnumericErrorNAME", GNM_ERROR_NAME);
	init_err (module_dict, "GnumericErrorNUM", GNM_ERROR_NUM);
	init_err (module_dict, "GnumericErrorNA", GNM_ERROR_NA);

	gnm_py_dict_store
		(module_dict, "functions",
		 py_new_GnumericFuncDict_object (module_dict));

	return GnmModule;
}

void
py_gnumeric_shutdown (void)
{
	if (GnmModule) {
		// At least clear the module.  We leak a ref somewhere.
		PyDict_Clear (PyModule_GetDict (GnmModule));
		Py_CLEAR (GnmModule);
	}
}

void
py_gnumeric_add_plugin (PyObject *module, GnmPyInterpreter *interpreter)
{
	PyObject *module_dict, *py_pinfo;
	GOPlugin *pinfo;
	char *key, *name;
	int i;

	module_dict = PyModule_GetDict (module);
 	pinfo = gnm_py_interpreter_get_plugin (interpreter);
	g_return_if_fail (pinfo);
	name = g_strdup (go_plugin_get_name (pinfo));
	i = strlen (name);
	while (i > 0)
		if (name[--i] == ' ')
			name[i] = '_';
	key = g_strconcat ("plugin_", name, "_info", NULL);
 	py_pinfo = py_new_GnmPlugin_object (pinfo);
	gnm_py_dict_store (module_dict, key, py_pinfo);
	g_free (name);
	g_free (key);
}
