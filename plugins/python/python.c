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

#include "Python.h"

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

static Value *
marshal_func (FunctionEvalInfo *ei, Value *argv [])
{
	PyObject *args, *result;
	FunctionDefinition *fndef = ei->func_def;
	Value *v;
	GList *l;
	int i, min, max;

	function_def_count_args (fndef, &min, &max);

	/* Find the Python code object for this FunctionDefinition. */
	l = g_list_find_custom (funclist, fndef, (GCompareFunc) fndef_compare);
	if (!l)
		return value_new_error (&ei->pos, _("Unable to lookup Python code object."));

	/* Build the argument list which is a Python tuple. */
	args = PyTuple_New (min);
	for (i = 0; i < min; i++) {
		/* ref is stolen from us */
		PyTuple_SetItem (args, i, convert_value_to_python (argv [i]));
	}

	/* Call the Python object. */
	result = PyEval_CallObject (((FuncData *)(l->data))->codeobj, args);
	Py_DECREF (args);

	if (!result) {
		PyErr_Clear (); /* XXX should popup window with exception info */
		return value_new_error (&ei->pos, _("Python exception."));
	}

	v = convert_python_to_value (result);
	Py_DECREF (result);
	return v;
}

static PyObject *
__register_function (PyObject *m, PyObject *py_args)
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

	cat   = function_get_category (_("Perl"));
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
	{ "register_function", __register_function, 1 },
	{ NULL, NULL },
};

static void
initgnumeric(void)
{
	PyImport_AddModule ("gnumeric");
	Py_InitModule ("gnumeric", gnumeric_funcs);
	g_print (_("Gnumeric/Python module initialized\n"));
}

static int
no_unloading_for_me (PluginData *pd)
{
	return 0;
}

int
init_plugin (CmdContext *context, PluginData * pd)
{
	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return -2;

	pd->can_unload = no_unloading_for_me;
	pd->title = g_strdup (_("Python Plugin"));

	/* initialize the python interpreter */
	Py_SetProgramName ("gnumeric");
	Py_Initialize ();

	/* setup standard functions */
	initgnumeric ();
	if (PyErr_Occurred ()) {
		PyErr_Print ();
		return -1;
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

	return 0;
}
