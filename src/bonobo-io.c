/*
 * bonobo-io.c: Workbook IO using Bonobo storages.
 *
 * Author:
 *   Michael Meeks <michael@helixcode.com>
 *
 * Copyright 2000, Helix Code, Inc.
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "bonobo-io.h"

#include "sheet-object-bonobo.h"
#include "sheet-object-container.h"
#include "command-context.h"
#include "io-context.h"
#include "workbook-control-component.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "file.h"

#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <math.h>
#include <limits.h>
#include <bonobo.h>

#include <gsf/gsf-input.h>
#include <gsf-gnome/gsf-input-bonobo.h>

#ifdef GNOME2_CONVERSION_COMPLETE
static GnumFileOpener *gnumeric_bonobo_opener;
static GnumFileSaver *gnumeric_bonobo_saver;
#endif

void
gnumeric_bonobo_read_from_stream (BonoboPersistStream       *ps,
				  Bonobo_Stream              stream,
				  Bonobo_Persist_ContentType type,
				  void                      *data,
				  CORBA_Environment         *ev)
{
	WorkbookControl *wbc;
	WorkbookView    *wb_view;
	Workbook        *wb;
	IOContext       *ioc;
	GsfInput       *input = NULL;
	GnumFileOpener const *fo    = NULL;
	FileProbeLevel pl;
	GList *l;
	gboolean         old;
	Workbook        *old_wb;

	g_return_if_fail (data != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_COMPONENT (data));	
	wbc = WORKBOOK_CONTROL (data);

	ioc = gnumeric_io_context_new (COMMAND_CONTEXT (wbc));
	input = gsf_input_bonobo_new (stream, NULL);
	/* Search for an applicable opener */
	/* Fixme: We should be able to choose opener by mime type */
	for (pl = FILE_PROBE_FILE_NAME; pl <
		     FILE_PROBE_LAST && fo == NULL; pl++) {
		for (l = get_file_openers (); l != NULL; l = l->next) {
			GnumFileOpener const *tmp_fo
				= GNUM_FILE_OPENER (l->data);
			if (gnum_file_opener_probe (tmp_fo, input, pl)) {
				fo = tmp_fo;
				break;
			}
		}
	}

	if (fo != NULL) {
		wb_view = workbook_view_new (NULL);
		wb      = wb_view_workbook (wb_view);
		
		/* disable recursive dirtying while loading */
		old = workbook_enable_recursive_dirty (wb, FALSE);
		gnum_file_opener_open (fo, ioc, wb_view, input);
		workbook_enable_recursive_dirty (wb, old);
		if (gnumeric_io_error_occurred (ioc))
			workbook_unref (wb);		
	} else
		gnumeric_io_error_read (ioc, _("Unsupported file format."));
	if (gnumeric_io_error_occurred (ioc)) {
		gnumeric_io_error_display (ioc);
		/* This may be a bad exception to throw, but they're all bad */
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
	}
	g_object_unref (G_OBJECT (ioc));
	if (BONOBO_EX (ev)) {	
		return;
	}

	workbook_set_dirty (wb, FALSE);
	
	old_wb = wb_control_workbook (wbc);
	
	if (workbook_is_dirty (old_wb)) {
		/* No way to interact properly with user */
		g_warning ("Old workbook has unsaved changes.");
		/* FIXME: Do something about it when the viewer has a real
		 *        read only mode. For now, it doesn't mean a thing. */
		/* goto exit_error; */
	}
	g_object_ref (G_OBJECT (wbc));
	workbook_unref (old_wb);		
	workbook_control_set_view (wbc, wb_view, NULL);
	workbook_control_init_state (wbc);
	workbook_recalc (wb);
	g_return_if_fail (!workbook_is_dirty (wb));
	sheet_update (wb_view_cur_sheet (wb_view));
	return;

}

