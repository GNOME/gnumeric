#ifndef _GNOME_STREAM_OLE_H_
#define _GNOME_STREAM_OLE_H_

#include <bonobo/gnome-stream.h>
#include "gnome-storage-ole.h"

BEGIN_GNOME_DECLS

#define GNOME_STREAM_OLE_TYPE        (gnome_stream_ole_get_type ())
#define GNOME_STREAM_OLE(o)          (GTK_CHECK_CAST ((o), GNOME_STREAM_OLE_TYPE, GnomeStreamOLE))
#define GNOME_STREAM_OLE_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GNOME_STREAM_OLE_TYPE, GnomeStreamOLEClass))
#define GNOME_IS_STREAM_OLE(o)       (GTK_CHECK_TYPE ((o), GNOME_STREAM_OLE_TYPE))
#define GNOME_IS_STREAM_OLE_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GNOME_STREAM_OLE_TYPE))

typedef struct {
	GnomeStream stream;
	GnomeStorageOLE *storage;
	MsOleStream *file;
} GnomeStreamOLE;

typedef struct {
	GnomeStreamClass parent_class;
} GnomeStreamOLEClass;

GtkType         gnome_stream_ole_get_type     (void);
GnomeStream    *gnome_stream_ole_open         (GnomeStorageOLE *storage,
					       const CORBA_char *path, 
					       GNOME_Storage_OpenMode mode);
GnomeStream    *gnome_stream_ole_create       (GnomeStorageOLE *storage,
					       const CORBA_char *path);
	
END_GNOME_DECLS

#endif /* _GNOME_STREAM_OLE_H_ */
