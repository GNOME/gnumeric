/*
 * Rudimentary support for Python in gnumeric.
 */

#include <gnome.h>
#include <string.h>
#include "../../src/gnumeric.h"
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
marshal_func (FunctionDefinition *fndef, Value *argv[], char **error_string)
{
	PyObject *args, *result;
	Value *v;
	GList *l;
	int i, count = strlen(fndef->args);
	
	/* Find the Python code object for this FunctionDefinition. */
	l = g_list_find_custom(funclist, fndef, (GCompareFunc) fndef_compare);
	if (!l) {
		*error_string = "Unable to lookup Python code object.";
		return NULL;
	}
	
	/* Build the argument list which is a Python tuple. */
	args = PyTuple_New (count);
	for (i = 0; i < count; i++){
		/* ref is stolen from us */
		PyTuple_SetItem(args, i, convert_value_to_python (argv[i]));  
	}
	
	/* Call the Python object. */
	result = PyEval_CallObject (((FuncData *)(l->data))->codeobj, args);
	Py_DECREF (args);

	if (!result){
		*error_string = "Python exception.";
		PyErr_Clear (); /* XXX should popup window with exception info */
		return NULL;
	}
	
	v = convert_python_to_value (result);
	Py_DECREF (result);
	return v;
}

static PyObject *
__register_function (PyObject *m, PyObject *py_args)
{
	FunctionDefinition *fndef;
	FuncData *fdata;
	char *name, *args, *named_args, *help1, **help;
	PyObject *codeobj;

	if (!PyArg_ParseTuple (py_args, "ssssO", &name, &args, &named_args, &help1, &codeobj))
		return NULL;
	
	if (!PyCallable_Check (codeobj)){
		PyErr_SetString (PyExc_TypeError, "object must be callable");
		return NULL;
	}
	
	fndef = g_new0 (FunctionDefinition, 1);
	if (!fndef){
		PyErr_SetString (PyExc_MemoryError, "could not alloc FuncDef");
		return NULL;
	}
	
	fdata = g_new (FuncData, 1);
	fdata->fndef = fndef;
	fdata->codeobj = codeobj;
	Py_INCREF(codeobj);
	funclist = g_list_append(funclist, fdata);
	
	fndef->name    	       = g_strdup (name);
	fndef->args    	       = g_strdup (args);
	fndef->named_arguments = g_strdup (named_args);
	fndef->help            = g_new (char *, 1);
	*fndef->help           = g_strdup (help1);
	
	fndef->fn              = marshal_func;
	
	symbol_install (global_symbol_table, fndef->name, SYMBOL_FUNCTION, fndef);
	
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
	g_print ("Gnumeric/Python module initialized\n");
}  

static int
no_unloading_for_me (PluginData *pd)
{
	return 0;
}

int
init_plugin (PluginData * pd)
{
	pd->can_unload = no_unloading_for_me;
	pd->title = g_strdup ("Python Plugin");
	
	
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
