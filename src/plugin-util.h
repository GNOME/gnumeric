#ifndef GNUMERIC_PLUGIN_UTIL_H
#define GNUMERIC_PLUGIN_UTIL_H

#include "gnumeric.h"
#include "error-info.h"

#include <stdio.h>

FILE *gnumeric_fopen_error_info (char const *file_name, char const *mode,
				 ErrorInfo **ret_error);
FILE *gnumeric_fopen		(IOContext *wbc, char const *path,
				 char const *mode);

gint gnumeric_open_error_info (char const *file_name, gint flags,
			       ErrorInfo **ret_error);
gint gnumeric_open	      (IOContext *wbc, char const *filename, gint flag);

guchar const *gnumeric_mmap_error_info  (char const *file_name, gint *file_size,
					 ErrorInfo **ret_error);
guchar const *gnumeric_mmap_open	(IOContext *wbc, const gchar *filename,
					 gint *fd, gint *file_size);
void	      gnumeric_mmap_close	(IOContext *wbc, const guchar *data,
					 gint fd, gint file_size);

#endif /* GNUMERIC_PLUGIN_UTIL_H */
