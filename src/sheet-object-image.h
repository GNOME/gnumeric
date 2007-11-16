/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_OBJECT_IMAGE_H_
# define _GNM_SHEET_OBJECT_IMAGE_H_

#include "sheet-object.h"

G_BEGIN_DECLS

#define SHEET_OBJECT_IMAGE_TYPE  (sheet_object_image_get_type ())
#define SHEET_OBJECT_IMAGE(o)	 (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_IMAGE_TYPE, SheetObjectImage))
#define IS_SHEET_OBJECT_IMAGE(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_IMAGE_TYPE))

typedef struct _SheetObjectImage SheetObjectImage;

GType	     sheet_object_image_get_type (void);
void sheet_object_image_set_image (SheetObjectImage *soi,
				   char const   *type,
				     guint8       *data,
				   unsigned	 data_len,
				     gboolean      copy_data);
void sheet_object_image_set_crop (SheetObjectImage *soi,
				  double crop_left,  double crop_top,
				  double crop_right, double crop_bottom);

G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_IMAGE_H_ */
