#ifndef GNUMERIC_SHEET_OBJECT_CONTAINER_H
#define GNUMERIC_SHEET_OBJECT_CONTAINER_H

#include "sheet-object-bonobo.h"
#include <bonobo/bonobo-client-site.h>

/*
 * SheetObjectContainer:
 *
 * Container for Bonobo objects
 */
#define SHEET_OBJECT_CONTAINER_TYPE     (sheet_object_container_get_type ())
#define SHEET_OBJECT_CONTAINER(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_CONTAINER_TYPE, SheetObjectContainer))
#define SHEET_OBJECT_CONTAINER_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_CONTAINER_TYPE, SheetObjectContainerClass))
#define IS_SHEET_OBJECT_CONTAINER(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_CONTAINER_TYPE))

typedef struct {
	SheetObjectBonobo  parent_object;
} SheetObjectContainer;

typedef struct {
	SheetObjectBonoboClass parent_class;
} SheetObjectContainerClass;

/*
 * Bonobo::View Bonobo containers.
 */
GtkType      sheet_object_container_get_type (void);

SheetObject *sheet_object_container_new		(Sheet *sheet);
SheetObject *sheet_object_container_new_file	(Sheet *sheet,
						 const char *filename);
SheetObject *sheet_object_container_new_object	(Sheet *sheet,
						 const char *object_id);

#endif /* GNUMERIC_SHEET_OBJECT_CONTAINER_H */
