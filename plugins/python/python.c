/*
 * Support for Python in gnumeric.
 */

/**
 * TODO:
 * Cell value
 * Cell ranges - The sheet attribute is not yet handled.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnome.h>
#include <string.h>
#include "func.h"
#include "plugin.h"
#include "plugin-util.h"
#include "error-info.h"
#include "module-plugin-defs.h"
#include "gutils.h"
#include "value.h"
#include "str.h"
#include "command-context.h"
#include "expr.h"

#include "Python.h"

#define BROKEN_PY_INITIALIZE

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/* Classes we define in Python code, and where we define them. */
#define GNUMERIC_DEFS_MODULE "gnumeric_defs"
#define BOOLEAN_CLASS    "Boolean"
#define CELL_REF_CLASS   "CellRef"
#define CELL_RANGE_CLASS "CellRange"

static PyObject *value_to_python (Value *v);
static Value *value_from_python (PyObject *o, EvalPos const *pos);

/* This is standard idiom for defining exceptions in extension modules. */
static PyObject *GnumericError;

/**
 * string_from_exception
 *
 * Converts the current Python exception to a C string. Returns C string.
 * Caller must free it.
 */
static char *
string_from_exception (void)
{
	const char *header = _("Python exception");
	const char *retval = header;
	char buf[256];

	PyObject *ptype = NULL, *pvalue = NULL, *ptraceback = NULL;
	PyObject *stype = NULL, *svalue = NULL;

	PyErr_Fetch (&ptype, &pvalue, &ptraceback);
	if (!ptype)
		goto cleanup;

	if (pvalue)
		svalue = PyObject_Str (pvalue);

	if (PyErr_GivenExceptionMatches (ptype, GnumericError)) {
		/* This recovers a VALUE_ERROR which the Python code received
		 * from C  */
		retval = PyString_AsString (svalue);
	} else {
		int pos;

		/* Include exception class in the error message */
		stype = PyObject_Str (ptype);
		if (!stype)
			goto cleanup;
		pos = snprintf (buf, sizeof buf, "%s: %s",
				header, PyString_AsString (stype));
		retval = buf;

		if (!svalue)
			goto cleanup;
		if (pos + 3 < (int)sizeof (buf))
			snprintf (buf + pos , sizeof buf - pos , ": %s",
				  PyString_AsString (svalue));
	}
cleanup:
	Py_XDECREF (stype);
	Py_XDECREF (svalue);
	PyErr_Restore (ptype, pvalue, ptraceback);

	return g_strdup (retval);
}

/**
 * value_from_exception
 * @ei  function eval info
 *
 * Converts the current Python exception to a new VALUE_ERROR.
 *
 * For now, also prints traceback to standard error.
 */
static Value *
value_from_exception (FunctionEvalInfo *ei)
{
	char *exc_string = string_from_exception ();
	Value *v = value_new_error (ei->pos, exc_string);
	g_free (exc_string);

	PyErr_Print ();	/* Traceback to stderr for now */
	PyErr_Clear ();

	return v;
}

/*
 * Support for registering Python-based functions for use in formulas.
 */

/**
 * cell_ref_to_python
 * @v   value union
 *
 * Converts a cell reference to Python. Returns owned reference to python
 * object.
 */
static PyObject *
cell_ref_to_python (CellRef *cell)
{
	PyObject *mod, *klass, *ret;

	mod = PyImport_ImportModule ((char *) GNUMERIC_DEFS_MODULE);
	if (mod == NULL)
		return NULL;

	klass  = PyObject_GetAttrString (mod, (char *) CELL_REF_CLASS);
	if (klass == NULL)
		return NULL;

	ret = PyObject_CallFunction (klass, (char *) "iiii",
				     cell->col, cell->row,
				     cell->col_relative, cell->row_relative
				     /*, sheet */);
	Py_DECREF (klass);
	return ret;
}

/**
 * range_to_python
 * @v   value union
 *
 * Converts a cell range to Python. Returns owned reference to python object. */
static PyObject *
range_to_python (Value *v)
{
	PyObject *mod, *klass = NULL, *ret = NULL;

	mod = PyImport_ImportModule ((char *) GNUMERIC_DEFS_MODULE);
	if (mod == NULL)
		return NULL;

	klass  = PyObject_GetAttrString (mod, (char *) CELL_RANGE_CLASS);
	if (klass == NULL)
		return NULL;

	/* FIXME : Support inverted ranges */
	ret = PyObject_CallFunction
		(klass, (char *) "O&O&",
		 cell_ref_to_python, &v->v_range.cell.a,
		 cell_ref_to_python, &v->v_range.cell.b);

	Py_DECREF (klass);
	return ret;
}

