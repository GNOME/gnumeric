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
#include "gnumeric.h"
#include "workbook.h"
#include "sheet.h"
#include "workbook-private.h"
#include "gnumeric-util.h"
#include "sheet-object-bonobo.h"

#include <math.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <bonobo/bonobo-item-container.h>
#include <bonobo/bonobo-view-frame.h>
#include <bonobo/bonobo-client-site.h>
#include <bonobo/bonobo-embeddable.h>
#include <gal/util/e-util.h>

static SheetObjectClass *sheet_object_bonobo_parent_class;

#define SOB_CLASS(o) SHEET_OBJECT_CLASS (GTK_OBJECT (o)->klass)

static void
sheet_object_bonobo_destroy (GtkObject *object)
{
	SheetObjectBonobo *sob = SHEET_OBJECT_BONOBO (object);

	if (sob->client_site != NULL) {
		bonobo_object_unref (BONOBO_OBJECT (sob->client_site));
		sob->client_site = NULL;
	}

#if 0
	if (sob->object_server != NULL) {
		bonobo_object_unref (BONOBO_OBJECT (sob->object_server));
		sob->object_server = NULL;
	}
#endif

	if (sob->object_id != NULL) {
		g_free (sob->object_id);
		sob->object_id = NULL;
	}

	/* Call parent's destroy method */
	GTK_OBJECT_CLASS (sheet_object_bonobo_parent_class)->destroy (object);
}

static char *
get_file_name (void)
{
	GtkFileSelection *fs;
	char *filename;
	char *basename;

	fs = GTK_FILE_SELECTION (gtk_file_selection_new ("Select filename"));

        gtk_signal_connect (GTK_OBJECT (fs->ok_button),
                            "clicked",
                            GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

        gtk_signal_connect (GTK_OBJECT (fs->cancel_button),
                            "clicked",
                            GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

	gtk_widget_show (GTK_WIDGET (fs));
	gtk_main ();

        filename = gtk_file_selection_get_filename (fs);

	if (!(basename = g_basename (filename)) ||
	    basename [0] == '\0')
		filename = NULL;
	else
		filename = g_strdup (filename);

	gtk_object_destroy (GTK_OBJECT (fs));

	return filename;
}

/**
 * sheet_object_bonobo_load_file:
 * @sob: A SheetBonoboObject
 * @fname: File from which the state is loaded for @sob
 *
 * Loads the state for the Bonobo component from @fname
 *
 * Returns TRUE on success, FALSE on failure.
 */
gboolean
sheet_object_bonobo_load_file (SheetObjectBonobo *sob, const char *fname)
{
	CORBA_Environment ev;
	Bonobo_PersistFile pf;
	Bonobo_PersistStream ps;

	CORBA_exception_init (&ev);

	pf = Bonobo_Unknown_queryInterface (
		bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
		"IDL:Bonobo/PersistFile:1.0", &ev);

	if (ev._major == CORBA_NO_EXCEPTION && pf != CORBA_OBJECT_NIL){
		char *file;

		if (!fname)
			file = get_file_name ();
		else
			file = g_strdup (fname);
		if (file) {
			Bonobo_PersistFile_load (pf, file, &ev);
			if (BONOBO_EX (&ev))
				g_warning ("Error '%s'", bonobo_exception_get_text (&ev));
		}

		bonobo_object_release_unref (pf, &ev);
		g_free (file);

		goto finish;
	}

	ps = Bonobo_Unknown_queryInterface (
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

			stream = bonobo_stream_open ("fs", file, Bonobo_Storage_READ, 0);
			if (stream) {
				Bonobo_PersistStream_load (
					ps,
					(Bonobo_Stream) bonobo_object_corba_objref (
						BONOBO_OBJECT (stream)), "", &ev);
				if (BONOBO_EX (&ev))
					g_warning ("Error '%s'", bonobo_exception_get_text (&ev));
			} else
				g_warning ("Failed to open '%s'", file);
		}

		bonobo_object_release_unref (pf, &ev);
		g_free (file);

		goto finish;
	}
	CORBA_exception_free (&ev);
	return FALSE;

 finish:
	CORBA_exception_free (&ev);
	return TRUE;
}

/**
 * sheet_object_bonobo_load_stream:
 * @sob: SheetObject Bonobo component
 * @stream: Stream used to load the state of the @sob component
 */
gboolean
sheet_object_bonobo_load_stream (SheetObjectBonobo *sob,
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

	ret = Bonobo_Unknown_queryInterface (
		bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
		"IDL:Bonobo/PersistStream:1.0", &ev);
	if (ev._major == CORBA_NO_EXCEPTION && ret != CORBA_OBJECT_NIL) {
		if (stream) {
			Bonobo_PersistStream_load (
				ret,
				(Bonobo_Stream) bonobo_object_corba_objref (
					BONOBO_OBJECT (stream)), "", &ev);
			Bonobo_Unknown_unref ((Bonobo_Unknown) ret, &ev);
			CORBA_Object_release (ret, &ev);
		}
	} else {
		g_warning ("Component has data to load but no PersistStream interface");
		CORBA_exception_free (&ev);
		return FALSE;
	}
	CORBA_exception_free (&ev);

	return TRUE;
}

static void
sheet_object_bonobo_print (SheetObject const *so,
			   SheetObjectPrintInfo const *pi)
{
	SheetObjectBonobo const *sob;
	BonoboPrintClient *bpc;

	g_return_if_fail (IS_SHEET_OBJECT_BONOBO (so));

	sob = SHEET_OBJECT_BONOBO (so);

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

static void
open_cb (GtkMenuItem *item, SheetObjectBonobo *sheet_object)
{
	sheet_object_bonobo_load_file (sheet_object, NULL);
}

static void
sheet_object_bonobo_populate_menu (SheetObject *sheet_object,
				   GtkObject   *obj_view,
				   GtkMenu     *menu)
{
	GtkWidget *item = gtk_menu_item_new_with_label (_("Open"));

	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    (GtkSignalFunc) open_cb, sheet_object);

	gtk_menu_append (menu, item);

	if (sheet_object_bonobo_parent_class->populate_menu)
		sheet_object_bonobo_parent_class->
			populate_menu (sheet_object, obj_view, menu);
}

static gboolean
sheet_object_bonobo_read_xml (SheetObject *so,
			      XmlParseContext const *ctxt, xmlNodePtr	tree)
{
	if (ctxt->read_fn == NULL) {
		g_warning ("Internal error, no read fn");
		return TRUE;
	}

	return ctxt->read_fn (tree, so, ctxt->sheet, ctxt->user_data);
}

static gboolean
sheet_object_bonobo_write_xml (SheetObject const *so,
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	if (ctxt->write_fn == NULL ||
	    !ctxt->write_fn (tree, so, ctxt->user_data)) {
		g_warning ("Error serializing bonobo sheet object");
		return TRUE;
	}

	return FALSE;
}

static void
sheet_object_bonobo_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_bonobo_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_bonobo_destroy;

	sheet_object_class->print = sheet_object_bonobo_print;
	sheet_object_class->populate_menu = sheet_object_bonobo_populate_menu;
	sheet_object_class->read_xml	  = sheet_object_bonobo_read_xml;
	sheet_object_class->write_xml	  = sheet_object_bonobo_write_xml;
}

