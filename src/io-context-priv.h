#ifndef GNUMERIC_IO_CONTEXT_PRIV_H
#define GNUMERIC_IO_CONTEXT_PRIV_H

#include "io-context.h"
#include "workbook-control.h"
#include "error-info.h"

/* FIXME : This is a placeholder implementation */

struct _IOContext {
	WorkbookControl *impl;
	ErrorInfo *error_info;
};

#endif /* GNUMERIC_IO_CONTEXT_PRIV_H */
