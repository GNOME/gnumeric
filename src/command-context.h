#ifndef GNUMERIC_CMD_CONTEXT_H
#define GNUMERIC_CMD_CONTEXT_H

#include "gnumeric.h"

/*
 * These routines should be part of the eventual worbook-view
 * structure.  They represent the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void gnumeric_error_plugin_problem (CmdContext *context,
				    char const * const message);

void gnumeric_error_splits_array (CmdContext *context);

#endif /* GNUMERIC_CMD_CONTEXT_H */
