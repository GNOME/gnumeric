/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_IO_CONTEXT_GTK_H_
# define _GNM_IO_CONTEXT_GTK_H_

#include <stdarg.h>
#include "gui-gnumeric.h"
#include <gtk/gtkwindow.h>
#include <goffice/app/io-context.h>

G_BEGIN_DECLS

typedef struct _IOContextGtk IOContextGtk;
typedef struct _IOContextGtkClass IOContextGtkClass;

#define TYPE_IO_CONTEXT_GTK    (io_context_gtk_get_type ())
#define IO_CONTEXT_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_IO_CONTEXT_GTK, IOContextGtk))
#define IS_IO_CONTEXT_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_IO_CONTEXT_GTK))

GType io_context_gtk_get_type (void);
void  icg_set_transient_for (IOContextGtk *icg, GtkWindow *parent_window);
gboolean icg_get_interrupted (IOContextGtk *icg);

G_END_DECLS

#endif /* _GNM_IO_CONTEXT_GTK_H_ */