/**
 * boolean_to_python
 * @v   value union
 *
 * Converts a boolean to Python. Returns owned reference to python object.
 * gnumeric_defs.FALSE and gnumeric_defs.TRUE are pre-initialized instances of
 * the class gnumeric_defs.Boolean.
 */
static PyObject *
boolean_to_python (Value *v)
{
	PyObject *mod;

	mod = PyImport_ImportModule ((char *) GNUMERIC_DEFS_MODULE);
	if (mod == NULL)
		return NULL;

	return PyObject_GetAttrString
		(mod, (char *) (v->v_bool.val ? "TRUE" : "FALSE"));
}

/**
 * row_to_python
 * @v   value union
 * @i   column index
 *
 * Converts an array row to Python.
 * Returns owned reference to python object.
 */
static PyObject *
row_to_python (Value *v, int j)
{
	PyObject * list = NULL, *o = NULL;
	int cols, i;

	cols = v->v_array.x;
	list = PyList_New (cols);
	if (list == NULL)
		return NULL;

	for (i = 0; i < cols; i++) {
		o = value_to_python (v->v_array.vals[i][j]);
		if (o == NULL) {
			Py_DECREF (list);
			return NULL;
		}
		PyList_SetItem (list, i, o);
	}

	return list;
}

/**
 * array_to_python
 * @v   value union
 *
 * Converts an array to Python. Returns owned reference to python object.
 *
 * User visible array notation in Gnumeric: Row n, col m is [n][m]
 * In C, the same elt is v->v_array.vals[m][n]
 * For scripting, I think it's best to do it the way the user sees it,
 * i.e. opposite from C.
 */
static PyObject *
array_to_python (Value *v)
{
	PyObject * list = NULL, *row = NULL;
	int rows, j;

	rows = v->v_array.y;
	list = PyList_New (rows);
	if (list == NULL)
		return NULL;

	for (j = 0; j < rows; j++) {
		row = row_to_python (v, j);
		if (row == NULL) {
			Py_DECREF (list);
			return NULL;
		}
		PyList_SetItem (list, j, row);
	}

	return list;
}

/**
 * value_to_python
 * @v   value union
 *
 * Converts a Gnumeric value to Python. Returns owned reference to python
 * object.
 *
 * VALUE_ERROR is not handled here. It is not possible to receive VALUE_ERROR
 * as a parameter to a function.  But a C function may return VALUE_ERROR. In
 * this case, the caller an exception is raised before value_to_python is
 * called.  */
static PyObject *
value_to_python (Value *v)
{
	PyObject *o;

	switch (v->type) {
	case VALUE_INTEGER:
		o = PyInt_FromLong (v->v_int.val);
		break;
	case VALUE_FLOAT:
		o = PyFloat_FromDouble (v->v_float.val);
		break;
	case VALUE_STRING:
		o = PyString_FromString (v->v_str.val->str);
		break;
	case VALUE_CELLRANGE:
		o = range_to_python (v);
		break;
	case VALUE_EMPTY:
		Py_INCREF (Py_None);
		o = Py_None;
		break;
	case VALUE_BOOLEAN:
		o = boolean_to_python (v);
		break;
	case VALUE_ARRAY:
		o = array_to_python (v);
		break;
	case VALUE_ERROR: /* See comment */
	default:
		o = NULL;
		break;
	}
	return o;
}

/**
 * boolean_check
 * @o    Python object
 *
 * Returns TRUE if object is an instance of gnumeric_defs.Boolean and
 * FALSE otherwise.
 * I don't believe this is 100% kosher, but should work in practice. A 100%
 * solution seems to require that the boolean class be defined in C.
 */
static int
boolean_check (PyObject *o)
{
	PyObject *klass;
	gchar *s;

	if (!PyObject_HasAttrString (o, (char *) "__class__"))
		return FALSE;

	klass = PyObject_GetAttrString (o, (char *) "__class__");
	s = PyString_AsString (PyObject_Str (klass));
	Py_XDECREF (klass);
	return (s != NULL &&
		strcmp (s, (GNUMERIC_DEFS_MODULE "." BOOLEAN_CLASS)) == 0);
}

