#ifndef GNUMERIC_SHEET_OBJECT_IMAGE_H
#define GNUMERIC_SHEET_OBJECT_IMAGE_H

#include "sheet-object.h"

#define SHEET_OBJECT_IMAGE_TYPE  (sheet_object_image_get_type ())

GType	     sheet_object_image_get_type (void);
SheetObject *sheet_object_image_new (char const   *type,
				     guint8       *data,
				     guint32	   data_len,
				     gboolean      copy_data);

#endif /* GNUMERIC_SHEET_OBJECT_IMAGE_H */
