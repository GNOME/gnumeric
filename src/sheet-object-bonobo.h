#ifndef GNUMERIC_SHEET_OBJECT_BONOBO_H
#define GNUMERIC_SHEET_OBJECT_BONOBO_H

#include "sheet-object-impl.h"
#include <bonobo/bonobo-client-site.h>

/*
 * SheetObjectBonobo:
 *
 * SheetObject *abstract* class for embedding Bonobo components.
 *    The sheet-object-container implements a window-based Gnome::View embedder
 *    The sheet-object-item implements a canvas-based Gnome::Canvas
 */
#define SHEET_OBJECT_BONOBO_TYPE     (sheet_object_bonobo_get_type ())
#define SHEET_OBJECT_BONOBO(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_BONOBO_TYPE, SheetObjectBonobo))
#define SHEET_OBJECT_BONOBO_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_BONOBO_TYPE, SheetObjectBonoboClass))
#define IS_SHEET_OBJECT_BONOBO(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_BONOBO_TYPE))

typedef struct {
	SheetObject         parent_object;

	/* The ClientSite for the bonobo object
	 *
	 * If this is NULL the object has not yet been
	 * activated/bound to this site
	 */
	BonoboClientSite   *client_site;
	char *object_id;

	/* the object server that implements this SheetObjectBonobo */
	BonoboObjectClient *object_server;
	gboolean has_persist_file;
	gboolean has_persist_stream;
} SheetObjectBonobo;

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectBonoboClass;

GtkType            sheet_object_bonobo_get_type  (void);
SheetObjectBonobo *sheet_object_bonobo_construct (SheetObjectBonobo	 *sob,
						  BonoboItemContainer	 *container,
						  char const		 *object_id);
char const *sheet_object_bonobo_get_object_iid	(SheetObjectBonobo const *sob);
gboolean    sheet_object_bonobo_set_object_iid	(SheetObjectBonobo	 *sob,
						 char const	   	 *object_id);
gboolean    sheet_object_bonobo_set_server	(SheetObjectBonobo	 *sob,
						 BonoboObjectClient	 *object_server);
void        sheet_object_bonobo_query_size	(SheetObjectBonobo *sob);

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