/**
 * boolean_from_python
 * @o    Python object
 *
 * Converts an instance of gnumeric_defs.Boolean to Gnumeric boolean.
 * Returns Gnumeric value on success, NULL on failure.
 */
static Value *
boolean_from_python (PyObject *o)
{
	PyObject *ret;
	Value *v;

	if (!(ret = PyObject_CallMethod (o, (char *) "__nonzero__", NULL)))
		return NULL;

	v = value_new_bool (PyInt_AsLong (ret) ? TRUE : FALSE);

	Py_DECREF (ret);
	return v;
}

/**
 * range_check
 * @o    Python object
 *
 * Returns TRUE if object is an instance of gnumeric_defs.CellRange and FALSE
 * otherwise.
 * I don't believe this is 100% kosher, but should work in practice. A 100 %
 * solution seems to require that the cell range and cell ref classes are
 * defined in C.
 */
static int
range_check (PyObject *o)
{
	PyObject *klass;
	gchar *s;

	if (!PyObject_HasAttrString (o, (char *) "__class__"))
		return FALSE;

	klass = PyObject_GetAttrString  (o, (char *) "__class__");
	s = PyString_AsString (PyObject_Str (klass));
	Py_XDECREF (klass);
	return (s != NULL &&
		strcmp (s, (GNUMERIC_DEFS_MODULE "." CELL_RANGE_CLASS)) == 0);
}

/**
 * cell_ref_from_python
 * @o    Python object
 * @c    Cell reference
 *
 * Converts a Python cell reference to Gnumeric. Returns TRUE on success and
 * FALSE on failure.
 * Used as a converter function by PyArg_ParseTuple.
 */
static int
cell_ref_from_python (PyObject *o, CellRef *c)
{
	int ret = FALSE;
	PyObject *column = NULL, *row = NULL;
	PyObject *col_relative = NULL, *row_relative = NULL/*, *sheet = NULL */;

	column = PyObject_GetAttrString (o, (char *) "column");
	if (!column || !PyInt_Check (column))
		goto cleanup;

	row = PyObject_GetAttrString (o, (char *) "row");
	if (!row || !PyInt_Check (row))
		goto cleanup;

	col_relative = PyObject_GetAttrString (o, (char *) "col_relative");
	if (!col_relative || !PyInt_Check (col_relative))
		goto cleanup;

	row_relative = PyObject_GetAttrString (o, (char *) "row_relative");
	if (!row_relative || !PyInt_Check (row_relative))
		goto cleanup;

	/* sheet        = PyObject_GetAttrString (o, (char *) "sheet"); */
	/* if (!sheet || !PyString_Check (sheet) */
	/*         goto cleanup; */

	c->col = (int) PyInt_AsLong (column);
	c->row = (int) PyInt_AsLong (row);
	c->col_relative = (unsigned char) PyInt_AsLong (col_relative);
	c->row_relative = (unsigned char) PyInt_AsLong (row_relative);
	c->sheet = NULL; /* = string_get (PyString_AsString (sheet)); */
	ret = TRUE;

cleanup:
	Py_XDECREF (column);
	Py_XDECREF (row);
	Py_XDECREF (col_relative);
	Py_XDECREF (row_relative);
	/* Py_XDECREF (sheet); */

	return ret;
}

/**
 * range_from_python
 * @o    Python object
 *
 * Converts a Python cell range to Gnumeric. Returns Gnumeric value on success,
 * NULL on failure.
 */
static Value *
range_from_python (PyObject *o, EvalPos const *pos)
{
	PyObject *range = NULL;
	CellRef a, b;
	Value *ret = NULL;

	if ((range = PyObject_GetAttrString  (o, (char *) "range")) == NULL)
		return NULL;

	if (!PyArg_ParseTuple (range, (char *) "O&O&",
			       cell_ref_from_python, &a,
			       cell_ref_from_python, &b))
		goto cleanup;
	ret = value_new_cellrange (&a, &b, pos->eval.col, pos->eval.row);

cleanup:
	Py_DECREF (range);

	return ret;
}

/**
 * array_check
 * @o    Python object
 *
 * Returns TRUE if object is a list of lists and FALSE otherwise
 */
static int
array_check (PyObject *o)
{
	PyObject *item;

	if (!PyList_Check (o))
		return FALSE;
	else if (PyList_Size == 0)
		return FALSE;
	else if ((item = PyList_GetItem (o, 0)) == NULL)
		return FALSE;
	else if (!PyList_Check (item))
		return FALSE;
	else
		return TRUE;
}

