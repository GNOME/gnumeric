/*
 * command-context-corba.c: A Command Context for the CORBA interface
 *
 * Authors:
 *   Jody Goldberg
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "workbook-control-corba.h"

#include "workbook-control-priv.h"
#include <gal/util/e-util.h>

#define CCG_CLASS(o) CMD_CONTEXT_CORBA_CLASS (G_OBJECT_GET_CLASS (o))

typedef struct {
	WorkbookControl parent;
} WorkbookControlCorba;

typedef struct {
	WorkbookControlClass parent_class;
} WorkbookControlCorbaClass;

static void
wbcc_error_system (CommandContext *context, char const * message)
{
	/* FIXME set exception */
}

static void
wbcc_error_plugin (CommandContext *context, char const * message)
{
	/* FIXME set exception */
}

static void
wbcc_error_read (CommandContext *context, char const * message)
{
	/* FIXME set exception */
}

static void
wbcc_error_save (CommandContext *context, char const * message)
{
	/* FIXME set exception */
}

static void
wbcc_error_invalid (CommandContext *context, char const * message, char const *value)
{
	/* FIXME set exception */
}

static void
wbcc_init_class (GtkObjectClass *object_class)
{
	CommandContextClass *cc_class = (CommandContextClass *) object_class;

	cc_class->error.system	= &wbcc_error_system;
	cc_class->error.plugin	= &wbcc_error_plugin;
	cc_class->error.read	= &wbcc_error_read;
	cc_class->error.save	= &wbcc_error_save;
	cc_class->error.invalid	= &wbcc_error_invalid;
}

static E_MAKE_TYPE (workbook_control_corba, "WorkbookControlCorba", WorkbookControlCorba,
		    wbcc_init_class, NULL, WORKBOOK_CONTROL_TYPE);

CommandContext *
command_context_corba_new (void)
{
	WorkbookControlCorba *wbcc;

	wbcc = g_object_new (workbook_control_corba_get_type (), NULL);

	return COMMAND_CONTEXT (wbcc);
}
