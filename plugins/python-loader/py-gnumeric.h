#ifndef PLUGIN_PY_GNUMERIC_H
#define PLUGIN_PY_GNUMERIC_H

#include <Python.h>
#include <glib.h>
#include <plugin.h>
#include "gnm-py-interpreter.h"

void     py_initgnumeric (GnmPyInterpreter *interpreter);

Value    *call_python_function (PyObject *python_fn, const EvalPos *eval_pos, gint n_args, Value **args);
PyObject *python_call_gnumeric_function (GnmFunc *fn_def, const EvalPos *opt_eval_pos, PyObject *args);
Value    *convert_python_exception_to_gnumeric_value (const EvalPos *eval_pos);
gchar    *convert_python_exception_to_string (void);
PyObject *convert_gnumeric_value_to_python (const EvalPos *eval_pos, const Value *val);
Value    *convert_python_to_gnumeric_value (const EvalPos *eval_pos, PyObject *py_val);


PyTypeObject py_Boolean_object_type;
typedef struct _py_Boolean_object py_Boolean_object;
PyObject      *py_new_Boolean_object (gboolean value);
gboolean       py_Boolean_as_gboolean (py_Boolean_object *self);

PyTypeObject py_CellPos_object_type;
typedef struct _py_CellPos_object py_CellPos_object;
PyObject      *py_new_CellPos_object (const CellPos *cell_pos);
CellPos       *py_CellPos_as_CellPos (py_CellPos_object *self);

PyTypeObject py_Range_object_type;
typedef struct _py_Range_object py_Range_object;
PyObject      *py_new_Range_object (const Range *range);
Range         *py_Range_as_Range (py_Range_object *self);

PyTypeObject py_CellRef_object_type;
typedef struct _py_CellRef_object py_CellRef_object;
PyObject      *py_new_CellRef_object (const CellRef *cell_ref);
CellRef       *py_CellRef_as_CellRef (py_CellRef_object *self);

PyTypeObject py_RangeRef_object_type;
typedef struct _py_RangeRef_object py_RangeRef_object;
PyObject      *py_new_RangeRef_object (const RangeRef *range_ref);
RangeRef      *py_RangeRef_as_RangeRef (py_RangeRef_object *self);

PyTypeObject py_MStyle_object_type;
typedef struct _py_MStyle_object py_MStyle_object;
PyObject      *py_new_MStyle_object (MStyle *mstyle);
MStyle        *py_mstyle_as_MStyle (py_MStyle_object *self);

PyTypeObject py_Cell_object_type;
typedef struct _py_Cell_object py_Cell_object;
PyObject      *py_new_Cell_object (Cell *cell);
Cell          *py_Cell_as_Cell (py_Cell_object *self);

PyTypeObject py_Sheet_object_type;
typedef struct _py_Sheet_object py_Sheet_object;
PyObject      *py_new_Sheet_object (Sheet *sheet);
Sheet         *py_sheet_as_Sheet (py_Sheet_object *self);

PyTypeObject py_Workbook_object_type;
typedef struct _py_Workbook_object py_Workbook_object;
PyObject      *py_new_Workbook_object (Workbook *wb);
Workbook      *py_Workbook_as_Workbook (py_Workbook_object *self);

PyTypeObject py_GnmPlugin_object_type;
typedef struct _py_GnmPlugin_object py_GnmPlugin_object;
PyObject      *py_new_GnmPlugin_object (GnmPlugin *pinfo);
GnmPlugin    *py_GnmPlugin_as_GnmPlugin (py_GnmPlugin_object *self);

#endif /* PLUGIN_PY_GNUMERIC_H */
