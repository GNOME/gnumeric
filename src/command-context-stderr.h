#ifndef GNUMERIC_COMMAND_CONTEXT_STDERR_H
#define GNUMERIC_COMMAND_CONTEXT_STDERR_H

#include "gnumeric.h"
#include <glib-object.h>

#define COMMAND_CONTEXT_STDERR_TYPE     (command_context_stderr_get_type ())
#define COMMAND_CONTEXT_STDERR(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), COMMAND_CONTEXT_STDERR_TYPE, CommandContextStderr))
#define IS_COMMAND_CONTEXT_STDERR(o)	  (G_TYPE_CHECK_INSTANCE_TYPE ((o), COMMAND_CONTEXT_STDERR_TYPE))

GType command_context_stderr_get_type    (void);
CommandContextStderr *command_context_stderr_new (void);
void command_context_stderr_set_status (CommandContextStderr *ccs, int status);
int  command_context_stderr_get_status (CommandContextStderr *ccs);

#endif /* GNUMERIC_COMMAND_CONTEXT_STDERR_H */
