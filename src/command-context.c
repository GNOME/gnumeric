/*
 * command-context.c : Error dispatch utilities.
 *
 * Author:
 * 	Jody Goldberg <jody@gnome.org>
 *
 * (C) 1999-2001 Jody Goldberg
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "command-context-priv.h"

#include <gsf/gsf-impl-utils.h>
#include <libgnome/gnome-i18n.h>

#define CC_CLASS(o) COMMAND_CONTEXT_CLASS (G_OBJECT_GET_CLASS (o))

/**
 * command_context_format_message:
 *
 * Format a message using the template on the stack (if any).
 * The caller must free the returned message.
 * FIXME: Make it accept varargs.
 */
static char *
format_message (CommandContext *context, char const *message)
{
	char const * const msg = message ? message : "";
	return g_strdup (msg);
}

void
gnumeric_error_system (CommandContext *context, char const *message)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error.system (context, message);
}

void
gnumeric_error_read (CommandContext *context, char const *message)
{
	char *fmessage;

	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	fmessage = format_message (context, message);
	CC_CLASS (context)->error.read (context, fmessage);
	g_free (fmessage);
}

void
gnumeric_error_save (CommandContext *context, char const *message)
{
	char *fmessage;

	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	fmessage = format_message (context, message);
	CC_CLASS (context)->error.save (context, fmessage);
	g_free (fmessage);
}

void
gnumeric_error_plugin (CommandContext *context,
		       char const *message)
{
	char *fmessage;

	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	fmessage = format_message (context, message);
	CC_CLASS (context)->error.plugin (context, message);
	g_free (fmessage);
}

void
gnumeric_error_invalid (CommandContext *context, char const *message, char const *val)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error.invalid (context, message, val);
}

void
gnumeric_error_splits_array (CommandContext *context,
			     char const *cmd, Range const *array)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error.splits_array (context, cmd, array);
}

void
gnumeric_error_error_info (CommandContext *context, ErrorInfo *error)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error.error_info (context, error);
}


void
gnumeric_progress_set (CommandContext *context, gfloat f)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->progress_set (context, f);
}

void
gnumeric_progress_message_set (CommandContext *context, gchar const *msg)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->progress_message_set (context, msg);
}

char *
cmd_context_get_password (CommandContext *cc, char const *msg)
{
	g_return_val_if_fail (IS_COMMAND_CONTEXT (cc), NULL);

	return CC_CLASS (cc)->get_password (cc, msg);
}

void
cmd_context_set_sensitive (CommandContext *cc, gboolean sensitive)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (cc));

	CC_CLASS (cc)->set_sensitive (cc, sensitive);
}

GSF_CLASS (CommandContext, command_context,
	   NULL, NULL, G_TYPE_OBJECT)
