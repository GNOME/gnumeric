#ifndef GNM_WORKBOOK_CONTROL_GTK_H
#define GNM_WORKBOOK_CONTROL_GTK_H

#include <glib-object.h>

#define WBC_GTK_TYPE	(wbc_gtk_get_type ())
#define WBC_GTK(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), WBC_GTK_TYPE, WBCgtk))
#define IS_WBC_GTK(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), WBC_GTK_TYPE))

typedef struct _WBCgtk WBCgtk;

GType wbc_gtk_get_type (void);

#endif /* GNM_WORKBOOK_CONTROL_GTK_H */