/**
 * row_from_python
 * @o      python list
 * @rowno  rowno
 * @array  array
 *
 * Converts a Python list object to array row. Returns 0 on success, -1
 * on failure.
 *
 * Row n, col m is [n][m].  */
static int
row_from_python (PyObject *o, int rowno, Value *array,
		 EvalPos const *pos)
{
	PyObject *item;
	int i;
	int cols = array->v_array.x;

	for (i = 0; i < cols; i++) {
		if ((item = PyList_GetItem (o, i)) == NULL)
			return -1;
		array->v_array.vals[i][rowno] =
		    value_from_python (item, pos);
	}
	return 0;
}

/**
 * array_from_python
 * @o   python sequence
 *
 * Converts a Python sequence object to array. Returns Gnumeric value on
 * success, NULL on failure.
 *
 * Row n, col m is [n][m].
 */
static Value *
array_from_python (PyObject *o, EvalPos const *pos)
{
	Value *v = NULL, *array = NULL;
	PyObject *row = NULL;
	int rows, cols, j;

	rows = PyList_Size (o);

	for (j = 0; j < rows; j++) {
		if ((row = PyList_GetItem (o, j)) == NULL)
			goto cleanup;
		if (!PyList_Check (row)) {
			PyErr_SetString (PyExc_TypeError, "Sequence expected");
			goto cleanup;
		}
		cols = PyList_Size (row);
		if (j == 0) {
			array = value_new_array (cols, rows);
		} else if (cols != array->v_array.x) {
			PyErr_SetString (PyExc_TypeError,
					 "Rectangular array expected");
			goto cleanup;
		}
		if ((row_from_python (row, j, array, pos)) != 0)
			goto cleanup;
	}
	v = array;

cleanup:
	if (array && array != v)
		value_release (array);
	return v;
}

/**
 * value_from_python
 * @o   Python object
 *
 * Converts a Python object to a Gnumeric value. Returns Gnumeric value, or
 * NULL on failure.
 */
static Value *
value_from_python (PyObject *o, EvalPos const *pos)
{
	Value *v = NULL;

	if (o == Py_None) {
		v = value_new_empty ();
	} else if (PyInt_Check (o)){
		v = value_new_int ((gnum_int) PyInt_AsLong (o));
	} else if (PyFloat_Check (o)) {
		v = value_new_float ((gnum_float) PyFloat_AsDouble (o));
	} else if (PyString_Check (o)) {
		v = value_new_string (PyString_AsString (o));
	} else if (boolean_check (o)) {
		v = boolean_from_python (o);
	} else if (array_check (o)) {
		v = array_from_python (o, pos);
	} else if (range_check (o)) {
		v = range_from_python (o, pos);
	} else {
		PyErr_SetString (PyExc_TypeError, _("Unknown Python type"));
	}

	return v;
}

typedef struct {
	FunctionDefinition *fndef;
	PyObject *codeobj;
} FuncData;

static GList *funclist = NULL;

static int
fndef_compare (FuncData *fdata, FunctionDefinition *fndef)
{
	return (fdata->fndef != fndef);
}

/**
 * call_function
 * @ei    Function definition and context
 * @args  Python arguments tuple
 *
 * Call function, with args, and return result as a Gnumeric Value
 */
static Value *
call_function (FunctionEvalInfo *ei, PyObject *args)
{
	PyObject *result;
	/* Marshaller validated pointers */
	FunctionDefinition const * const fndef = ei->func_call->func;
	Value *v = NULL;
	GList *l;

	/* Find the Python code object for this FunctionDefinition. */
	l = g_list_find_custom (funclist, (gpointer)fndef,
				(GCompareFunc) fndef_compare);
	if (!l)
		return value_new_error
			(ei->pos, _("Unable to lookup Python code object."));

	/* Call the Python object. */
	result = PyEval_CallObject (((FuncData *)(l->data))->codeobj, args);

	if (result) {
		v = value_from_python (result, ei->pos);
		Py_DECREF (result);
	}
	if (PyErr_Occurred ()) {
		v = value_from_exception (ei);
	}

	return v;
}

/**
 * marshal_func_args
 * @ei    Function definition and context
 * @argv  Gnumeric argument vector.
 *
 * Marshal  a fixed number of arguments and hand them to call_function.
 * Optional arguments are supported.
 */