#ifdef GNOME2_CONVERSION_COMPLETE
static void
write_stream_to_storage (xmlNodePtr           cur,
			 Bonobo_PersistStream persist,
			 Bonobo_Storage       storage,
			 CORBA_Environment    *ev)
{
	static int idx = 0;
	Bonobo_Stream stream;
	char *name = NULL;
	gboolean loop;
	int flags;

	flags = Bonobo_Storage_CREATE |	Bonobo_Storage_WRITE |
		Bonobo_Storage_FAILIFEXIST;

	do {
		loop = FALSE;
		g_free (name);
		name = g_strdup_printf ("stream %d", idx++);
		stream = Bonobo_Storage_openStream (storage, name, flags, ev);
		if (ev->_major == CORBA_USER_EXCEPTION &&
		    strcmp (ev->_id, ex_Bonobo_Storage_NameExists)) {
			CORBA_exception_free (ev);
			loop = TRUE;
		}
	} while (loop);

	if (BONOBO_EX (ev)) {
		g_free (name);
		return;
	}

	xmlSetProp (cur, (xmlChar *)"Stream", (xmlChar *)name);
	g_free (name);

	/*
	 * We need to save the type too, to fall back to in case of
	 * no oafiid match on remote machine
	 */
	Bonobo_PersistStream_save (persist, stream, "", ev);

	bonobo_object_release_unref (stream, ev);
}

static gboolean
gnumeric_bonobo_obj_write (xmlNodePtr   cur,
			   SheetObject const *object,
			   gpointer     user_data)
{
	CORBA_Environment    ev;
	SheetObjectBonobo   *sob;
	Bonobo_PersistStream ps;
	Bonobo_Storage       storage;
	gboolean             ret = TRUE;

	g_return_val_if_fail (user_data != NULL, FALSE);
	storage = BONOBO_OBJREF (user_data);
	g_return_val_if_fail (storage != CORBA_OBJECT_NIL, FALSE);
	CORBA_exception_init (&ev);
#if 0
#	ifdef ORBIT_IMPLEMENTS_IS_A
	g_return_val_if_fail (CORBA_Object_is_a (storage, "IDL:Bonobo/Storage:1.0", &ev), FALSE);
	g_return_val_if_fail (ev._major == CORBA_NO_EXCEPTION, FALSE);
#	endif
#endif
	g_return_val_if_fail (IS_SHEET_OBJECT_BONOBO (object), FALSE);

	sob = SHEET_OBJECT_BONOBO (object);

	ps = Bonobo_Unknown_queryInterface (
		sob->object_server, "IDL:Bonobo/PersistStream:1.0", &ev);

	if (!BONOBO_EX (&ev) && ps != CORBA_OBJECT_NIL) {
		write_stream_to_storage (cur, ps, storage, &ev);
		bonobo_object_release_unref (ps, &ev);
	} else {
		g_warning ("Component has data to save but no PersistStream interface");
		ret = FALSE;
	}

	if (ret && BONOBO_EX (&ev))
		ret = FALSE;

	CORBA_exception_free (&ev);

	/*
	 * If it is a complex object / container it will need to
	 * serialize to a Storage;
	 * this needs implementing
	 * xml_set_value_string (cur, "Storage", ###);
	 */

	if (ret)
		xmlSetProp (cur, (xmlChar *)"OAFIID", (xmlChar *)sheet_object_bonobo_get_object_iid (sob));

	return ret;
}

static void
read_stream_from_storage (Bonobo_Unknown       object,
			  Bonobo_Storage       storage,
			  const char          *sname,
			  CORBA_Environment    *ev)
{
	Bonobo_Stream        stream;
	Bonobo_PersistStream ps;

	ps = Bonobo_Unknown_queryInterface (
		object, "IDL:Bonobo/PersistStream:1.0", ev);

	if (BONOBO_EX (ev) || ps == CORBA_OBJECT_NIL) {
		g_warning ("Wierd, component used to have a PersistStream interface");
		return;
	}

	stream = Bonobo_Storage_openStream (
		storage, sname, Bonobo_Storage_READ, ev);

	if (ev->_major != CORBA_NO_EXCEPTION) {
		g_warning ("Can't open stream '%s'", sname);
		return;
	}

	/*
	 * We need to restore the type too.
	 */
	Bonobo_PersistStream_load (ps, stream, "", ev);
	if (BONOBO_EX (ev)) {
		g_warning ("Error '%s' loading stream",
			   bonobo_exception_get_text (ev));
		return;
	}

	bonobo_object_release_unref (stream, ev);
	bonobo_object_release_unref (ps, ev);
}