E_MAKE_TYPE (sheet_object_bonobo, "SheetObjectBonobo", SheetObjectBonobo,
	     sheet_object_bonobo_class_init, NULL, SHEET_OBJECT_TYPE);

SheetObjectBonobo *
sheet_object_bonobo_construct (SheetObjectBonobo *sob,
			       Sheet const *sheet,
			       char const *object_id)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), NULL);

	sob->object_id     = NULL;
	sob->object_server = NULL;
	sob->client_site = bonobo_client_site_new (sheet->workbook->priv->bonobo_container);
	if (object_id != NULL &&
	    !sheet_object_bonobo_set_object_iid (sob, object_id))
		return NULL;

	return sob;
}

const char *
sheet_object_bonobo_get_object_iid (SheetObjectBonobo const *sob)
{
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), NULL);

	return sob->object_id;
}

/**
 * sheet_object_bonobo_set_object_iid :
 *
 * @sob : The Sheet bonobo object
 * @object_id : An optional id
 *
 * returns TRUE if things succeed.
 */
gboolean
sheet_object_bonobo_set_object_iid (SheetObjectBonobo *sob,
				    char const *object_id)
{
	BonoboObjectClient *server;

	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), FALSE);
	g_return_val_if_fail (sob->object_id == NULL, FALSE);
	g_return_val_if_fail (object_id != NULL, FALSE);

	server = bonobo_object_activate (object_id, 0);
	if (!server) {
		gtk_object_destroy (GTK_OBJECT (sob));
		return FALSE;
	}
	sob->object_id = g_strdup (object_id);

	return sheet_object_bonobo_set_server (sob, server);
}

gboolean
sheet_object_bonobo_set_server (SheetObjectBonobo *sob,
				BonoboObjectClient *server)
{
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), FALSE);
	g_return_val_if_fail (sob->object_server == NULL, FALSE);
	g_return_val_if_fail (BONOBO_IS_OBJECT_CLIENT (server), FALSE);

	if (!bonobo_client_site_bind_embeddable (sob->client_site, server)) {
		gtk_object_destroy (GTK_OBJECT (sob));
		return FALSE;
	}
	bonobo_object_ref (BONOBO_OBJECT (server));
	sob->object_server = server;

	return TRUE;
}
