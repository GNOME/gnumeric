/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * py-gnumeric.c - "Gnumeric" module for Python
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <Python.h>
#include <gnumeric.h>
#include <glib.h>
#include "pygobject.h"
#include "application.h"
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
#include "wbc-gtk.h"
#include <goffice/app/go-plugin.h>
#include "parse-util.h"
#include "gnm-py-interpreter.h"
#include "py-gnumeric.h"

#include <glib/gi18n-lib.h>

static PyTypeObject py_Boolean_object_type;
typedef struct _py_Boolean_object py_Boolean_object;
static PyObject *py_new_Boolean_object (gboolean value);
static gboolean py_Boolean_as_gboolean (py_Boolean_object *self);

static PyTypeObject py_CellPos_object_type;
typedef struct _py_CellPos_object py_CellPos_object;

static PyTypeObject py_Range_object_type;
typedef struct _py_Range_object py_Range_object;

static PyTypeObject py_CellRef_object_type;
typedef struct _py_CellRef_object py_CellRef_object;

static PyTypeObject py_RangeRef_object_type;
typedef struct _py_RangeRef_object py_RangeRef_object;
static PyObject *py_new_RangeRef_object (GnmRangeRef const *range_ref);
static GnmRangeRef *py_RangeRef_as_RangeRef (py_RangeRef_object *self);

static PyTypeObject py_Style_object_type;
typedef struct _py_Style_object py_Style_object;

static PyTypeObject py_Cell_object_type;
typedef struct _py_Cell_object py_Cell_object;

static PyTypeObject py_Sheet_object_type;
typedef struct _py_Sheet_object py_Sheet_object;

static PyTypeObject py_Workbook_object_type;
typedef struct _py_Workbook_object py_Workbook_object;

static PyTypeObject py_Gui_object_type;
typedef struct _py_Gui_object py_Gui_object;

static PyTypeObject py_GnmPlugin_object_type;
typedef struct _py_GnmPlugin_object py_GnmPlugin_object;

typedef struct _py_GnumericFunc_object py_GnumericFunc_object;
static PyTypeObject py_GnumericFunc_object_type;

typedef struct _py_GnumericFuncDict_object py_GnumericFuncDict_object;
static PyTypeObject py_GnumericFuncDict_object_type;

/*
Available types, attributes, methods, etc.:
(please update this list after adding/removing anything)

Boolean:

GnmCellPos:
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
	Attributes (read-only):
	- col
	- row
	- sheet
	- col_relative
	- row_relative

RangeRef:
	Attributes (read-only):
	- start
	- end
	Methods:
	- get_tuple

GnmStyle:
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
	- get_style
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
	- sheets
	- sheet_add ([name, insert_after_position])
	- gui_add

Gui:
        Methods:
	- get_workbook
	- get_window

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
	- plugin_info   (value of type GOPlugin)
	Methods:
	- GnmStyle   (creates GnmStyle object with default style, uses gnm_style_new_default())
	- GnmCellPos  (creates GnmCellPos object)
	- Range    (creates Range Object)
	- workbooks
	- workbook_new

*/

#define GNUMERIC_MODULE \
	(PyImport_AddModule ((char *) "Gnumeric"))
#define GNUMERIC_MODULE_GET(key) \
	PyDict_GetItemString (PyModule_GetDict (GNUMERIC_MODULE), (char *) (key))
#define GNUMERIC_MODULE_SET(key, val) \
	PyDict_SetItemString (PyModule_GetDict (GNUMERIC_MODULE), (char *) (key), val)
#define SET_EVAL_POS(val) \
	GNUMERIC_MODULE_SET ("Gnumeric_eval_pos", PyCObject_FromVoidPtr (val, NULL))

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
	} else if (py_val_type == (PyObject *) &py_Boolean_object_type) {
		ret_val = value_new_bool (py_Boolean_as_gboolean ((py_Boolean_object *) py_val));
	} else if (PyInt_Check (py_val)) {
		ret_val = value_new_int ((gint) PyInt_AsLong (py_val));
	} else if (PyFloat_Check (py_val)) {
		ret_val = value_new_float ((gnm_float) PyFloat_AsDouble (py_val));
	} else if (PyString_Check (py_val)) {
		ret_val = value_new_string (PyString_AsString (py_val));
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
				       PyString_AsString (py_val_type_str));
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

    switch (val->type) {
	case VALUE_BOOLEAN:
		py_val = py_new_Boolean_object (val->v_bool.val);
		break;
	case VALUE_FLOAT:
		py_val = PyFloat_FromDouble (value_get_as_float (val));
		break;
	case VALUE_STRING:
		py_val = PyString_FromString (val->v_str.val->str);
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

	return gep ? PyCObject_AsVoidPtr (gep) : NULL;
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
		SET_EVAL_POS (NULL);
	}

	return ret_value;
}


