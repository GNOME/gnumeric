/*
 * command-context.c : Error dispatch utilities.
 *
 * Author:
 * 	Jody Goldberg <jgoldberg@home.com>
 *
 * (C) 1999, 2000 Jody Goldberg
 */
#include <config.h>
#include "gnumeric-type-util.h"
#include "command-context.h"

#define PARENT_TYPE gtk_object_get_type ()

#define CC_CLASS(o) COMMAND_CONTEXT_CLASS (GTK_OBJECT (o)->klass)

void
gnumeric_error_plugin_problem (CommandContext *context,
			       char const * const message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error_plugin_problem (context, message);
}

void
gnumeric_error_read (CommandContext *context,
		     char const * const message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error_read (context, message);
}

void
gnumeric_error_save (CommandContext *context,
		     char const * const message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error_save (context, message);
}

void
gnumeric_error_splits_array (CommandContext *context)
{
	g_return_if_fail (context);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error_splits_array (context);
}

GNUMERIC_MAKE_TYPE(command_context, "CommandContext", CommandContext, NULL, NULL, PARENT_TYPE);

