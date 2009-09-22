/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_XML_IO_H_
# define _GNM_XML_IO_H_

#include <gnumeric.h>
#include <xml-io-version.h>
#include <goffice/goffice.h>
#include <gsf/gsf-libxml.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

G_BEGIN_DECLS

struct _XmlParseContext {
	xmlDocPtr doc;		/* Xml document */
	xmlNsPtr  ns;		/* Main name space */

	Sheet	     *sheet;	/* the associated sheet */
	Workbook     *wb;	/* the associated workbook */
	WorkbookView *wb_view;
	GOIOContext    *io_context;

	GHashTable *style_table;/* old style styles compatibility */
	GHashTable *expr_map;	/*
				 * Emitted expressions with ref count > 1
				 * When writing this is map from expr pointer -> index
				 */
	GPtrArray *shared_exprs;/*
				 * When reading this is a map from index -> expr pointer
				 */
	GnumericXMLVersion    version;

	GnmConventions *convs;
};

XmlParseContext *xml_parse_ctx_new     (xmlDoc		*doc,
				        xmlNs		*ns,
				        WorkbookView	*wb_view);
void		 xml_parse_ctx_destroy (XmlParseContext *ctxt);


GnmCellRegion *xml_cellregion_read  (WorkbookControl *context, Sheet *sheet,
				     const char *buffer, int length);

GnmStyle      *xml_read_style     (XmlParseContext *ctxt, xmlNodePtr tree,
				   gboolean leave_empty);

void      xml_dom_init (void);

G_END_DECLS

#endif /* _GNM_XML_IO_H_ */
