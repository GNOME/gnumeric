/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-object-bonobo.h"

#include "workbook.h"
#include "sheet.h"
#include "workbook-private.h"
#include "gui-util.h"

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <bonobo/bonobo-item-container.h>
#include <bonobo/bonobo-stream-memory.h>

#include <gsf/gsf-impl-utils.h>

#ifdef GNOME2_CONVERSION_COMPLETE
static SheetObjectClass *sheet_object_bonobo_parent_class;

#define SOB_CLASS(o) SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS (o))

static void
sheet_object_bonobo_destroy (GtkObject *object)
{
	SheetObjectBonobo *sob = SHEET_OBJECT_BONOBO (object);

	if (sob->control_frame != NULL) {
		bonobo_object_unref (BONOBO_OBJECT (sob->control_frame));
		sob->control_frame = NULL;
	}

	if (sob->object_server != CORBA_OBJECT_NIL) {
		bonobo_object_release_unref (sob->object_server, NULL);
		sob->object_server = CORBA_OBJECT_NIL;
	}

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

        g_signal_connect (G_OBJECT (fs->ok_button),
		"clicked",
		G_CALLBACK (gtk_main_quit), NULL);
        g_signal_connect (G_OBJECT (fs->cancel_button),
		"clicked",
		G_CALLBACK (gtk_main_quit), NULL);

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

void
sheet_object_bonobo_load_persist_file (SheetObjectBonobo *sob,
				       const char *fname,
				       CORBA_Environment *ev)
{
	Bonobo_PersistFile pf;

	g_return_if_fail (IS_SHEET_OBJECT_BONOBO (sob));
	g_return_if_fail (sob->has_persist_file);

	pf = Bonobo_Unknown_queryInterface (
		sob->object_server, "IDL:Bonobo/PersistFile:1.0", ev);

	if (!BONOBO_EX (ev)) {
		Bonobo_PersistFile_load (pf, fname, ev);
		bonobo_object_release_unref (pf, NULL);
	}
}

void
sheet_object_bonobo_load_persist_stream (SheetObjectBonobo *sob,
				         BonoboStream      *stream,
					 CORBA_Environment *ev)
{
	Bonobo_PersistStream ps;

	bonobo_return_if_fail (IS_SHEET_OBJECT_BONOBO (sob), ev);
	g_return_if_fail (sob->has_persist_stream);

	ps = Bonobo_Unknown_queryInterface (
		sob->object_server, "IDL:Bonobo/PersistStream:1.0", ev);

	if (!BONOBO_EX (ev)) {
		Bonobo_PersistStream_load (ps,
			(Bonobo_Stream) BONOBO_OBJREF (stream), "", ev);
		bonobo_object_release_unref (ps, NULL);
	}
}

void
sheet_object_bonobo_save_persist_stream (SheetObjectBonobo const *sob,
				         BonoboStream      *stream,
					 CORBA_Environment *ev)
{
	Bonobo_PersistStream ps;

	bonobo_return_if_fail (IS_SHEET_OBJECT_BONOBO (sob), ev);
	g_return_if_fail (sob->has_persist_stream);

	ps = Bonobo_Unknown_queryInterface (
		sob->object_server, "IDL:Bonobo/PersistStream:1.0", ev);

	if (!BONOBO_EX (ev)) {
		Bonobo_PersistStream_save (ps,
			(Bonobo_Stream) BONOBO_OBJREF (stream), "", ev);
		bonobo_object_release_unref (ps, NULL);
	}
}

static void
sheet_object_bonobo_print (SheetObject const *so, GnomePrintContext *ctx,
			   double base_x, double base_y)
{
	SheetObjectBonobo const *sob;
	BonoboPrintClient *bpc;
	BonoboPrintData *bpd;
	double coords [4];

	sob = SHEET_OBJECT_BONOBO (so);
	g_return_if_fail (sob != NULL);
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (ctx));


	bpc = bonobo_print_client_get (sob->object_server);
	if (!bpc) {
		static gboolean warned = FALSE;
		if (!warned)
			g_warning ("Some bonobo objects are not printable");
		warned = TRUE;
		return;
	}

	sheet_object_position_pts_get (so, coords);
	bpd = bonobo_print_data_new ((coords [2] - coords [0]),
				     (coords [3] - coords [1]));
	bonobo_print_client_render (bpc, bpd);
	bonobo_print_data_render (ctx, base_x, base_y, bpd, 0.0, 0.0);
	bonobo_print_data_free (bpd);
}

void
sheet_object_bonobo_load_file (SheetObjectBonobo *sob, const gchar *fname,
		               CORBA_Environment *ev)
{
	BonoboStream *stream;

	bonobo_return_if_fail (IS_SHEET_OBJECT_BONOBO (sob), ev);
	g_return_if_fail (sob->has_persist_file || sob->has_persist_stream);

	if (sob->has_persist_file)
		sheet_object_bonobo_load_persist_file (sob, fname, ev);
	else {
		stream = bonobo_stream_open ("fs", fname,
					     Bonobo_Storage_READ, 0);
		sheet_object_bonobo_load_persist_stream (sob, stream, ev);
		bonobo_object_unref (BONOBO_OBJECT (stream));
	}
}

static void
open_cb (GtkMenuItem *item, SheetObjectBonobo *sob)
{
	gchar *filename;
	CORBA_Environment ev;

	g_return_if_fail (sob->has_persist_file || sob->has_persist_stream);

	filename = get_file_name ();
	if (filename == NULL)
		return;

	CORBA_exception_init (&ev);

	sheet_object_bonobo_load_file (sob, filename, &ev);
	g_free (filename);
	if (BONOBO_EX (&ev)) {
		g_warning ("Could not open: %s",
			   bonobo_exception_get_text (&ev));
	}

	CORBA_exception_free (&ev);
}

