#ifndef GNUMERIC_IO_CONTEXT_GTK_H
#define GNUMERIC_IO_CONTEXT_GTK_H

#include "gui-gnumeric.h"
#include <io-context.h>
#include <stdarg.h>

typedef struct _IOContextGtk IOContextGtk;
typedef struct _IOContextGtkClass IOContextGtkClass;

#define TYPE_IO_CONTEXT_GTK    (io_context_gtk_get_type ())
#define IO_CONTEXT_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_IO_CONTEXT_GTK, IOContextGtk))
#define IS_IO_CONTEXT_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_IO_CONTEXT_GTK))

GType         io_context_gtk_get_type (void);
IOContextGtk *gnumeric_io_context_gtk_new (void);
void          icg_set_files_total (IOContextGtk *icg, guint files_total);
void          icg_inc_files_done (IOContextGtk *icg);

#endif /* GNUMERIC_IO_CONTEXT_GTK_H */
