/*
 * xml-io.h: interfaces to save/read gnumeric Sheets using an XML encoding.
 *
 * Daniel Veillard <Daniel.Veillard@w3.org>
 *
 * $Id$
 */

#ifndef GNUMERIC_XML_IO_H
#define GNUMERIC_XML_IO_H

#include <gdk/gdktypes.h>
#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-stream.h>
#endif
#include "gnumeric.h"
#include "file.h"
#include <gnome-xml/tree.h>
#include <gnome-xml/xmlmemory.h>

typedef enum
{
    GNUM_XML_V1,
    GNUM_XML_V2,
    GNUM_XML_V3,	/* >= 0.52 */
    GNUM_XML_V4,	/* >= 0.57 */
    GNUM_XML_V5,	/* >= 0.58 */
    GNUM_XML_V6,	/* >= 0.62 */
    GNUM_XML_V7,        /* >= 0.66 */
    
    /* NOTE : Keep this up to date */
    GNUM_XML_LATEST = GNUM_XML_V7
} GnumericXMLVersion;

typedef struct _XmlParseContext XmlParseContext;

typedef gboolean (*XmlSheetObjectWriteFn) (xmlNodePtr   cur,
					   SheetObject const *object,
					   gpointer     user_data);
typedef gboolean (*XmlSheetObjectReadFn)  (xmlNodePtr   tree,
					   SheetObject *object,
					   Sheet       *sheet,
					   gpointer     user_data);

struct _XmlParseContext {
	xmlDocPtr doc;		/* Xml document */
	xmlNsPtr ns;		/* Main name space */
	xmlNodePtr parent;	/* used only for g_hash_table_foreach callbacks */
	Sheet *sheet;		/* the associated sheet */
	Workbook *wb;		/* the associated workbook */
	IOContext *io_context;
	GHashTable *style_table;/* old style styles compatibility */
	GHashTable *expr_map;	/*
				 * Emitted expressions with ref count > 1
				 * When writing this is map from expr pointer -> index
				 * when reading this is a map from index -> expr pointer
				 */
	XmlSheetObjectWriteFn write_fn;
	XmlSheetObjectReadFn  read_fn;
	gpointer              user_data;
	GnumericXMLVersion    version;
};

GnumFileOpener *gnumeric_xml_get_opener (void);
GnumFileSaver  *gnumeric_xml_get_saver (void);

void gnumeric_xml_read_workbook (GnumFileOpener const *fo, IOContext *context,
                                 WorkbookView *wbv, gchar const *filename);
void gnumeric_xml_write_workbook (GnumFileSaver const *fs, IOContext *context,
                                  WorkbookView *wbv, gchar const *filename);
#ifdef ENABLE_BONOBO
void gnumeric_xml_write_workbook_to_stream (GnumFileSaver const *fs, 
		                            IOContext *context,
					    WorkbookView *wbv,
					    BonoboStream *stream, 
					    CORBA_Environment *ev);
#endif

XmlParseContext *xml_parse_ctx_new      (xmlDocPtr             doc,
					 xmlNsPtr              ns);
XmlParseContext *xml_parse_ctx_new_full (xmlDocPtr             doc,
					 xmlNsPtr              ns,
					 GnumericXMLVersion    version,
					 XmlSheetObjectReadFn  read_fn,
					 XmlSheetObjectWriteFn write_fn,
					 gpointer              user_data);
void             xml_parse_ctx_destroy  (XmlParseContext      *ctxt);

xmlNodePtr       xml_workbook_write     (XmlParseContext      *ctx,
					 WorkbookView         *wbv);
gboolean         xml_workbook_read      (IOContext            *context,
					 WorkbookView	      *new_wb,
					 XmlParseContext      *ctx,
					 xmlNodePtr           tree);

xmlNsPtr         xml_check_version      (xmlDocPtr            doc,
					 GnumericXMLVersion  *version);

int        gnumeric_xml_write_selection_clipboard (WorkbookControl *context, Sheet *sheet,
						   xmlChar **buffer, int *size);
int        gnumeric_xml_read_selection_clipboard  (WorkbookControl *context, CellRegion **cr,
						   xmlChar *buffer);
/*
 * Some utility routines for setting properties
 */
xmlChar *xml_prop_get_cstr	(xmlNodePtr node, char const *name);
void	 xml_prop_set_cstr	(xmlNodePtr node, char const *name, char const *val);
gboolean xml_prop_get_int	(xmlNodePtr node, char const *name, int *val);
void     xml_prop_set_int	(xmlNodePtr node, char const *name, int val);
gboolean xml_prop_get_double	(xmlNodePtr node, char const *name, double *val);
void     xml_prop_set_double	(xmlNodePtr node, char const *name, double val, int precision);
GdkColor *xml_prop_get_gdkcolor (xmlNodePtr node, char const *name);
void      xml_prop_set_gdkcolor (xmlNodePtr node, char const *name,
			         GdkColor const *color);


xmlNodePtr   xml_write_style    (XmlParseContext *ctxt, MStyle *style);
MStyle      *xml_read_style     (XmlParseContext *ctxt, xmlNodePtr tree);

void      xml_init (void);

#endif /* GNUMERIC_XML_IO_H */
