#ifndef GNUMERIC_PLUGIN_GUILE_GUILE_SUPPORT_H
#define GNUMERIC_PLUGIN_GUILE_GUILE_SUPPORT_H

#include <libguile.h>
#include "gnumeric.h"

SCM value_to_scm (Value const *val, CellRef cell_ref);
Value* scm_to_value (SCM scm);

#endif
