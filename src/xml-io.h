/*
 * xml-io.h: interfaces to save/read gnumeric Sheets using an XML encoding.
 *
 * Daniel Veillard <Daniel.Veillard@w3.org>
 *
 * $Id$
 */

#ifndef GNUMERIC_XML_IO_H
#define GNUMERIC_XML_IO_H

#include "sheet.h"
#include "sheet-object.h"
#include "gnome-xml/tree.h"

typedef struct _XmlParseContext XmlParseContext;

typedef gboolean     (*XmlSheetObjectWriteFn) (xmlNodePtr   cur,
					       SheetObject *object,
					       gpointer     user_data);
typedef SheetObject *(*XmlSheetObjectReadFn)  (xmlNodePtr   tree,
					       Sheet       *sheet,
					       double       x1,
					       double       y1,
					       double       x2,
					       double       y2,
					       gpointer     user_data);

int        gnumeric_xml_read_workbook   (CommandContext *context, Workbook *wb,
					 const char *filename);
int        gnumeric_xml_write_workbook  (CommandContext *context, Workbook *wb,
					 const char *filename);

XmlParseContext *xml_parse_ctx_new      (xmlDocPtr             doc,
					 xmlNsPtr              ns);
XmlParseContext *xml_parse_ctx_new_full (xmlDocPtr             doc,
					 xmlNsPtr              ns,
					 XmlSheetObjectReadFn  read_fn,
					 XmlSheetObjectWriteFn write_fn,
					 gpointer              user_data);
void             xml_parse_ctx_destroy  (XmlParseContext      *ctxt);
					
xmlNodePtr       xml_workbook_write     (XmlParseContext *ctx,
					 Workbook        *wb);
gboolean         xml_workbook_read      (Workbook        *wb,
					 XmlParseContext *ctx,
					 xmlNodePtr       tree);

int        gnumeric_xml_write_selection_clipboard (CommandContext *context, Sheet *sheet,
						   xmlChar **buffer, int *size);
int        gnumeric_xml_read_selection_clipboard  (CommandContext *context, CellRegion **cr,
						   xmlChar *buffer);
/*
 * Exported support functions
 */
xmlNodePtr   xml_search_child      (xmlNodePtr node, const char *name);

String *     xml_get_value_string  (xmlNodePtr node, const char *name);
void         xml_set_value_string  (xmlNodePtr node, const char *name, const String *val);
gboolean     xml_get_value_int     (xmlNodePtr node, const char *name, int *val);
void         xml_set_value_int     (xmlNodePtr node, const char *name, int val);

xmlNodePtr   xml_write_style       (XmlParseContext *ctxt, MStyle *style);
MStyle      *xml_read_style        (XmlParseContext *ctxt, xmlNodePtr tree);

void      xml_init (void);

#endif /* GNUMERIC_XML_IO_H */
