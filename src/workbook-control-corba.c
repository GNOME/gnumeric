/*
 * command-context-corba.c: A Command Context for the CORBA interface
 *
 * Authors:
 *   Jody Goldberg
 *   Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include "gnumeric-type-util.h"
#include "command-context-corba.h"
#include "gnumeric-util.h"

#define PARENT_TYPE command_context_get_type ()

#define CCG_CLASS(o) CMD_CONTEXT_CORBA_CLASS (GTK_OBJECT (o)->klass)

static void
ccc_error_plugin_problem (CommandContext *context, char const * const app_ver)
{
	/* FIXME set exception */
}

static void
ccc_error_splits_array (CommandContext *context)
{
#if 0
	CommandContextCorba *ccc = COMMAND_CONTEXT_CORBA (context);

	/* FIXME set exception */
#endif
}

static void
ccc_init_class (GtkObjectClass *object_class)
{
	CommandContextClass *cc_class = (CommandContextClass *) object_class;

	cc_class->error_plugin_problem = ccc_error_plugin_problem;
	cc_class->error_splits_array   = ccc_error_splits_array;
}

GNUMERIC_MAKE_TYPE(command_context_corba, "CommandContextCorba", CommandContextCorba, ccc_init_class, NULL, PARENT_TYPE)

CommandContext *
command_context_corba_new (Workbook *wb)
{
	CommandContextCorba *ccg;

	ccg = gtk_type_new (command_context_corba_get_type ());

	ccg->wb = wb;
	
	return COMMAND_CONTEXT (ccg);
}

