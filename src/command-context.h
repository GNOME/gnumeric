#ifndef GNUMERIC_COMMAND_CONTEXT_H
#define GNUMERIC_COMMAND_CONTEXT_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

#define COMMAND_CONTEXT_TYPE        (command_context_get_type ())
#define COMMAND_CONTEXT(o)          (GTK_CHECK_CAST ((o), COMMAND_CONTEXT_TYPE, CommandContext))
#define IS_COMMAND_CONTEXT(o)       (GTK_CHECK_TYPE ((o), COMMAND_CONTEXT_TYPE))

GtkType   command_context_get_type (void);

/*
 * These are the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void gnumeric_error_system	 (CommandContext *context, char const *msg);
void gnumeric_error_read	 (CommandContext *context, char const *msg);
void gnumeric_error_save	 (CommandContext *context, char const *msg);
void gnumeric_error_plugin	 (CommandContext *context, char const *msg);
void gnumeric_error_invalid	 (CommandContext *context,
				  char const *msg, char const *val);
void gnumeric_error_splits_array (CommandContext *context,
				  char const *cmd, Range const *array);
void gnumeric_progress_set	 (CommandContext *context, gfloat f);

/* Push a printf template to the list. The template is used to provide
 * context for error messages. E.g.: "Could not read file: %s". */
void command_context_pop_err_template (CommandContext *context);
void command_context_push_err_template (CommandContext *context,
					const char *template_str);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */
