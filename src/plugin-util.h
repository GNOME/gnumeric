#ifndef GNUMERIC_PLUGIN_UTIL_H
#define GNUMERIC_PLUGIN_UTIL_H

#include <stdio.h>
#include "gnumeric.h"

FILE *gnumeric_fopen (IOContext *wbc, const char *path, const char *mode);
int   gnumeric_open  (IOContext *wbc, const char *pathname, int flags);

const unsigned char *gnumeric_mmap_open  (IOContext *wbc, const char *filename,
					  int *fdesc, int *file_size);

void                 gnumeric_mmap_close (IOContext *wbc, const unsigned char *data,
					  int fdesc, int file_size);

#endif /* GNUMERIC_PLUGIN_UTIL_H */

