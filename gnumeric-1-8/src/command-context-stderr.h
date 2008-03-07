/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COMMAND_CONTEXT_STDERR_H_
# define _GNM_COMMAND_CONTEXT_STDERR_H_

#include "gnumeric.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define CMD_CONTEXT_STDERR_TYPE		(cmd_context_stderr_get_type ())
#define COMMAND_CONTEXT_STDERR(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_CONTEXT_STDERR_TYPE, CmdContextStderr))
#define IS_COMMAND_CONTEXT_STDERR(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), CMD_CONTEXT_STDERR_TYPE))

typedef struct _CmdContextStderr CmdContextStderr;

GType		cmd_context_stderr_get_type   (void);
GOCmdContext  *cmd_context_stderr_new	      (void);
void		cmd_context_stderr_set_status (CmdContextStderr *ccs, int status);
int		cmd_context_stderr_get_status (CmdContextStderr *ccs);

G_END_DECLS

#endif /* _GNM_COMMAND_CONTEXT_STDERR_H_ */
