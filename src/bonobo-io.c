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
#include "command-context.h"
#include "io-context.h"
#include "workbook-control-component.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "file.h"

#include <libgnome/gnome-i18n.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <math.h>
#include <limits.h>
#include <bonobo.h>

#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlIO.h>

#include <zlib.h>

#define GZIP_HEADER_SIZE 10
#define GZIP_MAGIC_1 0x1f
#define GZIP_MAGIC_2 0x8b
#define GZIP_FLAG_RESERVED     0xE0 /* bits 5..7: reserved */

#ifdef GNOME2_CONVERSION_COMPLETE
static GnumFileOpener *gnumeric_bonobo_opener;
static GnumFileSaver *gnumeric_bonobo_saver;
#endif

typedef struct {
	Bonobo_Stream        bstream;
	CORBA_Environment    *ev;
	char                 *infbuf;
	int                  infbsiz;
	gboolean             compressed;
	z_stream             zstream;
} StreamIOCtxt;

static int
get_raw_bytes_from_stream (StreamIOCtxt *sc, char *buffer, int len)
{
	Bonobo_Stream_iobuf *buf;
	int                  ret;

	g_return_val_if_fail (sc != NULL, -1);
		
	Bonobo_Stream_read (sc->bstream, len, &buf, sc->ev);
	if (BONOBO_EX (sc->ev))
		ret = -1;
	else {
		ret = buf->_length;
		memcpy (buffer, buf->_buffer, ret);
		CORBA_free (buf);
	}

	return ret;
}

static int
get_bytes_from_compressed_stream (StreamIOCtxt *sc, char *buffer, int len)
{
	int	 z_err;
	z_stream *zstream = &sc->zstream;

	zstream->next_out = (Bytef*) buffer;
	zstream->avail_out = len;

	while (zstream->avail_out != 0) {
		if (zstream->avail_in == 0) {
			zstream->avail_in =
				get_raw_bytes_from_stream (sc, sc->infbuf,
							   sc->infbsiz);
			if (zstream->avail_in < 0)
				return -1;
			zstream->next_in = sc->infbuf;
		}
		z_err = inflate(zstream, Z_NO_FLUSH);
		if (z_err == Z_STREAM_END)
			break;
		if (z_err != Z_OK)
			return -1;
	}
	return (int)(len - zstream->avail_out);
}

static int
get_bytes_from_stream (StreamIOCtxt *sc, char *buffer, int len)
{
	if (sc->compressed)
		return get_bytes_from_compressed_stream (sc, buffer, len);
	else 
		return get_raw_bytes_from_stream (sc, buffer, len);
}

static int
cleanup_stream (StreamIOCtxt *sc)
{
	if (sc->compressed)
		inflateEnd (&sc->zstream);
}

/* Returns true if a valid gzip header */
static gboolean
check_gzip_header (StreamIOCtxt *sc)
{
	unsigned char header [GZIP_HEADER_SIZE];
	int bytes_read;

	bytes_read = get_raw_bytes_from_stream (sc, header, GZIP_HEADER_SIZE);
	if (bytes_read < GZIP_HEADER_SIZE)
		return FALSE;

	if (header[0] != GZIP_MAGIC_1 || header[1] != GZIP_MAGIC_2)
		return FALSE;
	if (header[2] != Z_DEFLATED || (header[3] & GZIP_FLAG_RESERVED) != 0)
		return FALSE;

	/* This is indeed gzipped data. Ignore the rest of the header */
	return TRUE;
}

static gboolean
init_for_inflate (StreamIOCtxt *sc, char *infbuf, int len)
{
	sc->infbuf = infbuf;
	sc->zstream.next_in = infbuf;
	sc->zstream.avail_in = 0;
	sc->zstream.next_out = 0;
	sc->zstream.avail_out = 0;
	sc->infbsiz = sizeof infbuf;
	sc->zstream.zalloc = NULL;
	sc->zstream.zfree  = NULL;
	sc->zstream.opaque = NULL;
		
	if (inflateInit2 (&sc->zstream, -MAX_WBITS) != Z_OK) {
		g_warning ("zlib initialization error");
		return FALSE;
	}

	return TRUE;
}

