/*
 * Rudimentary support for Python in gnumeric.
 */

#include <glib.h>
#include <gnome.h>

#include "../../src/gnumeric.h"
#include "../../src/symbol.h"
#include "../../src/plugin.h"

#include "Python.h"

/*
 * Support for registering Python-based functions for use in formulas.
 */

static PyObject *
v2py(Value *v)
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
py2v(PyObject *o)
{
  Value *v = g_new(Value, 1);

  if (!v) return NULL;

  if (PyInt_Check(o)) {
    v->type = VALUE_INTEGER;
    v->v.v_int = (int_t) PyInt_AsLong(o);
  } else if (PyFloat_Check(o)) {
    v->type = VALUE_FLOAT;
    v->v.v_float = (float_t) PyFloat_AsDouble(o);
  } else {
    g_free(v);
    return NULL;
  }

  /* Python strings ae not null-terminated ... add code later */ 

  return v;
}

typedef struct {
  FunctionDefinition *fndef;
  PyObject *codeobj;
} FuncData;

static GList *funclist = NULL;

static int
fndef_compare(FunctionDefinition *fndef, FuncData *fdata)
{
  return (fdata->fndef == fndef);
}

static Value *
marshal_func(FunctionDefinition *fndef, Value *argv[], char **error_string)
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

  /* Build the argument list wihch is a Python tuple. */
  args = PyTuple_New(count);
  for (i = 0; i < count; i++) {
    PyTuple_SetItem(args, i, v2py(argv[i]));  /* ref is stolen from us */
  }

  /* Call the Python object. */
  result = PyEval_CallObject( ((FuncData *)(l->data))->codeobj, args);
  Py_DECREF(args);
  if (!result) {
    *error_string = "Python exception.";
    PyErr_Clear(); /* XXX should popup window with exception info */
    return NULL;
  }

  v = py2v(result);
  Py_DECREF(result);
  return v;
}

static PyObject *
__register_function(PyObject *m, PyObject *args)
{
  FunctionDefinition *fndef;
  FuncData *fdata;
  char *funcname, *params;
  PyObject *codeobj;

  if (! PyArg_ParseTuple(args, "ssO", &funcname, &params, &codeobj))
    return NULL;

  if (! PyCallable_Check(codeobj)) {
    PyErr_SetString(PyExc_TypeError, "object must be callable");
    return NULL;
  }

  fndef = g_new0(FunctionDefinition, 1);
  if (!fndef) {
    PyErr_SetString(PyExc_MemoryError, "could not alloc FuncDef");
    return NULL;
  }

  fdata = g_new(FuncData, 1);
  fdata->fndef = fndef;
  fdata->codeobj = codeobj;
  Py_INCREF(codeobj);
  funclist = g_list_append(funclist, fdata);

  fndef->name = g_strdup(funcname);
  fndef->args = g_strdup(params);
  fndef->fn = marshal_func;

  symbol_install(fndef->name, SYMBOL_FUNCTION, fndef);

  Py_INCREF(Py_None);
  return Py_None;
}

static PyMethodDef gnumeric_funcs[] = {
  { "register_function", __register_function, 1 },
  { NULL, NULL },
};

static void
initgnumeric(void)
{
  PyImport_AddModule("gnumeric");
  Py_InitModule("gnumeric", gnumeric_funcs);
  g_print("gnumeric module initialized\n");
}  

static int
no_unloading_for_me(PluginData *pd)
{
  return 0;
}

int
init_plugin(PluginData * pd)
{
  PyObject *m, *d, *f;

  pd->can_unload = no_unloading_for_me;
  pd->title = g_strdup("Python Plugin");


  /* initialize the python interpreter */
  Py_SetProgramName("gnumeric");
  Py_Initialize();

  /* setup standard functions */
  initgnumeric();
  if (PyErr_Occurred()) {
    PyErr_Print();
    return -1;
  }

  /* plugin stuff */

  /* run the magic python file */
  /* XXX should run single Python file in system directory. This file would
   * then contain policy for the loading the remainder of the Python
   * scripts.
   */

  {
    char *homedir = getenv("HOME");
    char *fname;
    FILE *fp;

    g_warning ("FIXME: This should load a system installed file");
    fname = g_copy_strings(homedir ? homedir : "", "/.gnumeric/main.py", NULL);
    fp = fopen(fname, "r");
    PyRun_SimpleFile(fp, fname);
    /* XXX Detect Python exception and popup window with info */
    g_free(fname);
  }

  return 0;
}
