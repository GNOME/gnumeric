#ifndef __GNUMERIC_WBC_GTK_H__
#define __GNUMERIC_WBC_GTK_H__

#include <glib-object.h>

#define WBC_GTK_TYPE	(wbc_gtk_get_type ())
#define WBC_GTK(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), WBC_GTK_TYPE, WBCgtk))
#define IS_WBC_GTK(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), WBC_GTK_TYPE))

typedef struct _WBCgtk WBCgtk;

GType wbc_gtk_get_type (void);

#endif /* __GNUMERIC_WBC_GTK_H__ */
