#ifndef _GNOME_STORAGE_OLE_H_
#define _GNOME_STORAGE_OLE_H_

#include <bonobo/gnome-storage.h>
#include "ms-ole.h"

BEGIN_GNOME_DECLS

#define GNOME_STORAGE_OLE_TYPE        (gnome_storage_ole_get_type ())
#define GNOME_STORAGE_OLE(o)          (GTK_CHECK_CAST ((o), GNOME_STORAGE_OLE_TYPE, GnomeStorageOLE))
#define GNOME_STORAGE_OLE_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GNOME_STORAGE_OLE_TYPE, GnomeStorageOLEClass))
#define GNOME_IS_STORAGE_OLE(o)       (GTK_CHECK_TYPE ((o), GNOME_STORAGE_OLE_TYPE))
#define GNOME_IS_STORAGE_OLE_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GNOME_STORAGE_OLE_TYPE))

typedef struct {
        GnomeStorage storage;
	MsOle *f;
} GnomeStorageOLE;

typedef struct {
	GnomeStorageClass parent_class;
} GnomeStorageOLEClass;

GtkType       gnome_storage_ole_get_type  (void);

GnomeStorage *gnome_storage_ole_open      (const gchar *path, gint flags, 
					   gint mode);


END_GNOME_DECLS

#endif /* _GNOME_STORAGE_OLE_H_ */
