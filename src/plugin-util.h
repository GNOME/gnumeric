#ifndef GNUMERIC_PLUGIN_UTIL_H
#define GNUMERIC_PLUGIN_UTIL_H

#include <gnome.h>

#include "command-context.h"

FILE                *gnumeric_fopen      (CommandContext *context, const char *path, const char *mode);

int                  gnumeric_open       (CommandContext *context, const char *pathname, int flags);


const unsigned char *gnumeric_mmap_open  (CommandContext *context, const char *filename,
					  int *fdesc, int *file_size);

void                 gnumeric_mmap_close (CommandContext *context, const unsigned char *data,
					  int fdesc, int file_size);

#endif /* GNUMERIC_PLUGIN_UTIL_H */

