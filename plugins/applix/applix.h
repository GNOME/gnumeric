#ifndef PLUGIN_APPLIX_H
#define PLUGIN_APPLIX_H

#include "gnumeric.h"
#include "io-context.h"
#include <stdio.h>

gboolean applix_read_header (FILE *file);
void     applix_read  (IOContext *io_context, WorkbookView *wb_view, FILE *file);
void     applix_write (IOContext *io_context, WorkbookView *wb_view, FILE *file);

#endif /* PLUGIN_APPLIX_H */
