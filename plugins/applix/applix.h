#ifndef PLUGIN_APPLIX_H
# define PLUGIN_APPLIX_H

#include "gnumeric.h"
#include <stdio.h>

gboolean applix_read_header (FILE *file);
int      applix_read (IOContext *context, WorkbookView *wb_view,
		      FILE *file);

#endif /* PLUGIN_APPLIX_H */