static xmlDocPtr
hack_xmlSAXParseFile (StreamIOCtxt *sc)
{
	xmlDocPtr ret;
	xmlParserCtxtPtr xc;

	xc = xmlCreateIOParserCtxt (
		NULL, NULL,
		(xmlInputReadCallback) get_bytes_from_stream, 
		(xmlInputCloseCallback) cleanup_stream,
		sc, XML_CHAR_ENCODING_NONE);
	if (!xc)
		return NULL;

	xmlParseDocument (xc);

	if (xc->wellFormed)
		ret = xc->myDoc;
	else {
		ret = NULL;
		xmlFreeDoc (xc->myDoc);
		xc->myDoc = NULL;
	}
	xmlFreeParserCtxt (xc);

	return ret;
}

void
gnumeric_bonobo_read_from_stream (BonoboPersistStream       *ps,
				  Bonobo_Stream              stream,
				  Bonobo_Persist_ContentType type,
				  void                      *data,
				  CORBA_Environment         *ev)
{
	WorkbookControl *wbc;
	WorkbookView       *wb_view;
	Workbook           *wb;
	Workbook           *old_wb;
	xmlDoc             *doc;
	xmlNs              *gmr;
	GnumericXMLVersion  version;
	StreamIOCtxt       sc;
	XmlParseContext    *xc;
	CommandContext     *cc;
	IOContext     *ioc;
	char           infbuf[16384];

	g_return_if_fail (data != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_COMPONENT (data));
	wbc  = WORKBOOK_CONTROL (data);
	
	/* FIXME: Probe for file type */
	
	CORBA_exception_init (ev);

	old_wb = wb_control_workbook (wbc);
	if (!workbook_is_pristine (old_wb)) {
		/* No way to interact properly with user */
		g_warning ("Old workbook has unsaved changes.");
		goto exit_error;
	}
		
	wb_view = wb_control_view (wbc);

	sc.bstream = stream;
	sc.ev     = ev;
	sc.compressed = check_gzip_header (&sc);

	if (sc.compressed) {
		if (!init_for_inflate (&sc, infbuf, sizeof infbuf))
					goto exit_error;
	} else {
		Bonobo_Stream_seek (stream, 0, Bonobo_Stream_SeekSet, ev);
		if (BONOBO_EX (ev))
			goto exit_error;
	}
	
	/*
	 * Load the stream into an XML tree.
	 */
	doc = hack_xmlSAXParseFile (&sc);
	if (!doc) {
		g_warning ("Failed to parse file");
		goto exit_error;
	}
	if (!doc->xmlRootNode) {
		xmlFreeDoc (doc);
		g_warning ("Invalid xml file. Tree is empty ?");
		goto exit_error;
	}
	/*
	 * Do a bit of checking, get the namespaces, and check the top elem.
	 */
	gmr = xml_check_version (doc, &version);
	if (!gmr) {
		xmlFreeDoc (doc);
		goto exit_error;
	}
	xc = xml_parse_ctx_new_full (doc, gmr, version, NULL, NULL, NULL);
	ioc = gnumeric_io_context_new (COMMAND_CONTEXT (wbc));
	xml_workbook_read (ioc, wb_view, xc, doc->xmlRootNode);
	
	xml_parse_ctx_destroy (xc);
	xmlFreeDoc (doc);

	if (gnumeric_io_error_occurred (ioc)) {
		g_object_unref (G_OBJECT (ioc));
		goto exit_error;
	}	

	g_object_unref (G_OBJECT (ioc));
	return;

exit_error:
	/* Propagate exceptions which are in the PersistStream interface */
	if (BONOBO_EX (ev)) {
		if (BONOBO_USER_EX (ev, ex_Bonobo_Persist_WrongDataType) ||
		    BONOBO_USER_EX (ev, ex_Bonobo_NotSupported) ||
		    BONOBO_USER_EX (ev, ex_Bonobo_IOError) ||
		    BONOBO_USER_EX (ev, ex_Bonobo_Persist_FileNotFound))
			return;

		CORBA_exception_free (ev);
	}

	/* This may be a bad exception to throw, but they're all bad */
	CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
			     ex_Bonobo_Persist_WrongDataType, NULL);
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
		xml, NULL, GNUM_XML_LATEST, NULL,
		gnumeric_bonobo_obj_write, storage);

	xml->xmlRootNode = xml_workbook_write (ctxt, wb_view);
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
		doc, gmr, version,
		gnumeric_bonobo_obj_read,
		NULL, storage);

	xml_workbook_read (context, wb_view, ctxt, doc->xmlRootNode);
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
