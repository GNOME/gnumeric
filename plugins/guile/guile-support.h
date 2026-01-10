#ifndef GNM_PLUGIN_GUILE_GUILE_SUPPORT_H_
#define GNM_PLUGIN_GUILE_GUILE_SUPPORT_H_

#include <libguile.h>
#include <gnumeric.h>

SCM value_to_scm (GnmValue const *val, GnmCellRef cell_ref);
GnmValue* scm_to_value (SCM scm);

#endif
