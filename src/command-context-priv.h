#ifndef GNUMERIC_COMMAND_CONTEXT_PRIV_H
#define GNUMERIC_COMMAND_CONTEXT_PRIV_H

#include "command-context.h"

struct _CommandContext {
	GObject g_object;
};

typedef struct {
	GObjectClass g_object_class;

	char *  (*get_password)		(CommandContext *cc, char const *msg);
	void    (*set_sensitive)	(CommandContext *cc, gboolean sensitive);
	void    (*progress_set)		(CommandContext *cc, gfloat val);
	void    (*progress_message_set)	(CommandContext *cc, gchar const *msg);
	struct {
		void (*error)		(CommandContext *cc, GError *err);
		void (*error_info)  (CommandContext *ctxt, ErrorInfo *error);
	} error;
} CommandContextClass;

#define COMMAND_CONTEXT_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), COMMAND_CONTEXT_TYPE, CommandContextClass))
#define IS_COMMAND_CONTEXT_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), COMMAND_CONTEXT_TYPE))

/* protected, these do not really belong here, they are associated with io-context */
void  cmd_context_progress_set	    (CommandContext *cc, gfloat f);
void  cmd_context_progress_message_set (CommandContext *cc, char const *msg);

#endif /* GNUMERIC_COMMAND_CONTEXT_PRIV_H */
