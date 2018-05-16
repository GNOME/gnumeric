#ifndef PY_COMMAND_LINE
#define PY_COMMAND_LINE

#include <glib.h>
#include <gnumeric.h>
#include "gnm-py-interpreter.h"

#define GNM_PY_COMMAND_LINE_TYPE     (gnm_py_command_line_get_type ())
#define GNM_PY_COMMAND_LINE(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PY_COMMAND_LINE_TYPE, GnmPyCommandLine))
#define GNM_IS_PY_COMMAND_LINE(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PY_COMMAND_LINE_TYPE))

GType gnm_py_command_line_get_type (void);
typedef struct _GnmPyCommandLine GnmPyCommandLine;

GtkWidget         *gnm_py_command_line_new (void);

#endif /* PY_COMMAND_LINE */
