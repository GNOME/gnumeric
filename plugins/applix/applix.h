#ifndef PLUGIN_APPLIX_H
# define PLUGIN_APPLIX_H

#include "gnumeric.h"
#include <stdio.h>

gboolean applix_read_header (FILE *file);
int      applix_read (CommandContext *context, Workbook *wb, FILE *file);

#endif /* PLUGIN_APPLIX_H */
