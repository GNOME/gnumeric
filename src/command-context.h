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
};

typedef struct {
	GtkObjectClass parent_class;
	void (*error_plugin_problem) (CommandContext *context, char const * const app_ver);
	void (*error_splits_array)   (CommandContext *context);
} CommandContextClass;

GtkType   command_context_get_type (void);

/*
 * These routines should be part of the eventual worbook-view
 * structure.  They represent the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void gnumeric_error_plugin_problem (CommandContext *context, char const *const message);

void gnumeric_error_splits_array   (CommandContext *context);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */
