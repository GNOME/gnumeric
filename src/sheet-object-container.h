#ifndef GNUMERIC_SHEET_OBJECT_CONTAINER_H
#define GNUMERIC_SHEET_OBJECT_CONTAINER_H

#include "sheet-object.h"

/*
 * SheetObjectContainer:
 *
 * Container for Bonobo objects
 */
#define SHEET_OBJECT_CONTAINER_TYPE     (sheet_object_container_get_type ())
#define SHEET_OBJECT_CONTAINER(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_CONTAINER_TYPE, SheetObjectContainer))
#define SHEET_OBJECT_CONTAINER_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_CONTAINER_TYPE))
#define IS_SHEET_CONTAINER_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_CONTAINER_TYPE))

typedef struct {
	SheetObject     parent_object;

	/*
	 * The ClientSite for the bonobo object
	 *
	 * If this is NULL the object has not yet been
	 * activated/bound to this site
	 */
	GnomeClientSite *client_site;

	/*
	 * Points to the object server that implements this view
	 */
	GnomeObjectClient *object_server;
	char *repoid;
} SheetObjectContainer;

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectContainerClass;

/*
 * Generic Bonobo containers.
 */
GtkType      sheet_object_container_get_type (void);
SheetObject *sheet_object_container_new      (Sheet *sheet,
					      double x1, double y1,
					      double x2, double y2,
					      const char *object_name);
void         sheet_object_container_land     (SheetObject *so);

/*
 * Graphics
 */
SheetObject *sheet_object_graphic_new        (Sheet *sheet,
					      double x1, double y1,
					      double x2, double y2);

#endif /* GNUMERIC_SHEET_OBJECT_CONTAINER_H */