static gboolean
gnumeric_bonobo_obj_read (xmlNodePtr   tree,
			  SheetObject *so,
			  Sheet       *sheet,
			  gpointer     user_data)
{
	CORBA_Environment    ev;
	SheetObjectBonobo   *sob;
	Bonobo_Storage       storage;
	xmlChar             *object_id, *sname;

	g_return_val_if_fail (tree != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (user_data != NULL, TRUE);

	sob = SHEET_OBJECT_BONOBO (so);
	g_return_val_if_fail (sob != NULL, TRUE);

	object_id = xmlGetProp (tree, (xmlChar *)"OAFIID");
	if (!object_id) {
		g_warning ("Malformed object, error in save; no id");
		return TRUE;
	}

	storage = BONOBO_OBJREF (user_data);
	g_return_val_if_fail (storage != CORBA_OBJECT_NIL, TRUE);

	CORBA_exception_init (&ev);

	sname = xmlGetProp (tree, (xmlChar *)"Stream");
	if (sname)
		read_stream_from_storage (
			sob->object_server, storage, (char *)sname, &ev);
	else
		g_warning ("No stream");

	if (BONOBO_EX (&ev)) {
		gtk_object_unref (GTK_OBJECT (sob));
		CORBA_exception_free (&ev);
		g_warning ("nasty error");
		return TRUE;
	}

	/*
	 * FIXME: If it is a complex object / container it will need
	 * to serialize to a Storage; this needs implementing
	 * xml_set_value_string (cur, "Storage", ###);
	 */
	CORBA_exception_free (&ev);

	return FALSE;
}

static void
gnumeric_bonobo_write_workbook (GnumFileSaver const *fs,
                                IOContext    *context,
                                WorkbookView *wb_view,
                                const gchar   *filename)
{
	int              size;
	xmlChar         *mem;
	xmlDocPtr        xml;
	XmlParseContext *ctxt;
	BonoboStorage   *storage;
	int              flags;

	g_return_if_fail (wb_view != NULL);
	g_return_if_fail (filename != NULL);

	flags = Bonobo_Storage_CREATE | Bonobo_Storage_WRITE |
		Bonobo_Storage_FAILIFEXIST;

#warning FIXME - needs grunt.
#ifdef GNOME2_CONVERSION_COMPLETE
	storage = bonobo_storage_open (BONOBO_IO_DRIVER_FS,
				       filename, flags, 0664);
#endif
	storage = NULL;

	if (!storage) {
		char *msg = g_strdup_printf ("Can't open '%s'", filename);
		gnumeric_io_error_save (context, msg);
		g_free (msg);
		return;
	}

	/*
	 * Create the tree
	 */
	xml = xmlNewDoc ((xmlChar *)"1.0");
	if (!xml) {
		gnumeric_io_error_save (context, "");
		bonobo_object_unref (BONOBO_OBJECT (storage));
		return;
	}
	ctxt = xml_parse_ctx_new_full (
		xml, NULL, wb_view, GNUM_XML_LATEST, NULL,
		gnumeric_bonobo_obj_write, storage);

	xml->xmlRootNode = xml_workbook_write (ctxt);
	xml_parse_ctx_destroy (ctxt);

	/*
	 * save the content to a temp buffer.
	 */
	xmlDocDumpMemory (xml, &mem, &size);

	if (mem) {
		CORBA_Environment ev;
		Bonobo_Stream stream;
		int flags;

		flags = Bonobo_Storage_CREATE |	Bonobo_Storage_WRITE |
			Bonobo_Storage_FAILIFEXIST;

		CORBA_exception_init (&ev);

		stream = Bonobo_Storage_openStream (
			BONOBO_OBJREF (storage),
			"Workbook", flags, &ev);
		if (ev._major == CORBA_USER_EXCEPTION &&
		    strcmp (ev._id, ex_Bonobo_Storage_NameExists)) {
			g_warning ("Workbook stream already exists");
			gnumeric_io_error_unknown (context);
		} else {
			if (!BONOBO_EX (&ev))
				bonobo_stream_client_write (stream, mem,
							    size, &ev);

			if (BONOBO_EX (&ev)) {
				gnumeric_io_error_save (context,
					"Error storing workbook stream");
			}
		}

		CORBA_exception_free (&ev);
		xmlFree (mem);
	}

	xmlFreeDoc (xml);
	bonobo_object_unref (BONOBO_OBJECT (storage));
}
#endif

#ifdef GNOME2_CONVERSION_COMPLETE
static void
gnumeric_bonobo_read_workbook (GnumFileOpener const *fo,
                               IOContext    *context,
                               WorkbookView *wb_view,
                               const char   *filename)
{
	CORBA_Environment   ev;
	xmlDoc             *doc;
	xmlNs              *gmr;
	XmlParseContext    *ctxt;
	BonoboStorage      *storage;
	Bonobo_Stream       stream;
	GnumericXMLVersion  version;

	g_return_if_fail (filename != NULL);

#warning FIXME - needs grunt.
#ifdef GNOME2_CONVERSION_COMPLETE
	storage = bonobo_storage_open (BONOBO_IO_DRIVER_FS,
				       filename, Bonobo_Storage_READ, 0);
#endif
	storage = NULL;

	if (!storage) {
		char *msg = g_strdup_printf ("Can't open '%s'", filename);
		gnumeric_io_error_save (context, msg);
		g_free (msg);
		return;
	}

	CORBA_exception_init (&ev);
	stream = Bonobo_Storage_openStream (
		BONOBO_OBJREF (storage),
		"Workbook", Bonobo_Storage_READ, &ev);

	if (BONOBO_EX (&ev) || stream == CORBA_OBJECT_NIL) {
		char *txt = g_strdup_printf (_("Error '%s' opening workbook stream"),
					     bonobo_exception_get_text (&ev));
		gnumeric_io_error_save (context, txt);
		g_free (txt);
		goto storage_err;
	}

	/*
	 * Load the file into an XML tree.
	 */
	doc = hack_xmlSAXParseFile (stream);
	if (!doc) {
		gnumeric_io_error_read (
			context, "Failed to parse file");
		goto storage_err;
	}
	if (!doc->xmlRootNode) {
		xmlFreeDoc (doc);
		gnumeric_io_error_read (
			context, _("Invalid xml file. Tree is empty ?"));
		goto storage_err;
	}

	/*
	 * Do a bit of checking, get the namespaces, and check the top elem.
	 */
	gmr = xml_check_version (doc, &version);
	if (!gmr) {
		xmlFreeDoc (doc);
		gnumeric_io_error_read (
			context, _("Does not contain a Workbook file"));
		goto storage_err;
	}

	ctxt = xml_parse_ctx_new_full (
		doc, gmr, wb_view, version,
		gnumeric_bonobo_obj_read,
		NULL, storage);

	xml_workbook_read (context, ctxt, doc->xmlRootNode);
	workbook_set_saveinfo (wb_view_workbook (wb_view),
	                       (char *) filename, FILE_FL_AUTO,
	                       gnumeric_bonobo_saver);

	xml_parse_ctx_destroy (ctxt);

	xmlFreeDoc (doc);
	bonobo_object_unref (BONOBO_OBJECT (storage));
	CORBA_exception_free (&ev);
	return;

 storage_err:
	bonobo_object_unref (BONOBO_OBJECT (storage));
	CORBA_exception_free (&ev);
	return;
}

static gboolean
gnumeric_bonobo_io_probe (GnumFileOpener const *fo, const char *filename,
                          FileProbeLevel pl)
{
	char *p;

	if (((p = strrchr (filename, '.')) &&
	     !g_strncasecmp (p + 1, "efs", 3)) ||
	    filename [strlen (filename) - 1] == '/') {
/*		g_warning ("I like '%s'", filename);*/
		return TRUE;
	} else {
/*		g_warning ("I don't like '%s'", filename);*/
		return FALSE;
	}
}

void
gnumeric_bonobo_io_init (void)
{
	char *desc = _("EXPERIMENTAL Bonobo EFS format");

	gnumeric_bonobo_opener = gnum_file_opener_new (
	                         NULL, desc, gnumeric_bonobo_io_probe,
	                         gnumeric_bonobo_read_workbook);
	gnumeric_bonobo_saver = gnum_file_saver_new (
	                        NULL, "efs", desc, FILE_FL_AUTO,
	                        gnumeric_bonobo_write_workbook);
	register_file_opener (gnumeric_bonobo_opener, 100);
	register_file_saver (gnumeric_bonobo_saver);
}
#endif
