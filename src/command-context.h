#ifndef GNM_CMD_CONTEXT_H
#define GNM_CMD_CONTEXT_H

#include <gnumeric.h>
#include <goffice/app/go-cmd-context.h>

/* some gnumeric specific utility routines */
GQuark go_error_array (void);
GQuark go_error_calc  (void);

void  go_cmd_context_error_calc		(GOCmdContext *cc, char const *msg);
void  go_cmd_context_error_splits_array (GOCmdContext *cc, char const *cmd,
					 GnmRange const *array);

#endif /* GNM_CMD_CONTEXT_H */
