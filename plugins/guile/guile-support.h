#include <libguile.h>
#include <gtk/gtk.h>

#include "cell.h"
#include "expr.h"
#include "gnumeric.h"
#include "value.h"

SCM value_to_scm (Value const *val, CellRef cell_ref);
Value* scm_to_value (SCM scm);


