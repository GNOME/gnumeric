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
#include <gnome.h> /* Ick.  This is required to get _("") */

#define PARENT_TYPE gtk_object_get_type ()

#define CC_CLASS(o) COMMAND_CONTEXT_CLASS (GTK_OBJECT (o)->klass)

/**
 * command_context_push_template
 * @template: printf template to display message
 *
 * Push a printf template to the stack. The template is used to provide
 * context for error messages. E.g.: "Could not read file: %s"
 */
void
command_context_push_template (CommandContext *context, const char *template)
{
	context->template_list = g_slist_prepend(context->template_list,
						 g_strdup(template));
}

/**
 * command_context_pop_template:
 *
 * Call this routine to remove the current template from the stack.
 */
void
command_context_pop_template (CommandContext *context)
{
	if(context->template_list) {
		GSList *tlist = context->template_list;
		g_free(context->template_list->data);
		context->template_list = context->template_list->next;
		g_slist_free_1(tlist);
	}
}


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
	GSList *tlist = context->template_list;
	char const * const msg = message ? message : "";

	if (tlist)
		return g_strdup_printf ((char *) (tlist->data), msg);
	else
		return g_strdup (msg);
}

void
gnumeric_error_plugin (CommandContext *context,
		       char const *message)
{
	char *fmessage;
	
	g_return_if_fail (context != NULL);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	fmessage = format_message (context, message);
	CC_CLASS (context)->error_plugin (context, message);
	g_free (fmessage);
}

void
gnumeric_error_read (CommandContext *context,
		     char const *message)
{
	char *fmessage;

	g_return_if_fail (context != NULL);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	fmessage = format_message (context, message);
	CC_CLASS (context)->error_read (context, fmessage);
	g_free (fmessage);
}

void
gnumeric_error_save (CommandContext *context,
		     char const *message)
{
	char *fmessage;

	g_return_if_fail (context != NULL);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	fmessage = format_message (context, message);
	CC_CLASS (context)->error_save (context, fmessage);
	g_free (fmessage);
}

void
gnumeric_error_splits_array (CommandContext *context, char const *cmd)
{
	g_return_if_fail (context);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	gnumeric_error_invalid	(context, cmd, _("Would split an array."));
}

void
gnumeric_error_sys_err (CommandContext *context, char const *message)
{
	g_return_if_fail (context);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error_sys_err (context, message);
}

void
gnumeric_error_invalid (CommandContext *context, char const *message, char const *val)
{
	g_return_if_fail (context);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->error_invalid (context, message, val);
}

void
gnumeric_set_progress (CommandContext *context, gfloat f)
{
	g_return_if_fail (context);
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->set_progress (context, f);
}

GNUMERIC_MAKE_TYPE(command_context, "CommandContext", CommandContext, NULL, NULL, PARENT_TYPE);

