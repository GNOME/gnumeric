#ifndef GNUMERIC_XML_IO_H
#define GNUMERIC_XML_IO_H

#include <gdk/gdktypes.h>
#ifdef WITH_BONOBO
#include <bonobo/bonobo-stream.h>
#endif
#include "gnumeric.h"
#include "xml-io-version.h"
#include "file.h"
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

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
	xmlNsPtr  ns;		/* Main name space */

	Sheet	     *sheet;	/* the associated sheet */
	Workbook     *wb;	/* the associated workbook */
	WorkbookView *wb_view;
	IOContext    *io_context;

	GHashTable *style_table;/* old style styles compatibility */
	GHashTable *expr_map;	/*
				 * Emitted expressions with ref count > 1
				 * When writing this is map from expr pointer -> index
				 */
	GPtrArray *shared_exprs;/*
				 * When reading this is a map from index -> expr pointer
				 */
	XmlSheetObjectWriteFn write_fn;
	XmlSheetObjectReadFn  read_fn;
	gpointer              user_data;
	GnumericXMLVersion    version;
};

GnumFileOpener *gnumeric_xml_get_opener (void);
GnumFileSaver  *gnumeric_xml_get_saver (void);

void gnumeric_xml_write_workbook (GnumFileSaver const *fs, IOContext *context,
                                  WorkbookView *wbv, gchar const *filename);

XmlParseContext *xml_parse_ctx_new      (xmlDocPtr             doc,
					 xmlNsPtr              ns,
					 WorkbookView	      *wb_view);
XmlParseContext *xml_parse_ctx_new_full (xmlDocPtr             doc,
					 xmlNsPtr              ns,
					 WorkbookView	      *wb_view,
					 GnumericXMLVersion    version,
					 XmlSheetObjectReadFn  read_fn,
					 XmlSheetObjectWriteFn write_fn,
					 gpointer              user_data);
void             xml_parse_ctx_destroy  (XmlParseContext      *ctxt);

xmlNodePtr       xml_workbook_write     (XmlParseContext      *ctx);
gboolean         xml_workbook_read      (IOContext            *context,
					 XmlParseContext      *ctx,
					 xmlNodePtr           tree);

xmlNsPtr         xml_check_version      (xmlDocPtr            doc,
					 GnumericXMLVersion  *version);

xmlChar	   *xml_cellregion_write (WorkbookControl *context,
				  CellRegion *cr, int *size);
CellRegion *xml_cellregion_read  (WorkbookControl *context, Sheet *sheet,
				  guchar *buffer, int length);

/* Some utility routines for setting attributes or content */
xmlChar   *xml_node_get_cstr	(xmlNodePtr node, char const *name);
void	   xml_node_set_cstr	(xmlNodePtr node, char const *name, char const *val);
gboolean   xml_node_get_int	(xmlNodePtr node, char const *name, int *val);
void       xml_node_set_int	(xmlNodePtr node, char const *name, int val);
gboolean   xml_node_get_double	(xmlNodePtr node, char const *name, double *val);
void       xml_node_set_double	(xmlNodePtr node, char const *name, double val, int precision);
StyleColor*xml_node_get_color	(xmlNodePtr node, char const *name);
void       xml_node_set_color	(xmlNodePtr node, char const *name, StyleColor const *color);

xmlNodePtr   xml_write_style    (XmlParseContext *ctxt, MStyle *style);
MStyle      *xml_read_style     (XmlParseContext *ctxt, xmlNodePtr tree);

void      xml_init (void);

#endif /* GNUMERIC_XML_IO_H */
