/*
 * sheet-object-container.c:
 *   SheetObject abstract class for Bonobo-based embeddings
 *
 *   See sheet-object-container.c for Gnome::View based embeddings
 *   See sheet-object-item.c for Gnome::Canvas based embeddings
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks (mmeeks@gnu.org)
 *
 * TODO:
 *
 *    Perhaps we should relay "realize" and get from the Bonobo
 *    versions both the GnomeCanvasItem to use for the gnumeric
 *    display and the BonoboViewFrame that logically "controls" this
 *    to keep track of the view frames.
 */
#include <config.h>
#include <gnome.h>
#include <libgnorba/gnorba.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include "gnumeric.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "sheet-object-bonobo.h"
#include <bonobo/bonobo-container.h>
#include <bonobo/bonobo-view-frame.h>
#include <bonobo/bonobo-client-site.h>
#include <bonobo/bonobo-embeddable.h>

static SheetObject *sheet_object_bonobo_parent_class;

#define SOB_CLASS(o) SHEET_OBJECT_CLASS (GTK_OBJECT (o)->klass)

static void
sheet_object_bonobo_destroy (GtkObject *object)
{
	SheetObjectBonobo *sob = SHEET_OBJECT_BONOBO (object);
	
	if (sob->client_site)
		bonobo_object_destroy (BONOBO_OBJECT (sob->client_site));
	
	sob->client_site = NULL;
	
	/* Call parent's destroy method */
	GTK_OBJECT_CLASS (sheet_object_bonobo_parent_class)->destroy (object);
}

static char *
get_file_name (void)
{
	GtkFileSelection *fs;
	gchar *filename;
 
	fs = GTK_FILE_SELECTION (gtk_file_selection_new ("Select filename"));
	
        gtk_signal_connect (GTK_OBJECT (fs->ok_button),
                            "clicked",
                            GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

        gtk_signal_connect (GTK_OBJECT (fs->cancel_button),
                            "clicked",
                            GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

	gtk_widget_show (GTK_WIDGET (fs));
	gtk_main ();
	
        filename = g_strdup (gtk_file_selection_get_filename (fs));

	gtk_object_destroy (GTK_OBJECT (fs));

	return filename;
}

/**
 * sheet_object_bonobo_load_from_file:
 * @sob: A SheetBonoboObject
 * @fname: File from which the state is loaded for @sob
 *
 * Loads the state for the Bonobo component from @fname
 *
 * Returns TRUE on success, FALSE on failure.
 */
gboolean
sheet_object_bonobo_load_from_file (SheetObjectBonobo *sob, const char *fname)
{
	CORBA_Environment ev;
	Bonobo_PersistFile pf;
	Bonobo_PersistStream ps;
	
	CORBA_exception_init (&ev);

	pf = Bonobo_Unknown_query_interface (
		bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
		"IDL:GNOME/PersistFile:1.0", &ev);

	if (ev._major == CORBA_NO_EXCEPTION && pf != CORBA_OBJECT_NIL){
		char *file;

		if (!fname)
			file = get_file_name ();
		else
			file = g_strdup (fname);
		if (file)
			Bonobo_PersistFile_load (pf, file, &ev);

		Bonobo_Unknown_unref ((Bonobo_Unknown) pf, &ev);
		CORBA_Object_release (pf, &ev);
		g_free (file);

		goto finish;
	} 
		
	ps = Bonobo_Unknown_query_interface (
		bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
		"IDL:GNOME/PersistStream:1.0", &ev);
	
	if (ev._major == CORBA_NO_EXCEPTION && ps != CORBA_OBJECT_NIL){
		char *file;
		
		if (!fname)
			file = get_file_name ();
		else
			file = g_strdup (fname);

		if (file) {
			BonoboStream *stream;
			
			stream = bonobo_stream_fs_open (file, Bonobo_Storage_READ);
			if (stream) {
				Bonobo_PersistStream_load (
					ps,
					(Bonobo_Stream) bonobo_object_corba_objref (
						BONOBO_OBJECT (stream)), &ev);
			}
		}
		Bonobo_Unknown_unref ((Bonobo_Unknown) ps, &ev);
		CORBA_Object_release (ps, &ev);
		g_free (file);

		goto finish;
	}
	CORBA_exception_free (&ev);
	return FALSE;

 finish:
	sheet_object_realize (SHEET_OBJECT (sob));
	CORBA_exception_free (&ev);
	return TRUE;
}

/**
 * sheet_object_bonobo_load:
 * @sob: SheetObject Bonobo component
 * @stream: Stream used to load the state of the @sob component
 */
gboolean
sheet_object_bonobo_load (SheetObjectBonobo *sob,
			  BonoboStream *stream)
{
	CORBA_Environment   ev;
	Bonobo_PersistStream ret;
	
	g_return_val_if_fail (sob != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), FALSE);
	g_return_val_if_fail (sob->client_site != NULL, FALSE);
	
	CORBA_exception_init (&ev);
	
	ret = Bonobo_Unknown_query_interface (
		bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
		"IDL:GNOME/PersistStream:1.0", &ev);
	if (ev._major == CORBA_NO_EXCEPTION && ret != CORBA_OBJECT_NIL) {
		if (stream) {
			Bonobo_PersistStream_load (
				ret,
				(Bonobo_Stream) bonobo_object_corba_objref (
					BONOBO_OBJECT (stream)), &ev);
		}
		Bonobo_Unknown_unref ((Bonobo_Unknown) ret, &ev);
		CORBA_Object_release (ret, &ev);
	} else {
		CORBA_exception_free (&ev);
		return FALSE;
	}
	CORBA_exception_free (&ev);

	sheet_object_realize (SHEET_OBJECT (sob));
	return TRUE;
}

void
sheet_object_bonobo_query_size (SheetObjectBonobo *sob)
{
#if 0
	int dx = -1, dy = -1;
	bonobo_view_frame_size_request (view_frame, &dx, &dy);
	
	if (dx > 0 && dy > 0) {
		double tlx, tly, brx, bry;
		
		sheet_object_get_bounds (so, &tlx, &tly, &brx, &bry);
		sheet_object_set_bounds (so,  tlx,  tly, tlx + dx, tly + dy);
	}
#else
	g_warning ("We need to get our hands on the ViewFrame :-)");
#endif
}

static void
sheet_object_bonobo_class_init (GtkObjectClass *object_class)
{
	sheet_object_bonobo_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_bonobo_destroy;
}

GtkType
sheet_object_bonobo_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObjectBonobo",
			sizeof (SheetObjectBonobo),
			sizeof (SheetObjectBonoboClass),
			(GtkClassInitFunc) sheet_object_bonobo_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (sheet_object_get_type (), &info);
	}

	return type;
}

SheetObjectBonobo *
sheet_object_bonobo_construct (SheetObjectBonobo *sob, Sheet *sheet,
			       BonoboObjectClient *object_server,
			       double x1, double y1,
			       double x2, double y2)
{
	g_return_val_if_fail (sob != NULL, NULL);
	g_return_val_if_fail (object_server != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), NULL);
	g_return_val_if_fail (BONOBO_IS_OBJECT_CLIENT (object_server), NULL);

	sheet_object_construct (SHEET_OBJECT (sob), sheet);
	sheet_object_set_bounds (SHEET_OBJECT (sob), x1, y1, x2, y2);

	sob->object_server = object_server;
	sob->client_site = bonobo_client_site_new (sheet->workbook->bonobo_container);
	
	if (!bonobo_client_site_bind_embeddable (sob->client_site, sob->object_server)){
		gtk_object_destroy (GTK_OBJECT (sob));
		return NULL;
	}

	return sob;
}
