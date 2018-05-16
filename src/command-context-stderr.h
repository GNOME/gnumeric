#ifndef _GNM_COMMAND_CONTEXT_STDERR_H_
# define _GNM_COMMAND_CONTEXT_STDERR_H_

#include <gnumeric.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_CMD_CONTEXT_STDERR_TYPE		(gnm_cmd_context_stderr_get_type ())
#define GNM_CMD_CONTEXT_STDERR(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_CMD_CONTEXT_STDERR_TYPE, GnmCmdContextStderr))
#define GNM_IS_CMD_CONTEXT_STDERR(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_CMD_CONTEXT_STDERR_TYPE))

typedef struct GnmCmdContextStderr_ GnmCmdContextStderr;

GType		gnm_cmd_context_stderr_get_type   (void);
GOCmdContext  *gnm_cmd_context_stderr_new	      (void);
void		gnm_cmd_context_stderr_set_status (GnmCmdContextStderr *ccs, int status);
int		gnm_cmd_context_stderr_get_status (GnmCmdContextStderr *ccs);

G_END_DECLS

#endif /* _GNM_COMMAND_CONTEXT_STDERR_H_ */
