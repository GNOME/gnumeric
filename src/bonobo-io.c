/*
 * bonobo-io.c: Workbook IO using Bonobo storages.
 *
 * Author:
 *   Michael Meeks <michael@helixcode.com>
 *
 * Copyright 2000, Helix Code, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include <locale.h>
#include <math.h>
#include <limits.h>
#include <bonobo.h>

#include "gnumeric.h"
#include "gnome-xml/parser.h"
#include "gnome-xml/parserInternals.h"
#include "gnome-xml/xmlmemory.h"
#include "gnome-xml/xmlIO.h"
#include "sheet-object-bonobo.h"
#include "sheet-object-container.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "file.h"
#include "xml-io.h"
#include "bonobo-io.h"

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
		stream = Bonobo_Storage_open_stream (storage, name, flags, ev);
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

	Bonobo_Unknown_unref ((Bonobo_Unknown) stream, ev);
	CORBA_Object_release (stream, ev);
}

static gboolean
gnumeric_bonobo_obj_write (xmlNodePtr   cur,
			   SheetObject *object,
			   gpointer     user_data)
{
	CORBA_Environment    ev;
	SheetObjectBonobo   *sob;
	Bonobo_PersistStream ps;
	Bonobo_Storage       storage;
	gboolean             ret = TRUE;

	g_return_val_if_fail (user_data != NULL, FALSE);
	storage = bonobo_object_corba_objref (user_data);
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
	
	ps = Bonobo_Unknown_query_interface (
		bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
		"IDL:Bonobo/PersistStream:1.0", &ev);

	if (!BONOBO_EX (&ev) && ps != CORBA_OBJECT_NIL) {
		write_stream_to_storage (cur, ps, storage, &ev);

		Bonobo_Unknown_unref ((Bonobo_Unknown) ps, &ev);
		CORBA_Object_release (ps, &ev);
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

	ps = Bonobo_Unknown_query_interface (object, "IDL:Bonobo/PersistStream:1.0", ev);

	if (BONOBO_EX (ev) || ps == CORBA_OBJECT_NIL) {
		g_warning ("Wierd, component used to have a PersistStream interface");
		return;
	}

	stream = Bonobo_Storage_open_stream (storage, sname, Bonobo_Storage_READ, ev);
	if (ev->_major != CORBA_NO_EXCEPTION) {
		g_warning ("Can't open stream '%s'", sname);
		return;
	}

	/*
	 * We need to restore the type too.
	 */
	Bonobo_PersistStream_load (ps, stream, "", ev);
	if (ev->_major != CORBA_NO_EXCEPTION)
		return;

	Bonobo_Unknown_unref ((Bonobo_Unknown) stream, ev);
	CORBA_Object_release (stream, ev);

	Bonobo_Unknown_unref ((Bonobo_Unknown) ps, ev);
	CORBA_Object_release (ps, ev);
}

