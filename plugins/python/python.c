/*
 * Rudimentary support for Python in gnumeric.
 */

#include <glib.h>
#include <gnome.h>

#include "../../src/gnumeric.h"
#include "../../src/func.h"
#include "../../src/plugin.h"

#include "Python.h"

PyObject *codeobj = NULL;

static Value *
marshal(Value *argv[], char **error_string)
{
  PyObject *args = Py_BuildValue("()");
  PyObject *result;
  Value *v;

  g_print("marshall: activated\n");

  result = PyEval_CallObject(codeobj, args);
  if (!result)
    return NULL;

  Py_DECREF(args);

  g_print("checking result\n");

  v = g_new0(Value, 1);
  if (PyInt_Check(result)) {
    g_print("marshal: making integer value\n");
    v->type = VALUE_INTEGER;
    v->v.v_int = (int_t) PyInt_AsLong(result);
  } else if (PyFloat_Check(result)) {
    g_print("marshal: making float value\n");
    v->type = VALUE_FLOAT;
    v->v.v_float = (float_t) PyFloat_AsDouble(result);
  } else {
    PyErr_SetString(PyExc_TypeError, "unexpected return type");
    g_free(v);
    v = NULL;
  }

  Py_DECREF(result);
  return v;
}

static FunctionDefinition plugin_functions [] = {
  { "plusone", "f", "number", NULL, NULL, marshal },
  { NULL, NULL },
};

static PyObject *
__register_function(PyObject *m, PyObject *args)
{
  char *funcname;

  if (! PyArg_ParseTuple(args, "sO", &funcname, &codeobj))
    return NULL;

  g_print("__register_function called (func=%s)\n", funcname);

  if (! PyCallable_Check(codeobj)) {
    g_print("object is not callable\n");
    PyErr_SetString(PyExc_TypeError, "object must be callable");
    return NULL;
  }

  g_print("object is callable\n");
  
  plugin_functions[0].name = g_strdup(funcname);
  g_print("installed function name\n");
  install_symbols(plugin_functions);
  g_print("told gnumeric about the function");
  
  return Py_BuildValue("");
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

int
init_plugin(PluginData * pd)
{
  PyObject *m, *d, *f;

  /* initialize the python interpreter */
  g_print("python plugin started\n");
  Py_SetProgramName("gnumeric");
  Py_Initialize();

  /* setup standard functions */

  initgnumeric();
  if (PyErr_Occurred()) {
    PyErr_Print();
    return -1;
  }

  /* plugin stuff */

  pd->title = g_strdup("Python Plugin");

  /* run the magic python file */

  {
    char *homedir = getenv("HOME");
    char *fname;
    FILE *fp;

    g_print("running python file\n");
    g_warning ("FIXME: This should load a systme installed file");
    fname = g_copy_strings(homedir ? homedir : "", "/.gnumeric/main.py", NULL);
    fp = fopen(fname, "r");
    PyRun_AnyFile(fp, fname);
    g_free(fname);
    g_print("done running python file\n");
  }

  return 0;
}

void cleanup_plugin(PluginData *pd)
{
  g_free(pd->title);
  Py_Finalize();
}
