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

#define ENABLE_BONOBO_PRINT

static SheetObjectClass *sheet_object_bonobo_parent_class;

#define SOB_CLASS(o) SHEET_OBJECT_CLASS (GTK_OBJECT (o)->klass)

static void
sheet_object_bonobo_destroy (GtkObject *object)
{
	SheetObjectBonobo *sob = SHEET_OBJECT_BONOBO (object);

	/* Call parent's destroy method */
	GTK_OBJECT_CLASS (sheet_object_bonobo_parent_class)->destroy (object);

	if (sob->client_site)
		bonobo_object_destroy (BONOBO_OBJECT (sob->client_site));

	sob->object_server = NULL;
	sob->client_site   = NULL;	
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
		"IDL:Bonobo/PersistFile:1.0", &ev);

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
		"IDL:Bonobo/PersistStream:1.0", &ev);
	
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
			  BonoboStream      *stream)
{
	CORBA_Environment   ev;
	Bonobo_PersistStream ret;
	
	if (!stream)
		return TRUE;
	
	g_return_val_if_fail (sob != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), FALSE);
	g_return_val_if_fail (sob->client_site != NULL, FALSE);
	
	CORBA_exception_init (&ev);
	
	ret = Bonobo_Unknown_query_interface (
		bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
		"IDL:Bonobo/PersistStream:1.0", &ev);
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
		g_warning ("Component has data to load but no PersistStream interface");
		CORBA_exception_free (&ev);
		return FALSE;
	}
	CORBA_exception_free (&ev);

	sheet_object_realize (SHEET_OBJECT (sob));
	return TRUE;
}

#ifdef ENABLE_BONOBO_PRINT
static void
sheet_object_bonobo_print (SheetObject *so, SheetObjectPrintInfo *pi)
{
	SheetObjectBonobo  *sob = SHEET_OBJECT_BONOBO (so);
	BonoboPrintClient  *bpc;

	bpc = bonobo_print_client_get (sob->object_server);
	if (!bpc) {
		static gboolean warned = FALSE;
		if (!warned)
			g_warning ("Some bonobo objects are not printable");
		warned = TRUE;
		return;
	}

	bonobo_print_client_render (bpc, pi->pd);
}
#endif

static void
sheet_object_bonobo_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_bonobo_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_bonobo_destroy;

#ifdef ENABLE_BONOBO_PRINT
	sheet_object_class->print = sheet_object_bonobo_print;
#endif
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

	sheet_object_construct  (SHEET_OBJECT (sob), sheet);
	sheet_object_set_bounds (SHEET_OBJECT (sob), x1, y1, x2, y2);

	sob->object_server = object_server;
	sob->client_site   = bonobo_client_site_new (sheet->workbook->bonobo_container);
	
	if (!bonobo_client_site_bind_embeddable (sob->client_site, sob->object_server)) {
		gtk_object_destroy (GTK_OBJECT (sob));
		return NULL;
	}

	return sob;
}
