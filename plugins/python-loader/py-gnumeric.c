/*
 * py-gnumeric.c - "Gnumeric" module for Python
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <Python.h>
#include <glib.h>
#include <libgnome/libgnome.h>
#include "workbook.h"
#include "cell.h"
#include "mstyle.h"
#include "sheet.h"
#include "sheet-style.h"
#include "position.h"
#include "value.h"
#include "expr.h"
#include "func.h"
#include "str.h"
#include "plugin.h"
#include "py-gnumeric.h"

/*
Available types, attributes, methods, etc.:
(please update this list after adding/removing anything)

Boolean:

CellPos:
	Attributes (read-only):
	- col
	- row
	Methods:
	- get_tuple
	str

Range:
	Attributes (read-only):
	- start
	- end
	Methods:
	- get_tuple

CellRef:

RangeRef:

MStyle:
	Methods:
	- set_font_bold
	- get_font_bold
	- set_font_italic
	- get_font_italic
	- set_font_strike
	- get_font_strike
	- set_font_size
	- get_font_size
	- set_wrap_text
	- get_wrap_text

Cell:
	Methods:
	- set_text
	- get_mstyle
	- get_value
	- get_rendered_text
	- get_entered_text

Sheet:
	Methods:
	- cell_fetch
	- style_get
	- style_apply_range
	- style_set_range
	- style_set_pos
	- get_extent
	- rename
	- get_name_unquoted
	subscript ([col,row] == cell_fetch)

Workbook:
	Methods:
	- get_sheets

GnumericFunc:
	call

GnumericFuncDict:
	subscript

GnmPlugin:
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
	- plugin_info   (value of type GnmPlugin)
	Methods:
	- MStyle   (creates MStyle object with default style, uses mstyle_new_default())
	- CellPos  (creates CellPos object)
	- Range    (creates Range Object)

*/

static InterpreterInfo *current_interpreter_info;

static PyObject *py_initgnumeric (GnmPlugin *pinfo);

static const gchar *plugin_argv[] = {"gnumeric", NULL};


InterpreterInfo *
create_python_interpreter (GnmPlugin *pinfo)
{
	InterpreterInfo *interpreter_info;
	PyThreadState *py_thread_state;
	PyObject *Gnumeric_module;

	py_thread_state = Py_NewInterpreter ();
	if (py_thread_state == NULL) {
		return NULL;
	}
	PySys_SetArgv (sizeof plugin_argv / sizeof plugin_argv[0] - 1,
		       (char **) plugin_argv);
	Gnumeric_module = py_initgnumeric (pinfo);

	interpreter_info = g_new (InterpreterInfo, 1);
	interpreter_info->py_thread_state = py_thread_state;
	interpreter_info->Gnumeric_module = Gnumeric_module;
	interpreter_info->Gnumeric_module_dict = PyModule_GetDict (Gnumeric_module);
	interpreter_info->GnumericError = PyDict_GetItemString (
	                                  interpreter_info->Gnumeric_module_dict,
	                                  (char *) "GnumericError");
	interpreter_info->eval_pos = NULL;

	current_interpreter_info = interpreter_info;

	return interpreter_info;
}

void
destroy_python_interpreter (InterpreterInfo *py_interpreter_info)
{
	g_return_if_fail (py_interpreter_info != NULL);

	Py_EndInterpreter (py_interpreter_info->py_thread_state);
	g_free (py_interpreter_info);
}

void
switch_python_interpreter_if_needed (InterpreterInfo *interpreter_info)
{
	if (current_interpreter_info == NULL ||
	    current_interpreter_info != interpreter_info) {
		(void) PyThreadState_Swap (interpreter_info->py_thread_state);
		current_interpreter_info = interpreter_info;
		g_print ("Python interpreter switched to %p\n",
		         (gpointer) interpreter_info->py_thread_state);
	}
}

void
clear_python_error_if_needed (void)
{
	if (PyErr_Occurred () != NULL) {
		PyErr_Clear ();
	}
}

PyObject *
python_call_gnumeric_function (FunctionDefinition *fn_def, const EvalPos *opt_eval_pos, PyObject *args)
{
	gint n_args, i;
	Value **values, *ret_val;
	PyObject *py_ret_val;
	const EvalPos *eval_pos;

	g_return_val_if_fail (fn_def != NULL, NULL);
	g_return_val_if_fail (args != NULL && PySequence_Check (args), NULL);

	if (opt_eval_pos != NULL) {
		eval_pos = opt_eval_pos;
	} else {
		eval_pos = current_interpreter_info->eval_pos;
	}
	if (eval_pos == NULL) {
		PyErr_SetString (current_interpreter_info->GnumericError,
		                 "Missing Evaluation Position.");
		return NULL;
	}

	n_args = PySequence_Length (args);
	values = g_new (Value *, n_args);
	for (i = 0; i < n_args; i++) {
		PyObject *py_val;

		py_val = PySequence_GetItem (args, i);
		g_assert (py_val != NULL);
		values[i] = convert_python_to_gnumeric_value (eval_pos, py_val);
	}

	ret_val = function_def_call_with_values (eval_pos, fn_def, n_args, values);
	py_ret_val = convert_gnumeric_value_to_python (eval_pos, ret_val);
	value_release (ret_val);
	for (i = 0; i < n_args; i++) {
		value_release (values[i]);
	}
	g_free (values);

	return py_ret_val;
}

Value *
call_python_function (PyObject *python_fn, const EvalPos *eval_pos, gint n_args, Value **args)
{
	PyObject *python_args;
	PyObject *python_ret_value;
	gint i;
	Value *ret_value;
	gboolean eval_pos_set;

	g_return_val_if_fail (python_fn != NULL && PyCallable_Check (python_fn), NULL);

	python_args = PyTuple_New (n_args);
	g_return_val_if_fail (python_args != NULL, NULL);
	for (i = 0; i < n_args; i++) {
		(void) PyTuple_SetItem (python_args, i, convert_gnumeric_value_to_python (eval_pos, args[i]));
	}
	if (current_interpreter_info->eval_pos != NULL) {
		eval_pos_set = FALSE;
	} else {
		current_interpreter_info->eval_pos = (EvalPos *) eval_pos;
		eval_pos_set = TRUE;
	}
	python_ret_value = PyObject_CallObject (python_fn, python_args);
	Py_DECREF (python_args);
	if (python_ret_value != NULL) {
		ret_value = convert_python_to_gnumeric_value (eval_pos, python_ret_value);
	} else {
		ret_value = convert_python_exception_to_gnumeric_value (eval_pos);
		clear_python_error_if_needed ();
	}
	if (eval_pos_set) {
		current_interpreter_info->eval_pos = NULL;
	}

	return ret_value;
}