static Value *
marshal_func_args (FunctionEvalInfo *ei, Value *argv [])
{
	PyObject *args;
	Value *v = NULL;
	int i, min, max, argc;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);
	g_return_val_if_fail (ei->func_call->func != NULL, NULL);

	function_def_count_args (ei->func_call->func, &min, &max);

	/* Count actual arguments */
	for (argc = min; argc < max; argc++)
		if (!argv [argc])
			break;

	/* Build the argument list which is a Python tuple. */
	args = PyTuple_New (argc + 1);

	/* First, the EvalInfo */
	PyTuple_SetItem (args, 0, PyCObject_FromVoidPtr ((void *) ei, NULL));

	/* Now, the actual arguments */
	for (i = 0; i < argc; i++) {
		/* ref is stolen from us */
		PyTuple_SetItem (args, i + 1, value_to_python (argv [i]));
	}

	v = call_function (ei, args);

	Py_DECREF (args);
	return v;
}

/**
 * marshal_func_nodes
 * @ei     Function definition and context
 * @nodes  Argument list
 *
 * Marshal a variable number of arguments and hand them to call_function.
 */
static Value *
marshal_func_nodes (FunctionEvalInfo *ei, ExprList *nodes)
{
	PyObject *args;
	Value *v = NULL, *ev;
	ExprList *l;
	int i, argc;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);
	g_return_val_if_fail (ei->func_call->func != NULL, NULL);

	/* Count actual arguments */
	argc = expr_list_length (nodes);

	/* Build the argument list which is a Python tuple. */
	args = PyTuple_New (argc + 1);

	/* First, the EvalInfo */
	PyTuple_SetItem (args, 0, PyCObject_FromVoidPtr ((void *) ei, NULL));

	/* Now, the actual arguments */
	for (i = 0, l = nodes; i < argc && l; i++, l = l->next) {
		ev = expr_eval (l->data, ei->pos, EVAL_PERMIT_NON_SCALAR);
		/* ref is stolen from us */
		PyTuple_SetItem (args, i + 1, value_to_python (ev));
		value_release (ev);
	}

	v = call_function (ei, args);

	Py_DECREF (args);
	return v;
}

/**
 * apply
 * @m        Dummy
 * @py_args  Argument tuple
 *
 * Apply a gnumeric function to the arguments in py_args. Returns result as
 * owned reference to a Python object.
 *
 * Signature when called from Python:
 *      gnumeric.apply (cobject context, string function_name,
 *                      sequence arguments)
 */
static PyObject *
apply (PyObject *m, PyObject *py_args)
{
	PyObject *context = NULL, *seq = NULL, *item = NULL, *retval = NULL;
	FunctionEvalInfo *ei;
	char *funcname;
	int i, num_args = 0;
	Value **values = NULL;
	Value *v = NULL;


	if (!PyArg_ParseTuple (py_args, (char *) "OsO",
			       &context, &funcname, &seq))
		return NULL;

	if ((ei = (FunctionEvalInfo *) PyCObject_AsVoidPtr (context)) == NULL)
		return NULL;

	/* Third arg should be a sequence */
	if (!PySequence_Check (seq)) {
		PyErr_SetString (PyExc_TypeError,
				 "Argument list must be a sequence");
		return NULL;
	}

	num_args = PySequence_Length (seq);
	values = g_new0 (Value*, num_args);
	for (i = 0; i < num_args; ++i)
	{
		item = PySequence_GetItem (seq, i);
		if (item == NULL)
			goto cleanup;
		values[i] = value_from_python (item, ei->pos);
		if (PyErr_Occurred ()) {
			Py_DECREF (item);
			goto cleanup;
		}
	}

	v = function_call_with_values (ei->pos, funcname, num_args, values);
	if (v->type == VALUE_ERROR) {
		/* Raise an exception */
		retval = NULL;
		PyErr_SetString (GnumericError, v->v_err.mesg->str);
	} else
		retval = value_to_python (v);

cleanup:
	if (v)
		value_release (v);
	Py_XDECREF (item);
	for (i = 0; i < num_args; ++i)
		if (values[i])
			value_release (values[i]);
	g_free (values);

	/* We do not own a reference to seq, so don't decrement it. */
	return retval;
}

/**
 * register_function
 * @m        Dummy
 * @py_args  Argument tuple
 *
 * Make a Python function known to Gnumeric. Returns the Python object None.
 *
 * hmm. How to distinguish between varargs and no args?.
 *      - Don't distinguish at all?
 *      - Separate keyword arguments: args and varargs?
 *
 * Signature when called from Python:
 *      gnumeric.register_function (string function_name,
 *                                  string category,
 *                                  string argument_format,
 *                                  string argument_names,
 *                                  string help_string,
 *                                  function python_function)
 */
