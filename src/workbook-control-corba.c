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
ccc_error_plugin (WorkbookControl *context, char const * message)
{
	/* FIXME set exception */
}

static void
ccc_error_read (WorkbookControl *context, char const * message)
{
	/* FIXME set exception */
}

static void
ccc_error_save (WorkbookControl *context, char const * message)
{
	/* FIXME set exception */
}

static void
ccc_error_sys_err (WorkbookControl *context, char const * message)
{
	/* FIXME set exception */
}

static void
ccc_error_invalid (WorkbookControl *context, char const * message, char const *value)
{
	/* FIXME set exception */
}

static void
ccc_set_progress (WorkbookControl *context, gfloat f)
{
    /* Ignore */
}

static void
ccc_init_class (GtkObjectClass *object_class)
{
	WorkbookControlClass *cc_class = (WorkbookControlClass *) object_class;

	cc_class->error_plugin		= &ccc_error_plugin;
	cc_class->error_read		= &ccc_error_read;
	cc_class->error_save		= &ccc_error_save;
	cc_class->error_sys_err		= &ccc_error_sys_err;
	cc_class->error_invalid		= &ccc_error_invalid;
	cc_class->set_progress		= &ccc_set_progress;
}

GNUMERIC_MAKE_TYPE(command_context_corba, "WorkbookControlCorba", WorkbookControlCorba, ccc_init_class, NULL, PARENT_TYPE)

WorkbookControl *
command_context_corba_new (void)
{
	WorkbookControlCorba *ccg;

	ccg = gtk_type_new (command_context_corba_get_type ());
	
	return COMMAND_CONTEXT (ccg);
}
