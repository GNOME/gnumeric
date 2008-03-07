/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_XML_IO_H_
# define _GNM_XML_IO_H_

#include <gnumeric.h>
#include <xml-io-version.h>
#include <goffice/utils/goffice-utils.h>
#include <goffice/app/file.h>
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

	GnmConventions *convs;
};

XmlParseContext *xml_parse_ctx_new     (xmlDoc		*doc,
				        xmlNs		*ns,
				        WorkbookView	*wb_view);
void		 xml_parse_ctx_destroy (XmlParseContext *ctxt);


GnmCellRegion *xml_cellregion_read  (WorkbookControl *context, Sheet *sheet,
				     const char *buffer, int length);

GnmColor  *xml_node_get_color	(xmlNodePtr node, char const *name);
void       xml_node_set_color	(xmlNodePtr node, char const *name, GnmColor const *color);

GnmStyle      *xml_read_style     (XmlParseContext *ctxt, xmlNodePtr tree,
				   gboolean leave_empty);

void      xml_init (void);
/* Gnumeric specific SAX utilities */
void gnm_xml_out_add_color   (GsfXMLOut *o, char const *id, GnmColor const *c);
void gnm_xml_out_add_gocolor (GsfXMLOut *o, char const *id, GOColor c);
void gnm_xml_out_add_cellpos (GsfXMLOut *o, char const *id, GnmCellPos const *p);

/* Gnumeric specific SAX import */
gboolean gnm_xml_attr_int     (xmlChar const * const *attrs,
			       char const *name, int * res);
gboolean gnm_xml_attr_double  (xmlChar const * const *attrs,
			       char const *name, double * res);
gboolean gnm_xml_attr_bool    (xmlChar const * const *attrs,
			       char const *name, gboolean *res);

SheetObject *gnm_xml_in_cur_obj   (GsfXMLIn const *xin);
Sheet	    *gnm_xml_in_cur_sheet (GsfXMLIn const *xin);

GnmConventions *gnm_xml_io_conventions (void);

G_END_DECLS

#endif /* _GNM_XML_IO_H_ */
