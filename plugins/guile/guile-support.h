#include <libguile.h>
#include <gtk/gtk.h>

#include "cell.h"
#include "expr.h"
#include "gnumeric.h"
#include "value.h"

SCM scm_symbolfrom0str (char *name);

/*
  Deprecated funcs that will go away shortly
*/
SCM list_to_scm (GList *, CellRef);
SCM cell_ref_to_scm (CellRef cell, CellRef eval_cell);
CellRef scm_to_cell_ref (SCM scm);

SCM gnumeric_list2scm (GList *list, SCM (*data_to_scm)(void*));
GList *gnumeric_scm2list (SCM obj , void (*fromscm)(SCM, void*));
SCM value_to_scm (Value const *val, CellRef cell_ref);
Value* scm_to_value (SCM scm);
SCM expr_to_scm (ExprTree *expr, CellRef cell_ref);