/*
 * Boolean
 */

struct _py_Boolean_object {
	PyObject_HEAD
	gboolean value;
};

static gboolean
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
	PyObject_Del (self);
}

static PyObject *
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

static PyTypeObject py_Boolean_object_type = {
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
 * GnmCellPos
 */

struct _py_CellPos_object {
	PyObject_HEAD
	GnmCellPos cell_pos;
};

static PyObject *
py_CellPos_get_tuple_method (py_CellPos_object *self, PyObject *args);

static struct PyMethodDef py_CellPos_object_methods[] = {
	{(char *) "get_tuple", (PyCFunction) py_CellPos_get_tuple_method,
	 METH_VARARGS},
	{NULL, NULL}
};

static GnmCellPos *
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
	PyObject_Del (self);
}

static PyObject *
py_CellPos_object_str (py_CellPos_object *self)
{
	return PyString_FromString
		((char *) cellpos_as_string (&self->cell_pos));
}

static PyObject *
py_new_CellPos_object (const GnmCellPos *cell_pos)
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
	GnmCellPos cell_pos;

	cell_pos.col = col;
	cell_pos.row = row;

	return py_new_CellPos_object (&cell_pos);
}

static PyTypeObject py_CellPos_object_type = {
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
	(reprfunc) &py_CellPos_object_str, /* tp_str */
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
	GnmRange range;
};

static PyObject *
py_Range_get_tuple_method (py_Range_object *self, PyObject *args);

static struct PyMethodDef py_Range_object_methods[] = {
	{(char *) "get_tuple", (PyCFunction) py_Range_get_tuple_method,
	 METH_VARARGS},
	{NULL, NULL}
};

static GnmRange *
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
	PyObject_Del (self);
}

static PyObject *
py_new_Range_object (GnmRange const *range)
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
py_new_Range_object_from_start_end (const GnmCellPos *start, const GnmCellPos *end)
{
	GnmRange range;

	range.start = *start;
	range.end = *end;

	return py_new_Range_object (&range);
}

