#ifndef GNUMERIC_CMD_CONTEXT_IMPL_H
#define GNUMERIC_CMD_CONTEXT_IMPL_H

#include "gnumeric.h"

typedef enum {
	CMD_CONTEXT_GUI   = 0x11,
	CMD_CONTEXT_CORBA = 0x22
} CmdContextType;

typedef struct
{
	void (*plugin_problem) (CmdContext *context,
				char const * const app_ver);
	void (*splits_array) (CmdContext *context);
} GnmCmdcontext_vtbl;

struct _CmdContext
{
	CmdContextType type;
	GnmCmdcontext_vtbl const * vtbl;
};

void command_context_vtbl_init (GnmCmdcontext_vtbl *vtbl);

#endif /* GNUMERIC_CMD_CONTEXT_IMPL_H */
