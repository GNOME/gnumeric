#ifndef GNUMERIC_COMMAND_CONTEXT_CORBA_H
#define GNUMERIC_COMMAND_CONTEXT_CORBA_H

#include "gnumeric.h"
#include "command-context.h"

#define COMMAND_CONTEXT_CORBA_TYPE        (command_context_corba_get_type ())
#define COMMAND_CONTEXT_CORBA(o)          (GTK_CHECK_CAST ((o), COMMAND_CONTEXT_CORBA_TYPE, CommandContextCorba))
#define COMMAND_CONTEXT_CORBA_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), COMMAND_CONTEXT_CORBA_TYPE, CommandContextCorbaClass))
#define IS_COMMAND_CONTEXT_CORBA(o)       (GTK_CHECK_TYPE ((o), COMMAND_CONTEXT_CORBA_TYPE))
#define IS_COMMAND_CONTEXT_CORBA_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), COMMAND_CONTEXT_CORBA_TYPE))

GtkType         command_context_corba_get_type (void);
CommandContext *command_context_corba_new      (void);

#endif /* GNUMERIC_COMMAND_CONTEXT_CORBA_H */

