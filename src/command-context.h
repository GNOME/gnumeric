#ifndef GNUMERIC_CMD_CONTEXT_H
#define GNUMERIC_CMD_CONTEXT_H

#include "gnumeric.h"

typedef enum {
	CMD_CONTEXT_GUI   = 0x11,
	CMD_CONTEXT_CORBA = 0x22
} CmdContextType;

struct _CmdContext
{
	CmdContextType type;
};

#endif /* GNUMERIC_CMD_CONTEXT_H */