static PyObject *
register_function (PyObject *m, PyObject *py_args)
{
	FunctionCategory *cat;
	FunctionDefinition *fndef;
	FuncData *fdata;
	char *name, *category_name, *args, *named_args, *help1;
	char const **help;
	PyObject *codeobj;

	if (!PyArg_ParseTuple (py_args, (char *) "sssssO",
			       &name, &category_name,
			       &args, &named_args, &help1, &codeobj))
		return NULL;

	if (!PyCallable_Check (codeobj)){
		PyErr_SetString (PyExc_TypeError, _("object must be callable"));
		return NULL;
	}

	cat   = function_get_category (category_name);
	help = g_new (char const *, 1);
	*help = g_strdup (help1);
	if (*args)
		fndef = function_add_args (cat, g_strdup (name),
					   g_strdup (args),
					   g_strdup (named_args), help,
					   marshal_func_args);
	else
		fndef = function_add_nodes (cat, g_strdup (name), NULL,
					    g_strdup (named_args), help,
					    marshal_func_nodes);

	fdata = g_new (FuncData, 1);
	fdata->fndef   = fndef;
	fdata->codeobj = codeobj;
	Py_INCREF (codeobj);
	funclist = g_list_append (funclist, fdata);

	Py_INCREF (Py_None);
	return Py_None;
}

/**
 * gnumeric_funcs
 *
 * Method table.
 */
static const PyMethodDef gnumeric_funcs[] = {
	{ (char *) "apply",             apply,             METH_VARARGS },
	{ (char *) "register_function", register_function, METH_VARARGS },
	{ NULL, NULL },
};


/**
 * initgnumeric
 *
 * Initialize module.
 */
static void
initgnumeric (void)
{
	PyObject *m, *d;

	PyImport_AddModule ((char *) "gnumeric");
	m = Py_InitModule ((char *) "gnumeric",
			   (PyMethodDef *) gnumeric_funcs);

	/* Add our own exception class. */
	d = PyModule_GetDict (m);
	GnumericError = PyErr_NewException ((char *) "gnumeric.error",
					    NULL, NULL);
	if (GnumericError != NULL)
		PyDict_SetItemString (d, (char *) "error", GnumericError);
}

gboolean
plugin_can_deactivate_general (void)
{
	return FALSE;
}

void
plugin_cleanup_general (ErrorInfo **ret_error)
{
	*ret_error = NULL;
}

#ifdef BROKEN_PY_INITIALIZE
extern char **environ;
#endif

void
plugin_init_general (ErrorInfo **ret_error)
{

#ifdef BROKEN_PY_INITIALIZE
	int i;

	/*
	 * Python's convertenviron has gotten into its head that it can
	 * write to the strings in the environment.  We have little choice
	 * but to allocate a copy of everything.
	 */
	for (i = 0; environ[i]; i++)
		environ[i] = g_strdup (environ[i]);
#endif

	*ret_error = NULL;
	/* initialize the python interpreter */
	Py_SetProgramName ((char *) "gnumeric");
	Py_Initialize ();

	/* setup standard functions */
	initgnumeric ();
	if (PyErr_Occurred ()) {
		char *exc_string = string_from_exception ();
		PyErr_Print (); /* Also do a full traceback to stderr */
		*ret_error = error_info_new_printf (
		             _("Unhandled Python exception: %s"), exc_string);
		g_free (exc_string);
		Py_Finalize ();
		return;
	}

	/* plugin stuff */

	{
		int ret;
		char *dir, *name, buf[256];
		FILE *fp;

		/* Add gnumeric python directory to sys.path, so that we can
		 * import modules  from there */
		dir = gnumeric_sys_data_dir ("python");
		name = g_concat_dir_and_file (dir, "gnumeric_startup.py");

		ret = PyRun_SimpleString ((char *) "import sys");
		if (ret == 0) {
			g_snprintf (buf, sizeof buf,
				    "sys.path.append(\"%s\")", dir);
			ret = PyRun_SimpleString (buf);
		}

		/* run the python initialization file */
		fp = fopen (name, "r");
		if (fp){
			PyRun_SimpleFile (fp, name);
			fclose (fp);
		}
		g_free (name);
		g_free (dir);
	}
}