gchar *
convert_python_exception_to_string (void)
{
	PyObject *exc_type, *exc_value, *exc_traceback;
	PyObject *exc_type_str = NULL, *exc_value_str = NULL;
	gchar *error_str;

	g_return_val_if_fail (PyErr_Occurred () != NULL, NULL);

	PyErr_Fetch (&exc_type, &exc_value, &exc_traceback);
	if (PyErr_GivenExceptionMatches (exc_type, current_interpreter_info->GnumericError)) {
		if (exc_value != NULL) {
			exc_value_str = PyObject_Str (exc_value);
			g_assert (exc_value_str != NULL);
			error_str = g_strdup (PyString_AsString (exc_value_str));
		} else {
			error_str = g_strdup (_("Unknown error"));
		}
	} else {
		exc_type_str = PyObject_Str (exc_type);
		if (exc_value != NULL) {
			exc_value_str = PyObject_Str (exc_value);
			error_str = g_strdup_printf (_("Python exception (%s: %s)"),
			                             PyString_AsString (exc_type_str),
			                             PyString_AsString (exc_value_str));
		} else {
			error_str = g_strdup_printf (_("Python exception (%s)"),
			                             PyString_AsString (exc_type_str));
		}
	}

	Py_DECREF (exc_type);
	Py_XDECREF (exc_value);
	Py_XDECREF (exc_traceback);
	Py_XDECREF (exc_type_str);
	Py_XDECREF (exc_value_str);

	return error_str;
}

Value *
convert_python_exception_to_gnumeric_value (const EvalPos *eval_pos)
{
	Value *ret_value;
	PyObject *exc_type, *exc_value, *exc_traceback;
	PyObject *exc_type_str = NULL, *exc_value_str = NULL;

	g_return_val_if_fail (PyErr_Occurred () != NULL, NULL);

	PyErr_Fetch (&exc_type, &exc_value, &exc_traceback);
	if (PyErr_GivenExceptionMatches (exc_type, current_interpreter_info->GnumericError)) {
		if (exc_value != NULL) {
			exc_value_str = PyObject_Str (exc_value);
			g_assert (exc_value_str != NULL);
			ret_value = value_new_error (eval_pos, PyString_AsString (exc_value_str));
		} else {
			ret_value = value_new_error (eval_pos, _("Unknown error"));
		}
	} else {
		gchar *error_str;

		exc_type_str = PyObject_Str (exc_type);
		if (exc_value != NULL) {
			exc_value_str = PyObject_Str (exc_value);
			error_str = g_strdup_printf (_("Python exception (%s: %s)"),
			                             PyString_AsString (exc_type_str),
			                             PyString_AsString (exc_value_str));
		} else {
			error_str = g_strdup_printf (_("Python exception (%s)"),
			                             PyString_AsString (exc_type_str));
		}
		ret_value = value_new_error (eval_pos, error_str);
		g_free (error_str);
	}

	Py_DECREF (exc_type);
	Py_XDECREF (exc_value);
	Py_XDECREF (exc_traceback);
	Py_XDECREF (exc_type_str);
	Py_XDECREF (exc_value_str);

	return ret_value;
}

PyObject *
convert_gnumeric_value_to_python (const EvalPos *eval_pos, const Value *val)
{
	PyObject *py_val = NULL;

	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (val != NULL, NULL);

    switch (val->type) {
	case VALUE_BOOLEAN:
		py_val = py_new_Boolean_object (val->v_bool.val);
		break;
	case VALUE_INTEGER:
		py_val = PyInt_FromLong (val->v_int.val);
		break;
	case VALUE_FLOAT:
		py_val = PyFloat_FromDouble (val->v_float.val);
		break;
	case VALUE_STRING: {
		py_val = PyString_FromString (val->v_str.val->str);
		break;
	}
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
				python_val = convert_gnumeric_value_to_python (eval_pos, val->v_array.vals[x][y]);
				(void) PyList_SetItem (col, y, python_val);
			}
			(void) PyList_SetItem (py_val, x, col);
		}
		break;
	}
	case VALUE_ERROR:
		g_warning ("convert_gnumeric_value_to_python(): unsupported value type");
	case VALUE_EMPTY:
		Py_INCREF (Py_None);
		py_val = Py_None;
		break;
	default:
		g_assert_not_reached ();
	}

	return py_val;
}

Value *
convert_python_to_gnumeric_value (const EvalPos *eval_pos, PyObject *py_val)
{
	PyObject *py_val_type;
	Value *ret_val;

	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (py_val != NULL, NULL);

	py_val_type = PyObject_Type (py_val);
	if (py_val_type == NULL) {
		clear_python_error_if_needed ();
		ret_val = value_new_empty ();
	} else if (py_val == Py_None) {
		ret_val = value_new_empty ();
	} else if (py_val_type == (PyObject *) &py_Boolean_object_type) {
		ret_val = value_new_bool (py_Boolean_as_gboolean ((py_Boolean_object *) py_val));
	} else if (PyInt_Check (py_val)) {
		ret_val = value_new_int ((gint) PyInt_AsLong (py_val));
	} else if (PyFloat_Check (py_val)) {
		ret_val = value_new_float ((gnum_float) PyFloat_AsDouble (py_val));
	} else if (PyString_Check (py_val)) {
		ret_val = value_new_string (PyString_AsString (py_val));
	} else if (py_val_type == (PyObject *) &py_RangeRef_object_type) {
		RangeRef *range_ref;

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
					ret_val->v_array.vals[x][y] = convert_python_to_gnumeric_value (eval_pos, python_val);
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
		                       PyString_AsString (py_val_type_str));
		ret_val = value_new_error (eval_pos, msg);
		g_free (msg);
		Py_DECREF (py_val_type_str);
	}
	Py_XDECREF (py_val_type);

	return ret_val;
}


