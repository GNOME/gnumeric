#ifndef GNUMERIC_SHEET_OBJECT_ITEM_H
#define GNUMERIC_SHEET_OBJECT_ITEM_H

#include "sheet-object-bonobo.h"
#include <bonobo/bonobo-client-site.h>

/*
 * SheetObjectItem:
 *
 * SheetObjec for Canvas-based Bonobo items.
 */
#define SHEET_OBJECT_ITEM_TYPE     (sheet_object_item_get_type ())
#define SHEET_OBJECT_ITEM(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_OBJECT_ITEM_TYPE, SheetObjectItem))
#define SHEET_OBJECT_ITEM_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_ITEM_TYPE, SheetObjectItemClass))
#define IS_SHEET_OBJECT_ITEM(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_ITEM_TYPE))

typedef struct {
	SheetObjectBonobo parent_object;
} SheetObjectItem;

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectItemClass;

/*
 * Bonobo Items.
 */
GtkType      sheet_object_item_get_type (void);
SheetObject *sheet_object_item_new      (Sheet *sheet,
					 double x1, double y1,
					 double x2, double y2,
					 const char *object_id);

#endif /* GNUMERIC_SHEET_OBJECT_ITEM_H */


