#ifndef GNUMERIC_SHEET_OBJECT_CONTAINER_H
#define GNUMERIC_SHEET_OBJECT_CONTAINER_H

#include "sheet-object-bonobo.h"

/*
 * SheetObjectContainer:
 *
 * Container for Bonobo objects
 */
#define SHEET_OBJECT_CONTAINER_TYPE     (sheet_object_container_get_type ())
#define SHEET_OBJECT_CONTAINER(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_OBJECT_CONTAINER_TYPE, SheetObjectContainer))
#define SHEET_OBJECT_CONTAINER_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_CONTAINER_TYPE, SheetObjectContainerClass))
#define IS_SHEET_OBJECT_CONTAINER(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_CONTAINER_TYPE))

typedef struct {
	SheetObjectBonobo  parent_object;
} SheetObjectContainer;

typedef struct {
	SheetObjectBonoboClass parent_class;
} SheetObjectContainerClass;

GtkType sheet_object_container_get_type (void);

SheetObject *sheet_object_container_new		(Workbook *wb);
SheetObject *sheet_object_container_new_file	(Workbook *wb,
						 char const *filename);
SheetObject *sheet_object_container_new_object	(Workbook *wb,
						 char const *object_id);

#endif /* GNUMERIC_SHEET_OBJECT_CONTAINER_H */
