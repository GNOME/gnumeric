#ifndef GNUMERIC_COMMAND_CONTEXT_H
#define GNUMERIC_COMMAND_CONTEXT_H

#include "gnumeric.h"
#include <glib-object.h>

#define COMMAND_CONTEXT_TYPE        (command_context_get_type ())
#define COMMAND_CONTEXT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), COMMAND_CONTEXT_TYPE, CommandContext))
#define IS_COMMAND_CONTEXT(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), COMMAND_CONTEXT_TYPE))

GType command_context_get_type (void);

void  cmd_context_error		(CommandContext *cc, GError *err);
char *cmd_context_get_password	(CommandContext *cc, char const *msg);
void  cmd_context_set_sensitive	(CommandContext *cc, gboolean sensitive);

/* utility routines for common errors */
void  gnumeric_error_system	(CommandContext *cc, char const *msg);
void  gnumeric_error_read	(CommandContext *cc, char const *msg);
void  gnumeric_error_save	(CommandContext *cc, char const *msg);
void  gnumeric_error_invalid	(CommandContext *cc,
				 char const *msg, char const *val);
void  gnumeric_error_error_info	(CommandContext *cc, ErrorInfo *error);

/* some gnumeric specific utility routines */
void  gnumeric_error_splits_array   (CommandContext *cc, char const *cmd,
				     Range const *array);

/* An initial set of std errors */
GQuark gnm_error_system  (void);
GQuark gnm_error_read    (void);
GQuark gnm_error_write   (void);
GQuark gnm_error_array   (void);
GQuark gnm_error_invalid (void);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */
