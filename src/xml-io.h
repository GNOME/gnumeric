#ifndef GNUMERIC_XML_IO_H
#define GNUMERIC_XML_IO_H

#include <gdk/gdktypes.h>
#include "gnumeric.h"
#include "xml-io-version.h"
#include "file.h"
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

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
	GnumericXMLVersion    version;
};

XmlParseContext *xml_parse_ctx_new     (xmlDoc		*doc,
				        xmlNs		*ns,
				        WorkbookView	*wb_view);
void		 xml_parse_ctx_destroy (XmlParseContext *ctxt);


xmlNodePtr   xml_write_style    (XmlParseContext *ctxt, MStyle *style);

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

GnumFileOpener *gnumeric_xml_get_opener (void);
GnumFileSaver  *gnumeric_xml_get_saver (void);

#endif /* GNUMERIC_XML_IO_H */
