/*
 * Support for Python in gnumeric.
 */

/**
 * TODO:
 * Arrays   - Use Python arrays
 * Booleans - Do we need a distinguished data type so that Gnumeric can
 *            recognize that Python is returning a boolean? If so, we can
 *            define a Python boolean class.
 * Cell ranges - The sheet attribute is not yet handled.
 * Varargs
 */

#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "func.h"
#include "symbol.h"
#include "plugin.h"
#include "value.h"
#include "command-context.h"

#include "Python.h"

/* Classes we define in Python code, and where we define them. */
#define GNUMERIC_DEFS_MODULE "gnumeric_defs"
#define CELL_REF_CLASS   "CellRef"
#define CELL_RANGE_CLASS "CellRange"

/* This is standard idiom for defining exceptions in extension modules. */
static PyObject *GnumericError;

/* Yuck!
 * See comment in plugins/guile/plugin.c
 */
static EvalPosition const *eval_pos = NULL;

/**
 * convert_py_exception_to_string
 *
 * Converts the current Python exception to a C string. Returns C string.
 * Caller must free it.
 */
static char *
convert_py_exception_to_string ()
{
	char *header = _("Python exception");
	char *retval = header;
	char buf [256];
	int pos = 0;
	
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
		/* Include exception class in the error message */
		stype = PyObject_Str (ptype);
		if (!stype)
			goto cleanup;
		pos = snprintf (buf, sizeof buf, "%s: %s",
				header, PyString_AsString (stype));
		retval = buf;
	
		if (!svalue)
			goto cleanup;
		if (pos + 3 < sizeof buf)
			snprintf (buf + pos , sizeof buf - pos , ": %s",
				  PyString_AsString (svalue));
	}
cleanup:
	Py_XDECREF (stype);
	Py_XDECREF (svalue);
	PyErr_Restore (ptype, pvalue, ptraceback);

	return g_strdup (retval);
}

/*
 * Support for registering Python-based functions for use in formulas.
 */

/**
 * convert_cell_ref_to_python
 * @v   value union
 *
 * Converts a cell reference to Python. Returns python object.
 */
static PyObject *
convert_cell_ref_to_python (CellRef *cell)
{
	PyObject *mod, *klass = NULL, *ret = NULL;

	mod = PyImport_ImportModule(GNUMERIC_DEFS_MODULE);
	if (PyErr_Occurred ())
		goto cleanup;

	klass  = PyObject_GetAttrString(mod, CELL_REF_CLASS);
	if (PyErr_Occurred ())
		goto cleanup;

	ret = PyObject_CallFunction (klass, "iiii", cell->col, cell->row,
				     cell->col_relative, cell->row_relative
				     /*, sheet */);
cleanup:
	Py_XDECREF (klass);
	return ret;
}

/**
 * convert_range_to_python
 * @v   value union
 *
 * Converts a cell range to Python. Returns python object.
 */
static PyObject *
convert_range_to_python (Value *v)
{
	PyObject *mod, *klass = NULL, *ret = NULL;

	mod = PyImport_ImportModule (GNUMERIC_DEFS_MODULE);
	if (PyErr_Occurred ())
		goto cleanup;

	klass  = PyObject_GetAttrString (mod, CELL_RANGE_CLASS);
	if (PyErr_Occurred ())
		goto cleanup;

	ret = PyObject_CallFunction
		(klass, "O&O&",
		 convert_cell_ref_to_python, &v->v.cell_range.cell_a,
		 convert_cell_ref_to_python, &v->v.cell_range.cell_b);

cleanup:
	Py_XDECREF (klass);
	return ret;
}

/**
 * convert_boolean_to_python
 * @v   value union
 *
 * Converts a boolean to Python. Returns python object.
 * NOTE: This implementation converts to Python	integer, so it will not
 * be possible to convert back to Gnumeric boolean.
 */
static PyObject *
convert_boolean_to_python (Value *v)
{
	PyObject * o;

	o = PyInt_FromLong (v->v.v_bool);

	return o;
}

/**
 * convert_value_to_python
 * @v   value union
 *
 * Converts a Gnumeric value to Python. Returns python object.
 *
 * VALUE_ERROR is not handled here. It is not possible to receive VALUE_ERROR
 * as a parameter to a function.  But a C function may return VALUE_ERROR. In
 * this case, the caller an exception is raised before convert_value_to_python
 * is called.
 */
