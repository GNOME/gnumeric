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
#include "workbook-private.h"
#include "workbook.h"

#define PARENT_TYPE command_context_get_type ()

#define CCG_CLASS(o) CMD_CONTEXT_GUI_CLASS (GTK_OBJECT (o)->klass)

static void
ccg_error_plugin (CommandContext *context, char const * message)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, message);
}

static void
ccg_error_read (CommandContext *context, char const * message)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, message);
}

static void
ccg_error_save (CommandContext *context, char const * message)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, message);
}

static void
ccg_error_sys_err (CommandContext *context, char const * message)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, message);
}

static void
ccg_error_invalid (CommandContext *context, char const * message, char const * value)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);

	char *buf = g_strconcat (message, " : ", value, NULL);
	gnumeric_notice (ccg->wb, GNOME_MESSAGE_BOX_ERROR, buf);
	g_free (buf);
}

static void
ccg_set_progress (CommandContext *context, gfloat f)
{
	CommandContextGui *ccg = COMMAND_CONTEXT_GUI (context);
#ifdef ENABLE_BONOBO
	gtk_progress_bar_update (
		GTK_PROGRESS_BAR (ccg->wb->priv->progress_bar), f);
#else
	gnome_appbar_set_progress (ccg->wb->priv->appbar, f);
#endif
}

static void
ccg_init_class (GtkObjectClass *object_class)
{
	CommandContextClass *cc_class = (CommandContextClass *) object_class;

	cc_class->error_plugin		= &ccg_error_plugin;
	cc_class->error_read		= &ccg_error_read;
	cc_class->error_save		= &ccg_error_save;
	cc_class->error_sys_err		= &ccg_error_sys_err;
	cc_class->error_invalid		= &ccg_error_invalid;
	cc_class->set_progress		= &ccg_set_progress;
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

