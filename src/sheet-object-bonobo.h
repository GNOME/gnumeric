#ifndef GNUMERIC_SHEET_OBJECT_BONOBO_H
#define GNUMERIC_SHEET_OBJECT_BONOBO_H

#include "sheet-object-impl.h"
#include <bonobo/bonobo-control-frame.h>

/*
 * SheetObjectBonobo:
 *
 * SheetObject *abstract* class for embedding Bonobo components.
 *    The sheet-object-container implements a window-based Gnome::View embedder
 *    The sheet-object-item implements a canvas-based Gnome::Canvas
 */
#define SHEET_OBJECT_BONOBO_TYPE     (sheet_object_bonobo_get_type ())
#define SHEET_OBJECT_BONOBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_OBJECT_BONOBO_TYPE, SheetObjectBonobo))
#define SHEET_OBJECT_BONOBO_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k),	SHEET_OBJECT_BONOBO_TYPE, SheetObjectBonoboClass))
#define IS_SHEET_OBJECT_BONOBO(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o),	SHEET_OBJECT_BONOBO_TYPE))

typedef struct {
	SheetObject         parent_object;

	/* The ControlFrame for the bonobo object
	 *
	 * If this is NULL the object has not yet been
	 * activated/bound to this site
	 */
	BonoboControlFrame   *client_site;
	char *object_id;

	Bonobo_ControlFactory *factory;
	gboolean has_persist_file;
	gboolean has_persist_stream;
} SheetObjectBonobo;

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectBonoboClass;

GType              sheet_object_bonobo_get_type  (void);
SheetObjectBonobo *sheet_object_bonobo_construct (SheetObjectBonobo	 *sob,
						  Bonobo_UIContainer	 *container,
						  char const		 *object_id);
char const *sheet_object_bonobo_get_object_iid	(SheetObjectBonobo const *sob);
gboolean    sheet_object_bonobo_set_object_iid	(SheetObjectBonobo	 *sob,
						 char const	   	 *object_id);
gboolean    sheet_object_bonobo_set_server	(SheetObjectBonobo	 *sob,
						 BonoboObjectClient	 *object_server);

void sheet_object_bonobo_load_file           (SheetObjectBonobo *sob,
					      char const        *fname, 
					      CORBA_Environment *ev);
void sheet_object_bonobo_load_persist_file   (SheetObjectBonobo *sob,
					      char const        *fname,
					      CORBA_Environment *ev);
void sheet_object_bonobo_load_persist_stream (SheetObjectBonobo *sob,
					      BonoboStream      *stream,
					      CORBA_Environment *ev);
void sheet_object_bonobo_save_persist_stream (SheetObjectBonobo const *sob,
					      BonoboStream      *stream,
					      CORBA_Environment *ev);

#endif /* GNUMERIC_SHEET_OBJECT_ITEM_H */