/*
 * Boolean
 */

struct _py_Boolean_object {
	PyObject_HEAD
	gboolean value;
};

gboolean
py_Boolean_as_gboolean (py_Boolean_object *self)
{
	return self->value;
}

static PyObject *
py_Boolean_object_str (py_Boolean_object *self)
{
	return PyString_FromString (self->value ? "True" : "False");
}

static void
py_Boolean_object_dealloc (py_Boolean_object *self)
{
	free (self);
}

PyObject *
py_new_Boolean_object (gboolean value)
{
	py_Boolean_object *self;

	self = PyObject_NEW (py_Boolean_object, &py_Boolean_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->value = value;

	return (PyObject *) self;
}

PyTypeObject py_Boolean_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "Boolean",                       /* tp_name */
	sizeof (py_Boolean_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_Boolean_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	0, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	(reprfunc) py_Boolean_object_str,         /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * CellPos
 */

struct _py_CellPos_object {
	PyObject_HEAD
	CellPos cell_pos;
};

static PyObject *
py_CellPos_get_tuple_method (py_CellPos_object *self, PyObject *args);

static struct PyMethodDef py_CellPos_object_methods[] = {
	{(char *) "get_tuple", (PyCFunction) py_CellPos_get_tuple_method,
	 METH_VARARGS},
	{NULL, NULL}
};

CellPos *
py_CellPos_as_CellPos (py_CellPos_object *self)
{
	return &self->cell_pos;
}

static PyObject *
py_CellPos_get_tuple_method (py_CellPos_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_tuple")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "(ii)", self->cell_pos.col, self->cell_pos.row);
}

static PyObject *
py_CellPos_object_getattr (py_CellPos_object *self, gchar *name)
{
	if (strcmp (name, "col") == 0) {
		return Py_BuildValue ((char *) "i", self->cell_pos.col);
	} else if (strcmp (name, "row") == 0) {
		return Py_BuildValue ((char *) "i", self->cell_pos.row);
	} else {
		return Py_FindMethod (py_CellPos_object_methods, (PyObject *) self, name);
	}
}

static void
py_CellPos_object_dealloc (py_CellPos_object *self)
{
	free (self);
}

PyObject *
py_new_CellPos_object (const CellPos *cell_pos)
{
	py_CellPos_object *self;

	self = PyObject_NEW (py_CellPos_object, &py_CellPos_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->cell_pos = *cell_pos;

	return (PyObject *) self;
}

static PyObject *
py_new_CellPos_object_from_col_row (gint col, gint row)
{
	CellPos cell_pos;

	cell_pos.col = col;
	cell_pos.row = row;

	return py_new_CellPos_object (&cell_pos);
}

PyTypeObject py_CellPos_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char * ) "CellPos",                      /* tp_name */
	sizeof (py_CellPos_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_CellPos_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_CellPos_object_getattr, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * Range
 */

struct _py_Range_object {
	PyObject_HEAD
	Range range;
};

static PyObject *
py_Range_get_tuple_method (py_Range_object *self, PyObject *args);

static struct PyMethodDef py_Range_object_methods[] = {
	{(char *) "get_tuple", (PyCFunction) py_Range_get_tuple_method,
	 METH_VARARGS},
	{NULL, NULL}
};

Range *
py_Range_as_Range (py_Range_object *self)
{
	return &self->range;
}

static PyObject *
py_Range_get_tuple_method (py_Range_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_tuple")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "(iiii)",
	                      self->range.start.col, self->range.start.row,
	                      self->range.end.col, self->range.end.row);
}

static PyObject *
py_Range_object_getattr (py_Range_object *self, gchar *name)
{
	if (strcmp (name, "start") == 0) {
		return py_new_CellPos_object (&self->range.start);
	} else if (strcmp (name, "end") == 0) {
		return py_new_CellPos_object (&self->range.end);
	} else {
		return Py_FindMethod (py_Range_object_methods, (PyObject *) self, name);
	}
}

static void
py_Range_object_dealloc (py_Range_object *self)
{
	free (self);
}

PyObject *
py_new_Range_object (const Range *range)
{
	py_Range_object *self;

	self = PyObject_NEW (py_Range_object, &py_Range_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->range = *range;

	return (PyObject *) self;
}

static PyObject *
py_new_Range_object_from_start_end (const CellPos *start, const CellPos *end)
{
	Range range;

	range.start = *start;
	range.end = *end;

	return py_new_Range_object (&range);
}

PyTypeObject py_Range_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "Range",                         /* tp_name */
	sizeof (py_Range_object),                 /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_Range_object_dealloc,    /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_Range_object_getattr,   /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * CellRef
 */

struct _py_CellRef_object {
	PyObject_HEAD
	CellRef cell_ref;
};

static struct PyMethodDef py_CellRef_object_methods[] = {
/*	{"get_tuple", (PyCFunction) py_CellPos_get_tuple_method,   METH_VARARGS},*/
	{NULL, NULL}
};

CellRef *
py_CellRef_as_CellRef (py_CellRef_object *self)
{
	return &self->cell_ref;
}

static PyObject *
py_CellRef_object_getattr (py_CellRef_object *self, gchar *name)
{
	return Py_FindMethod (py_CellRef_object_methods, (PyObject *) self, name);
}

static void
py_CellRef_object_dealloc (py_CellRef_object *self)
{
	free (self);
}

PyObject *
py_new_CellRef_object (const CellRef *cell_ref)
{
	py_CellRef_object *self;

	self = PyObject_NEW (py_CellRef_object, &py_CellRef_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->cell_ref = *cell_ref;

	return (PyObject *) self;
}

PyTypeObject py_CellRef_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "CellRef",                       /* tp_name */
	sizeof (py_CellRef_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_CellRef_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_CellRef_object_getattr, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * RangeRef
 */

struct _py_RangeRef_object {
	PyObject_HEAD
	RangeRef range_ref;
};

static struct PyMethodDef py_RangeRef_object_methods[] = {
/*	{"get_tuple", (PyCFunction) py_RangePos_get_tuple_method,   METH_VARARGS},*/
	{NULL, NULL}
};

RangeRef *
py_RangeRef_as_RangeRef (py_RangeRef_object *self)
{
	return &self->range_ref;
}

static PyObject *
py_RangeRef_object_getattr (py_RangeRef_object *self, gchar *name)
{
	return Py_FindMethod (py_RangeRef_object_methods, (PyObject *) self, name);
}

static void
py_RangeRef_object_dealloc (py_RangeRef_object *self)
{
	free (self);
}

PyObject *
py_new_RangeRef_object (const RangeRef *range_ref)
{
	py_RangeRef_object *self;

	self = PyObject_NEW (py_RangeRef_object, &py_RangeRef_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->range_ref = *range_ref;

	return (PyObject *) self;
}

PyTypeObject py_RangeRef_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "RangeRef",                       /* tp_name */
	sizeof (py_RangeRef_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_RangeRef_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_RangeRef_object_getattr, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * MStyle
 */

struct _py_MStyle_object {
	PyObject_HEAD
	MStyle *mstyle;
};

static PyObject *
py_mstyle_set_font_bold_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_get_font_bold_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_set_font_italic_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_get_font_italic_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_set_font_strike_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_get_font_strike_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_set_font_size_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_get_font_size_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_set_wrap_text_method (py_MStyle_object *self, PyObject *args);
static PyObject *
py_mstyle_get_wrap_text_method (py_MStyle_object *self, PyObject *args);

static struct PyMethodDef py_MStyle_object_methods[] = {
	{(char *) "set_font_bold",
	 (PyCFunction) py_mstyle_set_font_bold_method,   METH_VARARGS},
	{(char *) "get_font_bold",
	 (PyCFunction) py_mstyle_get_font_bold_method,   METH_VARARGS},
	{(char *) "set_font_italic",
	 (PyCFunction) py_mstyle_set_font_italic_method, METH_VARARGS},
	{(char *) "get_font_italic",
	 (PyCFunction) py_mstyle_get_font_italic_method, METH_VARARGS},
	{(char *) "set_font_strike",
	 (PyCFunction) py_mstyle_set_font_strike_method, METH_VARARGS},
	{(char *) "get_font_strike",
	 (PyCFunction) py_mstyle_get_font_strike_method, METH_VARARGS},
	{(char *) "set_font_size",
	 (PyCFunction) py_mstyle_set_font_size_method,   METH_VARARGS},
	{(char *) "get_font_size",
	 (PyCFunction) py_mstyle_get_font_size_method,   METH_VARARGS},
	{(char *) "set_wrap_text",
	 (PyCFunction) py_mstyle_set_wrap_text_method,   METH_VARARGS},
	{(char *) "get_wrap_text",
	 (PyCFunction) py_mstyle_get_wrap_text_method,   METH_VARARGS},
	{NULL, NULL}
};

MStyle *
py_mstyle_as_MStyle (py_MStyle_object *self)
{
	return self->mstyle;
}

static PyObject *
py_mstyle_set_font_bold_method (py_MStyle_object *self, PyObject *args)
{
	gint bold;

	if (!PyArg_ParseTuple (args, (char *) "i:set_font_bold", &bold)) {
		return NULL;
	}

	mstyle_set_font_bold (self->mstyle, bold);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_mstyle_get_font_bold_method (py_MStyle_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_font_bold")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
			      mstyle_get_font_bold (self->mstyle));
}

static PyObject *
py_mstyle_set_font_italic_method (py_MStyle_object *self, PyObject *args)
{
	gint italic;

	if (!PyArg_ParseTuple (args, (char *) "i:set_font_italic", &italic)) {
		return NULL;
	}

	mstyle_set_font_italic (self->mstyle, italic);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_mstyle_get_font_italic_method (py_MStyle_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_font_italic")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
			      mstyle_get_font_italic (self->mstyle));
}

static PyObject *
py_mstyle_set_font_strike_method (py_MStyle_object *self, PyObject *args)
{
	gint strike;

	if (!PyArg_ParseTuple (args, (char *) "i:set_font_strike", &strike)) {
		return NULL;
	}

	mstyle_set_font_strike (self->mstyle, strike);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_mstyle_get_font_strike_method (py_MStyle_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_font_strike")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
			      mstyle_get_font_strike (self->mstyle));
}

static PyObject *
py_mstyle_set_font_size_method (py_MStyle_object *self, PyObject *args)
{
	gdouble size;

	if (!PyArg_ParseTuple (args, (char *) "d:set_font_size", &size)) {
		return NULL;
	}

	mstyle_set_font_size (self->mstyle, size);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_mstyle_get_font_size_method (py_MStyle_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":set_font_size")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "d",
			      mstyle_get_font_size (self->mstyle));
}

static PyObject *
py_mstyle_set_wrap_text_method (py_MStyle_object *self, PyObject *args)
{
	gint wrap_text;

	if (!PyArg_ParseTuple (args, (char * )"i:set_wrap_text",
			       &wrap_text)) {
		return NULL;
	}

	mstyle_set_wrap_text (self->mstyle, wrap_text);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_mstyle_get_wrap_text_method (py_MStyle_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_wrap_text")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
			      mstyle_get_wrap_text (self->mstyle));
}

static PyObject *
py_MStyle_object_getattr (py_MStyle_object *self, gchar *name)
{
	return Py_FindMethod (py_MStyle_object_methods, (PyObject *) self, name);
}

static void
py_MStyle_object_dealloc (py_MStyle_object *self)
{
	mstyle_unref (self->mstyle);
	free (self);
}

PyObject *
py_new_MStyle_object (MStyle *mstyle)
{
	py_MStyle_object *self;

	self = PyObject_NEW (py_MStyle_object, &py_MStyle_object_type);
	if (self == NULL) {
		return NULL;
	}
	mstyle_ref (mstyle);
	self->mstyle = mstyle;

	return (PyObject *) self;
}

PyTypeObject py_MStyle_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "MStyle",  /* tp_name */
	sizeof (py_MStyle_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_MStyle_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_MStyle_object_getattr, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * Cell
 */

struct _py_Cell_object {
	PyObject_HEAD
	Cell *cell;
};

static PyObject *
py_Cell_set_text_method (py_Cell_object *self, PyObject *args);
static PyObject *
py_Cell_get_mstyle_method (py_Cell_object *self, PyObject *args);
static PyObject *
py_Cell_get_value_method (py_Cell_object *self, PyObject *args);
static PyObject *
py_Cell_get_value_as_string_method (py_Cell_object *self, PyObject *args);
static PyObject *
py_Cell_get_rendered_text_method (py_Cell_object *self, PyObject *args);
static PyObject *
py_Cell_get_entered_text_method (py_Cell_object *self, PyObject *args);

static struct PyMethodDef py_Cell_object_methods[] = {
	{(char *) "set_text",
	 (PyCFunction) py_Cell_set_text_method,            METH_VARARGS},
	{(char *) "get_mstyle",
	 (PyCFunction) py_Cell_get_mstyle_method,          METH_VARARGS},
	{(char *) "get_value",
	 (PyCFunction) py_Cell_get_value_method,           METH_VARARGS},
	{(char *) "get_value_as_string",
	 (PyCFunction) py_Cell_get_value_as_string_method, METH_VARARGS},
	{(char *) "get_rendered_text",
	 (PyCFunction) py_Cell_get_rendered_text_method,   METH_VARARGS},
	{(char *) "get_entered_text",
	 (PyCFunction) py_Cell_get_entered_text_method,    METH_VARARGS},
	{NULL, NULL}
};

Cell *
py_Cell_as_Cell (py_Cell_object *self)
{
	return self->cell;
}

static PyObject *
py_Cell_set_text_method (py_Cell_object *self, PyObject *args)
{
	gchar *text;

	if (!PyArg_ParseTuple (args, (char *) "s:set_text", &text)) {
		return NULL;
	}

	cell_set_text (self->cell, text);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_Cell_get_mstyle_method (py_Cell_object *self, PyObject *args)
{
	MStyle *mstyle;

	if (!PyArg_ParseTuple (args, (char *) ":get_mstyle")) {
		return NULL;
	}

	mstyle = cell_get_mstyle (self->cell);

	return py_new_MStyle_object (mstyle);
}

static PyObject *
py_Cell_get_value_method (py_Cell_object *self, PyObject *args)
{
	EvalPos eval_pos;

	if (!PyArg_ParseTuple (args, (char *) ":get_value")) {
		return NULL;
	}

	(void) eval_pos_init_cell (&eval_pos, self->cell);
	return convert_gnumeric_value_to_python (&eval_pos, self->cell->value);
}

static PyObject *
py_Cell_get_value_as_string_method (py_Cell_object *self, PyObject *args)
{
	PyObject *py_ret_val;
	gchar *str;

	if (!PyArg_ParseTuple (args, (char *) ":get_value_as_string")) {
		return NULL;
	}

	str = value_get_as_string (self->cell->value);
	py_ret_val = PyString_FromString (str);
	g_free (str);

	return py_ret_val;
}

static PyObject *
py_Cell_get_rendered_text_method (py_Cell_object *self, PyObject *args)
{
	gchar *text;
	PyObject *py_text;

	if (!PyArg_ParseTuple (args, (char *) ":get_rendered_text")) {
		return NULL;
	}

	text = cell_get_rendered_text (self->cell);
	py_text = PyString_FromString (text);
	g_free (text);

	return py_text;
}

static PyObject *
py_Cell_get_entered_text_method (py_Cell_object *self, PyObject *args)
{
	gchar *text;
	PyObject *py_text;

	if (!PyArg_ParseTuple (args, (char *)" :get_entered_text")) {
		return NULL;
	}

	text = cell_get_entered_text (self->cell);
	py_text = PyString_FromString (text);
	g_free (text);

	return py_text;
}

static PyObject *
py_Cell_object_getattr (py_Cell_object *self, gchar *name)
{
	return Py_FindMethod (py_Cell_object_methods, (PyObject *) self, name);
}

static void
py_Cell_object_dealloc (py_Cell_object *self)
{
	free (self);
}

PyObject *
py_new_Cell_object (Cell *cell)
{
	py_Cell_object *self;

	self = PyObject_NEW (py_Cell_object, &py_Cell_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->cell = cell;

	return (PyObject *) self;
}

PyTypeObject py_Cell_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "Cell",  /* tp_name */
	sizeof (py_Cell_object),                /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_Cell_object_dealloc,   /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_Cell_object_getattr,  /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * Sheet
 */

struct _py_Sheet_object {
	PyObject_HEAD
	Sheet *sheet;
};

static PyObject *
py_sheet_cell_fetch_method (py_Sheet_object *self, PyObject *args);
static PyObject *
py_sheet_style_get_method (py_Sheet_object *self, PyObject *args);
static PyObject *
py_sheet_style_apply_range_method (py_Sheet_object *self, PyObject *args);
static PyObject *
py_sheet_style_set_range_method (py_Sheet_object *self, PyObject *args);
static PyObject *
py_sheet_style_set_pos_method (py_Sheet_object *self, PyObject *args);
static PyObject *
py_sheet_get_extent_method (py_Sheet_object *self, PyObject *args);
static PyObject *
py_sheet_rename_method (py_Sheet_object *self, PyObject *args);
static PyObject *
py_sheet_get_name_unquoted_method (py_Sheet_object *self, PyObject *args);

static struct PyMethodDef py_Sheet_object_methods[] = {
	{(char *) "cell_fetch",
	 (PyCFunction) py_sheet_cell_fetch_method,        METH_VARARGS},
	{(char *) "style_get",
	 (PyCFunction) py_sheet_style_get_method,         METH_VARARGS},
	{(char *) "style_apply_range",
	 (PyCFunction) py_sheet_style_apply_range_method, METH_VARARGS},
	{(char *) "style_set_range",
	 (PyCFunction) py_sheet_style_set_range_method,   METH_VARARGS},
	{(char *) "style_set_pos",
	 (PyCFunction) py_sheet_style_set_pos_method,     METH_VARARGS},
	{(char *) "get_extent",
         (PyCFunction) py_sheet_get_extent_method,        METH_VARARGS},
	{(char *) "rename",
	 (PyCFunction) py_sheet_rename_method,            METH_VARARGS},
	{(char *) "get_name_unquoted",
	 (PyCFunction) py_sheet_get_name_unquoted_method, METH_VARARGS},
	{NULL, NULL}
};

Sheet *
py_sheet_as_Sheet (py_Sheet_object *self)
{
	return self->sheet;
}

static PyObject *
py_sheet_cell_fetch_method (py_Sheet_object *self, PyObject *args)
{
	gint col, row;
	Cell *cell;

	if (!PyArg_ParseTuple (args, (char *) "ii:cell_fetch", &col, &row)) {
		return NULL;
	}

	cell = sheet_cell_fetch (self->sheet, col, row);

	return py_new_Cell_object (cell);
}

static PyObject *
py_sheet_style_get_method (py_Sheet_object *self, PyObject *args)
{
	gint col, row;
	py_CellPos_object *py_cell_pos;
	MStyle *mstyle;

	if (PyArg_ParseTuple (args, (char *) "ii:style_get", &col, &row)) {
		;
	} else {
		PyErr_Clear ();
		if (PyArg_ParseTuple (args, (char *) "O!:style_get",
		                      &py_CellPos_object_type, &py_cell_pos)) {
			col = py_cell_pos->cell_pos.col;
			row = py_cell_pos->cell_pos.row;
		} else {
			return NULL;
		}
	}

	mstyle = sheet_style_get (self->sheet, col, row);

	return py_new_MStyle_object (mstyle);
}

static PyObject *
py_sheet_style_apply_range_method (py_Sheet_object *self, PyObject *args)
{
	py_Range_object *py_range;
	py_MStyle_object *py_mstyle;

	if (!PyArg_ParseTuple (args, (char *) "O!O!:style_apply_range",
	                       &py_Range_object_type, &py_range,
	                       &py_MStyle_object_type, &py_mstyle)) {
		return NULL;
	}

	sheet_style_apply_range (self->sheet, &py_range->range, py_mstyle->mstyle);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_sheet_style_set_range_method (py_Sheet_object *self, PyObject *args)
{
	py_Range_object *py_range;
	py_MStyle_object *py_mstyle;

	if (!PyArg_ParseTuple (args, (char *) "O!O!:style_set_range",
	                       &py_Range_object_type, &py_range,
	                       &py_MStyle_object_type, &py_mstyle)) {
		return NULL;
	}

	sheet_style_set_range (self->sheet, &py_range->range, py_mstyle->mstyle);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_sheet_style_set_pos_method (py_Sheet_object *self, PyObject *args)
{
	gint col, row;
	py_CellPos_object *py_cell_pos;
	py_MStyle_object *py_mstyle;

	if (PyArg_ParseTuple (args, (char *) "iiO!:style_set_pos",
	                      &col, &row, &py_MStyle_object_type, &py_mstyle)) {
		;
	} else {
		PyErr_Clear ();
		if (!PyArg_ParseTuple (args, (char *) "O!O!:style_set_pos",
		                       &py_CellPos_object_type, &py_cell_pos,
		                       &py_MStyle_object_type, &py_mstyle)) {
			return NULL;
		}
	}
	sheet_style_set_pos (self->sheet, col, row, py_mstyle->mstyle);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_sheet_get_extent_method (py_Sheet_object *self, PyObject *args)
{
	Range range;

	if (!PyArg_ParseTuple (args, (char *) ":get_extent")) {
		return NULL;
	}

	range = sheet_get_extent (self->sheet, FALSE);
	return py_new_Range_object (&range);
}

static PyObject *
py_sheet_rename_method (py_Sheet_object *self, PyObject *args)
{
	gchar *new_name;

	if (!PyArg_ParseTuple (args, (char *) "s:rename", &new_name)) {
		return NULL;
	}

	sheet_rename (self->sheet, new_name);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_sheet_get_name_unquoted_method (py_Sheet_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_name_unquoted")) {
		return NULL;
	}

	return PyString_FromString (self->sheet->name_unquoted);
}

static PyObject *
py_sheet_subscript (py_Sheet_object *self, PyObject *key)
{
	gint col, row;
	Cell *cell;

	if (!PyArg_ParseTuple (key, (char *) "ii", &col, &row)) {
		return NULL;
	}

	cell = sheet_cell_fetch (self->sheet, col, row);

	return py_new_Cell_object (cell);
}

static PyObject *
py_Sheet_object_getattr (py_Sheet_object *self, gchar *name)
{
	return Py_FindMethod (py_Sheet_object_methods, (PyObject *) self, name);
}

static void
py_Sheet_object_dealloc (py_Sheet_object *self)
{
	free (self);
}

PyObject *
py_new_Sheet_object (Sheet *sheet)
{
	py_Sheet_object *self;

	self = PyObject_NEW (py_Sheet_object, &py_Sheet_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->sheet = sheet;

	return (PyObject *) self;
}

static PyMappingMethods py_sheet_as_mapping = {
	0, /* mp_length */
	(binaryfunc) &py_sheet_subscript,       /* mp_subscript */
	0  /* mp_ass_subscript */
};

PyTypeObject py_Sheet_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "Sheet",  /* tp_name */
	sizeof (py_Sheet_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_Sheet_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_Sheet_object_getattr, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	&py_sheet_as_mapping,                   /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	NULL  /* tp_doc */
};

/*
 * Workbook
 */

struct _py_Workbook_object {
	PyObject_HEAD
	Workbook *wb;
};

static PyObject *
py_Workbook_get_sheets_method (py_Workbook_object *self, PyObject *args);

static struct PyMethodDef py_Workbook_object_methods[] = {
	{(char *) "get_sheets", (PyCFunction) py_Workbook_get_sheets_method,
	 METH_VARARGS},
	{NULL, NULL}
};

Workbook *
py_Workbook_as_Workbook (py_Workbook_object *self)
{
	return self->wb;
}

static PyObject *
py_Workbook_get_sheets_method (py_Workbook_object *self, PyObject *args)
{
	GList *sheets, *l;
	gint i;
	PyObject *py_sheets;

	if (!PyArg_ParseTuple (args, (char *) ":get_sheets")) {
		return NULL;
	}

	sheets = workbook_sheets (self->wb);
	py_sheets = PyTuple_New (g_list_length (sheets));
	if (py_sheets == NULL) {
		return NULL;
	}
	for (l = sheets, i = 0; l != NULL; l = l->next, i++) {
		PyObject *py_sheet;

		py_sheet = py_new_Sheet_object ((Sheet *) l->data);
		g_assert (py_sheet);
		(void) PyTuple_SetItem (py_sheets, i, py_sheet);
	}
	g_list_free (sheets);

	return py_sheets;
}

static PyObject *
py_Workbook_object_getattr (py_Workbook_object *self, gchar *name)
{
	return Py_FindMethod (py_Workbook_object_methods, (PyObject *) self, name);
}

static void
py_Workbook_object_dealloc (py_Workbook_object *self)
{
	free (self);
}

PyObject *
py_new_Workbook_object (Workbook *wb)
{
	py_Workbook_object *self;

	self = PyObject_NEW (py_Workbook_object, &py_Workbook_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->wb = wb;

	return (PyObject *) self;
}

PyTypeObject py_Workbook_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "Workbook",  /* tp_name */
	sizeof (py_Workbook_object),                /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_Workbook_object_dealloc,   /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_Workbook_object_getattr,  /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * GnumericFunc
 */

typedef struct _py_GnumericFunc_object py_GnumericFunc_object;
struct _py_GnumericFunc_object {
	PyObject_HEAD
	FunctionDefinition *fn_def;
	EvalPos *eval_pos;
};

PyTypeObject py_GnumericFunc_object_type;

static PyObject *
py_GnumericFunc_call (py_GnumericFunc_object *self, PyObject *args, PyObject *keywords)
{
	return python_call_gnumeric_function (self->fn_def, self->eval_pos, args);
}

static void
py_GnumericFunc_object_dealloc (py_GnumericFunc_object *self)
{
	func_unref (self->fn_def);
	g_free (self->eval_pos);
	free (self);
}

static PyObject *
py_new_GnumericFunc_object (FunctionDefinition *fn_def, const EvalPos *opt_eval_pos)
{
	py_GnumericFunc_object *self;

	self = PyObject_NEW (py_GnumericFunc_object, &py_GnumericFunc_object_type);
	if (self == NULL) {
		return NULL;
	}

	func_ref (fn_def);
	self->fn_def = fn_def;
	if (opt_eval_pos != NULL) {
		self->eval_pos = g_new (EvalPos, 1);
		*self->eval_pos = *opt_eval_pos;
	} else {
		self->eval_pos = NULL;
	}

	return (PyObject *) self;
}

PyTypeObject py_GnumericFunc_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "GnumericFunc",  /* tp_name */
	sizeof (py_GnumericFunc_object),      /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_GnumericFunc_object_dealloc, /* tp_dealloc */
	0, /* tp_print */
	0, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	(ternaryfunc) py_GnumericFunc_call,   /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * GnumericFuncDict
 */

typedef struct _py_GnumericFuncDict_object py_GnumericFuncDict_object;
struct _py_GnumericFuncDict_object {
	PyObject_HEAD
	PyObject *module_dict;
};

PyTypeObject py_GnumericFuncDict_object_type;

static PyObject *
py_GnumericFuncDict_subscript (py_GnumericFuncDict_object *self, PyObject *key)
{
	gchar *fn_name;
	FunctionDefinition *fn_def;

	if (!PyArg_Parse(key, (char *) "s", &fn_name)) {
		return NULL;
	}

	fn_def = func_lookup_by_name (fn_name, NULL);
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
	free (self);
}

static PyObject *
py_new_GnumericFuncDict_object (PyObject *module_dict)
{
	py_GnumericFuncDict_object *self;

	self = PyObject_NEW (py_GnumericFuncDict_object, &py_GnumericFuncDict_object_type);
	if (self == NULL) {
		return NULL;
	}

	self->module_dict = module_dict;

	return (PyObject *) self;
}

PyMappingMethods py_GnumericFuncDict_mapping_methods = {
	0, /* mp_length */
	(binaryfunc) py_GnumericFuncDict_subscript, /* mp_subscript */
	0  /* mp_ass_subscript */
};

PyTypeObject py_GnumericFuncDict_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "GnumericFuncDict",  /* tp_name */
	sizeof (py_GnumericFuncDict_object),      /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_GnumericFuncDict_object_dealloc, /* tp_dealloc */
	0, /* tp_print */
	0, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	&py_GnumericFuncDict_mapping_methods,     /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};

/*
 * GnmPlugin
 */

struct _py_GnmPlugin_object {
	PyObject_HEAD
	GnmPlugin *pinfo;
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
	{(char *) "get_dir_name",
	 (PyCFunction) py_GnmPlugin_get_dir_name_method,    METH_VARARGS},
	{(char *) "get_id",
	 (PyCFunction) py_GnmPlugin_get_id_method,          METH_VARARGS},
	{(char *) "get_name",
	 (PyCFunction) py_GnmPlugin_get_name_method,        METH_VARARGS},
	{(char *) "get_description",
	 (PyCFunction) py_GnmPlugin_get_description_method, METH_VARARGS},
	{NULL, NULL}
};

static PyObject *
py_GnmPlugin_get_dir_name_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_dir_name")) {
		return NULL;
	}

	return PyString_FromString (gnm_plugin_get_dir_name (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_id_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_id")) {
		return NULL;
	}

	return PyString_FromString (gnm_plugin_get_id (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_name_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_name")) {
		return NULL;
	}

	return PyString_FromString (gnm_plugin_get_name (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_description_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_description")) {
		return NULL;
	}

	return PyString_FromString (gnm_plugin_get_description (self->pinfo));
}

GnmPlugin *
py_GnmPlugin_as_GnmPlugin (py_GnmPlugin_object *self)
{
	return self->pinfo;
}

static PyObject *
py_GnmPlugin_object_getattr (py_GnmPlugin_object *self, gchar *name)
{
	return Py_FindMethod (py_GnmPlugin_object_methods, (PyObject *) self, name);
}

static void
py_GnmPlugin_object_dealloc (py_GnmPlugin_object *self)
{
	free (self);
}

PyObject *
py_new_GnmPlugin_object (GnmPlugin *pinfo)
{
	py_GnmPlugin_object *self;

	self = PyObject_NEW (py_GnmPlugin_object, &py_GnmPlugin_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->pinfo = pinfo;

	return (PyObject *) self;
}

PyTypeObject py_GnmPlugin_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "GnmPlugin",                                /* tp_name */
	sizeof (py_GnmPlugin_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_GnmPlugin_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_GnmPlugin_object_getattr, /* tp_getattr */
	0, /* tp_setattr */
	0, /* tp_compare */
	0, /* tp_repr */
	0, /* tp_as_number */
	0, /* tp_as_sequence */
	0, /* tp_as_mapping */
	0, /* tp_hash */
	0, /* tp_call */
	0, /* tp_str */
	0, /* tp_getattro */
	0, /* tp_setattro */
	0, /* tp_as_buffer */
	0, /* tp_flags */
	0  /* tp_doc */
};


/*
 * Gnumeric module
 */

static PyObject *
py_gnumeric_Boolean_method (PyObject *self, PyObject *args)
{
	PyObject *src_obj;

	if (!PyArg_ParseTuple (args, (char *) "O:Boolean", &src_obj)) {
		return NULL;
	}

	return py_new_Boolean_object (PyObject_IsTrue (src_obj));
}

static PyObject *
py_gnumeric_CellPos_method (PyObject *self, PyObject *args)
{
	gint col, row;

	if (!PyArg_ParseTuple (args, (char *) "ii:CellPos", &col, &row)) {
		return NULL;
	}

	return py_new_CellPos_object_from_col_row (col, row);
}

static PyObject *
py_gnumeric_Range_method (PyObject *self, PyObject *args)
{
	PyObject *result = NULL;
	gint start_col, start_row, end_col, end_row;
	py_CellPos_object *py_start, *py_end;

	if (PyArg_ParseTuple (args, (char *) "iiii:Range",
	                      &start_col, &start_row, &end_col, &end_row)) {
		CellPos start, end;
		start.col = start_col; start.row = start_row;
		end.col = end_col; end.row = end_row;
		result = py_new_Range_object_from_start_end (&start, &end);
	} else {
		PyErr_Clear ();
		if (PyArg_ParseTuple (args, (char *) "O!O!:Range",
		                      &py_CellPos_object_type, &py_start,
		                      &py_CellPos_object_type, &py_end)) {
			result = py_new_Range_object_from_start_end (&py_start->cell_pos,
			                                             &py_end->cell_pos);
		} else {
			return NULL;
		}
	}

	return result;
}

static PyObject *
py_gnumeric_MStyle_method (PyObject *self, PyObject *args)
{
	MStyle *mstyle;
	PyObject *result;

	if (!PyArg_ParseTuple (args, (char *) ":MStyle")) {
		return NULL;
	}

	mstyle = mstyle_new_default ();
	result = py_new_MStyle_object (mstyle);
/*	mstyle_unref (mstyle); FIXME ?? */

	return result;
}

static PyMethodDef GnumericMethods[] = {
	{ (char *) "Boolean", py_gnumeric_Boolean_method, METH_VARARGS },
	{ (char *) "CellPos", py_gnumeric_CellPos_method, METH_VARARGS },
	{ (char *) "Range",   py_gnumeric_Range_method,   METH_VARARGS },
	{ (char *) "MStyle",  py_gnumeric_MStyle_method,  METH_VARARGS },
	{ NULL, NULL },
};


static PyObject *
py_initgnumeric (GnmPlugin *pinfo)
{
	PyObject *module, *module_dict;

	PyImport_AddModule ((char *) "Gnumeric");
	module = Py_InitModule ((char *) "Gnumeric", GnumericMethods);
	module_dict = PyModule_GetDict (module);
	g_assert (module_dict != NULL);

	(void) PyDict_SetItemString
		(module_dict, (char *) "TRUE", py_new_Boolean_object (TRUE));
	(void) PyDict_SetItemString
		(module_dict, (char *) "FALSE", py_new_Boolean_object (FALSE));

	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericError",
		 PyErr_NewException ((char *) "Gnumeric.GnumericError",
				     NULL, NULL));
	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericErrorNULL",
		 PyString_FromString (gnumeric_err_NULL));
	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericErrorDIV0",
		 PyString_FromString (gnumeric_err_DIV0));
	(void) PyDict_SetItemString (module_dict,
				     (char *) "GnumericErrorVALUE",
	                             PyString_FromString (gnumeric_err_VALUE));
	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericErrorREF",
		 PyString_FromString (gnumeric_err_REF));
	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericErrorNAME",
		 PyString_FromString (gnumeric_err_NAME));
	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericErrorNUM",
		 PyString_FromString (gnumeric_err_NUM));
	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericErrorNA",
		 PyString_FromString (gnumeric_err_NA));
	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericErrorRECALC",
		 PyString_FromString (gnumeric_err_RECALC));

	(void) PyDict_SetItemString
		(module_dict, (char *) "functions",
		 py_new_GnumericFuncDict_object (module_dict));

	(void) PyDict_SetItemString
		(module_dict, (char *) "plugin_info",
		 py_new_GnmPlugin_object (pinfo));

	return module;
}
