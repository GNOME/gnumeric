#ifndef GNUMERIC_PLUGIN_UTIL_H
#define GNUMERIC_PLUGIN_UTIL_H

#include "gnumeric.h"
#include "error-info.h"

#include <stdio.h>

FILE *gnumeric_fopen (IOContext *wbc, const gchar *path, const gchar *mode);
gint  gnumeric_open  (IOContext *wbc, const gchar *pathname, gint flags);

const guchar        *gnumeric_mmap_open  (IOContext *wbc, const gchar *filename,
                                          gint *fdesc, gint *file_size);

void                 gnumeric_mmap_close (IOContext *wbc, const guchar *data,
                                          gint fdesc, gint file_size);

FILE                *gnumeric_fopen_error_info (const gchar *file_name, const gchar *mode,
                                                ErrorInfo **ret_error);
gint                 gnumeric_open_error_info (const gchar *file_name, gint flags,
                                               ErrorInfo **ret_error);
guchar              *gnumeric_mmap_error_info (const gchar *file_name, gint *file_size,
                                               ErrorInfo **ret_error);

#endif /* GNUMERIC_PLUGIN_UTIL_H */