static void
sheet_object_bonobo_populate_menu (SheetObject *so,
			           GtkObject   *obj_view,
			           GtkMenu     *menu)
{
	SheetObjectBonobo *sob;
	GtkWidget *item;

	sob = SHEET_OBJECT_BONOBO (so);
	g_return_if_fail (sob != NULL);

	if (sob->has_persist_file || sob->has_persist_stream) {
		item = gtk_menu_item_new_with_label (_("Open..."));
		g_signal_connect (G_OBJECT (item),
			"activate",
			G_CALLBACK (open_cb), so);
		gtk_menu_append (menu, item);
	}

	if (sheet_object_bonobo_parent_class->populate_menu)
		sheet_object_bonobo_parent_class->populate_menu (so, obj_view, menu);
}

static gboolean
sheet_object_bonobo_read_xml (SheetObject *so,
			      XmlParseContext const *ctxt, xmlNodePtr	tree)
{
	/* Leave crufty old bonobo-io override in place until we have something
	 * better
	 */
	if (ctxt->read_fn != NULL)
		return ctxt->read_fn (tree, so, ctxt->sheet, ctxt->user_data);

#if 0
	BonoboStream *stream;
	stream = bonobo_stream_mem_create (data, len, TRUE, FALSE);
	sheet_object_bonobo_load_persist_stream (sob);
	bonobo_object_unref (BONOBO_OBJECT (stream));
#endif

	return FALSE;
}

static gboolean
sheet_object_bonobo_write_xml (SheetObject const *so,
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectBonobo const *sob = SHEET_OBJECT_BONOBO (so);
	BonoboStream *stream = NULL;
	gboolean res = FALSE;

	if (ctxt->write_fn != NULL)
		return ctxt->write_fn (tree, so, ctxt->user_data);

	if (sob->has_persist_stream) {
		CORBA_Environment ev;

		stream = bonobo_stream_mem_create (NULL, 0, FALSE, TRUE);

		CORBA_exception_init (&ev);
		sheet_object_bonobo_save_persist_stream (sob, stream, &ev);
		if (BONOBO_EX (&ev)) {
			/* TODO : Generate a decent message when sheetobjects
			 * get user visible ids */
			gnumeric_io_error_save (ctxt->io_context,
				   bonobo_exception_get_text (&ev));
		} else
			res = TRUE;

		CORBA_exception_free (&ev);
	} else if (sob->has_persist_file) {
		/* Copy approach from gnum_file_saver_save_to_stream_real. */
	}
	if (res) {
		/* store this somewhere, somehow */
		bonobo_object_unref (BONOBO_OBJECT (stream));
	}
	return res;
}

static void
sheet_object_bonobo_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_bonobo_parent_class = gtk_type_class (sheet_object_get_type ());

	/* GtkObject class method overrides */
	object_class->destroy = sheet_object_bonobo_destroy;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->print         = sheet_object_bonobo_print;
	sheet_object_class->populate_menu = sheet_object_bonobo_populate_menu;
	sheet_object_class->read_xml	  = sheet_object_bonobo_read_xml;
	sheet_object_class->write_xml	  = sheet_object_bonobo_write_xml;
}

GSF_CLASS (SheetObjectBonobo, sheet_object_bonobo,
	   sheet_object_bonobo_class_init, NULL, SHEET_OBJECT_TYPE);

SheetObjectBonobo *
sheet_object_bonobo_construct (SheetObjectBonobo   *sob,
			       Bonobo_UIContainer *container,
			       char const *object_id)
{
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), NULL);

	sob->object_id     = NULL;
	sob->object_server = NULL;
	sob->control_frame = bonobo_control_frame_new (container);
	if (object_id != NULL &&
	    !sheet_object_bonobo_set_object_iid (sob, object_id)) {
		bonobo_object_unref (BONOBO_OBJECT (sob->control_frame));
		sob->control_frame = NULL;
		return NULL;
	}

	return sob;
}

char const *
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
	gboolean result;

	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), FALSE);
	g_return_val_if_fail (sob->object_id == NULL, FALSE);
	g_return_val_if_fail (object_id != NULL, FALSE);

	server = bonobo_object_activate (object_id, 0);
	if (!server)
		return FALSE;

	result = sheet_object_bonobo_set_server (sob, server);
	bonobo_object_unref (BONOBO_OBJECT (server));
	if (result == TRUE) {
		sob->object_id = g_strdup (object_id);
		return (TRUE);
	}

	return (FALSE);
}

gboolean
sheet_object_bonobo_set_server (SheetObjectBonobo *sob,
				BonoboObjectClient *server)
{
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (sob), FALSE);
	g_return_val_if_fail (sob->object_server == CORBA_OBJECT_NIL, FALSE);

	if (!bonobo_client_site_bind_embeddable (sob->control_frame, server))
		return FALSE;

	bonobo_object_ref (BONOBO_OBJECT (server));
	sob->object_server = server;

	sob->has_persist_file = bonobo_object_client_has_interface (server,
					"IDL:Bonobo/PersistFile:1.0", NULL);
	sob->has_persist_stream = bonobo_object_client_has_interface (server,
					"IDL:Bonobo/PersistStream:1.0", NULL);

	return TRUE;
}
#endif
