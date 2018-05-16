#ifndef _GNM_COMMAND_CONTEXT_H_
# define _GNM_COMMAND_CONTEXT_H_

#include <gnumeric.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

/* some gnumeric specific utility routines */
void  gnm_cmd_context_error_calc	 (GOCmdContext *cc, char const *msg);
void  gnm_cmd_context_error_splits_array (GOCmdContext *cc, char const *cmd,
					  GnmRange const *array);
void  gnm_cmd_context_error_splits_merge (GOCmdContext *cc,
					  GnmRange const *merge);

GQuark gnm_error_array (void);
GQuark gnm_error_calc  (void);

G_END_DECLS

#endif /* _GNM_COMMAND_CONTEXT_H_ */
