/*
 * bonobo-io.c: Workbook IO using Bonobo storages.
 *
 * Author:
 *   Michael Meeks <michael@helixcode.com>
 *
 * Copyright 2000, Helix Code, Inc.
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "bonobo-io.h"

#include "sheet-object-bonobo.h"
#include "sheet-object-container.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "file.h"

#include <libgnome/gnome-i18n.h>
#include <stdio.h>
#include <locale.h>
#include <math.h>
#include <limits.h>
#include <bonobo.h>

#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlIO.h>

static GnumFileOpener *gnumeric_bonobo_opener;
static GnumFileSaver *gnumeric_bonobo_saver;

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
		    strcmp (ev->_repo_id, ex_Bonobo_Storage_NameExists)) {
			CORBA_exception_free (ev);
			loop = TRUE;
		}
	} while (loop);

	if (BONOBO_EX (ev)) {
		g_free (name);
		return;
	}

	xmlSetProp (cur, "Stream", name);
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
		BONOBO_OBJREF (sob->object_server),
		"IDL:Bonobo/PersistStream:1.0", &ev);

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
		xmlSetProp (cur, "OAFIID", sheet_object_bonobo_get_object_iid (sob));

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
	char                *object_id, *sname;

	g_return_val_if_fail (tree != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (user_data != NULL, TRUE);

	sob = SHEET_OBJECT_BONOBO (so);
	g_return_val_if_fail (sob != NULL, TRUE);

	object_id = xmlGetProp (tree, "OAFIID");
	if (!object_id) {
		g_warning ("Malformed object, error in save; no id");
		return TRUE;
	}

	storage = BONOBO_OBJREF (user_data);
	g_return_val_if_fail (storage != CORBA_OBJECT_NIL, TRUE);

	CORBA_exception_init (&ev);

	sname = xmlGetProp (tree, "Stream");
	if (sname)
		read_stream_from_storage (
			BONOBO_OBJREF (sob->object_server),
			storage, sname, &ev);
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

	storage = bonobo_storage_open (BONOBO_IO_DRIVER_FS,
				       filename, flags, 0664);

	if (!storage) {
		char *msg = g_strdup_printf ("Can't open '%s'", filename);
		gnumeric_io_error_save (context, msg);
		g_free (msg);
		return;
	}

	/*
	 * Create the tree
	 */
	xml = xmlNewDoc ("1.0");
	if (!xml) {
		gnumeric_io_error_save (context, "");
		bonobo_object_unref (BONOBO_OBJECT (storage));
		return;
	}
	ctxt = xml_parse_ctx_new_full (
		xml, NULL, GNUM_XML_LATEST, NULL,
		gnumeric_bonobo_obj_write, storage);

	xml->root = xml_workbook_write (ctxt, wb_view);
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
		    strcmp (ev._repo_id, ex_Bonobo_Storage_NameExists)) {
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

#ifdef HAVE_LIBXML_2
static int
xml_input_read_cb (void *context, char *buffer, int len)
{
	Bonobo_Stream_iobuf *buf;
	Bonobo_Stream        stream = context;
	CORBA_Environment    ev;
	int                  ret;

	CORBA_exception_init (&ev);

	Bonobo_Stream_read (stream, len, &buf, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		ret = -1;
	else {
		ret = buf->_length;
		memcpy (buffer, buf, ret);
		CORBA_free (buf);
	}

	CORBA_exception_free (&ev);

	return ret;
}

static void
xml_input_close_cb (void * context)
{
	CORBA_Environment ev;

	CORBA_Object_release (context, &ev);
}
#endif

static xmlDocPtr
hack_xmlSAXParseFile (Bonobo_Stream stream)
{
#ifdef HAVE_LIBXML_2
	xmlDocPtr ret;
	xmlParserCtxtPtr ctxt;

	ctxt = xmlCreateIOParserCtxt (NULL, NULL,
				      xml_input_read_cb,
				      xml_input_close_cb,
				      stream, XML_CHAR_ENCODING_NONE);
	if (!ctxt)
		return NULL;

	xmlParseDocument (ctxt);

	if (ctxt->wellFormed)
		ret = ctxt->myDoc;
	else {
		ret = NULL;
		xmlFreeDoc (ctxt->myDoc);
		ctxt->myDoc = NULL;
	}
	xmlFreeParserCtxt (ctxt);

	return ret;
#else
	xmlDocPtr ret;
	xmlParserCtxtPtr ctxt;
       	Bonobo_Stream_iobuf *buf;
	CORBA_Environment    ev;
	int                  len = 0;

	ctxt = xmlCreatePushParserCtxt (NULL, NULL, NULL, 0, NULL);

	CORBA_exception_init (&ev);

	do {
		Bonobo_Stream_read (stream, 65536, &buf, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			CORBA_exception_free (&ev);
			g_warning ("Leak bits of tree everywhere");
			return NULL;
		} else {
			len = buf->_length;
			if (xmlParseChunk (ctxt, buf->_buffer, len, (len == 0))) {
				g_warning ("Leak bits of tree everywhere");
				return NULL;
			}
			CORBA_free (buf);
		}
	} while (len > 0);

	CORBA_exception_free (&ev);

	if (ctxt->wellFormed)
		ret = ctxt->myDoc;
	else {
		ret = NULL;
		xmlFreeDoc (ctxt->myDoc);
		ctxt->myDoc = NULL;
	}
	xmlFreeParserCtxt (ctxt);

	return ret;
#endif /* HAVE_LIBXML_2*/
}

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

	storage = bonobo_storage_open (BONOBO_IO_DRIVER_FS,
				       filename, Bonobo_Storage_READ, 0);
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
	if (!doc->root) {
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
		doc, gmr, version,
		gnumeric_bonobo_obj_read,
		NULL, storage);

	xml_workbook_read (context, wb_view, ctxt, doc->root);
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