static SheetObject *
gnumeric_bonobo_obj_read (xmlNodePtr   tree,
			  Sheet       *sheet,
			  double       x1,
			  double       y1,
			  double       x2,
			  double       y2,
			  gpointer     user_data)
{
	CORBA_Environment    ev;
	SheetObject         *so;
	SheetObjectBonobo   *sob;
	Bonobo_Storage       storage;
	char                *object_id, *sname;

	g_return_val_if_fail (tree != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (user_data != NULL, FALSE);
	storage = bonobo_object_corba_objref (user_data);
	g_return_val_if_fail (storage != CORBA_OBJECT_NIL, FALSE);

	object_id = xmlGetProp (tree, "OAFIID");
	if (!object_id) {
		g_warning ("Malformed object, error in save; no id");
		return NULL;
	}

	so = sheet_object_container_new_object (sheet, object_id);
	sob = SHEET_OBJECT_BONOBO (so);
	if (!sob) {
		g_warning ("Error activating '%s'", object_id);
		return NULL;
	}

	CORBA_exception_init (&ev);

	sname = xmlGetProp (tree, "Stream");
	if (sname)
		read_stream_from_storage (
			bonobo_object_corba_objref (BONOBO_OBJECT (sob->object_server)),
			storage, sname, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		gtk_object_unref (GTK_OBJECT (sob));
		CORBA_exception_free (&ev);
		return NULL;
	}

	/*
	 * If it is a complex object / container it will need to 
	 * serialize to a Storage;
	 * this needs implementing
	 * xml_set_value_string (cur, "Storage", ###);
	 */
	CORBA_exception_free (&ev);
	sheet_object_realize (SHEET_OBJECT (sob));

	return SHEET_OBJECT (sob);
}

static int
gnumeric_bonobo_write_workbook (IOContext *context, WorkbookView *wb_view,
				const char *filename)
{
	int              size, ret;
	xmlChar         *mem;
	xmlDocPtr        xml;
	XmlParseContext *ctxt;
	BonoboStorage   *storage;
	int              flags;

	g_return_val_if_fail (wb_view != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	flags = Bonobo_Storage_CREATE | Bonobo_Storage_READ |
		Bonobo_Storage_WRITE | Bonobo_Storage_FAILIFEXIST;

	storage = bonobo_storage_open (BONOBO_IO_DRIVER_EFS,
				       filename, flags, 0);

	if (!storage) {
		char *msg = g_strdup_printf ("Can't open '%s'", filename);
		gnumeric_io_error_save (context, msg);
		g_free (msg);
		return -1;
	}

	/*
	 * Create the tree
	 */
	xml = xmlNewDoc ("1.0");
	if (!xml) {
		gnumeric_io_error_save (context, "");
		bonobo_object_unref (BONOBO_OBJECT (storage));
		return -1;
	}
	ctxt = xml_parse_ctx_new_full (xml, NULL, NULL, gnumeric_bonobo_obj_write,
				       storage);
	xml->root = xml_workbook_write (ctxt, wb_view);
	xml_parse_ctx_destroy (ctxt);

	/* 
	 * save the content to a temp buffer.
	 */
	xmlDocDumpMemory (xml, &mem, &size);

	ret = 0;
	if (mem) {
		CORBA_Environment ev;
		Bonobo_Stream stream;
		int flags;
		
		flags = Bonobo_Storage_CREATE |	Bonobo_Storage_WRITE |
			Bonobo_Storage_FAILIFEXIST;

		CORBA_exception_init (&ev);
	
		stream = Bonobo_Storage_open_stream (
			bonobo_object_corba_objref (BONOBO_OBJECT (storage)),
			"Workbook", flags, &ev);
		if (ev._major == CORBA_USER_EXCEPTION &&
		    strcmp (ev._repo_id, ex_Bonobo_Storage_NameExists)) {
			g_warning ("Workbook stream already exists");
			ret = -1;
		} else {
			if (!BONOBO_EX (&ev))
				bonobo_stream_client_write (stream, mem,
							    size, &ev);
		
			if (BONOBO_EX (&ev)) {
				gnumeric_io_error_save (context,
					"Error storing workbook stream");
				ret = -1;
			}
		}

		CORBA_exception_free (&ev);
		xmlFree (mem);
	}

	xmlFreeDoc (xml);
	bonobo_object_unref (BONOBO_OBJECT (storage));

	return ret;
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

static int
gnumeric_bonobo_read_workbook (IOContext *context, WorkbookView *wb_view,
			       const char *filename)
{
	CORBA_Environment ev;
	xmlDocPtr         res;
	xmlNsPtr          gmr;
	XmlParseContext  *ctxt;
	BonoboStorage    *storage;
	Bonobo_Stream     stream;

	g_return_val_if_fail (filename != NULL, -1);

	storage = bonobo_storage_open (BONOBO_IO_DRIVER_EFS,
				       filename, Bonobo_Storage_READ, 0);
	if (!storage) {
		char *msg = g_strdup_printf ("Can't open '%s'", filename);
		gnumeric_io_error_save (context, msg);
		g_free (msg);
		return -1;
	}

	CORBA_exception_init (&ev);
	stream = Bonobo_Storage_open_stream (
		bonobo_object_corba_objref (BONOBO_OBJECT (storage)),
		"Workbook", Bonobo_Storage_READ, &ev);

	if (BONOBO_EX (&ev) || stream == CORBA_OBJECT_NIL) {
		gnumeric_io_error_save (context, "Error opening workbook stream");
		goto storage_err;
	}

	/*
	 * Load the file into an XML tree.
	 */
	res = hack_xmlSAXParseFile (stream);
	if (!res) {
		gnumeric_io_error_read (context, "Failed to parse file");
		goto storage_err;
	}
	if (!res->root) {
		xmlFreeDoc (res);
		gnumeric_io_error_read (context,
				     _("Invalid xml file. Tree is empty ?"));
		goto storage_err;
	}

	/*
	 * Do a bit of checking, get the namespaces, and check the top elem.
	 */
	gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/v3");
	if (gmr == NULL)
		gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/v2");
	if (gmr == NULL)
		gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/");
	if (strcmp (res->root->name, "Workbook") || (gmr == NULL)) {
		xmlFreeDoc (res);
		gnumeric_io_error_read (context,
				     _("Is not an Workbook file"));
		goto storage_err;
	}

	ctxt = xml_parse_ctx_new_full (res, gmr, gnumeric_bonobo_obj_read, NULL,
				       storage);

	xml_workbook_read (context, wb_view, ctxt, res->root);
	workbook_set_saveinfo (wb_view_workbook (wb_view),
			       (char *) filename, FILE_FL_AUTO,
			       gnumeric_bonobo_write_workbook);

	xml_parse_ctx_destroy (ctxt);

	xmlFreeDoc (res);
	bonobo_object_unref (BONOBO_OBJECT (storage));
	CORBA_exception_free (&ev);
	return 0;

 storage_err:
	bonobo_object_unref (BONOBO_OBJECT (storage));
	CORBA_exception_free (&ev);
	return -1;
}

static gboolean
gnumeric_bonobo_io_probe (const char *filename)
{
	char *p;

	if ((p = strrchr (filename, '.')) &&
	    !g_strncasecmp (p + 1, "efs", 3))
		return TRUE;
	else
		return FALSE;
}

void
gnumeric_bonobo_io_init (void)
{
	char *desc = _("Gnumeric Bonobo file format");

	file_format_register_open (100, desc, gnumeric_bonobo_io_probe,
				   gnumeric_bonobo_read_workbook);
	file_format_register_save ("", desc, FILE_FL_AUTO,
				   gnumeric_bonobo_write_workbook);
}
