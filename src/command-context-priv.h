#ifndef GNUMERIC_COMMAND_CONTEXT_PRIV_H
#define GNUMERIC_COMMAND_CONTEXT_PRIV_H

#include "command-context.h"
#include <gtk/gtkobject.h>

struct _CommandContext {
	GtkObject gtk_object;
	GSList *template_list;
};

typedef struct {
	GtkObjectClass gtk_object_class;

	void (*progress_set)	(CommandContext *context, gfloat val);
	struct {
		void (*system)	(CommandContext *context, char const *msg);
		void (*plugin)	(CommandContext *context, char const *msg);
		void (*read)	(CommandContext *context, char const *msg);
		void (*save)	(CommandContext *context, char const *msg);
		void (*invalid)	(CommandContext *context,
				 char const *msg, char const *val);
	} error;
} CommandContextClass;

#define COMMAND_CONTEXT_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), COMMAND_CONTEXT_TYPE, CommandContextClass))
#define IS_COMMAND_CONTEXT_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), COMMAND_CONTEXT_TYPE))

#endif /* GNUMERIC_COMMAND_CONTEXT_PRIV_H */
