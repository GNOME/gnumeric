/*
 * command-context-gui.c: A Command Context for the User Interaface
 *
 * Authors:
 *   Jody Goldberg
 *   Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include "gnumeric-type-util.h"
#include "command-context-gui.h"
#include "gnumeric-util.h"

#define PARENT_TYPE command_context_get_type ()

#define CCG_CLASS(o) CMD_CONTEXT_GUI_CLASS (GTK_OBJECT (o)->klass)

static void
ccg_error_plugin_problem (CommandContext *context, char const * const message)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, message);
}

static void
ccg_error_read (CommandContext *context, char const * const message)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, message);
}

static void
ccg_error_save (CommandContext *context, char const * const message)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, message);
}

static void
ccg_error_splits_array (CommandContext *context)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);
	
	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR,
			 _("You cannot change part of an array."));
}

static void
ccg_init_class (GtkObjectClass *object_class)
{
	CommandContextClass *cc_class = (CommandContextClass *) object_class;

	cc_class->error_plugin_problem = ccg_error_plugin_problem;
	cc_class->error_read           = ccg_error_read;
	cc_class->error_save           = ccg_error_save;
	cc_class->error_splits_array   = ccg_error_splits_array;
}

GNUMERIC_MAKE_TYPE(command_context_gui, "CommandContextGui", CommandContextGui, ccg_init_class, NULL, PARENT_TYPE)

CommandContext *
command_context_gui_new (Workbook *wb)
{
	CommandContextGui *ccg;

	ccg = gtk_type_new (command_context_gui_get_type ());

	ccg->wb = wb;
	
	return COMMAND_CONTEXT (ccg);
}

