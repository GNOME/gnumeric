/*
 * Rudimentary support for Python in gnumeric.
 */

#include <config.h>
#include <gnome.h>
#include <string.h>
#include "../../src/gnumeric.h"
#include "../../src/func.h"
#include "../../src/symbol.h"
#include "../../src/plugin.h"
#include "../../src/command-context.h"

#include "Python.h"

/* Yuck!
 * See comment in plugins/guile/plugin.c
 */
static EvalPosition *eval_pos = NULL;

/*
 * Support for registering Python-based functions for use in formulas.
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

		/* The following aren't implemented yet */
	case VALUE_EMPTY:
	case VALUE_BOOLEAN:
	case VALUE_ERROR:
	case VALUE_CELLRANGE:
	case VALUE_ARRAY:
	default:
		o = NULL;
		break;
	}
	return o;
}

static Value *
convert_python_to_value (PyObject *o)
{
	Value *v = g_new (Value, 1);

	if (!v)
		return NULL;

	if (PyInt_Check (o)){
		v->type = VALUE_INTEGER;
		v->v.v_int = (int_t) PyInt_AsLong (o);
	} else if (PyFloat_Check (o)) {
		v->type = VALUE_FLOAT;
		v->v.v_float = (float_t) PyFloat_AsDouble (o);
	} else if (PyString_Check (o)) {
		int size = PyString_Size (o);
		gchar *s;

		s = g_malloc (size + 1);
		strncpy(s, PyString_AsString (o), size);
		s[size] = '\0';
		v->type = VALUE_STRING;
		v->v.str = string_get (s);
		g_free (s);
	} else {
		g_free (v);
		return NULL;
	}

	return v;
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

static char *
convert_py_exception_to_string ()
{
	char *header = _("Python exception");
	char *retval = header;
	char buf [256];
	int pos;
	
	PyObject *ptype = NULL, *pvalue = NULL, *ptraceback = NULL;
	PyObject *stype = NULL, *svalue = NULL;
	
	PyErr_Fetch (&ptype, &pvalue, &ptraceback);
	if (!ptype)
		goto cleanup;
	stype = PyObject_Str (ptype);
	if (!stype)
		goto cleanup;
	pos = snprintf (buf, sizeof buf, "%s: %s",
			header, PyString_AsString (stype));
	retval = buf;
	
	if (pvalue && (pos + 3 < sizeof buf))
		svalue = PyObject_Str (pvalue);
	if (!svalue)
		goto cleanup;
	snprintf (buf + pos , sizeof buf - pos , ": %s",
			PyString_AsString (svalue));
cleanup:
	Py_XDECREF (stype);
	Py_XDECREF (svalue);
	PyErr_Restore (ptype, pvalue, ptraceback);

	return g_strdup (retval);
}

/* FIXME: What to about exceptions?
 *
 * Suggestion: A python console to display tracebacks - tag each with
 * eval_pos.  Certainly not a popup window for each exception. There
 * can be many for each recomputation. */
/* It is possible to replace the sys.stderr binding with a cStringIO object.

  { PyObject *mod, *klass, *obj;

    mod = PyImport_ImportModule("cStringIO");
    klass = PyObject_GetAttrString(mod, "StringIO");
    obj = PyObject_CallFunction(klass, NULL);
    Py_DECREF(klass);

    PySys_SetObject("stderr", obj);
  }
  -- "Michael P. Reilly" <arcege@shore.net>
*/

static Value *
marshal_func (FunctionEvalInfo *ei, Value *argv [])
{
	PyObject *args, *result;
	FunctionDefinition const * const fndef = ei->func_def;
	Value *v;
	GList *l;
	EvalPosition *old_eval_pos;
	int i, min, max;
	char *exc_string;
	
	function_def_count_args (fndef, &min, &max);

	/* Find the Python code object for this FunctionDefinition. */
	l = g_list_find_custom (funclist, (gpointer)fndef, (GCompareFunc) fndef_compare);
	if (!l)
		return value_new_error (ei->pos, _("Unable to lookup Python code object."));

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
		PyErr_Print ();	/* Traceback to stderr for now */
		PyErr_Clear ();
		v = value_new_error (ei->pos, exc_string);
		g_free (exc_string);
		return v;
	}

	v = convert_python_to_value (result);
	Py_DECREF (result);
	return v;
}

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
		PyErr_SetString(PyExc_TypeError,
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

	v = function_call_with_values(eval_pos, funcname, num_args, values);
	retval = convert_value_to_python (v);

cleanup:
	Py_XDECREF(item);
	/* We do not own a reference to seq, so don't decrement it. */
	return retval;
}

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

static PyMethodDef gnumeric_funcs[] = {
	{ "apply",             gnumeric_apply,             METH_VARARGS },
	{ "register_function", gnumeric_register_function, METH_VARARGS },
	{ NULL, NULL },
};

static void
initgnumeric(void)
{
	PyImport_AddModule ("gnumeric");
	Py_InitModule ("gnumeric", gnumeric_funcs);
}

static int
no_unloading_for_me (PluginData *pd)
{
	return 0;
}

PluginInitResult
init_plugin (CommandContext *context, PluginData * pd)
{
	char *exc_string;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	pd->can_unload = no_unloading_for_me;
	pd->title = g_strdup (_("Python Plugin"));

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

	/* run the magic python file */

	{
		char *name;
		FILE *fp;

		name = gnome_unconditional_datadir_file ("gnumeric/python/gnumeric_startup.py");
		fp = fopen (name, "r");
		if (fp){
			PyRun_SimpleFile(fp, name);
		}
		g_free(name);
	}

	return PLUGIN_OK;
}
