#ifndef GNUMERIC_COMMAND_CONTEXT_PRIV_H
#define GNUMERIC_COMMAND_CONTEXT_PRIV_H

#include "command-context.h"

struct _CommandContext {
	GObject g_object;
	GSList *template_list;
};

typedef struct {
	GObjectClass g_object_class;

	void (*progress_set)         (CommandContext *context, gfloat val);
	void (*progress_message_set) (CommandContext *context, gchar const *msg);
	struct {
		void (*system)		(CommandContext *ctxt, char const *msg);
		void (*plugin)		(CommandContext *ctxt, char const *msg);
		void (*read)		(CommandContext *ctxt, char const *msg);
		void (*save)		(CommandContext *ctxt, char const *msg);
		void (*splits_array)	(CommandContext *ctxt,
					 char const *cmd, Range const *array);
		void (*invalid)		(CommandContext *ctxt,
					 char const *msg, char const *val);
		void (*error_info)  (CommandContext *ctxt, ErrorInfo *error);
	} error;
} CommandContextClass;

#define COMMAND_CONTEXT_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), COMMAND_CONTEXT_TYPE, CommandContextClass))
#define IS_COMMAND_CONTEXT_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), COMMAND_CONTEXT_TYPE))

#endif /* GNUMERIC_COMMAND_CONTEXT_PRIV_H */
