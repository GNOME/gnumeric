#ifndef _GNM_XML_SAX_H_
# define _GNM_XML_SAX_H_

#include <gnumeric.h>
#include <gsf/gsf-output-memory.h>

G_BEGIN_DECLS

/* Gnumeric specific SAX utilities */
void gnm_xml_out_add_gocolor (GsfXMLOut *o, char const *id, GOColor c);
gboolean gnm_xml_attr_int     (xmlChar const * const *attrs,
			       char const *name, int * res);
gboolean gnm_xml_attr_double  (xmlChar const * const *attrs,
			       char const *name, double * res);
gboolean gnm_xml_attr_bool    (xmlChar const * const *attrs,
			       char const *name, gboolean *res);

SheetObject *gnm_xml_in_cur_obj   (GsfXMLIn const *xin);
Sheet	    *gnm_xml_in_cur_sheet (GsfXMLIn const *xin);


GsfOutputMemory *gnm_cellregion_to_xml (GnmCellRegion const *cr);

GnmCellRegion *gnm_xml_cellregion_read (WorkbookControl *wbc,
				    GOIOContext *io_context,
				    Sheet *sheet,
				    const char *buffer, int length);

typedef void (*GnmXmlStyleHandler) (GsfXMLIn *xin,
				    GnmStyle *style,
				    gpointer user);
void      gnm_xml_prep_style_parser (GsfXMLIn *xin,
				     xmlChar const **attrs,
				     GnmXmlStyleHandler handler,
				     gpointer user);

void      gnm_xml_sax_read_init (void);
void      gnm_xml_sax_read_shutdown (void);

void      gnm_xml_sax_write_init (void);
void      gnm_xml_sax_write_shutdown (void);

GnmConventions *gnm_xml_io_conventions (void);

G_END_DECLS

#endif /* _GNM_XML_SAX_H_ */