static PyObject *
convert_value_to_python (Value *v)
{
	PyObject *o;

	switch (v->type) {
	case VALUE_INTEGER:
		o = PyInt_FromLong(v->v.v_int);
		break;

	case VALUE_FLOAT:
		o = PyFloat_FromDouble(v->v.v_float);
		break;

	case VALUE_STRING:
		o = PyString_FromString(v->v.str->str);
		break;

	case VALUE_CELLRANGE:
		o = convert_range_to_python(v);
		break;
	case VALUE_EMPTY:
		o = Py_None;
		break;
	case VALUE_BOOLEAN:	/* Sort of implemented */
		o = convert_boolean_to_python (v);
		break;
		/* The following aren't implemented yet */
	case VALUE_ARRAY:
	case VALUE_ERROR:
	default:
		o = NULL;
		break;
	}
	return o;
}

/**
 * convert_cell_ref_from_python
 * @o    Python object
 * @c    Cell reference
 *
 * Converts a Python cell reference to Gnumeric. Returns TRUE on success and
 * FALSE on failure.
 * Used as a converter function by PyArg_ParseTuple.
 */
static int
convert_cell_ref_from_python (PyObject *o, CellRef *c)
{
	int ret = FALSE;
	PyObject *column = NULL, *row = NULL;
	PyObject *col_relative = NULL, *row_relative = NULL/*, *sheet = NULL */;

	column       = PyObject_GetAttrString (o, "column");
	if (PyErr_Occurred () || !PyInt_Check (column))
		goto cleanup;
	row          = PyObject_GetAttrString (o, "row");
	if (PyErr_Occurred () || !PyInt_Check (row))
		goto cleanup;
	col_relative = PyObject_GetAttrString (o, "col_relative");
	if (PyErr_Occurred () || !PyInt_Check (col_relative))
		goto cleanup;
	row_relative = PyObject_GetAttrString (o, "row_relative");
	if (PyErr_Occurred () || !PyInt_Check (row_relative))
		goto cleanup;
	/* sheet        = PyObject_GetAttrString (o, "sheet");
	if (PyErr_Occurred () || !PyString_Check (sheet)) {
		ret = -1;
		goto cleanup;
		} */
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
 * convert_range_from_python
 * @o    Python object
 * @v    Value union
 *
 * Converts a Python cell range to Gnumeric. Returns TRUE on success and
 * FALSE on failure.
 * Conforms to calling conventions for converter functions for
 * PyArg_ParseTuple. 
 */
static int
convert_range_from_python (PyObject *o, Value *v)
{
	int ret = FALSE;
	PyObject *range = NULL;

	memset (v, 0, sizeof (*v));
	v->type  = VALUE_CELLRANGE;

	range = PyObject_GetAttrString  (o, "range");
	if (PyErr_Occurred ())
		goto cleanup;
	/* Slightly abusing PyArg_ParseTuple */
	if (!PyArg_ParseTuple (range, "O&O&",
			       convert_cell_ref_from_python,
			       &v->v.cell_range.cell_a,
			       convert_cell_ref_from_python,
			       &v->v.cell_range.cell_b))
		goto cleanup;
	ret = TRUE;
	
cleanup:
	Py_XDECREF (range);

	return ret;
}

/**
 * convert_python_to_value
 * @o   Python object
 *
 * Converts a Python object to a Gnumeric value. Returns Gnumeric value.
 */
static Value *
convert_python_to_value (PyObject *o)
{
	Value *v = g_new (Value, 1);
	Value *ret = NULL;

	if (!v)
		return NULL;

	if (PyErr_Occurred ()) {
		goto cleanup;
	} else if (o == Py_None) {
		v->type = VALUE_EMPTY;
	} else if (PyInt_Check (o)){
		v->type = VALUE_INTEGER;
		v->v.v_int = (int_t) PyInt_AsLong (o);
		ret = v;
	} else if (PyFloat_Check (o)) {
		v->type = VALUE_FLOAT;
		v->v.v_float = (float_t) PyFloat_AsDouble (o);
		ret = v;
	} else if (PyString_Check (o)) {
		v->type = VALUE_STRING;
		v->v.str = string_get (PyString_AsString (o));
		ret = v;
	} else if (PyObject_HasAttrString (o, "__class__")) {
		PyObject *klass = PyObject_GetAttrString  (o, "__class__");
		gchar *s;
		
		if (PyErr_Occurred ())
			goto cleanup;

		s = PyString_AsString (PyObject_Str(klass));
		Py_XDECREF (klass);

 		if (s && strcmp (s,
				 GNUMERIC_DEFS_MODULE "." CELL_RANGE_CLASS)
		    == 0) {
			if (convert_range_from_python (o, v))
				ret = v;
		}
	}

cleanup:
	if (PyErr_Occurred ()) {
		char *exc_string = convert_py_exception_to_string ();
		v->type = VALUE_ERROR;
		v->v.error.mesg = string_get (exc_string);
		g_free (exc_string);
		PyErr_Print ();	/* Traceback to stderr for now */
		PyErr_Clear ();
	} else if (!ret) {
		v->type = VALUE_ERROR;
		v->v.error.mesg	= string_get (_("Unknown Python type"));
	}

	return ret;
}

typedef struct {
	FunctionDefinition *fndef;
	PyObject *codeobj;
} FuncData;

static GList *funclist = NULL;

static int
fndef_compare(FuncData *fdata, FunctionDefinition *fndef)
{
	return (fdata->fndef != fndef);
}

static Value *
marshal_func (FunctionEvalInfo *ei, Value *argv [])
{
	PyObject *args, *result;
	FunctionDefinition const * const fndef = ei->func_def;
	Value *v;
	GList *l;
	EvalPosition const *old_eval_pos;
	int i, min, max;
	char *exc_string;
	
	function_def_count_args (fndef, &min, &max);

	/* Find the Python code object for this FunctionDefinition. */
	l = g_list_find_custom (funclist, (gpointer)fndef,
				(GCompareFunc) fndef_compare);
	if (!l)
		return value_new_error
			(ei->pos, _("Unable to lookup Python code object."));

	/* Build the argument list which is a Python tuple. */
	args = PyTuple_New (min);
	for (i = 0; i < min; i++) {
		/* ref is stolen from us */
		PyTuple_SetItem (args, i, convert_value_to_python (argv [i]));
	}

	old_eval_pos = eval_pos;
	eval_pos = ei->pos;
	/* Call the Python object. */
	result = PyEval_CallObject (((FuncData *)(l->data))->codeobj, args);
	Py_DECREF (args);
	eval_pos = old_eval_pos;

	if (!result) {
		exc_string = convert_py_exception_to_string ();
		v = value_new_error (ei->pos, exc_string);
		g_free (exc_string);
		PyErr_Print ();	/* Traceback to stderr for now */
		PyErr_Clear ();
		return v;
	}

	v = convert_python_to_value (result);
	Py_DECREF (result);
	return v;
}

/**
 * gnumeric_apply
 * @m        Dummy
 * @py_args  Argument tuple
 *
 * Apply a gnumeric function to the arguments in py_args. Returns result as a
 * Python object.
 *
 * Signature when called from Python:
 *      gnumeric.apply (string function_name, sequence arguments)
 */
static PyObject *
gnumeric_apply (PyObject *m, PyObject *py_args)
{
	PyObject *seq = NULL, *item = NULL, *retval = NULL;
	char *funcname;
	int i, num_args;
	Value **values;
	Value *v = NULL;

	if (!PyArg_ParseTuple (py_args, "sO", &funcname, &seq))
		goto cleanup;

	/* Second arg should be a sequence */
	if (!PySequence_Check (seq)) {
		PyErr_SetString (PyExc_TypeError,
				 "Argument list must be a sequence");
		goto cleanup;
	}

	num_args = PySequence_Length (seq);
	values = g_new(Value*, num_args);
	for (i = 0; i < num_args; ++i)
	{
		item = PySequence_GetItem (seq, i);
		if (item == NULL)
			goto cleanup;
		Py_DECREF (item);
		values[i] = convert_python_to_value (item);
	}
	Py_INCREF (item);	/* Otherwise, we would decrement twice. */

	v = function_call_with_values (eval_pos, funcname, num_args, values);
	if (v->type == VALUE_ERROR) {
		/* Raise an exception */
		retval = NULL;	
		PyErr_SetString (GnumericError, v->v.error.mesg->str);
	} else 
		retval = convert_value_to_python (v);
	
cleanup:
	Py_XDECREF (item);
	/* We do not own a reference to seq, so don't decrement it. */
	return retval;
}

/**
 * gnumeric_register_function
 * @m        Dummy
 * @py_args  Argument tuple
 *
 * Make a Python function known to Gnumeric. Returns the Python object None. 
 *
 * Signature when called from Python:
 *      gnumeric.register_function (string function_name,
 *                                  string argument_format,
 *                                  string argument_names,
 *                                  string help_string,
 *                                  function python_function)
 */
static PyObject *
gnumeric_register_function (PyObject *m, PyObject *py_args)
{
	FunctionCategory *cat;
	FunctionDefinition *fndef;
	FuncData *fdata;
	char *name, *args, *named_args, *help1, **help;
	PyObject *codeobj;

	if (!PyArg_ParseTuple (py_args, "ssssO", &name, &args, &named_args, &help1, &codeobj))
		return NULL;

	if (!PyCallable_Check (codeobj)){
		PyErr_SetString (PyExc_TypeError, _("object must be callable"));
		return NULL;
	}

	cat   = function_get_category (_("Python"));
	help  = g_new (char *, 1);
	*help = g_strdup (help1);
	fndef = function_add_args (cat, g_strdup (name), g_strdup (args),
				   g_strdup (named_args), help, marshal_func);

	fdata = g_new (FuncData, 1);
	fdata->fndef   = fndef;
	fdata->codeobj = codeobj;
	Py_INCREF(codeobj);
	funclist = g_list_append(funclist, fdata);

	Py_INCREF (Py_None);
	return Py_None;
}

/**
 * gnumeric_funcs
 *
 * Method table.
 */
static PyMethodDef gnumeric_funcs[] = {
	{ "apply",             gnumeric_apply,             METH_VARARGS },
	{ "register_function", gnumeric_register_function, METH_VARARGS },
	{ NULL, NULL },
};


/**
 * initgnumeric
 *
 * Initialize module.
 */
static void
initgnumeric(void)
{
	PyObject *m, *d;
	
	PyImport_AddModule ("gnumeric");
	m = Py_InitModule ("gnumeric", gnumeric_funcs);

	/* Add our own exception class. */
	d = PyModule_GetDict(m);
	GnumericError = PyErr_NewException("gnumeric.error", NULL, NULL);
	if (GnumericError != NULL)
		PyDict_SetItemString(d, "error", GnumericError);
}

static int
no_unloading_for_me (PluginData *pd)
{
	return FALSE;
}

static void
no_cleanup_for_me (PluginData *pd)
{
        return;
}

#define PY_TITLE _("Python Plugin")
#define PY_DESCR \
_("This plugin provides Python language support in Gnumeric")

/**
 * init_plugin
 * @context Command context
 * @pd      PluginData
 *
 * Initialize the plugin. Returns result.
 */
PluginInitResult
init_plugin (CommandContext *context, PluginData * pd)
{
	char *exc_string;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	/* initialize the python interpreter */
	Py_SetProgramName ("gnumeric");
	Py_Initialize ();

	/* setup standard functions */
	initgnumeric ();
	if (PyErr_Occurred ()) {
		exc_string = convert_py_exception_to_string ();
		PyErr_Print (); /* Also do a full traceback to stderr */
		gnumeric_error_plugin_problem (context, exc_string);
		g_free (exc_string);
		Py_Finalize ();
		return PLUGIN_QUIET_ERROR;
	}

	/* plugin stuff */

	{
		int ret = -1;
		char *dir = NULL, *name = NULL, buf[256];
		FILE *fp;

		/* Add gnumeric python directory to sys.path, so that we can
		 * import modules  from there */
		dir = gnome_unconditional_datadir_file ("gnumeric/python");
		name = g_strjoin ("/", dir, "gnumeric_startup.py", NULL);
			
		ret = PyRun_SimpleString ("import sys");
		if (ret == 0) {
			g_snprintf (buf, sizeof buf,
				    "sys.path.append(\"%s\")", dir);
			ret = PyRun_SimpleString (buf);
		}

		/* run the python initialization file */
		fp = fopen (name, "r");
		if (fp){
			PyRun_SimpleFile(fp, name);
		}
		g_free(name);
		g_free(dir);
	}

	if (plugin_data_init (pd, no_unloading_for_me, no_cleanup_for_me,
			      PY_TITLE, PY_DESCR))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;

}
