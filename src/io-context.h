#ifndef GNUMERIC_IO_CONTEXT_H
#define GNUMERIC_IO_CONTEXT_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

#define IO_CONTEXT_TYPE  (io_context_get_type ())
#define IO_CONTEXT(o)    (GTK_CHECK_CAST ((o), IO_CONTEXT_TYPE, IOContext))
#define IS_IO_CONTEXT(o) (GTK_CHECK_TYPE ((o), IO_CONTEXT_TYPE))

GtkType   io_context_get_type (void);

/*
 * These are the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void gnumeric_io_error_system		(IOContext *context, char const *msg);
void gnumeric_io_error_read		(IOContext *context, char const *msg);
void gnumeric_io_error_save		(IOContext *context, char const *msg);

void gnumeric_warning_unknown_font	(IOContext *context, char const *msg);
void gnumeric_warning_unknown_feature	(IOContext *context, char const *msg);
void gnumeric_warning_unknown_function	(IOContext *context, char const *msg);

#endif /* GNUMERIC_IO_CONTEXT_H */
