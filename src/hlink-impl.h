#ifndef GNUMERIC_HLINK_IMPL_H
#define GNUMERIC_HLINK_IMPL_H

#include "hlink.h"

#define GNM_HLINK_TYPE		(command_context_get_type ())
#define GNM_HLINK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_HLINK_TYPE, GnmHLink))
#define GNM_IS_HLINK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_HLINK_TYPE))

GType command_context_get_type (void);

/*
 * These are the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void  gnumeric_error_system	    (CommandContext *cc, char const *msg);
void  gnumeric_error_read	    (CommandContext *cc, char const *msg);
void  gnumeric_error_save	    (CommandContext *cc, char const *msg);
void  gnumeric_error_plugin	    (CommandContext *cc, char const *msg);
void  gnumeric_error_invalid	    (CommandContext *cc, char const *msg,
				     char const *val);
void  gnumeric_error_splits_array   (CommandContext *cc, char const *cmd,
				     Range const *array);
void  gnumeric_error_error_info	    (CommandContext *cc, ErrorInfo *error);
void  gnumeric_progress_set	    (CommandContext *cc, gfloat f);
void  gnumeric_progress_message_set (CommandContext *cc, char const *msg);
char *cmd_context_get_password	    (CommandContext *cc, char const *msg);

/* Push a printf template to the list. The template is used to provide
 * context for error messages. E.g.: "Could not read file: %s". */
void command_context_pop_err_template  (CommandContext *context);
void command_context_push_err_template (CommandContext *context,
					char const *template_str);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */
