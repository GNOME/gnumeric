#include <libguile.h>
#include <gtk/gtk.h>

#include "cell.h"
#include "expr.h"
#include "gnumeric.h"
#include "value.h"

SCM gnumeric_list2scm (GList *list, SCM (*data_to_scm)(void*));
GList *gnumeric_scm2list (SCM obj , void (*fromscm)(SCM, void*));
SCM value_to_scm (Value const *val, CellRef cell_ref);
Value* scm_to_value (SCM scm);
SCM expr_to_scm (ExprTree *expr, CellRef cell_ref);