static PyTypeObject py_Range_object_type = {
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

static GnmCellRef *
py_CellRef_as_CellRef (py_CellRef_object *self)
{
	return &self->cell_ref;
}

static PyObject *
py_CellRef_object_getattr (py_CellRef_object *self, gchar *name)
{
	PyObject *result;

	if (strcmp (name, "col") == 0) {
		result = Py_BuildValue ((char *) "i", self->cell_ref.col);
	} else if (strcmp (name, "row") == 0) {
		result = Py_BuildValue ((char *) "i", self->cell_ref.row);
	} else if (strcmp (name, "sheet") == 0) {
		if (self->cell_ref.sheet)
			result = py_new_Sheet_object (self->cell_ref.sheet);
		else {
			Py_INCREF (Py_None);
			result = Py_None;
		}
	} else if (strcmp (name, "col_relative") == 0) {
		result = Py_BuildValue ((char *) "i",
				      self->cell_ref.col_relative ? 1 : 0);
	} else if (strcmp (name, "row_relative") == 0) {
		result = Py_BuildValue ((char *) "i",
				      self->cell_ref.row_relative ? 1 : 0);
	} else {
		result = Py_FindMethod (py_CellRef_object_methods,
				      (PyObject *) self, name);
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
	{(char *) "get_tuple", (PyCFunction) py_RangeRef_get_tuple_method,
	 METH_VARARGS},
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
	if (!PyArg_ParseTuple (args, (char *) ":get_tuple")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "(O&O&)",
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
		return Py_FindMethod (py_RangeRef_object_methods,
				      (PyObject *) self, name);
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

static PyObject *
py_gnm_style_set_font_bold_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_get_font_bold_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_set_font_italic_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_get_font_italic_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_set_font_strike_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_get_font_strike_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_set_font_size_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_get_font_size_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_set_wrap_text_method (py_Style_object *self, PyObject *args);
static PyObject *
py_gnm_style_get_wrap_text_method (py_Style_object *self, PyObject *args);

static struct PyMethodDef py_Style_object_methods[] = {
	{(char *) "set_font_bold",
	 (PyCFunction) py_gnm_style_set_font_bold_method,   METH_VARARGS},
	{(char *) "get_font_bold",
	 (PyCFunction) py_gnm_style_get_font_bold_method,   METH_VARARGS},
	{(char *) "set_font_italic",
	 (PyCFunction) py_gnm_style_set_font_italic_method, METH_VARARGS},
	{(char *) "get_font_italic",
	 (PyCFunction) py_gnm_style_get_font_italic_method, METH_VARARGS},
	{(char *) "set_font_strike",
	 (PyCFunction) py_gnm_style_set_font_strike_method, METH_VARARGS},
	{(char *) "get_font_strike",
	 (PyCFunction) py_gnm_style_get_font_strike_method, METH_VARARGS},
	{(char *) "set_font_size",
	 (PyCFunction) py_gnm_style_set_font_size_method,   METH_VARARGS},
	{(char *) "get_font_size",
	 (PyCFunction) py_gnm_style_get_font_size_method,   METH_VARARGS},
	{(char *) "set_wrap_text",
	 (PyCFunction) py_gnm_style_set_wrap_text_method,   METH_VARARGS},
	{(char *) "get_wrap_text",
	 (PyCFunction) py_gnm_style_get_wrap_text_method,   METH_VARARGS},
	{NULL, NULL}
};

static GnmStyle *
get_rw_style (py_Style_object *self)
{
	if (self->ro) {
		GnmStyle *tmp = gnm_style_dup (self->u.ro_style);
		gnm_style_unref (self->u.ro_style);
		self->ro = FALSE;
		self->u.rw_style = tmp;
	}
	return self->u.rw_style;
}

static PyObject *
py_gnm_style_set_font_bold_method (py_Style_object *self, PyObject *args)
{
	gint bold;

	if (!PyArg_ParseTuple (args, (char *) "i:set_font_bold", &bold)) {
		return NULL;
	}

	gnm_style_set_font_bold (get_rw_style (self), bold);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_gnm_style_get_font_bold_method (py_Style_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_font_bold")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
		gnm_style_get_font_bold (self->u.ro_style));
}

static PyObject *
py_gnm_style_set_font_italic_method (py_Style_object *self, PyObject *args)
{
	gint italic;

	if (!PyArg_ParseTuple (args, (char *) "i:set_font_italic", &italic)) {
		return NULL;
	}

	gnm_style_set_font_italic (get_rw_style (self), italic);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_gnm_style_get_font_italic_method (py_Style_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_font_italic")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
			      gnm_style_get_font_italic (self->u.ro_style));
}

static PyObject *
py_gnm_style_set_font_strike_method (py_Style_object *self, PyObject *args)
{
	gint strike;

	if (!PyArg_ParseTuple (args, (char *) "i:set_font_strike", &strike)) {
		return NULL;
	}

	gnm_style_set_font_strike (get_rw_style (self), strike);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_gnm_style_get_font_strike_method (py_Style_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_font_strike")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
			      gnm_style_get_font_strike (self->u.ro_style));
}

static PyObject *
py_gnm_style_set_font_size_method (py_Style_object *self, PyObject *args)
{
	gdouble size;

	if (!PyArg_ParseTuple (args, (char *) "d:set_font_size", &size)) {
		return NULL;
	}

	gnm_style_set_font_size (get_rw_style (self), size);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_gnm_style_get_font_size_method (py_Style_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":set_font_size")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "d",
			      gnm_style_get_font_size (self->u.ro_style));
}

static PyObject *
py_gnm_style_set_wrap_text_method (py_Style_object *self, PyObject *args)
{
	gint wrap_text;

	if (!PyArg_ParseTuple (args, (char * )"i:set_wrap_text",
			       &wrap_text)) {
		return NULL;
	}

	gnm_style_set_wrap_text (get_rw_style (self), wrap_text);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_gnm_style_get_wrap_text_method (py_Style_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_wrap_text")) {
		return NULL;
	}

	return Py_BuildValue ((char *) "i",
			      gnm_style_get_wrap_text (self->u.ro_style));
}

static PyObject *
py_Style_object_getattr (py_Style_object *self, gchar *name)
{
	return Py_FindMethod (py_Style_object_methods, (PyObject *) self, name);
}

static void
py_Style_object_dealloc (py_Style_object *self)
{
	g_return_if_fail (self != NULL);

	gnm_style_unref (self->u.ro_style);
	PyObject_Del (self);
}

static PyObject *
py_new_Style_object (GnmStyle *style)
{
	py_Style_object *self;

	self = PyObject_NEW (py_Style_object, &py_Style_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->u.rw_style = style;
	self->ro = FALSE;

	return (PyObject *) self;
}
static PyObject *
py_new_Style_const_object (GnmStyle const *style)
{
	py_Style_object *self;

	self = PyObject_NEW (py_Style_object, &py_Style_object_type);
	if (self == NULL) {
		return NULL;
	}
	gnm_style_ref (style);
	self->u.ro_style = style;
	self->ro = TRUE;

	return (PyObject *) self;
}

static PyTypeObject py_Style_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "GnmStyle",  /* tp_name */
	sizeof (py_Style_object),               /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_Style_object_dealloc,  /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_Style_object_getattr, /* tp_getattr */
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

/*
 * FIXME: The GnmCell may be deleted behind our backs.
 */
struct _py_Cell_object {
	PyObject_HEAD
	GnmCell *cell;
};

static PyObject *
py_Cell_set_text_method (py_Cell_object *self, PyObject *args);
static PyObject *
py_Cell_get_gnm_style_method (py_Cell_object *self, PyObject *args);
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
	{(char *) "get_style",
	 (PyCFunction) py_Cell_get_gnm_style_method,          METH_VARARGS},
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

static GnmCell *
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

	sheet_cell_set_text (self->cell, text, NULL);

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_Cell_get_gnm_style_method (py_Cell_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_style")) {
		return NULL;
	}

	return py_new_Style_const_object (gnm_cell_get_style (self->cell));
}

static PyObject *
py_Cell_get_value_method (py_Cell_object *self, PyObject *args)
{
	GnmEvalPos eval_pos;

	if (!PyArg_ParseTuple (args, (char *) ":get_value")) {
		return NULL;
	}

	(void) eval_pos_init_cell (&eval_pos, self->cell);
	return gnm_value_to_py_obj (&eval_pos, self->cell->value);
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

	text = gnm_cell_get_rendered_text (self->cell);
	py_text = PyString_FromString (text);
	g_free (text);

	return py_text;
}

static PyObject *
py_Cell_get_entered_text_method (py_Cell_object *self, PyObject *args)
{
	gchar *text;
	PyObject *py_text;

	if (!PyArg_ParseTuple (args, (char *) ":get_entered_text")) {
		return NULL;
	}

	text = gnm_cell_get_entered_text (self->cell);
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
	PyObject_Del (self);
}

static PyObject *
py_new_Cell_object (GnmCell *cell)
{
	py_Cell_object *self;

	self = PyObject_NEW (py_Cell_object, &py_Cell_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->cell = cell;

	return (PyObject *) self;
}

static PyTypeObject py_Cell_object_type = {
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

static Sheet *
py_sheet_as_Sheet (py_Sheet_object *self)
{
	return self->sheet;
}

static PyObject *
py_sheet_cell_fetch_method (py_Sheet_object *self, PyObject *args)
{
	gint col, row;
	GnmCell *cell;

	if (!PyArg_ParseTuple (args, (char *) "ii:cell_fetch", &col, &row)) {
		return NULL;
	}

	cell = sheet_cell_fetch (self->sheet, col, row);

	return py_new_Cell_object (cell);
}

static PyObject *
py_sheet_style_get_method (py_Sheet_object *self, PyObject *args)
{
	gint c, r;
	py_CellPos_object *py_cell_pos;

	if (PyArg_ParseTuple (args, (char *) "ii:style_get", &c, &r)) {
		;
	} else {
		PyErr_Clear ();
		if (!PyArg_ParseTuple (args, (char *) "O!:style_get",
				      &py_CellPos_object_type, &py_cell_pos)) {
			return NULL;
		}
		c = py_cell_pos->cell_pos.col;
		r = py_cell_pos->cell_pos.row;
	}

	return py_new_Style_const_object (sheet_style_get (self->sheet, c, r));
}

static PyObject *
py_sheet_style_apply_range_method (py_Sheet_object *self, PyObject *args)
{
	py_Range_object *py_range;
	py_Style_object *py_style;

	if (!PyArg_ParseTuple (args, (char *) "O!O!:style_apply_range",
			       &py_Range_object_type, &py_range,
			       &py_Style_object_type, &py_style)) {
		return NULL;
	}

	sheet_style_apply_range (self->sheet, &py_range->range,
		gnm_style_dup (py_style->u.ro_style));

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_sheet_style_set_range_method (py_Sheet_object *self, PyObject *args)
{
	py_Range_object *py_range;
	py_Style_object *py_style;

	if (!PyArg_ParseTuple (args, (char *) "O!O!:style_set_range",
			       &py_Range_object_type, &py_range,
			       &py_Style_object_type, &py_style)) {
		return NULL;
	}

	sheet_style_set_range (self->sheet, &py_range->range,
		gnm_style_dup (py_style->u.ro_style));

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_sheet_style_set_pos_method (py_Sheet_object *self, PyObject *args)
{
	gint col, row;
	py_CellPos_object *py_cell_pos;
	py_Style_object *py_style;

	if (PyArg_ParseTuple (args, (char *) "iiO!:style_set_pos",
			      &col, &row, &py_Style_object_type, &py_style)) {
		;
	} else {
		PyErr_Clear ();
		if (!PyArg_ParseTuple (args, (char *) "O!O!:style_set_pos",
				       &py_CellPos_object_type, &py_cell_pos,
				       &py_Style_object_type, &py_style)) {
			return NULL;
		}
	}
	sheet_style_set_pos (self->sheet, col, row,
		gnm_style_dup (py_style->u.ro_style));

	Py_INCREF (Py_None);
	return Py_None;
}

static PyObject *
py_sheet_get_extent_method (py_Sheet_object *self, PyObject *args)
{
	GnmRange range;

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

	g_object_set (self->sheet, "name", new_name, NULL);

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
	GnmCell *cell;

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
	g_return_if_fail (self != NULL);

	g_object_unref (self->sheet);
	PyObject_Del (self);
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
	g_object_ref (self->sheet);

	return (PyObject *) self;
}

static PyMappingMethods py_sheet_as_mapping = {
	0, /* mp_length */
	(binaryfunc) &py_sheet_subscript,       /* mp_subscript */
	0  /* mp_ass_subscript */
};

static PyTypeObject py_Sheet_object_type = {
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

static Workbook *
py_Workbook_as_Workbook (py_Workbook_object *self)
{
	return self->wb;
}

static PyObject *
py_Workbook_sheets (py_Workbook_object *self, PyObject *args)
{
	GList *sheets, *l;
	gint i;
	PyObject *py_sheets;

	if (!PyArg_ParseTuple (args, (char *) ":sheets")) {
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
py_Workbook_sheet_add (py_Workbook_object *self, PyObject *args)
{
	Sheet *sheet = NULL;
	char *name = NULL;
	int   insert_before = -1;

	if (!PyArg_ParseTuple (args, (char *) "|zi:sheet_add"))
		return NULL;

	sheet = workbook_sheet_add (self->wb, insert_before);
	if (sheet != NULL && name != NULL)
		g_object_set (sheet, "name", name, NULL);
	return py_new_Sheet_object (sheet);
}

static PyObject *
py_Workbook_gui_add (py_Workbook_object *self, PyObject *args)
{
	WBCGtk *wbcg;
	PyObject *result;

	if (!PyArg_ParseTuple (args, (char *) ":gui_add"))
		return NULL;

	if (workbook_sheet_count (self->wb) == 0)
		(void)workbook_sheet_add (self->wb, -1);

	wbcg = wbc_gtk_new (NULL, self->wb, NULL, NULL);
	result = py_new_Gui_object (wbcg);
	g_object_unref (wbcg);    /* py_new_Gui_object added a reference */
	return result;
}

static PyObject *
py_Workbook_object_getattr (py_Workbook_object *self, gchar *name)
{
	static struct PyMethodDef methods [] = {
		{ (char *) "sheets",	(PyCFunction) py_Workbook_sheets,
		 METH_VARARGS},
		{ (char *) "sheet_add",	(PyCFunction) py_Workbook_sheet_add,
		 METH_VARARGS},
		{ (char *) "gui_add",	(PyCFunction) py_Workbook_gui_add,
		 METH_VARARGS},
		{NULL, NULL}
	};
	return Py_FindMethod (methods, (PyObject *) self, name);
}

static void
py_Workbook_object_dealloc (py_Workbook_object *self)
{
	g_return_if_fail (self != NULL);

	g_object_unref (self->wb);
	PyObject_Del (self);
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
	g_object_ref (wb);

	return (PyObject *) self;
}

static PyTypeObject py_Workbook_object_type = {
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
 * Gui
 */

struct _py_Gui_object {
	PyObject_HEAD
	WBCGtk *wbcg;
};

static PyObject *
py_Gui_get_workbook (py_Gui_object *self, PyObject *args)
{
	Workbook *workbook;

	if (!PyArg_ParseTuple (args, (char *) ":get_workbook")) {
		return NULL;
	}

	workbook = wb_control_get_workbook (WORKBOOK_CONTROL (self->wbcg));

	return py_new_Workbook_object (workbook);
}

static PyObject *
py_Gui_get_window (py_Gui_object *self, PyObject *args)
{
	GtkWindow *toplevel;

	if (!PyArg_ParseTuple (args, (char *) ":get_window")) {
		return NULL;
	}

	g_return_val_if_fail (_PyGObject_API != NULL, NULL);

	toplevel = wbcg_toplevel (self->wbcg);

	return pygobject_new (G_OBJECT(toplevel));
}

static PyObject *
py_Gui_object_getattr (py_Gui_object *self, gchar *name)
{
	static struct PyMethodDef methods [] = {
		{ (char *) "get_workbook",  (PyCFunction) py_Gui_get_workbook,
		 METH_VARARGS},
		{ (char *) "get_window", (PyCFunction) py_Gui_get_window,
		 METH_VARARGS},

		{NULL, NULL}
	};
	return Py_FindMethod (methods, (PyObject *) self, name);
}

static void
py_Gui_object_dealloc (py_Gui_object *self)
{
	g_return_if_fail (self != NULL);

	g_object_unref (self->wbcg);
	PyObject_Del (self);
}

PyObject *
py_new_Gui_object (WBCGtk *wbcg)
{
	py_Gui_object *self;

	self = PyObject_NEW (py_Gui_object, &py_Gui_object_type);
	if (self == NULL) {
		return NULL;
	}
	self->wbcg = wbcg;
	g_object_ref (self->wbcg);

	return (PyObject *) self;
}

static PyTypeObject py_Gui_object_type = {
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "Gui",  /* tp_name */
	sizeof (py_Gui_object),                /* tp_size */
	0, /* tp_itemsize */
	(destructor) &py_Gui_object_dealloc,   /* tp_dealloc */
	0, /* tp_print */
	(getattrfunc) &py_Gui_object_getattr,  /* tp_getattr */
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

	gnm_func_unref (self->fn_def);
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

	gnm_func_ref (fn_def);
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

struct _py_GnumericFuncDict_object {
	PyObject_HEAD
	PyObject *module_dict;
};


static PyObject *
py_GnumericFuncDict_subscript (py_GnumericFuncDict_object *self, PyObject *key)
{
	gchar *fn_name;
	GnmFunc *fn_def;

	if (!PyArg_Parse(key, (char *) "s", &fn_name)) {
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

static PyTypeObject py_GnumericFuncDict_object_type = {
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
 * GOPlugin
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

	return PyString_FromString (go_plugin_get_dir_name (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_id_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_id")) {
		return NULL;
	}

	return PyString_FromString (go_plugin_get_id (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_name_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_name")) {
		return NULL;
	}

	return PyString_FromString (go_plugin_get_name (self->pinfo));
}

static PyObject *
py_GnmPlugin_get_description_method (py_GnmPlugin_object *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":get_description")) {
		return NULL;
	}

	return PyString_FromString (go_plugin_get_description (self->pinfo));
}

static GOPlugin *
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
	PyObject_HEAD_INIT(0)
	0, /* ob_size */
	(char *) "GOPlugin",                                /* tp_name */
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
		GnmCellPos start, end;
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
py_gnumeric_Style_method (PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple (args, (char *) ":GnmStyle")) {
		return NULL;
	}
	return py_new_Style_object (gnm_style_new_default ());
}

static PyObject *
py_gnumeric_workbooks_method (PyObject *self, PyObject *args)
{
	GList *workbooks, *l;
	int len, i;
	PyObject *result;

	if (!PyArg_ParseTuple (args, (char *) ":workbooks"))
		return NULL;

	workbooks = gnm_app_workbook_list ();
	len = g_list_length (workbooks);
	result = PyTuple_New (len);
	for (l = workbooks, i = 0; i < len; l = l->next, i++) {
		PyTuple_SetItem (result, i, py_new_Workbook_object (l->data));
	}

	return result;
}

static PyObject *
py_gnumeric_workbook_new (PyObject *self, PyObject *args)
{
	Workbook *workbook = NULL;
	PyObject *result;

	if (!PyArg_ParseTuple (args, (char *) "|O:workbook_new"))
		return NULL;

	workbook =  workbook_new ();
	result = py_new_Workbook_object (workbook);
	g_object_unref (workbook); /* py_new_Workbook_object
				      added a reference */
	return result;
}

static PyMethodDef GnumericMethods[] = {
	{ (char *) "Boolean", py_gnumeric_Boolean_method, METH_VARARGS },
	{ (char *) "CellPos", py_gnumeric_CellPos_method, METH_VARARGS },
	{ (char *) "Range",   py_gnumeric_Range_method,   METH_VARARGS },
	{ (char *) "GnmStyle",  py_gnumeric_Style_method,  METH_VARARGS },
	{ (char *) "workbooks", py_gnumeric_workbooks_method, METH_VARARGS },
	{ (char *) "workbook_new", py_gnumeric_workbook_new,  METH_VARARGS},
	{ NULL, NULL },
};

static void
init_err (PyObject *module_dict, const char *name, GnmStdError e)
{
	GnmValue *v = value_new_error_std (NULL, e);

	PyDict_SetItemString
		(module_dict, (char *)name,
		 PyString_FromString (v->v_err.mesg->str));

	value_release (v);
}


void
py_initgnumeric (GnmPyInterpreter *interpreter)
{
	PyObject *module, *module_dict, *py_pinfo;
	GOPlugin *pinfo;

	py_Boolean_object_type.ob_type          =
	py_CellPos_object_type.ob_type          =
	py_Range_object_type.ob_type            =
	py_CellRef_object_type.ob_type          =
	py_RangeRef_object_type.ob_type         =
	py_Style_object_type.ob_type           =
	py_Cell_object_type.ob_type             =
	py_Sheet_object_type.ob_type            =
	py_Workbook_object_type.ob_type         =
	py_Gui_object_type.ob_type              =
	py_GnumericFunc_object_type.ob_type     =
	py_GnumericFuncDict_object_type.ob_type =
	py_GnmPlugin_object_type.ob_type        = &PyType_Type;

	module = Py_InitModule ((char *) "Gnumeric", GnumericMethods);
	module_dict = PyModule_GetDict (module);

	(void) PyDict_SetItemString
		(module_dict, (char *) "TRUE", py_new_Boolean_object (TRUE));
	(void) PyDict_SetItemString
		(module_dict, (char *) "FALSE", py_new_Boolean_object (FALSE));

	(void) PyDict_SetItemString
		(module_dict, (char *) "GnumericError",
		 PyErr_NewException ((char *) "Gnumeric.GnumericError",
				     NULL, NULL));

	init_err (module_dict, "GnumericErrorNULL", GNM_ERROR_NULL);
	init_err (module_dict, "GnumericErrorDIV0", GNM_ERROR_DIV0);
	init_err (module_dict, "GnumericErrorVALUE", GNM_ERROR_VALUE);
	init_err (module_dict, "GnumericErrorREF", GNM_ERROR_REF);
	init_err (module_dict, "GnumericErrorNAME", GNM_ERROR_NAME);
	init_err (module_dict, "GnumericErrorNUM", GNM_ERROR_NUM);
	init_err (module_dict, "GnumericErrorNA", GNM_ERROR_NA);

	(void) PyDict_SetItemString
		(module_dict, (char *) "functions",
		 py_new_GnumericFuncDict_object (module_dict));

	pinfo = gnm_py_interpreter_get_plugin (interpreter);
	if (pinfo) {
		py_pinfo = py_new_GnmPlugin_object (pinfo);
	} else {
		py_pinfo = Py_None;
		Py_INCREF (Py_None);
	}
	(void) PyDict_SetItemString (module_dict,
				     (char *) "plugin_info", py_pinfo);
}
