#ifndef GNUMERIC_COMMAND_CONTEXT_H
#define GNUMERIC_COMMAND_CONTEXT_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>
#include "command-context.h"

#define COMMAND_CONTEXT_TYPE        (command_context_get_type ())
#define COMMAND_CONTEXT(o)          (GTK_CHECK_CAST ((o), COMMAND_CONTEXT_TYPE, CommandContext))
#define COMMAND_CONTEXT_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), COMMAND_CONTEXT_TYPE, CommandContextClass))
#define IS_COMMAND_CONTEXT(o)       (GTK_CHECK_TYPE ((o), COMMAND_CONTEXT_TYPE))
#define IS_COMMAND_CONTEXT_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), COMMAND_CONTEXT_TYPE))

struct _CommandContext {
	GtkObject parent;
	GSList *template_list;
};

typedef struct {
	GtkObjectClass parent_class;
	void (*error_sys_err)        (CommandContext *context,
				      char const * message);
	void (*error_plugin_problem) (CommandContext *context,
				      char const * message);
	void (*error_read)           (CommandContext *context,
				      char const * message);
	void (*error_save)           (CommandContext *context,
				      char const * message);
	void (*error_splits_array)   (CommandContext *context);
	void (*error_invalid)        (CommandContext *context,
				      char const * message,
				      char const * val);
} CommandContextClass;

GtkType   command_context_get_type (void);

/* Push a printf template to the list. The template is used to provide
 * context for error messages. E.g.: "Could not read file: %s". */
void
command_context_push_template (CommandContext *context, const char *template);

void
command_context_pop_template (CommandContext *context);

/*
 * These routines should be part of the eventual worbook-view
 * structure.  They represent the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void gnumeric_error_sys_err        (CommandContext *context, char const *message);
void gnumeric_error_plugin_problem (CommandContext *context, char const *message);
void gnumeric_error_read           (CommandContext *context, char const *message);
void gnumeric_error_save           (CommandContext *context, char const *message);
void gnumeric_error_invalid        (CommandContext *context, char const *message, char const *val);
void gnumeric_error_splits_array   (CommandContext *context);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */
