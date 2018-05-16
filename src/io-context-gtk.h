#ifndef _GNM_IO_CONTEXT_GTK_H_
# define _GNM_IO_CONTEXT_GTK_H_

#include <stdarg.h>
#include <gnumeric-fwd.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

typedef struct GnmIOContextGtk_ GnmIOContextGtk;
typedef struct GnmIOContextGtkClass_ GnmIOContextGtkClass;

#define GNM_TYPE_IO_CONTEXT_GTK    (gnm_io_context_gtk_get_type ())
#define GNM_IO_CONTEXT_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_TYPE_IO_CONTEXT_GTK, GnmIOContextGtk))
#define GNM_IS_IO_CONTEXT_GTK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_TYPE_IO_CONTEXT_GTK))

GType gnm_io_context_gtk_get_type (void);
void  gnm_io_context_gtk_set_transient_for (GnmIOContextGtk *icg, GtkWindow *parent_window);
gboolean gnm_io_context_gtk_get_interrupted (GnmIOContextGtk *icg);

void gnm_io_context_gtk_discharge_splash (GnmIOContextGtk *icg);

G_END_DECLS

#endif /* _GNM_IO_CONTEXT_GTK_H_ */
